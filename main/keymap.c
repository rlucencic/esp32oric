/*
 * The board's keyboards (Bluetooth, USB, the Tab5 Keyboard: HID usages) to
 * the Oric Atmos's keys, by position. The Atmos's layout is a US one's
 * (docs/oric-hardware.md 5.1: `1-0 - = \`, `ESC Q-P [ ] DEL`, `CTRL A-L ; '
 * RETURN`, `SHIFT Z-M , . / SHIFT`), its shifted symbols too, so letters,
 * digits and punctuation are where they are on a PC keyboard: Tab (left
 * of Q) and Esc are ESC, Caps Lock's place CTRL (Ctrl too), both Shifts
 * themselves, Alt FUNCT. Kept for their use: Backspace and Delete DEL,
 * the cursor keys the Oric's, Caps Lock CTRL-T (BASIC's CAPS on and off),
 * F12 and Pause the button underneath (NMI: BASIC's warm start), the
 * keypad its digits and signs.
 *
 * Caps Lock and the keypad's * and + need a SHIFT or CTRL with the key:
 * they are typed (oric_type(): the modifier a frame before the key, so the
 * ROM's scan can't see the key alone).
 */
#include <string.h>
#include "oric.h"
#include "keymap.h"

#define NONE    0xFF

typedef struct {
    uint8_t key1;                       /* the key + 1 (0: none; key 0 is the 7) */
    char typed;                         /* or a character typed */
    bool nmi;                           /* or the button underneath */
} keymap_t;

#define K(k)        { (k) + 1, 0, false }
#define TYPED(c)    { 0, (c), false }
#define NMI         { 0, 0, true }

static const keymap_t usage_map[0x66] = {
    [0x04] = K(ORIC_KEY_A), [0x05] = K(ORIC_KEY_B), [0x06] = K(ORIC_KEY_C), [0x07] = K(ORIC_KEY_D),
    [0x08] = K(ORIC_KEY_E), [0x09] = K(ORIC_KEY_F), [0x0A] = K(ORIC_KEY_G), [0x0B] = K(ORIC_KEY_H),
    [0x0C] = K(ORIC_KEY_I), [0x0D] = K(ORIC_KEY_J), [0x0E] = K(ORIC_KEY_K), [0x0F] = K(ORIC_KEY_L),
    [0x10] = K(ORIC_KEY_M), [0x11] = K(ORIC_KEY_N), [0x12] = K(ORIC_KEY_O), [0x13] = K(ORIC_KEY_P),
    [0x14] = K(ORIC_KEY_Q), [0x15] = K(ORIC_KEY_R), [0x16] = K(ORIC_KEY_S), [0x17] = K(ORIC_KEY_T),
    [0x18] = K(ORIC_KEY_U), [0x19] = K(ORIC_KEY_V), [0x1A] = K(ORIC_KEY_W), [0x1B] = K(ORIC_KEY_X),
    [0x1C] = K(ORIC_KEY_Y), [0x1D] = K(ORIC_KEY_Z),
    [0x1E] = K(ORIC_KEY_1), [0x1F] = K(ORIC_KEY_2), [0x20] = K(ORIC_KEY_3), [0x21] = K(ORIC_KEY_4),
    [0x22] = K(ORIC_KEY_5), [0x23] = K(ORIC_KEY_6), [0x24] = K(ORIC_KEY_7), [0x25] = K(ORIC_KEY_8),
    [0x26] = K(ORIC_KEY_9), [0x27] = K(ORIC_KEY_0),
    [0x28] = K(ORIC_KEY_RETURN),
    [0x29] = K(ORIC_KEY_ESC),
    [0x2A] = K(ORIC_KEY_DEL),           /* Backspace */
    [0x2B] = K(ORIC_KEY_ESC),           /* Tab */
    [0x2C] = K(ORIC_KEY_SPACE),
    [0x2D] = K(ORIC_KEY_MINUS),
    [0x2E] = K(ORIC_KEY_EQUALS),
    [0x2F] = K(ORIC_KEY_LBRACKET),
    [0x30] = K(ORIC_KEY_RBRACKET),
    [0x31] = K(ORIC_KEY_BACKSLASH),
    [0x32] = K(ORIC_KEY_BACKSLASH),     /* non-US # */
    [0x33] = K(ORIC_KEY_SEMICOLON),
    [0x34] = K(ORIC_KEY_QUOTE),
    [0x35] = K(ORIC_KEY_ESC),           /* ` */
    [0x36] = K(ORIC_KEY_COMMA),
    [0x37] = K(ORIC_KEY_PERIOD),
    [0x38] = K(ORIC_KEY_SLASH),
    [0x39] = TYPED(0x14),               /* Caps Lock: CTRL-T */
    [0x45] = NMI,                       /* F12 */
    [0x48] = NMI,                       /* Pause/Break */
    [0x4C] = K(ORIC_KEY_DEL),           /* Delete */
    [0x4F] = K(ORIC_KEY_RIGHT), [0x50] = K(ORIC_KEY_LEFT), [0x51] = K(ORIC_KEY_DOWN), [0x52] = K(ORIC_KEY_UP),
    [0x54] = K(ORIC_KEY_SLASH),         /* the keypad */
    [0x55] = TYPED('*'),
    [0x56] = K(ORIC_KEY_MINUS),
    [0x57] = TYPED('+'),
    [0x58] = K(ORIC_KEY_RETURN),
    [0x59] = K(ORIC_KEY_1), [0x5A] = K(ORIC_KEY_2), [0x5B] = K(ORIC_KEY_3), [0x5C] = K(ORIC_KEY_4),
    [0x5D] = K(ORIC_KEY_5), [0x5E] = K(ORIC_KEY_6), [0x5F] = K(ORIC_KEY_7), [0x60] = K(ORIC_KEY_8),
    [0x61] = K(ORIC_KEY_9), [0x62] = K(ORIC_KEY_0),
    [0x63] = K(ORIC_KEY_PERIOD),
    [0x64] = K(ORIC_KEY_BACKSLASH),     /* non-US \, left of Z */
};

static uint8_t s_count[ORIC_KEYS];      /* presses holding each key */
static uint8_t s_held[256];             /* per HID usage: the key it pressed + 1 */

static void key_ref(uint8_t key, bool down)
{
    if (key >= ORIC_KEYS) {
        return;
    }
    bool was = s_count[key] > 0;
    s_count[key] = down ? s_count[key] + 1 : s_count[key] ? s_count[key] - 1 : 0;
    bool now = s_count[key] > 0;
    if (was != now) {
        oric_key(key, now);
    }
}

void keymap_hid_key(uint8_t usage, bool pressed)
{
    if (usage >= 0xE0 && usage <= 0xE7) {
        /* LCtrl LShift LAlt LGUI RCtrl RShift RAlt RGUI */
        static const uint8_t keys[8] = {
            ORIC_KEY_CTRL, ORIC_KEY_LSHIFT, ORIC_KEY_FUNCT, NONE,
            ORIC_KEY_CTRL, ORIC_KEY_RSHIFT, ORIC_KEY_FUNCT, NONE,
        };
        key_ref(keys[usage - 0xE0], pressed);
        return;
    }
    if (pressed) {
        if (usage >= sizeof(usage_map) / sizeof(usage_map[0])) {
            return;
        }
        keymap_t m = usage_map[usage];
        if (m.nmi) {
            oric_nmi();
        } else if (m.typed) {
            char text[2] = { m.typed, 0 };
            oric_type(text);
        } else if (m.key1) {
            s_held[usage] = m.key1;
            key_ref(m.key1 - 1, true);
        }
    } else if (s_held[usage]) {
        key_ref(s_held[usage] - 1, false);
        s_held[usage] = 0;
    }
}
