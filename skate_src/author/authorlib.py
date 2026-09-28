#!/usr/bin/env python3
"""authorlib.py -- the authoring math: quats, rest-pose FK, and aim-a-bone.

The seed of the P1/P2 authoring library (design/animation_authoring.md). Conventions
locked to the engine's, verified against crowd_kms.c:
  - quaternions are XYZW (m4_trs reads q[0..3] as x,y,z,w)
  - a node's local transform is T * R * S; world = parent_world * local
  - rotation tracks REPLACE the node's rest q; untracked nodes hold rest

Everything here is deterministic (no randomness, pure float math) -- the same score must
bake to the same bytes, or the calibration ledger measures noise.

Run `authorlib.py --selftest` after touching any of it: three instrument bugs on this
bench were in the measuring tools, so the authoring tools carry their own checks from
day one.
"""
import math, sys

# ---- quaternions, xyzw --------------------------------------------------------------
def qmul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz]

def qinv(q):                      # unit quaternion inverse = conjugate
    return [-q[0], -q[1], -q[2], q[3]]

def qnorm(q):
    l = math.sqrt(sum(x * x for x in q)) or 1.0
    return [x / l for x in q]

def qrot(q, v):                   # rotate vector v by quaternion q
    p = qmul(qmul(q, [v[0], v[1], v[2], 0.0]), qinv(q))
    return p[:3]

def q_axis_angle(axis, ang):
    l = math.sqrt(sum(x * x for x in axis)) or 1.0
    s = math.sin(ang / 2)
    return [axis[0] / l * s, axis[1] / l * s, axis[2] / l * s, math.cos(ang / 2)]

def q_between(u, v):
    """Shortest-arc quaternion taking direction u to direction v."""
    lu = math.sqrt(sum(x * x for x in u)) or 1.0
    lv = math.sqrt(sum(x * x for x in v)) or 1.0
    u = [x / lu for x in u]; v = [x / lv for x in v]
    d = sum(a * b for a, b in zip(u, v))
    if d < -0.999999:             # opposite: rotate pi about any perpendicular
        ax = [1, 0, 0] if abs(u[0]) < 0.9 else [0, 1, 0]
        c = [u[1] * ax[2] - u[2] * ax[1], u[2] * ax[0] - u[0] * ax[2], u[0] * ax[1] - u[1] * ax[0]]
        return q_axis_angle(c, math.pi)
    c = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
    return qnorm([c[0], c[1], c[2], 1.0 + d])

def qslerp(a, b, t):
    d = sum(x * y for x, y in zip(a, b))
    if d < 0: b = [-x for x in b]; d = -d
    if d > 0.9995:                # nearly parallel: nlerp
        return qnorm([x + (y - x) * t for x, y in zip(a, b)])
    th = math.acos(max(-1.0, min(1.0, d)))
    sa, sb = math.sin((1 - t) * th) / math.sin(th), math.sin(t * th) / math.sin(th)
    return [x * sa + y * sb for x, y in zip(a, b)]

# ---- FK over a pack's rest pose ------------------------------------------------------
def fk_world(nodes, q_override=None, t_override=None):
    """World-space position AND world rotation quaternion per node, from the rest pose,
    with some nodes' local q replaced via q_override={idx: q} and/or local translation
    replaced via t_override={idx: t} (that is how authored ROOT MOTION exists: a
    translation track on the pelvis/root is just a t override per frame). Scale is
    applied to the translation of children, matching m4_mul over T*R*S composition for
    the uniform scales the club's rigs use at rest."""
    n = len(nodes)
    wq = [None] * n; wp = [None] * n; ws = [None] * n
    for i, nd in enumerate(nodes):
        lq = (q_override or {}).get(i, nd["q"])
        lt = (t_override or {}).get(i, nd["t"])
        p = nd["parent"]
        if p < 0:
            wq[i] = qnorm(lq); wp[i] = list(lt); ws[i] = list(nd["s"])
        else:
            wq[i] = qnorm(qmul(wq[p], lq))
            step = qrot(wq[p], [lt[0] * ws[p][0], lt[1] * ws[p][1], lt[2] * ws[p][2]])
            wp[i] = [wp[p][k] + step[k] for k in range(3)]
            ws[i] = [ws[p][k] * nd["s"][k] for k in range(3)]
    return wp, wq

def aim_bone(nodes, bone, child, target_dir, q_override=None, t_override=None):
    """The first authoring primitive: a new LOCAL q for `bone` so that the world-space
    direction bone->child points along target_dir. Solved in closed form:
        world_dir = Rp * Rl * t_hat        (t_hat = child's local translation)
        want      = A * world_dir          (A = shortest arc)
        Rl'       = inv(Rp) * A * Rp * Rl
    """
    wp, wq = fk_world(nodes, q_override, t_override)
    p = nodes[bone]["parent"]
    rp = wq[p] if p >= 0 else [0, 0, 0, 1]
    cur = [wp[child][k] - wp[bone][k] for k in range(3)]
    A = q_between(cur, target_dir)
    rl = (q_override or {}).get(bone, nodes[bone]["q"])
    return qnorm(qmul(qinv(rp), qmul(A, qmul(rp, rl))))

def rotate_bone_world(nodes, bone, axis, ang, q_override=None):
    """A new LOCAL q for `bone` = its current local rotated by axis-angle IN WORLD SPACE.
    Same conjugation as aim_bone. For a chain (three spine joints sharing one bend),
    apply sequentially, accumulating q_override -- each call re-runs FK, so the child's
    parent rotation includes the bend already applied above it. Correct by construction,
    at the price of an FK pass per joint (microseconds; authoring is offline)."""
    _wp, wq = fk_world(nodes, q_override)
    p = nodes[bone]["parent"]
    rp = wq[p] if p >= 0 else [0, 0, 0, 1]
    A = q_axis_angle(axis, ang)
    rl = (q_override or {}).get(bone, nodes[bone]["q"])
    return qnorm(qmul(qinv(rp), qmul(A, qmul(rp, rl))))

# ---- 2-bone analytic IK (P2) ---------------------------------------------------------
def solve_limb(nodes, chain, target, pole=None, q_override=None, t_override=None):
    """Closed-form two-bone IK. chain = (root, mid, end) node indices -- shoulder/elbow/
    wrist or hip/knee/ankle. Returns {root: q, mid: q} local rotations putting the END
    joint at `target` (world), law-of-cosines exact when reachable, clamped to the
    reachable annulus when not (never NaN -- the degenerate cases are the selftest's
    whole point). `pole` biases which way the elbow/knee points; None keeps it near the
    chain's current plane, which for a rest-posed rig is anatomical."""
    a, b, c = chain
    qo = dict(q_override or {})
    wp, _ = fk_world(nodes, qo, t_override)
    A, B, C = wp[a], wp[b], wp[c]
    L1 = math.dist(A, B); L2 = math.dist(B, C)
    d = math.dist(A, target)
    lo, hi = abs(L1 - L2) + 1e-5, L1 + L2 - 1e-5
    dc = max(lo, min(hi, d))
    if d < 1e-9:
        ahat = [0.0, -1.0, 0.0]                    # target on the root: pick a direction
    else:
        ahat = [(target[k] - A[k]) / d for k in range(3)]
    # bend-plane normal: from the pole if given, else the chain's own current plane
    ref = pole if pole else [C[k] - A[k] for k in range(3)]
    if not pole:
        ref = [B[k] - A[k] for k in range(3)]      # current mid direction defines the plane
    n = [ahat[1] * ref[2] - ahat[2] * ref[1],
         ahat[2] * ref[0] - ahat[0] * ref[2],
         ahat[0] * ref[1] - ahat[1] * ref[0]]
    nl = math.sqrt(sum(x * x for x in n))
    if nl < 1e-9:                                  # chain collinear with target: any plane
        n = [1.0, 0.0, 0.0] if abs(ahat[0]) < 0.9 else [0.0, 1.0, 0.0]
        n = [n[1] * ahat[2] - n[2] * ahat[1], n[2] * ahat[0] - n[0] * ahat[2],
             n[0] * ahat[1] - n[1] * ahat[0]]
        nl = math.sqrt(sum(x * x for x in n))
    n = [x / nl for x in n]
    # angle at the root, law of cosines; rotate ahat by it about -n toward the pole side
    ca = (L1 * L1 + dc * dc - L2 * L2) / (2 * L1 * dc)
    alpha = math.acos(max(-1.0, min(1.0, ca)))
    bdir = qrot(q_axis_angle(n, alpha), ahat)
    Bnew = [A[k] + bdir[k] * L1 for k in range(3)]
    tgt = [A[k] + ahat[k] * dc for k in range(3)]  # the clamped target
    q_root = aim_bone(nodes, a, b, bdir, qo, t_override)
    qo[a] = q_root
    q_mid = aim_bone(nodes, b, c, [tgt[k] - Bnew[k] for k in range(3)], qo, t_override)
    qo[b] = q_mid
    return {a: q_root, b: q_mid}

# ---- timing curves -------------------------------------------------------------------
def ease_smooth(t):               # smoothstep: zero-velocity at both ends
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)

def ease_settle(t, over=0.10):    # overshoot the target, then come to rest
    t = max(0.0, min(1.0, t))
    s = t * t * (3 - 2 * t)
    return s + over * math.sin(math.pi * s) * s

def ease_anticipate(t, back=0.12):  # dip against the motion first
    t = max(0.0, min(1.0, t))
    s = t * t * (3 - 2 * t)
    return s - back * math.sin(math.pi * (1 - s)) * (1 - s)

def profile(keys, t):
    """Piecewise scalar timing: keys = [(t, value, ease_fn_to_NEXT), ...] sorted.
    Evaluate at t with each segment's own easing. Before the first / after the last key,
    the value holds."""
    if t <= keys[0][0]: return keys[0][1]
    for (t0, v0, ez), (t1, v1, _e2) in zip(keys, keys[1:]):
        if t <= t1:
            u = (t - t0) / (t1 - t0) if t1 > t0 else 1.0
            return v0 + (v1 - v0) * (ez or ease_smooth)(u)
    return keys[-1][1]

# ---- skin weights --------------------------------------------------------------------
def quantise4(items):
    """[(bone, weight)] (one to four) -> four (bone, byte) pairs summing to EXACTLY 255.

    Every tool that re-binds a vertex needs this, and every one of them got it wrong the
    same way first.

    ⛔ "round each, give the LAST the remainder" does not sum to 255. If rounding-up
    pushes the earlier slots to 256 between them, the remainder goes negative, clamps to
    zero, and the vertex ends up carrying 256/255 of itself -- which at rest is not the
    identity, so the body MOVES. It takes three real influences and a sliver of a fourth
    (raw parts .586/.687/.687 all round up while the sliver is handed nothing), which is
    about one vertex in nine thousand: 7 of kaeli's 293,760, off by 0.18 rig units, and
    the rest-pose gate rightly refused the whole write.

    It takes a FOURTH slot to bite: with three or fewer, two round-ups can gain at most
    1.0 while the raw parts already total 255, so the old pattern is exact there. jaw.py
    keeps a vertex's existing influences AND adds the jaw, which is how it found the
    fourth.

    LARGEST REMAINDER instead: floor everything, then hand the leftover units to the
    biggest fractional parts. Exact by construction -- the total is 255 before a single
    unit is handed out -- and each spare unit lands where it was most nearly earned.
    """
    assert items, "quantise4 got no influences -- a vertex must ride something"
    assert len(items) <= 4, ("quantise4 got %d influences; the skin has four slots -- "
                             "sort and truncate before calling, or the sum is a lie"
                             % len(items))
    tot = sum(w for _b, w in items) or 1.0
    raw = [255.0 * w / tot for _b, w in items]
    q = [int(math.floor(r)) for r in raw]
    left = 255 - sum(q)
    order = sorted(range(len(q)), key=lambda i: -(raw[i] - q[i]))
    for k in range(left):
        q[order[k % len(order)]] += 1
    out = [(items[i][0], min(255, max(0, q[i]))) for i in range(len(items))]
    while len(out) < 4: out.append((0, 0))
    return out[:4]

# ---- selftest ------------------------------------------------------------------------
def selftest():
    bad = 0
    def ck(name, got, want, tol=1e-5):
        nonlocal bad
        ok = all(abs(g - w) < tol for g, w in zip(got, want))
        bad += not ok
        print("  %-34s %s" % (name, "ok" if ok else "WRONG got %s want %s" % (got, want)))
    # rotate +X 90deg about +Z -> +Y (xyzw, right-handed)
    ck("qrot 90z on +X", qrot(q_axis_angle([0, 0, 1], math.pi / 2), [1, 0, 0]), [0, 1, 0])
    ck("q_between +X->+Y acts", qrot(q_between([1, 0, 0], [0, 1, 0]), [1, 0, 0]), [0, 1, 0])
    ck("q_between antipodal", qrot(q_between([1, 0, 0], [-1, 0, 0]), [1, 0, 0]), [-1, 0, 0])
    ck("qmul . qinv = identity", qmul(q_axis_angle([1, 2, 3], 1.1), qinv(q_axis_angle([1, 2, 3], 1.1))), [0, 0, 0, 1])
    ck("qslerp midpoint 90z", qrot(qslerp([0, 0, 0, 1], q_axis_angle([0, 0, 1], math.pi / 2), 0.5), [1, 0, 0]),
       [math.cos(math.pi / 4), math.sin(math.pi / 4), 0])
    # a 3-node chain: root -> A(+Y 1) -> B(+Y 1); aim A's segment (A->B) at +X
    nodes = [dict(name="root", parent=-1, kind=1, t=[0, 0, 0], q=[0, 0, 0, 1], s=[1, 1, 1]),
             dict(name="A", parent=0, kind=1, t=[0, 1, 0], q=[0, 0, 0, 1], s=[1, 1, 1]),
             dict(name="B", parent=1, kind=1, t=[0, 1, 0], q=[0, 0, 0, 1], s=[1, 1, 1])]
    q2 = aim_bone(nodes, 1, 2, [1, 0, 0])
    wp, _ = fk_world(nodes, {1: q2})
    seg = [wp[2][k] - wp[1][k] for k in range(3)]
    ck("aim_bone chain seg -> +X", seg, [1, 0, 0])
    # and with a ROTATED parent (the conjugation must survive a non-identity Rp)
    nodes[0]["q"] = q_axis_angle([0, 1, 0], 0.7)
    q2 = aim_bone(nodes, 1, 2, [1, 0, 0])
    wp, _ = fk_world(nodes, {1: q2})
    seg = [wp[2][k] - wp[1][k] for k in range(3)]
    ck("aim_bone under rotated parent", seg, [1, 0, 0])
    # rotate_bone_world: +90 about world Z on a straight-up chain -> segment lands on -X
    nodes[0]["q"] = [0, 0, 0, 1]
    q2 = rotate_bone_world(nodes, 1, [0, 0, 1], math.pi / 2)
    wp, _ = fk_world(nodes, {1: q2})
    seg = [wp[2][k] - wp[1][k] for k in range(3)]
    ck("rotate_bone_world 90z on +Y", seg, [-1, 0, 0])
    # profile endpoints + hold + monotone ramp
    keys = [(0.0, 0.0, ease_smooth), (1.0, 45.0, None), (2.0, 45.0, ease_smooth), (3.0, 0.0, None)]
    ck("profile endpoints", [profile(keys, 0.0), profile(keys, 1.5), profile(keys, 3.5)],
       [0.0, 45.0, 0.0])
    mono = all(profile(keys, a / 20) <= profile(keys, (a + 1) / 20) + 1e-9 for a in range(20))
    bad += not mono
    print("  %-34s %s" % ("profile ramp monotone", "ok" if mono else "WRONG"))
    # ---- IK: sweep reachable targets folded->extended, then the degenerate cases ----
    nodes = [dict(name="root", parent=-1, kind=1, t=[0, 0, 0], q=[0, 0, 0, 1], s=[1, 1, 1]),
             dict(name="A", parent=0, kind=1, t=[0, 1, 0], q=[0, 0, 0, 1], s=[1, 1, 1]),
             dict(name="B", parent=1, kind=1, t=[0, 0.8, 0], q=[0, 0, 0, 1], s=[1, 1, 1]),
             dict(name="C", parent=2, kind=1, t=[0, 0.7, 0], q=[0, 0, 0, 1], s=[1, 1, 1])]
    chain = (1, 2, 3)   # A(root of limb)->B->C, lengths 0.8 + 0.7
    ikbad = 0
    for i in range(9):                      # radial sweep from nearly-folded to nearly-extended
        r = 0.15 + (1.45 - 0.02 - 0.15) * i / 8
        for j in range(6):                  # and around the clock
            th = j * math.pi / 3
            T = [1.0 + r * math.sin(th) * 0.7, 1.0 + r * math.cos(th), r * math.sin(th) * 0.7]
            d = math.dist(T, [0, 1, 0])
            if not (0.11 < d < 1.49): continue
            q = solve_limb(nodes, chain, T)
            wp, _ = fk_world(nodes, q)
            err = math.dist(wp[3], T)
            if err > 1e-4: ikbad += 1
    bad += ikbad > 0
    print("  %-34s %s" % ("IK sweep folded->extended", "ok" if not ikbad else "%d targets MISSED" % ikbad))
    # beyond reach: end lands at full extension ALONG the target direction
    T = [3.0, 1.0, 0.0]
    q = solve_limb(nodes, chain, T)
    wp, _ = fk_world(nodes, q)
    reach = math.dist(wp[3], [0, 1, 0])
    dirdot = (wp[3][0] - 0) * 1.0 / max(1e-9, math.dist(wp[3], [0, 1, 0]))
    ok = abs(reach - 1.5) < 0.01 and dirdot > 0.999
    bad += not ok
    print("  %-34s %s" % ("IK beyond reach: clamps, aligned", "ok" if ok else "WRONG reach %.3f dot %.3f" % (reach, dirdot)))
    # target AT the root joint: no NaN, end lands on the inner annulus (|L1-L2|)
    q = solve_limb(nodes, chain, [0, 1, 0])
    wp, _ = fk_world(nodes, q)
    inner = math.dist(wp[3], [0, 1, 0])
    ok = all(x == x for qq in q.values() for x in qq) and abs(inner - 0.1) < 0.01
    bad += not ok
    print("  %-34s %s" % ("IK target at joint: no NaN, inner", "ok" if ok else "WRONG inner %.3f" % inner))
    # ---- skin weights: the four bytes sum to 255, or the body moves at rest ----
    # the shape that bit jaw.py: three real influences and a sliver of a fourth, every
    # raw part above .5, so "round each and give the last the remainder" hands out 256.
    got = quantise4([(7, 0.856), (8, 0.847), (9, 0.847), (10, 0.0004)])
    ck("quantise4 3+sliver sums 255", [sum(q for _b, q in got)], [255], 0.5)
    ck("quantise4 pairs stay in order", [b for b, _q in got], [7, 8, 9, 10], 0.5)
    ck("quantise4 pads a lone influence", [q for _b, q in quantise4([(5, 0.3)])],
       [255, 0, 0, 0], 0.5)
    qbad = 0
    for i in range(1, 41):                      # a deterministic sweep of 1-4 influences,
        for j in range(1, 41):                  # including slivers that floor to zero
            for k in range(0, 7):
                items = [(0, i / 40.0), (1, j / 57.0), (2, (41 - i) / 61.0), (3, k / 997.0)]
                items = [e for e in items if e[1] > 0.0]
                if sum(q for _b, q in quantise4(items)) != 255: qbad += 1
    bad += qbad > 0
    print("  %-34s %s" % ("quantise4 sweep sums 255",
                          "ok" if not qbad else "%d tuples MISSED 255" % qbad))
    print("selftest: %s" % ("all ok" if not bad else "%d WRONG -- fix before authoring" % bad))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(selftest() if "--selftest" in sys.argv else 0)

# ---- contact declarations (P2) -------------------------------------------------------
def declare(pack_path, move, decl):
    """Write this move's declaration into the pack's sidecar
    (<pack>.decl.json). The score's declarations are movecheck's test fixture: the same
    statements that produced the motion are what the ENGINE playback is checked against.
    All tolerances are scale-free (leg-lengths), resolved by movecheck from the dump."""
    import json, os
    path = pack_path.replace(".rpack", "") + ".decl.json"   # NEVER contain ".rpack": the pack dir loader matches by substring and died parsing JSON as RPK1
    d = {}
    if os.path.exists(path):
        d = json.load(open(path))
    d[move] = decl
    json.dump(d, open(path, "w"), indent=1)
    return path

def translate_node_world(nodes, node, delta_world, q_override=None, t_override=None):
    """A new LOCAL translation for `node` = its current local t plus a WORLD-space
    delta, conjugated through the parent's world rotation (and divided by the parent's
    accumulated scale). Discovered the hard way: subtracting from the pelvis's local Y
    moved manny's hips AWAY from his ankles -- UE rigs do not world-align the root
    basis, so authored root motion must be conjugated exactly like authored rotation."""
    _wp, wq = fk_world(nodes, q_override, t_override)
    p = nodes[node]["parent"]
    lt = list((t_override or {}).get(node, nodes[node]["t"]))
    if p < 0:
        return [lt[k] + delta_world[k] for k in range(3)]
    # accumulated parent scale (uniform in practice, but honour it)
    ws = [1.0, 1.0, 1.0]; i = p
    chain = []
    while i >= 0:
        chain.append(i); i = nodes[i]["parent"]
    for i in reversed(chain):
        s = nodes[i]["s"]
        ws = [ws[0] * s[0], ws[1] * s[1], ws[2] * s[2]]
    dl = qrot(qinv(wq[p]), delta_world)
    return [lt[k] + dl[k] / (ws[k] if abs(ws[k]) > 1e-9 else 1.0) for k in range(3)]

def hold_world_rotation(nodes, node, wq_target, q_override=None, t_override=None):
    """A new LOCAL q for `node` such that its WORLD rotation equals wq_target -- used to
    keep a foot FLAT while the leg above it bends. Discovered by movecheck's first run:
    pinning the ankle is not pinning the foot; a deep knee-bend rotates the foot bone
    with the calf, the toes plunge through the rig-space floor, and the engine's floor
    clamp then (correctly) lifts the whole body off its declared contacts."""
    _wp, wq = fk_world(nodes, q_override, t_override)
    p = nodes[node]["parent"]
    rp = wq[p] if p >= 0 else [0, 0, 0, 1]
    return qnorm(qmul(qinv(rp), wq_target))

# ---- the expressive layer (P6e): hands, toes, faces, tails ---------------------------
# jacob 08-27: "fingers faces and tails and toes... very important for animations."
# Fleet survey: fingers on 113 of 134 packs, toes near-universal, eyes on ~20, jaw on 3,
# tail chains on the creatures (cow: 8 bones). The engine already plays any node's
# track -- expressiveness is RESOLUTION plus PRIMITIVES, not engine surgery.

FINGER_NAMES = ("thumb", "index", "middle", "ring", "pinky")

def _kids(nodes, i):
    return [k for k in range(len(nodes)) if nodes[k]["parent"] == i]

def find_hands(p, nodes, names, handL, handR):
    """Per side: {'thumb': [j1,j2,j3], 'index': [...], ...} joint chains, plus
    'tips': {fname: leaf_node} for measurement. Chains walk by name family; a JOINT has
    children, the childless duplicate at the end is the fingertip."""
    out = {}
    for side, hand in (("L", handL), ("R", handR)):
        if hand < 0: continue
        fingers = {}
        tips = {}
        for fname in FINGER_NAMES:
            chain = []
            cur = hand
            for _seg in range(4):
                nxt = -1
                for k in _kids(nodes, cur):
                    if fname in names[k].lower() and _kids(nodes, k):
                        nxt = k; break
                if nxt < 0: break
                chain.append(nxt); cur = nxt
            if chain:
                fingers[fname] = chain
                for k in _kids(nodes, chain[-1]):
                    if not _kids(nodes, k): tips[fname] = k; break
        if fingers:
            out[side] = {"hand": hand, "fingers": fingers, "tips": tips}
    return out

def find_toes(p, nodes, names, footL, footR):
    """The ball joints: a foot child named ball/toe with children (the toe leaf under it
    is the tip)."""
    out = {}
    for side, foot in (("L", footL), ("R", footR)):
        if foot < 0: continue
        for k in _kids(nodes, foot):
            lo = names[k].lower()
            if "ball" in lo or "toe" in lo:
                out[side] = k; break
    return out

def find_face(nodes, names):
    """jaw + eye bones by name; empty dicts where the rig has no face."""
    jaw = [i for i, n in enumerate(names) if "jaw" in n.lower()]
    eyes = [i for i, n in enumerate(names) if "eye" in n.lower() and _kids(nodes, i) is not None]
    return {"jaw": jaw[:1], "eyes": eyes[:2]}

def find_tail(nodes, names):
    """The tail chain, root-first (ordered by walking parents)."""
    tl = [i for i, n in enumerate(names) if "tail" in n.lower()]
    def depth(i):
        d, a = 0, i
        while a >= 0: a = nodes[a]["parent"]; d += 1
        return d
    return sorted(tl, key=depth)

class HandRig:
    """Per-rig hand axes, derived and sign-audited -- never hand-tuned."""
    def __init__(self, nodes, base_q, base_t, hand_spec, elbow):
        self.nodes, self.spec = nodes, hand_spec
        self.bq, self.bt = base_q, base_t
        wp, _ = fk_world(nodes, base_q, base_t)
        f = hand_spec["fingers"]
        idx = f.get("index", f.get("middle"))
        pnk = f.get("pinky", f.get("ring"))
        h = hand_spec["hand"]
        lat = [wp[idx[0]][k] - wp[pnk[0]][k] for k in range(3)]
        ll = math.sqrt(sum(x * x for x in lat)) or 1.0
        self.lat = [x / ll for x in lat]                      # curl axis (index->pinky line)
        md = [0.0, 0.0, 0.0]
        for fn, ch in f.items():
            if fn == "thumb" or len(ch) < 2: continue
            d = [wp[ch[-1]][k] - wp[ch[0]][k] for k in range(3)]
            md = [md[k] + d[k] for k in range(3)]
        ml = math.sqrt(sum(x * x for x in md)) or 1.0
        self.fdir = [x / ml for x in md]                      # mean finger direction
        pn = [self.fdir[1] * self.lat[2] - self.fdir[2] * self.lat[1],
              self.fdir[2] * self.lat[0] - self.fdir[0] * self.lat[2],
              self.fdir[0] * self.lat[1] - self.fdir[1] * self.lat[0]]
        pl = math.sqrt(sum(x * x for x in pn)) or 1.0
        self.palm = [x / pl for x in pn]                      # palm normal (sign audited below)
        # CURL SIGN AUDIT: curling must bring the fingertips toward the elbow (the inner
        # forearm). Try both signs on the index chain and keep the one that closes in.
        self.elbow = elbow
        self.curl_sign = 1.0
        d_pos = self._probe_curl(+1.0, wp)
        d_neg = self._probe_curl(-1.0, wp)
        self.curl_sign = 1.0 if d_pos < d_neg else -1.0
        # SPREAD AXIS AUDIT -- the curl got one from day one, the spread did not, and
        # the un-audited fan SHRANK the mannequin's tip span (0.97x for an authored
        # open hand). Probe both palm signs; keep the one that grows the span.
        s_pos = self._probe_spread(+1.0)
        s_neg = self._probe_spread(-1.0)
        if s_neg > s_pos:
            self.palm = [-x for x in self.palm]

    def _probe_spread(self, sgn):
        FAN = {"index": 1.3, "middle": 0.45, "ring": -0.45, "pinky": -1.3, "thumb": 2.0}
        qo = dict(self.bq)
        for fn, ch in self.spec["fingers"].items():
            if not ch: continue
            ax = [sgn * x for x in self.palm]
            qo[ch[0]] = rotate_bone_world(self.nodes, ch[0], ax,
                                          math.radians(11.0) * FAN[fn], qo)
        w, _ = fk_world(self.nodes, qo, self.bt)
        t = self.spec["tips"]
        if "index" in t and "pinky" in t:
            return math.dist(w[t["index"]], w[t["pinky"]])
        ks = list(t.values())
        return math.dist(w[ks[0]], w[ks[-1]]) if len(ks) >= 2 else 0.0

    def _probe_curl(self, sgn, wp):
        qo = dict(self.bq)
        ch = self.spec["fingers"].get("index") or next(iter(self.spec["fingers"].values()))
        for b in ch:
            qo[b] = rotate_bone_world(self.nodes, b, self.lat, sgn * math.radians(50), qo)
        w, _ = fk_world(self.nodes, qo, self.bt)
        tip = self.spec["tips"].get("index", ch[-1])
        return math.dist(w[tip], w[self.elbow])

    def pose(self, qo, curl=0.0, spread=0.0, thumb=None, wiggle=None):
        """Apply a hand pose into qo. curl 0..1 (fist), spread 0..1 (fan open),
        thumb defaults to curl, wiggle = {fname: extra_curl_deg} for per-finger life.

        THE AXES RIDE WITH THE HAND: lat/palm were derived at rest, and a raised arm
        rotates the hand ~120 deg -- fanning about the REST axes on a raised hand read
        0.97x span for an authored open (the direct probe on a rest pose read 1.50x,
        which is what localized the bug). The hand's current world delta carries them."""
        if thumb is None: thumb = curl
        _wp, wq = fk_world(self.nodes, qo, self.bt)
        _wp0, wq0 = fk_world(self.nodes, self.bq, self.bt)
        hd = qmul(wq[self.spec["hand"]], qinv(wq0[self.spec["hand"]]))
        lat = qrot(hd, self.lat)
        palm = qrot(hd, self.palm)
        FAN = {"index": 1.3, "middle": 0.45, "ring": -0.45, "pinky": -1.3, "thumb": 2.0}
        W = (0.75, 1.0, 1.0)                                  # per-segment curl weights
        for fn, ch in self.spec["fingers"].items():
            c = thumb if fn == "thumb" else curl
            extra = math.radians((wiggle or {}).get(fn, 0.0))
            if spread > 1e-4 and ch:
                qo[ch[0]] = rotate_bone_world(self.nodes, ch[0], palm,
                                              math.radians(11.0) * FAN[fn] * spread, qo)
            for si, b in enumerate(ch):
                ang = self.curl_sign * (math.radians(86.0) * c * W[min(si, 2)] + extra)
                if abs(ang) > 1e-5:
                    qo[b] = rotate_bone_world(self.nodes, b, lat, ang, qo)
        return qo

# ---- P5b: the rig adapter -- a score resolves ANY humanoid, not manny's names --------
def body_dims(p, RM=None):
    """The body's REAL silhouette, measured from its own bind-pose mesh -- what any
    collision proxy should be built from instead of guessed constants (jacob: "can you
    do something about collision?"; the hug's hardcoded 0.19/0.135 ellipse fit nobody
    exactly). Rig units; multiply by the engine's scale for world.

    Returned about the body's own origin/spine axis:
      rx      torso half-width  (max |x| in the torso band -- bind T-pose arms sit at
              shoulder height, so the band top stays below them)
      front   chest half-depth  (max +z in the band: +z is club forward = her front)
      back    back half-depth   (|min z|)
      headr   head horizontal radius
      hipY/shY  the band bounds used, for the caller's own banding
    """
    import math as _m
    if RM is None: RM = rig_map(p)
    nodes = p.nodes()
    wp, _ = fk_world(nodes)
    hipY = wp[RM["pelvis"]][1]
    shY = (wp[RM["upperarm_l"]][1] + wp[RM["upperarm_r"]][1]) / 2.0
    headY = wp[RM["head"]][1]
    # The RIBS band only, with arm outliers rejected by hip width. The naive
    # hip-to-shoulder band measured reva's T-POSE ARM as a 1.33u torso and F05's
    # flared DRESS as a 0.64u chest -- the ribcage between them is what arms wrap.
    span = shY - hipY
    lo, hi = hipY + 0.45 * span, hipY + 0.85 * span
    hipHalf = abs(wp[RM["thigh_l"]][0] - wp[RM["thigh_r"]][0]) / 2.0 or 0.1
    # ARM FLESH IS NOT TORSO. A T-pose keeps the arms above the rib band, but an
    # A-pose (F05) hangs them straight through it -- her rx read 0.38 on a 0.25 body.
    # The skeleton knows exactly where the arms are: skip any vertex near the
    # shoulder->elbow->wrist segments.
    segs = []
    for sd in ("l", "r"):
        a1, a2, a3 = RM["upperarm_" + sd], RM["lowerarm_" + sd], RM["hand_" + sd]
        segs.append((wp[a1], wp[a2])); segs.append((wp[a2], wp[a3]))
    armR = 0.35 * _m.dist(wp[RM["upperarm_l"]], wp[RM["lowerarm_l"]])
    def near_arm(pt):
        for (A, B) in segs:
            ab = [B[k] - A[k] for k in range(3)]
            l2 = sum(v * v for v in ab) or 1e-9
            t = max(0.0, min(1.0, sum((pt[k] - A[k]) * ab[k] for k in range(3)) / l2))
            if _m.dist(pt, [A[k] + ab[k] * t for k in range(3)]) < armR: return True
        return False
    xs, fs, bs, hs = [], [], [], []
    for pt in p.skinned_positions():
        x, y, z = pt
        if lo <= y <= hi:
            if near_arm(pt): continue
            if abs(x) <= hipHalf * 2.2: xs.append(abs(x))
            if abs(x) <= hipHalf * 1.2:
                if z > 0: fs.append(z)
                else: bs.append(-z)
        elif y > headY:
            hs.append(_m.hypot(x, z))
    # PERCENTILES, not maxima. A max proxy is one costume detail wide: reva's ponytail
    # read as a 0.42u SKULL and got an arm convicted of head collision during a clean
    # release; F05's puffed dress read as a 0.39u ribcage and starved the landing
    # solver entirely. p95 (p85 for hair) tracks the body; an arm pressing through the
    # last centimetres of hair or cloth is what real hugs do.
    def pct(v, q, d=0.0):
        if not v: return d
        v = sorted(v); return v[min(len(v) - 1, int(len(v) * q))]
    return dict(rx=pct(xs, 0.95), front=pct(fs, 0.95), back=pct(bs, 0.95),
                headr=pct(hs, 0.85), hipY=hipY, shY=shY)

def body_capsules(p, RM=None):
    """The body as MEASURED CAPSULES, one per canon torso/head/leg bone (homecoming
    H5): segment = the bone's rest world head to its canonical child's head, radius =
    p90 perpendicular distance of the vertices DOMINANTLY WEIGHTED to that bone. One
    collision model for every two-body score, no hand-tuned proxy constants -- and the
    weights themselves say which flesh belongs to which bone, so an A-pose's arms
    cannot contaminate a rib measurement the way band-scans allowed.

    Rig units; multiply by the engine's scale for world. Arms are deliberately absent:
    a wrap must negotiate the torso, not the hanging arms it goes over and around."""
    import math as _m
    if RM is None: RM = rig_map(p)
    nodes = p.nodes()
    wp, _ = fk_world(nodes)
    chain = [("pelvis", "spine_01"), ("spine_01", "spine_02"), ("spine_02", "spine_03"),
             ("spine_03", "neck_01"), ("neck_01", "head"), ("head", None),
             ("thigh_l", "calf_l"), ("calf_l", "foot_l"),
             ("thigh_r", "calf_r"), ("calf_r", "foot_r")]
    byb = {}
    for pt in p.skinned_positions(with_bones=True):
        byb.setdefault(pt[3], []).append(pt[:3])
    caps = []
    for nm, child in chain:
        b = RM.get(nm, -1)
        if b < 0: continue
        a = list(wp[b])
        if child and RM.get(child, -1) >= 0:
            t = list(wp[RM[child]])
        else:
            t = [a[0], a[1] + 0.12 * abs(wp[RM["head"]][1] - wp[RM["pelvis"]][1]), a[2]]
        pts = byb.get(b, [])
        ds = []
        ab = [t[k] - a[k] for k in range(3)]
        l2 = sum(v * v for v in ab) or 1e-12
        for v in pts:
            u = max(0.0, min(1.0, sum((v[k] - a[k]) * ab[k] for k in range(3)) / l2))
            cp = [a[k] + ab[k] * u for k in range(3)]
            ds.append(_m.dist(v, cp))
        ds.sort()
        r = ds[int(len(ds) * 0.90)] if ds else 0.0
        caps.append(dict(name=nm, a=a, b=t, r=r))
    return caps

def face_points(p, RM=None):
    """THE FACE, measured (z_kiss): mouth / nose / forehead / chin as rest-world points,
    read from the vertices dominantly weighted to the HEAD FAMILY (head + descendants --
    jaw and eye bones own face flesh on rigged faces). The mesh is anatomy the pipeline
    never placed, so these are legal for contact gates (circular-verification law); +Z
    front is the engine's pixel-verified contract.

    Hair is the trap: it is head-weighted and hangs everywhere. The face is therefore
    read from the FRONT-CENTRAL slice only (in front of the family's z-median -- back
    hair and ponytails fall behind it -- and inside half the head's half-width, which
    sheds the side falls), and the levels are anatomical fractions of chin->crown
    height: mouth 0.22, nose 0.35, forehead 0.75. Percentiles, never maxima.
    Rig units about the body origin; multiply by the engine scale for world."""
    import math as _m
    if RM is None: RM = rig_map(p)
    nodes = p.nodes()
    fam = {RM["head"]}
    grew = True
    while grew:
        grew = False
        for i, nd in enumerate(nodes):
            if nd["parent"] in fam and i not in fam:
                fam.add(i); grew = True
    pts = [v[:3] for v in p.skinned_positions(with_bones=True) if v[3] in fam]
    assert pts, "no head-weighted vertices on this pack"
    def pct(v, q):
        v = sorted(v); return v[max(0, min(len(v) - 1, int(len(v) * q)))]
    zmed = pct([q9[2] for q9 in pts], 0.50)
    xw = pct([abs(q9[0]) for q9 in pts], 0.95) or 0.1
    front = [q9 for q9 in pts if q9[2] > zmed and abs(q9[0]) < 0.5 * xw]
    assert front, "no front-central face vertices"
    # LEVELS COME FROM THE PROFILE, not from chin->crown fractions: hair owns the crown
    # (kaeli's updo put "the top of her head" 0.48u above her skull; reva's ponytail
    # stretched the span until her "mouth" landed under her chin). The NOSE is the one
    # feature no hairstyle owns -- the maximum forward protrusion of the front-central
    # slice near the skull base -- and both club faces put it 0.4 half-widths above the
    # head bone. Everything else is anchored to it in units of the head's half-width.
    wp9, _ = fk_world(nodes)
    hy = wp9[RM["head"]][1]
    def zq_at(yc, half, q):
        band = [q9 for q9 in front if abs(q9[1] - yc) <= half]
        if not band: return None, None
        return pct([q9[2] for q9 in band], q), pct([q9[0] for q9 in band], 0.50)
    # THE LOWEST STRONG PROTRUSION IS THE NOSE. Taking the global maximum picks the
    # HAIR FRINGE on any body with a full head of hair: reva's profile has two peaks,
    # the nose at +0.03 above the head bone and her bangs at +0.09, and the bangs win
    # by a hair (0.253 vs 0.248). Every landmark below then inherits the error, which
    # is how a camera aimed at "the mouth" ended up framing her eyes -- jacob: "i
    # suspect you dont know where her eyes and mouth are." A nose sits BELOW a brow,
    # so among the peaks that are within 6% of the strongest, take the lowest.
    steps = 34
    prof = []
    for s in range(steps):
        yc = hy - 0.5 * xw + (1.3 * xw) * s / (steps - 1)
        z9, x9 = zq_at(yc, 0.05 * xw, 0.97)
        if z9 is not None: prof.append((yc, z9, x9))
    assert prof, "no face profile in the window"
    zmax = max(e[1] for e in prof)
    peaks = []
    for i, (yc, z9, x9) in enumerate(prof):
        lo9 = prof[i - 1][1] if i else -1e9
        hi9 = prof[i + 1][1] if i + 1 < len(prof) else -1e9
        if z9 >= lo9 and z9 >= hi9 and z9 >= 0.94 * zmax:
            peaks.append((yc, z9, x9))
    if not peaks: peaks = [max(prof, key=lambda e: e[1])]
    nose_y, nose_z, nose_x = min(peaks, key=lambda e: e[0])
    out = {"xw": xw, "chin": pct([q9[1] for q9 in front], 0.02)}
    out["nose"] = [nose_x, nose_y, nose_z]
    for nm, dy, half, q in (("mouth", -0.26, 0.08, 0.95),
                            ("forehead", +0.55, 0.10, 0.97)):
        z9, x9 = zq_at(nose_y + dy * xw, half * xw, q)
        assert z9 is not None, "no %s band" % nm
        out[nm] = [x9, nose_y + dy * xw, z9]
    return out

def rig_map(p):
    """Canonical bone lookup for a pack: the names the scores use, resolved per rig via
    the canon (retarget.resolve handles the spine-by-topology and name families). Also
    the base stand clip and the derived chain ends. A score written against this map
    re-solves its contacts on ANY body's own proportions -- which is what makes
    inherited moves contact-exact instead of merely pose-true."""
    import retarget
    nodes = p.nodes(); names = p.node_names()
    pb = retarget.resolve(p)
    m = {"pelvis": pb[0], "spine_01": pb[1], "spine_02": pb[2], "spine_03": pb[3],
         "neck_01": pb[4], "head": pb[5],
         "clavicle_l": pb[6], "upperarm_l": pb[7], "lowerarm_l": pb[8],
         "clavicle_r": pb[9], "upperarm_r": pb[10], "lowerarm_r": pb[11],
         "thigh_l": pb[12], "calf_l": pb[13], "thigh_r": pb[14], "calf_r": pb[15]}
    m["foot_l"] = retarget.lowest_below(nodes, pb[13]) if pb[13] >= 0 else -1
    m["foot_r"] = retarget.lowest_below(nodes, pb[15]) if pb[15] >= 0 else -1
    # feet from lowest_below can be the TOE on toe-having rigs; the ankle proper is the
    # foot-named child of the calf where one exists
    for side, calf in (("l", pb[13]), ("r", pb[15])):
        if calf < 0: continue
        for k in _kids(nodes, calf):
            if "foot" in names[k].lower():
                m["foot_" + side] = k; break
    m["hand_l"] = hand_of(p, nodes, pb[8]) if pb[8] >= 0 else -1
    m["hand_r"] = hand_of(p, nodes, pb[11]) if pb[11] >= 0 else -1
    toes = find_toes(p, nodes, names, m["foot_l"], m["foot_r"])
    m["ball_l"] = toes.get("L", m["foot_l"])
    m["ball_r"] = toes.get("R", m["foot_r"])
    # the base stand clip: a_idle where it exists, else the first non-floor idle
    cn = p.clip_names()
    base = "a_idle" if "a_idle" in cn else None
    if base is None:
        for nm in cn:
            lo = nm.lower()
            if "idle" in lo and not any(w in lo for w in ("lay", "sleep", "sit", "crouch")):
                base = nm; break
    m["_base_clip"] = base
    m["_missing"] = [k for k, v in m.items() if not k.startswith("_") and (v is None or v < 0)]
    return m

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
