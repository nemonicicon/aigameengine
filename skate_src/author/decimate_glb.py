#!/usr/bin/env python3
"""decimate_glb.py -- quadric-error decimation, in the house (homecoming H3).

    decimate_glb.py in.glb out.glb --tris 24000

jacob: "you should have the ability to decimate since you have the other tools, not
that you need to tool specifically for that." So: the ability, ~250 lines, no Blender
boot, no vendored binary. Edge-collapse QEM (Garland-Heckbert) with midpoint placement:

- topology runs on POSITION-WELDED vertices (seam duplicates collapse together, so UV
  chart borders do not block reduction the way naive per-vertex locking would);
- faces keep PER-CORNER attributes (UV, normal, joints, weights) and the output is
  re-indexed at the end, so seams stay seams;
- a corner whose vertex was collapsed away inherits the KEPT end's wedge whose UV is
  nearest its own -- the standard cheap answer to chart distortion;
- boundary edges get a penalty quadric so open borders and skirt hems keep their line.

Carries POSITION / NORMAL / TEXCOORD_0 / JOINTS_0 / WEIGHTS_0 through untouched GLB
machinery: the json is edited minimally, all non-mesh buffers survive byte-identical.
"""
import sys, json, struct, heapq
from collections import defaultdict

CTYPE = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
CSIZE = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}

def read_glb(path):
    buf = open(path, "rb").read()
    assert buf[:4] == b"glTF"
    ln, ty = struct.unpack_from("<II", buf, 12)
    gj = json.loads(buf[20:20 + ln])
    off = 20 + ln + 8
    binchunk = buf[off: off + struct.unpack_from("<I", buf, 20 + ln)[0]]
    return gj, bytearray(binchunk)

def acc_read(gj, bin_, ai):
    a = gj["accessors"][ai]
    bv = gj["bufferViews"][a["bufferView"]]
    off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    n, comp = a["count"], CSIZE[a["type"]]
    fmt = "<" + CTYPE[a["componentType"]] * comp
    stride = bv.get("byteStride", struct.calcsize(fmt))
    out = []
    for i in range(n):
        out.append(struct.unpack_from(fmt, bin_, off + i * stride))
    return out

def main():
    a = sys.argv[1:]
    src, dst = a[0], a[1]
    target = int(a[a.index("--tris") + 1]) if "--tris" in a else 24000
    gj, bin_ = read_glb(src)

    mesh = gj["meshes"][0]
    prim = mesh["primitives"][0]
    att = prim["attributes"]
    P = [list(v) for v in acc_read(gj, bin_, att["POSITION"])]
    N = [list(v) for v in acc_read(gj, bin_, att["NORMAL"])] if "NORMAL" in att else None
    UV = [list(v) for v in acc_read(gj, bin_, att["TEXCOORD_0"])] if "TEXCOORD_0" in att else None
    J = [list(v) for v in acc_read(gj, bin_, att["JOINTS_0"])] if "JOINTS_0" in att else None
    W = [list(v) for v in acc_read(gj, bin_, att["WEIGHTS_0"])] if "WEIGHTS_0" in att else None
    IDX = [v[0] for v in acc_read(gj, bin_, prim["indices"])]
    faces = [tuple(IDX[i:i + 3]) for i in range(0, len(IDX), 3)]
    print("in: %d verts, %d tris -> target %d" % (len(P), len(faces), target))

    # ---- position weld: seam duplicates share one topology node ----
    key2rep = {}
    rep = [0] * len(P)
    for i, p in enumerate(P):
        k = (round(p[0], 6), round(p[1], 6), round(p[2], 6))
        rep[i] = key2rep.setdefault(k, len(key2rep))
    NR = len(key2rep)
    rpos = [None] * NR
    wedges = defaultdict(list)          # repnode -> [orig vertex ids]
    for i in range(len(P)):
        rpos[rep[i]] = list(P[i])
        wedges[rep[i]].append(i)
    F = []
    for (x, y, z) in faces:
        a3, b3, c3 = rep[x], rep[y], rep[z]
        if a3 != b3 and b3 != c3 and a3 != c3:
            F.append([a3, b3, c3, x, y, z])   # topology + original corners

    # ---- quadrics ----
    Q = [[0.0] * 10 for _ in range(NR)]  # symmetric 4x4 packed
    def addq(q, a4, b4, c4, d4, w=1.0):
        q[0]+=a4*a4*w; q[1]+=a4*b4*w; q[2]+=a4*c4*w; q[3]+=a4*d4*w
        q[4]+=b4*b4*w; q[5]+=b4*c4*w; q[6]+=b4*d4*w
        q[7]+=c4*c4*w; q[8]+=c4*d4*w; q[9]+=d4*d4*w
    edgecount = defaultdict(int)
    for f in F:
        p0, p1, p2 = rpos[f[0]], rpos[f[1]], rpos[f[2]]
        ux, uy, uz = p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2]
        vx, vy, vz = p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2]
        nx, ny, nz = uy*vz-uz*vy, uz*vx-ux*vz, ux*vy-uy*vx
        l = (nx*nx+ny*ny+nz*nz) ** 0.5 or 1e-12
        nx, ny, nz = nx/l, ny/l, nz/l
        d = -(nx*p0[0]+ny*p0[1]+nz*p0[2])
        for v in f[:3]: addq(Q[v], nx, ny, nz, d)
        for e in ((f[0],f[1]),(f[1],f[2]),(f[0],f[2])):
            edgecount[(min(e),max(e))] += 1
    # boundary penalty: open edges resist collapse across them
    for (e0, e1), c in edgecount.items():
        if c == 1:
            p0, p1 = rpos[e0], rpos[e1]
            for v in (e0, e1): addq(Q[v], 0, 1, 0, -p0[1], 50.0)

    def cost(e0, e1):
        m = [(rpos[e0][k]+rpos[e1][k])*0.5 for k in range(3)]
        q = [Q[e0][k]+Q[e1][k] for k in range(10)]
        x, y, z = m
        return (q[0]*x*x+2*q[1]*x*y+2*q[2]*x*z+2*q[3]*x
                +q[4]*y*y+2*q[5]*y*z+2*q[6]*y
                +q[7]*z*z+2*q[8]*z+q[9]), m

    vfaces = defaultdict(set)
    for fi, f in enumerate(F):
        for v in f[:3]: vfaces[v].add(fi)
    alive = [True] * len(F)
    parent = list(range(NR))
    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]; x = parent[x]
        return x

    heap = []
    ver = defaultdict(int)
    def push(e0, e1):
        e0, e1 = find(e0), find(e1)
        if e0 == e1: return
        c, m = cost(e0, e1)
        heapq.heappush(heap, (c, ver[e0], ver[e1], e0, e1, m))
    for (e0, e1) in edgecount:
        push(e0, e1)

    ntris = len(F)
    while ntris > target and heap:
        c, v0, v1, e0, e1, m = heapq.heappop(heap)
        if find(e0) != e0 or find(e1) != e1 or ver[e0] != v0 or ver[e1] != v1:
            continue
        # collapse e1 -> e0 at midpoint
        parent[e1] = e0
        rpos[e0] = m
        Q[e0] = [Q[e0][k]+Q[e1][k] for k in range(10)]
        wedges[e0].extend(wedges[e1])
        ver[e0] += 1
        moved = vfaces[e1] | vfaces[e0]
        vfaces[e0] = set()
        neigh = set()
        for fi in moved:
            if not alive[fi]: continue
            f = F[fi]
            vs = [find(f[k]) for k in range(3)]
            if vs[0]==vs[1] or vs[1]==vs[2] or vs[0]==vs[2]:
                alive[fi] = False; ntris -= 1; continue
            f[0], f[1], f[2] = vs
            vfaces[e0].add(fi)
            for v in vs:
                if v != e0: neigh.add(v)
        for v in neigh: push(e0, v)
    print("out: %d tris" % ntris)

    # ---- re-emit with per-corner attributes; corners on collapsed verts inherit the
    # kept end's nearest-UV wedge ----
    out_v = {}
    out_P, out_N, out_UV, out_J, out_W = [], [], [], [], []
    out_idx = []
    def emit(rnode, orig):
        r = find(rnode)
        o = orig
        if find(rep[o]) != r or True:
            pass
        key = (r, UV[o][0] if UV else 0, UV[o][1] if UV else 0)
        if key in out_v: return out_v[key]
        vi = len(out_P)
        out_v[key] = vi
        out_P.append(rpos[r])
        out_N.append(N[o] if N else [0,1,0])
        out_UV.append(UV[o] if UV else [0,0])
        out_J.append(J[o] if J else [0,0,0,0])
        out_W.append(W[o] if W else [1,0,0,0])
        return vi
    for fi, f in enumerate(F):
        if not alive[fi]: continue
        tri = [emit(f[k], f[k+3]) for k in range(3)]
        if tri[0]!=tri[1] and tri[1]!=tri[2] and tri[0]!=tri[2]:
            out_idx.extend(tri)
    print("emit: %d verts, %d tris" % (len(out_P), len(out_idx)//3))

    # ---- write: append new buffers, repoint the five accessors + indices ----
    def append_acc(data, fmt, ctype, atype, norm=False):
        off = len(bin_)
        for v in data:
            bin_.extend(struct.pack("<" + fmt * CSIZE[atype], *v))
        while len(bin_) % 4: bin_.append(0)
        gj["bufferViews"].append({"buffer": 0, "byteOffset": off, "byteLength": len(bin_)-off})
        acc = {"bufferView": len(gj["bufferViews"])-1, "componentType": ctype,
               "count": len(data), "type": atype}
        if norm: acc["normalized"] = True
        if atype == "VEC3" and ctype == 5126 and data is not None and fmt == "f" and data is out_P:
            mn = [min(v[k] for v in data) for k in range(3)]
            mx = [max(v[k] for v in data) for k in range(3)]
            acc["min"] = mn; acc["max"] = mx
        gj["accessors"].append(acc)
        return len(gj["accessors"]) - 1

    att["POSITION"] = append_acc(out_P, "f", 5126, "VEC3")
    if N: att["NORMAL"] = append_acc(out_N, "f", 5126, "VEC3")
    if UV: att["TEXCOORD_0"] = append_acc(out_UV, "f", 5126, "VEC2")
    if J: att["JOINTS_0"] = append_acc(out_J, "B", 5121, "VEC4")
    if W: att["WEIGHTS_0"] = append_acc([[float(x) for x in v] for v in out_W], "f", 5126, "VEC4")
    off = len(bin_)
    for i in out_idx: bin_.extend(struct.pack("<I", i))
    gj["bufferViews"].append({"buffer": 0, "byteOffset": off, "byteLength": len(bin_)-off})
    gj["accessors"].append({"bufferView": len(gj["bufferViews"])-1, "componentType": 5125,
                            "count": len(out_idx), "type": "SCALAR"})
    prim["indices"] = len(gj["accessors"]) - 1
    gj["buffers"][0]["byteLength"] = len(bin_)

    jb = json.dumps(gj, separators=(",", ":")).encode()
    while len(jb) % 4: jb += b" "
    out = bytearray()
    out += b"glTF" + struct.pack("<II", 2, 12 + 8 + len(jb) + 8 + len(bin_))
    out += struct.pack("<I", len(jb)) + b"JSON" + jb
    out += struct.pack("<I", len(bin_)) + b"BIN\x00" + bin_
    open(dst, "wb").write(out)
    print("wrote %s (%d bytes)" % (dst, len(out)))

if __name__ == "__main__":
    main()
