#!/usr/bin/env python3
"""Each TAP in a folder (recursively; files starting 16 16 16 24 or named
.tap) through the host build: CLOAD"" at frame 150, FRAMES frames (6000;
the environment's), a picture every SHOTS (1000), INPUT typed for every
tape (the environment's, ORIC_TYPE's syntax after the CLOAD; by default a
Space at 3500 and a Return at 4500, for titles waiting for a key), the
tape's state each second (ORIC_TAPE_LOG), the text screen at the end.

Writes <out>/<nnn>/log.txt and the pictures, <out>/summary.txt (the files
loaded at once, the tape's position, the frame it last moved, the PC and
flags: NOT-AT-END the tape didn't reach its end, MOTOR-ON, ERROR a BASIC
error on the screen (its line), HALT the CPU stopped on a JAM, STILL the
last three pictures the same, DARK the last one mostly black) and
<out>/sheet-NN.png, 30 tapes a sheet, a row of pictures each. "real" loads
in real time (ORIC_REALTIME): give it FRAMES=20000. Eight at once. Needs
Pillow.

usage: tapesmoke.py <folder> <out> <oric_host> <basic11b.rom> [real]"""
import glob, os, re, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from PIL import Image, ImageChops, ImageDraw

folder, out, host, rom = sys.argv[1:5]
real = len(sys.argv) > 5 and sys.argv[5] == 'real'
FRAMES = int(os.environ.get('FRAMES', '6000'))
SHOTS = int(os.environ.get('SHOTS', '1000'))
INPUT = os.environ.get('INPUT', '3500: |4500:\\n')
os.makedirs(out, exist_ok=True)

taps = []
for d, _, files in os.walk(folder):
    for f in files:
        p = os.path.join(d, f)
        with open(p, 'rb') as fh:
            if fh.read(4) == b'\x16\x16\x16\x24' or f.lower().endswith('.tap'):
                taps.append(p)
taps.sort()


def dark(im):
    """Mostly black: under 2% of the pixels lit"""
    g = im.convert('L').point(lambda v: 255 if v > 16 else 0)
    return g.histogram()[255] < 0.02 * im.width * im.height


def run(i_p):
    i, path = i_p
    name = os.path.relpath(path, folder).replace('/', '_')
    work = os.path.join(out, '%03d' % i)
    os.makedirs(work, exist_ok=True)
    for old in glob.glob(os.path.join(work, '*.ppm')):
        os.remove(old)
    tap = os.path.join(work, 'tape.tap')
    shutil.copy(path, tap)
    typed = '150:CLOAD""\\n' + ('|' + INPUT if INPUT else '')
    env = dict(os.environ, ORIC_TAPE=tap, ORIC_TAPE_LOG='1', ORIC_TYPE=typed, ORIC_TEXT='1',
               ORIC_SHOTS=str(SHOTS))
    if real:
        env['ORIC_REALTIME'] = '1'
    ppm = os.path.join(work, 'last.ppm')
    r = subprocess.run([host, rom, str(FRAMES), ppm], env=env, capture_output=True, text=True, timeout=900)
    lines = r.stdout.splitlines()
    loads = [l for l in lines if 'tape: loaded' in l]
    last = [l for l in lines if l.startswith('frame ')]
    state = last[-1] if last else ''
    m = re.search(r'tape (\d+)/(\d+) file (\d+)/(\d+).*motor (\d) playing (\d).*PC ([0-9A-F]{4})', state)
    pos, size, f, files, motor, playing, pc = m.groups() if m else ('?',) * 7
    moved, prev = 0, None
    for l in last:
        mm = re.search(r'frame (\d+): tape (\d+)/', l)
        if mm and mm.group(2) != prev:
            prev = mm.group(2)
            moved = int(mm.group(1))
    screen = [l[1:] for l in lines if l.startswith('|')]
    errors = [l.strip() for l in screen if 'ERROR' in l]
    halted = any('(halted' in l for l in lines)
    open(os.path.join(work, 'log.txt'), 'w').write(r.stdout)
    shots = sorted(glob.glob(os.path.join(work, 'last-*.ppm'))) + [ppm]
    still = dk = False
    try:
        pics = [Image.open(p).convert('RGB') for p in shots[-3:]]
        still = len(pics) == 3 and all(ImageChops.difference(pics[0], q).getbbox() is None for q in pics[1:])
        dk = dark(pics[-1])
    except Exception:
        pass
    flags = []
    if pos != size:
        flags.append('NOT-AT-END')
    if motor == '1':
        flags.append('MOTOR-ON')
    if errors:
        flags.append('ERROR "%s"' % errors[-1])
    if halted:
        flags.append('HALT')
    if still:
        flags.append('STILL')
    if dk:
        flags.append('DARK')
    return i, name, len(loads), pos, size, files, moved, pc, flags, shots


results = []
with ThreadPoolExecutor(8) as ex:
    for res in ex.map(run, enumerate(taps)):
        results.append(res)

with open(os.path.join(out, 'summary.txt'), 'w') as s:
    for i, name, loads, pos, size, files, moved, pc, flags, _ in results:
        line = '%03d %-40s loads %2d  tape %s/%s files %s  last move f%-5d PC %s %s' % (
            i, name, loads, pos, size, files, moved, pc, ' '.join(flags))
        print(line)
        s.write(line + '\n')

# contact sheets: 30 tapes each, a row of pictures a tape
w, h, per = 240, 224, 30
cols = max(len(r[-1]) for r in results) if results else 1
for k0 in range(0, len(results), per):
    part = results[k0:k0 + per]
    sheet = Image.new('RGB', (cols * w, len(part) * (h + 14)), 'white')
    draw = ImageDraw.Draw(sheet)
    for k, (i, name, *_rest, flags, pictures) in enumerate(part):
        y = k * (h + 14)
        draw.text((2, y), '%03d %s  %s' % (i, name, ' '.join(flags)), fill='black')
        for c, ppm in enumerate(pictures):
            try:
                sheet.paste(Image.open(ppm), (c * w, y + 14))
            except Exception:
                pass
    sheet.save(os.path.join(out, 'sheet-%02d.png' % (k0 // per)))
