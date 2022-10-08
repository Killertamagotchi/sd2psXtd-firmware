#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

#include "memory_card.h"
#include "gui.h"
#include "input.h"
#include "config.h"

/* reboot to bootloader if either button is held on startup
   to make the device easier to flash when assembled inside case */
static void check_bootloader_reset(void) {
    /* make sure at least DEBOUNCE interval passes or we won't get inputs */
    for (int i = 0; i < 2 * DEBOUNCE_MS; ++i) {
        input_task();
        sleep_ms(1);
    }

    if (input_is_down(0) || input_is_down(1))
        reset_usb_boot(0, 0);
}

static void debug_task(void) {
    for (int i = 0; i < 10; ++i) {
        char ch = debug_get();
        if (ch) {
            if (ch == '\n')
                uart_putc_raw(UART_PERIPH, '\r');
            uart_putc_raw(UART_PERIPH, ch);
        } else {
            break;
        }
    }
}

int main() {
    stdio_uart_init_full(UART_PERIPH, UART_BAUD, UART_TX, UART_RX);

    // printf("prepare...\n");
    // set_sys_clock_khz(270000, true);
    // sleep_ms(50);
    // stdio_uart_init_full(UART_PERIPH, UART_BAUD, UART_TX, UART_RX);
    // sleep_ms(50);

    printf("\n\n\nStarted! Clock %d\n", clock_get_hz(clk_sys));

    input_init();
    check_bootloader_reset();

    // psram_init();
    // sd_init();
    gui_init();

    multicore_launch_core1(memory_card_main);

    while (1) {
        debug_task();
        gui_task();
        input_task();
    }
}
