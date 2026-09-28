#!/usr/bin/env python3
"""fixfeet.py -- repair BACKWARD FEET baked into a pack's mesh (the miko disease).

    fixfeet.py <pack>            repair in place (writes <pack>.bak first)

Diagnosis that led here (2026-08-27, jacob: "this characters feet are backwards"):
miko's chest faces +Z but her foot MESH extends -Z -- the shoes were on backwards at
the source bake, visible for months to anyone who looked at a dancer's feet. The
census found the same disease on the chibi and the ninjato fighter.

The repair rotates each foot's flesh 180 degrees about a vertical axis through that
side's ANKLE, in the only frame where that is honest: each vertex is carried to rig
space through its DOMINANT bone's rest transform (restWorld x IBM -- the buffers are
NOT rig space on these packs), flipped there, and carried back:

    v' = M_b^-1 * F_side * M_b * v

Normals get the rotation part. Skinning algebra stays untouched -- at rest the foot
now shows flipped, and every animated ankle rotation poses a forward foot, because
the flip lives in the geometry the bones act on. The toe joint's PIVOT still sits at
its old spot (toe curls would hinge slightly off); no clip in the club animates toes
on these packs, and the honest fix for that is a re-ingest, not deeper surgery.
"""
import sys, struct, math
sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0] if "\\" in __file__ or "/" in __file__ else ".")
from rpack import Rpack, _m4_trs, _m4_mul

def inv_affine(m):
    a = [[m[0], m[4], m[8]], [m[1], m[5], m[9]], [m[2], m[6], m[10]]]
    det = (a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])
           - a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])
           + a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]))
    if abs(det) < 1e-12: det = 1e-12
    c = [[0]*3 for _ in range(3)]
    for i in range(3):
        for j in range(3):
            mm = [a[(i+1)%3][(j+1)%3]*a[(i+2)%3][(j+2)%3] - a[(i+1)%3][(j+2)%3]*a[(i+2)%3][(j+1)%3]]
            c[j][i] = mm[0]/det
    t = [m[12], m[13], m[14]]
    ti = [-(c[0][0]*t[0]+c[0][1]*t[1]+c[0][2]*t[2]),
          -(c[1][0]*t[0]+c[1][1]*t[1]+c[1][2]*t[2]),
          -(c[2][0]*t[0]+c[2][1]*t[1]+c[2][2]*t[2])]
    o = [0.0]*16
    o[0],o[4],o[8] = c[0][0],c[0][1],c[0][2]
    o[1],o[5],o[9] = c[1][0],c[1][1],c[1][2]
    o[2],o[6],o[10] = c[2][0],c[2][1],c[2][2]
    o[12],o[13],o[14],o[15] = ti[0],ti[1],ti[2],1.0
    return o

def main(path):
    p = Rpack(path)
    names = p.node_names()
    nodes = p.nodes()
    W = [None]*len(nodes)
    for i, nd in enumerate(nodes):
        L = _m4_trs(nd["t"], nd["q"], nd["s"])
        W[i] = L if nd["parent"] < 0 else _m4_mul(W[nd["parent"]], L)
    def famset(side):
        out = set()
        for i, n in enumerate(names):
            lo = n.lower()
            if side in lo and any(k in lo for k in ("ankle", "foot", "toe")):
                out.add(i)
        # descendants of anything found
        grew = True
        while grew:
            grew = False
            for i, nd in enumerate(nodes):
                if nd["parent"] in out and i not in out:
                    out.add(i); grew = True
        return out
    sides = {}
    for side, keys in (("L", ("left", "_l")), ("R", ("right", "_r"))):
        s = set()
        for k in keys: s |= famset(k)
        anks = [i for i in s if "ankle" in names[i].lower() or "foot" in names[i].lower()]
        if not anks or not s: continue
        ank = min(anks, key=lambda i: len(names[i]))
        sides[side] = (s, ank)
    assert sides, "no foot bones found"
    buf = bytearray(p.buf)
    open(path + ".bak", "wb").write(p.buf)
    total = 0
    for mi in range(p.nMeshes):
        rec = p.MESH.unpack_from(p.buf, p.offMeshes + mi * 60)
        verts, bc = rec[2], rec[4]
        offPos, offNorm, offJ, offWt = rec[6], rec[7], rec[9], rec[10]
        offBM, offIBM = rec[12], rec[13]
        if not bc: continue
        bm = struct.unpack_from("<%dH" % bc, p.buf, offBM)
        M = {}
        for b in range(bc):
            ibm = struct.unpack_from("<16f", p.buf, offIBM + b * 64)
            M[b] = _m4_mul(W[bm[b]], list(ibm))
        jnt = p.buf[offJ: offJ + verts * 4]
        wgt = p.buf[offWt: offWt + verts * 4]
        for side, (fam, ank) in sides.items():
            aw = _m4_mul(W[ank], [1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1])
            ax, ay, az = W[ank][12], W[ank][13], W[ank][14]
            def F(v):   # 180 about vertical through the ankle, rig space
                return [2*ax - v[0], v[1], 2*az - v[2]]
            def Frot(n):
                return [-n[0], n[1], -n[2]]
            for v in range(verts):
                dom, dw = -1, 0
                for k in range(4):
                    w9 = wgt[v*4+k]
                    if w9 > dw: dw, dom = w9, jnt[v*4+k]
                if dom < 0 or bm[dom] not in fam: continue
                Mb = M[dom]; Mi = inv_affine(Mb)
                x, y, z = struct.unpack_from("<3f", buf, offPos + v * 12)
                r = [Mb[0]*x+Mb[4]*y+Mb[8]*z+Mb[12], Mb[1]*x+Mb[5]*y+Mb[9]*z+Mb[13], Mb[2]*x+Mb[6]*y+Mb[10]*z+Mb[14]]
                r = F(r)
                nv = [Mi[0]*r[0]+Mi[4]*r[1]+Mi[8]*r[2]+Mi[12], Mi[1]*r[0]+Mi[5]*r[1]+Mi[9]*r[2]+Mi[13], Mi[2]*r[0]+Mi[6]*r[1]+Mi[10]*r[2]+Mi[14]]
                struct.pack_into("<3f", buf, offPos + v * 12, *nv)
                if offNorm:
                    nx, ny, nz = struct.unpack_from("<3f", buf, offNorm + v * 12)
                    rn = [Mb[0]*nx+Mb[4]*ny+Mb[8]*nz, Mb[1]*nx+Mb[5]*ny+Mb[9]*nz, Mb[2]*nx+Mb[6]*ny+Mb[10]*nz]
                    rn = Frot(rn)
                    nn = [Mi[0]*rn[0]+Mi[4]*rn[1]+Mi[8]*rn[2], Mi[1]*rn[0]+Mi[5]*rn[1]+Mi[9]*rn[2], Mi[2]*rn[0]+Mi[6]*rn[1]+Mi[10]*rn[2]]
                    struct.pack_into("<3f", buf, offNorm + v * 12, *nn)
                total += 1
    open(path, "wb").write(buf)
    print("%s: flipped %d foot vertices (backup at .bak)" % (path, total))

if __name__ == "__main__":
    main(sys.argv[1])
