#!/usr/bin/env python3
"""say_espeak.py -- THE SCRATCH TRACK: speak a dialogue file with espeak-ng, one wav per line.
    say_espeak.py one_take.txt [--out wav/one_take]

⛔ THIS IS NOT THE SHOW'S VOICE. Kokoro is (jacob: "swithc to kokoro"), and say_kokoro.py stays
the tool. This exists for a machine that cannot reach Kokoro at all -- 2026-09-30, a Linux
container whose proxy answers nothing from the model host and whose pip cannot build the
package -- and it says so in the log every time it runs. A scratch track keeps the pipeline
whole (the compile, the mouth envelopes, the mix, the subtitles, the sfx timings all need SOME
wav per line) so an episode can be blocked, shot and timed; the voices are then re-spoken with
Kokoro on a machine that has it and the .scn recompiled (say_kokoro.py's own warning: every
line changes length, so the staging must be recompiled after re-speaking).

SAME CONTRACT AS say_kokoro.py: `cast N=name` names who is in the scene, slots are positional,
one wav per line into wav/<scene>/line_NN.wav (22050 Hz mono, which compile_scene.py resamples),
action lines (empty text) are counted and not voiced, `voice=` and `rate=` per line are honoured.
"""
import argparse, os, sys, subprocess, shutil
HERE = os.path.dirname(os.path.abspath(__file__))
# THE PEOPLE, keyed by name: an espeak voice, words per minute, pitch 0..99.
PEOPLE = {
    "clanker": dict(voice="en-us+m7", wpm=135, pitch=12, note="the studio's robot: low, even, a machine that has always sounded like this"),
    "x3d":     dict(voice="en-us+f3", wpm=175, pitch=60, note="the skater girl: quick, dry"),
    "reva":    dict(voice="en-gb+f2", wpm=150, pitch=50, note="scratch for bf_alice"),
    "kaeli":   dict(voice="en-us+f4", wpm=170, pitch=55, note="scratch for af_heart"),
    "holly":   dict(voice="en-gb+m3", wpm=150, pitch=40, note="scratch for the head on the monitor"),
    "director":dict(voice="en-gb+m3", wpm=150, pitch=40, note="the off-screen voice"),
}
CAST = {}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("script")
    ap.add_argument("--out", default=None)
    a = ap.parse_args()
    if not shutil.which("espeak-ng"):
        print("no espeak-ng on PATH (apt-get install espeak-ng)"); return 1
    print("[say] ⛔ SCRATCH TRACK (espeak-ng): not the show's voice. Re-speak with say_kokoro.py where Kokoro exists, then recompile.")
    src = a.script if os.path.isabs(a.script) else os.path.join(HERE, a.script)
    for raw in open(src, encoding="utf-8"):
        t = raw.strip()
        if not t.startswith("cast "): continue
        for b in t.split()[1:]:
            if "=" in b:
                slot, who = b.split("=", 1); who = who.lower()
                assert who in PEOPLE, "unknown character %r (PEOPLE has %s)" % (who, sorted(PEOPLE))
                CAST[slot] = who
        break
    for slot, who in sorted(CAST.items()):
        p = PEOPLE[who]; print("voice %s = %-9s %-9s %3d wpm pitch %2d  %s" % (slot, who, p["voice"], p["wpm"], p["pitch"], p["note"]))
    outdir = os.path.join(HERE, a.out or os.path.join("wav", os.path.splitext(os.path.basename(src))[0]))
    os.makedirs(outdir, exist_ok=True)
    for f in os.listdir(outdir):
        if f.startswith("line_") and f.endswith(".wav"): os.remove(os.path.join(outdir, f))
    i = 0
    for raw in open(src, encoding="utf-8"):
        s = raw.strip()
        if not s or s.startswith("#") or s.startswith("cast ") or "|" not in s: continue
        head, text = s.split("|", 1); bits = head.split(); spk = bits[0].strip(); text = text.strip()
        who = CAST.get(spk, "clanker")
        if not text:
            print("  %2d  %-8s (action line: no voice)" % (i, who)); i += 1; continue
        p = dict(PEOPLE[who]); wpm = p["wpm"]
        for b in bits[1:]:
            if b.startswith("voice=") and b[6:].lower() in PEOPLE: p = dict(PEOPLE[b[6:].lower()]); wpm = p["wpm"]
            if b.startswith("rate="):
                try: wpm = int(p["wpm"] / max(0.35, float(b[5:])))
                except ValueError: pass
        out = os.path.join(outdir, "line_%02d.wav" % i)
        rc = subprocess.run(["espeak-ng", "-v", p["voice"], "-s", str(wpm), "-p", str(p["pitch"]), "-w", out, text],
                            capture_output=True, text=True)
        if rc.returncode != 0 or not os.path.exists(out):
            print("  %2d  %-8s FAILED: %s" % (i, who, rc.stderr.strip()[-200:])); return 1
        print("  %2d  %-8s %s" % (i, who, text[:70])); i += 1
    print("%d line(s) -> %s" % (i, outdir))
    return 0

if __name__ == "__main__":
    sys.exit(main())
