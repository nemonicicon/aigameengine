#!/usr/bin/env python3
"""ridecheck.py -- a skater, from four angles, across time. One sheet.

    python reva_native/anim_bench/ridecheck.py [--set rider] [--body r2moves]
                                               [--secs 5] [--cols 8] [--out X.png]

WHY THIS EXISTS. jacob, watching me judge a moving pose off single stills: "you need to
take more or maybe read the code to see how the vertices change location over time" and
then "the many shots like that is better, but you need multi angle as well."

He is right twice over, and the repo already had the first half written down as a law
(anim_verify_motion: animation is verified in MOTION, never from a still). A skate pose
lerps between rolling and carving and fires tricks on a timer, so ANY single frame is one
sample of something that is mostly not that. And a side view cannot show a stance that is
too narrow, a front view cannot show a knee bending backwards -- the two bugs found here
were each invisible from the other angle.

REVA_FIXEDSTEP is what makes the grid legible: with a virtual clock the same frame index
in two different runs is the SAME INSTANT, so four passes at four camera positions can be
laid out as rows and read down a column as one moment from four sides.

Rows are the angles, columns are time.
"""
import argparse
import math, glob, os, shutil, subprocess, sys, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
NATIVE = os.path.join(ROOT, "reva_native")
PY = os.environ.get("CLUB_PYTHON", sys.executable)

# eye, aim, fov -- around a rider standing near the origin facing +x
ANGLES = [
    ("side",  (0.0, 2.5, -7.0), (0.0, 1.5, 0.0), 30),
    ("front", (8.5, 2.4,  0.0), (0.0, 1.5, 0.0), 30),
    ("3/4",   (-5.5, 2.6, -5.5), (0.0, 1.5, 0.0), 32),
    ("low",   (-2.0, 0.55, -6.0), (0.0, 1.7, 0.0), 34),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--set", dest="setname", default="rider")
    ap.add_argument("--body", default="r2moves")
    ap.add_argument("--secs", type=float, default=5.0)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--start", type=int, default=420,
                    help="boot costs ~250 frames; capture after that")
    ap.add_argument("--cols", type=int, default=8)
    ap.add_argument("--orbit", type=int, default=0,
                    help="N evenly-spaced yaws around the subject instead of the four "
                         "named framings. 8 is the ring; every panel is comparable.")
    ap.add_argument("--radius", type=float, default=7.2, help="orbit distance, club units")
    ap.add_argument("--height", type=float, default=2.3, help="orbit eye height")
    ap.add_argument("--tile", type=int, default=0,
                    help="panel width; 0 = size it from --cols, so asking for fewer "
                         "columns gets BIGGER pictures rather than merely fewer")
    ap.add_argument("--sun", default="50,210,1.3,1.0,0.96,0.90")
    ap.add_argument("--sky", default="0.44,0.50,0.62")
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--out", default=os.path.join(NATIVE, "anim_bench", "ridecheck.png"))
    a = ap.parse_args()

    from PIL import Image, ImageDraw
    angles = ANGLES
    if a.orbit > 0:
        # \u2b50 A RING, not a handful of opinions about which sides matter. One height, one
        # radius, one fov -- so a difference between two panels is a difference in the
        # SUBJECT and not in how it was shot.
        angles = []
        for i in range(a.orbit):
            th = 2.0 * math.pi * i / a.orbit
            angles.append(("%d\u00b0" % round(math.degrees(th)),
                           (a.radius * math.cos(th), a.height, a.radius * math.sin(th)),
                           (0.0, 1.5, 0.0), 30))
    if a.tile <= 0:
        # \u26d4 A FIXED TILE MEANT --cols 2 GAVE FEWER PICTURES AT THE SAME SIZE. The sheet
        # has a width budget; spend it on however many columns were asked for.
        a.tile = max(150, min(430, 1680 // max(1, a.cols)))
    frames = int(a.secs * a.fps)
    every = max(1, frames // (a.cols * 3))
    tmp = os.path.join(NATIVE, "anim_bench", "_ride")
    rows = []

    for name, eye, aim, fov in angles:
        shutil.rmtree(tmp, ignore_errors=True)
        os.makedirs(tmp, exist_ok=True)
        cam = "%g,%g,%g,%g,%g,%g,%d" % (eye + aim + (fov,))
        ctl = os.path.join(NATIVE, "anim_bench", "lab_ctl.txt")
        open(ctl, "w").write(
            "seq=%d face=0 grid=0 dqs=0 clip=z_upright set=%s sun=%s sky=%s cam=%s\n"
            % (int(time.time()) % 100000, a.setname, a.sun, a.sky, cam))
        env = dict(os.environ, REVA_WINDOWED="1", REVA_LABCTL="anim_bench/lab_ctl.txt",
                   REVA_AUTOMODE="11", REVA_ANIMSUBJ=a.body, REVA_ANIMSTATE="0",
                   REVA_FIXEDSTEP=str(a.fps),
                   # ⛔ REVA_SHOT is what ARMS the capture. SHOTN/SHOTEVERY only say how
                   # many and how often -- without a start frame nothing is ever written,
                   # and the run just sits there until it is killed.
                   REVA_SHOT=str(a.start), REVA_SHOTN=str(frames),
                   REVA_SHOTEVERY=str(every),
                   REVA_SHOT_PATH=os.path.join(tmp, "f.ppm").replace("/", "\\"))
        for kv in a.env:
            k, _, v = kv.partition("=")
            env[k] = v
        # ⛔ THE ENGINE DOES NOT EXIT WHEN ITS SHOT QUOTA IS MET -- it keeps running
        # and the capture hangs forever. Poll for the frames we asked for, then stop it.
        pr = subprocess.Popen([os.path.join(NATIVE, "crowd_new.exe"), "400", "packs", a.body],
                              cwd=NATIVE, env=env,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        want = max(a.cols, frames // every - 1)
        for _ in range(600):
            if len(glob.glob(os.path.join(tmp, "*.ppm"))) >= want:
                break
            if pr.poll() is not None:
                break
            time.sleep(0.5)
        pr.terminate()
        try: pr.wait(timeout=10)
        except Exception: pr.kill()
        fs = sorted(glob.glob(os.path.join(tmp, "*.ppm")))[:a.cols]
        if not fs:
            sys.exit("no frames for angle %s -- is sets/%s.set present?" % (name, a.setname))
        rows.append((name, [Image.open(f).convert("RGB").copy() for f in fs]))
        print("  %-6s %d frames" % (name, len(fs)))

    # crop to the middle of the frame where the rider is, then tile
    box = (330, 40, 950, 700)
    tw = a.tile
    th = int(tw * (box[3] - box[1]) / (box[2] - box[0]))
    sh = Image.new("RGB", (tw * a.cols + 54, (th + 4) * len(rows)), (10, 10, 13))
    d = ImageDraw.Draw(sh)
    for r, (name, ims) in enumerate(rows):
        d.text((5, r * (th + 4) + th // 2), name, fill=(130, 255, 190))
        for c, im in enumerate(ims[:a.cols]):
            sh.paste(im.crop(box).resize((tw, th), Image.LANCZOS), (54 + c * tw, r * (th + 4)))
    sh.save(a.out)
    shutil.rmtree(tmp, ignore_errors=True)
    print("wrote %s  (%d angles x %d frames)" % (a.out, len(rows), a.cols))


if __name__ == "__main__":
    main()
