#include "ps2_cardman.h"

#include "ps2_exploit.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sd.h"
#include "debug.h"
#include "ps2_psram.h"
#include "settings.h"

#include "hardware/timer.h"


#define PS2_DEFAULT_CARD_SIZE   PS2_CARD_SIZE_8M
#define BLOCK_SIZE (512)

static uint8_t flushbuf[BLOCK_SIZE];
static int fd = -1;

#define IDX_MIN 1
#define IDX_BOOT 0
#define CHAN_MIN 1
#define CHAN_MAX 8

#define MAX_GAME_NAME_LENGTH    (127)
#define MAX_PREFIX_LENGTH       (4)
#define MAX_GAME_ID_LENGTH      (16)

static int card_idx;
static int card_chan;
static uint32_t card_size;
static cardman_cb_t cardman_cb;
static char folder_name[MAX_GAME_ID_LENGTH];
static uint64_t cardprog_start;
static size_t cardprog_pos;
static int cardprog_wr;

void ps2_cardman_init(void) {
    if (settings_get_ps2_autoboot()) {
        card_idx = IDX_BOOT;
        card_chan = CHAN_MIN;
        snprintf(folder_name, sizeof(folder_name), "BOOT");
    } else {
        card_idx = settings_get_ps2_card();
        card_chan = settings_get_ps2_channel();
        snprintf(folder_name, sizeof(folder_name), "Card%d", card_idx);
    }
}

int ps2_cardman_write_sector(int sector, void *buf512) {
    if (fd < 0)
        return -1;

    if (sd_seek(fd, sector * BLOCK_SIZE) != 0)
        return -1;

    if (sd_write(fd, buf512, BLOCK_SIZE) != BLOCK_SIZE)
        return -1;

    return 0;
}

void ps2_cardman_flush(void) {
    if (fd >= 0)
        sd_flush(fd);
}

static void ensuredirs(void) {
    char cardpath[32];
    
    snprintf(cardpath, sizeof(cardpath), "MemoryCards/PS2/%s", folder_name);

    sd_mkdir("MemoryCards");
    sd_mkdir("MemoryCards/PS2");
    sd_mkdir(cardpath);

    if (!sd_exists("MemoryCards") || !sd_exists("MemoryCards/PS2") || !sd_exists(cardpath))
        fatal("error creating directories");
}

static const uint8_t block0[384] = {
    0x53, 0x6F, 0x6E, 0x79, 0x20, 0x50, 0x53, 0x32, 0x20, 0x4D, 0x65, 0x6D, 0x6F, 0x72, 0x79, 0x20,
    0x43, 0x61, 0x72, 0x64, 0x20, 0x46, 0x6F, 0x72, 0x6D, 0x61, 0x74, 0x20, 0x31, 0x2E, 0x32, 0x2E,
    0x30, 0x2E, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x10, 0x00, 0x00, 0xFF,
    0x00, 0x20, 0x00, 0x00, 0x29, 0x00, 0x00, 0x00, 0xC7, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x03, 0x00, 0x00, 0xFE, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x02, 0x2B, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x41, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF
};

static const uint8_t block2000[128] = {
    0x09, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00,
    0x0D, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x11, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00,
    0x15, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x19, 0x00, 0x00, 0x00, 0x1A, 0x00, 0x00, 0x00, 0x1B, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00,
    0x1D, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x21, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00,
    0x25, 0x00, 0x00, 0x00, 0x26, 0x00, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00
};

static const uint8_t blockA400[512] = {
    0x27, 0x84, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x31, 0x0C, 0x18, 0x0A, 0xE6, 0x07,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x31, 0x0C, 0x18, 0x0A, 0xE6, 0x07,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x2E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t blockA600[512] = {
    0x26, 0xA4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x31, 0x0C, 0x18, 0x0A, 0xE6, 0x07,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x31, 0x0C, 0x18, 0x0A, 0xE6, 0x07,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x2E, 0x2E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


static void genblock(size_t pos, void *vbuf) {
    uint8_t *buf = vbuf;

    memset(buf, 0xFF, BLOCK_SIZE);

    if (pos == 0) {
        memcpy(buf, block0, sizeof(block0));
    } else if (pos == 0x2000) {
        memcpy(buf, block2000, sizeof(block2000));
    } else if (pos >= 0x2400 && pos < 0xA400) {
        for (size_t i = 0; i < BLOCK_SIZE/4; ++i) {
            uint32_t val = 0x7FFFFFFF;
            memcpy(&buf[i * 4], &val, sizeof(val));
        }
        if (pos == 0x2400) {
            buf[0] = buf[1] = buf[2] = buf[3] = 0xFF;
        }
        if (pos == 0xA200) {
            memset(buf + 0x11C, 0xFF, BLOCK_SIZE - 0x11C);
        }
    } else if (pos == 0xA400) {
        memcpy(buf, blockA400, sizeof(blockA400));
    } else if (pos == 0xA600) {
        memcpy(buf, blockA600, sizeof(blockA600));
    }
}

void ps2_cardman_open(void) {
    char path[64];

    ensuredirs();

    if (IDX_BOOT == card_idx)
        snprintf(path, sizeof(path), "MemoryCards/PS2/%s/BootCard.mcd", folder_name);
    else
    {
        snprintf(path, sizeof(path), "MemoryCards/PS2/%s/%s-%d.mcd", folder_name, folder_name, card_chan);
        /* this is ok to do on every boot because it wouldn't update if the value is the same as currently stored */
        settings_set_ps2_card(card_idx);
        settings_set_ps2_channel(card_chan);
    }

    printf("Switching to card path = %s\n", path);


    if (!sd_exists(path)) {
        cardprog_wr = 1;
        fd = sd_open(path, O_RDWR | O_CREAT | O_TRUNC);

        if (fd < 0)
            fatal("cannot open for creating new card");

        printf("create new image at %s... ", path);
        cardprog_start = time_us_64();

        for (size_t pos = 0; pos < PS2_DEFAULT_CARD_SIZE; pos += BLOCK_SIZE) {
            if (PS2_DEFAULT_CARD_SIZE == PS2_CARD_SIZE_8M)
                genblock(pos, flushbuf);
            else
                memset(flushbuf, 0xFF, sizeof(flushbuf)/sizeof(flushbuf[0]));
            if (sd_write(fd, flushbuf, BLOCK_SIZE) != BLOCK_SIZE)
                fatal("cannot init memcard");
            psram_write(pos, flushbuf, BLOCK_SIZE);
            cardprog_pos = pos;

            if (cardman_cb)
                cardman_cb(100 * pos / PS2_DEFAULT_CARD_SIZE);
        }
        sd_flush(fd);

        uint64_t end = time_us_64();
        printf("OK!\n");

        printf("took = %.2f s; SD write speed = %.2f kB/s\n", (end - cardprog_start) / 1e6,
            1000000.0 * PS2_DEFAULT_CARD_SIZE / (end - cardprog_start) / 1024);
    } else {
        cardprog_wr = 0;
        fd = sd_open(path, O_RDWR);

        if (fd < 0)
            fatal("cannot open card");

        card_size = sd_filesize(fd);
        if ((card_size != PS2_CARD_SIZE_512K)
            && (card_size != PS2_CARD_SIZE_1M)
            && (card_size != PS2_CARD_SIZE_2M)
            && (card_size != PS2_CARD_SIZE_4M)
            && (card_size != PS2_CARD_SIZE_8M))
            fatal("Card %d Channel %d is corrupted", card_idx, card_chan);

        /* read 8 megs of card image */
        printf("reading card (%lu KB).... ", (uint32_t)(card_size / 1024));
        cardprog_start = time_us_64();
        for (size_t pos = 0; pos < card_size; pos += BLOCK_SIZE) {
            if (sd_read(fd, flushbuf, BLOCK_SIZE) != BLOCK_SIZE)
                fatal("cannot read memcard");
            psram_write(pos, flushbuf, BLOCK_SIZE);
            cardprog_pos = pos;

            if (cardman_cb)
                cardman_cb(100 * pos / card_size);
        }
        uint64_t end = time_us_64();
        printf("OK!\n");

        printf("took = %.2f s; SD read speed = %.2f kB/s\n", (end - cardprog_start) / 1e6,
            1000000.0 * card_size / (end - cardprog_start) / 1024);
    }
}

void ps2_cardman_close(void) {
    if (fd < 0)
        return;
    ps2_cardman_flush();
    sd_close(fd);
    fd = -1;
}

void ps2_cardman_set_channel(uint16_t chan_num) {
    if (card_idx != IDX_BOOT) {
        if (chan_num <= CHAN_MAX && chan_num >= CHAN_MIN) {
            card_chan = chan_num;
        }
    } else {
        card_idx = settings_get_ps2_card();
        card_chan = settings_get_ps2_channel();
    }
    snprintf(folder_name, sizeof(folder_name), "Card%d", card_idx);
}

void ps2_cardman_next_channel(void) {
    if (card_idx != IDX_BOOT) {
        card_chan += 1;
        if (card_chan > CHAN_MAX)
            card_chan = CHAN_MIN;
    } else {
        card_idx = settings_get_ps2_card();
        card_chan = settings_get_ps2_channel();
    }
    snprintf(folder_name, sizeof(folder_name), "Card%d", card_idx);
}

void ps2_cardman_prev_channel(void) {
    if (card_idx != IDX_BOOT) {
        card_chan -= 1;
        if (card_chan < CHAN_MIN)
            card_chan = CHAN_MAX;
    } else {
        card_idx = settings_get_ps2_card();
        card_chan = settings_get_ps2_channel();
    }
    snprintf(folder_name, sizeof(folder_name), "Card%d", card_idx);
}

void ps2_cardman_set_idx(uint16_t idx_num) {
    if (card_idx != IDX_BOOT) {
        if (idx_num >= IDX_MIN && idx_num <= 0xFFFF) {
            card_idx = idx_num;
            card_chan = CHAN_MIN;
        }
    } else {
        card_idx = settings_get_ps2_card();
        card_chan = settings_get_ps2_channel();
    }
    snprintf(folder_name, sizeof(folder_name), "Card%d", card_idx);

}

void ps2_cardman_next_idx(void) {
    if (card_idx != IDX_BOOT) {
        card_idx += 1;
        card_chan = CHAN_MIN;
    } else {
        card_idx = settings_get_ps2_card();
        card_chan = settings_get_ps2_channel();
    }
    snprintf(folder_name, sizeof(folder_name), "Card%d", card_idx);
}

void ps2_cardman_prev_idx(void) {
    if (card_idx != IDX_BOOT) {
        int minIndex = (settings_get_ps2_autoboot() ? IDX_BOOT : IDX_MIN);
        card_idx -= 1;
        card_chan = CHAN_MIN;
        if (card_idx < minIndex)
            card_idx = minIndex;
    } else {
        card_idx = settings_get_ps2_card();
        card_chan = settings_get_ps2_channel();
    }
    if (card_idx != IDX_BOOT)
        snprintf(folder_name, sizeof(folder_name), "Card%d", card_idx);
    else
        snprintf(folder_name, sizeof(folder_name), "BOOT");

}

int ps2_cardman_get_idx(void) {
    return card_idx;
}

int ps2_cardman_get_channel(void) {
    return card_chan;
}

void ps2_cardman_set_progress_cb(cardman_cb_t func) {
    cardman_cb = func;
}

char *ps2_cardman_get_progress_text(void) {
    static char progress[32];

    snprintf(progress, sizeof(progress), "%s %.2f kB/s", cardprog_wr ? "Wr" : "Rd",
        1000000.0 * cardprog_pos / (time_us_64() - cardprog_start) / 1024);

    return progress;
}

uint32_t ps2_cardman_get_card_size(void) {
    return card_size;
}

void ps2_cardman_set_gameid(const char* game_id) {
    strlcpy(folder_name, game_id, MAX_GAME_ID_LENGTH);
}

const char* ps2_cardman_get_folder_name(void) {
    return folder_name;
}
