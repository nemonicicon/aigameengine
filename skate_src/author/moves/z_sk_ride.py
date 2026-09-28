#!/usr/bin/env python3
"""z_sk_ride.py -- ROBOT NEKO ep 10: RIDING AND TRICKS, each clip with a BOARD CHANNEL.

  z_rm_sk_ride    regular stance rolling: soft knees, a bob, an arm balance sway (LOOP 2.0 s, first frame == last)
  z_rm_sk_push    the back foot steps down beside the board, sweeps the floor at the path speed, comes back (1.2 s)
  z_rm_sk_pop     the ollie: crouch, tail strike, the front foot drags it level, ~0.45 u up, catch, absorb (1.0 s)
  z_rm_sk_kf      the kickflip: the ollie, the front foot flicks off the heel-side edge, a 360 roll about X (1.1 s)
  z_rm_sk_shove   the pop shove-it: she hops square, the board turns 180 about its Y under her feet (1.0 s)
  z_rm_sk_manual  weight back, nose up ~12 degrees on the back wheels, hold, set it down (1.6 s)
  -- ep 11, the half-pipe AIRS (the engine flies the root and turns it 180; these are the body and the board) --
  z_rm_sk_grab    INDY: knees tucked, the back (right) hand down between the feet onto the TOE rail, hold, land (1.1 s)
  z_rm_sk_method  METHOD: the front (left) hand on the HEEL rail, knees bent back, the board up behind her (1.2 s)

THE REACH IS SOLVED, NOT GUESSED. How high the tuck brings the board is whatever puts the grabbing rail at a
comfortable fraction of THIS body's own arm (shoulder -> wrist, plus the measured palm) from its shoulder in the
held pose -- closed form, the board rises straight up in her model space. reva's arm and kaeli's differ by 12%.
The hand is IK'd so its PALM (the centroid of its own vertices) sits on the rail, then blended in by rotation.

Every trick starts and ends EXACTLY on z_rm_sk_ride frame 0 (asserted), so the engine can cut ride -> trick -> ride.

THE BOARD CHANNEL (ep 10 contract). For every clip NAME there is a companion NAME_bd: same fps, same frame
count, two tracks on node 0 -- (0,1) translation and (0,0) rotation, XYZW like every quat in a pack. Each
frame is the pose of the BOARD FRAME in the rider's MODEL space (fk_world's space, rig units):
  origin = the centre of the deck's TOP surface;  +X = the nose = her LEFT, the lead foot, the way she
  travels;  +Y = the deck's up-normal;  +Z = X cross Y (her toe side).
Every clip is z_rm_: the pelvis track owns Y, the feet stand ON the deck inside the clip, and the engine
adds no deck lift. The floor is the stand's floor -- the lowest sole vertex of the rest pose, where
z_rm_board_stepon's first frame stands.

THE BOARD IS MEASURED, NOT REMEMBERED. The prop is read here (skateboard.rpack at its 1.4 u draw length).
⛔ Its bounding box is 0.223 u tall, but that is the KICK TIPS: the flat deck between the trucks, where the
feet stand, is lower -- the number is printed and written into the stance JSON. The mesh's long axis is
its Z; the frame's +X is mesh +Z, so local = (mesh.z, mesh.y - deck_top, -mesh.x).

GATES (the score is the test; every number printed): a grounded board's wheels on the floor, no board
point under it; a planted sole on the deck top or the floor; soles hovering just over a level board in
the air; no foot under the floor; body capsules (authorlib.body_capsules + measured arm capsules) never
deeper into each other than they already are at rest; the board as boxes never inside a capsule (a calf
over a sole that stands on the deck excepted); knees bend over the toes, never backwards.

usage: z_sk_ride.py <pack> [--only CLIP,..|ep10|air] [--dry] [--dump DIR] [--stance-only] [--live]
  ⛔ a pack in reva_native/packs is REFUSED unless --live is given (author against a copy; the director installs).
"""
import sys, os, math, json, struct
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
from rpack import Rpack, append_clips, drop_clip, lint_name
from authorlib import (rig_map, fk_world, aim_bone, solve_limb, rotate_bone_world, translate_node_world,
                       hold_world_rotation, profile, ease_smooth, qnorm, qrot, qmul, qinv, q_axis_angle,
                       qslerp, body_capsules, q_between)
import z_board as ZB

FPS = 30.0
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
BOARD_PROP = os.path.join(REPO, "reva_native", "props", "skateboard.rpack")
BOARD_LEN = 1.4             # club u: the engine draws the prop with its longest side this long
BODY_H = 3.45               # club u: the cast loader sizes every body to this by its mesh
SPEED = 3.0                 # club u/s: the engine's default path speed -- the push foot sweeps at it
STANCE_OUT = os.environ.get("CLUB_OUT", "out")
UP = [0.0, 1.0, 0.0]

# the stance, club units along the board from its centre (+ = nose), degrees of toe-out toward the nose
FRONT_X, BACK_X = +0.34, -0.36
FRONT_YAW, BACK_YAW = 26.0, -2.0
RIDE_SINK = 0.26            # club u the pelvis sits lower than the stand's, over and above the deck height: BENT knees
RIDE_KNEEOUT = 0.00         # the knees track out over the toes instead of driving together over the deck

# gates, club units
TOL_WHEEL = 0.010
TOL_SOLE = 0.015
AIR_HOVER = 0.12
FLOOR_EPS = 0.005
CAP_EXTRA = 0.030           # rig u: deeper than at rest by more than this is a clip
TOL_GRIP = 0.040            # club u: a holding palm is this close to its rail, or it is not holding
REACH_FRAC = 0.90           # the held arm: shoulder -> wrist at this fraction of its full length (an elbow, not a rod)


# ---- small vector kit ---------------------------------------------------------------------------------
def vadd(a, b): return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
def vsub(a, b): return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
def vmul(a, s): return [a[0] * s, a[1] * s, a[2] * s]
def vdot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def vcross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def vlen(a): return math.sqrt(vdot(a, a))
def vnorm(a):
    l = vlen(a) or 1.0
    return [a[0] / l, a[1] / l, a[2] / l]
def lerp(a, b, u): return [a[k] + (b[k] - a[k]) * u for k in range(3)]
def mixf(a, b, u): return a + (b - a) * u
def qa(axis, deg): return q_axis_angle(axis, math.radians(deg))
def ease_out(t):
    t = max(0.0, min(1.0, t)); return 1.0 - (1.0 - t) * (1.0 - t)
def ease_in(t):
    t = max(0.0, min(1.0, t)); return t * t
def lin(t): return max(0.0, min(1.0, t))
def wave(t, D, cycles, ph=0.0): return math.sin(2.0 * math.pi * cycles * t / D + ph)


def q_from_basis(X, Y, Z):
    m00, m01, m02 = X[0], Y[0], Z[0]
    m10, m11, m12 = X[1], Y[1], Z[1]
    m20, m21, m22 = X[2], Y[2], Z[2]
    tr = m00 + m11 + m22
    if tr > 0:
        s = 0.5 / math.sqrt(tr + 1.0)
        q = [(m21 - m12) * s, (m02 - m20) * s, (m10 - m01) * s, 0.25 / s]
    elif m00 > m11 and m00 > m22:
        s = 2.0 * math.sqrt(1.0 + m00 - m11 - m22)
        q = [0.25 * s, (m10 + m01) / s, (m02 + m20) / s, (m21 - m12) / s]
    elif m11 > m22:
        s = 2.0 * math.sqrt(1.0 + m11 - m00 - m22)
        q = [(m10 + m01) / s, 0.25 * s, (m21 + m12) / s, (m02 - m20) / s]
    else:
        s = 2.0 * math.sqrt(1.0 + m22 - m00 - m11)
        q = [(m02 + m20) / s, (m21 + m12) / s, 0.25 * s, (m10 - m01) / s]
    q = qnorm(q)
    for v, e in ((X, [1, 0, 0]), (Y, [0, 1, 0]), (Z, [0, 0, 1])):
        assert vlen(vsub(qrot(q, e), v)) < 1e-4, "q_from_basis: not a rotation"
    return q


def seg_seg(p1, q1, p2, q2):
    """Closest distance between segments p1q1 and p2q2 (Ericson 5.1.9)."""
    d1 = vsub(q1, p1); d2 = vsub(q2, p2); r = vsub(p1, p2)
    a = vdot(d1, d1); e = vdot(d2, d2); f = vdot(d2, r)
    if a < 1e-12 and e < 1e-12: return vlen(r), 0.0, 0.0
    if a < 1e-12:
        s = 0.0; t = max(0.0, min(1.0, f / e))
    else:
        c = vdot(d1, r)
        if e < 1e-12:
            t = 0.0; s = max(0.0, min(1.0, -c / a))
        else:
            b = vdot(d1, d2); den = a * e - b * b
            s = max(0.0, min(1.0, (b * f - c * e) / den)) if den > 1e-12 else 0.0
            t = (b * s + f) / e
            if t < 0.0: t = 0.0; s = max(0.0, min(1.0, -c / a))
            elif t > 1.0: t = 1.0; s = max(0.0, min(1.0, (b - c) / a))
    return vlen(vsub(vadd(p1, vmul(d1, s)), vadd(p2, vmul(d2, t)))), s, t


def cap_pen(c1, c2):
    """How deep two TAPERED capsules (nm, a, b, r, ra, rb) are into each other (negative = apart). Sampled along
    both segments: with tapering radii the closest pair of points is not the deepest one, and two near-parallel
    thighs let a closest-point solver pick either end -- a rest baseline that changed its mind about which."""
    best = -9.0
    for (A, Bc) in ((c1, c2), (c2, c1)):
        ab = vsub(Bc[2], Bc[1]); l2 = vdot(ab, ab) or 1e-12
        for k in range(9):
            s = k / 8.0
            p = lerp(A[1], A[2], s)
            u = max(0.0, min(1.0, vdot(vsub(p, Bc[1]), ab) / l2))
            d = vlen(vsub(p, vadd(Bc[1], vmul(ab, u))))
            best = max(best, A[4] + (A[5] - A[4]) * s + Bc[4] + (Bc[5] - Bc[4]) * u - d)
    return best


def box_sdist(p, lo, hi):
    """Signed distance from p to an axis-aligned box (negative inside)."""
    c = [(lo[k] + hi[k]) / 2 for k in range(3)]; h = [(hi[k] - lo[k]) / 2 for k in range(3)]
    d = [abs(p[k] - c[k]) - h[k] for k in range(3)]
    out = vlen([max(x, 0.0) for x in d])
    return out + min(max(d[0], max(d[1], d[2])), 0.0)


# ---- the board, measured --------------------------------------------------------------------------------
def measure_board(path=BOARD_PROP):
    p = Rpack(path)
    pos, tris = [], []
    for mi in range(p.nMeshes):
        rec = p.MESH.unpack_from(p.buf, p.offMeshes + mi * 60)
        verts, idxc, offPos, offIdx = rec[2], rec[3], rec[6], rec[11]
        base = len(pos)
        pos.extend(struct.iter_unpack("<3f", p.buf[offPos: offPos + verts * 12]))
        idx = struct.unpack_from("<%dI" % idxc, p.buf, offIdx)
        tris.extend((base + idx[k], base + idx[k + 1], base + idx[k + 2]) for k in range(0, idxc - idxc % 3, 3))
    lo = [min(v[k] for v in pos) for k in range(3)]; hi = [max(v[k] for v in pos) for k in range(3)]
    ext = [hi[k] - lo[k] for k in range(3)]
    assert ext[2] >= ext[0] and ext[2] >= ext[1], "the board's long axis is no longer mesh Z"
    s = BOARD_LEN / ext[2]
    cx, cz = (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2
    # board-local club units, origin at the deck-top centre (fixed below): x = mesh z, y = mesh y, z = -mesh x
    P = [((v[2] - cz) * s, (v[1] - lo[1]) * s, -(v[0] - cx) * s) for v in pos]
    T = []
    for (i, j, k) in tris:
        a, b, c = P[i], P[j], P[k]
        T.append((a, b, c, min(a[0], b[0], c[0]), max(a[0], b[0], c[0]), min(a[2], b[2], c[2]), max(a[2], b[2], c[2])))

    def top_at(x, z, pick=max, above=-1.0):
        best = None
        for (a, b, c, x0, x1, z0, z1) in T:
            if x < x0 or x > x1 or z < z0 or z > z1: continue
            den = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
            if abs(den) < 1e-12: continue
            l1 = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / den
            l2 = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / den
            l3 = 1.0 - l1 - l2
            if min(l1, l2, l3) < -1e-6: continue
            y = l1 * a[1] + l2 * b[1] + l3 * c[1]
            if y > above: best = y if best is None else pick(best, y)
        return best

    deck = top_at(0.0, 0.0)
    assert deck is not None, "no deck surface at the board's centre"
    bolts = [top_at(x, 0.0) for x in (-0.33, 0.33)]
    edges = [top_at(0.0, z) for z in (-0.14, 0.14)]
    kick = []
    for i in range(0, 17):
        ax = 0.40 + 0.30 * i / 16.0
        ys = [top_at(sg * min(ax, 0.699), 0.0) for sg in (-1.0, 1.0)]
        ys = [y for y in ys if y is not None]
        kick.append((ax, (max(ys) if ys else deck) - deck))
    ground = [q for q in P if q[1] < 0.012]
    wx = sum(abs(q[0]) for q in ground) / len(ground)
    wz = sorted(abs(q[2]) for q in ground)[len(ground) // 2]
    tipu = min(q[1] for q in P if abs(q[0]) > 0.66) - deck
    tipt = max(q[1] for q in P if abs(q[0]) > 0.66) - deck
    nose_tail = [max(q[1] for q in P if sg * q[0] > 0.60) for sg in (-1.0, 1.0)]
    B = dict(scale_prop=s, length=ext[2] * s, width=ext[0] * s, height=ext[1] * s, deck_top=deck,
             deck_top_bolts=bolts, deck_top_edges=edges, kick=kick, wheel_x=wx, wheel_z=wz,
             tip_under=tipu, tip_top=tipt, tail_nose_top=nose_tail,
             mesh_to_frame="local = (mesh.z, mesh.y - deck_top, -mesh.x) about the bbox centre, prop scale 1.4/len")
    under = top_at(0.0, 0.0, min, 0.05)
    B["deck_thick"] = (deck - under) if under is not None else 0.024
    # THE RAILS: the deck's own edge between the trucks (no truck or wheel is out there), mid-thickness
    mid = [q for q in P if abs(q[0]) < 0.25]
    rz = max(abs(q[2]) for q in mid)
    et = top_at(0.0, rz - 0.012); eu = top_at(0.0, rz - 0.012, min, 0.05)
    B["rail_z"] = rz
    B["rail_y"] = (((et if et is not None else deck) + (eu if eu is not None else under or deck - 0.024)) / 2.0) - deck
    B["com_dy"] = 0.5 * deck                   # the roll axis of a flip: half the deck height under the top
    return B


def kick_top(B, ax):
    """Deck-top height above the flat deck at |x| = ax (club), from the measured profile."""
    ax = abs(ax)
    k = B["kick"]
    if ax <= k[0][0]: return 0.0
    for (a0, y0), (a1, y1) in zip(k, k[1:]):
        if ax <= a1: return max(0.0, y0 + (y1 - y0) * (ax - a0) / (a1 - a0))
    return max(0.0, k[-1][1])


class Rigid:
    """p -> t + qrot(q, p)."""
    def __init__(self, q=None, t=None):
        self.q = q or [0.0, 0.0, 0.0, 1.0]; self.t = t or [0.0, 0.0, 0.0]
    def ap(self, p): return vadd(self.t, qrot(self.q, p))
    def __mul__(self, o): return Rigid(qnorm(qmul(self.q, o.q)), self.ap(o.t))


def about(pivot, q):
    """Rotate by q about a pivot point (both in the same frame)."""
    return Rigid(None, pivot) * Rigid(q) * Rigid(None, vmul(pivot, -1.0))


# ---- the rider: a rig adapter over rig_map, everything measured from the pack --------------------------
class Rider:
    def __init__(self, pack_path, B):
        p = Rpack(pack_path)
        self.p, self.B = p, B
        nodes = p.nodes(); self.nodes = nodes
        RM = rig_map(p); self.RM = RM
        assert not RM["_missing"], "rig incomplete: %s" % RM["_missing"]
        self.base_q = ZB.stand_q(nodes, RM)
        wp0, wq0 = fk_world(nodes, self.base_q)
        self.wp0, self.wq0 = wp0, wq0
        self.pel = RM["pelvis"]; self.spine = [RM["spine_01"], RM["spine_02"], RM["spine_03"]]
        self.neck, self.head = RM["neck_01"], RM["head"]
        self.leg = {s: (RM["thigh_" + s], RM["calf_" + s], RM["foot_" + s]) for s in "lr"}
        self.arm = {s: (RM["upperarm_" + s], RM["lowerarm_" + s], RM["hand_" + s]) for s in "lr"}
        lg = self.leg["l"]
        self.L = math.dist(wp0[lg[0]], wp0[lg[1]]) + math.dist(wp0[lg[1]], wp0[lg[2]])
        sk = p.skinned_positions(with_bones=True)
        ys = [v[1] for v in sk]
        self.scale = BODY_H / (max(ys) - min(ys))
        self.c2r = 1.0 / self.scale
        # her FRONT from both feet (toe-out cancels), her LEFT = UP x front, the board's +Z = LEFT x UP
        fw = [0.0, 0.0, 0.0]
        for s in "lr":
            fw = vadd(fw, vsub(wp0[RM["ball_" + s]], wp0[RM["foot_" + s]]))
        self.fwd = ZB.flat(fw)
        self.lat = vnorm(vcross(UP, self.fwd))
        lt = ZB.flat(vsub(wp0[self.leg["l"][0]], wp0[self.leg["r"][0]]))
        assert vdot(lt, self.lat) > math.cos(math.radians(8.0)), "her hips and her feet disagree about which way is left"
        zb = vcross(self.lat, UP)
        assert vdot(zb, self.fwd) > 0.999
        self.qB0 = q_from_basis(self.lat, UP, zb)
        # ---- the feet: sole samples, rigid below the ankle (the ball bone keeps its rest local) ----
        fam = {}
        for s in "lr":
            st = {self.leg[s][2]}; grew = True
            while grew:
                grew = False
                for i, nd in enumerate(nodes):
                    if nd["parent"] in st and i not in st: st.add(i); grew = True
            fam[s] = st
        pts = {"l": [], "r": []}
        for v in sk:
            for s in "lr":
                if v[3] in fam[s]: pts[s].append(v)
        self.floor = min(v[1] for s in "lr" for v in pts[s])
        self.sole, self.ref0, self.ankoff = {}, {}, {}
        for s in "lr":
            vs = pts[s]
            y0, y1 = min(v[1] for v in vs), max(v[1] for v in vs)
            low = sorted([v for v in vs if v[1] < y0 + 0.30 * (y1 - y0)], key=lambda v: (v[1], v[0], v[2]))
            samp = low[::max(1, len(low) // 90)]
            samp += [max(vs, key=lambda v: vdot(v, self.fwd)), min(vs, key=lambda v: vdot(v, self.fwd)),
                     max(vs, key=lambda v: vdot(v, self.lat)), min(vs, key=lambda v: vdot(v, self.lat))]
            ref = [sum(v[0] for v in low) / len(low), self.floor, sum(v[2] for v in low) / len(low)]
            self.ref0[s] = ref
            self.ankoff[s] = vsub(wp0[self.leg[s][2]], ref)
            self.sole[s] = [(v[3], qrot(qinv(wq0[v[3]]), vsub(list(v[:3]), wp0[v[3]])), vsub(list(v[:3]), ref)) for v in samp]
            self.anksole = wp0[self.leg[s][2]][1] - self.floor
        # the board at rest: centred under the mid-feet, flat on the floor
        self.G0 = [(self.ref0["l"][0] + self.ref0["r"][0]) / 2, self.floor + B["deck_top"] * self.c2r,
                   (self.ref0["l"][2] + self.ref0["r"][2]) / 2]
        # ---- capsules: the canonical ten + measured arms, carried by their bones ----
        # ⛔ MEASURED IN THE BIND POSE. The vertices are the bind (a T-pose on reva); measuring them against the
        # relaxed-arm segment read her forearm as a 0.54 u radius -- a forearm "inside" her calf on frame 0.
        caps = body_capsules(p, RM)
        wpr, wqr = fk_world(nodes)
        byb = {}
        for v in sk: byb.setdefault(v[3], []).append(v[:3])
        for s in "lr":
            ua, la, ha = self.arm[s]
            for nm, b, c in (("upperarm_" + s, ua, la), ("lowerarm_" + s, la, ha)):
                a, t = wpr[b], wpr[c]
                ab = vsub(t, a); l2 = vdot(ab, ab) or 1e-12
                ds = []
                for v in byb.get(b, []):
                    u = max(0.0, min(1.0, vdot(vsub(v, a), ab) / l2))
                    ds.append(vlen(vsub(v, vadd(a, vmul(ab, u)))))
                ds.sort()
                caps.append(dict(name=nm, a=list(a), b=list(t), r=ds[int(len(ds) * 0.9)] if ds else 0.03))
        self.caps = []
        for c in caps:                                    # carried in the frame they were measured in: the bind
            b = RM[c["name"]]
            a, t = c["a"], c["b"]
            ab = vsub(t, a); l2 = vdot(ab, ab) or 1e-12
            band = ([], [])
            for v in byb.get(b, []):                      # TAPERED: p90 radius near each end (a calf is thin at the ankle)
                u = vdot(vsub(v, a), ab) / l2
                if u < 0.35 or u > 0.65:
                    band[0 if u < 0.35 else 1].append(vlen(vsub(v, vadd(a, vmul(ab, max(0.0, min(1.0, u)))))))
            ends = [min(c["r"], sorted(bd)[int(len(bd) * 0.9)]) if len(bd) >= 20 else c["r"] for bd in band]
            if c["name"] == "head":
                # ⛔ THE HEAD IS THE SKULL, NOT THE PONYTAIL: reva's head-weighted p90 is her hair (0.22 u), and a
                # head turned to watch the nose "hit" her own shoulder through it. Front-half vertices only.
                fr = sorted(vlen(vsub(v, vadd(a, vmul(ab, max(0.0, min(1.0, vdot(vsub(v, a), ab) / l2))))))
                            for v in byb.get(b, []) if vdot(vsub(v, a), self.fwd) > 0.0)
                if len(fr) >= 20:
                    ends = [min(e, fr[int(len(fr) * 0.9)]) for e in ends]
            self.caps.append((c["name"], b, qrot(qinv(wqr[b]), vsub(a, wpr[b])),
                              qrot(qinv(wqr[b]), vsub(t, wpr[b])), c["r"], ends[0], ends[1]))
        adj = {("pelvis", "spine_01"), ("spine_01", "spine_02"), ("spine_02", "spine_03"), ("spine_03", "neck_01"),
               ("neck_01", "head"), ("pelvis", "thigh_l"), ("pelvis", "thigh_r"), ("thigh_l", "calf_l"),
               ("thigh_r", "calf_r"), ("spine_03", "upperarm_l"), ("spine_03", "upperarm_r"),
               ("upperarm_l", "lowerarm_l"), ("upperarm_r", "lowerarm_r"), ("neck_01", "upperarm_l"),
               ("neck_01", "upperarm_r")}
        self.pairs = [(i, j) for i in range(len(self.caps)) for j in range(i + 1, len(self.caps))
                      if (self.caps[i][0], self.caps[j][0]) not in adj and (self.caps[j][0], self.caps[i][0]) not in adj]
        cw = self.posed_caps(wp0, wq0)
        self.pen0 = {pr: max(0.0, cap_pen(cw[pr[0]], cw[pr[1]])) for pr in self.pairs}
        # ---- the hands: the PALM is the grip point (own vertices + the whole hand's, averaged -- the palm on a
        # fingered hand, the knuckles on a mitten), carried in the hand's bind frame like the capsules ----
        self.grip, self.glen, self.arml, self.handpts, self.hrig = {}, {}, {}, {}, {}
        for s in "lr":
            ua, la, ha = self.arm[s]
            famh = {ha}; grew = True
            while grew:
                grew = False
                for i, nd in enumerate(nodes):
                    if nd["parent"] in famh and i not in famh: famh.add(i); grew = True
            own = byb.get(ha, [])
            allv = [v[:3] for v in sk if v[3] in famh]
            assert own and allv, "no vertices on the %s hand" % s
            co = [sum(v[k] for v in own) / len(own) for k in range(3)]
            ca = [sum(v[k] for v in allv) / len(allv) for k in range(3)]
            g = [(co[k] + ca[k]) / 2.0 for k in range(3)]
            self.grip[s] = qrot(qinv(wqr[ha]), vsub(g, wpr[ha]))
            self.glen[s] = vlen(self.grip[s])
            self.arml[s] = vlen(vsub(wpr[la], wpr[ua])) + vlen(vsub(wpr[ha], wpr[la]))
            self.handpts[s] = [qrot(qinv(wqr[ha]), vsub(list(v), wpr[ha])) for v in own[::max(1, len(own) // 60)]]
        try:                                              # fingers where the rig has them (kaeli); a mitten has none
            from authorlib import find_hands, HandRig
            H = find_hands(p, nodes, p.node_names(), RM["hand_l"], RM["hand_r"])
            for s, k in (("l", "L"), ("r", "R")):
                if k in H and len(H[k]["fingers"]) >= 4:
                    self.hrig[s] = HandRig(nodes, self.base_q, {}, H[k], RM["lowerarm_" + s])
        except Exception as e:                            # noqa -- a grab without curled fingers still holds
            print("z_sk_ride: no finger rig (%s)" % e)
        self._air = {}

    def posed_caps(self, w, wq):
        return [(nm, vadd(w[b], qrot(wq[b], la)), vadd(w[b], qrot(wq[b], lb)), r, ra, rb) for (nm, b, la, lb, r, ra, rb) in self.caps]

    # ---- the board ------------------------------------------------------------------------------------
    def board_rigid(self, bs, with_roll=True):
        """bs: dx dy dz (club, board axes), pitch about a local Z pivot (ppx, ppy club), yaw, roll (degrees).
        Local, applied innermost first: roll about the CoM line, yaw about the centre, pitch about the pivot."""
        c2r = self.c2r
        R = about([bs.get("ppx", 0.0) * c2r, bs.get("ppy", 0.0) * c2r, 0.0], qa([0, 0, 1], bs.get("pitch", 0.0)))
        if bs.get("tilt", 0.0):                           # a TWEAK about the long axis the feet ride (a method's board)
            R = R * Rigid(qa([1, 0, 0], bs["tilt"]))
        R = R * Rigid(qa([0, 1, 0], bs.get("yaw", 0.0)))
        if with_roll and bs.get("roll", 0.0):
            R = R * about([0.0, -self.B["com_dy"] * c2r, 0.0], qa([1, 0, 0], bs["roll"]))
        lift = [bs.get("dx", 0.0) * c2r, bs.get("dy", 0.0) * c2r, bs.get("dz", 0.0) * c2r]
        return Rigid(self.qB0, self.G0) * Rigid(None, lift) * R

    def foot_world(self, s, spec, BT):
        """spec = (mode, x, z, hover, yaw[, frac]). mode floor: x/z club along her left/front from the board's
        rest centre, on the floor. deck/air/flip: board-local club x/z, hover above the deck top, the foot
        tilted with the board by frac. The foot rests on its LOWEST sole point, never its centre."""
        mode, x, z, hover, yaw = spec[:5]
        yq = qa(UP, yaw)
        if mode == "floor":
            base = vadd(self.G0, vadd(vmul(self.lat, x * self.c2r), vmul(self.fwd, z * self.c2r)))
            miny = min(qrot(yq, rel)[1] for (_b, _l, rel) in self.sole[s])
            return [base[0], self.floor + hover * self.c2r - miny, base[2]], yq
        qBd = qnorm(qmul(BT.q, qinv(self.qB0)))
        frac = spec[5] if len(spec) > 5 else 1.0
        qd = qnorm(qmul(qslerp([0.0, 0.0, 0.0, 1.0], qBd, frac), yq))
        ib = qinv(BT.q)
        miny = 9.0
        for (_b, _l, rel) in self.sole[s]:
            o = qrot(ib, qrot(qd, rel))
            miny = min(miny, o[1] - kick_top(self.B, x + o[0] * self.scale) * self.c2r)
        loc = [x * self.c2r, hover * self.c2r - miny, z * self.c2r]
        return BT.ap(loc), qd

    # ---- the pose -------------------------------------------------------------------------------------
    def build(self, S, feet, BTr=None):
        nodes, pel, lat, fwd, c2r = self.nodes, self.pel, self.lat, self.fwd, self.c2r
        qo = dict(self.base_q); to = {}
        off = vadd(vmul(lat, S["px"] * c2r), vmul(fwd, S["pz"] * c2r)); off[1] = S["py"] * c2r
        to[pel] = translate_node_world(nodes, pel, off)
        yq = qa(UP, S["yaw"])
        qo[pel] = rotate_bone_world(nodes, pel, UP, math.radians(S["yaw"]), qo)
        qo[pel] = rotate_bone_world(nodes, pel, qrot(yq, lat), math.radians(S["lean"]) * 0.45, qo)
        qo[pel] = rotate_bone_world(nodes, pel, qrot(yq, fwd), math.radians(S["roll"]) * 0.4, qo)
        tw = S["yaw"]
        for b in self.spine:
            qo[b] = rotate_bone_world(nodes, b, UP, math.radians(S["twist"]) / 3.0, qo)
            tw += S["twist"] / 3.0
            cq = qa(UP, tw)
            qo[b] = rotate_bone_world(nodes, b, qrot(cq, lat), math.radians(S["lean"]) * 0.55 / 3.0, qo)
            qo[b] = rotate_bone_world(nodes, b, qrot(cq, fwd), math.radians(S["roll"]) * 0.6 / 3.0, qo)
        for s, sg in (("l", 1.0), ("r", -1.0)):
            ref, qd, pole = feet[s]
            ch = self.leg[s]
            qo.update(solve_limb(nodes, ch, vadd(ref, qrot(qd, self.ankoff[s])), pole=pole, q_override=qo, t_override=to))
            qo[ch[2]] = hold_world_rotation(nodes, ch[2], qnorm(qmul(qd, self.wq0[ch[2]])), qo, to)
        cq = qa(UP, S["yaw"] + S["twist"])
        clat, cfwd = qrot(cq, lat), qrot(cq, fwd)
        for s, sg in (("l", 1.0), ("r", -1.0)):          # the shoulder girdle: drop / reach forward (a grab's last inches)
            dr, fw = S.get("clav", {}).get(s, (0.0, 0.0))
            cb = self.RM["clavicle_" + s]
            if dr: qo[cb] = rotate_bone_world(nodes, cb, cfwd, math.radians(-sg * dr), qo)
            if fw: qo[cb] = rotate_bone_world(nodes, cb, UP, math.radians(-sg * fw), qo)
        for s, sg in (("l", 1.0), ("r", -1.0)):
            abd, swing, bend = S["arms"][s]
            ua, la, ha = self.arm[s]
            a = math.radians(abd)
            d0 = vadd(vmul(clat, sg * math.sin(a)), [0.0, -math.cos(a), 0.0])
            d = qrot(q_axis_angle(clat, -math.radians(swing)), d0)
            qo[ua] = aim_bone(nodes, ua, la, d, qo, to)
            qo[la] = aim_bone(nodes, la, ha, vnorm(vadd(d, vadd(vmul(cfwd, bend), [0.0, 0.35 * bend, 0.0]))), qo, to)
        for s, (gx, edge, blend, curl, dn) in S.get("grab", {}).items():
            if blend <= 1e-9 or BTr is None: continue
            qo = self.grab_arm(qo, to, s, gx, edge, blend, curl, dn, BTr, clat, cfwd)
        turn, down = S["look"]
        qo[self.neck] = rotate_bone_world(nodes, self.neck, UP, math.radians(turn) * 0.35, qo)
        qo[self.head] = rotate_bone_world(nodes, self.head, UP, math.radians(turn) * 0.65, qo)
        hl = qrot(qa(UP, S["yaw"] + S["twist"] + turn), lat)
        qo[self.head] = rotate_bone_world(nodes, self.head, hl, math.radians(down), qo)
        return qo, to

    def frame(self, S):
        # the FEET ride the board's position and pitch, never its flip or its spin: a foot over a rolling or
        # turning board keeps its place in her stance and the board turns underneath it
        BT = self.board_rigid({k: v for k, v in S["board"].items() if k not in ("roll", "yaw")}, with_roll=False)
        BTr = self.board_rigid(S["board"], with_roll=True)
        feet = {}
        for s, sg in (("l", 1.0), ("r", -1.0)):
            spec = S["feet"][s]
            ref, qd = self.foot_world(s, spec, BT)
            pole = vadd(qrot(qa(UP, spec[4]), self.fwd), vmul(self.lat, sg * (0.25 + S.get("kneeout", 0.0))))
            feet[s] = (ref, qd, pole)
        qo, to = self.build(S, feet, BTr)
        return qo, to, BTr, feet

    # ---- THE GRAB: a palm onto a rail -------------------------------------------------------------------
    def rail_local(self, gx, edge):
        """Board-local club point on a rail: gx along the board (+ nose), edge +1 toe side / -1 heel side."""
        return [gx, self.B["rail_y"], edge * self.B["rail_z"]]

    def grab_arm(self, qo, to, s, gx, edge, blend, curl, dn, BTr, clat, cfwd):
        """IK the arm so the PALM sits on the rail, the hand aimed along the reach (bent toward the board's own
        down by dn), then blend from the free arm by ROTATION -- a blend by position swings the elbow plane."""
        nodes = self.nodes
        ua, la, ha = self.arm[s]
        sg = 1.0 if s == "l" else -1.0
        rail = BTr.ap(vmul(self.rail_local(gx, edge), self.c2r))
        w, wq = fk_world(nodes, qo, to)
        bdown = qrot(BTr.q, [0.0, -1.0, 0.0])
        d = vnorm(vadd(vnorm(vsub(rail, w[ua])), vmul(bdown, dn)))
        wrist = vsub(rail, vmul(d, self.glen[s]))
        pole = vadd(vadd(vmul(clat, sg * 1.0), vmul(cfwd, -0.35)), [0.0, 0.25, 0.0])
        g = dict(qo)
        g.update(solve_limb(nodes, (ua, la, ha), wrist, pole=pole, q_override=g, t_override=to))
        w2, wq2 = fk_world(nodes, g, to)
        u = qrot(wq2[ha], self.grip[s])
        A = q_between(u, d)
        rp = wq2[la]
        g[ha] = qnorm(qmul(qinv(rp), qmul(A, qmul(rp, g.get(ha, nodes[ha]["q"])))))
        out = dict(qo)
        for b in (ua, la, ha):
            out[b] = qnorm(qslerp(qnorm(qo.get(b, nodes[b]["q"])), g[b], blend)) if blend < 1.0 else g[b]
        if curl > 1e-6 and s in self.hrig:
            out = self.hrig[s].pose(out, curl=curl * blend, thumb=0.6 * curl * blend)
        return out

    def grip_world(self, s, w, wq):
        ha = self.arm[s][2]
        return vadd(w[ha], qrot(wq[ha], self.grip[s]))

    # ---- THE AIRS ------------------------------------------------------------------------------------
    AIRK = {"indy": dict(TK=8, G0=4, GR=10, RL=22, G1=25, EX=26, AB=29, side="r", gx=-0.06, edge=+1.0, dn=0.55, curl=0.85,
                         lean=80.0, twist=18.0, roll=14.0, py=-0.10, pz=-0.10, px=-0.02, dz=0.14, tilt=0.0, pitch=-4.0,
                         clav=(26.0, 16.0), reach=0.95, kneeout=1.3),
            "method": dict(TK=9, G0=4, GR=11, RL=24, G1=27, EX=29, AB=32, side="l", gx=+0.02, edge=-1.0, dn=0.35, curl=0.85,
                           lean=10.0, twist=30.0, roll=-24.0, py=+0.0, pz=+0.08, px=+0.04, dz=-0.52, tilt=50.0, pitch=0.0,
                           clav=(26.0, -10.0), reach=0.95, kneeout=0.35)}

    def s_grab(self, t, D): return self.air_like(t, D, "indy")
    def s_method(self, t, D): return self.air_like(t, D, "method")

    def air_h(self, kind, D):
        """Solve the board's lift (club) at the hold: the grabbing rail at REACH_FRAC of the arm from the shoulder.
        The rail rises straight up in model space with the lift, the shoulder does not move with it -> closed form."""
        if kind in self._air: return self._air[kind]
        A = self.AIRK[kind]; s = A["side"]
        tm = 0.5 * (A["GR"] + A["RL"]) / FPS
        S = self.air_like(tm, D, kind, h=0.0)
        S.pop("grab", None)
        qo, to, BTr, _f = self.frame(S)
        w, _wq = fk_world(self.nodes, qo, to)
        sh = w[self.arm[s][0]]
        rail0 = BTr.ap(vmul(self.rail_local(A["gx"], A["edge"]), self.c2r))
        v = vsub(rail0, sh)
        Rt = A.get("reach", REACH_FRAC) * self.arml[s] + self.glen[s]
        disc = v[1] * v[1] - vdot(v, v) + Rt * Rt
        assert disc >= 0.0, "%s: the rail is out of reach sideways (%.3f rig off a %.3f arm) -- lean her into it" % (
            kind, math.sqrt(v[0] ** 2 + v[2] ** 2), Rt)
        h = max(0.0, -v[1] - math.sqrt(disc)) * self.scale
        self._air[kind] = h
        print("z_sk_ride: %-6s the board rises %.3f u at the hold (shoulder->rail %.3f rig = %.2f of a %.3f arm+palm)"
              % (kind, h, Rt, A.get("reach", REACH_FRAC), self.arml[s] + self.glen[s]))
        return h

    def air_like(self, t, D, kind, h=None):
        A = self.AIRK[kind]; R0 = self.s_ride(0.0); S = self.s_ride(0.0); K = self.keys
        if h is None: h = self.air_h(kind, D)
        TK, G0, GR, RL, G1, EX, AB = [A[k] / FPS for k in ("TK", "G0", "GR", "RL", "G1", "EX", "AB")]
        up = K([(0.0, 0.0), (TK, 1.0), (RL, 1.0), (EX, 0.0), (D, 0.0)], t)         # the tuck, and the board with it
        body = K([(0.0, 0.0), (TK, 1.0), (RL, 1.0), (EX + 1 / FPS, 0.0), (D, 0.0)], t)
        bs = {}
        if up > 1e-9:
            bs = dict(dy=h * up, dz=A["dz"] * up)
            if A["tilt"]: bs["tilt"] = A["tilt"] * up
            if A["pitch"]: bs["pitch"] = A["pitch"] * up
        S["board"] = bs; S["btag"] = "air" if up > 1e-9 else "4"
        # the body: fold (indy) or arch (method), the girdle drops toward the rail, the free arm out for balance
        land = K([(0.0, 0.0), (EX, 0.0), (AB, 1.0), (D, 0.0)], t)                 # the landing's absorb
        S["py"] = R0["py"] + A["py"] * body - 0.17 * land
        S["pz"] = R0["pz"] + A["pz"] * body - 0.03 * land
        S["px"] = R0["px"] + A["px"] * body
        S["lean"] = R0["lean"] + (A["lean"] - R0["lean"]) * body + 10.0 * land
        S["twist"] = R0["twist"] + (A["twist"] - R0["twist"]) * body
        S["roll"] = R0["roll"] + A["roll"] * body
        S["clav"] = {A["side"]: (A["clav"][0] * body, A["clav"][1] * body)}
        S["kneeout"] = A["kneeout"] * up
        # the grab: the palm reaches the rail by GR, holds to RL, lets go by G1
        gb = K([(0.0, 0.0), (G0, 0.0), (GR, 1.0), (RL, 1.0), (G1, 0.0), (D, 0.0)], t)
        S["grab"] = {A["side"]: (A["gx"], A["edge"], gb, A["curl"], A["dn"])}
        S["_hold"] = (GR - 1e-9 <= t <= RL + 1e-9)
        # the arms: the grabbing arm swings toward the board before the IK takes it; the other one is the show
        wob = math.sin(2.0 * math.pi * (t - GR) / (RL - GR)) if GR <= t <= RL else 0.0
        if kind == "indy":
            reach = (18.0, 38.0, 0.25); free = (112.0 + 6.0 * wob, 34.0, 0.30)
            look = (44.0, 24.0)
            fside = "l"
        else:
            reach = (22.0, -38.0, 0.20); free = (100.0 + 5.0 * wob, -18.0, 0.15)
            look = (74.0, 6.0)
            fside = "r"
        gs = A["side"]
        S["arms"] = {gs: tuple(mixf(R0["arms"][gs][i], reach[i], body) for i in range(3)),
                     fside: tuple(mixf(R0["arms"][fside][i], free[i], body) + (0.0 if i else 12.0 * land) for i in range(3))}
        lk = K([(0.0, 0.0), (TK, 1.0), (RL, 1.0), (EX, 0.0), (D, 0.0)], t)
        S["look"] = (R0["look"][0] + (look[0] - R0["look"][0]) * lk, R0["look"][1] + (look[1] - R0["look"][1]) * lk + 8.0 * land)
        return S

    # ---- THE FILMER'S RIDE: the same roll, one arm out holding the camera ----------------------------
    def s_film(self, t, D=2.0):
        S = self.s_ride(t, D)
        sw = math.sin(2.0 * math.pi * t / D)
        sw2 = math.sin(2.0 * math.pi * t / D + 1.2)
        # the right arm goes OUT and FORWARD: abduction away from the ribs, swung ahead of her, elbow soft.
        # It breathes a little (a held camera is never still) but far less than a balancing arm.
        S["arms"] = {"l": (34.0 + 8.0 * sw2, 6.0 + 7.0 * sw, 0.55),          # the balance arm works harder
                     "r": (30.0 + 2.5 * sw2, 74.0 + 3.0 * sw, 0.42)}         # the camera arm, out front
        # and she is watching the shot, not the road
        S["look"] = (40.0 - 3.0 * sw2, 6.0)
        return S

    # ---- THE RIDE: frame 0 of this is where every trick starts and ends ------------------------------
    def s_ride(self, t, D=2.0):
        deck = self.B["deck_top"]
        bob = 0.5 - 0.5 * math.cos(2.0 * math.pi * 2.0 * t / D)
        sw = math.sin(2.0 * math.pi * t / D)
        sw2 = math.sin(2.0 * math.pi * t / D + 1.2)
        return dict(px=0.018 * sw, pz=-0.02, py=deck - RIDE_SINK - 0.040 * bob, kneeout=RIDE_KNEEOUT,
                    yaw=12.0 + 2.0 * sw2, lean=24.0 + 3.0 * bob, roll=-1.8 * sw, twist=16.0 - 2.0 * sw2,
                    feet={"l": ("deck", FRONT_X, 0.0, 0.0, FRONT_YAW), "r": ("deck", BACK_X, 0.0, 0.0, BACK_YAW)},
                    arms={"l": (26.0 + 7.0 * sw2, 14.0 + 6.0 * sw, 0.55 + 0.10 * bob),
                          "r": (20.0 - 6.0 * sw2, -10.0 + 5.0 * sw, 0.45 + 0.10 * bob)},
                    look=(56.0 - 2.0 * sw2, 14.0 + 3.0 * bob), board={})

    # ---- trick kit ----------------------------------------------------------------------------------------
    def strike_angle(self):
        """Nose-up pitch about the rear wheels' contact at which the TAIL touches the floor -- measured."""
        rx = -0.70 + self.B["wheel_x"]; ry = self.B["tip_under"] + self.B["deck_top"]
        return math.degrees(math.atan2(ry, -rx))

    @staticmethod
    def keys(ks, t):
        """[(time, value)] smoothstep between keys; values may be tuples."""
        if isinstance(ks[0][1], tuple):
            return tuple(profile([(k[0], k[1][i], ease_smooth) for k in ks], t) for i in range(len(ks[0][1])))
        return profile([(k[0], k[1], ease_smooth) for k in ks], t)

    # ---- THE PUSH: the back foot plants beside the board and sweeps at the path speed ---------------------
    def s_push(self, t, D):
        R0 = self.s_ride(0.0); S = self.s_ride(0.0); K = self.keys
        dt, eps = self.B["deck_top"], 1e-9
        tL, tP, tE, tB = 5 / FPS, 11 / FPS, 19 / FPS, 27 / FPS
        calf = next(c for c in self.caps if c[0] == "calf_r")
        PZ = max(0.34, self.B["width"] / 2 + calf[6] * self.scale + 0.035)   # beside the board by her own ankle's thickness
        PX0 = 0.05; PX1 = PX0 - SPEED * (tE - tP)              # a planted foot moves back exactly as fast as she rolls
        kt = K([(0.0, 0.0), (tP, 1.0), (tB, 1.0), (D, 0.0)], t)
        kp = K([(0.0, 0.0), (tP, 1.0), (tE, 1.0), (D, 0.0)], t)
        if t <= tL + eps:
            fr = ("deck", BACK_X, 0.0, 0.0, BACK_YAW)
        elif t < tP - eps:
            u = (t - tL) / (tP - tL); e = ease_smooth(u)
            fr = ("free", mixf(BACK_X, PX0, e), PZ * ease_smooth(lin(u / 0.6)),
                  dt * (1.0 - ease_in(u)) + 0.10 * math.sin(math.pi * u), mixf(BACK_YAW, 80.0, e))
        elif t <= tE + eps:
            u = (t - tP) / (tE - tP)
            fr = ("floor", mixf(PX0, PX1, u), PZ, 0.0, 80.0); S["sweep"] = True
        elif t < tB - eps:
            u = (t - tE) / (tB - tE)
            fr = ("free", mixf(PX1, BACK_X, ease_smooth(u)), PZ * (1.0 - ease_smooth(lin((u - 0.45) / 0.55))),
                  dt * ease_smooth(lin(u / 0.55)) + 0.12 * math.sin(math.pi * lin(u / 0.95)), mixf(80.0, BACK_YAW, ease_smooth(u)))
        else:
            fr = ("deck", BACK_X, 0.0, 0.0, BACK_YAW)
        S["feet"] = {"l": ("deck", FRONT_X, 0.0, 0.0, mixf(FRONT_YAW, 70.0, kt)), "r": fr}
        S["px"] = R0["px"] + K([(0.0, 0.0), (tP, 0.04), (tE, -0.10), (tB, -0.02), (D, 0.0)], t)
        S["py"] = R0["py"] + K([(0.0, 0.0), (tL, -0.06), (tP, -0.20), (tE, -0.24), (tB, -0.08), (D, 0.0)], t)
        S["pz"] = R0["pz"] + 0.04 * kp
        S["yaw"] = R0["yaw"] + 46.0 * kt; S["twist"] = R0["twist"] + 10.0 * kt
        S["lean"] = R0["lean"] + 10.0 * kp; S["roll"] = R0["roll"] - 4.0 * kp
        S["arms"] = {"l": K([(0.0, R0["arms"]["l"]), (tE, (24.0, -20.0, 0.5)), (D, R0["arms"]["l"])], t),
                     "r": K([(0.0, R0["arms"]["r"]), (tE, (22.0, 28.0, 0.55)), (D, R0["arms"]["r"])], t)}
        S["look"] = (R0["look"][0] - 50.0 * kt, R0["look"][1] + 6.0 * kp)
        return S

    # ---- THE OLLIE and its two children ------------------------------------------------------------------
    POPK = {"pop": dict(SET=4, CR=8, ST=10, LV=14, CA=18, LD=22, AB=26, H=0.45, arc=0.28, R0=0, R1=1, spin=0.0, roll=0.0),
            "kf": dict(SET=4, CR=9, ST=11, LV=15, CA=21, LD=24, AB=28, H=0.45, arc=0.32, R0=12, R1=20, spin=0.0, roll=-360.0),
            "shove": dict(SET=4, CR=8, ST=10, LV=13, CA=20, LD=22, AB=26, H=0.30, arc=0.22, R0=11, R1=19, spin=-180.0, roll=0.0)}

    def s_pop(self, t, D): return self.pop_like(t, D, "pop")
    def s_kf(self, t, D): return self.pop_like(t, D, "kf")
    def s_shove(self, t, D): return self.pop_like(t, D, "shove")

    def pop_like(self, t, D, kind):
        Kp = self.POPK[kind]; R0 = self.s_ride(0.0); S = self.s_ride(0.0); K = self.keys
        B, eps = self.B, 1e-9
        dt = B["deck_top"]
        tSET, tCR, tST, tLV, tCA, tLD, tAB, tR0, tR1 = [Kp[k] / FPS for k in ("SET", "CR", "ST", "LV", "CA", "LD", "AB", "R0", "R1")]
        th_s = self.strike_angle()
        def seg(a, b): return lin((t - a) / (b - a))
        # ---- the board: rear-wheel pivot to the tail strike, a ballistic arc, levelled by the front foot ----
        bs, tag = {}, "4"
        if tCR + eps < t < tST - eps:
            bs = dict(pitch=th_s * ease_in(seg(tCR, tST)), ppx=-B["wheel_x"], ppy=-dt); tag = "rear"
        elif tST - eps <= t < tLD - eps:
            u = (t - tST) / (tLD - tST)
            # the front foot levels it about the same rear contact it was popped about, while the arc lifts it: every
            # board point is then above the floor whenever the lift is (the tail only rises as the pitch comes off)
            bs = dict(pitch=th_s * (1.0 - ease_smooth(seg(tST, tLV))), ppx=-B["wheel_x"], ppy=-dt, dy=Kp["H"] * 4.0 * u * (1.0 - u))
            tag = "tail" if abs(t - tST) < eps else "air"
            if Kp["roll"]: bs["roll"] = Kp["roll"] * ease_smooth(seg(tR0, tR1))
            if Kp["spin"]: bs["yaw"] = Kp["spin"] * ease_smooth(seg(tR0, tR1))
        elif t >= tLD - eps and Kp["spin"]:
            bs = dict(yaw=Kp["spin"])
        S["board"] = bs; S["btag"] = tag
        # ---- the feet ----
        if t <= tSET + eps:
            k = ease_smooth(t / tSET)
            fl = ("deck", mixf(FRONT_X, 0.10, k), 0.0, 0.0, mixf(FRONT_YAW, 32.0, k))
            fr = ("deck", mixf(BACK_X, -0.47, k), 0.0, 0.0, mixf(BACK_YAW, 0.0, k))
        elif t < tST - eps:
            fl = ("deck", 0.10, 0.0, 0.0, 32.0, 0.7); fr = ("deck", -0.47, 0.0, 0.0, 0.0, 0.6)
        elif abs(t - tST) <= eps:
            fl = ("deck", 0.10, 0.0, 0.0, 32.0, 0.7); fr = ("deck", -0.47, 0.0, 0.0, 0.0, 0.6)
        elif t < tCA - eps:
            if kind == "pop":
                ul, uc = seg(tST, tLV), seg(tLV, tCA)
                fl = ("air", mixf(mixf(0.10, 0.40, ease_out(ul)), FRONT_X, ease_smooth(uc)), 0.0,
                      0.025 * math.sin(math.pi * ul) + 0.05 * math.sin(math.pi * uc), mixf(32.0, FRONT_YAW, ease_smooth(uc)), mixf(0.7, 1.0, ul))
                ub = seg(tST, tCA)
                fr = ("air", mixf(-0.47, BACK_X, ease_smooth(ub)), 0.0, 0.05 * math.sin(math.pi * ub), mixf(0.0, BACK_YAW, ease_smooth(ub)), mixf(0.6, 1.0, ul))
            else:
                flick = kind == "kf"
                if t < tR0 - eps:
                    u = seg(tST, tR0)
                    if flick:
                        fl = ("air", mixf(0.10, 0.40, ease_out(u)), -0.24 * ease_smooth(u), 0.08 * ease_out(u), mixf(32.0, 40.0, u), mixf(0.7, 1.0, u))
                        fr = ("air", mixf(-0.47, -0.42, u), 0.0, 0.10 * ease_out(u), 0.0, mixf(0.6, 1.0, u))
                    else:
                        fl = ("air", mixf(0.10, 0.18, u), 0.0, 0.05 * ease_out(u), mixf(32.0, 26.0, u), mixf(0.7, 1.0, u))
                        fr = ("air", mixf(-0.47, -0.50, u), -0.08 * ease_smooth(u), 0.05 * ease_out(u), 0.0, mixf(0.6, 1.0, u))
                elif t <= tR1 + eps:
                    u = seg(tR0, tR1); e = ease_smooth(u); sn = math.sin(math.pi * u)
                    if flick:
                        fl = ("flip", mixf(0.40, FRONT_X, e), mixf(-0.24, 0.0, e), mixf(0.08, 0.10, u) + 0.08 * sn, mixf(40.0, FRONT_YAW, e))
                        fr = ("flip", mixf(-0.42, BACK_X, e), 0.0, 0.10 + 0.06 * sn, mixf(0.0, BACK_YAW, e))
                    else:
                        fl = ("flip", mixf(0.18, FRONT_X, e), 0.0, 0.05 + 0.04 * sn, mixf(26.0, FRONT_YAW, e))
                        fr = ("flip", mixf(-0.50, BACK_X, e), mixf(-0.08, 0.0, e), 0.05 + 0.05 * sn, mixf(0.0, BACK_YAW, e))
                else:
                    u = seg(tR1, tCA)
                    h0 = 0.10 if flick else 0.05
                    fl = ("air", FRONT_X, 0.0, h0 * (1.0 - ease_in(u)), FRONT_YAW)
                    fr = ("air", BACK_X, 0.0, h0 * (1.0 - ease_in(u)), BACK_YAW)
        else:
            fl = ("deck", FRONT_X, 0.0, 0.0, FRONT_YAW); fr = ("deck", BACK_X, 0.0, 0.0, BACK_YAW)
        S["feet"] = {"l": fl, "r": fr}
        # ---- the body: crouch, the explosive extension, the tuck on a ballistic arc, the absorb ----
        if t < tST - eps:
            dpy = K([(0.0, 0.0), (tSET, -0.05), (tCR, -0.20), (tST, 0.0)], t)
        elif t < tLD - eps:
            u = (t - tST) / (tLD - tST)
            dpy = mixf(0.0, -0.03, u) + Kp["arc"] * 4.0 * u * (1.0 - u)
        else:
            dpy = profile([(tLD, -0.03, ease_out), (tAB, -0.20, ease_smooth), (D, 0.0, None)], t)
        S["py"] = R0["py"] + dpy
        S["pz"] = R0["pz"] + K([(0.0, 0.0), (tCR, -0.03), (tCA, 0.0), (tAB, -0.03), (D, 0.0)], t)
        S["lean"] = R0["lean"] + K([(0.0, 0.0), (tCR, 12.0), (tST, -4.0), (tCA, 6.0), (tLD, 6.0), (tAB, 12.0), (D, 0.0)], t)
        S["twist"] = R0["twist"] + K([(0.0, 0.0), (tST, 4.0), (tCA, 8.0), (tAB, 2.0), (D, 0.0)], t)
        pop_r = (70.0, -10.0, 0.3) if kind == "kf" else (58.0, 10.0, 0.35)
        S["arms"] = {"l": K([(0.0, R0["arms"]["l"]), (tCR, (18.0, -25.0, 0.5)), (tST + 2 / FPS, (72.0, 35.0, 0.35)),
                             (tCA, (62.0, 20.0, 0.4)), (tAB, (45.0, 5.0, 0.5)), (D, R0["arms"]["l"])], t),
                     "r": K([(0.0, R0["arms"]["r"]), (tCR, (16.0, -35.0, 0.4)), (tST + 2 / FPS, pop_r),
                             (tCA, (60.0, -5.0, 0.4)), (tAB, (40.0, -15.0, 0.45)), (D, R0["arms"]["r"])], t)}
        S["look"] = K([(0.0, R0["look"]), (tCR, (48.0, 28.0)), (tCA, (46.0, 34.0)), (tAB, (52.0, 26.0)), (D, R0["look"])], t)
        return S

    # ---- THE MANUAL: weight back, the nose up on the back wheels, hold, set it down -----------------------
    def s_manual(self, t, D):
        R0 = self.s_ride(0.0); S = self.s_ride(0.0); K = self.keys
        wob = math.sin(2.0 * math.pi * 1.5 * (t - 0.40) / 0.80) if 0.40 <= t <= 1.20 else 0.0
        th = profile([(0.0, 0.0, ease_smooth), (0.12, 0.0, ease_smooth), (0.40, 12.0, ease_smooth), (1.20, 12.0, ease_in), (1.40, 0.0, None)], t) + 1.8 * wob
        S["board"] = dict(pitch=th, ppx=-self.B["wheel_x"], ppy=-self.B["deck_top"]) if th > 1e-9 else {}
        S["btag"] = "rear" if th > 1e-9 else "4"
        kw = K([(0.0, 0.0), (0.30, 1.0), (1.30, 1.0), (1.55, 0.0)], t)
        kb = K([(0.0, 0.0), (0.35, 1.0), (1.22, 1.0), (1.50, 0.0)], t)
        ka = K([(0.0, 0.0), (0.30, 1.0), (1.25, 1.0), (1.55, 0.0)], t)
        S["feet"] = {"l": ("deck", mixf(FRONT_X, 0.24, kw), 0.0, 0.0, mixf(FRONT_YAW, 28.0, kw)),
                     "r": ("deck", mixf(BACK_X, -0.42, kw), 0.0, 0.0, mixf(BACK_YAW, 0.0, kw))}
        S["px"] = R0["px"] - 0.12 * kb
        S["py"] = R0["py"] - 0.03 * kb
        S["lean"] = R0["lean"] - 9.0 * kb - 2.0 * wob
        S["roll"] = R0["roll"] + 5.0 * kb
        al, ar = R0["arms"]["l"], R0["arms"]["r"]
        S["arms"] = {"l": tuple(mixf(al[i], (72.0 + 8.0 * wob, 8.0, 0.3)[i], ka) for i in range(3)),
                     "r": tuple(mixf(ar[i], (66.0 - 8.0 * wob, -4.0, 0.3)[i], ka) for i in range(3))}
        S["look"] = (R0["look"][0] - 6.0 * kb, R0["look"][1] + 12.0 * kb)
        return S


def stance_json(R, pack_path, B):
    S = R.s_ride(0.0)
    qo, to, BT, feet = R.frame(S)
    w, wq = fk_world(R.nodes, qo, to)
    nodes = R.nodes
    out = dict(pack=os.path.basename(pack_path), clip="z_rm_sk_ride", frame=0, fps=FPS, units="rig",
               quat_order="xyzw", engine_scale_club_per_rig=R.scale, floor_y_rig=R.floor, leg_rig=R.L,
               her_left_model=R.lat, her_front_model=R.fwd, pelvis_node=R.pel,
               nodes=[dict(i=i, name=nodes[i]["name"], parent=nodes[i]["parent"],
                           q=qnorm(qo.get(i, nodes[i]["q"])), t=list(to.get(i, nodes[i]["t"]))) for i in range(len(nodes))],
               board_frame=dict(t=BT.ap([0.0, 0.0, 0.0]), q=qnorm(BT.q),
                                note="origin = deck top centre, +X nose = her left, +Y up, +Z = X x Y; model space, rig units"),
               board_rest_centre_rig=R.G0,
               feet={s: dict(mode=S["feet"][s][0], x_club=S["feet"][s][1], z_club=S["feet"][s][2], yaw_deg=S["feet"][s][4],
                             ankle_world=w[R.leg[s][2]], sole_ref_world=feet[s][0]) for s in "lr"},
               world={k: w[R.RM[k]] for k in ("pelvis", "head", "thigh_l", "calf_l", "foot_l", "thigh_r", "calf_r", "foot_r",
                                               "hand_l", "hand_r", "ball_l", "ball_r")},
               rig_map={k: v for k, v in R.RM.items() if not k.startswith("_")},
               board_measured_club=B)
    os.makedirs(STANCE_OUT, exist_ok=True)
    path = os.path.join(STANCE_OUT, "stance_%s.json" % os.path.splitext(os.path.basename(pack_path))[0])
    json.dump(out, open(path, "w"), indent=1)
    return path


CLIPS = [("z_rm_sk_ride", 2.0, "s_ride", "loop"), ("z_rm_sk_film", 2.0, "s_film", "loop"),
         ("z_rm_sk_push", 1.2, "s_push", "once"),
         ("z_rm_sk_pop", 1.0, "s_pop", "once"), ("z_rm_sk_kf", 1.1, "s_kf", "once"),
         ("z_rm_sk_shove", 1.0, "s_shove", "once"), ("z_rm_sk_manual", 1.6, "s_manual", "once"),
         ("z_rm_sk_grab", 1.1, "s_grab", "once"), ("z_rm_sk_method", 1.2, "s_method", "once")]
GROUPS = {"ep10": ["z_rm_sk_ride", "z_rm_sk_push", "z_rm_sk_pop", "z_rm_sk_kf", "z_rm_sk_shove", "z_rm_sk_manual"],
          "film": ["z_rm_sk_film"],
          "air": ["z_rm_sk_grab", "z_rm_sk_method"]}
BONES = [("pelvis", "spine_01", "c"), ("spine_01", "spine_02", "c"), ("spine_02", "spine_03", "c"), ("spine_03", "neck_01", "c"),
         ("neck_01", "head", "c")] + [(a + s, b + s, s[1]) for s in ("_l", "_r") for a, b in
                                      (("thigh", "calf"), ("calf", "foot"), ("foot", "ball"), ("clavicle", "upperarm"),
                                       ("upperarm", "lowerarm"), ("lowerarm", "hand"))] + \
        [("pelvis", "thigh_l", "l"), ("pelvis", "thigh_r", "r"), ("spine_03", "clavicle_l", "l"), ("spine_03", "clavicle_r", "r")]


def board_points(B):
    """Board-local club points that must never go under the floor, and the wheel contacts."""
    hx, hz, dt = 0.70, B["width"] / 2, B["deck_top"]
    pts = [(sx * 0.55, y, sz * hz) for sx in (-1, 1) for sz in (-1, 1) for y in (0.0, -B["deck_thick"])]
    pts += [(sx * hx, y, sz * 0.08) for sx in (-1, 1) for sz in (-1, 1) for y in (B["tip_under"], B["tip_top"])]
    wheels = [(sx * B["wheel_x"], -dt, sz * B["wheel_z"]) for sx in (-1, 1) for sz in (-1, 1)]
    return pts + wheels, wheels


def gate_frame(R, S, qo, to, BT, feet, st, prev):
    B, sc, c2r, nodes = R.B, R.scale, R.c2r, R.nodes
    w, wq = fk_world(nodes, qo, to)
    bad = []
    ib = qinv(BT.q)
    def loc(p): return vmul(qrot(ib, vsub(p, BT.t)), sc)          # model rig -> board-local club
    # the feet reached their targets
    for s in "lr":
        ref, qd, pole = feet[s]
        e = math.dist(w[R.leg[s][2]], vadd(ref, qrot(qd, R.ankoff[s]))) / R.L
        st["reach"] = max(st["reach"], e)
        if e > 0.012: bad.append("reach %s %.3fL" % (s, e))
    # the board and the floor
    pts, wheels = board_points(B)
    low = min((BT.ap(vmul(list(p), c2r))[1] - R.floor) * sc for p in pts)
    st["low"] = min(st["low"], low)
    if low < -TOL_WHEEL: bad.append("board under floor %.3f" % low)
    tag = S.get("btag", "4")
    wy = [(BT.ap(vmul(list(p), c2r))[1] - R.floor) * sc for p in wheels]
    if tag == "4": chk = wy
    elif tag == "rear": chk = [y for p, y in zip(wheels, wy) if p[0] < 0]
    elif tag == "tail": chk = [min((BT.ap(vmul([-0.70, B["tip_under"], z], c2r))[1] - R.floor) * sc for z in (-0.08, 0.08))]
    else: chk = []
    if chk:
        st["ground_n"] += 1
        e = max(abs(y) for y in chk)
        st["wheel"] = max(st["wheel"], e)
        if e > TOL_WHEEL: bad.append("board contact %s off %.3f" % (tag, e))
    # soles
    lo, hi = [-0.70, -B["deck_top"], -B["width"] / 2], [0.70, 0.0, B["width"] / 2]
    for s in "lr":
        mode = S["feet"][s][0]
        sp = [vadd(w[b], qrot(wq[b], l)) for (b, l, _r) in R.sole[s]]
        fl = min((p[1] - R.floor) * sc for p in sp)
        st["underfloor"] = min(st["underfloor"], fl)
        if fl < -FLOOR_EPS: bad.append("%s sole under floor %.3f" % (s, fl))
        lp = [loc(p) for p in sp]
        over = [q for q in lp if abs(q[0]) <= 0.70 and abs(q[2]) <= B["width"] / 2]
        dy = min((q[1] - kick_top(B, q[0]) for q in over), default=None)
        inside = min((box_sdist(q, lo, hi) for q in lp), default=1.0)
        if inside < -TOL_SOLE: bad.append("%s sole inside board %.3f" % (s, inside))
        if mode == "deck":
            st["deck"] = max(st["deck"], abs(dy) if dy is not None else 9.0)
            if dy is None or abs(dy) > TOL_SOLE: bad.append("%s deck sole %.3f" % (s, dy if dy is not None else 9.0))
        elif mode == "floor":
            st["floorc"] = max(st["floorc"], abs(fl))
            if abs(fl) > TOL_SOLE: bad.append("%s floor sole %.3f" % (s, fl))
            if prev is not None and S.get("sweep") and prev.get("sweep"):
                v = vdot(vsub(w[R.leg[s][2]], prev["ank_" + s]), R.lat) * sc * FPS
                st["sweep"].append(v)
        elif mode == "air" and dy is not None:
            st["air"] = [min(st["air"][0], dy), max(st["air"][1], dy)]
            if dy < -FLOOR_EPS or dy > AIR_HOVER: bad.append("%s air hover %.3f" % (s, dy))
        elif mode == "flip":
            st["flipclear"] = min(st["flipclear"], inside)
    # capsules against each other, and the board against the capsules
    cw = R.posed_caps(w, wq)
    for (i, j) in R.pairs:
        pen = cap_pen(cw[i], cw[j]) - R.pen0[(i, j)]
        if pen > st["cap"][0]: st["cap"] = (pen, cw[i][0] + "/" + cw[j][0])
        if pen > CAP_EXTRA: bad.append("caps %s/%s %.3f" % (cw[i][0], cw[j][0], pen))
    boxes = [(lo, hi), ([0.55, 0.0, -B["width"] / 2], [0.70, B["tip_top"], B["width"] / 2]),
             ([-0.70, 0.0, -B["width"] / 2], [-0.55, B["tip_top"], B["width"] / 2])]
    for (nm, a, b, r, ra, rb) in cw:
        la, lb = loc(a), loc(b)
        allow = 0.0
        if nm.startswith("calf_") and S["feet"][nm[-1]][0] in ("deck", "air"):
            allow = max(0.0, rb - (R.wp0[R.leg[nm[-1]][2]][1] - R.floor)) * sc + TOL_SOLE
        for k in range(9):
            q = lerp(la, lb, k / 8.0)
            rr = ra + (rb - ra) * k / 8.0
            for (blo, bhi) in boxes:
                pen = rr * sc - box_sdist(q, blo, bhi) - allow
                if pen > st["bbody"][0]: st["bbody"] = (pen, nm)
        if st["bbody"][0] > 0.0 and st["bbody"][1] == nm and "board in " + nm not in " ".join(bad):
            bad.append("board in %s %.3f" % (nm, st["bbody"][0]))
    # a HOLDING palm is on its rail -- by the grip point, and by the hand's own mesh
    if S.get("_hold"):
        for s, (gx, edge, blend, curl, dn) in S.get("grab", {}).items():
            gl = loc(R.grip_world(s, w, wq))
            def rail_d(q):
                x = max(-0.45, min(0.45, q[0]))
                return vlen(vsub(q, [x, B["rail_y"], edge * B["rail_z"]]))
            e = rail_d(gl)
            ha = R.arm[s][2]
            em = min(rail_d(loc(vadd(w[ha], qrot(wq[ha], hp)))) for hp in R.handpts[s])
            st["grip"] = max(st["grip"], e); st["gripmesh"] = max(st["gripmesh"], em)
            st["gripn"] += 1
            if e > TOL_GRIP: bad.append("%s palm off the rail %.3f" % (s, e))
            if em > TOL_GRIP: bad.append("%s hand mesh off the rail %.3f" % (s, em))
    # knees over the toes
    for s in "lr":
        h, k, a = w[R.leg[s][0]], w[R.leg[s][1]], w[R.leg[s][2]]
        ha = vsub(a, h); u = vdot(vsub(k, h), ha) / (vdot(ha, ha) or 1e-12)
        v = vsub(k, vadd(h, vmul(ha, u)))
        c = vdot(vnorm(vsub(h, k)), vnorm(vsub(a, k)))
        flex = 180.0 - math.degrees(math.acos(max(-1.0, min(1.0, c))))
        st["flex"] = [min(st["flex"][0], flex), max(st["flex"][1], flex)]
        if flex > 3.0:
            d = vdot(vnorm(v), vnorm(feet[s][2]))
            st["pole"] = min(st["pole"], d)
            if d < 0.0: bad.append("%s knee backwards %.2f" % (s, d))
    cur = {"sweep": S.get("sweep", False), "ank_l": w[R.leg["l"][2]], "ank_r": w[R.leg["r"][2]]}
    return w, wq, cw, bad, cur


def bake(R, name, D, fn, kind, dump_dir):
    nodes, pel = R.nodes, R.pel
    frames = int(round(D * FPS)) + 1
    st = dict(reach=0.0, low=9.0, wheel=0.0, ground_n=0, underfloor=9.0, deck=0.0, floorc=0.0, air=[9.0, -9.0],
              flipclear=9.0, cap=(-9.0, ""), bbody=(-9.0, ""), flex=[999.0, -999.0], pole=9.0, sweep=[],
              grip=0.0, gripmesh=0.0, gripn=0, lift=0.0)
    qtr, ttr, bt, bq = {}, [], [], []
    frames_dump, bads, prev = [], [], None
    for f in range(frames):
        t = min(D, f / FPS)
        S = fn(t, D)
        qo, to, BT, feet = R.frame(S)
        w, wq, cw, bad, prev = gate_frame(R, S, qo, to, BT, feet, st, prev)
        if bad: bads.append((f, bad))
        for i in range(len(nodes)):
            q = qnorm(qo.get(i, nodes[i]["q"]))
            qtr.setdefault(i, []).append(q)
        ttr.append(list(to[pel]))
        bt.append(BT.ap([0.0, 0.0, 0.0])); bq.append(qnorm(BT.q))
        st["lift"] = max(st["lift"], (bt[-1][1] - R.G0[1]) * R.scale)
        grips = []
        for s, g in S.get("grab", {}).items():
            if g[2] > 1e-9:
                grips.append((R.grip_world(s, w, wq), BT.ap(vmul(R.rail_local(g[0], g[1]), R.c2r)), bool(S.get("_hold"))))
        if dump_dir:
            J = {k: w[v] for k, v in R.RM.items() if not k.startswith("_") and isinstance(v, int) and v >= 0}
            frames_dump.append(dict(t=t, joints=J, bones=[(J[a], J[b], sd) for a, b, sd in BONES if a in J and b in J],
                                    caps=[(nm, a, b, 0.5 * (ra + rb)) for (nm, a, b, r, ra, rb) in cw], board=dict(t=bt[-1], q=bq[-1]),
                                    soles={s: [vadd(w[b], qrot(wq[b], l)) for (b, l, _r) in R.sole[s][::3]] for s in "lr"},
                                    grips=grips,
                                    tag="%s %s/%s%s" % (S.get("btag", "4"), S["feet"]["l"][0], S["feet"]["r"][0],
                                                        " HOLD" if S.get("_hold") else ""),
                                    bad="; ".join(bad)))
    # quaternion hemispheres: continuous frame to frame (qnlerp takes the short way regardless)
    for seq in list(qtr.values()) + [bq]:
        for f in range(1, len(seq)):
            if vdot(seq[f][:3], seq[f - 1][:3]) + seq[f][3] * seq[f - 1][3] < 0: seq[f] = [-x for x in seq[f]]
    if dump_dir:
        os.makedirs(dump_dir, exist_ok=True)
        json.dump(dict(pack=os.path.basename(R.p.path), name=name, fps=FPS, lat=R.lat, fwd=R.fwd, floor=R.floor, c2r=R.c2r,
                       board_rest=R.G0, board=R.B, frames=frames_dump), open(os.path.join(dump_dir, name + ".json"), "w"))
    sw = st["sweep"]
    msg = ("  %-15s %3d f | reach %.4f L | board low %+.4f u, contact %.4f u (%d f) | soles: deck %.4f u, floor %.4f u, "
           "air %s, under floor %+.4f u%s | caps extra %.4f rig (%s) | board-in-body %+.4f u (%s) | knee flex %.0f..%.0f deg, "
           "pole dot %.2f" % (name, frames, st["reach"], st["low"], st["wheel"], st["ground_n"], st["deck"], st["floorc"],
                              ("%.3f..%.3f u" % tuple(st["air"])) if st["air"][0] < 9 else "-", st["underfloor"],
                              (" flip clear %.3f u" % st["flipclear"]) if st["flipclear"] < 9 else "",
                              st["cap"][0], st["cap"][1], st["bbody"][0], st["bbody"][1], st["flex"][0], st["flex"][1],
                              st["pole"] if st["pole"] < 9 else 1.0))
    if st["gripn"]:
        msg += " | HOLD %d f: palm to rail %.4f u, hand mesh to rail %.4f u, board up %.3f u" % (
            st["gripn"], st["grip"], st["gripmesh"], st["lift"])
    if sw:
        msg += " | push foot %.2f..%.2f u/s" % (min(sw), max(sw))
        if max(abs(v + SPEED) for v in sw) > 0.06 * SPEED: bads.append((-1, ["push foot slides: %.2f..%.2f u/s" % (min(sw), max(sw))]))
    return dict(name=name, frames=frames, qtr=qtr, ttr=ttr, bt=bt, bq=bq, bads=bads, msg=msg, kind=kind)


def rot_close(a, b):
    return min(max(abs(x - y) for x, y in zip(a, b)), max(abs(x + y) for x, y in zip(a, b)))


def main(argv):
    pack_path = argv[1]
    live = os.path.normcase(os.path.abspath(os.path.dirname(pack_path))) == \
        os.path.normcase(os.path.join(REPO, "reva_native", "packs"))
    if live and "--live" not in argv:
        print("z_sk_ride: REFUSED -- %s is a LIVE pack. Author against a copy; the director installs with --live." % pack_path)
        return 2
    if not os.path.isfile(pack_path):
        print("z_sk_ride: no pack at %s" % pack_path)
        return 2
    if live:
        print("z_sk_ride: --live: writing INTO %s (append-only; dropped clips' bytes stay as orphans)" % pack_path)
    dump_dir = argv[argv.index("--dump") + 1] if "--dump" in argv else None
    only = None
    if "--only" in argv:
        only = []
        for tok in argv[argv.index("--only") + 1].split(","):
            only.extend(GROUPS.get(tok, [tok]))
        unknown = [n for n in only if n not in [c[0] for c in CLIPS]]
        assert not unknown, "--only names no such clip: %s (groups: %s)" % (unknown, ", ".join(GROUPS))
    B = measure_board()
    R = Rider(pack_path, B)
    print("z_sk_ride: %s  leg %.3f rig  scale %.4f club/rig  floor %.4f rig  deck top %.4f club (bbox %.4f = the kick tips)"
          % (os.path.basename(pack_path), R.L, R.scale, R.floor, B["deck_top"], B["height"]))
    sp = stance_json(R, pack_path, B)
    print("z_sk_ride: stance -> %s" % sp)
    if "--stance-only" in argv:
        return 0
    for name, _D, _fn, _k in CLIPS:
        for nm in (name, name + "_bd"):
            assert not lint_name(nm), "%s trips the miners: %s" % (nm, lint_name(nm))
    res = []
    for name, D, fn, kind in CLIPS:
        if not hasattr(R, fn) or (only and name not in only): continue
        r = bake(R, name, D, getattr(R, fn), kind, dump_dir)
        print(r["msg"]); res.append(r)
    # the reference every clip starts and ends on: ride frame 0, built here whether or not the ride was baked
    S0 = R.s_ride(0.0); q0, t0, B0, _f0 = R.frame(S0)
    ride = dict(qtr={i: [qnorm(q0.get(i, R.nodes[i]["q"]))] for i in range(len(R.nodes))}, ttr=[list(t0[R.pel])],
                bt=[B0.ap([0.0, 0.0, 0.0])], bq=[qnorm(B0.q)])
    fails = []
    for r in res:
        for f, b in r["bads"][:6]:
            fails.append("%s f%d: %s" % (r["name"], f, "; ".join(b)))
        if len(r["bads"]) > 6: fails.append("%s: ... %d frames failing" % (r["name"], len(r["bads"])))
        if ride is None: continue
        if r.get("kind") == "loop" and r["name"] != "z_rm_sk_ride":
            gap0 = 0.0
            for i in r["qtr"]:
                gap0 = max(gap0, rot_close(r["qtr"][i][0], r["qtr"][i][r["frames"] - 1]))
            gap0 = max(gap0, max(abs(x - y) for x, y in zip(r["ttr"][0], r["ttr"][r["frames"] - 1])))
            print("  %-15s a LOOP: first frame vs last %.1e (it is not cut into, so it owes the ride nothing)"
                  % (r["name"], gap0))
            if gap0 > 1e-5:
                fails.append("%s does not loop (%.2e between its first and last frame)" % (r["name"], gap0))
            continue
        ends = [0, r["frames"] - 1]
        gap = 0.0
        for f in ends:
            for i in r["qtr"]:
                gap = max(gap, rot_close(r["qtr"][i][f], ride["qtr"][i][0]))
            gap = max(gap, max(abs(x - y) for x, y in zip(r["ttr"][f], ride["ttr"][0])))
        bgap = max(max(max(abs(x - y) for x, y in zip(r["bt"][f], ride["bt"][0])), rot_close(r["bq"][f], ride["bq"][0])) for f in ends)
        print("  %-15s first/last frame vs ride frame 0: body %.1e  board %.1e%s" % (
            r["name"], gap, bgap, "  (the board turned 180: same deck, nose and tail swapped)" if r["name"] == "z_rm_sk_shove" else ""))
        if gap > 1e-5: fails.append("%s does not start/end on ride frame 0 (%.2e)" % (r["name"], gap))
        if bgap > 1e-5 and r["name"] != "z_rm_sk_shove": fails.append("%s board does not start/end on ride frame 0 (%.2e)" % (r["name"], bgap))
    if fails:
        print("z_sk_ride: GATES FAILED -- nothing written\n    " + "\n    ".join(fails))
        return 1
    if "--dry" in argv:
        print("z_sk_ride: gates pass (dry run, nothing written)")
        return 0
    out = []
    for r in res:
        tracks = {(i, 0): [x for q in r["qtr"][i] for x in q] for i in r["qtr"]}
        tracks[(R.pel, 1)] = [x for t in r["ttr"] for x in t]
        out.append((r["name"], FPS, r["frames"], tracks))
        out.append((r["name"] + "_bd", FPS, r["frames"], {(0, 1): [x for t in r["bt"] for x in t], (0, 0): [x for q in r["bq"] for x in q]}))
    for (nm, _f, _n, _t) in out:
        while drop_clip(pack_path, nm): pass
    append_clips(pack_path, out)
    p2 = Rpack(pack_path)
    names = p2.clip_names()
    for (nm, _f, n, _t) in out:
        assert names.count(nm) == 1, "%s is in the pack %d times" % (nm, names.count(nm))
        c = p2.clip(names.index(nm))
        assert c["frames"] == n and abs(c["fps"] - FPS) < 1e-6
    print("z_sk_ride: appended %s -> %d clips" % (", ".join(o[0] for o in out), p2.nClips))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
