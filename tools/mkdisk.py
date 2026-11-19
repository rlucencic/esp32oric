#!/usr/bin/env python3
"""A Microdisc test disk: MFM_DISK, 2 sides of 42 tracks of 17 sectors of
256 bytes (Sedoric's layout), every sector's byte i = (track * 17 + sector
+ side * 7 + i) & $FF, and a SYSTEM.DOS the Microdisc's EPROM boots (Oric
DOS 0.6's directory on track 0 sector 3 and file format: a first sector
with the next one's link, segments of load, end and exec addresses, records
of a length and that many bytes): a test of the WD1793 at $0900 that runs
its commands one after another, polling (no interrupts), and puts each
one's result at $0800:

  $0800  Restore: status & $04 (track 0)                    04
  $0801  Seek 5 with verify: status & $D8 (errors)          00
  $0802  ... the track register                             05
  $0803  Read t5 s3 to $2000: status                        00
  $0804  ... bytes that differ from the pattern             00
  $0805  Write t5 s4 from $3000 ((i & $7F) ^ $25): status  00
  $0806  Read t5 s4 back to $2100: status                   00
  $0807  ... bytes that differ from $3000                   00
  $0808  Read Address to $2200: status                      00
  $0809  ... its track and sector bytes ($2200, $2202)      05 xx
  $080B  Read t6 s1.. multiple to $4000: status (RNF)       10
  $080C  ... pages read                                     11
  $080D  Step in, u: the track register                     06
  $080E  Step out, no u: the track register                 06
  $080F  Seek 50 (past the image), read: status (RNF)       10
  $0810  Side 1, t0 s1 with side compare 1: status          00
  $0811  ... the first byte (0 + 1 + 7)                     08
  $0812  Side 1, side compare 0: status (RNF)               10
  $0813  Write Track t41 s0 (one sector 1): status          00
  $0814  Read t41 s1: status                                00
  $0815  ... bytes that differ from $3000                   00
  $0816  Read t41 s2 (formatted away): status (RNF)         10
  $0817  Force Interrupt $D0: status & $01 (not busy)       00
  $08FF  the end: $AA

and shows them in hex on the screen's third row, PASS (or FAIL) on the
fifth.

The drive is A. Written: track 5 sector 4 and track 41 (side 0) of the
image. With the Microdisc's EPROM in the host build:

  ORIC_MICRODISC=microdis.rom ORIC_DISK0=<copy> ORIC_DUMP=0800:24 \\
      oric_host basic11b.rom 1500

then `mkdisk.py --check <copy>`: track 5 sector 4 and track 41's one
sector hold $3000's bytes with their CRCs right, the rest as made.

usage: mkdisk.py <out.dsk> | --check <written.dsk>"""
import sys

TRACKS, SIDES, SECTORS = 42, 2, 17
GAP1, GAP2, GAP3, GAP4 = 40, 22, 40, 40         # Sedoric's: IDs 358 bytes apart


def crc16(data, c=0xFFFF):
    for x in data:
        c ^= x << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def track(cyl, side, sectors):
    """A track's 6400 bytes: the standard MFM layout, sectors 1-17 of 256"""
    t = bytearray(b'\x4e' * GAP1 + b'\x00' * 12 + b'\xc2\xc2\xc2\xfc' + b'\x4e' * GAP4)
    for r in range(1, SECTORS + 1):
        idf = bytes([0xA1, 0xA1, 0xA1, 0xFE, cyl, side, r, 1])
        c = crc16(idf)
        t += b'\x00' * 12 + idf + bytes([c >> 8, c & 0xFF]) + b'\x4e' * GAP2
        dat = bytes([0xA1, 0xA1, 0xA1, 0xFB]) + sectors[r]
        c = crc16(dat)
        t += b'\x00' * 12 + dat + bytes([c >> 8, c & 0xFF]) + b'\x4e' * GAP3
    assert len(t) <= 6250, len(t)
    return bytes(t + b'\x4e' * (6400 - len(t)))


def pattern(cyl, side, r):
    return bytes((cyl * 17 + r + side * 7 + i) & 0xFF for i in range(256))


# ---- a small 6502 assembler: (label) strings and (mnemonic, operand) ----
OPS = {
    # mnemonic: {mode: opcode}
    'LDA': {'imm': 0xA9, 'abs': 0xAD, 'absx': 0xBD, 'absy': 0xB9, 'indy': 0xB1},
    'STA': {'abs': 0x8D, 'absx': 0x9D, 'absy': 0x99, 'indy': 0x91, 'zp': 0x85},
    'LDX': {'imm': 0xA2, 'abs': 0xAE}, 'LDY': {'imm': 0xA0, 'abs': 0xAC, 'zp': 0xA4},
    'STX': {'abs': 0x8E}, 'STY': {'abs': 0x8C},
    'CMP': {'imm': 0xC9, 'abs': 0xCD, 'absx': 0xDD, 'absy': 0xD9, 'indy': 0xD1},
    'CPX': {'imm': 0xE0}, 'CPY': {'imm': 0xC0},
    'AND': {'imm': 0x29}, 'EOR': {'imm': 0x49}, 'ORA': {'imm': 0x09},
    'ADC': {'imm': 0x69}, 'INC': {'abs': 0xEE, 'zp': 0xE6},
    'JMP': {'abs': 0x4C}, 'JSR': {'abs': 0x20},
    'BNE': {'rel': 0xD0}, 'BEQ': {'rel': 0xF0}, 'BPL': {'rel': 0x10}, 'BMI': {'rel': 0x30},
    'BCS': {'rel': 0xB0}, 'BCC': {'rel': 0x90},
    'RTS': {'imp': 0x60}, 'SEI': {'imp': 0x78}, 'CLC': {'imp': 0x18}, 'LSR': {'imp': 0x4A},
    'INX': {'imp': 0xE8}, 'INY': {'imp': 0xC8}, 'DEX': {'imp': 0xCA}, 'DEY': {'imp': 0x88},
    'TAX': {'imp': 0xAA}, 'TAY': {'imp': 0xA8}, 'TXA': {'imp': 0x8A}, 'TYA': {'imp': 0x98},
    'PHA': {'imp': 0x48}, 'PLA': {'imp': 0x68},
}
SIZE = {'imm': 2, 'abs': 3, 'absx': 3, 'absy': 3, 'indy': 2, 'zp': 2, 'rel': 2, 'imp': 1}


def assemble(prog, org):
    def parse(op):
        if op is None:
            return 'imp', None
        if op.startswith('#'):
            return 'imm', op[1:]
        if op.startswith('(') and op.endswith('),Y'):
            return 'indy', op[1:-3]
        if op.endswith(',X'):
            return 'absx', op[:-2]
        if op.endswith(',Y'):
            return 'absy', op[:-2]
        return 'abs', op

    labels = {}
    for pass_ in (0, 1):
        pc, out = org, bytearray()
        for item in prog:
            if isinstance(item, str):
                labels[item] = pc
                continue
            if item[0] == '.byte':
                out += bytes(item[1])
                pc += len(item[1])
                continue
            m, op = item[0], item[1] if len(item) > 1 else None
            mode, arg = parse(op)
            if m in ('BNE', 'BEQ', 'BPL', 'BMI', 'BCS', 'BCC'):
                mode = 'rel'
            if mode == 'abs' and 'abs' not in OPS[m] and 'zp' in OPS[m]:
                mode = 'zp'
            def val(a):
                if '+' in a:
                    x, y = a.split('+')
                    return val(x) + int(y)
                if a in labels:
                    return labels[a]
                if pass_ == 0 and a[0].isalpha() and not a.startswith('$'):
                    return 0
                if a.startswith('<'):
                    return val(a[1:]) & 0xFF
                if a.startswith('>'):
                    return val(a[1:]) >> 8
                return int(a[1:], 16) if a.startswith('$') else int(a)
            out.append(OPS[m][mode])
            if mode == 'rel':
                d = val(arg) - (pc + 2)
                assert pass_ == 0 or -128 <= d < 128, (m, arg, d)
                out.append(d & 0xFF)
            elif mode in ('imm', 'zp', 'indy'):
                out.append(val(arg) & 0xFF)
            elif mode != 'imp':
                v = val(arg)
                out += bytes([v & 0xFF, v >> 8])
            pc += SIZE[mode]
    return bytes(out), labels


ORG = 0x0900
prog = [
    'start', ('SEI',), ('LDA', '#$84'), ('STA', '$0314'),      # drive A, side 0, overlay RAM, no IRQ
    # Restore, head load
    ('LDA', '#$08'), ('JSR', 'cmd'), ('AND', '#$04'), ('STA', '$0800'),
    # Seek 5, verify
    ('LDA', '#5'), ('STA', '$0313'), ('LDA', '#$1C'), ('JSR', 'cmd'), ('AND', '#$D8'), ('STA', '$0801'),
    ('LDA', '$0311'), ('STA', '$0802'),
    # Read t5 s3 to $2000, compare with the pattern (5 * 17 + 3 + i)
    ('LDA', '#3'), ('STA', '$0312'), ('LDA', '#$20'), ('LDY', '#$80'), ('JSR', 'read'), ('STA', '$0803'),
    ('LDX', '#0'), ('LDY', '#0'), 'cmp1', ('TYA',), ('CLC',), ('ADC', '#88'), ('CMP', '$2000,Y'), ('BEQ', 'cmp1ok'),
    ('INX',), 'cmp1ok', ('INY',), ('BNE', 'cmp1'), ('STX', '$0804'),
    # $3000: (i & $7F) ^ $25 (no $F5-$F7: Write Track takes those for marks and CRCs); write t5 s4 from it
    ('LDY', '#0'), 'fill', ('TYA',), ('AND', '#$7F'), ('EOR', '#$25'), ('STA', '$3000,Y'), ('INY',), ('BNE', 'fill'),
    ('LDA', '#4'), ('STA', '$0312'), ('LDA', '#$30'), ('LDY', '#$A0'), ('JSR', 'write'), ('STA', '$0805'),
    # read it back to $2100
    ('LDA', '#$21'), ('LDY', '#$80'), ('JSR', 'read'), ('STA', '$0806'),
    ('LDA', '#$21'), ('JSR', 'diff'), ('STX', '$0807'),
    # Read Address to $2200
    ('LDA', '#$22'), ('LDY', '#$C0'), ('JSR', 'read'), ('STA', '$0808'),
    ('LDA', '$2200'), ('STA', '$0809'), ('LDA', '$2202'), ('STA', '$080A'),
    # t6: step in (u), read sectors 1.. (multiple) to $4000
    ('LDA', '#$58'), ('JSR', 'cmd'), ('LDA', '$0311'), ('STA', '$080D'),
    ('LDA', '#1'), ('STA', '$0312'), ('LDA', '#$40'), ('LDY', '#$90'), ('JSR', 'read'), ('STA', '$080B'),
    ('LDA', '$F1'), ('CLC',), ('ADC', '#$C0'), ('STA', '$080C'),      # pages: $F1 - $40
    # step out without u: the head to 5, the register stays 6
    ('LDA', '#$68'), ('JSR', 'cmd'), ('LDA', '$0311'), ('STA', '$080E'),
    # seek 50 (no verify), read: RNF
    ('LDA', '#50'), ('STA', '$0313'), ('LDA', '#$18'), ('JSR', 'cmd'),
    ('LDA', '#1'), ('STA', '$0312'), ('LDA', '#$20'), ('LDY', '#$80'), ('JSR', 'read'), ('STA', '$080F'),
    # side 1, track 0: side compare 1 (C, S), then 0
    ('LDA', '#$94'), ('STA', '$0314'), ('LDA', '#$08'), ('JSR', 'cmd'),
    ('LDA', '#1'), ('STA', '$0312'), ('LDA', '#$20'), ('LDY', '#$8A'), ('JSR', 'read'), ('STA', '$0810'),
    ('LDA', '$2000'), ('STA', '$0811'),
    ('LDA', '#$20'), ('LDY', '#$82'), ('JSR', 'read'), ('STA', '$0812'),
    # side 0, seek 41, Write Track: one sector (41, 0, 1, 1) from $3000
    ('LDA', '#$84'), ('STA', '$0314'), ('LDA', '#41'), ('STA', '$0313'), ('LDA', '#$18'), ('JSR', 'cmd'),
    # the track's bytes expanded at $5000 from the runs, $4E after them; then written as a sector is
    ('LDA', '#$00'), ('STA', '$F2'), ('LDA', '#$50'), ('STA', '$F3'), ('LDA', '#0'), ('STA', '$F4'),
    'xr', ('LDY', '$F4'), ('LDA', 'runs,Y'), ('BEQ', 'xdata'), ('TAX',), ('LDA', 'runs+1,Y'), ('LDY', '#0'),
    'xb', ('STA', '($F2),Y'), ('INC', '$F2'), ('BNE', 'xb1'), ('INC', '$F3'), 'xb1', ('DEX',), ('BNE', 'xb'),
    ('INC', '$F4'), ('INC', '$F4'), ('JMP', 'xr'),
    'xdata', ('LDA', 'runs+1,Y'), ('BEQ', 'xfill'), ('LDY', '#0'),
    'xd', ('LDA', '$3000,Y'), ('STA', '($F2),Y'), ('INY',), ('BNE', 'xd'), ('INC', '$F3'),
    ('INC', '$F4'), ('INC', '$F4'), ('JMP', 'xr'),
    'xfill', ('LDA', '#$4E'), ('LDY', '#0'), ('LDX', '#$1A'),
    'xf', ('STA', '($F2),Y'), ('INY',), ('BNE', 'xf'), ('INC', '$F3'), ('DEX',), ('BNE', 'xf'),
    ('LDA', '#$50'), ('LDY', '#$F4'), ('JSR', 'write'),
    ('STA', '$0813'),
    ('LDA', '#1'), ('STA', '$0312'), ('LDA', '#$21'), ('LDY', '#$80'), ('JSR', 'read'), ('STA', '$0814'),
    ('LDA', '#$21'), ('JSR', 'diff'), ('STX', '$0815'),
    ('LDA', '#2'), ('STA', '$0312'), ('LDA', '#$21'), ('LDY', '#$80'), ('JSR', 'read'), ('STA', '$0816'),
    # Force Interrupt
    ('LDA', '#$D0'), ('STA', '$0310'), ('LDA', '$0310'), ('AND', '#$01'), ('STA', '$0817'),
    ('LDA', '#$AA'), ('STA', '$08FF'),
    # the results in hex on the screen's third row, PASS or FAIL on the fifth
    ('LDX', '#0'), ('LDY', '#0'),
    'sh', ('LDA', '$0800,X'), ('LSR',), ('LSR',), ('LSR',), ('LSR',), ('JSR', 'hex'), ('STA', '$BBD0,Y'), ('INY',),
    ('LDA', '$0800,X'), ('AND', '#$0F'), ('JSR', 'hex'), ('STA', '$BBD0,Y'), ('INY',),
    ('LDA', '#$20'), ('STA', '$BBD0,Y'), ('INY',),
    ('INX',), ('CPX', '#24'), ('BNE', 'sh'),
    ('LDX', '#0'), 'ck', ('LDA', 'expect,X'), ('CMP', '#$FF'), ('BEQ', 'ck1'), ('CMP', '$0800,X'), ('BNE', 'fail'),
    'ck1', ('INX',), ('CPX', '#24'), ('BNE', 'ck'), ('LDA', '$08FF'), ('CMP', '#$AA'), ('BNE', 'fail'),
    ('LDX', '#0'), 'ps', ('LDA', 'pass,X'), ('STA', '$BC22,X'), ('INX',), ('CPX', '#4'), ('BNE', 'ps'),
    'halt', ('JMP', 'halt'),
    'fail', ('LDX', '#0'), 'fl', ('LDA', 'failt,X'), ('STA', '$BC22,X'), ('INX',), ('CPX', '#4'), ('BNE', 'fl'),
    ('JMP', 'halt'),
    'hex', ('CMP', '#10'), ('BCC', 'hex1'), ('ADC', '#6'), 'hex1', ('ADC', '#$30'), ('RTS',),
    'pass', ('.byte', list(b'PASS')), 'failt', ('.byte', list(b'FAIL')),
    # the results expected ($FF: any)
    'expect', ('.byte', [0x04, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0xFF, 0x10,
                         0x11, 0x06, 0x06, 0x10, 0x00, 0x08, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00]),

    # cmd: A a command; waits while busy; A the status
    'cmd', ('STA', '$0310'), 'cmdw', ('LDA', '$0310'), ('LSR',), ('BCS', 'cmdw'), ('LDA', '$0310'), ('RTS',),
    # read: Y the command, A the page; bytes while DRQ until not busy; A the status
    'read', ('STA', '$F1'), ('LDA', '#0'), ('STA', '$F0'), ('TYA',), ('STA', '$0310'), ('LDY', '#0'),
    'rdl', ('LDA', '$0318'), ('BMI', 'rdn'), ('LDA', '$0313'), ('STA', '($F0),Y'), ('INY',), ('BNE', 'rdl'),
    ('INC', '$F1'), ('JMP', 'rdl'),
    'rdn', ('LDA', '$0310'), ('LSR',), ('BCS', 'rdl'), ('LDA', '$0310'), ('RTS',),
    # write: Y the command, A the page
    'write', ('STA', '$F1'), ('LDA', '#0'), ('STA', '$F0'), ('TYA',), ('STA', '$0310'), ('LDY', '#0'),
    'wrl', ('LDA', '$0318'), ('BMI', 'wrn'), ('LDA', '($F0),Y'), ('STA', '$0313'), ('INY',), ('BNE', 'wrl'),
    ('INC', '$F1'), ('JMP', 'wrl'),
    'wrn', ('LDA', '$0310'), ('LSR',), ('BCS', 'wrl'), ('LDA', '$0310'), ('RTS',),
    # diff: A a page; X the bytes that differ from $3000
    'diff', ('STA', '$F1'), ('LDA', '#0'), ('STA', '$F0'), ('LDX', '#0'), ('LDY', '#0'),
    'dfl', ('LDA', '($F0),Y'), ('CMP', '$3000,Y'), ('BEQ', 'dfo'), ('INX',), 'dfo', ('INY',), ('BNE', 'dfl'), ('RTS',),
    # Write Track's runs: (count, byte) ..., (0, 1): the 256 bytes at $3000, (0, 0): the end
    'runs', ('.byte', [60, 0x4E, 12, 0x00, 3, 0xF5, 1, 0xFE, 1, 41, 1, 0, 1, 1, 1, 1, 1, 0xF7,
                       22, 0x4E, 12, 0x00, 3, 0xF5, 1, 0xFB, 0, 1, 1, 0xF7, 0, 0]),
]
code, labels = assemble(prog, ORG)

# ---- the system file: Oric DOS 0.6's format, the EPROM's loader ----
def sysfile(segments, exec_):
    """Sectors of a file: [link track, link sector] (sector 0: the last),
    then each segment's header ($FF, $00, load, end, exec: the first's
    exec is the file's) and its records (a length, that many bytes); 0
    ends a sector's records"""
    stream = []                         # ('h', header bytes) or ('d', data bytes)
    for org, data in segments:
        end = org + len(data) - 1
        stream.append(bytes([0xFF, 0x00, org & 0xFF, org >> 8, end & 0xFF, end >> 8, exec_ & 0xFF, exec_ >> 8]))
        stream.append(data)
    sectors, s = [], bytearray(2)
    for k, item in enumerate(stream):
        if k % 2 == 0:                  # a header goes whole into a sector
            if len(s) + len(item) + 2 > 256:
                sectors.append(s + bytes(256 - len(s)))
                s = bytearray(2)
            s += item
            continue
        rest = item
        while rest:
            n = min(len(rest), 256 - len(s) - 2)
            if n <= 0:
                sectors.append(s + bytes(256 - len(s)))
                s = bytearray(2)
                continue
            s += bytes([n]) + rest[:n]
            rest = rest[n:]
    sectors.append(s + bytes(256 - len(s)))
    return sectors

# the EPROM shows the 0-ended text at $9FD0 (the DOS's banner) after loading
file_sectors = sysfile([(0x9FD0, b'MKDISK TEST\x00'), (ORG, code)], ORG)
# the first on track 0 sector 2, the rest on track 1 from sector 1
places = [(0, 2)] + [(1, r) for r in range(1, len(file_sectors))]
for k, sec in enumerate(file_sectors):
    nxt = places[k + 1] if k + 1 < len(places) else (0, 0)
    sec[0], sec[1] = nxt[0], nxt[1]     # track, sector (sector 0: the end)

def sectors_of(t):
    """A track's sectors as {r: (data, crc ok)} (Sedoric's marks)"""
    found, i = {}, 0
    while i < 6250:
        if t[i:i + 4] == b'\xa1\xa1\xa1\xfe':
            r = t[i + 6]
            j = t.find(b'\xa1\xa1\xa1\xfb', i + 10, i + 60)
            if j < 0:
                i += 10
                continue
            d = t[j:j + 4 + 256]
            found[r] = (d[4:], crc16(d) == (t[j + 260] << 8 | t[j + 261]))
            i = j + 4 + 256
        else:
            i += 1
    return found


if sys.argv[1] == '--check':
    disk = open(sys.argv[2], 'rb').read()
    ram3000 = bytes((i & 0x7F) ^ 0x25 for i in range(256))
    t5 = sectors_of(disk[256 + 5 * 6400:256 + 6 * 6400])
    t41 = sectors_of(disk[256 + 41 * 6400:256 + 42 * 6400])
    ok = t5.get(4) == (ram3000, True) and len(t5) == 17 and all(
        t5[r] == (pattern(5, 0, r), True) for r in t5 if r != 4) and t41 == {1: (ram3000, True)}
    print('written tracks right' if ok else 'written tracks WRONG')
    sys.exit(0 if ok else 1)

img = {}
for side in range(SIDES):
    for cyl in range(TRACKS):
        img[(cyl, side)] = {r: pattern(cyl, side, r) for r in range(1, SECTORS + 1)}
sys_info = bytearray(b'\x01' + b'\x00' * 7 + b' ' * 8 + bytes([0, 0, 3, 0, 0, 0, 1, 0]) + b'MKDISK TEST'.ljust(32))
directory = bytearray(bytes([0, 0, 2]) + b'SYSTEMDOS' + bytes([1, 0, 2, 0, 2, 0, 0]) + b'BOOTUPCOM' + bytes(7))
img[(0, 0)][1] = bytes(sys_info.ljust(256, b'\x00'))
img[(0, 0)][3] = bytes(directory.ljust(256, b'\x00'))
for (cyl, r), sec in zip(places, file_sectors):
    img[(cyl, 0)][r] = bytes(sec)

out = bytearray(b'MFM_DISK' + bytes([SIDES, 0, 0, 0, TRACKS, 0, 0, 0, 1, 0, 0, 0])).ljust(256, b'\x00')
for side in range(SIDES):
    for cyl in range(TRACKS):
        out += track(cyl, side, img[(cyl, side)])
open(sys.argv[1], 'wb').write(out)
print('test program %d bytes at $%04X, %d file sectors' % (len(code), ORG, len(file_sectors)))
