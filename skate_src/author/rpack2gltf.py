#!/usr/bin/env python3
"""rpack2gltf.py -- export a pack's SKELETON and AUTHORED CLIPS as a .glb any engine can import.

    python skate_src/author/rpack2gltf.py reva_native/packs/v1_r2moves.rpack out/reva_skate_moves.glb \
        --clips "z_rm_sk_*" --exclude z_rm_sk_pickup --board
    python skate_src/author/rpack2gltf.py reva_native/packs/v1_r2moves.rpack out/reva_moves.glb \
        --clips "z_*" --exclude "z_rm_sk_*,z_twisttest"
    python skate_src/author/rpack2gltf.py reva_native/packs/v1_clanker.rpack out/clanker.glb \
        --clips "z_rm_sk_*" --board --mesh --materials-from D:/reva902/clanker/clanker.glb

Written for a Godot 4 team (godot/ANIMATION_IMPORT.md): Godot's native 3D format is glTF, and a clip
here is already what a glTF animation is -- per-bone LOCAL rotation / translation keys at a frame rate.

What goes in the file, and what deliberately does not:
  * the skeleton (every bone, rest pose, names as they are -- reva's are Mixamo-style: Hips, Spine,
    LeftUpLeg ...), a skin over it, and each selected clip as a glTF animation (LINEAR, the clip's own
    fps). Root motion rides on the bone that carries it (Hips on reva's z_rm_* clips).
  * by default NO MESH FROM ANY ASSET: a generated STICK FIGURE (an octahedron per bone, left side blue,
    right side red, rigidly skinned) so the moves can be looked at in Godot or Blender before
    retargeting; delete it.
  * `--mesh`: the pack's OWN meshes instead of the stick figure, for a character that is ours to hand
    over (Clanker, 09-27). Skinned exactly as the club engine skins them -- each vertex through its
    mesh's bind matrix, then its bones' world x inverse-bind -- with the pack's textures embedded.
    The materials are what the engine draws: base colour (+ normal map), double-sided (the engine never
    culls), BLEND where the pack blends, a 0.4 alpha cut where the texture has alpha, and an untextured
    mesh's `#rrggbb` name as its colour. `--materials-from <source.glb>` names the file the pack was
    baked from: each mesh then takes the source's material (name, metallic, roughness, emission),
    matched by GEOMETRY. Morph targets (a face's shapes) are not exported, and it says so.
  * `--board`: a skateboard node (a generated deck + four wheels) driven by each clip's `<clip>_bd`
    channel -- the board frame in the rider's model space (origin the deck-top centre, +X the nose, +Y
    the deck's up), exactly as the club engine draws it. Clips without a channel leave the board where
    it was, so keep boarded and board-less clips in separate files.
  * a SEAMLESS clip (first and last frame within 20 degrees on every bone, the engine's own test) gets
    frame 0 repeated at t = frames/fps, so a looped playback wraps like the engine's does. Which clips
    loop is written to the animation's `extras` and printed.

Checked by godot/verify_export_blender.py: Blender's glTF importer re-reads the file and every joint's
world position is compared with this file's own forward kinematics at sampled frames -- and with
`--mesh`, every vertex of the deformed body against the engine's skinning of the pack.
"""
import argparse, fnmatch, io, json, math, os, re, struct, sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rpack import Rpack


# ------------------------------------------------------------------ small math
def q2m(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def trs(t, q, s):
    M = np.eye(4)
    M[:3, :3] = q2m(q) * np.array(s)[None, :]
    M[:3, 3] = t
    return M


def qangle(a, b):
    d = abs(float(np.dot(a, b)))
    return 2.0 * math.degrees(math.acos(min(1.0, d)))


# ------------------------------------------------------------------ glTF writer
class Gltf:
    def __init__(self):
        self.j = {"asset": {"version": "2.0", "generator": "club-engine rpack2gltf.py"},
                  "scene": 0, "scenes": [{"nodes": []}], "nodes": [], "meshes": [], "materials": [],
                  "skins": [], "animations": [], "accessors": [], "bufferViews": [], "buffers": [{}]}
        self.bin = bytearray()

    def view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        v = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            v["target"] = target
        self.bin += data
        self.j["bufferViews"].append(v)
        return len(self.j["bufferViews"]) - 1

    def acc(self, arr, atype, ctype=5126, target=None, minmax=False):
        arr = np.ascontiguousarray(arr)
        dt = {5126: "<f4", 5123: "<u2", 5125: "<u4", 5121: "u1"}[ctype]
        bv = self.view(arr.astype(dt).tobytes(), target)
        a = {"bufferView": bv, "componentType": ctype, "count": int(arr.shape[0]), "type": atype}
        if minmax:
            flat = arr.reshape(arr.shape[0], -1)
            a["min"] = [float(x) for x in flat.min(0)]
            a["max"] = [float(x) for x in flat.max(0)]
        self.j["accessors"].append(a)
        return len(self.j["accessors"]) - 1

    def material(self, name, rgb, rough=0.8):
        self.j["materials"].append({"name": name, "pbrMetallicRoughness": {
            "baseColorFactor": [rgb[0], rgb[1], rgb[2], 1.0], "metallicFactor": 0.0, "roughnessFactor": rough}})
        return len(self.j["materials"]) - 1

    def write(self, path):
        j = {k: v for k, v in self.j.items() if v != [] or k in ("scenes", "nodes", "buffers")}
        while len(self.bin) % 4:
            self.bin.append(0)
        j["buffers"] = [{"byteLength": len(self.bin)}]
        js = json.dumps(j, separators=(",", ":")).encode("utf-8")
        while len(js) % 4:
            js += b" "
        out = b"glTF" + struct.pack("<II", 2, 12 + 8 + len(js) + 8 + len(self.bin))
        out += struct.pack("<I", len(js)) + b"JSON" + js
        out += struct.pack("<I", len(self.bin)) + b"BIN\x00" + bytes(self.bin)
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        open(path, "wb").write(out)


def octa(a, b, r):
    """an octahedral bone from point a to point b, radius r at 15% along"""
    d = b - a
    L = np.linalg.norm(d)
    if L < 1e-9:
        return None, None
    ax = d / L
    up = np.array([0.0, 0.0, 1.0]) if abs(ax[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
    u = np.cross(ax, up); u /= np.linalg.norm(u)
    v = np.cross(ax, u)
    m = a + d * 0.15
    P = np.array([a, m + u * r, m + v * r, m - u * r, m - v * r, b])
    T = np.array([[0, 1, 2], [0, 2, 3], [0, 3, 4], [0, 4, 1], [5, 2, 1], [5, 3, 2], [5, 4, 3], [5, 1, 4]])
    return P, T


def box(lo, hi):
    x0, y0, z0 = lo; x1, y1, z1 = hi
    P = np.array([[x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0],
                  [x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]])
    T = np.array([[0, 2, 1], [0, 3, 2], [4, 5, 6], [4, 6, 7], [0, 1, 5], [0, 5, 4],
                  [2, 3, 7], [2, 7, 6], [1, 2, 6], [1, 6, 5], [3, 0, 4], [3, 4, 7]])
    return P, T


def flat_normals(P, T):
    """unshared vertices, one normal per face -- a stick figure should read faceted"""
    V = P[T].reshape(-1, 3)
    n = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
    return V, np.repeat(n, 3, axis=0), np.arange(len(V)).reshape(-1, 3)


# ------------------------------------------------------------------ the pack's own meshes (--mesh)
MESH = struct.Struct("<15I")
MESH_FIELDS = ("name", "node", "verts", "nidx", "nbones", "flags", "oPos", "oNorm", "oUV", "oJ", "oW", "oIdx",
               "oBM", "oIBM", "oBind")


def pack_meshes(R):
    return [dict(zip(MESH_FIELDS, MESH.unpack_from(R.buf, R.offMeshes + mi * 60))) for mi in range(R.nMeshes)]


def col_major(buf, off):
    """a pack matrix (16 floats, column-major, as GL takes it) as a row-major numpy 4x4"""
    return np.frombuffer(buf, "<f4", 16, off).astype(np.float64).reshape(4, 4).T


def pack_materials(R):
    """per mesh (texture, normal texture, flags) from the FIRST material record naming it, and the
    per-texture flip / nearest sets from EVERY record -- both exactly as the engine's loader reads them"""
    per, flip, near = {}, set(), set()
    for r in range(R.nMats):
        o = R.offMats + r * 36
        mesh, fl = struct.unpack_from("<I", R.buf, o)[0], struct.unpack_from("<I", R.buf, o + 16)[0]
        tex = struct.unpack_from("<i", R.buf, o + 24)[0]
        ntex = struct.unpack_from("<i", R.buf, o + 28)[0] - 1      # +1 in the file, so an old pack reads none
        tex = tex if 0 <= tex < R.nTex else -1
        ntex = ntex if 0 <= ntex < R.nTex else -1
        if tex >= 0:
            if fl & 2: flip.add(tex)
            if fl & 1: near.add(tex)
        if ntex >= 0 and fl & 2: flip.add(ntex)
        per.setdefault(mesh, (tex, ntex, fl))
    return per, flip, near


def texture_blob(R, t):
    off, ln = struct.unpack_from("<II", R.buf, R.offTexTable + t * 16 + 8)
    return bytes(R.buf[off:off + ln])


def has_alpha(blob):
    """does this image cut anything out? (the engine discards below 0.4 on every textured mesh)"""
    try:
        from PIL import Image
        im = Image.open(io.BytesIO(blob))
        if im.mode in ("RGBA", "LA") or (im.mode == "P" and "transparency" in im.info):
            return im.convert("RGBA").getextrema()[3][0] < 255
        return False
    except ImportError:                    # no PIL: a PNG whose colour type carries alpha (4, 6) counts
        return blob[:4] == b"\x89PNG" and len(blob) > 25 and blob[25] in (4, 6)


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def source_materials(path, R, meshes):
    """--materials-from: the material each pack mesh had in the file it was baked from, matched by
    GEOMETRY -- vertex count and position bounds, which the baker copies bit for bit.
    ⛔ NOT BY ORDER: the baker reorders meshes. Measured on Clanker: his metal is the pack's mesh 1 and
    the source's primitive 3, so matching by list position would have put chrome on his paint."""
    b = open(path, "rb").read()
    if b[:4] != b"glTF":
        sys.exit("--materials-from: %s is not a .glb" % path)
    j = json.loads(b[20:20 + struct.unpack_from("<I", b, 12)[0]])
    prims = []
    for m in j.get("meshes", []):
        for p in m["primitives"]:
            acc = j["accessors"][p["attributes"]["POSITION"]]
            prims.append((acc["count"], np.array(acc.get("min", [0.0] * 3)), np.array(acc.get("max", [0.0] * 3)),
                          p.get("material")))
    out = {}
    for mi, m in enumerate(meshes):
        P = np.frombuffer(R.buf, "<f4", m["verts"] * 3, m["oPos"]).reshape(-1, 3)
        lo, hi = P.min(0), P.max(0)
        hits = [k for k, (cnt, mn, mx, _mat) in enumerate(prims)
                if cnt == m["verts"] and np.abs(mn - lo).max() < 1e-5 and np.abs(mx - hi).max() < 1e-5]
        if len(hits) == 1 and prims[hits[0]][3] is not None:
            out[mi] = j["materials"][prims[hits[0]][3]]
        else:
            print("   ⛔ mesh %d (%d verts): %s in %s -- it keeps the engine's plain material"
                  % (mi, m["verts"], "no partner" if not hits else "%d partners" % len(hits), os.path.basename(path)))
    return out


def export_meshes(R, G, nodes, gi, keep, Wr, jidx, meshes, src_mats, stem):
    """the pack's skinned meshes as glTF primitives: returns a one-line summary"""
    per, tflip, tnear = pack_materials(R)
    gtex = {}

    def gl_texture(t, kind):
        """one pack texture as a glTF image + texture (once); `kind` names it -- Godot extracts an embedded
        image to a file named after it, as `<file>_<name>.png`: `clanker_basecolor_0.png`, not `clanker_image_0.png`"""
        if t in gtex:
            return gtex[t]
        blob = texture_blob(R, t)
        mime = "image/png" if blob[:4] == b"\x89PNG" else ("image/jpeg" if blob[:2] == b"\xff\xd8" else None)
        if mime is None:
            print("   ⛔ texture %d is neither PNG nor JPEG -- left out" % t)
            gtex[t] = None
            return None
        G.j.setdefault("images", []).append({"bufferView": G.view(blob), "mimeType": mime,
                                             "name": "%s_%d" % (kind, t)})
        smp = {"magFilter": 9728 if t in tnear else 9729, "minFilter": 9728 if t in tnear else 9987,
               "wrapS": 10497, "wrapT": 10497}                     # REPEAT, as the engine's loader
        samplers = G.j.setdefault("samplers", [])
        if smp not in samplers:
            samplers.append(smp)
        G.j.setdefault("textures", []).append({"sampler": samplers.index(smp), "source": len(G.j["images"]) - 1})
        gtex[t] = len(G.j["textures"]) - 1
        return gtex[t]

    groups, skinkey, nv_all, nt_all, reused = {}, {}, 0, 0, 0
    for mi, m in enumerate(meshes):
        if not (m["flags"] & 4) or not m["nbones"]:
            print("   mesh %d: not skinned -- the engine does not draw it on a body, left out" % mi)
            continue
        nv, nb = m["verts"], m["nbones"]
        B = col_major(R.buf, m["oBind"])
        P = np.frombuffer(R.buf, "<f4", nv * 3, m["oPos"]).reshape(nv, 3).astype(np.float64)
        attrs = {"POSITION": G.acc(P @ B[:3, :3].T + B[:3, 3], "VEC3", minmax=True)}
        if m["flags"] & 1:
            N = np.frombuffer(R.buf, "<f4", nv * 3, m["oNorm"]).reshape(nv, 3).astype(np.float64)
            N = N @ np.linalg.inv(B[:3, :3])                         # the inverse-transpose, as rows
            L = np.linalg.norm(N, axis=1, keepdims=True)
            N = np.where(L > 1e-9, N / np.maximum(L, 1e-12), np.array([0.0, 1.0, 0.0]))
            attrs["NORMAL"] = G.acc(N, "VEC3")
        tex, ntex, _mfl = per.get(mi, (-1, -1, 0))
        if m["flags"] & 2:
            UV = np.frombuffer(R.buf, "<f4", nv * 2, m["oUV"]).reshape(nv, 2).astype(np.float64).copy()
            if tex in tflip:            # the engine flips that image on load: in glTF's convention, v' = 1 - v
                UV[:, 1] = 1.0 - UV[:, 1]
            attrs["TEXCOORD_0"] = G.acc(UV, "VEC2")
        # ---- the skin: the pack's bone map and inverse binds, or the skeleton's own skin when they agree
        bm = np.frombuffer(R.buf, "<u2", nb, m["oBM"]).astype(int)
        IBM = np.frombuffer(R.buf, "<f4", nb * 16, m["oIBM"]).astype(np.float64).reshape(nb, 4, 4).transpose(0, 2, 1)
        J = np.frombuffer(R.buf, "u1", nv * 4, m["oJ"]).reshape(nv, 4).astype(int)
        Wt = np.frombuffer(R.buf, "u1", nv * 4, m["oW"]).reshape(nv, 4).astype(np.float64) / 255.0
        bad = J >= nb                                                 # the engine skips an index past the map
        Wt[bad] = 0.0; J[bad] = 0
        dead = Wt.sum(1) < 1e-6
        Wt[dead] = [1.0, 0.0, 0.0, 0.0]
        Wt /= Wt.sum(1, keepdims=True)
        if all(int(b) in jidx for b in bm) and all(np.abs(IBM[k] - np.linalg.inv(Wr[int(b)])).max() < 1e-4
                                                   for k, b in enumerate(bm)):
            skin = 0
            J = np.array([jidx[int(b)] for b in bm])[J]
            reused += 1
        else:
            key = (tuple(bm.tolist()), IBM.tobytes())
            if key not in skinkey:
                raw = np.frombuffer(R.buf, "<f4", nb * 16, m["oIBM"]).reshape(nb, 16)   # already column-major
                G.j["skins"].append({"name": "%s_skin%d" % (stem, len(G.j["skins"])),
                                     "joints": [gi[int(b)] for b in bm], "inverseBindMatrices": G.acc(raw, "MAT4")})
                skinkey[key] = len(G.j["skins"]) - 1
            skin = skinkey[key]
        attrs["JOINTS_0"] = G.acc(J, "VEC4", 5123)
        attrs["WEIGHTS_0"] = G.acc(Wt, "VEC4")
        # ---- the material
        mname = R.sname(m["name"]) or ("mesh%d" % mi)
        mat = {"name": "%s_%d" % (mname, mi), "doubleSided": True,
               "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 0.8}}
        src = src_mats.get(mi)
        if src:
            mat["name"] = src.get("name", mat["name"])
            spbr = src.get("pbrMetallicRoughness", {})
            for k in ("baseColorFactor", "metallicFactor", "roughnessFactor"):
                if k in spbr: mat["pbrMetallicRoughness"][k] = spbr[k]
            for k in ("emissiveFactor", "doubleSided"):
                if k in src: mat[k] = src[k]
            lost = ([k for k in ("metallicRoughnessTexture",) if k in spbr] +
                    [k for k in ("emissiveTexture", "occlusionTexture") if k in src])
            # an extension made only of numbers (x3d's KHR_materials_specular: her low sheen) travels; one that
            # points at a texture cannot -- the pack does not hold that texture
            for ek, ev in (src.get("extensions") or {}).items():
                if '"index"' in json.dumps(ev):
                    lost.append(ek)
                else:
                    mat.setdefault("extensions", {})[ek] = ev
                    used = G.j.setdefault("extensionsUsed", [])
                    if ek not in used: used.append(ek)
            if lost:
                print("   ⛔ %s: %s not carried (a pack holds base colour and normal maps only)"
                      % (mat["name"], ", ".join(lost)))
        if tex >= 0 and m["flags"] & 2:
            gt = gl_texture(tex, "basecolor")
            if gt is not None: mat["pbrMetallicRoughness"]["baseColorTexture"] = {"index": gt}
        if ntex >= 0 and m["flags"] & 2:
            gt = gl_texture(ntex, "normal")
            if gt is not None: mat["normalTexture"] = {"index": gt}
        if tex < 0:                     # an untextured mesh takes its colour from #rrggbb in its NAME
            hx = re.search(r"#([0-9a-fA-F]{6})", mname)
            if hx:
                mat["pbrMetallicRoughness"]["baseColorFactor"] = [
                    srgb_to_linear(int(hx.group(1)[k:k + 2], 16) / 255.0) for k in (0, 2, 4)] + [1.0]
        if m["flags"] & 8:
            mat["alphaMode"] = "BLEND"
        elif tex >= 0 and has_alpha(texture_blob(R, tex)):
            mat["alphaMode"] = "MASK"; mat["alphaCutoff"] = 0.4
        G.j["materials"].append(mat)
        prim = {"attributes": attrs, "material": len(G.j["materials"]) - 1}
        if m["nidx"]:
            prim["indices"] = G.acc(np.frombuffer(R.buf, "<u4", m["nidx"], m["oIdx"]), "SCALAR", 5125)
            nt_all += m["nidx"] // 3
        groups.setdefault(skin, []).append(prim)
        nv_all += nv
    body = nodes[meshes[0]["node"]]["name"] if meshes else stem
    if body in {nodes[n]["name"] for n in keep}:
        body += "_mesh"
    for skin, prims in groups.items():
        nm = body if len(groups) == 1 else "%s_%d" % (body, skin)
        G.j["meshes"].append({"name": nm, "primitives": prims})
        G.j["nodes"].append({"name": nm, "mesh": len(G.j["meshes"]) - 1, "skin": skin})
        G.j["scenes"][0]["nodes"].append(len(G.j["nodes"]) - 1)
    if struct.unpack_from("<I", R.buf, 4 + 14 * 4)[0]:
        print("   note: this pack carries a morph table (a face's shapes) -- NOT exported by --mesh")
    return "%d meshes (%d verts, %d tris) on %s, %d texture(s)" % (
        sum(len(p) for p in groups.values()), nv_all, nt_all,
        "the skeleton's own skin" if reused and len(groups) == 1 else "%d skin(s)" % len(groups),
        sum(1 for v in gtex.values() if v is not None))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pack"); ap.add_argument("out")
    ap.add_argument("--clips", default="z_*", help="comma list of glob patterns")
    ap.add_argument("--exclude", default="", help="comma list of glob patterns")
    ap.add_argument("--board", action="store_true", help="a skateboard node driven by <clip>_bd channels")
    ap.add_argument("--no-figure", action="store_true", help="skeleton only, no stick figure")
    ap.add_argument("--mesh", action="store_true", help="the pack's own skinned, textured meshes (no stick figure)")
    ap.add_argument("--materials-from", default="", help="the .glb the pack was baked from: its materials, "
                                                         "matched to the meshes by geometry (with --mesh)")
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    R = Rpack(a.pack)
    nodes = R.nodes()
    names = [n["name"] for n in nodes]
    allclips = [R.clip(c) for c in range(R.nClips)]
    cname = [c["name"] for c in allclips]
    inc = [p for p in a.clips.split(",") if p]
    exc = [p for p in a.exclude.split(",") if p]
    pick = [i for i, c in enumerate(allclips)
            if any(fnmatch.fnmatch(c["name"], p) for p in inc)
            and not any(fnmatch.fnmatch(c["name"], p) for p in exc)
            and not c["name"].endswith("_bd")]
    if not pick:
        sys.exit("no clip matches %s" % a.clips)
    meshes = pack_meshes(R) if a.mesh else []
    stem = os.path.splitext(os.path.basename(a.pack))[0]
    stem = stem[3:] if stem.startswith("v1_") else stem

    # the skeleton: the root chain and every bone (and, with --mesh, every node a mesh is skinned to);
    # mesh nodes and measurement marks stay behind
    skinned_to = set()
    for m in meshes:
        if m["flags"] & 4 and m["nbones"]:
            skinned_to.update(np.frombuffer(R.buf, "<u2", m["nbones"], m["oBM"]).tolist())
    keep = [i for i, n in enumerate(nodes)
            if n["kind"] == 1 or (n["kind"] == 0 and n["parent"] < 0) or i in skinned_to]
    keep_set = set(keep)
    for i in list(keep):                       # every ancestor of a bone comes too
        p = nodes[i]["parent"]
        while p >= 0 and p not in keep_set:
            keep_set.add(p); keep.append(p); p = nodes[p]["parent"]
    keep = sorted(keep_set)
    gi = {n: k for k, n in enumerate(keep)}

    G = Gltf()
    for n in keep:
        nd = nodes[n]
        e = {"name": nd["name"]}
        if any(abs(x) > 1e-9 for x in nd["t"]): e["translation"] = [float(x) for x in nd["t"]]
        if abs(nd["q"][3] - 1.0) > 1e-9 or any(abs(x) > 1e-9 for x in nd["q"][:3]): e["rotation"] = [float(x) for x in nd["q"]]
        if any(abs(x - 1.0) > 1e-9 for x in nd["s"]): e["scale"] = [float(x) for x in nd["s"]]
        kids = [gi[c] for c in keep if nodes[c]["parent"] == n]
        if kids: e["children"] = kids
        G.j["nodes"].append(e)
    roots = [gi[n] for n in keep if nodes[n]["parent"] < 0]
    G.j["scenes"][0]["nodes"] += roots

    # rest world matrices (the bind pose)
    Wr = {}
    for n in keep:
        L = trs(nodes[n]["t"], nodes[n]["q"], nodes[n]["s"])
        p = nodes[n]["parent"]
        Wr[n] = L if p < 0 else Wr[p] @ L
    bones = [n for n in keep if nodes[n]["kind"] == 1]
    jidx = {n: k for k, n in enumerate(bones)}
    ibm = np.stack([np.linalg.inv(Wr[n]).T for n in bones])          # column-major for glTF
    skin = {"name": "skeleton", "joints": [gi[n] for n in bones],
            "inverseBindMatrices": G.acc(ibm.reshape(len(bones), 16), "MAT4")}
    if len(roots) == 1: skin["skeleton"] = roots[0]
    G.j["skins"].append(skin)

    # ---- the body: the pack's own meshes, or the stick figure (one octahedron per bone, rigidly on it)
    body_line = ""
    if a.mesh:
        src = source_materials(a.materials_from, R, meshes) if a.materials_from else {}
        body_line = export_meshes(R, G, nodes, gi, keep, Wr, jidx, meshes, src, stem)
    elif not a.no_figure:
        head = {n: Wr[n][:3, 3] for n in bones}
        height = max(h[1] for h in head.values()) - min(h[1] for h in head.values())
        PP, JJ, TT, side = [], [], [], []
        for n in bones:
            kids = [c for c in bones if nodes[c]["parent"] == n]
            ends = [head[c] for c in kids]
            if not ends:                                   # a leaf: along the bone's own +Y
                ax = Wr[n][:3, 1] / max(np.linalg.norm(Wr[n][:3, 1]), 1e-9)
                lim = 0.12 if "head" in nodes[n]["name"].lower() else 0.06
                ends = [head[n] + ax * lim * height / 1.7]
            for e in ends:
                L = np.linalg.norm(e - head[n])
                P, T = octa(head[n], e, min(0.035 * height / 1.7, 0.09 * L + 0.004))
                if P is None: continue
                base = sum(len(p) for p in PP)
                PP.append(P); TT.append(T + base); JJ.append(np.full(len(P), jidx[n]))
                nm = nodes[n]["name"].lower()
                side.append(1 if ("left" in nm or nm.endswith("_l")) else (2 if ("right" in nm or nm.endswith("_r")) else 0))
        prims = []
        for sd, rgb in ((0, (0.80, 0.80, 0.78)), (1, (0.30, 0.55, 0.95)), (2, (0.95, 0.35, 0.30))):
            ks = [k for k, s in enumerate(side) if s == sd]
            if not ks: continue
            P = np.concatenate([PP[k] for k in ks]); J = np.concatenate([JJ[k] for k in ks])
            off = np.cumsum([0] + [len(PP[k]) for k in ks])
            T = np.concatenate([TT[k] - (sum(len(p) for p in PP[:k])) + off[m] for m, k in enumerate(ks)])
            V, N, I = flat_normals(P, T)
            Jv = np.repeat(J[T.ravel()], 1)
            prims.append({"attributes": {"POSITION": G.acc(V, "VEC3", minmax=True),
                                         "NORMAL": G.acc(N, "VEC3"),
                                         "JOINTS_0": G.acc(np.stack([Jv, 0 * Jv, 0 * Jv, 0 * Jv], 1), "VEC4", 5123),
                                         "WEIGHTS_0": G.acc(np.tile([1.0, 0, 0, 0], (len(V), 1)), "VEC4")},
                          "indices": G.acc(I.ravel(), "SCALAR", 5125), "material": G.material(
                              ["figure_centre", "figure_left", "figure_right"][sd], rgb)})
        G.j["meshes"].append({"name": "stick_figure", "primitives": prims})
        G.j["nodes"].append({"name": "stick_figure", "mesh": len(G.j["meshes"]) - 1, "skin": 0})
        G.j["scenes"][0]["nodes"].append(len(G.j["nodes"]) - 1)

    # ---- the board (deck top at the frame origin, +X nose, +Y up): 0.80 x 0.21 m, wheels below
    board_node = None
    if a.board:
        s = (max(Wr[n][1, 3] for n in bones) - min(Wr[n][1, 3] for n in bones)) / 1.62 if bones else 1.0
        parts = [box((-0.40 * s, -0.016 * s, -0.105 * s), (0.40 * s, 0.0, 0.105 * s))]
        wheels = [box(((wx - 0.028) * s, -0.068 * s, (wz - 0.02) * s), ((wx + 0.028) * s, -0.016 * s, (wz + 0.02) * s))
                  for wx in (-0.27, 0.27) for wz in (-0.075, 0.075)]
        prims = []
        for grp, rgb in (([parts[0]], (0.18, 0.13, 0.10)), (wheels, (0.92, 0.90, 0.82))):
            P = np.concatenate([g[0] for g in grp]); off = np.cumsum([0] + [len(g[0]) for g in grp])
            T = np.concatenate([g[1] + off[k] for k, g in enumerate(grp)])
            V, N, I = flat_normals(P, T)
            prims.append({"attributes": {"POSITION": G.acc(V, "VEC3", minmax=True), "NORMAL": G.acc(N, "VEC3")},
                          "indices": G.acc(I.ravel(), "SCALAR", 5125),
                          "material": G.material("deck" if len(grp) == 1 else "wheels", rgb)})
        G.j["meshes"].append({"name": "skateboard", "primitives": prims})
        G.j["nodes"].append({"name": "skateboard", "mesh": len(G.j["meshes"]) - 1})
        board_node = len(G.j["nodes"]) - 1
        G.j["scenes"][0]["nodes"].append(board_node)

    # ---- the clips
    summary = []
    for ci in pick:
        c = allclips[ci]
        F, fps = c["frames"], c["fps"]
        tracks = [(n, t, np.frombuffer(d, "<f4").reshape(F, 4 if t == 0 else 3)) for n, t, d in R.tracks(ci)]
        # does it loop? the engine's own test: every bone's first and last rotation within 20 degrees
        worst = 0.0
        for n, t, D in tracks:
            if t == 0 and nodes[n]["kind"] == 1:
                worst = max(worst, qangle(D[0], D[-1]))
        loops = worst <= 20.0 and F > 2
        FF = F + 1 if loops else F
        times = np.arange(FF, dtype=np.float64) / fps
        tin = G.acc(times.reshape(-1, 1), "SCALAR", minmax=True)
        chans, samps = [], []
        skipped = 0
        for n, t, D in tracks:
            if n not in gi or t not in (0, 1, 2):
                skipped += 1; continue
            if loops:
                D = np.concatenate([D, D[:1]])
            path = {0: "rotation", 1: "translation", 2: "scale"}[t]
            samps.append({"input": tin, "output": G.acc(D, "VEC4" if t == 0 else "VEC3"), "interpolation": "LINEAR"})
            chans.append({"sampler": len(samps) - 1, "target": {"node": gi[n], "path": path}})
        # ⛔ A BONE THE CLIP DOES NOT KEY IS AT REST in the club engine -- and in Blender and Godot it
        # KEEPS WHATEVER THE LAST ANIMATION LEFT IT AT. The first export passed 31 of 32 animations to
        # 5 micrometres and failed z_upright by 2.04 m: it keys a few bones, and played after the hand
        # walk its left arm stayed in the air. Every bone gets a track in every animation: the clip's
        # own, or its rest pose held (two keys, start and end).
        have = {(gi[n], {0: "rotation", 1: "translation"}.get(t)) for n, t, D in tracks if n in gi}
        tend = G.acc(np.array([[0.0], [times[-1]]]), "SCALAR", minmax=True)
        held = 0
        for n in keep:
            for path, val in (("rotation", nodes[n]["q"]), ("translation", nodes[n]["t"])):
                if (gi[n], path) in have:
                    continue
                samps.append({"input": tend, "output": G.acc(np.array([val, val], np.float64),
                                                             "VEC4" if path == "rotation" else "VEC3"),
                              "interpolation": "LINEAR"})
                chans.append({"sampler": len(samps) - 1, "target": {"node": gi[n], "path": path}})
                held += 1
        bd = cname.index(c["name"] + "_bd") if c["name"] + "_bd" in cname else -1
        if board_node is not None and bd >= 0:
            for n, t, d in R.tracks(bd):
                if n != 0 or t not in (0, 1): continue
                D = np.frombuffer(d, "<f4").reshape(allclips[bd]["frames"], 4 if t == 0 else 3)
                if len(D) != F:       # sampled at the body's phase, like the engine
                    idx = np.minimum((np.arange(F) * len(D) / F).astype(int), len(D) - 1); D = D[idx]
                if loops: D = np.concatenate([D, D[:1]])
                samps.append({"input": tin, "output": G.acc(D, "VEC4" if t == 0 else "VEC3"), "interpolation": "LINEAR"})
                chans.append({"sampler": len(samps) - 1, "target": {"node": board_node,
                                                                  "path": "rotation" if t == 0 else "translation"}})
        G.j["animations"].append({"name": c["name"], "channels": chans, "samplers": samps,
                                  "extras": {"fps": fps, "frames": F, "seamless": bool(loops),
                                             "board_channel": bool(board_node is not None and bd >= 0)}})
        summary.append((c["name"], F, fps, loops, worst, board_node is not None and bd >= 0, skipped))

    G.write(a.out)
    what = (", " + body_line) if body_line else ("" if a.no_figure else ", a stick figure")
    print("wrote %s: %d bones, %d animations%s%s" % (a.out, len(bones), len(summary), what,
          ", a skateboard" if board_node is not None else ""))
    for nm, F, fps, loops, worst, hasbd, sk in summary:
        print("   %-22s %4d f @ %2g fps  %-5s (first-last %5.1f deg)%s%s" % (nm, F, fps, "seamless" if loops else "one-way",
              worst, "  + board" if hasbd else "", ("  (%d tracks skipped)" % sk) if sk else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
