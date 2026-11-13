#!/usr/bin/env python3
"""audiocheck.py -- THE MIXER'S RECEIPT. Audio cannot be seen in a PPM, so the engine writes
what it mixed (REVA_AUDIODUMP=<path>, raw s16 stereo 44.1k) and this reads it back.

    audiocheck.py                    boot the lab, fire three effects through the ctl, count the bursts
    audiocheck.py --pcm out.pcm      analyse an existing dump
    audiocheck.py --selftest         feed it a synthetic dump

What it proves: the device opened, the bank loaded, voices played, the mix reached the
callback -- and silence where nothing was asked for. A burst is a run of 20 ms windows whose
RMS clears the floor by 20 dB. The lab boots silent, so the FIRST burst is the ctl's.
"""
import argparse, os, struct, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
PY = sys.executable
RATE, WIN = 44100, 0.020


def bursts(pcm_path, floor_db=-60.0, rise_db=20.0):
    raw = open(pcm_path, "rb").read()
    n = len(raw) // 4
    if n == 0:
        return [], 0.0
    import array
    a = array.array("h"); a.frombytes(raw[: n * 4])
    win = int(RATE * WIN)
    rms = []
    for i in range(0, n - win, win):
        s = 0
        for k in range(i * 2, (i + win) * 2, 2):
            v = a[k]; s += v * v
        rms.append((s / win) ** 0.5 / 32768.0)
    import math
    db = [20 * math.log10(max(r, 1e-9)) for r in rms]
    quiet = sorted(db)[len(db) // 5] if db else -99      # the 20th percentile is the floor
    thr = max(floor_db, quiet + rise_db)
    out, cur = [], None
    for i, d in enumerate(db):
        t = i * WIN
        if d > thr and cur is None:
            cur = [t, t, d]
        elif d > thr:
            cur[1] = t; cur[2] = max(cur[2], d)
        elif cur is not None:
            out.append(tuple(cur)); cur = None
    if cur is not None:
        out.append(tuple(cur))
    return out, n / RATE


def selftest():
    import math, tempfile
    p = os.path.join(tempfile.gettempdir(), "audiocheck_self.pcm")
    with open(p, "wb") as f:
        for i in range(RATE * 3):
            t = i / RATE
            v = int(12000 * math.sin(2 * math.pi * 440 * t)) if 1.0 < t < 1.25 or 2.0 < t < 2.3 else 0
            f.write(struct.pack("<hh", v, v))
    b, dur = bursts(p)
    ok = len(b) == 2 and abs(b[0][0] - 1.0) < 0.05 and abs(b[1][0] - 2.0) < 0.05
    print("selftest: %d bursts %s -> %s" % (len(b), [(round(x, 2), round(y, 2)) for x, y, _ in b], "ok" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pcm"); ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--frame", type=int, default=440)
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    pcm = a.pcm
    if not pcm:
        pcm = os.path.join(os.environ.get("TEMP", "."), "audiocheck.pcm")
        if os.path.exists(pcm):
            os.remove(pcm)
        drv = os.path.join(ROOT, ".claude", "skills", "run-reva", "driver.py")
        png = os.path.join(os.environ.get("TEMP", "."), "audiocheck.png")
        cmd = [PY, drv, "lab", png, "--new", "--subject", "r2moves", "--frame", str(a.frame),
               "--env", "REVA_AUDIODUMP=" + pcm, "--env", "REVA_SFXDBG=1",
               "--ctl", "face=0 grid=0 sfx=shot,pickup,kill", "--grep", r"\[sfx\]"]
        r = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True, encoding="utf-8", errors="replace")
        print("\n".join(l for l in r.stdout.splitlines() if "[sfx]" in l))
    if not os.path.exists(pcm):
        print("FAIL: no dump at", pcm); return 2
    b, dur = bursts(pcm)
    print("dump %.2fs, %d burst(s): %s" % (dur, len(b), ", ".join("%.2f-%.2fs peak %.0f dB" % x for x in b)))
    ok = len(b) >= 1 and dur > 0.5 and b[0][0] > 0.05
    print("audiocheck:", "ok -- the mixer played into the device and the ctl's effects are in the dump" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
