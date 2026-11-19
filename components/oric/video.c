/*
 * The ULA's picture (docs/oric-hardware.md 4.5-4.10, the die-derived RTL's
 * behaviour): 224 lines of 40 cells of 6 pixels. Each screen byte is
 * either an attribute (bits 6-5 00: bits 4-3 pick ink, style, paper or
 * mode, bits 2-0 its value; its own cell shows paper) or data (text: a
 * character code, its pattern from the character set; hires: 6 pixels);
 * bit 7 inverts the cell. Ink 7, paper 0 and style 0 at every line's
 * start; the mode register (text/hires, 50/60 Hz) lasts until the next
 * mode attribute, a switch taking effect at the next cell. Lines 200-223
 * are text whatever the mode (their character set still follows it).
 * Flashing: the ink shows as paper for 16 of every 32 frames, on hires
 * lines too (the RTL's blink_pixel ignores the mode).
 *
 * A line is decoded as it starts into one of three frames of cells (the
 * pattern, ink, paper, inverse, flash: 16 bits a cell); a finished frame
 * that differs from the last one shown becomes the one the presenter
 * (another task) draws. Decoding is skipped for a frame that would come
 * out the same: nothing written to $9800-$BFFF since the last decoded one
 * (oric.screen_touched) and the same mode register at its start (the mode
 * it leaves is then the one recorded).
 */
#include <string.h>
#include "oric_internal.h"

#define FRAMES          3
#define CELLS           40

/* A cell: pattern bits 0-5 (bit 5 the leftmost pixel), ink 6-8, paper
 * 9-11, inverse 12, flash 13
 */
enum { CELL_INVERSE = 0x1000, CELL_FLASH = 0x2000 };

typedef struct {
    uint16_t cells[ORIC_PICTURE_LINES][CELLS];
    bool any_flash;
    uint8_t styles;                     /* the styles its lines used: 1 alternate, 2 double height, 4 flash */
} frame_t;

static frame_t *s_frames;
static int s_writing = 1;
static volatile int s_latest;
static volatile uint32_t s_signature;

static bool s_decoding;                 /* this frame's lines are decoded */
static bool s_valid;                    /* a frame was decoded: */
static uint8_t s_start_mode, s_end_mode;        /* ... its mode register at its start, after line 223 */
static uint8_t s_fcnt;                  /* the ULA's 5-bit frame counter (1 at reset, the RTL) */
static volatile bool s_flash_off;       /* its top bit: flashing ink hidden */

bool oric_video_init(void)
{
    if (s_frames == NULL) {
        s_frames = ORIC_SLOW_ALLOC(FRAMES * sizeof(frame_t));
    }
    return s_frames != NULL;
}

void oric_video_reset(void)
{
    s_valid = false;
    s_fcnt = 1;
    s_flash_off = false;
}

void oric_video_frame_start(void)
{
    s_decoding = oric.screen_touched || !s_valid || oric.mode != s_start_mode;
    if (s_decoding) {
        s_start_mode = oric.mode;
        s_frames[s_writing].any_flash = false;
        s_frames[s_writing].styles = 0;
    }
}

void oric_video_line(int y)
{
    if (!s_decoding) {
        if (y == ORIC_PICTURE_LINES - 1) {
            oric.mode = s_end_mode;
        }
        return;
    }
    const uint8_t *ram = oric.ram;
    const uint8_t *text = ram + 0xBB80 + CELLS * (y >> 3);
    const uint8_t *hires = ram + 0xA000 + CELLS * y;
    uint16_t *out = s_frames[s_writing].cells[y];
    unsigned mode = oric.mode, ink = 7, paper = 0, style = 0, flash = 0, styles = 0;
    for (int c = 0; c < CELLS; c++) {
        bool hires_cell = (mode & MODE_HIRES) && y < 200;
        uint8_t b = hires_cell ? hires[c] : text[c];
        unsigned pattern;
        if ((b & 0x60) == 0) {
            unsigned v = b & 7;
            switch ((b >> 3) & 3) {
            case 0: ink = v; break;
            case 1: style = v; break;
            case 2: paper = v; break;
            default: mode = v; break;
            }
            pattern = 0;
        } else if (hires_cell) {
            pattern = b & 0x3F;
        } else {
            unsigned base = (mode & MODE_HIRES) ? 0x9800 : 0xB400;
            if (style & 1) {
                base += 0x400;
            }
            unsigned row = (style & 2) ? (y >> 1) & 7 : y & 7;
            pattern = ram[base + (b & 0x7F) * 8 + row] & 0x3F;
        }
        out[c] = pattern | (ink << 6) | (paper << 9) | ((b & 0x80) << 5) | ((style & 4) << 11);
        flash |= style & 4;
        styles |= style;
    }
    oric.mode = mode;
    s_frames[s_writing].styles |= styles;
    if (flash) {
        s_frames[s_writing].any_flash = true;
    }
}

void oric_video_frame_end(void)
{
    if (s_decoding) {
        s_end_mode = oric.mode;
        s_valid = true;
        if (memcmp(&s_frames[s_writing], &s_frames[s_latest], sizeof(frame_t)) != 0) {
            s_latest = s_writing;
            s_signature++;
            s_writing = (s_writing + 1) % FRAMES;
        }
    }
    s_fcnt = (s_fcnt + 1) & 31;
    bool off = (s_fcnt & 16) != 0;
    if (off != s_flash_off) {
        s_flash_off = off;
        if (s_frames[s_latest].any_flash) {
            s_signature++;
        }
    }
    if (oric.screen_touched) {
        oric.screen_touched--;
    }
}

uint32_t oric_screen_signature(void)
{
    return s_signature;
}

/* The latest picture: the colours it shows (a bit each: inverse applied,
 * paper where a cell has a 0 pixel, ink where a 1), its flashing cells
 * and the styles used (any task: a frame that changes meanwhile counts
 * partly)
 */
void oric_screen_usage(oric_screen_usage_t *u)
{
    memset(u, 0, sizeof(*u));
    if (s_frames == NULL) {
        return;
    }
    const frame_t *f = &s_frames[s_latest];
    for (int y = 0; y < ORIC_PICTURE_LINES; y++) {
        for (int c = 0; c < CELLS; c++) {
            unsigned v = f->cells[y][c];
            unsigned ink = (v >> 6) & 7, paper = (v >> 9) & 7, inv = (v & CELL_INVERSE) ? 7 : 0;
            if ((v & 0x3F) != 0x3F) {
                u->colours |= 1u << (paper ^ inv);
            }
            if (v & 0x3F) {
                u->colours |= 1u << (ink ^ inv);
                if (v & CELL_FLASH) {
                    u->flashing++;
                }
            }
        }
    }
    u->flashing /= 8;                   /* (a cell's 8 lines: character cells) */
    u->alternate = (f->styles & 1) != 0;
    u->double_height = (f->styles & 2) != 0;
}

/* ---- Drawing ---- */

/* 3-bit RGB: bit 0 red, 1 green, 2 blue */
static const uint16_t s_rgb565[8] = {
    0x0000, 0xF800, 0x07E0, 0xFFE0, 0x001F, 0xF81F, 0x07FF, 0xFFFF,
};

void oric_render_line_rgb565(int y, uint16_t *dst)
{
    if (s_frames == NULL || y < 0 || y >= ORIC_SCREEN_HEIGHT) {
        return;
    }
    const uint16_t *cells = s_frames[s_latest].cells[y];
    bool off = s_flash_off;
    for (int c = 0; c < CELLS; c++) {
        unsigned v = cells[c];
        unsigned ink = (v >> 6) & 7, paper = (v >> 9) & 7;
        if ((v & CELL_FLASH) && off) {
            ink = paper;
        }
        if (v & CELL_INVERSE) {
            ink ^= 7;
            paper ^= 7;
        }
        uint16_t fg = s_rgb565[ink], bg = s_rgb565[paper];
        dst[0] = (v & 0x20) ? fg : bg;
        dst[1] = (v & 0x10) ? fg : bg;
        dst[2] = (v & 0x08) ? fg : bg;
        dst[3] = (v & 0x04) ? fg : bg;
        dst[4] = (v & 0x02) ? fg : bg;
        dst[5] = (v & 0x01) ? fg : bg;
        dst += 6;
    }
}
