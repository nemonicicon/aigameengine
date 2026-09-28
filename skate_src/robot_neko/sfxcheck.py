"""sfxcheck.py MP4 t1,t2,... -- is a SHORT effect (a pop, a slam) in the delivered mix? verify.py --tones looks
for a sustained pitch and a 0.22 s broadband crack never registers as one. This reads the RMS envelope: the
loudest 40 ms window in the 0.35 s after each time, against the median 40 ms window in the 0.8 s before it.
PRESENT when the peak is 6 dB up on the bed."""
import os, subprocess, sys, tempfile, wave
import numpy as np

mp4 = sys.argv[1]
times = [float(x) for x in sys.argv[2].split(",")]
d = tempfile.mkdtemp()
w = os.path.join(d, "a.wav")
subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", mp4, "-vn", "-ac", "1", "-ar", "24000", w], check=True)
with wave.open(w, "rb") as f:
    sr = f.getframerate()
    x = np.frombuffer(f.readframes(f.getnframes()), dtype=np.int16).astype(np.float64) / 32768.0
win = int(sr * 0.040)


def rms_windows(a, b):
    seg = x[int(a * sr):int(b * sr)]
    n = len(seg) // win
    if n < 1:
        return np.array([1e-9])
    seg = seg[:n * win].reshape(n, win)
    return np.sqrt((seg ** 2).mean(axis=1)) + 1e-9


ok = True
for t in times:
    bed = np.median(rms_windows(t - 0.8, t - 0.02))
    peak = rms_windows(t - 0.02, t + 0.35).max()
    db = 20 * np.log10(peak / bed)
    verdict = "PRESENT" if db >= 6.0 else "NOT HEARD"
    ok &= db >= 6.0
    print("sfx %6.2fs: peak %+5.1f dB over the bed before it -> %s" % (t, db, verdict))
sys.exit(0 if ok else 1)
