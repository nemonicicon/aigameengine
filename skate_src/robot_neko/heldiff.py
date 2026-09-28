#!/usr/bin/env python3
"""heldiff.py MP4 T IDXA IDXB OUT.png [--amp 8] [--grid 8x6] [--thr 10] -- WHAT moves on a held pair.

ep 4's OPEN RESIDUE: a held frame (two frames inside the same stop-motion exposure, which
should be bit-identical) drifts by a fraction of a percent, worst on a tight/solo. A whole-frame
percentage says THAT something moves; this says WHERE, which is the difference between guessing
at a fix and finding one. T is a start time (mp4steps.py's own -ss); IDXA/IDXB are 0-based frame
offsets from T within the SAME single extraction -- pick an "h" pair from mp4steps.py's pattern
(e.g. its 12-frame run at T, pair indices (1,2)) and pass the SAME T and those two indices here.
⛔ Two independent `-ss` seeks for "the same" two timestamps is NOT guaranteed to land on the
same frames mp4steps compared -- fractional-second seeks can drift a frame either way, and a
drifted pair reads as a bigger (wrong) diff or a suspiciously clean (also wrong) one. Pulling
both frames from ONE extraction, the same way mp4steps does, is what makes the two tools agree.

Writes an AMPLIFIED diff image (every channel's |a-b| * --amp, clipped) so a sub-pixel drift
is visible at all, plus the bounding box of pixels over --thr and the busiest cells of a
--grid (colsxrows) grid -- the two numbers that turn "0.3% moved" into "it's her hairline".
"""
import glob, os, subprocess, sys, tempfile
import numpy as np
from PIL import Image

a = sys.argv[1:]
mp4, t0, ia, ib, out = a[0], float(a[1]), int(a[2]), int(a[3]), a[4]
amp = float(a[a.index("--amp") + 1]) if "--amp" in a else 8.0
grid = a[a.index("--grid") + 1] if "--grid" in a else "8x6"
gc, gr = (int(x) for x in grid.split("x"))
thr = int(a[a.index("--thr") + 1]) if "--thr" in a else 10

FF = "ffmpeg"
for cand in glob.glob(os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "WinGet",
                                    "Packages", "Gyan.FFmpeg*", "ffmpeg*", "bin", "ffmpeg.exe")):
    FF = cand; break

d = tempfile.mkdtemp()
n = max(ia, ib) + 1
subprocess.run([FF, "-y", "-ss", "%.3f" % t0, "-i", mp4, "-frames:v", str(n),
                os.path.join(d, "f%03d.png")], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
frames = sorted(glob.glob(os.path.join(d, "f*.png")))
if len(frames) <= max(ia, ib):
    sys.exit("heldiff: only got %d frames from t0=%.3f, needed index %d" % (len(frames), t0, max(ia, ib)))


def grab(idx):
    return np.asarray(Image.open(frames[idx]).convert("RGB"), dtype=np.int16)


A, B = grab(ia), grab(ib)
if A.shape != B.shape:
    sys.exit("heldiff: frame size mismatch %s vs %s" % (A.shape, B.shape))
H, W, _ = A.shape

diff = np.abs(A - B)                      # per-channel, 0..255
mono = diff.max(axis=2)                   # the biggest channel move per pixel, for bbox/grid
hot = mono > thr

pct = 100.0 * hot.sum() / (W * H)
if hot.any():
    ys, xs = np.where(hot)
    bbox = (int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max()))
else:
    bbox = None

cellw, cellh = W / gc, H / gr
counts = np.zeros((gr, gc), dtype=np.int64)
if hot.any():
    cx = (xs / cellw).astype(int).clip(0, gc - 1)
    cy = (ys / cellh).astype(int).clip(0, gr - 1)
    np.add.at(counts, (cy, cx), 1)
order = np.dstack(np.unravel_index(np.argsort(-counts, axis=None), counts.shape))[0]

amped = np.clip(diff.astype(np.float32) * amp, 0, 255).astype(np.uint8)
Image.fromarray(amped, "RGB").save(out)

print("heldiff: %s  t0=%.3f idx %d,%d  %dx%d" % (mp4, t0, ia, ib, W, H))
print("  changed >%d/255: %.3f%%  bbox=%s" % (thr, pct, bbox if bbox else "(none)"))
print("  busiest cells (col,row of %dx%d grid), top 5:" % (gc, gr))
for cy, cx in order[:5]:
    if counts[cy, cx] == 0:
        break
    print("    (%d,%d)  %d px  x=%.0f-%.0f y=%.0f-%.0f" %
          (cx, cy, counts[cy, cx], cx * cellw, (cx + 1) * cellw, cy * cellh, (cy + 1) * cellh))
print("  wrote %s  <-- LOOK AT IT" % out)
