"""One frame per LINE, taken at its midpoint -- the only honest way to judge framing.

Sampling a scene on a fixed 2-second grid keeps landing on the frame just after a cut,
where the camera is still holding the previous setup (it runs a frame behind the scene
clock -- 42ms at 24fps, but half a second when the sweep steps the clock at 2fps). Three
frames in a row got misread as "the camera framed the wrong woman" before that was
obvious. Read the .scn, take each line's middle, label it with who is speaking and what
the shot is supposed to be.
"""
import argparse, glob, os, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SCN = r"C:\MyVeryOwnVideoGames\reva\reva_native\scenes"

ap = argparse.ArgumentParser()
ap.add_argument("--fps", type=float, default=2.0)
ap.add_argument("--name", default="the_spot")
ap.add_argument("--dir", default=os.path.join(HERE, "_sweep"))
ap.add_argument("--out", default=os.path.join(HERE, "perline.png"))
ap.add_argument("--cols", type=int, default=4)
ap.add_argument("--tile", type=int, default=470)
ap.add_argument("--only", default="", help="comma list of line indices")
a = ap.parse_args()

lines, stages = [], []
for ln in open(os.path.join(SCN, "%s.scn" % a.name), encoding="utf-8"):
    if ln.startswith("line "):
        p = ln.split(None, 4)
        lines.append((int(p[1]), float(p[2]), float(p[3]), p[4].strip()))
    elif ln.startswith("stage "):
        d = dict(kv.split("=", 1) for kv in ln.split()[1:])
        stages.append(d)

pick = range(len(lines))
if a.only:
    pick = [int(x) for x in a.only.split(",")]

fs = []
for i in pick:
    spk, t0, dur, txt = lines[i]
    # +1: the camera runs one rendered frame behind the scene clock (camDirector is
    # called at the top of the loop, the clock advances at the bottom), so the frame
    # that SHOWS line i's setup is the one after line i's midpoint.
    n = int(round((t0 + dur * 0.5) * a.fps)) + 1
    p = os.path.join(a.dir, "f_%05d.ppm" % n)
    if not os.path.exists(p):
        p = os.path.join(a.dir, "f_%03d.ppm" % n)
    if os.path.exists(p):
        fs.append((i, p, "%02d %-5s %-5s hgt%s" % (i, "reva" if spk == 0 else "kaeli",
                                                   stages[i].get("shot", "?"),
                                                   stages[i].get("hgt", "0"))))
if not fs:
    sys.exit("no frames in %s" % a.dir)

im0 = Image.open(fs[0][1])
tw = a.tile; th = int(tw * im0.height / im0.width)
cols = a.cols; rows = (len(fs) + cols - 1) // cols
sh = Image.new("RGB", (cols * tw, rows * (th + 16)), (10, 10, 13))
d = ImageDraw.Draw(sh)
for k, (i, p, lab) in enumerate(fs):
    x, y = (k % cols) * tw, (k // cols) * (th + 16)
    sh.paste(Image.open(p).convert("RGB").resize((tw, th), Image.LANCZOS), (x, y + 16))
    d.text((x + 4, y + 3), lab, fill=(130, 255, 190))
sh.save(a.out)
print("wrote %s  (%d lines)" % (a.out, len(fs)))
