# esp32oric: what's next

In order. A finished item goes into CLAUDE.md (what it is, how it was
checked) and leaves this list. The notes (`docs/oric-hardware.md`) have
the hardware for each; the section numbers below are theirs. (The core to
"Ready", the host build, the board side on both boards, tapes, the
Microdisc, the web page's card and the original software's run: done
2026-11-19, CLAUDE.md; `~/Development/oric-dl/FINDINGS.md`.)

## Next work

1. **Turn the original-software pass into a small replay suite.** Use
   `tools/tapesmoke.py` and `tools/disksmoke.py` to select one multi-stage TAP,
   one slow real-time tape and one writable Sedoric disk. Record checkpoints
   for both BASIC ROMs and verify save/reopen on a scratch disk without
   committing archive media.

2. **Open and close setup while media are active.** On the MaTouch and Tab5,
   check status JSON and CAPS before Start, while paused during tape playback
   and during a Microdisc transfer. Verify resume completes the operation and
   save UART reset reasons alongside the existing intermittent-reset follow-up.

## Conditional follow-up

1. **The MaTouch reset loop** (CLAUDE.md, Testing): if it comes back,
   keep the UART console recording at 921600 baud and look for
   `emu: reset reason:` on the next boot. For resets before the
   application console starts, capture the ROM's `rst:` line at 115200
   baud or the panic on the native USB port.
2. **Only if the user wants them** (`FINDINGS.md`, "Not usable yet"):
   oric.org's software (an account there: the user's), the Telestrat (7
   disks, 9 cartridges: another machine), the Jasmin (FT-DOS: another
   disk interface).
