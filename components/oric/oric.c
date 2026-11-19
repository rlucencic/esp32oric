/*
 * The Oric: set up, reset, the frame loop, the 6502's bus and the VIA's
 * wiring (docs/oric-hardware.md 1, 2, 8).
 *
 * Time is counted in the 6502's 1 MHz cycles. The CPU never waits: each
 * microsecond has a CPU slot and the ULA's two fetches (docs 4.2), so
 * RAM and the ROM go through the core's page pointers. Page 3 is the
 * VIA (its 16 registers every 16 bytes) through the bus callbacks; so are
 * writes to $9800-$BFFF (the screens and character sets: video.c decodes
 * the picture again only after one). Writes to $C000-$FFFF are lost while
 * BASIC is there (MAP high); the Microdisc (microdisc.c) claims $0310-$031B
 * and pages its EPROM and the overlay RAM in.
 */
#include <string.h>
#include "oric_internal.h"

oric_t oric;

static uint8_t s_sink[256];             /* writes to the ROM go here: lost */

/* ---- Memory map ---- */

#ifdef ORIC_HOST_HOOK
void oric_host_write(uint16_t a, uint8_t v);    /* host builds: ORIC_WATCH */
int oric_host_watch_page = -1;                  /* ... its page's writes through the callback */
#endif

uint8_t *oric_sink(void)
{
    return s_sink;
}

void oric_map_memory(void)
{
    for (int p = 0; p < 0xC0; p++) {
        uint8_t *ram = oric.ram + p * 256;
        if (p == 0x03) {
            oric.cpu.rd[p] = NULL;      /* the VIA, the Microdisc */
            oric.cpu.wr[p] = NULL;
        } else {
            oric.cpu.rd[p] = ram;
            oric.cpu.wr[p] = p >= 0x98 ? NULL : ram;
        }
    }
#ifdef ORIC_HOST_HOOK
    if (oric_host_watch_page >= 0 && oric_host_watch_page < 0xC0) {
        oric.cpu.wr[oric_host_watch_page] = NULL;
    }
#endif
    oric_map_high();
}

/* $C000-$FFFF: BASIC, or what the Microdisc pages in */
void oric_map_high(void)
{
    oric.basic_in = !microdisc_map();
    if (oric.basic_in) {
        for (int p = 0xC0; p < 0x100; p++) {
            oric.cpu.rd[p] = (uint8_t *)oric.rom + (p - 0xC0) * 256;
            oric.cpu.wr[p] = s_sink;
        }
    }
    tape_map();                         /* the tape routines' pages: traps (BASIC's only) */
}


static uint8_t bus_read(void *ctx, uint16_t a)
{
    if ((a & 0xFF00) == 0x0300) {
        if (microdisc_claims(a)) {
            return microdisc_read(a);
        }
        tape_sync(oric.cpu.cycles);
        if ((a & 0x0F) == 0x0D) {
            tape_poll();                /* IFR: a loader may be polling CB1 */
        }
        return via_read(&oric.via, a & 0x0F);
    }
    if (a >= 0xC000) {
        return tape_rom_read(a);        /* the tape routines' pages, while trapped */
    }
    return 0xFF;                        /* (nothing else comes here) */
}

static void bus_write(void *ctx, uint16_t a, uint8_t v)
{
#ifdef ORIC_HOST_HOOK
    oric_host_write(a, v);
#endif
    if ((a & 0xFF00) == 0x0300) {
        if (microdisc_claims(a)) {
            microdisc_write(a, v);
            return;
        }
        tape_sync(oric.cpu.cycles);
        via_write(&oric.via, a & 0x0F, v);
        return;
    }
    if (a < 0xC000) {                   /* the screens (the host's watched page too) */
        if (oric.ram[a] != v) {
            oric.ram[a] = v;
            if (a >= 0x9800) {
                oric.screen_touched = 2;
            }
        }
    }
}

void oric_write(uint16_t a, uint8_t v)
{
    uint8_t *p = oric.cpu.wr[a >> 8];
    if (p) {
        p[a & 0xFF] = v;
    } else {
        bus_write(NULL, a, v);
    }
}

/* ---- The VIA's wiring (docs 2.1) ---- */

/* Port A: the AY's data bus, which the AY drives in its read cycle (BDIR
 * low, BC1 high); port B: PB3 the keyboard's sense line for the row on
 * PB0-PB2 and the columns on the AY's port A (1: a key down)
 */
static uint8_t via_in(via_t *v, int port)
{
    if (port == 0) {
        return (!via_cb2(v) && via_ca2(v)) ? psg_read() : 0xFF;
    }
    uint8_t pb = via_port_out(v, 1);
    return (pb & ~0x08) | (oric_keyboard_sense(pb & 7, psg_port_a()) ? 0x08 : 0);
}

/* CA2 is the AY's BC1, CB2 its BDIR (BC2 tied high), port A its bus;
 * PB4 the printer's strobe (no printer: acknowledged at once on CA1, so
 * LPRINT doesn't wait for ever), PB6 the cassette motor's relay (driven
 * high as an output; an input's pull-up at reset doesn't count)
 */
static void via_out(via_t *v, int port)
{
    if (port == 0) {
        psg_bus(via_cb2(v), via_ca2(v), via_port_out(v, 0));
        return;
    }
    uint8_t pb = via_port_out(v, 1);
    if ((pb & ~oric.port_b) & 0x10) {
        via_ca1(v, false);
        via_ca1(v, true);
    }
    oric.port_b = pb;
    bool motor = (v->ddrb & 0x40) && (pb & 0x40);
    if (motor != oric.motor) {
        oric.motor = motor;
        tape_motor(motor);
    }
}

static void via_irq(via_t *v, bool on)
{
    if (on) {
        oric.cpu.irq |= IRQ_VIA;
    } else {
        oric.cpu.irq &= ~IRQ_VIA;
    }
}

/* ---- Set up and reset ---- */

bool oric_init(const oric_config_t *config)
{
    memset(&oric, 0, sizeof(oric));
    oric.cfg = *config;
    oric.ram = config->ram;
    oric.rom = config->rom;
    oric.disk_rom = config->disk_rom;
    if (!oric_video_init() || !psg_init() || !fdc_init()) {
        return false;
    }
    const m6502_bus_t bus = { bus_read, bus_write, NULL, NULL };
    m6502_init(&oric.cpu, &bus);
    oric.cpu.decimal = true;
    via_init(&oric.via, &oric.cpu.cycles);
    oric.via.cpu = &oric.cpu;
    oric.via.port_in = via_in;
    oric.via.port_out = via_out;
    oric.via.irq_out = via_irq;
    oric_reset();
    return true;
}

/* Power off and on. RAM's contents at power-on are unknown (docs 1.5).
 * $0400-$FFFF, which the ROM's RAM test fills and the picture shows
 * meanwhile, and the overlay RAM: the same made-up noise every time (runs
 * that must match), bit 6 set so no attributes show. Sedoric's boot sector
 * sums the overlay RAM: 0 says Sedoric is still there (a warm start) and
 * it loads 4 sectors of 64, as it did with zeros there (MAME puts $42 at
 * $E000 for it). Pages 0-3: zeros, as MAME has them: the ROM reads page
 * 2's key buffer ($02DF, bit 7 a new key) before it ever writes it, and
 * noise there typed a phantom key after "Ready". The ULA's mode register
 * (unknown too): 50 Hz text.
 */
void oric_reset(void)
{
    memset(oric.ram, 0, 0x0400);
    uint32_t x = 0x2545F491;
    for (int i = 0x0400; i < 0x10000; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        oric.ram[i] = (uint8_t)x | 0x40;
    }
    oric.mode = MODE_50HZ;
    oric.lines = ORIC_LINES_50HZ;
    oric.frame_start = oric.cpu.cycles;
    oric.screen_touched = 2;
    oric.nmi_request = false;
    oric_video_reset();
    psg_reset();
    oric_keyboard_reset();
    oric.port_b = 0xFF;
    oric.motor = false;
    tape_reset();
    via_reset(&oric.via);
    microdisc_reset();                  /* (its EPROM paged in: the RESET vector) */
    oric_map_memory();
    m6502_reset(&oric.cpu);
}

bool oric_halted(void)
{
    return oric.cpu.jammed;
}

/* The button underneath: NMI (an edge), taken as the next frame starts */
void oric_nmi(void)
{
    oric.nmi_request = true;
}

/* ---- The frame ---- */

static uint64_t next_event(void)
{
    uint64_t ev = oric.via.next_event < tape_edge_at ? oric.via.next_event : tape_edge_at;
    return fdc_event_at < ev ? fdc_event_at : ev;
}

/* The CPU up to cycle t, stopping at the VIA's timer underflows, the
 * tape's edges on CB1 and the disk controller's steps (an interrupt then
 * reaches the 6502 at the instruction it fell in, if before that one's
 * interrupt poll), and after an instruction that read a tape routine's
 * first byte (tape.c's traps)
 */
static void run_to(uint64_t t)
{
    m6502_t *c = &oric.cpu;
    while (c->cycles < t) {
        uint64_t ev = next_event();
        if (c->cycles >= ev) {
            bool before = c->irq != 0;
            tape_sync(c->cycles);
            if (c->cycles >= oric.via.next_event) {
                via_update(&oric.via);
            }
            if (c->cycles >= fdc_event_at) {
                fdc_update(c->cycles);
            }
            if (!before && c->irq && ev <= c->poll_cycle) {
                m6502_repoll(c);
            }
            ev = next_event();
        }
        uint64_t u = ev < t ? ev : t;
        c->until = u > c->cycles ? u : c->cycles + 1;
        m6502_run(c);
        tape_trap();
    }
}

/* A frame: each picture line is decoded as it starts (video.c), the mode
 * register it leaves after line 223 decides the frame's length (the last
 * 50/60 Hz attribute rules that same frame: docs 4.4)
 */
void oric_run_frame(void)
{
    if (oric.nmi_request) {
        oric.nmi_request = false;
        m6502_nmi(&oric.cpu);
        tape_nmi();
    }
    oric_video_frame_start();
    int lines = ORIC_LINES_50HZ;
    for (oric.line = 0; oric.line < lines; oric.line++) {
        if (oric.line < ORIC_PICTURE_LINES) {
            oric_video_line(oric.line);
            if (oric.line == ORIC_PICTURE_LINES - 1) {
                lines = (oric.mode & MODE_50HZ) ? ORIC_LINES_50HZ : ORIC_LINES_60HZ;
            }
        }
        run_to(oric_line_start() + ORIC_LINE_CYCLES);
        psg_line();
    }
    oric.lines = lines;
    oric.frame_start += (uint64_t)lines * ORIC_LINE_CYCLES;
    oric.frames++;
    oric_video_frame_end();
    oric_keyboard_frame();
    tape_frame_end();
    fdc_frame_end();
}

/* ---- Status ---- */

void oric_get_status(oric_status_t *s)
{
    s->frames = oric.frames;
    s->pc = oric.cpu.pc;
    s->a = oric.cpu.a;
    s->x = oric.cpu.x;
    s->y = oric.cpu.y;
    s->s = oric.cpu.s;
    s->p = oric.cpu.p;
    s->model = oric.cfg.model;
    s->hires = (oric.mode & MODE_HIRES) != 0;
    s->hz50 = (oric.mode & MODE_50HZ) != 0;
    s->caps = (oric.ram[0x020C] & 0x80) != 0;     /* $FF on, $7F off */
    s->motor = oric.motor;
    s->timer1 = oric.via.t1_latch;
    s->timer1_irq = (oric.via.ier & 0x40) != 0;
    psg_get_regs(s->ay);
    s->microdisc = oric.disk_rom != NULL;
    s->disk_control = microdisc_control();
    s->irq = (uint8_t)oric.cpu.irq;
    const via_t *v = &oric.via;
    s->via.ora = v->ora;
    s->via.orb = v->orb;
    s->via.ddra = v->ddra;
    s->via.ddrb = v->ddrb;
    s->via.acr = v->acr;
    s->via.pcr = v->pcr;
    s->via.ifr = v->ifr;
    s->via.ier = v->ier;
    s->via.t1 = (uint16_t)v->t1;
    s->via.t2 = v->t2;
}

uint8_t oric_peek(uint16_t addr)
{
    return m6502_peek(&oric.cpu, addr);
}
