/*
 * Oric Atmos emulator on the Makerfabs MaTouch ESP32-S3 7" and the M5Stack
 * Tab5.
 *
 * The board side (tasks, LCD, touch, WiFi, Bluetooth, setup screen, web
 * page, console) is the shared runtime in ../esp32emu; this file plugs the
 * Oric into it: memory, ROMs, settings, keyboard map, status.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "emu/emu.h"
#include "oric.h"
#include "keymap.h"
#include "rom_store.h"
#include "settings.h"

static const char *TAG = "main";

static const char *const model_names[] = { "Atmos", "Oric-1", "" };
static const char *const model_full[] = { "Oric Atmos (BASIC 1.1)", "Oric-1 (BASIC 1.0)" };

static oric_config_t s_cfg;
static uint8_t *s_rom, *s_disk_rom;

/* ---- Memory: everything at power-on, before the screen, WiFi and
 * Bluetooth fragment PSRAM
 */
static void reserve_memory(void)
{
    settings_load();
    s_rom = heap_caps_malloc(ORIC_ROM_SIZE, MALLOC_CAP_SPIRAM);
    s_disk_rom = heap_caps_malloc(ORIC_DISK_ROM_SIZE, MALLOC_CAP_SPIRAM);
    s_cfg.ram = heap_caps_malloc(ORIC_RAM_SIZE, MALLOC_CAP_SPIRAM);
}

static void storage(bool sd_card)
{
    emu_set_drive_info(sd_card ? "Tapes (.TAP) and disks (.DSK) on the SD card" : "No SD card: no tapes or disks");
}

/* ---- The drives: the tape and the Microdisc's A and B, their images in
 * the SD card's oric/ folder, listed, uploaded and downloaded by the web
 * page. The core plays the tape (tape.c) and reads and writes the disks
 * (wd1793.c). A blank tape is an empty .TAP: CSAVE puts its files on the
 * end of any; a blank disk is an MFM_DISK with no track formatted
 * (Sedoric's INIT formats it). Drive 1 of the web API is still the tape.
 */

enum { DRIVE_TAPE, DRIVE_A, DRIVE_B, DRIVES };

#define BLANK_TAPE      1               /* the "size" of a blank tape */
#define DSK_BYTES(t)    (256 + 6400 * 2 * (t))  /* two sides of t tracks */

static bool has_ext(const char *name, const char *ext)
{
    size_t n = strlen(name), e = strlen(ext);
    return n >= e && strcasecmp(name + n - e, ext) == 0;
}

static bool accepts(int drive, const char *name)
{
    return drive == DRIVE_TAPE ? !has_ext(name, ".dsk") : has_ext(name, ".dsk");
}

static bool drive_insert(int drive, const char *path)
{
    if (!accepts(drive, path)) {
        return false;
    }
    return drive == DRIVE_TAPE ? oric_tape_insert(path) : oric_disk_insert(drive - DRIVE_A, path);
}

static void drive_eject(int drive)
{
    if (drive == DRIVE_TAPE) {
        oric_tape_eject();
    } else {
        oric_disk_eject(drive - DRIVE_A);
    }
}

static const char *drive_path(int drive)
{
    return drive == DRIVE_TAPE ? oric_tape_path() : oric_disk_path(drive - DRIVE_A);
}

static bool drive_write_protected(int drive)
{
    return drive != DRIVE_TAPE && oric_disk_protected(drive - DRIVE_A);
}

static bool drive_create(const char *path, uint32_t bytes)
{
    if (bytes == BLANK_TAPE) {
        return oric_tape_create(path);
    }
    return oric_disk_create(path, 2, (int)((bytes - 256) / (6400 * 2)));
}

static uint32_t default_size(int drive)
{
    return drive == DRIVE_TAPE ? BLANK_TAPE : DSK_BYTES(42);
}

static const char *extension_for(uint32_t bytes)
{
    return bytes == BLANK_TAPE ? ".tap" : ".dsk";
}

/* A disk in A or B while the Microdisc is off: offered with a restart */
static const char *offer(int drive)
{
    if (drive == DRIVE_TAPE || settings.microdisc || oric_disk_path(drive - DRIVE_A) == NULL) {
        return NULL;
    }
    return "Disks want the Microdisc: switch it on and restart the Oric (its EPROM boots the disk in A)";
}

static void take_offer(int drive)
{
    settings.microdisc = 1;
    settings_save();
}

static const char *const extensions[] = { ".tap", ".dsk", NULL };
static const char *const drive_names[] = { "Tape", "A:", "B:" };
static const disks_size_t sizes[] = {
    { DSK_BYTES(42), "Blank disk, 2 sides of 42 tracks (.DSK)" },
    { DSK_BYTES(80), "Blank disk, 2 sides of 80 tracks (.DSK)" },
    { BLANK_TAPE, "Blank tape (.TAP)" },
};

static const disks_config_t disks = {
    .folder = "oric",
    .extensions = extensions,
    .description = "Tapes (.TAP) and disks (.DSK)",
    .title = "Tapes and disks",
    .button = "Media",
    .nvs_namespace = "oricdisks",
    .drives = DRIVES,
    .drive_names = drive_names,
    .new_prefix = "BLANK",
    .new_extension = ".dsk",
    .sizes = sizes,
    .size_count = sizeof(sizes) / sizeof(sizes[0]),
    .max_upload = 4 * 1024 * 1024,
    .insert = drive_insert,
    .eject = drive_eject,
    .path = drive_path,
    .write_protected = drive_write_protected,
    .create = drive_create,
    .accepts = accepts,
    .default_size = default_size,
    .extension_for = extension_for,
    .offer = offer,
    .take_offer = take_offer,
};

/* Disk writes go to the SD card between frames, once nothing has been
 * written for two seconds (the core keeps the changed tracks till then)
 */
static void flush(void *arg)
{
    oric_disk_flush();
}

static void every_second(void)
{
    static int dirty_for;
    if (oric_disk_dirty()) {
        if (++dirty_for >= 2 && emu_paused(flush, NULL)) {
            dirty_for = 0;
        }
    } else {
        dirty_for = 0;
    }
}

/* ---- Starting ---- */

static bool start(void)
{
    if (s_rom == NULL || s_cfg.ram == NULL) {
        emu_set_message("Not enough memory: see the log.");
        return false;
    }
    rom_id_t rom = settings.model ? ROM_BASIC10 : ROM_BASIC11;
    if (!rom_store_load(rom, s_rom)) {
        static char message[160];
        snprintf(message, sizeof(message), "No ROM: put %s into roms/ and flash with idf.py flash.",
                 rom_store_name(rom));
        emu_set_message(message);
        return false;
    }
    s_cfg.model = settings.model ? ORIC_1 : ORIC_ATMOS;
    s_cfg.rom = s_rom;
    s_cfg.disk_rom = NULL;
    if (settings.microdisc) {
        if (s_disk_rom == NULL || !rom_store_load(ROM_MICRODISC, s_disk_rom)) {
            emu_set_message("No Microdisc EPROM: put microdis.rom into roms/ and flash with idf.py flash.");
            return false;
        }
        s_cfg.disk_rom = s_disk_rom;
    }
    oric_tape_fast(settings.tape_fast);
    if (!oric_init(&s_cfg)) {
        emu_set_message("The Oric could not be set up: see the log.");
        return false;
    }
    ESP_LOGI(TAG, "%s%s", model_full[settings.model], settings.microdisc ? " with a Microdisc" : "");
    return true;
}

static void screen_mode(emu_screen_t *mode)
{
    mode->width = ORIC_SCREEN_WIDTH;
    mode->height = ORIC_SCREEN_HEIGHT;
    mode->hz = ORIC_SCREEN_HZ;
}

/* ---- Setup screen choices ---- */

static int model_get(void)
{
    return settings.model;
}

static void model_set(int i)
{
    if (i >= 0 && i <= 1) {
        settings.model = i;
        settings_save();
    }
}

static const char *const tape_names[] = { "Fast", "Real time", "" };

static int tape_get(void)
{
    return settings.tape_fast ? 0 : 1;
}

static void tape_set(int i)
{
    settings.tape_fast = i == 0;
    settings_save();
    oric_tape_fast(settings.tape_fast);
}

static const char *const microdisc_names[] = { "Off", "On", "" };

static int microdisc_get(void)
{
    return settings.microdisc;
}

static void microdisc_set(int i)
{
    if (i >= 0 && i <= 1) {
        settings.microdisc = i;
        settings_save();
    }
}

static const emu_choice_t choices[] = {
    { "Model", model_names, model_get, model_set, false },
    { "Tape", tape_names, tape_get, tape_set, true },
    { "Microdisc", microdisc_names, microdisc_get, microdisc_set, false },
};

/* ---- Status ----
 *
 * The web page's machine card: the state now, a row each, short (the whole
 * /api/status reply has 8 KB, esp32emu's STATUS_SIZE; a " goes out as \").
 * status() runs on the HTTP task: it reads what the core keeps.
 */

static const char *const colour_names[8] = { "black", "red", "green", "yellow", "blue", "magenta", "cyan",
                                             "white" };

/* The ULA's colours: bit 0 red, 1 green, 2 blue, full or nothing */
static size_t swatch(char *text, size_t size, unsigned c)
{
    unsigned long rgb = ((c & 1) ? 0xFF0000ul : 0) | ((c & 2) ? 0x00FF00ul : 0) | ((c & 4) ? 0x0000FFul : 0);
    int n = snprintf(text, size, "<b style=\"background:#%06lx\" title=\"%s\">&nbsp;&nbsp;&nbsp;</b> ", rgb,
                     colour_names[c]);
    return n < 0 ? 0 : (size_t)n < size ? (size_t)n : size - 1;
}

/* An AY channel: its tone's frequency and its level (or the envelope) */
static int channel_text(char *buf, size_t len, const oric_status_t *s, int ch)
{
    unsigned period = s->ay[2 * ch] | ((s->ay[2 * ch + 1] & 0x0F) << 8);
    bool tone = !(s->ay[7] & (1 << ch)), noise = !(s->ay[7] & (8 << ch));
    uint8_t vol = s->ay[8 + ch];
    if (!(vol & 0x10) && (vol & 0x0F) == 0) {
        return snprintf(buf, len, "%c silent", 'A' + ch);
    }
    char level[24];
    if (vol & 0x10) {
        snprintf(level, sizeof(level), "envelope");
    } else {
        snprintf(level, sizeof(level), "volume %u", vol & 0x0F);
    }
    if (tone) {                         /* 1 MHz / 16 / period */
        return snprintf(buf, len, "%c %lu Hz%s, %s", 'A' + ch, 62500ul / (period ? period : 1),
                        noise ? " + noise" : "", level);
    }
    return snprintf(buf, len, "%c %s, %s", 'A' + ch, noise ? "noise" : "no tone", level);
}

/* The AY: its channels, the noise and envelope when used, port A (the
 * keyboard's columns, low a column scanned)
 */
static void sound_text(char *text, size_t len, const oric_status_t *s)
{
    size_t n = 0;
    bool noise = false, envelope = false;
    for (int ch = 0; ch < 3 && n < len; ch++) {
        n += (size_t)snprintf(text + n, len - n, "%s", ch ? "; " : "");
        if (n < len) {
            n += (size_t)channel_text(text + n, len - n, s, ch);
        }
        bool on = (s->ay[8 + ch] & 0x10) || (s->ay[8 + ch] & 0x0F);
        noise |= on && !(s->ay[7] & (8 << ch));
        envelope |= (s->ay[8 + ch] & 0x10) != 0;
    }
    if (noise && n < len) {             /* 1 MHz / 16 / period */
        unsigned p = s->ay[6] & 31;
        n += (size_t)snprintf(text + n, len - n, "; noise %lu Hz", 62500ul / (p ? p : 1));
    }
    if (envelope && n < len) {          /* a sweep: 256 x its period, at 1 MHz */
        unsigned p = s->ay[11] | (s->ay[12] << 8);
        n += (size_t)snprintf(text + n, len - n, "; envelope shape %u, %lu ms", s->ay[13] & 15,
                              (256ul * (p ? p : 1) + 500) / 1000);
    }
    if (n < len) {
        snprintf(text + n, len - n, "; port A #%02X (the keyboard's columns)", s->ay[14]);
    }
}

/* The keys by matrix position (oric.h's ORIC_KEY_*) */
static const char *const key_names[ORIC_KEYS] = {
    "7", "N", "5", "V", NULL, "1", "X", "3",
    "J", "T", "R", "F", NULL, "ESC", "Q", "D",
    "M", "6", "B", "4", "CTRL", "Z", "2", "C",
    "K", "9", ";", "-", NULL, NULL, "\\", "'",
    "SPACE", ",", ".", "↑", "LEFT SHIFT", "←", "↓", "→",
    "U", "I", "O", "P", "FUNCT", "DEL", "]", "[",
    "Y", "H", "G", "E", NULL, "A", "S", "W",
    "8", "L", "0", "/", "RIGHT SHIFT", "RETURN", NULL, "=",
};

static void keys_text(char *text, size_t len)
{
    uint8_t rows[8];
    oric_keys_down(rows);
    size_t n = 0;
    for (int k = 0; k < ORIC_KEYS && n < len; k++) {
        if ((rows[k / 8] & (1 << (k % 8))) && key_names[k]) {
            n += (size_t)snprintf(text + n, len - n, "%s%s", n ? " " : "", key_names[k]);
        }
    }
    if (n == 0) {
        snprintf(text, len, "none");
    }
}

/* The VIA: its timers, interrupts and ports as the Oric wires them */
static void via_text(char *text, size_t len, const oric_status_t *s)
{
    static const char *const flags[7] = { "CA2", "CA1", "SR", "CB2", "CB1", "T2", "T1" };
    char ier[48] = "", ifr[48] = "";
    for (int b = 6, ni = 0, nf = 0; b >= 0; b--) {
        if (s->via.ier & (1 << b)) {
            ni += snprintf(ier + ni, sizeof(ier) - ni, "%s%s", ni ? " " : "", flags[b]);
        }
        if (s->via.ifr & (1 << b)) {
            nf += snprintf(ifr + nf, sizeof(ifr) - nf, "%s%s", nf ? " " : "", flags[b]);
        }
    }
    /* the AY's bus: BC1 on CA2, BDIR on CB2 (PCR's manual outputs) */
    bool bc1 = ((s->via.pcr >> 1) & 7) == 7, bdir = ((s->via.pcr >> 5) & 7) == 7;
    static const char *const bus[4] = { "idle", "read", "write", "address" };
    uint8_t pb = s->via.orb | ~s->via.ddrb;
    snprintf(text, len, "T1 every %u µs, %s; T2 %s; interrupts on: %s, pending: %s; "
             "port A #%02X, the AY %s; port B #%02X: keyboard row %u",
             s->timer1 + 2, (s->via.acr & 0x40) ? "free running" : "one-shot",
             (s->via.acr & 0x20) ? "counting PB6" : "one-shot", ier[0] ? ier : "none", ifr[0] ? ifr : "none",
             s->via.ora, bus[bdir * 2 + bc1], pb, pb & 7);
}

/* Where the tape is: the file at the head, or its end */
static void tape_text(char *text, size_t len, const oric_tape_status_t *t)
{
    static const char *const how[] = { "BASIC", "machine code", "an array" };
    if (t->file >= t->files) {
        snprintf(text, len, "At its end: %d file%s, %lu bytes", t->files, t->files == 1 ? "" : "s",
                 (unsigned long)t->size);
        return;
    }
    const char *type = t->type == 0x00 ? how[0] : t->type == 0x80 ? how[1] : t->type == 0x40 ? how[2] : "type ?";
    snprintf(text, len, "File %d of %d: \"%s\", %s at #%04X-#%04X%s", t->file + 1, t->files, t->name, type,
             t->start, t->last, t->autorun ? ", AUTO" : "");
}

/* A disk: its size, where the head is */
static void disk_text(char *text, size_t len, const oric_disk_status_t *d, bool microdisc)
{
    int n = snprintf(text, len, "%d side%s of %d tracks%s; the head on track %d", d->sides, d->sides > 1 ? "s" : "",
                     d->tracks, d->protect ? ", write protected" : "", d->cyl);
    if (d->selected && microdisc) {
        snprintf(text + n, len - n, ", side %d%s", d->side, d->motor ? ", turning" : "");
    }
    if (!microdisc) {
        snprintf(text + n, len - n, " (the Microdisc is off)");
    }
}

/* A Sedoric disk's free space: its bitmap (track 20, sector 2): $FF, 0,
 * the free sectors, the files, tracks, sectors a track, ..., the tracks
 * again with $80 for two sides. Other DOSes: nothing (size 0)
 */
static void disk_space(int drive, uint64_t *free_bytes, uint64_t *size)
{
    *free_bytes = *size = 0;
    uint8_t b[256];
    if (!oric_disk_sector(drive, 0, 20, 2, b)) {
        return;
    }
    unsigned tracks = b[6], sectors = b[7], sides = (b[9] & 0x80) ? 2 : 1;
    if (b[0] != 0xFF || b[1] != 0 || tracks == 0 || tracks != (b[9] & 0x7Fu) || sectors < 10 || sectors > 19) {
        return;
    }
    *size = (uint64_t)sides * tracks * sectors * 256;
    *free_bytes = (uint64_t)(b[2] | (b[3] << 8)) * 256;
    if (*free_bytes > *size) {
        *free_bytes = *size = 0;
    }
}

/* The WD1793's last command and its status bits (Type I's or the rest's) */
static void microdisc_text(char *text, size_t len, uint8_t control, const oric_fdc_status_t *f)
{
    static const char *const commands[16] = {
        "Restore", "Seek", "Step", "Step", "Step in", "Step in", "Step out", "Step out",
        "Read Sector", "Read Sector", "Write Sector", "Write Sector", "Read Address", "Force Interrupt",
        "Read Track", "Write Track",
    };
    static const char *const type1[8] = { "busy", "index", "track 0", "CRC error", "seek error", "head loaded",
                                          "protected", "not ready" };
    static const char *const type2[8] = { "busy", "DRQ", "lost data", "CRC error", "not found", "deleted mark",
                                          "protected", "not ready" };
    bool t1 = f->command < 0x80 || (f->command & 0xF0) == 0xD0;
    char bits[96] = "";
    for (int b = 7, n = 0; b >= 0; b--) {
        if (f->status & (1 << b)) {
            n += snprintf(bits + n, sizeof(bits) - n, "%s%s", n ? ", " : "", t1 ? type1[b] : type2[b]);
        }
    }
    const char *paged = (control & 0x02) ? "BASIC" : (control & 0x80) ? "overlay RAM" : "overlay RAM, the EPROM at #E000";
    snprintf(text, len, "WD1793 %s %s (#%02X), track %u, sector %u, status #%02X (%s); drive %c side %d%s%s; "
             "#C000-#FFFF: %s; %lu sectors read, %lu written",
             f->busy ? "busy with" : "done with", commands[f->command >> 4], f->command, f->track, f->sector,
             f->status, bits[0] ? bits : "clear", 'A' + f->drive, f->side, f->motor ? ", motor on" : "",
             (control & 0x01) ? ", its IRQ on" : "", paged, (unsigned long)f->sectors_read,
             (unsigned long)f->sectors_written);
}

/* BASIC's pointers in zero page (1.0 and 1.1 alike): the program from $9A,
 * its variables from $9C, arrays $9E, their end $A0, strings down to $A2,
 * HIMEM $A6
 */
static void basic_text(char *text, size_t len, const oric_status_t *s)
{
    uint16_t p[7];
    for (int i = 0; i < 7; i++) {
        p[i] = oric_peek(0x9A + 2 * i) | (oric_peek(0x9B + 2 * i) << 8);
    }
    uint16_t prog = p[0], vars = p[1], arrays = p[2], end = p[3], strings = p[4], himem = p[6];
    if (!(prog >= 0x0400 && prog <= vars && vars <= arrays && arrays <= end && end <= strings &&
          strings <= himem && himem <= 0xC000)) {
        snprintf(text, len, "BASIC's pointers not set up%s", s->microdisc ? " (a DOS in charge?)" : "");
        return;
    }
    char program[48] = "no BASIC program";
    if (vars - prog > 2) {              /* (an empty one: its end's two zeros) */
        snprintf(program, sizeof(program), "BASIC's program #%04X-#%04X (%u bytes)", prog, vars - 1, vars - prog);
    }
    snprintf(text, len, "%s, variables %u bytes, arrays %u; %u bytes free below HIMEM #%04X", program,
             arrays - vars, end - arrays, strings - end, himem);
}

static void status(emu_json_t *j, int section)
{
    oric_status_t s;
    oric_get_status(&s);
    oric_tape_status_t t;
    oric_tape_status(&t);
    char text[300];
    if (section == EMU_STATUS_DRIVES) {
        /* the light: the cassette motor's relay (PB6) */
        if (t.inserted) {
            tape_text(text, sizeof(text), &t);
        }
        emu_json_drive(j, drive_names[DRIVE_TAPE], disks_name(DRIVE_TAPE), false, s.motor, t.loaded, t.saved,
                       t.inserted ? text : NULL);
        /* the disks' light: the controller's head load (it runs the motors) on the drive selected */
        for (int d = 0; d < 2; d++) {
            oric_disk_status_t ds;
            oric_disk_status(d, &ds);
            uint64_t free_bytes = 0, size = 0;
            if (ds.inserted) {
                disk_text(text, sizeof(text), &ds, s.microdisc);
                disk_space(d, &free_bytes, &size);
            }
            emu_json_drive_space(j, drive_names[DRIVE_A + d], disks_name(DRIVE_A + d), ds.protect,
                                 s.microdisc && ds.selected && ds.motor, ds.reads, ds.writes,
                                 ds.inserted ? text : NULL, free_bytes, size);
        }
        return;
    }
    const char *irq = (s.irq & 3) == 3 ? "the VIA and the Microdisc" : (s.irq & 1) ? "the VIA" :
                      (s.irq & 2) ? "the Microdisc" : "none";
    snprintf(text, sizeof(text), "%s: a 6502A at 1 MHz (IRQ asserted: %s%s), 64 KB RAM, the ULA, a 6522, "
             "an AY-3-8912%s", s.model == ORIC_1 ? "Oric-1" : "Oric Atmos", irq,
             (s.p & 0x04) ? "; interrupts masked" : "", s.microdisc ? ", the Microdisc" : "");
    emu_json_row(j, "System", text);

    const char *paged = !s.microdisc || (s.disk_control & 0x02) ? "BASIC" :
                        (s.disk_control & 0x80) ? "the overlay RAM" : "the overlay RAM, the EPROM at #E000";
    snprintf(text, sizeof(text), "%s%s; #C000-#FFFF now: %s",
             s.model == ORIC_1 ? "BASIC 1.0 (basic10.rom)" : "BASIC 1.1 (basic11b.rom)",
             s.microdisc ? ", the Microdisc's EPROM (microdis.rom)" : "", paged);
    emu_json_row(j, "ROM", text);

    int n = snprintf(text, sizeof(text), "RAM #0000-#BFFF, page 3 the VIA%s; ",
                     s.microdisc ? " (#0310-#031B the Microdisc)" : "");
    basic_text(text + n, sizeof(text) - n, &s);
    emu_json_row(j, "Memory", text);

    snprintf(text, sizeof(text), "%s, %s (%s); the picture 240 × 224",
             s.hires ? "HIRES: 240 × 200 at #A000 (characters #9800), three text rows below" :
             "TEXT: 40 × 28 at #BB80 (characters #B400, the alternate set #B800)",
             s.hz50 ? "50 Hz" : "60 Hz", s.hz50 ? "312 lines" : "264 lines");
    emu_json_row(j, "Screen", text);

    /* the colours the picture shows now, of the 8 */
    oric_screen_usage_t u;
    oric_screen_usage(&u);
    static char swatches[800];
    size_t m = (size_t)snprintf(swatches, sizeof(swatches), "%d of 8: ", __builtin_popcount(u.colours));
    for (unsigned c = 0; c < 8; c++) {
        if (u.colours & (1u << c)) {
            m += swatch(swatches + m, sizeof(swatches) - m, c);
        }
    }
    if (m < sizeof(swatches)) {
        snprintf(swatches + m, sizeof(swatches) - m, "%s%s%s", u.flashing ? "flashing" : "",
                 u.double_height ? (u.flashing ? ", double height" : "double height") : "",
                 u.alternate ? (u.flashing || u.double_height ? ", the alternate set" : "the alternate set") : "");
    }
    emu_json_row(j, "Colours", swatches);

    sound_text(text, sizeof(text), &s);
    emu_json_row(j, "Sound", text);
    via_text(text, sizeof(text), &s);
    emu_json_row(j, "VIA", text);

    char keys[120];
    keys_text(keys, sizeof(keys));
    snprintf(text, sizeof(text), "Down now: %s; CAPS %s (Caps Lock: CTRL-T); by position, Alt FUNCT, Tab and "
             "Esc ESC, F12 or Pause the reset button (NMI)", keys, s.caps ? "on" : "off");
    emu_json_row(j, "Keyboard", text);
    snprintf(text, sizeof(text), "Motor %s%s; %s%s", s.motor ? "on" : "off",
             t.playing ? (t.slow ? ", playing at 300 baud" : ", playing at 2400 baud") : "",
             !t.fast ? "loads in real time" : t.traps ? "CLOAD and CSAVE at once" : "real time (not BASIC 1.1)",
             t.write_failed ? "; a CSAVE couldn't be written to the card" : "");
    emu_json_row(j, "Cassette", text);
    if (s.microdisc) {
        oric_fdc_status_t f;
        oric_fdc_status(&f);
        microdisc_text(text, sizeof(text), s.disk_control, &f);
        emu_json_row(j, "Microdisc", text);
    } else {
        emu_json_row(j, "Expansions", "None (the Microdisc is off: the setup's Microdisc row)");
    }
}

/* ---- Console ---- */

static void console_info(char *buf, size_t len)
{
    oric_status_t s;
    oric_get_status(&s);
    oric_tape_status_t t;
    oric_tape_status(&t);
    oric_fdc_status_t f;
    oric_fdc_status(&f);
    snprintf(buf, len, "frames %lu PC %04X A %02X X %02X Y %02X S %02X P %02X %s %s tape %lu/%lu%s%s"
             " disk %s ctl %02X fdc %02X/%02X t%u s%u %c%d%s",
             (unsigned long)s.frames, s.pc, s.a, s.x, s.y, s.s, s.p, s.hires ? "hires" : "text",
             s.hz50 ? "50Hz" : "60Hz", (unsigned long)t.pos, (unsigned long)t.size, s.motor ? " motor" : "",
             t.playing ? " playing" : "", s.microdisc ? "on" : "off", s.disk_control, f.command, f.status,
             f.track, f.sector, 'A' + f.drive, f.side, f.busy ? " busy" : "");
}

static void rewind_tape(void *arg)
{
    oric_tape_rewind();
}

/*   k <n>              tap matrix key n (row * 8 + column)
 *   K <n> <0|1>        key n down (1) or up (0)
 *   T <text>           type text (\n is RETURN)
 *   N                  the button underneath (NMI)
 *   W                  rewind the tape
 */
static bool console_command(const char *line)
{
    switch (line[0]) {
    case 'k': {
        unsigned key = strtoul(line + 1, NULL, 10);
        oric_key(key, true);
        vTaskDelay(pdMS_TO_TICKS(100));
        oric_key(key, false);
        return true;
    }
    case 'K': {
        unsigned key = 0, down = 0;
        if (sscanf(line + 1, "%u %u", &key, &down) == 2) {
            oric_key(key, down);
        }
        return true;
    }
    case 'T': {
        static char text[200];
        const char *s = line + 1;
        while (*s == ' ') {
            s++;
        }
        size_t n = 0;
        for (; *s && n < sizeof(text) - 1; s++) {
            if (s[0] == '\\' && s[1] == 'n') {
                text[n++] = '\n';
                s++;
            } else {
                text[n++] = *s;
            }
        }
        text[n] = 0;
        oric_type(text);
        return true;
    }
    case 'N':
        oric_nmi();
        return true;
    case 'W':
        emu_paused(rewind_tape, NULL);
        return true;
    default:
        return false;
    }
}

/* ---- The machine ---- */

static const emu_machine_t machine = {
    .name = "Oric Atmos",
    .short_name = "Oric",
    .view_width = 800,                  /* 240 x 224 as on a TV: 1.33 wide to 1 high (960 x 720 on the Tab5) */
    .scale = EMU_SCALE_NEAREST,
    .reserve_memory = reserve_memory,
    .storage = storage,
    .start = start,
    .reset = oric_reset,
    .run_frame = oric_run_frame,
    .halted = oric_halted,
    .screen_mode = screen_mode,
    .screen_signature = oric_screen_signature,
    .render_line = oric_render_line_rgb565,
    .audio_hz = ORIC_AUDIO_HZ,
    .audio_enable = oric_audio_enable,
    .audio_render = oric_audio_render,
    .audio_pending = oric_audio_pending,
    .hid_key = keymap_hid_key,
    .choices = choices,
    .choice_count = sizeof(choices) / sizeof(choices[0]),
    .disks = &disks,
    .every_second = every_second,
    .status = status,
    .console_info = console_info,
    .console_command = console_command,
};

void app_main(void)
{
    emu_run(&machine);
}
