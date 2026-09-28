#!/usr/bin/env python3
"""z_bedup.py -- ROBOT NEKO ep 12, "1 AM": kaeli GETS OUT OF BED, and SMOKES at the window. (kaeli's two clips;
A1's z_bed.py lies the pair down -- z_rm_bed_back / z_rm_bed_nestle -- and these follow it.)

  z_rm_bed_getup  4.0 s ONE-SHOT  from z_rm_bed_nestle frame 0 EXACTLY. Her head comes up off reva and she rocks
                                  IN toward her first (anticipation); the bottom (left) hand PUSHES the mattress and
                                  she rolls away onto her back, the top arm and leg coming back over reva; up on the
                                  right hand, the knees draw up and swing to the far edge while the hips hitch over
                                  twice; she SITS on the edge -- a breath, the right hand back through her hair; then
                                  leans in, hands to the thighs, and stands, the knees taking the load, a small
                                  overshoot and settle. Ends on the plain stand (feet flat on the floor) facing +z.
  z_smoke         5.0 s LOOP      at the window, the classic: left forearm across the waist, the right elbow resting
                                  in that hand, the cigarette up by the chin, fingers loose. A small dip (anticipation),
                                  the lift slowing into the lips, a soft 0.8 s drag with the lids half down (morph
                                  `blink`, if the face sink takes it) and the chest filling; the lazy lower that
                                  overshoots and settles; a slow exhale lifting the face toward the window; the weight
                                  rolling from hip to hip across the loop and a slow head tilt.

⛔ THE GET-UP NEEDS A *FEET* MARK. A1's nestle plays on a `hips` mark, and the engine pins the PELVIS JOINT at a hips
mark's height every frame (crowd_kms.c, partner and hero alike) -- pinned by the hips, nobody can stand up. So the
get-up is authored for a FEET mark ON THE FLOOR at the nestle mark's x/z, yaw 0: the rest mesh's lowest point is the
floor (the engine's `MK->y - P->mny*sc`), and the clip's own pelvis track carries her on the bed. Frame 0 is A1's
frame 0 joint for joint; only the pelvis translation is re-based, by exactly the height the hips mark gave it, so
the two lines meet in the WORLD on the cut. The script prints the mark line and the standing spot.

WORLD FRAME (kaeli_house.set, A1's convention: yaw 0, model axes ARE the world's): x runs along the bed (head of the
bed at x ~2.3), z runs across it toward the far edge (7.80) and the window (14), y up. Mattress solid top 1.02
(x 2.30..6.90, z 3.40..7.80); pillows are boxes to y 1.34. The bedding gives: a body may sink SINK into it.

THE GRIP IN THE SMOKE GATE is the knuckle point between the index and middle fingers (where a cigarette is held),
not the engine's gripNode: on anima that is `hand_r`, the WRIST, and a wrist 0.06 u from the mouth puts the hand
across her face. Both distances are printed.

usage: z_bedup.py <v1_anima_outfit pack> [--nestle-mark x,y,z] [--preview DIR] [--only a,b] [--nobake] [--force] [--live] [--no-lids]
  ⛔ reva_native/packs is REFUSED without --live; a failed gate bakes nothing without --force.
"""
import sys, os, math, struct
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE)); sys.path.insert(0, HERE)
from rpack import Rpack, append_clips, drop_clip, lint_name
from authorlib import (fk_world, solve_limb, translate_node_world, hold_world_rotation, qnorm, qmul, qinv, qrot,
                       q_axis_angle, q_between, qslerp, face_points)
import z_sk_board as SK
from z_sk_board import add, sub, mul, dot, cross, nrm, lerp, clamp01, q_from_axes, seg_seg_dist, seg_box_dist, UP

FPS = 30.0
GETUP_D, SMOKE_D = 4.0, 5.0
NESTLE = "z_rm_bed_nestle"
NESTLE_MARK = (4.90, 1.25, 5.85)       # A1's hips mark for kaeli: a GUESS until z_bed.py prints it (--nestle-mark)
MATTRESS = ((2.30, 0.00, 3.40), (6.90, 1.02, 7.80))
PILLOWS = [((2.33, 1.02, 4.43), (3.57, 1.34, 5.27)), ((2.33, 1.02, 6.03), (3.57, 1.34, 6.87))]
SINK = 0.12                            # club u a body may press into the duvet / a pillow
EDGE_Z = 7.80
SEAT_BACK, FEET_OUT = 0.36, 0.50       # seated: the pelvis this far inside the edge; standing: this far outside it
MOUTH_TOL = 0.06
TORSO = ("pelvis", "spine_01", "spine_02", "spine_03", "neck_01", "head")
I4 = [0.0, 0.0, 0.0, 1.0]


# ---- a smooth spline: Hermite through the keys, Catmull-Rom tangents, a key with a 3rd element 0 STOPS -------------
def herm(keys, t):
    n = len(keys)
    if t <= keys[0][0]: return list(keys[0][1])
    if t >= keys[-1][0]: return list(keys[-1][1])
    i = max(k for k in range(n - 1) if keys[k][0] <= t)
    def tan(k):
        if k == 0 or k == n - 1 or (len(keys[k]) > 2 and keys[k][2] == 0):
            return [0.0] * len(keys[k][1])
        (ta, pa), (tb, pb) = keys[k - 1][:2], keys[k + 1][:2]
        return [(b - a) / (tb - ta) for a, b in zip(pa, pb)]
    (t0, p0), (t1, p1) = keys[i][:2], keys[i + 1][:2]
    h = t1 - t0; u = (t - t0) / h
    m0, m1 = tan(i), tan(i + 1)
    h00, h10, h01, h11 = 2*u**3 - 3*u**2 + 1, u**3 - 2*u**2 + u, -2*u**3 + 3*u**2, u**3 - u**2
    return [h00 * a + h10 * h * ma + h01 * b + h11 * h * mb for a, b, ma, mb in zip(p0, p1, m0, m1)]


def sstep(t, t0, t1):
    u = clamp01((t - t0) / (t1 - t0)) if t1 > t0 else (1.0 if t >= t1 else 0.0)
    return u * u * (3 - 2 * u)


def rotv(v, axis, deg): return qrot(q_axis_angle(axis, math.radians(deg)), v)


# ---- the body ---------------------------------------------------------------------------------------------------
class Body:
    def __init__(self, pack):
        self.R = R = SK.Rig(pack)
        SK.add_foot_caps(R)
        self.p = R.p; self.nodes = R.nodes
        self.mny = R.floor                                   # the engine's P->mny: the rest mesh's lowest point
        fp = face_points(R.p, R.RM)
        hd = R.RM["head"]
        self.mouth_loc = qrot(qinv(R.wq0[hd]), sub(fp["mouth"], R.wp0[hd]))
        # the palm normal of each hand, MEASURED: curling moves the fingertips toward the palm
        self.palm_loc = {}
        for s in "lr":
            hr = R.handrig.get(s); ha = R.arm[s][2]
            if hr and "middle" in hr.spec["tips"]:
                tip = hr.spec["tips"]["middle"]
                w0, _ = fk_world(R.nodes, R.base_q)
                qc = hr.pose(dict(R.base_q), curl=0.7)
                w1, _ = fk_world(R.nodes, qc)
                d = sub(w1[tip], w0[tip]); fd = nrm(sub(w0[tip], w0[ha]))
                pn = nrm(sub(d, mul(fd, dot(d, fd))))
            else:
                pn = mul(R.lat, -1.0 if s == "l" else 1.0)
            self.palm_loc[s] = qrot(qinv(R.wq0[ha]), pn)
        # the morph table (rpack v3 header slot 14): which weights are the lids
        offM = struct.unpack_from("<15I", self.p.buf, 4)[14]
        self.blink = []
        if offM:
            nM = struct.unpack_from("<I", self.p.buf, offM)[0]
            strs = self.p.strings()
            for i in range(nM):
                _mesh, nameId, _n, _off = struct.unpack_from("<4I", self.p.buf, offM + 4 + 16 * i)
                if strs[nameId].decode("utf-8", "replace").lower() == "blink": self.blink.append(i)
        self.blink = [i for i in self.blink if i < len(self.nodes)]   # clipSampleF skips a track whose index >= nNodes

    def body_q(self, up, front):
        up = nrm(up); left = nrm(cross(up, front))
        return qnorm(qmul(q_from_axes(left, up), qinv(q_from_axes(self.R.lat, UP))))

    def frame_of(self, wq, b):
        Q = qnorm(qmul(wq[b], qinv(self.R.wq0[b])))
        return qrot(Q, UP), qrot(Q, self.R.fwd), qrot(Q, self.R.lat)

    def mouth(self, w, wq):
        hd = self.R.RM["head"]
        return add(w[hd], qrot(wq[hd], self.mouth_loc))

    def solve_arm(self, qo, to, s, G, F, pole, palm=None):
        R, nodes = self.R, self.nodes
        ua, la, ha = R.arm[s]
        F = nrm(F)
        wrist = sub(G, mul(F, R.grip_len[s]))
        qo.update(solve_limb(nodes, (ua, la, ha), wrist, pole=pole, q_override=qo, t_override=to))
        w, wq = fk_world(nodes, qo, to)
        cur = nrm(qrot(wq[ha], R.grip_local[s]))
        Q1 = qnorm(qmul(q_between(cur, F), wq[ha]))
        if palm is not None:
            pn = qrot(Q1, self.palm_loc[s])
            a = sub(pn, mul(F, dot(pn, F))); b = sub(palm, mul(F, dot(palm, F)))
            if math.sqrt(dot(a, a)) > 1e-6 and math.sqrt(dot(b, b)) > 1e-6:
                a, b = nrm(a), nrm(b)
                Q1 = qnorm(qmul(q_axis_angle(F, math.atan2(dot(cross(a, b), F), dot(a, b))), Q1))
        qo[ha] = hold_world_rotation(nodes, ha, Q1, qo, to)

    def build(self, C, arms_fn):
        """C: channels in MODEL space. arms_fn(w, wq) -> [(side, G, F, pole, palm), ...] in solve order, called
        with the torso and legs posed (so an arm can aim at the mouth, or at the other elbow). Returns qo, to."""
        R, nodes = self.R, self.nodes
        qo = dict(R.base_q); to = {}
        pel = R.pel
        to[pel] = translate_node_world(nodes, pel, sub(C["pel"], R.wp0[pel]))
        qo[pel] = hold_world_rotation(nodes, pel, qnorm(qmul(self.body_q(C["pu"], C["pf"]), R.wq0[pel])), qo, to)
        top = R.spine[2]
        w, wq = fk_world(nodes, qo, to)
        D = qnorm(qmul(qmul(self.body_q(C["cu"], C["cf"]), R.wq0[top]), qinv(wq[top])))
        part = qslerp(I4, D, 1.0 / 3.0)
        for b in R.spine:
            w, wq = fk_world(nodes, qo, to)
            qo[b] = hold_world_rotation(nodes, b, qnorm(qmul(part, wq[b])), qo, to)
        nk, hd = R.RM["neck_01"], R.RM["head"]
        Qh = qnorm(qmul(self.body_q(C["hu"], C["hf"]), R.wq0[hd]))
        w, wq = fk_world(nodes, qo, to)
        D = qnorm(qmul(Qh, qinv(wq[hd])))
        qo[nk] = hold_world_rotation(nodes, nk, qnorm(qmul(qslerp(I4, D, 0.45), wq[nk])), qo, to)
        qo[hd] = hold_world_rotation(nodes, hd, Qh, qo, to)
        for s in "lr":
            f, b = R.leg[s][2], R.ball[s]
            Qf = self.body_q(C["fu" + s], C["ff" + s])
            qo.update(solve_limb(nodes, R.leg[s], C["a" + s], pole=C["k" + s], q_override=qo, t_override=to))
            qo[f] = hold_world_rotation(nodes, f, qnorm(qmul(Qf, R.wq0[f])), qo, to)
            if b != f: qo[b] = hold_world_rotation(nodes, b, qnorm(qmul(Qf, R.wq0[b])), qo, to)
        w, wq = fk_world(nodes, qo, to)
        for (s, G, F, pole, palm) in arms_fn(w, wq, qo, to):
            self.solve_arm(qo, to, s, G, F, pole, palm)
        for s, hr in R.handrig.items():
            cw = C.get("curl_" + s, 0.3)
            wig = C.get("wig_" + s)
            qo = hr.pose(qo, curl=cw, wiggle=wig)
        return qo, to

    def full(self, qo, to):
        return ({i: qnorm(qo.get(i, self.nodes[i]["q"])) for i in range(len(self.nodes))}, list(to[self.R.pel]))


def blend_pose(A, B, u):
    """Joint-level blend of two full poses (q dict, pelvis t)."""
    if u <= 0.0: return A
    if u >= 1.0: return B
    q = {i: qnorm(qslerp(A[0][i], B[0][i], u)) for i in A[0]}
    return (q, lerp(A[1], B[1], u))


# ---- gates ------------------------------------------------------------------------------------------------------
def box_pen(caps, sc, boxes, W):
    """Deepest a capsule reaches into any box (world, club u; >0 = inside), boxes already shrunk by the sink."""
    worst, who = -9.0, ""
    for (a, b, r, nm) in caps:
        A, B = W(a), W(b)
        for (lo, hi) in boxes:
            pen = r * sc - seg_box_dist(A, B, list(lo), list(hi))
            if pen > worst: worst, who = pen, nm
    return worst, who


def arm_pen(R, caps, base):
    worst, who = -9.0, ""
    arms = [c for c in caps if c[3].startswith(("upperarm", "lowerarm"))]
    body = [c for c in caps if c[3] in TORSO or c[3].startswith(("thigh", "calf"))]
    for (a, b, r, n1) in arms:
        for (a2, b2, r2, n2) in body:
            if n1.startswith("upperarm") and n2 in ("spine_03", "neck_01"): continue    # the shoulder joint itself
            pen = r + r2 - seg_seg_dist(a, b, a2, b2) - base.get((n1, n2), 0.0)
            if pen > worst: worst, who = pen, "%s/%s" % (n1, n2)
    return worst, who


def qmat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


class Cloud:
    """THE TORSO AND THIGHS AS THEIR OWN SKIN (voxel-thinned, rigid per dominant bone) -- what an arm must not go
    through. A capsule is the wrong witness here: the chest's p90 capsule ends in a sphere at the neck base that
    swallows the chin, so every hand brought to the mouth 'penetrated' it."""
    def __init__(self, B):
        R = B.R
        fam = [R.pel] + list(R.spine) + [R.leg[s][0] for s in "lr"]
        pts = {}
        for v in R.p.skinned_positions(with_bones=True):
            if v[3] in fam:
                key = (round(v[0] / 0.018), round(v[1] / 0.018), round(v[2] / 0.018))
                if key not in pts: pts[key] = v
        wr, wqr = fk_world(R.nodes)
        self.groups = {}
        for b in fam:
            loc = [qrot(qinv(wqr[b]), sub(v[:3], wr[b])) for v in pts.values() if v[3] == b]
            if loc: self.groups[b] = np.array(loc)
        self.R = R
        self.base = None
        self.base = self.pen(R.wp0, R.wq0, raw=True)

    def hand_pen(self, w, wq, s, r):
        """The HAND against the skin: wrist to middle fingertip at the hand's half-thickness r (rig u); > 0 inside."""
        A, Bv = (np.array(v) for v in _hand_seg(self.R, w, wq, s))
        P = self.world(w, wq)
        ab = Bv - A; l2 = float(ab @ ab) or 1e-12
        t = np.clip((P - A) @ ab / l2, 0.0, 1.0)
        return r - float(np.linalg.norm(P - (A + t[:, None] * ab), axis=1).min())

    def world(self, w, wq):
        return np.vstack([loc @ qmat(wq[b]).T + np.array(w[b]) for b, loc in self.groups.items()])

    def pen(self, w, wq, raw=False, sides="lr", upper_from=0.25):
        R = self.R
        P = self.world(w, wq)
        out = {}
        for s in sides:
            for nm, a, b, r in (("upperarm_" + s, R.arm[s][0], R.arm[s][1], [c for c in R.caps if c["name"] == "upperarm_" + s][0]["r"]),
                                ("lowerarm_" + s, R.arm[s][1], R.arm[s][2], [c for c in R.caps if c["name"] == "lowerarm_" + s][0]["r"])):
                A, Bv = np.array(w[a]), np.array(w[b])
                ab = Bv - A; l2 = float(ab @ ab) or 1e-12
                t = np.clip((P - A) @ ab / l2, 0.0, 1.0)
                d = np.linalg.norm(P - (A + t[:, None] * ab), axis=1)
                if nm.startswith("upperarm"):
                    d = np.where(t < upper_from, 9.0, d)    # the shoulder joint itself is not a contact
                out[nm] = r - float(d.min())
        if raw: return out
        return {k: v - max(0.0, self.base.get(k, 0.0)) for k, v in out.items()}


def _hand_seg(R, w, wq, s):
    hr = R.handrig.get(s)
    if hr and "middle" in hr.spec["tips"]: return w[R.arm[s][2]], w[hr.spec["tips"]["middle"]]
    return w[R.arm[s][2]], R.grip_point(s, w, wq)


def arm_base(R):
    caps = R.capsules(R.wp0, R.wq0)
    out = {}
    for (a, b, r, n1) in caps:
        for (a2, b2, r2, n2) in caps:
            if n1.startswith(("upperarm", "lowerarm")):
                out[(n1, n2)] = max(0.0, r + r2 - seg_seg_dist(a, b, a2, b2))
    return out


# ---- THE GET-UP ------------------------------------------------------------------------------------------------
class Getup:
    def __init__(self, B, nestle_mark):
        self.B = B; R = B.R
        self.H = nestle_mark
        sc = R.scale; self.sc = sc
        self.W2M = lambda p: [(p[0] - self.H[0]) / sc, p[1] / sc + B.mny, (p[2] - self.H[2]) / sc]
        self.M2W = lambda m: [self.H[0] + sc * m[0], (m[1] - B.mny) * sc, self.H[2] + sc * m[2]]
        names = B.p.clip_names()
        self.A = None
        if NESTLE in names:
            ci = names.index(NESTLE)
            q = {}; t = {}
            for (node, typ, data) in B.p.tracks(ci):
                comp = 4 if typ == 0 else (1 if typ == 3 else 3)
                if typ == 3: continue
                v = list(struct.unpack_from("<%df" % comp, data, 0))
                if typ == 0: q[node] = qnorm(v)
                elif typ == 1: t[node] = v
            qA = {i: q.get(i, qnorm(R.nodes[i]["q"])) for i in range(len(R.nodes))}
            tA = {R.pel: t.get(R.pel, R.nodes[R.pel]["t"])}
            wA, _ = fk_world(R.nodes, qA, tA)
            dy = (self.H[1] / sc + B.mny) - wA[R.pel][1]          # the hips mark's height, re-based onto a floor mark
            tpel = translate_node_world(R.nodes, R.pel, [0.0, dy, 0.0], qA, tA)
            self.A = (qA, list(tpel))
            print("z_bedup: START POSE = A1's %s frame 0, read from %s (pelvis re-based %+.4f rig u for a FLOOR feet mark)"
                  % (NESTLE, B.p.path, dy))
        else:
            print("z_bedup: ⛔ START POSE = STAND-IN: there is no %s in %s. Re-run on the pack that carries A1's clip "
                  "(with --nestle-mark from z_bed.py's print) to seat it exactly." % (NESTLE, B.p.path))
        self.keys()

    def start_channels(self):
        """World-space channels of the start pose: measured off A1's frame 0, or the stand-in."""
        B, R = self.B, self.B.R
        if self.A:
            w, wq = fk_world(R.nodes, self.A[0], {R.pel: self.A[1]})
            ch = {"pel": self.M2W(w[R.pel])}
            for k, b in (("p", R.pel), ("c", R.spine[2]), ("h", R.RM["head"])):
                u, f, _l = B.frame_of(wq, b); ch[k + "u"], ch[k + "f"] = u, f
            for s in "lr":
                hip, kn, an = w[R.leg[s][0]], w[R.leg[s][1]], w[R.leg[s][2]]
                ch["a" + s] = self.M2W(an)
                u, f, _l = B.frame_of(wq, R.leg[s][2]); ch["fu" + s], ch["ff" + s] = u, f
                ha = sub(an, hip); l2 = dot(ha, ha) or 1e-9
                ch["k" + s] = nrm(sub(sub(kn, hip), mul(ha, dot(sub(kn, hip), ha) / l2)))
                ua, la, hnd = w[R.arm[s][0]], w[R.arm[s][1]], w[R.arm[s][2]]
                g = R.grip_point(s, w, wq)
                ch["g" + s] = self.M2W(g); ch["d" + s] = nrm(sub(g, hnd))
                sw = sub(hnd, ua); l2 = dot(sw, sw) or 1e-9
                ch["e" + s] = nrm(sub(sub(la, ua), mul(sw, dot(sub(la, ua), sw) / l2)))
                ch["palm" + s] = qrot(wq[R.arm[s][2]], B.palm_loc[s])
            return ch
        x0, y0, z0 = self.H
        return {"pel": [x0, y0, z0], "pu": [-1.0, 0.0, 0.0], "pf": nrm([0.0, -0.3, -1.0]),
                "cu": nrm([-1.0, 0.05, -0.12]), "cf": nrm([0.0, -0.25, -1.0]),
                "hu": nrm([-0.92, 0.12, -0.38]), "hf": nrm([-0.3, -0.35, -0.89]),
                "al": [x0 + 1.55, 1.13, z0 - 0.10], "ar": [x0 + 1.20, 1.45, z0 - 0.72],
                "ful": nrm([-0.87, 0.0, -0.5]), "ffl": nrm([0.5, 0.0, -0.87]),
                "fur": nrm([-0.87, 0.0, -0.5]), "ffr": nrm([0.5, 0.0, -0.87]),
                "kl": nrm([0.0, 0.2, -1.0]), "kr": nrm([0.0, 0.3, -1.0]),
                "gr": [x0 - 0.10, 1.50, z0 - 0.95], "dr": nrm([0.2, -0.4, -0.9]), "er": nrm([0.0, 1.0, 0.3]),
                "gl": [x0 - 0.95, 1.42, z0 - 0.30], "dl": nrm([-1.0, 0.0, -0.2]), "el": nrm([0.0, -1.0, 0.4]),
                "palmr": [0.0, -1.0, 0.0], "palml": [0.0, 0.0, -1.0]}

    def keys(self):
        B, R = self.B, self.B.R
        S0 = self.start_channels(); self.S0 = S0
        x0, y0, z0 = S0["pel"]
        xs, zs, zf = x0 + 0.15, EDGE_Z - SEAT_BACK, EDGE_Z + FEET_OUT
        self.spot = (xs, zf)
        # THE END STAND: the rest pose, translated (her rest faces +z = the window, and the mark is yaw 0)
        pe_m = self.W2M([xs, 0.0, zf]); pe_m[1] = R.wp0[R.pel][1] - B.mny + B.mny   # rest height above the floor
        pe_m[1] = R.wp0[R.pel][1]
        self.T = sub(pe_m, R.wp0[R.pel])
        qe = dict(R.base_q); te = {R.pel: add(R.nodes[R.pel]["t"], [0, 0, 0])}
        te[R.pel] = translate_node_world(R.nodes, R.pel, self.T)
        self.E = B.full(qe, te)
        wE, wqE = fk_world(R.nodes, self.E[0], {R.pel: self.E[1]})
        AL, AR = self.M2W(wE[R.leg["l"][2]]), self.M2W(wE[R.leg["r"][2]])
        self.plant = {"l": wE[R.leg["l"][2]], "r": wE[R.leg["r"][2]]}
        GLe, GRe = self.M2W(R.grip_point("l", wE, wqE)), self.M2W(R.grip_point("r", wE, wqE))
        DLe = nrm(sub(R.grip_point("l", wE, wqE), wE[R.arm["l"][2]])); DRe = nrm(sub(R.grip_point("r", wE, wqE), wE[R.arm["r"][2]]))
        PE = self.M2W(wE[R.pel])
        PB = [x0 + 0.04, 1.25, z0 + 0.32]
        zb = PB[2]
        zh1 = zb + 0.55 * (zs - zb)
        PS = [xs, 1.255, zs]
        up, fz = [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]
        K = {}
        K["pel"] = [(0.0, S0["pel"], 0), (0.30, add(S0["pel"], [0.0, 0.03, -0.03])), (0.85, PB),
                    (1.22, [x0 + 0.08, 1.31, zh1]), (1.42, [x0 + 0.10, 1.27, zh1 + 0.06]),
                    (1.68, [x0 + 0.13, 1.30, zs - 0.03]), (1.95, PS, 0), (2.40, add(PS, [0.0, 0.012, 0.0])),
                    (2.95, PS, 0), (3.15, add(PS, [0.0, 0.0, 0.05])), (3.45, [xs, 1.60, zf - 0.20]),
                    (3.72, [xs, PE[1] - 0.03, zf - 0.03]), (3.86, [xs, PE[1] + 0.012, zf]), (4.0, PE, 0)]
        def fr(u, f): return (nrm(u), nrm(f))
        pf = [(0.0, fr(S0["pu"], S0["pf"]), 0), (0.30, fr(S0["pu"], S0["pf"])),
              (0.85, fr([-1.0, 0.05, 0.0], [0.05, 1.0, 0.0])), (1.25, fr([-0.55, 0.83, 0.10], [0.83, 0.55, 0.30])),
              (1.50, fr([-0.36, 0.88, -0.30], [0.62, 0.47, 0.63])), (1.70, fr([-0.10, 0.96, -0.26], [0.45, 0.24, 0.86])), (1.95, fr([0.0, 0.97, -0.24], [0.0, 0.24, 0.97]), 0),
              (2.95, fr([0.0, 0.97, -0.24], [0.0, 0.24, 0.97]), 0), (3.20, fr([0.0, 0.91, 0.42], [0.0, -0.42, 0.91])),
              (3.50, fr([0.0, 0.90, 0.44], [0.0, -0.44, 0.90])), (3.76, fr([0.0, 0.995, 0.10], [0.0, -0.10, 0.995])),
              (3.88, fr([0.0, 0.998, -0.06], [0.0, 0.06, 0.998])), (4.0, fr(up, fz), 0)]
        cf = [(0.0, fr(S0["cu"], S0["cf"]), 0), (0.30, fr(rotv(S0["cu"], [0, 0, 1], -6.0), rotv(S0["cf"], [0, 0, 1], -6.0))),
              (0.85, fr([-1.0, 0.10, 0.05], [0.10, 1.0, 0.0])), (1.25, fr([-0.55, 0.80, 0.22], [0.70, 0.55, 0.45])),
              (1.50, fr([-0.40, 0.84, -0.36], [0.62, 0.52, 0.58])), (1.70, fr([-0.10, 0.95, -0.30], [0.25, 0.32, 0.92])), (1.95, fr([0.0, 0.95, 0.30], [0.0, -0.30, 0.95]), 0),
              (2.30, fr([0.0, 0.99, 0.12], [0.0, -0.12, 0.99])), (2.62, fr([0.0, 0.995, 0.05], [0.0, -0.05, 0.995])),
              (2.95, fr([0.0, 0.95, 0.30], [0.0, -0.30, 0.95])), (3.20, fr([0.0, 0.83, 0.56], [0.0, -0.56, 0.83])),
              (3.50, fr([0.0, 0.89, 0.46], [0.0, -0.46, 0.89])), (3.76, fr([0.0, 0.99, 0.12], [0.0, -0.12, 0.99])),
              (3.88, fr([0.0, 0.998, -0.06], [0.0, 0.06, 0.998])), (4.0, fr(up, fz), 0)]
        hf = [(0.0, fr(S0["hu"], S0["hf"]), 0), (0.30, fr(rotv(S0["hu"], [0, 0, 1], -14.0), rotv(S0["hf"], [0, 0, 1], -14.0))),
              (0.85, fr([-0.92, 0.34, 0.18], [0.30, 0.85, 0.40])), (1.25, fr([-0.25, 0.94, 0.22], [0.55, 0.05, 0.83])),
              (1.70, fr([0.0, 0.97, 0.25], [0.2, -0.25, 0.95])), (1.95, fr([0.0, 0.90, 0.44], [0.0, -0.44, 0.90]), 0),
              (2.30, fr([0.0, 0.97, -0.25], [0.0, 0.25, 0.97])), (2.62, fr([0.05, 0.99, -0.12], [0.0, 0.12, 0.99])),
              (2.95, fr([0.0, 0.88, 0.47], [0.0, -0.47, 0.88])), (3.20, fr([0.0, 0.80, 0.60], [0.0, -0.60, 0.80])),
              (3.50, fr([0.0, 0.93, 0.36], [0.0, -0.36, 0.93])), (3.80, fr([0.0, 1.0, 0.02], [0.0, -0.02, 1.0])),
              (4.0, fr(up, fz), 0)]
        for nm, lst in (("p", pf), ("c", cf), ("h", hf)):
            K[nm + "u"] = [(k[0], k[1][0]) + k[2:] for k in lst]
            K[nm + "f"] = [(k[0], k[1][1]) + k[2:] for k in lst]
        def swing(Pc, deg, reach, y, side):
            a = math.radians(deg); d = [math.cos(a), 0.0, math.sin(a)]; lf9 = [math.sin(a), 0.0, -math.cos(a)]
            o = 0.36 if side == "l" else -0.14
            return [Pc[0] + reach * d[0] + o * lf9[0], y, Pc[2] + reach * d[2] + o * lf9[2]]
        P122, P150 = [x0 + 0.08, 1.31, zh1], [x0 + 0.115, 1.285, 0.5 * (zh1 + 0.06 + zs - 0.03)]
        K["al"] = [(0.0, S0["al"], 0), (0.85, [x0 + 1.72, 1.12, zb - 0.16]), (1.25, swing(P122, 18.0, 1.42, 1.13, "l")),
                   (1.50, swing(P150, 48.0, 1.48, 1.04, "l")), (1.64, [AL[0] + 0.05, 0.55, EDGE_Z + 0.52]), (1.95, AL, 0), (4.0, AL, 0)]
        K["ar"] = [(0.0, S0["ar"], 0), (0.42, [x0 + 1.30, 1.58, (S0["ar"][2] + zb) * 0.5 + 0.05]),
                   (0.85, [x0 + 1.74, 1.12, zb + 0.16]), (1.25, swing(P122, 18.0, 1.42, 1.15, "r")),
                   (1.50, swing(P150, 48.0, 1.48, 1.04, "r")), (1.64, [AR[0] - 0.05, 0.55, EDGE_Z + 0.52]), (1.95, AR, 0), (4.0, AR, 0)]
        for s in "lr":
            K["fu" + s] = [(0.0, S0["fu" + s], 0), (0.85, nrm([-0.87, 0.50, 0.0])), (1.30, nrm([-0.3, 0.9, 0.3])),
                           (1.70, nrm([0.0, 0.95, -0.30])), (1.95, up, 0), (4.0, up, 0)]
            K["ff" + s] = [(0.0, S0["ff" + s], 0), (0.85, nrm([0.50, 0.87, 0.0])), (1.30, nrm([0.7, 0.1, 0.7])),
                           (1.70, nrm([0.0, 0.30, 0.95])), (1.95, fz, 0), (4.0, fz, 0)]
            sx = 0.15 if s == "l" else -0.15
            K["k" + s] = [(0.0, S0["k" + s], 0), (0.85, nrm([0.05, 1.0, 0.0])), (1.25, nrm([0.45, 0.80, 0.25])),
                          (1.50, nrm([0.40, 0.65, 0.65])), (1.72, nrm([0.1, 0.35, 0.93])), (1.95, nrm([sx, 0.25, 1.0]), 0), (4.0, nrm([sx * 0.5, 0.1, 1.0]), 0)]
        # the arms: ("h", (left, up, front)) keys ride the head, evaluated every frame
        K["gr"] = [(0.0, S0["gr"], 0), (0.28, add(S0["gr"], [0.0, -0.02, 0.0])), (0.46, add(S0["gr"], [0.02, 0.0, 0.04])),
                   (0.64, [x0 - 0.15, 1.78, z0 + 0.18]),
                   (0.85, [x0 - 0.05, 1.05, zb + 0.52]), (1.25, [x0 - 0.10, 1.05, zh1 + 0.30]),
                   (1.68, [xs - 0.48, 1.05, zs - 0.10]), (1.95, [xs - 0.47, 1.05, zs - 0.02], 0),
                   (2.12, [xs - 0.58, 1.52, zs - 0.06]), (2.32, ("h", (-0.08, 0.14, 0.28))), (2.48, ("h", (-0.11, 0.37, 0.07))),
                   (2.62, ("h", (-0.17, 0.25, -0.21))), (2.82, ("h", (-0.36, -0.10, -0.08))),
                   (3.02, [xs - 0.47, 1.05, zs + 0.02]), (3.20, [xs - 0.47, 1.05, zs + 0.04]), (3.46, [xs - 0.52, 1.42, zs + 0.40]), (4.0, GRe, 0)]
        K["gl"] = [(0.0, S0["gl"], 0), (0.45, [x0 - 0.55, 1.82, z0 - 0.02]), (0.66, [x0 - 0.05, 1.46, zb - 0.54]),
                   (0.85, [x0 + 0.25, 1.06, zb - 0.50]), (1.25, [x0 + 0.20, 1.06, zh1 - 0.42]),
                   (1.45, [x0 + 0.45, 1.16, zh1 - 0.18]), (1.68, [xs + 0.50, 1.06, zs - 0.16]), (1.95, [xs + 0.47, 1.05, zs - 0.02], 0),
                   (2.95, [xs + 0.47, 1.05, zs - 0.02], 0), (3.20, [xs + 0.47, 1.05, zs + 0.04]),
                   (3.46, [xs + 0.52, 1.42, zs + 0.40]), (4.0, GLe, 0)]
        dm = nrm([0.0, -0.45, 0.89])
        K["dr"] = [(0.0, S0["dr"], 0), (0.46, S0["dr"]), (0.64, nrm([0.3, 0.2, 0.9])), (0.85, nrm([0.9, -0.44, 0.0])), (1.25, nrm([0.6, -0.5, 0.6])),
                   (1.95, dm, 0), (2.12, nrm([0.0, 0.6, 0.8])), (2.32, ("h", (0.0, 0.25, -0.97))), (2.62, ("h", (0.0, -0.2, -0.98))),
                   (2.82, ("h", (0.0, -0.8, -0.3))), (3.02, dm), (3.20, dm), (4.0, DRe, 0)]
        K["dl"] = [(0.0, S0["dl"], 0), (0.45, nrm([0.2, 0.9, 0.1])), (0.66, nrm([0.6, -0.2, -0.6])), (0.85, nrm([0.9, -0.44, 0.0])), (1.25, nrm([0.3, -0.5, 0.8])),
                   (1.95, dm, 0), (2.95, dm, 0), (3.20, dm), (4.0, DLe, 0)]
        K["er"] = [(0.0, S0["er"], 0), (0.46, S0["er"]), (0.64, nrm([0.0, 1.0, 0.5])), (0.85, nrm([0.0, -0.3, 1.0])), (1.25, nrm([-0.6, -0.1, -0.8])),
                   (1.95, nrm([-0.5, 0.0, -1.0]), 0), (2.32, nrm([-1.0, 0.3, 0.7])), (2.62, nrm([-1.0, 0.5, 0.4])),
                   (3.02, nrm([-0.5, 0.0, -1.0])), (3.20, nrm([-0.5, 0.0, -1.0])), (4.0, nrm([-0.5, 0.0, -1.0]), 0)]
        K["el"] = [(0.0, S0["el"], 0), (0.45, nrm([0.1, -0.3, -1.0])), (0.85, nrm([0.0, -0.3, -1.0])), (1.25, nrm([0.4, -0.1, -0.9])),
                   (1.95, nrm([0.5, 0.0, -1.0]), 0), (2.95, nrm([0.5, 0.0, -1.0]), 0), (3.20, nrm([0.5, 0.0, -1.0])),
                   (4.0, nrm([0.5, 0.0, -1.0]), 0)]
        pdn = [0.0, -1.0, 0.0]
        K["palmr"] = [(0.0, S0["palmr"], 0), (0.46, S0["palmr"]), (0.64, nrm([0.0, -0.5, -0.8])), (0.85, pdn), (1.95, pdn, 0), (2.12, nrm([0.0, 0.3, -1.0])),
                      (2.32, ("h", (0.0, -1.0, 0.0))), (2.62, ("h", (0.0, -1.0, 0.0))), (2.82, ("h", (0.9, -0.3, 0.0))),
                      (3.08, pdn), (3.40, pdn), (4.0, nrm([1.0, 0.0, 0.0]), 0)]
        K["palml"] = [(0.0, S0["palml"], 0), (0.45, nrm([0.0, -0.3, -1.0])), (0.85, pdn), (1.95, pdn, 0), (2.95, pdn, 0), (3.10, pdn), (3.40, pdn),
                      (4.0, nrm([-1.0, 0.0, 0.0]), 0)]
        self.K = K
        self.end_frames = (wE, wqE)

    def on_hair(self, w, wq, G, F):
        """THE HAND THROUGH HER HAIR TOUCHES THE HAIR, IT DOES NOT ENTER IT. Her head proxy is the p90 of the
        head-weighted skin -- the updo included -- so the knuckles are held on its surface (plus the hand's own
        thickness) and the fingers lie along it, tipped a little in toward the scalp: then the wrist, and the whole
        forearm behind it, stay outside. The spline alone cut the chord between its keys: 0.085 u into the proxy."""
        R = self.B.R
        hd = R.RM["head"]
        cap = [c for c in R.caps if c["name"] == "head"][0]
        a = w[hd]
        b = add(a, qrot(qmul(wq[hd], qinv(R.wq0[hd])), cap["off"]))
        ab = sub(b, a); l2 = dot(ab, ab) or 1e-12
        u = clamp01(dot(sub(G, a), ab) / l2)
        c = add(a, mul(ab, u))
        v = sub(G, c); d = math.sqrt(dot(v, v)) or 1e-9
        n = mul(v, 1.0 / d)
        need = cap["r"] + 0.035 * R.cu
        if d < need: G = add(c, mul(n, need))
        Ft = sub(F, mul(n, dot(F, n)))
        if math.sqrt(dot(Ft, Ft)) < 1e-6: Ft = qrot(qmul(wq[hd], qinv(R.wq0[hd])), [0.0, 0.0, -1.0])
        F = nrm(sub(nrm(Ft), mul(n, 0.28)))
        return G, F

    def head_eval(self, key, w, wq, direction):
        """A key value: a world point/direction, or ("h", (left, up, front)) in the head's frame (club u)."""
        B = self.B; R = B.R
        if isinstance(key, tuple) and key and key[0] in ("kl", "kr"):
            kn = w[R.leg[key[0][1]][1]]
            if direction: return nrm(list(key[1]))
            return self.M2W(add(kn, mul(list(key[1]), 1.0 / self.sc)))
        if isinstance(key, tuple) and key and key[0] == "h":
            u, f, l = B.frame_of(wq, R.RM["head"])
            o = key[1]
            v = add(add(mul(l, o[0]), mul(u, o[1])), mul(f, o[2]))
            if direction: return nrm(v)
            return self.M2W(add(w[R.RM["head"]], mul(v, 1.0 / self.sc)))
        return key

    def chan(self, nm, t, w=None, wq=None, direction=False):
        ks = self.K[nm]
        ev = [(k[0], self.head_eval(k[1], w, wq, direction)) + tuple(k[2:]) for k in ks]
        return herm(ev, t)

    def frame(self, t):
        B, R = self.B, self.B.R
        C = {}
        C["pel"] = self.W2M(self.chan("pel", t))
        for nm in ("pu", "pf", "cu", "cf", "hu", "hf", "ful", "ffl", "fur", "ffr", "kl", "kr"):
            C[nm] = nrm(self.chan(nm, t))
        for s in "lr":
            C["a" + s] = self.W2M(self.chan("a" + s, t))
        C["curl_l"] = 0.30 + 0.15 * sstep(t, 1.8, 2.0) - 0.15 * sstep(t, 3.0, 3.3)
        C["curl_r"] = 0.30 - 0.18 * sstep(t, 2.1, 2.3) + 0.18 * sstep(t, 2.7, 2.9)
        def arms(w, wq, qo, to):
            out = []
            for s in "rl":
                G = self.W2M(self.chan("g" + s, t, w, wq))
                F = nrm(self.chan("d" + s, t, w, wq, True))
                if s == "r" and 2.02 <= t <= 3.00:
                    G, F = self.on_hair(w, wq, G, F)
                out.append((s, G, F, nrm(self.chan("e" + s, t)), nrm(self.chan("palm" + s, t, w, wq, True))))
            return out
        qo, to = B.build(C, arms)
        P = B.full(qo, to)
        if self.A:
            P = blend_pose(P, self.A, 1.0 - sstep(t, 0.0, 0.30))
        P = blend_pose(P, self.E, sstep(t, GETUP_D - 0.35, GETUP_D))
        return P


# ---- THE SMOKE LOOP ---------------------------------------------------------------------------------------------
class Smoke:
    T_MOUTH, T_DRAG_END, T_EXHALE = 0.85, 1.65, 3.15

    def __init__(self, B, cloud):
        self.B = B; R = B.R; self.cloud = cloud; self.L_loc = None
        self.setup_drag()
        self.r_hand = 0.028 * R.cu                     # the hand's half-thickness: palm to the knuckle line
        self.sc = R.scale; self.cu = R.cu
        self.lft, self.fw = R.lat, R.fwd
        self.rgt = mul(R.lat, -1.0)

    def frame(self, t):
        B, R = self.B, self.B.R
        L, cu = R.L, R.cu
        D = SMOKE_D
        ph = 2.0 * math.pi * t / D
        # the weight, hip to hip across the loop: on the RIGHT leg at t=0, the LEFT at 2.5 s
        wgt = math.cos(ph)                                        # +1 right, -1 left
        up, fz, lf = [0.0, 1.0, 0.0], R.fwd, R.lat
        pel = add(R.wp0[R.pel], add(mul(lf, -0.030 * L * wgt), [0.0, -0.012 * L - 0.004 * L * abs(wgt), 0.0]))
        pu = rotv(up, fz, -4.0 * wgt)                                                  # the free hip drops
        pf = rotv(fz, up, 3.0 * wgt)
        # the drag fills the chest; the exhale lets it go
        inh = sstep(t, 0.70, 1.60) * (1.0 - sstep(t, 2.90, 3.90))
        chu = rotv(rotv(up, fz, 3.5 * wgt), lf, -4.0 * inh)                           # contrapposto: the shoulders tilt back
        chf = rotv(rotv(fz, up, -2.0 * wgt), lf, -4.0 * inh)
        # the head: in to the hand for the drag, a slow lift to the window on the exhale, a slow tilt all loop
        dip = sstep(t, 0.30, 0.85) * (1.0 - sstep(t, 1.55, 2.10))
        ex = sstep(t, 2.95, 3.85) * (1.0 - sstep(t, 4.05, 4.95))
        tilt = 5.0 * math.sin(ph + 0.9)
        hu = rotv(rotv(rotv(up, lf, 7.0 * dip - 16.0 * ex), fz, tilt), up, 0.0)
        hf = rotv(rotv(rotv(fz, lf, 7.0 * dip - 16.0 * ex), fz, tilt), up, -6.0 * ex)
        hu = rotv(hu, up, -6.0 * ex)
        C = {"pel": pel, "pu": pu, "pf": pf, "cu": chu, "cf": chf, "hu": hu, "hf": hf}
        for s in "lr":
            C["a" + s] = R.ank0[s]
            C["fu" + s], C["ff" + s] = up, fz
            C["k" + s] = add(fz, mul(lf, 0.15 if s == "l" else -0.15))
        # the fingers: the cigarette between index and middle, loose; ring and pinky fall in. The free hand: loose.
        C["curl_l"] = 0.34; C["wig_l"] = {"thumb": -8.0, "index": -6.0, "pinky": 8.0}
        # THE SMOKING HAND, languid (jacob: "intimate, sensual, kind sweet... low energy, relaxed, dreamy"): hanging
        # easy by her hip after the exhale -> a long slow raise that runs straight on into the lift -> slowing into the
        # lips (0.85) -> the drag -> the lazy lower to her chest -> the exhale (3.15) -> and down to hang again.
        lift = sstep(t, 0.20, 0.85)
        lift = 1.0 - (1.0 - lift) ** 2 if lift < 1.0 else 1.0                             # the slow-in at the lips
        low = sstep(t, 1.65, 2.45)
        settle = math.sin(math.pi * clamp01((t - 2.25) / 0.55)) if 2.25 < t < 2.80 else 0.0   # overshoot, lazy
        at = lift * (1.0 - low)
        lam = self.lam(t)
        sway = math.sin(ph - 0.9)                                                             # a beat behind the hips
        # the cigarette hand: loose at rest; for the drag, index and middle only slightly bent, ring and pinky INTO the palm
        C["curl_r"] = 0.22 + (self.CURL_DRAG - 0.22) * at
        C["wig_r"] = {"ring": 18.0 + self.RING_X * at, "pinky": 26.0 + self.RING_X * at, "thumb": -10.0 + self.THUMB_X * at,
                      "index": -4.0 + self.INDEX_X * at}

        def hang(w, s9, abd, swing, reach, fwd_tilt):
            """A hanging arm, in the WORLD (gravity), from wherever her shoulder has got to."""
            sg = 1.0 if s9 == "l" else -1.0
            d = rotv(rotv([0.0, -1.0, 0.0], fz, sg * abd), lf, -swing)
            wrist = add(w[R.arm[s9][0]], mul(d, R.A[s9] * reach))
            F = nrm(add(d, mul(fz, fwd_tilt)))
            return (add(wrist, mul(F, R.grip_len[s9])), F, nrm(add(mul(fz, -1.0), mul(lf, sg * 0.35))),
                    nrm(add(mul(lf, -sg), mul(fz, -0.25))))

        def arms(w, wq, qo, to):
            cu_, cf_, cl_ = B.frame_of(wq, R.spine[2])
            hu_, hf_, hl_ = B.frame_of(wq, R.RM["head"])
            chest = w[R.spine[2]]
            m = B.mouth(w, wq)
            g_lo = add(chest, add(add(mul(cl_, -0.24 * cu), mul(cu_, -0.04 * settle * cu)), mul(cf_, 0.34 * cu)))
            d_lo = nrm(add(add(mul(cu_, 1.0), mul(cf_, 0.10)), mul(cl_, 0.12)))
            g_hg, d_hg, pole_hg, palm_hg = hang(w, "r", self.abd_r, 5.0 + 2.0 * sway, 0.88, 0.45)
            g_hi = add(m, add(add(mul(hf_, 0.045 * cu), mul(hu_, -0.012 * cu)), mul(hl_, -0.008 * cu)))
            d_hi = nrm(add(add(mul(hu_, 0.85), mul(hf_, -0.35)), mul(hl_, 0.28)))
            # between the hang and the chest the hand travels an ARC, out in front and to the side, so the forearm swings
            # clear of her hip and thigh instead of cutting the straight line through them
            arc9 = 4.0 * lam * (1.0 - lam)
            G = add(lerp(g_lo, g_hg, lam), mul(add(mul(fz, 0.16), mul(self.rgt, 0.12)), arc9 * cu))
            F = nrm(lerp(d_lo, d_hg, lam))
            pole_c = nrm(add(add(mul(up, -1.0), mul(self.rgt, 0.70)), mul(fz, 0.25)))
            pole_r = nrm(lerp(pole_c, pole_hg, lam))
            palm_r = nrm(lerp(add(mul(cf_, -0.6), mul(cl_, 0.8)), palm_hg, lam))
            return [("r", G, F, pole_r, palm_r)]
        # the torso and legs first; then the arms, each checked against her own skin
        qo, to = B.build(C, lambda w, wq, qo, to: [])
        w, wq = fk_world(R.nodes, qo, to)
        abd_r0 = self.ABD_R + 3.0 * max(0.0, wgt) + 1.5 * sway
        if lam > 0.0:
            def pose_r(abd):
                G_r, F_r, pole_h, palm_h = hang(w, "r", abd, 5.0 + 2.0 * sway, 0.88, 0.45)
                q9 = dict(qo)
                B.solve_arm(q9, to, "r", G_r, F_r, pole_h, palm_h)
                w9, wq9 = fk_world(R.nodes, q9, to)
                return q9, -self.free_pen(w9, wq9, "r") * R.scale, self.cap_pen(w9, wq9, "r")
            self.abd_r = self.clear_abd(pose_r, abd_r0)
        else:
            self.abd_r = abd_r0
        (s9, G, F, pole_r, palm_r), = arms(w, wq, qo, to)
        cu_, cf_, cl_ = B.frame_of(wq, R.spine[2])
        # the smoking forearm clears her chest while the hand is up (it moves out in front) -- never inside the
        # lips window, which the mouth gate owns, and not once it hangs (the hang is clear by construction, and gated)
        pushed = 0.0
        for _it in range(8):
            q9 = dict(qo)
            B.solve_arm(q9, to, "r", G, F, pole_r, palm_r)
            w9, wq9 = fk_world(R.nodes, q9, to)
            worst = self.cloud.pen(w9, wq9, sides="r")["lowerarm_r"] * R.scale
            if worst <= 0.012 or pushed >= 0.10 * cu or 0.70 <= t <= 1.70 or lam > 0.5: break
            st9 = min((worst + 0.008) * cu, 0.10 * cu - pushed); pushed += st9
            G = add(G, mul(cf_, st9))
        qo = q9
        # THE DRAG, POSED FROM THE CIGARETTE (the engine holds it on middle_01_r, 0.16 u long along the palm normal,
        # gripped 30% along: the FILTER is 0.048 u out on the palm side). jacob: "looks like she is picking her nose" --
        # the old pose put the knuckles at the lips with the fingers straight up in front of her nose. Now the palm faces
        # her mouth from her front-right, index and middle point up-and-out 45 deg toward her right so the fingertips sit
        # beside her cheek, and the filter tip is seated ON the lips. Blended in by `at`: wrist lerp, hand slerp.
        if at > 1e-6:
            wo, wqo = fk_world(R.nodes, qo, to)
            W_o, Q_o = wo[R.arm["r"][2]], wqo[R.arm["r"][2]]
            W_d, Q_d, pole_d, n_p = self.drag_config(w, wq)
            Qh = qnorm(qslerp(Q_o, Q_d, at)); pole = nrm(lerp(pole_r, pole_d, at))
            Wt = lerp(W_o, W_d, at)
            pushed = 0.0
            for _it in range(8):
                q9 = dict(qo)
                q9.update(solve_limb(R.nodes, R.arm["r"], Wt, pole=pole, q_override=q9, t_override=to))
                q9[R.arm["r"][2]] = hold_world_rotation(R.nodes, R.arm["r"][2], Qh, q9, to)
                w9, wq9 = fk_world(R.nodes, q9, to)
                if at >= 0.999:
                    # the drag: seat the filter tip on the lips -- a hair (STANDOFF) in front of the lip point
                    err = sub(sub(B.mouth(w9, wq9), mul(n_p, self.STANDOFF * R.cu)), self.filter_tip(w9, wq9))
                    if math.sqrt(dot(err, err)) * R.scale < 0.001: break
                    Wt = add(Wt, err)
                else:
                    # ON THE WAY UP AND DOWN the blended forearm keeps clear of her chest: the wrist moves out in front
                    # (the chest push ran only on the chest pose, before the blend -- the lowering grazed her 0.077)
                    worst = self.cloud.pen(w9, wq9, sides="r")["lowerarm_r"] * R.scale
                    if worst <= 0.012 or pushed >= 0.12 * cu: break
                    st9 = min((worst + 0.008) * cu, 0.12 * cu - pushed); pushed += st9
                    Wt = add(Wt, mul(cf_, st9))
            qo = q9
        # THE FREE ARM, RELAXED AT HER SIDE: a soft elbow, hanging in the world, swaying a beat behind the weight
        # shift -- and CLEAR of her waist, hip and thigh by more than the contact tolerance. The gap is measured
        # against her skin and the arm swings out exactly as far as it must: a bisection for the smallest angle that
        # clears, so it is a smooth function of time (no per-frame nudging, no jitter).
        w, wq = fk_world(R.nodes, qo, to)
        abd0, swing = 11.0 + 1.5 * sway, 3.0 + 2.5 * sway

        def pose_l(abd):
            G_l, F_l, pole_l, palm_l = hang(w, "l", abd, swing, 0.955, 0.12)
            q9 = dict(qo)
            B.solve_arm(q9, to, "l", G_l, F_l, pole_l, palm_l)
            w9, wq9 = fk_world(R.nodes, q9, to)
            return q9, -self.free_pen(w9, wq9, "l") * R.scale, self.cap_pen(w9, wq9, "l")
        q9 = pose_l(self.clear_abd(pose_l, abd0))[0]
        return B.full(q9, to)

    GAP = 0.032         # club u: the free arm (and the hanging smoking arm) stay this far off her skin
    FILTER, EMBER = 0.048, 0.112   # club u from middle_01_r along the palm normal: filter (palm side), ember (the back)
    STANDOFF = 0.006    # club u: the filter tip sits this far in front of the lip point (touching, not inside)
    # the drag hand, chosen by probe (probe_drag*.py): fingers at the 40 deg end of the brief (the ring/pinky edge of a
    # palm-to-mouth right hand RISES as the fingers tilt out -- anatomy -- and at 45-50 deg it reached her nose), the
    # palm facing the mouth from her front-right, ring and pinky tucked tight, index and middle straight
    FING_DEG = 40.0
    PALM_F, PALM_L = 0.66, 0.75
    POLE_OUT = 1.30
    CURL_DRAG, RING_X, THUMB_X, INDEX_X = -0.12, 100.0, 0.0, -16.0

    def setup_drag(self):
        B, R = self.B, self.B.R
        hr = R.handrig["r"]; ha = R.arm["r"][2]
        self.mid01 = hr.spec["fingers"]["middle"][0]
        tip = hr.spec["tips"]["middle"]
        wp0, wq0 = R.wp0, R.wq0
        palm_w = qrot(wq0[ha], B.palm_loc["r"])
        self.palm_mid_loc = qrot(qinv(wq0[self.mid01]), palm_w)
        self.palm_hand_loc = B.palm_loc["r"]
        self.off_mid_loc = qrot(qinv(wq0[ha]), sub(wp0[self.mid01], wp0[ha]))
        self.fdir_loc = qrot(qinv(wq0[ha]), nrm(sub(wp0[tip], wp0[self.mid01])))
        hd = R.RM["head"]
        fam = {hd}; grew = True
        while grew:
            grew = False
            for i, nd in enumerate(R.nodes):
                if nd["parent"] in fam and i not in fam: fam.add(i); grew = True
        pts = {}
        for v in R.p.skinned_positions(with_bones=True):
            if v[3] in fam:
                key = (round(v[0] / 0.010), round(v[1] / 0.010), round(v[2] / 0.010))
                if key not in pts: pts[key] = v
        wr, wqr = fk_world(R.nodes)
        self.hgroups = {}
        for b in fam:
            loc = [qrot(qinv(wqr[b]), sub(v[:3], wr[b])) for v in pts.values() if v[3] == b]
            if loc: self.hgroups[b] = np.array(loc)
        fp = face_points(R.p, R.RM)
        self.nose_loc = qrot(qinv(wq0[hd]), sub(fp["nose"], wp0[hd]))
        nz = [v for v in pts.values() if abs(v[1] - fp["nose"][1]) < 0.02 and v[2] > fp["nose"][2] - 0.03
              and abs(v[0] - fp["nose"][0]) < 0.06]
        hw = sorted(abs(v[0] - fp["nose"][0]) for v in nz)
        self.nose_hw = (hw[int(0.95 * (len(hw) - 1))] if hw else 0.035 * R.cu) * R.scale
        print("z_bedup: smoke -- head skin %d points (hair included); nose half-width %.3f u (%d skin points round it); "
              "the cigarette: filter %.3f u / ember %.3f u along the palm normal from %s"
              % (sum(len(g) for g in self.hgroups.values()), self.nose_hw, len(nz), self.FILTER, self.EMBER, R.names[self.mid01]))

    def hworld(self, w, wq):
        return np.vstack([loc @ qmat(wq[b]).T + np.array(w[b]) for b, loc in self.hgroups.items()])

    def filter_tip(self, w, wq):
        return add(w[self.mid01], mul(qrot(wq[self.mid01], self.palm_mid_loc), self.FILTER * self.B.R.cu))

    def ember(self, w, wq):
        return add(w[self.mid01], mul(qrot(wq[self.mid01], self.palm_mid_loc), -self.EMBER * self.B.R.cu))

    def drag_config(self, w, wq):
        """Wrist position, hand world rotation and elbow pole for the drag, from the head's frame this frame."""
        B, R = self.B, self.B.R
        u, f, l = B.frame_of(wq, R.RM["head"])
        m = B.mouth(w, wq)
        n_p = nrm(add(mul(f, -self.PALM_F), mul(l, self.PALM_L)))   # the palm faces the mouth, from her front-right
        a = math.radians(self.FING_DEG)
        fd0 = nrm(add(mul(u, math.cos(a)), mul(l, -math.sin(a))))   # up-and-out, toward her right
        fd = nrm(sub(fd0, mul(n_p, dot(fd0, n_p))))
        Qh = qnorm(qmul(q_from_axes(fd, n_p), qinv(q_from_axes(self.fdir_loc, self.palm_hand_loc))))
        K = sub(m, mul(n_p, self.FILTER * R.cu))                    # middle_01_r: the filter tip then sits on the lips
        W = sub(K, qrot(Qh, self.off_mid_loc))
        pole = nrm(add(add(mul(u, -1.0), mul(l, -self.POLE_OUT)), mul(f, 0.15)))
        return W, Qh, pole, n_p

    def finger_gap(self, w, wq):
        """The closest any finger segment (knuckle to tip, every finger) comes to her head's skin, club u, less a
        finger's half-thickness (0.012 u)."""
        R = self.B.R
        P = self.hworld(w, wq)
        hr = R.handrig["r"]
        best = 9.0
        for fn, ch in hr.spec["fingers"].items():
            pts9 = [w[j] for j in ch] + ([w[hr.spec["tips"][fn]]] if fn in hr.spec["tips"] else [])
            for a9, b9 in zip(pts9, pts9[1:]):
                A, Bv = np.array(a9), np.array(b9)
                ab = Bv - A; l2 = float(ab @ ab) or 1e-12
                t9 = np.clip((P - A) @ ab / l2, 0.0, 1.0)
                best = min(best, float(np.linalg.norm(P - (A + t9[:, None] * ab), axis=1).min()))
        return best * R.scale - 0.012

    def tips_out(self, w, wq):
        """How far the index and middle fingertips sit to her RIGHT of the nose's own half-width, club u."""
        R = self.B.R
        hd = R.RM["head"]
        u, f, l = self.B.frame_of(wq, hd)
        nose = add(w[hd], qrot(wq[hd], self.nose_loc))
        hr = R.handrig["r"]
        offs = [-dot(sub(w[hr.spec["tips"][fn]], nose), l) * R.scale for fn in ("index", "middle")]
        return min(offs) - self.nose_hw
    ABD_R = 12.0        # the smoking arm hangs this far out, in degrees, before the weight shift adds to it

    def lam(self, t):
        """How far the smoking hand has sunk to its easy hang: down after the exhale (3.30 -> 4.40), and back up in a
        long raise that wraps the loop (4.55 -> 0.20) and runs straight on into the lift."""
        u9 = (t - 4.55) % SMOKE_D
        if u9 <= 0.65: return 1.0 - sstep(u9, 0.0, 0.65)
        return sstep(t, 3.30, 4.40)

    def free_pen(self, w, wq, s):
        """RAW penetration (rig u; < 0 is a gap) of an arm -- upper, fore AND hand -- into her torso and thigh skin."""
        pe = self.cloud.pen(w, wq, raw=True, sides=s, upper_from=0.5)
        return max(pe["upperarm_" + s], pe["lowerarm_" + s], self.cloud.hand_pen(w, wq, s, self.r_hand))

    def cap_pen(self, w, wq, s):
        """The arm's capsules against every other body capsule (the body gate's own test, this arm only), club u."""
        R = self.B.R
        caps = R.capsules(w, wq)
        worst = -9.0
        for pr in R.pairs:
            n1, n2 = caps[pr[0]][3], caps[pr[1]][3]
            if not (n1.endswith("arm_" + s) or n2.endswith("arm_" + s)): continue
            (a1, b1, r1, _n1), (a2, b2, r2, _n2) = caps[pr[0]], caps[pr[1]]
            worst = max(worst, r1 + r2 - seg_seg_dist(a1, b1, a2, b2) - R.pen0[pr])
        return worst * R.scale

    def clear_abd(self, pose_fn, abd0):
        """The smallest outward angle at which a hanging arm is off her skin by GAP and off every capsule by the body
        tolerance -- by bisection, so it is a smooth function of time."""
        q9, gap, cp = pose_fn(abd0)
        if gap >= self.GAP and cp <= 0.02: return abd0
        lo, hi = abd0, abd0 + 30.0
        for _it in range(12):
            mid = 0.5 * (lo + hi)
            _q, g9, c9 = pose_fn(mid)
            if g9 >= self.GAP and c9 <= 0.02: hi = mid
            else: lo = mid
        return hi

    def blink(self, t):
        """The lids come half down on the drag (the face's own blinks keep running)."""
        return 0.42 * sstep(t, 0.80, 1.05) * (1.0 - sstep(t, 1.55, 1.95))


# ---- run, gate, preview, bake ------------------------------------------------------------------------------------
def evaluate(B, P, W, floor_m, boxes, abase, cloud=None, bed_from=0.0, t=0.0):
    R = B.R
    w, wq = fk_world(R.nodes, P[0], {R.pel: P[1]})
    caps = R.capsules(w, wq)
    m = {"w": w, "wq": wq}
    m["body"], m["body_who"] = SK.body_pen_named(R, caps); m["body"] *= R.scale
    if cloud is not None:
        pe = cloud.pen(w, wq); k9 = max(pe, key=pe.get)
        m["arm"], m["arm_who"] = pe[k9] * R.scale, k9 + " vs torso/thigh skin"
    else:
        a, who = arm_pen(R, caps, abase); m["arm"], m["arm_who"] = a * R.scale, who
    soles = min(p9[1] for s in "lr" for p9 in R.sole_points(s, w, wq))
    m["floor"] = (floor_m - min(soles, min(min(c[0][1], c[1][1]) - c[2] for c in caps))) * R.scale
    m["knee"] = SK.knee_ok(R, w, wq)
    m["bed"], m["bed_who"] = box_pen(caps, R.scale, boxes, W) if (boxes and t >= bed_from - 1e-9) else (-9.0, "")
    return m


def render_face(B, S, path, worst, t=1.25):
    """The drag, close: her head's skin, the hand and fingers, the cigarette -- front and right-side views."""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return
    R = B.R
    P = S.frame(t)
    w, wq = fk_world(R.nodes, P[0], {R.pel: P[1]})
    hd = R.RM["head"]
    u, f, l = B.frame_of(wq, hd)
    m = B.mouth(w, wq)
    H = S.hworld(w, wq)
    hr = R.handrig["r"]
    nose = add(w[hd], qrot(wq[hd], S.nose_loc))
    CW, CH, half = 520, 560, 0.34 * R.cu
    img = Image.new("RGB", (CW * 2, CH + 44), (240, 240, 236))
    dr = ImageDraw.Draw(img)
    for vi, (name, ax, title) in enumerate(((0, l, "FRONT (her right on the LEFT)"), (1, f, "RIGHT SIDE (her front to the right)"))):
        ox = vi * CW
        sp = (CW - 20) / (2 * half)
        def pr(p9):
            v = sub(p9, m)
            x = dot(v, ax) if vi == 1 else -dot(v, l) * -1.0
            return (ox + CW / 2 + x * sp, CH / 2 - dot(v, u) * sp)
        for q9 in H:
            x9, y9 = pr(list(q9)); dr.point((x9, y9), fill=(165, 160, 150))
        if vi == 0:
            for sgn in (-1.0, 1.0):
                xx = pr(add(nose, mul(l, sgn * (S.nose_hw + 0.03) * R.cu)))[0]
                dr.line([xx, 20, xx, CH - 10], fill=(210, 120, 200), width=1)
        a9 = pr(w[R.arm["r"][2]]); b9 = pr(w[S.mid01])
        dr.line([a9, b9], fill=(40, 60, 160), width=5)
        for fn, ch in hr.spec["fingers"].items():
            pts9 = [w[j] for j in ch] + ([w[hr.spec["tips"][fn]]] if fn in hr.spec["tips"] else [])
            dr.line([pr(p9) for p9 in [w[R.arm["r"][2]]] + pts9], fill=(60, 90, 210), width=4)
        fl, em = S.filter_tip(w, wq), S.ember(w, wq)
        dr.line([pr(fl), pr(em)], fill=(245, 245, 245), width=7)
        dr.line([pr(fl), pr(em)], fill=(120, 120, 120), width=1)
        for p9, col, r9 in ((m, (240, 160, 0), 5), (fl, (20, 170, 60), 5), (em, (230, 40, 20), 5), (nose, (200, 60, 200), 4)):
            x9, y9 = pr(p9); dr.ellipse([x9 - r9, y9 - r9, x9 + r9, y9 + r9], fill=col)
        dr.text((ox + 6, 6), "%s  t=%.2f" % (title, t), fill=(0, 0, 0))
    g = lambda k: ("%s %.3f" % (k, worst[k][0])) if k in worst else ""
    dr.text((6, CH + 6), "orange mouth, green FILTER tip, red EMBER, magenta nose (lines: its half-width + 0.03), blue hand/fingers, grey dots = her head skin", fill=(0, 0, 0))
    dr.text((6, CH + 24), "gates: %s   %s   %s" % (g("filter"), g("finger_gap"), g("tips_out")), fill=(0, 0, 0))
    img.save(path)
    print("z_bedup: preview %s" % path)


def render(B, frames, path, title, W, boxes, marks_fn=None, ncols=8):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print("z_bedup: no PIL, preview skipped"); return
    R = B.R
    picks = sorted(set(int(round(i * (len(frames) - 1) / (ncols - 1))) for i in range(ncols)))
    views = [("top  (x right, z down)", 0, 2, False), ("side (x right, y up)", 0, 1, True), ("end  (z right, y up)", 2, 1, True)]
    CW, CH = 240, 280
    img = Image.new("RGB", (CW * len(picks), CH * 3 + 18), (240, 240, 236))
    dr = ImageDraw.Draw(img)
    dr.text((4, CH * 3 + 3), title, fill=(0, 0, 0))
    geo = []
    for (t, w, wq) in frames:
        caps = R.capsules(w, wq)
        geo.append([(W(a), W(b), r * R.scale, nm) for (a, b, r, nm) in caps])
    allp = [p for g in geo for c in g for p in (c[0], c[1])]
    for (lo, hi) in boxes: allp += [list(lo), list(hi)]
    for vi, (vn, ax, ay, yup) in enumerate(views):
        xs = [p[ax] for p in allp]; ys = [p[ay] for p in allp]
        if yup: ys.append(0.0)
        cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
        sp = min((CW - 16) / max(1e-6, max(xs) - min(xs)), (CH - 26) / max(1e-6, max(ys) - min(ys)))
        for ci, k in enumerate(picks):
            t, w, wq = frames[k]
            ox, oy = ci * CW, vi * CH
            def pr(p):
                y = (p[ay] - cy) if yup else -(p[ay] - cy)
                return (ox + CW / 2 + (p[ax] - cx) * sp, oy + CH / 2 - y * sp)
            dr.rectangle([ox, oy, ox + CW - 1, oy + CH - 1], outline=(190, 190, 185))
            for (lo, hi) in boxes:
                a9, b9 = pr(lo), pr(hi)
                dr.rectangle([min(a9[0], b9[0]), min(a9[1], b9[1]), max(a9[0], b9[0]), max(a9[1], b9[1])], outline=(150, 120, 90))
            if yup:
                fy = pr([0.0, 0.0, 0.0])[1]
                dr.line([ox, fy, ox + CW, fy], fill=(120, 90, 60), width=1)
            for (a, b, r, nm) in geo[k]:
                col = (60, 90, 200) if nm.endswith("_l") else ((200, 70, 60) if nm.endswith("_r") else (70, 70, 70))
                dr.line([pr(a), pr(b)], fill=col, width=max(1, int(2 * r * sp * 0.6)))
            for s, col in (("l", (60, 90, 200)), ("r", (200, 70, 60))):
                g = W(R.grip_point(s, w, wq)); x9, y9 = pr(g)
                dr.ellipse([x9 - 3, y9 - 3, x9 + 3, y9 + 3], outline=(0, 0, 0), fill=col)
                dr.line([pr(W(w[R.arm[s][2]])), pr(g)], fill=col, width=2)
            if marks_fn:
                for (p, col) in marks_fn(w, wq):
                    x9, y9 = pr(W(p)); dr.ellipse([x9 - 3, y9 - 3, x9 + 3, y9 + 3], fill=col)
            dr.text((ox + 3, oy + 3), "%s t=%.2f" % (vn, t), fill=(0, 0, 0))
    img.save(path)
    print("z_bedup: preview %s" % path)


def run(B, name, D, fn, loop, W, floor_m, boxes, planted=None, extra=None, cloud=None, bed_from=0.0, gate_from=0.0):
    R = B.R
    pre = {}
    n = int(round(D * FPS)) + 1
    abase = arm_base(R)
    qtr, ttr = {}, []
    worst = {}
    frames = []
    prev = None
    for f in range(n):
        t = f / FPS
        P = fn(t)
        m = evaluate(B, P, W, floor_m, boxes, abase, cloud, bed_from, t)
        for k in ("body", "arm", "floor", "bed"):
            tgt = worst if (t >= gate_from - 1e-9 or k == "floor") else pre
            if k not in tgt or m[k] > tgt[k][0]: tgt[k] = (m[k], t, m.get(k + "_who", ""))
        if "knee" not in worst or m["knee"] < worst["knee"][0]: worst["knee"] = (m["knee"], t, "")
        if planted:
            for s, (t0, spot) in planted.items():
                if t >= t0 - 1e-9:
                    d = math.dist(m["w"][R.leg[s][2]], spot) * R.scale
                    if "plant" not in worst or d > worst["plant"][0]: worst["plant"] = (d, t, s)
        if extra: extra(t, m, worst)
        for i in range(len(R.nodes)):
            q = P[0][i]
            if prev is not None and dot(q[:3], prev[0][i][:3]) + q[3] * prev[0][i][3] < 0: q = [-x for x in q]
            P[0][i] = q
            qtr.setdefault(i, []).extend(q)
        ttr.extend(P[1])
        prev = P
        frames.append((t, m["w"], m["wq"]))
    tracks = {(i, 0): v for i, v in qtr.items()}
    tracks[(R.pel, 1)] = ttr
    return dict(name=name, frames=n, tracks=tracks, worst=worst, keep=frames, first=fn(0.0), last=fn(D), pre=pre)


def report(res, tols):
    w = res["worst"]; fails = []
    parts = []
    for k, (tol, mode) in tols.items():
        if k not in w: continue
        v, t, who = w[k]
        bad = (v > tol) if mode == "max" else (v < tol)
        parts.append("%s %.4f%s" % (k, v, ("(%s @%.2fs)" % (who, t)) if who or bad else ""))
        if bad: fails.append("%s: %s %.4f @%.2fs %s (tol %s)" % (res["name"], k, v, t, who, tol))
    print("  %-16s %3df  %s" % (res["name"], res["frames"], "  ".join(parts)))
    return fails


def pose_gap(A, Bp):
    gq = max(1.0 - abs(sum(x * y for x, y in zip(A[0][i], Bp[0][i]))) for i in A[0])
    return gq, math.dist(A[1], Bp[1])


def main(argv):
    try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception: pass
    pack = argv[1]
    ap = os.path.abspath(pack).replace("\\", "/").lower()
    if "reva_native/packs" in ap and "--live" not in argv:
        print("z_bedup: REFUSED: %s is a LIVE pack -- run against a copy, or pass --live" % pack); return 2
    prev = argv[argv.index("--preview") + 1] if "--preview" in argv else None
    only = argv[argv.index("--only") + 1].split(",") if "--only" in argv else None
    nm_arg = argv[argv.index("--nestle-mark") + 1] if "--nestle-mark" in argv else None
    H = tuple(float(v) for v in nm_arg.split(",")) if nm_arg else NESTLE_MARK
    B = Body(pack); R = B.R
    cloud = Cloud(B)
    stem = os.path.splitext(os.path.basename(pack))[0]
    print("z_bedup: %s  leg %.3f rig u  engine scale %.4f  mny %.4f  lids (blink morphs) %s" % (stem, R.L, R.scale, B.mny, B.blink))
    out, fails = [], []
    tols = {"body": (0.03, "max"), "arm": (0.03, "max"), "floor": (0.01, "max"), "bed": (0.01, "max"),
            "knee": (-0.02, "min"), "plant": (0.015, "max"), "filter": (0.02, "max"), "finger_gap": (0.0, "min"), "tips_out": (0.03, "min"), "seam": (1e-5, "max"),
            "free_arm": (-0.03, "max"), "hang_arm": (0.0, "max")}
    if not only or "z_rm_bed_getup" in only:
        G = Getup(B, H)
        W = G.M2W
        boxes = [((MATTRESS[0][0], MATTRESS[0][1], MATTRESS[0][2]), (MATTRESS[1][0], MATTRESS[1][1] - SINK, MATTRESS[1][2]))]
        boxes += [(lo, (hi[0], hi[1] - SINK, hi[2])) for (lo, hi) in PILLOWS]
        res = run(B, "z_rm_bed_getup", GETUP_D, G.frame, False, W, B.mny, boxes,
                  planted={s: (1.95, G.plant[s]) for s in "lr"}, cloud=cloud, bed_from=0.30, gate_from=0.30)
        if res["pre"]:
            print("z_bedup: (info) the 0.00-0.30 s blend out of the %s start: %s" % ("A1" if G.A else "STAND-IN",
                  "  ".join("%s %.3f(%s @%.2fs)" % (k, v[0], v[2], v[1]) for k, v in sorted(res["pre"].items()))))
        seam0 = pose_gap(res["first"], G.A)[0] if G.A else 0.0
        seam1 = max(pose_gap(res["last"], G.E))
        res["worst"]["seam"] = (max(seam0, seam1), 0.0, "start %.1e end %.1e" % (seam0, seam1))
        fails += report(res, tols)
        out.append(res)
        wE = G.end_frames[0]
        feet = G.M2W(lerp(wE[R.leg["l"][2]], wE[R.leg["r"][2]], 0.5))
        pw = G.M2W(wE[R.pel])
        print("z_bedup: GET-UP mark: `mark bed_kaeli_up %.2f 0.00 %.2f 0` (FEET, on the floor under the nestle's hips mark"
              " %.2f,%.2f,%.2f; yaw 0)" % (H[0], H[2], H[0], H[1], H[2]))
        print("z_bedup: STANDING SPOT at the end: pelvis world (%.3f, %.3f, %.3f), feet centre (%.3f, 0.00, %.3f), facing +z (yaw 0)"
              " -- %.3f u along x and %.3f u along z from the bed mark's x/z" % (pw[0], pw[1], pw[2], feet[0], feet[2],
                                                                                 feet[0] - H[0], feet[2] - H[2]))
        if prev:
            render(B, res["keep"], os.path.join(prev, "%s_z_rm_bed_getup.png" % stem),
                   "%s  z_rm_bed_getup (world; mattress + pillows drawn at full height)" % stem, W,
                   [MATTRESS] + PILLOWS)
    if not only or "z_smoke" in only:
        S = Smoke(B, cloud)
        Wm = lambda m: [m[0] * R.scale, (m[1] - B.mny) * R.scale, m[2] * R.scale]
        info = {"t_mouth": None}
        def extra(t, m, worst):
            w, wq = m["w"], m["wq"]
            dm = math.dist(R.grip_point("r", w, wq), B.mouth(w, wq)) * R.scale
            dw = math.dist(w[R.arm["r"][2]], B.mouth(w, wq)) * R.scale
            if info["t_mouth"] is None and dm <= MOUTH_TOL: info["t_mouth"] = t
            fp = S.free_pen(w, wq, "l") * R.scale
            if "free_arm" not in worst or fp > worst["free_arm"][0]: worst["free_arm"] = (fp, t, "free arm vs her skin (<0 = gap)")
            if S.lam(t) > 0.5:
                hp9 = S.free_pen(w, wq, "r") * R.scale
                if "hang_arm" not in worst or hp9 > worst["hang_arm"][0]: worst["hang_arm"] = (hp9, t, "hanging smoking arm vs her skin")
            dfl = math.dist(S.filter_tip(w, wq), B.mouth(w, wq)) * R.scale
            if info.get("t_filter") is None and dfl <= 0.02: info["t_filter"] = t
            if Smoke.T_MOUTH - 1e-9 <= t <= Smoke.T_DRAG_END + 1e-9:
                if "filter" not in worst or dfl > worst["filter"][0]: worst["filter"] = (dfl, t, "filter tip to mouth; knuckle %.3f" % dm)
                fg = S.finger_gap(w, wq)
                if "finger_gap" not in worst or fg < worst["finger_gap"][0]: worst["finger_gap"] = (fg, t, "fingers off her head skin")
                to9 = S.tips_out(w, wq)
                if "tips_out" not in worst or to9 < worst["tips_out"][0]: worst["tips_out"] = (to9, t, "fingertips right of the nose")
        res = run(B, "z_smoke", SMOKE_D, S.frame, True, Wm, B.mny, [],
                  planted={s: (0.0, R.ank0[s]) for s in "lr"}, extra=extra, cloud=cloud)
        res["worst"]["seam"] = (max(pose_gap(res["first"], res["last"])), 0.0, "loop")
        if B.blink and "--no-lids" not in argv:          # type-3 morph tracks: only clipSampleF is known to read them
            n = res["frames"]
            for mi in B.blink:
                res["tracks"][(mi, 3)] = [S.blink(f / FPS) for f in range(n)]
        fails += report(res, tols)
        out.append(res)
        print("z_bedup: SMOKE timing: the FILTER reaches the lips at %.2f s (scored %.2f s, frame %d); the drag holds to %.2f s;"
              " EXHALE at %.2f s (frame %d) -- ppuff=%.2f; loop %.1f s" % (info["t_filter"] if info.get("t_filter") is not None else -1,
              Smoke.T_MOUTH, int(round(Smoke.T_MOUTH * FPS)), Smoke.T_DRAG_END, Smoke.T_EXHALE, int(round(Smoke.T_EXHALE * FPS)),
              Smoke.T_EXHALE, SMOKE_D))
        if prev:
            def mk(w, wq):
                fl, em = S.filter_tip(w, wq), S.ember(w, wq)
                return ([(lerp(fl, em, k / 6.0), (150, 150, 150)) for k in range(1, 6)] +
                        [(B.mouth(w, wq), (240, 160, 0)), (fl, (20, 170, 60)), (em, (230, 40, 20))])
            render(B, res["keep"], os.path.join(prev, "%s_z_smoke.png" % stem),
                   "%s  z_smoke (model space; orange = mouth, green = FILTER tip, red = EMBER, grey = the cigarette)" % stem, Wm, [], mk)
            render_face(B, S, os.path.join(prev, "%s_z_smoke_face.png" % stem), res["worst"])
    if fails:
        print("z_bedup: ⛔ GATES FAILED (%d):\n  %s" % (len(fails), "\n  ".join(fails)))
        if "--force" not in argv:
            print("z_bedup: NOTHING BAKED (--force bakes anyway)"); return 1
    if "--nobake" in argv: return 1 if fails else 0
    for res in out:
        assert not lint_name(res["name"]), "%s trips the miners: %s" % (res["name"], lint_name(res["name"]))
        while drop_clip(pack, res["name"]): pass
    append_clips(pack, [(r["name"], FPS, r["frames"], r["tracks"]) for r in out])
    p9 = Rpack(pack)
    for r in out:
        c = p9.clip(p9.clip_names().index(r["name"]))
        print("z_bedup: baked %-15s %3d f @ %g fps (%.2f s), %d tracks" % (r["name"], c["frames"], c["fps"], (c["frames"] - 1) / c["fps"], c["nTracks"]))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
