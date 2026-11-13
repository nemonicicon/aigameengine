#!/usr/bin/env python3
"""make_assets.py -- the roaming world, made by code: a square of cells, each its own little pack on disk, and the set that names them. They're not in git, because there can be thousands; this makes them.

usage: make_assets.py                   40 x 40 cells of 20 units, each 40 boxes, a small texture
       make_assets.py --cells 80 --boxes 200 --tex 256     as big and as heavy as you like, which is what a bench is for
Writes beside itself: props/*.rpack, sets/roam.set. The body comes from the tower game's folder if you don't have one: roam only needs it for the hero."""
import argparse
import math
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "skate_src"))
from rpackwrite import rigid_skin, to_png, write_pack

SWATCH = [(92, 96, 104), (78, 82, 90), (150, 120, 84), (120, 132, 150), (168, 88, 60), (70, 92, 70), (196, 190, 170), (60, 70, 100)]


def texture(size, seed):
    """swatches in a strip when small. Bigger, each cell's own pattern over them, so a bench's textures are really that many bytes"""
    px = bytearray()
    for y in range(size):
        for x in range(size):
            c = SWATCH[x * len(SWATCH) // size]
            k = ((x * 7 + y * 13 + seed * 31) ^ (x * y + seed)) & 31 if size > 16 else 16
            px += bytes((min(255, c[0] + k - 16), min(255, c[1] + k - 16), min(255, c[2] + k - 16), 255))
    return to_png(bytes(px), size, size)


class Mesh:
    def __init__(self):
        self.pos, self.nrm, self.uv, self.idx = [], [], [], []

    def quad(self, a, b, c, d, n, swatch):
        base = len(self.pos) // 3
        u = (swatch + 0.5) / len(SWATCH)
        for p in (a, b, c, d):
            self.pos += p
            self.nrm += n
            self.uv += (u, 0.5)
        self.idx += [base, base + 1, base + 2, base, base + 2, base + 3]

    def box(self, x0, x1, y0, y1, z0, z1, s):
        self.quad((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0), (0, 1, 0), s)
        self.quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), (0, 0, 1), s)
        self.quad((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (0, 0, -1), s)
        self.quad((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (1, 0, 0), s)
        self.quad((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0), (-1, 0, 0), s)


def dice(seed):
    x = seed * 2654435761 % 4294967296 or 1
    while True:
        x = (x * 1103515245 + 12345) % 2147483648
        yield (x >> 8) / 8388608.0


def cell_pack(path, i, j, boxes, tex, size):
    m = Mesh()
    half = size / 2
    m.quad((-half, 0, half), (half, 0, half), (half, 0, -half), (-half, 0, -half), (0, 1, 0), (i + j) % 2)
    r = dice(i * 1000 + j + 1)
    placed = []
    street = 3.0   # half a street's width: one runs through the middle of every cell each way, so there's always a way across
    for _ in range(boxes):
        w, d, h = 0.4 + next(r) * 1.1, 0.4 + next(r) * 1.1, 0.5 + next(r) ** 3 * 9
        room = half - street - 1.5
        x = (1 if next(r) < 0.5 else -1) * (street + 1.5 + next(r) * room)
        z = (1 if next(r) < 0.5 else -1) * (street + 1.5 + next(r) * room)
        s = 2 + int(next(r) * 6)
        m.box(x - w, x + w, 0, h, z - d, z + d, s)
        placed.append((x, z, w, d, h, s))
    nodes = [{"name": "root", "parent": -1, "kind": 1, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]},
             {"name": "cell", "parent": 0, "kind": 2, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]}]
    joints, weights, bone_map = rigid_skin(len(m.pos) // 3, 0)
    mesh = {"name": "cell", "node": 1, "pos": m.pos, "nrm": m.nrm, "uv": m.uv, "idx": m.idx, "joints": joints, "weights": weights, "boneMap": bone_map, "ibm": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], "tex": 0}
    write_pack(path, nodes, [mesh], [], [texture(tex, i * 37 + j)], verbose=False)
    return placed


def far_mesh(stem, cx, cz, size, placed, keep):
    """what's left of a cell from a long way off: its ground and its tallest few boxes, where they really are, in the world's own coordinates. The mesh is named after the cell, which is how the engine knows whose it is"""
    m = Mesh()
    half = size / 2
    m.quad((cx - half, 0, cz + half), (cx + half, 0, cz + half), (cx + half, 0, cz - half), (cx - half, 0, cz - half), (0, 1, 0), int(round((cx + cz) / size)) % 2)
    for x, z, w, d, h, s in sorted(placed, key=lambda b: -b[4])[:keep]:
        m.box(cx + x - w, cx + x + w, 0, h, cz + z - d, cz + z + d, s)
    joints, weights, bone_map = rigid_skin(len(m.pos) // 3, 0)
    return {"name": stem, "node": 1, "pos": m.pos, "nrm": m.nrm, "uv": m.uv, "idx": m.idx, "joints": joints, "weights": weights, "boneMap": bone_map, "ibm": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], "tex": 0}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cells", type=int, default=40)
    ap.add_argument("--boxes", type=int, default=40)
    ap.add_argument("--tex", type=int, default=8)
    ap.add_argument("--size", type=float, default=20.0)
    ap.add_argument("--far", type=int, default=6, help="how many of each cell's boxes its far stand-in keeps")
    ap.add_argument("--fardist", type=float, default=500.0)
    ap.add_argument("--bc", action="store_true", help="cook each cell: compressed textures with their mips, as a real game's would be")
    ap.add_argument("--out", default=HERE, help="where to write it, if not beside this file: a bench's world is big")
    a = ap.parse_args()
    cook = None
    if a.bc:
        sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "skate_src", "author"))
        import cook_game, rpack
        cook = lambda path: cook_game.cook_retex(rpack.Rpack(path), path, 1 << 30, True)
    props, sets, packs = os.path.join(a.out, "props"), os.path.join(a.out, "sets"), os.path.join(a.out, "packs")
    os.makedirs(props, exist_ok=True)
    os.makedirs(sets, exist_ok=True)
    os.makedirs(packs, exist_ok=True)
    edge = a.cells * a.size / 2 - a.size * 1.5   # the middle of a cell, so the host's line runs down a street
    lines = ["# the roaming world: %d x %d cells of %g units. make_assets.py writes this file. See engine/stream.inc" % (a.cells, a.cells, a.size), "streaming 70 90", "mark start %g 0 %g 0" % (-edge, -edge), "mark edge %g 0 0 0" % edge, "fardetail roam_far %g" % a.fardist]
    total = 0
    far = []
    for i in range(a.cells):
        for j in range(a.cells):
            stem = "c%03d_%03d" % (i, j)
            path = os.path.join(props, stem + ".rpack")
            placed = cell_pack(path, i, j, a.boxes, a.tex, a.size)
            if cook:
                cook(path)
            total += os.path.getsize(path)
            cx, cz = (i - a.cells / 2 + 0.5) * a.size, (j - a.cells / 2 + 0.5) * a.size
            lines.append("cell %s %g 0 %g" % (stem, cx, cz))
            far.append(far_mesh(stem, cx, cz, a.size, placed, a.far))
    nodes = [{"name": "root", "parent": -1, "kind": 1, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]},
             {"name": "far", "parent": 0, "kind": 2, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]}]
    write_pack(os.path.join(props, "roam_far.rpack"), nodes, far, [], [texture(8, 0)], verbose=False)
    with open(os.path.join(sets, "roam.set"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    body = os.path.join(HERE, "..", "tower", "packs", "v1_tower.rpack")
    if os.path.exists(body):
        shutil.copyfile(body, os.path.join(packs, "v1_tower.rpack"))
    print("%d cells, %.1f MB, %.1f KB each" % (a.cells * a.cells, total / 1048576, total / 1024 / (a.cells * a.cells)))


if __name__ == "__main__":
    main()
