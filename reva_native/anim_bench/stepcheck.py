#!/usr/bin/env python3
"""stepcheck.py -- does STOP MOTION actually hold? (design/stop_motion.md)

    stepcheck.py [--exe crowd_new.exe] [--step 12] [--fps 24] [--n 12]

Two captures of the same street through the same PINNED lens, consecutive frames, a fixed
virtual clock: one with ctl `step=<step>`, one without. Every neighbouring pair is diffed.

  step ON   the bodies expose every fps/step frames and HOLD between -- so a held pair
            must come back IDENTICAL, and the pattern must repeat with period fps/step.
  step OFF  every pair moves. This is the CONTROL: without it a street with nothing
            walking would pass the ON test by standing still.

The lens is pinned on purpose. The stop-motion rule is that the camera stays SMOOTH, so a
moving lens would make every pair differ and hide whether the bodies held.
Writes a strip of the first frames next to the captures -- look at it.
"""
import argparse, glob, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.path.dirname(HERE)


def kill():
    for im in ("crowd_new.exe", "crowd.exe"):
        subprocess.run(["taskkill", "/F", "/IM", im], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def capture(a, tag, step):
    out = os.path.join(a.out, tag)
    os.makedirs(out, exist_ok=True)
    for f in glob.glob(os.path.join(out, "*.ppm")): os.remove(f)
    ctl = ("seq=%d face=0 grid=0 dqs=0 set=%s step=%s cam=%s sun=%s sky=%s\n"
           % (int(time.time() * 1000) % 1000000000, a.set, step, a.cam, a.sun, a.sky))
    open(os.path.join(NATIVE, "anim_bench", "lab_ctl.txt"), "w").write(ctl)
    env = dict(os.environ, REVA_WINDOWED="1", REVA_LABCTL="anim_bench/lab_ctl.txt",
               REVA_AUTOMODE="11", REVA_ANIMSUBJ="r2moves", REVA_ANIMSTATE="0",
               REVA_FIXEDSTEP=str(a.fps), REVA_SHOT=str(a.frame), REVA_SHOTN=str(a.n),
               REVA_SHOTEVERY="1", REVA_NOSKY="1",
               REVA_SHOT_PATH=os.path.join(out, "s.ppm"))
    kill()
    log = open(os.path.join(out, "log.txt"), "w")
    pr = subprocess.Popen([os.path.join(NATIVE, a.exe), "400", "packs", "r2moves"],
                          cwd=NATIVE, env=env, stdout=log, stderr=subprocess.STDOUT)
    t0 = time.time()
    # ⛔ THE ENGINE NEVER EXITS AT ITS QUOTA. Poll the frames and kill it.
    while time.time() - t0 < a.timeout:
        if pr.poll() is not None: break
        if len(glob.glob(os.path.join(out, "s_*.ppm"))) >= a.n: break
        time.sleep(1.0)
    time.sleep(1.0)
    kill(); log.close()
    fs = sorted(glob.glob(os.path.join(out, "s_*.ppm")))
    print("[%s] step=%s  %d frames  (log: %s)" % (tag, step, len(fs), os.path.join(out, "log.txt")))
    for ln in open(os.path.join(out, "log.txt"), errors="replace"):
        if ln.startswith("[step]") or ln.startswith("[set]"): print("   " + ln.rstrip())
    return fs


def diffs(fs):
    from PIL import Image, ImageChops
    ims = [Image.open(f).convert("RGB") for f in fs]
    tot = ims[0].size[0] * ims[0].size[1]
    out = []
    for i in range(len(ims) - 1):
        d = ImageChops.difference(ims[i], ims[i + 1]).convert("L").point(lambda v: 255 if v > 3 else 0)
        out.append(100.0 * d.histogram()[255] / tot)
    return ims, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default="crowd_new.exe")
    ap.add_argument("--step", default="12")
    ap.add_argument("--fps", type=int, default=24)
    ap.add_argument("--n", type=int, default=12)
    ap.add_argument("--frame", type=int, default=520)
    ap.add_argument("--set", default="parsons")
    # across the road from the porch mark: the walking neighbours and mrs parsons in frame
    ap.add_argument("--cam", default="30.0,3.2,-8.0,42.0,1.4,12.0,40")
    ap.add_argument("--sun", default="66,178,1.38,1.00,0.98,0.94")
    ap.add_argument("--sky", default="0.460,0.520,0.640")
    ap.add_argument("--out", default="out/_stepcheck")
    ap.add_argument("--timeout", type=float, default=240.0)
    a = ap.parse_args()
    period = max(1, int(round(a.fps / float(a.step))))

    on = capture(a, "on", a.step)
    off = capture(a, "off", "0")
    if len(on) < 4 or len(off) < 4:
        print("FAIL: too few frames (on %d, off %d) -- read the logs" % (len(on), len(off))); return 1
    ims_on, d_on = diffs(on)
    ims_off, d_off = diffs(off)
    print("\npair   step=%-4s  step=0    (%% of pixels changed by more than 3/255)" % a.step)
    for i in range(max(len(d_on), len(d_off))):
        print("%2d-%-2d  %7.3f   %7.3f" % (i, i + 1, d_on[i] if i < len(d_on) else -1,
                                          d_off[i] if i < len(d_off) else -1))
    HELD, MOVED = 0.002, 0.02
    held = [v for v in d_on if v <= HELD]
    moved = [v for v in d_on if v > MOVED]
    # the held pairs must repeat with the period: in any `period` consecutive pairs,
    # exactly one is an exposure
    ok_pattern = all(sum(1 for v in d_on[k:k + period] if v > HELD) == 1
                     for k in range(0, len(d_on) - period + 1))
    ok_control = all(v > MOVED for v in d_off)
    ok = ok_pattern and ok_control and len(held) > 0 and len(moved) > 0
    print("\nheld pairs %d, exposed pairs %d, period %d -> pattern %s; control (all pairs move) %s"
          % (len(held), len(moved), period, "OK" if ok_pattern else "BROKEN", "OK" if ok_control else "BROKEN"))
    # a strip to LOOK at: the first frames of each run, half size
    from PIL import Image
    k = min(6, len(ims_on), len(ims_off))
    w, h = ims_on[0].size[0] // 2, ims_on[0].size[1] // 2
    sheet = Image.new("RGB", (w * k, h * 2))
    for i in range(k):
        sheet.paste(ims_on[i].resize((w, h)), (w * i, 0))
        sheet.paste(ims_off[i].resize((w, h)), (w * i, h))
    sp = os.path.join(a.out, "strip.png"); sheet.save(sp)
    print("strip (top step=%s, bottom step=0): %s   <-- LOOK AT IT" % (a.step, sp))
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
