# Oric Atmos hardware, for a clean-room emulator core

Research notes, 2026-11-19. Every fact carries a source tag (key in
[Sources](#sources)). Marks used:

- **[confirmed]**: two or more independent sources agree.
- **[one source]**: a single source, not contradicted.
- **[derived]**: worked out here from ROM code, a schematic or numbers,
  not stated anywhere.
- **[UNCERTAIN]**: sources disagree, or nothing settles it; what each
  says is given.

Where one emulator differs from the better-supported answer the text says
"[MAME differs]" or "[CLK differs]".

Licences: MAME's files (`oric.cpp`, `microdisc.cpp/.h`, `oricext.cpp/.h`,
`jasmin.cpp`, `oric_dsk.cpp/.h`, `oric_tap.cpp/.h`, `6522via.cpp`,
`ay8910.cpp/.h`) carry `// license:BSD-3-Clause` and are cited freely.
Clock Signal (CLK, Thomas Harte) is MIT. Erik Persson's `oric-ula` RTL,
made from a transistor netlist extracted from a photograph of the real
HCS10017 die, is MIT. The MiSTer core's `ula.vhd` (seilebost 2001-2009,
d18c7db 2013) has a BSD-style licence and is cited; the same core's
`microdisc.vhd` (Retromaster 2010) is **GPL-3.0** and was read only to
confirm behaviour: nothing is quoted or paraphrased from it beyond what it
does. **Oricutron (GPL) and Euphoric (closed source) were not read at
all**; their behaviour appears only as reported by others. Mike Brown's
ULA guide v1.02 is public domain (with attribution); v2.00, the books, the
manuals and data sheets are copyrighted and are cited by page for facts,
nothing reproduced beyond short identifiers. ROM code facts come from a
disassembly made here of the identified ROM images (MAME's set, CRCs in
section 1.7), cited by address. Independence caveats: MB2 and ULARTL share
the die netlist (Mike Brown processed the netlist Erik Persson converted),
but MB2's oscilloscope measurements are a separate observation; MAME and
CLK are separate code bases but both postdate Euphoric-era folklore, so
"[confirmed]" below always includes a non-emulator source (ROM code, a
period book or manual, the die-derived RTL or a schematic).

### Summary

Certain: the memory map (RAM $0000-$BFFF of 64 KB of DRAM, the 6522 VIA in
all of page 3 unless an expansion pulls I/O CONTROL low, BASIC ROM at
$C000-$FFFF read only, the 16 KB underneath reachable only with MAP low;
CPU writes to $C000-$FFFF are lost while MAP is high); no CPU/video
contention (each 1 us has one CPU slot and two video fetches); the VIA
wiring (PA = printer data and AY bus, PB0-2 keyboard row, PB3 key sense
with 1 = pressed, PB4 printer strobe, PB6 motor, PB7 tape out, CA1 printer
ACK, CA2 = AY BC1, CB2 = AY BDIR, CB1 tape in, BC2 tied high); the PCR
values ($DD idle, $FF latch address, $FD write data); timer 1 free-running
at N = 10000 (an IRQ every 10002 us), the IRQ handler's 30 ms keyboard scan
and 0.25 s cursor toggle; the AY at 1 MHz, mono, port A as the active-low
keyboard column select; the ULA clocks (12 MHz crystal, 6 MHz pixels,
1 MHz CPU); 64 us lines, 40 cells of 6 pixels, 224 active lines, **312
lines at 50 Hz and 264 at 60 Hz** (the die netlist, not 260); vsync lines
256-259 (50 Hz) and 236-239 (60 Hz); the serial attributes (bits 6-5 = 00;
bits 4-3 pick ink/style/paper/mode; bit 7 inverts the cell) and their
rules (applied from the attribute's own cell, which shows paper; ink 7,
paper 0, standard single steady reset every line; mode persistent; the
last 50/60 attribute seen in a frame sets that frame's length; text/hires
switches take effect at the next cell, even mid-line; lines 200-223 are
forced text); the screen and character set addresses; double height by
line parity; the colour bits (bit 0 red, 1 green, 2 blue); the keyboard
matrix (8 rows x 8 columns, modifiers in column 4); the tape encodings
(fast: one cycle per bit, 210 us + 210/418 us; slow: 8 x 2400 Hz or 4 x
1200 Hz), the byte frame (start 0, 8 data LSB first, odd parity, 3 stop
1s), the 9-byte header, TAP files as concatenated byte streams; the
Microdisc's registers, its paging and its WD1793 clocked at 1 MHz; the
MFM_DISK layout; the reset sequence, messages and page-2 vectors.

Uncertain (details in the text): the flash rate is settled by the die RTL
and MB2 (16 frames on, 16 off) but MAME and CLK use 32/32; when the 50/60
choice takes effect (die RTL: the same frame; CLK: the next one); the
power-on state of the ULA's mode register and of RAM; what floating reads
return; whether the Microdisc's $0314 status bit is gated by its IRQ
enable (MAME) or not (CLK, MiSTer); how far the Microdisc mirrors and
claims page 3 ($0314/$0318 exactly in MAME, $0310-$031B in the MiSTer
model, $0310-$03FF in CLK); control register bit 2's meaning; MFM_DISK
geometry type 2's exact layout; how a Sedoric disk satisfies the EPROM's
boot lookup (not traced); why MAME puts $42 at RAM $E000 on reset (nothing
in the EPROM reads it). (The Atmos Manual's double-height example, once
listed here, agrees with the hardware rule: 4.8.) Single sources: the vertical counter
stepping at the start of horizontal sync (ULARTL), PB5 unused (AUG),
Oric DOS's 16 sectors per track (MDM), the MiSTer model's Microdisc
address decoding.

Contents

1. [Memory map](#1-memory-map)
2. [The 6522 VIA](#2-the-6522-via)
3. [The AY-3-8912](#3-the-ay-3-8912)
4. [The ULA and the display](#4-the-ula-and-the-display)
5. [Keyboard and the NMI button](#5-keyboard-and-the-nmi-button)
6. [Cassette and TAP files](#6-cassette-and-tap-files)
7. [The Microdisc](#7-the-microdisc)
8. [Reset and start-up](#8-reset-and-start-up)
9. [Sources](#sources)

---

## 1. Memory map

The CPU is a 6502A (NMOS) [SM App. 1 parts list, IC5 "6502A"; AUG ch. 1]
[confirmed], clocked at 1 MHz from the ULA's PHI output. That clock is
asymmetric (high for 4 of the ULA's 12 states per microsecond), which is
why a 2 MHz part is fitted [MB1 "Video Address Generator"; ULARTL `PHI =
rg_cnt[3]`] [confirmed]. RDY and SO are tied high, SYNC unused [AUG p31]
[one source]. The CPU never waits: each microsecond gives it one memory
slot and the ULA two (section 4.2) [AUG p57, p64; MB1; ULARTL]
[confirmed].

| Range | Contents |
|---|---|
| $0000-$02FF | RAM: zero page, stack, page 2 (system variables, the jump vectors at $0238-$024A, section 8.3) |
| $0300-$03FF | I/O: the VIA's 16 registers repeated every 16 bytes, unless an expansion pulls I/O CONTROL low (1.2). The RAM under page 3 is never reachable by the CPU |
| $0400-$BFFF | RAM. BASIC program from $0501; text screen $BB80-$BFDF; character sets $B400-$BB7F (text mode) or $9800-$9FFF (hires mode); hires screen $A000-$BF3F |
| $C000-$FFFF | BASIC ROM (16 KB) for reads; 16 KB of RAM underneath ("overlay" or "shadow" RAM), reachable only while MAP is low |

RAM is eight 4164 (64 Kbit) DRAMs, 64 KB, "48K for user programs and 16K
which remains unused (except for I/O expansion)" [SM pp10-11; AUG p63]
[confirmed]. The 16 KB Oric-1 (two TMS4416, A14/A15 ignored, no overlay)
is out of scope [SM p11].

### 1.1 Address decoding (the ULA)

The ULA (HCS10017) sees A8-A15, R/W and MAP and drives the ROM select, the
VIA select (/I/O), RAS/CAS/WE for the DRAMs [MB1 "Address Decode";
ULARTL; SM p14] [confirmed]. From the die-derived RTL: /I/O is low for
every address $03xx; the ROM is selected only for **reads** of
$C000-$FFFF with MAP high; the CPU's RAM cycle gets CAS only when (MAP high,
address below $C000, not page 3) or (MAP low, address $C000 or above)
[ULARTL `nIO`, `nROM`, `nx_casa`] [derived]. This matches the period
descriptions:

| MAP | $0000-$BFFF (not page 3) | page 3 | $C000-$FFFF read | $C000-$FFFF write |
|---|---|---|---|---|
| high (normal) | RAM | VIA (1.2) | ROM | lost |
| low (expansion) | nothing on board: the expansion drives the bus | VIA unless I/O CONTROL is low | overlay RAM | overlay RAM |

Sources: [AUG p78; SM p14; MB1; ULARTL] [confirmed]. MAP is a ~250 ns low
pulse starting 80-100 ns before phase 2 rises [SM p15; AUG p79 fig. 5.5]
[confirmed]: an emulator can treat it as a per-access decision of the
expansion.

ROMDIS (expansion input, pulled up by 2.2 kohm) goes to the ROM chip
itself, not the ULA, and disables it [AUG p79; SM p12; ULARTL's `nROM`
ignores it] [confirmed]. With ROMDIS low and MAP high, a read of
$C000-$FFFF is driven by nothing on the board: the expansion must supply
the data [derived from AUG p79 and ULARTL].

### 1.2 Page 3 and I/O CONTROL

/I/O (ULA output) drives the VIA's active-low chip select and the
expansion port; I/O CONTROL (expansion input, pulled up by 2.2 kohm)
drives the VIA's active-high chip select. With nothing connected the VIA
answers in all of $0300-$03FF, every 16 bytes [AUG p80; SM p13; ULARTL;
MAME `mirror(0xf0)`; CLK] [confirmed]. An expansion that uses $0310-$03FF
must pull I/O CONTROL low for its own addresses and never for
$0300-$030F, or the timer interrupt, keyboard, printer, tape and sound
stop working [AUG p80; SM p13] [confirmed]. Oric's own peripherals sit
"from 0300 upwards" [SM p14] [one source].

### 1.3 Writes to $C000-$FFFF

While MAP is high a CPU write to $C000-$FFFF reaches neither the ROM (its
select is for reads only) nor the RAM (no CAS): **the write is lost**
[ULARTL; MB2 p22 note 33 "Shadow RAM ... where the CPU cannot write in
normal use"; SM p14; MAME `unmapw`; CLK drops it] [confirmed]. MAME is
right.

### 1.4 Unmapped and floating reads [UNCERTAIN]

On a bare machine there is no unmapped I/O: page 3 is all VIA. Where an
expansion pulls I/O CONTROL low but drives nothing, or with ROMDIS low and
no data supplied, the bus floats; no source gives the value read. With a
Microdisc, MAME leaves $0315-$0317 and $0319-$03FF to the VIA mirrors, CLK
returns the previous bus value for its unhandled addresses in
$0310-$03FF, and the MiSTer model claims only $0310-$031B (section 7.2).
No software is known to depend on it.

### 1.5 RAM at power-on [UNCERTAIN]

Not specified anywhere. At power-on "the screen goes blank, with random
white blocks on it for a few seconds (while the Oric checks through its
circuits)" [AM p3]: random DRAM shown while the RAM test runs. The test
then leaves $55 in every byte of $0400-$BFFF (it writes $AA, then $55)
[ROM $FA4E-$FA5B] [derived], so the ROM does not care. MAME starts from
zero except $E000 = $42 (section 7.5); CLK fills RAM with random values
with bit 6 set, so that no stray attribute (notably a 60 Hz one) appears
[CLK `Oric.cpp` constructor comment]. Suggested: random with bit 6 set.
But not in page 2: the ROM reads its key buffer $02DF (bit 7 = a new key)
before it ever writes it, and noise there typed a phantom key after
"Ready" in our first run (2026-11-19). The core keeps noise in
$0400-$BFFF only and zeros elsewhere [derived].

### 1.6 What the emulator's map needs [derived]

Read: $0000-$02FF and $0400-$BFFF RAM; $0300-$03FF VIA (register = address
& 15) unless a disk interface claims the address; $C000-$FFFF ROM, or
whatever the expansion's paging selects (section 7.3). Write: RAM below
$C000 except page 3; page 3 to the VIA or the expansion; $C000-$FFFF only
to overlay RAM when the expansion selects it.

### 1.7 The ROM files

| File (MAME set) | Size | CRC32 | NMI / RESET / IRQ vectors |
|---|---|---|---|
| `basic11b.rom` (Atmos BASIC 1.1) | 16 KB | c3a92bef | $0247 / $F88F / $0244 |
| `basic10.rom` (Oric-1 BASIC 1.0) | 16 KB | f18710b4 | $022B / $F42D / $0228 |
| `microdis.rom` (Microdisc EPROM) | 8 KB | a9664a9c | $E0AE / $EB7E / $E3C0 |

MAME's set and oric-soft.bedroomlan.org's files are identical [MAME
`oric.cpp`, `microdisc.cpp` CRCs; files compared here] [confirmed].
Vectors read from the images [derived]. BASIC 1.0's IRQ/NMI page-2
vectors are $0228/$022B, which the Microdisc EPROM also knows [MDROM
$EBB6-$EBC6] [derived].

## 2. The 6522 VIA

### 2.1 Wiring

| VIA line | Oric use | Sources |
|---|---|---|
| PA0-PA7 | printer data and the AY's DA0-DA7 bus | AUG p34, p56, p70; SM p20; MAME; CLK [confirmed] |
| CA1 | printer ACKNOWLEDGE (polled in IFR bit 1, not an interrupt) | AUG p34, p70; SM p20; MAME [confirmed] |
| CA2 | AY BC1 (manual output mode) | AUG p34, p56; ROM $F590; MAME; CLK [confirmed] |
| PB0-PB2 | keyboard row select (4051 multiplexer) | AUG p34, p74; SM p17; ROM $F56E; MAME; CLK [confirmed] |
| PB3 | keyboard sense input, 1 = a key in the selected row and columns is down | AUG p34; ROM $F578-$F57D; MAME; CLK [confirmed] (AUG p74 says PB4, a slip) |
| PB4 | printer STROBE (pulsed low) | AUG p34, p70; SM p20; ROM $F5C6-$F5D3 [confirmed] |
| PB5 | unused (the ROM keeps it 1) | AUG p34 [one source] |
| PB6 | cassette motor relay, 1 = on | AUG p34; SM p19 (TR3); ROM $E77C; MAME; CLK [confirmed] |
| PB7 | cassette output; driven by timer 1 (ACR bit 7) while saving | AUG p34, p71; SM p18; ROM $E76A; MAME [confirmed] |
| CB1 | cassette input (VIA pin 18) | AUG p34, p71; SM p38; MAME; CLK [confirmed] |
| CB2 | AY BDIR (manual output mode) | AUG p34, p56; ROM $F590; MAME; CLK [confirmed] |
| /IRQ | CPU /IRQ, wired-OR with the expansion port's IRQ | AUG p78; MAME `update_irq` [confirmed] |
| CS1 / /CS2 | I/O CONTROL / the ULA's /I/O (1.2) | AUG p80; SM p13 [confirmed] |
| RS0-RS3, phase 2 | A0-A3, the 1 MHz clock | AUG p33 [one source] |

The AY's BC2 is tied to +5 V [AUG p52] [one source; consistent with
MAME's and CLK's models]. The shift register is unusable (CB2 is taken)
[AUG p34-35].

### 2.2 What the ROM sets at reset

[ROM $F9AA-$F9C8, then $EDE0-$EE19] [derived]:

| Register | Value | Meaning |
|---|---|---|
| DDRA ($0303) | $FF | port A all outputs |
| DDRB ($0302) | $F7 | port B outputs except PB3 |
| ORB ($0300) | $B7 | PB7 = 1 (tape idle high), PB6 = 0 (motor off), PB5 = 1, PB4 = 1 (strobe idle), row 7 |
| PCR ($030C) | $DD | CA1 and CB1 positive edge; CA2 and CB2 manual low (AY idle) |
| IER ($030E) | $7F, later $C0 | all off; then timer 1 only |
| ACR ($030B) | $00, later ($00 & $7F) \| $40 | timer 1 free-running, PB7 not timer-driven; timer 2 one-shot |
| T1L-L, T1C-L ($0306, $0304) | $10 | |
| T1L-H, T1C-H ($0307, $0305) | $27 | N = $2710 = 10000; writing T1C-H last starts it |

A VIA reset clears every register except the timers and the shift
register, so no interrupt is possible until the ROM enables one [VIA
"RESET (RES)"] [one source]. The ROM's CLI at $F892 comes before any VIA
set-up, which is safe for that reason [derived].

### 2.3 CA2/CB2 and the AY bus

The AY's three control lines decide the bus cycle [AUG p52 table 3.1]:
BC1 BC2 BDIR = 0 1 0 inactive; 0 1 1 write data; 1 1 0 read data; 1 1 1
latch address. With BC2 fixed high, CA2 = BC1 and CB2 = BDIR give [ROM
$F590-$F5BF; MAME `update_psg`; CLK `set_control_line_output`]
[confirmed]:

| PCR | CB2 (BDIR) | CA2 (BC1) | AY cycle |
|---|---|---|---|
| $DD | low | low | inactive |
| $FF | high | high | latch address from port A |
| $FD | high | low | write port A into the selected register |
| $DF | low | high | read: the AY drives port A (the ROM never does this) |

(Values assume PCR's CA1/CB1 bits stay 1, as the ROM keeps them; the ROM
computes them as `PCR | $EE`, `(PCR & $11) | $CC`, `PCR | $EC`.)

The ROM's routine W8912 at $F590 takes the register in A and the value in
X: SEI, register number to ORA *without handshake* ($030F), latch, idle,
value to $030F, write, idle; for register 7 it ORs $40 into the value so
that port A stays an output (the keyboard needs it) [ROM $F590-$F5C0; AM
App. 9 p270 "ensures that the keyboard port is kept enabled"]
[confirmed]. Using $030F leaves the CA1/CA2 flags alone (reading or
writing $0301 clears them) [VIA fig. 29] [derived].

### 2.4 Timer 1: the 100 Hz tick

In free-running mode the first interrupt comes N + 1.5 cycles after the
T1C-H write and each later one N + 2 cycles after the previous [VIA fig.
16]. With N = 10000 the tick is every 10002 us, 99.98 Hz [derived]; "an
interrupt every 10 mS" [AUG p32, p34] [confirmed]. A real-machine
observation supports the N + 2 rule: a game author found that a timer
reloaded with 19966 stays locked to the picture [DF-FORUM p19855,
Chema 2019-07-16]; 19966 + 2 = 19968 = 312 x 64, one 50 Hz frame
[derived].

There is no video interrupt: the tick runs freely against the frame.
Some users connect the video sync to the tape input ("vsync cable"),
which MAME offers as an option [MAME `oric.cpp` CONFIG port; MB2 p18
note 23] [confirmed]. CLK always routes vsync to CB1 while the tape motor
is off [CLK `set_via_port_b_input`] [CLK differs from a stock machine].

### 2.5 The IRQ handler

Hardware IRQ vector $FFFE -> $0244 (page 2), which holds JMP $EE22 [ROM
$F87C-$F88E copied at reset]. The handler [ROM $EE22-$EE8B] [derived]:

1. PHA; reads IFR; if bit 6 (timer 1) is clear it does nothing else.
2. Clears the flag by writing $40 to IFR, then the tick routine $EE34:
   decrements three 16-bit software timers at $0272 (keyboard), $0274
   (cursor) and $0276 (WAIT and general use), "decrementing every 1/100th
   of second. Continues past 0" [AM App. 9 p271] [confirmed].
3. When the keyboard timer's low byte reaches 0: reload 3 and scan the
   keyboard ($F495); a new key is stored with bit 7 set in $02DF. So the
   keyboard is scanned every 30 ms [AUG p74 "every 30 mS"] [confirmed].
4. When the cursor timer's low byte reaches 0: reload 25 and toggle the
   cursor ($0271 ^= 1, $F801 sets or clears bit 7, inverse video, of the
   cell under the cursor). The cursor flips every 0.25 s [AUG p32]
   [confirmed].
5. PLA, JMP $024A, which holds RTI.

No other VIA source is serviced and no other register is read: an
expansion IRQ left asserted would loop forever through this handler
unless its owner hooks $0244 [derived].

### 2.6 VIA behaviour the ROM relies on

From the data sheet [VIA figs. 13, 16, 29, "RESET"] and the ROM uses
[derived]:

- Free-running period N + 2 (2.4).
- Writing the T1 latches ($0306/$0307) does not restart the count; the
  next reload uses them. The tape writer sets the next half-period this
  way ($E6B2-$E6BD). MAME also clears the T1 flag on a T1L-H write
  [MAME `6522via.cpp` `VIA_T1LH`]; the ROM does not care.
- Reading T1C-L ($0304) clears the T1 flag ($E6C0); writing 1s to IFR
  clears flags ($EE2A).
- Timer 2 one-shot; reading T2C-H gives the high byte of the live count;
  writing T2C-H reloads the counter from the latch (the tape reader,
  $E727-$E72D, 6.5).
- Reading or writing ORB ($0300) clears the CB1 flag ($E71D); reading or
  writing ORA ($0301) clears CA1 (the printer routine).
- PB7 follows timer 1 when ACR bit 7 and DDRB bit 7 are both 1 (tape
  save).
- PB3 is an input: reading ORB returns the pin there and the output
  latch on the output bits.

The printer routine [ROM $F5C1-$F5E1]: byte to $0301, PB4 low then high,
then a wait for IFR bit 1 (CA1). With no printer, LPRINT waits for ever
unless the emulator acknowledges [derived].

## 3. The AY-3-8912

### 3.1 Connection

A GI AY-3-8912 (IC4) [SM App. 1] on VIA port A, BC1 = CA2, BDIR = CB2,
BC2 = +5 V (2.3), clocked at 1 MHz from the CPU clock [AUG p47, p52, p56;
MAME `AY8912(..., 12_MHz_XTAL / 12)`] [confirmed]. The 8912 has one 8-bit
I/O port (A, register 14); on the Oric it drives the keyboard columns
(section 5) [AUG p47, p74; SM p17] [confirmed].

### 3.2 Registers [AUG p46 fig. 3.1, p47-49] [confirmed with MAME `ay8910.cpp`]

| Reg | Bits | Use |
|---|---|---|
| R0/R1 | 8 + 4 | channel A tone period (fine, coarse) |
| R2/R3 | 8 + 4 | channel B tone period |
| R4/R5 | 8 + 4 | channel C tone period |
| R6 | 5 | noise period |
| R7 | 8 | mixer: bits 0-2 tone off A/B/C (0 = on), bits 3-5 noise off A/B/C, bit 6 port A direction (1 = output), bit 7 port B (absent on the 8912) |
| R8-R10 | 5 | volume A/B/C: bits 0-3 level, bit 4 = use the envelope |
| R11/R12 | 16 | envelope period (fine, coarse) |
| R13 | 4 | envelope shape: bit 0 hold, 1 alternate, 2 attack, 3 continue |
| R14 | 8 | port A data (keyboard columns) |

### 3.3 Formulas

Tone: f = 1 MHz / (16 x TP), TP 1-4095 (15.26 Hz to 62.5 kHz) [AUG p47].
Noise: 1 MHz / (16 x NP), NP 1-31 [AUG p47]. Envelope: one cycle of 16
steps at 1 MHz / (256 x EP), EP 1-65535 [AUG p49]. Volume is logarithmic:
every two steps halve the output (15 = 1.0, 13 = 0.5, 11 = 0.25, 9 =
0.125, 0 = off) [AUG p50 fig. 3.4] [confirmed by MAME's tables]. A
channel cannot be silenced through R7, only by volume 0 [AUG p48]. MAME's
model adds [MAME `ay8910.cpp`/`.h`] [one source]: period 0 acts as 1; a
channel with both tone and noise disabled outputs a constant 1, so volume
writes then play samples; the noise generator is a 17-bit shift register
fed with bit 0 XOR bit 3, output bit 0, stepped every 16 x NP clocks;
writing R13 restarts the envelope; the envelope has 16 steps on the AY.

### 3.4 Output

The three channel outputs are wired together into one mono signal, through
an LM386 amplifier (gain 20) to the internal speaker; the signal also
reaches the cassette socket [AUG p45; SM p16] [confirmed]. MAME mixes the
three channels to mono with 4.7 kohm loads [MAME `oric_common`].

### 3.5 What the ROM writes

At reset only R7 = $40 (all tones and noises enabled, port A output;
volumes stay 0) [ROM $F8BB-$F8BF] [derived]; the emulator should clear the
AY at reset (MAME does). The keyboard scan writes R14 every 30 ms
($F562-$F564). The preset sounds write R0-R13 from 14-byte tables through
$FA86 [ROM; entry addresses also in AM App. 9 pp269-270] [confirmed]:

| Routine | Table | Effect (registers set) |
|---|---|---|
| PING $FA9F (also CTRL-G) | $FAA7 | R0 = $18 tone A, R7 = $3E, R8 = $10 envelope, EP = $0F00, R13 = 0 (decay) |
| SHOOT $FAB5 | $FABD | R6 = $0F noise, R7 = $07 (noise only), R8-R10 envelope, EP = $0800, R13 = 0 |
| EXPLODE $FACB | $FAD3 | R6 = $1F, R7 = $07, R8-R10 envelope, EP = $1800, R13 = 0 |
| ZAP $FAE1 | $FB06 | R7 = $3E, R8 = 15; then R0 swept 0 to $6F with delays; R8 = 0 |
| key click $FB14 | $FB1C | R0 = $1F, R7 = $3E, R8 envelope, EP = $001F, R13 = 0 |
| CTRL key click $FB2A | $FB32 | as above with R0 = $2F |

SOUND, MUSIC, PLAY use the parameter block at $02E0 [AM App. 9 p270; AUG
p56] [confirmed]; their details are not needed by the hardware.

## 4. The ULA and the display

### 4.1 Clocks

A 12 MHz crystal (XT1) feeds the ULA's pin 7 [SM p15; MB1; ULARTL]
[confirmed]; AUG's functional diagram says 16 MHz [AUG p58 fig. 4.1], an
error. The ULA divides it by 2 for the 6 MHz dot clock and by 12 for the
1 MHz CPU clock [MB1; ULARTL; MAME] [confirmed].

### 4.2 Memory cycle: no contention

Each microsecond is 12 ULA states: states 8-11 (PHI high) are the CPU's
memory access; states 0-3 fetch the screen byte (phase 1); states 4-7 fetch
the character pattern (phase 2: in hires the same address is read again
and ignored) [ULARTL `rg_cnt`, `a_dma`, `en_dbuf`; MB1 "Control and
Sequencing"; AUG p57 "sequences of three"] [confirmed]. The horizontal
counter advances at state 8, so the CPU's cycle comes first and the
ULA's two fetches for the new count follow [ULARTL] [derived]. The video
fetches refresh the DRAMs [MB1] [one source]. The ULA never drives the
data bus [MB1]. Nothing ever stalls the CPU [AUG p31 RDY tied high; p64].

### 4.3 Line timing

A line is 64 us; a 6-bit horizontal counter runs at 1 MHz [MB1; MB2 p20;
ULARTL; AUG p64 "64 uS ... 40 uS is spent displaying"] [confirmed]:

| Count | What |
|---|---|
| 0-39 | the 40 cells are fetched (active lines only produce pixels) |
| 1-40 | the cells' pixels are output, 6 per count, 1 us after their fetch |
| 40-63 | horizontal blanking (black) |
| 50-53 | horizontal sync, 4 us (as output) |

Sources: [MB2 p19 (measured), p21; ULARTL `nx_hsync`, `hactive`]
[confirmed]; CLK puts sync at 48-53 [CLK `Video.cpp`]. The composite sync
has no serrations during vertical sync [MB2 p19]. The vertical counter
steps at the start of horizontal sync (count 49 to 50), not at count 0
[ULARTL `en_vcnt`] [one source]. The border is always black and is not
memory mapped [MB2 p22; ULARTL `active`] [confirmed].

### 4.4 Frame timing

| | 50 Hz | 60 Hz |
|---|---|---|
| lines per frame | 312 | **264** |
| active lines | 0-223 | 0-223 |
| blank after the picture | 224-255 | 224-235 |
| vertical sync (4 lines) | 256-259 | 236-239 |
| blank before the picture | 260-311 | 240-263 |
| frame length | 19968 us (50.08 Hz) | 16896 us (59.19 Hz) |

Sources: [ULARTL `vcnt_wrap` = 311/263, `vsync` = lines 256-259/236-239;
MB2 p21 "VFRAME ... 312 in 50Hz mode. [Changes to 264 in 60Hz mode]",
VSYNC "256,257,258 and 259 ... [Changes to 236,237,238,239 in 60Hz
mode]"; CLK 312/264 and vsync 256-259; DF-FORUM p15173 (ThomH,
2016-11-28) "It turns out it's 264 lines"] [confirmed]. The older figure
of 260 lines (MB1 of 1996, written before the die was read; MAME's comment
"260 lines in 60 Hz mode", MAME not implementing 60 Hz at all; the MiSTer
ula.vhd counting to 260) is superseded [MAME differs]. CLK's 60 Hz vsync
is at lines 234-237 [CLK differs by 2 lines].

When the 50/60 bit takes effect: the ULA compares the line counter with
263 or 311 continuously, and the mode register can change only while
attributes are decoded (counts 0-39 of lines 0-223), so the last mode
attribute seen in a frame decides where that same frame ends; "if a 50Hz
and 60Hz attribute are placed on screen at the same time, whichever is
seen LAST will govern the frame rate" [ULARTL `vcnt_wrap`, `en_ctrl`; MB2
p27] [confirmed]. CLK latches it at the start of the next frame [CLK
`run_for`] [CLK differs]. The BASIC ROM always writes 50 Hz attributes
($1A, $1E) [ROM $F9C9, $F933].

### 4.5 Serial attributes

Every screen byte is read and decoded [AUG p59-60; MB2 p26; ULARTL;
MAME; CLK] [confirmed]:

| Bits 6-5 | Meaning |
|---|---|
| 00 | attribute: bits 4-3 pick the register, bits 2-0 the value |
| 01, 10, 11 | data: in text a character code (bits 6-0), in hires 6 pixels (bits 5-0) |

Bit 7 of any byte, attribute or data, inverts that cell: both ink and
paper RGB are complemented for those 6 pixels only [MB2 p26 ("code 145 =
INVERSE and Red paper ... gives Cyan paper for that cell, and Red for the
remainder of the row"); ULARTL `rg_invert`; MAME; CLK] [confirmed].

| Byte (bit 7 = 0) | Register | Values |
|---|---|---|
| $00-$07 | ink | colour 0-7 |
| $08-$0F | style | bit 0 alternate character set, bit 1 double height, bit 2 flashing |
| $10-$17 | paper | colour 0-7 |
| $18-$1F | mode | bit 1: 1 = 50 Hz, 0 = 60 Hz; bit 2: 1 = hires, 0 = text; bit 0 unused |

Sources: [MB2 p26 table; ULARTL `en_ink`/`en_attr`/`en_paper`/`en_mode`;
AM p75 (ESC @ to ESC W, codes 0-23); MAME; CLK; MISTER] [confirmed]. So
$18 = text 60 Hz, $1A = text 50 Hz, $1C = hires 60 Hz, $1E = hires 50 Hz
[derived]. On hires pixel lines the alternate set and double height do
nothing (they only choose a character pattern), but **flashing applies**:
the RTL's `blink_pixel = (!rg_fcnt[4] || !rg_attr_blink) && rg_shift[5]`
ignores the mode, and MB1 has the flash clock modulate the pixel stream
after the shift register [ULARTL; MB1 "Attribute handling"] [confirmed].
(These notes first said all three style bits do nothing there, after MB2
p27; corrected 2026-11-19 from the RTL. MB2's PDF text can't be extracted
here to recheck its wording.)

Rules:

- **The attribute's own cell** shows 6 pixels of paper, using the paper
  in force *after* the attribute (a paper attribute shows its new
  colour), inverted if bit 7 is set [MB2 p26-27 "Paper colour (or inverse
  paper) will be generated"; ULARTL (registers and the empty shift
  register both take effect at the cell's first pixel); AUG p60; MAME;
  CLK] [confirmed].
- **Scope**: from that cell to the end of the scan line (one pixel row,
  not a character row), unless replaced [MB2 p22, p26; ULARTL] [confirmed].
- **Reset every line** (during horizontal sync): ink 7 (white), paper 0
  (black), style 0 (standard set, single height, steady) [MB1 registers
  "r0 ink = 111b ... r2 paper = 000b" (its prose says "white paper, black
  ink", a slip); MB2 p26; ULARTL `attr_reset`; MAME; CLK; MISTER]
  [confirmed].
- **The mode register is not reset**: it persists across lines and
  frames until another mode attribute [MB2 p27; ULARTL; MAME `m_pattr`;
  CLK] [confirmed].
- **Text/hires switch**: "takes effect from the next column", even in mid
  line; the next fetch address is computed from the same line counter in
  the new mode (text: $BB80 + 40 x (line / 8) + column; hires: $A000 + 40
  x line + column) [MB2 p27; AUG p62 (worked mixed-mode example);
  ULARTL `hires_line` uses the new mode; MAME; CLK] [confirmed].
- Attributes are decoded only inside the active area (counts 0-39 of
  lines 0-223) [ULARTL `en_ctrl`] [one source].

### 4.6 Text mode

Screen at $BB80, 40 columns x 28 rows = 1120 bytes, to $BFDF [AUG p60;
MB2 p22; ULARTL; MAME; CLK] [confirmed]. Line y (0-223) fetches row y / 8;
the pattern byte is at base + (code & $7F) x 8 + (y & 7), base $B400 for
the standard set and $B800 for the alternate set (style bit 0); pattern
bits 5-0 are the 6 pixels, bit 5 leftmost, 1 = ink; bits 7-6 of pattern
bytes are ignored [AUG p61; MB1; MB2 p30; ULARTL `a_dma1`, `rg_shift`;
MAME; CLK] [confirmed]. Codes 0-31 are attributes, so the patterns at
$B400-$B4FF and $B800-$B8FF are never shown; the alternate set's codes
112-127 would sit at $BB80-$BBFF, the screen itself [derived].

Row 0 is the status line; the ULA treats it like any row, the ROM keeps
the cursor out of it [MB2 p22 note 32; AUG p60] [confirmed].

### 4.7 Hires mode

Lines 0-199 fetch $A000 + 40 x line + column: 240 x 200 pixels,
$A000-$BF3F; bits 5-0 are pixels, bit 5 leftmost; bit 6 must be 1 or the
byte is an attribute (the ROM clears the hires screen to $40) [AUG p62;
MB2 p22, p26 note 41; ROM $F938-$F93F; ULARTL; MAME; CLK] [confirmed].
Lines 200-223 are forced to text whatever the mode register says: three
rows at $BF68-$BFDF (rows 25-27 of the text layout) [MB1; MB2 p22, p27;
ULARTL `hires_line = vcnt < 200 && hires`; MAME; CLK; MISTER]
[confirmed]. The character set base follows the mode register, not the
line-200 force: in hires mode the bottom rows use $9800 (standard) and
$9C00 (alternate) [MB1 "The line-200 force does *not* affect the base
address of the character set"; ULARTL `a_dma1`; MAME; CLK] [confirmed].
Double height is ignored on hires lines and works on the forced text rows
[MB1] [confirmed with ULARTL, where the pattern fetch is not used on hires
lines].

### 4.8 Double height, flashing, colours

**Double height** (style bit 1): the pattern row is (y / 2) & 7 instead
of y & 7, y being the line 0-223. Even text rows therefore show pattern
rows 0-3 (top halves) and odd rows 4-7 (bottom halves); text must start
on an even row [MB1 ("even lines have the top half ... explains the bug
where bottom halves of text appear above the top half"); ULARTL
`vcnt[3:1]`; MAME; CLK; MISTER] [confirmed]. The Atmos Manual's example
puts the top halves on "line 1" [AM p76]: its lines count from below the
status line, as PLOT's do (PLOT X,0 is screen row 1), and its line 10
`PRINT CHR$(12)` ends with a newline, so the message lands on screen rows
2 and 3. Run on the emulator with the rule above it shows right (checked
2026-11-19): no contradiction [derived].

**Flashing** (style bit 2): ink pixels are replaced by paper during the
off half of the flash clock; inverse still applies. The flash clock is the
top bit of a 5-bit frame counter: **16 frames on, 16 frames off** (32-frame
period, about 1.56 Hz at 50 Hz) [ULARTL `rg_fcnt[4]`; MB2 p19 "total
period 32 video fields", p30 "high for 16 fields, and then low for 16
fields ... just over 1.5Hz"; MISTER `lCTR_FLASH(4)`] [confirmed]. MAME and
CLK use bit 5 of their frame counters (32 on, 32 off) [MAME differs, CLK
differs]. MB1's "counting 32 frames before toggling" is ambiguous. The
counter's phase at power-on is undefined.

**Colours**: 3-bit RGB, bit 0 red, bit 1 green, bit 2 blue: 0 black,
1 red, 2 green, 3 yellow, 4 blue, 5 magenta, 6 cyan, 7 white [AM p75;
ULARTL `RED = ... rg_ink[0]`; MAME `RGB_3BIT`; DF-CODE] [confirmed]. The
RGB socket carries digital on/off levels plus composite sync [AUG p65];
the TV output goes through a PAL encoder PROM [AUG p68; SM p20], not
needed for an RGB picture.

### 4.9 What the ROM does with the screen

[ROM addresses as given] [derived], with the defaults confirmed by AUG
p61 ("paper attribute ... initialised to #17 ... ink attribute ... #0")
and AM App. 9 p272 (BGND $026B = paper + 16, FGND $026C = ink):

- **Character sets at reset**: chars 32-127 copied from ROM $FC78-$FF77
  (768 bytes) to $B500 ($F8D0, copy table at $F992). The alternate set is
  generated at $B900-$BAFF for codes 32-95 ($F816): 2 x 3 blocks, the
  code minus 32 giving bits 0-1 for pattern lines 0-2, bits 2-3 for lines
  3-4, bits 4-5 for lines 5-7, even bit = left block ($38), odd bit =
  right block ($07). ROM glyph $5F is the pound sign, $60 the copyright
  sign, $7F a solid block [ROM character set] [derived].
- **TEXT** ($F967, core $F9C9): copies $9800-$9F7F back to $B400 (only
  when leaving hires), writes $1A at $BFDF, waits three ticks ($FA07: so
  the ULA has met it), fills the status line $BB80-$BBA7 with spaces, sets
  the text window to rows 1-27, clears the screen through $0238 with $0C.
- **HIRES** ($F920): copies $B400-$BB7F to $9800, writes $1E at $BFDF,
  fills $A000-$BF3F with $40, puts the text window on the three bottom
  rows.
- The mode attribute at $BFDF is the last cell of line 223, so it rules
  the next frame from its first line [derived].
- **CLS** puts the paper attribute ($026B = $17, white) in column 0 and
  the ink attribute ($026C = 0, black) in column 1 of each row: black text
  on white, with 38 printable columns [AUG p61] [confirmed]. CTRL-]
  toggles protection of columns 0-1 ($026A bit 5) [AUG p61; AM App. 9].
- **Status line**: when CAPS is on, $07 (white ink) at column 35 and
  "CAPS" at columns 36-39 ($F75A); tape messages "Searching ..",
  "Loading ..", "Found ..", "Saving .." are ROM strings at $E50D-$E64F.
  The status line has no paper attribute, so it is white on black.
- The cursor is shown by toggling bit 7 (inverse) of the cell under it
  ($F801), every 0.25 s (2.5).

### 4.10 A model for the emulator [derived]

Per line y in 0-223, with ink = 7, paper = 0, style = 0 at the line's
start and mode carried over: for c in 0-39, hires = mode.hires and y <
200; byte = RAM[hires ? $A000 + 40y + c : $BB80 + 40(y >> 3) + c]; if
(byte & $60) == 0, update the register picked by bits 4-3 and draw 6 paper
pixels; else pattern = hires ? byte : RAM[(mode.hires ? $9800 : $B400) +
(style.alt ? $400 : 0) + (byte & $7F) x 8 + (style.double ? (y >> 1) & 7
: y & 7)] and draw bits 5..0 (ink for 1, paper for 0, ink forced to paper
when flashing and the flash clock is off); invert both colours of the cell
if byte bit 7. At the end of line 263 (60 Hz) or 311 (50 Hz) start a new
frame; lines 224 onwards are black. Rendering a whole line at once is
exact except for programs that write a cell during the microseconds
around its fetch; the fetch of cell c happens c us into the line,
together with CPU cycle c of the line (4.2).

## 5. Keyboard and the NMI button

### 5.1 The matrix

Rows are selected by PB0-PB2, columns by AY port A bits (0 = column
driven); PB3 reads 1 when any key at a driven column of the selected row
is down [AUG p74; SM p17, p33; ROM $F561-$F58F; MAME `update_keyboard`;
CLK `get_port_input`] [confirmed]. Several columns may be driven at once;
PB3 is then the OR (MAME, CLK; programs use it for "any key").

| Row (PB2-0) | col 7 ($7F) | col 6 ($BF) | col 5 ($DF) | col 4 ($EF) | col 3 ($F7) | col 2 ($FB) | col 1 ($FD) | col 0 ($FE) |
|---|---|---|---|---|---|---|---|---|
| 0 | 3 # | X | 1 ! | - | V | 5 % | N | 7 & |
| 1 | D | Q | ESC | - | F | R | T | J |
| 2 | C | 2 @ | Z | CTRL | 4 $ | B | 6 ^ | M |
| 3 | ' " | \ \| | - | - | - pound | ; : | 9 ( | K |
| 4 | right | down | left | left SHIFT | up | . > | , < | SPACE |
| 5 | [ { | ] } | DEL | FUNCT | P | O | I | U |
| 6 | W | S | A | - | E | G | H | Y |
| 7 | = + | - | RETURN | right SHIFT | / ? | 0 ) | L | 8 * |

(The value in the header is what the ROM writes to R14 to drive that
column alone.) Sources: the ROM's key table [ROM $FF78-$FFF7, decoded by
position] [derived]; the keyboard circuit diagram BN0138 issue 2 [SM p79];
MAME `INPUT_PORTS_START(oric)`/`(orica)`; CLK `Keyboard.hpp` [confirmed].
FUNCT (row 5, column 4) is the Atmos's extra key [AUG p74, p77 ("row 5,
... column 4 ... #EF"); MAME `orica`] [confirmed]. "-" marks an empty
position.

Codes the ROM produces [ROM $FF78 table] [derived]: cursor left $08,
right $09, down $0A, up $0B, DEL $7F, ESC $1B, RETURN $0D; letters with
CAPS (default on, $020C = $FF) upper case; SHIFT selects the second half
of the table; CTRL turns codes $40 and above into $00-$1F.

Physical Atmos layout [AUG cover photograph; AM p7]: `1-0 - = \`; `ESC
Q-P [ ] DEL`; `CTRL A-L ; ' RETURN`; `SHIFT Z-M , . / SHIFT`; `left down
SPACE up right FUNCT` [one source plus the matrix].

### 5.2 How the ROM scans [ROM $F495-$F58F; AUG pp74-75] [confirmed]

- Every 30 ms (2.5). If a key was down at the last scan, only its row and
  column are tested again, for auto-repeat: first repeat after $024E = 32
  scans (0.96 s), then every $024F = 4 scans (0.12 s) [ROM $F89F-$F8A6,
  $F4AF-$F4BD; AUG p76] [derived for the times].
- Otherwise a full scan: column masks $7F, $BF ... $FE (column 7 first),
  and for each, rows 7 down to 0 ($F561: R14 = mask, ORB = $B8 | row, a
  ~20 us delay, test PB3). The first pressed row found in a column ends
  that column. Column 4 results go to $0209 (modifier), others to $0208;
  key code = column x 8 + row, bit 7 set.
- So of the modifiers right SHIFT wins over FUNCT over left SHIFT over
  CTRL ("if either of the shift keys are pressed then the CTRL key will
  not be scanned") [AUG p75] [confirmed].
- The character goes to $02DF with bit 7 set; $0238/$023B readers take it
  from there [AUG p75; ROM $EB78] [confirmed]. Key click via $FB14/$FB2A
  unless $026A bit 3 (CTRL-F toggles it) [AUG p45; AM App. 9].
- The scan writes ORB with PB6 = 0: the tape motor is switched off
  whenever the keyboard is scanned, which is why tape I/O runs with
  interrupts off [derived; AUG p71-72].

An emulator can answer PB3 at once (no settling time) and must keep the
AY's R14 as the column state, written through the AY bus [derived].

### 5.3 The button underneath: NMI

The button in the hole under the case "resets the machine without wiping
out the program" [AM p8]; it drives the 6502's NMI, which is edge
triggered [AUG p30; MAME `nmi_pressed`; CLK `KeyNMI`] [confirmed]. NMI
vector $FFFA -> $0247, holding JMP $F8B2: the hardware set-up of the
reset path again ($F8B8, without the RAM test) and a BASIC warm start at
$C471 (the program stays) [ROM $F8B2-$F8B5] [derived]. The expansion
port's RESET is a power-up type reset [SM p13].

## 6. Cassette and TAP files

### 6.1 Hardware

Output: PB7 through a 22 kohm / 1 kohm divider and 47 nF, about 150 mV
peak to peak [AUG p71 fig. 5.3; SM p18] [confirmed]. Input: an inverting
buffer, then an op-amp with ~50 mV hysteresis and a transistor to CB1, in
phase with the tape signal [AUG p71 fig. 5.2; SM pp18-19] [confirmed].
Motor: PB6 drives a relay [AUG p34; SM p19]. Two speeds: fast 2400 baud
(default), slow 300 baud (`CSAVE "NAME",S`, `CLOAD "",S`) [AM p63; SM
p18] [confirmed].

### 6.2 Writing [ROM $E76A-$E788, $E65E-$E6C8] [derived]

Set-up ($E76A): IER = $7F (all VIA interrupts off: no cursor, no keyboard
during tape I/O; AUG p71-72 confirms), T2 low latch = $F4, PCR = $10,
DDRB = $FF, ACR = $C0 (timer 1 free-running with PB7 output, timer 2
one-shot), T1 = $00D0, ORB = $40 (motor on). PB7 then toggles at every
timer 1 time-out; the writer picks each half-period by setting the latch
to 208 or 416 ($E6B2) and waits for the time-out ($E6C0). With the N + 2
rule the half-periods are 210 us and 418 us.

| Bit | Fast mode | Slow mode |
|---|---|---|
| 1 | 210 us + 210 us (one cycle, ~2381 Hz) | 16 half-cycles of 210 us (8 cycles of ~2400 Hz, 3.36 ms) |
| 0 | 210 us + 418 us (~1592 Hz) | 8 half-cycles of 418 us (4 cycles of ~1200 Hz, 3.34 ms) |

[derived]; [confirmed] by MAME (`oric_tap.cpp`: 1 = 1 + 1 samples, 0 = 1
+ 2 samples at 4800 Hz; slow "4 periods at 1200 Hz for a 0 or 8 periods
at 2400 Hz for a 1"), CLK (`OricTAP.cpp` comment) and SM p18 ("in fast
mode each data bit is represented by one cycle of the carrier").

Byte ($E65E): one extra time-out first (an extra half-cycle at the
current length), then start bit 0, 8 data bits LSB first, a parity bit,
and 3 stop bits of 1: 13 bits [derived]. Parity: the bit is 1 when the
data has an even number of 1s, so data plus parity always has an odd
number of 1s ($E67A-$E67C; checked the same way by the reader at
$E6EA-$E6F2) [derived; MAME agrees ("if data has even parity, parity bit
will be 1")]. CLK writes the XOR of the data bits (even parity) [CLK
differs]; the real loader counts each such byte as an error in $02B1.
MAME writes 4 stop bits where its comment says 3 [MAME differs,
harmlessly].

### 6.3 File layout on tape

[ROM $E607-$E62D (save), $E4AC-$E4DF (load); AUG pp72-73 fig. 5.4]
[confirmed]:

1. Leader: 259 bytes of $16 ($E75A writes 3 + 256) [AUG p72 "259 bytes of
   #16"].
2. $24 (the "$" marker).
3. Nine header bytes, stored by the loader from $02B0 down to $02A8:

   | Tape byte | RAM | Meaning |
   |---|---|---|
   | 1 | $02B0 | unused for programs (array saves: integer flag) |
   | 2 | $02AF | unused for programs (array saves: string flag) |
   | 3 | $02AE | type: $00 BASIC, $80 machine code or memory block, $40 array (STORE) |
   | 4 | $02AD | autorun: 0 = no; non-zero = yes (CSAVE ... AUTO writes $C7, the AUTO token) |
   | 5, 6 | $02AC, $02AB | end address, high then low |
   | 7, 8 | $02AA, $02A9 | start address, high then low |
   | 9 | $02A8 | unused |

4. The name: up to 16 characters are kept by the loader, then a $00.
5. A short gap (~1.3 ms of continued 1-cycles; $E628 delay loop).
6. The data: end - start + 1 bytes, no end marker [ROM $E56C-$E57C; AUG
   p73; MAME; CLK; TAPTAP] [confirmed].

Type and autorun values: [ROM $E7FE-$E802, $E82F-$E831; TAPTAP; CLK
`Tape.cpp`; FILE] [confirmed]; AUG p73 gives them schematically ("Basic=0,
Machine Code=1", "AUTO IF 1 ELSE 0"). On load, machine code with autorun
jumps to the start address, BASIC with autorun runs [ROM $E8D6-$E8E5]
[derived]. The speed flag is $024D: 0 fast, non-zero slow [ROM
$E7B2-$E7B4, $E807-$E80B; CLK] [confirmed].

### 6.4 How the ROM reads [ROM $E71C-$E759, $E6C9-$E71B] [derived]

- Period ($E71C): read ORB (clears the CB1 flag), wait for IFR bit 4 (CB1
  rising edge, PCR $10), read T2C-H, restart timer 2 by writing $FF to
  T2C-H (count reloads to $FFF4), and compare the read high byte with $FE.
  Carry set ("short" = 1) when the time since the previous rising edge was
  under about 500 us ($FFF4 - $FE00). Timer 2 times each bit [AUG p32,
  p34; SM p19] [confirmed].
- Fast mode: one period per bit, short = 1, long = 0.
- Slow mode ($E6FC/$E6FF): skips one period, reads one; after a long one
  reads 2 more, after a short one 6 more, and calls it 1 if at least 4 of
  those were short (4 periods per 0, 8 per 1 in all).
- Byte ($E6C9): skips 1s until a 0 (start bit), 8 data bits LSB first,
  checks parity; stop bits are not read.
- Synchronisation ($E735): shifts bits in until the last 8 read $16, then
  needs three more $16 bytes, then skips bytes until $24 ($E4AF).

Because periods are measured rising edge to rising edge and every bit is
one period (fast) or whole periods (slow), the polarity of the signal fed
to CB1 does not matter [derived].

### 6.5 TAP files

A TAP file is the tape's byte stream without the bit framing: some $16
bytes (in practice 3 or 4; file(1) checks `16 16 16 24`), $24, the 9
header bytes, the name, $00, the data; several files are simply
concatenated, each with its own $16...$24 [MAME `oric_tap.cpp` state
machine; CLK `OricTAP.cpp` (returns to lead-in when a $16 follows the
data); TAPTAP; FILE] [confirmed]. Emulators lengthen the leader when they
turn a TAP into a signal (MAME: 0.25 s of silence, 512 x $16; CLK: 259
bytes), since the ROM needs a bit-sync plus three whole $16 bytes before
the $24 [derived from $E735-$E759]. After the name MAME inserts 100
1-bits and CLK a short gap; the ROM itself leaves only ~1.3 ms, during
which the loader prints "Found"/"Loading" and compares names [derived].
TAPTAP notes array (STORE/RECALL) blocks and the ROM's odd size field for
them.

Fast loading by trap: CLK intercepts the ROM's read-byte routine at $E6C9
(BASIC 1.1; $E630 in 1.0) and returns the next byte in A, with $024D (1.1;
$67 in 1.0) as the speed flag [CLK `Oric.cpp`] [one source].

## 7. The Microdisc

### 7.1 What it is

Oric's 3" disk system: the master drive holds the interface, which
plugs into the expansion port; up to three slave drives chain from it
[AUG p113-114]. On the interface: a WD1793 (IC9) clocked at 1 MHz (an
8 MHz crystal divided by 8 in a 74LS393), an FDC9216 data separator, an
8 KB EPROM, a control latch [MDSCH sheet 2; MAME `FD1793(config, fdc,
8_MHz_XTAL / 8)`] [confirmed]. It has its own reset button, which
reboots [SED p1; MAME's extension reset line].

### 7.2 Registers

| Address | Read | Write |
|---|---|---|
| $0310 | WD1793 status (reading clears INTRQ) | command |
| $0311 | track register | track register |
| $0312 | sector register | sector register |
| $0313 | data register | data register |
| $0314 | bit 7 = 0 while INTRQ is active; other bits 1 | control register (below) |
| $0318 | bit 7 = 0 while DRQ is active; other bits 1 | - |

Sources: [MAME `microdisc.cpp`; CLK `Microdisc.cpp`; MDROM uses $0310 as
command/status, $0311 track, $0312 sector, $0313 data, $0314, $0318]
[confirmed]. Mirrors [UNCERTAIN]: MAME decodes $0314 and $0318 exactly;
CLK mirrors $0314-$0317 and $0318-$031B and claims all of $0310-$03FF;
the MiSTer model (GPL, behaviour only) claims $0310-$031B (FDC
$0310-$0313, control $0314-$0317, DRQ $0318-$031B) and leaves
$031C-$03FF to the VIA. The EPROM uses only the addresses in the table.

Whether $0314's bit 7 is gated by the IRQ enable [UNCERTAIN]: MAME reports
INTRQ only while control bit 0 is set; CLK and the MiSTer model report raw
INTRQ. The EPROM reads it only with bit 0 set [MDROM $E3C1], so both work
for it.

Control register ($0314, write; all 0 at reset):

| Bit | Meaning | Sources |
|---|---|---|
| 0 | IRQ enable: FDC INTRQ drives the CPU's /IRQ | MAME; CLK; MiSTer model; MDROM $E3A4-$E3A9 [confirmed] |
| 1 | 1 = BASIC ROM enabled; 0 = ROMDIS asserted (BASIC off) | MAME (`P_ROMDIS`); CLK ("0 = disable"); MiSTer model [confirmed] |
| 2 | "DDS": data separator rate? The EPROM always sets it | MAME (name only), CLK ("TODO ... 1 = double") [UNCERTAIN] |
| 3 | WD1793 /DDEN: 0 = double density (MFM) | MAME `dden_w`; CLK [confirmed] |
| 4 | side select | MAME; CLK; MiSTer model [confirmed] |
| 5-6 | drive select 0-3 | MAME; CLK; MiSTer model [confirmed] |
| 7 | 0 = EPROM at $E000-$FFFF (when bit 1 = 0); 1 = EPROM off | MAME (`P_EPROM`); CLK ("0 = select"); MiSTer model [confirmed] |

The EPROM's values: $04, $24, $44, $64 for drives 0-3, plus $10 for side
1 [MDROM table $E3F3]; $06 to reach BASIC; $84 to run a DOS from overlay
RAM [MDROM $04E6 (page-4 copy), $ED40] [derived]. The head-load output
switches the drive motors on in both emulators [MAME `fdc_hld_w`; CLK
`set_head_load_request`] [confirmed].

### 7.3 Paging

| Bit 1 | Bit 7 | $C000-$DFFF | $E000-$FFFF |
|---|---|---|---|
| 1 | any | BASIC ROM | BASIC ROM |
| 0 | 0 | overlay RAM | EPROM (reads; writes lost) |
| 0 | 1 | overlay RAM | overlay RAM |

[MAME views; CLK `paged_item`; MiSTer model: ROMDIS from bit 1, MAP
asserted for $C000-$FFFF except where the EPROM answers] [confirmed],
consistent with MAP/ROMDIS (1.1). At reset the register is 0, so the 6502
fetches its RESET vector from the EPROM ($EB7E) [MAME `device_reset`; CLK;
MDROM] [confirmed].

### 7.4 How the EPROM drives the FDC [MDROM] [derived]

- A command ($E393): SEI, command to $0310, control bit 0 set.
- Data transfer loops ($E2EF, $E309) run with interrupts enabled: poll
  $0318 until bit 7 = 0, then move one byte through $0313. The loop never
  ends by itself; the end of the command is the FDC's interrupt.
- The EPROM's IRQ handler ($E3C0): if $0314 bit 7 = 0 it clears control
  bit 0, discards the interrupted loop's return frame (three PLAs), reads
  the status at $0310 (clearing INTRQ) and returns to the command's caller;
  otherwise it passes the interrupt to BASIC's handler through page 4
  ($E0E6).
- Timer 1's interrupt is disabled during disk commands (IER = $40 at
  $E3E3, $C0 again at $E3EB).

So the emulator needs INTRQ -> /IRQ gated by bit 0, INTRQ cleared by a
status read, DRQ visible at $0318, and double-density data arriving no
faster than one byte per 32 us.

### 7.5 Boot from the EPROM [MDROM] [derived]

1. $EB7E: SEI, CLD, stack; a delay of about 0.33 s; clears overlay RAM
   $C000-$C1FF, zero page and page 2; copies 123 bytes of paging code to
   $0480-$04FA (page 4), $0480 keeping a copy of the control register
   (initially $04).
2. Tests overlay RAM $C0A5-$C1A4 with $55/$AA; on failure fills the screen
   with $1A and shows "RV1 adjustment required" ($EEAE, $EE70; RV1 is a
   preset on the interface).
3. Reads BASIC's byte $C002 through a stub at $BFE0 with control $06: $EA
   means BASIC 1.0; otherwise it flags 1.1 at $C007 and points the page-4
   IRQ/NMI forwarders at $0244/$0247.
4. Sets up BASIC's workspace itself (it holds copies of both banners and
   "47870" / "37631 BYTES FREE"; HIMEM $97FF).
5. Sends FORCE INTERRUPT ($D8) to the FDC ($ECD3), finds "SYSTEM.DOS"
   (name at $EE41) in the directory, prompting "insert system disc" or
   reporting "No operating system on disc" (strings at $ED63, $ED77), then
   "BOOTUP.COM" ($EE4B).
6. Copies a stub to $BFE0 and jumps there: SEI, control = $84 (EPROM and
   BASIC off: 16 KB of overlay RAM), JMP ($C14B) into the loaded DOS.

AUG describes an earlier method for Oric DOS V1.1: the bootstrap copies
itself to RAM at $E000-$FFFF, loads the DOS to $7400-$A020, which copies
itself to $D400-$FFFF and prints "ORIC DOS V1.1" [AUG p114] [one source;
not what this EPROM does]. Sedoric lives in overlay RAM $C000-$FFFF and
uses page 4 [SED p1]; how a Sedoric master disk meets this EPROM's
SYSTEM.DOS lookup was not traced [UNCERTAIN].

MAME writes $42 to RAM $E000 at every reset ("Microdisc needs a
non-fully-zero high ram") [MAME `machine_reset`]. Nothing in the EPROM
reads RAM $E000 (no data read of $E000 in the disassembly; the boot path
clears and tests only $C000-$C1FF) [derived]; perhaps a DOS's warm-start
check [UNCERTAIN]. CLK's comment says the Microdisc "sort of assumes"
random RAM [CLK].

### 7.6 Drives and disk geometry

3" drives with 40 or 80 tracks [AUG p111]; the drives Oric supplied were
single-sided, the disks turned over by hand [AUG p113] [one source]. MAME
offers 3" double-sided 40- and 80-track drives (default 80) on four
connectors; CLK four drives of two heads [MAME `microdisc_floppies`; CLK
`DiskController`]. Oric DOS: 40 tracks of 16 sectors of 256 bytes, 640
sectors a side [MDM p35 note] [one source] (AUG's and MDM's "10 sectors"
figure is an illustration [AUG p111; MDM fig. 1]). Sedoric formats a
chosen number of sectors and tracks [SED "INIT"]; a typical Sedoric 3.0
image is 2 sides x 80 tracks x 17 sectors [DFW-DSK]; Sedoric's free-sector
bitmap is at track 20, sectors 2-3 [SED]. At 300 rpm and 250 kbit/s a
double-density track holds 6250 bytes [derived; DFW-DSK].

### 7.7 The MFM_DISK image format

[MAME `oric_dsk.cpp`; CLK `OricMFMDSK.cpp`; DFW-DSK; FILE] [confirmed]:

| Offset | Size | Content |
|---|---|---|
| 0 | 8 | "MFM_DISK" |
| 8 | 4 | sides (little endian) |
| 12 | 4 | tracks per side |
| 16 | 4 | geometry type: 1 or 2 |
| 20 | 236 | zero padding (header is 256 bytes) |
| 256 | 6400 per track | tracks; the first 6250 bytes of each are used |

Geometry 1: all tracks of side 0, then all of side 1 (track offset 256 +
6400 x (side x tracks + track)) [MAME; CLK; DFW-DSK] [confirmed]. MAME
accepts only type 1 and a file size of exactly 256 + 6400 x sides x
tracks. Geometry 2: interleaved by track (side 0 track 0, side 1 track 0,
side 0 track 1 ...) [DFW-DSK]; CLK's offset formula for it (track x
tracks x sides + side) does not match that [UNCERTAIN; images are almost
always type 1].

Track bytes are the decoded byte stream of the track (no clock bits)
[DFW-DSK; MAME; CLK]:

- $C2 $C2 $C2: index-mark sync, each $C2 with the missing clock (MFM word
  $5224 in MAME).
- $A1 $A1 $A1: address-mark sync ($4489 each); the next byte is $FE (ID
  record: track, side, sector, size code N, 2 CRC bytes) or $FB (data:
  128 << N bytes, 2 CRC bytes; $F8 = deleted data).
- Every other byte is ordinary MFM data (gaps of $4E, sync runs of $00).
- A reader must track whether it is inside a record, so that $A1/$C2
  bytes inside sector data are not taken as marks: MAME copies 7 bytes
  after an ID mark and 128 << N + 3 after a data mark; CLK likewise and
  lets a data record run past 6250 to 6400 (for BD-500 disks).
- Standard layout [DFW-DSK]: 80 x $4E, 12 x $00, C2 C2 C2 FC, 50 x $4E,
  then per sector 12 x $00, A1 A1 A1, FE + ID + CRC, 22 x $4E, 12 x $00,
  A1 A1 A1, FB + data + CRC, 54 x $4E; padding to the end. Sedoric
  shortens the first gaps to 40 x $4E.

### 7.8 The older ORICDISK format

256-byte header: "ORICDISK", sides, tracks, sectors per track (32-bit
little endian), zero padding; then 256-byte sectors, side 0 first, track
by track, sector by sector [DFW-DSK; FILE] [one source for the layout].
Neither MAME nor CLK loads it; `old2mfm` converts it [DFW-DSK].

### 7.9 The Jasmin, for contrast

A different interface: WD1770 at $03F4-$03F7, a 74LS259 control latch at
$03F8-$03FF, a 2 KB ROM at $F800-$FFFF, its DRQ wired to /IRQ, a boot
button; MAME also has a plain sector format of 41 tracks x 17 sectors x
256 bytes for it [MAME `jasmin.cpp`, `oric_dsk.cpp`] [one source]. Not
needed.

## 8. Reset and start-up

### 8.1 RESET (BASIC 1.1)

The power-on reset is an RC network on the 6502's /RES [SM p16]; the VIA
is reset with it (2.2). Sequence [ROM addresses] [derived]:

| ROM | Action |
|---|---|
| $F88F | LDX #$FF, TXS, CLI, CLD |
| $F894 | copies $F87C-$F88E to $0238-$024A (the page-2 vectors, 8.3) |
| $F89F | key repeat: $024E = $20, $024F = $04 |
| $F8A9 -> $FA14 | RAM test: writes 0 to $0500 and $FF to $4500, a 16 KB machine if $0500 changed; then tests $0400-$BFFF with $AA and $55 (about 2.3 s at ~48 cycles a byte; AM p3 "a few seconds"). HIMEM = $9800 when RAM reaches $C000 |
| $F8AC -> $F8B8 | hardware set-up: VIA ($F9AA); AY R7 = $40 ($F590); timers and the 100 Hz interrupt ($EDE0); $026A = 3 (cursor and screen on), $026B = $17 (paper white), $026C = 0 (ink black) ($F90E); CAPS on ($020C = $FF); TEXT ($F9C9: $1A at $BFDF, a three-tick wait, status line blanked, CLS); character set copy ($F982); alternate set ($F816); "CAPS" ($F75A) |
| $F8AF -> $ECCC | BASIC cold start: prints the banner, clears the program area from $0500, prints the free bytes ($9800 - $0501 = 37631) and " BYTES FREE", then "Ready" |

### 8.2 Messages

[ROM strings] [derived; the screen as described in AM p3] [confirmed]:
"ORIC EXTENDED BASIC V1.1" ($ED96), then "` 1983 TANGERINE" where ` ($60)
is the character set's copyright sign, then "37631 BYTES FREE" ($ED88
holds " BYTES FREE"), then "Ready" ($C3B2, printed with CR/LF around it;
AM shows it as READY) and the flashing cursor. Oric-1 BASIC 1.0 shows
"ORIC EXTENDED BASIC V1.0" and 47870 bytes free (the copies held in the
Microdisc EPROM at $EDAF-$EDDD) [MDROM] [derived].

### 8.3 Page-2 vectors

| Address | Contents at reset | Use |
|---|---|---|
| $0238 | JMP $F77C | character output to the screen |
| $023B | JMP $EB78 | read a key (from $02DF; bit 7 = new key) |
| $023E | JMP $F5C1 | printer output |
| $0241 | JMP $F865 | print a string on the status line |
| $0244 | JMP $EE22 | IRQ (the 6502 vector at $FFFE points here) |
| $0247 | JMP $F8B2 | NMI (the 6502 vector at $FFFA points here) |
| $024A | RTI | end of the IRQ handler |

[ROM $F87C-$F88E, $FFFA-$FFFF] [derived]; programs hook interrupts by
patching these [AUG p76 (an IRQ hook example)] [confirmed]. BASIC 1.0
uses $0228 (IRQ) and $022B (NMI) [ROM 1.0 vectors; MDROM] [derived].

### 8.4 Cold and warm start

Cold: RESET (power-on, the expansion RESET line, a disk interface's reset
button). Warm: the NMI button (5.3), which re-initialises the VIA, AY,
timers, screen and character sets but keeps the BASIC program
[ROM $F8B2; AM p8] [confirmed].

### 8.5 What the hardware must provide to reach "Ready" [derived]

- RAM at $0400-$BFFF that holds $AA and $55, with no mirror between $0500
  and $4500 (else the ROM decides it is a 16 KB machine), and page 2 RAM.
- The ROM at $C000-$FFFF, read only.
- VIA timer 1 interrupts: the TEXT routine waits three ticks on the
  interrupt-driven timer at $0276 ($FA07 -> $EEC9); without timer 1
  interrupts the boot hangs before the banner. The IRQ line must be
  level-sensitive and cleared by the IFR write.
- PB3 reading 0 when no key is down (else phantom keys and repeat).
- AY writes are accepted through the VIA without any reply; nothing is
  read back from the AY.
- The ULA only needs to draw; the ROM never reads anything back from it.

---

## Sources

Tags used in the text, then where to find them. Downloads are under
`~/Development/oric-dl/docs/` (untrusted copies, read as data).

- **AUG**: Leycester Whewell, *The Oric Advanced User Guide*, Adder
  Publishing, September 1984, ISBN 0 947929 01 0. Book page numbers (the
  scan has two book pages per PDF page: book page p is on PDF page p/2 +
  1). Chapters 1.7 (pp29-31), 2 (pp32-44), 3 (pp45-56), 4 (pp57-69), 5
  (pp70-83), 7 (pp111-122). Local: `defence-force/oric_advanced_user_guide.pdf`
  (from the Defence Force library); page images `aug-img/`.
- **AM**: Ian Adamson, *The Oric Atmos Manual*, Pan Books / Oric Products
  International, 1984. Pages p3-p4 (power-on), p8 (reset button), p63
  (tape speeds), p75-76 (attributes, double height), App. 9 pp269-272
  (ROM routines, system variables). Local: `atmos-manual/manual_atmos.pdf`
  (text layer `manual_atmos.txt`).
- **SM**: *Service Manual for the Oric-1 and Oric Atmos Microcomputers*,
  Oric Products International 1984, typed by S. D. Marshall 2003. Pages
  10-20 (technical description), 32-38 (fault finding), App. 1 (parts
  list), p79 (keyboard circuit BN0138 issue 2). http://www.48katmos.freeuk.com/servman.pdf;
  local `servman/servman.pdf`. (The French "Manuel technique" in
  `defence-force/` is a translation of the same text, not a second source.)
- **MDM**: *The Oric Microdisc Manual*, Oric Products International (fig.
  1, p35 notes). Local `defence-force/microdiscandoricdosmanual.pdf`.
- **MDSCH**: Microdisc circuit diagram, two sheets scanned by Fabrice
  Francès (sheet 2: XT1 8 MHz, IC13 74LS393, IC9 WD1793, IC12 9216). From
  "Peter's Oric-1 page"; local `regensburg/microdisc-1.gif`, `-2.gif`.
- **MB1**: Mike Brown, *The ORIC 1/ATMOS Unofficial ULA Guide*, version
  1.02, 1 June 1996, public domain with attribution.
  http://oric.free.fr/HARDWARE/ula.html; local `oricfree/ula.html`, `ula.txt`.
- **MB2**: Mike Brown, *Oric Atmos Unofficial ULA Guide* 2.00 with
  schematics, August 2018 (part 1 revised with oscilloscope measurements;
  part 2 from the die photograph). PDF page numbers: p18-22 (timings,
  combinatorial logic, modes), p26-27 (attributes), p30 (pixel output).
  https://oric.signal11.org.uk/files/pub/ula-dieshot/OricAtmosUnofficialULAGuide-2.00-WithSchems.pdf;
  local `signal11/ULAGuide-2.00.pdf`.
- **ULARTL**: Erik Persson, `oric-ula`, `design/oric_ula.v` (MIT, 2023):
  RTL checked against an automatic extraction of a transistor netlist taken
  from a die photograph (netlist by Mike Connors, Datel; processed by Mike
  Brown). https://github.com/erik-persson/oric-ula; local `erik/`.
- **MISTER**: MiSTer-devel `Oric_MiSTer`, `rtl/ula.vhd` (seilebost
  2001-2009, d18c7db 2013, BSD-style) cited; `rtl/microdisc.vhd`
  (Retromaster 2010, GPL-3.0) behaviour only.
  https://github.com/MiSTer-devel/Oric_MiSTer; local `mister/`.
- **MAME**: github.com/mamedev/mame master, fetched 2026-11-18/19, all
  BSD-3-Clause: `src/mame/tangerine/oric.cpp`;
  `src/devices/bus/oricext/oricext.cpp/.h`, `microdisc.cpp/.h`,
  `jasmin.cpp`; `src/lib/formats/oric_dsk.cpp/.h`, `oric_tap.cpp/.h`;
  `src/devices/machine/6522via.cpp`; `src/devices/sound/ay8910.cpp/.h`.
  Local `mame/`.
- **CLK**: Clock Signal, github.com/TomHarte/CLK master (MIT):
  `Machines/Oric/Oric.cpp`, `Video.cpp/.hpp`, `Keyboard.cpp/.hpp`,
  `Microdisc.cpp`, `DiskController.hpp`;
  `Storage/Tape/Formats/OricTAP.cpp`; `Storage/Tape/Parsers/Oric.cpp`;
  `Storage/Disk/DiskImage/Formats/OricMFMDSK.cpp`;
  `Analyser/Static/Oric/Tape.cpp`. Local `clk/` (Video.cpp re-fetched and
  identical, `clk-verify/`); commit 9136917 (2020-01-24) introduced the
  60 Hz mode.
- **VIA**: Rockwell R6522 data sheet (`datasheets/R6522_Rockwell.pdf`)
  and the R6522 programming text (`datasheets/6522-via.txt`): fig. 11
  (PCR), 12 (ACR), 13 (T1 latches), 16 (free-run N+1.5 / N+2), 29 (IFR),
  "RESET (RES)".
- **DF-FORUM**: forum.defence-force.org, "A possible software alternative
  to Vsync?" (topic 78): p15173 ThomH 2016-11-28 (264 lines), p19855 Chema
  2019-07-16 (19966). Local `defence-force/forum_p19406.txt`.
- **DF-CODE**: Defence Force, "Oric Coding Part 7" (video display,
  colour table). Local `defence-force/coding_part_7.html`.
- **DFW-DSK**: Defence Force wiki, "The DSK file format"
  (oric:hardware:dsk_disk_format, last modified 2016-12-28), via the
  Wayback Machine. Local `dfwiki/oric_hardware_dsk_disk_format.html`.
- **TAPTAP**: Chloé Avrillon, "Oric Tape Format (tap file format)", 2018,
  github.com/DJChloe/taptap, `Tape format documentation/Oric tap file
  format.pdf`. Local `taptap/oric_tap_format.pdf`.
- **FILE**: file(1) magic, `Magdir/oric` (Oric tape and image
  signatures). Local `filemagic/oric.magic`.
- **SED**: *SEDORIC DOS Version 2.0, A User's Guide*, Jon Haworth and
  Allan Whitaker, issue 3, 1992 (introduction, INIT, sector
  reservation). oric-soft.bedroomlan.org `files/SEDORIC/sedoric.asc`;
  local `sedoric/sedoric.asc`.
- **ROM $xxxx**: Atmos BASIC 1.1 (`basic11b.rom`, CRC32 c3a92bef)
  disassembled here; addresses as cited. **MDROM $xxxx**: the Microdisc
  EPROM (`microdis.rom`, CRC32 a9664a9c) disassembled here. Images in
  `~/Development/oric-dl/roms/`.

Not used as sources: Oricutron and Euphoric (no code read); the AY and
WD1793 data sheet scans in `datasheets/` (image only; AUG reproduces the
AY tables, MAME's `wd_fdc.cpp` is the reference for the 1793's commands).
