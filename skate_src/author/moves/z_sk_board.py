#!/usr/bin/env python3
"""z_sk_board.py -- ROBOT NEKO ep 10: handling the board, with a BOARD CHANNEL.

jacob after ep 9: "you need to improve the animation for picking up the board as well as make
sure they are never clipping through anything."

What was wrong in ep 9 (z_board.py, read off ep09_lesson_two.mp4): the reach was a stiff
jackknife held for the whole line, the board jumped from the floor to the hand on a line cut
(a prop slot was on the floor OR in the hand), kaeli folded into a hairpin with her head below
her knees, and the carried board stuck out horizontally through the hip.

  z_rm_sk_carry     LOOP 2.0 s   the carry: right hand on the back rail by the front truck, nose up
  z_rm_sk_pickup    1.6 s        THE PICKUP: step in, knees + hip hinge, the hand closes on the rail,
                                 the nose lifts on the back wheels, the other hand helps, up to carry
  z_rm_sk_catchup   1.2 s        the skater's pickup: step on the tail, the nose pops into the hand
  z_rm_sk_dropin    1.4 s        carry -> step back, set the board down across, front foot, back foot
  z_rm_sk_stepoff   1.0 s        ride stance -> back foot off first, stand behind the board
  z_rm_sk_bail      1.8 s        ride stance -> the board shoots out toward the nose, she sits down hard

THE BOARD CHANNEL (the engine contract): every clip NAME has a companion NAME + "_bd", same fps
and frames, two tracks on node 0 -- (0,1) translation, (0,0) rotation xyzw -- the pose of the
BOARD FRAME in the rider's model space (fk_world space, rig units): origin = centre of the deck's
top surface; +X = the nose (her LEFT foot leads); +Y = the deck normal; +Z = X x Y. All clips are
z_rm_: root motion owns Y, and the engine adds no deck lift.

The board is MEASURED from reva_native/props/skateboard.rpack at its held size (long side 1.4 u).
⛔ The ep 9 "deck height" 0.223 is the kicktail TIPS (the mesh bbox); the flat deck top between
the trucks is ~0.16 u above the floor. The channel's origin is the deck top, so it uses the latter.

Gates are asserted and printed (club units): grounded board wheels on the floor, nothing below
the floor, planted feet on the floor or deck, body capsules not deeper into each other than at the
stand, the board clear of every capsule (except a sole on it / the gripping forearm), the grip
within 0.04 u of the rail after contact and the board static before first contact, knees forward.

usage: z_sk_board.py <pack> [--stance-dir DIR] [--preview DIR] [--only a,b] [--nobake] [--force] [--live]
  refuses reva_native/packs without --live; refuses to bake a failed gate without --force.
"""
import sys, os, math, json
HERE = os.path.dirname(os.path.abspath(__file__))
AUTHOR = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(AUTHOR))
sys.path.insert(0, AUTHOR); sys.path.insert(0, HERE)
from rpack import Rpack, append_clips, drop_clip, lint_name
from authorlib import (rig_map, fk_world, solve_limb, rotate_bone_world, translate_node_world,
                       hold_world_rotation, profile, ease_smooth, qnorm, qmul, qinv, qrot,
                       q_axis_angle, q_between, qslerp, body_capsules, find_hands, HandRig)
import z_board as ZB

FPS = 30.0
PROP = os.path.join(REPO, "reva_native", "props", "skateboard.rpack")
STANCE_DIR = os.environ.get("CLUB_OUT", "out")
UP = [0.0, 1.0, 0.0]
CLIPS = ["z_rm_sk_carry", "z_rm_sk_pickup", "z_rm_sk_catchup", "z_rm_sk_dropin", "z_rm_sk_stepoff", "z_rm_sk_bail"]
# gate tolerances, CLUB units
TOL_WHEEL, TOL_FLOOR, TOL_DECKFOOT, TOL_GRIP = 0.01, 0.01, 0.015, 0.04
TOL_BODY, TOL_BOARD = 0.03, 0.01


# ---- small vector kit ------------------------------------------------------------------------
def add(a, b): return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
def sub(a, b): return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
def mul(a, s): return [a[0] * s, a[1] * s, a[2] * s]
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def nrm(a):
    l = math.sqrt(dot(a, a)) or 1.0
    return [a[0] / l, a[1] / l, a[2] / l]
def lerp(a, b, u): return [a[k] + (b[k] - a[k]) * u for k in range(3)]
def clamp01(x): return max(0.0, min(1.0, x))
def seg(t, t0, t1, ez=ease_smooth):
    """0..1 over [t0,t1] with easing; holds outside."""
    return (ez or (lambda u: u))(clamp01((t - t0) / (t1 - t0))) if t1 > t0 else (1.0 if t >= t1 else 0.0)
def arc(t, t0, t1):
    """0 -> 1 -> 0 bump over [t0,t1] (a foot's lift, a swing's bulge)."""
    u = clamp01((t - t0) / (t1 - t0)) if t1 > t0 else 0.0
    return math.sin(math.pi * u)


def q_from_axes(X, Y):
    """Rotation whose columns are X, Y, Z = X x Y (Y re-orthogonalised) -> xyzw."""
    X = nrm(X); Y = nrm(sub(Y, mul(X, dot(X, Y)))); Z = cross(X, Y)
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
    return qnorm(q)


def seg_seg_dist(p1, q1, p2, q2):
    d1, d2, r = sub(q1, p1), sub(q2, p2), sub(p1, p2)
    a, e, f = dot(d1, d1), dot(d2, d2), dot(d2, r)
    if a < 1e-12 and e < 1e-12: return math.dist(p1, p2)
    if a < 1e-12: s, t = 0.0, clamp01(f / e)
    else:
        c = dot(d1, r)
        if e < 1e-12: t, s = 0.0, clamp01(-c / a)
        else:
            b = dot(d1, d2); den = a * e - b * b
            s = clamp01((b * f - c * e) / den) if den > 1e-12 else 0.0
            t = (b * s + f) / e
            if t < 0: t, s = 0.0, clamp01(-c / a)
            elif t > 1: t, s = 1.0, clamp01((b - c) / a)
    return math.dist(add(p1, mul(d1, s)), add(p2, mul(d2, t)))


def seg_box_dist(a, b, lo, hi):
    """Exact distance from segment ab to an axis-aligned box: distance to a convex set is convex
    along the segment, so a ternary search finds the minimum."""
    def d(u):
        p = lerp(a, b, u)
        return math.sqrt(sum(max(lo[k] - p[k], 0.0, p[k] - hi[k]) ** 2 for k in range(3)))
    l, h = 0.0, 1.0
    for _ in range(40):
        m1, m2 = l + (h - l) / 3, h - (h - l) / 3
        if d(m1) < d(m2): h = m2
        else: l = m1
    return min(d(0.0), d(1.0), d((l + h) / 2))


# ---- the board, measured ---------------------------------------------------------------------
def measure_board():
    p = Rpack(PROP)
    pts = [v[:3] for v in p.skinned_positions()]
    lo = [min(v[k] for v in pts) for k in range(3)]; hi = [max(v[k] for v in pts) for k in range(3)]
    ext = [hi[k] - lo[k] for k in range(3)]
    la = max(range(3), key=lambda k: ext[k]); ua = min(range(3), key=lambda k: ext[k]); wa = 3 - la - ua
    S = 1.4 / ext[la]
    c = [(lo[k] + hi[k]) / 2 for k in range(3)]
    C = [((v[la] - c[la]) * S, (v[ua] - lo[ua]) * S, (v[wa] - c[wa]) * S) for v in pts]
    H = ext[ua] * S
    mid = [q for q in C if abs(q[0]) < 0.33 and q[1] > 0.5 * H]
    DT = max(q[1] for q in mid)
    under = sorted(q[1] for q in mid)
    TH = max(0.015, min(0.04, DT - under[int(len(under) * 0.05)]))
    W = max(abs(q[2]) for q in C if q[1] > 0.5 * H and abs(q[0]) < 0.5)
    low = [q for q in C if q[1] < 0.4 * H]
    WB = sum(abs(q[0]) for q in low) / len(low)
    wheelR = max(q[1] for q in low) / 2.0
    outer = [abs(q[2]) for q in low if abs(q[2]) > 0.5 * W]
    track = sum(outer) / len(outer) if outer else 0.7 * W
    tip = max(q[1] for q in C if abs(q[0]) > 0.66) - DT
    kick0 = min([abs(q[0]) for q in C if q[1] > DT + 0.012 and abs(q[0]) > 0.3] or [0.52])
    # a voxel-thinned cloud of the REAL mesh, in the BOARD FRAME (origin = deck top centre, X long,
    # Y up): what touches the floor is whatever is lowest, wheels round, tails kicked
    vox = {}
    for q in C:
        key = (round(q[0] / 0.03), round(q[1] / 0.03), round(q[2] / 0.03))
        vox.setdefault(key, (q[0], q[1] - DT, q[2]))
    cloud = list(vox.values())
    B = dict(DT=DT, TH=TH, W=W, WB=WB, wheelR=wheelR, track=track, tip=tip, kick0=kick0, H=H,
             long_axis="xyz"[la], up_axis="xyz"[ua], cloud=cloud)
    # collision boxes (board frame, club units): the flat deck, the two kicks, the two trucks
    B["boxes"] = [([-kick0, -TH, -W], [kick0, 0.0, W], "deck")]
    for s in (-1.0, 1.0):
        x0, x1 = sorted([s * kick0, s * 0.70])
        B["boxes"].append(([x0, -TH, -0.8 * W], [x1, tip, 0.8 * W], "kick"))
        x0, x1 = sorted([s * (WB - wheelR - 0.015), s * (WB + wheelR + 0.015)])
        B["boxes"].append(([x0, -DT, -W], [x1, -TH, W], "truck"))
    return B


def board_top(B, X):
    """Height of the deck's top surface above the origin at station X (the kicks rise)."""
    ax = abs(X)
    return 0.0 if ax <= B["kick0"] else B["tip"] * min(1.0, (ax - B["kick0"]) / (0.70 - B["kick0"]))


# ---- the rig ---------------------------------------------------------------------------------
class Rig:
    def __init__(self, pack_path):
        self.path = pack_path
        self.p = p = Rpack(pack_path)
        self.nodes = nodes = p.nodes(); self.names = names = p.node_names()
        self.RM = RM = rig_map(p)
        assert not RM["_missing"], "rig incomplete: %s" % RM["_missing"]
        self.base_q = ZB.stand_q(nodes, RM)
        self.pel = RM["pelvis"]
        self.spine = [RM["spine_01"], RM["spine_02"], RM["spine_03"]]
        self.leg = {s: (RM["thigh_" + s], RM["calf_" + s], RM["foot_" + s]) for s in "lr"}
        self.arm = {s: (RM["upperarm_" + s], RM["lowerarm_" + s], RM["hand_" + s]) for s in "lr"}
        self.ball = {s: RM["ball_" + s] for s in "lr"}
        wp0, wq0 = fk_world(nodes, self.base_q)
        self.wp0, self.wq0 = wp0, wq0
        self.L = math.dist(wp0[self.leg["l"][0]], wp0[self.leg["l"][1]]) + math.dist(wp0[self.leg["l"][1]], wp0[self.leg["l"][2]])
        self.A = {s: math.dist(wp0[self.arm[s][0]], wp0[self.arm[s][1]]) + math.dist(wp0[self.arm[s][1]], wp0[self.arm[s][2]]) for s in "lr"}
        self.lat = nrm([wp0[self.leg["l"][0]][0] - wp0[self.leg["r"][0]][0], 0.0, wp0[self.leg["l"][0]][2] - wp0[self.leg["r"][0]][2]])
        self.fwd = nrm(cross(self.lat, UP))
        self.rgt = mul(self.lat, -1.0)
        self.ank0 = {s: list(wp0[self.leg[s][2]]) for s in "lr"}
        pts = p.skinned_positions(with_bones=True)
        ys = [v[1] for v in pts]
        self.scale = 3.45 / (max(ys) - min(ys))          # club units per rig unit
        self.cu = 1.0 / self.scale                       # rig units per club unit
        # hinge / side-bend signs, measured (a forward fold moves the head forward)
        def head_after(axis, ang):
            qo = dict(self.base_q); qo[self.pel] = rotate_bone_world(nodes, self.pel, axis, ang, qo)
            w, _ = fk_world(nodes, qo); return w[RM["head"]]
        h0 = wp0[RM["head"]]
        self.hs = 1.0 if dot(sub(head_after(self.lat, 0.5), h0), self.fwd) > 0 else -1.0
        self.ss = 1.0 if dot(sub(head_after(self.fwd, 0.3), h0), self.rgt) > 0 else -1.0
        # soles: the lowest flesh of each foot, carried by the foot / ball bones
        self.floor = min(ys)
        self.sole = {}
        for s in "lr":
            fam = {RM["foot_" + s]: None}
            grew = True
            while grew:
                grew = False
                for i, nd in enumerate(nodes):
                    if nd["parent"] in fam and i not in fam: fam[i] = None; grew = True
            fp = [v for v in pts if v[3] in fam and v[1] < self.floor + 0.03]
            assert fp, "no sole under foot_" + s
            picks = [min(fp, key=lambda v: dot(v, self.fwd)), max(fp, key=lambda v: dot(v, self.fwd)),
                     min(fp, key=lambda v: dot(v, self.lat)), max(fp, key=lambda v: dot(v, self.lat))]
            out = []
            for k, v in enumerate(picks):
                bone = self.ball[s] if (k == 1 and self.ball[s] != RM["foot_" + s]) else RM["foot_" + s]
                out.append((bone, qrot(qinv(wq0[bone]), sub(v[:3], wp0[bone]))))
            # the BALL contact (for the tail stomp): the sole under the ball joint
            bj = wp0[self.ball[s]]
            out.append((RM["foot_" + s], qrot(qinv(wq0[RM["foot_" + s]]), sub([bj[0], self.floor, bj[2]], wp0[RM["foot_" + s]]))))
            self.sole[s] = out
        # the GRIP point of each hand: the middle knuckle where there are fingers, else 55% of the
        # way down the hand's own flesh
        self.hands = find_hands(p, nodes, names, RM["hand_l"], RM["hand_r"])
        self.handrig = {}
        self.grip_local, self.grip_len = {}, {}
        for s in "lr":
            h = RM["hand_" + s]
            spec = self.hands.get(s.upper())
            if spec and "middle" in spec["fingers"]:
                ch = spec["fingers"]["middle"]
                g = lerp(wp0[ch[0]], wp0[ch[1]], 0.25) if len(ch) > 1 else wp0[ch[0]]
                self.handrig[s] = HandRig(nodes, self.base_q, {}, spec, RM["lowerarm_" + s])
            else:
                d = nrm(sub(wp0[h], wp0[RM["lowerarm_" + s]]))
                hv = sorted(dot(sub(v[:3], wp0[h]), d) for v in pts if v[3] == h)
                tip = hv[int(len(hv) * 0.95)] if hv else 0.08 * self.L
                g = add(wp0[h], mul(d, 0.55 * tip))
            self.grip_local[s] = qrot(qinv(wq0[h]), sub(g, wp0[h]))
            self.grip_len[s] = math.dist(g, wp0[h])
        # capsules: body_capsules (torso, head, legs) + the arms, radii measured the same way
        caps = body_capsules(p, RM)
        bone_of = {"pelvis": RM["pelvis"], "spine_01": RM["spine_01"], "spine_02": RM["spine_02"], "spine_03": RM["spine_03"],
                   "neck_01": RM["neck_01"], "head": RM["head"], "thigh_l": RM["thigh_l"], "calf_l": RM["calf_l"],
                   "thigh_r": RM["thigh_r"], "calf_r": RM["calf_r"]}
        child_of = {"pelvis": "spine_01", "spine_01": "spine_02", "spine_02": "spine_03", "spine_03": "neck_01",
                    "neck_01": "head", "head": None, "thigh_l": "calf_l", "calf_l": "foot_l", "thigh_r": "calf_r", "calf_r": "foot_r"}
        wr, _ = fk_world(nodes)
        self.caps = []
        for c in caps:
            b = bone_of[c["name"]]; ch = child_of[c["name"]]
            off = None if ch else sub(c["b"], c["a"])
            self.caps.append(dict(name=c["name"], bone=b, child=RM[ch] if ch else None, r=c["r"], off=off))
        byb = {}
        for v in pts: byb.setdefault(v[3], []).append(v[:3])
        for s in "lr":
            for nm, a9, b9 in (("upperarm_" + s, self.arm[s][0], self.arm[s][1]), ("lowerarm_" + s, self.arm[s][1], self.arm[s][2])):
                A9, B9 = wr[a9], wr[b9]; ab = sub(B9, A9); l2 = dot(ab, ab) or 1e-12
                ds = sorted(math.dist(v, add(A9, mul(ab, clamp01(dot(sub(v, A9), ab) / l2)))) for v in byb.get(a9, []))
                self.caps.append(dict(name=nm, bone=a9, child=b9, r=ds[int(len(ds) * 0.90)] if ds else 0.04, off=None))
        idx = {c["name"]: i for i, c in enumerate(self.caps)}
        par = {"pelvis": None, "spine_01": "pelvis", "spine_02": "spine_01", "spine_03": "spine_02", "neck_01": "spine_03",
               "head": "neck_01", "thigh_l": "pelvis", "calf_l": "thigh_l", "thigh_r": "pelvis", "calf_r": "thigh_r",
               "upperarm_l": "spine_03", "lowerarm_l": "upperarm_l", "upperarm_r": "spine_03", "lowerarm_r": "upperarm_r"}
        torso = {"pelvis", "spine_01", "spine_02", "spine_03", "neck_01"}
        self.pairs = []
        names_c = [c["name"] for c in self.caps]
        for i, a in enumerate(names_c):
            for j in range(i + 1, len(names_c)):
                b = names_c[j]
                if par[a] == b or par[b] == a or (par[a] is not None and par[a] == par[b]): continue
                arm_a, arm_b = a.startswith(("upper", "lower")), b.startswith(("upper", "lower"))
                if (arm_a and b in torso) or (arm_b and a in torso): continue    # hands on hips are not clipping
                if arm_a and arm_b: continue
                self.pairs.append((i, j))
        w0c = self.capsules(wp0, wq0)
        self.pen0 = {pr: max(0.0, w0c[pr[0]][2] + w0c[pr[1]][2] - seg_seg_dist(w0c[pr[0]][0], w0c[pr[0]][1], w0c[pr[1]][0], w0c[pr[1]][1]))
                     for pr in self.pairs}
        self.cap_index = idx

    def capsules(self, w, wq):
        out = []
        for c in self.caps:
            a = w[c["bone"]]
            if c["child"] is not None: b = w[c["child"]]
            else: b = add(a, qrot(qmul(wq[c["bone"]], qinv(self.wq0[c["bone"]])), c["off"]))
            out.append((a, b, c["r"], c["name"]))
        return out

    def sole_points(self, s, w, wq):
        return [add(w[b], qrot(wq[b], off)) for b, off in self.sole[s]]

    def grip_point(self, s, w, wq):
        h = self.arm[s][2]
        return add(w[h], qrot(wq[h], self.grip_local[s]))

    # ---- the pose builder ----
    def pose(self, S):
        """S: pel [lat, drop, fwd] in LEGS (drop positive = down), yaw/hinge/side/twist degrees,
        feet {s: (ankle_target, foot_world_q)}, hands {s: ('grip', G, F) | ('hang', abd, swing)},
        curl {s: 0..1}, look (down, turn) degrees, blend (q_dict, pel_t, w)."""
        nodes, L = self.nodes, self.L
        qo = dict(self.base_q); to = {}
        pl = S.get("pel", [0.0, 0.0, 0.0])
        off = add(add(mul(self.lat, pl[0] * L), mul(self.fwd, pl[2] * L)), [0.0, -pl[1] * L, 0.0])
        to[self.pel] = translate_node_world(nodes, self.pel, off)
        yaw, hinge = math.radians(S.get("yaw", 0.0)), math.radians(S.get("hinge", 0.0))
        side, twist = math.radians(S.get("side", 0.0)), math.radians(S.get("twist", 0.0))
        qy = q_axis_angle(UP, yaw)
        fy, ly = qrot(qy, self.fwd), qrot(qy, self.lat)
        if abs(yaw) > 1e-6: qo[self.pel] = rotate_bone_world(nodes, self.pel, UP, yaw, qo)
        if abs(hinge) > 1e-6: qo[self.pel] = rotate_bone_world(nodes, self.pel, ly, self.hs * hinge * 0.45, qo)
        if abs(side) > 1e-6: qo[self.pel] = rotate_bone_world(nodes, self.pel, fy, self.ss * side * 0.25, qo)
        for b in self.spine:
            if abs(twist) > 1e-6: qo[b] = rotate_bone_world(nodes, b, UP, twist / 3.0, qo)
            if abs(hinge) > 1e-6: qo[b] = rotate_bone_world(nodes, b, ly, self.hs * hinge * 0.55 / 3.0, qo)
            if abs(side) > 1e-6: qo[b] = rotate_bone_world(nodes, b, fy, self.ss * side * 0.75 / 3.0, qo)
        for s9, ang9 in S.get("clav", {}).items():         # the shoulder girdle drops toward a low reach
            if abs(ang9) > 1e-6 and hasattr(self, "cs"):
                cl9 = self.RM["clavicle_" + s9]
                qo[cl9] = rotate_bone_world(nodes, cl9, fy, self.cs[s9] * math.radians(ang9), qo)
        for s in "lr":
            ank, fq = S["feet"][s]
            chain = self.leg[s]
            pole = S.get("knee_pole", {}).get(s) or add(self.fwd, mul(fy, 0.0))
            qo.update(solve_limb(nodes, chain, ank, pole=pole, q_override=qo, t_override=to))
            qo[chain[2]] = hold_world_rotation(nodes, chain[2], fq, qo, to)
            if self.ball[s] != chain[2]:
                qo[self.ball[s]] = hold_world_rotation(nodes, self.ball[s], S.get("toe_q", {}).get(s) or self.wq0[self.ball[s]], qo, to)
        w, wq = fk_world(nodes, qo, to)
        for s in "lr":
            hs = S.get("hands", {}).get(s, ("hang", 14.0, 0.0))
            ua, la, ha = self.arm[s]
            sg = 1.0 if s == "l" else -1.0
            elbow_pole = add(mul(self.fwd, -1.0), mul(self.lat, 0.6 * sg))

            def wrist_of(h9):
                if h9[0] == "grip":
                    return sub(h9[1], mul(nrm(h9[2]), self.grip_len[s])), nrm(h9[2])
                _k, abd, swing = h9[:3]
                a = math.radians(abd)
                d0 = add(mul(self.lat, sg * math.sin(a)), [0.0, -math.cos(a), 0.0])
                d = qrot(q_axis_angle(self.lat, -math.radians(swing)), d0)
                return add(w[ua], mul(d, self.A[s] * 0.97)), None
            if hs[0] == "mix":                    # ("mix", specA, specB, u): wrist lerped, aim eased in
                wa9, fa9 = wrist_of(hs[1]); wb9, fb9 = wrist_of(hs[2]); u9 = hs[3]
                wrist = lerp(wa9, wb9, u9)
                F, fw9 = (fb9, u9) if fb9 is not None else (fa9, 1.0 - u9)
            else:
                wrist, F = wrist_of(hs); fw9 = 1.0
            qo.update(solve_limb(nodes, (ua, la, ha), wrist, pole=elbow_pole, q_override=qo, t_override=to))
            if F is not None and fw9 > 1e-4:
                w, wq = fk_world(nodes, qo, to)
                cur = nrm(qrot(wq[ha], self.grip_local[s]))
                want = F if fw9 >= 1.0 else nrm(lerp(cur, F, fw9))
                qo[ha] = hold_world_rotation(nodes, ha, qnorm(qmul(q_between(cur, want), wq[ha])), qo, to)
            w, wq = fk_world(nodes, qo, to)
        for s, hr in self.handrig.items():
            c = S.get("curl", {}).get(s, 0.25)
            qo = hr.pose(qo, curl=c)
        lk = S.get("look")
        if lk:
            down, turn = math.radians(lk[0]), math.radians(lk[1])
            nk, hd = self.RM["neck_01"], self.RM["head"]
            if abs(turn) > 1e-6:
                qo[nk] = rotate_bone_world(nodes, nk, UP, turn * 0.4, qo, )
                qo[hd] = rotate_bone_world(nodes, hd, UP, turn * 0.6, qo)
            if abs(down) > 1e-6:
                ax = qrot(q_axis_angle(UP, yaw + turn), self.lat)
                qo[nk] = rotate_bone_world(nodes, nk, ax, self.hs * down * 0.4, qo)
                qo[hd] = rotate_bone_world(nodes, hd, ax, self.hs * down * 0.6, qo)
        ns9 = math.radians(S.get("neck_side", 0.0))
        if abs(ns9) > 1e-6:                                 # the head stays nearer level than the lean
            nk, hd = self.RM["neck_01"], self.RM["head"]
            qo[nk] = rotate_bone_world(nodes, nk, fy, self.ss * ns9 * 0.45, qo)
            qo[hd] = rotate_bone_world(nodes, hd, fy, self.ss * ns9 * 0.55, qo)
        bl = S.get("blend")
        if bl and bl[2] > 0.0:
            tq, tt, u = bl
            for i in range(len(nodes)):
                a = qnorm(qo.get(i, nodes[i]["q"])); b = qnorm(tq.get(i, nodes[i]["q"]))
                qo[i] = qnorm(qslerp(a, b, u)) if u < 1.0 else b
            to[self.pel] = lerp(to[self.pel], tt, u) if u < 1.0 else list(tt)
        return qo, to

# ==== SCORES ====
class Scores:
    GX, EY = 0.46, -0.012          # the grip station on the rail: just outboard of the front truck (club)

    def __init__(self, R, B, stance_dir):
        self.R, self.B = R, B
        L, cu = R.L, R.cu
        self.fw, self.lt, self.rg = R.fwd, R.lat, R.rgt
        self.stance = self.load_stance(stance_dir)
        if self.stance.get("deck_top"): B["DT"] = self.stance["deck_top"]
        self.floor = self.stance.get("floor", R.floor)
        self.cloud = [mul(p9, cu) for p9 in B["cloud"]]
        self.feet0 = {s: (R.ank0[s], R.wq0[R.leg[s][2]]) for s in "lr"}
        # a heel-up about the ball: which sign lifts the heel
        s9 = "r"
        rel = sub(R.ank0[s9], R.wp0[R.ball[s9]])
        self.heel_sign = 1.0 if qrot(q_axis_angle(self.lt, 0.3), rel)[1] > rel[1] else -1.0
        self.carry_setup()

    # ---- board kit (club dims -> rig) ----
    def P(self, x, y, z): return mul([x, y, z], self.R.cu)
    def bpt(self, bp, x, y, z): return add(bp[0], qrot(bp[1], self.P(x, y, z)))
    def edge(self, bp, zs, x=None): return self.bpt(bp, self.GX if x is None else x, self.EY, zs * (self.B["W"] - 0.004))
    def lowest(self, q): return min(qrot(q, p9)[1] for p9 in self.cloud)
    def grounded(self, xz, q):
        return ([xz[0], self.floor - self.lowest(q), xz[2]], q)
    def fb(self, v, q):                        # a world direction expressed in the board frame
        return qrot(qinv(q), v)

    def load_stance(self, d):
        R = self.R
        f = os.path.join(d, "stance_%s.json" % os.path.splitext(os.path.basename(R.path))[0])
        if not os.path.exists(f):
            print("z_sk_board: ⛔ NO STANCE at %s -- dropin/stepoff/bail chain onto a FALLBACK stance; re-run when it lands" % f)
            return {}
        j = json.load(open(f))
        assert len(j["nodes"]) == len(R.nodes), "stance node count %d != pack %d" % (len(j["nodes"]), len(R.nodes))
        q = {n["i"]: qnorm(n["q"]) for n in j["nodes"]}
        pel = j.get("pelvis_node", R.pel)
        bf = j["board_frame"]
        print("z_sk_board: stance from %s (clip %s frame %s)" % (f, j.get("clip"), j.get("frame")))
        return dict(q=q, pel_t=list(j["nodes"][pel]["t"]), board=(list(bf["t"]), qnorm(bf["q"])),
                    floor=j.get("floor_y_rig"), deck_top=j.get("board_measured_club", {}).get("deck_top"), file=f)

    def stance_pose(self):
        """(q dict, pelvis t, board pose, ankle targets) of the ride stance; a fallback if A1's is absent."""
        R = self.R
        if self.stance:
            qo = self.stance["q"]; to = {R.pel: self.stance["pel_t"]}
            w, wq = fk_world(R.nodes, qo, to)
            return qo, self.stance["pel_t"], self.stance["board"], {s: (list(w[R.leg[s][2]]), wq[R.leg[s][2]]) for s in "lr"}
        bq = q_from_axes(self.lt, UP)
        bo = add([0.0, self.floor + self.B["DT"] * R.cu, 0.0], mul(self.fw, 0.10 * R.L))
        feet = {}
        for s, x9 in (("l", 0.30), ("r", -0.33)):
            sole = self.bpt((bo, bq), x9, 0.0, 0.0)
            feet[s] = (add(sole, [0.0, R.ank0[s][1] - R.floor, 0.0]), R.wq0[R.leg[s][2]])
        feet = {s: (sub(feet[s][0], mul(self.fw, R.wp0[R.ball[s]][2] - R.ank0[s][2])), feet[s][1]) for s in "lr"}
        S = dict(pel=[0.0, 0.10, 0.0], hinge=8.0, twist=20.0, feet=feet, hands={"l": ("hang", 40.0, 10.0), "r": ("hang", 40.0, 10.0)}, look=(8.0, 50.0))
        qo, to = R.pose(S)
        return qo, to[R.pel], (bo, bq), feet

    # ---- carry ----
    def carry_setup(self):
        R, L, cu = self.R, self.R.L, self.R.cu
        X = nrm(add(add(UP, mul(self.lt, 0.10)), mul(self.fw, 0.05)))
        self.carry_q0 = q_from_axes(X, mul(self.lt, -1.0))
        self.F_carry = nrm(add(mul(UP, -1.0), mul(self.fw, 0.20)))
        sh = R.wp0[R.arm["r"][0]]
        a = math.radians(17.0)
        wrist = add(sh, mul(add(mul(self.rg, math.sin(a)), [0.0, -math.cos(a), 0.0]), R.A["r"] * 0.93))
        G = add(add(wrist, mul(self.F_carry, R.grip_len["r"])), mul(self.fw, 0.03 * L))
        S0 = self.carry_spec(0.0, None)
        qo, to = R.pose(S0)
        w, wq = fk_world(R.nodes, qo, to)
        caps = [c for c in R.capsules(w, wq) if not c[3].startswith(("upper", "lower"))]
        for it in range(60):
            bp = (sub(G, qrot(self.carry_q0, self.P(self.GX, self.EY, -(self.B["W"] - 0.004)))), self.carry_q0)
            worst = max(board_pen(self, bp, caps))
            low = bp[0][1] + self.lowest(bp[1]) - self.floor
            if worst < -0.085 * cu and low > 0.15 * L: break
            G = add(G, mul(self.rg, 0.006))
            if low <= 0.15 * L: G = add(G, [0.0, 0.006, 0.0])
        self.carry_G0 = G
        self.carry_bp0 = bp
        print("z_sk_board: carry grip %.3f L out of the right hip, board clear of the legs by %.3f u, tail %.3f L off the floor (%d nudges)"
              % (dot(sub(G, R.wp0[R.leg["r"][0]]), self.rg) / L, -worst * R.scale, low / L, it))

    def carry_board(self, t):
        b = math.sin(2 * math.pi * t / 2.0)
        o = add(self.carry_bp0[0], [0.0, -0.004 * self.R.L * b - 0.010 * self.R.L, 0.0])
        q = qnorm(qmul(q_axis_angle(self.fw, math.radians(1.2) * math.sin(2 * math.pi * t / 2.0 + 0.8)), self.carry_q0))
        return (o, q)

    def carry_spec(self, t, bp):
        b = math.sin(2 * math.pi * t / 2.0)
        S = dict(pel=[0.0, 0.010 + 0.004 * b, 0.0], hinge=-1.0 + 0.8 * b, feet=self.feet0,
                 hands={"l": ("hang", 18.0 + 1.0 * b, 4.0)}, curl={"r": 0.8, "l": 0.3}, look=(4.0 + 1.5 * b, 0.0))
        if bp is not None:
            S["hands"]["r"] = ("grip", self.edge(bp, -1.0), self.F_carry)
        return S

    def carry(self, t):
        bp = self.carry_board(t)
        return self.carry_spec(t, bp), bp, dict(grip="r", contact=True, planted={"l": "floor", "r": "floor"}, grounded=False)

    # ---- helpers for one-shots ----
    def heel_up(self, s, ang_deg, ank=None, ball=None):
        R = self.R
        qa = q_axis_angle(self.lt, self.heel_sign * math.radians(ang_deg))
        b0 = ball if ball is not None else R.wp0[R.ball[s]]
        a0 = R.ank0[s] if ank is None else ank
        rel = sub(a0, b0) if ank is not None else sub(R.ank0[s], R.wp0[R.ball[s]])
        return add(b0, qrot(qa, rel)), qnorm(qmul(qa, R.wq0[R.leg[s][2]]))

    def step(self, a, b, t, t0, t1, h):
        u = seg(t, t0, t1)
        p9 = lerp(a, b, u)
        p9[1] += h * self.R.L * arc(t, t0, t1)
        return p9

    def mixnum(self, A, Bv, u):
        return A + (Bv - A) * u

    # ---- the pickup ----
    def pickup_setup(self):
        R, L, cu = self.R, self.R.L, self.R.cu
        W = self.B["W"]
        # THE BOARD'S SPOT IS DERIVED FROM THE CONTACT POSE (z_board's rule): for each candidate
        # crouch, find where the right shoulder got to, hang a 92%-straight arm from it down to
        # the rail's height, and lay the board so its right rail is under that hand.
        q = q_from_axes(mul(self.fw, -1.0), UP)                      # flat, nose toward her
        F = qrot(q, nrm([0.0, -0.25, 0.97]))                         # fingers across the right rail, wrist low beside it
        Gy = self.floor + (self.B["DT"] + self.EY) * cu
        wy = Gy - F[1] * R.grip_len["r"]
        best = None
        fallback = None
        for stepside in ("l", "r"):
            for drop in (0.50, 0.60, 0.70):
                for hinge in (70.0, 90.0, 110.0):
                    for yaw, side in ((-15.0, 12.0), (-28.0, 18.0)):
                        prm = dict(step=stepside, drop=drop, hinge=hinge, yaw=yaw, side=side, BF=0.0)
                        S = self.pickup_contact_spec(prm, None, None, None)
                        qo, to = R.pose(S)
                        w, _wq = fk_world(R.nodes, qo, to)
                        sh = w[R.arm["r"][0]]
                        reach = 0.95 * R.A["r"]
                        dv = sh[1] - wy
                        if fallback is None or dv < fallback[0]:
                            fallback = (dv, prm, sh)
                        if dv >= reach: continue
                        hz = math.sqrt(reach * reach - dv * dv)
                        fy = qrot(q_axis_angle(UP, math.radians(yaw)), self.fw)
                        hdir = nrm(add(mul(self.rg, 0.7), mul(fy, 0.7)))
                        for hk in (0.6, 0.9):
                            wt = [sh[0] + hdir[0] * hz * hk, wy, sh[2] + hdir[2] * hz * hk]
                            G = add(wt, mul(F, R.grip_len["r"]))
                            o = sub(G, qrot(q, self.P(self.GX, self.EY, -(W - 0.004))))
                            bp = self.grounded(o, q)
                            G = self.edge(bp, -1.0)
                            prm = dict(prm, BF=hk)
                            S2 = self.pickup_contact_spec(prm, bp, G, F)
                            ok, sc, info = self.judge(S2, bp, prm, G)
                            # and the start: the board must not be under either foot before she moves
                            if ok:
                                S0, bp0_, m0 = self.pickup_probe_start(prm, bp)
                                ok = m0 < -0.01 * cu
                                info += ", start feet-board %.3f u" % (m0 * R.scale)
                            if ok and (best is None or sc < best[0]):
                                best = (sc, prm, bp, G, F, info)
        if best is None:
            # ⛔ LOUD, NOT SILENT: no candidate passed every gate. Take the crouch whose shoulder got
            # lowest, lay the board under its reach anyway, and let the per-frame gates say how far off.
            dv, prm, sh = fallback
            hz = math.sqrt(max(0.0, (0.95 * R.A["r"]) ** 2 - min(dv, 0.95 * R.A["r"]) ** 2))
            fy = qrot(q_axis_angle(UP, math.radians(prm["yaw"])), self.fw)
            hdir = nrm(add(mul(self.rg, 0.7), mul(fy, 0.7)))
            wt = [sh[0] + hdir[0] * hz * 0.6, wy, sh[2] + hdir[2] * hz * 0.6]
            G = add(wt, mul(F, R.grip_len["r"]))
            bp = self.grounded(sub(G, qrot(q, self.P(self.GX, self.EY, -(W - 0.004)))), q)
            G = self.edge(bp, -1.0)
            prm = dict(prm, BF=0.6)
            best = (0.0, prm, bp, G, F, "⛔ NO CANDIDATE PASSED -- fallback, shoulder %.3f rig u above the wrist line" % dv)
        sc, prm, bp, G, F, info = best
        self.pk = dict(prm=prm, bp0=bp, G=G, F=F)
        print("z_sk_board: pickup contact solved: step %s  drop %.2f L  hinge %.0f  yaw %.0f  side %.0f  (%s)"
              % (prm["step"], prm["drop"], prm["hinge"], prm["yaw"], prm["side"], info))
        return prm

    def pickup_feet(self, prm, t):
        """(feet, planted) at time t: the stepping foot out and back, the other heel up while low."""
        R, L = self.R, self.R.L
        st = prm["step"]; ot = "r" if st == "l" else "l"
        out = add(add(R.ank0[st], mul(self.fw, 0.30 * L)), mul(self.lt if st == "l" else self.rg, 0.03 * L))
        if t < 0.80:
            a = self.step(R.ank0[st], out, t, 0.12, 0.46, 0.07)
        else:
            a = self.step(out, R.ank0[st], t, 1.04, 1.40, 0.05)
        feet = {st: (a, R.wq0[R.leg[st][2]])}
        hu = 26.0 * (seg(t, 0.36, 0.72) - seg(t, 0.98, 1.30))
        feet[ot] = self.heel_up(ot, hu) if hu > 1e-4 else self.feet0[ot]
        moving = (0.12 < t < 0.46) or (1.04 < t < 1.40)
        planted = {st: None if moving else "floor", ot: "floor"}
        return feet, planted

    def pickup_contact_spec(self, prm, bp, G, F):
        R = self.R
        feet, _ = self.pickup_feet(prm, 0.80)
        pf = 0.13 if prm["step"] == "l" else 0.15
        return dict(pel=[-0.05, prm["drop"], pf], yaw=prm["yaw"], hinge=prm["hinge"], side=prm["side"], twist=prm["yaw"] * 0.5,
                    feet=feet, hands={"r": ("grip", G, F) if G is not None else ("hang", 14.0, 0.0), "l": ("hang", 26.0, -6.0)},
                    curl={"r": 0.2, "l": 0.3}, look=(28.0, -12.0))

    def judge(self, S, bp, prm, G):
        R = self.R
        sh = None
        qo, to = R.pose(S)
        w, wq = fk_world(R.nodes, qo, to)
        reach = math.dist(R.grip_point("r", w, wq), G) * R.scale
        caps = R.capsules(w, wq)
        body = body_pen(R, caps) * R.scale
        bpen = max(board_pen(self, bp, [c for c in caps if c[3] not in ("lowerarm_r",)])) * R.scale
        kn = knee_ok(R, w, wq)
        ok = reach < TOL_GRIP and body < TOL_BODY and bpen < TOL_BOARD and kn > -0.002
        sc = abs(prm["hinge"] - 55.0) + 60.0 * abs(prm["drop"] - 0.32) + 0.3 * abs(prm["yaw"]) + (8.0 if prm["BF"] > 0.55 else 0.0)
        return ok, sc, "grip miss %.3f u, body %.3f u, board %.3f u, knee %.4f" % (reach, body, bpen, kn)

    def pickup(self, t):
        R, L = self.R, self.R.L
        prm, bp0, G0, F0c = self.pk["prm"], self.pk["bp0"], self.pk["G"], self.pk["F"]
        TC = 0.80
        feet, planted = self.pickup_feet(prm, t)
        c0 = self.carry_spec(0.0, None)
        # ---- the body: anticipate, step and fold to the contact; rise past the carry; settle ----
        pf = 0.13 if prm["step"] == "l" else 0.15
        down = seg(t, 0.30, TC)
        up = seg(t, 0.90, 1.42)
        ant = arc(t, 0.0, 0.26)
        pel = [(-0.05 * down) * (1 - up) + (0.03 if prm["step"] == "l" else -0.03) * ant,
               (prm["drop"] * down + 0.02 * arc(t, 0.76, 0.92)) * (1 - up) + c0["pel"][1] * up - 0.01 * ant + 0.012 * arc(t, 1.36, 1.60),
               pf * seg(t, 0.10, 0.60) * (1 - up)]
        hinge = prm["hinge"] * down * (1 - up) + c0["hinge"] * up - 5.0 * arc(t, 1.20, 1.60) + 3.0 * ant
        yaw = prm["yaw"] * seg(t, 0.20, TC) * (1 - up)
        side = prm["side"] * down * (1 - up)
        look = (28.0 * seg(t, 0.05, 0.50) * (1 - up) + c0["look"][0] * up + 10.0 * arc(t, 0.95, 1.35), -12.0 * seg(t, 0.05, 0.5) * (1 - up))
        # ---- the board: static until the hand closes, then nose up on the back wheels, then free ----
        lift = seg(t, 0.88, 1.42)
        s1 = 0.36
        cbp = self.carry_board(0.0)
        if lift <= 0.0:
            bp = bp0
        else:
            q0 = bp0[1]
            piv = self.bpt(bp0, -self.B["WB"], -self.B["DT"], 0.0)
            def pivot(k):
                th = math.radians(30.0) * k
                qq = qnorm(qmul(q_axis_angle(qrot(q0, [0, 0, 1]), th), q0))
                pw = add(piv, mul(qrot(q0, [1, 0, 0]), 0.08 * L * k))
                oo = sub(pw, qrot(qq, self.P(-self.B["WB"], -self.B["DT"], 0.0)))
                oo[1] += self.floor - (oo[1] + self.lowest(qq)) if oo[1] + self.lowest(qq) < self.floor else 0.0
                return (oo, qq)
            if lift <= s1:
                bp = pivot(lift / s1)
            else:
                u = (lift - s1) / (1 - s1)
                pe = pivot(1.0)
                o = lerp(pe[0], cbp[0], u)
                o = add(o, add([0.0, 0.10 * L * math.sin(math.pi * u), 0.0], mul(self.rg, 0.12 * L * math.sin(math.pi * u))))
                bp = (o, qnorm(qslerp(pe[1], cbp[1], u)))
        if t > 1.42:
            bp = (bp[0], qnorm(qmul(q_axis_angle(self.fw, math.radians(4.0) * math.sin(math.pi * (t - 1.42) / 0.18)), bp[1])))
        # ---- the hands ----
        hang0 = ("hang", 14.0, 0.0)
        Fb0 = self.fb(F0c, bp0[1]); Fbc = self.fb(self.F_carry, cbp[1])
        if t < TC:
            u = seg(t, 0.22, TC)
            above = add(G0, add([0.0, 0.10 * L, 0.0], mul(self.rg, 0.04 * L)))
            wa = ("grip", lerp(above, G0, seg(t, 0.58, TC)), F0c)
            hr = ("mix", hang0, wa, u)
        else:
            Fw = qrot(bp[1], nrm(lerp(Fb0, Fbc, seg(t, 0.95, 1.45))))
            hr = ("grip", self.edge(bp, -1.0), Fw)
        la = c0["hands"]["l"]
        if t < 0.92:
            hl = ("hang", 14.0 + 2.0 * down, 30.0 * down)
        else:
            gl = ("grip", self.edge(bp, +1.0, x=0.0), qrot(bp[1], nrm([0.0, -0.6, -0.8])))
            hold = seg(t, 0.92, 1.08) * (1 - seg(t, 1.22, 1.50))
            rel = seg(t, 1.22, 1.60)
            base = ("hang", 16.0 * (1 - rel) + la[1] * rel, 30.0 * (1 - rel) + (la[2] - 10.0 * arc(t, 1.22, 1.60)) * rel)
            hl = ("mix", base, gl, hold)
        curl_r = 0.2 + 0.6 * seg(t, TC, 0.92)
        curl_r = curl_r * (1 - seg(t, 1.45, 1.60)) + 0.8 * seg(t, 1.45, 1.60)
        S = dict(pel=pel, yaw=yaw, hinge=hinge, side=side, twist=yaw * 0.5, feet=feet,
                 hands={"r": hr, "l": hl}, curl={"r": curl_r, "l": 0.3 + 0.4 * seg(t, 0.92, 1.08) * (1 - seg(t, 1.22, 1.45))}, look=look)
        grounded = lift <= 0.0
        return S, bp, dict(grip="r", contact=t >= TC - 1e-9, planted=planted, grounded=grounded, static_until=TC,
                           exempt={"lowerarm_r"} | ({"lowerarm_l"} if 0.92 <= t <= 1.5 else set()))

# ==== MORE SCORES ====
def add_foot_caps(R):
    """The feet are not in body_capsules; a board that lies across a toe is clipping all the same."""
    if any(c["name"].startswith("foot_") for c in R.caps): return
    for s in "lr":
        f, b = R.leg[s][2], R.ball[s]
        # ankle -> over the ball, radius sized so the capsule's floor end sits ON the sole, not through it
        r = min(0.55 * (R.ank0[s][1] - R.floor), 0.05 * R.L)
        over = [R.wp0[b][0], R.floor + r + 0.002, R.wp0[b][2]]
        R.caps.append(dict(name="foot_" + s, bone=f, child=None, r=r, off=sub(over, R.wp0[f])))
        toe = R.sole_points(s, R.wp0, R.wq0)[1]
        R.caps.append(dict(name="toe_" + s, bone=b, child=None, r=0.025, off=sub(add(toe, [0.0, 0.027, 0.0]), R.wp0[b])))


def ensure_extras(sc):
    R = sc.R
    add_foot_caps(R)
    if not hasattr(R, "cs"):
        R.cs = {}
        for s in "lr":
            cl = R.RM["clavicle_" + s]
            qo = dict(R.base_q); qo[cl] = rotate_bone_world(R.nodes, cl, R.fwd, 0.2, qo)
            w, _ = fk_world(R.nodes, qo)
            R.cs[s] = 1.0 if w[R.arm[s][0]][1] < R.wp0[R.arm[s][0]][1] else -1.0


def _step2(self, a, b, t, t0, t1, h):
    """A step that LIFTS FIRST: straight up off whatever it stood on, then across, then down onto its mark."""
    d = t1 - t0
    lift = seg(t, t0, t0 + 0.35 * d) * (1.0 - seg(t, t1 - 0.35 * d, t1))
    u = seg(t, t0 + 0.22 * d, t1 - 0.18 * d)
    p9 = lerp(a, b, u)
    p9[1] += h * self.R.L * lift
    return p9


def _judge(self, S, bp, prm, G):
    R = self.R
    qo, to = R.pose(S)
    w, wq = fk_world(R.nodes, qo, to)
    reach = math.dist(R.grip_point("r", w, wq), G) * R.scale
    caps = R.capsules(w, wq)
    body = body_pen(R, caps) * R.scale
    bpen = max(board_pen(self, bp, [c for c in caps if c[3] != "lowerarm_r"])) * R.scale
    kn = knee_ok(R, w, wq)
    ok = reach < TOL_GRIP and body < TOL_BODY and bpen < -0.02 and kn > -0.002
    sc = abs(prm["hinge"] - 70.0) + 40.0 * abs(prm["drop"] - 0.46) + 0.3 * abs(prm["yaw"]) + 6.0 * abs(prm["BF"] - 0.9)
    return ok, sc, "grip miss %.3f u, body %.3f u, board %.3f u, knee %.4f" % (reach, body, bpen, kn)


def _pickup_probe_start(self, prm, bp):
    R = self.R
    worst = -1.0
    for t in (0.0, 0.2, 0.3, 0.46, 0.6):
        feet, _pl = self.pickup_feet(prm, t)
        S = dict(feet=feet)
        qo, to = R.pose(S)
        w, wq = fk_world(R.nodes, qo, to)
        caps = [c for c in R.capsules(w, wq) if c[3].startswith(("foot", "toe", "calf"))]
        worst = max(worst, max(board_pen(self, bp, caps)))
    return None, bp, worst


def _stance_cache(self):
    if not hasattr(self, "_st"):
        R = self.R
        q, pt, bp, feet = self.stance_pose()
        qd = {i: qnorm(q.get(i, R.nodes[i]["q"])) for i in range(len(R.nodes))}
        w, wq = fk_world(R.nodes, qd, {R.pel: pt})
        d = sub(w[R.pel], R.wp0[R.pel])
        prm = [dot(d, R.lat) / R.L, -d[1] / R.L, dot(d, R.fwd) / R.L]
        z_back = dot(bp[0], R.fwd) - self.B["W"] * R.cu
        toes = max(dot(p9, R.fwd) for s in "lr" for p9 in R.sole_points(s, R.wp0, R.wq0))
        Sb = (toes - z_back) / R.L + 0.08
        self._st = dict(q=qd, t=list(pt), bp=bp, feet=feet, pel=prm, Sb=Sb)
    return self._st


def _behind_feet(self):
    st = self._stance_cache()
    return {s: (sub(self.R.ank0[s], mul(self.R.fwd, st["Sb"] * self.R.L)), self.R.wq0[self.R.leg[s][2]]) for s in "lr"}


def _behind_spec(self):
    st = self._stance_cache()
    return dict(pel=[0.0, 0.0, -st["Sb"]], feet=self._behind_feet(), hands={"l": ("hang", 14.0, 0.0), "r": ("hang", 14.0, 0.0)},
                curl={"l": 0.25, "r": 0.25}, look=(6.0, 0.0))


def _stepoff(self, t):
    R, L = self.R, self.R.L
    st = self._stance_cache()
    end = self._behind_spec()
    fr, fl = end["feet"]["r"], end["feet"]["l"]
    ar = self.step2(st["feet"]["r"][0], fr[0], t, 0.08, 0.45, 0.13)
    al = self.step2(st["feet"]["l"][0], fl[0], t, 0.40, 0.80, 0.13)
    qr = qslerp(st["feet"]["r"][1], fr[1], seg(t, 0.08, 0.45))
    ql = qslerp(st["feet"]["l"][1], fl[1], seg(t, 0.40, 0.80))
    u = seg(t, 0.05, 0.85)
    pel = [lerp(st["pel"], end["pel"], u)[k] for k in range(3)]
    pel[1] += 0.03 * arc(t, 0.35, 0.60) + 0.02 * arc(t, 0.72, 0.95)
    pel[0] += 0.04 * arc(t, 0.0, 0.5)
    bal = arc(t, 0.10, 0.85)
    S = dict(pel=pel, feet={"r": (ar, qr), "l": (al, ql)}, hinge=6.0 * bal,
             hands={"l": ("hang", 14.0 + 22.0 * bal, 6.0 * bal), "r": ("hang", 14.0 + 16.0 * bal, -6.0 * bal)},
             curl={"l": 0.25, "r": 0.25}, look=(6.0 + 14.0 * bal, 0.0), blend=(st["q"], st["t"], 1.0 - seg(t, 0.0, 0.40)))
    planted = {"r": "deck" if t <= 0.08 else (None if t < 0.45 else "floor"), "l": "deck" if t <= 0.40 else (None if t < 0.80 else "floor")}
    lifting = {"r": 0.08 < t < 0.08 + 0.30 * 0.37, "l": 0.40 < t < 0.40 + 0.30 * 0.40}   # straight up off the deck, nothing moves sideways yet
    ex = set()
    for s in "lr":
        if planted[s] == "deck" or lifting[s]: ex |= {"calf_" + s, "foot_" + s, "toe_" + s}
    return S, st["bp"], dict(planted=planted, grounded=True, exempt=ex, static_until=99.0)


def _catch_board(self, t):
    R, L, cu = self.R, self.R.L, self.R.cu
    st = self._stance_cache()
    bp0 = st["bp"]
    q0 = bp0[1]
    TS, TP, TC = 0.28, 0.40, 0.62
    if t <= TS:
        return bp0
    zax = qrot(q0, [0.0, 0.0, 1.0])
    piv = self.bpt(bp0, -self.B["WB"], -self.B["DT"], 0.0)
    def about_wheels(th):
        qq = qnorm(qmul(q_axis_angle(zax, th), q0))
        oo = sub(piv, qrot(qq, self.P(-self.B["WB"], -self.B["DT"], 0.0)))
        low = oo[1] + self.lowest(qq)
        if low < self.floor: oo[1] += self.floor - low
        return (oo, qq)
    b30 = about_wheels(math.radians(30.0))
    qa = q_from_axes(UP, mul(self.lt, -1.0))
    tipw = self.bpt(b30, -0.70, 0.0, 0.0)
    oa = [tipw[0], self.floor + (0.70 - 0.02) * cu + 0.36 * L, tipw[2]]
    oa = add(oa, add(mul(self.rg, 0.14 * L), mul(self.fw, 0.14 * L)))   # the pop carries it up and OUT of the shin's way
    apex = (oa, qa)
    if t <= TP:
        return about_wheels(math.radians(30.0) * seg(t, TS, TP))
    if t <= TC:
        u = seg(t, TP, TC, lambda x: 1 - (1 - x) * (1 - x))
        o = lerp(b30[0], apex[0], u)
        q = qnorm(qslerp(b30[1], apex[1], u))
        low = o[1] + self.lowest(q)
        if low < self.floor: o[1] += self.floor - low
        return (o, q)
    c0 = self.carry_board(0.0)
    u = seg(t, TC + 0.02, 1.02)
    o = lerp(apex[0], c0[0], u)
    o[1] -= 0.04 * L * arc(t, TC, 0.80)                     # the catch gives a little under the weight
    q = qnorm(qslerp(apex[1], c0[1], u))
    if t > 1.02:
        q = qnorm(qmul(q_axis_angle(self.fw, math.radians(3.0) * math.sin(math.pi * (t - 1.02) / 0.18)), q))
    return (o, q)


def _catchup(self, t):
    R, L = self.R, self.R.L
    st = self._stance_cache()
    TS, TP, TC = 0.28, 0.40, 0.62
    start = self._behind_spec()
    c0 = self.carry_spec(0.0, None)
    bp = self._catch_board(t)
    # the right foot: onto the tail pad, rides it down, then off to its carry mark
    pad_b = (-0.56, board_top(self.B, -0.56), 0.0)
    ball_sole0 = R.sole_points("r", R.wp0, R.wq0)[4]
    rel = sub(R.ank0["r"], ball_sole0)
    pad0 = self.bpt(st["bp"], *pad_b)
    on_tail = add(pad0, rel)
    if t <= TS:
        ar, qr = self.step(start["feet"]["r"][0], on_tail, t, 0.02, TS, 0.20), R.wq0[R.leg["r"][2]]
    elif t <= TP:
        dq = qnorm(qmul(bp[1], qinv(st["bp"][1])))
        ar, qr = add(self.bpt(bp, *pad_b), qrot(dq, rel)), qnorm(qmul(dq, R.wq0[R.leg["r"][2]]))
    else:
        b40 = self._catch_board(TP)
        dq = qnorm(qmul(b40[1], qinv(st["bp"][1])))
        a40 = add(self.bpt(b40, *pad_b), qrot(dq, rel))
        u = seg(t, TP, 0.78)
        ar = self.step(a40, R.ank0["r"], t, TP, 0.78, 0.05)
        ar = add(ar, mul(R.fwd, -0.06 * L * arc(t, TP, 0.60)))
        qr = qnorm(qslerp(qnorm(qmul(dq, R.wq0[R.leg["r"][2]])), R.wq0[R.leg["r"][2]], u))
    al = self.step(start["feet"]["l"][0], R.ank0["l"], t, 0.62, 0.95, 0.06)
    u = seg(t, 0.10, 1.00)
    pel = [0.0, c0["pel"][1] * u + 0.05 * arc(t, 0.26, 0.46) + 0.06 * arc(t, 0.58, 0.86), -st["Sb"] * (1 - u)]
    hinge = 10.0 * arc(t, 0.25, 0.80) + c0["hinge"] * seg(t, 0.8, 1.2) - 3.0 * arc(t, 0.95, 1.2)
    # the hand: waits where the nose will arrive, closes as it gets there, carries it up
    apex = self._catch_board(TC)
    Fc = nrm(add([0.0, -0.45, 0.0], mul(R.fwd, 0.9)))
    Gc = self.edge(apex, -1.0)
    if t < TC:
        wait = add(Gc, add([0.0, 0.05 * L, 0.0], mul(R.fwd, -0.05 * L)))
        g = ("grip", lerp(wait, Gc, seg(t, 0.46, TC)), Fc)
        hr = ("mix", ("hang", 14.0, 0.0), g, seg(t, 0.12, 0.46))
    else:
        c0b = self.carry_board(0.0)
        Fb0, Fb1 = self.fb(Fc, apex[1]), self.fb(self.F_carry, c0b[1])
        hr = ("grip", self.edge(bp, -1.0), qrot(bp[1], nrm(lerp(Fb0, Fb1, seg(t, TC, 1.05)))))
    bal = arc(t, 0.2, 0.9)
    la = c0["hands"]["l"]
    S = dict(pel=pel, feet={"r": (ar, qr), "l": (al, R.wq0[R.leg["l"][2]])}, hinge=hinge,
             hands={"r": hr, "l": ("hang", 14.0 + (la[1] - 14.0) * seg(t, 0.9, 1.2) + 14.0 * bal, la[2] * seg(t, 0.9, 1.2) - 8.0 * bal)},
             curl={"r": 0.25 + 0.55 * seg(t, TC - 0.04, TC + 0.06), "l": 0.25 + 0.05 * seg(t, 0.9, 1.2)},
             look=(6.0 * (1 - seg(t, 0.8, 1.2)) + 22.0 * arc(t, 0.0, 0.9) + c0["look"][0] * seg(t, 0.8, 1.2), 0.0))
    planted = {"r": "floor" if t <= 0.02 else (None if t < TS else ("deck" if t <= TP else (None if t < 0.78 else "floor"))),
               "l": "floor" if (t <= 0.62 or t >= 0.95) else None}
    ex = {"lowerarm_r"} if t >= TC else set()
    if planted["r"] == "deck": ex |= {"calf_r", "foot_r", "toe_r"}
    return S, bp, dict(grip="r", contact=t >= TC - 1e-9, planted=planted, grounded=t <= TS, exempt=ex, static_until=TS)


def _dropin(self, t):
    R, L, cu = self.R, self.R.L, self.R.cu
    st = self._stance_cache()
    c0 = self.carry_spec(0.0, None)
    behind = self._behind_feet()
    TR = 0.55
    g = 17.17 / R.scale
    cb = self.carry_board(0.0)
    rel_o = add(st["bp"][0], [0.0, 0.10 * L, 0.0])
    fall_t = math.sqrt(2 * 0.10 * L / g)
    if t <= TR:
        u = seg(t, 0.06, TR)
        o = lerp(cb[0], rel_o, u)
        o = add(o, add(mul(R.fwd, 0.34 * L * arc(t, 0.06, TR)), [0.0, 0.10 * L * arc(t, 0.06, TR), 0.0]))
        bp = (o, qnorm(qslerp(cb[1], st["bp"][1], u)))
    elif t <= TR + fall_t:
        dt = t - TR
        bp = ([rel_o[0], rel_o[1] - 0.5 * g * dt * dt, rel_o[2]], st["bp"][1])
    else:
        bump = 0.012 * L * arc(t, TR + fall_t, TR + fall_t + 0.10)
        bp = (add(st["bp"][0], [0.0, bump, 0.0]), st["bp"][1])
    ar = self.step(R.ank0["r"], behind["r"][0], t, 0.0, 0.20, 0.05)
    al = self.step(R.ank0["l"], behind["l"][0], t, 0.18, 0.38, 0.05)
    if t > 0.60:
        al = self.step2(behind["l"][0], st["feet"]["l"][0], t, 0.62, 0.95, 0.16)
    if t > 0.86:
        ar = self.step2(behind["r"][0], st["feet"]["r"][0], t, 0.88, 1.18, 0.16)
    qr = qslerp(R.wq0[R.leg["r"][2]], st["feet"]["r"][1], seg(t, 0.88, 1.18))
    ql = qslerp(R.wq0[R.leg["l"][2]], st["feet"]["l"][1], seg(t, 0.62, 0.95))
    down = seg(t, 0.20, 0.50) * (1 - seg(t, 0.58, 0.95))
    back = seg(t, 0.0, 0.40) * (1 - seg(t, 0.60, 1.10))
    pel = [0.0, c0["pel"][1] + 0.26 * down, -self._stance_cache()["Sb"] * back]
    pel = lerp(pel, st["pel"], seg(t, 0.90, 1.30))
    hinge = c0["hinge"] + 42.0 * down
    twist = 22.0 * down
    if t <= TR:
        Fw = qrot(bp[1], nrm(lerp(self.fb(self.F_carry, cb[1]), [0.0, -0.6, 0.8], seg(t, 0.1, TR))))
        hr = ("grip", self.edge(bp, -1.0), Fw)
    else:
        hr = ("mix", ("grip", self.edge((add(rel_o, [0.0, 0.03 * L, 0.0]), st["bp"][1]), -1.0), qrot(st["bp"][1], nrm([0.0, -0.6, 0.8]))),
              ("hang", 38.0, 20.0), seg(t, TR, 0.90))
    S = dict(pel=pel, feet={"r": (ar, qr), "l": (al, ql)}, hinge=hinge, twist=twist, yaw=10.0 * down,
             hands={"r": hr, "l": ("hang", c0["hands"]["l"][1] + 20.0 * down + 16.0 * arc(t, 0.6, 1.2), c0["hands"]["l"][2] + 10.0 * down)},
             curl={"r": 0.8 * (1 - seg(t, TR - 0.02, TR + 0.06)) + 0.25 * seg(t, TR, 0.8), "l": 0.3},
             look=(c0["look"][0] + 30.0 * seg(t, 0.1, 0.5) * (1 - seg(t, 0.9, 1.3)), 16.0 * down),
             blend=(st["q"], st["t"], seg(t, 1.10, 1.40)))
    stepping_l = 0.18 < t < 0.38 or 0.62 < t < 0.95
    stepping_r = 0.0 < t < 0.20 or 0.88 < t < 1.18
    planted = {"l": None if stepping_l else ("deck" if t >= 0.95 else "floor"),
               "r": None if stepping_r else ("deck" if t >= 1.18 else "floor")}
    if t >= 1.10: planted = {"l": None, "r": None}     # the stance blend owns the feet; checked at the last frame
    if t >= 1.40 - 1e-9: planted = {"l": "deck", "r": "deck"}
    ex = set()
    for s in "lr":
        if planted[s] == "deck" or (t >= 1.10): ex |= {"calf_" + s, "foot_" + s, "toe_" + s}
    if t <= TR + 0.1: ex |= {"lowerarm_r"}
    return S, bp, dict(planted=planted, grounded=t >= TR + fall_t + 0.10, exempt=ex, static_until=None)


def _bail(self, t):
    R, L, cu = self.R, self.R.L, self.R.cu
    st = self._stance_cache()
    roll = 2.0 * cu * (1 - (1 - seg(t, 0.04, 1.10, lambda x: x)) ** 2)
    bp = (add(st["bp"][0], mul(qrot(st["bp"][1], [1.0, 0.0, 0.0]), roll)), st["bp"][1])
    # THE SEAT IS ON THE FLOOR: the pelvis high enough that neither its capsule nor the thighs' go through it
    rp = [c for c in R.caps if c["name"] == "pelvis"][0]["r"]
    rt = max(c["r"] for c in R.caps if c["name"].startswith("thigh"))
    hip_below = R.wp0[R.pel][1] - min(R.wp0[R.leg["l"][0]][1], R.wp0[R.leg["r"][0]][1])
    sit = (R.wp0[R.pel][1] - (self.floor + max(rp, rt + hip_below) + 0.012 * L)) / L
    fall = seg(t, 0.08, 0.52, lambda x: x * x)
    bounce = 0.035 * arc(t, 0.52, 0.72)
    pel = lerp(st["pel"], [0.04, sit, -0.20], fall)
    pel[1] -= bounce
    ends = {s: add(add(R.ank0[s], mul(R.fwd, 0.80 * L)), mul(R.lat, 0.04 * L)) for s in "lr"}
    feet = {}
    for s in "lr":
        a0 = st["feet"][s][0]
        up_ = add(lerp(a0, ends[s], 0.5), [0.0, 0.16 * L, 0.0])
        u = seg(t, 0.06, 0.66)
        a = lerp(lerp(a0, up_, u), lerp(up_, ends[s], u), u)
        a[1] += 0.08 * L * seg(t, 0.03, 0.12) * (1.0 - seg(t, 0.40, 0.66))      # the feet come UP off the deck as it goes
        feet[s] = (a, qnorm(qslerp(st["feet"][s][1], R.wq0[R.leg[s][2]], u)))
    hinge = -30.0 * fall + 16.0 * arc(t, 0.52, 0.85) + 8.0 * seg(t, 0.85, 1.3)
    flail = arc(t, 0.10, 0.50)
    hands = {}
    pw = add(R.wp0[R.pel], add(mul(R.fwd, -0.20 * L), [0.0, -sit * L, 0.0]))
    for s, sg in (("l", 1.0), ("r", -1.0)):
        plant = add(add(pw, mul(R.fwd, -0.36 * L)), add(mul(R.lat, sg * 0.30 * L), [0.0, self.floor - pw[1] + 0.004, 0.0]))
        hands[s] = ("mix", ("hang", 40.0 + 70.0 * flail, 30.0 * flail), ("grip", plant, [0.0, -1.0, 0.0]), seg(t, 0.36, 0.54))
    S = dict(pel=pel, feet=feet, hinge=hinge, hands=hands, curl={"l": 0.15, "r": 0.15},
             look=(-10.0 * arc(t, 0.3, 0.6) + 22.0 * arc(t, 0.55, 0.95) + 10.0 * seg(t, 0.9, 1.4), 0.0),
             knee_pole={s: add(R.fwd, [0.0, 0.6, 0.0]) for s in "lr"},
             blend=(st["q"], st["t"], 1.0 - seg(t, 0.0, 0.22)))
    planted = {s: ("deck" if t <= 0.04 else (None if t < 0.66 else "floor")) for s in "lr"}
    ex = set()
    for s in "lr":
        if planted[s] == "deck" or t <= 0.07: ex |= {"calf_" + s, "foot_" + s, "toe_" + s}
    return S, bp, dict(planted=planted, grounded=True, exempt=ex, static_until=0.04)


# ---- THE PICKUP, v2: a split-stance kneel, torso under 45 degrees, the board right at the front foot ----
def _pk_feet(self, t, prm):
    R, L = self.R, self.R.L
    b0 = R.wp0[R.ball["r"]]
    b1 = sub(b0, mul(self.fw, prm["back"] * L))
    if t < 0.9:
        ball = self.step2(b0, b1, t, 0.10, 0.46, 0.07); hu = prm["heel"] * seg(t, 0.34, 0.74)
    else:
        ball = self.step2(b1, b0, t, 1.10, 1.48, 0.07); hu = prm["heel"] * (1.0 - seg(t, 0.96, 1.16))
    qa = q_axis_angle(self.lt, self.heel_sign * math.radians(hu))
    ank = add(ball, qrot(qa, sub(R.ank0["r"], b0)))
    feet = {"l": self.feet0["l"], "r": (ank, qnorm(qmul(qa, R.wq0[R.leg["r"][2]])))}
    moving = (0.10 < t < 0.46) or (1.10 < t < 1.48)
    return feet, {"l": "floor", "r": None if moving else "floor"}


def _pk_body(self, t, prm):
    TC = 0.80
    c0 = self.carry_spec(0.0, None)
    down = seg(t, 0.24, TC); up = seg(t, 0.92, 1.46); ant = arc(t, 0.0, 0.24)
    k = down * (1.0 - up)
    pel = [prm["px"] * k + 0.03 * ant * (1.0 - down),
           prm["drop"] * k + c0["pel"][1] * up + 0.015 * arc(t, 0.72, 0.92) - 0.01 * ant + 0.012 * arc(t, 1.38, 1.60),
           prm["pz"] * k]
    return dict(pel=pel, hinge=prm["fold"] * k + c0["hinge"] * up + 2.0 * ant - 4.0 * arc(t, 1.24, 1.60),
                side=prm["side"] * k, yaw=prm["yaw"] * k, twist=0.4 * prm["yaw"] * k,
                clav={"r": prm["clav"] * k}, neck_side=-0.55 * prm["side"] * k,
                look=(24.0 * seg(t, 0.04, 0.50) * (1.0 - up) + c0["look"][0] * up + 8.0 * arc(t, 0.95, 1.35), -8.0 * k))


def _pk_left(self, t, S):
    """The free hand braces on the front thigh while she is low (two passes: the legs first)."""
    R = self.R
    la = self.carry_spec(0.0, None)["hands"]["l"]
    rel = seg(t, 0.96, 1.60)
    hang = ("hang", 18.0 * (1.0 - rel) + la[1] * rel, 6.0 * (1.0 - rel) + la[2] * rel - 6.0 * arc(t, 1.10, 1.60))
    wb = seg(t, 0.36, 0.74) * (1.0 - seg(t, 0.96, 1.24))
    if wb <= 1e-4:
        return hang
    S2 = dict(S); S2["hands"] = {"l": hang}
    qo, to = R.pose(S2)
    w, _wq = fk_world(R.nodes, qo, to)
    hip, knee = w[R.leg["l"][0]], w[R.leg["l"][1]]
    rt = [c for c in R.caps if c["name"] == "thigh_l"][0]["r"]
    top = add(lerp(hip, knee, 0.80), [0.0, 0.9 * rt + 0.02 * R.L, 0.0])
    return ("mix", hang, ("grip", top, nrm(add(nrm(sub(knee, hip)), [0.0, -0.8, 0.0]))), wb)


def _pk_spec(self, t, prm, hr):
    feet, planted = self._pk_feet(t, prm)
    S = dict(self._pk_body(t, prm), feet=feet)
    S["hands"] = {"r": hr, "l": self._pk_left(t, S)}
    return S, planted


def _pk_judge(self, prm, bp, G, F):
    R = self.R
    S, _pl = self._pk_spec(0.80, prm, ("grip", G, F))
    qo, to = R.pose(S)
    w, wq = fk_world(R.nodes, qo, to)
    miss = math.dist(R.grip_point("r", w, wq), G) * R.scale
    caps = R.capsules(w, wq)
    body = body_pen(R, caps) * R.scale
    bpen = max(board_pen(self, bp, [c for c in caps if c[3] != "lowerarm_r"])) * R.scale
    kn = knee_ok(R, w, wq)
    S0, _ = self._pk_spec(0.0, prm, ("hang", 14.0, 0.0))
    q0, t0 = R.pose(S0)
    w0, wq0 = fk_world(R.nodes, q0, t0)
    b0 = max(board_pen(self, bp, R.capsules(w0, wq0))) * R.scale
    fwd_u = dot(bp[0], R.fwd) * R.scale; side_u = -dot(bp[0], R.lat) * R.scale
    ok = miss < 0.03 and body < 0.02 and bpen < -0.01 and kn > -0.02 and b0 < -0.02
    sc9 = prm["fold"] + 0.5 * prm["side"] + 25.0 * abs(fwd_u - 0.65) + 15.0 * abs(side_u - 0.45)
    return ok, sc9, ("grip miss %.3f u, body %+.3f u, board %+.3f u, knee %+.3f, clear at start %+.3f u, board %.2f u fwd %.2f u right"
                     % (miss, body, bpen, kn, b0, fwd_u, side_u))


def _pk_setup(self):
    R, L, cu = self.R, self.R.L, self.R.cu
    W = self.B["W"] - 0.004
    q = q_from_axes(self.lt, UP)                                  # flat, nose to her LEFT: its near rail is the carry rail
    F = nrm(add(mul(UP, -0.92), mul(self.fw, 0.38)))              # fingers down over the near rail
    Gy = self.floor + (self.B["DT"] + self.EY) * cu
    best, closest = None, None
    for px in (0.0, 0.08):
        for fold in (28.0, 38.0, 46.0):
            for side in (18.0, 30.0):
                for drop in (0.52, 0.60):
                    prm = dict(back=0.46, heel=38.0, pz=-0.10, px=px, fold=fold, side=side, drop=drop, clav=14.0, yaw=-10.0)
                    feet, _pl = self._pk_feet(0.80, prm)
                    S = dict(self._pk_body(0.80, prm), feet=feet, hands={"l": ("hang", 18.0, 6.0)})
                    qo, to = R.pose(S)
                    w, wq = fk_world(R.nodes, qo, to)
                    fb = max(self.floor - (min(c[0][1], c[1][1]) - c[2]) for c in R.capsules(w, wq)
                             if c[3] in ("thigh_r", "calf_r", "thigh_l", "calf_l", "pelvis"))
                    sh = w[R.arm["r"][0]]
                    wy = Gy - F[1] * R.grip_len["r"]
                    reach = 0.95 * R.A["r"]
                    dv = sh[1] - wy
                    if closest is None or dv - reach < closest[0]: closest = (dv - reach, dict(prm), sh, wy)
                    if fb * R.scale > 0.005 or dv >= reach: continue
                    hz = math.sqrt(reach * reach - dv * dv)
                    for hk in (0.15, 0.55):
                        d9 = nrm(add(mul(self.fw, 0.85), mul(self.rg, 0.5)))
                        wt = [sh[0] + d9[0] * hz * hk, wy, sh[2] + d9[2] * hz * hk]
                        G = add(wt, mul(F, R.grip_len["r"]))
                        bp = self.grounded(sub(G, qrot(q, self.P(self.GX, self.EY, -W))), q)
                        G = self.edge(bp, -1.0)
                        ok, sc9, info = self._pk_judge(prm, bp, G, F)
                        if ok and (best is None or sc9 < best[0]):
                            best = (sc9, dict(prm), bp, G, F, info)
    if best is None:
        gap, prm, sh, wy = closest
        G = add([sh[0], wy, sh[2]], mul(F, R.grip_len["r"]))
        bp = self.grounded(sub(G, qrot(q, self.P(self.GX, self.EY, -W))), q)
        best = (0.0, prm, bp, self.edge(bp, -1.0), F, "⛔ NO CANDIDATE PASSED -- closest reach gap %.3f rig u; gates below will fail" % gap)
    _s, prm, bp, G, F, info = best
    self.pk = dict(prm=prm, bp0=bp, G=G, F=F)
    print("z_sk_board: pickup contact: right foot back %.2f L, heel %.0f, pelvis down %.2f L, fold %.0f, side %.0f, clavicle %.0f -- %s"
          % (prm["back"], prm["heel"], prm["drop"], prm["fold"], prm["side"], prm["clav"], info))


def _pk_clip(self, t):
    R, L = self.R, self.R.L
    prm, bp0, G0, F0c = self.pk["prm"], self.pk["bp0"], self.pk["G"], self.pk["F"]
    TC = 0.80
    cbp = self.carry_board(0.0)
    lift = seg(t, 0.90, 1.40)
    s1 = 0.34
    if lift <= 0.0:
        bp = bp0
    else:
        q0 = bp0[1]; zax = qrot(q0, [0.0, 0.0, 1.0])
        piv = self.bpt(bp0, -self.B["WB"], -self.B["DT"], 0.0)
        def pivot(kk):
            qq = qnorm(qmul(q_axis_angle(zax, math.radians(28.0) * kk), q0))
            oo = sub(piv, qrot(qq, self.P(-self.B["WB"], -self.B["DT"], 0.0)))
            low = oo[1] + self.lowest(qq)
            if low < self.floor: oo[1] += self.floor - low
            return (oo, qq)
        if lift <= s1:
            bp = pivot(lift / s1)
        else:
            u = (lift - s1) / (1.0 - s1)
            pe = pivot(1.0)
            o = lerp(pe[0], cbp[0], u)
            o = add(o, add([0.0, 0.08 * L * math.sin(math.pi * u), 0.0], mul(self.rg, 0.10 * L * math.sin(math.pi * u))))
            bp = (o, qnorm(qslerp(pe[1], cbp[1], u)))
    if t > 1.40:
        bp = (bp[0], qnorm(qmul(q_axis_angle(self.fw, math.radians(4.0) * math.sin(math.pi * (t - 1.40) / 0.20)), bp[1])))
    Fb0 = self.fb(F0c, bp0[1]); Fbc = self.fb(self.F_carry, cbp[1])
    if t < TC:
        above = add(G0, add([0.0, 0.10 * L, 0.0], mul(self.fw, -0.05 * L)))
        hr = ("mix", ("hang", 14.0, 0.0), ("grip", lerp(above, G0, seg(t, 0.56, TC)), F0c), seg(t, 0.22, TC))
    else:
        hr = ("grip", self.edge(bp, -1.0), qrot(bp[1], nrm(lerp(Fb0, Fbc, seg(t, 0.95, 1.42)))))
    S, planted = self._pk_spec(t, prm, hr)
    S["curl"] = {"r": (0.25 + 0.60 * seg(t, TC, 0.90)) * (1.0 - seg(t, 1.45, 1.60)) + 0.8 * seg(t, 1.45, 1.60), "l": 0.3}
    ex = {"lowerarm_r"} if t >= 0.60 else set()
    return S, bp, dict(grip="r", contact=t >= TC - 1e-9, planted=planted, grounded=lift <= 0.0, static_until=TC, exempt=ex)


# ---- THE CATCHUP, v2: the pop rises OUTSIDE the right shin, into a hand that can reach it ----
C_TS, C_TP, C_TC = 0.28, 0.40, 0.62


def _catch_board2(self, t):
    R, L = self.R, self.R.L
    st = self._stance_cache()
    bp0 = st["bp"]; q0 = bp0[1]
    if t <= C_TS:
        return bp0
    zax = qrot(q0, [0.0, 0.0, 1.0])
    piv = self.bpt(bp0, -self.B["WB"], -self.B["DT"], 0.0)
    def about_wheels(th):
        qq = qnorm(qmul(q_axis_angle(zax, th), q0))
        oo = sub(piv, qrot(qq, self.P(-self.B["WB"], -self.B["DT"], 0.0)))
        low = oo[1] + self.lowest(qq)
        if low < self.floor: oo[1] += self.floor - low
        return (oo, qq)
    if t <= C_TP:
        return about_wheels(math.radians(30.0) * seg(t, C_TS, C_TP))
    b30 = about_wheels(math.radians(30.0))
    apex = self._apex
    if t <= C_TC:
        u = seg(t, C_TP, C_TC, lambda x: 1 - (1 - x) * (1 - x))
        o = lerp(b30[0], apex[0], u)
        o = add(o, add(mul(self.rg, 0.10 * L * math.sin(math.pi * u)), mul(self.fw, 0.10 * L * math.sin(math.pi * u))))
        q = qnorm(qslerp(b30[1], apex[1], min(1.0, u * 1.2)))
        low = o[1] + self.lowest(q)
        if low < self.floor: o[1] += self.floor - low
        return (o, q)
    c0 = self.carry_board(0.0)
    u = seg(t, C_TC + 0.02, 1.02)
    o = lerp(apex[0], c0[0], u)
    o[1] -= 0.04 * L * arc(t, C_TC, 0.80)
    q = qnorm(qslerp(apex[1], c0[1], u))
    if t > 1.02:
        q = qnorm(qmul(q_axis_angle(self.fw, math.radians(3.0) * math.sin(math.pi * (t - 1.02) / 0.18)), q))
    return (o, q)


def _catch_body(self, t):
    R, L = self.R, self.R.L
    st = self._stance_cache()
    start = self._behind_spec()
    c0 = self.carry_spec(0.0, None)
    pad_b = (-0.56, board_top(self.B, -0.56), 0.0)
    rel = sub(R.ank0["r"], R.sole_points("r", R.wp0, R.wq0)[4])
    on_tail = add(self.bpt(st["bp"], *pad_b), rel)
    if t <= C_TS:
        ar, qr = self.step2(start["feet"]["r"][0], on_tail, t, 0.02, C_TS, 0.14), R.wq0[R.leg["r"][2]]
    elif t <= C_TP:
        bp = self._catch_board(t)
        dq = qnorm(qmul(bp[1], qinv(st["bp"][1])))
        ar, qr = add(self.bpt(bp, *pad_b), qrot(dq, rel)), qnorm(qmul(dq, R.wq0[R.leg["r"][2]]))
    else:
        b40 = self._catch_board(C_TP)
        dq = qnorm(qmul(b40[1], qinv(st["bp"][1])))
        a40 = add(self.bpt(b40, *pad_b), qrot(dq, rel))
        ar = self.step2(a40, R.ank0["r"], t, C_TP, 0.80, 0.07)
        qr = qnorm(qslerp(qnorm(qmul(dq, R.wq0[R.leg["r"][2]])), R.wq0[R.leg["r"][2]], seg(t, C_TP, 0.70)))
    al = self.step2(start["feet"]["l"][0], R.ank0["l"], t, 0.64, 0.98, 0.07)
    u = seg(t, 0.10, 1.00)
    pel = [0.0, c0["pel"][1] * u + 0.05 * arc(t, 0.26, 0.46) + 0.07 * arc(t, 0.52, 0.90), -st["Sb"] * (1.0 - u)]
    hinge = 12.0 * arc(t, 0.25, 0.85) + c0["hinge"] * seg(t, 0.80, 1.20) - 3.0 * arc(t, 0.95, 1.20)
    bal = arc(t, 0.2, 0.9)
    la = c0["hands"]["l"]
    S = dict(pel=pel, feet={"r": (ar, qr), "l": (al, R.wq0[R.leg["l"][2]])}, hinge=hinge,
             hands={"l": ("hang", 14.0 + (la[1] - 14.0) * seg(t, 0.9, 1.2) + 14.0 * bal, la[2] * seg(t, 0.9, 1.2) - 8.0 * bal)},
             curl={"l": 0.25 + 0.05 * seg(t, 0.9, 1.2)},
             look=(6.0 * (1 - seg(t, 0.8, 1.2)) + 22.0 * arc(t, 0.0, 0.9) + c0["look"][0] * seg(t, 0.8, 1.2), 0.0))
    planted = {"r": "floor" if t <= 0.02 else (None if t < C_TS else ("deck" if t <= C_TP else (None if t < 0.80 else "floor"))),
               "l": "floor" if (t <= 0.64 or t >= 0.98) else None}
    return S, planted


def _catch_setup(self):
    R, L, cu = self.R, self.R.L, self.R.cu
    st = self._stance_cache()
    S, _pl = self._catch_body(C_TC)
    qo, to = R.pose(S)
    w, wq = fk_world(R.nodes, qo, to)
    sh = w[R.arm["r"][0]]
    qa = q_from_axes(nrm(add(UP, mul(self.lt, 0.06))), mul(self.lt, -1.0))
    calf = [c for c in R.caps if c["name"] == "calf_r"][0]["r"]
    shin_out = max(dot(w[R.leg["r"][1]], self.rg), dot(w[R.leg["r"][2]], self.rg)) + calf
    ox = shin_out + self.B["DT"] * cu + 0.05 * L                 # its trucks face her (Y = -lat): they clear the shin
    oz = dot(st["bp"][0], self.fw) + 0.10 * L
    Fc = nrm(add([0.0, -0.45, 0.0], mul(self.fw, 0.9)))
    rel = qrot(qa, self.P(self.GX, self.EY, -(self.B["W"] - 0.004)))
    base = add(mul(self.rg, ox), mul(self.fw, oz))
    wr0 = sub(add(base, rel), mul(Fc, R.grip_len["r"]))
    reach = 0.90 * R.A["r"]
    dxz = math.hypot(wr0[0] - sh[0], wr0[2] - sh[2])
    if dxz > 0.95 * reach:
        d9 = nrm([wr0[0] - sh[0], 0.0, wr0[2] - sh[2]])
        shift = mul(d9, -(dxz - 0.95 * reach))
        base = add(base, shift); dxz = 0.95 * reach
        print("z_sk_board: catchup apex pulled in %.3f rig u to stay in reach (shin clearance now %.3f)" % (math.hypot(shift[0], shift[2]), ox + dot(shift, self.rg) - self.B["DT"] * cu - shin_out))
    wy = sh[1] - math.sqrt(max(0.0, reach * reach - dxz * dxz))
    oy = wy - (rel[1] - Fc[1] * R.grip_len["r"])
    oy = max(oy, self.floor + 0.70 * cu + 0.08 * L)
    self._apex = ([base[0], oy, base[2]], qa)
    self._Fc = Fc
    print("z_sk_board: catchup apex: board %.3f u right of her right shin's outside, tail %.3f u off the floor"
          % ((ox - self.B["DT"] * cu - shin_out) * R.scale, (oy - 0.70 * cu - self.floor) * R.scale))


def _catchup2(self, t):
    R, L = self.R, self.R.L
    S, planted = self._catch_body(t)
    bp = self._catch_board(t)
    apex, Fc = self._apex, self._Fc
    Gc = self.edge(apex, -1.0)
    if t < C_TC:
        wait = add(Gc, add([0.0, 0.06 * L, 0.0], mul(self.fw, -0.06 * L)))
        hr = ("mix", ("hang", 14.0, 0.0), ("grip", lerp(wait, Gc, seg(t, 0.44, C_TC)), Fc), seg(t, 0.10, 0.44))
    else:
        c0b = self.carry_board(0.0)
        Fb0, Fb1 = self.fb(Fc, apex[1]), self.fb(self.F_carry, c0b[1])
        hr = ("grip", self.edge(bp, -1.0), qrot(bp[1], nrm(lerp(Fb0, Fb1, seg(t, C_TC, 1.05)))))
    S["hands"]["r"] = hr
    S["curl"]["r"] = 0.25 + 0.55 * seg(t, C_TC - 0.04, C_TC + 0.06)
    ex = {"lowerarm_r"} if t >= C_TC - 0.10 else set()
    if planted["r"] == "deck" or (C_TP < t < C_TP + 0.08):
        ex |= {"calf_r", "foot_r", "toe_r"}
    return S, bp, dict(grip="r", contact=t >= C_TC - 1e-9, planted=planted, grounded=t <= C_TS, exempt=ex, static_until=C_TS)


Scores.step2 = _step2
Scores._pk_feet = _pk_feet
Scores._pk_body = _pk_body
Scores._pk_left = _pk_left
Scores._pk_spec = _pk_spec
Scores._pk_judge = _pk_judge
Scores.pickup_setup = _pk_setup
Scores.pickup = _pk_clip
Scores._catch_body = _catch_body
Scores._catch_setup = _catch_setup
Scores.judge = _judge
Scores.pickup_probe_start = _pickup_probe_start
Scores._stance_cache = _stance_cache
Scores._behind_feet = _behind_feet
Scores._behind_spec = _behind_spec
Scores._catch_board = _catch_board2
Scores.stepoff = _stepoff
Scores.catchup = _catchup2
Scores.dropin = _dropin
Scores.bail = _bail


def registry(sc):
    ensure_extras(sc)
    sc._catch_setup()
    reg = [("z_rm_sk_carry", 2.0, sc.carry, "loop")]
    if hasattr(sc, "pickup_setup"):
        sc.pickup_setup(); reg.append(("z_rm_sk_pickup", 1.6, sc.pickup, "oneshot"))
    for nm, D, meth in (("z_rm_sk_catchup", 1.2, "catchup"), ("z_rm_sk_dropin", 1.4, "dropin"),
                        ("z_rm_sk_stepoff", 1.0, "stepoff"), ("z_rm_sk_bail", 1.8, "bail")):
        if hasattr(sc, meth):
            reg.append((nm, D, getattr(sc, meth), "oneshot"))
    return reg


def snap(sc, fn, t):
    S, bp, meta = fn(t)
    qo, to = sc.R.pose(S)
    return ({i: qnorm(qo.get(i, sc.R.nodes[i]["q"])) for i in range(len(sc.R.nodes))}, list(to[sc.R.pel]), bp)


def chain_checks(sc, res):
    """The seams: every one-shot starts / ends EXACTLY where its neighbour does (body AND board)."""
    R = sc.R
    fails = []
    def first(nm): k = res[nm]["keep"][0]; return (k[5], k[6], k[3])
    def last(nm): k = res[nm]["keep"][-1]; return (k[5], k[6], k[3])
    st = sc.stance_pose()
    stance = ({i: qnorm(st[0].get(i, R.nodes[i]["q"])) for i in range(len(R.nodes))}, list(st[1]), st[2])
    seams = []
    if "z_rm_sk_carry" in res:
        c0 = first("z_rm_sk_carry")
        seams.append(("carry loops (last == first)", last("z_rm_sk_carry"), c0))
        for nm in ("z_rm_sk_pickup", "z_rm_sk_catchup"):
            if nm in res: seams.append(("%s ends on carry 0" % nm, last(nm), c0))
        if "z_rm_sk_dropin" in res: seams.append(("dropin starts on carry 0", first("z_rm_sk_dropin"), c0))
    if "z_rm_sk_dropin" in res: seams.append(("dropin ends on the stance", last("z_rm_sk_dropin"), stance))
    if "z_rm_sk_stepoff" in res: seams.append(("stepoff starts on the stance", first("z_rm_sk_stepoff"), stance))
    if "z_rm_sk_bail" in res: seams.append(("bail starts on the stance", first("z_rm_sk_bail"), stance))
    if "z_rm_sk_stepoff" in res and "z_rm_sk_catchup" in res:
        seams.append(("stepoff ends on catchup 0", last("z_rm_sk_stepoff"), first("z_rm_sk_catchup")))
    for label, A, Bp in seams:
        gq, gt, gb = pose_gap(R, A, Bp)
        ok = gq < 1e-6 and gt < 1e-5 and gb < 1e-5
        print("  seam %-34s quat %.2e  pelvis %.2e  board %.2e  %s" % (label, gq, gt, gb, "ok" if ok else "BROKEN"))
        if not ok: fails.append("seam " + label)
    return fails


def offsets(sc):
    """Where the set's marks go: the board's pose relative to the body's mark, in WORLD club units."""
    R = sc.R
    def rel(bp, label):
        o = bp[0]
        f = dot(o, R.fwd) * R.scale; l = dot(o, R.lat) * R.scale
        X = qrot(bp[1], [1, 0, 0])
        yaw = math.degrees(math.atan2(dot(X, R.lat), dot(X, R.fwd)))
        print("z_sk_board: OFFSET %-9s board deck-top centre %.3f u FORWARD, %.3f u %s of the body mark (y %.3f); nose points %.0f deg from her front toward her left"
              % (label, f, abs(l), "LEFT" if l >= 0 else "RIGHT", (o[1] - sc.floor) * R.scale, yaw))
    if hasattr(sc, "pk"): rel(sc.pk["bp0"], "pickup")
    if hasattr(sc, "catchup"): rel(sc.catchup(0.0)[1], "catchup")
    if hasattr(sc, "stepoff"): rel(sc.stepoff(1.0)[1], "stepoff")


# ==== GATES ====
def board_pen(sc, bp, caps):
    """Per capsule: the deepest it reaches into any of the board's measured boxes (rig units; <0 = clear)."""
    cu = sc.R.cu
    qi = qinv(bp[1])
    out = []
    for (a, b, r, _nm) in caps:
        ab, bb = qrot(qi, sub(a, bp[0])), qrot(qi, sub(b, bp[0]))
        d = min(seg_box_dist(ab, bb, mul(lo, cu), mul(hi, cu)) for (lo, hi, _k) in sc.B["boxes"])
        out.append(r - d)
    return out or [-1.0]


def body_pen(R, caps):
    """How much deeper than at the stand any two non-adjacent capsules sit inside each other (rig units)."""
    worst = -1.0
    for pr in R.pairs:
        (a1, b1, r1, _n1), (a2, b2, r2, _n2) = caps[pr[0]], caps[pr[1]]
        pen = r1 + r2 - seg_seg_dist(a1, b1, a2, b2)
        worst = max(worst, pen - R.pen0[pr])
    return worst


def body_pen_named(R, caps):
    worst, who = -1.0, ""
    for pr in R.pairs:
        (a1, b1, r1, n1), (a2, b2, r2, n2) = caps[pr[0]], caps[pr[1]]
        pen = r1 + r2 - seg_seg_dist(a1, b1, a2, b2) - R.pen0[pr]
        if pen > worst: worst, who = pen, "%s/%s" % (n1, n2)
    return worst, who


def _flex(R, w, wq, s):
    h, k, a = w[R.leg[s][0]], w[R.leg[s][1]], w[R.leg[s][2]]
    ax = qrot(qmul(wq[R.leg[s][0]], qinv(R.wq0[R.leg[s][0]])), R.lat)   # the thigh's own lateral axis
    return dot(cross(nrm(sub(k, h)), nrm(sub(a, k))), ax)


def knee_ok(R, w, wq):
    """Knee flexion measured in the THIGH'S OWN frame, relative to the stand (sin of the angle; >= 0 is
    bent the right way, < 0 is straighter-than-the-stand = hyperextending). Posture-independent: a
    seated leg bends up, a kneeling one down, and both read positive."""
    if not hasattr(R, "flex0"):
        R.flex0 = {s: _flex(R, R.wp0, R.wq0, s) for s in "lr"}
    return min(_flex(R, w, wq, s) - R.flex0[s] for s in "lr")


def evaluate(sc, S, bp, meta, qo, to):
    R, cu, sca = sc.R, sc.R.cu, sc.R.scale
    w, wq = fk_world(R.nodes, qo, to)
    m = {}
    soles = {s: R.sole_points(s, w, wq) for s in "lr"}
    m["foot_below"] = max(sc.floor - min(p9[1] for p9 in soles[s]) for s in "lr") * sca
    board_low = bp[0][1] + sc.lowest(bp[1])
    m["board_below"] = (sc.floor - board_low) * sca
    m["wheel"] = 0.0
    if meta.get("grounded"):
        m["wheel"] = max(abs(sc.bpt(bp, xs * sc.B["WB"], -sc.B["DT"], zs * sc.B["track"])[1] - sc.floor)
                         for xs in (-1, 1) for zs in (-1, 1)) * sca
    m["plant"] = 0.0
    deckfeet = set()
    for s, where in meta.get("planted", {}).items():
        if where == "floor":
            m["plant"] = max(m["plant"], abs(min(p9[1] for p9 in soles[s]) - sc.floor) * sca)
        elif where == "deck":
            qi = qinv(bp[1])
            hs = []
            for p9 in soles[s]:
                pb = mul(qrot(qi, sub(p9, bp[0])), sca)
                if abs(pb[0]) <= 0.70 and abs(pb[2]) <= sc.B["W"] + 0.03:
                    hs.append(pb[1] - board_top(sc.B, pb[0]))
            m["plant"] = max(m["plant"], abs(min(hs)) if hs else 9.0)
            deckfeet.add(s)
    caps = R.capsules(w, wq)
    m["body"], m["body_who"] = body_pen_named(R, caps)
    m["body"] *= sca
    ex = set(meta.get("exempt", set())) | {"calf_" + s for s in deckfeet} | set(meta.get("exempt_extra", set()))
    pens = board_pen(sc, bp, caps)
    m["board"], m["board_who"] = -9.0, ""
    for c, pn in zip(caps, pens):
        if c[3] in ex: continue
        if pn * sca > m["board"]: m["board"], m["board_who"] = pn * sca, c[3]
    m["flesh_below"] = max(sc.floor - (min(c[0][1], c[1][1]) - c[2]) for c in caps if c[3] in ("pelvis", "thigh_l", "thigh_r")) * sca
    m["grip"] = 0.0
    g = meta.get("grip")
    if g and meta.get("contact"):
        hs9 = S["hands"].get(g)
        if hs9 and hs9[0] == "grip":
            m["grip"] = math.dist(R.grip_point(g, w, wq), hs9[1]) * sca
    m["knee"] = knee_ok(R, w, wq)
    m["w"], m["wq"] = w, wq
    return m


# ==== BAKE, CHECK, PREVIEW ====
def pose_gap(R, A, B):
    (qa, ta, ba), (qb, tb, bb) = A, B
    gq = 0.0
    for i in range(len(R.nodes)):
        x, y = qnorm(qa.get(i, R.nodes[i]["q"])), qnorm(qb.get(i, R.nodes[i]["q"]))
        d = sum(p * q for p, q in zip(x, y))
        gq = max(gq, 1.0 - abs(d))
    gt = math.dist(ta, tb)
    gb = math.dist(ba[0], bb[0]) + (1.0 - abs(sum(p * q for p, q in zip(ba[1], bb[1]))))
    return gq, gt, gb


def run_clip(sc, name, fn, D, kind):
    R = sc.R
    frames = int(round(D * FPS)) + 1
    qtr, ttr, btr, bqtr = {}, [], [], []
    worst = {}
    keep = []
    b0 = None; prevq = None
    static_mv = 0.0
    for f in range(frames):
        t = f / FPS
        S, bp, meta = fn(t)
        qo, to = R.pose(S)
        m = evaluate(sc, S, bp, meta, qo, to)
        if b0 is None: b0 = bp
        if meta.get("static_until") is not None and t < meta["static_until"] - 1e-9:
            static_mv = max(static_mv, math.dist(bp[0], b0[0]) * R.scale + (1.0 - abs(dot4(bp[1], b0[1]))))
        for k in ("foot_below", "board_below", "wheel", "plant", "body", "board", "flesh_below", "grip"):
            if k not in worst or m[k] > worst[k][0]:
                worst[k] = (m[k], t, m.get(k + "_who", ""))
        if "knee" not in worst or m["knee"] < worst["knee"][0]: worst["knee"] = (m["knee"], t, "")
        for i in range(len(R.nodes)):
            qtr.setdefault(i, []).extend(qnorm(qo.get(i, R.nodes[i]["q"])))
        pt = to.get(R.pel, R.nodes[R.pel]["t"])
        ttr.extend(pt)
        bq9 = qnorm(bp[1])
        if prevq is not None and dot4(bq9, prevq) < 0: bq9 = [-x for x in bq9]   # one hemisphere: no flips mid-clip
        prevq = bq9
        btr.extend(bp[0]); bqtr.extend(bq9)
        keep.append((t, m["w"], m["wq"], bp, meta, {i: qnorm(qo.get(i, R.nodes[i]["q"])) for i in range(len(R.nodes))}, list(pt)))
    worst["static"] = (static_mv, 0.0, "")
    tracks = {(i, 0): v for i, v in qtr.items()}
    tracks[(R.pel, 1)] = ttr
    btracks = {(0, 1): btr, (0, 0): bqtr}
    return dict(name=name, frames=frames, tracks=tracks, btracks=btracks, worst=worst, keep=keep, D=D, kind=kind)


def dot4(a, b): return sum(x * y for x, y in zip(a, b))


def report(res, scale_note=""):
    w = res["worst"]
    fails = []
    def lim(k, tol, s="max"):
        v = w[k][0]
        bad = (v > tol) if s == "max" else (v < tol)
        if bad: fails.append("%s %.4f @%.2fs %s (tol %s)" % (k, v, w[k][1], w[k][2], tol))
        return v
    line = ("  %-17s %3df  foot<floor %.4f  board<floor %.4f  wheels %.4f  plant %.4f  body %.4f(%s)  board-vs-body %.4f(%s)  "
            "grip %.4f  static %.4f  knee %.4f  flesh<floor %.3f"
            % (res["name"], res["frames"], lim("foot_below", TOL_FLOOR), lim("board_below", TOL_FLOOR), lim("wheel", TOL_WHEEL),
               lim("plant", TOL_DECKFOOT), lim("body", TOL_BODY), w["body"][2], lim("board", TOL_BOARD), w["board"][2],
               lim("grip", TOL_GRIP), lim("static", 1e-6), lim("knee", -0.02, "min"), lim("flesh_below", 0.01)))
    print(line)
    return fails


def render_preview(sc, res, path, ncols=8):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print("z_sk_board: no PIL, preview skipped"); return
    R = sc.R
    keep = res["keep"]
    picks = sorted(set(int(round(i * (len(keep) - 1) / (ncols - 1))) for i in range(ncols)))
    views = [("front", R.lat, UP, True), ("side", R.fwd, UP, False), ("top", R.lat, R.fwd, True)]
    CW, CH = 230, 300
    img = Image.new("RGB", (CW * len(picks), CH * 3 + 18), (240, 240, 236))
    dr = ImageDraw.Draw(img)
    dr.text((4, CH * 3 + 3), "%s  %s  (front: her left is screen LEFT; side: her front is screen RIGHT; top: her front is UP)" % (os.path.basename(R.path), res["name"]), fill=(0, 0, 0))
    allpts = []
    for (t, w, wq, bp, meta, _q, _t) in keep:
        allpts += [w[i] for i in R.leg["l"] + R.leg["r"] + R.arm["l"] + R.arm["r"] + (R.pel, R.RM["head"])]
        for (lo, hi, _k) in sc.B["boxes"]:
            allpts.append(sc.bpt(bp, lo[0], lo[1], lo[2])); allpts.append(sc.bpt(bp, hi[0], hi[1], hi[2]))
    for vi, (vn, ax, ay, flip) in enumerate(views):
        xs = [dot(p9, ax) * (-1 if flip else 1) for p9 in allpts]; ys = [dot(p9, ay) for p9 in allpts]
        if vn != "top": ys.append(sc.floor)
        cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
        sp = min((CW - 20) / max(1e-6, max(xs) - min(xs)), (CH - 30) / max(1e-6, max(ys) - min(ys)))
        for ci, k in enumerate(picks):
            t, w, wq, bp, meta, _q, _t = keep[k]
            ox, oy = ci * CW, vi * CH
            def pr(p9):
                return (ox + CW / 2 + (dot(p9, ax) * (-1 if flip else 1) - cx) * sp, oy + CH / 2 - (dot(p9, ay) - cy) * sp)
            dr.rectangle([ox, oy, ox + CW - 1, oy + CH - 1], outline=(190, 190, 185))
            if vn != "top":
                fy = oy + CH / 2 - (sc.floor - cy) * sp
                dr.line([ox, fy, ox + CW, fy], fill=(150, 120, 90), width=1)
            for (a, b, r, nm) in R.capsules(w, wq):
                col = (60, 90, 200) if nm.endswith("_l") else ((200, 70, 60) if nm.endswith("_r") else (70, 70, 70))
                dr.line([pr(a), pr(b)], fill=col, width=max(1, int(2 * r * sp * 0.6)))
            for s, col in (("l", (60, 90, 200)), ("r", (200, 70, 60))):
                for p9 in R.sole_points(s, w, wq):
                    x9, y9 = pr(p9); dr.ellipse([x9 - 2, y9 - 2, x9 + 2, y9 + 2], fill=col)
                g9 = R.grip_point(s, w, wq); x9, y9 = pr(g9)
                dr.ellipse([x9 - 3, y9 - 3, x9 + 3, y9 + 3], outline=(0, 0, 0), fill=col)
                dr.line([pr(w[R.arm[s][2]]), pr(g9)], fill=col, width=2)
            for (lo, hi, kind) in sc.B["boxes"]:
                cs = [sc.bpt(bp, (lo, hi)[i >> 0 & 1][0], (lo, hi)[i >> 1 & 1][1], (lo, hi)[i >> 2 & 1][2]) for i in range(8)]
                for i in range(8):
                    for bit in (1, 2, 4):
                        j = i | bit
                        if j != i: dr.line([pr(cs[i]), pr(cs[j])], fill=(20, 140, 40) if kind != "truck" else (120, 120, 40), width=1)
            o9 = pr(bp[0]); xa = pr(sc.bpt(bp, 0.35, 0, 0))
            dr.line([o9, xa], fill=(230, 0, 0), width=2)
            dr.text((ox + 3, oy + 3), "%s t=%.2f" % (vn, t), fill=(0, 0, 0))
    img.save(path)
    print("z_sk_board: preview %s" % path)


def bake_all(sc, results):
    path = sc.R.path
    for res in results:
        for nm in (res["name"], res["name"] + "_bd"):
            assert not lint_name(nm), "%s trips the miners: %s" % (nm, lint_name(nm))
            while drop_clip(path, nm): pass
    out = []
    for res in results:
        out.append((res["name"], FPS, res["frames"], res["tracks"]))
        out.append((res["name"] + "_bd", FPS, res["frames"], res["btracks"]))
    append_clips(path, out)
    print("z_sk_board: appended %s" % ", ".join(o[0] for o in out))


# ==== MAIN ====
def main(argv):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    ap = os.path.abspath(argv[1]).replace("\\", "/").lower()
    if "reva_native/packs" in ap and "--live" not in argv:
        print("z_sk_board: REFUSED: %s is a LIVE pack -- run against a copy, or pass --live to append into it" % argv[1])
        return 2
    if "--force" in argv:
        argv = argv + ["--bake-on-fail"]
    pack = argv[1]
    stance_dir = argv[argv.index("--stance-dir") + 1] if "--stance-dir" in argv else STANCE_DIR
    prev = argv[argv.index("--preview") + 1] if "--preview" in argv else None
    only = argv[argv.index("--only") + 1].split(",") if "--only" in argv else None
    nobake = "--nobake" in argv
    R = Rig(pack)
    B = measure_board()
    sc = Scores(R, B, stance_dir)
    sc.cloud += [sc.P(xs * B["WB"], -B["DT"], zs * B["track"]) for xs in (-1, 1) for zs in (-1, 1)]
    print("z_sk_board: %s  leg %.3f rig u  engine scale %.4f club/rig  floor y %.4f  board: deck top %.4f u, half width %.3f, trucks at +-%.3f, kicks from %.3f"
          % (os.path.basename(pack), R.L, R.scale, sc.floor, B["DT"], B["W"], B["WB"], B["kick0"]))
    reg = registry(sc)
    results, fails = [], []
    for name, D, fn, kind in reg:
        if only and name not in only: continue
        res = run_clip(sc, name, fn, D, kind)
        fails += ["%s: %s" % (name, x) for x in report(res)]
        results.append(res)
        if prev:
            render_preview(sc, res, os.path.join(prev, "%s_%s.png" % (os.path.splitext(os.path.basename(pack))[0], name)))
    fails += chain_checks(sc, {r["name"]: r for r in results})
    offsets(sc)
    if fails:
        print("z_sk_board: ⛔ GATES FAILED (%d):\n  %s" % (len(fails), "\n  ".join(fails)))
        if "--bake-on-fail" not in argv:
            print("z_sk_board: NOTHING BAKED (a gate failed; --force bakes anyway)")
            return 1
        print("z_sk_board: --force: baking despite the failed gates")
    if not nobake:
        bake_all(sc, results)
        p9 = Rpack(pack)
        for res in results:
            ci = p9.clip_names().index(res["name"]); cb = p9.clip_names().index(res["name"] + "_bd")
            a, b = p9.clip(ci), p9.clip(cb)
            assert a["frames"] == b["frames"] and a["fps"] == b["fps"] and b["nTracks"] == 2
            print("z_sk_board: baked %-18s %3d f @ %g fps (%.3f s)  + _bd %d f, %d tracks" % (res["name"], a["frames"], a["fps"], (a["frames"] - 1) / a["fps"], b["frames"], b["nTracks"]))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
