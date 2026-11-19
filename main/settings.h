#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Machine settings kept in NVS (namespace "oric") */
typedef struct {
    uint8_t model;          /* 0: Oric Atmos (BASIC 1.1), 1: Oric-1 (BASIC 1.0) */
} settings_t;

extern settings_t settings;

void settings_load(void);
void settings_save(void);
