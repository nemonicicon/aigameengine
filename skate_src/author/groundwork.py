#!/usr/bin/env python3
"""groundwork.py -- a pond's bed, a rock, and steps against a wall, made by code (queue step 24).

    python skate_src/author/groundwork.py [reva_native/props]        writes gw_pond.rpack, gw_rock.rpack and gw_steps.rpack there

Plain set pieces for the things step 24 built that need ground with a shape to it, and what `reva_native/scenes/everything.py` stands them on:
  gw_pond   a low mound 8.4 by 6.4 with a bowl in it. A set's `water` 6.2 by 4.2 laid at y 0.36 meets its sides all the way round, which is a shore: where the
            water breaks, and where what's under it shows through and goes dark as it deepens.
  gw_rock   a lumpy stone, to stand in the water.
  gw_steps  three steps up to a wall, all one piece: what a decal thrown at a set is thrown at, since a splat should fold over a step's edge
            and a scorch should go up the wall.
Each stands on its own origin and is marked `Open` where a body should walk over it and not round it.
"""
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
from rpackwrite import to_png, write_pack
import material
from foliage import blob, mesh, nodes


def ramp_stops(stops, n=64):
    """a texture 4 wide from colours at heights: [(0..1 from the bottom of v, (r, g, b)), ...]"""
    px = bytearray()
    for y in range(n):
        t = 1.0 - y / (n - 1.0)                              # row 0 is the top of v
        for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
            if t <= t1 or (t1, c1) == stops[-1]:
                k = min(1.0, max(0.0, (t - t0) / max(t1 - t0, 1e-9)))
                c = [int(round(c0[i] + (c1[i] - c0[i]) * k)) for i in range(3)]
                break
        px += bytes(c + [255]) * 4
    return to_png(bytes(px), 4, n)


def smooth(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3.0 - 2.0 * t)


def pond(dst, hx=4.2, hz=3.2, top=0.5, seed=5):
    """a mound with a bowl in it, as a grid of heights"""
    R = random.Random(seed)
    nx, nz = 56, 44
    bump = [[R.uniform(-1.0, 1.0) for _ in range(nx + 1)] for _ in range(nz + 1)]

    def height(x, z):
        # how far outside a rounded rectangle 3.0 by 2.0: under 0 inside it
        qx, qz = abs(x) - 2.2, abs(z) - 1.2
        d = math.hypot(max(qx, 0.0), max(qz, 0.0)) + min(max(qx, qz), 0.0) - 0.8
        return (0.05 + (top - 0.05) * smooth(-1.25, 0.05, d)) if d < 0.05 else top * (1.0 - smooth(0.25, 1.15, d))
    pos, nrm, uv, idx = [], [], [], []
    H = [[0.0] * (nx + 1) for _ in range(nz + 1)]
    for j in range(nz + 1):
        for i in range(nx + 1):
            x, z = -hx + 2.0 * hx * i / nx, -hz + 2.0 * hz * j / nz
            edge = min(i, nx - i, j, nz - j)
            H[j][i] = (height(x, z) + 0.025 * bump[j][i] * smooth(0.0, 0.2, height(x, z))) if edge > 0 else -0.08      # its last ring goes under the floor: lying in it, the two fight for every pixel
    for j in range(nz + 1):
        for i in range(nx + 1):
            x, z = -hx + 2.0 * hx * i / nx, -hz + 2.0 * hz * j / nz
            dx = (H[j][min(i + 1, nx)] - H[j][max(i - 1, 0)]) / (4.0 * hx / nx)
            dz = (H[min(j + 1, nz)][i] - H[max(j - 1, 0)][i]) / (4.0 * hz / nz)
            l = math.sqrt(dx * dx + 1.0 + dz * dz)
            pos += [x, H[j][i], z]
            nrm += [-dx / l, 1.0 / l, -dz / l]
            uv += [i / nx * 3.0, 1.0 - min(1.0, H[j][i] / top)]          # v by height: mud at the bottom of the bowl, sand up its sides, grass on top
    for j in range(nz):
        for i in range(nx):
            a, b = j * (nx + 1) + i, (j + 1) * (nx + 1) + i
            idx += [a, b, a + 1, a + 1, b, b + 1]
    tex = ramp_stops([(0.0, (46, 42, 34)), (0.45, (92, 80, 58)), (0.80, (150, 132, 92)), (0.93, (94, 118, 58)), (1.0, (70, 104, 46))])
    write_pack(dst, nodes("Open"), [mesh("pond_bed", pos, nrm, uv, idx)], [], [tex], verbose=False)
    material.set_materials(dst, {"pond_bed": (0.9, 0.0, 0.6, 0.0)})
    return len(idx) // 3


def rock(dst, seed=3):
    pos, nrm, uv, idx = [], [], [], []
    blob(pos, nrm, uv, idx, (0.0, 0.16, 0.0), 0.34, 0.62, rings=6, segs=10, seed=seed)
    blob(pos, nrm, uv, idx, (0.22, 0.10, 0.10), 0.2, 0.6, rings=5, segs=8, seed=seed + 1)
    tex = ramp_stops([(0.0, (58, 58, 60)), (1.0, (132, 130, 126))])
    write_pack(dst, nodes("Open"), [mesh("rock", pos, nrm, uv, idx)], [], [tex], verbose=False)
    material.set_materials(dst, {"rock": (0.8, 0.0, 0.7, 0.0)})
    return len(idx) // 3


def box(pos, nrm, uv, idx, lo, hi):
    """a box with a normal a face, wound to face out"""
    x0, y0, z0 = lo
    x1, y1, z1 = hi
    faces = (((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), (0, 0, 1)), ((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (0, 0, -1)),
             ((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (1, 0, 0)), ((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0), (-1, 0, 0)),
             ((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0), (0, 1, 0)), ((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1), (0, -1, 0)))
    for a, b, c, d, n in faces:
        k = len(pos) // 3
        for p, t in zip((a, b, c, d), ((0, 1), (1, 1), (1, 0), (0, 0))):
            pos.extend(p)
            nrm.extend(n)
            uv.extend(t)
        idx.extend([k, k + 1, k + 2, k, k + 2, k + 3])


def steps(dst, wide=5.0, rise=0.34, run=0.62, n=3, wall=2.6):
    """n steps going back (-z) and up to a wall, the foot of the first at the origin's z"""
    pos, nrm, uv, idx = [], [], [], []
    for k in range(n):
        box(pos, nrm, uv, idx, (-wide / 2, 0.0, -run * (k + 1)), (wide / 2, rise * (k + 1), -run * k))
    back = -run * n
    box(pos, nrm, uv, idx, (-wide / 2, 0.0, back - 0.9), (wide / 2, rise * n, back))                    # the landing
    box(pos, nrm, uv, idx, (-wide / 2, 0.0, back - 1.15), (wide / 2, rise * n + wall, back - 0.9))      # the wall behind it
    tex = to_png(bytes([168, 166, 160, 255]) * 16, 4, 4)
    write_pack(dst, nodes(), [mesh("steps", pos, nrm, uv, idx)], [], [tex], verbose=False)
    material.set_materials(dst, {"steps": (0.85, 0.0, 0.5, 0.0)})
    return len(idx) // 3


def main(argv):
    out = argv[1] if len(argv) > 1 else os.path.join(HERE, "..", "..", "reva_native", "props")
    assert os.path.isdir(out), "%s isn't there" % out
    for name, fn in (("gw_pond", pond), ("gw_rock", rock), ("gw_steps", steps)):
        p = os.path.join(out, name + ".rpack")
        print("%s: %d triangles" % (os.path.normpath(p), fn(p)))


if __name__ == "__main__":
    main(sys.argv)
