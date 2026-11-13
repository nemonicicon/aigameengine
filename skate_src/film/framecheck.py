#!/usr/bin/env python3
"""framecheck.py -- IS THE CAMERA EVEN POINTED AT THEM?

    python skate_src/film/framecheck.py

⛔ THE REASON THIS EXISTS. The dogwalk A camera shoots an EMPTY STREET: the pair walks out
of the left edge inside the first second and the remaining fifty seconds are a sidewalk
with subtitles over it. Nothing in the pipeline noticed, because a take that renders is a
take that "worked" -- the shoot script checks the file exists and the file was 4 MB of
correctly-lit nobody.

⭐ AND IT NEEDS NO ENGINE. Where the pair stands is `stage=x,z,yaw` plus whatever `hx=`/
`hz=` marks the scene script moves them to; where the camera looks is `cam=ex,ey,ez,
ax,ay,az,fov`. Whether a point is inside that frustum is arithmetic. So every mark of
every scene can be checked against both lenses in a second, instead of shooting forty
minutes of film and looking at it afterwards.

It reports, per camera, per mark:
    dist    how far the mark is from the lens (near plane is 0.8 in club units)
    off     the angle between the lens axis and the mark, in degrees
    half    half the vertical field of view -- the mark must sit inside it
and flags OUT when a mark falls outside the cone.

⛔ IT IS A CONE, NOT A FRUSTUM. Checking the angle to the frame's centre is a slightly
STRICTER test than the real rectangle at the corners and a slightly looser one at the top
and bottom edges, so a mark just outside the cone may still be on screen. It is a summons
to look, not a verdict -- which is the only kind of automatic check this project trusts.
"""
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shoot as S

NATIVE = S.NATIVE
HEAD_Y = 1.55          # a standing head, club units -- what the lens is actually framing


def marks(scene, stage):
    """Every place the pair is asked to stand during a scene: the stage, then every hx/hz."""
    sx, sz, _yaw = [float(v) for v in stage.split(",")]
    out = [("stage", sx, sz)]
    path = os.path.join(NATIVE, "scenes", scene + ".txt")
    try:
        txt = open(path, encoding="utf-8").read()
    except OSError:
        return out
    x, z = sx, sz
    for i, ln in enumerate(txt.splitlines()):
        if ln.startswith("#") or "|" not in ln:
            continue
        mx = re.search(r"\bhx=(-?[\d.]+)", ln)
        mz = re.search(r"\bhz=(-?[\d.]+)", ln)
        if not (mx or mz):
            continue
        if mx:
            x = float(mx.group(1))
        if mz:
            z = float(mz.group(1))
        out.append(("line %d" % (i + 1), x, z))
    return out


BODY_H = 1.75          # a standing body, club units -- what "big enough to act" is measured against
MIN_FRAC = 0.12        # below this a face stops being a performance and becomes a silhouette


def check(cam, x, z):
    ex, ey, ez, ax, ay, az, fov = [float(v) for v in cam.split(",")]
    fx, fy, fz = ax - ex, ay - ey, az - ez
    fl = math.sqrt(fx * fx + fy * fy + fz * fz)
    if fl < 1e-6:
        return None
    fx, fy, fz = fx / fl, fy / fl, fz / fl
    tx, ty, tz = x - ex, HEAD_Y - ey, z - ez
    tl = math.sqrt(tx * tx + ty * ty + tz * tz)
    if tl < 1e-6:
        return (0.0, 180.0, fov / 2.0)
    d = (tx * fx + ty * fy + tz * fz) / tl
    off = math.degrees(math.acos(max(-1.0, min(1.0, d))))
    # ⭐ AND HOW BIG SHE IS IN IT. "In frame" is not "readable": this passed act IV,
    # which is 112 seconds of a parked pickup with the three leads as specks at the edge.
    # A body of BODY_H at distance tl subtends 2*atan(h/2/tl); the frame subtends fov.
    # The ratio is the fraction of picture height she occupies.
    sub = 2.0 * math.degrees(math.atan(BODY_H / 2.0 / max(0.01, tl)))
    return (tl, off, fov / 2.0, sub / fov)


def main():
    bad = 0
    for row in S.SHOTS:
        name, act, _h, _p, _p2, _set, stage, _hr, camA, camB = row[:10]
        mk = marks(name, stage)
        print("\n=== act %-3s %-12s  stage %s   %d mark(s) ===" % (act, name, stage, len(mk)))
        for label, cam in (("A", camA), ("B", camB)):
            if not cam:
                print("  %s: the conversation lens -- it FOLLOWS the heads, nothing to check" % label)
                continue
            worst = []
            for (tag, x, z) in mk:
                r = check(cam, x, z)
                if r is None:
                    continue
                tl, off, half, frac = r
                flag = ""
                if tl < 0.8:
                    flag = "  <-- INSIDE THE NEAR PLANE"
                elif off > half:
                    flag = "  <-- OUT OF FRAME"
                elif frac < MIN_FRAC:
                    flag = "  <-- TOO SMALL: %.0f%% of frame height" % (100 * frac)
                if flag:
                    bad += 1
                    worst.append("     %-9s dist %6.1f  off %5.1f deg  size %4.0f%%%s"
                                 % (tag, tl, off, 100 * frac, flag))
            print("  %s: %s" % (label, "all %d mark(s) in frame" % len(mk) if not worst
                                else "%d of %d mark(s) MISSED" % (len(worst), len(mk))))
            for w in worst:
                print(w)
    print("\n%d camera/mark failure(s). A miss is a summons: shoot that one lens and LOOK."
          % bad)
    return 0


if __name__ == "__main__":
    sys.exit(main())
