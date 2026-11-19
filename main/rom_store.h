#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * The Oric's ROMs: built in, from the oric_roms flash partition
 * (roms/<name> in the project, written by idf.py flash into 64 KB slots).
 * A file of the same name in the SD card's "oric/roms" folder overrides
 * one.
 */
typedef enum { ROM_BASIC11, ROM_BASIC10, ROM_MICRODISC, ROM_COUNT } rom_id_t;

/* At power-on, from a task whose stack is in internal RAM (reserve_memory()) */
void rom_store_init(void);

const char *rom_store_name(rom_id_t id);    /* "basic11b.rom" */
uint32_t rom_store_size(rom_id_t id);       /* 16 KB, the Microdisc's 8 */

/* The ROM into buf (rom_store_size() bytes); false if there's none */
bool rom_store_load(rom_id_t id, uint8_t *buf);
