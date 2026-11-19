/*
 * Host build of the Oric core, for testing on the PC:
 *
 *   oric_host <basic11b.rom> [frames] [out.ppm]
 *
 * Environment:
 *   ORIC_MODEL=1            an Oric-1 (give BASIC 1.0's ROM)
 *   ORIC_LOG=<n>            the CPU's state every n frames
 *   ORIC_SHOTS=<n>          a picture every n frames (out-NNNNN.ppm)
 *   ORIC_TEXT=1             the text screen ($BB80) as characters at the end
 *   ORIC_STOP=<pc>[:<frame>] stop at pc (hex), from that frame on: the
 *                           instructions before and the registers
 *   ORIC_TRACE=<pc>:<n>     from pc on, the next n instructions
 *   ORIC_KEYS=<frame>:<key>:<0|1>[;...]  matrix keys (row * 8 + column)
 *   ORIC_NMI=<frame>        the button underneath
 *   ORIC_TYPE=<frame>:<text>[|...]  type text from that frame (\n is RETURN)
 *   ORIC_WAV=<file>         the sound
 *   ORIC_DUMP=<addr>:<len>[;...]  memory in hex at the end
 *   ORIC_ZP=<addr>          with the registers, 16 bytes of zero page from there
 *   ORIC_AY=1               with the registers, the AY's
 *   ORIC_WATCH=<addr>[:<frame>]  a CPU write to addr: the instructions before
 *   ORIC_DASM=<addr>:<count> disassemble there and exit
 *   ORIC_TAPE=<file.tap>    a tape in (CSAVE writes onto its end: use a copy)
 *   ORIC_NEWTAPE=<file.tap> a blank tape made and put in
 *   ORIC_REALTIME=1         tapes load in real time (no traps)
 *   ORIC_TAPE_LOG=1         the tape's state each second
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "oric.h"
#include "oric_internal.h"
#include "m6502.h"

static uint32_t s_frame;

static void disassemble(uint16_t pc)
{
    char buf[64];
    m6502_disassemble(&oric.cpu, pc, buf, sizeof(buf));
    printf("  %04X  %s\n", pc, buf);
}

static void registers(void)
{
    const m6502_t *c = &oric.cpu;
    printf("  A %02X X %02X Y %02X S %02X P %02X PC %04X  mode %02X  IFR %02X IER %02X  cycles %llu\n", c->a, c->x, c->y,
           c->s, c->p, c->pc, oric.mode, oric.via.ifr, oric.via.ier, (unsigned long long)c->cycles);
    if (getenv("ORIC_AY")) {
        uint8_t ay[16];
        psg_get_regs(ay);
        printf("  AY");
        for (int i = 0; i < 16; i++) {
            printf(" %02X", ay[i]);
        }
        printf("\n");
    }
    if (getenv("ORIC_ZP")) {
        unsigned from = strtoul(getenv("ORIC_ZP"), NULL, 16);
        printf("  %02X:", from);
        for (unsigned i = 0; i < 16; i++) {
            printf(" %02X", oric.ram[(from + i) & 0xFF]);
        }
        printf("\n");
    }
}

#define RING_SIZE 4096
static uint16_t ring[RING_SIZE];
static uint32_t ring_pos;

void m6502_host_hook(m6502_t *c)
{
    uint16_t pc = c->pc;
    ring[ring_pos++ % RING_SIZE] = pc;

    static long stop_pc = -2;
    static uint32_t stop_after;
    if (stop_pc == -2) {
        const char *e = getenv("ORIC_STOP");
        stop_pc = e ? (long)strtoul(e, NULL, 16) : -1;
        stop_after = e && strchr(e, ':') ? atoi(strchr(e, ':') + 1) : 0;
    }
    if (stop_pc >= 0 && pc == stop_pc && s_frame >= stop_after) {
        printf("-- reached %04X at frame %lu after:\n", pc, (unsigned long)s_frame);
        for (uint32_t i = ring_pos - 60; i != ring_pos; i++) {
            disassemble(ring[i % RING_SIZE]);
        }
        registers();
        exit(0);
    }

    static long trace_pc = -2;
    static int trace_left;
    if (trace_pc == -2) {
        const char *e = getenv("ORIC_TRACE");
        trace_pc = e ? (long)strtoul(e, NULL, 16) : -1;
        trace_left = e && strchr(e, ':') ? atoi(strchr(e, ':') + 1) : 0;
    }
    if (trace_left > 0 && (trace_pc < 0 || pc == trace_pc)) {
        trace_pc = -1;
        trace_left--;
        disassemble(pc);
    }
}

void oric_host_write(uint16_t a, uint8_t v)
{
    static long watch = -2;
    static uint32_t watch_after;
    if (watch == -2) {
        const char *e = getenv("ORIC_WATCH");
        watch = e ? (long)strtoul(e, NULL, 16) : -1;
        watch_after = e && strchr(e, ':') ? atoi(strchr(e, ':') + 1) : 0;
    }
    if (watch >= 0 && a == watch && s_frame >= watch_after) {
        printf("-- write %02X to %04X at frame %lu, after:\n", v, a, (unsigned long)s_frame);
        for (uint32_t i = ring_pos - (getenv("ORIC_RING") ? atoi(getenv("ORIC_RING")) : 30); i != ring_pos; i++) {
            disassemble(ring[i % RING_SIZE]);
        }
        registers();
    }
}

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", ORIC_SCREEN_WIDTH, ORIC_SCREEN_HEIGHT);
    uint16_t line[ORIC_SCREEN_WIDTH];
    for (int y = 0; y < ORIC_SCREEN_HEIGHT; y++) {
        oric_render_line_rgb565(y, line);
        for (int x = 0; x < ORIC_SCREEN_WIDTH; x++) {
            uint16_t c = line[x];
            uint8_t rgb[3] = { (uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 0x3F) << 2), (uint8_t)((c & 0x1F) << 3) };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

/* The text screen: 28 rows of 40 at $BB80, attributes as spaces, inverse
 * cells (the cursor) as '#' unless they hold a character
 */
static void print_text(void)
{
    for (int row = 0; row < 28; row++) {
        char s[41];
        for (int c = 0; c < 40; c++) {
            uint8_t b = oric.ram[0xBB80 + row * 40 + c];
            uint8_t ch = b & 0x7F;
            s[c] = ch < 0x20 ? ((b & 0x80) ? '#' : ' ') : ch == 0x7F ? '#' : (char)ch;
        }
        int n = 40;
        while (n > 0 && s[n - 1] == ' ') {
            n--;
        }
        s[n] = 0;
        printf("|%s\n", s);
    }
}

/* ORIC_KEYS: frame:key:down;... */
static void keys_for_frame(uint32_t frame)
{
    const char *e = getenv("ORIC_KEYS");
    while (e && *e) {
        unsigned f, key, down;
        if (sscanf(e, "%u:%u:%u", &f, &key, &down) == 3 && f == frame) {
            oric_key(key, down);
        }
        e = strchr(e, ';');
        e = e ? e + 1 : NULL;
    }
}

/* ORIC_TYPE: frame:text|... (\n in the text is RETURN) */
static void type_for_frame(uint32_t frame)
{
    const char *e = getenv("ORIC_TYPE");
    while (e && *e) {
        char *colon;
        unsigned long f = strtoul(e, &colon, 10);
        const char *end = strchr(e, '|');
        if (*colon == ':' && f == frame) {
            char text[256];
            size_t n = 0;
            for (const char *s = colon + 1; *s && s != end && n < sizeof(text) - 1; s++) {
                if (s[0] == '\\' && s[1] == 'n') {
                    text[n++] = '\n';
                    s++;
                } else {
                    text[n++] = *s;
                }
            }
            text[n] = 0;
            oric_type(text);
        }
        e = end ? end + 1 : NULL;
    }
}

static void wav_header(FILE *f, uint32_t frames)
{
    uint32_t bytes = frames * 4, rate = ORIC_AUDIO_HZ, byte_rate = rate * 4;
    uint16_t pcm = 1, channels = 2, align = 4, bits = 16;
    uint32_t riff = 36 + bytes, fmt_len = 16;
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt_len, 4, 1, f);
    fwrite(&pcm, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&bytes, 4, 1, f);
}

static uint8_t *load(const char *path, size_t size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        perror(path);
        exit(1);
    }
    uint8_t *buf = calloc(1, size);
    size_t n = fread(buf, 1, size, f);
    fclose(f);
    if (n != size) {
        fprintf(stderr, "%s: %zu bytes, not %zu\n", path, n, size);
        exit(1);
    }
    return buf;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <basic11b.rom> [frames] [out.ppm]\n", argv[0]);
        return 1;
    }
    uint32_t frames = argc > 2 ? strtoul(argv[2], NULL, 10) : 300;
    const char *out = argc > 3 ? argv[3] : "screen.ppm";

    oric_config_t cfg = { 0 };
    cfg.model = getenv("ORIC_MODEL") && atoi(getenv("ORIC_MODEL")) == 1 ? ORIC_1 : ORIC_ATMOS;
    cfg.rom = load(argv[1], ORIC_ROM_SIZE);
    cfg.ram = calloc(1, ORIC_RAM_SIZE);
    if (!oric_init(&cfg)) {
        fprintf(stderr, "oric_init failed\n");
        return 1;
    }

    if (getenv("ORIC_DASM")) {
        unsigned addr = 0, count = 20;
        sscanf(getenv("ORIC_DASM"), "%x:%u", &addr, &count);
        for (unsigned i = 0; i < count; i++) {
            char buf[64];
            int len = m6502_disassemble(&oric.cpu, addr, buf, sizeof(buf));
            printf("  %04X  %s\n", addr, buf);
            addr += len;
        }
        return 0;
    }
    if (getenv("ORIC_WATCH")) {
        unsigned a = strtoul(getenv("ORIC_WATCH"), NULL, 16);
        if (oric.cpu.wr[a >> 8] != NULL && a < 0xC000) {
            oric.cpu.wr[a >> 8] = NULL;     /* its page's writes through the bus callback */
        }
    }
    oric_tape_fast(!getenv("ORIC_REALTIME"));
    if (getenv("ORIC_NEWTAPE") && !oric_tape_create(getenv("ORIC_NEWTAPE"))) {
        perror(getenv("ORIC_NEWTAPE"));
        return 1;
    }
    const char *tape = getenv("ORIC_NEWTAPE") ? getenv("ORIC_NEWTAPE") : getenv("ORIC_TAPE");
    if (tape && !oric_tape_insert(tape)) {
        fprintf(stderr, "%s: can't put it in\n", tape);
        return 1;
    }
    bool tape_log = getenv("ORIC_TAPE_LOG") != NULL;
    int log_every = getenv("ORIC_LOG") ? atoi(getenv("ORIC_LOG")) : 0;
    int shots = getenv("ORIC_SHOTS") ? atoi(getenv("ORIC_SHOTS")) : 0;
    long nmi_at = getenv("ORIC_NMI") ? atol(getenv("ORIC_NMI")) : -1;
    FILE *wav = getenv("ORIC_WAV") ? fopen(getenv("ORIC_WAV"), "wb") : NULL;
    uint32_t wav_frames = 0;
    if (wav) {
        wav_header(wav, 0);
    }
    for (s_frame = 0; s_frame < frames; s_frame++) {
        keys_for_frame(s_frame);
        if ((long)s_frame == nmi_at) {
            oric_nmi();
        }
        type_for_frame(s_frame);
        oric_run_frame();
        if (wav) {
            int16_t buf[2048];
            int n;
            while ((n = oric_audio_render(buf, 1024)) > 0) {
                fwrite(buf, 4, n, wav);
                wav_frames += n;
            }
        } else {
            oric_audio_enable(false);
        }
        if (tape_log && s_frame % 50 == 0) {
            oric_tape_status_t ts;
            oric_tape_status(&ts);
            printf("frame %u: tape %u/%u file %d/%d \"%s\" motor %d playing %d%s loaded %u saved %u PC %04X\n",
                   s_frame, ts.pos, ts.size, ts.file, ts.files, ts.name, ts.motor, ts.playing,
                   ts.slow ? " (slow)" : "", ts.loaded, ts.saved, oric.cpu.pc);
        }
        if (log_every && s_frame % log_every == 0) {
            printf("frame %u:", s_frame);
            registers();
        }
        if (shots && s_frame % shots == 0) {
            char name[300];
            snprintf(name, sizeof(name), "%.*s-%05u.ppm", (int)(strlen(out) - 4), out, s_frame);
            write_ppm(name);
        }
    }
    if (wav) {
        fseek(wav, 0, SEEK_SET);
        wav_header(wav, wav_frames);
        fclose(wav);
    }
    const char *d = getenv("ORIC_DUMP");
    while (d && *d) {
        unsigned addr, len;
        if (sscanf(d, "%x:%u", &addr, &len) == 2) {
            for (unsigned i = 0; i < len; i++) {
                uint16_t at = addr + i;
                printf("%s%02X", i % 16 ? " " : (i ? "\n" : ""), oric.ram[at]);
            }
            printf("\n");
        }
        d = strchr(d, ';');
        d = d ? d + 1 : NULL;
    }
    if (getenv("ORIC_TEXT")) {
        print_text();
    }
    if (tape) {
        oric_tape_eject();              /* (writes what CSAVE left) */
    }
    printf("after %u frames: PC %04X\n", frames, oric.cpu.pc);
    write_ppm(out);
    return 0;
}
