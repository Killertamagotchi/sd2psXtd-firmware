#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "pico/platform.h"

#include "config.h"
#include "psx_spi.pio.h"
#include "debug.h"

uint64_t us_startup;

int byte_count;
volatile int reset;
int ignore;
uint8_t flag;

typedef struct {
    uint32_t offset;
    uint32_t sm;
} pio_t;

pio_t cmd_reader, dat_writer;
extern char tonyhax[];

static void __time_critical_func(reset_pio)(void) {
    // debug_printf("!!\n");

    pio_set_sm_mask_enabled(pio0, (1 << cmd_reader.sm) | (1 << dat_writer.sm), false);
    pio_restart_sm_mask(pio0, (1 << cmd_reader.sm) | (1 << dat_writer.sm));

    pio_sm_exec(pio0, cmd_reader.sm, pio_encode_jmp(cmd_reader.offset));
    pio_sm_exec(pio0, dat_writer.sm, pio_encode_jmp(dat_writer.offset));

    pio_sm_clear_fifos(pio0, cmd_reader.sm);
    pio_sm_drain_tx_fifo(pio0, dat_writer.sm);

    pio_enable_sm_mask_in_sync(pio0, (1 << cmd_reader.sm) | (1 << dat_writer.sm));

    reset = 1;
}

static void __time_critical_func(init_pio)(void) {
    /* Set all pins as floating inputs */
    gpio_set_dir(PIN_PSX_ACK, 0);
    gpio_set_dir(PIN_PSX_SEL, 0);
    gpio_set_dir(PIN_PSX_CLK, 0);
    gpio_set_dir(PIN_PSX_CMD, 0);
    gpio_set_dir(PIN_PSX_DAT, 0);
    gpio_disable_pulls(PIN_PSX_ACK);
    gpio_disable_pulls(PIN_PSX_SEL);
    gpio_disable_pulls(PIN_PSX_CLK);
    gpio_disable_pulls(PIN_PSX_CMD);
    gpio_disable_pulls(PIN_PSX_DAT);

    cmd_reader.offset = pio_add_program(pio0, &cmd_reader_program);
    cmd_reader.sm = pio_claim_unused_sm(pio0, true);

    dat_writer.offset = pio_add_program(pio0, &dat_writer_program);
    dat_writer.sm = pio_claim_unused_sm(pio0, true);

    cmd_reader_program_init(pio0, cmd_reader.sm, cmd_reader.offset);
    dat_writer_program_init(pio0, dat_writer.sm, dat_writer.offset);
}

static void __time_critical_func(card_deselected)(uint32_t gpio, uint32_t event_mask) {
    if (gpio == PIN_PSX_SEL && (event_mask & GPIO_IRQ_EDGE_RISE))
        reset_pio();
}

static uint8_t __time_critical_func(recv_cmd)(void) {
    return (uint8_t) (pio_sm_get_blocking(pio0, cmd_reader.sm) >> 24);
}

static int __time_critical_func(mc_do_state)(uint8_t ch) {
    static uint8_t payload[256];
    if (byte_count >= sizeof(payload))
        return -1;
    payload[byte_count++] = ch;

    if (byte_count == 1) {
        /* First byte - determine the device the command is for */
        if (ch == 0x81)
            return flag;
    } else if (payload[0] == 0x81) {
        /* Command for the memory card */
        uint8_t cmd = payload[1];

        if (cmd == 'S') {
            /* Memory card status */
            switch (byte_count) {
                case 2: return 0x5A;
                case 3: return 0x5D;
                case 4: return 0x5C;
                case 5: return 0x5D;
                case 6: return 0x04;
                case 7: return 0x00;
                case 8: return 0x00;
                case 9: return 0x80;
            }
        } else if (cmd == 'R') {
            /* Memory card read */
            #define MSB (payload[4])
            #define LSB (payload[5])
            #define OFF ((MSB * 256 + LSB) * 128 + byte_count - 10)

            static uint8_t chk;

            switch (byte_count) {
                case 2: return 0x5A;
                case 3: return 0x5D;
                case 4: return 0x00;
                case 5: return MSB;
                case 6: return 0x5C;
                case 7: return 0x5D;
                case 8: return MSB;
                case 9: chk = MSB ^ LSB; return LSB;
                case 10 ... 137: chk ^= tonyhax[OFF]; return tonyhax[OFF]; // TODO: data
                case 138: return chk;
                case 139: return 0x47;
            }

            #undef MSB
            #undef LSB
            #undef OFF
        } else if (cmd == 'W') {
            /* Memory card write */
            #define MSB (payload[4])
            #define LSB (payload[5])
            #define OFF ((MSB * 256 + LSB) * 128 + byte_count - 6)

            switch (byte_count) {
                case 2: flag = 0; return 0x5A;
                case 3: return 0x5D;
                case 4: return 0x00;
                case 5: return MSB;
                case 6 ... 133: tonyhax[OFF] = payload[byte_count - 1]; return payload[byte_count - 1];
                case 134: return payload[byte_count - 1];
                case 135: return 0x5C;
                case 136: return 0x5D;
                case 137: return 0x47;
            }

            #undef MSB
            #undef LSB
            #undef OFF
        }
    }

    return -1;
}

static void __time_critical_func(mc_respond)(uint8_t ch) {
    pio_sm_put_blocking(pio0, dat_writer.sm, ~ch & 0xFF);
}

void __time_critical_func(mc_main_loop)(void) {
    flag = 8;

    while (1) {
        uint8_t ch = recv_cmd();
        /* If this ch belongs to the next command sequence */
        if (reset)
            reset = ignore = byte_count = 0;

        /* If the command sequence is not to be processed (e.g. controller command or unknown) */
        if (ignore)
            continue;

        /* Process by the state machine */
        int next = mc_do_state(ch);

        /* Respond on next byte transfer or stop responding to this sequence altogether if it's not for us */
        if (next == -1)
            ignore = 1;
        else {
            // debug_printf("R %02X -> %02X\n", ch, next);
            mc_respond(next);
        }
    }
}

void memory_card_main(void) {
    init_pio();

    us_startup = time_us_64();
    debug_printf("Secondary core!\n");

    gpio_set_irq_enabled_with_callback(PIN_PSX_SEL, GPIO_IRQ_EDGE_RISE, 1, card_deselected);

    mc_main_loop();
}
