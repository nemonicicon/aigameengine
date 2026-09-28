#!/usr/bin/env python3
"""scenecheck.py -- IS ANYTHING ACTUALLY MOVING, AND IS THE CAMERA LOOKING AT IT?

    python skate_src/film/scenecheck.py takes/            # every take
    python skate_src/film/scenecheck.py takes/ii_denial_A.mp4 --sheet

jacob: "make sure the screenshots you get when you film these are logical. as in, is this
character traveling when they are walking? are they using a proper animation? basic stuff
like that."

⛔ THE FAILURES THIS IS FOR ARE ALL SILENT ONES. A body playing `z_upright` -- which is
EIGHT FRAMES of a static pose -- renders perfectly, lights perfectly, lip-syncs perfectly,
and is a photograph. A body whose walk cycle plays while its root never moves renders
perfectly and moonwalks. Neither produces an error, a warning, or a log line. Both have
shipped in this film already.

⭐ SO MEASURE MOTION ENERGY: the mean absolute difference between consecutive frames,
per second of take. It needs no model of a body and no idea what a clip is called.

    a real performance      -- energy varies, with peaks on gestures and cuts
    a static pose           -- energy at the floor and FLAT, for seconds at a time
    a hard cut              -- one enormous spike (excluded from the stillness test)
    a locked-off empty shot -- also flat, which is why FLATNESS is reported beside it

⛔ AND STILLNESS IS NOT ALWAYS A BUG. The lull is seven HELD shots and act V opens on
three seconds of nothing; both are supposed to be still. This prints WHERE the stillness
is so it can be judged, and never says "fail" on its own -- the same rule as framecheck.
It is a summons to look, not a verdict.

Reports per take: seconds, mean/min energy, the longest dead stretch and its timecode,
the fraction of the take that is flat colour, and how many hard cuts it contains.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys

DEAD = 0.9       # mean abs frame delta (0-255) below which a second is "nothing moved"
FLAT = 0.88      # fraction-of-one-colour above which a frame reads as empty
CUT = 18.0       # delta above which it is a cut, not motion


def frames(path, fps, out):
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    ff = shutil.which("ffmpeg") or "ffmpeg"
    r = subprocess.run([ff, "-y", "-v", "error", "-i", path,
                        "-vf", "fps=%s,scale=128:-2" % fps,
                        os.path.join(out, "f_%05d.png")])
    return r.returncode == 0


def flatness(im):
    q = im.quantize(colors=16, method=2).convert("RGB")
    cols = q.getcolors(1 << 16) or []
    if not cols:
        return 1.0
    cols.sort(reverse=True)
    top = cols[0][1]
    tot = sum(c for c, _ in cols)
    near = sum(c for c, px in cols if all(abs(a - b) <= 30 for a, b in zip(px, top)))
    return near / float(tot or 1)


def analyse(path, fps, tmp):
    from PIL import Image, ImageChops, ImageStat
    if not frames(path, fps, tmp):
        return None
    fs = sorted(glob.glob(os.path.join(tmp, "*.png")))
    if len(fs) < 3:
        return None
    step = 1.0 / float(fps)
    ims = [Image.open(f).convert("L") for f in fs]
    energy = []
    for a, b in zip(ims, ims[1:]):
        energy.append(ImageStat.Stat(ImageChops.difference(a, b)).mean[0])
    flats = [flatness(Image.open(f).convert("RGB")) for f in fs]

    cuts = sum(1 for e in energy if e >= CUT)
    live = [e for e in energy if e < CUT]          # cuts are not performance
    # longest run of consecutive dead samples
    best = cur = 0
    best_i = cur_i = 0
    for i, e in enumerate(energy):
        if e < DEAD:
            if cur == 0:
                cur_i = i
            cur += 1
            if cur > best:
                best, best_i = cur, cur_i
        else:
            cur = 0
    return dict(
        secs=len(fs) * step,
        mean=sum(live) / max(1, len(live)),
        lo=min(live) if live else 0.0,
        dead=best * step, dead_at=best_i * step,
        flat=sum(1 for f in flats if f >= FLAT) / float(len(flats)),
        cuts=cuts, n=len(fs))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("target")
    ap.add_argument("--fps", default="2")
    ap.add_argument("--sheet", action="store_true", help="also write a contact sheet per take")
    a = ap.parse_args()
    from PIL import Image

    paths = ([a.target] if a.target.endswith(".mp4")
             else sorted(p for p in glob.glob(os.path.join(a.target, "*.mp4"))
                         if "_A" in os.path.basename(p) or "_B" in os.path.basename(p)
                         or os.path.basename(p).startswith("b_")))
    if not paths:
        print("no takes found in %s" % a.target)
        return 1
    tmp = os.path.join(os.path.dirname(os.path.abspath(paths[0])), "_scenecheck")
    print("%-26s %6s %6s %6s %8s %6s %5s" %
          ("take", "secs", "mean", "min", "deadest", "flat%", "cuts"))
    print("-" * 72)
    rows = []
    for p in paths:
        r = analyse(p, a.fps, tmp)
        n = os.path.basename(p)[:26]
        if not r:
            print("%-26s  -- unreadable --" % n)
            continue
        rows.append((n, r))
        flag = ""
        if r["dead"] >= 3.0:
            flag = "  <-- %0.0fs STILL at %0.0fs" % (r["dead"], r["dead_at"])
        if r["flat"] >= 0.25:
            flag += "  <-- %0.0f%% EMPTY" % (100 * r["flat"])
        print("%-26s %6.1f %6.2f %6.2f %7.1fs %5.0f%% %5d%s" %
              (n, r["secs"], r["mean"], r["lo"], r["dead"], 100 * r["flat"], r["cuts"], flag))
        if a.sheet:
            fs = sorted(glob.glob(os.path.join(tmp, "*.png")))
            keep = fs[::max(1, len(fs) // 24)][:24]
            ims = [Image.open(f).convert("RGB") for f in keep]
            tw, th = ims[0].size
            sh = Image.new("RGB", (tw * 6, th * ((len(ims) + 5) // 6)), (255, 255, 255))
            for i, im in enumerate(ims):
                sh.paste(im, ((i % 6) * tw, (i // 6) * th))
            out = os.path.splitext(p)[0] + "_sheet.png"
            sh.save(out)
            print("      sheet -> %s" % out)
    print("")
    print("NOTE: stillness is not automatically a fault -- the lull is seven HELD shots")
    print("      and act V opens on three seconds of nothing. This says WHERE to look.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
