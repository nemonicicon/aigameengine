#!/usr/bin/env python3
"""verify.py MP4 -- the checks an episode must pass ON THE DELIVERED FILE, in one command.

    python skate_src/robot_neko/verify.py ep.mp4 --steps 6,30 --clip 1.3,20,58 \
        --frames 57.3,58.2,59.0 --tones 23.15,53.6 --hz 880,1318

The ledger's rule (ep 2): "done kept being declared from the wrong evidence -- a log line, a sheet
at 8fps, a whole-frame percentage". Ep 3 had to run each check by hand -- mp4steps, a clipped count,
a hand-built frame montage, an ear on the chime -- so they are one tool now.

  --steps   mp4steps.py at each time (twos read EhEhEh; ones move on every pair)
  --clip    % of pixels at the white point on FULL frames (a `tone=` scene should read ~0)
  --frames  full frames tiled into <mp4>_verify.png, two across -- LOOK AT IT
  --tones   at each time, is the effect in the MIX: the share of spectrum in +/-3% of each --hz,
            over 0.45 s, against the same window 0.6 s earlier. PRESENT or NOT HEARD.
"""
import argparse, os, subprocess, sys, wave
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", ".claude", "skills", "run-reva")))
import driver


def frame(mp4, t):
    p = os.path.splitext(mp4)[0] + "_v%.2f.png" % t
    subprocess.run([driver.FFMPEG, "-y", "-loglevel", "error", "-ss", "%.3f" % t, "-i", mp4,
                    "-frames:v", "1", p])
    if not os.path.exists(p):
        return None
    im = Image.open(p).convert("RGB"); im.load(); os.remove(p)
    return im


def floats(s):
    return [float(x) for x in s.split(",") if x.strip()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mp4")
    ap.add_argument("--steps", default="")
    ap.add_argument("--clip", default="")
    ap.add_argument("--frames", default="")
    ap.add_argument("--tones", default="")
    ap.add_argument("--hz", default="880,1318")
    a = ap.parse_args()
    bad = 0

    if a.steps:
        r = subprocess.run([sys.executable, os.path.join(HERE, "mp4steps.py"), a.mp4, a.steps])
        bad += r.returncode != 0

    for t in floats(a.clip):
        im = frame(a.mp4, t)
        if im is None:
            print("clip  %6.2fs: NO FRAME" % t); bad += 1; continue
        px = np.asarray(im)
        print("clip  %6.2fs: %5.2f%% of pixels at 254+ in any channel, %5.2f%% in all three"
              % (t, 100.0 * (px.max(axis=2) >= 254).mean(), 100.0 * (px.min(axis=2) >= 254).mean()))

    if a.frames:
        ts = floats(a.frames); W, H = 640, 360
        sheet = Image.new("RGB", (W * 2, H * ((len(ts) + 1) // 2)))
        for i, t in enumerate(ts):
            im = frame(a.mp4, t)
            if im is None:
                print("frame %6.2fs: NO FRAME" % t); bad += 1; continue
            im = im.resize((W, H), Image.LANCZOS)
            ImageDraw.Draw(im).text((8, 6), "%.2fs" % t, fill=(255, 255, 0))
            sheet.paste(im, ((i % 2) * W, (i // 2) * H))
        out = os.path.splitext(a.mp4)[0] + "_verify.png"
        sheet.save(out)
        print("wrote %s  <-- LOOK AT IT" % out)

    if a.tones:
        wavp = os.path.splitext(a.mp4)[0] + "_verify.wav"
        subprocess.run([driver.FFMPEG, "-y", "-loglevel", "error", "-i", a.mp4, "-vn", "-ac", "1",
                        "-ar", "24000", wavp])
        if not os.path.exists(wavp):
            print("tones: the mp4 has NO AUDIO TRACK"); return 1
        w = wave.open(wavp); sr = w.getframerate()
        x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float32) / 32768.0
        w.close(); os.remove(wavp)
        hz = floats(a.hz)

        def share(t0, dur=0.45):
            seg = x[int(t0 * sr):int((t0 + dur) * sr)]
            if len(seg) < 256:
                return 0.0
            sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
            f = np.fft.rfftfreq(len(seg), 1.0 / sr)
            return float(sum(sp[(f > h * 0.97) & (f < h * 1.03)].sum() for h in hz) / (sp.sum() + 1e-9))

        for t in floats(a.tones):
            on, before = share(t), share(max(0.0, t - 0.6))
            ok = on > 0.02 and on > 2.5 * before
            bad += not ok
            print("tone  %6.2fs: %.3f of the spectrum at %s Hz (%.3f just before) -> %s"
                  % (t, on, "/".join("%g" % h for h in hz), before, "PRESENT" if ok else "NOT HEARD"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
