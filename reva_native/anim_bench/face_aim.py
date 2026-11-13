#!/usr/bin/env python3
"""face_aim.py -- find a face's mouth and eye line BY LOOKING AT THE RENDER, then say
how far the marks are off.

    face_aim.py <subject> [--partner name]

Neither geometry nor texture colour could locate this face on its own: the nose-bump
heuristic finds the fringe, proportions from the "chin" are measuring a throat, and on
reva the whole head is purple -- hair and skin share a hue, so "the reddest texel" is
a hair highlight. What is unambiguous is the PICTURE. The face rig already frames the
head; this shoots it, finds the features in the panel, and converts pixel rows back
into world offsets using the camera the engine used.

    half-extent at the aim plane = dist * tan(fov/2)
    dy_world = (row_centre - row) / (panel_h/2) * half-extent

so a feature's offset from the current aim point comes straight out of where it landed
in frame. Run it, apply the offset to the marks, run it again: the loop closes.

The features are told apart by SHAPE, not by hue: lips are a wide, thin, horizontal
band of the most saturated warm pixels near the midline; the eye line is the row with
the two brightest specular blobs placed symmetrically about it.
"""
import sys, os, subprocess, math

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.path.dirname(HERE)
PY = os.environ.get("CLUB_PYTHON", sys.executable)

# must match the engine's face rig (crowd_kms.c, g_labFace)
PANEL_W, PANEL_H = 640, 360
MOUTH_PANEL = (0, 360, 640, 720)      # gi 8, bottom-left
EYE_PANEL = (640, 360, 1280, 720)     # gi 9, bottom-right
DIST, FOV = 0.60, 22.0

def shoot(subject, ctl, out):
    open(os.path.join(NATIVE, "anim_bench", "lab_ctl.txt"), "w").write(ctl)
    env = dict(os.environ, REVA_ANIMSUBJ=subject, REVA_WINDOWED="1", REVA_AUTOMODE="11",
               REVA_ANIMSTATE="0", REVA_SHOT="600", REVA_SHOTN="1", REVA_SHOT_PATH=out)
    subprocess.run([os.path.join(NATIVE, "crowd_new.exe"), "400", "packs", subject],
                   cwd=NATIVE, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return os.path.exists(out)

def analyse(path):
    from PIL import Image
    import colorsys
    im = Image.open(path).convert("RGB")
    half = DIST * math.tan(math.radians(FOV) / 2.0)

    def rows(box, want):
        crop = im.crop(box)
        w, h = crop.size
        px = crop.load()
        score = [0.0] * h
        pos = [[] for _ in range(h)]
        for y in range(h):
            for x in range(w):
                c = px[x, y]
                hh, ss, vv = colorsys.rgb_to_hsv(c[0] / 255, c[1] / 255, c[2] / 255)
                if want == "lips":
                    # warm and saturated, and NOT the purple of her hair
                    warm = (hh < 0.06 or hh > 0.93)
                    if warm and ss > 0.30 and vv > 0.22:
                        score[y] += ss * vv; pos[y].append(x)
                else:
                    # specular highlights: the brightest, least saturated pixels
                    if vv > 0.72 and ss < 0.45:
                        score[y] += vv; pos[y].append(x)
        return score, pos, w, h

    out = {}
    for name, box, want in (("mouth", MOUTH_PANEL, "lips"), ("eyes", EYE_PANEL, "spec")):
        score, pos, w, h = rows(box, want)
        # smooth, then take the strongest band
        sm = [(score[max(0, y-2)] + score[max(0, y-1)] + 2*score[y] +
               score[min(h-1, y+1)] + score[min(h-1, y+2)]) / 6.0 for y in range(h)]
        best = max(range(h), key=lambda y: sm[y])
        if sm[best] <= 0.0:
            out[name] = None; print("   %-5s: nothing found" % name); continue
        xs = pos[best] or [w // 2]
        cx = sum(xs) / len(xs)
        dy = (h / 2.0 - best) / (h / 2.0) * half
        dx = (cx - w / 2.0) / (w / 2.0) * half * (w / float(h))
        out[name] = (dy, dx, best, sm[best])
        print("   %-5s: row %3d of %d  -> aim is %+.4f world units off vertically "
              "(%+.1f cm), %+.4f horizontally  [strength %.1f]"
              % (name, best, h, dy, dy * 50, dx, sm[best]))
    return out

if __name__ == "__main__":
    subj = sys.argv[1] if len(sys.argv) > 1 else "r2moves"
    shot = os.path.join(os.environ.get("TEMP", "."), "face_aim.ppm")
    ctl = "seq=1 face=10 dqs=0 clip=z_upright partner=off hi5=0 once=0\n"
    if not shoot(subj, ctl, shot):
        raise SystemExit("no shot written")
    print("face_aim: %s" % subj)
    analyse(shot)
