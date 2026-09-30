#!/usr/bin/env python3
"""linux_scene.py -- record a SCENE with its audio on the Linux build, headless. 2026-09-30.

    linux_scene.py OUT.mp4 --name <scene> --subject <hero pack> --partner <pack> [--partner2 P]
                   [--set NAME] [--seconds N] [--fps 8|24|60] [--res WxH] [--cam ...] [--env K=V ...]

The Linux twin of `driver.py scene`, and the same contract: REVA_SCENEFPS makes the scene clock
fixed-step and starts the capture on the scene's own first frame, REVA_FIXEDSTEP goes with it
(driver.py's own warning: the clips would otherwise run at capture speed), the frames land as
PPMs and ffmpeg muxes them against scenes/<name>.wav. The engine exits itself when the burst is
met; a scene that ends early stops writing frames, so the harness watches the frame count and
wraps when it stops growing.

Packs are found where the engine's cast loader looks: reva_native/packs/v1_<name>.rpack (the
subject and partner are given as NAMES, like the Windows driver). The log lands beside the mp4.
"""
import os, sys, time, glob, argparse, subprocess, shutil
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
NATIVE = os.path.join(ROOT, "reva_native")
EXE = os.environ.get("REVA_LINUX_EXE") or os.path.join(NATIVE, "crowd_linux")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--name", required=True)
    ap.add_argument("--subject", required=True)
    ap.add_argument("--partner", default="off")
    ap.add_argument("--partner2", default="")
    ap.add_argument("--partner3", default="")
    ap.add_argument("--set", default="")
    ap.add_argument("--seconds", type=float, default=30)
    ap.add_argument("--fps", type=int, default=8)
    ap.add_argument("--res", default="1280x720")
    ap.add_argument("--cam", default="")
    ap.add_argument("--sun", default="")
    ap.add_argument("--sky", default="")
    ap.add_argument("--lamps", default=None)
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--keep", action="store_true", help="keep the PPM frames")
    a = ap.parse_args()
    if not shutil.which("ffmpeg"): print("no ffmpeg on PATH"); return 1
    if not os.path.exists(EXE): print("no %s -- run reva_native/build_linux.sh" % EXE); return 1
    scn = os.path.join(NATIVE, "scenes", a.name + ".scn")
    if not os.path.exists(scn): print("no %s -- compile_scene.py first" % scn); return 1
    out = os.path.abspath(a.out); os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    tmp = os.path.splitext(out)[0] + "_frames"; os.makedirs(tmp, exist_ok=True)
    for f in glob.glob(os.path.join(tmp, "*.ppm")): os.remove(f)
    frames = int(a.seconds * a.fps)
    ctl = "seq=%d face=0 grid=0 dqs=0 partner=%s jaw=-1 look=1 scene=%s" % (int(time.time()) % 100000, a.partner, a.name)
    if a.partner2: ctl += " partner2=%s" % a.partner2
    if a.partner3: ctl += " partner3=%s" % a.partner3
    if a.set:
        ctl += " set=%s" % a.set
        if a.sun: ctl += " sun=%s" % a.sun
        if a.sky: ctl += " sky=%s" % a.sky
        if a.lamps is not None: ctl += " lamps=%s" % a.lamps
    if a.cam: ctl += " cam=%s" % a.cam
    ctlp = os.path.join(tmp, "lab_ctl.txt"); open(ctlp, "w").write(ctl + "\n")
    print("ctl: " + ctl)
    env = dict(os.environ, REVA_WINDOWED="1", REVA_LABCTL=ctlp, REVA_AUTOMODE="11",
               REVA_ANIMSUBJ=a.subject, REVA_ANIMSTATE="0",
               REVA_SCENEFPS=str(a.fps), REVA_FIXEDSTEP=str(a.fps),
               REVA_SHOTN=str(frames), REVA_SHOT_PATH=os.path.join(tmp, "f.ppm"))
    for kv in a.env:
        k, _, v = kv.partition("="); env[k] = v
    logp = os.path.splitext(out)[0] + ".log"; logf = open(logp, "w")
    print("capturing %d frames at %d fps, %s ..." % (frames, a.fps, a.res))
    pr = subprocess.Popen(["xvfb-run", "-a", "-s", "-screen 0 %sx24" % a.res, "stdbuf", "-o0", "-e0",
                           EXE, "400", "packs", a.subject, a.res], cwd=NATIVE, env=env, stdout=logf, stderr=subprocess.STDOUT)
    have, last, t0, STALL = 0, time.time(), time.time(), 10.0
    while True:
        if pr.poll() is not None: break
        n = len(glob.glob(os.path.join(tmp, "*.ppm"))); now = time.time()
        if n != have: have, last = n, now
        if n >= frames: break
        if have > 0 and now - last > STALL: print("   frames stopped at %d/%d (the scene ended) -- wrapping" % (n, frames)); break
        if now - t0 > 3600: print("   an hour; stopping"); break
        time.sleep(1.0)
    pr.terminate()
    try: pr.wait(5)
    except subprocess.TimeoutExpired: pr.kill()
    logf.close()
    n = len(glob.glob(os.path.join(tmp, "*.ppm")))
    print("%d frame(s) in %.0f s" % (n, time.time() - t0))
    if n < 2: print("too few frames -- read %s" % logp); return 1
    wav = os.path.join(NATIVE, "scenes", a.name + ".wav")
    cmd = ["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(a.fps), "-start_number", "0",
           "-i", os.path.join(tmp, "f_%05d.ppm")]
    if os.path.exists(wav): cmd += ["-i", wav]
    cmd += ["-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "19", "-preset", "veryfast"]
    if os.path.exists(wav): cmd += ["-c:a", "aac", "-b:a", "192k", "-shortest"]
    cmd += [out]
    rc = subprocess.run(cmd, capture_output=True, text=True)
    if rc.returncode != 0: print(rc.stderr[-2000:]); return 1
    if not a.keep:
        for f in glob.glob(os.path.join(tmp, "*.ppm")): os.remove(f)
    print("wrote %s (%s, %d frames, %s)" % (out, a.res, n, "with audio" if os.path.exists(wav) else "silent"))
    for key in ("NOT FOUND", "REFUSED", "⛔", "[headroom]", "[hold]", "[walk]", "[path]", "[trick]"):
        hits = [l.rstrip() for l in open(logp, errors="replace") if key in l]
        if hits: print("  log %-11s x%d  e.g. %s" % (key, len(hits), hits[0][:110]))
    return 0

if __name__ == "__main__":
    sys.exit(main())
