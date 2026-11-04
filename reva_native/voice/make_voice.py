#!/usr/bin/env python3
"""make_voice.py -- the engine's own voice prompts, for the steps of setup (engine/display.inc). A set with no picture can still be talked through it, so these are what recovery rests on.

Spoken by Kokoro (Apache 2.0) on this machine and kept in git as wavs, so the engine needs no speech engine to run. Run from anywhere: it writes beside itself. A new prompt goes in the table here and in setup's step table. Name prompts to make only those: make_voice.py setup_read.
"""
import os
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
MODELS = os.path.join(HERE, "..", "scenes", "voices", "kokoro")
PROMPTS = {
    "setup_see": "If you can see a picture, press any button.",
    "setup_50": "No picture? Hold a button for three seconds, to try a European TV signal.",
    "setup_240": "Press A to try a sharper picture. Press B to skip it.",
    "setup_keep": "Press A to keep this picture. If you can't see it, it goes back by itself in fifteen seconds.",
    "setup_edges": "Move each edge until you can see all of it. Press A for the next one.",
    "setup_round": "Make the circle round. Then press A.",
    "setup_black": "Turn your TV's brightness until the left stripe just shows. Then press A.",
    "setup_white": "All three patches should look different. If they don't, turn your TV's contrast down. Then press A.",
    "setup_read": "Pick the smallest line you can read. Then press A.",
    "setup_done": "All set. Press A to play. To do this again, hold a button while the console starts.",
}
VOICE = "af_heart"


def main():
    from kokoro_onnx import Kokoro
    k = Kokoro(os.path.join(MODELS, "kokoro-v1.0.onnx"), os.path.join(MODELS, "voices-v1.0.bin"))
    for name, text in PROMPTS.items():
        if sys.argv[1:] and name not in sys.argv[1:]:
            continue
        x, sr = k.create(text, voice=VOICE, speed=1.0, lang="en-us")
        x = np.asarray(x, np.float32)
        x = x / max(1e-6, float(np.abs(x).max())) * 0.8
        with wave.open(os.path.join(HERE, name + ".wav"), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(sr)
            w.writeframes((x * 32767).astype(np.int16).tobytes())
        print("%-12s %5.1f s  %s" % (name, len(x) / sr, text))


if __name__ == "__main__":
    sys.exit(main())
