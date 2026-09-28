#!/usr/bin/env python3
"""say_piper.py -- speak a dialogue file with PIPER (local neural TTS), one wav per line.

    say_piper.py club_night.txt [--out wav]

jacob: "make the voices much better, dont we have piper or something very good" and
"reva has a scottish accent and kaeli speaks with an american accent like new york".

SAPI (the first pass) is a 2005-era formant synth: intelligible, robotic, and it made
two characters sound like one machine reading a list. Piper is a neural vocoder running
locally on onnxruntime -- no network at synthesis time, no per-line cost, and each voice
is a different SPEAKER rather than the same speaker with a pitch knob.

  reva  -> en_GB-alba     a Scottish voice, and the only Scottish one Piper ships.
                          Slightly slowed: R.E.V.A. chooses her words.
  kaeli -> en_US-amy      American and conversational; the closest Piper gets to the
                          streetwise read. Slightly quickened, less deliberate.

length_scale is the speed knob (bigger = slower); noise_w_scale is how much the
phoneme durations vary, which is most of what makes a read sound human rather than
metered. Voices live in scenes/voices/ (downloaded models, gitignored).
"""
import sys, os, wave, argparse
from piper import PiperVoice, SynthesisConfig

HERE = os.path.dirname(os.path.abspath(__file__))
VOICES = os.path.join(HERE, "voices")

# THE PEOPLE, keyed by NAME. Which SLOT a person occupies in a given scene is a staging
# question -- the hero is whoever the lab loaded -- so the voice cannot be keyed on it.
PEOPLE = {
    "reva":  dict(model="en_GB-alba-medium",   length_scale=1.06, noise_scale=0.667,
                  noise_w_scale=0.85, note="Scottish, measured; she chooses her words"),
    "kaeli": dict(model="en_US-ljspeech-high", length_scale=0.94, noise_scale=0.720,
                  noise_w_scale=1.05, note="American, quick, loose cadence"),
    "susan": dict(model="en_US-kristin-medium", length_scale=1.00, noise_scale=0.700,
                  noise_w_scale=1.15, note="the neighbour: warm, chatty, deflects"),
}
# the default cast is what every script written before this line meant
CAST = {"0": dict(PEOPLE["reva"]), "1": dict(PEOPLE["kaeli"])}
NAMES = {"0": "reva", "1": "kaeli"}

# PER-LINE DIRECTION. A voice with one setting for a whole scene reads like a voice
# with one setting for a whole scene. The dialogue file may carry direction after the
# speaker -- `1 rate=0.88 var=1.2 pitch=0.75|Show off.` -- and it is exactly what a
# director gives an actor: rate = pace (smaller is faster), var = how much the phoneme
# DURATIONS wander (the gait: low is metronomic, high is loose and conversational),
# pitch = how far the intonation swings.
DIRECTION = {"rate": "length_scale", "var": "noise_w_scale", "pitch": "noise_scale"}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("script", nargs="?", default="club_night.txt")
    ap.add_argument("--out", default=None, help="default: wav/<scene>")
    ap.add_argument("--voice0"); ap.add_argument("--voice1")
    a = ap.parse_args()
    if a.voice0: CAST["0"]["model"] = a.voice0
    if a.voice1: CAST["1"]["model"] = a.voice1
    src = a.script if os.path.isabs(a.script) else os.path.join(HERE, a.script)
    # ⭐ THE SCRIPT NAMES ITS OWN CAST: `cast 0=kaeli 1=susan`. Without it, 0=reva 1=kaeli.
    for raw in open(src, encoding="utf-8"):
        t = raw.strip()
        if not t.startswith("cast "):
            continue
        for b in t.split()[1:]:
            if "=" not in b:
                continue
            slot, who = b.split("=", 1)
            who = who.lower()
            assert who in PEOPLE, "unknown character %r (have %s)" % (who, ", ".join(PEOPLE))
            CAST[slot] = dict(PEOPLE[who]); NAMES[slot] = who
        break
    # ONE DIRECTORY PER SCENE. A single shared wav/ means compiling scene B after
    # speaking scene A silently mixes A's leftover line_NN.wav files into B.
    outdir = os.path.join(HERE, a.out or os.path.join("wav", os.path.splitext(os.path.basename(src))[0]))
    os.makedirs(outdir, exist_ok=True)
    loaded = {}
    for spk, c in CAST.items():
        path = os.path.join(VOICES, c["model"] + ".onnx")
        assert os.path.exists(path), "missing voice %s (python -m piper.download_voices %s)" % (path, c["model"])
        loaded[spk] = PiperVoice.load(path)
        print("voice %s = %-22s  %s" % (spk, c["model"], c["note"]))
    i = 0
    for raw in open(src, encoding="utf-8"):
        s = raw.strip()
        if not s or s.startswith("#"): continue
        if s.startswith("cast "): continue      # read above, before the voices load
        if "|" not in s: continue               # a directive (mouth, sfx), not a line of dialogue
        head, text = s.split("|", 1)
        bits = head.split()
        spk = bits[0].strip(); text = text.strip()
        if not text:                            # ⭐ ep 10: an ACTION line has no voice, but keeps its number
            print("  %2d  (action line: no voice)" % i)
            i += 1
            continue
        c = dict(CAST[spk])
        for b in bits[1:]:
            if "=" in b:
                k, v = b.split("=", 1)
                if k in DIRECTION: c[DIRECTION[k]] = float(v)
        cfg = SynthesisConfig(length_scale=c["length_scale"], noise_scale=c["noise_scale"],
                              noise_w_scale=c["noise_w_scale"], normalize_audio=True)
        out = os.path.join(outdir, "line_%02d.wav" % i)
        with wave.open(out, "wb") as w:
            loaded[spk].synthesize_wav(text, w, syn_config=cfg)
        with wave.open(out, "rb") as w:
            dur = w.getnframes() / float(w.getframerate())
            sr = w.getframerate()
        print("  %2d  %-5s %5.2fs @%dHz  %s" % (i, NAMES.get(spk, spk), dur, sr, text[:46]))
        i += 1
    print("lines: %d" % i)

if __name__ == "__main__":
    main()
