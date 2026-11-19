# esp32oric: an Oric Atmos on the MaTouch ESP32-S3 7" and the M5Stack Tab5

An Oric Atmos (1984; the Oric-1 as a second model): a 6502A at 1 MHz, 64
KB of DRAM (48 KB seen, 16 KB under the ROM), the ULA (HCS10017: the
picture with its serial attributes, address decoding), a 6522 VIA (the AY's
bus, the keyboard rows, the tape, the 100 Hz interrupt), an AY-3-8912
(three channels; its I/O port selects the keyboard columns) and BASIC 1.1
in a 16 KB ROM; the Oric Microdisc (a WD1793, its EPROM, 3" drives) is
required (esp32emu's plan). Both boards, as the Electron and the QL. The
plan: esp32emu's CLAUDE.md, "To do" (the Oric Atmos's entry). **What's
next: `TODO.md`**.

**State (2026-11-19): early. Nothing builds or runs yet.** Done: the
hardware research (`docs/oric-hardware.md`, complete), the ROMs (local,
not in git), the core's public API (`include/oric.h`), its internal state
and module interfaces (`oric_internal.h`) and the 6522 (`via.c`, not yet
compiled). Everything else is in `TODO.md`, in order.

The board side (tasks, LCD, touch, SD, WiFi, BLE, setup screen, web page,
console) and the 6502 core (`m6502`) are the shared library in
`../esp32emu`; its CLAUDE.md has the runtime and the board-level lessons.
Changes there need all its projects built. An SDL desktop sibling
(`sdloric`) is planned: keep `components/oric` platform independent.

The machine is our own (MIT), written from the Oric's documentation (the
Atmos manual, the Oric Advanced User Guide, the service manual, the
Microdisc manual, the WD1793 and AY-3-8912 data sheets, the ULA's die
netlist) and the community's; emulators (MAME's oric.cpp and
microdisc.cpp, BSD-3; Clock Signal, MIT; Oricutron, GPL; the MiSTer core)
only to compare behaviour against, never copied. The research notes:
`docs/oric-hardware.md` (each fact with its source; a Summary of what is
certain and what is not).

## Build, flash, debug (the Linux PC)

As esp32electron (copy its `CMakeLists.txt`, `sdkconfig.defaults`,
`partitions.csv`, `main/` when they're written, `TODO.md` 3):

```sh
# Tab5 (/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_60:55:F9:FB:03:03-if00)
bash -c '. ~/.espressif/tools/activate_idf_v5.5.6.sh >/dev/null 2>&1; \
  idf.py -B build-tab5 -DIDF_TARGET=esp32p4 -DSDKCONFIG=$PWD/build-tab5/sdkconfig build'
# ... -p PORT flash (writes the roms/ too), app-flash for the app only
# MaTouch (its UART port: /dev/serial/by-id/usb-1a86_USB_Serial-*, 921600)
bash -c '. ~/.espressif/tools/activate_idf_v5.5.6.sh >/dev/null 2>&1; idf.py build && idf.py -p PORT flash'
```

- The two builds share `managed_components/`: build one after the other,
  never both at once.
- Planned, as the siblings: `flash` writes the ROMs into 64 KB slots of
  `oric_roms` (0x810000): `basic11b.rom` slot 0, `basic10.rom` slot 1,
  `microdis.rom` slot 2; a file of the same name in the SD card's
  `oric/roms` overrides one (`roms/README.md`: where they come from). NVS
  namespace `oric` (model, Microdisc on/off), drives `oricdisks`. mDNS
  `oric-atmos.local`. The SD card's `oric/` folder: `.tap` tapes and
  `.dsk` (MFM_DISK) disks. The boards are shared with the other projects:
  after another one, a full `flash`.
- The MaTouch: its UART port, not native USB (the speaker's lines); opening
  that port resets the board.
- `../esp32emu/tools/emu_console.py PORT log N | lcd F.png | send "cmd" |
  info` with the IDF venv's Python (`-I`). Start on the setup screen: `t
  1065 637` (Tab5), `t 850 532` (MaTouch).

## Layout

| Path | What |
|---|---|
| `components/oric/include/oric.h` | The core's API: config (model, ROM, RAM), frame, picture lines, keys by matrix position (`ORIC_KEY_*`, row * 8 + column), typing, sound, status |
| `components/oric/oric_internal.h` | The machine's state (`oric_t`: CPU, VIA, RAM, ROM, the frame's line, the ULA's mode register), the time constants (64-cycle lines, 312 / 264 lines, 224 picture lines), the interfaces of `video.c`, `psg.c`, `keyboard.c` (to be written) |
| `components/oric/via.c`, `via.h` | The 6522, from esp32cbm's (the 1541's; the user's own MIT code): ports with their control lines, timer 1 (one-shot / free running, PB7), timer 2, IFR/IER; timers lazy (`via_update()` counts up to the CPU's cycle; the CPU's run ends at the next underflow through `m6502_lower_until()`) |
| `components/oric/host/` | The host build (empty: `TODO.md` 2) |
| `main/` | The board side (empty: `TODO.md` 3) |
| `docs/oric-hardware.md` | The hardware research: memory map, VIA, AY, ULA and display, keyboard and NMI, cassette and TAP, Microdisc and MFM_DISK, reset; sources |
| `roms/` | The ROMs (not in git; `roms/README.md` says where they come from) |

## The machine (from the notes; what the code must do)

- **Memory** (docs 1): RAM $0000-$BFFF, page 3 ($0300-$03FF) the VIA
  (mirrored every 16 bytes) unless an expansion pulls I/O CONTROL low (the
  Microdisc at $0310-$031F), BASIC at $C000-$FFFF read only; writes there
  are lost while MAP is high, the RAM underneath shows with MAP low (the
  Microdisc's control register; its EPROM at $E000-$FFFF in place of
  BASIC while the boot runs). No CPU/video contention: each microsecond
  has one CPU slot and two video fetches (docs 4.2): the 6502 runs flat at
  1 MHz.
- **The VIA** (docs 2): PA the printer data and the AY's data bus, PB0-2
  the keyboard row, PB3 key sense (1 = pressed), PB4 printer strobe, PB6
  the cassette motor, PB7 tape out, CA1 printer ACK, CA2 the AY's BC1, CB2
  its BDIR (BC2 tied high), CB1 tape in. The ROM sets PCR $DD idle, $FF
  latch address, $FD write data. Timer 1 free running at N = 10000: an IRQ
  every 10002 us; the handler scans the keyboard every 30 ms and blinks the
  cursor every 0.25 s.
- **The AY-3-8912** (docs 3): clocked at 1 MHz, mono (the three channels
  mixed), port A an output: the active-low keyboard column select.
- **The ULA and the picture** (docs 4): 12 MHz crystal, 6 MHz pixels;
  64 us lines, 40 cells of 6 pixels, 224 picture lines; 312 lines at 50 Hz,
  264 at 60 Hz (the die netlist, not 260); vsync at 256-259 / 236-239.
  Serial attributes: a byte with bits 6-5 = 00 is an attribute (bits 4-3:
  ink, style, paper, mode), shown as paper in its own cell; bit 7 inverts
  a cell; ink 7, paper 0, standard character set, single height, steady
  at every line's start; the mode register (text/hires, 50/60 Hz)
  persists, the last 50/60 attribute in a frame sets its length, a
  text/hires switch takes effect at the next cell; lines 200-223 are
  always text. Text at $BB80 (characters at $B400, alternate $B800),
  hires at $A000 (characters at $9800/$9C00). Double height by line
  parity, flashing, colour bits 0 red, 1 green, 2 blue.
- **The keyboard** (docs 5): 8 rows x 8 columns, modifiers in column 4;
  `oric.h`'s `ORIC_KEY_*` is the matrix (row * 8 + column). The button
  underneath raises NMI (BASIC's warm start).
- **Tapes** (docs 6): fast (a cycle a bit: 210 us + 210 or 418 us) and
  slow (8 x 2400 Hz or 4 x 1200 Hz) encodings; bytes framed as a 0 start
  bit, 8 data bits LSB first, odd parity, 3 stop bits; a 9-byte header;
  `.tap` files are the byte streams one after another (the ROM's read
  routine can be trapped for a fast load, or the bits played through CB1).
- **The Microdisc** (docs 7): a WD1793 at 1 MHz at $0310-$0313, the
  control register at $0314 (IRQ enable, ROM disable, MAP, side, drive,
  density), status at $0314 and DRQ at $0318; the 8 KB EPROM boots Oric
  DOS or Sedoric; disks as MFM_DISK images (a header, then the tracks as
  raw MFM bytes, 6400 a track).
- **Reset** (docs 8): BASIC 1.1's RESET at $F88F; NMI at $0247, IRQ at
  $0244 (page-2 jumps); "Ready" needs the RAM test, the VIA's timer 1
  interrupt and the keyboard scan's answers.
- Open questions (docs, Summary): the flash rate (16/16 frames from the
  die RTL, MAME and Clock Signal 32/32), when a 50/60 switch takes effect,
  the power-on state of the ULA and RAM, floating reads, the Microdisc's
  $0314 status gating and page-3 decoding, MFM_DISK geometry type 2,
  Sedoric's boot lookup.

## Where things are (outside git, this PC)

- `~/Development/oric-dl/`: the research downloads (`docs/`: the Atmos
  manual, the AUG's pages, the service manual, data sheets, the Microdisc
  and Sedoric manuals, MAME's, Clock Signal's and the MiSTer core's
  sources for comparison; `reference/`: MAME's oric.cpp, microdisc.cpp,
  jasmin.cpp; `roms/`: MAME's ROM sets), 79 MB. The software download
  was stopped early: 15 of oric-soft.bedroomlan.org's ~730 files
  (`bedroomlan_index.tsv` lists them all) and oric.org's 347 titles
  listed (`oricorg/titles.tsv`, nothing downloaded): `TODO.md` 7.
- `roms/*.rom` here (not in git): `roms/README.md` (MAME's sets or
  oric-soft; the CRCs to check).

## Working with the user

- Expects changes built (both boards), flashed and checked on the board,
  then a short report of what was verified and what wasn't. Flashing ends
  the session on the board: say so first.
- Asks "what's next": answer with `TODO.md`'s first item. Keep the list in
  order: a finished item moves into this file (what it is, how it was
  checked) and leaves `TODO.md`.
- Keep their private data (WiFi name, IP, SD card contents) out of
  anything meant for publishing (the setup screen's status lines: cut
  them out of pictures).
