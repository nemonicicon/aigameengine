#!/usr/bin/env python3
"""mp4steps.py MP4 T1,T2,... [--n 12] [--thr 10] -- is the DELIVERED file on twos?

For each start time, pull N consecutive frames out of the mp4 and print the % of pixels that
changed by more than --thr/255 between each neighbouring pair. On twos the pairs ALTERNATE
large/small (a held pair is not zero in a scene -- the lens is still easing, which is the
rule: the camera is not a puppet); on ones every pair moves.

⛔ The first version selected frames with `-vf select='gte(n\\,N)'` and got ZERO frames back
without an error. Input-side seeking (`-ss T -i file`) decodes forward from the keyframe and
is frame-accurate on modern ffmpeg; that is what this uses.
"""
import glob, os, shutil, subprocess, sys, tempfile
from PIL import Image, ImageChops

a = sys.argv[1:]
mp4 = a[0]
times = [float(x) for x in a[1].split(",")]
n = int(a[a.index("--n") + 1]) if "--n" in a else 12
thr = int(a[a.index("--thr") + 1]) if "--thr" in a else 10
FF = next(iter(glob.glob(os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "WinGet",
                                      "Packages", "Gyan.FFmpeg*", "ffmpeg*", "bin", "ffmpeg.exe"))))
for t in times:
    d = tempfile.mkdtemp()
    subprocess.run([FF, "-y", "-ss", "%.3f" % t, "-i", mp4, "-frames:v", str(n), os.path.join(d, "f%03d.png")],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ims = [Image.open(f).convert("L") for f in sorted(glob.glob(os.path.join(d, "f*.png")))]
    vals = []
    for i in range(len(ims) - 1):
        dd = ImageChops.difference(ims[i], ims[i + 1]).point(lambda v: 255 if v > thr else 0)
        vals.append(100.0 * dd.histogram()[255] / (ims[0].size[0] * ims[0].size[1]))
    # a pair is HELD when it is under half of both neighbours
    pat = ""
    for i, v in enumerate(vals):
        nb = [vals[j] for j in (i - 1, i + 1) if 0 <= j < len(vals)]
        pat += "h" if nb and v < 0.5 * min(nb) else "E"
    print("t=%6.2f  %2d frames  %s   %s" % (t, len(ims), pat, " ".join("%.2f" % v for v in vals)))
    shutil.rmtree(d, ignore_errors=True)
