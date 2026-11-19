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
static uint8_t *s_rom;

/* ---- Memory: everything at power-on, before the screen, WiFi and
 * Bluetooth fragment PSRAM
 */
static void reserve_memory(void)
{
    settings_load();
    s_rom = heap_caps_malloc(ORIC_ROM_SIZE, MALLOC_CAP_SPIRAM);
    s_cfg.ram = heap_caps_malloc(ORIC_RAM_SIZE, MALLOC_CAP_SPIRAM);
}

static void storage(bool sd_card)
{
    emu_set_drive_info(sd_card ? "Tapes (.TAP) on the SD card" : "No SD card: no tapes");
}

/* ---- The tape drive: TAP files in the SD card's oric/ folder, listed,
 * uploaded and downloaded by the web page; the core plays them (tape.c).
 * A blank tape is an empty .TAP: CSAVE puts its files on the end of any.
 */

#define BLANK_TAPE  1                   /* the "size" of a blank tape */

static bool tape_insert(int drive, const char *path)
{
    return oric_tape_insert(path);
}

static void tape_eject(int drive)
{
    oric_tape_eject();
}

static const char *tape_path(int drive)
{
    return oric_tape_path();
}

static bool tape_write_protected(int drive)
{
    return false;
}

static bool tape_create(const char *path, uint32_t bytes)
{
    return oric_tape_create(path);
}

static const char *const extensions[] = { ".tap", NULL };
static const char *const drive_names[] = { "Tape" };
static const disks_size_t sizes[] = { { BLANK_TAPE, "Blank tape (.TAP)" } };

static const disks_config_t disks = {
    .folder = "oric",
    .extensions = extensions,
    .description = "Tapes (.TAP)",
    .title = "Tapes",
    .button = "Tapes",
    .nvs_namespace = "oricdisks",
    .drives = 1,
    .drive_names = drive_names,
    .new_prefix = "TAPE",
    .new_extension = ".tap",
    .sizes = sizes,
    .size_count = 1,
    .max_upload = 4 * 1024 * 1024,
    .insert = tape_insert,
    .eject = tape_eject,
    .path = tape_path,
    .write_protected = tape_write_protected,
    .create = tape_create,
};

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
    oric_tape_fast(settings.tape_fast);
    if (!oric_init(&s_cfg)) {
        emu_set_message("The Oric could not be set up: see the log.");
        return false;
    }
    ESP_LOGI(TAG, "%s", model_full[settings.model]);
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

static const emu_choice_t choices[] = {
    { "Model", model_names, model_get, model_set, false },
    { "Tape", tape_names, tape_get, tape_set, true },
};

/* ---- Status ---- */

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

static void status(emu_json_t *j, int section)
{
    oric_status_t s;
    oric_get_status(&s);
    oric_tape_status_t t;
    oric_tape_status(&t);
    char text[200];
    if (section == EMU_STATUS_DRIVES) {
        /* the light: the cassette motor's relay (PB6) */
        if (t.inserted) {
            tape_text(text, sizeof(text), &t);
        }
        emu_json_drive(j, drive_names[0], disks_name(0), false, s.motor, t.loaded, t.saved,
                       t.inserted ? text : NULL);
        return;
    }
    emu_json_row(j, "System", s.model == ORIC_1 ? "Oric-1: a 6502A at 1 MHz, 64 KB RAM, the ULA, a 6522, an AY-3-8912" :
                 "Oric Atmos: a 6502A at 1 MHz, 64 KB RAM, the ULA, a 6522, an AY-3-8912");
    emu_json_row(j, "ROM", s.model == ORIC_1 ? "BASIC 1.0 (basic10.rom)" : "BASIC 1.1 (basic11b.rom)");
    snprintf(text, sizeof(text), "%s, %s (%s); the picture 240 × 224",
             s.hires ? "HIRES: 240 × 200 at #A000, three text rows below" : "TEXT: 40 × 28 at #BB80",
             s.hz50 ? "50 Hz" : "60 Hz", s.hz50 ? "312 lines" : "264 lines");
    emu_json_row(j, "Screen", text);
    int n = 0;
    for (int ch = 0; ch < 3 && n < (int)sizeof(text) - 2; ch++) {
        if (ch) {
            n += snprintf(text + n, sizeof(text) - n, "; ");
        }
        n += channel_text(text + n, sizeof(text) - n, &s, ch);
    }
    emu_json_row(j, "Sound", text);
    snprintf(text, sizeof(text), "Timer 1 every %u µs%s (the keyboard's scan, the cursor)", s.timer1 + 2,
             s.timer1_irq ? "" : ", its interrupt off");
    emu_json_row(j, "VIA", text);
    snprintf(text, sizeof(text), "CAPS %s (Caps Lock: CTRL-T); by position, Alt FUNCT, Tab and Esc ESC, "
             "F12 or Pause the reset button (NMI)", s.caps ? "on" : "off");
    emu_json_row(j, "Keyboard", text);
    snprintf(text, sizeof(text), "Motor %s%s; %s%s", s.motor ? "on" : "off",
             t.playing ? (t.slow ? ", playing at 300 baud" : ", playing at 2400 baud") : "",
             !t.fast ? "loads in real time" : t.traps ? "CLOAD and CSAVE at once" : "real time (not BASIC 1.1)",
             t.write_failed ? "; a CSAVE couldn't be written to the card" : "");
    emu_json_row(j, "Cassette", text);
    emu_json_row(j, "Expansions", "None (the Microdisc to come)");
}

/* ---- Console ---- */

static void console_info(char *buf, size_t len)
{
    oric_status_t s;
    oric_get_status(&s);
    oric_tape_status_t t;
    oric_tape_status(&t);
    snprintf(buf, len, "frames %lu PC %04X A %02X X %02X Y %02X S %02X P %02X %s %s tape %lu/%lu%s%s",
             (unsigned long)s.frames, s.pc, s.a, s.x, s.y, s.s, s.p, s.hires ? "hires" : "text",
             s.hz50 ? "50Hz" : "60Hz", (unsigned long)t.pos, (unsigned long)t.size, s.motor ? " motor" : "",
             t.playing ? " playing" : "");
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
    .status = status,
    .console_info = console_info,
    .console_command = console_command,
};

void app_main(void)
{
    emu_run(&machine);
}
