#!/usr/bin/env python3
"""foliage.py -- grass, a bush and a tree, made by code, for a set's `grass` and `scatter` lines (queue step 24).

    python skate_src/author/foliage.py [reva_native/props]        writes fol_grass.rpack, fol_bush.rpack and fol_tree.rpack there

The engine ships no bought plants. These are plain ones a set can use the day it's written, and the shape any made by hand should take:
  - a prop whose foot is at its own origin, standing up its own y. The set leans it from there in its wind (`sway=`), so a foot anywhere else slides.
  - a marker node `Open`: nobody walks into a blade of grass. Grass also says `NoCast`: a field's shadow is one grey, and costs the field again.
  - one texture a mesh, dark at the foot and light at the tip: what makes a blade read as a blade.
Sizes are the engine's units (1.7 to a metre): grass to the shin, a bush to the hip, a young tree three metres tall.
Then `python skate_src/author/lod.py reva_native/props/fol_tree.rpack` makes the tree's lighter copies and its far-off picture.
"""
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
from rpackwrite import rigid_skin, to_png, write_pack
import material

IBM = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]


def nodes(*markers):
    out = [{"name": "Armature", "parent": -1, "kind": 0, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]},
           {"name": "it", "parent": 0, "kind": 2, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]}]
    return out + [{"name": m, "parent": 0, "kind": 0, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]} for m in markers]


def mesh(name, pos, nrm, uv, idx, tex=0):
    joints, weights, bone_map = rigid_skin(len(pos) // 3, 0)
    return {"name": name, "node": 1, "pos": pos, "nrm": nrm, "uv": uv, "idx": idx, "joints": joints, "weights": weights, "boneMap": bone_map, "ibm": IBM, "tex": tex}


def ramp(foot, tip, n=32):
    """a texture 4 wide: `foot` at the bottom of a mesh's v, `tip` at the top"""
    px = bytearray()
    for y in range(n):
        t = y / (n - 1.0)
        c = [int(round(tip[k] + (foot[k] - tip[k]) * t)) for k in range(3)]       # row 0 is v = 0 flipped by the engine's own reading: the tip is written first
        px += bytes(c + [255]) * 4
    return to_png(bytes(px), 4, n)


def grass(dst, seed=902, blades=110, half=0.6):
    """a patch of blades 1.2 across. Each is a bent, tapered strip seen from both sides; its v runs foot to tip"""
    R = random.Random(seed)
    pos, nrm, uv, idx = [], [], [], []
    for _ in range(blades):
        x, z = R.uniform(-half, half), R.uniform(-half, half)
        h, w, a = R.uniform(0.24, 0.52), R.uniform(0.010, 0.020), R.uniform(0.0, 2.0 * math.pi)
        lean, la = R.uniform(0.02, 0.16), R.uniform(0.0, 2.0 * math.pi)
        ax, az = math.cos(a) * w, math.sin(a) * w
        b = len(pos) // 3
        for k, (t, wk) in enumerate(((0.0, 1.0), (0.55, 0.7), (1.0, 0.0))):
            ox, oz = math.cos(la) * lean * t * t * h * 3.0, math.sin(la) * lean * t * t * h * 3.0
            row = [(x - ax * wk + ox, h * t, z - az * wk + oz), (x + ax * wk + ox, h * t, z + az * wk + oz)] if wk > 0 else [(x + ox, h * t, z + oz)]
            for p in row:
                pos += list(p)
                nrm += [0.25 * math.cos(la), 0.94, 0.25 * math.sin(la)]                   # lit as ground is, so a field is one green and not a thousand glints
                uv += [0.5, 1.0 - t]
        tris = [(b, b + 1, b + 3), (b, b + 3, b + 2), (b + 2, b + 3, b + 4)]
        for t3 in tris:
            idx += list(t3) + [t3[0], t3[2], t3[1]]                                       # and its back
    write_pack(dst, nodes("Open", "NoCast"), [mesh("grass_blades", pos, nrm, uv, idx)], [], [ramp((38, 70, 26), (150, 190, 78))], verbose=False)
    material.set_materials(dst, {"grass_blades": (0.9, 0.0, 0.0, 0.0)})
    return len(idx) // 3


def blob(pos, nrm, uv, idx, c, r, squash=1.0, rings=7, segs=12, seed=0, v0=0.0, v1=1.0):
    """a lumpy ball of leaves"""
    R = random.Random(seed)
    b = len(pos) // 3
    lump = [[1.0 + R.uniform(-0.16, 0.16) for _ in range(segs)] for _ in range(rings + 1)]
    for i in range(rings + 1):
        th = math.pi * i / rings
        for j in range(segs + 1):
            ph = 2.0 * math.pi * j / segs
            k = lump[i][j % segs] if 0 < i < rings else 1.0
            n = (math.sin(th) * math.cos(ph), math.cos(th), math.sin(th) * math.sin(ph))
            pos += [c[0] + n[0] * r * k, c[1] + n[1] * r * k * squash, c[2] + n[2] * r * k]
            nrm += list(n)
            uv += [j / segs, v1 + (v0 - v1) * (0.5 + 0.5 * n[1])]
    for i in range(rings):
        for j in range(segs):
            a, d = b + i * (segs + 1) + j, b + (i + 1) * (segs + 1) + j
            idx += [a, a + 1, d, a + 1, d + 1, d]


def trunk(pos, nrm, uv, idx, h, r0, r1, segs=10, bend=0.0):
    b = len(pos) // 3
    for i in range(5):
        t = i / 4.0
        for j in range(segs + 1):
            ph = 2.0 * math.pi * j / segs
            r = r0 + (r1 - r0) * t
            pos += [math.cos(ph) * r + bend * t * t, h * t, math.sin(ph) * r]
            nrm += [math.cos(ph), 0.0, math.sin(ph)]
            uv += [j / segs, 1.0 - t]
    for i in range(4):
        for j in range(segs):
            a, d = b + i * (segs + 1) + j, b + (i + 1) * (segs + 1) + j
            idx += [a, d, a + 1, a + 1, d, d + 1]


def bush(dst, seed=7):
    pos, nrm, uv, idx = [], [], [], []
    blob(pos, nrm, uv, idx, (0.0, 0.55, 0.0), 0.62, 0.85, seed=seed)
    blob(pos, nrm, uv, idx, (0.42, 0.42, 0.18), 0.44, 0.9, seed=seed + 1)
    blob(pos, nrm, uv, idx, (-0.36, 0.40, -0.22), 0.46, 0.9, seed=seed + 2)
    write_pack(dst, nodes("Open"), [mesh("bush_leaves", pos, nrm, uv, idx)], [], [ramp((30, 62, 26), (104, 160, 62))], verbose=False)
    material.set_materials(dst, {"bush_leaves": (0.85, 0.0, 0.3, 0.0)})
    return len(idx) // 3


def tree(dst, seed=11):
    tp, tn, tu, ti = [], [], [], []
    trunk(tp, tn, tu, ti, 2.9, 0.20, 0.11, bend=0.12)
    lp, ln, lu, li = [], [], [], []
    for k, (c, r) in enumerate((((0.12, 3.5, 0.0), 1.25), ((0.9, 3.1, 0.3), 0.85), ((-0.7, 3.2, -0.4), 0.9), ((0.2, 4.4, 0.2), 0.95), ((-0.1, 3.0, 0.8), 0.7))):
        blob(lp, ln, lu, li, c, r, 0.9, rings=9, segs=16, seed=seed + k)
    write_pack(dst, nodes("Open"), [mesh("tree_trunk", tp, tn, tu, ti, tex=0), mesh("tree_leaves", lp, ln, lu, li, tex=1)], [],
               [ramp((60, 42, 30), (104, 80, 58)), ramp((26, 58, 30), (112, 168, 70))], verbose=False)
    material.set_materials(dst, {"tree_trunk": (0.9, 0.0, 0.5, 0.0), "tree_leaves": (0.85, 0.0, 0.3, 0.0)})
    return (len(ti) + len(li)) // 3


def main(argv):
    out = argv[1] if len(argv) > 1 else os.path.join(HERE, "..", "..", "reva_native", "props")
    assert os.path.isdir(out), "%s isn't there" % out
    for name, fn in (("fol_grass", grass), ("fol_bush", bush), ("fol_tree", tree)):
        p = os.path.join(out, name + ".rpack")
        print("%s: %d triangles" % (os.path.normpath(p), fn(p)))


if __name__ == "__main__":
    main(sys.argv)
