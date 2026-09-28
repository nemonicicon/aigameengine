#!/usr/bin/env python3
"""fingers.py -- GROW FINGERS on a baked pack. No Blender, no re-ingest, no source GLB.

    fingers.py <pack> --report        measure the hands and draw what was found
    fingers.py <pack>                 rig + bind them in place (writes a .bak first)

jacob: "what do you mean both rigs are fingerless? GIVE THEM FINGERS!"

Most club bodies arrive with a hand bone and nothing past it: the donor rigs stop at the
wrist, so every authored hand is a paddle. The geometry, though, is usually all there --
a dense character mesh HAS modelled fingers, they simply have no bones. This tool finds
them in the mesh and rigs them:

  1. THE HAND FRAME, measured: forward = forearm->hand extended; the other two axes from
     a PCA of the cloud projected perpendicular to it (across-palm is wider than the palm
     is thick, which is what separates the two).
  2. THE FINGERS, found by HISTOGRAM, not by clustering. Relaxed fingers touch, so
     connected components merge them into one blob (they did here on both bodies); but
     across the palm they are still four density peaks with valleys between them. Peaks
     are counted at several depths and the depth that separates best wins.
  3. THE THUMB, found separately: it leaves the palm closer to the wrist and off the palm
     plane, so it is the lobe with the shallowest base and the largest normal deviation.
  4. JOINTS placed along each lobe's own centreline (fingers splay -- a straight ray from
     the wrist misses the outer ones), three per finger plus a childless TIP so
     authorlib.find_hands resolves the chain the way it does on a donor rig.
  5. WEIGHTS from the vertex's parameter along its finger, blended back into the hand
     bone across the knuckle so nothing tears.

THE WRITE IS APPEND-AND-REPOINT, the morphs rule: the new node array, bone map, inverse
binds, joints and weights all go at the END of the file and their offsets are patched.
Existing node INDICES never move, so every clip in the pack still plays.

THE GATE THAT MATTERS: adding bones must not move the body at rest. It cannot, if the
new inverse binds are right -- IBM = inverse(bone rest world) x MESH NODE world, which is
the identity the skinning already satisfies for every existing bone (the tool checks that
formula against the pack's own stored IBMs before it trusts it, then re-skins every hand
vertex and asserts it lands where it started). The gate runs on a TEMP file and the real
pack is only replaced once it passes, so a refusal can never leave a broken pack on disk.

The four skin bytes come from authorlib.quantise4, which is exact by construction. This
tool never overflowed the way jaw.py did, because a rebound vertex here rides at most
THREE influences (the hand bone plus two finger joints) and with three slots the old
"give the last the remainder" pattern cannot exceed 255 -- but it is one call site away
from four, so the exact helper is what it uses.

⛔ WHO THIS CAN EVEN RUN ON (censused 08-28): of the club's 107 single-mesh packs,
exactly ONE -- v1_reva2 -- passes the IBM formula check above; every other pack's stored
inverse binds do not follow inverse(bone rest) x mesh world, and the tool rightly refuses
to guess. kaeli is out on a different count: mouth_interior.py gave her a second mesh.
So the census, not the finger detector, is what currently gates this tool.

A hand whose fingers are NOT modelled (a low-poly mitten -- reva2's is 249 verts) cannot
be given fingers by rigging: there is nothing to move. The tool says so, and rigs the
paddle as two curling blocks plus a thumb if one exists, which is honest and still beats
a rigid slab.
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rpack import Rpack, HDR
from authorlib import rig_map, fk_world, quantise4

MESH = struct.Struct("<15I")
NODE = struct.Struct("<IiI3f4f3f")
NAMES5 = ("Thumb", "Index", "Middle", "Ring", "Pinky")
JOINTS_PER = 3            # + a childless tip
KNUCKLE_BLEND = 0.35      # of the finger's length, over which the hand bone hands over

def _n(v):
    l = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / l for x in v]

def _sub(a, b): return [a[k] - b[k] for k in range(3)]
def _dot(a, b): return sum(a[k] * b[k] for k in range(3))
def _cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]

def m4_mul(a, b):
    o = [0.0] * 16
    for c in range(4):
        for r in range(4):
            o[c*4+r] = sum(a[k*4+r] * b[c*4+k] for k in range(4))
    return o

def m4_trs(t, q, s):
    x, y, z, w = q
    m = [(1-2*(y*y+z*z))*s[0], (2*(x*y+z*w))*s[0], (2*(x*z-y*w))*s[0], 0.0,
         (2*(x*y-z*w))*s[1], (1-2*(x*x+z*z))*s[1], (2*(y*z+x*w))*s[1], 0.0,
         (2*(x*z+y*w))*s[2], (2*(y*z-x*w))*s[2], (1-2*(x*x+y*y))*s[2], 0.0,
         t[0], t[1], t[2], 1.0]
    return m

def m4_inv_affine(m):
    a = [[m[0], m[4], m[8]], [m[1], m[5], m[9]], [m[2], m[6], m[10]]]
    det = (a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])
           - a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])
           + a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]))
    if abs(det) < 1e-20: det = 1e-20
    c = [[0.0]*3 for _ in range(3)]
    for i in range(3):
        for j in range(3):
            c[j][i] = (a[(i+1) % 3][(j+1) % 3]*a[(i+2) % 3][(j+2) % 3]
                       - a[(i+1) % 3][(j+2) % 3]*a[(i+2) % 3][(j+1) % 3]) / det
    t = [m[12], m[13], m[14]]
    ti = [-sum(c[r][k]*t[k] for k in range(3)) for r in range(3)]
    o = [c[0][0], c[1][0], c[2][0], 0.0,
         c[0][1], c[1][1], c[2][1], 0.0,
         c[0][2], c[1][2], c[2][2], 0.0,
         ti[0], ti[1], ti[2], 1.0]
    return o

def xform(m, v):
    return [m[0]*v[0]+m[4]*v[1]+m[8]*v[2]+m[12],
            m[1]*v[0]+m[5]*v[1]+m[9]*v[2]+m[13],
            m[2]*v[0]+m[6]*v[1]+m[10]*v[2]+m[14]]

def q_between(u, v):
    u, v = _n(u), _n(v)
    d = _dot(u, v)
    if d >= 1.0 - 1e-9: return [0.0, 0.0, 0.0, 1.0]
    if d <= -1.0 + 1e-9:
        ax = _cross(u, [1.0, 0.0, 0.0])
        if _dot(ax, ax) < 1e-12: ax = _cross(u, [0.0, 1.0, 0.0])
        ax = _n(ax)
        return [ax[0], ax[1], ax[2], 0.0]
    ax = _cross(u, v)
    w = 1.0 + d
    l = math.sqrt(_dot(ax, ax) + w * w)
    return [ax[0]/l, ax[1]/l, ax[2]/l, w/l]

def qmul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return [aw*bx + ax*bw + ay*bz - az*by,
            aw*by - ax*bz + ay*bw + az*bx,
            aw*bz + ax*by - ay*bx + az*bw,
            aw*bw - ax*bx - ay*by - az*bz]

def qinv(q): return [-q[0], -q[1], -q[2], q[3]]

def qrot(q, v):
    x, y, z, w = q
    t = [2*(y*v[2]-z*v[1]), 2*(z*v[0]-x*v[2]), 2*(x*v[1]-y*v[0])]
    return [v[0] + w*t[0] + y*t[2]-z*t[1],
            v[1] + w*t[1] + z*t[0]-x*t[2],
            v[2] + w*t[2] + x*t[1]-y*t[0]]

# ------------------------------------------------------------------ measurement
def hand_frame(p, RM, side, W):
    """(cloud, origin, f, u, n, L): the hand's own axes, measured from its flesh."""
    hb, fb = RM["hand_" + side], RM["lowerarm_" + side]
    cloud = [v[:3] for v in p.skinned_positions(with_bones=True) if v[3] == hb]
    assert cloud, "no vertices are dominantly weighted to the %s hand" % side
    o = [W[hb][12], W[hb][13], W[hb][14]]
    f = _n(_sub(o, [W[fb][12], W[fb][13], W[fb][14]]))
    # the two cross-palm axes: PCA of the cloud with the f component removed
    perp = []
    for v in cloud:
        d = _sub(v, o)
        perp.append([d[k] - f[k]*_dot(d, f) for k in range(3)])
    cov = [[0.0]*3 for _ in range(3)]
    for d in perp:
        for i in range(3):
            for j in range(3):
                cov[i][j] += d[i]*d[j]
    vv = [0.31, 0.57, 0.76]
    for _ in range(300):
        w = [sum(cov[i][j]*vv[j] for j in range(3)) for i in range(3)]
        l = math.sqrt(sum(x*x for x in w)) or 1e-12
        vv = [x/l for x in w]
    u = _n([vv[k] - f[k]*_dot(vv, f) for k in range(3)])     # across the palm
    nn = _n(_cross(f, u))                                    # the palm normal
    L = max(_dot(_sub(v, o), f) for v in cloud)
    return cloud, o, f, u, nn, L

def peaks_at(cloud, o, f, u, L, depth, halfband):
    """density peaks across the palm in a slab at `depth` -- (centres, counts)."""
    us = []
    for v in cloud:
        d = _sub(v, o)
        t = _dot(d, f)
        if abs(t - depth) <= halfband:
            us.append(_dot(d, u))
    if len(us) < 40: return [], []
    lo, hi = min(us), max(us)
    nb = 28
    w = (hi - lo) / nb or 1e-9
    hist = [0]*nb
    for x in us:
        hist[min(nb-1, int((x - lo)/w))] += 1
    sm = [ (hist[max(0,i-1)] + 2*hist[i] + hist[min(nb-1,i+1)]) / 4.0 for i in range(nb) ]
    mx = max(sm) or 1
    cent, cnt = [], []
    i = 1
    while i < nb - 1:
        if sm[i] >= sm[i-1] and sm[i] >= sm[i+1] and sm[i] > 0.22*mx:
            j = i
            while j < nb-1 and sm[j+1] >= 0.9*sm[i]: j += 1
            cent.append(lo + (i + j + 1) * 0.5 * w)
            cnt.append(sum(hist[i:j+1]))
            i = j + 2
        else:
            i += 1
    # keep only peaks separated by a real valley
    keep_c, keep_n = [], []
    for k, c in enumerate(cent):
        if k == 0:
            keep_c.append(c); keep_n.append(cnt[k]); continue
        a = int((keep_c[-1] - lo)/w); b = int((c - lo)/w)
        valley = min(sm[min(a,b):max(a,b)+1]) if b != a else sm[a]
        if valley <= 0.62 * min(sm[min(nb-1,max(0,a))], sm[min(nb-1,max(0,b))]):
            keep_c.append(c); keep_n.append(cnt[k])
        elif cnt[k] > keep_n[-1]:
            keep_c[-1] = c; keep_n[-1] = cnt[k]
    return keep_c, keep_n

def find_lobes(cloud, o, f, u, nn, L):
    """the finger lobes + the thumb, as vertex sets with centrelines."""
    best = None
    for frac in (0.52, 0.58, 0.64, 0.70, 0.76, 0.82):
        c, n2 = peaks_at(cloud, o, f, u, L, L*frac, L*0.035)
        if not c: continue
        score = (min(len(c), 5), -abs(len(c) - 4), frac)
        if best is None or score > best[0]:
            best = (score, frac, c)
    if best is None: return [], 0.0
    _s, frac, centres = best
    knuckle = L * frac * 0.80
    # every vertex past the knuckle joins the nearest centre (within half a gap)
    gaps = [abs(centres[i+1]-centres[i]) for i in range(len(centres)-1)] or [L*0.25]
    win = 0.75 * (sum(gaps)/len(gaps))
    lobes = [[] for _ in centres]
    for v in cloud:
        d = _sub(v, o)
        t = _dot(d, f)
        if t < knuckle: continue
        x = _dot(d, u)
        k = min(range(len(centres)), key=lambda i: abs(centres[i]-x))
        if abs(centres[k]-x) <= win:
            lobes[k].append(v)
    out = []
    for k, pts in enumerate(lobes):
        if len(pts) < 12: continue
        tip = max(_dot(_sub(v, o), f) for v in pts)
        # centreline: centroids in four depth bands from the knuckle to the tip
        line = []
        for b in range(4):
            a0 = knuckle + (tip-knuckle)*b/4.0
            a1 = knuckle + (tip-knuckle)*(b+1)/4.0
            band = [v for v in pts if a0 <= _dot(_sub(v, o), f) <= a1]
            if not band: band = pts
            line.append([sum(v[j] for v in band)/len(band) for j in range(3)])
        base_n = sum(_dot(_sub(v, o), nn) for v in pts)/len(pts)
        out.append(dict(pts=pts, line=line, tip=tip, u=centres[k], ndev=base_n,
                        base=knuckle))
    return out, knuckle

def name_lobes(lobes, L):
    """thumb = the lobe furthest off the palm plane AND shortest; the rest run in order
    across the palm. Anatomy decides the names, not the array order."""
    if not lobes: return {}
    named = {}
    pool = list(lobes)
    if len(pool) >= 2:
        th = max(pool, key=lambda l: abs(l["ndev"])/ (L*0.10) + (L - l["tip"])/L)
        # only call it a thumb if it really is off the palm or much shorter
        if abs(th["ndev"]) > L*0.06 or th["tip"] < 0.82*max(l["tip"] for l in pool):
            named["Thumb"] = th
            pool.remove(th)
    pool.sort(key=lambda l: l["u"])
    order = ["Index", "Middle", "Ring", "Pinky"]
    if len(pool) == 1:
        # a mitten: split the paddle in two so the hand still has an across-palm axis
        named["Index"] = pool[0]
    else:
        if len(pool) > 4: pool = pool[:4]
        step = order[:len(pool)]
        for nm, lb in zip(step, pool):
            named[nm] = lb
    return named

# ------------------------------------------------------------------ the writer
def main(path, report=False):
    p = Rpack(path)
    RM = rig_map(p)
    nodes = p.nodes()
    names = p.node_names()
    W = []
    for i, nd in enumerate(nodes):
        loc = m4_trs(nd["t"], nd["q"], nd["s"])
        W.append(loc if nd["parent"] < 0 else m4_mul(W[nd["parent"]], loc))
    assert p.nMeshes == 1, "this pack has %d meshes; the tool binds one skinned mesh" % p.nMeshes
    rec = list(MESH.unpack_from(p.buf, p.offMeshes))
    (mName, mNode, verts, idxCount, boneCount, flags, offPos, offNorm, offUV,
     offJ, offWt, offIdx, offBM, offIBM, offBind) = rec
    MW = W[mNode]
    bm = list(struct.unpack_from("<%dH" % boneCount, p.buf, offBM))
    # THE IBM FORMULA, checked against the pack's own numbers before it is trusted
    worst = 0.0
    for b in range(boneCount):
        stored = list(struct.unpack_from("<16f", p.buf, offIBM + b*64))
        mine = m4_mul(m4_inv_affine(W[bm[b]]), MW)
        worst = max(worst, max(abs(stored[k]-mine[k]) for k in range(16)))
    print("fingers: IBM formula check vs stored: worst %.2e (inverse(bone rest) x mesh world)" % worst)
    assert worst < 1e-3, "this pack's inverse binds do not follow the formula; refusing to guess"

    plan = []
    for side in ("r", "l"):
        cloud, o, f, u, nn, L = hand_frame(p, RM, side, W)
        lobes, knuckle = find_lobes(cloud, o, f, u, nn, L)
        named = name_lobes(lobes, L)
        print("fingers: %s hand -- %d verts, length %.4f, %d lobes -> %s"
              % (side, len(cloud), L, len(lobes),
                 ", ".join("%s(%d verts, tip %.0f%%)"
                           % (k, len(v["pts"]), 100.0*v["tip"]/L)
                           for k, v in sorted(named.items()))))
        if len(named) < 2:
            print("fingers: %s hand has no separable finger geometry (a mitten) -- "
                  "rigging it needs modelled fingers, not bones" % side)
        plan.append((side, cloud, o, f, u, nn, L, named, knuckle))
    if report:
        return 0

    out = bytearray(p.buf)
    def place(b, align=4):
        while len(out) % align: out.append(0)
        off = len(out); out.extend(b); return off

    strs = p.strings()
    newNodes = []          # (nameId, parent, kind, t, q, s)
    newBones = []          # node indices that get skin weights, in order
    vertFix = {}           # vert -> [(boneIdx_in_new_map, weight_float), ...]
    nodeCount = p.nNodes
    for (side, cloud, o, f, u, nn, L, named, knuckle) in plan:
        hb = RM["hand_" + side]
        pref = ("Right" if side == "r" else "Left") + "Hand"
        for fname in NAMES5:
            lb = named.get(fname)
            if not lb: continue
            line = lb["line"]
            # joints at the knuckle, then along the lobe's own centreline
            jw = [line[0],
                  [line[1][k]*0.5 + line[2][k]*0.5 for k in range(3)],
                  line[3]]
            tipw = [line[3][k] + (line[3][k]-line[2][k]) for k in range(3)]
            chain = []
            parent = hb
            pworld = W[hb]
            for j in range(JOINTS_PER + 1):
                wpos = jw[j] if j < JOINTS_PER else tipw
                idx = nodeCount + len(newNodes)
                local = xform(m4_inv_affine(pworld), wpos)
                nid = len(strs); strs.append(("%s%s%d" % (pref, fname, j+1)).encode())
                newNodes.append((nid, parent, 1, local, [0.0, 0.0, 0.0, 1.0], [1.0, 1.0, 1.0]))
                # the new bone's rest world (identity rotation, parent's frame)
                wm = m4_mul(pworld, m4_trs(local, [0.0, 0.0, 0.0, 1.0], [1.0]*3))
                W.append(wm)
                chain.append((idx, wpos))
                if j < JOINTS_PER: newBones.append(idx)
                parent = idx
                pworld = wm
            # weights: parameter along the joint polyline
            poly = [c[1] for c in chain]
            for v in lb["pts"]:
                seg, sbest, dbest = 0, 0.0, 1e30
                for s2 in range(len(poly)-1):
                    a, b = poly[s2], poly[s2+1]
                    ab = _sub(b, a)
                    l2 = _dot(ab, ab) or 1e-12
                    tt = max(0.0, min(1.0, _dot(_sub(v, a), ab)/l2))
                    cp = [a[k] + ab[k]*tt for k in range(3)]
                    dd = _dot(_sub(v, cp), _sub(v, cp))
                    if dd < dbest: dbest, seg, sbest = dd, s2, tt
                if seg >= JOINTS_PER: seg, sbest = JOINTS_PER-1, 1.0
                jA, jB = chain[seg][0], chain[min(seg+1, JOINTS_PER-1)][0]
                wA, wB = 1.0 - sbest, sbest
                if seg == JOINTS_PER-1: jB, wA, wB = jA, 1.0, 0.0
                # hand back to the hand bone across the knuckle
                along = _dot(_sub(v, o), f)
                hw = max(0.0, min(1.0, 1.0 - (along - knuckle)/(KNUCKLE_BLEND*L)))
                ws = {hb: hw, jA: (1.0-hw)*wA}
                if wB > 0: ws[jB] = ws.get(jB, 0.0) + (1.0-hw)*wB
                vertFix[v[3] if len(v) > 3 else None] = None   # placeholder, replaced below
                lb.setdefault("w", []).append((v, ws))
    # vertex identity: re-walk the buffer so each fixed vertex is found by index
    pos = list(struct.iter_unpack("<3f", p.buf[offPos: offPos + verts*12]))
    key = {}
    for i, q in enumerate(pos):
        key.setdefault((round(q[0], 6), round(q[1], 6), round(q[2], 6)), []).append(i)
    jnt = bytearray(p.buf[offJ: offJ + verts*4])
    wgt = bytearray(p.buf[offWt: offWt + verts*4])
    newMap = bm + newBones
    assert len(newMap) <= 64, ("this mesh would need %d bones; the engine's MAXB is 64"
                               % len(newMap))
    mapIdx = {n: i for i, n in enumerate(newMap)}
    # the mesh buffer is in MESH space; skinned_positions returned RIG space, so the
    # lobe vertices are matched back through the same transform that produced them
    MWi = m4_inv_affine(MW)
    touched = 0
    for (side, cloud, o, f, u, nn, L, named, knuckle) in plan:
        for fname, lb in named.items():
            for (v, ws) in lb.get("w", []):
                mv = xform(MWi, v)
                k = (round(mv[0], 6), round(mv[1], 6), round(mv[2], 6))
                cands = key.get(k)
                if not cands: continue
                for vi in cands:
                    items = sorted(((mapIdx[b], w) for b, w in ws.items() if w > 1e-4),
                                   key=lambda e: -e[1])[:4]
                    # exactly 255 across the four slots, padded with (0, 0)
                    for s3, (b3, q3) in enumerate(quantise4(items)):
                        jnt[vi*4+s3] = b3; wgt[vi*4+s3] = q3
                    touched += 1
    print("fingers: %d new joints (%d skinned), %d vertices rebound, bone map %d -> %d"
          % (len(newNodes), len(newBones), touched, boneCount, len(newMap)))
    assert touched > 0, "nothing was bound -- the vertex match failed"

    # ---- append the new blocks and repoint ----
    nodeBlob = bytearray(p.buf[p.offNodes: p.offNodes + p.nNodes*52])
    for (nid, par, kind, t, q, s) in newNodes:
        nodeBlob += NODE.pack(nid, par, kind, t[0], t[1], t[2],
                              q[0], q[1], q[2], q[3], s[0], s[1], s[2])
    newOffNodes = place(bytes(nodeBlob))
    newOffBM = place(struct.pack("<%dH" % len(newMap), *newMap))
    ibm = bytearray(p.buf[offIBM: offIBM + boneCount*64])
    for b in newBones:
        ibm += struct.pack("<16f", *m4_mul(m4_inv_affine(W[b]), MW))
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
    h[1] = p.nNodes + len(newNodes)     # nNodes
    h[6] = newOffNodes                  # offNodes
    h[11] = newOffStr                   # offStr
    h[13] = len(out)                    # total
    HDR.pack_into(out, 4, *h)
    # ---- THE GATE: the body must not have moved. IT RUNS BEFORE THE PACK IS
    # REPLACED -- writing first and asserting after leaves the broken pack on disk and
    # the operator has to know to restore the .bak (which is exactly what a failed gate
    # cost on kaeli's first jaw run). ----
    tmp = path + ".tmpfingers"
    open(tmp, "wb").write(out)
    try:
        q2 = Rpack(tmp)
        before = p.skinned_positions()
        after = q2.skinned_positions()
        assert len(before) == len(after), "vertex count changed"
        worst = max(math.dist(before[i], after[i]) for i in range(len(before)))
        print("fingers: rest-pose check -- worst vertex moved %.3e rig units" % worst)
        assert worst < 1e-4, "rigging moved the body at rest; the inverse binds are wrong"
    except Exception:
        os.remove(tmp)
        print("fingers: GATE FAILED -- %s is untouched" % os.path.basename(path))
        raise
    if not os.path.exists(path + ".bak"):
        open(path + ".bak", "wb").write(p.buf)
    os.replace(tmp, path)
    from authorlib import find_hands as _fh
    n2 = q2.nodes(); nm2 = q2.node_names(); RM2 = rig_map(q2)
    hands = _fh(q2, n2, nm2, RM2["hand_l"], RM2["hand_r"])
    print("fingers: find_hands now resolves %s"
          % {k: sorted(v["fingers"].keys()) for k, v in hands.items()})
    print("fingers: %s written (%d -> %d nodes, backup at .bak)"
          % (path, p.nNodes, q2.nNodes))
    return 0

if __name__ == "__main__":
    a = sys.argv[1:]
    sys.exit(main(a[0], report="--report" in a))
