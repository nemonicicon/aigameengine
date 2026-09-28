#!/usr/bin/env python3
"""say_kokoro.py -- speak a dialogue file with KOKORO, one wav per line.

    say_kokoro.py denial.txt [--out wav/denial]

jacob: "the tts is pretty bad, un realistic, microsoft sam-y. do we have a better option?"
-> then: "swithc to kokoro".

Piper is a small, fast vocoder and it sounds like one: intelligible, metrical, and flat
across a whole scene no matter what direction it is given. Kokoro-82M runs on the same
onnxruntime that is already here (no torch), carries 54 voices, and takes about one to
three seconds a line on this CPU -- so a 30-line scene still speaks in under a minute.

SAME CONTRACT AS say_piper.py, deliberately, so nothing downstream changes:
  * `cast 0=kaeli 1=susan` still names who is in the scene; slots are POSITIONAL (0 is
    the hero the lab loaded) so a voice can never be keyed on the index.
  * one wav per line into wav/<scene>/line_NN.wav, which compile_scene.py mixes.

⛔ DIRECTION DOES NOT MAP ONE TO ONE. Piper took `rate` (length_scale: bigger = slower),
`var` (how much phoneme durations wander) and `pitch` (intonation swing). Kokoro exposes
SPEED only, so `rate` is inverted into it and var/pitch are accepted and ignored rather
than silently dropped -- they still read as direction in the script, and if a future
engine has the knobs they are already written down.

⛔ AND THE TIMINGS CHANGE. Every line is a different length from Piper's, so every `.scn`
must be recompiled after re-speaking or the staging and the subtitles drift off the audio.
"""
import argparse, os, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
MODELS = os.path.join(HERE, "voices", "kokoro")

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
    # ROBOT NEKO ep 6 (THE HOLD): Mrs. Parsons' first line in the show's history, after five
    # episodes placed by geometry alone. Older, unhurried, the actual professional in the room.
    "parsons": dict(voice="af_bella", lang="en-us", speed=0.90,
                  note="older, dry, has clearly been right about this the whole time"),
    # ROBOT NEKO's director. ⭐ ep 8: he is HOLLY, and he always was -- Red Dwarf's ship's
    # computer, a head on a monitor with an IQ of 6000 who has been alone far too long.
    # He was am_onyx (American, authoritative, a man with a megaphone) for seven episodes
    # because nobody could see him; now that he has a face the voice has to match it.
    # Flat, unhurried, Northern, faintly disappointed. The RECAST is the joke: he sounds
    # different because you have never actually heard him properly before.
    # ⛔ Slot SCN_VOICE has no body -- but since ep 8 his jaw IS driven: the scene envelope
    # moves Holly's hinge on whatever screen he is being shown on.
    "director": dict(voice="bm_lewis", lang="en-gb", speed=0.88,
                  note="Holly. flat, unhurried, has been staring at the same six feet since July"),
    "holly": dict(voice="bm_lewis", lang="en-gb", speed=0.88,
                  note="the same voice under his own name, for scripts that prefer to say it"),
    # ROBOT NEKO ep 3 (UPDATE): a voice nobody in the cast has heard -- the device talking.
    # Never cast; only ever borrowed by a line's `voice=`.
    "stranger": dict(voice="bm_george", lang="en-gb", speed=0.95,
                  note="nobody we know: a system announcement"),
    # ROBOT NEKO ep 9: KICKFLIP, a Fab store skater jacob brought into the show. Every one of his
    # sixteen clips is on a skateboard and the board is skinned to his skeleton -- he has never
    # once been off it, which is the episode.
    "kickflip": dict(voice="am_puck", lang="en-us", speed=1.08,
                  note="a skate kid: easy, unbothered, has never touched the floor"),
}
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
        print("voice %s = %-10s %-9s speed %.2f  %s" % (slot, who, p["voice"], p["speed"], p["note"]))

    # ⛔ ONE DIRECTORY PER SCENE. A shared wav/ silently mixes the previous scene's
    # leftover line_NN.wav files into this one.
    outdir = os.path.join(HERE, a.out or os.path.join("wav", os.path.splitext(os.path.basename(src))[0]))
    os.makedirs(outdir, exist_ok=True)
    for f in os.listdir(outdir):
        if f.startswith("line_") and f.endswith(".wav"):
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
        # ⭐ ep 10: an ACTION line (nothing after `|`, a dur=) has no voice. Its number is still
        # counted: compile_scene.py names line_NN.wav by the line's place in the script.
        if not text:
            print("  %2d  %-5s (action line: no voice)" % (i, who))
            i += 1
            continue
        # ⭐ `voice=<person>` (per line, NOT sticky; ROBOT NEKO ep 3): the line is spoken in
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
        samples, sr = k.create(text, voice=p["voice"], speed=speed, lang=p["lang"])
        pcm = np.clip(np.asarray(samples, dtype="float32"), -1.0, 1.0)
        out = os.path.join(outdir, "line_%02d.wav" % i)
        with wave.open(out, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(sr)
            w.writeframes((pcm * 32767.0).astype("<i2").tobytes())
        print("  %2d  %-5s %5.2fs @%dHz  %s" % (i, who, len(pcm) / sr, sr, text[:46]))
        i += 1
    print("lines: %d" % i)


if __name__ == "__main__":
    main()
