#!/usr/bin/env python3
"""A test tape for a loader of its own: file 1, "LOADER", autorun machine
code at $0600: a driver and a copy of BASIC 1.1's sync and byte reader
($E6C9-$E759) at $0680, relocated, so the reading runs from RAM (no traps:
it polls CB1 as a game's own loader does). The driver: the ROM's tape
set-up ($E76A, motor on), the copied sync, bytes until $24, then 30 bytes
to $0800, the ROM's restore ($E93D), RTS. File 2: "DATA", 16 bytes $30-$3F
at $5000. After CLOAD"" (fast or real time) $0800 holds file 2's header,
"DATA", $00 and the 16 bytes:

  ORIC_TAPE=<copy> ORIC_DUMP=0800:30 ORIC_TYPE='150:CLOAD""\n' \
      oric_host basic11b.rom 600

usage: loadertap.py <basic11b.rom> <out.tap>"""
import sys

rom = open(sys.argv[1], 'rb').read()
lo, hi = 0xE6C9, 0xE75A
copy = bytearray(rom[lo - 0xC000:hi - 0xC000])
base = 0x0680
delta = base - lo
# relocate JSR/JMP abs into the copied range
i = 0
while i < len(copy):
    op = copy[i]
    if op in (0x20, 0x4C):
        t = copy[i + 1] | (copy[i + 2] << 8)
        if lo <= t < hi:
            t += delta
            copy[i + 1], copy[i + 2] = t & 0xFF, t >> 8
        i += 3
        continue
    i += 1
sync = 0xE735 + delta
byte = 0xE6C9 + delta

def w(a):
    return [a & 0xFF, a >> 8]

drv = []
drv += [0x20] + w(0xE76A)          # JSR set-up (motor on)
drv += [0x20] + w(sync)            # JSR sync (copy)
skip = 0x0600 + len(drv)
drv += [0x20] + w(byte)            # skip: JSR byte
drv += [0xC9, 0x24]                # CMP #$24
drv += [0xD0, 0x100 - 7]           # BNE skip
drv += [0xA2, 0x00]                # LDX #0
loop = 0x0600 + len(drv)
drv += [0x20] + w(byte)            # loop: JSR byte
drv += [0x9D] + w(0x0800)          # STA $0800,X
drv += [0xE8]                      # INX
drv += [0xE0, 30]                  # CPX #30
drv += [0xD0, 0x100 - 11]          # BNE loop
drv += [0x20] + w(0xE93D)          # JSR restore (motor off)
drv += [0x60]                      # RTS
assert len(drv) <= 0x80

mem = bytearray(0x0680 - 0x0600) + copy
mem[:len(drv)] = bytes(drv)
start, end = 0x0600, 0x0600 + len(mem) - 1

def tapfile(name, typ, auto, start, end, data):
    hdr = bytes([0, 0, typ, auto, end >> 8, end & 0xFF, start >> 8, start & 0xFF, 0])
    return b'\x16\x16\x16\x24' + hdr + name + b'\x00' + data

data = bytes(range(0x30, 0x40))
tap = tapfile(b'LOADER', 0x80, 0xC7, start, end, bytes(mem)) + \
      tapfile(b'DATA', 0x80, 0, 0x5000, 0x500F, data)
open(sys.argv[2], 'wb').write(tap)
print('driver %d bytes, copy at $%04X, sync $%04X, byte $%04X, file 1 $%04X-$%04X' % (
    len(drv), base, sync, byte, start, end))
