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

/* Through a mapping, not esp_partition_read(): that turns the caches off
 * while it reads, which stalls the LCD's refill (picture artefacts). (A
 * first mapping of a region turns them off too, on the P4 at least: start()
 * must run on a task whose stack is in internal RAM, as esp32emu's are.)
 */
static bool from_partition(rom_id_t id, uint8_t *buf)
{
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ROM_PARTITION_TYPE,
                                                           "oric_roms");
    uint32_t offset = id * SLOT, size = roms[id].size;
    if (part == NULL || offset + size > part->size) {
        return false;
    }
    const void *map;
    esp_partition_mmap_handle_t handle;
    if (esp_partition_mmap(part, offset, size, ESP_PARTITION_MMAP_DATA, &map, &handle) != ESP_OK) {
        return false;
    }
    memcpy(buf, map, size);
    esp_partition_munmap(handle);
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
