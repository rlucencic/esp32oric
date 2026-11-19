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
#define ORIC_SLOW_REALLOC(p, n) heap_caps_realloc((p), (n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define ORIC_HOT        IRAM_ATTR
#else
#include <stdio.h>
#include <stdlib.h>
#define ORIC_LOGI(...)  do { printf("oric: " __VA_ARGS__); printf("\n"); } while (0)
#define ORIC_LOGW(...)  do { printf("oric: warning: " __VA_ARGS__); printf("\n"); } while (0)
#define ORIC_SLOW_ALLOC(n) calloc(1, (n))
#define ORIC_SLOW_REALLOC(p, n) realloc((p), (n))
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

/* The 6502's IRQ line: a bit a source */
enum { IRQ_VIA = 0x01, IRQ_FDC = 0x02 };

typedef struct {
    oric_config_t cfg;
    m6502_t cpu;
    via_t via;
    uint8_t *ram;
    const uint8_t *rom;
    const uint8_t *disk_rom;            /* the Microdisc's EPROM (NULL: no Microdisc) */
    bool basic_in;                      /* $C000-$FFFF reads BASIC (the tape's traps) */

    uint64_t frame_start;               /* the cycle line 0 began at */
    int line;                           /* the line being run */
    int lines;                          /* the last frame's: 312 or 264 */
    uint32_t frames;

    uint8_t mode;                       /* the ULA's mode register: persists across lines and frames */

    /* Something the picture shows may have changed (a new value written
     * to $9800-$BFFF: the screens and character sets): video.c decodes the
     * frame's lines again for the next two frames (a write after its line
     * was decoded shows in the next)
     */
    uint8_t screen_touched;

    uint8_t port_b;                     /* the VIA's port B pins as last seen (PB4 strobe) */
    bool motor;                         /* PB6 an output and high: the cassette relay */
    volatile bool nmi_request;          /* the button underneath, from another task */
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
void oric_video_line(int line);         /* decode a picture line (0-223) as it starts */
void oric_video_frame_end(void);

/* psg.c: the AY-3-8912 */
void psg_reset(void);
void psg_bus(bool bdir, bool bc1, uint8_t data);
uint8_t psg_read(void);
uint8_t psg_port_a(void);               /* the I/O port's pins: the keyboard's column select */
void psg_line(void);                    /* a line's sound: two samples */
void psg_get_regs(uint8_t regs[16]);

/* keyboard.c */
bool oric_keyboard_sense(uint8_t row, uint8_t columns);   /* PB3 */
void oric_keyboard_reset(void);
void oric_keyboard_frame(void);

/* oric.c: a write as the 6502 makes it (the screens' flag, page 3, the ROM);
 * the memory map again (the Microdisc's paging); the page writes are lost in
 */
void oric_write(uint16_t a, uint8_t v);
void oric_map_memory(void);
void oric_map_high(void);               /* $C000-$FFFF only */
uint8_t *oric_sink(void);

/* microdisc.c: the Microdisc's page 3 ($0310-$031B), paging, IRQ */
bool microdisc_claims(uint16_t a);
uint8_t microdisc_read(uint16_t a);
void microdisc_write(uint16_t a, uint8_t v);
bool microdisc_map(void);               /* false: BASIC at $C000-$FFFF */
void microdisc_irq(void);               /* INTRQ or the enable changed */
uint8_t microdisc_control(void);
void microdisc_reset(void);

/* wd1793.c: the floppy controller and its drives */
extern uint64_t fdc_event_at;           /* the controller's next step (UINT64_MAX: none) */
void fdc_update(uint64_t now);
uint8_t fdc_read(int reg);
void fdc_write(int reg, uint8_t v);
bool fdc_intrq(void);
bool fdc_drq(void);
void fdc_select(int drive, int side);
void fdc_frame_end(void);
void fdc_reset(void);

/* tape.c: the cassette */
extern uint64_t tape_edge_at;           /* the tape's next edge on CB1 (UINT64_MAX: none) */
void tape_advance(uint64_t now);        /* the edges up to now */
static inline void tape_sync(uint64_t now)
{
    if (now >= tape_edge_at) {
        tape_advance(now);
    }
}
void tape_map(void);                    /* the ROM pages with the traps (after the memory map) */
uint8_t tape_rom_read(uint16_t a);      /* a read in those pages */
bool tape_trap(void);                   /* after a run: a routine the CPU entered, done */
void tape_motor(bool on);
void tape_poll(void);                   /* the 6502 read IFR */
void tape_frame_end(void);
void tape_reset(void);
void tape_nmi(void);
