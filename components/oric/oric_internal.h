#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "oric.h"
#include "m6502.h"
#include "via.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#define ORIC_LOGI(...)  ESP_LOGI("oric", __VA_ARGS__)
#define ORIC_LOGW(...)  ESP_LOGW("oric", __VA_ARGS__)
/* Buffers the CPU doesn't run in: PSRAM (the MaTouch's internal RAM is
 * short: esp32ql's lessons)
 */
#define ORIC_SLOW_ALLOC(n) heap_caps_calloc(1, (n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define ORIC_HOT        IRAM_ATTR
#else
#include <stdio.h>
#include <stdlib.h>
#define ORIC_LOGI(...)  do { printf("oric: " __VA_ARGS__); printf("\n"); } while (0)
#define ORIC_LOGW(...)  do { printf("oric: warning: " __VA_ARGS__); printf("\n"); } while (0)
#define ORIC_SLOW_ALLOC(n) calloc(1, (n))
#define ORIC_HOT
#endif

/* ---- Time: 1 MHz cycles (the 6502 core's) ----
 *
 * A line is 64 us; a frame 312 lines at 50 Hz, 264 at 60 Hz (the ULA's
 * mode register, docs 4.4). Line 0 is the picture's first: lines 0-223
 * are the picture, the rest black (vertical sync at 256-259, 236-239).
 */
#define ORIC_LINE_CYCLES    64
#define ORIC_PICTURE_LINES  224
#define ORIC_LINES_50HZ     312
#define ORIC_LINES_60HZ     264

/* The ULA's mode register (a $18-$1F attribute's low bits) */
enum { MODE_50HZ = 0x02, MODE_HIRES = 0x04 };

typedef struct {
    oric_config_t cfg;
    m6502_t cpu;
    via_t via;
    uint8_t *ram;
    const uint8_t *rom;

    uint64_t frame_start;               /* the cycle line 0 began at */
    int line;                           /* the line being run */
    int lines;                          /* this frame's: 312 or 264 */
    uint32_t frames;

    uint8_t mode;                       /* the ULA's mode register: persists across lines and frames */

    /* Something the picture shows may have changed (a new value written
     * to $9800-$BFFF: the screens and character sets): video.c decodes the
     * frame's lines again for the next two frames (a write after its line
     * was decoded shows in the next)
     */
    uint8_t screen_touched;

    /* The VIA's outputs as last seen */
    uint8_t port_a, port_b;
} oric_t;

extern oric_t oric;

static inline uint64_t oric_line_start(void)
{
    return oric.frame_start + (uint64_t)oric.line * ORIC_LINE_CYCLES;
}

/* video.c */
bool oric_video_init(void);
void oric_video_reset(void);
void oric_video_frame_start(void);
void oric_video_line(int line);         /* decode a picture line (0-223) */
void oric_video_frame_end(void);

/* psg.c: the AY-3-8912 */
void psg_init(void);
void psg_reset(void);
void psg_bus(bool bdir, bool bc1, uint8_t data);
uint8_t psg_read(void);
uint8_t psg_port_a(void);               /* the I/O port's pins: the keyboard's column select */
void psg_frame_end(void);
void psg_get_regs(uint8_t regs[16]);

/* keyboard.c */
bool oric_keyboard_sense(uint8_t row, uint8_t columns);   /* PB3 */
void oric_keyboard_frame(void);
