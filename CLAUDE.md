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

**State (2026-11-19): runs on the Tab5; the MaTouch builds, not tried
yet.** Done: the hardware research (`docs/oric-hardware.md`), the ROMs
(local, not in git), the core (`components/oric`: BASIC 1.1 to "Ready",
typing, text and hires with every attribute, the AY's sound, NMI, 50/60
Hz; BASIC 1.0 for the Oric-1), its host build (`make -C
components/oric/host run`, `check`) and the board side (`main/`: the
setup's Model row, keyboards, ROMs from flash or the card, status, console;
checked on the Tab5). Next: `TODO.md`.

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
  (`model`: 0 Atmos, 1 Oric-1; the Microdisc's to come), drives
  `oricdisks`. mDNS `oric-atmos.local`. The SD card's `oric/` folder:
  `.tap` tapes (listed and uploaded, they don't play yet), later `.dsk`
  (MFM_DISK) disks. The boards are shared with the other projects: after
  another one, a full `flash`.
- **The Tab5 after flashing (2026-11-19)**: the RTS hard reset at the
  flash's end (and `emu_console.py reset`) left it in the ROM's download
  mode ("boot:0x204 ... waiting for download", nothing on the console).
  `python -m esptool --chip esp32p4 -p PORT --after watchdog_reset
  read_mac` (in the IDF's environment) boots the app; ~10 s later the
  console answers.
- The MaTouch: its UART port, not native USB (the speaker's lines); opening
  that port resets the board.
- `../esp32emu/tools/emu_console.py PORT log N | lcd F.png | send "cmd" |
  info` with the IDF venv's Python (`-I`). Start on the setup screen: `t
  1065 637` (Tab5), `t 850 532` (MaTouch).

## Layout

| Path | What |
|---|---|
| `components/oric/include/oric.h` | The core's API: config (model, ROM, RAM), frame, picture lines, keys by matrix position (`ORIC_KEY_*`, row * 8 + column), typing, sound, status |
| `components/oric/oric_internal.h` | The machine's state (`oric_t`: CPU, VIA, RAM, ROM, the frame's line, the ULA's mode register), the time constants (64-cycle lines, 312 / 264 lines, 224 picture lines), the modules' interfaces |
| `components/oric/oric.c` | Init, reset (RAM noise, the mode register), the 6502's bus (page pointers; page 3 and $9800-$BFFF writes through callbacks), the VIA's wiring (the AY's bus, PB3, the printer's ACK, the IRQ), the frame loop, NMI, status |
| `components/oric/via.c`, `via.h` | The 6522, from esp32cbm's (the 1541's; the user's own MIT code): ports with their control lines, timer 1 (one-shot / free running, PB7), timer 2, IFR/IER; timers lazy (`via_update()` counts up to the CPU's cycle; the CPU's run ends at the next underflow through `m6502_lower_until()`) |
| `components/oric/psg.c` | The AY-3-8912: registers through BDIR/BC1, port A (the keyboard's columns), tones, noise, envelope, the mono mix through a capacitor into the sound ring (31250 Hz) |
| `components/oric/keyboard.c` | The matrix (PB3), keys from other tasks, typed text (`oric_type()`) |
| `components/oric/video.c` | The ULA: each line decoded as it starts into cells (pattern, ink, paper, inverse, flash), three frames for the presenter, decoding skipped while nothing shown changes; `oric_render_line_rgb565()` |
| `components/oric/host/` | The host build (`Makefile`, `host_main.c`): see Testing |
| `main/main.c` | The Oric as an `emu_machine_t`: memory (ROM and RAM in PSRAM), start (the model's ROM), the setup's Model row, the Tape drive (`.tap` names only, for now), status rows, console `k`/`K`/`T`/`N` |
| `main/keymap.c` | HID usages to the Atmos's keys by position (a US layout's, its shifted symbols too) |
| `main/rom_store.c` | The ROMs: a slot of `oric_roms` (mapped, copied, unmapped), the SD card's `oric/roms` over it |
| `main/settings.c` | NVS `oric`: the model |
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
  `screen_touched` for two frames), writes to $C000-$FFFF to a sink page.
  Power-on: noise with bit 6 set in $0400-$BFFF (no stray attributes on
  the picture while the RAM test runs), zeros elsewhere (see the lessons);
  the ULA's mode 50 Hz text.
- **The VIA's wiring**: port A out to the AY's bus at every change of ORA,
  DDRA or the PCR (`psg_bus(CB2, CA2, port A)`); port A in is the AY's
  register in its read cycle; PB3 in is the keyboard's sense for the row
  on PB0-2 and the columns low in the AY's port A; PB4's rising edge (the
  printer's strobe) acknowledges at once on CA1 (no printer: LPRINT doesn't
  hang); PB6 is the motor (status only so far); the IRQ is a level on the
  6502's IRQ bit 0.
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
  `\x14` CTRL-T toggles CAPS; `\n`/`\r` RETURN, ESC, DEL).

## The board side (`main/`)

- **Setup screen** (Tab5): Model at y 269 (Atmos x 440, Oric-1 x 795),
  Start/Restart `t 1065 637`, the setup corner `t 1265 15 2600` (held).
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
  NMI button. `i` (the tool's `info`): frames, registers, text/hires,
  50/60 Hz.
- **Web page's card** (`status()`): System, ROM, Screen (mode, 50/60 Hz),
  Sound (the AY's channels: tone frequency, noise, volume or envelope),
  VIA (timer 1's period, its interrupt), Keyboard (CAPS), Cassette (the
  motor), Expansions. The detailed card: `TODO.md`.

## Lessons (each one cost a debugging session)

- **Page 2 noise typed a phantom key**: power-on RAM noise everywhere put
  $E8 in $02DF, the key buffer (bit 7 = a new key): BASIC's first input
  read an 'h' after "Ready". The ROM never clears it before reading.
  Noise only where the RAM test goes ($0400-$BFFF).
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
- `make -C components/oric/host check`: "37631 BYTES FREE" and "Ready"
  after 300 frames, a typed FOR loop's output, typed mixed case with CAPS
  off.
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
  jasmin.cpp; `roms/`: MAME's ROM sets), 79 MB. The software download
  was stopped early: 15 of oric-soft.bedroomlan.org's ~730 files
  (`bedroomlan_index.tsv` lists them all) and oric.org's 347 titles
  listed (`oricorg/titles.tsv`, nothing downloaded): `TODO.md` 5.
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
