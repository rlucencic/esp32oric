# esp32oric: what's next

In order. A finished item goes into CLAUDE.md (what it is, how it was
checked) and leaves this list. The notes (`docs/oric-hardware.md`) have
the hardware for each; the section numbers below are theirs.

1. **The core to BASIC's "Ready"** (`components/oric/`):
   - `oric.c`: init/reset (the ROM's RESET at $F88F), the 6502's bus
     (RAM $0000-$BFFF through the core's page pointers, page 3 the VIA
     through callbacks, mirrored every 16 bytes; BASIC at $C000-$FFFF,
     writes lost), the frame loop (64-cycle lines, 312 or 264 a frame by
     the mode register, the CPU run to each line's end or the VIA's next
     event), the VIA's wiring (PA, PB, CA2/CB2 to the AY, the IRQ line,
     PB3 from the keyboard), NMI, status (docs 1, 2, 8).
   - `via.c`: compile it and check it against docs 2.4 and 2.6 (timer 1's
     N + 2 period, the IFR bits the ROM's handler clears, the IER read).
   - `psg.c`: the AY-3-8912's registers through BDIR/BC1 (latch, write,
     read), port A to the keyboard's columns (docs 3.1-3.2, 5.2).
   - `keyboard.c`: the matrix, PB3 for the selected row and columns,
     typed text (`oric_type()`: a key held for two scans, CTRL and SHIFT
     where needed) (docs 5).
   - `video.c`: the ULA's lines (docs 4.5-4.10): serial attributes, text
     and hires, double height, flashing, inverse, the mode register's
     persistence and the frame length, lines 200-223 forced text; line
     records for the presenter as in esp32electron (decoded again only
     after a change at $9800-$BFFF or of the mode), 240 x 224 RGB565.
   Done when the host shows the copyright lines and "Ready" with the
   cursor blinking at its 0.25 s.
2. **The host build** (`components/oric/host/`, as esp32electron's
   `Makefile` and `host_main.c`): `make -C components/oric/host run`, the
   picture as PPM, `ORIC_LOG`, `ORIC_STOP`, `ORIC_TRACE`, `ORIC_WATCH`,
   `ORIC_TYPE`, `ORIC_WAV`, `ORIC_SHOTS`. Checks: "Ready" in the frames the
   ROM takes (its RAM test), typed BASIC (`PRINT 6*7`, a FOR loop, LIST),
   `HIRES` then `CIRCLE` and `TEXT`, `PING`/`ZAP`/`SHOOT`/`EXPLODE` and
   `SOUND`/`PLAY` recorded (`ORIC_WAV`), the cursor's flash rate (the
   notes' open question: 16 or 32 frames).
3. **The board side** (`main/`, as esp32electron's): `main.c`
   (`emu_machine_t`: memory, start, setup rows (model Atmos/Oric-1,
   Microdisc on/off), status, console `k`/`K`/`T`/`N` for NMI), `keymap.c`
   (HID usages by position: FUNCT, the cursor keys, DEL, ESC, CTRL),
   `rom_store.c` (partition `oric_roms` at 0x810000, the SD card's
   `oric/roms`), `settings.c` (NVS `oric`); `CMakeLists.txt` (both
   targets), `sdkconfig.defaults` (hostname `oric-atmos`), `partitions.csv`;
   the sound through the runtime's ring (31250 Hz). Build both boards,
   flash, check the prompt, typing, sound and speed (core 1's load).
   Add the project to `~/Development/esp32emu.code-workspace` and to the
   siblings' lists (esp32emu's README and the siblings' READMEs).
4. **Tapes** (docs 6): `.tap` files in the SD card's `oric/` folder, a
   Tape drive (setup screen, web page, NVS `oricdisks`); CLOAD/CSAVE
   through a trap of the ROM's byte routines (fast), and the bits through
   CB1/PB7 for loaders of their own (real time, the motor on PB6). CSAVE
   to a new or appended `.tap`. Check with original games.
5. **The Microdisc** (docs 7, required): the WD1793 (Type I-IV commands,
   status, DRQ/INTRQ to the control register and the 6502's IRQ), the
   control register (MAP, the EPROM, drive and side select), the EPROM at
   $E000; MFM_DISK images (`.dsk`) read and written in place (kept after
   remounting), two drives (DF0/DF1 in the setup screen and the web page),
   blank disks. Boot Sedoric and Oric DOS from a system disk; DIR, SAVE,
   LOAD, a file still there after the image is ejected and inserted
   again. Settle the notes' open points ($0314's status gating, page 3's
   decoding, geometry type 2) against the EPROM's code and real disks.
6. **The web page in detail** (as the siblings' machine cards): system,
   ROM, memory (MAP), the ULA's mode (text/hires, 50/60 Hz, screen
   address), the VIA (timers, IFR/IER, ports), the AY's registers and
   channels, keyboard, tape motor, the Microdisc's drive, track and
   status.
7. **Original software**: finish the download stopped on 2026-11-19
   (`~/Development/oric-dl/`: oric-soft.bedroomlan.org's ~730 files,
   15 fetched; oric.org's 347 titles listed, none fetched), as the
   Electron's and the QL's (a FINDINGS.md: what runs, what needs what);
   run the games and demos (a smoke run on the host as esp32ami's
   `smoke.py`), fix what fails.
8. **The Oric-1**: BASIC 1.0's ROM, its own key table and tape routines;
   the model row in the setup.
9. **GitHub polish**: README screenshots (the boards' pictures, status
   lines cut), topics, the `sdloric` sibling when it starts.
