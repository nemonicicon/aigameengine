#!/usr/bin/env python3
"""retarget.py -- the canonical-basis transfer (P5), offline reference implementation.

The exact algorithm the engine's donorPoseF v2 implements, in Python first so the math
is proven before it touches 400 bodies. Per canonical bone:

    A_d, A_t   canonical frames at rest (X = bone direction toward the canonical
               child, Z = bend-plane normal for limbs / rig-forward for the trunk),
               built per rig from its own geometry
    D          the donor bone's WORLD rotation delta for the posed frame
    M          A_t * inv(A_d)  -- the alignment between the two rigs' bone frames
    W_t'       M * D * inv(M) * W_t_rest  -- the target bone's posed world rotation
    L_t'       inv(W_parent') * W_t'      -- recovered local, canonical parents first

Rotation-only, like the runtime it mirrors. See design/animation_authoring.md 3.6.
"""
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from authorlib import fk_world, qmul, qinv, qnorm

NCANON = 16

def canon_of(name):
    n = "".join(c for c in name.lower() if c not in ": .")
    if n.startswith("mixamorig"): n = n[9:]
    while n and (n[-1].isdigit() or n[-1] == "_"): n = n[:-1]
    EX = {"pelvis": 0, "neck_": 4, "clavicle_l": 6, "upperarm_l": 7, "lowerarm_l": 8,
          "clavicle_r": 9, "upperarm_r": 10, "lowerarm_r": 11, "thigh_l": 12,
          "calf_l": 13, "thigh_r": 14, "calf_r": 15, "chest": 2}
    if n in EX: return EX[n]
    if n.startswith("spine_"): return 1
    SUF = [("leftshoulder", 6), ("rightshoulder", 9), ("leftforearm", 8),
           ("rightforearm", 11), ("leftupleg", 12), ("rightupleg", 14),
           ("leftarm", 7), ("rightarm", 10), ("leftleg", 13), ("rightleg", 15),
           ("spine2", 3), ("spine1", 2), ("spine", 1), ("neck", 4), ("head", 5),
           ("hips", 0)]
    for s, c in SUF:
        if n.endswith(s): return c
    return -1

def resolve(p):
    names = p.node_names()
    nodes = p.nodes()
    pb = [-1] * NCANON
    for i, nm in enumerate(names):
        c = canon_of(nm)
        if c >= 0 and pb[c] < 0: pb[c] = i
    # SPINE CHAIN BY TOPOLOGY: name digits lie in both directions (UE spine_01/02/03
    # all collapse to "spine"; kaeli's Spine02 is the LOWEST bone). Walk the real
    # parent chain from the neck down to the hips: every spine-ish node between them
    # gets canon 1,2,3 in parent->child order. Identity z_bow measured the cost of not
    # doing this: two of three spine bones dropped, 45 deg of lean arriving as 27.
    if pb[0] >= 0 and pb[4] >= 0:
        chain = []
        a = nodes[pb[4]]["parent"]
        while a >= 0 and a != pb[0]:
            lo = names[a].lower()
            if "spine" in lo or "chest" in lo: chain.append(a)
            a = nodes[a]["parent"]
        chain.reverse()                        # now hips-side first
        for k, node in enumerate(chain[:3]):
            pb[1 + k] = node
        for k in range(len(chain), 3):
            pb[1 + k] = -1 if k > 0 else pb[1]
    return pb

def m3q(X, Y, Z):
    """Column frame [X Y Z] -> quaternion (xyzw), Shepperd."""
    m = [X[0], Y[0], Z[0], X[1], Y[1], Z[1], X[2], Y[2], Z[2]]
    tr = m[0] + m[4] + m[8]
    if tr > 0:
        s = math.sqrt(tr + 1) * 2
        return qnorm([(m[7] - m[5]) / s, (m[2] - m[6]) / s, (m[3] - m[1]) / s, s / 4])
    if m[0] > m[4] and m[0] > m[8]:
        s = math.sqrt(1 + m[0] - m[4] - m[8]) * 2
        return qnorm([s / 4, (m[1] + m[3]) / s, (m[2] + m[6]) / s, (m[7] - m[5]) / s])
    if m[4] > m[8]:
        s = math.sqrt(1 + m[4] - m[0] - m[8]) * 2
        return qnorm([(m[1] + m[3]) / s, s / 4, (m[5] + m[7]) / s, (m[2] - m[6]) / s])
    s = math.sqrt(1 + m[8] - m[0] - m[4]) * 2
    return qnorm([(m[2] + m[6]) / s, (m[5] + m[7]) / s, s / 4, (m[3] - m[1]) / s])

def sub(a, b): return [a[k] - b[k] for k in range(3)]
def norm(v):
    l = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / l for x in v]
def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]

def lowest_below(nodes, knee):
    """The foot: lowest rest descendant of the knee (the engine's S6 rule)."""
    wp, _ = fk_world(nodes)
    best, by = knee, 9e9
    for i in range(len(nodes)):
        a, hops = i, 0
        while a >= 0 and hops < 10:
            if a == knee: break
            a = nodes[a]["parent"]; hops += 1
        if a != knee or i == knee: continue
        if wp[i][1] < by: by, best = wp[i][1], i
    return best

def hand_of(p, nodes, fore):
    """The hand: forearm child named hand, else the farthest (the P2 rule)."""
    names = p.node_names()
    best, bd = fore, -1.0
    for i, nd in enumerate(nodes):
        if nd["parent"] != fore: continue
        d = sum(x * x for x in nd["t"])
        if "hand" in names[i].lower(): d += 1e12
        if d > bd: bd, best = d, i
    return best

class Basis:
    def __init__(self, p):
        self.p = p
        self.nodes = p.nodes()
        self.pb = resolve(p)
        wp, wq = fk_world(self.nodes)
        self.wp, self.wq = wp, wq
        pb = self.pb
        self.footL = lowest_below(self.nodes, pb[13]) if pb[13] >= 0 else -1
        self.footR = lowest_below(self.nodes, pb[15]) if pb[15] >= 0 else -1
        self.gripL = hand_of(p, self.nodes, pb[8]) if pb[8] >= 0 else -1
        self.gripR = hand_of(p, self.nodes, pb[11]) if pb[11] >= 0 else -1
        CHILD = {0: pb[1], 1: (pb[2] if pb[2] >= 0 else pb[4]),
                 2: (pb[3] if pb[3] >= 0 else pb[4]), 3: pb[4], 4: pb[5], 5: -1,
                 6: pb[7], 7: pb[8], 8: self.gripL, 9: pb[10], 10: pb[11],
                 11: self.gripR, 12: pb[13], 13: self.footL, 14: pb[15], 15: self.footR}
        lat = norm(sub(wp[pb[12]], wp[pb[14]])) if pb[12] >= 0 and pb[14] >= 0 else [1, 0, 0]
        fwd = norm(cross(lat, [0, 1, 0]))
        self.fwd = fwd
        # ONE Z SEMANTIC: rig-forward, for every bone. Bend-plane normals were the
        # plan's known degeneracy and it bit immediately: kaeli's near-straight rest
        # arms gave numerically arbitrary planes (armR read 53 for an authored 35).
        # Forward is measurable on every humanoid and means the same thing on all of
        # them; the parallel fallback below covers bones that point forward.
        self.A = [None] * NCANON; self.W = [None] * NCANON
        self.Wpar = [None] * NCANON; self.anc = [-1] * NCANON; self.pre = [None] * NCANON
        for c in range(NCANON):
            b = pb[c]
            if b < 0: continue
            ch = CHILD.get(c, -1)
            if ch is None or ch < 0:
                if c == 5 and pb[4] >= 0:
                    X = norm(sub(wp[b], wp[pb[4]]))     # head: extend neck->head
                else:
                    continue
            else:
                X = norm(sub(wp[ch], wp[b]))
            ref = fwd
            proj = sum(X[j] * ref[j] for j in range(3))
            Z = [ref[k] - X[k] * proj for k in range(3)]
            zl = math.sqrt(sum(x * x for x in Z))
            if zl < 1e-5:                                # bone parallel to ref: fall back
                ref2 = [0, 1, 0] if abs(X[1]) < 0.9 else fwd
                proj = sum(X[j] * ref2[j] for j in range(3))
                Z = [ref2[k] - X[k] * proj for k in range(3)]
                zl = math.sqrt(sum(x * x for x in Z))
                if zl < 1e-5: continue
            Z = [z / zl for z in Z]
            Y = cross(Z, X)
            self.A[c] = m3q(X, Y, Z)
            self.W[c] = wq[b]
            par = self.nodes[b]["parent"]
            self.Wpar[c] = wq[par] if par >= 0 else [0, 0, 0, 1]
            a = par
            while a >= 0:
                cc = next((k for k in range(NCANON) if pb[k] == a), -1)
                if cc >= 0: self.anc[c] = cc; break
                a = self.nodes[a]["parent"]
            if self.anc[c] >= 0:
                self.pre[c] = qmul(qinv(wq[pb[self.anc[c]]]), self.Wpar[c])

def char_frame(B):
    """The rig's character frame: X = its own forward, Y = up."""
    up = [0.0, 1.0, 0.0]
    Z = cross(B.fwd, up)
    return m3q(B.fwd, up, Z)

def transfer(dB, d_world_q, tB):
    """One posed frame: donor world quats -> target LOCAL q overrides.

    ABSOLUTE canonical semantics: the target bone's canonical frame is posed WHERE THE
    DONOR'S IS (facing-normalized), not displaced by the donor's delta from its own
    rest. Delta semantics read 'raise your arm 31 deg from wherever your rest is' --
    kaeli (arms hanging at 11) landed at 42 for an authored 120. A move means the same
    POSE on every body:

        W_t' = G_t * inv(G_d) * D * A_d * inv(A_t) * W_t
    """
    Gd, Gt = char_frame(dB), char_frame(tB)
    GG = qmul(Gt, qinv(Gd))
    out = {}
    wt = [None] * NCANON
    for c in range(NCANON):
        db, tb = dB.pb[c], tB.pb[c]
        if db < 0 or tb < 0 or dB.A[c] is None or tB.A[c] is None: continue
        D = qmul(d_world_q[db], qinv(dB.W[c]))
        Wp = qmul(qmul(GG, qmul(D, dB.A[c])), qmul(qinv(tB.A[c]), tB.W[c]))
        wt[c] = Wp
        a = tB.anc[c]
        Pw = qmul(wt[a], tB.pre[c]) if (a >= 0 and wt[a] is not None) else tB.Wpar[c]
        out[tb] = qnorm(qmul(qinv(Pw), Wp))
    return out
