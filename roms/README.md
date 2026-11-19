# Oric ROM images

Put the ROM images here (not part of this repository). `idf.py flash` will
write them into 64 KB slots of the `oric_roms` flash partition (to come, as
in the siblings), and a file of the same name in the SD card's `oric/roms`
folder will override the flashed one.

| File | What | Size | CRC32 |
|---|---|---|---|
| `basic11b.rom` | Oric Atmos BASIC 1.1 ($C000-$FFFF) | 16 KB | c3a92bef |
| `basic10.rom` | Oric-1 BASIC 1.0 | 16 KB | f18710b4 |
| `microdis.rom` | The Oric Microdisc's EPROM (its boot and DOS entry) | 8 KB | a9664a9c |

All three are in MAME's ROM sets (`orica.zip`, `oric1.zip` and the
Microdisc device's set: archive.org's "MAME 0.264 ROMs (non-merged)" item)
and in https://oric-soft.bedroomlan.org/files/; the two sources' files are
identical (docs/oric-hardware.md 1.7). The ROMs are Tangerine's and Oric
International's copyright; they are not distributed here.
