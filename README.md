# esp32oric

An Oric Atmos emulator for the Makerfabs MaTouch ESP32-S3 7" and the
M5Stack Tab5 (ESP32-P4): a 6502A at 1 MHz, 64 KB of RAM, the ULA's
serial-attribute text and hires screens, a 6522 VIA, an AY-3-8912, BASIC
1.1 and tapes, with the Oric Microdisc to follow.

**Early development.** On the Tab5 (and in a PC build, `make -C
components/oric/host run`) it boots BASIC 1.1 (or the Oric-1's 1.0) to
"Ready" and runs typed programs, the text and hires screens with their
serial attributes and the AY's sound. Tapes are `.TAP` files on the SD
card: CLOAD and CSAVE at once (BASIC 1.1's tape routines trapped), or in
real time through the VIA as on the real machine; games with loaders of
their own get the tape in real time. The Microdisc is still to come, and the
MaTouch is built but not yet tried. The hardware research is in
`docs/oric-hardware.md`; `TODO.md` has the plan, in order; `CLAUDE.md` the
state of things.

The board side (display, touch, SD card, WiFi and its web page, Bluetooth
and USB keyboards, sound) and the 6502 core are
[esp32emu](https://github.com/rlucencic/esp32emu), shared with the other
emulators: [esp32ste](https://github.com/rlucencic/esp32ste) (Atari STE),
[esp32mac](https://github.com/rlucencic/esp32mac) (Macintosh Plus, SE,
Classic), [esp32mac2](https://github.com/rlucencic/esp32mac2) (Macintosh LC
and II, Tab5 only), [esp32mac3](https://github.com/rlucencic/esp32mac3)
(Macintosh LC II and LC III, Tab5 only),
[esp32cpc](https://github.com/rlucencic/esp32cpc) (Amstrad CPC),
[esp32zx](https://github.com/rlucencic/esp32zx) (ZX Spectrum 128K, +2, +3),
[esp32cbm](https://github.com/rlucencic/esp32cbm) (Commodore 64 and 128),
[esp32xl](https://github.com/rlucencic/esp32xl) (Atari 800XL and 130XE),
[esp32msx](https://github.com/rlucencic/esp32msx) (MSX1 and MSX2),
[esp32gs](https://github.com/rlucencic/esp32gs) (Apple IIGS),
[esp32coco](https://github.com/rlucencic/esp32coco) (Tandy Color Computer 2
and 3), [esp32arc](https://github.com/rlucencic/esp32arc) (Acorn Archimedes,
Tab5 only), [esp32pc](https://github.com/rlucencic/esp32pc) (IBM PC/XT and
PC/AT), [esp32ql](https://github.com/rlucencic/esp32ql) (Sinclair QL),
[esp32electron](https://github.com/rlucencic/esp32electron) (Acorn
Electron), [esp32coupe](https://github.com/rlucencic/esp32coupe) (SAM
Coupé) and [esp32ami](https://github.com/rlucencic/esp32ami) (Amiga 500,
2000, 600 and 1200, Tab5 only).

The ROMs are not included: `roms/README.md` says where they come from.

## Licence

MIT (`LICENSE`). The machine is written from the Oric's documentation;
other emulators were only compared against, never copied.
