#!/usr/bin/env python3
"""jaw.py -- GROW A JAW BONE on a baked pack and skin the lower face to it.

    jaw.py <pack> --report          measure and print, write nothing
    jaw.py <pack>                   rig + bind it in place (writes a .jaw.bak first)

jacob: "do we have any models with working mouths, skeleton and all, so you can learn
how it should work?" We do, and this is what it taught.

THE REFERENCE is `v1_crowd_goblin.rpack`, a Character Creator head with a real facial
rig: CC_Base_JawRoot at the hinge, carrying CC_Base_Teeth02 and a Tongue01→03 chain,
with CC_Base_UpperJaw → CC_Base_Teeth01 staying put. The lesson is that a jaw is a
BONE with the lower face skinned to it -- not a morph. A bone works at any mesh
density, cannot be mis-banded by a percentile, and gives the teeth and tongue
something to ride.

⛔ THE GOBLIN'S GEOMETRY CANNOT BE COPIED, only its skeleton. Its face bones carry
ZERO skin weights in the club's bake and the teeth/tongue meshes did not survive the
merge, so "there is geometry inside the mouth" is true of the CC source and NOT of the
pack on disk. Nothing can be lifted from it; what transfers is the LAYOUT, and only in
BONE space -- its mesh buffer and its bone worlds are in different frames (its head
verts read z≈82 while its head bone reads z≈2.9), so every mesh-derived ratio off that
pack is nonsense. Bone-to-bone ratios are the only honest thing it has to give:

    hinge      0.495 of the eye-to-lip span ABOVE the lip line, 0.49 IOD behind it
    lower teeth  0.186 span below the lip line      upper teeth  0.186 span above
    tongue root  0.192 span below, tongue tip 0.696 span below and further forward

THE WEIGHT BAND comes from a different pack, because the goblin has none:
`v1_crowd_rabbit.rpack` is the club's ONLY body with a jaw bone that is actually
skinned (jaw_07, 805 vertices). Measured, its band is full out at the far chin, falls
off toward the hinge and above it, tapers laterally to about a quarter at the head's
own half-width, and -- the part worth copying -- is 46% GRADIENT. A jaw is a soft band,
not a hard cut.

The write is fingers.py's: append the node, extend the bone map and the inverse binds,
append the joints and weights, repoint. Existing node INDICES never move, so every
clip, morph and mark in the pack stays valid. IBM = inverse(bone rest world) x mesh
node world -- checked against the pack's own stored inverse binds before it is trusted,
and the rest pose is re-skinned and asserted unmoved afterwards.
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rpack import Rpack, HDR
from authorlib import rig_map, fk_world, quantise4
from fingers import m4_trs, m4_mul, m4_inv_affine, xform, NODE, MESH

JAW_NAME = "face_jaw"

# --- the goblin's bone-space layout, in the two rulers our marks supply -------------
HINGE_UP_SPAN = 0.495     # above the lip line, in eye-to-lip spans
HINGE_BACK_IOD = 0.52     # behind the lip SURFACE, in interocular distances
# --- the rabbit's measured band --------------------------------------------------
LAT_FLOOR = 0.25          # weight left at the head's own half-width
RELEASE_SPAN = 0.55      # below the chin, over which the jaw lets go of the throat
PLATEAU = 0.62            # of lip->chin, after which the band is at FULL weight
LAT_CORE = 0.45           # of the head half-width that stays at full weight
LIP_PART = 0.45           # the share of the jaw's motion the LOWER LIP takes at once


def face(p):
    """the marks, or a clear refusal"""
    names = p.node_names()
    need = ("face_mouth", "face_eye_l", "face_eye_r", "face_chin")
    for n in need:
        assert n in names, "%s has no %s -- run face_marks.py first" % (p.path, n)
    wp, _ = fk_world(p.nodes())
    g = lambda n: list(wp[names.index(n)])
    el, er = g("face_eye_l"), g("face_eye_r")
    out = dict(mouth=g("face_mouth"), chin=g("face_chin"), eyeL=el, eyeR=er,
               iod=math.dist(el, er))
    out["eyeY"] = 0.5 * (el[1] + er[1])
    out["span"] = out["eyeY"] - out["mouth"][1]
    if "face_mouth_l" in names and "face_mouth_r" in names:
        out["hw"] = 0.5 * math.dist(g("face_mouth_l"), g("face_mouth_r"))
    else:
        out["hw"] = 0.25 * out["iod"]
    return out


def jaw_weight(P, F, head_w, xw, fwd_sign):
    """The band, as WEIGHTS. Zero at and above the lip line, full at the chin,
    released before the throat, tapering laterally -- the shape face_morphs.py solved
    for deltas and the rabbit's real jaw confirms."""
    lip_y, chin_y = F["mouth"][1], F["chin"][1]
    if chin_y >= lip_y - 1e-9: return 0.0
    rel_y = chin_y - RELEASE_SPAN * F["span"]
    y = P[1]
    if y >= lip_y: return 0.0
    # ⛔ THE LOWER LIP MUST RIDE THE JAW. A band that is zero AT the lip line leaves her
    # lips perfectly still while the chin swings away beneath them, so the mouth appears
    # to open somewhere under her face -- jacob: "revas mouth is messed up". The old
    # jaw MORPH was carrying the lip (its LIP_PART share) and nothing replaced that when
    # the morph was switched off in favour of this bone.
    # A MOUTH IS AN ELLIPSE, NOT A LETTERBOX: the share tapers to zero at the corners,
    # so the lips part in the middle and stay anchored where they meet.
    # THE PLATEAU. A ramp that only touches 1.0 exactly at the chin line leaves the
    # whole band a gradient (92% of it on reva) and the jaw reads as mush -- every
    # vertex partly following the head. The rabbit's real band is 46% gradient with a
    # solid full-weight core, so the ramp finishes EARLY and holds.
    hw = F.get("hw") or (0.25 * F["iod"])
    corner = math.cos(min(1.0, abs(P[0] - F["mouth"][0]) / max(1e-9, hw)) * math.pi / 2.0)
    full_y = lip_y - PLATEAU * (lip_y - chin_y)
    if y >= full_y:
        t = (lip_y - y) / max(1e-9, lip_y - full_y)
        t = t * t * (3.0 - 2.0 * t)                  # smoothstep, no crease at the lips
        w = LIP_PART * corner + (1.0 - LIP_PART) * t
    elif y >= chin_y: w = 1.0
    else:             w = max(0.0, 1.0 - (chin_y - y) / max(1e-9, chin_y - rel_y))
    # lateral: the rabbit's profile -- a FLAT CORE across the front of the jaw, then a
    # taper to a quarter at the head's own half-width so the mandible's sides still
    # swing. A taper that starts at the midline leaves nothing at full weight (a cosine
    # from x=0 means only the centre line commits), which is the same mush the vertical
    # ramp had.
    t = min(1.0, abs(P[0] - F["mouth"][0]) / max(1e-9, xw))
    t = max(0.0, (t - LAT_CORE) / max(1e-9, 1.0 - LAT_CORE))
    w *= LAT_FLOOR + (1.0 - LAT_FLOOR) * math.cos(t * math.pi / 2.0)
    # nothing BEHIND the head bone moves: that is the back of the skull and the nape
    if (P[2] - head_w[2]) * fwd_sign < 0.0: w *= 0.0
    return max(0.0, min(1.0, w))


def build(path, report=False, hinge_back=HINGE_BACK_IOD, hinge_up=HINGE_UP_SPAN):
    p = Rpack(path)
    names = p.node_names()
    if JAW_NAME in names:
        print("jaw: %s already carries %s" % (os.path.basename(path), JAW_NAME))
        return 0
    RM = rig_map(p)
    nodes = p.nodes()
    W = []
    for i, nd in enumerate(nodes):
        loc = m4_trs(nd["t"], nd["q"], nd["s"])
        W.append(loc if nd["parent"] < 0 else m4_mul(W[nd["parent"]], loc))
    head = RM["head"]
    F = face(p)
    fwd_sign = 1.0 if F["mouth"][2] >= W[head][14] else -1.0
    hinge = [F["mouth"][0],
             F["mouth"][1] + hinge_up * F["span"],
             F["mouth"][2] - fwd_sign * hinge_back * F["iod"]]
    SP = p.skinned_positions(with_bones=True)
    # the head family owns the flesh that may be re-bound at all
    fam = {head}
    grew = True
    while grew:
        grew = False
        for i, nd in enumerate(nodes):
            if nd["parent"] in fam and i not in fam:
                fam.add(i); grew = True
    if nodes[head]["parent"] >= 0: fam.add(nodes[head]["parent"])   # the lower face is NECK-weighted
    xs = sorted(abs(v[0] - F["mouth"][0]) for v in SP if v[3] in fam)
    xw = xs[int(len(xs) * 0.95)] if xs else F["iod"]

    print("jaw: %s  IOD %.4f  eye-to-lip span %.4f  head half-width %.4f"
          % (os.path.basename(path), F["iod"], F["span"], xw))
    print("   lip line y %.4f z %.4f | chin y %.4f | hinge (%.4f, %.4f, %.4f)"
          % (F["mouth"][1], F["mouth"][2], F["chin"][1], hinge[0], hinge[1], hinge[2]))
    print("   hinge sits %.4f above the lips and %.4f behind them (%.2f IOD)"
          % (hinge[1] - F["mouth"][1], abs(hinge[2] - F["mouth"][2]),
             abs(hinge[2] - F["mouth"][2]) / F["iod"]))

    wts = {}
    for vi, v in enumerate(SP):
        if v[3] not in fam: continue
        w = jaw_weight(v, F, W[head], xw, fwd_sign)
        if w > 0.004: wts[vi] = w
    if not wts:
        print("   NO vertices in the band -- refusing to add a bone nothing rides"); return 1
    vals = sorted(wts.values())
    full = sum(1 for w in vals if w >= 0.95)
    grad = sum(1 for w in vals if 0.05 < w < 0.95)
    print("   band: %d verts, median w %.3f, %d full, %d gradient (%.0f%% -- the rabbit's is 46%%)"
          % (len(wts), vals[len(vals) // 2], full, grad, 100.0 * grad / len(wts)))
    # what the CHIN will actually travel, in world units, at 20 degrees
    ang = math.radians(20.0)
    dy = F["chin"][1] - hinge[1]; dz = F["chin"][2] - hinge[2]
    ty = hinge[1] + dy * math.cos(ang) - dz * math.sin(ang)
    tz = hinge[2] + dy * math.sin(ang) + dz * math.cos(ang)
    trav = math.hypot(ty - F["chin"][1], tz - F["chin"][2])
    print("   at 20 deg the CHIN travels %.4f rig = %.3f world (scale %.4f)"
          % (trav, trav * p_scale(p), p_scale(p)))
    if report:
        return 0

    # ---------------- the write, fingers.py's ----------------
    assert p.nMeshes == 1, "this pack has %d meshes; the tool binds one skinned mesh" % p.nMeshes
    rec = list(MESH.unpack_from(p.buf, p.offMeshes))
    (mName, mNode, verts, idxCount, boneCount, flags, offPos, offNorm, offUV,
     offJ, offWt, offIdx, offBM, offIBM, offBind) = rec
    bm = list(struct.unpack_from("<%dH" % boneCount, p.buf, offBM))
    # ---------------- RECOVER the mesh frame; do not assume it ----------------
    # IBM[b] = inv(W[bone_b]) . M  =>  M = W[bone_b] . IBM[b]. An earlier version
    # assumed M was W[meshNode] and asserted every bone agreed, which refused
    # v1_crowd_susan outright (worst 4.33e+01). She is not broken: her rest pose
    # reconstructs through her own inverse binds to a near-constant 6.7e-3 across the
    # whole face -- a uniform offset, not a deformation -- because her head was bound in
    # a frame ~7mm off identity and her HANDS were bound somewhere else again.
    # The jaw takes its weight from the HEAD, so the head's frame is the only one it has
    # to agree with. Recover it there and require the head's own chain to concur.
    hb = [b2 for b2 in range(boneCount) if bm[b2] == head]
    assert hb, "the head is not a skinning bone in this pack; nothing to hang a jaw on"
    def ibm_of(b2):
        return list(struct.unpack_from("<16f", p.buf, offIBM + b2 * 64))
    MW = m4_mul(W[bm[hb[0]]], ibm_of(hb[0]))
    kin = set()
    n2 = head
    while n2 >= 0:
        kin.add(n2)
        n2 = nodes[n2]["parent"]
    chain = [b2 for b2 in range(boneCount) if bm[b2] in kin]
    spread = 0.0
    for b2 in chain:
        M2 = m4_mul(W[bm[b2]], ibm_of(b2))
        spread = max(spread, max(abs(M2[k] - MW[k]) for k in range(16)))
    print("   mesh frame recovered from the head chain (%d bones): spread %.2e, "
          "offset from W[meshNode] (%.4f, %.4f, %.4f)"
          % (len(chain), spread, MW[12] - W[mNode][12], MW[13] - W[mNode][13],
             MW[14] - W[mNode][14]))
    assert spread < 1e-3, ("the head's own chain disagrees about the bind frame; "
                           "refusing to guess")
    assert boneCount + 1 <= 64, "bone map would exceed the engine's MAXB of 64"

    out = bytearray(p.buf)
    def place(b, align=4):
        while len(out) % align: out.append(0)
        off = len(out); out.extend(b); return off
    strs = p.strings()
    jawIdx = p.nNodes
    hw4 = m4_inv_affine(W[head])
    local = xform(hw4, hinge)
    nid = len(strs); strs.append(JAW_NAME.encode())
    nodeBlob = bytearray(p.buf[p.offNodes: p.offNodes + p.nNodes * 52])
    nodeBlob += NODE.pack(nid, head, 1, local[0], local[1], local[2],
                          0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0)
    Wjaw = m4_mul(W[head], m4_trs(local, [0.0, 0.0, 0.0, 1.0], [1.0] * 3))

    newMap = bm + [jawIdx]
    slot = len(bm)                       # the jaw's index INSIDE the bone map
    jnt = bytearray(p.buf[offJ: offJ + verts * 4])
    wgt = bytearray(p.buf[offWt: offWt + verts * 4])
    touched = 0
    for vi, w in wts.items():
        # keep the vertex's existing influences, scaled down, and give the jaw w.
        items = []
        for k in range(4):
            ww = wgt[vi * 4 + k]
            if ww: items.append((jnt[vi * 4 + k], ww / 255.0 * (1.0 - w)))
        items.append((slot, w))
        items = [e for e in items if e[1] > 1e-4]
        items.sort(key=lambda e: -e[1])
        items = items[:4]
        for s, (b, q) in enumerate(quantise4(items)):
            jnt[vi * 4 + s] = b; wgt[vi * 4 + s] = q
        touched += 1

    newOffNodes = place(bytes(nodeBlob))
    newOffBM = place(struct.pack("<%dH" % len(newMap), *newMap))
    ibm = bytearray(p.buf[offIBM: offIBM + boneCount * 64])
    ibm += struct.pack("<16f", *m4_mul(m4_inv_affine(Wjaw), MW))
    newOffIBM = place(bytes(ibm))
    newOffJ = place(bytes(jnt))
    newOffWt = place(bytes(wgt))
    tbl = struct.pack("<I", len(strs)); so = 0
    for s in strs:
        tbl += struct.pack("<II", so, len(s)); so += len(s)
    tbl += b"".join(strs)
    newOffStr = place(tbl)

    rec[4] = len(newMap); rec[9] = newOffJ; rec[10] = newOffWt
    rec[12] = newOffBM; rec[13] = newOffIBM
    MESH.pack_into(out, p.offMeshes, *rec)
    h = list(HDR.unpack_from(out, 4))
    h[1] = p.nNodes + 1; h[6] = newOffNodes; h[11] = newOffStr; h[13] = len(out)
    HDR.pack_into(out, 4, *h)
    # THE GATE RUNS BEFORE THE PACK IS REPLACED. Writing first and asserting after
    # leaves the broken pack on disk and the operator has to know to restore the .bak --
    # exactly what happened here on kaeli's first run. fingers.py now does the same.
    tmp = path + ".tmpjaw"
    open(tmp, "wb").write(out)
    try:
        q = Rpack(tmp)
        before = p.skinned_positions(); after = q.skinned_positions()
        worst = max(math.dist(before[i], after[i]) for i in range(len(before)))
        print("   %d vertices rebound, bone map %d -> %d, nodes %d -> %d"
              % (touched, boneCount, len(newMap), p.nNodes, q.nNodes))
        print("   REST-POSE GATE: worst vertex moved %.3e rig units" % worst)
        assert worst < 1e-4, "rigging moved the body at rest; the inverse bind is wrong"
    except Exception:
        os.remove(tmp)
        print("   GATE FAILED -- %s is untouched" % os.path.basename(path))
        raise
    if not os.path.exists(path + ".jaw.bak"):
        open(path + ".jaw.bak", "wb").write(p.buf)
    os.replace(tmp, path)
    print("jaw: %s written (backup at .jaw.bak)" % path)
    return 0


def p_scale(p):
    """the engine's own sizing: every club body is scaled to 3.45 units tall"""
    nodes = p.nodes()
    wp, _ = fk_world(nodes)
    bones = [i for i in range(len(nodes)) if nodes[i]["kind"] == 1]
    if not bones: return 1.0
    lo = min(wp[i][1] for i in bones); hi = max(wp[i][1] for i in bones)
    return 3.45 / max(1e-6, (hi - lo)) if hi > lo else 1.0


if __name__ == "__main__":
    a = sys.argv[1:]
    def arg(nm, d):
        return float(a[a.index(nm) + 1]) if nm in a else d
    sys.exit(build(a[0], "--report" in a,
                   arg("--back", HINGE_BACK_IOD), arg("--up", HINGE_UP_SPAN)))
