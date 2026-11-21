# esp32oric: what's next

In order. A finished item goes into CLAUDE.md (what it is, how it was
checked) and leaves this list. The notes (`docs/oric-hardware.md`) have
the hardware for each; the section numbers below are theirs. (The core to
"Ready", the host build, the board side on both boards, tapes, the
Microdisc, the web page's card and the original software's run: done
2026-11-19, CLAUDE.md; `~/Development/oric-dl/FINDINGS.md`.)

1. **The MaTouch reset loop** (CLAUDE.md, Testing): if it comes back,
   keep the UART console recording at 921600 baud and look for
   `emu: reset reason:` on the next boot. For resets before the
   application console starts, capture the ROM's `rst:` line at 115200
   baud or the panic on the native USB port.
2. **Only if the user wants them** (`FINDINGS.md`, "Not usable yet"):
   oric.org's software (an account there: the user's), the Telestrat (7
   disks, 9 cartridges: another machine), the Jasmin (FT-DOS: another
   disk interface).
