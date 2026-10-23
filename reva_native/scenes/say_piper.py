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
    # THE WORLD AT THE END OF TIME, scene 2.1 -- a TEMPORARY cast (jacob, 10-18: "a rough cut of scene 2.1 on
    # temporary voices", then "i like piper"). The only installed Piper model with men in it is LibriTTS (904
    # speakers, one `speaker` each); I cannot hear them, so the four were picked by MEASURED pitch and pace off one
    # line said by 113 of them (D:/worldattheendoftime/production/voices/audition/spk_NNN.wav): men read 99-152 Hz,
    # women 170-255. Recast by changing a number.
    "narrator": dict(model="en_US-libritts_r-medium", speaker=248, length_scale=1.02, noise_scale=0.620,
                  noise_w_scale=0.80, note="the narrator (V.O.): a low man (106 Hz), unhurried"),
    "viktor": dict(model="en_US-libritts_r-medium", speaker=464, length_scale=0.98, noise_scale=0.700,
                  noise_w_scale=0.95, note="a boy of twelve, voiced light (214 Hz) as a cartoon boy is"),
    "wanda": dict(model="en_US-libritts_r-medium", speaker=352, length_scale=1.05, noise_scale=0.600,
                  noise_w_scale=0.75, note="a hundred and clear-eyed: the slowest reader of the sample"),
    "amelia": dict(model="en_US-libritts_r-medium", speaker=864, length_scale=0.95, noise_scale=0.700,
                  noise_w_scale=1.00, note="his mother, hurrying in: quick (220 Hz)"),
    # chapter 2 (10-22), the same audition: men 99-152 Hz, women 170-255; nobody already cast is reused
    "pal": dict(model="en_US-libritts_r-medium", speaker=272, length_scale=0.98, noise_scale=0.680,
                  noise_w_scale=0.95, note="Pal, cheery, smiles often: a middle man (122 Hz), easy pace"),
    "marie_claude": dict(model="en_US-libritts_r-medium", speaker=96, length_scale=1.00, noise_scale=0.680,
                  noise_w_scale=0.95, note="Marie-Claude, twenty-five: a warm mid woman (192 Hz)"),
    "werner": dict(model="en_US-libritts_r-medium", speaker=432, length_scale=1.00, noise_scale=0.660,
                  noise_w_scale=0.90, note="Werner, thirty-eight, frayed: a lighter man (128 Hz)"),
    "bu": dict(model="en_US-libritts_r-medium", speaker=400, length_scale=1.06, noise_scale=0.600,
                  noise_w_scale=0.75, note="Captain Bu, fifty-two, formal: the lowest of the sample (100 Hz), slow"),
    "mtiga": dict(model="en_US-libritts_r-medium", speaker=376, length_scale=1.04, noise_scale=0.640,
                  noise_w_scale=0.85, note="Frances Mtiga, dour and fretful (202 Hz), slower"),
    "navigator": dict(model="en_US-libritts_r-medium", speaker=416, length_scale=0.96, noise_scale=0.700,
                  noise_w_scale=1.00, note="2.4's navigator, the joker: a high man (143 Hz), quick"),
    "billy": dict(model="en_US-libritts_r-medium", speaker=408, length_scale=0.94, noise_scale=0.760,
                  noise_w_scale=1.10, note="Billy, five, the fighter (240 Hz): voiced as a cartoon boy is"),
    "freddy": dict(model="en_US-libritts_r-medium", speaker=664, length_scale=0.96, noise_scale=0.740,
                  noise_w_scale=1.05, note="Freddy, five, the sly one: the highest of the sample (251 Hz)"),
    "pal_old": dict(model="en_US-libritts_r-medium", speaker=272, length_scale=1.08, noise_scale=0.600,
                  noise_w_scale=0.85, note="Pal sixteen years on (2.31): the same voice, slower, worn"),
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
        print("voice %s = %-22s %-9s %s" % (spk, c["model"], ("#%d" % c["speaker"]) if "speaker" in c else "", c["note"]))
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
        cfg = SynthesisConfig(speaker_id=c.get("speaker"),            # a multi-speaker model's ONE voice
                              length_scale=c["length_scale"], noise_scale=c["noise_scale"],
                              noise_w_scale=c["noise_w_scale"], normalize_audio=True)
        out = os.path.join(outdir, "line_%02d.wav" % i)
        with wave.open(out, "wb") as w:
            loaded[spk].synthesize_wav(text, w, syn_config=cfg)
        # ⭐ (10-22) with=N: two people say it at once (2.29, BILLY & FREDDY) -- slot N's voice, mixed in
        both = [b.split("=", 1)[1] for b in bits[1:] if b.startswith("with=")]
        if both and both[0] in loaded:
            import numpy as _np
            c2 = dict(CAST[both[0]])
            cfg2 = SynthesisConfig(speaker_id=c2.get("speaker"), length_scale=c2["length_scale"],
                                   noise_scale=c2["noise_scale"], noise_w_scale=c2["noise_w_scale"], normalize_audio=True)
            tmp2 = out + ".with.wav"
            with wave.open(tmp2, "wb") as w:
                loaded[both[0]].synthesize_wav(text, w, syn_config=cfg2)
            with wave.open(out, "rb") as w:
                p1 = w.getparams(); a1 = _np.frombuffer(w.readframes(w.getnframes()), dtype=_np.int16).astype(_np.float32)
            with wave.open(tmp2, "rb") as w:
                a2 = _np.frombuffer(w.readframes(w.getnframes()), dtype=_np.int16).astype(_np.float32)
            n = max(len(a1), len(a2)); m = _np.zeros(n, _np.float32); m[:len(a1)] += a1 * 0.62; m[:len(a2)] += a2 * 0.62
            with wave.open(out, "wb") as w:
                w.setparams(p1); w.writeframes(_np.clip(m, -32767, 32767).astype(_np.int16).tobytes())
            os.remove(tmp2)
        with wave.open(out, "rb") as w:
            dur = w.getnframes() / float(w.getframerate())
            sr = w.getframerate()
        print("  %2d  %-5s %5.2fs @%dHz  %s" % (i, NAMES.get(spk, spk), dur, sr, text[:46]))
        i += 1
    print("lines: %d" % i)

if __name__ == "__main__":
    main()
