#!/usr/bin/env python3
"""daycheck.py -- the same street, all the way through a day. One sheet.

    python reva_native/anim_bench/daycheck.py [--set parsons] [--from 5] [--to 23]
                                              [--cols 10] [--cam "ex,ey,ez,ax,ay,az,fov"]

SEVENTY-FOUR runs from a wet 7am to a boarded window after dark, so the sun going
down is not dressing, it is the plot. This shoots the arc as a strip: one camera, one set,
the clock running underneath.

⭐ IT RUNS THE CLOCK RATHER THAN SETTING IT. Twelve launches at twelve fixed hours would
each cost a ~15s boot and would only prove the twelve keyframes; `todrate=` lets one launch
play the whole day and the capture samples THROUGH it, which is the path the film actually
uses and the only way an interpolation bug between two keys can show up.

With REVA_FIXEDSTEP the clock is exact: at rate R hours per second and F steps per second,
every rendered frame advances the day by R/F hours, so frame N is a known hour and the
column labels are not guesses.

⛔ REVA_SHOT is what ARMS a capture (SHOTN/SHOTEVERY only say how many and how often), and
the engine never exits once its quota is met -- poll for the frames, then kill it.
"""
import argparse, glob, os, shutil, subprocess, sys, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
NATIVE = os.path.join(ROOT, "reva_native")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--set", dest="setname", default="parsons")
    ap.add_argument("--from", dest="h0", type=float, default=5.0)
    ap.add_argument("--to", dest="h1", type=float, default=23.0)
    ap.add_argument("--cols", type=int, default=10)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--rate", type=float, default=1.0, help="hours per second")
    ap.add_argument("--start", type=int, default=420, help="boot costs ~250 frames")
    ap.add_argument("--cam", default="-2,3.4,2,70,2.2,6,40")
    ap.add_argument("--tile", type=int, default=320)
    ap.add_argument("--out", default=os.path.join(NATIVE, "anim_bench", "daycheck.png"))
    a = ap.parse_args()
    from PIL import Image, ImageDraw

    span = a.h1 - a.h0
    frames = int(span / a.rate * a.fps)          # how many rendered frames the day takes
    every = max(1, frames // a.cols)
    tmp = os.path.join(NATIVE, "anim_bench", "_day")
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp, exist_ok=True)

    # ⛔ THE CLOCK STARTS AT BOOT, NOT AT THE FIRST FRAME. The ctl is read within the
    # first few frames, and the capture does not begin until `--start` (~420, because
    # booting costs ~250). At 1 h/s and 30fps that is FOURTEEN HOURS of day gone before
    # the first shot -- the first sheet was labelled 05:00 and was actually looking at
    # a 19:00 dusk. Wind the clock back by exactly the boot so frame 0 reads h0.
    lead = a.start * (a.rate / a.fps)
    h_ctl = (a.h0 - lead) % 24.0
    ctl = os.path.join(NATIVE, "anim_bench", "lab_ctl.txt")
    open(ctl, "w").write(
        "seq=%d face=0 grid=0 dqs=0 clip=z_upright set=%s tod=%g todrate=%g cam=%s\n"
        % (int(time.time()) % 100000, a.setname, h_ctl, a.rate, a.cam))
    print("clock set to %05.2f so the first captured frame reads %05.2f "
          "(%.1f h of boot)" % (h_ctl, a.h0, lead))
    env = dict(os.environ, REVA_WINDOWED="1", REVA_LABCTL="anim_bench/lab_ctl.txt",
               REVA_AUTOMODE="11", REVA_ANIMSUBJ="r2moves", REVA_ANIMSTATE="0",
               REVA_FIXEDSTEP=str(a.fps),
               REVA_SHOT=str(a.start), REVA_SHOTN=str(a.cols),
               REVA_SHOTEVERY=str(every),
               REVA_SHOT_PATH=os.path.join(tmp, "f.ppm").replace("/", "\\"))
    pr = subprocess.Popen([os.path.join(NATIVE, "crowd_new.exe"), "400", "packs", "r2moves"],
                          cwd=NATIVE, env=env,
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(180):
        if len(glob.glob(os.path.join(tmp, "*.ppm"))) >= a.cols:
            break
        if pr.poll() is not None:
            break
        time.sleep(1)
    time.sleep(0.8)
    pr.kill()
    got = sorted(glob.glob(os.path.join(tmp, "*.ppm")))
    print("captured %d frames over %.1f..%.1f h" % (len(got), a.h0, a.h1))
    if not got:
        sys.exit("no frames -- is crowd_new.exe built?")

    ims = [Image.open(f).convert("RGB") for f in got[:a.cols]]
    tw = a.tile
    th = int(tw * ims[0].height / ims[0].width)
    cols = min(a.cols, len(ims))
    rows = (cols + 4) // 5
    per = min(5, cols)
    sheet = Image.new("RGB", (per * tw, rows * (th + 20)), (12, 12, 15))
    d = ImageDraw.Draw(sheet)
    for i, im in enumerate(ims[:cols]):
        # frame i was captured at start + i*every rendered steps, and every step is
        # rate/fps hours -- so the hour on the label is derived, not assumed
        h = a.h0 + (i * every) * (a.rate / a.fps)
        rx, ry = i % per, i // per
        sheet.paste(im.resize((tw, th), Image.LANCZOS), (rx * tw, ry * (th + 20) + 20))
        d.text((rx * tw + 6, ry * (th + 20) + 5),
               "%02d:%02d" % (int(h) % 24, int((h % 1) * 60)), fill=(255, 220, 130))
    sheet.save(a.out)
    print("wrote %s  (%d hours across %d frames)" % (a.out, span, cols))


if __name__ == "__main__":
    main()
