#!/usr/bin/env python3
"""dressglb.py -- put a PREMADE garment on a body, inside the GLB, ready to bake.

    python skate_src/author/dressglb.py models/_out/anima_hi_fx.glb \
        models/Female_cargo_outfit-2e0b443f/glb/converted/female_cargo_outfit.glb \
        --mesh Pants_army --kind pants --name anima_cargo~cloth --out models/_out/anima_cargo.glb

jacob (09-11): "something premade, we have not been able to successfully make any decent
clothing." So this takes a garment somebody MODELLED -- a Sketchfab/Fab GLB, unrigged, in
whatever units it came in -- and fits it to a body the club already has:

  1. the garment is taken to WORLD space through its own node chain (Sketchfab GLBs carry a
     Z-up rotation and a cm scale in the parents, and a mesh read raw is on its side);
  2. the body measures itself: the skin's joints in bind pose give the waist, the hips, the
     knees, the ankles, the chest and the neck; the body mesh gives the hip and chest width;
  3. the garment is scaled between two landmarks (pants: waist to ankle; top: neck to hip)
     and widened to the body plus a clearance, then centred on the pelvis;
  4. every garment vertex takes its TWO nearest bone SEGMENTS (inverse-square), from the
     bones that kind of garment can sit on -- makepajamas.py's rule, which is right for a
     loose garment and needs no visibility gate;
  5. the mesh is appended to the body GLB under its skin, with its base-colour texture,
     named `<name>~cloth` so the engine's solver takes it (`meshIsCloth`).

Then: glb2rpack_anim.py --keepnames [--morphs] bakes the dressed body into a new pack, and
fitcheck.py says how far the fabric sits from the skin per bone. The body GLB is never
modified in place; the output is a new file.
"""
import argparse, json, math, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from decimate_glb import read_glb, acc_read


# ------------------------------------------------------------------ small matrix helpers
def m4_mul(a, b):
    """column-major 16-float glTF matrices: a*b"""
    r = [0.0] * 16
    for c in range(4):
        for rw in range(4):
            r[c * 4 + rw] = sum(a[k * 4 + rw] * b[c * 4 + k] for k in range(4))
    return r


def m4_trs(n):
    if "matrix" in n:
        return list(n["matrix"])
    t = n.get("translation", [0, 0, 0]); q = n.get("rotation", [0, 0, 0, 1]); s = n.get("scale", [1, 1, 1])
    x, y, z, w = q
    m = [0.0] * 16
    m[0] = (1 - 2 * (y * y + z * z)) * s[0]; m[1] = (2 * (x * y + z * w)) * s[0]; m[2] = (2 * (x * z - y * w)) * s[0]
    m[4] = (2 * (x * y - z * w)) * s[1]; m[5] = (1 - 2 * (x * x + z * z)) * s[1]; m[6] = (2 * (y * z + x * w)) * s[1]
    m[8] = (2 * (x * z + y * w)) * s[2]; m[9] = (2 * (y * z - x * w)) * s[2]; m[10] = (1 - 2 * (x * x + y * y)) * s[2]
    m[12], m[13], m[14], m[15] = t[0], t[1], t[2], 1.0
    return m


def node_worlds(gj):
    """world matrix per node, walking the scene from its roots"""
    ns = gj["nodes"]
    W = [None] * len(ns)
    ident = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]

    def walk(i, parent):
        W[i] = m4_mul(parent, m4_trs(ns[i]))
        for c in ns[i].get("children", []):
            walk(c, W[i])
    roots = gj.get("scenes", [{}])[0].get("nodes", [i for i in range(len(ns))])
    for r in roots:
        walk(r, ident)
    for i in range(len(ns)):          # nodes outside the scene: identity
        if W[i] is None:
            W[i] = ident
    return W


def xf_p(m, p):
    return [m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12],
            m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]]


def xf_n(m, n):
    v = [m[0] * n[0] + m[4] * n[1] + m[8] * n[2],
         m[1] * n[0] + m[5] * n[1] + m[9] * n[2],
         m[2] * n[0] + m[6] * n[1] + m[10] * n[2]]
    l = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / l for x in v]


# ------------------------------------------------------------------ GLB writing
def write_glb(path, gj, bin_):
    js = json.dumps(gj, separators=(",", ":")).encode("utf-8")
    js += b" " * ((4 - len(js) % 4) % 4)
    bb = bytes(bin_) + b"\0" * ((4 - len(bin_) % 4) % 4)
    gj["buffers"] = [{"byteLength": len(bb)}]
    js = json.dumps(gj, separators=(",", ":")).encode("utf-8")
    js += b" " * ((4 - len(js) % 4) % 4)
    total = 12 + 8 + len(js) + 8 + len(bb)
    with open(path, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A)); f.write(js)
        f.write(struct.pack("<II", len(bb), 0x004E4942)); f.write(bb)


def add_bv(gj, bin_, data, target=None):
    while len(bin_) % 4:
        bin_.append(0)
    off = len(bin_)
    bin_.extend(data)
    bv = {"buffer": 0, "byteOffset": off, "byteLength": len(data)}
    if target:
        bv["target"] = target
    gj["bufferViews"].append(bv)
    return len(gj["bufferViews"]) - 1


def add_acc(gj, bv, ctype, atype, count, mn=None, mx=None):
    a = {"bufferView": bv, "componentType": ctype, "type": atype, "count": count}
    if mn is not None:
        a["min"] = mn; a["max"] = mx
    gj["accessors"].append(a)
    return len(gj["accessors"]) - 1


# ------------------------------------------------------------------ the garment, in world space
def garment_world(gj, bin_, sub):
    W = node_worlds(gj)
    picks = []
    for ni, n in enumerate(gj["nodes"]):
        if "mesh" not in n:
            continue
        m = gj["meshes"][n["mesh"]]
        if sub.lower() not in (m.get("name", "") + " " + n.get("name", "")).lower():
            continue
        picks.append((ni, n["mesh"]))
    if not picks:
        names = [m.get("name") for m in gj["meshes"]]
        raise SystemExit("no mesh matches %r; the garment has: %s" % (sub, names))
    if len(picks) > 1:
        print("  %d meshes match %r; taking the first: %s" % (len(picks), sub, gj["meshes"][picks[0][1]].get("name")))
    ni, mi = picks[0]
    M = W[ni]
    P, N, UV, I, mat = [], [], [], [], None
    for pr in gj["meshes"][mi]["primitives"]:
        a = pr["attributes"]
        base = len(P)
        pos = acc_read(gj, bin_, a["POSITION"])
        P.extend(xf_p(M, p) for p in pos)
        if "NORMAL" in a:
            N.extend(xf_n(M, n) for n in acc_read(gj, bin_, a["NORMAL"]))
        else:
            N.extend([0.0, 1.0, 0.0] for _ in pos)
        if "TEXCOORD_0" in a:
            UV.extend(list(u) for u in acc_read(gj, bin_, a["TEXCOORD_0"]))
        else:
            UV.extend([0.0, 0.0] for _ in pos)
        idx = [v[0] for v in acc_read(gj, bin_, pr["indices"])] if "indices" in pr else list(range(len(pos)))
        I.extend(base + k for k in idx)
        if mat is None:
            mat = pr.get("material")
    return P, N, UV, I, mat, gj["meshes"][mi].get("name")


def image_bytes(gj, bin_, mat_index, slot="baseColorTexture"):
    """a material's image (base colour by default, or its normalTexture): (bytes, mimeType) or None"""
    if mat_index is None:
        return None
    m = gj["materials"][mat_index]
    bct = m.get("pbrMetallicRoughness", {}).get(slot) if slot == "baseColorTexture" else m.get(slot)
    if not bct:
        return None
    src = gj["textures"][bct["index"]].get("source")
    if src is None:
        return None
    im = gj["images"][src]
    if "bufferView" not in im:
        return None
    bv = gj["bufferViews"][im["bufferView"]]
    off = bv.get("byteOffset", 0)
    return bytes(bin_[off: off + bv["byteLength"]]), im.get("mimeType", "image/png")


# ------------------------------------------------------------------ the body measures itself
KIND_BONES = {
    "pants": ["pelvis", "spine_01", "thigh_l", "thigh_r", "calf_l", "calf_r", "foot_l", "foot_r"],
    "top":   ["spine_01", "spine_02", "spine_03", "neck_01", "clavicle_l", "clavicle_r",
              "upperarm_l", "upperarm_r", "lowerarm_l", "lowerarm_r", "hand_l", "hand_r"],
}
SEG_CHILD = {"pelvis": "spine_01", "spine_01": "spine_02", "spine_02": "spine_03", "spine_03": "neck_01",
             "neck_01": "head", "thigh_l": "calf_l", "thigh_r": "calf_r", "calf_l": "foot_l", "calf_r": "foot_r",
             "foot_l": "ball_l", "foot_r": "ball_r", "clavicle_l": "upperarm_l", "clavicle_r": "upperarm_r",
             "upperarm_l": "lowerarm_l", "upperarm_r": "lowerarm_r", "lowerarm_l": "hand_l", "lowerarm_r": "hand_r"}


def body_measure(gj, bin_, body_sub):
    W = node_worlds(gj)
    sk = gj["skins"][0]
    jnames = {gj["nodes"][j].get("name", "").lower(): k for k, j in enumerate(sk["joints"])}
    jpos = {}
    for k, j in enumerate(sk["joints"]):
        jpos[gj["nodes"][j].get("name", "").lower()] = W[j][12:15]
    # the body's own vertices (every primitive), in world space
    body = []
    for ni, n in enumerate(gj["nodes"]):
        if "mesh" not in n:
            continue
        m = gj["meshes"][n["mesh"]]
        if body_sub.lower() not in m.get("name", "").lower():
            continue
        # ⛔ A SKINNED MESH'S VERTICES ARE ALREADY IN THE SKIN'S SPACE (the bind pose, at the
        # scene root); its node transform is ignored by glTF and must be ignored here. Applied,
        # Anima's body collapsed to 8 mm and the hip band found no vertex at all.
        skinned = "skin" in n
        for pr in m["primitives"]:
            pos = acc_read(gj, bin_, pr["attributes"]["POSITION"])
            body.extend((list(p) if skinned else xf_p(W[ni], p)) for p in pos)
    if not body:
        raise SystemExit("no body mesh matches %r" % body_sub)
    return jnames, jpos, body


def body_normals(gj, bin_, body_sub):
    """the body's vertex normals, in the same order body_measure returned its vertices"""
    out = []
    for ni, n in enumerate(gj["nodes"]):
        if "mesh" not in n:
            continue
        m = gj["meshes"][n["mesh"]]
        if body_sub.lower() not in m.get("name", "").lower():
            continue
        for pr in m["primitives"]:
            a = pr["attributes"]
            if "NORMAL" not in a:
                return []
            out.extend(list(v) for v in acc_read(gj, bin_, a["NORMAL"]))
    return out


def half_width(verts, y, band, axis=0):
    """the MEAN radius about the vertical axis through the band's centre. Not the max |x|:
    the cargo pants' waistband is 0.185 half-wide but only 0.144 in mean radius (they are
    shallower front-to-back than Anima's hips), and the max fit them 2.3 cm INSIDE her.
    fitcheck measures a mean radius per bone, so the fit is made in the same currency."""
    sel = [v for v in verts if abs(v[1] - y) < band]
    if not sel:
        return 0.0
    cx = sum(v[0] for v in sel) / len(sel); cz = sum(v[2] for v in sel) / len(sel)
    return sum(math.hypot(v[0] - cx, v[2] - cz) for v in sel) / len(sel)


def centre_xz(verts, y, band):
    sel = [v for v in verts if abs(v[1] - y) < band]
    if not sel:
        return 0.0, 0.0
    return sum(v[0] for v in sel) / len(sel), sum(v[2] for v in sel) / len(sel)


# ------------------------------------------------------------------ main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("body"); ap.add_argument("garment")
    ap.add_argument("--mesh", required=True, help="substring of the garment mesh/node name")
    ap.add_argument("--kind", choices=["pants", "top"], default="pants")
    ap.add_argument("--name", default=None, help="mesh name in the output (default <mesh>~cloth)")
    ap.add_argument("--material", default=None, help="take the base-colour texture from THIS material of the garment instead (a variant placed elsewhere in the scene)")
    ap.add_argument("--body-mesh", default="body", help="substring of the body's skin mesh")
    ap.add_argument("--ease", type=float, default=0.12, help="width clearance over the body (0.06 left the cargo waistband 1.3 cm inside Anima's hips)")
    ap.add_argument("--waist", type=float, default=0.07, help="pants: waistband height above the pelvis joint (m)")
    ap.add_argument("--hem", type=float, default=0.03, help="pants: hem height above the ankle joint (m)")
    ap.add_argument("--clear", type=float, default=0.012, help="minimum fabric-to-skin clearance at build (m)")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    gb, bb = read_glb(a.body)
    gg, bg = read_glb(a.garment)
    P, N, UV, I, mat, gname = garment_world(gg, bg, a.mesh)
    lo = [min(p[k] for p in P) for k in range(3)]; hi = [max(p[k] for p in P) for k in range(3)]
    print("garment %r: %d verts, %d tris, world bbox %s .. %s" % (gname, len(P), len(I) // 3,
          [round(x, 3) for x in lo], [round(x, 3) for x in hi]))
    if hi[1] - lo[1] < max(hi[0] - lo[0], hi[2] - lo[2]) * 0.6 and a.kind == "pants":
        print("  !! the garment's Y extent is not its tallest -- check the up axis (its node chain may not be Y-up)")

    jn, jp, body = body_measure(gb, bb, a.body_mesh)
    need = ["pelvis", "spine_01", "thigh_l", "calf_l", "foot_l", "spine_03", "neck_01"]
    missing = [n for n in need if n not in jp]
    if missing:
        raise SystemExit("the body has no joint named %s (UE names expected)" % missing)
    pel, ank, sp3, nk = jp["pelvis"], jp["foot_l"], jp["spine_03"], jp["neck_01"]
    print("body: pelvis y %.3f  ankle y %.3f  spine_03 y %.3f  neck y %.3f  (%d body verts)" % (pel[1], ank[1], sp3[1], nk[1], len(body)))

    if a.kind == "pants":
        top_y, bot_y = pel[1] + a.waist, ank[1] + a.hem
        band = (hi[1] - lo[1]) * 0.08
        g_half = half_width(P, hi[1] - band * 0.5, band, 0)
        b_half = half_width(body, pel[1] - 0.02, 0.04, 0)
        cx, cz = centre_xz(body, pel[1], 0.05)
    else:
        top_y, bot_y = nk[1] + 0.02, pel[1] + 0.02
        band = (hi[1] - lo[1]) * 0.08
        g_half = half_width(P, sp3[1], band, 2)   # depth: the sleeves make x meaningless on a shirt
        b_half = half_width(body, sp3[1], 0.04, 2)
        cx, cz = centre_xz(body, sp3[1], 0.05)
    sy = (top_y - bot_y) / max(1e-6, hi[1] - lo[1])
    sxz = (b_half * (1.0 + a.ease)) / max(1e-6, g_half)
    gcx = (lo[0] + hi[0]) * 0.5; gcz = (lo[2] + hi[2]) * 0.5
    print("fit: vertical %.3f -> %.3f m (scale %.4f), width body half %.3f vs garment half %.3f (scale %.4f), centre (%.3f, %.3f)"
          % (bot_y, top_y, sy, b_half, g_half, sxz, cx, cz))
    if sxz / sy > 1.6 or sy / sxz > 1.6:
        print("  !! width and height scales disagree by more than 1.6x -- the garment will look stretched; check the landmarks")
    Q = [[(p[0] - gcx) * sxz + cx, (p[1] - lo[1]) * sy + bot_y, (p[2] - gcz) * sxz + cz] for p in P]

    # ⭐ NOTHING STARTS INSIDE HER (jacob, 09-11: "clothing cant clip into the body, it must
    # respect the mesh"). Every garment vertex looks up its nearest BODY vertex (a grid over the
    # body, with its normals) and, if it sits under that vertex's tangent plane by less than the
    # clearance, is moved out along the normal. Three passes, because a push can bring a vertex
    # under a neighbouring vertex's plane. The solver keeps this promise per frame in the engine.
    bodyN = body_normals(gb, bb, a.body_mesh)
    if bodyN and len(bodyN) == len(body):
        cell = 0.03
        grid = {}
        for i, v in enumerate(body):
            grid.setdefault((int(v[0] // cell), int(v[1] // cell), int(v[2] // cell)), []).append(i)
        clr, rad = a.clear, 0.06
        moved_total = 0
        for it in range(12):   # ⛔ three passes left 238 inside on the waistband; a push out of one plane lands under the next
            moved = 0
            for q in Q:
                cx0, cy0, cz0 = int(q[0] // cell), int(q[1] // cell), int(q[2] // cell)
                best, bi = rad * rad, -1
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        for dz in (-1, 0, 1):
                            for i in grid.get((cx0 + dx, cy0 + dy, cz0 + dz), ()):
                                v = body[i]
                                d2 = (q[0] - v[0]) ** 2 + (q[1] - v[1]) ** 2 + (q[2] - v[2]) ** 2
                                if d2 < best:
                                    best, bi = d2, i
                if bi < 0:
                    continue
                v, n = body[bi], bodyN[bi]
                d = (q[0] - v[0]) * n[0] + (q[1] - v[1]) * n[1] + (q[2] - v[2]) * n[2]
                if d < clr:
                    for k in range(3):
                        q[k] += n[k] * (clr - d)
                    moved += 1
            moved_total += moved
            print("  push-out pass %d: %d vertices were inside the body (clearance %.3f m)" % (it + 1, moved, clr))
            if not moved:
                break
    else:
        print("  !! the body has no normals; the push-out pass is skipped and the fit may clip")

    # ---- weights: two nearest bone segments among the bones this garment can sit on
    sk = gb["skins"][0]
    segs = []
    for bn in KIND_BONES[a.kind]:
        if bn not in jp:
            continue
        a0 = jp[bn]
        ch = SEG_CHILD.get(bn)
        a1 = jp[ch] if ch in jp else [a0[0], a0[1] - 0.1, a0[2]]
        segs.append((jn[bn], a0, a1))
    if not segs:
        raise SystemExit("no bones for kind %s on this body" % a.kind)

    def seg_d(p, a0, a1):
        ex, ey, ez = a1[0] - a0[0], a1[1] - a0[1], a1[2] - a0[2]
        el = ex * ex + ey * ey + ez * ez
        t = 0.0 if el < 1e-12 else max(0.0, min(1.0, ((p[0] - a0[0]) * ex + (p[1] - a0[1]) * ey + (p[2] - a0[2]) * ez) / el))
        dx, dy, dz = p[0] - (a0[0] + ex * t), p[1] - (a0[1] + ey * t), p[2] - (a0[2] + ez * t)
        return math.sqrt(dx * dx + dy * dy + dz * dz)
    J, Wt = [], []
    for p in Q:
        ds = sorted(((seg_d(p, s[1], s[2]), s[0]) for s in segs))[:2]
        w = [1.0 / max(1e-4, d) ** 2 for d, _ in ds]
        tot = sum(w)
        J.append([ds[0][1], ds[1][1] if len(ds) > 1 else 0, 0, 0])
        Wt.append([w[0] / tot, (w[1] / tot) if len(ds) > 1 else 0.0, 0.0, 0.0])

    # ---- append to the body GLB
    gj, bin_ = gb, bb
    bvP = add_bv(gj, bin_, b"".join(struct.pack("<fff", *p) for p in Q), 34962)
    bvN = add_bv(gj, bin_, b"".join(struct.pack("<fff", *n) for n in N), 34962)
    bvU = add_bv(gj, bin_, b"".join(struct.pack("<ff", *u) for u in UV), 34962)
    bvJ = add_bv(gj, bin_, b"".join(struct.pack("<BBBB", *j) for j in J), 34962)
    bvW = add_bv(gj, bin_, b"".join(struct.pack("<ffff", *w) for w in Wt), 34962)
    bvI = add_bv(gj, bin_, b"".join(struct.pack("<I", i) for i in I), 34963)
    qlo = [min(p[k] for p in Q) for k in range(3)]; qhi = [max(p[k] for p in Q) for k in range(3)]
    accP = add_acc(gj, bvP, 5126, "VEC3", len(Q), qlo, qhi)
    accN = add_acc(gj, bvN, 5126, "VEC3", len(N))
    accU = add_acc(gj, bvU, 5126, "VEC2", len(UV))
    accJ = add_acc(gj, bvJ, 5121, "VEC4", len(J))
    accW = add_acc(gj, bvW, 5126, "VEC4", len(Wt))
    accI = add_acc(gj, bvI, 5125, "SCALAR", len(I))
    # the material: base colour only, the club's shader reads nothing else yet
    mat_index = None
    if a.material:
        hits = [i for i, m in enumerate(gg.get("materials", [])) if a.material.lower() in m.get("name", "").lower()]
        if hits:
            mat = hits[0]; print("  texture from material %r" % gg["materials"][mat].get("name"))
        else:
            print("  !! no material matches %r; using the mesh's own" % a.material)
    img = image_bytes(gg, bg, mat)
    if img:
        data, mime = img
        bvImg = add_bv(gj, bin_, data)
        gj.setdefault("images", []).append({"bufferView": bvImg, "mimeType": mime, "name": gname + "_basecolor"})
        gj.setdefault("samplers", []).append({"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497})
        gj.setdefault("textures", []).append({"source": len(gj["images"]) - 1, "sampler": len(gj["samplers"]) - 1})
        newmat = {"name": gname, "pbrMetallicRoughness": {"baseColorTexture": {"index": len(gj["textures"]) - 1}, "metallicFactor": 0.0, "roughnessFactor": 0.9}}
        # the normal map too (09-11): the baker carries it into the pack now
        nimg = image_bytes(gg, bg, mat, "normalTexture")
        if nimg:
            ndata, nmime = nimg
            bvN = add_bv(gj, bin_, ndata)
            gj["images"].append({"bufferView": bvN, "mimeType": nmime, "name": gname + "_normal"})
            gj["textures"].append({"source": len(gj["images"]) - 1, "sampler": len(gj["samplers"]) - 1})
            newmat["normalTexture"] = {"index": len(gj["textures"]) - 1}
        gj.setdefault("materials", []).append(newmat)
        mat_index = len(gj["materials"]) - 1
    else:
        print("  no base-colour texture on the garment's material; it will take a tint from its name")
    name = a.name or ("%s~cloth" % gname)
    prim = {"attributes": {"POSITION": accP, "NORMAL": accN, "TEXCOORD_0": accU, "JOINTS_0": accJ, "WEIGHTS_0": accW}, "indices": accI, "mode": 4}
    if mat_index is not None:
        prim["material"] = mat_index
    gj["meshes"].append({"name": name, "primitives": [prim]})
    gj["nodes"].append({"name": name, "mesh": len(gj["meshes"]) - 1, "skin": 0})
    gj["scenes"][0]["nodes"].append(len(gj["nodes"]) - 1)
    write_glb(a.out, gj, bin_)
    print("wrote %s: +mesh %r (%d verts, %d tris, %s), %.1f MB" % (a.out, name, len(Q), len(I) // 3,
          "textured" if mat_index is not None else "untextured", os.path.getsize(a.out) / 1e6))
    return 0


if __name__ == "__main__":
    sys.exit(main())
