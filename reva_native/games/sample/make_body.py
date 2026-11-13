#!/usr/bin/env python3
"""make_body.py -- the sample game's one body, built by code: a blocky figure on a Mixamo-named skeleton, one small texture, and an idle, a walk and a run.
There's no source art, so a fresh checkout has a body to play with. It's also a golden pack: small enough to keep in git, with every part of the format a body uses (design/pack_format.md).

usage: make_body.py            writes packs/v1_sample.rpack beside this file
       make_body.py --check    builds it and compares with the one on disk, byte for byte"""
import math
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "skate_src"))
from rpackwrite import to_png, write_pack

# name, parent, rest position in the world. The rig faces +z, and its left is +x.
BONES = [
    ("mixamorig:Hips", "Armature", (0, 0.95, 0)),
    ("mixamorig:Spine", "mixamorig:Hips", (0, 1.05, 0)),
    ("mixamorig:Spine1", "mixamorig:Spine", (0, 1.25, 0)),
    ("mixamorig:Neck", "mixamorig:Spine1", (0, 1.50, 0)),
    ("mixamorig:Head", "mixamorig:Neck", (0, 1.58, 0)),
    ("mixamorig:HeadTop_End", "mixamorig:Head", (0, 1.80, 0)),
]
for side, sx in (("Left", 1), ("Right", -1)):
    BONES += [
        ("mixamorig:%sShoulder" % side, "mixamorig:Spine1", (sx * 0.05, 1.45, 0)),
        ("mixamorig:%sArm" % side, "mixamorig:%sShoulder" % side, (sx * 0.19, 1.45, 0)),
        ("mixamorig:%sForeArm" % side, "mixamorig:%sArm" % side, (sx * 0.47, 1.45, 0)),
        ("mixamorig:%sHand" % side, "mixamorig:%sForeArm" % side, (sx * 0.72, 1.45, 0)),
        ("mixamorig:%sUpLeg" % side, "mixamorig:Hips", (sx * 0.10, 0.92, 0)),
        ("mixamorig:%sLeg" % side, "mixamorig:%sUpLeg" % side, (sx * 0.10, 0.50, 0)),
        ("mixamorig:%sFoot" % side, "mixamorig:%sLeg" % side, (sx * 0.10, 0.09, 0)),
        ("mixamorig:%sToeBase" % side, "mixamorig:%sFoot" % side, (sx * 0.10, 0.03, 0.15)),
    ]
# the texture is a small grid of cells: 0 skin, 1 shirt, 2 trousers, 3 shoes, 4 the face
SKIN, SHIRT, TROUSERS, SHOES, FACE = 0, 1, 2, 3, 4
CELLS = [(214, 170, 140), (70, 110, 170), (50, 55, 70), (35, 30, 30), (214, 170, 140)]


def texture():
    w, h = 40, 8
    px = bytearray()
    for y in range(h):
        for x in range(w):
            c = CELLS[x // 8]
            if x // 8 == FACE and y in (2, 3) and x % 8 in (1, 2, 5, 6):
                c = (25, 20, 20)
            if x // 8 == FACE and y == 6 and 2 <= x % 8 <= 5:
                c = (120, 70, 60)
            px += bytes(c) + b"\xff"
    return to_png(bytes(px), w, h)


class Mesh:
    def __init__(self, bone_index):
        self.pos, self.nrm, self.uv, self.idx, self.joints = [], [], [], [], []
        self.bone_index = bone_index

    def face(self, corners, normal, bone, cell, whole_cell):
        """a quad. A plain cell samples its middle; the face shows the whole cell"""
        base = len(self.pos) // 3
        u0, u1 = cell * 8 / 40, (cell + 1) * 8 / 40
        um = (u0 + u1) / 2
        uvs = [(u0, 1), (u1, 1), (u1, 0), (u0, 0)] if whole_cell else [(um, 0.5)] * 4
        for c, uv in zip(corners, uvs):
            self.pos += c
            self.nrm += normal
            self.uv += uv
            self.joints.append([self.bone_index[bone], 0, 0, 0])
        self.idx += [base, base + 1, base + 2, base, base + 2, base + 3]

    def box(self, bone, x0, x1, y0, y1, z0, z1, cell, front=None):
        P = lambda x, y, z: [x, y, z]
        self.face([P(x1, y0, z1), P(x1, y0, z0), P(x1, y1, z0), P(x1, y1, z1)], [1, 0, 0], bone, cell, False)
        self.face([P(x0, y0, z0), P(x0, y0, z1), P(x0, y1, z1), P(x0, y1, z0)], [-1, 0, 0], bone, cell, False)
        self.face([P(x0, y1, z1), P(x1, y1, z1), P(x1, y1, z0), P(x0, y1, z0)], [0, 1, 0], bone, cell, False)
        self.face([P(x0, y0, z0), P(x1, y0, z0), P(x1, y0, z1), P(x0, y0, z1)], [0, -1, 0], bone, cell, False)
        self.face([P(x0, y0, z1), P(x1, y0, z1), P(x1, y1, z1), P(x0, y1, z1)], [0, 0, 1], bone, cell if front is None else front, front is not None)
        self.face([P(x1, y0, z0), P(x0, y0, z0), P(x0, y1, z0), P(x1, y1, z0)], [0, 0, -1], bone, cell, False)


def q_axis(ax, ay, az, angle):
    s = math.sin(angle / 2)
    return (ax * s, ay * s, az * s, math.cos(angle / 2))


def q_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx, aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


QX = lambda a: q_axis(1, 0, 0, a)
QY = lambda a: q_axis(0, 1, 0, a)
QZ = lambda a: q_axis(0, 0, 1, a)
TAU = 2 * math.pi


def clip(name, frames, pose):
    """a clip from a function of u, 0..1 over the loop. The last frame repeats the first, so it measures as a loop. Rotations for what pose() returns, and the hips' height."""
    n = frames + 1
    tracks = {}
    for i in range(n):
        u = (i % frames) / frames
        rot, lift = pose(u)
        for b, q in rot.items():
            tracks.setdefault((INDEX[b], 0), []).extend(q)
        tracks.setdefault((INDEX["mixamorig:Hips"], 1), []).extend((0, 0.95 + lift, 0))
    return (name, 30.0, n, tracks)


ARM_DOWN = 1.25


def arms(swing_l, swing_r, elbow):
    return {"mixamorig:LeftArm": q_mul(QX(swing_l), QZ(-ARM_DOWN)), "mixamorig:RightArm": q_mul(QX(swing_r), QZ(ARM_DOWN)),
            "mixamorig:LeftForeArm": QY(elbow), "mixamorig:RightForeArm": QY(-elbow)}


def idle(u):
    s = math.sin(TAU * u)
    r = arms(0.03 * s, -0.03 * s, 0.15)
    r["mixamorig:Spine"] = QX(0.015 * s)
    return r, 0.004 * s


def gait(thigh, knee, arm, elbow, lean, bob):
    def pose(u):
        s = math.sin(TAU * u)
        kl = max(0.0, math.cos(TAU * u)) ** 1.5
        kr = max(0.0, math.cos(TAU * (u + 0.5))) ** 1.5
        r = arms(arm * s, -arm * s, elbow)
        r["mixamorig:LeftUpLeg"] = QX(-thigh * s)
        r["mixamorig:RightUpLeg"] = QX(thigh * s)
        r["mixamorig:LeftLeg"] = QX(0.05 + knee * kl)
        r["mixamorig:RightLeg"] = QX(0.05 + knee * kr)
        r["mixamorig:Spine"] = q_mul(QX(lean), QY(0.08 * s))
        return r, bob * (math.cos(2 * TAU * u) - 1)
    return pose


NODES = [{"name": "Armature", "parent": -1, "kind": 0, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]}]
INDEX = {"Armature": 0}
WORLD = {"Armature": (0, 0, 0)}
for name, parent, at in BONES:
    p = WORLD[parent]
    NODES.append({"name": name, "parent": INDEX[parent], "kind": 1, "t": [at[0] - p[0], at[1] - p[1], at[2] - p[2]], "q": [0, 0, 0, 1], "s": [1, 1, 1]})
    INDEX[name] = len(NODES) - 1
    WORLD[name] = at
NODES.append({"name": "Body", "parent": 0, "kind": 2, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]})


def build(dst, more_clips=()):
    bone_map = [INDEX[b[0]] for b in BONES]
    m = Mesh({b[0]: i for i, b in enumerate(BONES)})
    m.box("mixamorig:Hips", -0.16, 0.16, 0.88, 1.05, -0.10, 0.10, TROUSERS)
    m.box("mixamorig:Spine", -0.15, 0.15, 1.05, 1.25, -0.09, 0.09, SHIRT)
    m.box("mixamorig:Spine1", -0.19, 0.19, 1.25, 1.50, -0.11, 0.11, SHIRT)
    m.box("mixamorig:Neck", -0.05, 0.05, 1.50, 1.58, -0.05, 0.05, SKIN)
    m.box("mixamorig:Head", -0.10, 0.10, 1.58, 1.80, -0.11, 0.11, SKIN, front=FACE)
    for side, s in (("Left", 1), ("Right", -1)):
        lo = lambda a, b: (min(s * a, s * b), max(s * a, s * b))
        m.box("mixamorig:%sArm" % side, *lo(0.19, 0.47), 1.40, 1.50, -0.05, 0.05, SHIRT)
        m.box("mixamorig:%sForeArm" % side, *lo(0.47, 0.72), 1.41, 1.49, -0.04, 0.04, SKIN)
        m.box("mixamorig:%sHand" % side, *lo(0.72, 0.82), 1.41, 1.49, -0.04, 0.04, SKIN)
        m.box("mixamorig:%sUpLeg" % side, *lo(0.02, 0.18), 0.50, 0.92, -0.08, 0.08, TROUSERS)
        m.box("mixamorig:%sLeg" % side, *lo(0.03, 0.17), 0.09, 0.50, -0.07, 0.07, TROUSERS)
        m.box("mixamorig:%sFoot" % side, *lo(0.03, 0.17), 0.0, 0.09, -0.08, 0.20, SHOES)
    ibm = []
    for b in BONES:
        x, y, z = b[2]
        ibm += [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -x, -y, -z, 1]
    mesh = {"name": "sample_body", "node": len(NODES) - 1, "pos": m.pos, "nrm": m.nrm, "uv": m.uv, "idx": m.idx, "joints": m.joints,
            "weights": [[1, 0, 0, 0]] * len(m.joints), "boneMap": bone_map, "ibm": ibm, "tex": 0}
    clips = [clip("idle", 60, idle), clip("w_walk", 30, gait(0.45, 0.9, 0.35, 0.25, 0.03, 0.012)), clip("w_run", 20, gait(0.8, 1.5, 0.6, 1.1, 0.14, 0.03))]
    write_pack(dst, NODES, [mesh], clips + list(more_clips), [texture()], verbose=False)


if __name__ == "__main__":
    out = os.path.join(HERE, "packs", "v1_sample.rpack")
    if "--check" in sys.argv:
        with tempfile.TemporaryDirectory() as d:
            t = os.path.join(d, "v1_sample.rpack")
            build(t)
            same = open(t, "rb").read() == open(out, "rb").read()
        print("packs/v1_sample.rpack: " + ("same as the code builds" if same else "⛔ DIFFERS from what the code builds"))
        sys.exit(0 if same else 1)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    build(out)
    print(out, os.path.getsize(out), "bytes")
