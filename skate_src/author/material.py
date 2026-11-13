#!/usr/bin/env python3
"""material.py -- say what a pack's meshes are made of (queue steps 22 and 23; engine/load.inc, packMaterials; design/pack_format.md).

A pack keeps only a colour texture and maybe a normal map for each mesh. This adds a MATL chunk after its body, by mesh name, so nothing is rebaked to say it and
no bake can lose it: how rough each mesh is, how much a metal, how much fine detail it shows up close, how deep its surface is for parallax, and for a body, its kind.

  rough   0 a mirror .. 1 chalk. Skin's about 0.55, paint 0.4, polished metal 0.15
  metal   0 or 1, and little between: a metal has no colour of its own under the light, and what it reflects is tinted by its colour
  detail  0 none .. 1: fine grain up close, for a wall, a road, wood
  depth   0 flat .. 0.25: how far its normal map's height (that map's alpha) shifts the texture with the angle of view, in the texture's widths
  kind    plain, skin, hair, eye or cloth: how a body's surface takes the light. alpha: its texture's alpha says, pixel by pixel (about 0.9 skin, 0.8 hair, 0.7 an eye, 0.6 cloth)
          fur: drawn as a coat of strands over the mesh, which lean, trail and settle. Then depth is how long the fur is in metres (to 0.25), detail how fine it is (0 coarse .. 1),
          and metal how curly: 0 a straight coat .. 1 wool. A short coat is stiff and a long one goes where it's pushed, by its length

usage:
  material.py PACK --list                                   its meshes, and what each is made of now
  material.py PACK --set "<mesh>=rough,metal[,detail[,depth[,kind]]]" [--set ...]
                                                            <mesh> is a mesh's whole name; a trailing * takes every mesh that starts that way.
                                                            <mesh>@N is the Nth mesh of that name, counted from 1 in the pack's order, for a body whose
                                                            parts all carry one name (Anima's are all anima_body): --list shows each one's number
  material.py PACK --clear                                  take the chunk off: the engine's tables by name decide again

A mesh the chunk doesn't name is looked up by the engine in data/materials.txt and then its own short lists, by words in its name.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import rpackchunks as RC

REC = struct.Struct("<40s6f")   # a name, then rough, metal, detail, depth, kind, and which mesh of that name (0: every one)
KINDS = {"plain": 0, "skin": 1, "hair": 2, "eye": 3, "cloth": 4, "alpha": 5, "fur": 6}


def kind_name(k):
    k = int(k + 0.5)
    return next((n for n, q in KINDS.items() if q == k), str(k))


def read(data):
    """{mesh name, or name@N for the Nth of that name: (rough, metal, detail, depth, kind)} from a pack's MATL chunk"""
    out = {}
    for tag, _was, d in RC.split(data)[1]:
        if tag != b"MATL" or len(d) < 4:
            continue
        n = struct.unpack_from("<I", d, 0)[0]
        for i in range(min(n, (len(d) - 4) // REC.size)):
            name, r, m, dt, dp, kd, nth = REC.unpack_from(d, 4 + i * REC.size)
            key = name.split(b"\0", 1)[0].decode("utf-8", "replace")
            out[key + ("@%d" % int(nth + 0.5) if nth >= 1 else "")] = (r, m, dt, dp, kd)
    return out


def write(data, table):
    """the pack's bytes with its MATL chunk saying `table`, and nothing else moved. An empty table takes the chunk off"""
    body, chunks = RC.split(data)
    chunks = [c for c in chunks if c[0] != b"MATL"]
    if table:
        def row(k, v):
            name, _, nth = k.partition("@")
            return REC.pack(name.encode("utf-8")[:39], *(([float(x) for x in v] + [0.0] * 5)[:5]), float(int(nth)) if nth.isdigit() else 0.0)
        # every-mesh-of-a-name records first, so one that names a single mesh is read after them and wins
        rows = b"".join(row(k, v) for k, v in sorted(table.items(), key=lambda kv: ("@" in kv[0], kv[0])))
        chunks.append((b"MATL", None, struct.pack("<I", len(table)) + rows))
    return RC.join(body, chunks) if chunks else bytes(body)


def set_materials(path, sets):
    """sets: {mesh name or prefix*: (rough, metal[, detail[, depth[, kind]]])}. Returns how many meshes it reached"""
    data = open(path, "rb").read()
    img = RC.Img(RC.split(data)[0])
    names = [img.mesh_name(i) for i in range(img.nMeshes)]
    table, hit = read(data), 0
    for key, v in sets.items():
        v = (list(v) + [0.0] * 5)[:5]
        v = [min(1.0, max(0.0, v[0])), min(1.0, max(0.0, v[1])), min(1.0, max(0.0, v[2])), min(0.25, max(0.0, v[3])), float(int(min(6.0, max(0.0, v[4])) + 0.5))]
        name, _, nth = key.partition("@")
        if nth.isdigit():   # one mesh of several with a name
            if names.count(name) >= int(nth) >= 1:
                table["%s@%d" % (name, int(nth))] = tuple(v)
                hit += 1
            continue
        for n in sorted(set(names)):
            if n == key or (key.endswith("*") and n.startswith(key[:-1])):
                table[n] = tuple(v)
                hit += names.count(n)
    open(path, "wb").write(write(data, table))
    return hit


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    path = argv[1]
    data = open(path, "rb").read()
    if "--clear" in argv:
        open(path, "wb").write(write(data, {}))
        print(path + ": no material chunk")
        return
    sets = {}
    for i, a in enumerate(argv):
        if a == "--set" and i + 1 < len(argv):
            k, _, v = argv[i + 1].rpartition("=")
            try:
                sets[k] = [float(KINDS.get(x.strip(), x)) for x in v.split(",")]
            except ValueError:
                sys.exit('--set "<mesh>=rough,metal[,detail[,depth[,kind]]]", and not ' + argv[i + 1])
    if sets:
        n = set_materials(path, sets)
        print("%s: %d mesh(es) said" % (path, n))
        data = open(path, "rb").read()
    img = RC.Img(RC.split(data)[0])
    said = read(data)
    names = [img.mesh_name(i) for i in range(img.nMeshes)]
    seen = {}
    for n in names:
        seen[n] = seen.get(n, 0) + 1
        shown = "%s@%d" % (n, seen[n]) if names.count(n) > 1 else n
        rec = said.get("%s@%d" % (n, seen[n]), said.get(n))
        if rec:
            r, m, dt, dp, kd = rec
            print("  %-40s rough %.2f metal %.2f detail %.2f depth %.3f kind %s" % (shown, r, m, dt, dp, kind_name(kd)))
        else:
            print("  %-40s (by the engine's tables, and its shape if it's an eye)" % shown)


if __name__ == "__main__":
    main(sys.argv)
