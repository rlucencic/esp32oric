/*
 * The keyboard (docs/oric-hardware.md 5): 8 rows of 8 keys, oric.h's
 * ORIC_KEY_* (row * 8 + column). The ROM puts a row on PB0-PB2 and drives
 * columns by 0 bits in the AY's port A; PB3 reads 1 while a key down in that
 * row sits in a driven column (several columns: any of them).
 *
 * Keys come from other tasks (the board's keyboards: atomic bits) and from
 * typed text (the emulation task: a key held for HOLD_FRAMES, its SHIFT or
 * CTRL from a frame before, then all let go for GAP_FRAMES; the ROM scans
 * every 30 ms, so a key is seen twice and its release once). Letters follow
 * BASIC's CAPS ($020C, on at reset): with it on a capital is the plain key
 * and a small letter isn't typed as such (the ROM makes every letter a
 * capital then: CTRL-T turns CAPS off). After a RETURN the next key waits
 * until BASIC waits for one: it empties the key buffer ($02DF) on its way
 * back to "Ready" ($C993), and a key typed while a command still ran was
 * lost. BASIC 1.1's wait is its key loop's PC at a frame's end (RET_MIN to
 * RET_MAX frames after); other ROMs get RET_OTHER frames.
 */
#include <string.h>
#include "oric_internal.h"

#define HOLD_FRAMES     3               /* a typed key down: two scans */
#define GAP_FRAMES      2               /* ... then up: a scan */
#define RET_MIN         3               /* after a RETURN: BASIC waits for a key again */
#define RET_MAX         250
#define RET_OTHER       25

enum { PLAIN, SHIFT, CTRL };

typedef struct {
    uint8_t key, how;
} typed_t;

static volatile uint8_t s_matrix[8];    /* the board's keys: a row's columns, 1 down */
static uint8_t s_typed[8];              /* typed keys down, the emulation task's */
static char s_text[256];
static int s_pos, s_len;
static int s_t;                         /* frames into the character being typed */
static int s_lead;                      /* 1: its modifier went down a frame before it */
static typed_t s_char;
static int s_after_return;              /* frames since a typed RETURN was let go (0: none) */

/* Every character the Atmos's keys make (its key caps: docs 5.1) */
static bool for_char(char ch, typed_t *t)
{
    static const struct {
        char c;
        uint8_t key, how;
    } table[] = {
        { ' ', ORIC_KEY_SPACE, PLAIN }, { '\n', ORIC_KEY_RETURN, PLAIN },
        { '0', ORIC_KEY_0, PLAIN }, { '1', ORIC_KEY_1, PLAIN }, { '2', ORIC_KEY_2, PLAIN }, { '3', ORIC_KEY_3, PLAIN },
        { '4', ORIC_KEY_4, PLAIN }, { '5', ORIC_KEY_5, PLAIN }, { '6', ORIC_KEY_6, PLAIN }, { '7', ORIC_KEY_7, PLAIN },
        { '8', ORIC_KEY_8, PLAIN }, { '9', ORIC_KEY_9, PLAIN },
        { ')', ORIC_KEY_0, SHIFT }, { '!', ORIC_KEY_1, SHIFT }, { '@', ORIC_KEY_2, SHIFT }, { '#', ORIC_KEY_3, SHIFT },
        { '$', ORIC_KEY_4, SHIFT }, { '%', ORIC_KEY_5, SHIFT }, { '^', ORIC_KEY_6, SHIFT }, { '&', ORIC_KEY_7, SHIFT },
        { '*', ORIC_KEY_8, SHIFT }, { '(', ORIC_KEY_9, SHIFT },
        { '-', ORIC_KEY_MINUS, PLAIN }, { '=', ORIC_KEY_EQUALS, PLAIN }, { '+', ORIC_KEY_EQUALS, SHIFT },
        { '\\', ORIC_KEY_BACKSLASH, PLAIN }, { '|', ORIC_KEY_BACKSLASH, SHIFT },
        { '[', ORIC_KEY_LBRACKET, PLAIN }, { '{', ORIC_KEY_LBRACKET, SHIFT },
        { ']', ORIC_KEY_RBRACKET, PLAIN }, { '}', ORIC_KEY_RBRACKET, SHIFT },
        { ';', ORIC_KEY_SEMICOLON, PLAIN }, { ':', ORIC_KEY_SEMICOLON, SHIFT },
        { '\'', ORIC_KEY_QUOTE, PLAIN }, { '"', ORIC_KEY_QUOTE, SHIFT },
        { ',', ORIC_KEY_COMMA, PLAIN }, { '<', ORIC_KEY_COMMA, SHIFT },
        { '.', ORIC_KEY_PERIOD, PLAIN }, { '>', ORIC_KEY_PERIOD, SHIFT },
        { '/', ORIC_KEY_SLASH, PLAIN }, { '?', ORIC_KEY_SLASH, SHIFT },
        { 0x1B, ORIC_KEY_ESC, PLAIN }, { 0x7F, ORIC_KEY_DEL, PLAIN }, { 0x08, ORIC_KEY_LEFT, PLAIN },
    };
    static const uint8_t letters[26] = {
        ORIC_KEY_A, ORIC_KEY_B, ORIC_KEY_C, ORIC_KEY_D, ORIC_KEY_E, ORIC_KEY_F, ORIC_KEY_G, ORIC_KEY_H,
        ORIC_KEY_I, ORIC_KEY_J, ORIC_KEY_K, ORIC_KEY_L, ORIC_KEY_M, ORIC_KEY_N, ORIC_KEY_O, ORIC_KEY_P,
        ORIC_KEY_Q, ORIC_KEY_R, ORIC_KEY_S, ORIC_KEY_T, ORIC_KEY_U, ORIC_KEY_V, ORIC_KEY_W, ORIC_KEY_X,
        ORIC_KEY_Y, ORIC_KEY_Z,
    };
    /* CAPS: $020C is a mask on the key table's bit 7 (small letters):
     * $FF on, $7F off (ROM $F50C)
     */
    bool caps = (oric.ram[0x020C] & 0x80) != 0;
    if (ch >= 'A' && ch <= 'Z') {
        t->key = letters[ch - 'A'];
        t->how = caps ? PLAIN : SHIFT;
        return true;
    }
    if (ch >= 'a' && ch <= 'z') {
        t->key = letters[ch - 'a'];
        t->how = PLAIN;
        return true;
    }
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (table[i].c == ch) {
            t->key = table[i].key;
            t->how = table[i].how;
            return true;
        }
    }
    if (ch == '\r') {
        t->key = ORIC_KEY_RETURN;
        t->how = PLAIN;
        return true;
    }
    if (ch >= 1 && ch <= 26) {          /* the other control codes: CTRL and the letter (CTRL-T: CAPS) */
        t->key = letters[ch - 1];
        t->how = CTRL;
        return true;
    }
    return false;
}

bool oric_keyboard_sense(uint8_t row, uint8_t columns)
{
    row &= 7;
    return ((s_matrix[row] | s_typed[row]) & (uint8_t)~columns) != 0;
}

static inline void set_bit(uint8_t *m, int key, bool down)
{
    if (down) {
        m[key / 8] |= 1 << (key % 8);
    } else {
        m[key / 8] &= ~(1 << (key % 8));
    }
}

void oric_key(int key, bool down)
{
    if (key < 0 || key >= ORIC_KEYS) {
        return;
    }
    uint8_t bit = 1 << (key % 8);
    if (down) {
        __atomic_fetch_or(&s_matrix[key / 8], bit, __ATOMIC_RELAXED);
    } else {
        __atomic_fetch_and(&s_matrix[key / 8], (uint8_t)~bit, __ATOMIC_RELAXED);
    }
}

void oric_keyboard_reset(void)
{
    memset(s_typed, 0, sizeof(s_typed));
    s_pos = s_len = s_t = 0;
    s_after_return = 0;
}

/* BASIC 1.1 waits for a key: its line editor's loop ($C5E8: JSR $023B,
 * BPL back) and the routine there ($EB78: LDA $02DF, BPL to its RTS)
 */
static bool basic_waits(void)
{
    static const uint8_t loop[] = { 0xAD, 0xDF, 0x02, 0x10 };    /* $EB78 */
    if (memcmp(oric.rom + (0xEB78 - 0xC000), loop, sizeof(loop)) != 0) {
        return s_after_return >= RET_OTHER;
    }
    uint16_t pc = oric.cpu.pc;
    return (s_after_return >= RET_MIN && ((pc >= 0xC5E8 && pc <= 0xC5EB) || (pc >= 0xEB78 && pc <= 0xEB88) ||
                                          (pc >= 0x023B && pc <= 0x023D))) ||
           s_after_return >= RET_MAX;
}

bool oric_type(const char *text)
{
    if (s_pos < s_len) {
        return false;                   /* still typing the last one */
    }
    size_t n = strlen(text);
    if (n >= sizeof(s_text)) {
        n = sizeof(s_text) - 1;
    }
    s_len = 0;
    memcpy(s_text, text, n);
    s_pos = 0;
    s_t = 0;
    s_len = n;
    return true;
}

/* Once a frame, in the emulation task. A character's SHIFT or CTRL goes
 * down a frame before its key: together, a scan under way could see the
 * key (column 2, say) after it had passed the modifiers' column 4 (the
 * ROM scans columns 7 down to 0), and ':' came out as ';'.
 */
void oric_keyboard_frame(void)
{
    if (s_pos >= s_len) {
        return;
    }
    if (s_after_return) {
        if (!basic_waits()) {
            s_after_return++;
            return;
        }
        s_after_return = 0;
    }
    if (s_t == 0) {
        while (s_pos < s_len && !for_char(s_text[s_pos], &s_char)) {
            s_pos++;                    /* nothing types it */
        }
        if (s_pos >= s_len) {
            return;
        }
        s_lead = s_char.how != PLAIN;
        if (s_char.how == SHIFT) {
            set_bit(s_typed, ORIC_KEY_LSHIFT, true);
        } else if (s_char.how == CTRL) {
            set_bit(s_typed, ORIC_KEY_CTRL, true);
        }
    }
    if (s_t == s_lead) {
        set_bit(s_typed, s_char.key, true);
    }
    if (s_t == s_lead + HOLD_FRAMES) {
        memset(s_typed, 0, sizeof(s_typed));        /* let go */
    }
    if (++s_t >= s_lead + HOLD_FRAMES + GAP_FRAMES) {
        s_t = 0;
        if (s_char.key == ORIC_KEY_RETURN) {
            s_after_return = 1;
        }
        s_pos++;
    }
}

/* The keys down now, a row's columns a byte (the board's and typed ones) */
void oric_keys_down(uint8_t rows[8])
{
    for (int r = 0; r < 8; r++) {
        rows[r] = s_matrix[r] | s_typed[r];
    }
}
