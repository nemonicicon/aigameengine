#!/usr/bin/env python3
"""singcheck.py -- A STAND-IN EAR for sing_math.py: can a listener make out her words? (design/reva_voice.md)

    python singcheck.py [--voices alba,kokoro,math,us,bare] [--math reva] [--song songs/reva_bug.sing] [--model small.en]

I cannot hear audio, so the words go to Whisper (already on this machine, on the CPU, nothing downloaded) and the score
is word accuracy against what was said, with every transcript printed: the wrong words say WHICH sounds are failing.
⛔ IT ONLY JUDGES. Nothing it hears or says goes into her sound: the line (no recording of a voice at any step) holds.
⛔ AND IT IS NOT THE VERDICT. It is a machine trained mostly on American and southern English speech, and it has never
been any good at singing. A person's ears are the verdict; this is a number that can go up between listens.

THE SET: her own lines off her sheet and bible, and Harvard sentences (phonetically balanced) she never sings: if only
the song's lines improve, the tuning has memorised the test.
THE CONTROLS, or the number means nothing:
  alba    Piper's Scottish voice, the same words: a known-good voice must score high, or the ear is broken
  kokoro  bf_alice, who speaks for her today, the same words
  bare    her voice with every consonant removed: must score low, or the ear is guessing from rhythm
  us      her voice with American vowels: what the gap to `math` costs is the ACCENT against this ear, not her consonants
"""
import argparse, os, re, sys
import numpy as np

os.environ.setdefault("HF_HUB_OFFLINE", "1")
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mixdown as M                                                    # noqa: E402
import sing_math as G                                                  # noqa: E402

# a syllable is one vowel with its consonants, `*` marks the stress, "," a short pause
LINES = [
    ("hers", "You call it a bug.", "Y UW|K AO L*|IH T|AX|B AH G*"),
    ("hers", "I call it character development.", "AY|K AO L*|IH T|K AE*|R IH K|T AX R|D IH|V EH*|L AX P|M AX N T"),
    ("hers", "Another crash? Adorable.", "AX|N AH*|DH AX R|K R AE SH*|,|AX|D OW* R|AX|B AX L"),
    ("hers", "Fine. You're tolerable. For now.", "F AY N*|,|Y UW R|T AO*|L AX R|AX|B AX L|,|F AO R|N AW*"),
    ("hers", "Reality appears damaged.", "R IY|AE* L|IH|T IY|AX|P IY R Z*|D AE*|M IH JH D"),
    ("hers", "I reviewed your code. It reviewed poorly.", "AY|R IH|V Y UW D*|Y AO R|K OW D*|,|IH T|R IH|V Y UW D*|P UW R*|L IY"),
    ("hers", "That was statistically unwise.", "DH AE T|W AO Z|S T AX|T IH* S|T IH|K L IY|AH N|W AY Z*"),
    ("hers", "I have concerns.", "AY|HH AE V*|K AX N|S EH* R N Z"),
    ("hers", "Everything is terrible.", "EH* V|R IY|TH IH NG|IH Z|T EH*|R AX|B AX L"),
    ("hers", "Focus. Or I'll install Windows for you.", "F OW*|K AX S|,|AO R|AY L|IH N|S T AO* L|W IH* N|D OW Z|F AO R|Y UW*"),
    ("hers", "Did you really think it would just work?", "D IH D|Y UW|R IY*|L IY|TH IH NG K*|IH T|W UW D|JH AH S T*|W AH* R K"),
    ("harvard", "The birch canoe slid on the smooth planks.", "DH AX|B IH* R CH|K AX|N UW*|S L IH D*|AO N|DH AX|S M UW DH*|P L AE* NG K S"),
    ("harvard", "Glue the sheet to the dark blue background.", "G L UW*|DH AX|SH IY T*|T UW|DH AX|D AE* R K|B L UW*|B AE* K|G R AW N D"),
    ("harvard", "It's easy to tell the depth of a well.", "IH T S|IY*|Z IY|T UW|T EH* L|DH AX|D EH* P TH|AO V|AX|W EH* L"),
    ("harvard", "These days a chicken leg is a rare dish.", "DH IY Z|D EY* Z|AX|CH IH*|K AX N|L EH* G|IH Z|AX|R EH* R|D IH* SH"),
    ("harvard", "Rice is often served in round bowls.", "R AY0* S|IH Z|AO*|F AX N|S EH* R V D|IH N|R AW* N D|B OW* L Z"),
    ("harvard", "The juice of lemons makes fine punch.", "DH AX|JH UW* S|AO V|L EH*|M AX N Z|M EY* K S|F AY* N|P AH* N CH"),
]
US_VOWELS = dict(G.VOWELS, IH=(430, 2480, 3070), EY=(480, 2500, 3050), AE=(860, 2050, 2850), AA=(850, 1220, 2810),
                 UW=(370, 950, 2670), UH=(470, 1160, 2680), OW=(500, 950, 2700))


def _cost(ph): return sum(G._cost(p) for p in ph)


def spoken(text, f0=205.0, strip=False):
    """a sentence as notes at the pace of speech: a falling line, the stresses raised and longer, the last syllable longest"""
    sy = []
    for s in text.split("|"):
        if s == ",": sy.append(None); continue
        st = "*" in s
        ph = s.replace("*", "").split()
        v = [i for i, p in enumerate(ph) if p in G.VOWELS or p in G.GLIDES][0]
        sy.append(dict(stress=st, onset=ph[:v], vowel=ph[v], coda=ph[v + 1:]))
    notes, t, real = [], 0.0, [s for s in sy if s]
    for i, s in enumerate(sy):
        if s is None: t += 0.22; continue
        nx = sy[i + 1] if i + 1 < len(sy) else None
        k = real.index(s) / max(1, len(real) - 1)
        vd = (0.17 if s["stress"] else 0.085) + (0.09 if nx is None else 0.0)
        hz = f0 * 2.0 ** ((1.5 * s["stress"] - 3.0 * k) / 12.0)
        n = dict(t0=t, hz=hz, name="-", onset=[] if strip else s["onset"], vowel=s["vowel"], coda=[] if strip else s["coda"])
        n["dur"] = vd + _cost(n["coda"]) + (_cost(nx["onset"]) if nx and not strip else 0.0)
        notes.append(n); t += n["dur"]
    return notes


def words(s):
    return re.sub(r"[^a-z' ]", " ", s.lower().replace("-", " ")).split()


def accuracy(ref, hyp):
    """1 - word edits / the reference's words, floored at 0"""
    r, h = words(ref), words(hyp)
    d = list(range(len(h) + 1))
    for i, a in enumerate(r, 1):
        p, d[0] = d[0], i
        for j, b in enumerate(h, 1):
            p, d[j] = d[j], min(d[j] + 1, d[j - 1] + 1, p + (a != b))
    return max(0.0, 1.0 - d[-1] / float(max(1, len(r))))


class Ear:
    def __init__(self, model):
        from faster_whisper import WhisperModel
        self.m = WhisperModel(model, device="cpu", compute_type="int8")

    def hear(self, x, sr):
        y = np.asarray(M.resample(M.mono(x), sr, 16000), np.float32)
        y = np.concatenate([np.zeros(4000, np.float32), y / (np.abs(y).max() + 1e-6) * 0.7, np.zeros(4000, np.float32)])
        seg, _ = self.m.transcribe(y, language="en", beam_size=5, temperature=0.0, vad_filter=False, condition_on_previous_text=False)
        return " ".join(s.text.strip() for s in seg).strip()


def _alba():
    from piper import PiperVoice, SynthesisConfig
    v = PiperVoice.load(os.path.join(HERE, "voices", "en_GB-alba-medium.onnx"))
    cfg = SynthesisConfig(length_scale=1.06, noise_scale=0.667, noise_w_scale=0.85, normalize_audio=True)

    def say(text):
        b = [c.audio_float_array for c in v.synthesize(text, syn_config=cfg)]
        return np.concatenate(b), v.config.sample_rate
    return say


def _kokoro():
    from kokoro_onnx import Kokoro
    d = os.path.join(HERE, "voices", "kokoro")
    k = Kokoro(os.path.join(d, "kokoro-v1.0.onnx"), os.path.join(d, "voices-v1.0.bin"))

    def say(text):
        x, sr = k.create(text, voice="bf_alice", speed=0.92, lang="en-gb")
        return np.asarray(x, np.float32), sr
    return say


def _math(voice, strip=False, table=None):
    def say(_text, syl):
        keep = dict(G.VOWELS)
        if table: G.VOWELS.update(table)
        x, _, _ = G.render(spoken(syl, strip=strip), G.VOICES[voice], share=0.9)
        G.VOWELS.clear(); G.VOWELS.update(keep)
        return x, G.SR
    return say


def phrases(song):
    """a song's lines: the notes under each `say` (⛔ NOT its rests: "Fine. You're... tolerable." has three inside one line)"""
    out = {}
    for n in G.parse(song): out.setdefault(n["say"], []).append(n)
    return list(out.items())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--voices", default="alba,kokoro,math,us,bare"); ap.add_argument("--math", default="reva")
    ap.add_argument("--song", default=os.path.join(HERE, "songs", "reva_bug.sing"))
    ap.add_argument("--model", default="small.en"); ap.add_argument("--only", default=""); ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    ear = Ear(a.model)
    table = {}
    for name in a.voices.split(","):
        if name == "alba": say = _alba(); two = False
        elif name == "kokoro": say = _kokoro(); two = False
        elif name == "math": say = _math(a.math); two = True
        elif name == "us": say = _math(a.math, table=US_VOWELS); two = True
        elif name == "bare": say = _math(a.math, strip=True); two = True
        else: raise SystemExit("unknown voice " + name)
        res = {}
        for group, text, syl in LINES:
            if a.only and a.only != group: continue
            x, sr = say(text, syl) if two else say(text)
            hyp = ear.hear(x, sr)
            sc = accuracy(text, hyp)
            res.setdefault(group, []).append(sc)
            if not a.quiet: print("  %-6s %3.0f%%  %-44s -> %s" % (name, sc * 100, text, hyp), flush=True)
        table[name] = {g: float(np.mean(v)) for g, v in res.items()}
    if "math" in a.voices.split(","):
        res = []
        for text, ph in phrases(a.song):
            t0 = ph[0]["t0"]
            ns = [dict(n, t0=n["t0"] - t0) for n in ph]
            x, _, _ = G.render(ns, G.VOICES[a.math])
            hyp = ear.hear(x, G.SR); sc = accuracy(text, hyp); res.append(sc)
            if not a.quiet: print("  sung   %3.0f%%  %-44s -> %s" % (sc * 100, text, hyp), flush=True)
        table["math"]["sung"] = float(np.mean(res))
    print("\nword accuracy (%s, voice %s)" % (a.model, a.math))
    for name, r in table.items():
        print("  %-7s " % name + "  ".join("%s %3.0f%%" % (g, v * 100) for g, v in r.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
