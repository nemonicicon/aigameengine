"""clipstrip.py -- one body, its clips, columns of time, THROUGH THE HERO PATH.

jacob (09-01): "you need to take more screenshots and have a better idea what you are
doing. do you know kaeli's is in a crouch to standing animation as her base idle animation?"

He was right and the tools I had could not have shown me:
  * modelcheck.py shoots the body as a SET ACTOR with a static clip -- one frame, and
    (it turned out) not even the clip it names;
  * zombiecheck.py also stages an actor, and came back EMPTY for kmoves;
  * driver.py lab shoots ONE frame per launch.
None of them shows what a clip DOES on the body the way the lab hero plays it -- which is
the path a scene's hero, the partner and the ANIM LAB all use, i.e. what jacob sees.

    python anim_bench/clipstrip.py kmoves x_getup,idle --secs 6 --cols 6

launches the lab ONCE per clip on crowd_new.exe with the clip pinned by ctl `clip=`,
captures --cols frames spread over --secs of fixed-step time, and lays them out as a sheet
(rows = clips, columns = time, timestamps under each column). The lab's own HUD stays ON
deliberately: it prints the clip index, the state and the foot-skate number in the frame,
so the picture carries its own receipt. Output: anim_bench/_strips/<pack>.png.

⛔ REVA_SHOT arms the capture; SHOTN/SHOTEVERY only say how many and how often, and the
engine never exits on its own -- poll for the frames, then kill it.
⛔ A file that exists is not a file that is finished: the last PPM is still streaming when
the poll sees it. Wait for its size to settle before opening.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import time

NATIVE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = "crowd_new.exe"


def kill_running():
    for exe in ("crowd_new.exe", "crowd.exe"):
        subprocess.run(["taskkill", "/F", "/IM", exe],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def settled(path, tries=40):
    last = -1
    for _ in range(tries):
        try:
            sz = os.path.getsize(path)
        except OSError:
            sz = -1
        if sz > 0 and sz == last:
            return True
        last = sz
        time.sleep(0.15)
    return False


def shoot_clip(pack, clip, start, cols, every, fps, extra_ctl=""):
    """Launch the lab on <pack>, pin <clip>, capture `cols` frames from `start` every
    `every` frames. Returns (list of PIL images, engine stdout tail)."""
    from PIL import Image
    kill_running()
    ctl = ("seq=%d face=0 grid=0 dqs=0 clip=%s %s\n"
           % (int(time.time()) % 100000, clip, extra_ctl))
    open(os.path.join(NATIVE, "anim_bench", "lab_ctl.txt"), "w").write(ctl)
    tmp = os.path.join(NATIVE, "anim_bench", "_strip_tmp")
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp, exist_ok=True)
    env = dict(os.environ, REVA_WINDOWED="1", REVA_LABCTL="anim_bench/lab_ctl.txt",
               REVA_AUTOMODE="11", REVA_ANIMSUBJ=pack, REVA_ANIMSTATE="0",
               REVA_FIXEDSTEP=str(fps), REVA_CAMLOCK="1", REVA_NOSKY="1", REVA_NOSAVE="1",
               REVA_SHOT=str(start), REVA_SHOTN=str(cols), REVA_SHOTEVERY=str(every),
               REVA_SHOT_PATH=os.path.join(tmp, "f.ppm"))
    pr = subprocess.Popen([os.path.join(NATIVE, EXE), "400", "packs", pack],
                          cwd=NATIVE, env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                          errors="replace")
    # drain stdout on a thread so a chatty engine cannot block on a full pipe
    import threading
    log = []
    def drain():
        for ln in pr.stdout:
            log.append(ln.rstrip("\n"))
    th = threading.Thread(target=drain, daemon=True); th.start()
    deadline = time.time() + 20 + (start + cols * every) / max(1.0, fps) * 3
    got = []
    while time.time() < deadline:
        got = sorted(glob.glob(os.path.join(tmp, "*.ppm")))
        if len(got) >= cols and settled(got[-1]):
            break
        if pr.poll() is not None:
            break
        time.sleep(0.5)
    time.sleep(0.4)
    pr.kill()
    got = sorted(glob.glob(os.path.join(tmp, "*.ppm")))
    ims = []
    for f in got[:cols]:
        settled(f)
        try:
            ims.append(Image.open(f).convert("RGB").copy())
        except OSError:
            pass
    return ims, log


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pack", help="packs/v1_<pack>.rpack, played as the lab hero")
    ap.add_argument("clips", help="comma list of clip names (any bank; the ctl borrows)")
    ap.add_argument("--secs", type=float, default=6.0, help="span of time per row")
    ap.add_argument("--cols", type=int, default=6)
    ap.add_argument("--fps", type=int, default=30, help="fixed step")
    ap.add_argument("--start", type=int, default=330, help="first capture frame (boot ~250)")
    ap.add_argument("--tile", type=int, default=320, help="tile width")
    ap.add_argument("--ctl", default="", help="extra lab ctl, e.g. 'set=_model cam=...'")
    ap.add_argument("--grep", default="[idle]|[lab] clip|NOT FOUND",
                    help="engine stdout substrings to echo (| separated)")
    ap.add_argument("--out", default=None)
    a = ap.parse_args(argv)
    from PIL import Image, ImageDraw

    clips = [c for c in a.clips.split(",") if c]
    every = max(1, int(round(a.secs * a.fps / max(1, a.cols - 1)))) if a.cols > 1 else 1
    rows = []
    for c in clips:
        ims, log = shoot_clip(a.pack, c, a.start, a.cols, every, a.fps, a.ctl)
        keys = [k for k in a.grep.split("|") if k]
        for ln in log:
            if any(k in ln for k in keys):
                print("  | " + ln)
        print("  %-16s %d/%d frames" % (c, len(ims), a.cols))
        rows.append((c, ims))

    TW = a.tile
    TH = int(TW * 9 / 16)
    LAB = 90
    sheet = Image.new("RGB", (LAB + TW * a.cols, (TH + 18) * len(rows) + 18), (14, 16, 20))
    dr = ImageDraw.Draw(sheet)
    for j in range(a.cols):
        t = (a.start + j * every - a.start) / float(a.fps)
        dr.text((LAB + j * TW + 6, 3), "t=%.2fs  f%d" % (t, a.start + j * every), fill=(160, 170, 190))
    for i, (c, ims) in enumerate(rows):
        y = 18 + i * (TH + 18)
        col = (92, 232, 160) if len(ims) == a.cols else (240, 98, 93)
        dr.text((6, y + TH // 2 - 6), c, fill=col)
        for j, im in enumerate(ims):
            sheet.paste(im.resize((TW, TH)), (LAB + j * TW, y))
        if len(ims) < a.cols:
            dr.text((LAB + len(ims) * TW + 6, y + 6), "%d/%d frames" % (len(ims), a.cols), fill=col)
    out = a.out or os.path.join(NATIVE, "anim_bench", "_strips", "%s.png" % a.pack)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    sheet.save(out)
    print("wrote %s  (%d clips x %d frames)   <-- LOOK AT IT" % (out, len(rows), a.cols))
    return 0


if __name__ == "__main__":
    sys.exit(main())
