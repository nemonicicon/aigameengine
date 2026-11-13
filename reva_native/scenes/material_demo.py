"""material_demo.py -- what a surface is made of, on one small set made by code (queue step 22, 11-04).

It writes props/mat_balls.rpack, props/mat_panel.rpack, props/mat_floor.rpack and sets/_mat_demo.set beside the engine:
  - ten balls: a row that isn't metal and a row that is, each from rough (left) to nearly a mirror (right)   (a MATL chunk, by mesh name)
  - a panel with a normal map made by code: a grid of studs, whose height is that map's alpha                  (normals, and parallax by its depth)
  - a floor with fine detail up close                                                                         (detail)
Then, for the picture on a strong PC and the same with every material setting off:
  python .claude/skills/run-reva/driver.py looks out.png --new --set _mat_demo --env REVA_TIER=high --cams "front=0,2.6,9.5,0,1.4,0,38"
  ... --env REVA_GFX=spec=0,reflect=0,normals=0,detail=0,parallax=0
The set's surroundings for reflections are baked by the no-display build, as its probes are: REVA_BAKE=sets/_mat_demo.probes (design/the_set.md).
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.path.join(HERE, "..")
sys.path.insert(0, os.path.join(HERE, "..", "..", "skate_src"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "skate_src", "author"))
from rpackwrite import rigid_skin, to_png, write_pack
import material

NODES = [{"name": "Armature", "parent": -1, "kind": 0, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]},
         {"name": "it", "parent": 0, "kind": 2, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]}]
IBM = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]


def flat(rgb, n=8):
    return to_png(bytes(rgb + (255,)) * (n * n), n, n)


def mesh(name, pos, nrm, uv, idx, tex=0, ntex=None):
    joints, weights, bone_map = rigid_skin(len(pos) // 3, 0)
    m = {"name": name, "node": 1, "pos": pos, "nrm": nrm, "uv": uv, "idx": idx, "joints": joints, "weights": weights, "boneMap": bone_map, "ibm": IBM, "tex": tex}
    if ntex is not None:
        m["ntex"] = ntex
    return m


def ball(cx, cy, cz, r, rings=20, segs=32):
    pos, nrm, uv, idx = [], [], [], []
    for i in range(rings + 1):
        v = i / rings
        a = math.pi * v
        for j in range(segs + 1):
            u = j / segs
            b = 2 * math.pi * u
            n = (math.sin(a) * math.cos(b), math.cos(a), math.sin(a) * math.sin(b))
            pos += [cx + n[0] * r, cy + n[1] * r, cz + n[2] * r]
            nrm += n
            uv += [u, v]
    for i in range(rings):
        for j in range(segs):
            a, b = i * (segs + 1) + j, (i + 1) * (segs + 1) + j
            idx += [a, a + 1, b, b, a + 1, b + 1]   # wound to face outward, as the body builders' boxes are
    return pos, nrm, uv, idx


def balls(dst):
    meshes, said = [], {}
    for row, metal in enumerate((0.0, 1.0)):
        for k in range(5):
            rough = (0.9, 0.65, 0.45, 0.25, 0.08)[k]
            name = "ball_%s_%d" % ("m" if metal else "d", k)
            meshes.append(mesh(name, *ball(-3.2 + 1.6 * k, 0.7 + 1.5 * row, 0.0, 0.62), tex=row))
            said[name] = (rough, metal)
    write_pack(dst, NODES, meshes, [], [flat((170, 60, 50)), flat((215, 170, 90))], verbose=False)   # a red paint, and a brass
    material.set_materials(dst, said)


def studs(n=256, cells=8):
    """a normal map of round studs on a flat plate, and its height in the alpha: (the colour texture, the normal map)"""
    col, nm = bytearray(), bytearray()
    for y in range(n):
        for x in range(n):
            fx, fy = (x + 0.5) / n * cells % 1.0 - 0.5, (y + 0.5) / n * cells % 1.0 - 0.5
            d = math.hypot(fx, fy)
            h = max(0.0, 1.0 - (d / 0.34) ** 2) if d < 0.34 else 0.0          # a dome
            sx, sy = (-2 * fx / 0.34 ** 2 if h > 0 else 0.0), (-2 * fy / 0.34 ** 2 if h > 0 else 0.0)
            nx, ny, nz = -sx * 0.22, -sy * 0.22, 1.0
            L = math.sqrt(nx * nx + ny * ny + nz * nz)
            nm += bytes((int((nx / L * 0.5 + 0.5) * 255), int((ny / L * 0.5 + 0.5) * 255), int((nz / L * 0.5 + 0.5) * 255), int(h * 255)))
            c = (96, 104, 116) if h > 0 else (70, 76, 86)
            col += bytes(c + (255,))
    return to_png(bytes(col), n, n), to_png(bytes(nm), n, n)


def panel(dst):
    pos = [-1.6, 0.0, 0.0, 1.6, 0.0, 0.0, 1.6, 2.4, 0.0, -1.6, 2.4, 0.0]
    c, n = studs()
    write_pack(dst, NODES, [mesh("stud_plate", pos, [0, 0, 1] * 4, [0, 1, 1, 1, 1, 0, 0, 0], [0, 1, 2, 0, 2, 3], tex=0, ntex=1)], [], [c, n], verbose=False)
    material.set_materials(dst, {"stud_plate": (0.35, 1.0, 0.0, 0.035)})


def floor(dst):
    pos = [-9, 0, 7, 9, 0, 7, 9, 0, -7, -9, 0, -7]
    write_pack(dst, NODES, [mesh("floor_concrete", pos, [0, 1, 0] * 4, [0, 0, 1, 0, 1, 1, 0, 1], [0, 1, 2, 0, 2, 3])], [], [flat((128, 126, 120))], verbose=False)
    material.set_materials(dst, {"floor_concrete": (0.55, 0.0, 0.8, 0.0)})


SET = """# _mat_demo -- made by scenes/material_demo.py: what a surface is made of
climb
sun 42 150 1.25 1.0 0.96 0.90
sky 0.42 0.50 0.64
ground 0.30 0.28 0.26
prop mat_floor  0 0 0 0 1
prop mat_balls  0 0 0 0 1
prop mat_panel  5.4 0 -1.2 -30 1
light -4.5 2.6 3.0 1.0 0.55 0.25 6
light  4.0 1.4 3.5 0.3 0.5 1.0 6
reflect 0 1.4 2.5
"""


def main():
    props = os.path.join(NATIVE, "props")
    balls(os.path.join(props, "mat_balls.rpack"))
    panel(os.path.join(props, "mat_panel.rpack"))
    floor(os.path.join(props, "mat_floor.rpack"))
    with open(os.path.join(NATIVE, "sets", "_mat_demo.set"), "w", newline="\n") as o:
        o.write(SET)
    print("wrote props/mat_balls.rpack, mat_panel.rpack, mat_floor.rpack and sets/_mat_demo.set")


if __name__ == "__main__":
    main()
