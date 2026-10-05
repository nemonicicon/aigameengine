#!/usr/bin/env python3
"""autoweights.py -- bind a mesh to its skeleton, in the house (homecoming H4).

    autoweights.py in.glb out.glb [--rigid-pieces | --hybrid-pieces]

--rigid-pieces: every index-connected piece rides one bone. --hybrid-pieces: pieces of the WELDED
mesh; a piece that spans one bone rides it rigidly, a piece that spans a joint (a limb modelled as
one tube, a torso along its spine) is smooth over exactly the bones it spans.

Distance-with-visibility weighting, Pinocchio-flavored: per vertex, candidate bones
score by distance to the bone's capsule segment; a candidate whose path from vertex to
bone is BLOCKED by the mesh is refused -- the gate Blender's auto-weights silently
lack, and exactly how a hand stops inheriting hip weights across empty space. Top 4
influences, inverse-square falloff, one Laplacian smoothing pass over the mesh graph.

The GLB must carry a skin (joints + inverse binds define the skeleton in mesh space);
JOINTS_0/WEIGHTS_0 are REPLACED, everything else survives byte-identical.
"""
import sys, json, struct, math
from collections import defaultdict
sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0] if "\\" in __file__ or "/" in __file__ else ".")
from decimate_glb import read_glb, acc_read, CTYPE, CSIZE

def node_world(gj):
    ns = gj["nodes"]
    W = {}
    def mat(n):
        if "matrix" in n:
            return list(n["matrix"])
        t = n.get("translation", [0, 0, 0]); q = n.get("rotation", [0, 0, 0, 1]); s = n.get("scale", [1, 1, 1])
        x, y, z, w = q
        m = [0.0] * 16
        m[0]=(1-2*(y*y+z*z))*s[0]; m[1]=(2*(x*y+z*w))*s[0]; m[2]=(2*(x*z-y*w))*s[0]
        m[4]=(2*(x*y-z*w))*s[1]; m[5]=(1-2*(x*x+z*z))*s[1]; m[6]=(2*(y*z+x*w))*s[1]
        m[8]=(2*(x*z+y*w))*s[2]; m[9]=(2*(y*z-x*w))*s[2]; m[10]=(1-2*(x*x+y*y))*s[2]
        m[12],m[13],m[14],m[15]=t[0],t[1],t[2],1.0
        return m
    def mul(a,b):
        o=[0.0]*16
        for c in range(4):
            for r in range(4):
                o[c*4+r]=sum(a[k*4+r]*b[c*4+k] for k in range(4))
        return o
    kids = defaultdict(list); rooted = set()
    for i, n in enumerate(ns):
        for c in n.get("children", []): kids[i].append(c); rooted.add(c)
    def walk(i, pm):
        m = mul(pm, mat(ns[i])) if pm else mat(ns[i])
        W[i] = m
        for c in kids[i]: walk(c, m)
    I = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
    for i in range(len(ns)):
        if i not in rooted: walk(i, [float(x) for x in I])
    return W

def main():
    src, dst = sys.argv[1], sys.argv[2]
    gj, bin_ = read_glb(src)
    # \u26d4 EVERY PRIMITIVE OF EVERY MESH, NOT meshes[0].primitives[0]. Binding one
    # primitive left Camilia's teeth, tongue, eyes and every limb on the zeroed default
    # (joint 0, weight 1) -- her face hung off her HIPS, which jacob saw instantly as "its
    # like the head is detatthed". Fourth instance of this same assumption in the artery.
    # \u2b50 The vertices are CONCATENATED into one array and the occluder BVH is built over
    # all of it, because the visibility gate is only meaningful against the whole body: gated
    # against its own primitive alone, the teeth see the hip joint straight through the skull.
    # That is not hypothetical -- it is how they came to be bound to it.
    prims = []
    P, tris = [], []
    for _m in gj["meshes"]:
        _mesh_lo = len(P)
        for _pr in _m.get("primitives", []):
            _a = _pr.get("attributes", {})
            if "POSITION" not in _a or "indices" not in _pr:
                continue
            _p = [list(v) for v in acc_read(gj, bin_, _a["POSITION"])]
            _i = [v[0] for v in acc_read(gj, bin_, _pr["indices"])]
            _base = len(P)
            prims.append((_pr, _a, _base, len(_p)))
            P.extend(_p)
            tris.extend(tuple(_base + _i[k] for k in range(t, t + 3))
                        for t in range(0, len(_i) - 2, 3))
    # ⛔ THE OCCLUDER SET IS PER MESH, NOT GLOBAL, AND THAT COST A ROUND. Making it
    # global to "let the teeth see they are inside the skull" refused 56616 bone candidates:
    # an upper-arm vertex was blocked from its OWN arm bones by her skin in a neighbouring
    # primitive, fell through to the nearest thing still visible, and bound to the HAND --
    # `Body 4738 verts -> LeftHand:1130`. Her arms rendered as rubber arcs.
    # ⭐ And the premise was wrong: the teeth were never MIS-GATED, they were UNBOUND,
    # still carrying statue2body's zeroed default. Binding them at all was the whole fix.
    # Grouping occluders by MESH reproduces what the single-mesh case always did -- a body's
    # material slices gate each other, and the teeth gate against the teeth.
    print("binding %d primitive(s), %d verts, %d tris"
          % (len(prims), len(P), len(tris)))
    skin = gj["skins"][0]
    joints = skin["joints"]
    W = node_world(gj)
    heads = {j: (W[j][12], W[j][13], W[j][14]) for j in joints}
    kids = defaultdict(list)
    for i, n in enumerate(gj["nodes"]):
        for c in n.get("children", []):
            if i in heads and c in heads: kids[i].append(c)
    # bone segments in mesh space: head -> mean of children. A LEAF takes its END NODE as the tail
    # (a plain child node named *End* -- statue2body writes HeadEnd / *HandEnd / *ToeEnd, Mixamo writes
    # *_End): ⛔ the old leaf rule was a 2 cm stub pointing world-UP, so a hand's claw or fist sat equally
    # far from the forearm's end and the hand's stub and was split between them by a coin toss
    # (Kanden, 10-05). Only with no end node does the stub remain.
    ends = defaultdict(list)
    for i, n in enumerate(gj["nodes"]):
        if i in heads or "mesh" in n or n.get("children"):
            continue
        if "end" not in n.get("name", "").lower():
            continue
        for p, pn in enumerate(gj["nodes"]):
            if i in pn.get("children", []) and p in heads:
                ends[p].append(i)
    segs = []
    for bi, j in enumerate(joints):
        h = heads[j]
        if kids[j]:
            t = [sum(heads[c][k] for c in kids[j]) / len(kids[j]) for k in range(3)]
        elif ends[j]:
            t = [sum(W[c][12 + k] for c in ends[j]) / len(ends[j]) for k in range(3)]
        else:
            t = [h[0], h[1] + 0.02, h[2]]
        segs.append((bi, h, t))
    _jn = [gj["nodes"][j].get("name", "?") for j in joints]
    print("leaf tails from end nodes: %s" % (", ".join("%s->%s" % (_jn[bi], gj["nodes"][ends[j][0]].get("name"))
                                                       for bi, j in enumerate(joints) if not kids[j] and ends[j]) or "none"))
    print("skeleton: %d joints, %d segments" % (len(joints), len(segs)))

    # ---- BVH over triangles for the visibility gate ----
    import random
    tcent = []
    for (a, b, c) in tris:
        tcent.append([(P[a][k]+P[b][k]+P[c][k])/3.0 for k in range(3)])
    def build(ids, depth=0):
        lo = [min(min(P[t[0]][k],P[t[1]][k],P[t[2]][k]) for t in (tris[i] for i in ids)) for k in range(3)]
        hi = [max(max(P[t[0]][k],P[t[1]][k],P[t[2]][k]) for t in (tris[i] for i in ids)) for k in range(3)]
        if len(ids) <= 8 or depth > 16:
            return (lo, hi, None, None, ids)
        ax = max(range(3), key=lambda k: hi[k]-lo[k])
        ids = sorted(ids, key=lambda i: tcent[i][ax])
        m = len(ids)//2
        return (lo, hi, build(ids[:m], depth+1), build(ids[m:], depth+1), None)
    # one BVH per MESH: a triangle occludes only vertices of the same mesh
    _tri_mesh = []
    _mesh_of_vert = {}
    _mi = 0
    _at = 0
    for _m in gj["meshes"]:
        _n = 0
        for _pr in _m.get("primitives", []):
            _a = _pr.get("attributes", {})
            if "POSITION" not in _a or "indices" not in _pr:
                continue
            _c = gj["accessors"][_a["POSITION"]]["count"]
            for _v in range(_at, _at + _c):
                _mesh_of_vert[_v] = _mi
            _at += _c
            _n += 1
        _mi += 1
    _by_mesh = defaultdict(list)
    for _ti, _t in enumerate(tris):
        _by_mesh[_mesh_of_vert.get(_t[0], -1)].append(_ti)
    bvh_of = {k: build(v) for k, v in _by_mesh.items() if v}
    bvh = None

    def ray_aabb(o, d, lo, hi):
        t0, t1 = 0.0, 1.0
        for k in range(3):
            if abs(d[k]) < 1e-12:
                if o[k] < lo[k] or o[k] > hi[k]: return False
            else:
                inv = 1.0/d[k]
                ta, tb = (lo[k]-o[k])*inv, (hi[k]-o[k])*inv
                if ta > tb: ta, tb = tb, ta
                t0, t1 = max(t0, ta), min(t1, tb)
                if t0 > t1: return False
        return True
    def ray_tri(o, d, a, b, c):
        e1 = [b[k]-a[k] for k in range(3)]; e2 = [c[k]-a[k] for k in range(3)]
        pv = [d[1]*e2[2]-d[2]*e2[1], d[2]*e2[0]-d[0]*e2[2], d[0]*e2[1]-d[1]*e2[0]]
        det = sum(e1[k]*pv[k] for k in range(3))
        if abs(det) < 1e-12: return None
        inv = 1.0/det
        tv = [o[k]-a[k] for k in range(3)]
        u = sum(tv[k]*pv[k] for k in range(3))*inv
        if u < 0 or u > 1: return None
        qv = [tv[1]*e1[2]-tv[2]*e1[1], tv[2]*e1[0]-tv[0]*e1[2], tv[0]*e1[1]-tv[1]*e1[0]]
        v = sum(d[k]*qv[k] for k in range(3))*inv
        if v < 0 or u+v > 1: return None
        t = sum(e2[k]*qv[k] for k in range(3))*inv
        return t if 0.02 < t < 0.98 else None
    def blocked(o, target, skipv):
        d = [target[k]-o[k] for k in range(3)]
        st = [bvh_of.get(_mesh_of_vert.get(skipv, -1))]
        while st:
            nd = st.pop()
            if nd is None: continue
            lo, hi, L, R, ids = nd
            if not ray_aabb(o, d, lo, hi): continue
            if ids is None:
                st.append(L); st.append(R); continue
            for i in ids:
                a, b, c = tris[i]
                if skipv in (a, b, c): continue
                if ray_tri(o, d, P[a], P[b], P[c]) is not None:
                    return True
        return False

    def seg_close(p, h, t):
        ab = [t[k]-h[k] for k in range(3)]
        l2 = sum(v*v for v in ab) or 1e-12
        u = max(0.0, min(1.0, sum((p[k]-h[k])*ab[k] for k in range(3))/l2))
        cp = [h[k]+ab[k]*u for k in range(3)]
        return math.dist(p, cp), cp

    JN, WN = [], []
    refused = 0
    for vi, p in enumerate(P):
        scored = []
        for (bi, h, t) in segs:
            d, cp = seg_close(p, h, t)
            scored.append((d, bi, cp))
        scored.sort()
        best = scored[0][0] or 1e-6
        keep = []
        for (d, bi, cp) in scored[:6]:
            if d > best*1.6 and blocked(p, cp, vi):
                refused += 1; continue
            keep.append((d, bi))
            if len(keep) == 4: break
        s = sum(1.0/(d*d+1e-6) for d, bi in keep) or 1.0
        ws = [(bi, (1.0/(d*d+1e-6))/s) for d, bi in keep]
        while len(ws) < 4: ws.append((0, 0.0))
        JN.append([b for b, w in ws]); WN.append([w for b, w in ws])
    print("bound %d verts (visibility refused %d bone-candidates)" % (len(P), refused))

    # ⭐ --rigid-pieces (10-05): a body built of PLATES rides its bones rigidly. Each connected
    # piece of mesh (union-find over the triangles) goes whole to the bone its vertices favour, weight
    # 1, no smoothing. Noxus's trophy is 300 armour pieces: with smooth distance weights his hood took
    # some of each arm bone and the T-pose stretched it into a cape between the claws. A plate that
    # bridges a joint stays rigid (a gap, never a smear) -- what the Wii's own single-bind objects did.
    if "--rigid-pieces" in sys.argv:
        parent = list(range(len(P)))
        def find(a):
            while parent[a] != a:
                parent[a] = parent[parent[a]]; a = parent[a]
            return a
        for (a, b, c) in tris:
            ra, rb, rc = find(a), find(b), find(c)
            parent[rb] = ra; parent[find(rc)] = find(ra)
        votes = defaultdict(lambda: defaultdict(float))
        for vi in range(len(P)):
            r = find(vi)
            for bi, w in zip(JN[vi], WN[vi]):
                votes[r][bi] += w
        pick = {r: max(v.items(), key=lambda kv: kv[1])[0] for r, v in votes.items()}
        JN = [[pick[find(vi)], 0, 0, 0] for vi in range(len(P))]
        WN = [[1.0, 0.0, 0.0, 0.0] for _ in range(len(P))]
        census = defaultdict(int)
        for r, b in pick.items(): census[b] += 1
        names = [gj["nodes"][j].get("name", "?") for j in joints]
        print("rigid pieces: %d pieces -> %s" % (len(pick), ", ".join("%s %d" % (names[b], n) for b, n in sorted(census.items(), key=lambda kv: -kv[1]))))

    # ⭐ --hybrid-pieces (10-05, Kanden round 2): RIGID where a piece is one plate, SMOOTH where a
    # piece is a limb that bends. Kanden's statue is 16 physical pieces, and his upper arm and forearm
    # are ONE of them: --rigid-pieces can only ride that whole arm on one bone, so a straightened
    # elbow left the forearm at its old angle; plain smooth weights let the arm bones reach into the
    # torso's shoulder pads and drag them. Here:
    #   * a piece is a connected component of the WELDED mesh -- coincident vertices merged. ⛔ The
    #     index graph splits every UV / normal seam: 414 "pieces" on Kanden, 690 on Noxus, where the
    #     bodies have 16 and 19, and --rigid-pieces rode the fragments of one plate on different bones.
    #   * per piece, the census: each vertex's NEAREST bone segment. A bone the piece SPANS owns
    #     >= 10 % of it and those vertices cover >= half the bone's length (5..95 % of their
    #     projections). A piece that spans one bone (or none) rides its top owner, weight 1: a fist,
    #     a cannon ring, a thigh, a shin -- the joint near its end does not make it bend.
    #   * a piece that spans two or more bones is a limb across a joint (the arm tube: upper arm +
    #     forearm + the clavicle at its top) or a trunk (the torso along the spine): smooth weights
    #     over THOSE bones only (inverse 4th power: the blend stays near the joint), two Laplacian
    #     passes inside the piece. The pads never see an arm bone; the arm never sees the hip.
    if "--hybrid-pieces" in sys.argv:
        import numpy as np
        Pa = np.array(P, dtype=float)
        nv = len(Pa)
        diag = float(np.linalg.norm(Pa.max(0) - Pa.min(0))) or 1.0
        parent = list(range(nv))
        def find(a):
            while parent[a] != a:
                parent[a] = parent[parent[a]]; a = parent[a]
            return a
        def union(a, b):
            ra, rb = find(a), find(b)
            if ra != rb: parent[rb] = ra
        for (a, b, c) in tris:
            union(a, b); union(a, c)
        weld = {}
        for vi, key in enumerate(map(tuple, np.round(Pa / (diag * 1e-5)).astype(np.int64))):
            if key in weld: union(weld[key], vi)
            else: weld[key] = vi
        root = np.array([find(v) for v in range(nv)])
        H = np.array([s[1] for s in segs], dtype=float); Tt = np.array([s[2] for s in segs], dtype=float)
        D = np.empty((nv, len(segs))); U = np.empty((nv, len(segs)))
        for si in range(len(segs)):
            ab = Tt[si] - H[si]; l2 = float(ab @ ab) or 1e-12
            u = np.clip((Pa - H[si]) @ ab / l2, 0.0, 1.0)
            D[:, si] = np.linalg.norm(Pa - (H[si] + u[:, None] * ab), axis=1); U[:, si] = u
        near = D.argmin(1)
        names = [gj["nodes"][j].get("name", "?") for j in joints]
        wadj = defaultdict(set)
        for (a, b, c) in tris:
            wadj[a] |= {b, c}; wadj[b] |= {a, c}; wadj[c] |= {a, b}
        by_root = defaultdict(list)
        for vi in range(nv): by_root[root[vi]].append(vi)
        # welded neighbours: coincident copies share their neighbourhoods
        twins = defaultdict(list)
        for vi, key in enumerate(map(tuple, np.round(Pa / (diag * 1e-5)).astype(np.int64))): twins[key].append(vi)
        for grp in twins.values():
            if len(grp) > 1:
                allnb = set().union(*(wadj[v] for v in grp))
                for v in grp: wadj[v] = allnb - {v}
        JN2, WN2 = [None] * nv, [None] * nv
        report = []
        for r, vs in sorted(by_root.items(), key=lambda kv: -len(kv[1])):
            vs = np.array(vs); n = len(vs)
            cnt = np.bincount(near[vs], minlength=len(segs))
            spans = []
            for si in np.argsort(-cnt):
                if cnt[si] < max(1, 0.10 * n): break
                uu = U[vs[near[vs] == si], si]
                if np.percentile(uu, 95) - np.percentile(uu, 5) >= 0.5: spans.append(int(si))
            top = int(np.argmax(cnt))
            if len(spans) <= 1:
                b = spans[0] if spans else top
                for v in vs: JN2[v], WN2[v] = [b, 0, 0, 0], [1.0, 0.0, 0.0, 0.0]
                report.append((n, 'rigid %s' % names[b]))
                continue
            sp = np.array(spans)
            w = 1.0 / (D[np.ix_(vs, sp)] ** 4 + 1e-10)
            w /= w.sum(1, keepdims=True)
            loc = {v: i for i, v in enumerate(vs)}
            for _pass in range(2):
                w2 = w.copy()
                for i, v in enumerate(vs):
                    nb = [loc[x] for x in wadj[v] if x in loc]
                    if nb: w2[i] = 0.6 * w[i] + 0.4 * w[nb].mean(0)
                w = w2 / w2.sum(1, keepdims=True)
            for i, v in enumerate(vs):
                order = np.argsort(-w[i])[:4]
                ws = w[i][order]; ws = ws / ws.sum()
                jj = [int(sp[o]) for o in order] + [0] * (4 - len(order))
                ww = [float(x) for x in ws] + [0.0] * (4 - len(order))
                JN2[v], WN2[v] = jj, ww
            report.append((n, 'SMOOTH over %s' % '+'.join(names[s] for s in spans)))
        frag = list(range(nv))
        def ffind(a):
            while frag[a] != a:
                frag[a] = frag[frag[a]]; a = frag[a]
            return a
        for (a, b, c) in tris:
            for x in (b, c):
                ra, rx = ffind(a), ffind(x)
                if ra != rx: frag[rx] = ra
        print("hybrid pieces: %d welded pieces (the index graph alone splits them into %d fragments)"
              % (len(by_root), len(set(ffind(v) for v in range(nv)))))
        for n, what in report:
            print("  piece %5d verts: %s" % (n, what))
        JN, WN = JN2, WN2

    # one smoothing pass over the mesh graph (none for rigid pieces)
    adj = defaultdict(set)
    for (a, b, c) in tris:
        adj[a] |= {b, c}; adj[b] |= {a, c}; adj[c] |= {a, b}
    JN2, WN2 = [], []
    if "--rigid-pieces" in sys.argv or "--hybrid-pieces" in sys.argv:
        JN2, WN2 = JN, WN
    for vi in range(len(P) if "--rigid-pieces" not in sys.argv and "--hybrid-pieces" not in sys.argv else 0):
        acc = defaultdict(float)
        for bi, w in zip(JN[vi], WN[vi]): acc[bi] += w*0.6
        nn = adj[vi] or {vi}
        for nb in nn:
            for bi, w in zip(JN[nb], WN[nb]): acc[bi] += w*0.4/len(nn)
        top = sorted(acc.items(), key=lambda kv: -kv[1])[:4]
        s = sum(w for _, w in top) or 1.0
        top = [(b, w/s) for b, w in top]
        while len(top) < 4: top.append((0, 0.0))
        JN2.append([b for b, _ in top]); WN2.append([w for _, w in top])

    # ---- write JOINTS_0 / WEIGHTS_0 ----
    def append_acc(data, fmt, ctype, atype):
        off = len(bin_)
        for v in data: bin_.extend(struct.pack("<"+fmt*4, *v))
        while len(bin_) % 4: bin_.append(0)
        gj["bufferViews"].append({"buffer": 0, "byteOffset": off, "byteLength": len(bin_)-off})
        gj["accessors"].append({"bufferView": len(gj["bufferViews"])-1, "componentType": ctype,
                                "count": len(data), "type": atype})
        return len(gj["accessors"])-1
    # \u26d4 SLICE THE RESULT BACK PER PRIMITIVE. One shared accessor cannot serve them:
    # each primitive indexes its own vertex range, so it needs its own JOINTS_0/WEIGHTS_0
    # covering exactly its own count.
    for (_pr, _a, _base, _n) in prims:
        _a["JOINTS_0"] = append_acc(JN2[_base:_base + _n], "B", 5121, "VEC4")
        _a["WEIGHTS_0"] = append_acc(WN2[_base:_base + _n], "f", 5126, "VEC4")
    gj["buffers"][0]["byteLength"] = len(bin_)
    jb = json.dumps(gj, separators=(",", ":")).encode()
    while len(jb) % 4: jb += b" "
    out = bytearray()
    out += b"glTF" + struct.pack("<II", 2, 12+8+len(jb)+8+len(bin_))
    out += struct.pack("<I", len(jb)) + b"JSON" + jb
    out += struct.pack("<I", len(bin_)) + b"BIN\x00" + bytes(bin_)
    open(dst, "wb").write(out)
    print("wrote %s" % dst)

if __name__ == "__main__":
    main()
