#!/usr/bin/env python3
"""Each MFM_DISK image in a folder (recursively; files starting "MFM_DISK")
booted through the host build with a Microdisc: a copy in drive A, FRAMES
frames (3000; the environment's), the controller's commands logged
(ORIC_DISK_LOG). INPUT (the environment's ORIC_TYPE syntax) types
something for every image (a Return to leave a menu, say). Writes
<out>/<nnn>/log.txt, last.ppm and the pictures every SHOTS frames (600),
<out>/summary.txt (commands, sectors read and written, the commands that
ended with errors, the last command's frame, the PC; ERRORS and BUSY flag
what to look at) and <out>/sheet.png: a row of pictures an image. Eight at
once. Needs Pillow.

usage: disksmoke.py <folder> <out> <oric_host> <basic11b.rom> <microdis.rom>"""
import glob, os, re, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from PIL import Image, ImageDraw

folder, out, host, rom, eprom = sys.argv[1:6]
FRAMES = int(os.environ.get('FRAMES', '3000'))
SHOTS = int(os.environ.get('SHOTS', '600'))
os.makedirs(out, exist_ok=True)

disks = []
for d, _, files in os.walk(folder):
    for f in files:
        p = os.path.join(d, f)
        with open(p, 'rb') as fh:
            if fh.read(8) == b'MFM_DISK':
                disks.append(p)
disks.sort()


def run(i_p):
    i, path = i_p
    name = os.path.relpath(path, folder).replace('/', '_')
    work = os.path.join(out, '%03d' % i)
    os.makedirs(work, exist_ok=True)
    dsk = os.path.join(work, 'disk.dsk')
    shutil.copy(path, dsk)
    env = dict(os.environ, ORIC_MICRODISC=eprom, ORIC_DISK0=dsk, ORIC_DISK_LOG='1', ORIC_SHOTS=str(SHOTS))
    if os.environ.get('INPUT'):
        env['ORIC_TYPE'] = os.environ['INPUT']
    ppm = os.path.join(work, 'last.ppm')
    r = subprocess.run([host, rom, str(FRAMES), ppm], env=env, capture_output=True, text=True, timeout=900)
    lines = r.stdout.splitlines()
    cmds = [l for l in lines if ': command $' in l]
    dones = [l for l in lines if ' done, errors $' in l]
    errors = [l for l in dones if not l.split('errors $')[1].startswith('00')]
    reads = sum(1 for l in cmds if re.search(r'command \$[89]', l))
    writes = sum(1 for l in cmds if re.search(r'command \$[AB]', l))
    last_cmd = int(cmds[-1].split()[1].rstrip(':')) // 19968 if cmds else -1
    busy = len(cmds) > len(dones) + sum(1 for l in cmds if 'command $D' in l)
    m = re.search(r'PC ([0-9A-F]{4})', lines[-1]) if lines else None
    open(os.path.join(work, 'log.txt'), 'w').write(r.stdout)
    shots = sorted(glob.glob(os.path.join(work, 'last-*.ppm')))
    return i, name, len(cmds), reads, writes, len(errors), last_cmd, m.group(1) if m else '?', busy, shots + [ppm]


results = []
with ThreadPoolExecutor(8) as ex:
    for res in ex.map(run, enumerate(disks)):
        results.append(res)

with open(os.path.join(out, 'summary.txt'), 'w') as s:
    for i, name, cmds, reads, writes, errors, last, pc, busy, _ in results:
        flag = (' ERRORS' if errors else '') + (' BUSY' if busy else '')
        line = '%03d %-36s commands %4d reads %4d writes %3d errors %3d last f%-5d PC %s%s' % (
            i, name, cmds, reads, writes, errors, last, pc, flag)
        print(line)
        s.write(line + '\n')

# contact sheet: an image a row, its pictures in time
w, h = 240, 224
cols = max(len(r[-1]) for r in results) if results else 1
sheet = Image.new('RGB', (cols * w, len(results) * (h + 14)), 'white')
draw = ImageDraw.Draw(sheet)
for k, (i, name, *_rest, pictures) in enumerate(results):
    y = k * (h + 14)
    draw.text((2, y), '%03d %s' % (i, name), fill='black')
    for c, ppm in enumerate(pictures):
        try:
            sheet.paste(Image.open(ppm), (c * w, y + 14))
        except Exception:
            pass
sheet.save(os.path.join(out, 'sheet.png'))
