"""stillcheck.py <mp4> -- how much of a TAKE is a frozen picture (10-23). Per shot (cuts found by frame difference): its
length, its motion (the mean absolute difference between frames, 0..255 grey at 192x108, the caption band left out) and
how many seconds of it did not change at all. A shot that holds still for seconds is dead air.
Not motioncheck.py, which asks whether a body's motion obeys physics; this asks whether anything moves.
The number that went UP: the film pass's second sample was 68% still in one scene and 43% in another (only mouths moved, no
lens moved); THE PERFORMANCE LAYER and push_holds took them to 9% and 18%.
⛔ It UNDERCOUNTS a sparse dark frame: a star field's slow zoom moves a few points a frame, under the threshold, while
the picture plainly changes (a scene of flares in a star field: "DEAD" here, a flare every few seconds on the frames). Look."""
import glob, subprocess, sys
import numpy as np

FF = __import__("shutil").which("ffmpeg") or sorted(glob.glob(__import__("os").path.expandvars(r"%LOCALAPPDATA%\Microsoft\WinGet\Packages\Gyan.FFmpeg*\ffmpeg*\bin\ffmpeg.exe")))[0]   # machine-independent paths
W, H = 192, 108


def frames(mp4):
    p = subprocess.run([FF, "-v", "error", "-i", mp4, "-vf", "scale=%d:%d,format=gray" % (W, H), "-f", "rawvideo", "-"],
                       capture_output=True, check=True)
    a = np.frombuffer(p.stdout, np.uint8)
    return a.reshape(-1, H, W).astype(np.float32)


def main(mp4, fps=24.0):
    F = frames(mp4)
    # the caption box (bottom 22%) changes with the words, not the picture: leave it out
    F = F[:, : int(H * 0.78), :]
    d = np.abs(np.diff(F, axis=0)).mean(axis=(1, 2))
    cut = d > max(18.0, np.median(d) * 8)
    shots, s0 = [], 0
    for i, c in enumerate(cut):
        if c:
            shots.append((s0, i + 1)); s0 = i + 1
    shots.append((s0, len(F)))
    still = 0.0
    print("%s: %d frames, %.1f s, %d shots" % (mp4.split("/")[-1], len(F), len(F) / fps, len(shots)))
    for a, b in shots:
        if b - a < 2: continue
        m = d[a:b - 1]
        mot = float(m.mean())
        # still frames: under 0.15 grey levels of change -- nothing in the picture is moving
        st = float((m < 0.15).sum()) / fps
        still += st
        flag = "  DEAD" if mot < 0.15 and (b - a) / fps > 3 else ("  slow" if mot < 0.35 and (b - a) / fps > 4 else "")
        print("  %6.1f-%6.1f s  %5.1f s  motion %.2f  still %4.1f s%s" % (a / fps, b / fps, (b - a) / fps, mot, st, flag))
    print("  STILL: %.1f s of %.1f (%.0f%%)" % (still, len(F) / fps, 100 * still / (len(F) / fps)))


if __name__ == "__main__":
    main(sys.argv[1])
