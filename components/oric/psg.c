/*
 * The AY-3-8912 (docs/oric-hardware.md 3): sixteen registers on the VIA's
 * port A, the bus cycle chosen by BDIR (CB2) and BC1 (CA2), BC2 tied high:
 * latch an address, write, read. Its I/O port A (R14, an output while R7
 * bit 6 is set) selects the keyboard's columns.
 *
 * Sound, clocked at 1 MHz like the CPU: three square-wave tones (half a
 * period every 8 x TP clocks), a noise generator (a 17-bit shift register,
 * bit 0 XOR bit 3 fed in at the top, stepped every 16 x NP clocks), an
 * envelope of 16 steps (one every 16 x EP clocks; R13's shape: continue,
 * attack, alternate, hold). A channel is on while its tone (or its tone
 * disabled in R7) and the noise (or noise disabled) are both high; its
 * level is R8-R10's volume or the envelope's, logarithmic (two steps
 * halve it: 15 is 1, 13 a half, 0 off). The three are mixed into one
 * (the Oric's speaker is mono), through a capacitor (the DC goes), two
 * samples a 64 us line (ORIC_AUDIO_HZ, 31250), each the average of its
 * 32 clocks, into a ring the board's speaker task drains (and paces the
 * emulation by).
 */
#include <string.h>
#include "oric_internal.h"

#define RING            8192            /* stereo frames, a power of two */
#define TICK            8               /* clocks a generator step */
#define SAMPLE_TICKS    4               /* steps a sample: 32 clocks */

/* Each register's bits (the 8912's port B register R15 isn't there) */
static const uint8_t s_mask[16] = {
    0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0xFF, 0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F, 0xFF, 0x00,
};

/* The levels: 15 is 1.0 (6000 of the 16-bit range: the three together
 * stay inside it), every two steps half as loud (AUG p50)
 */
static const int16_t s_level[16] = {
    0, 47, 66, 94, 133, 188, 265, 375, 530, 750, 1061, 1500, 2121, 3000, 4243, 6000,
};

static struct {
    uint8_t reg[16];
    uint8_t latch;                      /* the register the bus addresses */
    bool selected;                      /* the latched address's high nibble was 0 */
    uint16_t tone_count[3];
    uint8_t tone_out[3];
    uint8_t noise_count, noise_out;
    uint32_t lfsr;
    uint8_t prescale;                   /* noise and envelope step every other tick */
    uint16_t env_count;
    uint8_t env_step;                   /* 0-15 within a cycle */
    uint8_t env_vol;
    bool env_attack, env_holding;
    int32_t dc_x;                       /* the output capacitor: the last input */
    int64_t dc_y;                       /* ... its output, 16 bits of fraction */
} s;

static int16_t s_ring[RING][2];
static volatile uint32_t s_head, s_tail;

static void env_restart(void)
{
    s.env_count = 0;
    s.env_step = 0;
    s.env_attack = (s.reg[13] & 0x04) != 0;
    s.env_holding = false;
    s.env_vol = s.env_attack ? 0 : 15;
}

void psg_reset(void)
{
    memset(&s, 0, sizeof(s));
    s.lfsr = 1;
    s.selected = true;
    env_restart();
}

static void write_reg(uint8_t r, uint8_t v)
{
    s.reg[r] = v & s_mask[r];
    if (r == 13) {
        env_restart();
    }
}

/* Each change of the VIA's port A or CA2/CB2. The address's high nibble
 * must be 0 (the chip's mask-programmed address, the data sheet): any
 * other deselects the chip until the next latch.
 */
void psg_bus(bool bdir, bool bc1, uint8_t data)
{
    if (bdir && bc1) {
        s.latch = data & 0x0F;
        s.selected = (data & 0xF0) == 0;
    } else if (bdir && s.selected) {
        write_reg(s.latch, data);
    }
}

uint8_t psg_read(void)
{
    if (!s.selected) {
        return 0xFF;
    }
    if (s.latch == 14 && !(s.reg[7] & 0x40)) {
        return 0xFF;                    /* port A an input: nothing pulls its pins (the keyboard only drives) */
    }
    return s.reg[s.latch];
}

/* Port A's pins: R14 while it is an output, else pulled high (no column) */
uint8_t psg_port_a(void)
{
    return (s.reg[7] & 0x40) ? s.reg[14] : 0xFF;
}

void psg_get_regs(uint8_t regs[16])
{
    memcpy(regs, s.reg, 16);
}

/* ---- Sound ---- */

/* The envelope's next step; at a cycle's end its shape decides (docs 3.2,
 * R13: bit 3 continue, 2 attack, 1 alternate, 0 hold)
 */
static void env_step(void)
{
    if (s.env_holding) {
        return;
    }
    if (++s.env_step < 16) {
        s.env_vol = s.env_attack ? s.env_step : 15 - s.env_step;
        return;
    }
    uint8_t shape = s.reg[13];
    if (!(shape & 0x08)) {
        s.env_holding = true;
        s.env_vol = 0;
    } else if (shape & 0x01) {
        s.env_holding = true;
        bool up = (shape & 0x02) ? !s.env_attack : s.env_attack;
        s.env_vol = up ? 15 : 0;
    } else {
        if (shape & 0x02) {
            s.env_attack = !s.env_attack;
        }
        s.env_step = 0;
        s.env_vol = s.env_attack ? 0 : 15;
    }
}

/* One generator step (8 clocks); the mixed level */
static inline int tick(void)
{
    for (int i = 0; i < 3; i++) {
        uint16_t period = s.reg[2 * i] | (s.reg[2 * i + 1] << 8);
        if (++s.tone_count[i] >= (period ? period : 1)) {
            s.tone_count[i] = 0;
            s.tone_out[i] ^= 1;
        }
    }
    s.prescale ^= 1;
    if (s.prescale) {
        uint8_t np = s.reg[6];
        if (++s.noise_count >= (np ? np : 1)) {
            s.noise_count = 0;
            uint32_t bit = (s.lfsr ^ (s.lfsr >> 3)) & 1;
            s.lfsr = (s.lfsr >> 1) | (bit << 16);
            s.noise_out = s.lfsr & 1;
        }
        uint16_t ep = s.reg[11] | (s.reg[12] << 8);
        if (++s.env_count >= (ep ? ep : 1)) {
            s.env_count = 0;
            env_step();
        }
    }
    uint8_t mixer = s.reg[7];
    int out = 0;
    for (int i = 0; i < 3; i++) {
        bool tone = s.tone_out[i] || (mixer & (1 << i));
        bool noise = s.noise_out || (mixer & (8 << i));
        if (tone && noise) {
            uint8_t vol = s.reg[8 + i];
            out += s_level[(vol & 0x10) ? s.env_vol : (vol & 0x0F)];
        }
    }
    return out;
}

void psg_line(void)
{
    for (int k = 0; k < 2; k++) {
        int sum = 0;
        for (int i = 0; i < SAMPLE_TICKS; i++) {
            sum += tick();
        }
        int32_t x = sum / SAMPLE_TICKS;
        /* the capacitor: y = x - x' + 0.995 y', y kept with 16 bits of
         * fraction (in whole numbers it got stuck at up to -199: the floor
         * of 0.995 y is y itself there)
         */
        s.dc_y = ((int64_t)(x - s.dc_x) << 16) + (((int64_t)s.dc_y * 32604) >> 15);
        s.dc_x = x;
        int32_t y = (int32_t)(s.dc_y >> 16);
        int16_t v = y > 32767 ? 32767 : y < -32768 ? -32768 : (int16_t)y;
        uint32_t head = s_head;
        if (head - s_tail >= RING) {
            continue;                   /* nobody drains it (the host without a WAV) */
        }
        s_ring[head % RING][0] = v;
        s_ring[head % RING][1] = v;
        s_head = head + 1;
    }
}

void oric_audio_enable(bool on)
{
    if (!on) {
        s_tail = s_head;
    }
}

int oric_audio_render(int16_t *dst, int frames)
{
    uint32_t tail = s_tail, n = s_head - tail;
    if (n > (uint32_t)frames) {
        n = frames;
    }
    for (uint32_t i = 0; i < n; i++) {
        dst[2 * i] = s_ring[(tail + i) % RING][0];
        dst[2 * i + 1] = s_ring[(tail + i) % RING][1];
    }
    s_tail = tail + n;
    return n;
}

int oric_audio_pending(void)
{
    return s_head - s_tail;
}
