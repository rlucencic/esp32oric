# esp32oric: what's next

In order. A finished item goes into CLAUDE.md (what it is, how it was
checked) and leaves this list. The notes (`docs/oric-hardware.md`) have
the hardware for each; the section numbers below are theirs. (The core to
"Ready", the host build, the board side on the Tab5, tapes and the
Microdisc: done 2026-11-19, CLAUDE.md.)

1. **The board side's rest**: flash the MaTouch (its build is clean; it
   wasn't connected on 2026-11-19) and check the prompt, typing, sound, a
   tape (fast and real time), Sedoric from a disk (its boot, a SAVE: the
   images in PSRAM, the writes to the card) and core 1's load there; the sound on both
   boards' speakers by ear (PING, ZAP, a key click; only the host's
   recordings were measured); a Bluetooth or USB keyboard by hand (CTRL,
   FUNCT, Caps Lock as CTRL-T, the cursor keys, F12's NMI).
2. **The web page in detail** (as the siblings' machine cards): system,
   ROM, memory (MAP, what the Microdisc pages in), the ULA's mode
   (text/hires, 50/60 Hz, screen address), the VIA (timers, IFR/IER,
   ports), the AY's registers and channels, keyboard, tape motor, the
   WD1793's registers and the drives (a row already: its command, track,
   sector, status, paging).
3. **Original software**: finish the download (`~/Development/oric-dl/`:
   oric-soft.bedroomlan.org's ~730 files, its TAPES section fetched on
   2026-11-19 (206 archives, 348 TAP files in `tapes/`; Axayac's failed),
   its DISKS section (31 archives, 29 MFM_DISK images in `disks/`), 15
   others; oric.org's 347 titles listed, none fetched), as the Electron's
   and the QL's (a FINDINGS.md: what runs, what needs what). The tapes all
   load and the disks boot (CLAUDE.md, Testing): now whether each program
   runs (`tools/tapesmoke.py` and `tools/disksmoke.py` give each pictures;
   keys pressed for the ones waiting at a title or a menu; the ones that
   stopped oddly: Star Probe's "UNDEF'D STATEMENT ERROR IN 70", Oric
   Invaders' black screen, Parachute's empty one). Seven disks don't boot
   on the Microdisc (Stratsed, QVC 4.2 and 4.3, Hypercat in English and
   French, Animoric's second, Forth 83 2.4): their track 0 sector 1 is
   Stratsed's boot, and the Euphoric .BAT files beside four of them start
   it with `-t`, the Telestrat: another machine, out of scope unless the
   user wants it. Fix what fails.
4. **The Oric-1**: BASIC 1.0's ROM (it already boots on the host:
   `ORIC_MODEL=1`), its own key table, CAPS and tape routines (the
   typing's CAPS test and RETURN wait read 1.1's $020C and $EB78; the tape
   traps are 1.1's only, so 1.0 loads in real time and at 2400 baud: its
   routines ($E630 the byte reader per Clock Signal) and speed flag ($67)
   to find and trap), its system variables in the status; the model row in
   the setup; the Microdisc with 1.0 (its EPROM knows both ROMs: Sedoric
   on an Oric-1).
5. **GitHub polish**: the repository (`github.com/rlucencic/esp32oric`)
   is private since 2026-11-19: public when the user wants it, with README
   screenshots (the boards' pictures, status lines cut), and then in the
   siblings' lists (esp32emu's README and the siblings' READMEs: not
   before, they'd link to a page nobody can open); the `sdloric` sibling
   when it starts. (`~/Development/esp32emu.code-workspace` has it.)
