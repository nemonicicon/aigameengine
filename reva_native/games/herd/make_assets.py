#!/usr/bin/env python3
"""make_assets.py -- the herd game's field, body and sounds, made by code. Run it from anywhere; it writes beside itself: props/hd_field.rpack, packs/v1_herd.rpack, sets/herd.set, scenes/sfx/*.wav. --check says whether what's on disk is what this makes. The body is the sample game's figure."""
import importlib.util
import io
import math
import os
import struct
import sys
import tempfile
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "skate_src"))
from rpackwrite import rigid_skin, to_png, write_pack

spec = importlib.util.spec_from_file_location("sample_body", os.path.join(HERE, "..", "sample", "make_body.py"))
B = importlib.util.module_from_spec(spec)
spec.loader.exec_module(B)
RATE = 22050


def field():
    """one big square of grass, in squares you can see the crowd moving across"""
    pos, nrm, uv, idx = [], [], [], []
    size, n = 5.0, 26
    for i in range(-n, n):
        for j in range(-n, n):
            x0, z0, u = i * size, j * size, 0.25 if (i + j) % 2 else 0.75
            base = len(pos) // 3
            for p in ((x0, 0, z0 + size), (x0 + size, 0, z0 + size), (x0 + size, 0, z0), (x0, 0, z0)):
                pos += p
                nrm += (0, 1, 0)
                uv += (u, 0.5)
            idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    px = bytearray()
    for _ in range(2):
        px += bytes((66, 92, 60, 255)) + bytes((58, 82, 54, 255))
    nodes = [{"name": "root", "parent": -1, "kind": 1, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]},
             {"name": "field", "parent": 0, "kind": 2, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]}]
    joints, weights, bone_map = rigid_skin(len(pos) // 3, 0)
    mesh = {"name": "field", "node": 1, "pos": pos, "nrm": nrm, "uv": uv, "idx": idx, "joints": joints, "weights": weights, "boneMap": bone_map, "ibm": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], "tex": 0, "nearest": True}
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "x.rpack")
        write_pack(p, nodes, [mesh], [], [to_png(bytes(px), 2, 2)], verbose=False)
        return open(p, "rb").read()


def body():
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "x.rpack")
        B.build(p)
        return open(p, "rb").read()


def tone(secs, f0, f1, level):
    out, phase = [], 0.0
    n = int(secs * RATE)
    for i in range(n):
        t = i / n
        phase += (f0 + (f1 - f0) * t) / RATE
        out.append(level * math.sin(phase * 2 * math.pi) * (1.0 - t) ** 2)
    return out


def wav(samples):
    b = io.BytesIO()
    with wave.open(b, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32767)) for s in samples))
    return b.getvalue()


def files():
    return {
        "props/hd_field.rpack": field(),
        "packs/v1_herd.rpack": body(),
        "sets/herd.set": b"# the herd game's field: make_assets.py writes this file. `climb` because it's walked by its own rules, with none of the old colliders\nclimb\nprop hd_field 0 0 0 0 1\n",
        "scenes/sfx/turn.wav": wav(tone(0.12, 500.0, 900.0, 0.4)),
        "scenes/sfx/banner.wav": wav(tone(0.2, 660.0, 880.0, 0.3)),
    }


def main():
    bad = 0
    for name, data in files().items():
        path = os.path.join(HERE, name)
        if "--check" in sys.argv:
            same = os.path.exists(path) and open(path, "rb").read() == data
            print(name, "same" if same else "DIFFERS")
            bad += not same
        else:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            open(path, "wb").write(data)
            print(path, len(data))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
