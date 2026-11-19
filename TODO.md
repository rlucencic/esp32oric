# esp32oric: what's next

In order. A finished item goes into CLAUDE.md (what it is, how it was
checked) and leaves this list. The notes (`docs/oric-hardware.md`) have
the hardware for each; the section numbers below are theirs. (The core to
"Ready", the host build, the board side on both boards, tapes, the
Microdisc, the web page's card and the original software's run: done
2026-11-19, CLAUDE.md; `~/Development/oric-dl/FINDINGS.md`.)

1. **The board side's rest**: the sound on both boards' speakers by ear
   (PING, ZAP, a key click; only the host's recordings were measured); a
   Bluetooth or USB keyboard by hand (CTRL, FUNCT, Caps Lock as CTRL-T, the
   cursor keys, F12's NMI). The MaTouch's reset loop, seen once
   (CLAUDE.md, Testing): if it comes back, catch the reset reason (the
   ROM's line at 115200 on the UART, or the panic on the native USB port;
   esp32emu could log `esp_reset_reason()` at boot for every machine).
2. **The Oric-1**: BASIC 1.0's ROM (it already boots on the host:
   `ORIC_MODEL=1`; typing takes its key table at $FF70), its CAPS and tape
   routines (the typing's CAPS test and RETURN wait read 1.1's $020C and
   $EB78; the tape traps are 1.1's only, so 1.0 loads in real time and at
   2400 baud: its
   routines ($E630 the byte reader per Clock Signal) and speed flag ($67)
   to find and trap), its system variables in the status; the model row in
   the setup; the Microdisc with 1.0 (its EPROM knows both ROMs: Sedoric
   on an Oric-1).
3. **GitHub polish**: the repository (`github.com/rlucencic/esp32oric`)
   is private since 2026-11-19: public when the user wants it, with README
   screenshots (the boards' pictures, status lines cut), and then in the
   siblings' lists (esp32emu's README and the siblings' READMEs: not
   before, they'd link to a page nobody can open); the `sdloric` sibling
   when it starts. (`~/Development/esp32emu.code-workspace` has it.)
4. **Only if the user wants them** (`FINDINGS.md`, "Not usable yet"):
   oric.org's software (an account there: the user's), the Telestrat (7
   disks, 9 cartridges: another machine), the Jasmin (FT-DOS: another
   disk interface).
