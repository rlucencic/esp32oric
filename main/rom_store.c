#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "emu/sdcard.h"
#include "rom_store.h"

#define ROM_PARTITION_TYPE  0x40
#define ROM_FOLDER          SDCARD_MOUNT_POINT "/oric/roms"
#define SLOT                65536

static const char *TAG = "rom";

static const struct {
    const char *name;
    uint32_t size;
    const char *banner;                 /* text the ROM holds */
} roms[ROM_COUNT] = {
    [ROM_BASIC11] = { "basic11b.rom", 16 * 1024, "ORIC EXTENDED BASIC V1.1" },
    [ROM_BASIC10] = { "basic10.rom", 16 * 1024, "ORIC EXTENDED BASIC V1.0" },
    [ROM_MICRODISC] = { "microdis.rom", 8 * 1024, "ORIC EXTENDED BASIC" },   /* (its copies of the banners) */
};

const char *rom_store_name(rom_id_t id)
{
    return id < ROM_COUNT ? roms[id].name : "";
}

uint32_t rom_store_size(rom_id_t id)
{
    return id < ROM_COUNT ? roms[id].size : 0;
}

/* Its banner somewhere, its reset vector (the image's last bytes but two)
 * inside it: $C000 up for BASIC, $E000 up for the Microdisc's EPROM
 */
static bool plausible(rom_id_t id, const uint8_t *p)
{
    uint32_t size = roms[id].size;
    uint16_t reset = p[size - 4] | (p[size - 3] << 8);
    if (reset < 0x10000 - size) {
        return false;
    }
    size_t n = strlen(roms[id].banner);
    for (uint32_t i = 0; i + n <= size; i++) {
        if (p[i] == 'O' && memcmp(p + i, roms[id].banner, n) == 0) {
            return true;
        }
    }
    return false;
}

/* The partition, mapped once at power-on (rom_store_init(), from the main
 * task) and kept: reads through the mapping never touch the caches, but
 * making a mapping does (on the P4 it turns them off), and that asserts on
 * a task whose stack is in PSRAM: the setup screen's Restart runs start()
 * on the supervisor's (esp32emu), and the Oric-1's first mapping of slot 1
 * there rebooted the Tab5. Not esp_partition_read() either: it turns the
 * caches off while it reads (the LCD's refill stalls).
 */
static const uint8_t *s_map;
static uint32_t s_map_size;

void rom_store_init(void)
{
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ROM_PARTITION_TYPE,
                                                           "oric_roms");
    const void *map;
    esp_partition_mmap_handle_t handle;
    if (part == NULL || esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &map, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "no oric_roms partition: ROMs from the SD card only");
        return;
    }
    s_map = map;
    s_map_size = part->size;
}

static bool from_partition(rom_id_t id, uint8_t *buf)
{
    uint32_t offset = id * SLOT, size = roms[id].size;
    if (s_map == NULL || offset + size > s_map_size) {
        return false;
    }
    memcpy(buf, s_map + offset, size);
    return true;
}

/* oric/roms/<name> on the SD card (any case) */
static bool from_sd(rom_id_t id, uint8_t *buf)
{
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", ROM_FOLDER, roms[id].name);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        for (char *p = path + strlen(ROM_FOLDER) + 1; *p; p++) {
            *p = (*p >= 'a' && *p <= 'z') ? *p - 32 : *p;
        }
        f = fopen(path, "rb");
    }
    if (f == NULL) {
        return false;
    }
    size_t n = fread(buf, 1, roms[id].size, f);
    fclose(f);
    if (n != roms[id].size) {
        return false;
    }
    ESP_LOGI(TAG, "%s from the SD card", roms[id].name);
    return true;
}

bool rom_store_load(rom_id_t id, uint8_t *buf)
{
    if (id >= ROM_COUNT) {
        return false;
    }
    if (from_sd(id, buf) && plausible(id, buf)) {
        return true;                    /* the card's overrides the built-in one */
    }
    return from_partition(id, buf) && plausible(id, buf);
}
