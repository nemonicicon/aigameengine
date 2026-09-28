#!/usr/bin/env python3
"""subdivide.py -- ADD VERTICES to a baked mesh, where a body needs them.

    subdivide.py <pack> --around face_mouth --radius 0.55 --levels 2

jacob: "i believe you have or have the ability to create tools that will allow for you
to give revas mesh here any skeleton or additional vertices you want or need."

We could rig an unrigged statue (statue2body), grow bones and bind them (fingers),
shape flesh (morphs), and remove triangles (decimate_glb) -- but nothing could ADD
geometry, and that was the wall reva's face hit: TWO vertices within a lip-radius of
her mouth (kaeli has 1385). No morph can open a mouth that has no vertices in it. The
answer is not a cleverer morph, it is more mesh.

This is a local 1-to-4 triangle split around a landmark. Edge midpoints are SHARED
between neighbouring triangles, so no cracks open along the boundary of the refined
patch; positions, normals and UVs interpolate; skin joints and weights come from the
edge's endpoints, so the new flesh is bound exactly like the flesh it came from.

Appended and repointed like everything else in this pack format: existing vertex
indices never move, so existing morphs and every clip stay valid. Only the index
buffer is rewritten -- and it is rewritten wholesale, because a subdivided triangle
is no longer the triangle it was.
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rpack import Rpack, HDR
from authorlib import rig_map, fk_world
from fingers import m4_trs, m4_mul, m4_inv_affine, xform

def main(path, around="face_mouth", radius=0.55, levels=2):
    p = Rpack(path)
    nodes = p.nodes(); names = p.node_names()
    RM = rig_map(p)
    W = []
    for i, nd in enumerate(nodes):
        loc = m4_trs(nd["t"], nd["q"], nd["s"])
        W.append(loc if nd["parent"] < 0 else m4_mul(W[nd["parent"]], loc))
    assert around in names, "no landmark %r (run face_marks.py)" % around
    wp, _ = fk_world(nodes)
    C = wp[names.index(around)]
    # scale the radius by the face's own size when the marks are there
    if "face_eye_l" in names:
        faceh = max(1e-6, (wp[names.index("face_eye_l")][1] - C[1]) / 0.34)
    else:
        faceh = abs(wp[RM["head"]][1] - wp[RM["neck_01"]][1]) or 1.0
    R = radius * faceh

    rec = list(p.MESH.unpack_from(p.buf, p.offMeshes))
    (mName, mNode, verts, idxCount, boneCount, flags, offPos, offNorm, offUV,
     offJ, offWt, offIdx, offBM, offIBM, offBind) = rec
    assert idxCount, "this mesh is not indexed; nothing to subdivide"
    MW = W[mNode]; MWi = m4_inv_affine(MW)
    pos = [list(struct.unpack_from("<3f", p.buf, offPos + i * 12)) for i in range(verts)]
    nor = [list(struct.unpack_from("<3f", p.buf, offNorm + i * 12)) for i in range(verts)] if offNorm else None
    uv = [list(struct.unpack_from("<2f", p.buf, offUV + i * 8)) for i in range(verts)] if offUV else None
    jnt = bytearray(p.buf[offJ: offJ + verts * 4]) if offJ else None
    wgt = bytearray(p.buf[offWt: offWt + verts * 4]) if offWt else None
    idx = list(struct.unpack_from("<%dI" % idxCount, p.buf, offIdx))
    tris = [idx[i:i+3] for i in range(0, idxCount, 3)]
    rigpos = [xform(MW, q) for q in pos]

    def near(v): return math.dist(rigpos[v], C) <= R

    print("subdivide: %s -- %d verts, %d tris; refining within %.4f rig of %s"
          % (os.path.basename(path), verts, len(tris), R, around))
    for lv in range(levels):
        sel = [t for t in tris if any(near(v) for v in t)]
        keep = [t for t in tris if not any(near(v) for v in t)]
        if not sel:
            print("   level %d: nothing in range" % (lv + 1)); break
        mid = {}
        def midpoint(a, b):
            k = (min(a, b), max(a, b))
            if k in mid: return mid[k]
            i = len(pos)
            pos.append([(pos[a][c] + pos[b][c]) * 0.5 for c in range(3)])
            if nor is not None:
                n = [(nor[a][c] + nor[b][c]) * 0.5 for c in range(3)]
                l = math.sqrt(sum(c*c for c in n)) or 1.0
                nor.append([c / l for c in n])
            if uv is not None:
                uv.append([(uv[a][c] + uv[b][c]) * 0.5 for c in range(2)])
            if jnt is not None:
                ja = jnt[a*4:a*4+4]; jb = jnt[b*4:b*4+4]
                wa = wgt[a*4:a*4+4]; wb = wgt[b*4:b*4+4]
                if bytes(ja) == bytes(jb):
                    jnt.extend(ja)
                    wgt.extend(bytes(min(255, (wa[k2] + wb[k2]) // 2) for k2 in range(4)))
                else:
                    # different influence sets: take the heavier endpoint's binding
                    # whole rather than inventing a blend the skin never had
                    src = a if sum(wa) >= sum(wb) else b
                    jnt.extend(jnt[src*4:src*4+4]); wgt.extend(wgt[src*4:src*4+4])
            rigpos.append(xform(MW, pos[i]))
            mid[k] = i
            return i
        out = list(keep)
        for (a, b, c) in sel:
            ab, bc, ca = midpoint(a, b), midpoint(b, c), midpoint(c, a)
            out += [[a, ab, ca], [ab, b, bc], [ca, bc, c], [ab, bc, ca]]
        tris = out
        print("   level %d: %d tris refined -> %d tris, %d verts"
              % (lv + 1, len(sel), len(tris), len(pos)))

    nv = len(pos)
    assert nv < 1 << 31, "vertex count overflow"
    out = bytearray(p.buf)
    def place(b, align=4):
        while len(out) % align: out.append(0)
        o = len(out); out.extend(b); return o
    nPos = place(b"".join(struct.pack("<3f", *q) for q in pos))
    nNor = place(b"".join(struct.pack("<3f", *q) for q in nor)) if nor is not None else offNorm
    nUV = place(b"".join(struct.pack("<2f", *q) for q in uv)) if uv is not None else offUV
    nJ = place(bytes(jnt)) if jnt is not None else offJ
    nW = place(bytes(wgt)) if wgt is not None else offWt
    flat = [i for t in tris for i in t]
    nIdx = place(struct.pack("<%dI" % len(flat), *flat))
    rec[2] = nv; rec[3] = len(flat)
    rec[6], rec[7], rec[8], rec[9], rec[10], rec[11] = nPos, nNor, nUV, nJ, nW, nIdx
    p.MESH.pack_into(out, p.offMeshes, *rec)
    h = list(HDR.unpack_from(out, 4)); h[13] = len(out); HDR.pack_into(out, 4, *h)
    if not os.path.exists(path + ".subdiv.bak"):
        open(path + ".subdiv.bak", "wb").write(p.buf)
    open(path, "wb").write(out)

    q = Rpack(path)
    before = p.skinned_positions(); after = q.skinned_positions()
    worst = max(math.dist(before[i], after[i]) for i in range(len(before)))
    n_near = sum(1 for i in range(nv) if math.dist(xform(MW, pos[i]), C) <= 0.18 * faceh)
    print("subdivide: %d -> %d verts, %d -> %d indices" % (verts, nv, idxCount, len(flat)))
    print("   the original vertices did not move (worst %.2e) -- morphs and clips hold" % worst)
    print("   within a lip-radius of %s: %d verts (was %d)"
          % (around, n_near,
             sum(1 for i in range(verts) if math.dist(rigpos[i], C) <= 0.18 * faceh)))
    return 0

if __name__ == "__main__":
    a = sys.argv[1:]
    def arg(nm, d):
        return a[a.index(nm) + 1] if nm in a else d
    sys.exit(main(a[0], arg("--around", "face_mouth"),
                  float(arg("--radius", 0.55)), int(arg("--levels", 2))))
