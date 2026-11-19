/*
 * The Microdisc (docs/oric-hardware.md 7): its WD1793 (wd1793.c), the
 * control register, the 8 KB EPROM and the paging.
 *
 * Page 3: a 74LS138 on A4-A2 (the schematic, BN0136 sheet 2) selects the
 * WD1793 at $0310-$0313, the control register (write) and INTRQ (read, bit
 * 7 low while active) at $0314-$0317, DRQ (bit 7 low) at $0318-$031B, and
 * pulls I/O CONTROL low for those only: the rest of page 3 stays the VIA's.
 * INTRQ reaches the 6502's IRQ while control bit 0 is set; $0314 shows it
 * as it is (CLK, the MiSTer model; MAME gates it by bit 0 too: the EPROM
 * reads it only with bit 0 set).
 *
 * The control register (all 0 at reset): bit 0 IRQ enable, 1 BASIC on (0:
 * ROMDIS), 2 the data separator's rate, 3 /DDEN, 4 side, 5-6 drive, 7 the
 * EPROM off. Paging: BASIC at $C000-$FFFF while bit 1 is set; else the
 * overlay RAM at $C000-$DFFF and the EPROM at $E000-$FFFF (writes lost),
 * or with bit 7 set overlay RAM in all of $C000-$FFFF. At reset the 6502
 * takes its RESET vector from the EPROM.
 */
#include <string.h>
#include "oric_internal.h"

static uint8_t s_control;

bool microdisc_claims(uint16_t a)
{
    return oric.disk_rom != NULL && (a & 0xFF) >= 0x10 && (a & 0xFF) < 0x1C;
}

void microdisc_irq(void)
{
    if (fdc_intrq() && (s_control & 0x01)) {
        oric.cpu.irq |= IRQ_FDC;
    } else {
        oric.cpu.irq &= ~IRQ_FDC;
    }
}

uint8_t microdisc_read(uint16_t a)
{
    switch (a & 0x0C) {
    case 0x00:
        return fdc_read(a & 3);
    case 0x04:
        fdc_update(oric.cpu.cycles);
        return fdc_intrq() ? 0x7F : 0xFF;
    default:
        return fdc_drq() ? 0x7F : 0xFF;
    }
}

void microdisc_write(uint16_t a, uint8_t v)
{
    switch (a & 0x0C) {
    case 0x00:
        fdc_write(a & 3, v);
        return;
    case 0x04: {
        fdc_update(oric.cpu.cycles);
        uint8_t changed = s_control ^ v;
        s_control = v;
        fdc_select((v >> 5) & 3, (v >> 4) & 1);
        microdisc_irq();
        if (changed & 0x82) {
            oric_map_high();
        }
        return;
    }
    default:
        return;                         /* DRQ: read only */
    }
}

/* $C000-$FFFF as the control register pages it (with a Microdisc) */
bool microdisc_map(void)
{
    if (oric.disk_rom == NULL || (s_control & 0x02)) {
        return false;                   /* BASIC */
    }
    for (int p = 0xC0; p < 0x100; p++) {
        uint8_t *ram = oric.ram + p * 256;
        if (p >= 0xE0 && !(s_control & 0x80)) {
            oric.cpu.rd[p] = (uint8_t *)oric.disk_rom + (p - 0xE0) * 256;
            oric.cpu.wr[p] = oric_sink();
        } else {
            oric.cpu.rd[p] = ram;
            oric.cpu.wr[p] = ram;
        }
    }
    return true;
}

uint8_t microdisc_control(void)
{
    return s_control;
}

void microdisc_reset(void)
{
    s_control = 0;
    fdc_reset();
    microdisc_irq();
}
