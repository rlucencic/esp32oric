/*
 * The cassette (docs/oric-hardware.md 6): TAP files, the tape's bytes
 * without their bit framing (each file some $16s, $24, nine header bytes,
 * the name and a $00, then the data).
 *
 * Played: while the motor (PB6) runs, the bytes go to CB1 as the ROM writes
 * them. 2400 baud: a cycle a bit, rising edge to rising edge, 1 a short one
 * (210 + 210 us), 0 a long one (210 + 418); 300 baud when BASIC 1.1's speed
 * flag ($024D) is set as the motor starts (1 eight short cycles, 0 four of
 * 418 + 418). Each byte: three 1s (the stop bits before it), a start bit 0,
 * eight data bits from bit 0, the parity bit (1 when the data has an even
 * number of 1s). A file's leader gets LEADER_EXTRA more $16s (the ROM's
 * sync wants a bit-synchronised one and three whole ones; TAP files have
 * three or four in all), and GAP_BITS 1s follow its name (the loader
 * prints "Found" and compares names meanwhile).
 *
 * Fast (the default): BASIC 1.1's tape routines trapped. Their ROM pages
 * ($E4, $E6, $E7) are read through the bus callback (tape_rom_read(): the
 * ROM's bytes, always: Poopy's loader copies these routines into RAM and
 * runs the copy); a read of a routine's first byte stops the CPU after
 * that instruction, and if the PC is then where it leads (a JSR's target,
 * the return address on the stack) the routine was entered: tape_trap()
 * does its work and its RTS (a JSR undone first). A read as data leads
 * elsewhere and changes nothing. Loading:
 * the sync ($E735: on to the next file's $24, from the start once when the
 * tape has run out), the byte reader ($E6C9), the data loop ($E4E0: the
 * whole file at once). Saving, with any tape in: the leader ($E75A: three
 * $16s), the byte writer ($E65E), the data loop ($E62E); the bytes go on
 * the tape's end and into its file when the motor stops. A routine the
 * tape can't serve (it has run out, or a name isn't on it) goes on as it
 * is (its first instruction has run), and so do the loading ones until
 * the motor starts again: the ROM reads on in real time (its sync loops
 * through $E735 a bit at a time) and waits at the end, as on an Oric. A loader of its own that polls CB1 (IFR read 500 times
 * in a frame, or CB1's interrupt enabled) with the motor on gets the tape
 * played in real time from where the traps left it.
 *
 * Real time: no load traps; the tape plays while the motor runs and
 * something reads it (CLOAD's search: a frame after the motor starts; a
 * CSAVE, trapped, doesn't), and the ROM's sync ($E735) is only watched: a
 * search that begins at the tape's end starts it again from the
 * beginning, once a motor start (a name that isn't on the tape still ends
 * in a wait at its end).
 *
 * Either way the tape waits while nothing reads it (two frames without
 * polling), and goes on when something does: a TAP file has lost the real
 * tape's gaps, and a program that keeps the motor on while it shows
 * something (Demodiam's 8 s intro before its next file) or clicks the
 * relay in a game (Damsel) would have the tape run past its next file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "oric_internal.h"

#define MAX_TAPE        (4 * 1024 * 1024)
#define MAX_FILES       256
#define LEADER_EXTRA    64              /* $16s before a file's own (0.44 s) */
#define GAP_BITS        100             /* 1s after a file's name (42 ms) */
#define HALF_SHORT      210             /* timer 1's 208 and 416 (N + 2) */
#define HALF_LONG       418
#define FRAME_BITS      13              /* three 1s, start, eight data, parity */
#define POLL_READS      500             /* IFR reads in a frame: a loader polling CB1 */
#define QUIET_READS     50              /* ... fewer: nothing reads the tape */
#define QUIET_FRAMES    2
#define SAVE_IDLE       50              /* frames without saving: the save goes into the file */

/* ---- BASIC 1.1's tape routines ---- */

enum { TRAP_SYNC, TRAP_BYTE, TRAP_DATA, TRAP_LEADER, TRAP_WBYTE, TRAP_WDATA, TRAPS };

static const struct {
    uint16_t addr;
    uint8_t code[4];                    /* its first bytes: the ROM is 1.1's */
    uint16_t after;                     /* the PC after its first instruction */
    bool jsr;                           /* ... a JSR: its return on the stack */
    bool load;                          /* fast loading only */
} s_traps[TRAPS] = {
    [TRAP_SYNC] = { 0xE735, { 0x20, 0xFC, 0xE6, 0x66 }, 0xE6FC, true, true },     /* JSR $E6FC; ROR $2F */
    [TRAP_BYTE] = { 0xE6C9, { 0x98, 0x48, 0x8A, 0x48 }, 0xE6CA, false, true },    /* TYA; PHA; TXA; PHA */
    [TRAP_DATA] = { 0xE4E0, { 0xAD, 0xA9, 0x02, 0xAC }, 0xE4E3, false, true },    /* LDA $02A9; LDY $02AA */
    [TRAP_LEADER] = { 0xE75A, { 0xA2, 0x02, 0xA0, 0x03 }, 0xE75C, false, false }, /* LDX #2; LDY #3 */
    [TRAP_WBYTE] = { 0xE65E, { 0x85, 0x2F, 0x8A, 0x48 }, 0xE660, false, false },  /* STA $2F; TXA; PHA */
    [TRAP_WDATA] = { 0xE62E, { 0xAD, 0xA9, 0x02, 0xAC }, 0xE631, false, false },  /* LDA $02A9; LDY $02AA */
};

static const uint8_t s_trap_pages[] = { 0xE4, 0xE6, 0xE7 };
#define TRAP_PAGES  (sizeof(s_trap_pages))

static int s_hit = -1;                  /* the trap whose first byte was read in this run */

/* BASIC 1.1's addresses */
enum {
    A_SPEED = 0x024D,                   /* 0 fast, else slow */
    A_VERIFY = 0x025B,                  /* CLOAD ...,V */
    A_ERRORS = 0x025C,                  /* verify's count (16 bits) */
    A_START = 0x02A9,                   /* the header: start, end (inclusive) */
    A_END = 0x02AB,
    A_PTR = 0x33,                       /* the data loops' pointer */
    A_BYTE = 0x2F,                      /* the byte being read or written */
};

/* ---- The tape ---- */

typedef struct {
    uint32_t leader, marker, data, end; /* its first $16, the $24, after the name, after the data */
    uint16_t start, last;
    uint8_t type;                       /* $00 BASIC, $80 machine code, $40 an array */
    bool autorun;
    char name[17];
} tfile_t;

static struct {
    char path[256];
    bool inserted;
    uint8_t *data;
    uint32_t size, cap;
    uint32_t pos;                       /* the next byte the head gets to */
    tfile_t *file;                      /* the index (kept once made: the status reads it) */
    int files;
    bool basic11;                       /* the ROM's routines are 1.1's: traps */
    bool fast, want_fast;
    bool armed_load, armed_save;
    bool motor;
    bool wrapped;                       /* the sync went back to the start (once a search) */
    bool declined;                      /* a load trap couldn't serve: the ROM's routines read on */
    uint32_t unsaved;                   /* bytes from here on not in the file yet */
    int save_idle;
    bool write_failed;
    uint32_t loaded, saved;             /* files */
    uint32_t ifr_reads;
    int busy_frames, quiet_frames;
} s = { .want_fast = true, .fast = true };

/* ---- The player ---- */

#define NO_BYTE UINT32_MAX

uint64_t tape_edge_at = UINT64_MAX;

static struct {
    bool playing, slow;
    bool high;                          /* in a cycle's first half */
    uint8_t cur;                        /* the byte being played */
    uint32_t cur_from;                  /* its offset (NO_BYTE: an extra $16) */
    int bit_i;                          /* bits of its frame begun */
    bool bit;                           /* the bit being played */
    int cycles_left;                    /* its cycles still to come */
    int extra_lead, gap;
    uint32_t inserted_at;               /* the offset the extras were made at */
    bool trailed;                       /* the tape's last stop bits played */
} p = { .bit_i = FRAME_BITS, .cur_from = NO_BYTE, .inserted_at = NO_BYTE };

/* ---- Files ---- */

static void index_files(void)
{
    if (s.file == NULL) {
        s.file = ORIC_SLOW_ALLOC(MAX_FILES * sizeof(tfile_t));
        if (s.file == NULL) {
            s.files = 0;
            return;
        }
    }
    const uint8_t *d = s.data;
    uint32_t size = s.size, pos = 0;
    int n = 0;
    while (pos < size && n < MAX_FILES) {
        uint32_t q = pos;
        while (q < size && d[q] == 0x16) {
            q++;
        }
        if (q == pos || q >= size || d[q] != 0x24) {
            /* no file here: the next 16 16 16 24 */
            for (q = pos + 1; q + 3 < size; q++) {
                if (d[q] == 0x16 && d[q + 1] == 0x16 && d[q + 2] == 0x16 && d[q + 3] == 0x24) {
                    break;
                }
            }
            if (q + 3 >= size) {
                break;
            }
            pos = q;
            continue;
        }
        if (q + 1 + 9 >= size) {
            break;
        }
        tfile_t *f = &s.file[n];
        memset(f, 0, sizeof(*f));
        f->leader = pos;
        f->marker = q;
        const uint8_t *h = d + q + 1;   /* tape byte 1 goes to $02B0, 9 to $02A8 */
        f->type = h[2];
        f->autorun = h[3] != 0;
        f->last = (h[4] << 8) | h[5];
        f->start = (h[6] << 8) | h[7];
        uint32_t i = q + 10;
        int k = 0;
        while (i < size && d[i] != 0 && i - (q + 10) < 255) {
            if (k < 16) {
                f->name[k++] = d[i] >= 0x20 && d[i] < 0x7F ? (char)d[i] : '?';
            }
            i++;
        }
        if (i >= size || d[i] != 0) {
            break;
        }
        f->data = i + 1;
        uint32_t len = f->start <= f->last ? (uint32_t)(f->last - f->start) + 1 : 1;
        f->end = f->data + len > size ? size : f->data + len;
        n++;
        pos = f->end;
    }
    s.files = n;
}

/* The first file whose $24 the head hasn't passed */
static int next_file(uint32_t from)
{
    for (int i = 0; i < s.files; i++) {
        if (s.file[i].marker >= from) {
            return i;
        }
    }
    return -1;
}

/* The file at the head: the one being read, else the next one */
static int file_at(uint32_t pos)
{
    for (int i = 0; i < s.files; i++) {
        if (pos < s.file[i].end) {
            return i;
        }
    }
    return -1;
}

/* ---- Saving ---- */

static void flush_saves(void)
{
    if (s.unsaved >= s.size) {
        return;
    }
    size_t n = s.size - s.unsaved;
    FILE *f = fopen(s.path, "ab");
    bool ok = f && fwrite(s.data + s.unsaved, 1, n, f) == n;
    if (f && fclose(f) != 0) {
        ok = false;
    }
    if (ok) {
        ORIC_LOGI("tape: %u bytes saved to %s", (unsigned)n, s.path);
    } else {
        ORIC_LOGW("tape: can't write %s (the save stays in memory only)", s.path);
        s.write_failed = true;
    }
    s.unsaved = s.size;
    index_files();
}

/* A byte onto the tape's end (in memory; the file at flush_saves()) */
static void record(uint8_t v)
{
    if (s.size >= s.cap) {
        uint32_t cap = s.cap ? s.cap * 2 : 4096;
        uint8_t *d = cap <= MAX_TAPE ? ORIC_SLOW_REALLOC(s.data, cap) : NULL;
        if (d == NULL) {
            s.write_failed = true;
            return;
        }
        s.data = d;
        s.cap = cap;
    }
    s.data[s.size++] = v;
    s.pos = s.size;
    s.save_idle = 0;
}

/* ---- The player ---- */

static void map_pages(void);

/* Playing, the trap pages are the ROM's again: something reads the tape
 * then (the ROM's own bit reader runs in them: through the bus callback
 * it cost core 1 35 points on the Tab5), and nothing to trap (a CSAVE
 * doesn't run, and a search's start at the tape's end finds it stopped)
 */
static void play(void)
{
    if (!p.playing) {
        p.playing = true;
        p.high = false;
        tape_edge_at = oric.cpu.cycles;
        map_pages();
    }
}

static void pause(void)
{
    if (p.playing) {
        p.playing = false;
        map_pages();
    }
    tape_edge_at = UINT64_MAX;
}

static void player_rewind(void)
{
    p.bit_i = FRAME_BITS;
    p.cur_from = NO_BYTE;
    p.cycles_left = 0;
    p.extra_lead = 0;
    p.gap = 0;
    p.inserted_at = NO_BYTE;
    p.high = false;
    p.trailed = false;
}

/* No more of the tape's bytes to play (a gap's or the end's 1s may be) */
static bool at_end(void)
{
    return s.pos >= s.size && p.extra_lead == 0 && p.bit_i >= FRAME_BITS;
}

static int next_byte(void)
{
    if (p.extra_lead) {
        p.extra_lead--;
        p.cur_from = NO_BYTE;
        return 0x16;
    }
    if (s.pos >= s.size) {
        return -1;
    }
    if (p.inserted_at != s.pos) {
        p.inserted_at = s.pos;
        for (int i = 0; i < s.files; i++) {
            if (s.file[i].leader == s.pos) {
                p.extra_lead = LEADER_EXTRA - 1;
                p.cur_from = NO_BYTE;
                return 0x16;
            }
            if (s.file[i].data == s.pos) {
                p.gap = GAP_BITS;
            }
        }
    }
    p.cur_from = s.pos;
    return s.data[s.pos++];
}

/* The next bit: 0 or 1, -1 at the tape's end (the gap's 1s and a frame's
 * first three come in any order: all 1s). The last byte's stop bits end
 * the tape: a bit is measured from its rising edge to the next one, so
 * the parity bit needs one after it.
 */
static int next_bit(void)
{
    if (p.gap) {
        p.gap--;
        return 1;
    }
    if (p.bit_i >= FRAME_BITS) {
        int b = next_byte();
        if (b < 0) {
            if (!p.trailed) {
                p.trailed = true;
                p.gap = 2;
                return 1;
            }
            return -1;
        }
        p.trailed = false;
        p.cur = (uint8_t)b;
        p.bit_i = 0;
    }
    int i = p.bit_i++;
    if (i < 3) {
        return 1;
    }
    if (i == 3) {
        return 0;
    }
    if (i < 12) {
        return (p.cur >> (i - 4)) & 1;
    }
    return !(__builtin_popcount(p.cur) & 1);
}

static void cb1(bool level)
{
    via_cb1(&oric.via, level);
}

/* An edge at tape_edge_at: a cycle's falling edge, or the next cycle's
 * rising one
 */
static void edge(void)
{
    if (p.high) {
        cb1(false);
        p.high = false;
        tape_edge_at += p.bit ? HALF_SHORT : HALF_LONG;
        return;
    }
    if (p.cycles_left == 0) {
        int b = next_bit();
        if (b < 0) {
            pause();                    /* the tape's end */
            return;
        }
        p.bit = b != 0;
        p.cycles_left = p.slow ? (p.bit ? 8 : 4) : 1;
    }
    p.cycles_left--;
    cb1(true);
    p.high = true;
    tape_edge_at += (p.bit || !p.slow) ? HALF_SHORT : HALF_LONG;
}

void tape_advance(uint64_t now)
{
    while (tape_edge_at <= now) {
        edge();
    }
}

/* A trap reads bytes: the head goes to the first one whose start bit
 * hasn't begun, and the playing stops there
 */
static void player_to_byte(void)
{
    tape_advance(oric.cpu.cycles);
    if (p.cur_from != NO_BYTE && p.bit_i <= 3) {
        s.pos = p.cur_from;
    }
    pause();
    player_rewind();
    p.inserted_at = s.pos;              /* no extra leader or gap here */
}

/* ---- The ROM's pages with the traps in ---- */

static bool rom_is_basic11(void)
{
    for (int t = 0; t < TRAPS; t++) {
        if (memcmp(oric.rom + (s_traps[t].addr - 0xC000), s_traps[t].code, 4) != 0) {
            return false;
        }
    }
    return true;
}

/* (the sync in real time too: watched) */
static bool armed(int t)
{
    return s_traps[t].load && t != TRAP_SYNC ? s.armed_load : s.armed_save;
}

void tape_map(void)
{
    s.fast = s.want_fast;
    if (oric.rom == NULL) {
        return;                         /* not started */
    }
    s.basic11 = rom_is_basic11();
    s.armed_load = s.basic11 && s.inserted && s.fast;
    s.armed_save = s.basic11 && s.inserted;
    map_pages();
}

/* A page with an armed trap through the bus callback, while the tape
 * stands still
 */
static void map_pages(void)
{
    if (oric.rom == NULL || !oric.basic_in) {
        return;                         /* the Microdisc's EPROM or the overlay RAM there */
    }
    for (size_t i = 0; i < TRAP_PAGES; i++) {
        uint8_t page = s_trap_pages[i];
        bool any = false;
        for (int t = 0; t < TRAPS && !p.playing; t++) {
            any |= armed(t) && (s_traps[t].addr >> 8) == page;
        }
        oric.cpu.rd[page] = any ? NULL : (uint8_t *)oric.rom + (page - 0xC0) * 256;
    }
}

/* A read in a trapped page: the ROM's byte; a routine's first one stops
 * the CPU after its instruction (tape_trap() then looks)
 */
uint8_t tape_rom_read(uint16_t a)
{
    for (int t = 0; t < TRAPS; t++) {
        if (a == s_traps[t].addr && armed(t)) {
            s_hit = t;
            m6502_lower_until(&oric.cpu, oric.cpu.cycles);
            break;
        }
    }
    return oric.rom[a - 0xC000];
}

/* ---- The traps ---- */

static uint8_t peek(uint16_t a)
{
    if (a >= 0xC000) {
        return oric.rom[a - 0xC000];
    }
    return (a >> 8) == 0x03 ? 0xFF : oric.ram[a];
}

static uint16_t word(uint16_t a)
{
    return oric.ram[a] | (oric.ram[a + 1] << 8);
}

static void rts(void)
{
    m6502_t *c = &oric.cpu;
    uint8_t lo = oric.ram[0x100 | (uint8_t)(c->s + 1)];
    uint8_t hi = oric.ram[0x100 | (uint8_t)(c->s + 2)];
    c->s += 2;
    c->pc = (uint16_t)(((hi << 8) | lo) + 1);
}

static void flags(uint8_t nz, int carry)
{
    m6502_t *c = &oric.cpu;
    c->p = (c->p & ~(M6502_N | M6502_Z)) | (nz & M6502_N) | (nz ? 0 : M6502_Z);
    if (carry >= 0) {
        c->p = (c->p & ~M6502_C) | (carry ? M6502_C : 0);
    }
}

static void log_file(const char *what, uint32_t pos)
{
    int i = file_at(pos ? pos - 1 : 0);
    if (i >= 0) {
        const tfile_t *f = &s.file[i];
        ORIC_LOGI("tape: %s \"%s\" ($%04X-$%04X)", what, f->name, f->start, f->last);
    }
}

/* Sync: on to the next file's $24 (the ROM then reads it, the header and
 * the name through the byte reader); X = 0 clears the error count
 */
static bool trap_sync(void)
{
    int f = next_file(s.pos);
    if (f < 0 && !s.wrapped) {
        s.wrapped = true;               /* the tape's end: from the start, once */
        f = next_file(0);
    }
    if (f < 0) {
        return false;
    }
    oric.cpu.s += 2;                    /* its JSR $E6FC undone */
    s.pos = s.file[f].marker;
    oric.cpu.a = 0x16;
    oric.cpu.x = 0;
    flags(0, 1);
    rts();
    return true;
}

static bool trap_byte_read(void)
{
    if (s.pos >= s.size) {
        return false;
    }
    uint8_t v = s.data[s.pos++];
    oric.ram[A_BYTE] = v;
    oric.cpu.a = v;
    flags(v, 0);                        /* (the parity check's carry: no error) */
    rts();
    return true;
}

/* The data from the header's start to its end (inclusive), stored, or
 * compared with memory for CLOAD ...,V (the differences counted). A file
 * the tape ends in loads as far as it goes, and that's the file (on an
 * Oric the ROM would wait for the rest for ever): TAP images a byte short
 * are about (Fire Flash's: its header's end taken as exclusive, likely).
 */
static bool trap_data(void)
{
    m6502_t *c = &oric.cpu;
    uint16_t addr = word(A_START), last = word(A_END);
    bool verify = oric.ram[A_VERIFY] != 0;
    uint8_t v = 0;
    bool done;
    do {
        if (s.pos >= s.size) {
            ORIC_LOGW("tape: the tape ends %u bytes before the file's end ($%04X): loaded as it is",
                      (unsigned)(last - addr) + 1, last);
            addr = last + 1;
            break;
        }
        v = s.data[s.pos++];
        if (!verify) {
            oric_write(addr, v);
        } else if (peek(addr) != v) {
            uint16_t e = word(A_ERRORS) + 1;
            oric.ram[A_ERRORS] = e;
            oric.ram[A_ERRORS + 1] = e >> 8;
        }
        done = addr >= last;
        addr++;
    } while (!done);
    oric.ram[A_PTR] = addr;
    oric.ram[A_PTR + 1] = addr >> 8;
    c->a = v;
    c->x = oric.ram[A_VERIFY];
    c->y = 0;
    flags(c->x, 1);
    s.loaded++;
    s.wrapped = false;
    log_file(verify ? "verified" : "loaded", s.pos);
    rts();
    return true;
}

static bool trap_leader(void)
{
    for (int i = 0; i < 3; i++) {
        record(0x16);
    }
    oric.cpu.a = 0x16;
    oric.cpu.x = 0;
    oric.cpu.y = 0;
    flags(0, -1);
    rts();
    return true;
}

static bool trap_byte_write(void)
{
    m6502_t *c = &oric.cpu;
    record(c->a);
    oric.ram[A_BYTE] = 0;
    c->a = c->x;
    flags(c->a, 1);
    rts();
    return true;
}

static bool trap_data_write(void)
{
    m6502_t *c = &oric.cpu;
    uint16_t addr = word(A_START), last = word(A_END);
    bool done;
    do {
        record(peek(addr));
        done = addr >= last;
        addr++;
    } while (!done);
    oric.ram[A_PTR] = addr;
    oric.ram[A_PTR + 1] = addr >> 8;
    c->y = 0;
    flags(0, 1);
    s.saved++;
    ORIC_LOGI("tape: saved $%04X-$%04X", word(A_START), last);
    rts();
    return true;
}

/* After a run: a routine entered (its first instruction run) is done
 * here; one the tape can't serve goes on as it is
 */
bool tape_trap(void)
{
    int t = s_hit;
    if (t < 0) {
        return false;
    }
    s_hit = -1;
    m6502_t *c = &oric.cpu;
    if (!armed(t) || c->pc != s_traps[t].after) {
        return false;                   /* read as data, or not run yet */
    }
    if (s_traps[t].jsr) {
        uint16_t ret = oric.ram[0x100 | (uint8_t)(c->s + 1)] | (oric.ram[0x100 | (uint8_t)(c->s + 2)] << 8);
        if (ret != s_traps[t].addr + 2) {
            return false;
        }
    }
    if (t == TRAP_SYNC && !s.fast) {
        /* real time: a search from the tape's end, from its start */
        if (at_end() && !s.wrapped) {
            s.wrapped = true;
            s.pos = 0;
            player_rewind();
            if (s.motor) {
                play();
            }
        }
        return false;
    }
    if (s_traps[t].load) {
        if (s.declined) {
            return false;
        }
        player_to_byte();
    }
    switch (t) {
    case TRAP_SYNC: return s.declined = !trap_sync(), !s.declined;
    case TRAP_BYTE: return s.declined = !trap_byte_read(), !s.declined;
    case TRAP_DATA: return trap_data();
    case TRAP_LEADER: return trap_leader();
    case TRAP_WBYTE: return trap_byte_write();
    case TRAP_WDATA: return trap_data_write();
    }
    return false;
}

/* ---- The machine's side ---- */

void tape_motor(bool on)
{
    if (on == s.motor) {
        return;
    }
    s.motor = on;
    if (!on) {
        pause();
        return;
    }
    s.wrapped = false;
    s.declined = false;
    p.slow = s.basic11 && oric.ram[A_SPEED] != 0;
    s.busy_frames = 0;
}

void tape_poll(void)
{
    s.ifr_reads++;
}

void tape_frame_end(void)
{
    if (s.want_fast != s.fast) {
        tape_map();
    }
    if (s.inserted && s.motor) {
        bool irq = (oric.via.ier & 0x10) != 0;
        if (p.playing) {
            s.quiet_frames = s.ifr_reads < QUIET_READS && !irq ? s.quiet_frames + 1 : 0;
            if (s.quiet_frames >= QUIET_FRAMES) {
                pause();                /* nothing reads it: the tape waits */
            }
        } else {
            bool polled = s.ifr_reads >= POLL_READS || irq;
            s.busy_frames = polled && !at_end() ? s.busy_frames + 1 : 0;
            /* fast: a loader of its own (the ROM's are trapped) */
            if (s.busy_frames >= (s.fast ? 2 : 1)) {
                play();
                s.quiet_frames = 0;
            }
        }
    }
    s.ifr_reads = 0;
    if (s.unsaved < s.size && (!s.motor || ++s.save_idle >= SAVE_IDLE)) {
        flush_saves();
    }
}

void tape_reset(void)
{
    pause();
    s.motor = false;
    s.wrapped = false;
    s.declined = false;
    s.busy_frames = 0;
}

void tape_nmi(void)
{
    s.wrapped = false;
}

/* ---- The API ---- */

static void tape_free(void)
{
    flush_saves();
    free(s.data);
    s.data = NULL;
    s.size = s.cap = s.pos = s.unsaved = 0;
    s.files = 0;
    s.inserted = false;
    s.path[0] = 0;
    pause();
    player_rewind();
}

bool oric_tape_insert(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint32_t cap = n > 0 ? (uint32_t)n : 4096;
    uint8_t *d = n >= 0 && n <= MAX_TAPE ? ORIC_SLOW_ALLOC(cap) : NULL;
    if (d == NULL || (n > 0 && fread(d, 1, n, f) != (size_t)n)) {
        free(d);
        fclose(f);
        return false;
    }
    fclose(f);
    tape_free();
    s.data = d;
    s.size = s.unsaved = (uint32_t)n;
    s.cap = cap;
    s.inserted = true;
    s.wrapped = false;
    s.declined = false;
    s.write_failed = false;
    s.loaded = s.saved = 0;
    snprintf(s.path, sizeof(s.path), "%s", path);
    index_files();
    tape_map();
    ORIC_LOGI("tape %s: %u bytes, %d files", path, (unsigned)n, s.files);
    return true;
}

void oric_tape_eject(void)
{
    tape_free();
    tape_map();
}

const char *oric_tape_path(void)
{
    return s.inserted ? s.path : NULL;
}

bool oric_tape_create(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    return fclose(f) == 0;
}

void oric_tape_rewind(void)
{
    pause();
    s.pos = 0;
    s.wrapped = false;
    s.declined = false;
    player_rewind();
}

void oric_tape_fast(bool fast)
{
    s.want_fast = fast;
}

void oric_tape_status(oric_tape_status_t *st)
{
    memset(st, 0, sizeof(*st));
    st->inserted = s.inserted;
    st->fast = s.want_fast;
    st->traps = s.basic11;
    st->motor = s.motor;
    st->playing = p.playing;
    st->slow = p.slow;
    st->pos = s.pos;
    st->size = s.size;
    st->files = s.files;
    st->loaded = s.loaded;
    st->saved = s.saved;
    st->write_failed = s.write_failed;
    int i = file_at(s.pos);
    st->file = i < 0 ? s.files : i;
    if (i >= 0 && s.file) {
        const tfile_t *f = &s.file[i];
        memcpy(st->name, f->name, sizeof(st->name));
        st->name[sizeof(st->name) - 1] = 0;
        st->type = f->type;
        st->autorun = f->autorun;
        st->start = f->start;
        st->last = f->last;
    }
}
