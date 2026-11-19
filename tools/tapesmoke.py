#!/usr/bin/env python3
"""Each TAP in a folder (recursively; files starting 16 16 16 24 or named
.tap) through the host build: CLOAD"" at frame 150, FRAMES frames (3000;
the environment's FRAMES), the tape's state each second (ORIC_TAPE_LOG).
Writes <out>/<nnn>/log.txt and last.ppm, <out>/summary.txt (the files
loaded at once, the tape's position, the frame it last moved, the PC;
NOT-AT-END, MOTOR-ON flag what to look at) and <out>/sheet.png, the last
pictures. "real" loads in real time (ORIC_REALTIME): give it FRAMES=20000.
Eight at once. Needs Pillow.

usage: tapesmoke.py <folder> <out> <oric_host> <basic11b.rom> [real]"""
import os, re, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from PIL import Image, ImageDraw

folder, out, host, rom = sys.argv[1:5]
real = len(sys.argv) > 5 and sys.argv[5] == 'real'
FRAMES = int(os.environ.get('FRAMES', '3000'))
os.makedirs(out, exist_ok=True)

taps = []
for d, _, files in os.walk(folder):
    for f in files:
        p = os.path.join(d, f)
        with open(p, 'rb') as fh:
            if fh.read(4) == b'\x16\x16\x16\x24' or f.lower().endswith('.tap'):
                taps.append(p)
taps.sort()

def run(i_p):
    i, path = i_p
    name = os.path.relpath(path, folder).replace('/', '_')
    work = os.path.join(out, '%03d' % i)
    os.makedirs(work, exist_ok=True)
    tap = os.path.join(work, 'tape.tap')
    shutil.copy(path, tap)
    env = dict(os.environ, ORIC_TAPE=tap, ORIC_TAPE_LOG='1', ORIC_TYPE='150:CLOAD""\\n')
    if real:
        env['ORIC_REALTIME'] = '1'
    ppm = os.path.join(work, 'last.ppm')
    r = subprocess.run([host, rom, str(FRAMES), ppm], env=env, capture_output=True, text=True, timeout=600)
    lines = r.stdout.splitlines()
    loads = [l for l in lines if 'tape: loaded' in l]
    last = [l for l in lines if l.startswith('frame ')]
    state = last[-1] if last else ''
    m = re.search(r'tape (\d+)/(\d+) file (\d+)/(\d+).*motor (\d) playing (\d).*PC ([0-9A-F]{4})', state)
    pos, size, f, files, motor, playing, pc = m.groups() if m else ('?',) * 7
    # the frame the tape last moved
    moved = 0
    prev = None
    for l in last:
        mm = re.search(r'frame (\d+): tape (\d+)/', l)
        if mm and mm.group(2) != prev:
            prev = mm.group(2)
            moved = int(mm.group(1))
    open(os.path.join(work, 'log.txt'), 'w').write(r.stdout)
    return i, name, len(loads), pos, size, files, motor, pc, moved, ppm

results = []
with ThreadPoolExecutor(8) as ex:
    for res in ex.map(run, enumerate(taps)):
        results.append(res)

with open(os.path.join(out, 'summary.txt'), 'w') as s:
    for i, name, loads, pos, size, files, motor, pc, moved, ppm in results:
        flag = ''
        if pos != size:
            flag += ' NOT-AT-END'
        if motor == '1':
            flag += ' MOTOR-ON'
        line = '%03d %-40s loads %2d  tape %s/%s files %s  last move f%-5d PC %s%s' % (
            i, name, loads, pos, size, files, moved, pc, flag)
        print(line)
        s.write(line + '\n')

# contact sheet: 8 across, the picture halved
cols, w, h = 8, 240, 224
rows = (len(results) + cols - 1) // cols
sheet = Image.new('RGB', (cols * w, rows * (h + 14)), 'white')
draw = ImageDraw.Draw(sheet)
for k, (i, name, *_rest, ppm) in enumerate(results):
    try:
        im = Image.open(ppm)
    except Exception:
        continue
    x, y = (k % cols) * w, (k // cols) * (h + 14)
    sheet.paste(im, (x, y + 14))
    draw.text((x + 2, y), '%03d %s' % (i, name[:34]), fill='black')
sheet.save(os.path.join(out, 'sheet.png'))
