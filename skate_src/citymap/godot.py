"""godot.py -- the cooked city as one .glb and a sidecar a Godot 4 game can load (11-11, for the skate game
`dont get dropped`, which runs in Godot). The cook calls export() at its end when CITYMAP_GODOT names a folder.

What it writes there, in METRES (the engine's units over units_per_metre; x east, y up, z south, which is glTF's and Godot's own handedness):
  <name>.glb         every cell's meshes, gathered by what they are and by blocks of `block` cells, each a node whose NAME says how the skate game's park
                     loader (addons/skate_system/skatepark_loader.gd) is to treat it: SM_<Name>_Terrain_*, _Roads_* and _Skate_* are ridden on, anything
                     else is an obstacle. A mesh's colour (the `#rrggbb` its kind carries) is a cell of one small PALETTE texture, and every vertex's
                     texture coordinate is the middle of its colour's cell, so the whole city is one material; sheer kinds (#rrggbbaa: shelter glass)
                     are a second, blended, on the same palette; what carries a texture (signs, chain link, murals) keeps it.
                     ⛔ (11-11) Not vertex colours: glTF's COLOR_0 is in the file, but Godot's importer leaves its material's "vertex colour as
                     albedo" off, and the whole city came out white in the skate game. A palette is read the same by every engine.
  <name>.park.json   the loader's sidecar: `rails`, the tops of the low walls, retaining walls, guard rails and jersey barriers the cook stood, as lines
                     to grind. No gaps and no walls.
Nothing of a business's name is in either: the cook never reads one (README.md). The data is OpenStreetMap's (ODbL) and the USGS's: credit both."""
import json
import os
import struct

import numpy as np

RIDE = {"Terrain": {"ground", "paved"},
        "Roads": {"road", "concrete", "bridge", "lane", "track", "crossing", "paint", "rail", "line", "stripe", "mark", "box"},
        "Skate_Walks": {"sidewalk", "platform", "curb", "path", "deck"}}
OBSTACLE = {"Buildings": {"roof", "wall", "glass", "trim", "awning", "unit", "shingle", "sign", "letters", "board"},
            "Cars": {"car", "tyre"},
            "Plants": {"leaf", "bark", "shrub", "pine", "needle", "trunk", "tree"},
            "Wires": {"wire"}}


def category(kind):
    w = kind.split("#")[0]
    for cat, words in list(RIDE.items()) + list(OBSTACLE.items()):
        if w in words or (cat == "Buildings" and w.startswith("bldg")):
            return cat
    return "Street"


def linear(c):
    """sRGB 0..255 to glTF's linear 0..1 (a vertex colour multiplies the base colour in linear light)"""
    c = np.asarray(c, dtype=np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


class Glb:
    def __init__(self):
        self.bin = bytearray()
        self.j = {"asset": {"version": "2.0", "generator": "skate_src/citymap/godot.py"}, "scene": 0, "scenes": [{"nodes": []}], "nodes": [], "meshes": [],
                  "accessors": [], "bufferViews": [], "buffers": [{}], "materials": [], "images": [], "textures": [],
                  "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497},
                               {"magFilter": 9728, "minFilter": 9728, "wrapS": 33071, "wrapT": 33071}]}   # 1: the palette, nearest and clamped

    def view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin += b"\0"
        v = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            v["target"] = target
        self.bin += data
        self.j["bufferViews"].append(v)
        return len(self.j["bufferViews"]) - 1

    def acc(self, arr, ctype, kind, target=None, normalized=False, minmax=False):
        a = {"bufferView": self.view(arr.tobytes(), target), "componentType": ctype, "count": int(len(arr)), "type": kind}
        if normalized:
            a["normalized"] = True
        if minmax:
            a["min"], a["max"] = arr.min(axis=0).tolist(), arr.max(axis=0).tolist()
        self.j["accessors"].append(a)
        return len(self.j["accessors"]) - 1

    def material(self, name, blend=False, texture=None, mask=False):
        m = {"name": name, "doubleSided": True, "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0.0, "roughnessFactor": 0.9}}
        if texture is not None:
            m["pbrMetallicRoughness"]["baseColorTexture"] = {"index": texture}
        if blend:
            m["alphaMode"] = "BLEND"
        elif mask:
            m["alphaMode"], m["alphaCutoff"] = "MASK", 0.4
        self.j["materials"].append(m)
        return len(self.j["materials"]) - 1

    def image(self, name, png, sampler=0):
        self.j["images"].append({"name": name, "mimeType": "image/png", "bufferView": self.view(png)})
        self.j["textures"].append({"source": len(self.j["images"]) - 1, "sampler": sampler})
        return len(self.j["textures"]) - 1

    def node(self, name, prims):
        self.j["meshes"].append({"name": name, "primitives": prims})
        self.j["nodes"].append({"name": name, "mesh": len(self.j["meshes"]) - 1})
        self.j["scenes"][0]["nodes"].append(len(self.j["nodes"]) - 1)

    def write(self, path):
        while len(self.bin) % 4:
            self.bin += b"\0"
        self.j["buffers"][0]["byteLength"] = len(self.bin)
        for k in ("images", "textures"):
            if not self.j[k]:
                del self.j[k]
        js = json.dumps(self.j, separators=(",", ":")).encode()
        js += b" " * (-len(js) % 4)
        with open(path, "wb") as f:
            f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(self.bin)))
            f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
            f.write(struct.pack("<II", len(self.bin), 0x004E4942) + bytes(self.bin))
        return 12 + 8 + len(js) + 8 + len(self.bin)


def weld(pos, nrm, col, idx):
    """the same corner, facing and colour once: a box's faces keep their own corners, so it stays flat-shaded"""
    key = np.concatenate([np.round(pos * 1000.0), np.round(nrm * 100.0), col.reshape(len(col), -1).astype(np.float64)], axis=1)
    _u, first, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
    inv = inv.reshape(-1)
    return pos[first], nrm[first], col[first], inv[idx]


PAL_CELL, PAL_COLS = 8, 32   # each colour an 8 x 8 cell of the palette, 32 to a row


def palette_png(colours):
    """the palette: colour k fills cell (k % PAL_COLS, k // PAL_COLS), in the sRGB the kind's name gives (a base colour texture is sRGB)"""
    from rpackwrite import to_png
    rows = max(1, (len(colours) + PAL_COLS - 1) // PAL_COLS)
    W, H = PAL_COLS * PAL_CELL, rows * PAL_CELL
    px = np.zeros((H, W, 4), dtype=np.uint8)
    for k, c in enumerate(colours):
        r, q = divmod(k, PAL_COLS)
        px[r * PAL_CELL:(r + 1) * PAL_CELL, q * PAL_CELL:(q + 1) * PAL_CELL] = c
    return to_png(px.tobytes(), W, H), W, H


def export(cook, out, name="Seattle", block=4):
    os.makedirs(out, exist_ok=True)
    U = cook.U
    g = Glb()
    palette = {}   # (r, g, b, a) sRGB -> its cell, in the order first met (the cells and kinds are walked sorted, so the same every cook)
    groups = {}    # (category, block i, block j, sheer) -> lists
    for (i, j), kinds in sorted(cook.cells.parts.items()):
        for kind, chunks in sorted(kinds.items()):
            kn = cook.kind_name(kind)
            hx = kn.split("#", 1)[1] if "#" in kn else "808080"
            rgba = (int(hx[0:2], 16), int(hx[2:4], 16), int(hx[4:6], 16), int(hx[6:8], 16) if len(hx) == 8 else 255)
            pk = palette.setdefault(rgba, len(palette))
            G = groups.setdefault((category(kind), i // block, j // block, len(hx) == 8), [[], [], [], [], 0])
            for pos, nrm, idx in chunks:
                p = np.asarray(pos, dtype=np.float64)
                G[0].append(p / U)
                G[1].append(np.asarray(nrm, dtype=np.float64))
                G[2].append(np.full(len(p), pk, dtype=np.int64))
                G[3].append(np.asarray(idx, dtype=np.int64) + G[4])
                G[4] += len(p)
    png, PW, PH = palette_png(list(palette))
    ptex = g.image("T_%s_Palette" % name, png, sampler=1)
    solid = g.material("M_%s" % name, texture=ptex)
    sheer = g.material("M_%s_Sheer" % name, blend=True, texture=ptex)
    stats = {}
    nodes = {}
    for (cat, bi, bj, sh), (P, N, C, I, _n) in sorted(groups.items()):
        pos, nrm, col, idx = weld(np.concatenate(P), np.concatenate(N), np.concatenate(C), np.concatenate(I))
        r, q = np.divmod(col, PAL_COLS)
        uv = np.stack([(q * PAL_CELL + PAL_CELL / 2) / PW, (r * PAL_CELL + PAL_CELL / 2) / PH], axis=1)   # each vertex at its colour's cell's middle
        prim = {"attributes": {"POSITION": g.acc(pos.astype(np.float32), 5126, "VEC3", 34962, minmax=True), "NORMAL": g.acc(nrm.astype(np.float32), 5126, "VEC3", 34962),
                               "TEXCOORD_0": g.acc(uv.astype(np.float32), 5126, "VEC2", 34962)},
                "indices": g.acc(idx.astype(np.uint32), 5125, "SCALAR", 34963), "material": sheer if sh else solid}
        nodes.setdefault("SM_%s_%s_%02d_%02d" % (name, cat, bi, bj), []).append(prim)
        s = stats.setdefault(cat, [0, 0])
        s[0] += len(idx) // 3
        s[1] += len(pos)
    # what carries a texture: a quad's two faces are one double-sided face here
    mats = {}
    ntex = 0
    for (i, j) in sorted(getattr(cook, "texq", {})):
        for tex, quads in cook.sign_atlas(sorted(cook.texq[(i, j)].items()), i, j):
            Q = np.array([q[0] for q in quads], dtype=np.float64)        # (m, 4, 3): east, height, north
            UV = np.array([q[1] for q in quads], dtype=np.float64)
            X = cook.cells.xyz(Q[..., 0], Q[..., 2], Q[..., 1]) / U
            nrm = np.cross(X[:, 1] - X[:, 0], X[:, 3] - X[:, 0])
            nrm /= np.maximum(np.linalg.norm(nrm, axis=1), 1e-12)[:, None]
            m = len(Q)
            key = str(tex)   # a cell's sign atlas is named for its cell; a weave, a mural or a banner is the same picture wherever it's used
            if key not in mats:
                blend = tex in getattr(cook, "WEAVES", ()) or key.startswith(("letters:", "emblem:"))
                cache = getattr(cook, "_weave_png", {})
                png = cache[tex] if tex in cache else cook.tex_png(tex)
                mats[key] = g.material("M_%s_Tex_%d" % (name, len(mats)), blend=blend, texture=g.image("T_%s_%d" % (name, len(mats)), png), mask=not blend)
            idx = ((np.arange(m) * 4)[:, None] + np.array([0, 1, 2, 0, 2, 3])[None, :]).reshape(-1)
            uv = UV.reshape(-1, 2)   # the pack's own coordinates: rpack2gltf.py passes them to glTF as they are too
            prim = {"attributes": {"POSITION": g.acc(X.reshape(-1, 3).astype(np.float32), 5126, "VEC3", 34962, minmax=True),
                                   "NORMAL": g.acc(np.repeat(nrm, 4, axis=0).astype(np.float32), 5126, "VEC3", 34962),
                                   "TEXCOORD_0": g.acc(uv.astype(np.float32), 5126, "VEC2", 34962)},
                    "indices": g.acc(idx.astype(np.uint32), 5125, "SCALAR", 34963), "material": mats[key]}
            nodes.setdefault("SM_%s_Signs_%02d_%02d" % (name, i // block, j // block), []).append(prim)
            ntex += m
    for nm in sorted(nodes):
        g.node(nm, nodes[nm])
    nbytes = g.write(os.path.join(out, "SM_%s.glb" % name))
    # the sidecar: the tops of the low barriers, as lines to grind
    e0, e1, n0, n1 = cook.be0, cook.be1, cook.bn0, cook.bn1
    rails = []
    for kind, pts in getattr(cook, "rail_tops", []):
        P = [(e, h, n) for e, h, n in pts if e0 <= e <= e1 and n0 <= n <= n1]
        if len(P) < 2 or sum(np.hypot(P[k + 1][0] - P[k][0], P[k + 1][2] - P[k][2]) for k in range(len(P) - 1)) < 3.0:
            continue
        rails.append({"name": "%s_%d" % (kind, len(rails)), "closed": False,
                      "points": [[round(float(e), 3), round(float(h - cook.floor), 3), round(float(-n), 3)] for e, h, n in P]})
    side = {"format": "skatepark_sidecar", "version": 1, "park": name, "rails": rails, "gaps": [], "walls": [],
            "credit": "(c) OpenStreetMap contributors (ODbL); elevation: USGS 3DEP", "box_m": {"east": [e0, e1], "south": [-n1, -n0]},
            "marks_m": {}}
    for mk, spec in sorted(cook.cfg.get("marks", {}).items()):
        e, n, h = cook.place(spec)
        side["marks_m"][mk] = [round(float(e), 3), round(float(h - cook.floor), 3), round(float(-n), 3)]
    with open(os.path.join(out, "SM_%s.park.json" % name), "w", newline="\n", encoding="utf-8") as f:
        json.dump(side, f, indent=1)
    cook.say("for Godot: %s/SM_%s.glb, %.1f MB, %d nodes, %s; %d colours in its palette (%d x %d); %d textured quads in %d textures; the sidecar has %d rails" % (
        out.replace("\\", "/"), name, nbytes / 1048576, len(nodes), ", ".join("%s %d triangles" % (c, s[0]) for c, s in sorted(stats.items())), len(palette), PW, PH,
        ntex, len(mats), len(rails)))
