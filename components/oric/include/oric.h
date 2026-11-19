#pragma once

/*
 * Oric Atmos (and Oric-1): a 6502A at 1 MHz, 64 KB of DRAM (48 KB seen,
 * 16 KB under the ROM), the ULA (HCS10017: the picture, address decoding),
 * a 6522 VIA (the AY's bus, the keyboard rows, the tape, the 100 Hz
 * interrupt), an AY-3-8912 (sound; its I/O port selects the keyboard
 * columns) and BASIC 1.1 (1.0 on the Oric-1) in a 16 KB ROM. Platform
 * independent: the board's main.c and the host build (host/) drive it.
 * docs/oric-hardware.md has the hardware and its sources.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ORIC_ROM_SIZE           (16 * 1024)     /* BASIC, $C000-$FFFF */
#define ORIC_DISK_ROM_SIZE      (8 * 1024)      /* the Microdisc's EPROM, $E000-$FFFF */
#define ORIC_RAM_SIZE           (64 * 1024)

/* The picture: 40 cells of 6 pixels, 224 lines (28 text rows of 8) */
#define ORIC_SCREEN_WIDTH       240
#define ORIC_SCREEN_HEIGHT      224
#define ORIC_SCREEN_HZ          50

/* Sound: 16-bit stereo (the Oric's is mono: both sides the same) */
#define ORIC_AUDIO_HZ           31250

typedef enum { ORIC_ATMOS, ORIC_1 } oric_model_t;

typedef struct {
    oric_model_t model;
    const uint8_t *rom;         /* ORIC_ROM_SIZE bytes: BASIC 1.1 (Atmos) or 1.0 (Oric-1) */
    const uint8_t *disk_rom;    /* ORIC_DISK_ROM_SIZE bytes: the Microdisc's EPROM; NULL: no Microdisc */
    uint8_t *ram;               /* ORIC_RAM_SIZE bytes */
} oric_config_t;

bool oric_init(const oric_config_t *config);
void oric_reset(void);                  /* power off and on */
void oric_run_frame(void);
bool oric_halted(void);
void oric_nmi(void);                    /* the button underneath: NMI, BASIC's warm start */

/* ---- Display ---- */
uint32_t oric_screen_signature(void);
void oric_render_line_rgb565(int y, uint16_t *dst);

/* ---- Keyboard: keys by matrix position (row * 8 + column) ----
 *
 * Rows are selected by the VIA's PB0-PB2, columns by the AY's port A (a 0
 * bit drives that column); PB3 reads 1 if a key at a driven column of the
 * row is down (docs 5.1: MAME's oric.cpp, the ROM's key table at $FF78,
 * the service manual's keyboard diagram).
 */
enum {
    ORIC_KEY_7 = 0, ORIC_KEY_N, ORIC_KEY_5, ORIC_KEY_V, ORIC_KEY_NONE_04, ORIC_KEY_1, ORIC_KEY_X, ORIC_KEY_3,
    ORIC_KEY_J, ORIC_KEY_T, ORIC_KEY_R, ORIC_KEY_F, ORIC_KEY_NONE_14, ORIC_KEY_ESC, ORIC_KEY_Q, ORIC_KEY_D,
    ORIC_KEY_M, ORIC_KEY_6, ORIC_KEY_B, ORIC_KEY_4, ORIC_KEY_CTRL, ORIC_KEY_Z, ORIC_KEY_2, ORIC_KEY_C,
    ORIC_KEY_K, ORIC_KEY_9, ORIC_KEY_SEMICOLON, ORIC_KEY_MINUS, ORIC_KEY_NONE_34, ORIC_KEY_NONE_35,
    ORIC_KEY_BACKSLASH, ORIC_KEY_QUOTE,
    ORIC_KEY_SPACE, ORIC_KEY_COMMA, ORIC_KEY_PERIOD, ORIC_KEY_UP, ORIC_KEY_LSHIFT, ORIC_KEY_LEFT, ORIC_KEY_DOWN,
    ORIC_KEY_RIGHT,
    ORIC_KEY_U, ORIC_KEY_I, ORIC_KEY_O, ORIC_KEY_P, ORIC_KEY_FUNCT, ORIC_KEY_DEL, ORIC_KEY_RBRACKET,
    ORIC_KEY_LBRACKET,
    ORIC_KEY_Y, ORIC_KEY_H, ORIC_KEY_G, ORIC_KEY_E, ORIC_KEY_NONE_64, ORIC_KEY_A, ORIC_KEY_S, ORIC_KEY_W,
    ORIC_KEY_8, ORIC_KEY_L, ORIC_KEY_0, ORIC_KEY_SLASH, ORIC_KEY_RSHIFT, ORIC_KEY_RETURN, ORIC_KEY_NONE_76,
    ORIC_KEY_EQUALS,
    ORIC_KEYS
};
void oric_key(int key, bool down);      /* from any task */
bool oric_type(const char *text);       /* \n is RETURN; false while still typing */

/* ---- Cassette: TAP files ----
 *
 * The tape plays on CB1 while the motor runs. Fast (the default, BASIC
 * 1.1): CLOAD and CSAVE through the ROM's tape routines at once, a CSAVE
 * onto the tape's end (the file written when the motor stops); a loader of
 * its own gets the tape in real time. Insert, eject, create and rewind not
 * while oric_run_frame() runs; oric_tape_fast() and the status from any
 * task.
 */
bool oric_tape_insert(const char *path);    /* any file: an empty one is a blank tape */
void oric_tape_eject(void);
const char *oric_tape_path(void);           /* NULL when empty */
bool oric_tape_create(const char *path);    /* an empty .TAP */
void oric_tape_rewind(void);
void oric_tape_fast(bool fast);             /* false: loads in real time too */

typedef struct {
    bool inserted, fast;
    bool traps;                 /* the ROM has 1.1's tape routines (fast works) */
    bool motor, playing, slow;  /* the relay; the tape on CB1 (300 baud) */
    uint32_t pos, size;         /* the head's offset in the file, its size */
    int file, files;            /* the file at the head (0 ..; files: past the last) */
    char name[17];              /* ... its name, type, autorun, addresses */
    uint8_t type;
    bool autorun;
    uint16_t start, last;
    uint32_t loaded, saved;     /* files loaded, saved at once */
    bool write_failed;
} oric_tape_status_t;

void oric_tape_status(oric_tape_status_t *s);

/* ---- The Microdisc: MFM_DISK images (.dsk) ----
 *
 * Drives 0-3 (A-D); an image is read whole and written back in place,
 * its changed tracks at oric_disk_flush() (and at eject). A file that
 * can't be written is a write-protected disk. Insert, eject and flush not
 * while oric_run_frame() runs; the status from any task.
 */
#define ORIC_DRIVES 4

bool oric_disk_insert(int drive, const char *path);
void oric_disk_eject(int drive);
const char *oric_disk_path(int drive);     /* NULL when empty */
bool oric_disk_protected(int drive);
bool oric_disk_create(const char *path, int sides, int tracks);   /* unformatted */
bool oric_disk_dirty(void);                 /* tracks changed since the last flush */
void oric_disk_flush(void);

typedef struct {
    bool inserted, protect, dirty;
    int sides, tracks;
    int cyl;                    /* the head */
    bool selected;              /* the control register's drive */
    int side;                   /* ... its side */
    bool motor;                 /* the controller's HLD (all drives' motors) */
    uint32_t reads, writes;     /* sectors */
} oric_disk_status_t;

void oric_disk_status(int drive, oric_disk_status_t *s);

typedef struct {
    bool busy, intrq, motor;
    uint8_t command, status, track, sector, data;
    int drive, side;
    uint32_t sectors_read, sectors_written;
} oric_fdc_status_t;

void oric_fdc_status(oric_fdc_status_t *s);

/* ---- Sound ---- */
void oric_audio_enable(bool on);
int oric_audio_render(int16_t *dst, int frames);
int oric_audio_pending(void);

/* ---- Status ---- */
typedef struct {
    uint32_t frames;
    uint16_t pc;
    uint8_t a, x, y, s, p;
    oric_model_t model;
    bool hires;                 /* the ULA's mode register: hires */
    bool hz50;                  /* ... 50 Hz (312 lines) or 60 Hz (264) */
    bool caps;                  /* BASIC's CAPS ($020C) */
    bool motor;                 /* the cassette relay (PB6) */
    uint16_t timer1;            /* the VIA's timer 1 latch (the ROM's 10000: 100 Hz) */
    bool timer1_irq;            /* ... its interrupt enabled */
    uint8_t ay[16];             /* the AY's registers */
    bool microdisc;             /* one is attached */
    uint8_t disk_control;       /* its control register */
} oric_status_t;

void oric_get_status(oric_status_t *s);
