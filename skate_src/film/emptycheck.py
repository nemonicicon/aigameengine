#!/usr/bin/env python3
"""emptycheck.py -- HOW MUCH OF THE FILM IS NOBODY?

    python skate_src/film/emptycheck.py seventy_four.mp4 [--fps 2]

⛔ WHY THIS EXISTS. Two full shoots were called finished while a third of the running
time was an empty sky with a shadow on it, and both times I missed it by eyeballing five
frames of a take. jacob: "if you take more screenshots it would be apparent to you". It
was. So this counts, instead of looking.

⭐ THE MEASURE IS FLATNESS, and it needs no model of what a person looks like. A shot of a
street, a room or a face spreads its pixels over many colours; a frame of open sky is one
colour with a gradient in it. So: sample the frame down, and report the fraction of it
that sits within a small distance of the frame's OWN most common colour. Sky and black
come back over 0.90; anything with a body, a house or a car in it comes back far lower.

⛔ IT IS A SMELL, NOT A VERDICT. A deliberate hold on an empty road at dawn is a shot and
will be flagged; a night interior is dark but not flat. Read the timecodes it prints and
go and look at those seconds -- which is the point, because now you know WHICH seconds.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys

FLAT = 0.88          # fraction-of-one-colour above which a frame reads as empty
TOL = 30             # per-channel distance that still counts as "the same colour"


def sample(path, fps, out):
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    ff = shutil.which("ffmpeg") or "ffmpeg"
    r = subprocess.run([ff, "-y", "-v", "error", "-i", path,
                        "-vf", "fps=%s,scale=96:-2" % fps,
                        os.path.join(out, "f_%05d.png")])
    return r.returncode == 0


def flatness(im):
    """Fraction of the frame within TOL of its own most common colour."""
    q = im.quantize(colors=16, method=2).convert("RGB")
    cols = q.getcolors(96 * 96) or []
    if not cols:
        return 1.0
    cols.sort(reverse=True)
    n, top = cols[0]
    tot = sum(c for c, _ in cols)
    near = 0
    for c, px in cols:
        if all(abs(a - b) <= TOL for a, b in zip(px, top)):
            near += c
    return near / float(tot or 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("film")
    ap.add_argument("--fps", default="2")
    ap.add_argument("--tmp", default="")
    a = ap.parse_args()
    from PIL import Image
    tmp = a.tmp or os.path.join(os.path.dirname(os.path.abspath(a.film)), "_emptycheck")
    if not sample(a.film, a.fps, tmp):
        print("ffmpeg failed")
        return 1
    fs = sorted(glob.glob(os.path.join(tmp, "*.png")))
    if not fs:
        print("no frames")
        return 1
    step = 1.0 / float(a.fps)
    flags, runs, cur = [], [], None
    for i, f in enumerate(fs):
        v = flatness(Image.open(f).convert("RGB"))
        empty = v >= FLAT
        flags.append(empty)
        if empty and cur is None:
            cur = i
        elif not empty and cur is not None:
            runs.append((cur, i - 1))
            cur = None
    if cur is not None:
        runs.append((cur, len(fs) - 1))
    n = sum(1 for e in flags if e)
    print("%s -- %d frames sampled at %s fps (%.1fs of film)"
          % (os.path.basename(a.film), len(fs), a.fps, len(fs) * step))
    print("EMPTY: %d frames = %.1f%% of the running time" % (n, 100.0 * n / len(fs)))
    runs.sort(key=lambda r: r[0] - r[1])
    print("longest empty stretches (go and look at these):")
    for (s0, s1) in runs[:10]:
        if (s1 - s0 + 1) * step < 1.0:
            continue
        print("   %5.1fs .. %5.1fs   (%.1fs)" % (s0 * step, (s1 + 1) * step,
                                                 (s1 - s0 + 1) * step))
    return 0


if __name__ == "__main__":
    sys.exit(main())
