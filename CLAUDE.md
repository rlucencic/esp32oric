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

**State (2026-11-19): runs on the Tab5 and the MaTouch.** Done: the hardware research (`docs/oric-hardware.md`), the ROMs
(local, not in git), the core (`components/oric`: BASIC 1.1 to "Ready",
typing, text and hires with every attribute, the AY's sound, NMI, 50/60
Hz; BASIC 1.0 for the Oric-1), its host build (`make -C
components/oric/host run`, `check`), the board side (`main/`: the
setup's Model row, keyboards, ROMs from flash or the card, status, console;
checked on the Tab5), tapes (TAP files, CLOAD and CSAVE at once or in
real time, 2026-11-19: 348 originals on the host, the Tab5) and the
Microdisc (its EPROM, a WD1793 in real time, MFM_DISK images in drives A
and B written in place, 2026-11-19: Sedoric 2 and 3, Oric DOS and 22 of 29
original disks booting on the host, Sedoric on the Tab5). Next: `TODO.md`.

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

As esp32electron's (its `CMakeLists.txt`, `sdkconfig.defaults` without
the slow pages, `partitions.csv` and `main/` were the start):

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
- `flash` writes the ROMs into 64 KB slots of `oric_roms` (0x810000):
  `basic11b.rom` slot 0, `basic10.rom` slot 1, `microdis.rom` slot 2; a
  file of the same name in the SD card's `oric/roms` overrides one (any
  case; `roms/README.md`: where they come from). NVS namespace `oric`
  (`model`: 0 Atmos, 1 Oric-1; `tape_fast`: 1 fast, 0 real time;
  `microdisc`: 1 attached), drives `oricdisks` (`disk_a` the tape, `disk_b`
  and `disk_c` drives A and B). mDNS `oric-atmos.local`. The SD card's
  `oric/` folder: `.tap` tapes and `.dsk` (MFM_DISK) disks (blank ones
  `BLANKnnnn.tap`, an empty file, and `BLANKnnnn.dsk`, unformatted; until
  2026-11-19 tapes were `TAPEnnnn.tap`). The boards are shared with the
  other projects: after another one, a full `flash`.
- **The Tab5 after flashing (2026-11-19)**: the RTS hard reset at the
  flash's end (and `emu_console.py reset`) left it in the ROM's download
  mode ("boot:0x204 ... waiting for download", nothing on the console).
  `python -m esptool --chip esp32p4 -p PORT --after watchdog_reset
  read_mac` (in the IDF's environment) boots the app; ~10 s later the
  console answers.
- The MaTouch: its UART port, not native USB (the speaker's lines); opening
  that port resets the board, so a check is one session (a script on
  `emu_console.py`'s `open_port()` and `screenshot()`: waits, console
  commands, `L` screenshots in turn; wait ~15 s after opening). Its panics
  print on the native USB port (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`), not
  the UART: a reset loop leaves only the ROM's 115200-baud lines (garbage at
  921600). Setup screen: Model y 193, Tape y 253, Microdisc y 313, Sound y
  373, the rows' buttons at x 340 and 626, Start `t 850 532`. Both boards
  call themselves `oric-atmos.local`: use their IPv4 addresses.
- `../esp32emu/tools/emu_console.py PORT log N | lcd F.png | send "cmd" |
  info` with the IDF venv's Python (`-I`; `lcd` wants macOS's `sips`: on
  Linux a Pillow stand-in named `sips` first on PATH). Start on the setup
  screen: `t 1065 637` (Tab5), `t 850 532` (MaTouch). `log` resets the
  Tab5 (its boot is in the log); `info` and `send` don't.
- The web API (`curl http://oric-atmos.local/...`; this PC resolves the
  name to IPv6 only: the board's IPv4 address works): `curl -T game.tap
  .../disks/game.tap` uploads, `curl -X PUT --data "game.tap"
  .../api/drive/1` puts it in (drive 1 the tape, 2 A:, 3 B:; `DELETE
  .../api/drive/2` ejects), `curl -X POST ".../api/new?size=1"` makes a
  blank tape, `size=537856` a blank disk of 2 x 42 tracks (`1024256`: 80),
  `POST .../api/drive/2/offer` takes the offer of a disk in A: or B: while
  the Microdisc is off (switched on, a restart), `POST .../api/reset`,
  `.../api/status` (`board.load`: the cores' busy %; `offers`).
  `/api/status` got no answer before the machine's first start (the setup
  screen up after a boot) on 2026-11-19, the other requests did; after
  Start it answers (esp32emu's, not looked into yet).

## Layout

| Path | What |
|---|---|
| `components/oric/include/oric.h` | The core's API: config (model, ROM, the Microdisc's EPROM, RAM), frame, picture lines and the colours shown, keys by matrix position (`ORIC_KEY_*`, row * 8 + column) and those down, typing, the tape, the disks, sound, status (the VIA's registers), peek |
| `components/oric/oric_internal.h` | The machine's state (`oric_t`: CPU, VIA, RAM, ROM, the frame's line, the ULA's mode register), the time constants (64-cycle lines, 312 / 264 lines, 224 picture lines), the modules' interfaces |
| `components/oric/oric.c` | Init, reset (RAM noise, the mode register), the 6502's bus (page pointers; page 3 and $9800-$BFFF writes through callbacks), the VIA's wiring (the AY's bus, PB3, the printer's ACK, the IRQ), the frame loop, NMI, status |
| `components/oric/via.c`, `via.h` | The 6522, from esp32cbm's (the 1541's; the user's own MIT code): ports with their control lines, timer 1 (one-shot / free running, PB7), timer 2, IFR/IER; timers lazy (`via_update()` counts up to the CPU's cycle; the CPU's run ends at the next underflow through `m6502_lower_until()`) |
| `components/oric/psg.c` | The AY-3-8912: registers through BDIR/BC1, port A (the keyboard's columns), tones, noise, envelope, the mono mix through a capacitor into the sound ring (31250 Hz) |
| `components/oric/keyboard.c` | The matrix (PB3), keys from other tasks, typed text (`oric_type()`) |
| `components/oric/tape.c` | The cassette: TAP files indexed, played as CB1 edges while the motor runs and something reads them, BASIC 1.1's tape routines trapped (CLOAD and CSAVE at once), saves onto the tape's end |
| `components/oric/microdisc.c` | The Microdisc: page 3's $0310-$031B, the control register, paging (BASIC, the EPROM, the overlay RAM), INTRQ to the IRQ |
| `components/oric/wd1793.c` | Its WD1793 and drives: Type I-IV commands in real time (steps, the head's load, searches, bytes 32 us apart, worked out when the CPU looks), MFM_DISK images in memory, written tracks back to their files |
| `components/oric/video.c` | The ULA: each line decoded as it starts into cells (pattern, ink, paper, inverse, flash), three frames for the presenter, decoding skipped while nothing shown changes; `oric_render_line_rgb565()` |
| `components/oric/host/` | The host build (`Makefile`, `host_main.c`): see Testing |
| `main/main.c` | The Oric as an `emu_machine_t`: memory (ROM and RAM in PSRAM), start (the model's ROM, the Microdisc's EPROM), the setup's Model, Tape and Microdisc rows, the drives (the tape, disks A: and B:, blank ones, the disks' offer), disk writes flushed, the web page's card, console `k`/`K`/`T`/`N`/`W` |
| `main/keymap.c` | HID usages to the Atmos's keys by position (a US layout's, its shifted symbols too) |
| `main/rom_store.c` | The ROMs: a slot of `oric_roms` (mapped, copied, unmapped), the SD card's `oric/roms` over it |
| `main/settings.c` | NVS `oric`: the model, fast tapes, the Microdisc |
| `tools/tapesmoke.py` | Every TAP in a folder through the host build (CLOAD"", fast or real time): a summary and a contact sheet |
| `tools/loadertap.py` | A test tape whose loader reads CB1 itself (the ROM's routines copied to RAM) |
| `tools/mkdisk.py` | A test disk the EPROM boots: its SYSTEM.DOS (a small 6502 assembler inside) runs the WD1793's commands and leaves their results at $0800 and on the screen; `--check` the tracks it wrote |
| `tools/disksmoke.py` | Every MFM_DISK image in a folder booted through the host build with a Microdisc: commands, errors, pictures, a contact sheet |
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
  `.tap` files are the byte streams one after another (the ROM's routines
  trapped for a fast load, or the bits played through CB1: both, below).
- **The Microdisc** (docs 7): a WD1793 at 1 MHz at $0310-$0313, the
  control register at $0314 (IRQ enable, ROM disable, MAP, side, drive,
  density), INTRQ at $0314 and DRQ at $0318 (bit 7 low), claimed
  $0310-$031B (the schematic's 74LS138: the rest of page 3 is the VIA's);
  the 8 KB EPROM boots a SYSTEM.DOS (Oric DOS 0.6's directory and file
  format: Sedoric, Oric DOS) from drive A, or stops at "No operating
  system on disc"; disks as MFM_DISK images (a header, then the tracks as
  raw MFM bytes, 6400 a track, 6250 a turn).
- **Reset** (docs 8): BASIC 1.1's RESET at $F88F; NMI at $0247, IRQ at
  $0244 (page-2 jumps); "Ready" needs the RAM test, the VIA's timer 1
  interrupt and the keyboard scan's answers.
- Open questions (docs, Summary): the flash rate (16/16 frames from the
  die RTL, MAME and Clock Signal 32/32), when a 50/60 switch takes effect,
  the power-on state of the ULA and RAM, floating reads, the Microdisc's
  $0314 status gating (raw here, as CLK; everything run works either way)
  and MFM_DISK geometry type 2 (no image uses it).

## The core (`components/oric`), as written

- **Time**: the 6502's 1 MHz cycles. A frame runs line by line
  (`oric_run_frame()`): each picture line (0-223) is decoded as it starts,
  then the CPU runs to the line's end, stopping at the VIA's timer
  underflows (`run_to()`: `via_update()`, and `m6502_repoll()` when the
  interrupt came before the instruction's poll cycle), then the AY's two
  samples. After line 223's decode the mode register sets the frame's
  length (312 or 264 lines: the last 50/60 attribute rules that same frame,
  the RTL). NMI (`oric_nmi()`, any task) is taken as the next frame starts.
- **Memory** (`oric.c`): RAM and the ROM through the 6502 core's page
  pointers, flat at 1 MHz; page 3 the VIA (register = address & 15)
  through the callbacks, writes to $9800-$BFFF too (a changed byte sets
  `screen_touched` for two frames), writes to $C000-$FFFF to a sink page
  while BASIC is there (`oric_map_high()`: BASIC, or what the Microdisc
  pages in; the tape's traps only with BASIC in). The sound's ring (mono),
  the drives' tables and the controller's buffer are in PSRAM
  (`ORIC_SLOW_ALLOC`): the core keeps ~3 KB of internal RAM (see the
  lessons). Power-on: noise with bit
  6 set in $0400-$FFFF (no stray attributes on the picture while the RAM
  test runs; the overlay RAM's for Sedoric), zeros in pages 0-3 (see the
  lessons); the ULA's mode 50 Hz text.
- **The VIA's wiring**: port A out to the AY's bus at every change of ORA,
  DDRA or the PCR (`psg_bus(CB2, CA2, port A)`); port A in is the AY's
  register in its read cycle; PB3 in is the keyboard's sense for the row
  on PB0-2 and the columns low in the AY's port A; PB4's rising edge (the
  printer's strobe) acknowledges at once on CA1 (no printer: LPRINT doesn't
  hang); PB6 the motor while it's an output driven high (`tape_motor()`;
  the inputs' pull-up at reset doesn't count); CB1 the tape's edges; the
  IRQ is a level on the 6502's IRQ bit 0.
- **The AY** (`psg.c`): registers masked to their bits, R13 restarts the
  envelope, an address with its high nibble set deselects the chip (the
  data sheet's mask); port A reads R14 while R7 bit 6 makes it an output,
  else $FF. Sound in 8-clock steps: tone half-periods of TP steps, noise
  and envelope every other step (17-bit LFSR, bit 0 ^ bit 3; 16 envelope
  steps with continue/attack/alternate/hold), levels halving every two
  steps (15 = 6000), three channels summed (mono, both sides), a DC
  blocker (y kept with 16 fraction bits), two samples a line averaged over
  their 32 clocks: 31250 Hz into a 8192-frame ring (`oric_audio_render()`).
- **The ULA** (`video.c`): docs 4.10's model per line; a cell is 16 bits
  (pattern 0-5, ink 6-8, paper 9-11, inverse 12, flash 13); flashing hides
  the ink for 16 of 32 frames (the RTL's counter from 1) **on hires lines
  too** (the RTL's `blink_pixel`; the notes had it otherwise, corrected).
  Three frames of 224 x 40 cells (PSRAM on the boards); a decoded frame
  that differs from the latest becomes the latest (`oric_screen_signature()`
  moves, also when the flash phase turns over a frame with flashing
  cells). A frame is not decoded at all while nothing in $9800-$BFFF
  changed and its starting mode equals the last decoded one's (the mode it
  leaves is then the recorded one). `oric_render_line_rgb565()`: 240
  pixels, 3-bit RGB.
- **The keyboard** (`keyboard.c`): `oric_key()` from any task (atomic bits
  by row), typed text (`oric_type()`: a key held 3 frames, then 2 up; its
  SHIFT or CTRL a frame before it; capitals plain while CAPS is on, with
  SHIFT when off; small letters plain; control codes as CTRL + letter,
  `\x14` CTRL-T toggles CAPS; `\n`/`\r` RETURN, ESC, DEL). After a RETURN
  the next key waits until BASIC waits for one (1.1: the PC in its key
  loop, `$C5E8`/`$023B`/`$EB78`, at a frame's end, 3-250 frames; other ROMs
  25 frames): see the lessons.
- **The tape** (`tape.c`, docs 6): a TAP file read whole into PSRAM (4 MB
  at most) and indexed (each file's leader, `$24`, header, name, data; a
  file the scan can't follow is searched for again at `16 16 16 24`).
  *Played* as CB1 edges at their cycles (`tape_edge_at` an event in
  `run_to()`; `tape_sync()` before every VIA access): each bit a cycle from
  rising edge to rising edge, 1 = 210 + 210 us, 0 = 210 + 418 (the ROM's
  writer, timer 1's N + 2); 300 baud (1 = eight short cycles, 0 = four of
  418 + 418) when BASIC 1.1's `$024D` says so as the motor starts. A byte:
  three 1s, the 0 start bit, 8 data bits from bit 0, parity (1 for an even
  count of ones). 64 more `$16`s in front of each file's leader (TAP files
  have 3 or 4; the ROM's sync wants a bit-synchronised one and three whole
  ones), 100 1s after a name (the loader prints and compares meanwhile),
  two 1s after the tape's last byte (a bit is measured to the next rising
  edge). It plays while the motor runs and something reads it: IFR read 500
  times in a frame or CB1's interrupt enabled starts it (fast: two such
  frames, real time: one), two frames under 50 reads stop it (TAP files
  lost the real tapes' gaps). *Fast* (the default; BASIC 1.1 only, its
  routines' first bytes checked): `$E4`, `$E6`, `$E7` read through the bus
  callback while the tape stands still (`tape_rom_read()`: the ROM's bytes
  always), and a read of a routine's first byte stops the run after that
  instruction; if the PC then is where it leads (a JSR's target with the
  return on the stack), `tape_trap()` does the routine: the sync `$E735`
  (on to the next file's `$24`, X = 0; at the tape's end from its start,
  once a motor start), the byte reader `$E6C9`, the data loop `$E4E0` (the
  file at once, `,V` counted in `$025C`; a file the tape ends in loads as
  far as it goes), and with any tape in the save side: the leader `$E75A`
  (three `$16`s), the byte writer `$E65E`, the data loop `$E62E`, onto the
  tape's end, into the file when the motor stops (or 50 frames after the
  last). A routine it can't serve (the tape out, a name not on it) goes on
  in the ROM, and the load traps rest until the motor starts again (the
  ROM reads on in real time). *Real time*: the load traps off; the sync
  only watched (a search starting at the tape's end rewinds it once). The
  status (`oric_tape_status()`): the file at the head, motor, playing,
  files loaded and saved.
- **The Microdisc** (`microdisc.c`, docs 7; `oric_config_t.disk_rom` set:
  attached): $0310-$031B claimed (the FDC, the control register and INTRQ,
  DRQ), the rest of page 3 the VIA's; the control register (0 at reset:
  the EPROM's RESET vector) pages $C000-$FFFF (`microdisc_map()`: BASIC
  with bit 1, else overlay RAM and the EPROM at $E000, bit 7 overlay RAM
  all through); only a change of bits 1 or 7 remaps, and then only
  $C0-$FF (Sedoric writes the register ~180 times a frame); INTRQ to the
  6502's IRQ bit 1 while bit 0 is set, $0314 shows it raw.
- **The WD1793** (`wd1793.c`): an image read whole (PSRAM), its tracks the
  bytes of a turn: every disk turns in phase from cycle 0, byte i under
  the head while (cycles / 32) % 6250 == i; a track is parsed for its IDs
  and data marks (records skipped whole, as MAME's reader does) and kept
  until it changes. Commands in real time, an event (`fdc_event_at`, in
  `run_to()`) a phase: Type I a step at a time at r1r0's 6/12/20/30 ms
  (restore 255 steps at most, seek, step, in, out; u; the head stops at
  0 and 84), verify after 30 ms (an ID of the track, else a seek error
  after 5 index pulses; no disk: the same); Type II/III after HLT (1 s
  after HLD rose: the schematic's LS123, CLK's 0.96 s) and E's 30 ms: the
  first ID wanted from then (track, sector, side compare with C; CRC
  errors noted and passed; RNF at the 5th index pulse), its data mark
  within 43 bytes, the bytes 32 us apart, worked out when the CPU looks
  (DRQ at $0318, the data register: the byte due, lost data if one was
  missed; a write's byte late goes out as $00), the CRC checked, multiple
  sectors with m; Write Sector at the ID's CRC + 22 (12 zeros, three $A1,
  the mark, the data, its CRC, a $4E) into the track; Read Address from
  the $FE on (the sector register gets the track); Read Track and Write
  Track from the index ($F5 an $A1 presetting the CRC, $F6 a $C2, $F7 the
  CRC: data can't hold $F5-$F7); Force Interrupt ends a command, $D8 an
  interrupt now (cleared as any other: see the lessons), $D4 every index.
  Status: Type I's (head loaded, track 0, the index pulse, 2 ms a turn)
  or II/III's; INTRQ cleared by a status read or a command. Not ready
  with no disk (Type II/III end at once). HLD (the motor) drops after 15
  idle turns. Written tracks are marked; `oric_disk_flush()` writes them
  into the file in place (the board: after 2 s without writes, at eject);
  a file that can't be opened for writing is a write-protected disk.
  Geometry 1 and 2 (2 untried), 1-2 sides, up to 100 tracks.

## The board side (`main/`)

- **Setup screen** (Tab5, since the Microdisc row): Model at y 227 (Atmos
  x 426, Oric-1 x 781), Tape at y 298 (Fast, Real time: live, no
  restart), Microdisc at y 369 (Off, On: a restart), Sound at y 440,
  Start/Restart/Back `t 1065 637`, the setup corner `t 1265 15 2600`
  (held).
- **The drives** (`disks_config_t`, folder `oric`, NVS `oricdisks`):
  Tape, A: and B: (`accepts()`: `.dsk` for A: and B:, anything else for
  the tape). The tape: the core's `oric_tape_insert()` (an empty file is a
  blank tape), its light the motor, its reads and writes the files loaded
  and saved at once, its detail the file at the head ("File 2 of 3:
  "CODE", machine code at #0400-#0405, AUTO") or "At its end". The disks:
  `oric_disk_insert()` (the whole image into PSRAM), their light the
  controller's head load on the drive selected, reads and writes in
  sectors, the detail their sides, tracks and the head's track. New:
  `BLANKnnnn.dsk` (2 x 42 or 2 x 80 tracks, unformatted) or
  `BLANKnnnn.tap`. A disk in A: or B: while the Microdisc is off is
  offered (esp32emu's offers: the setup's disk page, the web page): taking
  it switches the Microdisc on and restarts. Disk writes go to the card
  from `every_second()` through `emu_paused()` once 2 s passed without
  one.
  The status lines show the WiFi name and IP: keep them out of published
  images.
- **Picture**: 240 x 224 at `view_width` 800 (1.33 : 1, as a TV frames the
  Oric's picture), nearest pixel: 960 x 720 on the Tab5 (4 columns a
  pixel, 3 or 4 rows a line). Circles come out wide, as on a TV.
- **Keyboards** (`keymap.c`): by position; Tab and Esc (and `) ESC, Alt
  FUNCT, Ctrl CTRL, the Shifts LEFT/RIGHT SHIFT; Backspace and Delete DEL,
  the cursor keys the Oric's; Caps Lock, keypad * and + typed through
  `oric_type()` (CTRL-T, SHIFT-8, SHIFT-=: the modifier a frame early); F12
  and Pause the NMI button.
- **Console**: `k <n>` taps matrix key n (row * 8 + column, decimal),
  `K <n> <0|1>`, `T <text>` (`\n` RETURN; ~6 frames a character), `N` the
  NMI button, `W` rewinds the tape. `i` (the tool's `info`): frames,
  registers, text/hires, 50/60 Hz, the tape's position, motor, playing,
  the Microdisc (on/off, its control register, the WD1793's command and
  status, track, sector, drive and side, busy).
- **Web page's card** (`status()`, the state now; the reply ~2-2.6 KB of
  8 KB): System (the IRQ lines asserted, interrupts masked, the
  Microdisc), ROM (BASIC, the EPROM, what $C000-$FFFF holds now), Memory
  (page 3; BASIC's program, variables, arrays and free bytes from its
  zero-page pointers $9A-$A7, the same in 1.0 and 1.1; "not set up" when
  they don't make sense, e.g. a DOS in charge), Screen (mode, the
  character sets' addresses, 50/60 Hz), Colours (the colours the latest
  picture shows as swatches, inverse applied: `oric_screen_usage()`;
  flashing cells, double height, the alternate set), Sound (the AY's
  channels, the noise's and the envelope's rates, port A), VIA (T1, T2,
  IER and IFR by name, port A and the AY's bus from the PCR, port B's
  keyboard row), Keyboard (the keys down now: `oric_keys_down()`, CAPS),
  Cassette, Microdisc (the WD1793's last command by name, its status bits
  decoded as Type I's or the others', the drive, side, motor, IRQ, what
  $C000-$FFFF holds, sectors read and written) or Expansions. The drives:
  A: and B: with their free space for Sedoric disks (`oric_disk_sector()`
  reads track 20 sector 2, its bitmap: $FF, $00, the free sectors, the
  files, tracks, sectors a track, ..., the tracks with $80 for two sides;
  other DOSes no bar).

## Lessons (each one cost a debugging session)

- **Page 2 noise typed a phantom key**: power-on RAM noise everywhere put
  $E8 in $02DF, the key buffer (bit 7 = a new key): BASIC's first input
  read an 'h' after "Ready". The ROM never clears it before reading.
  Noise only from $0400 on (where the RAM test goes, and the overlay RAM:
  Sedoric, below).
- **CAPS is a mask, not a flag**: $020C is $FF with CAPS on, $7F off (ANDed
  onto the key table's bit 7, the small letters: ROM $F50C). Taken as
  "non-zero = on", typed capitals came out small with CAPS off.
- **The setup screen while running rebooted the Tab5** (twice,
  2026-11-19): esp32emu's supervisor showed it with its stack in PSRAM,
  where turning the caches off asserts (`esp_task_stack_is_sane_cache_disabled()`):
  the Model row's NVS write in `set()`, then `esp_partition_mmap()` of the
  Oric-1's ROM slot on the Restart. Fixed in esp32emu the same day (the
  main task shows the setup screen; its lessons); the Oric's own
  workarounds (a deferred save, the partition mapped once) went again, and
  its Model row with Restart is the check.
- **A typed key and its SHIFT together can miss each other**: the ROM's
  full scan takes ~4.5 ms, columns 7 down to 0; a key in column 2 pressed
  after the scan passed the modifiers' column 4 is read unshifted (':'
  came out ';'). The modifier now goes down a frame earlier.
- **BASIC empties the key buffer on its way back to "Ready"** (`$C993`):
  typed lines after a command lost their first key ("OKE#403", "RINT"),
  program lines (no command run) didn't. A real Oric drops a key typed
  that early too; typing waits for BASIC's key loop after a RETURN.
- **A trapped ROM page must read as the ROM**: JAMs in copies of the tape
  routines' pages made a fine trap until Poopy, whose loader copies
  `$E6C9`-`$E759` into RAM and runs it: the copy had the JAMs. The pages go
  through the bus callback now (the ROM's bytes), and a routine counts as
  entered only when the PC after its first byte's instruction is where
  that instruction leads.
- **The ROM's sync loops through its own entry**: `$E735` is entered again
  for every bit it shifts in (`BNE $E735`). A trapped sync that declined
  (the name not on the tape) was taken as entered again at each bit: the
  tape was synced and stopped every bit and never moved. A declined load
  trap now rests until the motor starts again.
- **A TAP file has lost the tape's gaps**: Demodiam keeps the motor on for
  an 8 s intro before its next CLOAD, Damsel clicks the relay during play;
  playing whenever the motor ran, the tape ran past the next file
  (Demodiam got the wrong one). It plays only while something reads it.
- **The parity bit needs an edge after it**: a bit is measured from its
  rising edge to the next; a tape ending on its last byte's parity bit
  left the ROM waiting for ever ("Loading ..", the last byte). The tape
  ends with two 1s, as the Spectrum's does with an edge.
- **TAP images a byte short**: Fire Flash's header says `$2000-$67FF`,
  its file ends a byte before (an exclusive end, likely): the ROM waited
  for the last byte. A file the tape ends in loads as far as it goes (and
  logs it); in real time it still waits, as an Oric would.
- **The ROM's bit reader runs in the trapped pages**: playing in real
  time, every fetch went through the bus callback: core 1 53-55% on the
  Tab5 (16% idle). While the tape plays the pages are the ROM's again
  (nothing to trap then): 25-27%.
- **Force Interrupt $D8's INTRQ is cleared as any other**: the data sheet
  has it held until a $D0; the EPROM sends $D8 at boot and never a $D0, so
  held, its IRQ handler took the next Read Sector for ended at once (lost
  data, "No operating system on disc"). Clock Signal clears it on a status
  read or a command, and the EPROM boots.
- **Sedoric checks the overlay RAM for itself**: its boot sector sums
  $C980 on and takes 0 for a warm start (Sedoric still there: 4 sectors
  loaded, not 60). With the overlay RAM all zeros at power-on it jumped
  into code it never loaded (an IRQ vector into zeros, BRKs for ever after
  its banner). The power-on noise covers the overlay RAM now; MAME's
  "$42 at $E000" is the same fix.
- **Read Address's bytes follow the $FE**: searching to the ID's CRC, as
  Read Sector does, left all six bytes in the past when the transfer
  began: lost data every time. Sedoric reads an address to find the head
  on another drive (DIR B), so drive B failed only in a new session
  (straight after INIT Sedoric had the directory in memory).
- **The sound ring in internal RAM starved the MaTouch's web server**:
  `psg.c`'s 32 KB stereo ring was static, in internal RAM; with the
  Microdisc's tables the MaTouch had 22 KB free, 13.8 KB the largest
  block, and a download (an 8 KB DMA buffer after the transfer task's
  stack) answered "Out of memory". The ring mono in PSRAM, the disk tables
  there too: 57 KB free, 31.7 KB the largest, 40 KB at the lowest.
- **Write Track's data can't hold $F5-$F7**: they write an $A1 sync, a $C2
  and the CRC, as on a 1793. `mkdisk.py`'s first test pattern (i ^ $A5)
  had them from byte 80, and its sector read back with a CRC error; a
  test loop that feeds Write Track must also keep up with 32 us a byte (a
  JSR a byte doesn't).

## Testing

- **Host build**: `make -C components/oric/host run` (ROM `roms/
  basic11b.rom`, 300 frames, `build/screen.ppm`), or `build/oric_host
  <rom> [frames] [out.ppm]` (~0.1 ms a frame). Environment: `ORIC_MODEL=1`
  (an Oric-1: give `basic10.rom`), `ORIC_TEXT=1` (the text screen as
  characters at the end), `ORIC_TYPE=<frame>:<text>|...` (`\n` RETURN;
  `oric_type()` drops text given while the last is still being typed: ~6
  frames a character), `ORIC_KEYS=<frame>:<key>:<0|1>;...`, `ORIC_NMI=
  <frame>`, `ORIC_WAV=<file>`, `ORIC_SHOTS=<n>`, `ORIC_LOG=<n>` (with
  `ORIC_AY=1` the AY's registers, `ORIC_ZP`), `ORIC_STOP=<pc>[:<frame>]`,
  `ORIC_TRACE=<pc>:<n>`, `ORIC_WATCH=<addr>[:<frame>]` (any RAM address:
  its page's writes go through the callback; `ORIC_RING=<n>` instructions
  before), `ORIC_DUMP=<addr>:<len>;...`, `ORIC_DASM=<addr>:<n>`.
  Tapes: `ORIC_TAPE=<file>` (CSAVE writes onto it: a copy),
  `ORIC_NEWTAPE=<file>` (a blank one made and put in), `ORIC_REALTIME=1`,
  `ORIC_TAPE_LOG=1` (the tape's state each second); loads and saves are
  logged (`tape: loaded "NAME" ($0501-$051A)`). With a tape in, `$E4`,
  `$E6` and `$E7` read as `$FF` to the disassembler (`m6502_peek()`: the
  bus callback's pages).
- `make -C components/oric/host check`: "37631 BYTES FREE" and "Ready"
  after 300 frames, a typed FOR loop's output, typed mixed case with CAPS
  off; CSAVE onto a blank tape (its header's bytes), CLOAD by name fast and
  at 300 baud in real time; `tools/loadertap.py`'s tape fast and in real
  time; `tools/mkdisk.py`'s disk booted by the Microdisc's EPROM (`MDROM=`,
  `roms/microdis.rom`), its 24 results (restore, seek with verify, read,
  write and read back, read address, a multiple read to RNF, steps with
  and without u, a seek past the image, side compare both ways, Write
  Track and its sector read back, Force Interrupt) and `--check` of the
  tracks it wrote.
- Host disks: `ORIC_MICRODISC=<microdis.rom>` (a Microdisc),
  `ORIC_DISK0`..`3=<file.dsk>` (written in place: copies),
  `ORIC_NEWDISK=<file>:<sides>:<tracks>` (blank, in A),
  `ORIC_INSERT=<frame>:<drive>:<file>[;...]` (empty: eject),
  `ORIC_DISK_LOG=1` (each command and its end: errors, track, sector, the
  head). The written tracks go to the file at the end (the eject).
- **Tapes** (2026-11-19, host): CSAVE onto a blank tape (the ROM's format:
  `16 16 16 24`, the header, the name, the data), CSAVE ,S and ,AUTO, a
  memory block (`A#400,E#405`); loaded back fast and in real time at 2400
  and 300 baud; a BASIC loader with AUTO that CLOADs a machine-code file
  by name and CALLs it; a search past another file; CLOAD ,V (0 errors,
  also after the tape's end); a name not on the tape (read once, then the
  wait); `tools/loadertap.py`'s loader. **348 original TAP files**
  (oric-soft.bedroomlan.org's TAPES section, 206 archives; Axayac's
  didn't download) with `tools/tapesmoke.py`, fast (3000 frames) and in
  real time (20000): all read to their end or as far as their programs
  ask (the rest wait for a key, or for a part on another TAP: Pacman,
  Tenis, Sokoban's .COM; Scribe's SCRIBE.2/.3 aren't on its tape;
  Diamant's last part resets unless the earlier parts ran), the two modes
  ending in the same place (bar tapes with bytes after their last file:
  one byte, the real time's started one, and tapes still loading at the
  cut-off); the contact sheets show title screens and menus. Not checked
  here: whether each program then runs (`TODO.md`, original software).
- **The Microdisc** (2026-11-19, host): `tools/disksmoke.py` over
  oric-soft.bedroomlan.org's DISKS section (29 MFM_DISK images, all
  geometry 1): 22 boot without a controller error (Sedoric 2 and 3 to
  their menus, Oric DOS 1.13's menu, Minised, Randos to BASIC, Forth 83
  3.1, FontEdit, the picture disks, Patricia, Modem, the Magic X game
  demo, Slime's menu); the seven with Stratsed's boot sector show "No
  operating system on disc" (the Telestrat's: `TODO.md`); FT-DOS (a Jasmin
  disk) gets "insert system disc". Sedoric 3: its menu, DIR, SAVE, LOAD
  and RUN, the file still there in a new session from the written image,
  drive B, INIT of a blank disk in B (both sides formatted, 1412 sectors
  free, D/42/17) and a file saved onto it and loaded back. The whole
  29-image run takes 2 s.
- **The Microdisc on the Tab5** (2026-11-19): the setup's Microdisc row;
  a disk put in A: with it off brings the offer (web API), taking it
  restarts with the Microdisc; `mkdisk.py`'s disk: PASS, the same 24
  results, and the image downloaded back is byte for byte the host's;
  Sedoric 3 to its menu in ~4 s (77 sectors), SAVE, NEW, LOAD, RUN, DIR,
  eject and insert again and LOAD, INIT of a blank disk made by the web
  API in B: and a SAVE onto it; the Magic X demo from its disk (207
  sectors) at 50 frames a second; core 1 16% idle, 23-27% while the disk
  works or the game runs, core 0 4-14%. Left on the card: a clean
  `sedoric3.dsk` and `demomagx.dsk` (the test disks deleted); the
  Microdisc switched off again.
- **The web page's card** (2026-11-19, the Tab5 and the MaTouch, from
  `/api/status`): BASIC's program and free bytes (an empty one: "no BASIC
  program"; Sedoric's menu program, 808 bytes), HIRES with PAPER 4 showing
  black, blue and white (the text rows' black on white), keys held through
  the console ("LEFT SHIFT A"), the Microdisc's rows with Sedoric, A:'s
  free space (2304 of 365568 bytes: the 9 sectors its DIR shows).
- **On the MaTouch** (2026-11-19, over its UART): the setup's rows (the
  Microdisc's too), BASIC's "Ready", typed lines; Cobra at once (core 1
  42-46%, core 0 10-14%); CSAVE onto a blank tape made by the web API (36
  bytes to the card) and CLOAD of it in real time (the Tape row), LIST,
  RUN, 50 frames a second; the Microdisc: `mkdisk.py`'s disk PASS, its
  image downloaded back byte for byte the host's (after the internal RAM
  fix: before it the download failed), Sedoric 3's boot (77 sectors, core
  1 40-51% meanwhile), SAVE, NEW, LOAD, RUN, DIR at 100%. Once (the third
  session) it reset ~3 s after every boot, just after WiFi connected, 18
  times in 100 s, the internal RAM then 14 KB at its lowest; six sessions
  since, with the same steps, booted once each. Not explained: the
  panics go to the native USB port and no reset reason was caught. Left
  on its card: `cobra.tap` (in the tape drive) and a clean
  `sedoric3.dsk`; the Microdisc off.
- **Tapes on the Tab5** (2026-11-19): the setup's Tape row (Fast / Real
  time, kept after a reflash); tapes uploaded through the web API; Cobra
  (4 files) at once and running; CSAVE onto a blank tape made by the API,
  the file downloaded back (51 bytes, as on the host); CLOAD in real time
  of it, LIST; 007 (28 KB) in real time at 100%, 50 fps, core 1 25-27%
  (16% idle); `loadertap.py`'s tape fast: the first file through the
  traps, the second played to its loader.
- Checked on the host (2026-11-19): BASIC 1.1's banner, "37631 BYTES
  FREE" and "Ready" at frame ~123 (the RAM test); the cursor toggles every
  250,050 cycles (25 ticks of 10,002); typed PRINT, FOR/NEXT, LIST, RUN,
  every symbol on the keys; CTRL-T and small letters; HIRES with CURSET,
  CIRCLE, DRAW, PAPER 4 and INK 3 (yellow on blue, the three text rows
  below), TEXT back; double height (the Atmos Manual's own example), the
  alternate set, paper and ink attributes, an inverse red-paper attribute
  (a cyan cell, then red: MB2's example), flashing 16 frames on and 16
  off; PING at 2604 Hz, SOUND 1,10/30/100 at 6250, 2084 and 624 Hz, ZAP's
  R0 $00-$6F in 0.17 s, SHOOT and EXPLODE's noise decays (0.5 s and
  1.4 s), key clicks; NMI's warm start keeps the program; a 60 Hz attribute
  at $BFDF makes frames 16,896 cycles. BASIC 1.0 (`ORIC_MODEL=1`): its
  banner, "47870 BYTES FREE", PRINT 2+2.
- **On the Tab5** (2026-11-19): the setup screen (Model, Sound, Tapes),
  Start: the banner, "Ready" and the cursor; a program typed through the
  console (`T`), LIST and RUN; HIRES with PAPER 4, INK 3 and two CIRCLEs;
  `N` (TEXT again, the program kept); the Model switched three times with
  Restart (Atmos and Oric-1, "47870 BYTES FREE" and PRINT 3*3 from ROM slot
  1), the choice still there after a reboot; again with esp32emu's fix and
  the workarounds gone (the save straight from `set()`): Model changes
  with Restart and the volume changed while running, no reboot, the setup
  screen using ~5.5 KB of the main task's 16 KB. 50 frames a second, 100%
  speed, core 1 16-17% busy, core 0 4-10%. Not checked: the sound on the
  speaker (by ear), Bluetooth and USB keyboards by hand, the MaTouch (not
  connected; its build is clean).

## Where things are (outside git, this PC)

- `~/Development/oric-dl/`: the research downloads (`docs/`: the Atmos
  manual, the AUG's pages, the service manual, data sheets, the Microdisc
  and Sedoric manuals, MAME's, Clock Signal's and the MiSTer core's
  sources for comparison; `reference/`: MAME's oric.cpp, microdisc.cpp,
  jasmin.cpp; `roms/`: MAME's ROM sets), 79 MB. The software: of
  oric-soft.bedroomlan.org's ~730 files (`bedroomlan_index.tsv` lists
  them all; `bedroomlan_fetch.tsv` how each came: the site times out, the
  Wayback Machine's copies), its TAPES section (206 archives,
  `bedroomlan/TAPES/`; unpacked in `tapes/`: 348 TAP files, no extension
  in most), its DISKS section (31 archives, `bedroomlan/DISKS/`, the last
  17 from the Wayback Machine's 2024 captures; unpacked in `disks/`: 29
  MFM_DISK images) and 15 others; oric.org's 347 titles listed
  (`oricorg/titles.tsv`, nothing downloaded): `TODO.md`, original
  software.
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
