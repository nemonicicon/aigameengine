#!/usr/bin/env python3
"""say_kokoro.py -- speak a dialogue file with KOKORO, one wav per line.

    say_kokoro.py <scene>.txt [--out wav/<scene>]

The first text-to-speech was unrealistic, in the manner of an old desktop synthesizer; Kokoro replaced it.

Piper is a small, fast vocoder and it sounds like one: intelligible, metrical, and flat
across a whole scene no matter what direction it is given. Kokoro-82M runs on the same
onnxruntime that is already here (no torch), carries 54 voices, and takes about one to
three seconds a line on this CPU -- so a 30-line scene still speaks in under a minute.

SAME CONTRACT AS say_piper.py, deliberately, so nothing downstream changes:
  * `cast 0=<person> 1=<person>` still names who is in the scene; slots are POSITIONAL (0 is
    the hero the lab loaded) so a voice can never be keyed on the index.
  * one wav per line into wav/<scene>/line_NN.wav, which compile_scene.py mixes.

⛔ DIRECTION DOES NOT MAP ONE TO ONE. Piper took `rate` (length_scale: bigger = slower),
`var` (how much phoneme durations wander) and `pitch` (intonation swing). Kokoro exposes
SPEED only, so `rate` is inverted into it and var/pitch are accepted and ignored rather
than silently dropped -- they still read as direction in the script, and if a future
engine has the knobs they are already written down.

⛔ AND THE TIMINGS CHANGE. Every line is a different length from Piper's, so every `.scn`
must be recompiled after re-speaking or the staging and the subtitles drift off the audio.

⭐ (10-23, the space series, for better voices) A LINE IS SPOKEN IN SENTENCES. Kokoro reads a
sentence far better than a paragraph (a narration slot runs to 1350 characters), so each sentence is said on its own,
trimmed, and joined to the next by the breath its own stop asks for (a full stop longer than a comma). Where each one
starts is written beside the wav -- line_NN.json, {"sr", "sentences": [[t0, t1, text], ...]} -- and compile_scene.py
times the CAPTIONS off it. A voice may be a BLEND of Kokoro's (`af_sarah+af_aoede`, `a*0.6+b*0.4`), and `shift` is AGE
in semitones: the voice resampled up (a child's smaller throat raises pitch and formants together) or down (an old
one), Kokoro's speed compensating so the pace stays the person's. `with=N` says a line in unison with slot N.
"""
import argparse, json, os, re, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
MODELS = os.path.join(HERE, "voices", "kokoro")
sys.path.insert(0, HERE)
import mixdown as M                                     # noqa: E402  (the resampler and the trims)

# THE PEOPLE, keyed by NAME. reva is the odd one: Piper had a Scottish voice and Kokoro
# does not ship one, so she takes the most measured British voice and keeps her cadence
# from the direction instead of the accent.
PEOPLE = {
    "reva":  dict(voice="bf_alice",  lang="en-gb", speed=0.92,
                  note="measured, deliberate; she chooses her words"),
    "kaeli": dict(voice="af_heart",  lang="en-us", speed=1.04,
                  note="American, quick, loose"),
    "susan": dict(voice="af_sarah",  lang="en-us", speed=1.00,
                  note="the neighbour: warm, chatty, deflects"),
    # the stop-motion show's older neighbour: unhurried.
    "parsons": dict(voice="af_bella", lang="en-us", speed=0.90,
                  note="older, dry, has clearly been right about this the whole time"),
    # the stop-motion show's director: a head on a monitor, after Red Dwarf's ship's computer.
    # He was am_onyx (American, authoritative, a man with a megaphone) while
    # nobody could see him; once he had a face the voice had to match it.
    # Flat, unhurried, Northern, faintly disappointed.
    # ⛔ Slot SCN_VOICE has no body -- but his jaw IS driven: the scene envelope
    # moves the director's hinge on whatever screen he is being shown on.
    "director": dict(voice="bm_lewis", lang="en-gb", speed=0.88,
                  note="Holly. flat, unhurried, has been staring at the same six feet since July"),
    "holly": dict(voice="bm_lewis", lang="en-gb", speed=0.88,
                  note="the same voice under his own name, for scripts that prefer to say it"),
    # a voice nobody in the cast has heard -- the device talking.
    # Never cast; only ever borrowed by a line's `voice=`.
    "stranger": dict(voice="bm_george", lang="en-gb", speed=0.95,
                  note="nobody we know: a system announcement"),
    # a Fab store skater (a bought model). Every one of his
    # sixteen clips is on a skateboard and the board is skinned to his skeleton -- he has never
    # once been off it.
    "kickflip": dict(voice="am_puck", lang="en-us", speed=1.08,
                  note="a skate kid: easy, unbothered, has never touched the floor"),
    # ---- THE SPACE SERIES (10-23): the whole cast, recast from the temporary Piper readers. Picked by
    # MEASURED pitch (every English voice saying one line, median F0: men 92-160 Hz, women 151-226) and Kokoro's own
    # quality grades (A af_heart, A- af_bella, B- bf_emma, C+ af_aoede af_kore af_sarah am_fenrir am_michael am_puck,
    # C bm_fable bm_george). A child is a light woman's voice raised: pitch and formants rise together, as a smaller
    # throat's do. Recast by changing one entry.
    # (10-23) speed 1.10 read 183-199 words a minute (an audiobook's is 150-165): at Kokoro's own pace, ~170
    "narrator": dict(voice="am_michael", lang="en-us", speed=1.00,
                  note="the narrator (V.O.): warm and low (111 Hz), unhurried"),
    "viktor": dict(voice="af_sarah", lang="en-us", speed=1.00, shift=2.5,
                  note="twelve: a light voice raised 2.5 st (~229 Hz), as a cartoon boy is"),
    "wanda": dict(voice="af_bella", lang="en-us", speed=0.86, shift=-1.5,
                  note="a hundred and clear-eyed: warm, lowered 1.5 st, slow"),
    "amelia": dict(voice="af_heart", lang="en-us", speed=1.06,
                  note="his mother, hurrying in from the next chamber"),
    "pal": dict(voice="am_puck", lang="en-us", speed=1.00,
                  note="Pal, cheery, smiles often (130 Hz)"),
    "pal_old": dict(voice="am_puck", lang="en-us", speed=0.90, shift=-1.5,
                  note="Pal sixteen years on (2.31): the same voice, slower, worn"),
    "marie_claude": dict(voice="bf_emma", lang="en-gb", speed=1.00,
                  note="Marie-Claude, twenty-five: warm"),
    "werner": dict(voice="bm_fable", lang="en-gb", speed=1.04, shift=0.5,
                  note="Werner, thirty-eight, frayed"),
    "bu": dict(voice="bm_george", lang="en-gb", speed=0.92, shift=-2.0,
                  note="Captain Bu, fifty-two, formal: lowered 2 st"),
    "mtiga": dict(voice="af_kore", lang="en-us", speed=0.94, shift=-0.5,
                  note="Frances Mtiga, dour and fretful (155 Hz)"),
    "navigator": dict(voice="am_fenrir", lang="en-us", speed=1.08,
                  note="2.4's navigator, the joker: quick, swinging"),
    "billy": dict(voice="af_aoede", lang="en-us", speed=1.05, shift=4.5,
                  note="Billy, five, the fighter: raised 4.5 st (~241 Hz)"),
    "freddy": dict(voice="af_sarah+af_aoede", lang="en-us", speed=1.02, shift=5.5,
                  note="Freddy, five, the sly one: a blend raised 5.5 st, the highest"),
}

# ---- SENTENCES (10-23) --------------------------------------------------------------------------------------------
PAUSE = {".": 0.27, "?": 0.32, "!": 0.30, ";": 0.22, ":": 0.22, ",": 0.15, "-": 0.18}   # after a sentence ending so
ABBR = re.compile(r"(?:\b(?:Mr|Mrs|Ms|Dr|St|Jr|Sr|vs|etc|Capt|Lt|Gen|Col|Sgt|No|Mt)\.|\b[A-Z]\.)$")


def style_of(k, spec):
    """'af_sarah' -> the name; 'af_sarah+af_aoede' / 'af_sarah*0.6+af_aoede*0.4' -> their blended style vector"""
    if "+" not in spec and "*" not in spec:
        return spec
    parts = []
    for term in spec.split("+"):
        nm, _, w = term.partition("*")
        parts.append((k.get_voice_style(nm.strip()), float(w) if w else 1.0))
    tot = sum(w for _, w in parts)
    return sum(s * (w / tot) for s, w in parts).astype(np.float32)


def speakable(t):
    """what the voice is handed: the manuscript's `--` is a pause, not two hyphens"""
    t = t.replace("\u2014", ", ").replace("--", ", ").replace("\u2026", "...")
    t = re.sub(r",\s*,", ",", t)
    t = re.sub(r"\s+", " ", t).strip()
    return t.strip(", ")


def _halve(s, cap=300):
    """a sentence too long for one breath, cut at its strongest stop near the middle"""
    if len(s) <= cap:
        return [s]
    best = None
    for m in re.finditer(r"(;|:|,|--|\u2014)\s*", s):
        c = m.end()
        if c < 40 or c > len(s) - 40:
            continue
        pen = 0 if m.group(1) in (";", ":") else (25 if m.group(1) in ("--", "\u2014") else 40)
        sc = abs(c - len(s) / 2.0) + pen
        if best is None or sc < best[0]:
            best = (sc, c)
    if best is None:
        sp = s.rfind(" ", 0, len(s) // 2 + 40)
        if sp <= 0:
            return [s]
        best = (0, sp + 1)
    return _halve(s[:best[1]].strip(), cap) + _halve(s[best[1]:].strip(), cap)


def sentences(text):
    """the line in sentences (an abbreviation or an initial does not end one), each at most ~300 characters"""
    parts = re.split(r'(?<=[.!?])["\u201d\u2019)\]]*\s+(?=["\u201c\u2018(\[]?[A-Z0-9])', text.strip())
    out = []
    for p in parts:
        if out and ABBR.search(out[-1]):
            out[-1] += " " + p
        else:
            out.append(p)
    res = []
    for s in out:
        res.extend(_halve(s))
    return [s for s in res if s.strip()]


def speak(k, p, text, speed):
    """the line said sentence by sentence: (pcm, sr, [(t0, t1, sentence), ...])"""
    r = 2.0 ** (p.get("shift", 0.0) / 12.0)
    sty = style_of(k, p["voice"])
    out, sents, t, sr = [], [], 0.0, 24000
    parts = sentences(text)
    for j, s in enumerate(parts):
        say = speakable(s)
        if not re.search(r"[A-Za-z0-9]", say):
            continue
        x, sr = k.create(say, voice=sty, speed=min(1.6, max(0.5, speed / r)), lang=p["lang"])
        x = np.asarray(x, np.float32)
        if r != 1.0:
            x = M.speedup(x, r)
        x, _ = M.trim(x, sr, lead=0.02, tail=0.06)
        if out:
            end = parts[j - 1].rstrip(" \"'\u201d\u2019)]")[-1:]
            gap = PAUSE.get(end, 0.26) / max(0.7, speed)
            out.append(np.zeros(int(gap * sr), np.float32))
            t += gap
        sents.append([round(t, 3), round(t + len(x) / float(sr), 3), s])
        out.append(x)
        t += len(x) / float(sr)
    pcm = np.concatenate(out) if out else np.zeros(int(0.2 * sr), np.float32)
    return np.clip(pcm, -1.0, 1.0), sr, sents
CAST = {"0": "reva", "1": "kaeli"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("script", nargs="?", default="denial.txt")
    ap.add_argument("--out", default=None)
    a = ap.parse_args()
    src = a.script if os.path.isabs(a.script) else os.path.join(HERE, a.script)

    # the cast line, read before anything is synthesised
    for raw in open(src, encoding="utf-8"):
        t = raw.strip()
        if not t.startswith("cast "):
            continue
        for b in t.split()[1:]:
            if "=" in b:
                slot, who = b.split("=", 1)
                who = who.lower()
                assert who in PEOPLE, "unknown character %r" % who
                CAST[slot] = who
        break

    from kokoro_onnx import Kokoro
    onnx = os.path.join(MODELS, "kokoro-v1.0.onnx")
    bins = os.path.join(MODELS, "voices-v1.0.bin")
    assert os.path.exists(onnx), "missing %s -- see design/seventy_four.md" % onnx
    k = Kokoro(onnx, bins)
    for slot, who in sorted(CAST.items()):
        p = PEOPLE[who]
        print("voice %s = %-12s %-18s speed %.2f shift %+.1f  %s" % (slot, who, p["voice"], p["speed"], p.get("shift", 0.0), p["note"]))

    # ⛔ ONE DIRECTORY PER SCENE. A shared wav/ silently mixes the previous scene's
    # leftover line_NN.wav files into this one.
    outdir = os.path.join(HERE, a.out or os.path.join("wav", os.path.splitext(os.path.basename(src))[0]))
    os.makedirs(outdir, exist_ok=True)
    for f in os.listdir(outdir):
        if f.startswith("line_") and (f.endswith(".wav") or f.endswith(".json")):
            os.remove(os.path.join(outdir, f))

    i = 0
    for raw in open(src, encoding="utf-8"):
        s = raw.strip()
        if not s or s.startswith("#") or s.startswith("cast ") or "|" not in s:
            continue
        head, text = s.split("|", 1)
        bits = head.split()
        spk = bits[0].strip()
        text = text.strip()
        who = CAST.get(spk, "reva")
        # ⭐ an ACTION line (nothing after `|`, a dur=) has no voice. Its number is still
        # counted: compile_scene.py names line_NN.wav by the line's place in the script.
        if not text:
            print("  %2d  %-5s (action line: no voice)" % (i, who))
            i += 1
            continue
        # ⭐ `voice=<person>` (per line, NOT sticky): the line is spoken in
        # someone else's voice while the slot's body and jaw still speak it -- the engine never
        # sees this key, compile_scene passes it over like rate/var/pitch.
        for b in bits[1:]:
            if b.startswith("voice="):
                v = b[6:].lower()
                assert v in PEOPLE, "line %d: unknown voice %r (PEOPLE has %s)" % (i, v, sorted(PEOPLE))
                who = "%s>%s" % (who, v)
        p = PEOPLE[who.split(">")[-1]]
        speed = p["speed"]
        for b in bits[1:]:
            if b.startswith("rate="):
                # piper's length_scale is bigger-is-slower; kokoro's speed is the inverse
                try:
                    speed = p["speed"] / max(0.35, float(b[5:]))
                except ValueError:
                    pass
        speed = min(1.6, max(0.6, speed))
        pcm, sr, sents = speak(k, p, text, speed)
        # ⭐ (10-22) with=N: two people say it at once (two boys in one scene) -- slot N's voice, mixed in
        both = [b.split("=", 1)[1] for b in bits[1:] if b.startswith("with=")]
        if both and both[0] in CAST:
            q = PEOPLE[CAST[both[0]]]
            pcm2, _, _ = speak(k, q, text, q["speed"])
            n = max(len(pcm), len(pcm2))
            m = np.zeros(n, np.float32)
            m[:len(pcm)] += pcm * 0.62
            m[:len(pcm2)] += pcm2 * 0.62
            pcm = m
        out = os.path.join(outdir, "line_%02d.wav" % i)
        with wave.open(out, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(sr)
            w.writeframes((np.clip(pcm, -1.0, 1.0) * 32767.0).astype("<i2").tobytes())
        json.dump({"sr": sr, "voice": p["voice"], "shift": p.get("shift", 0.0), "sentences": sents},
                  open(out[:-4] + ".json", "w", encoding="utf-8"), ensure_ascii=False)
        print("  %2d  %-12s %6.2fs %2d sent.  %s" % (i, who, len(pcm) / float(sr), len(sents), text[:46]))
        i += 1
    print("lines: %d" % i)


if __name__ == "__main__":
    main()
