#!/usr/bin/env python3
"""broll.py -- capture a SILENT shot of the world. No dialogue, no subtitles, no pair.

    python skate_src/film/broll.py OUT.mp4 --set parsons --cam "..." --seconds 8

jacob: "there needs to be more scenic shots especially in the beginning. just dumps me in
to dialouge" and "this script is very disjointed."

Both are the same fault and it is mine: the shot list only ever contained scenes with
WORDS in them, so the film is seven conversations butt-spliced together with nothing
between them. The treatment opens on a sprinkler ticking, wet asphalt, long shadows and a
paper landing on a driveway -- forty seconds of a street being ordinary before anybody
speaks -- and none of it was ever shot, because there was no way to shoot a take that
nobody talks in.

⛔ THE SCENE RECORDER CANNOT DO THIS. Its capture is armed by `REVA_SCENEFPS && g_scnT>=0`
-- it only rolls while dialogue is playing. A silent shot arms the other way, on
`REVA_SHOT`'s frame, and encodes the PPM sequence with a silent audio track so the
concatenated film keeps one continuous stream layout.
"""
import argparse, glob, os, subprocess, sys, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
NATIVE = os.path.join(ROOT, "reva_native")
FFMPEG = next(iter(glob.glob(os.path.join(
    os.environ.get("LOCALAPPDATA", os.path.expanduser("~/AppData/Local")),
    "Microsoft", "WinGet", "Packages", "Gyan.FFmpeg*", "ffmpeg*", "bin", "ffmpeg.exe"))), None)


def kill():
    for exe in ("crowd.exe", "crowd_new.exe"):
        subprocess.run(["taskkill", "/IM", exe, "/F"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def shoot(out, setname, cam, seconds, fps=60, sun="", sky="",
          start=300, subject="r2moves", quiet=True, extra=""):
    """⭐ `extra` is any ctl the shot wants -- tod=, chains=, wms=, stage=, a different
    clip. A b-roll shot could already name its OWN set, and the set clock starts when the
    CAPTURE starts (g_capBegun), so `cue` and `path` in that set are already timed from
    frame one of the take. What it could not do was carry anything else."""
    frames = int(seconds * fps)
    tmp = os.path.abspath(os.path.join(os.path.dirname(out) or ".", "_broll"))
    os.makedirs(tmp, exist_ok=True)
    for f in glob.glob(os.path.join(tmp, "*.ppm")):
        os.remove(f)
    ctl = os.path.join(NATIVE, "anim_bench", "lab_ctl.txt")
    body = ("seq=%d face=0 grid=0 dqs=0 clip=idle partner=off set=%s cam=%s"
            % (int(time.time() * 7) % 100000, setname, cam))
    if sun:
        body += " sun=%s" % sun
    if sky:
        body += " sky=%s" % sky
    if extra:
        body += " " + extra
    open(ctl, "w").write(body + "\n")
    kill()
    env = dict(os.environ, REVA_WINDOWED="1", REVA_LABCTL="anim_bench/lab_ctl.txt",
               REVA_AUTOMODE="11", REVA_ANIMSUBJ=subject, REVA_ANIMSTATE="0",
               REVA_FIXEDSTEP=str(fps), REVA_SHOT=str(start), REVA_SHOTN=str(frames),
               REVA_SHOTEVERY="1",
               REVA_SHOT_PATH=os.path.join(tmp, "f.ppm").replace("/", "\\"))
    pr = subprocess.Popen([os.path.join(NATIVE, "crowd_new.exe"), "400", "packs", subject],
                          cwd=NATIVE, env=env,
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    have, last, t0 = 0, time.time(), time.time()
    while True:
        if pr.poll() is not None:
            break
        n = len(glob.glob(os.path.join(tmp, "*.ppm")))
        now = time.time()
        if n != have:
            have, last = n, now
        if n >= frames:
            break
        if have > 0 and now - last > 8:
            break
        if now - t0 > 1800:
            break
        time.sleep(0.8)
    time.sleep(0.6)
    kill()
    got = sorted(glob.glob(os.path.join(tmp, "*.ppm")))
    if len(got) < 4:
        print("   broll FAILED (%d frames)" % len(got))
        return None
    # silent audio so every clip in the film has the same stream layout for concat
    cmd = [FFMPEG, "-y", "-v", "error", "-framerate", str(fps), "-start_number", "0",
           "-i", os.path.join(tmp, "f_%05d.ppm"),
           "-f", "lavfi", "-i", "anullsrc=channel_layout=mono:sample_rate=22050",
           "-shortest", "-c:v", "libx264", "-crf", "18", "-preset", "veryfast",
           "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k", out]
    r = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    for f in got:
        os.remove(f)
    if r.returncode != 0:
        print(r.stderr[-800:])
        return None
    if not quiet:
        print("   broll: %d frames -> %.1f MB" % (len(got), os.path.getsize(out) / 1e6))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--set", dest="setname", required=True)
    ap.add_argument("--cam", required=True)
    ap.add_argument("--seconds", type=float, default=8.0)
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--sun", default="")
    ap.add_argument("--sky", default="")
    a = ap.parse_args()
    r = shoot(a.out, a.setname, a.cam, a.seconds, a.fps, a.sun, a.sky, quiet=False)
    return 0 if r else 1


if __name__ == "__main__":
    sys.exit(main())
