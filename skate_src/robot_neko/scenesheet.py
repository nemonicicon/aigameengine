#!/usr/bin/env python3

"""scenesheet.py MP4 SCN OUT.png [--extra t1,t2,...] [--fps 24] [--card <seconds>]



One frame per LINE of a recorded scene, at the line's midpoint plus one frame (the camera

runs one frame behind the scene clock -- design/scene_direction.md), tiled with the line's

number, time and text. --extra adds frames at absolute seconds (a beat with no line in it).

--card adds an offset to all sampled times to compensate for a packaged episode's title card

(default 0.0; package.py uses ~2.835s for the card duration).



This is the ROBOT NEKO test-take read: shoot the sketch at 8fps, run this, LOOK, fix, and only

then spend a 24fps take. On episode 1 it caught a head cut off in every two-shot, a mark that

stood reva inside a bench, a nude body and a blown-out sky before the real take was shot.

"""

import glob, os, subprocess, sys, tempfile
import shutil

from PIL import Image, ImageDraw



a = sys.argv[1:]

mp4, scn, out = a[0], a[1], a[2]

extra = [float(x) for x in a[a.index("--extra") + 1].split(",")] if "--extra" in a else []

fps = float(a[a.index("--fps") + 1]) if "--fps" in a else 24.0

card = float(a[a.index("--card") + 1]) if "--card" in a else 0.0

FF = shutil.which("ffmpeg") or next(iter(glob.glob(os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "WinGet",

                                      "Packages", "Gyan.FFmpeg*", "ffmpeg*", "bin", "ffmpeg.exe"))), None)   # PATH first (Linux, 09-30), then WinGet

shots = []

nline = 0

for ln in open(scn, encoding="utf-8"):

    if ln.startswith("line "):

        b = ln.split(None, 4)

        t0, dur, txt = float(b[2]), float(b[3]), (b[4].strip() if len(b) > 4 else "")

        shots.append((t0 + dur * 0.5 + 1.0 / fps + card, "L%d %s" % (nline, txt[:34])))

        nline += 1

for t in extra:

    shots.append((t + card, "t=%.2f" % t))

shots.sort()

if card != 0.0:

    print("card offset %.3fs" % card)

tmp = tempfile.mkdtemp()

tiles = []

for i, (t, lab) in enumerate(shots):

    p = os.path.join(tmp, "f%03d.png" % i)

    subprocess.run([FF, "-y", "-ss", "%.3f" % t, "-i", mp4, "-frames:v", "1", p],

                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    if os.path.exists(p):

        im = Image.open(p).convert("RGB").resize((426, 240))

        d = ImageDraw.Draw(im)

        d.rectangle([0, 0, 426, 16], fill=(0, 0, 0))

        d.text((4, 2), "%5.2fs %s" % (t, lab), fill=(255, 255, 0))

        tiles.append(im)

cols = 4

rows = (len(tiles) + cols - 1) // cols

sheet = Image.new("RGB", (426 * cols, 240 * rows), (40, 40, 40))

for i, im in enumerate(tiles):

    sheet.paste(im, ((i % cols) * 426, (i // cols) * 240))

sheet.save(out)

print("wrote %s (%d tiles)" % (out, len(tiles)))

