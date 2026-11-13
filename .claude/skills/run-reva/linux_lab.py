#!/usr/bin/env python3
"""linux_lab.py -- the ANIM LAB on the Linux build, headless: one body, the lab's four
cameras, one frame per angle. The first Linux instrument (2026-09-30); driver.py is still
the Windows driver (taskkill, crowd.exe, Windows shot paths).

    linux_lab.py <pack.rpack> <clip> <outdir> [cams=0,1,2,3] [WxH]

Boots reva_native/crowd_linux under Xvfb (build it with reva_native/build_linux.sh), with the
subject as the hero (argv[3] = the pack, so the select screens never run), REVA_FIXEDSTEP=30
so frames are comparable, and walks cam=0..3 through the lab ctl file, waiting for each
shot= to land. PPMs are converted to PNG beside them. LOOK AT THEM.

⛔ The first frame is the boot finishing: the lab has not switched clips yet, so cam 0 shows
   the idle (clip -1). Ask for cams=0,1,2,3,0 if you need the clip from all four.
⛔ The packdir the engine loads is the directory the pack is in: keep the moves pack and any
   partner there, nothing else you don't want as a crowd.
"""
import os, sys, time, subprocess, glob
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
NATIVE = os.path.join(ROOT, "reva_native")
EXE = os.environ.get("REVA_LINUX_EXE") or os.path.join(NATIVE, "crowd_linux")   # an A/B build goes here

def main():
    if len(sys.argv) < 4:
        print(__doc__); return 2
    pack, clip, outdir = os.path.abspath(sys.argv[1]), sys.argv[2], os.path.abspath(sys.argv[3])
    cams = [int(c) for c in (sys.argv[4] if len(sys.argv) > 4 else "0,1,2,3").split(",")]
    res = sys.argv[5] if len(sys.argv) > 5 else "1280x720"
    if not os.path.exists(EXE):
        print("no %s -- run reva_native/build_linux.sh first" % EXE); return 1
    os.makedirs(outdir, exist_ok=True)
    ctl = os.path.join(outdir, "lab_ctl.txt")
    log = open(os.path.join(outdir, "engine.log"), "w")
    env = dict(os.environ, REVA_WINDOWED="1", REVA_FIXEDSTEP="30", REVA_AUTOMODE="11",
               REVA_ANIMSUBJ=pack, REVA_ANIMSTATE="0", REVA_LABCTL=ctl)
    seq = [int(time.time()) % 100000]
    def write_ctl(cam, shot):
        seq[0] += 1
        open(ctl, "w").write("seq=%d face=0 grid=0 dqs=0 clip=%s partner=off cam=%d shot=%s\n"
                             % (seq[0], clip, cam, shot))
    shots = [os.path.join(outdir, "cam%d_%d.ppm" % (c, i)) for i, c in enumerate(cams)]
    write_ctl(cams[0], shots[0])
    # ⛔ ITS OWN SESSION, SO THE KILL REACHES THE ENGINE. xvfb-run is a shell script; terminating it
    # left crowd_linux and Xvfb alive (three of each after a morning's labs, 10-05) -- and anything
    # that waits for "no engine running" waited for ever. killpg at the end takes the whole group.
    pr = subprocess.Popen(["xvfb-run", "-a", "-s", "-screen 0 %sx24" % res, "stdbuf", "-o0", "-e0",
                           EXE, "400", os.path.dirname(pack), pack, res],
                          cwd=NATIVE, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    def wait(path, secs):
        t0 = time.time()
        while time.time() - t0 < secs:
            if os.path.exists(path) and os.path.getsize(path) > 1000:
                time.sleep(0.5); return True
            if pr.poll() is not None: return False
            time.sleep(0.5)
        return False
    ok = wait(shots[0], 300)
    print("boot -> first frame: %s" % ("ok" if ok else "NO FRAME (see engine.log)"))
    if ok:
        for cam, p in zip(cams[1:], shots[1:]):
            write_ctl(cam, p)
            print("cam %d: %s" % (cam, "ok" if wait(p, 60) else "NO FRAME"))
    import signal
    try:
        os.killpg(pr.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    pr.terminate()
    try: pr.wait(5)
    except subprocess.TimeoutExpired: pr.kill()
    try:
        from PIL import Image
    except ImportError:
        print("PPMs written; pip install Pillow to get PNGs"); return 0 if ok else 1
    for f in sorted(glob.glob(os.path.join(outdir, "cam*.ppm"))):
        im = Image.open(f); im.save(f[:-4] + ".png"); print(f[:-4] + ".png", im.size, im.getextrema())
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
