#!/usr/bin/env python3
"""glb_morphs.py -- carry a GLB's MORPH TARGETS into an rpack (rpack v3, H6).

    glb_morphs.py <glb> <pack> [--only a,b,c] [--min-delta 1e-5] [--dry]

⭐ WHY: the club could already BLEND morphs (morphBlend, header slot 14, clip track type 3)
and could only ever AUTHOR them -- morphs.py grows a breath by pushing a chest band along
its normals. Nothing read the targets an artist had already sculpted. Anima's HiPoly body
arrives with a complete ARKit / Character Creator face -- 107 shape keys: the six CC
visemes, eight brow shapes, per-eye look directions, per-eye blinks, jawOpen, smile and
frown -- and every one of them was being thrown away at the door.

⛔ glTF REPEATS EVERY TARGET ON EVERY PRIMITIVE. A mesh split by material carries the whole
morph list on each piece, so Anima's 15 body primitives x 28 kept shapes is 420 nominal
targets. Nearly all are EMPTY: a viseme does not move her leg, it just has an accessor full
of zeros there. So a pair is written only when it actually displaces something, which is
what keeps the count inside MORPH_MAX instead of blowing through it with nulls.

⛔ AND THE BAKER REORDERS MESHES. glb2rpack_anim writes one rpack mesh per SKINNED
primitive, and the engine then permutes them again at load so blended meshes draw last.
A morph records a MESH INDEX, so the mapping from (glTF mesh, primitive) to rpack mesh has
to be rebuilt the same way the baker built it -- in the baker's own order, skinned
primitives only -- not assumed to be sequential.

⛔ THE DELTAS ARE IN THE GLB'S UNITS AND THE PACK IS SCALED. The bake applies --scale
(1.75 for a metric body), so a delta lifted raw from the GLB moves a face by 57% of what it
should. The factor is recovered by comparing a base position in both files rather than
passed in and hoped for.
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from morphs import add_morph
from rpack import Rpack, HDR

CT = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2),
      5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def load_glb(path):
    with open(path, "rb") as f:
        assert f.read(4) == b"glTF"
        f.read(8)
        jl, _ = struct.unpack("<II", f.read(8))
        js = json.loads(f.read(jl))
        bl, _ = struct.unpack("<II", f.read(8))
        bin_ = f.read(bl)
    return js, bin_


def accessor(js, bin_, idx):
    a = js["accessors"][idx]
    n = a["count"]
    nc = NC[a["type"]]
    fmt, sz = CT[a["componentType"]]
    if "bufferView" not in a:
        return [(0.0,) * nc] * n
    bv = js["bufferViews"][a["bufferView"]]
    off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = bv.get("byteStride") or nc * sz
    out = []
    for i in range(n):
        out.append(struct.unpack_from("<%d%s" % (nc, fmt), bin_, off + i * stride))
    return out


def main():
    glb, pack = sys.argv[1], sys.argv[2]
    only = None
    mind = 1e-5
    ren = {}
    dry = "--dry" in sys.argv
    for i, t in enumerate(sys.argv):
        if t == "--only" and i + 1 < len(sys.argv):
            only = {x.strip().lower() for x in sys.argv[i + 1].split(",") if x.strip()}
        if t == "--min-delta" and i + 1 < len(sys.argv):
            mind = float(sys.argv[i + 1])
        if t == "--rename" and i + 1 < len(sys.argv):
            # ⭐ THE ENGINE DRIVES A FACE BY CANONICAL NAME -- "jaw", "smile", "brow",
            # "blink" -- and the scene's whole face layer (speech envelope, staged brow,
            # the independent blink clocks) is written against those. An ARKit rig calls
            # them jawOpen / Mouth_Smile / Brow_Raise_Left / eyeBlinkLeft, so renaming at
            # INJECTION reuses that entire proven layer instead of teaching the engine a
            # second vocabulary.
            # ⭐ AND TWO SHAPES MAY SHARE ONE NAME ON PURPOSE: Brow_Raise_Left and
            # Brow_Raise_Right both become `brow`, eyeBlinkLeft/Right both become `blink`,
            # so one weight drives the pair -- which is what morphSetName() makes possible.
            for kv in sys.argv[i + 1].split(","):
                k, _, v = kv.partition("=")
                if k.strip() and v.strip():
                    ren[k.strip().lower()] = v.strip()

    js, bin_ = load_glb(glb)
    p = Rpack(pack)

    # ---- rebuild the baker's mesh order: every SKINNED primitive, meshes then primitives
    order = []
    nodes = js.get("nodes", [])
    for mi, m in enumerate(js.get("meshes", [])):
        skinned = any(nd.get("mesh") == mi and nd.get("skin") is not None for nd in nodes)
        if not skinned:
            continue
        for pi, prim in enumerate(m.get("primitives", [])):
            att = prim.get("attributes", {})
            if "JOINTS_0" in att and "POSITION" in att:
                order.append((mi, pi, prim, m.get("name", "mesh%d" % mi),
                              js["accessors"][att["POSITION"]]["count"]))
    # ⛔ THE BAKER SORTS PRIMITIVES BY DESCENDING VERTEX COUNT
    # (glb2rpack_anim.py: `prims.sort(key=lambda q: -q['verts'])`), so rpack mesh N is NOT
    # the Nth skinned primitive in file order. Getting this wrong does not raise -- it
    # writes every morph onto the wrong mesh, and the first symptom is a face that twitches
    # somewhere on the body. The scale check below caught it as a nonsense -0.2560.
    order.sort(key=lambda q: -q[4])
    if len(order) != p.nMeshes:
        print("!! %d skinned primitives in the GLB vs %d meshes in the pack -- "
              "the mapping would be guesswork, refusing" % (len(order), p.nMeshes))
        return 1

    # ---- the bake scale, MEASURED, not assumed
    rec0 = p.MESH.unpack_from(p.buf, p.offMeshes)
    pv0 = struct.unpack_from("<3f", p.buf, rec0[6])
    gv0 = accessor(js, bin_, order[0][2]["attributes"]["POSITION"])[0]
    scale = 1.0
    for k in range(3):
        if abs(gv0[k]) > 1e-4:
            scale = pv0[k] / gv0[k]
            break
    print("bake scale recovered from vertex 0: %.4f" % scale)
    # ⭐ AND IT IS CHECKED. The baker only multiplies raw glTF positions by --scale
    # (no mesh-node transform), so this ratio must be a clean positive constant. When the
    # primitive ordering was wrong it came back -0.2560 -- a negative "scale" is two
    # different vertices being compared, and every morph would have landed on the wrong
    # mesh silently.
    ok = True
    for k in range(3):
        if abs(gv0[k]) > 1e-4 and abs(pv0[k] / gv0[k] - scale) > 1e-3 * max(1.0, abs(scale)):
            ok = False
    if scale <= 0 or not ok:
        print("!! that is not a uniform positive scale -- the mesh mapping is wrong, "
              "refusing (glb v0 %s, pack v0 %s)" % (gv0, pv0))
        return 1

    names = js.get("meshes", [])[order[0][0]].get("extras", {}).get("targetNames", [])
    written = skipped = 0
    for rp_mesh, (mi, pi, prim, mname, _nv) in enumerate(order):
        tg = prim.get("targets", [])
        if not tg:
            continue
        tn = js["meshes"][mi].get("extras", {}).get("targetNames", names)
        for ti, t in enumerate(tg):
            nm = tn[ti] if ti < len(tn) else "morph%d" % ti
            if only and nm.lower() not in only:
                continue
            nm = ren.get(nm.lower(), nm)
            if "POSITION" not in t:
                continue
            d = accessor(js, bin_, t["POSITION"])
            sparse = [(vi, dx * scale, dy * scale, dz * scale)
                      for vi, (dx, dy, dz) in enumerate(d)
                      if abs(dx) > mind or abs(dy) > mind or abs(dz) > mind]
            if not sparse:
                # EMPTY IS THE COMMON CASE, NOT AN ERROR -- glTF gives every primitive
                # every target, so a viseme carries a zero-filled accessor on the legs.
                skipped += 1
                continue
            print("   %-28s mesh %2d (%-22s) %6d verts move"
                  % (nm, rp_mesh, mname, len(sparse)))
            if not dry:
                add_morph(pack, rp_mesh, nm, sparse)
            written += 1
    print("%d morph(s) written, %d empty pair(s) skipped" % (written, skipped))
    return 0


sys.exit(main())
