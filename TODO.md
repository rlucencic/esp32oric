# esp32oric: what's next

In order. A finished item goes into CLAUDE.md (what it is, how it was
checked) and leaves this list. The notes (`docs/oric-hardware.md`) have
the hardware for each; the section numbers below are theirs. (The core to
"Ready" and the host build: done 2026-11-19, CLAUDE.md.)

1. **The board side** (`main/`, as esp32electron's): `main.c`
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
2. **Tapes** (docs 6): `.tap` files in the SD card's `oric/` folder, a
   Tape drive (setup screen, web page, NVS `oricdisks`); CLOAD/CSAVE
   through a trap of the ROM's byte routines (fast), and the bits through
   CB1/PB7 for loaders of their own (real time, the motor on PB6). CSAVE
   to a new or appended `.tap`. Check with original games.
3. **The Microdisc** (docs 7, required): the WD1793 (Type I-IV commands,
   status, DRQ/INTRQ to the control register and the 6502's IRQ), the
   control register (MAP, the EPROM, drive and side select), the EPROM at
   $E000; MFM_DISK images (`.dsk`) read and written in place (kept after
   remounting), two drives (DF0/DF1 in the setup screen and the web page),
   blank disks. Boot Sedoric and Oric DOS from a system disk; DIR, SAVE,
   LOAD, a file still there after the image is ejected and inserted
   again. Settle the notes' open points ($0314's status gating, page 3's
   decoding, geometry type 2) against the EPROM's code and real disks.
4. **The web page in detail** (as the siblings' machine cards): system,
   ROM, memory (MAP), the ULA's mode (text/hires, 50/60 Hz, screen
   address), the VIA (timers, IFR/IER, ports), the AY's registers and
   channels, keyboard, tape motor, the Microdisc's drive, track and
   status.
5. **Original software**: finish the download stopped on 2026-11-19
   (`~/Development/oric-dl/`: oric-soft.bedroomlan.org's ~730 files,
   15 fetched; oric.org's 347 titles listed, none fetched), as the
   Electron's and the QL's (a FINDINGS.md: what runs, what needs what);
   run the games and demos (a smoke run on the host as esp32ami's
   `smoke.py`), fix what fails.
6. **The Oric-1**: BASIC 1.0's ROM (it already boots on the host:
   `ORIC_MODEL=1`), its own key table, CAPS and tape routines (the
   typing's CAPS test reads 1.1's $020C), its system variables in the
   status; the model row in the setup.
7. **GitHub polish**: the repository (`github.com/rlucencic/esp32oric`)
   is private since 2026-11-19: public when the user wants it, with README
   screenshots (the boards' pictures, status lines cut); the `sdloric`
   sibling when it starts.
