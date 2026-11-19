/*
 * The Microdisc's floppy controller, a WD1793 at 1 MHz, and its drives on
 * MFM_DISK images (docs/oric-hardware.md 7).
 *
 * An image is read whole into memory; its tracks are the bytes a turn
 * holds (6250 of each 6400: 200 ms at 32 us a byte, 300 rpm, MFM at 250
 * kbit/s), marks as $A1 $A1 $A1 then $FE (an ID: track, side, sector,
 * size, CRC) or $FB/$F8 (data, CRC). Every disk turns in the same phase
 * from the CPU's cycle 0: byte i of a track is under the head while
 * (cycles / 32) % 6250 == i. Written sectors and tracks go into the image
 * at once and back to the file at oric_disk_flush().
 *
 * The controller's commands take their real time: steps at the rate
 * asked, the head settling, the search for an ID (5 index pulses, then
 * Record Not Found), the bytes 32 us apart. A transfer is worked out when
 * the CPU looks (DRQ at $0318, the data register): the bytes due by then,
 * lost data when it was late; an event ends each phase (fdc_event_at:
 * the command's end raises INTRQ). Head load: HLD runs the motor (MAME
 * and CLK); the head is engaged (HLT) HEAD_LOAD_US later (the schematic's
 * LS123 with 270 kohm and 10 uF, ~1.2 s by its formula; CLK tuned 0.96 s
 * against a video of a real one), and HLD drops after 15 idle turns.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "oric_internal.h"

#define HEADER          256
#define TRACK_BYTES     6250            /* a turn */
#define TRACK_STRIDE    6400            /* a track in the image */
#define BYTE_US         32
#define TURN_US         (TRACK_BYTES * BYTE_US)
#define INDEX_US        2000            /* the index pulse */
#define HEAD_STOP       84              /* the drive's last track */
#define MAX_TRACKS      100             /* an image's, a side */
#define MAX_IDS         64              /* IDs a track */
#define SETTLE_US       30000           /* verify's settling, E's delay: 15 ms at 2 MHz */
#define HEAD_LOAD_US    1000000         /* HLD to HLT */
#define UNLOAD_TURNS    15
#define DAM_WINDOW      43              /* MFM: from an ID's CRC to its data mark's sync */
#define WRITE_GATE      22              /* MFM: from an ID's CRC to a write's 12 zeros */
#define SEARCH_INDEXES  5

enum {
    ST_BUSY = 0x01, ST_DRQ = 0x02, ST_INDEX = 0x02, ST_LOST = 0x04, ST_TRACK0 = 0x04, ST_CRC = 0x08,
    ST_RNF = 0x10, ST_SEEK = 0x10, ST_DELETED = 0x20, ST_HEAD = 0x20, ST_PROTECT = 0x40, ST_NOT_READY = 0x80,
};

/* What runs: a Type I command's steps, a search for an ID, a transfer */
enum { PH_IDLE, PH_STEP, PH_SEARCH, PH_READ, PH_WRITE_GATE, PH_WRITE, PH_WTRACK_WAIT, PH_WTRACK };
enum { CMD_VERIFY, CMD_READ, CMD_WRITE, CMD_ADDRESS, CMD_READ_TRACK, CMD_WRITE_TRACK };

typedef struct {
    char path[256];
    uint8_t *image;
    uint32_t size;
    int sides, tracks, geometry;
    bool inserted, protect;
    bool dirty;
    uint8_t dirty_track[2 * MAX_TRACKS];
    int cyl;                            /* the head */
    uint32_t reads, writes;             /* sectors */
} drive_t;

typedef struct {
    uint16_t id;                        /* the $FE */
    uint16_t end;                       /* the byte after its CRC */
    int16_t dam;                        /* the $FB / $F8, -1: none */
    uint8_t c, h, r, n;
    bool ok;                            /* the ID's CRC */
} sector_id_t;

uint64_t fdc_event_at = UINT64_MAX;

static drive_t *s_drive;                /* ORIC_DRIVES of them, and the track's IDs: PSRAM (fdc_init()) */

static struct {
    uint8_t status, command, track, sector, data;
    uint8_t err;                        /* the latched status bits */
    bool type1;                         /* the status shows Type I's bits */
    bool busy, intrq;
    bool index_irq;                     /* Force Interrupt's I2 */
    int phase, kind;
    int drive, side;                    /* the control register's */
    int dir;                            /* the last step: +1 in, -1 out */
    int steps;                          /* Type I's still to go */
    uint32_t step_us;
    bool verify;
    bool hld;
    uint64_t hld_at, idle_since;
    bool crc_seen;
    /* a transfer: byte i is due at t0 + i * BYTE_US (read: in the data
     * register; write: taken from it), len bytes; done: the CPU's
     */
    uint64_t t0, t_end;
    int len, done;
    const uint8_t *src;                 /* a read's bytes */
    uint8_t *buf;                       /* a write's, read address's: TRACK_STRIDE in PSRAM */
    int at;                             /* where the write goes in the track */
    uint8_t mark;
    uint16_t crc;                       /* Write Track's */
    bool f5;
    uint32_t sectors_read, sectors_written;
} wd;

static struct {
    const uint8_t *raw;                 /* the track parsed (NULL: none) */
    int count;
    sector_id_t *id;                    /* MAX_IDS */
} s_trk;

/* ---- CRC-CCITT, as the 1793's (preset to $FFFF before the $A1s) ---- */

static uint16_t crc16(uint16_t crc, const uint8_t *p, int n)
{
    while (n--) {
        crc ^= (uint16_t)(*p++ << 8);
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static uint16_t crc_byte(uint16_t crc, uint8_t v)
{
    return crc16(crc, &v, 1);
}

#define CRC_A1A1A1  0xCDB4              /* after three $A1s */

/* ---- Images ---- */

static uint8_t *track_ptr(const drive_t *d, int side, int cyl)
{
    if (!d->inserted || side >= d->sides || cyl < 0 || cyl >= d->tracks) {
        return NULL;
    }
    uint32_t n = d->geometry == 2 ? (uint32_t)(cyl * d->sides + side) : (uint32_t)(side * d->tracks + cyl);
    return d->image + HEADER + n * TRACK_STRIDE;
}

static drive_t *cur(void)
{
    return &s_drive[wd.drive];
}

static uint8_t *cur_track(void)
{
    return track_ptr(cur(), wd.side, cur()->cyl);
}

static void mark_dirty(void)
{
    drive_t *d = cur();
    int n = d->geometry == 2 ? d->cyl * d->sides + wd.side : wd.side * d->tracks + d->cyl;
    d->dirty_track[n] = 1;
    d->dirty = true;
    s_trk.raw = NULL;
}

/* The track's IDs and data marks, in order (a mark's $A1s inside a record
 * are its data: records are skipped whole, as MAME's reader does)
 */
static void parse(const uint8_t *t)
{
    s_trk.raw = t;
    s_trk.count = 0;
    if (t == NULL) {
        return;
    }
    int i = 0;
    while (i + 4 <= TRACK_BYTES && s_trk.count < MAX_IDS) {
        if (!(t[i] == 0xA1 && t[i + 1] == 0xA1 && t[i + 2] == 0xA1 && t[i + 3] == 0xFE)) {
            i++;
            continue;
        }
        int m = i + 3;
        if (m + 7 > TRACK_STRIDE) {
            break;
        }
        sector_id_t *s = &s_trk.id[s_trk.count++];
        s->id = (uint16_t)m;
        s->end = (uint16_t)(m + 7);
        s->c = t[m + 1];
        s->h = t[m + 2];
        s->r = t[m + 3];
        s->n = t[m + 4];
        s->ok = crc16(CRC_A1A1A1, t + m, 5) == ((t[m + 5] << 8) | t[m + 6]);
        s->dam = -1;
        int size = 128 << (s->n & 3);
        for (int j = m + 7; j < m + 7 + DAM_WINDOW && j + 4 <= TRACK_STRIDE; j++) {
            if (t[j] == 0xA1 && t[j + 1] == 0xA1 && t[j + 2] == 0xA1 && (t[j + 3] == 0xFB || t[j + 3] == 0xF8)) {
                if (j + 4 + size + 2 <= TRACK_STRIDE) {
                    s->dam = (int16_t)(j + 3);
                }
                break;
            }
        }
        i = s->dam >= 0 ? s->dam + 1 + size + 2 : m + 7;
    }
}

static void parse_current(void)
{
    const uint8_t *t = cur_track();
    if (t != s_trk.raw || t == NULL) {
        parse(t);
    }
}

/* ---- The head, the index, INTRQ ---- */

static bool ready(void)
{
    return cur()->inserted;
}

static bool head_engaged(uint64_t now)
{
    return wd.hld && now >= wd.hld_at + HEAD_LOAD_US;
}

static void head_load(uint64_t now)
{
    if (!wd.hld) {
        wd.hld = true;
        wd.hld_at = now;
    }
}

static bool index_pulse(uint64_t now)
{
    return wd.hld && ready() && now % TURN_US < INDEX_US;
}

static void set_intrq(bool on)
{
    if (on != wd.intrq) {
        wd.intrq = on;
        microdisc_irq();
    }
}

static void schedule(uint64_t t)
{
    fdc_event_at = t;
    m6502_lower_until(&oric.cpu, t);
}

#ifdef ORIC_HOST_HOOK
static int s_log = -1;

static bool logging(void)
{
    if (s_log < 0) {
        s_log = getenv("ORIC_DISK_LOG") != NULL;
    }
    return s_log;
}
#define DLOG(...)   do { if (logging()) { printf(__VA_ARGS__); } } while (0)
#else
#define DLOG(...)   do { } while (0)
#endif

static void finish(uint64_t now, uint8_t err)
{
    DLOG("fdc %10llu: $%02X done, errors $%02X, track %u sector %u (head %d side %d)\n",
         (unsigned long long)now, wd.command, wd.err | err, wd.track, wd.sector, cur()->cyl, wd.side);
    wd.err |= err;
    wd.busy = false;
    wd.phase = PH_IDLE;
    wd.idle_since = now;
    fdc_event_at = wd.index_irq ? (now / TURN_US + 1) * TURN_US : UINT64_MAX;
    set_intrq(true);
}

/* ---- The search for an ID ---- */

/* The first ID from time t on that the command wants, by the time its CRC
 * has passed (Read Address: its $FE, the bytes after it are the
 * transfer); or none by the 5th index pulse (RNF then)
 */
static int search(uint64_t t, uint64_t *at)
{
    parse_current();
    uint64_t a0 = t / BYTE_US + 1;
    uint64_t first_index = (a0 + TRACK_BYTES - 1) / TRACK_BYTES * TRACK_BYTES;
    uint64_t limit = first_index + (uint64_t)(SEARCH_INDEXES - 1) * TRACK_BYTES;
    int best = -1;
    uint64_t best_a = 0;
    for (int i = 0; i < s_trk.count; i++) {
        const sector_id_t *s = &s_trk.id[i];
        bool want;
        switch (wd.kind) {
        case CMD_VERIFY:
            want = s->c == wd.track;
            break;
        case CMD_ADDRESS:
            want = true;
            break;
        default:
            want = s->c == wd.track && s->r == wd.sector &&
                   (!(wd.command & 0x02) || (s->h & 1) == ((wd.command >> 3) & 1));
            break;
        }
        if (!want) {
            continue;
        }
        if (!s->ok && wd.kind != CMD_ADDRESS) {
            wd.crc_seen = true;
            continue;
        }
        uint64_t e = (wd.kind == CMD_ADDRESS ? s->id + 1u : s->end) % TRACK_BYTES;
        uint64_t a = a0 + (e + TRACK_BYTES - a0 % TRACK_BYTES) % TRACK_BYTES;
        if (best < 0 || a < best_a) {
            best = i;
            best_a = a;
        }
    }
    if (best < 0 || best_a > limit) {
        *at = limit * BYTE_US;
        return -1;
    }
    *at = best_a * BYTE_US;
    return best;
}

/* Begin (or go on with) a search from time t */
static void begin_search(uint64_t t)
{
    if (t < wd.hld_at + HEAD_LOAD_US) {
        t = wd.hld_at + HEAD_LOAD_US;       /* the head engages first */
    }
    wd.phase = PH_SEARCH;
    wd.crc_seen = false;
    uint64_t at;
    search(t, &at);
    wd.t0 = t;                          /* found again at the event: the image may change meanwhile */
    schedule(at);
}

/* ---- Transfers ---- */

/* Bytes of a read due by now */
static int arrived(uint64_t now)
{
    if (now < wd.t0) {
        return 0;
    }
    uint64_t n = (now - wd.t0) / BYTE_US + 1;
    return n > (uint64_t)wd.len ? wd.len : (int)n;
}

/* Bytes of a write taken from the data register by now */
static int taken(uint64_t now)
{
    return arrived(now);
}

static bool drq(uint64_t now)
{
    if (!wd.busy) {
        return false;
    }
    switch (wd.phase) {
    case PH_READ:
        return arrived(now) > wd.done;
    case PH_WRITE_GATE:
    case PH_WTRACK_WAIT:
        return wd.done == 0;
    case PH_WRITE:
    case PH_WTRACK:
        return wd.done < wd.len && wd.done <= taken(now);
    default:
        return false;
    }
}

/* A sector's ID found at time `at` (its CRC passed): its data, or the write's */
static void found(int i, uint64_t at)
{
    const sector_id_t *s = &s_trk.id[i];
    const uint8_t *t = s_trk.raw;
    uint64_t a_end = at / BYTE_US;      /* the absolute byte at s->end (Read Address: after the $FE) */
    if (wd.kind == CMD_ADDRESS) {
        memcpy(wd.buf, t + s->id + 1, 6);
        wd.src = wd.buf;
        wd.len = 6;
        wd.done = 0;
        wd.t0 = at + BYTE_US;
        wd.t_end = wd.t0 + 6 * BYTE_US;
        wd.phase = PH_READ;
        if (!s->ok) {
            wd.err |= ST_CRC;
        }
        wd.sector = s->c;
        schedule(wd.t_end);
        return;
    }
    if (wd.kind == CMD_READ) {
        if (s->dam < 0) {
            begin_search(at + BYTE_US);     /* no data: on to the next ID */
            return;
        }
        int size = 128 << (s->n & 3);
        wd.src = t + s->dam + 1;
        wd.len = size;
        wd.done = 0;
        wd.t0 = (a_end + (s->dam + 1 - s->end) + 1) * BYTE_US;
        wd.t_end = wd.t0 + (uint64_t)(size - 1 + 2) * BYTE_US;
        wd.mark = t[s->dam];
        wd.at = s->dam;
        wd.phase = PH_READ;
        schedule(wd.t_end);
        return;
    }
    /* write: the data register is checked WRITE_GATE bytes on */
    int size = 128 << (s->n & 3);
    wd.len = size;
    wd.done = 0;
    wd.at = s->end + WRITE_GATE;
    wd.t0 = at + (uint64_t)(WRITE_GATE + 16) * BYTE_US;    /* 12 zeros, three $A1s, the mark: then byte 0 */
    wd.t_end = wd.t0 + (uint64_t)(size + 2 + 1) * BYTE_US;
    wd.phase = PH_WRITE_GATE;
    schedule(at + (uint64_t)WRITE_GATE * BYTE_US);
}

/* A sector's data passed: its CRC */
static void read_done(uint64_t now)
{
    if (wd.kind == CMD_ADDRESS || wd.kind == CMD_READ_TRACK) {
        if (arrived(now) > wd.done + 1) {
            wd.err |= ST_LOST;
        }
        finish(now, 0);
        return;
    }
    const uint8_t *d = wd.src;
    uint16_t crc = crc16(crc_byte(CRC_A1A1A1, wd.mark), d, wd.len);
    bool ok = crc == ((d[wd.len] << 8) | d[wd.len + 1]);
    if (wd.len - wd.done > 1) {
        wd.err |= ST_LOST;
    }
    if (wd.mark == 0xF8) {
        wd.err |= ST_DELETED;
    }
    cur()->reads++;
    wd.sectors_read++;
    if (!ok) {
        finish(now, ST_CRC);
        return;
    }
    if (wd.command & 0x10) {            /* multiple: the next sector */
        wd.sector++;
        begin_search(now);
        return;
    }
    finish(now, 0);
}

/* A write's bytes into the track: 12 zeros, the mark, the data, its CRC */
static void write_done(uint64_t now)
{
    if (wd.done < wd.len) {
        memset(wd.buf + wd.done, 0, wd.len - wd.done);
        wd.err |= ST_LOST;
    }
    uint8_t *t = cur_track();
    if (t != NULL) {
        int at = wd.at;
        uint16_t crc = crc16(crc_byte(CRC_A1A1A1, wd.mark), wd.buf, wd.len);
#define PUT(v)  do { if (at < TRACK_STRIDE) { t[at] = (v); } at++; } while (0)
        for (int i = 0; i < 12; i++) {
            PUT(0x00);
        }
        PUT(0xA1);
        PUT(0xA1);
        PUT(0xA1);
        PUT(wd.mark);
        for (int i = 0; i < wd.len; i++) {
            PUT(wd.buf[i]);
        }
        PUT((uint8_t)(crc >> 8));
        PUT((uint8_t)crc);
        PUT(0x4E);
#undef PUT
        mark_dirty();
    }
    cur()->writes++;
    wd.sectors_written++;
    if (wd.command & 0x10) {
        wd.sector++;
        begin_search(now);
        return;
    }
    finish(now, 0);
}

/* Write Track's bytes as the 1793 writes them: $F5 an $A1 (the CRC preset
 * at the first), $F6 a $C2, $F7 the CRC (two bytes)
 */
static void wtrack_byte(uint8_t v)
{
    if (wd.done >= TRACK_BYTES) {
        return;
    }
    switch (v) {
    case 0xF5:
        if (!wd.f5) {
            wd.crc = 0xFFFF;
        }
        wd.f5 = true;
        wd.buf[wd.done++] = 0xA1;
        wd.crc = crc_byte(wd.crc, 0xA1);
        return;
    case 0xF6:
        wd.f5 = false;
        wd.buf[wd.done++] = 0xC2;
        return;
    case 0xF7:
        wd.f5 = false;
        wd.buf[wd.done++] = (uint8_t)(wd.crc >> 8);
        if (wd.done < TRACK_BYTES) {
            wd.buf[wd.done++] = (uint8_t)wd.crc;
        }
        return;
    default:
        wd.f5 = false;
        wd.buf[wd.done++] = v;
        wd.crc = crc_byte(wd.crc, v);
        return;
    }
}

static void wtrack_done(uint64_t now)
{
    while (wd.done < TRACK_BYTES) {
        wd.buf[wd.done++] = 0x4E;       /* (late bytes: lost data, below) */
    }
    uint8_t *t = cur_track();
    if (t != NULL) {
        memcpy(t, wd.buf, TRACK_BYTES);
        memset(t + TRACK_BYTES, 0x4E, TRACK_STRIDE - TRACK_BYTES);
        mark_dirty();
    }
    finish(now, 0);
}

/* ---- The commands ---- */

static void step_head(drive_t *d, int dir)
{
    d->cyl += dir;
    if (d->cyl < 0) {
        d->cyl = 0;
    }
    if (d->cyl > HEAD_STOP) {
        d->cyl = HEAD_STOP;
    }
}

/* A Type I command, a step at a time: restore (out to track 0, 255 steps
 * at most), seek (to the data register's track), step, step in, step out
 * (u: the track register too); then the verify
 */
static void type1_event(uint64_t now)
{
    drive_t *d = cur();
    uint8_t c = wd.command;
    if ((c & 0xE0) == 0x00) {
        bool restore = !(c & 0x10);
        if (restore ? d->cyl != 0 : wd.track != wd.data) {
            if (restore && wd.steps-- <= 0) {
                finish(now, ST_SEEK);
                return;
            }
            step_head(d, wd.dir);
            if (!restore) {
                wd.track += wd.dir;
            }
            schedule(now + wd.step_us);
            return;
        }
        if (restore) {
            wd.track = 0;
        }
    } else if (wd.steps > 0) {
        wd.steps = 0;
        step_head(d, wd.dir);
        if (c & 0x10) {
            wd.track += wd.dir;
        }
        schedule(now + wd.step_us);
        return;
    }
    if (wd.verify) {
        wd.verify = false;
        head_load(now);
        wd.kind = CMD_VERIFY;
        if (!ready()) {                 /* no disk, no index: a seek error after 5 turns */
            wd.phase = PH_SEARCH;
            wd.t0 = UINT64_MAX;
            schedule(now + SETTLE_US + SEARCH_INDEXES * (uint64_t)TURN_US);
            return;
        }
        begin_search(now + SETTLE_US);
        return;
    }
    finish(now, 0);
}

static void command(uint8_t v, uint64_t now)
{
    DLOG("fdc %10llu: command $%02X track %u sector %u data %u drive %d side %d PC %04X\n",
         (unsigned long long)now, v, wd.track, wd.sector, wd.data, wd.drive, wd.side, oric.cpu.pc);
    if ((v & 0xF0) == 0xD0) {           /* Force Interrupt */
        if (wd.busy) {
            wd.busy = false;
            wd.phase = PH_IDLE;
            wd.idle_since = now;
        } else {
            wd.type1 = true;
            wd.err = 0;
        }
        wd.command = v;
        wd.index_irq = (v & 0x04) != 0;
        fdc_event_at = wd.index_irq ? (now / TURN_US + 1) * TURN_US : UINT64_MAX;
        if (wd.index_irq) {
            schedule(fdc_event_at);
        }
        /* I3: an interrupt now. The data sheet has it held until a $D0
         * (status reads and commands not clearing it); the EPROM sends $D8
         * at boot and no $D0, and its first Read Sector would end at once
         * (its IRQ handler takes INTRQ for the command's end): cleared as
         * any other (CLK)
         */
        set_intrq((v & 0x08) != 0);
        return;
    }
    if (wd.busy) {
        return;                         /* only Force Interrupt while busy */
    }
    set_intrq(false);
    wd.command = v;
    wd.busy = true;
    wd.err = 0;
    wd.done = 0;
    if (!(v & 0x80)) {                  /* Type I */
        wd.type1 = true;
        if (v & 0x08) {
            head_load(now);
        } else {
            wd.hld = false;
        }
        wd.verify = (v & 0x04) != 0;
        static const uint32_t step_us[4] = { 6000, 12000, 20000, 30000 };   /* r1 r0 at 1 MHz */
        wd.step_us = step_us[v & 3];
        wd.phase = PH_STEP;
        wd.steps = 1;
        switch (v & 0xE0) {
        case 0x00:
            if (!(v & 0x10)) {          /* restore */
                wd.track = 0xFF;
                wd.dir = -1;
                wd.steps = 255;
            } else {                    /* seek */
                wd.dir = wd.data > wd.track ? 1 : -1;
            }
            break;
        case 0x20:                      /* step: as the last one */
            break;
        case 0x40:
            wd.dir = 1;
            break;
        default:
            wd.dir = -1;
            break;
        }
        schedule(now);
        return;
    }
    wd.type1 = false;
    if (!ready()) {
        finish(now, ST_NOT_READY);
        return;
    }
    head_load(now);
    uint64_t t = now + ((v & 0x04) ? SETTLE_US : 0);
    switch (v & 0xF0) {
    case 0x80:
    case 0x90:
        wd.kind = CMD_READ;
        begin_search(t);
        return;
    case 0xA0:
    case 0xB0:
        if (cur()->protect) {
            finish(now, ST_PROTECT);
            return;
        }
        wd.kind = CMD_WRITE;
        wd.mark = (v & 0x01) ? 0xF8 : 0xFB;
        begin_search(t);
        return;
    case 0xC0:
        wd.kind = CMD_ADDRESS;
        begin_search(t);
        return;
    case 0xE0: {                        /* read track: from the next index */
        wd.kind = CMD_READ_TRACK;
        if (t < wd.hld_at + HEAD_LOAD_US) {
            t = wd.hld_at + HEAD_LOAD_US;
        }
        uint64_t idx = (t / TURN_US + 1) * TURN_US;
        const uint8_t *trk = cur_track();
        if (trk == NULL) {
            memset(wd.buf, 0x4E, TRACK_BYTES);
            trk = wd.buf;
        }
        wd.src = trk;
        wd.len = TRACK_BYTES;
        wd.t0 = idx + BYTE_US;
        wd.t_end = wd.t0 + (uint64_t)(TRACK_BYTES - 1) * BYTE_US;
        wd.phase = PH_READ;
        schedule(wd.t_end);
        return;
    }
    case 0xF0: {                        /* write track: from the next index */
        if (cur()->protect) {
            finish(now, ST_PROTECT);
            return;
        }
        wd.kind = CMD_WRITE_TRACK;
        if (t < wd.hld_at + HEAD_LOAD_US) {
            t = wd.hld_at + HEAD_LOAD_US;
        }
        uint64_t idx = (t / TURN_US + 1) * TURN_US;
        wd.len = TRACK_BYTES;
        wd.done = 0;
        wd.f5 = false;
        wd.crc = 0xFFFF;
        wd.t0 = idx;
        wd.t_end = idx + TURN_US;
        wd.phase = PH_WTRACK_WAIT;
        schedule(idx);
        return;
    }
    default:                            /* $D0 above */
        finish(now, 0);
        return;
    }
}

/* ---- Time ---- */

void fdc_update(uint64_t now)
{
    while (now >= fdc_event_at) {
        uint64_t t = fdc_event_at;
        fdc_event_at = UINT64_MAX;
        if (!wd.busy) {
            if (wd.index_irq) {         /* I2: an interrupt every index pulse */
                set_intrq(true);
                fdc_event_at = (t / TURN_US + 1) * TURN_US;
            }
            continue;
        }
        switch (wd.phase) {
        case PH_STEP:
            type1_event(t);
            break;
        case PH_SEARCH: {
            uint64_t at;
            int i = wd.t0 == UINT64_MAX ? -1 : search(wd.t0, &at);
            if (i < 0) {
                finish(t, wd.kind == CMD_VERIFY ? ST_SEEK : (uint8_t)(ST_RNF | (wd.crc_seen ? ST_CRC : 0)));
            } else if (wd.kind == CMD_VERIFY) {
                finish(t, 0);
            } else {
                found(i, at);
            }
            break;
        }
        case PH_READ:
            read_done(t);
            break;
        case PH_WRITE_GATE:
            if (wd.done == 0) {         /* the data register not loaded in time */
                finish(t, ST_LOST);
            } else {
                wd.phase = PH_WRITE;
                schedule(wd.t_end);
            }
            break;
        case PH_WRITE:
            write_done(t);
            break;
        case PH_WTRACK_WAIT:
            if (wd.done == 0) {
                finish(t, ST_LOST);
            } else {
                uint8_t first = wd.buf[0];  /* the byte loaded before the index */
                wd.done = 0;
                wtrack_byte(first);
                wd.phase = PH_WTRACK;
                schedule(wd.t_end);
            }
            break;
        case PH_WTRACK:
            wtrack_done(t);
            break;
        default:
            break;
        }
    }
}

/* ---- The registers ---- */

uint8_t fdc_read(int reg)
{
    uint64_t now = oric.cpu.cycles;
    fdc_update(now);
    switch (reg & 3) {
    case 0: {
        uint8_t s = wd.err | (wd.busy ? ST_BUSY : 0) | (ready() ? 0 : ST_NOT_READY);
        if (wd.type1) {
            s &= ~(ST_INDEX | ST_TRACK0 | ST_HEAD | ST_PROTECT);
            s |= (head_engaged(now) ? ST_HEAD : 0) | (cur()->cyl == 0 ? ST_TRACK0 : 0) |
                 (index_pulse(now) ? ST_INDEX : 0) | (cur()->protect ? ST_PROTECT : 0);
        } else {
            s = (uint8_t)((s & ~ST_DRQ) | (drq(now) ? ST_DRQ : 0));
        }
        set_intrq(false);
        wd.status = s;
        return s;
    }
    case 1:
        return wd.track;
    case 2:
        return wd.sector;
    default:
        if (wd.busy && wd.phase == PH_READ) {
            int n = arrived(now);
            if (n > wd.done) {
                if (n - wd.done > 1) {
                    wd.err |= ST_LOST;
                }
                wd.data = wd.src[n - 1];
                wd.done = n;
            }
        }
        return wd.data;
    }
}

void fdc_write(int reg, uint8_t v)
{
    uint64_t now = oric.cpu.cycles;
    fdc_update(now);
    switch (reg & 3) {
    case 0:
        command(v, now);
        break;
    case 1:
        wd.track = v;
        break;
    case 2:
        wd.sector = v;
        break;
    default:
        wd.data = v;
        if (!wd.busy) {
            break;
        }
        switch (wd.phase) {
        case PH_WRITE_GATE:
        case PH_WRITE: {
            int k = wd.phase == PH_WRITE ? taken(now) : 0;
            if (wd.done < k) {          /* bytes went out as zeros meanwhile */
                memset(wd.buf + wd.done, 0, k - wd.done);
                wd.done = k;
                wd.err |= ST_LOST;
            }
            if (wd.done < wd.len) {
                wd.buf[wd.done++] = v;
            }
            break;
        }
        case PH_WTRACK_WAIT:
            if (wd.done == 0) {
                wd.buf[0] = v;          /* written out at the index */
                wd.done = 1;
            }
            break;
        case PH_WTRACK: {
            int k = taken(now);
            if (wd.done < k) {
                while (wd.done < k) {
                    wd.buf[wd.done++] = 0x4E;
                }
                wd.err |= ST_LOST;
            }
            wtrack_byte(v);
            break;
        }
        default:
            break;
        }
        break;
    }
}

bool fdc_intrq(void)
{
    return wd.intrq;
}

bool fdc_drq(void)
{
    uint64_t now = oric.cpu.cycles;
    fdc_update(now);
    return drq(now);
}

/* The control register's drive, side (and density: MFM only, not checked) */
void fdc_select(int drive, int side)
{
    if (drive != wd.drive || side != wd.side) {
        wd.drive = drive;
        wd.side = side;
        s_trk.raw = NULL;
    }
}

/* The head unloads after 15 turns with nothing to do */
void fdc_frame_end(void)
{
    uint64_t now = oric.cpu.cycles;
    if (wd.hld && !wd.busy && now >= wd.idle_since + (uint64_t)UNLOAD_TURNS * TURN_US) {
        wd.hld = false;
    }
}

/* The tables in PSRAM (the MaTouch's internal RAM is short); the API's
 * calls before the machine starts make them too
 */
bool fdc_init(void)
{
    if (s_drive == NULL) {
        s_drive = ORIC_SLOW_ALLOC(ORIC_DRIVES * sizeof(drive_t));
    }
    if (s_trk.id == NULL) {
        s_trk.id = ORIC_SLOW_ALLOC(MAX_IDS * sizeof(sector_id_t));
    }
    if (wd.buf == NULL) {
        wd.buf = ORIC_SLOW_ALLOC(TRACK_STRIDE);
    }
    return s_drive != NULL && s_trk.id != NULL && wd.buf != NULL;
}

void fdc_reset(void)
{
    uint8_t *buf = wd.buf;
    memset(&wd, 0, sizeof(wd));
    wd.buf = buf;
    wd.type1 = true;
    wd.dir = 1;
    fdc_event_at = UINT64_MAX;
    s_trk.raw = NULL;
    for (int i = 0; s_drive != NULL && i < ORIC_DRIVES; i++) {
        s_drive[i].cyl = 0;             /* (the drives keep theirs on a real one: Restore finds 0) */
    }
}

/* ---- Images: the API ---- */

bool oric_disk_insert(int drive, const char *path)
{
    if (drive < 0 || drive >= ORIC_DRIVES || !fdc_init()) {
        return false;
    }
    oric_disk_eject(drive);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    uint8_t h[HEADER];
    if (fread(h, 1, HEADER, f) != HEADER || memcmp(h, "MFM_DISK", 8) != 0) {
        fclose(f);
        ORIC_LOGW("disk: %s is not an MFM_DISK image", path);
        return false;
    }
    int sides = h[8] | (h[9] << 8) | (h[10] << 16) | (h[11] << 24);
    int tracks = h[12] | (h[13] << 8) | (h[14] << 16) | (h[15] << 24);
    int geometry = h[16] | (h[17] << 8) | (h[18] << 16) | (h[19] << 24);
    uint32_t size = HEADER + (uint32_t)TRACK_STRIDE * (uint32_t)(sides * tracks);
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    if (sides < 1 || sides > 2 || tracks < 1 || tracks > MAX_TRACKS || (geometry != 1 && geometry != 2) ||
        file_size < (long)size) {
        fclose(f);
        ORIC_LOGW("disk: %s: %d sides, %d tracks, geometry %d, %ld bytes: not taken", path, sides, tracks,
                  geometry, file_size);
        return false;
    }
    uint8_t *image = ORIC_SLOW_ALLOC(size);
    if (image == NULL) {
        fclose(f);
        return false;
    }
    fseek(f, 0, SEEK_SET);
    bool ok = fread(image, 1, size, f) == size;
    fclose(f);
    if (!ok) {
        free(image);
        return false;
    }
    drive_t *d = &s_drive[drive];
    snprintf(d->path, sizeof(d->path), "%s", path);
    d->image = image;
    d->size = size;
    d->sides = sides;
    d->tracks = tracks;
    d->geometry = geometry;
    FILE *w = fopen(path, "r+b");
    d->protect = w == NULL;
    if (w) {
        fclose(w);
    }
    d->inserted = true;
    d->dirty = false;
    memset(d->dirty_track, 0, sizeof(d->dirty_track));
    d->reads = d->writes = 0;
    s_trk.raw = NULL;
    ORIC_LOGI("disk: %c: %s, %d side%s of %d tracks%s", 'A' + drive, path, sides, sides > 1 ? "s" : "", tracks,
              d->protect ? ", write protected" : "");
    return true;
}

static bool flush_drive(drive_t *d)
{
    if (!d->dirty) {
        return true;
    }
    FILE *f = fopen(d->path, "r+b");
    if (f == NULL) {
        ORIC_LOGW("disk: %s couldn't be written", d->path);
        return false;
    }
    bool ok = true;
    for (int n = 0; n < d->sides * d->tracks; n++) {
        if (!d->dirty_track[n]) {
            continue;
        }
        uint32_t off = HEADER + (uint32_t)n * TRACK_STRIDE;
        ok &= fseek(f, (long)off, SEEK_SET) == 0 && fwrite(d->image + off, 1, TRACK_STRIDE, f) == TRACK_STRIDE;
        d->dirty_track[n] = 0;
    }
    ok &= fclose(f) == 0;
    d->dirty = false;
    if (!ok) {
        ORIC_LOGW("disk: writing %s failed", d->path);
    }
    return ok;
}

bool oric_disk_dirty(void)
{
    if (s_drive == NULL) {
        return false;
    }
    for (int i = 0; i < ORIC_DRIVES; i++) {
        if (s_drive[i].dirty) {
            return true;
        }
    }
    return false;
}

void oric_disk_flush(void)
{
    if (s_drive == NULL) {
        return;
    }
    for (int i = 0; i < ORIC_DRIVES; i++) {
        flush_drive(&s_drive[i]);
    }
}

void oric_disk_eject(int drive)
{
    if (drive < 0 || drive >= ORIC_DRIVES || s_drive == NULL) {
        return;
    }
    drive_t *d = &s_drive[drive];
    if (d->inserted) {
        flush_drive(d);
        free(d->image);
    }
    int cyl = d->cyl;
    memset(d, 0, sizeof(*d));
    d->cyl = cyl;
    s_trk.raw = NULL;
}

const char *oric_disk_path(int drive)
{
    if (drive < 0 || drive >= ORIC_DRIVES || s_drive == NULL || !s_drive[drive].inserted) {
        return NULL;
    }
    return s_drive[drive].path;
}

bool oric_disk_protected(int drive)
{
    return drive >= 0 && drive < ORIC_DRIVES && s_drive != NULL && s_drive[drive].inserted && s_drive[drive].protect;
}

/* A blank disk: no track formatted (Sedoric's INIT, Oric DOS's FORMAT) */
bool oric_disk_create(const char *path, int sides, int tracks)
{
    if (sides < 1 || sides > 2 || tracks < 1 || tracks > MAX_TRACKS) {
        return false;
    }
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    uint8_t *block = calloc(1, TRACK_STRIDE);
    bool ok = block != NULL;
    if (ok) {
        uint8_t h[HEADER] = "MFM_DISK";
        memset(h + 8, 0, HEADER - 8);
        h[8] = (uint8_t)sides;
        h[12] = (uint8_t)tracks;
        h[16] = 1;
        ok = fwrite(h, 1, HEADER, f) == HEADER;
        for (int n = 0; ok && n < sides * tracks; n++) {
            ok = fwrite(block, 1, TRACK_STRIDE, f) == TRACK_STRIDE;
        }
        free(block);
    }
    ok &= fclose(f) == 0;
    if (!ok) {
        remove(path);
    }
    return ok;
}

void oric_disk_status(int drive, oric_disk_status_t *s)
{
    memset(s, 0, sizeof(*s));
    if (drive < 0 || drive >= ORIC_DRIVES || s_drive == NULL) {
        return;
    }
    const drive_t *d = &s_drive[drive];
    s->inserted = d->inserted;
    s->protect = d->protect;
    s->sides = d->sides;
    s->tracks = d->tracks;
    s->cyl = d->cyl;
    s->selected = drive == wd.drive;
    s->side = wd.side;
    s->motor = wd.hld;
    s->reads = d->reads;
    s->writes = d->writes;
    s->dirty = d->dirty;
}

void oric_fdc_status(oric_fdc_status_t *s)
{
    s->busy = wd.busy;
    s->command = wd.command;
    s->status = wd.status;
    s->track = wd.track;
    s->sector = wd.sector;
    s->data = wd.data;
    s->intrq = wd.intrq;
    s->motor = wd.hld;
    s->drive = wd.drive;
    s->side = wd.side;
    s->sectors_read = wd.sectors_read;
    s->sectors_written = wd.sectors_written;
}
