#!/usr/bin/env python3
"""make_assets.py -- the talking game's body, room and voices, made by code. Run it from anywhere; it writes beside itself: packs/v1_talk.rpack, props/tk_room.rpack, sets/talk.set and voice/*.wav.
The body is the sample game's blocky figure with a face that moves every way a pack can: a jaw on a bone, eyes on bones, and shapes called blink, brow and smile. The voices are babble with a pitch, so there's something to say with nothing recorded.

usage: make_assets.py            writes them
       make_assets.py --check    compares what's on disk, byte for byte"""
import importlib.util
import io
import math
import os
import random
import struct
import sys
import tempfile
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
SKATE = os.path.join(HERE, "..", "..", "..", "skate_src")
sys.path.insert(0, SKATE)
sys.path.insert(0, os.path.join(SKATE, "author"))
from rpackwrite import rigid_skin, to_png, write_pack
from morphs import add_morph

spec = importlib.util.spec_from_file_location("sample_body", os.path.join(HERE, "..", "sample", "make_body.py"))
B = importlib.util.module_from_spec(spec)
spec.loader.exec_module(B)

RATE = 22050
HEAD = "mixamorig:Head"
# the texture's cells
SKIN, SHIRT, TROUSERS, SHOES, WHITE, DARK, MOUTH, FLOOR, WALL = range(9)
CELLS = [(214, 170, 140), (70, 110, 170), (50, 55, 70), (35, 30, 30), (235, 235, 230), (22, 18, 18), (96, 34, 40), (118, 112, 100), (150, 140, 124)]


def texture():
    px = bytearray()
    for _ in range(8):
        for c in CELLS:
            for _ in range(8):
                px += bytes(c) + b"\xff"
    return to_png(bytes(px), 8 * len(CELLS), 8)


class Mesh(B.Mesh):
    def face(self, corners, normal, bone, cell, whole_cell):
        base = len(self.pos) // 3
        u = (cell + 0.5) / len(CELLS)
        for c in corners:
            self.pos += c
            self.nrm += normal
            self.uv += (u, 0.5)
            self.joints.append([self.bone_index[bone], 0, 0, 0])
        self.idx += [base, base + 1, base + 2, base, base + 2, base + 3]

    def mark(self, *a, **k):
        """a box, and which vertices it is"""
        v0 = len(self.pos) // 3
        self.box(*a, **k)
        return range(v0, len(self.pos) // 3)


def build_body(dst):
    FACE = [("face_jaw", HEAD, (0, 1.665, -0.01)), ("face_eye_l", HEAD, (0.05, 1.725, 0.07)), ("face_eye_r", HEAD, (-0.05, 1.725, 0.07))]
    nodes = list(B.NODES)
    body = nodes.pop()
    index = dict(B.INDEX)
    for name, parent, at in FACE:
        p = B.WORLD[parent]
        nodes.append({"name": name, "parent": index[parent], "kind": 1, "t": [at[0] - p[0], at[1] - p[1], at[2] - p[2]], "q": [0, 0, 0, 1], "s": [1, 1, 1]})
        index[name] = len(nodes) - 1
    nodes.append(body)
    bones = [(b[0], b[2]) for b in B.BONES] + [(f[0], f[2]) for f in FACE]
    bone_map = [index[b[0]] for b in bones]
    m = Mesh({b[0]: i for i, b in enumerate(bones)})
    m.box("mixamorig:Hips", -0.16, 0.16, 0.88, 1.05, -0.10, 0.10, TROUSERS)
    m.box("mixamorig:Spine", -0.15, 0.15, 1.05, 1.25, -0.09, 0.09, SHIRT)
    m.box("mixamorig:Spine1", -0.19, 0.19, 1.25, 1.50, -0.11, 0.11, SHIRT)
    m.box("mixamorig:Neck", -0.05, 0.05, 1.50, 1.58, -0.05, 0.05, SKIN)
    # the head: a top half on the head bone, a lower jaw on its own, and a dark mouth behind the gap
    m.box(HEAD, -0.10, 0.10, 1.665, 1.80, -0.11, 0.11, SKIN)
    m.box(HEAD, -0.07, 0.07, 1.60, 1.665, -0.07, 0.085, MOUTH)
    m.box("face_jaw", -0.09, 0.09, 1.585, 1.652, -0.10, 0.105, SKIN)
    lips = m.mark("face_jaw", -0.06, 0.06, 1.640, 1.650, 0.104, 0.110, MOUTH)
    brows, lids = [], []
    for name, s in (("face_eye_l", 1), ("face_eye_r", -1)):
        lo = lambda a, b: (min(s * a, s * b), max(s * a, s * b))
        m.box(HEAD, *lo(0.018, 0.082), 1.705, 1.745, 0.105, 0.125, WHITE)
        m.box(name, *lo(0.04, 0.06), 1.715, 1.735, 0.125, 0.132, DARK)
        lids.append(m.mark(HEAD, *lo(0.016, 0.084), 1.745, 1.751, 0.125, 0.137, SKIN))
        brows.append(m.mark(HEAD, *lo(0.016, 0.086), 1.757, 1.767, 0.110, 0.126, DARK))
    for side, s in (("Left", 1), ("Right", -1)):
        lo = lambda a, b: (min(s * a, s * b), max(s * a, s * b))
        m.box("mixamorig:%sArm" % side, *lo(0.19, 0.47), 1.40, 1.50, -0.05, 0.05, SHIRT)
        m.box("mixamorig:%sForeArm" % side, *lo(0.47, 0.72), 1.41, 1.49, -0.04, 0.04, SKIN)
        m.box("mixamorig:%sHand" % side, *lo(0.72, 0.82), 1.41, 1.49, -0.04, 0.04, SKIN)
        m.box("mixamorig:%sUpLeg" % side, *lo(0.02, 0.18), 0.50, 0.92, -0.08, 0.08, TROUSERS)
        m.box("mixamorig:%sLeg" % side, *lo(0.03, 0.17), 0.09, 0.50, -0.07, 0.07, TROUSERS)
        m.box("mixamorig:%sFoot" % side, *lo(0.03, 0.17), 0.0, 0.09, -0.08, 0.20, SHOES)
    ibm = []
    for _, at in bones:
        ibm += [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -at[0], -at[1], -at[2], 1]
    mesh = {"name": "talk_body", "node": len(nodes) - 1, "pos": m.pos, "nrm": m.nrm, "uv": m.uv, "idx": m.idx, "joints": m.joints,
            "weights": [[1, 0, 0, 0]] * len(m.joints), "boneMap": bone_map, "ibm": ibm, "tex": 0}
    clips = [B.clip("idle", 60, B.idle), B.clip("w_walk", 30, B.gait(0.45, 0.9, 0.35, 0.25, 0.03, 0.012)), B.clip("w_run", 20, B.gait(0.8, 1.5, 0.6, 1.1, 0.14, 0.03))]
    write_pack(dst, nodes, [mesh], clips, [texture()], verbose=False)
    # a lid comes down over the eye: the bottom of it, to the bottom of the eye
    add_morph(dst, 0, "blink", [(v, 0, -0.044, 0) for r in lids for v in r if m.pos[v * 3 + 1] < 1.748])
    add_morph(dst, 0, "brow", [(v, 0, 0.014, 0) for r in brows for v in r])
    add_morph(dst, 0, "smile", [(v, 0.006 if m.pos[v * 3] > 0 else -0.006, 0.012, 0) for v in lips if abs(m.pos[v * 3]) > 0.04])


def build_room(dst):
    """a floor and two walls, so a lens can be seen staying out of a wall"""
    pos, nrm, uv, idx = [], [], [], []

    def quad(a, b, c, d, n, cell):
        base = len(pos) // 3
        u = (cell + 0.5) / len(CELLS)
        for p in (a, b, c, d):
            pos.extend(p)
            nrm.extend(n)
            uv.extend((u, 0.5))
        idx.extend((base, base + 1, base + 2, base, base + 2, base + 3))
    quad((-9, 0, 9), (9, 0, 9), (9, 0, -9), (-9, 0, -9), (0, 1, 0), FLOOR)
    quad((-9, 0, -5), (9, 0, -5), (9, 5, -5), (-9, 5, -5), (0, 0, 1), WALL)
    quad((-5, 0, 9), (-5, 0, -9), (-5, 5, -9), (-5, 5, 9), (1, 0, 0), WALL)
    nodes = [{"name": "Armature", "parent": -1, "kind": 0, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]},
             {"name": "room", "parent": 0, "kind": 2, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]}]
    joints, weights, bone_map = rigid_skin(len(pos) // 3, 0)
    mesh = {"name": "tk_room", "node": 1, "pos": pos, "nrm": nrm, "uv": uv, "idx": idx, "joints": joints, "weights": weights, "boneMap": bone_map, "ibm": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], "tex": 0, "nearest": True}
    write_pack(dst, nodes, [mesh], [], [texture()], verbose=False)


VOWELS = [(730, 1090), (270, 2290), (570, 840), (530, 1840), (300, 870)]


def babble(secs, f0, seed):
    """syllables with a pitch that falls along the line and two formants, so a loudness has somewhere to come from"""
    rnd = random.Random(seed)
    n = int(secs * RATE)
    syl, t = [], 0.03
    while t < secs - 0.1:
        l = rnd.uniform(0.11, 0.26)
        syl.append((t, min(l, secs - t), rnd.choice(VOWELS)))
        t += l + (rnd.uniform(0.22, 0.34) if rnd.random() < 0.15 else rnd.uniform(0.01, 0.07))
    out = [0.0] * n
    for s0, l, (f1, f2) in syl:
        a, b = int(s0 * RATE), int((s0 + l) * RATE)
        for i in range(a, min(b, n)):
            tt, u = i / RATE, (i - a) / max(1, b - a)
            amp = math.sin(math.pi * u) ** 0.6
            f = f0 * (1.08 - 0.22 * i / n) * (1.0 + 0.012 * math.sin(2 * math.pi * 5.0 * tt))
            v = 0.0
            for h in range(1, 9):
                fh = f * h
                v += math.sin(2 * math.pi * fh * tt) / (1.0 + ((fh - f1) / 300.0) ** 2 + 0.6 * ((fh - f2) / 400.0) ** 2 * (1 if fh > f1 else 0))
            out[i] = 0.5 * amp * v
    return out


def wav(samples):
    b = io.BytesIO()
    with wave.open(b, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32767)) for s in samples))
    return b.getvalue()


SET = """# the talking game's room. `climb`: its walls stop a ray, so a conversation's lens stays out of them
climb
prop tk_room 0 0 0 0 1
"""


def pack_bytes(fn):
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "x.rpack")
        fn(p)
        return open(p, "rb").read()


def files():
    return {
        "packs/v1_talk.rpack": pack_bytes(build_body),
        "props/tk_room.rpack": pack_bytes(build_room),
        "sets/talk.set": SET.encode(),
        "voice/a1.wav": wav(babble(1.5, 118.0, 1)),
        "voice/a2.wav": wav(babble(0.9, 124.0, 2)),
        "voice/b1.wav": wav(babble(0.6, 205.0, 3)),
        "voice/b2.wav": wav(babble(2.8, 198.0, 4)),
    }


def main():
    bad = 0
    for name, data in files().items():
        path = os.path.join(HERE, name)
        if "--check" in sys.argv:
            same = os.path.exists(path) and open(path, "rb").read() == data
            print(name + ": " + ("same as the code builds" if same else "DIFFERS"))
            bad += not same
        else:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(data)
            print(path, len(data), "bytes")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
