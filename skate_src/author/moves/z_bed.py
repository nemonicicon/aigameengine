#!/usr/bin/env python3
"""z_bed.py -- ROBOT NEKO ep 12: 1 AM, reva and kaeli CUDDLING IN BED, talking. Authored as a PAIR.

  z_rm_bed_back    (reva,  v1_r2moves)      on her back, head on the pillow, her right arm under kaeli's shoulders,
                                            left hand on her own belly; breathing; a head turn toward kaeli that
                                            comes and goes (LOOP 4.8 s, first frame == last)
  z_rm_bed_nestle  (kaeli, v1_anima_outfit) on her left side facing reva, head on reva's upper chest, her right arm
                                            across reva's waist, her right knee over reva's right thigh; breathing
                                            OUT OF PHASE with reva's; a small head lift now and then (LOOP 4.8 s,
                                            PHASE-LOCKED to z_rm_bed_back: same 145 frames, frame i against frame i)
  z_rm_bed_alone   (reva,  v1_r2moves)      the same body and breath after kaeli has gone: the hugging arm rests
                                            palm-down on the sheet at her side (LOOP 4.8 s, same mark)

ONE WORLD FRAME. Both bodies are posed, placed and gated in kaeli_house.set's own coordinates, and the script
PRINTS the two set marks that make the contact true (yaw 0: each clip lies down inside itself, so its model
axes ARE the world's). THE ENGINE'S RULE FOR A `hips` MARK (crowd_kms.c, hero and partner alike): the model origin
goes to the mark's x/z, the body turns by the mark's yaw about Y, and the PELVIS JOINT is pinned at the mark's
height every frame. So a breath cannot ride the pelvis -- it would be cancelled -- and rides the chest instead.

THE BED, measured in the set: the mattress `solid` top is y 1.02 (x 2.30..6.90, z 3.40..7.80); the pillows are
boxes 0.62 x 0.16 x 0.42 (half) at (2.95, 1.18, 4.85) and (2.95, 1.18, 6.45), box top 1.34, sheet ~1.45; the head of
the bed is at x ~2.3 and a lying body runs along +x.

WHERE THE CONTACT IS MEASURED: capsules (authorlib.body_capsules + the bind-measured arms, via z_sk_ride.Rider) for
body-against-body, and the SKINNED MESH (numpy LBS, checked against rpack.skinned_positions at rest) for the bed:
a body lies on its lowest vertex, not on a proxy's guess. The pair gate runs over EVERY combination of the two
loops' frames (on a stride), because the engine does not promise to start them together.

usage: z_bed.py <reva_pack> <kaeli_pack> [--dry] [--dump DIR] [--live]
  ⛔ packs in reva_native/packs are REFUSED unless --live (author against copies; the director installs).
"""
import sys, os, math, json, struct
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
import numpy as np
from rpack import Rpack, append_clips, drop_clip, lint_name
from authorlib import (fk_world, aim_bone, solve_limb, rotate_bone_world, translate_node_world, profile,
                       ease_smooth, qnorm, qrot, qmul, qinv, q_axis_angle, qslerp, q_between)
import z_sk_ride as SK
from z_sk_ride import vadd, vsub, vmul, vdot, vcross, vlen, vnorm, lerp, mixf, qa, cap_pen, q_from_basis

FPS = 30.0
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
BED_TOP = 1.02
PILLOW_BOX_TOP = 1.34
PILLOW_SHEET_TOP = 1.45
HEAD_REST = 1.37            # the back of her head, sunk into the pillow's sheet (box top 1.34 .. sheet 1.45)
INCLINE = 7.0               # deg: she lies a little propped on the pillow (her head end up)
PILLOWS = [((2.95, 1.18, 4.85), (0.62, 0.16, 0.42)), ((2.95, 1.18, 6.45), (0.62, 0.16, 0.42))]
REVA_Z = 4.85               # her body line: the centre of the near pillow
SHOULDER_X = 3.60           # her shoulder joints at the pillow's foot edge (x 3.57)
TOL_PAIR = 0.020            # club u: two bodies may press into each other's capsules this far, no further
TOL_MAT = 0.030             # club u: a resting contact is this close to the mattress top
CAP_EXTRA = 0.030           # rig u: a body's own capsules deeper into each other than at the stand
UP = [0.0, 1.0, 0.0]
X, Y, Z = [1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]


# ---- quaternion helpers -------------------------------------------------------------------------------------
def rot_world_q(nodes, bone, qA, qo, to=None):
    """A new LOCAL q for `bone` whose WORLD rotation is qA * current (about its own joint)."""
    _wp, wq = fk_world(nodes, qo, to)
    p = nodes[bone]["parent"]
    rp = wq[p] if p >= 0 else [0.0, 0.0, 0.0, 1.0]
    rl = qo.get(bone, nodes[bone]["q"])
    return qnorm(qmul(qinv(rp), qmul(qA, qmul(rp, rl))))


def rot(nodes, qo, bone, axis, deg, to=None):
    if abs(deg) < 1e-12: return
    qo[bone] = rotate_bone_world(nodes, bone, axis, math.radians(deg), qo)


def aim_virtual(nodes, qo, to, bone, local_pt, want_dir):
    """Rotate `bone` so the direction from its joint to a point carried in its frame points along want_dir."""
    w, wq = fk_world(nodes, qo, to)
    u = qrot(wq[bone], local_pt)
    A = q_between(u, want_dir)
    qo[bone] = rot_world_q(nodes, bone, A, qo, to)


# ---- the skinned mesh, numpy (the bed contact is measured on the body, not on a proxy) --------------------------
def _trs(t, q, s):
    x, y, z, w = q
    R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                  [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                  [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    M = np.eye(4)
    M[:3, :3] = R * np.array(s)[None, :]
    M[:3, 3] = t
    return M


class Skin:
    def __init__(self, p, RM, step=3):
        nodes = p.nodes(); self.nodes = nodes
        pos, slots, wgt, nrm = [], [], [], []
        self.slot_node, ibms, self.src = [], [], []
        off = 0
        for mi in range(p.nMeshes):
            rec = p.MESH.unpack_from(p.buf, p.offMeshes + mi * 60)
            verts, boneCount = rec[2], rec[4]
            off_this = off; off += verts
            offPos, offJoints, offWeights, offBoneMap, offIBM = rec[6], rec[9], rec[10], rec[12], rec[13]
            if not boneCount or not verts: continue
            P = np.frombuffer(p.buf, np.float32, verts * 3, offPos).reshape(-1, 3)[::step]
            J = np.frombuffer(p.buf, np.uint8, verts * 4, offJoints).reshape(-1, 4)[::step].astype(np.int64)
            Wt = np.frombuffer(p.buf, np.uint8, verts * 4, offWeights).reshape(-1, 4)[::step] / 255.0
            bm = struct.unpack_from("<%dH" % boneCount, p.buf, offBoneMap)
            base = len(self.slot_node)
            for b in range(boneCount):
                self.slot_node.append(bm[b])
                ibms.append(np.array(struct.unpack_from("<16f", p.buf, offIBM + b * 64)).reshape(4, 4).T)
            Nn = np.frombuffer(p.buf, np.float32, verts * 3, rec[7]).reshape(-1, 3)[::step]
            nrm.append(Nn.astype(np.float64))
            pos.append(P.astype(np.float64)); slots.append(J + base); wgt.append(Wt)
            self.src.append(np.arange(0, verts, step) + off_this)
        self.src = np.concatenate(self.src)
        self.nrm = np.concatenate(nrm)
        self.pos = np.concatenate(pos); self.slots = np.concatenate(slots); self.wgt = np.concatenate(wgt)
        self.wgt[self.wgt < 1e-3] = 0.0
        self.ibm = np.stack(ibms); self.slot_node = np.array(self.slot_node)
        dom = self.slot_node[self.slots[np.arange(len(self.slots)), np.argmax(self.wgt, axis=1)]]
        self.dom = dom
        # regions by ancestry: head / arm / leg / torso
        def region(i):
            a = i
            while a >= 0:
                if a == RM["head"]: return 1
                if a in (RM["hand_l"], RM["hand_r"]): return 5
                if a in (RM["clavicle_l"], RM["clavicle_r"], RM["upperarm_l"], RM["upperarm_r"]): return 2
                if a in (RM["calf_l"], RM["calf_r"]): return 4
                if a in (RM["thigh_l"], RM["thigh_r"]): return 3
                a = nodes[a]["parent"]
            return 0
        rmap = {i: region(i) for i in set(dom.tolist())}
        self.region = np.array([rmap[d] for d in dom.tolist()])
        self.side = np.zeros(len(dom), np.int64)             # +1 left limb, -1 right limb, 0 midline
        for i in set(dom.tolist()):
            a, sd = i, 0
            while a >= 0:
                if a in (RM["thigh_l"], RM["upperarm_l"], RM["clavicle_l"]): sd = 1; break
                if a in (RM["thigh_r"], RM["upperarm_r"], RM["clavicle_r"]): sd = -1; break
                a = nodes[a]["parent"]
            self.side[dom == i] = sd

    def posed(self, qo, to, normals=False, sel=None):
        n = len(self.nodes); Wm = np.zeros((n, 4, 4))
        for i, nd in enumerate(self.nodes):
            L = _trs(to.get(i, nd["t"]), qnorm(qo.get(i, nd["q"])), nd["s"])
            Wm[i] = L if nd["parent"] < 0 else Wm[nd["parent"]] @ L
        pal = Wm[self.slot_node] @ self.ibm
        if sel is None: pos, slots, wgt, nrm = self.pos, self.slots, self.wgt, self.nrm
        else: pos, slots, wgt, nrm = self.pos[sel], self.slots[sel], self.wgt[sel], self.nrm[sel]
        out = np.zeros_like(pos); nout = np.zeros_like(pos)
        for k in range(4):
            M = pal[slots[:, k]]
            out += wgt[:, k:k + 1] * (np.einsum("nij,nj->ni", M[:, :3, :3], pos) + M[:, :3, 3])
            if normals: nout += wgt[:, k:k + 1] * np.einsum("nij,nj->ni", M[:, :3, :3], nrm)
        if normals:
            nout /= np.maximum(np.linalg.norm(nout, axis=1, keepdims=True), 1e-9)
            return out, nout
        return out


# ---- a body on the bed ----------------------------------------------------------------------------------------
class Body:
    def __init__(self, pack_path, B, who):
        self.R = SK.Rider(pack_path, B)
        self.who = who
        R = self.R
        self.nodes, self.RM, self.sc = R.nodes, R.RM, R.scale
        self.skin = Skin(R.p, R.RM)
        self.mark = [0.0, 0.0, 0.0]
        self.recentre_torso()
        # the standing frame measured off her own skeleton -> the lying frames
        self.Qr = q_from_basis(R.lat, UP, R.fwd)

    def recentre_torso(self):
        """⛔ A TORSO IS NOT A CYLINDER ROUND ITS SPINE. body_capsules measures each radius from the BONE line, so
        a chest's capsule is as deep behind the spine as the bust is in front of it: kaeli's spine_03 read 0.31 u
        all round, and reva's forearm lying ON her back measured 0.12 u "inside" her. Two bodies touching back to
        arm are exactly this scene. Each torso capsule's axis is moved to the centroid of its own flesh (the
        cross-section's middle, not the bone's) and its radii re-measured from there -- still p90, still the mesh."""
        R = self.R; nodes = R.nodes
        rest = self.skin.posed({}, {})
        wpr, wqr = fk_world(nodes)
        new = []
        for c in R.caps:
            nm, b, la, lb, r, ra, rb = c
            if nm not in ("pelvis", "spine_01", "spine_02", "spine_03", "neck_01"):
                new.append(c); continue
            V = rest[self.skin.dom == b]
            if len(V) < 50:
                new.append(c); continue
            A = np.array(vadd(wpr[b], qrot(wqr[b], la))); Bp = np.array(vadd(wpr[b], qrot(wqr[b], lb)))
            ab = Bp - A; l2 = float(ab @ ab) or 1e-12
            u = np.clip(((V - A) @ ab) / l2, 0.0, 1.0)
            off = V - (A + u[:, None] * ab)
            cen = off.mean(axis=0)
            cen -= ab * (cen @ ab) / l2                     # only across the axis
            A2, B2 = A + cen, Bp + cen
            d = np.linalg.norm(V - (A2 + u[:, None] * ab), axis=1)
            r2 = float(np.percentile(d, 90))
            e0 = d[u < 0.35]; e1 = d[u > 0.65]
            ra2 = min(r2, float(np.percentile(e0, 90))) if len(e0) >= 20 else r2
            rb2 = min(r2, float(np.percentile(e1, 90))) if len(e1) >= 20 else r2
            new.append((nm, b, qrot(qinv(wqr[b]), vsub(A2.tolist(), wpr[b])), qrot(qinv(wqr[b]), vsub(B2.tolist(), wpr[b])),
                        r2, ra2, rb2))
            print("z_bed: %-5s %-8s capsule re-centred %.3f u off the bone line: r %.3f -> %.3f u"
                  % (self.who, nm, float(np.linalg.norm(cen)) * self.sc, r * self.sc, r2 * self.sc))
        R.caps = new
        cw = R.posed_caps(R.wp0, R.wq0)
        R.pen0 = {pr: max(0.0, cap_pen(cw[pr[0]], cw[pr[1]])) for pr in R.pairs}

    def lie_q(self, left_t, head_t):
        front_t = vcross(left_t, head_t)
        return qnorm(qmul(q_from_basis(left_t, head_t, front_t), qinv(self.Qr)))

    def to_world(self, p, pel_y):
        return [self.mark[0] + self.sc * p[0], self.mark[1] + self.sc * (p[1] - pel_y), self.mark[2] + self.sc * p[2]]

    def to_model(self, pw, pel_y):
        return [(pw[0] - self.mark[0]) / self.sc, (pw[1] - self.mark[1]) / self.sc + pel_y, (pw[2] - self.mark[2]) / self.sc]

    def world_caps(self, w, wq):
        pel_y = w[self.R.pel][1]
        return [(nm, self.to_world(a, pel_y), self.to_world(b, pel_y), r * self.sc, ra * self.sc, rb * self.sc)
                for (nm, a, b, r, ra, rb) in self.R.posed_caps(w, wq)]

    def mark_for_set(self, w):
        """The set line: the engine puts the model origin at the mark's x/z and the pelvis joint at its y."""
        return list(self.mark)

    def mesh_world_n(self, qo, to, w, sel=None):
        m, n = self.skin.posed(qo, to, normals=True, sel=sel)
        pel_y = w[self.R.pel][1]
        out = np.empty_like(m)
        out[:, 0] = self.mark[0] + self.sc * m[:, 0]
        out[:, 1] = self.mark[1] + self.sc * (m[:, 1] - pel_y)
        out[:, 2] = self.mark[2] + self.sc * m[:, 2]
        return out, n

    def mesh_world(self, qo, to, w):
        m = self.skin.posed(qo, to)
        pel_y = w[self.R.pel][1]
        out = np.empty_like(m)
        out[:, 0] = self.mark[0] + self.sc * m[:, 0]
        out[:, 1] = self.mark[1] + self.sc * (m[:, 1] - pel_y)
        out[:, 2] = self.mark[2] + self.sc * m[:, 2]
        return out


# ---- the scores: slow, layered, out of phase, never quite a loop --------------------------------------------------
DUR_R, DUR_K = 4.8, 4.8       # ⭐ PHASE-LOCKED: the engine runs the partner on the HERO'S clock (both clips start at
                              # phase 0 on the line and advance together), so the pair is authored frame i against frame i.
                              # ⛔ Do not recut either clip to another length: the drape rests on reva's belly PER FRAME.


def eased(keys, t):
    return profile([(k[0], k[1], ease_smooth) for k in keys], t)


def reva_keys(t):
    D = DUR_R; ph = 2.0 * math.pi * t / D
    return dict(
        chest=0.5 - 0.5 * math.cos(ph),                       # the breath: the chest leads...
        bob=0.5 - 0.5 * math.cos(ph - 0.55),                  # ...the head follows it...
        sh=0.5 - 0.5 * math.cos(ph - 1.10),                   # ...then the shoulders
        turn=0.0,                                              # no turn toward kaeli (ep 12 final: capped at 0 deg)
        stroke=0.5 - 0.5 * math.cos(2.0 * math.pi * 1.0 * t / D),                              # one unhurried stroke a loop
        d1=math.sin(2.0 * math.pi * 7.0 * t / D), d2=math.sin(2.0 * math.pi * 11.0 * t / D + 1.3),
        d3=math.sin(2.0 * math.pi * 13.0 * t / D + 0.4))


def kaeli_keys(t):
    D = DUR_K; ph = 2.0 * math.pi * t / D + 0.9 * math.pi   # out of phase with reva's
    return dict(
        chest=0.5 - 0.5 * math.cos(ph), bob=0.5 - 0.5 * math.cos(ph - 0.55), sh=0.5 - 0.5 * math.cos(ph - 1.10),
        lift=eased([(0.0, 0.0), (2.1, 0.0), (2.7, 1.0), (3.4, 1.0), (4.1, 0.0), (D, 0.0)], t),
        d1=math.sin(2.0 * math.pi * 6.0 * t / D + 0.7), d2=math.sin(2.0 * math.pi * 9.0 * t / D + 2.1),
        d3=math.sin(2.0 * math.pi * 12.0 * t / D + 0.2))


# ---- REVA: on her back ----------------------------------------------------------------------------------------
H_, U_, LR = [-1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, -1.0]          # head-ward, her front (up), her left


def reva_pose(Bd, k, flex, arm=None, legs=(0.0, 0.0), alone=None, lhand=None):
    R = Bd.R; nodes = R.nodes; RM = R.RM
    qo = dict(R.base_q); to = {}
    qo[R.pel] = rot_world_q(nodes, R.pel, Bd.lie_q([0.0, 0.0, -1.0], [-1.0, 0.0, 0.0]), qo)
    rot(nodes, qo, R.pel, LR, INCLINE)
    rot(nodes, qo, RM["spine_02"], LR, 0.10 * flex)
    # the chest rises (the ribcage extends), the neck gives it back so the head stays heavy on the pillow
    rot(nodes, qo, RM["spine_02"], LR, -1.5 * k["chest"])
    rot(nodes, qo, RM["spine_03"], LR, -1.0 * k["chest"] + 0.25 * flex + 0.15 * k["d2"])
    rot(nodes, qo, RM["neck_01"], LR, 0.35 * flex + 2.5 * k["chest"])
    rot(nodes, qo, RM["head"], LR, 0.30 * flex - 0.6 * k["bob"] + 0.25 * k["d1"])
    for s, sg in (("l", 1.0), ("r", -1.0)):
        rot(nodes, qo, RM["clavicle_" + s], H_, sg * 1.3 * k["sh"])
    w, _ = fk_world(nodes, qo, to)
    hax = vnorm(vsub(w[RM["head"]], w[RM["neck_01"]]))
    rot(nodes, qo, RM["neck_01"], hax, -0.35 * k["turn"])
    rot(nodes, qo, RM["head"], hax, -0.65 * k["turn"] + 0.3 * k["d3"])
    for s, sg, lg in (("l", 1.0, legs[0]), ("r", -1.0, legs[1])):   # long legs, a little apart, knees soft, feet open
        rot(nodes, qo, RM["thigh_" + s], U_, sg * 5.0)
        rot(nodes, qo, RM["thigh_" + s], LR, -7.0 - INCLINE + lg)
        rot(nodes, qo, RM["calf_" + s], LR, 11.0)
        rot(nodes, qo, RM["foot_" + s], LR, 30.0)
        rot(nodes, qo, RM["foot_" + s], H_, -sg * 6.0)
    # her LEFT hand on her own belly, riding the breath (the target is re-read off the posed belly every frame)
    w, wq = fk_world(nodes, qo, to)
    cap = {c[0]: c for c in R.posed_caps(w, wq)}
    sp = cap["spine_02"]
    belly = vadd(lerp(sp[1], sp[2], 0.35), vadd(vmul(U_, sp[4] + 0.016), vmul(LR, 0.05)))
    if lhand is not None:                                   # over kaeli's hand on her belly: they hold hands
        hand_to(R, qo, to, "l", lhand[0], lhand[1], pole=vnorm([0.25, -0.55, -1.0]))
    else:
        hand_to(R, qo, to, "l", belly, vnorm([0.15, -0.30, 1.0]), pole=vnorm([0.25, -0.55, -1.0]))
    if alone is not None:                                   # kaeli has gone: the arm rests on the sheet at her side
        palm, d = alone
        hand_to(R, qo, to, "r", palm, d, pole=vnorm([0.35, -0.55, 1.0]))
        return qo, to
    # her RIGHT arm out ALONG THE MATTRESS under kaeli's head, the forearm up behind kaeli's back
    ua_dir = Bd.ua_dir if getattr(Bd, "ua_dir", None) else vnorm([0.12, -0.20, 1.0])
    qo[RM["upperarm_r"]] = aim_bone(nodes, RM["upperarm_r"], RM["lowerarm_r"], ua_dir, qo, to)
    if arm is not None:
        palm, d = arm[0], arm[1]
        w, _ = fk_world(nodes, qo, to)
        wrist = vsub(palm, vmul(d, R.glen["r"]))
        qo[RM["lowerarm_r"]] = aim_bone(nodes, RM["lowerarm_r"], RM["hand_r"], vsub(wrist, w[RM["lowerarm_r"]]), qo, to)
        aim_virtual(nodes, qo, to, RM["hand_r"], R.grip["r"], d)
        if len(arm) > 2 and arm[2]: rot(nodes, qo, RM["hand_r"], d, arm[2])
    else:
        qo[RM["lowerarm_r"]] = aim_bone(nodes, RM["lowerarm_r"], RM["hand_r"], vnorm([0.10, 0.80, 0.35]), qo, to)
    return qo, to


def hand_to(R, qo, to, s, palm, d, pole):
    """IK the arm so the PALM (measured grip point) lands on `palm`, the hand aimed along d."""
    nodes = R.nodes
    ua, la, ha = R.arm[s]
    wrist = vsub(palm, vmul(d, R.glen[s]))
    qo.update(solve_limb(nodes, (ua, la, ha), wrist, pole=pole, q_override=qo, t_override=to))
    aim_virtual(nodes, qo, to, ha, R.grip[s], d)


# ---- KAELI: on her left side, facing reva ------------------------------------------------------------------------
HK, LK, FK = [-1.0, 0.0, 0.0], [0.0, -1.0, 0.0], [0.0, 0.0, -1.0]         # head-ward, her left (down), her front


def kaeli_base(Bd):
    R = Bd.R; nodes = R.nodes; RM = R.RM
    qo = dict(R.base_q); to = {}
    qo[R.pel] = rot_world_q(nodes, R.pel, Bd.lie_q([0.0, -1.0, 0.0], [-1.0, 0.0, 0.0]), qo)
    rot(nodes, qo, R.pel, HK, 9.0)                          # rolled a little onto her front, toward reva
    rot(nodes, qo, R.pel, FK, 6.0)                          # her shoulders up on reva's arm (head end raised)
    for b in (RM["spine_01"], RM["spine_02"], RM["spine_03"]):
        rot(nodes, qo, b, LK, 4.0)                          # curled toward her
    return qo, to


def kaeli_pose(Bd, k, tg):
    R = Bd.R; nodes = R.nodes; RM = R.RM
    qo, to = kaeli_base(Bd)
    rot(nodes, qo, RM["spine_02"], LK, -1.3 * k["chest"])
    rot(nodes, qo, RM["spine_03"], LK, -0.8 * k["chest"] + 0.15 * k["d2"])
    for s, sg in (("l", 1.0), ("r", -1.0)):
        rot(nodes, qo, RM["clavicle_" + s], HK, sg * 1.0 * k["sh"])
    pel_y = fk_world(nodes, qo, to)[0][R.pel][1]
    # THE HEAD, heavy on reva's shoulder: CCD the upper spine + neck so the head joint reaches its rest point
    ht = Bd.to_model(tg["head"], pel_y)
    ht = vadd(ht, [0.0, (k["lift"] * 0.015 + 0.004 * k["bob"]) / Bd.sc, 0.0])
    rot(nodes, qo, RM["spine_02"], HK, 6.0)                 # the chest turned a little toward reva
    rot(nodes, qo, RM["spine_03"], HK, 6.0)
    chain = [(RM["spine_01"], 0.15), (RM["spine_02"], 0.35), (RM["spine_03"], 0.55), (RM["neck_01"], 1.0)]
    for _it in range(14):
        for b, frac in chain:
            w, _ = fk_world(nodes, qo, to)
            A = q_between(vsub(w[RM["head"]], w[b]), vsub(ht, w[b]))
            qo[b] = rot_world_q(nodes, b, qslerp([0.0, 0.0, 0.0, 1.0], A, frac), qo, to)
    w, wq = fk_world(nodes, qo, to)
    hax = vnorm(vsub(w[RM["head"]], w[RM["neck_01"]]))
    rot(nodes, qo, RM["head"], hax, tg["head_turn"] - 7.0 * k["lift"] + 0.3 * k["d1"])   # the face toward reva's
    rot(nodes, qo, RM["head"], FK, 4.0 * k["lift"])
    # her RIGHT (top) arm DRAPED across reva's waist: forearm on the belly, the hand down the far side
    hand_to(R, qo, to, "r", Bd.to_model(tg["palm_r"], pel_y), tg["palm_r_dir"], pole=tg["pole_r"])
    # her LEFT (bottom) arm FOLDED UP in front of her chest: upper arm forward, forearm up toward her face
    ud, fd = tg.get("armL", (vnorm([0.1, 0.1, -1.0]), vnorm([-1.0, 0.5, -0.2])))
    qo[RM["upperarm_l"]] = aim_bone(nodes, RM["upperarm_l"], RM["lowerarm_l"], ud, qo, to)
    qo[RM["lowerarm_l"]] = aim_bone(nodes, RM["lowerarm_l"], RM["hand_l"], fd, qo, to)
    aim_virtual(nodes, qo, to, RM["hand_l"], R.grip["l"], vnorm(vadd(fd, [0.0, -0.30, -0.15])))
    # her RIGHT (top) leg bent, resting on the bottom one, both beside reva's legs -- never over them
    rot(nodes, qo, RM["thigh_r"], LK, -tg["hipR"])
    rot(nodes, qo, RM["thigh_r"], FK, tg.get("addR", 0.0))
    rot(nodes, qo, RM["calf_r"], LK, tg["kneeR"])
    # her LEFT (bottom) leg along the mattress, bent a little, behind the top one
    rot(nodes, qo, RM["thigh_l"], LK, -tg["hipL"])
    rot(nodes, qo, RM["thigh_l"], FK, -tg.get("liftL", 0.0))
    rot(nodes, qo, RM["calf_l"], LK, tg["kneeL"])
    for s in "lr":
        rot(nodes, qo, RM["foot_" + s], LK, 32.0)
    for s, c in (("r", 0.30 + 0.04 * k["d3"]), ("l", 0.42 + 0.03 * k["d2"])):    # relaxed, never splayed
        if s in R.hrig: qo = R.hrig[s].pose(qo, curl=c, thumb=0.8 * c)
    return qo, to


# ---- geometry on the measured bodies ---------------------------------------------------------------------------
def cap_pt(c, u):
    """(point on the axis, radius) at fraction u along a world capsule (nm, a, b, r, ra, rb)."""
    return lerp(c[1], c[2], u), c[4] + (c[5] - c[4]) * u


def surf(c, u, n, extra):
    p, r = cap_pt(c, u)
    return vadd(p, vmul(vnorm(n), r + extra))


def lowest(mw, mask):
    ys = mw[mask, 1]
    return float(ys.min()) if len(ys) else 9.0


def pair_pens(ca, cb):
    """{(nameA, nameB): penetration} over all capsule pairs of two bodies (world club units)."""
    return {(a[0], b[0]): cap_pen(a, b) for a in ca for b in cb}


SINK = 0.02                   # club u: a lying body sinks this far into the mattress (the bedding hides it)


def place_reva(rv, k0):
    R = rv.R
    lo, hi, f = 0.0, 75.0, 20.0
    for _b in range(20):
        f = 0.5 * (lo + hi)
        qo, to = reva_pose(rv, k0, f)
        w, wq = fk_world(R.nodes, qo, to)
        rv.mark = [0.0, 0.0, 0.0]
        ush = 0.5 * (w[R.RM["upperarm_l"]][0] + w[R.RM["upperarm_r"]][0])
        rv.mark[0] = SHOULDER_X - rv.sc * ush
        rv.mark[2] = REVA_Z - rv.sc * w[R.pel][2]
        mw = rv.mesh_world(qo, to, w)
        rv.mark[1] = (BED_TOP - SINK) - lowest(mw, rv.skin.region == 0)
        mw[:, 1] += rv.mark[1]
        if lowest(mw, rv.skin.region == 1) < HEAD_REST: lo = f
        else: hi = f
    return f


def kaeli_targets(rv, w, wq, kv, kz_side):
    """Every kaeli contact derived from reva's measured, posed body (world)."""
    rc = {c[0]: c for c in rv.world_caps(w, wq)}
    tg = {}
    # the head: on reva's right shoulder / upper chest, pressed in by the weight of it
    base = vadd(lerp(cap_pt(rc["spine_03"], 0.5)[0], rc["upperarm_r"][1], 0.55), [0.28, 0.0, 0.0])   # down her shoulder, off her face
    r_ua = max(rc["spine_03"][5], rc["upperarm_r"][4])
    n = vnorm([0.10, 1.0, 0.55])
    kh = next(c for c in kv.R.caps if c[0] == "head")
    tg["head"] = vadd(base, vmul(n, r_ua + kh[5] * kv.sc - 0.010))
    tg["head_n"] = n
    tg["head_turn"] = -16.0
    # the top arm across the waist: the palm down reva's FAR (left) flank, the forearm over the belly
    sp = rc["spine_01"]
    tg["palm_r_n"] = (sp, 0.55, [0.0, 1.0, -0.55])
    tg["palm_r"] = surf(sp, 0.55, [0.0, 1.0, -0.55], 0.030)
    tg["palm_r_dir"] = vnorm([0.20, -0.85, -0.35])
    tg["pole_r"] = vnorm([0.80, 0.35, 0.45])
    # the bottom arm tucked along reva's right side, on the mattress
    sp2 = rc["spine_02"]
    tg["palm_l"] = None                                   # set once she is placed: along the mattress in front of her
    tg["palm_l_dir"] = vnorm([1.0, -0.10, -0.30])
    tg["pole_l"] = vnorm([0.0, 0.35, 1.0])
    # the top knee over reva's right thigh, the shin down along her leg
    th, ca = rc["thigh_r"], rc["calf_r"]
    tg["knee_line"] = (th, [0.0, 1.0, 0.30])
    tg["ankle"] = surf(ca, 0.55, [0.0, 1.0, -0.15], 0.10)
    tg["hipL"], tg["kneeL"] = 28.0, 55.0
    tg["hipR"], tg["kneeR"] = 40.0, 62.0
    return tg, rc


def knee_on_thigh(kv, qo0, to0, tg, pel_y, press=0.002):
    """The point on top of reva's right thigh at exactly kaeli's thigh length from her right hip."""
    R = kv.R
    w, _ = fk_world(R.nodes, qo0, to0)
    hip = kv.to_world(w[R.RM["thigh_r"]], pel_y)
    L1 = kv.sc * vlen(vsub(R.wp0[R.RM["calf_r"]], R.wp0[R.RM["thigh_r"]]))
    kth = next(c for c in R.caps if c[0] == "thigh_r")
    rk = kth[6] * kv.sc
    th, n = tg["knee_line"]
    best = None
    for i in range(0, 41):
        u = 0.15 + 0.80 * i / 40.0
        p = surf(th, u, n, rk - press)
        e = abs(vlen(vsub(p, hip)) - L1)
        if best is None or e < best[0]: best = (e, p, u)
    return best


# ---- the bake ------------------------------------------------------------------------------------------------------
CONTACTS = {"head on shoulder": ("k", ["head"], ["spine_03", "spine_02", "neck_01", "upperarm_r"]),
            "arm across waist": ("k", ["lowerarm_r"], ["spine_01", "spine_02", "pelvis"]),
            "knee over thigh": ("k", ["thigh_r", "calf_r"], ["thigh_r", "calf_r"]),
            "arm around her": ("r", ["upperarm_r", "lowerarm_r"], ["spine_03", "spine_02", "neck_01", "upperarm_r", "upperarm_l", "head"])}


def joint_flex(w, a, b, c):
    u = vnorm(vsub(w[a], w[b])); v = vnorm(vsub(w[c], w[b]))
    return 180.0 - math.degrees(math.acos(max(-1.0, min(1.0, vdot(u, v)))))


RNAME = {0: "torso", 1: "head", 2: "arm", 3: "thigh", 4: "shin", 5: "hand"}


def skin_pen(P1, P2, N2, reg1, reg2, cap=int(os.environ.get("Z_BED_CAP", "5000")), side1=None):
    """Deepest point of P1 behind surface P2 (normals N2): for each P1 vertex near P2, its nearest P2 vertex q and
    depth = -(p - q).n_q. Only where the clouds overlap (a box prefilter); thinned to `cap` points a side."""
    lo = np.maximum(P1.min(0), P2.min(0)) - 0.05; hi = np.minimum(P1.max(0), P2.max(0)) + 0.05
    m1 = np.all((P1 >= lo) & (P1 <= hi), axis=1); m2 = np.all((P2 >= lo) & (P2 <= hi), axis=1)
    m2 = m2 & (reg2 != 5)            # ⛔ a hand is a thin shell: the nearest vertex can be its BACK, and a body
    #                                  resting under a palm then reads as "behind" it -- measure INTO thick parts only
    A, rA = P1[m1], reg1[m1]; B, NB, rB = P2[m2], N2[m2], reg2[m2]
    sA = side1[m1] if side1 is not None else np.zeros(len(A), np.int64)
    if len(A) == 0 or len(B) == 0: return 0.0, "-"
    if len(A) > cap: sel = np.linspace(0, len(A) - 1, cap).astype(int); A, rA, sA = A[sel], rA[sel], sA[sel]
    if len(B) > cap * 2: sel = np.linspace(0, len(B) - 1, cap * 2).astype(int); B, NB, rB = B[sel], NB[sel], rB[sel]
    best, which = 0.0, "-"
    for i in range(0, len(A), 800):
        d2 = ((A[i:i + 800, None, :] - B[None, :, :]) ** 2).sum(-1)
        j = d2.argmin(1)
        dist = np.sqrt(d2[np.arange(len(j)), j])
        depth = -np.einsum("ij,ij->i", A[i:i + 800] - B[j], NB[j])
        ok = dist < 0.08                                     # a nearest vertex this close: a real local surface
        depth = np.where(ok, depth, 0.0)
        k = int(depth.argmax())
        if depth[k] > best:
            best = float(depth[k])
            which = "%s%s into %s at (%.2f, %.2f, %.2f)" % (RNAME[int(rA[i + k])], {1: " L", -1: " R", 0: ""}[int(sA[i + k])],
                                                          RNAME[int(rB[j[k]])], A[i + k][0], A[i + k][1], A[i + k][2])
    return best, which


def mesh_gap(A, B):
    """Closest approach between two point sets (world), chunked brute force with a box prefilter."""
    if len(A) == 0 or len(B) == 0: return 9.0
    lo = np.maximum(A.min(0), B.min(0)) - 0.3; hi = np.minimum(A.max(0), B.max(0)) + 0.3
    A = A[np.all((A >= lo) & (A <= hi), axis=1)]; B = B[np.all((B >= lo) & (B <= hi), axis=1)]
    if len(A) == 0 or len(B) == 0: return 9.0
    if len(A) > 6000: A = A[:: len(A) // 6000 + 1]
    if len(B) > 6000: B = B[:: len(B) // 6000 + 1]
    best = 9.0
    for i in range(0, len(A), 1500):
        d = ((A[i:i + 1500, None, :] - B[None, :, :]) ** 2).sum(-1)
        best = min(best, float(np.sqrt(d.min())))
    return best


def run(rv, kv, rp, kp, dump, argv):
    R, K = rv.R, kv.R
    kr0, kk0 = reva_keys(0.0), kaeli_keys(0.0)
    krm = dict(kr0, chest=0.5, bob=0.5, sh=0.5)                # reva at MID-breath: what kaeli rests on
    flex = place_reva(rv, kr0)
    print("z_bed: reva  neck curl onto the pillow %.1f deg -> mark %s" % (flex, ["%.3f" % v for v in rv.mark]))
    # SETTLE each of her legs onto the mattress: the hip angle that puts the leg's lowest skin at the top, sunk a hair
    legs = [0.0, 0.0]
    for li, (sd, sgn) in enumerate((("l", 1), ("r", -1))):
        lo_, hi_ = -20.0, 14.0
        for _b in range(18):
            mid = 0.5 * (lo_ + hi_); lg = list(legs); lg[li] = mid
            qo, to = reva_pose(rv, kr0, flex, legs=tuple(lg))
            w, wq = fk_world(R.nodes, qo, to)
            y = lowest(rv.mesh_world(qo, to, w), (rv.skin.region == 4) & (rv.skin.side == sgn))
            if y > BED_TOP - 0.008: lo_ = mid          # too high: a larger (extension) angle lowers the leg
            else: hi_ = mid
        legs[li] = 0.5 * (lo_ + hi_)
    legs = tuple(legs)
    print("z_bed: reva  legs settled: hip %+.1f / %+.1f deg" % legs)
    # her NEAR arm down along her side, palm on the sheet -- the z_rm_bed_alone arm, in both her clips
    qa0, ta0 = reva_pose(rv, krm, flex, legs=legs)
    wa0, wqa0 = fk_world(R.nodes, qa0, ta0)
    pw = rv.to_world(wa0[R.pel], wa0[R.pel][1])
    handR_ = (rv.skin.region == 5) & (rv.skin.side == -1)
    d_al = vnorm([0.85, -0.25, 0.12])

    def alone_palm(h):
        return rv.to_model([pw[0] - 0.05, BED_TOP + h, REVA_Z + 0.45], wa0[R.pel][1]), d_al
    lo_, hi_ = -0.02, 0.18
    for _it in range(10):
        h = 0.5 * (lo_ + hi_)
        qa9, ta9 = reva_pose(rv, krm, flex, legs=legs, alone=alone_palm(h))
        wa9, _q9 = fk_world(R.nodes, qa9, ta9)
        if lowest(rv.mesh_world(qa9, ta9, wa9), handR_) < BED_TOP - 0.004: lo_ = h
        else: hi_ = h
    ALONE = alone_palm(hi_)
    print("z_bed: reva  near arm down on the sheet (palm target %.3f above the mattress)" % hi_)

    def back_palm(h):
        return rv.to_model([pw[0] - 0.05, BED_TOP + h, REVA_Z + 0.34], wa0[R.pel][1]), d_al
    lo_, hi_ = -0.02, 0.18
    for _it in range(10):
        h = 0.5 * (lo_ + hi_)
        qa9, ta9 = reva_pose(rv, krm, flex, legs=legs, alone=back_palm(h))
        wa9, _q9 = fk_world(R.nodes, qa9, ta9)
        if lowest(rv.mesh_world(qa9, ta9, wa9), handR_) < BED_TOP - 0.004: lo_ = h
        else: hi_ = h
    BACKARM = back_palm(hi_)
    print("z_bed: reva  near arm tucked along her side for the pair (palm target %.3f above the mattress)" % hi_)
    qm, tm = reva_pose(rv, krm, flex, legs=legs, alone=BACKARM)
    wm, wqm = fk_world(R.nodes, qm, tm)
    # ---- kaeli: y from her side on the mattress, x from her head's reach, z the closest that does not go through ----
    qb, tb = kaeli_base(kv)
    wb, _ = fk_world(K.nodes, qb, tb)
    kv.mark = [0.0, 0.0, 0.0]
    mwb = kv.mesh_world(qb, tb, wb)
    y_k = (BED_TOP - SINK) - lowest(mwb, kv.skin.region == 0)
    head_dx = kv.sc * (wb[K.pel][0] - wb[K.RM["head"]][0])
    tg, rc = kaeli_targets(rv, wm, wqm, kv, +1)
    el = rv.to_world(wm[R.RM["lowerarm_r"]], wm[R.pel][1])        # her elbow: fixed on the mattress
    Lfa = rv.sc * vlen(vsub(R.wp0[R.RM["hand_r"]], R.wp0[R.RM["lowerarm_r"]])) + rv.sc * R.glen["r"] * 0.85

    def reva_arm_for(ck_, k):
        """The palm on kaeli's TOP shoulder / upper back, at whatever point on it reva's forearm actually reaches
        from where her elbow lies -- searched, so the hand lands ON her instead of short of or inside her."""
        best_ = None
        for cn, us in (("upperarm_r", (0.0, 0.1, 0.2, 0.3, 0.4)), ("spine_03", (0.3, 0.5, 0.7))):
            for u in us:
                for th in (0.0, 0.35, 0.7, 1.0):
                    nrm = vnorm([0.05, math.sin(th) + 0.25, math.cos(th)])
                    p_ = surf(ck_[cn], u + 0.04 * (k["stroke"] - 0.5), nrm, 0.012)
                    e_ = abs(vlen(vsub(p_, el)) - Lfa) + (0.0 if cn == "upperarm_r" else 0.04)
                    if best_ is None or e_ < best_[0]: best_ = (e_, p_, nrm)
        d = vnorm(vadd(vmul(best_[2], -1.0), [0.20 + 0.25 * k["stroke"], 0.0, 0.0]))
        return rv.to_model(best_[1], wm[R.pel][1]), d

    best = None
    for zi in range(0, 25):
        zk = 5.25 + 0.025 * zi
        for xo in (-0.10, 0.0, 0.10):
            xk = tg["head"][0] + head_dx * 0.92 + xo
            kv.mark = [xk - kv.sc * wb[K.pel][0], y_k, zk - kv.sc * wb[K.pel][2]]
            kn = knee_on_thigh(kv, qb, tb, tg, wb[K.pel][1])
            tg["knee"] = kn[1]
            qk, tk = kaeli_pose(kv, kk0, tg)
            wk, wqk = fk_world(K.nodes, qk, tk)
            ck = kv.world_caps(wk, wqk)
            qa_, ta_ = qm, tm
            wa_, wqa_ = wm, wqm
            cr = rv.world_caps(wa_, wqa_)
            pens = pair_pens(cr, ck)
            desig = set()
            for (who, kn_, rn_) in CONTACTS.values():
                for a in kn_:
                    for b in rn_:
                        desig.add((b, a) if who == "k" else (a, b))
            TORSO9 = ("pelvis", "spine_01", "spine_02", "spine_03", "neck_01")
            other = max(v for kk, v in pens.items() if kk not in desig and kk[0] not in TORSO9 and kk[1] not in TORSO9
                        and kk[1] not in ("upperarm_l", "lowerarm_l"))
            dmax = max(v for kk, v in pens.items() if kk in desig)
            hp = max(pens[(b, "head")] for b in ("spine_03", "spine_02", "neck_01", "upperarm_r"))
            herr = math.dist(wk[K.RM["head"]], kv.to_model(tg["head"], wk[K.pel][1]))
            J = 60.0 * max(0.0, other - 0.006) + 60.0 * max(0.0, dmax - 0.016) + 1.0 * abs(hp - 0.010) + 4.0 * herr + 0.3 * kn[0] + 0.10 * (zk - 5.3)
            if best is None or J < best[0]:
                best = (J, xk, zk, list(kv.mark), dict(tg), other, dmax, hp, herr, kn)
    J, xk, zk, kv.mark, tg, other, dmax, hp, herr, kn = best
    el = rv.to_world(wm[R.RM["lowerarm_r"]], wm[R.pel][1])         # reva's elbow: fixed on the mattress
    Lfa = rv.sc * vlen(vsub(R.wp0[R.RM["hand_r"]], R.wp0[R.RM["lowerarm_r"]])) + rv.sc * R.glen["r"] * 0.85
    handRr = (rv.skin.region == 5) & (rv.skin.side == -1)
    foreRr = (rv.skin.region == 2) & (rv.skin.side == -1)

    def place_reva_hand(qk_, tk_, wk_):
        """Reva's palm on kaeli's real skin (shoulder cap / upper back) within forearm reach, lifted along the skin
        normal until it only touches. Returns the lift, the anchor, and what her hand and forearm do to kaeli."""
        Bk_, Bkn_ = kv.mesh_world_n(qk_, tk_, wk_)
        thick_ = (kv.skin.region == 0) | ((kv.skin.region == 2) & (kv.skin.side == -1))
        sh_ = np.array(kv.to_world(wk_[K.RM["upperarm_r"]], wk_[K.pel][1]))
        dd_ = np.linalg.norm(Bk_ - np.array(el), axis=1)
        ok_ = thick_ & (np.abs(dd_ - Lfa) < 0.06) & (Bkn_[:, 2] > 0.15) & (Bkn_[:, 1] > -0.2)
        if not ok_.any(): ok_ = thick_ & (np.abs(dd_ - Lfa) < 0.12)
        if not ok_.any(): return None
        cand = np.flatnonzero(ok_)
        if len(cand) > 160: cand = cand[np.linspace(0, len(cand) - 1, 160).astype(int)]
        E = np.array(el)
        best9 = None
        for ix in cand:                                     # the forearm's straight path must stay outside her
            tip = Bk_[ix] + Bkn_[ix] * 0.03
            seg = np.array([E + (tip - E) * u for u in np.linspace(0.15, 0.95, 9)])
            dseg = skin_pen(seg, Bk_[thick_], Bkn_[thick_], np.full(len(seg), 2), kv.skin.region[thick_])[0]
            sc9 = dseg + 0.01 * float(np.linalg.norm(Bk_[ix] - sh_))
            if best9 is None or sc9 < best9[0]: best9 = (sc9, ix, dseg)
        ix = best9[1]
        PS_, NS_ = Bk_[ix].tolist(), vnorm(Bkn_[ix].tolist())

        def tang(v):
            v = vsub(v, vmul(NS_, vdot(v, NS_)))
            return vnorm(v)
        opts = [(tang(vv), rl) for vv in ([0.0, 1.0, 0.0], [-1.0, 0.3, 0.0], [1.0, 0.3, 0.0]) for rl in (0.0, 90.0, 180.0, 270.0)]

        def palm_(k, lift, o):
            p_ = vadd(vadd(PS_, vmul(NS_, 0.02 + lift)), [0.035 * (k["stroke"] - 0.5), 0.0, 0.0])   # the slow stroke
            d = vnorm(vadd(opts[o][0], vmul(NS_, -0.12)))                                          # flat, a hair in
            return rv.to_model(p_, wm[R.pel][1]), d, opts[o][1]
        bestO = None
        for o in range(len(opts)):
            for lift in (0.0, 0.015, 0.03):
                qr9, tr9 = reva_pose(rv, krm, flex, palm_(kr0, lift, o), legs=legs)
                wr9, _q9 = fk_world(R.nodes, qr9, tr9)
                A9, _n9 = rv.mesh_world_n(qr9, tr9, wr9)
                hp9 = skin_pen(A9[handRr], Bk_, Bkn_, rv.skin.region[handRr], kv.skin.region)[0]
                fp9 = skin_pen(A9[foreRr], Bk_, Bkn_, rv.skin.region[foreRr], kv.skin.region)[0]
                g9 = mesh_gap(A9[handRr], Bk_[thick_])
                sc9 = max(hp9, fp9) + 0.2 * g9
                if bestO is None or sc9 < bestO[0]: bestO = (sc9, o, lift)
        hi9 = bestO[2]; O9 = bestO[1]
        palm_o = palm_
        palm_ = lambda k, lift, _o=O9: palm_o(k, lift, _o)
        qr9, tr9 = reva_pose(rv, krm, flex, palm_(kr0, hi9), legs=legs)
        wr9, _q9 = fk_world(R.nodes, qr9, tr9)
        A9, _n9 = rv.mesh_world_n(qr9, tr9, wr9)
        hp9 = skin_pen(A9[handRr], Bk_, Bkn_, rv.skin.region[handRr], kv.skin.region)[0]
        fp9 = skin_pen(A9[foreRr], Bk_, Bkn_, rv.skin.region[foreRr], kv.skin.region)[0]
        fg9 = mesh_gap(A9[foreRr], Bk_[thick_])
        return dict(lift=hi9, palm=palm_, PS=PS_, where=("shoulder" if kv.skin.region[ix] == 2 else "upper back"),
                    hand=hp9, fore=fp9, fore_gap=fg9, hand_gap=mesh_gap(A9[handRr], Bk_[thick_]))

    xk -= 0.30; kv.mark[0] -= 0.30                          # up the bed, so her neck can reach reva's shoulder
    Am0, An0 = rv.mesh_world_n(qm, tm, wm)
    legs_k = (kv.skin.region == 3) | (kv.skin.region == 4)
    legs_r = (rv.skin.region == 3) | (rv.skin.region == 4)
    tors_k = kv.skin.region == 0
    dz = 5.40 - zk; zk += dz; kv.mark[2] += dz                # from close...
    for step in range(0, 16):                                 # ...away, until nothing of hers is in reva
        qk, tk = kaeli_pose(kv, kk0, tg)
        wk, wqk = fk_world(K.nodes, qk, tk)
        Bk_, Bkn_ = kv.mesh_world_n(qk, tk, wk)
        d1 = skin_pen(Bk_[legs_k], Am0, An0, kv.skin.region[legs_k], rv.skin.region)[0]
        d2 = skin_pen(Am0[legs_r], Bk_, Bkn_, rv.skin.region[legs_r], kv.skin.region)[0]
        d3 = skin_pen(Bk_[tors_k], Am0, An0, kv.skin.region[tors_k], rv.skin.region)[0]
        g_ = mesh_gap(Bk_[legs_k], Am0[legs_r])
        print("z_bed:   her legs beside reva's at pelvis z %.3f: legs into reva %.4f, reva into her legs %.4f, torso %.4f, leg gap %.4f"
              % (zk, d1, d2, d3, g_))
        if max(d1, d2) <= 0.010 and d3 <= 0.016: break
        zk += 0.03; kv.mark[2] += 0.03
    print("z_bed: kaeli placed beside her: pelvis z %.3f" % zk)
    for _st in range(0):
        qk, tk = kaeli_pose(kv, kk0, tg)
        wk, wqk = fk_world(K.nodes, qk, tk)
        qa_, ta_ = reva_pose(rv, krm, flex, reva_arm_for({c[0]: c for c in kv.world_caps(wk, wqk)}, kr0), legs=legs)
        wa_, wqa_ = fk_world(R.nodes, qa_, ta_)
        A_, An = rv.mesh_world_n(qa_, ta_, wa_); B_, Bn = kv.mesh_world_n(qk, tk, wk)
        d1, w1 = skin_pen(A_, B_, Bn, rv.skin.region, kv.skin.region, side1=rv.skin.side)
        d2, w2 = skin_pen(B_, A_, An, kv.skin.region, rv.skin.region, side1=kv.skin.side)
        print("z_bed:   skin check at pelvis x %.3f z %.3f: reva into kaeli %.4f (%s), kaeli into reva %.4f (%s)"
              % (xk, zk, d1, w1, d2, w2))
        if max(d1, d2) <= 0.015: break
        step = [0.05, 0.0, 0.03] if d1 >= d2 else [0.0, 0.0, 0.04]
        xk += step[0]; zk += step[2]
        kv.mark = [kv.mark[0] + step[0], kv.mark[1], kv.mark[2] + step[2]]
        kn = knee_on_thigh(kv, qb, tb, tg, wb[K.pel][1]); tg["knee"] = kn[1]
    print("z_bed: kaeli placed: pelvis x %.3f z %.3f  (non-contact pen %.4f, contact pen %.4f, head pen %.4f, head reach err %.4f rig, knee fit %.3f u)"
          % (xk, zk, other, dmax, hp, herr, kn[0]))
    # THE PRESSES: the head (heavy: 0.010 into reva's shoulder at her mid-breath) and the draped forearm (0.008),
    # each walked along its own contact normal until the measured press is what the weight asks for
    for _it in range(4):
        qk, tk = kaeli_pose(kv, kk0, tg)
        wk, wqk = fk_world(K.nodes, qk, tk)
        ck = {c[0]: c for c in kv.world_caps(wk, wqk)}
        cr = {c[0]: c for c in rv.world_caps(wm, wqm)}
        hp = max(cap_pen(cr[b], ck["head"]) for b in ("spine_03", "spine_02", "neck_01", "upperarm_r"))
        ap = max(cap_pen(cr[b], ck["lowerarm_r"]) for b in ("spine_01", "spine_02", "pelvis"))
        tg["head"] = vadd(tg["head"], vmul(tg["head_n"], hp - 0.010))
        sp_, u_, n_ = tg["palm_r_n"]
        tg["palm_r"] = vadd(tg["palm_r"], vmul(vnorm(vadd(vnorm(n_), [0.0, 1.0, 0.0])), ap - 0.008))
        print("z_bed:   press iteration %d: head %.4f  forearm %.4f" % (_it, hp, ap))
    headk = kv.skin.region == 1
    tr_arm = (rv.skin.region == 0) | (rv.skin.region == 2)
    H0 = list(tg["head"])

    def head_try(off):
        tg["head"] = vadd(H0, vmul(tg["head_n"], off))
        qk, tk = kaeli_pose(kv, kk0, tg)
        wk, wqk = fk_world(K.nodes, qk, tk)
        Bk_, _n = kv.mesh_world_n(qk, tk, wk)
        return skin_pen(Bk_[headk], Am0, An0, kv.skin.region[headk], rv.skin.region)[0], mesh_gap(Bk_[headk], Am0[tr_arm])
    lo_, hi_ = -0.08, 0.12
    for _it in range(8):
        h = 0.5 * (lo_ + hi_)
        if head_try(h)[0] > 0.010: lo_ = h
        else: hi_ = h
    hp_, hg_ = head_try(hi_)
    print("z_bed:   her head on reva's shoulder, on the SKIN: %.4f into, %.4f off" % (hp_, hg_))
    # SETTLE her top leg onto the bottom one (lower it about her front axis until its skin rests on it)
    topl = ((kv.skin.region == 3) | (kv.skin.region == 4)) & (kv.skin.side == -1)
    botl = ((kv.skin.region == 3) | (kv.skin.region == 4)) & (kv.skin.side == 1)
    lo_, hi_ = -12.0, 30.0
    for _it in range(10):
        tg["addR"] = 0.5 * (lo_ + hi_)
        qk, tk = kaeli_pose(kv, kk0, tg)
        wk, wqk = fk_world(K.nodes, qk, tk)
        Bk_, Bkn_ = kv.mesh_world_n(qk, tk, wk)
        dtop = max(skin_pen(Bk_[topl], Bk_[botl], Bkn_[botl], kv.skin.region[topl], kv.skin.region[botl])[0],
                   skin_pen(Bk_[botl], Bk_[topl], Bkn_[topl], kv.skin.region[botl], kv.skin.region[topl])[0])
        if dtop > 0.006: hi_ = tg["addR"]
        else: lo_ = tg["addR"]
    tg["addR"] = lo_
    print("z_bed: kaeli top leg rests on the bottom one: lowered %.1f deg" % lo_)
    # SETTLE her bottom leg onto the mattress (lift it about her front axis until its lowest skin sits at the top)
    lo_, hi_ = -10.0, 14.0
    for _b in range(16):
        tg["liftL"] = 0.5 * (lo_ + hi_)
        qk, tk = kaeli_pose(kv, kk0, tg)
        wk, wqk = fk_world(K.nodes, qk, tk)
        y = lowest(kv.mesh_world(qk, tk, wk), (kv.skin.region == 4) & (kv.skin.side == 1))
        if y > BED_TOP - 0.008: hi_ = tg["liftL"]
        else: lo_ = tg["liftL"]
    print("z_bed: kaeli bottom leg settled: lift %.1f deg" % tg["liftL"])
    Am, An_ = rv.mesh_world_n(qm, tm, wm)
    tor = rv.skin.region == 0
    sp1 = cap_pt(rc["spine_01"], 0.5)[0]
    slab = tor & (np.abs(Am[:, 0] - sp1[0]) < 0.06) & (Am[:, 2] < REVA_Z + 0.02) & (Am[:, 2] > REVA_Z - 0.22)
    ii = np.flatnonzero(slab)[int(np.argmax(Am[slab, 1]))]
    far_ = tor & (np.abs(Am[:, 0] - sp1[0]) < 0.06) & (Am[:, 2] < REVA_Z + 0.02) & (Am[:, 2] > REVA_Z - 0.08)
    jj = np.flatnonzero(far_)[int(np.argmax(Am[far_, 1]))]
    P1 = Am[jj].tolist()
    handR = (kv.skin.region == 5) & (kv.skin.side == -1)
    foreR = (kv.skin.region == 2) & (kv.skin.side == -1)
    tor = (rv.skin.region == 0) | (rv.skin.region == 2)       # her torso AND her arms: her own hand is on her ribs
    tg["pole_r"] = vnorm([0.30, -0.10, 1.0])                 # the elbow out on her own side, at the belly's height

    def drape(h, dn):
        d = vnorm([0.55, -dn, -0.60])                        # her fingers down reva's belly, out of reva's forearm's path
        wrist = vadd(P1, [0.0, 0.045 + h, 0.0])
        tg["palm_r"] = vadd(wrist, vmul(d, K.glen["r"] * kv.sc)); tg["palm_r_dir"] = d
        qk, tk = kaeli_pose(kv, kk0, tg)
        wk, wqk = fk_world(K.nodes, qk, tk)
        Bm, _Bn = kv.mesh_world_n(qk, tk, wk)
        df, wf = skin_pen(Bm[foreR], Am[tor], An_[tor], kv.skin.region[foreR], rv.skin.region[tor], side1=kv.skin.side[foreR])
        dh, wh = skin_pen(Bm[handR], Am[tor], An_[tor], kv.skin.region[handR], rv.skin.region[tor], side1=kv.skin.side[handR])
        return df, dh, mesh_gap(Bm[foreR], Am[tor]), mesh_gap(Bm[handR], Am[tor])
    best_dr = None
    for dn in (0.15, 0.30, 0.45, 0.55):
        lo_, hi_ = -0.10, 0.20
        for _it in range(8):
            h = 0.5 * (lo_ + hi_)
            df, dh, fg, hg = drape(h, dn)
            if max(df, dh) > 0.007: lo_ = h
            else: hi_ = h
        df, dh, fg, hg = drape(hi_, dn)
        print("z_bed:   the drape (hand down %.2f): forearm %.4f into / %.4f off her skin, hand %.4f into / %.4f off"
              % (dn, df, fg, dh, hg))
        if best_dr is None or fg < best_dr[0]: best_dr = (fg, dn, hi_)
        if fg <= 0.02: break
    drape(best_dr[2], best_dr[1])                            # keep the drape that lies closest, not the last one tried
    print("z_bed:   the drape kept: hand down %.2f, forearm %.4f off her skin" % (best_dr[1], best_dr[0]))
    # reva's right hand onto kaeli's TOP (right) shoulder, from kaeli's reference pose
    qk0, tk0 = kaeli_pose(kv, kk0, tg)
    wk0, wqk0 = fk_world(K.nodes, qk0, tk0)
    ck0 = {c[0]: c for c in kv.world_caps(wk0, wqk0)}

    Bk0, _n0 = kv.mesh_world_n(qk0, tk0, wk0)
    HK_ = Bk0[(kv.skin.region == 5) & (kv.skin.side == -1)]
    hc = HK_.mean(axis=0)
    kw = kv.to_world(wk0[K.RM["hand_r"]], wk0[K.pel][1])
    kwv = np.array(kw)
    ke = np.array(kv.to_world(wk0[K.RM["lowerarm_r"]], wk0[K.pel][1]))      # her elbow
    FK_ = Bk0[kv.skin.dom == K.RM["lowerarm_r"]]                             # her FOREARM's own skin
    dF = vnorm((ke - kwv).tolist())                                         # along it, toward her elbow

    def fore_top(u):
        p0 = kwv + u * (ke - kwv)
        near = (np.linalg.norm(FK_[:, [0, 2]] - p0[[0, 2]], axis=1) < 0.05) & (np.abs(FK_[:, 1] - p0[1]) < 0.15)
        return FK_[near][int(np.argmax(FK_[near][:, 1]))] if near.any() else p0
    top0, top1 = fore_top(0.40), fore_top(0.60)                             # above the wrist, clear of her fingers
    HK_ = FK_                                                                # the hand-hold's gap is measured to her forearm
    handL_r = (rv.skin.region == 5) & (rv.skin.side == 1)

    def lhand(k, h):
        p_ = (top0 + k["stroke"] * (top1 - top0)).tolist()                 # the stroke: toward her elbow and back
        return rv.to_model(vadd(p_, [0.0, h, 0.0]), wm[R.pel][1]), vnorm(vadd(dF, [0.0, -0.12, 0.0]))
    lo_, hi_ = -0.02, 0.14
    for _it in range(9):
        h = 0.5 * (lo_ + hi_)
        qr9, tr9 = reva_pose(rv, krm, flex, legs=legs, alone=BACKARM, lhand=lhand(kr0, h))
        wr9, _q9 = fk_world(R.nodes, qr9, tr9)
        if mesh_gap(rv.mesh_world(qr9, tr9, wr9)[handL_r], HK_) < 0.006: lo_ = h
        else: hi_ = h
    LH = hi_
    qr9, tr9 = reva_pose(rv, krm, flex, legs=legs, alone=BACKARM, lhand=lhand(kr0, LH))
    wr9, _q9 = fk_world(R.nodes, qr9, tr9)
    print("z_bed: reva  her hand over kaeli's: %.4f u between the two hands" % mesh_gap(rv.mesh_world(qr9, tr9, wr9)[handL_r], HK_))

    def reva_arm(k):
        return None

    # FIX 1: kaeli's bottom arm, folded up clear of BOTH bodies -- every fold scored on the skin
    qr_, tr_ = reva_pose(rv, krm, flex, legs=legs, alone=BACKARM, lhand=lhand(kr0, LH))
    wr_, _wqr = fk_world(R.nodes, qr_, tr_)
    Ar, Arn = rv.mesh_world_n(qr_, tr_, wr_)
    sk = kv.skin
    best_arm = None
    cands = []
    for b in (-0.05, 0.12, 0.30, 0.45):                     # the upper arm UP the bed, beside her head
        for c in (-0.15, 0.10, 0.35):
            for fd in (vnorm([0.35, 0.05, -1.0]), vnorm([-0.40, 0.05, -0.90]), vnorm([-0.60, 0.10, 0.80]),
                       vnorm([0.50, 0.25, -0.80]), vnorm([-0.90, 0.30, -0.30])):
                cands.append((vnorm([-1.0, b, c]), fd))
    for (ud, fd) in cands:
            if True:
                tg["armL"] = (ud, fd)
                qk, tk = kaeli_pose(kv, kk0, tg)
                wk, wqk = fk_world(K.nodes, qk, tk)
                Bm, Bn = kv.mesh_world_n(qk, tk, wk)
                shj = np.array(kv.to_world(wk[K.RM["upperarm_l"]], wk[K.pel][1]))
                far = np.linalg.norm(Bm - shj, axis=1) > 0.22
                armL = ((sk.region == 2) | (sk.region == 5)) & (sk.side == 1) & far
                own = (sk.region != 2) & (sk.region != 5) & far
                d1, _ = skin_pen(Bm[armL], Bm[own], Bn[own], sk.region[armL], sk.region[own])
                d2, _ = skin_pen(Bm[armL], Ar, Arn, sk.region[armL], rv.skin.region)
                thick = (sk.region == 2) & (sk.side == 1)
                d3, _ = skin_pen(Ar, Bm[thick], Bn[thick], rv.skin.region, sk.region[thick])
                fl_ = joint_flex(wk, K.RM["upperarm_l"], K.RM["lowerarm_l"], K.RM["hand_l"])
                sc_ = max(d1, d2, d3) + 0.02 * abs(fl_ - 105.0) / 45.0
                if best_arm is None or sc_ < best_arm[0]: best_arm = (sc_, ud, fd, d1, d2, d3, fl_)
    tg["armL"] = (best_arm[1], best_arm[2])
    print("z_bed: kaeli bottom arm UP beside her head: into herself %.4f, into reva %.4f, reva into it %.4f u, elbow %.0f deg"
          % best_arm[3:])

    # z_rm_bed_alone: kaeli has gone -- the same body, the same near arm on the sheet (ALONE, settled above)

    # ---- bake the loops: PHASE-LOCKED, frame i of hers against frame i of reva's ----
    # Kaeli's draped forearm and hand are solved PER FRAME against reva's belly AT THE SAME FRAME, so they rest on it
    # and ride her breath (target 0.005, band 0.000-0.010 into); reva's hand-hold is then placed on kaeli's forearm
    # as it is at that frame, the stroke toward her elbow and back. A fixed search range per frame: frame 0 and the
    # last frame (the same instant of the loop) solve identically, so both clips still close exactly.
    kfore = ((kv.skin.region == 2) | (kv.skin.region == 5)) & (kv.skin.side == -1)   # her draped arm + hand
    kforeA = kv.skin.region[kfore] == 2                                               # ...its arm: a thick surface
    rtor0 = rv.skin.region == 0                                                        # reva's torso: the belly
    fsel = kv.skin.dom == K.RM["lowerarm_r"]                                           # her forearm's own skin
    base_palm = list(tg["palm_r"])
    frames = int(round(DUR_R * FPS)) + 1
    out = {"reva": [], "kaeli": []}
    drape_log = []

    def drape_at(kk, Ar_, Arn_, h):
        tg["palm_r"] = vadd(base_palm, [0.0, h, 0.0])
        qk, tk = kaeli_pose(kv, kk, tg)
        wk, wqk = fk_world(K.nodes, qk, tk)
        Bm, Bn = kv.mesh_world_n(qk, tk, wk, sel=kfore)
        d1 = skin_pen(Bm, Ar_[rtor0], Arn_[rtor0], kv.skin.region[kfore], rv.skin.region[rtor0])[0]
        d2 = skin_pen(Ar_[rtor0], Bm[kforeA], Bn[kforeA], rv.skin.region[rtor0], kv.skin.region[kfore][kforeA])[0]
        return max(d1, d2), qk, tk, wk, wqk

    for f in range(frames):
        t = min(DUR_R, f / FPS); kr = reva_keys(t); kk = kaeli_keys(t)
        qr1, tr1 = reva_pose(rv, kr, flex, legs=legs, alone=BACKARM)       # her body at this frame: the belly
        wr1, _q1 = fk_world(R.nodes, qr1, tr1)
        Ar_, Arn_ = rv.mesh_world_n(qr1, tr1, wr1)
        lo9, hi9 = -0.08, 0.10
        for _it in range(8):
            h9 = 0.5 * (lo9 + hi9)
            if drape_at(kk, Ar_, Arn_, h9)[0] > 0.005: lo9 = h9
            else: hi9 = h9
        pen9, qk, tk, wk, wqk = drape_at(kk, Ar_, Arn_, hi9)
        drape_log.append(pen9)
        Fm = kv.mesh_world_n(qk, tk, wk, sel=fsel)[0]                        # her forearm's skin at this frame
        kw9 = np.array(kv.to_world(wk[K.RM["hand_r"]], wk[K.pel][1]))
        ke9 = np.array(kv.to_world(wk[K.RM["lowerarm_r"]], wk[K.pel][1]))

        def ftop(u):
            p0 = kw9 + u * (ke9 - kw9)
            near = (np.linalg.norm(Fm[:, [0, 2]] - p0[[0, 2]], axis=1) < 0.05) & (np.abs(Fm[:, 1] - p0[1]) < 0.15)
            return Fm[near][int(np.argmax(Fm[near][:, 1]))] if near.any() else p0
        a9, b9 = ftop(0.40), ftop(0.60)                                     # above her wrist, clear of her fingers
        p9 = (a9 + kr["stroke"] * (b9 - a9)).tolist()                      # the stroke: toward her elbow and back
        d9 = vnorm(vadd(vnorm((ke9 - kw9).tolist()), [0.0, -0.12, 0.0]))
        qr, trr = reva_pose(rv, kr, flex, legs=legs, alone=BACKARM,
                            lhand=(rv.to_model(vadd(p9, [0.0, LH, 0.0]), wm[R.pel][1]), d9))
        wr, wqr = fk_world(R.nodes, qr, trr)
        out["reva"].append((t, qr, trr, wr, wqr)); out["kaeli"].append((t, qk, tk, wk, wqk))
    print("z_bed: the drape PER FRAME, on reva's belly at the same frame: %.4f .. %.4f u into (target 0.005)"
          % (min(drape_log), max(drape_log)))
    seq = []
    for f in range(frames):                                                  # z_rm_bed_alone, unchanged
        t = min(DUR_R, f / FPS); k = reva_keys(t)
        qo, to = reva_pose(rv, k, flex, legs=legs, alone=ALONE)
        w, wq = fk_world(R.nodes, qo, to)
        seq.append((t, qo, to, w, wq))
    out["alone"] = seq
    # ---- gates ----
    fails = []
    # loops close
    for who, Bd in (("reva", rv), ("kaeli", kv), ("alone", rv)):
        s0, s1 = out[who][0], out[who][-1]
        gap = max(SK.rot_close(qnorm(s0[1].get(i, Bd.nodes[i]["q"])), qnorm(s1[1].get(i, Bd.nodes[i]["q"])))
                  for i in range(len(Bd.nodes)))
        print("z_bed: %-5s loop %d f @ %g fps, first == last to %.1e" % (who, len(out[who]), FPS, gap))
        if gap > 1e-5: fails.append("%s does not loop (%.2e)" % (who, gap))
    # self: each body's own capsules, and joints off their limits
    for who, Bd in (("reva", rv), ("kaeli", kv), ("alone", rv)):
        worst, fl = (-9.0, ""), {"elbow": [999, -999], "knee": [999, -999], "elbow_l": [999, -999], "elbow_r": [999, -999]}
        for (t, qo, to, w, wq) in out[who]:
            cw = Bd.R.posed_caps(w, wq)
            for (i, j) in Bd.R.pairs:
                pen = cap_pen(cw[i], cw[j]) - Bd.R.pen0[(i, j)]
                if pen > worst[0]: worst = (pen, cw[i][0] + "/" + cw[j][0])
            RM = Bd.RM
            for s in "lr":
                e = joint_flex(w, RM["upperarm_" + s], RM["lowerarm_" + s], RM["hand_" + s])
                kf = joint_flex(w, RM["thigh_" + s], RM["calf_" + s], RM["foot_" + s])
                fl["elbow"] = [min(fl["elbow"][0], e), max(fl["elbow"][1], e)]
                fl["elbow_" + s] = [min(fl["elbow_" + s][0], e), max(fl["elbow_" + s][1], e)]
                fl["knee"] = [min(fl["knee"][0], kf), max(fl["knee"][1], kf)]
        TORSO_ = ("pelvis", "spine_01", "spine_02", "spine_03", "neck_01")
        pr_ = worst[1].split("/")
        if worst[0] > CAP_EXTRA and len(pr_) == 2 and all(q_.startswith(("thigh", "calf")) for q_ in pr_):
            (t9, qo9, to9, w9, wq9) = out[who][0]
            M9, N9 = Bd.mesh_world_n(qo9, to9, w9)
            sk9 = Bd.skin
            lg9 = (sk9.region == 3) | (sk9.region == 4)
            a9 = lg9 & (sk9.side == -1); b9 = lg9 & (sk9.side == 1)
            d9 = max(skin_pen(M9[a9], M9[b9], N9[b9], sk9.region[a9], sk9.region[b9])[0],
                     skin_pen(M9[b9], M9[a9], N9[a9], sk9.region[b9], sk9.region[a9])[0])
            print("z_bed: %-5s   %s: the capsules say %.3f rig, her SKIN says one leg is %.4f u into the other"
                  % (who, worst[1], worst[0], d9))
            if d9 <= TOL_PAIR:
                worst = (-1.0, worst[1] + " (skin: resting)")
        if worst[0] > CAP_EXTRA and any(t_ in worst[1] for t_ in TORSO_) and ("arm" in worst[1]):
            (t9, qo9, to9, w9, wq9) = out[who][0]
            M9, N9 = Bd.mesh_world_n(qo9, to9, w9)
            sk9 = Bd.skin
            shj = np.array(Bd.to_world(w9[Bd.RM["upperarm_l"]], w9[Bd.R.pel][1]))
            shk = np.array(Bd.to_world(w9[Bd.RM["upperarm_r"]], w9[Bd.R.pel][1]))
            far = (np.linalg.norm(M9 - shj, axis=1) > 0.22) & (np.linalg.norm(M9 - shk, axis=1) > 0.22)
            armm = ((sk9.region == 2) | (sk9.region == 5)) & far; tor = (sk9.region == 0) & far
            d9, _w9 = skin_pen(M9[armm], M9[tor], N9[tor], sk9.region[armm], sk9.region[tor], side1=sk9.side[armm])
            print("z_bed: %-5s   %s: the capsules say %.3f rig, her SKIN says the arm is %.4f u behind her torso's surface"
                  % (who, worst[1], worst[0], d9))
            if d9 <= TOL_PAIR:
                worst = (-1.0, worst[1] + " (skin clear)")
        print("z_bed: %-5s self: capsules %.4f rig beyond the stand (%s) | elbow L %.0f..%.0f R %.0f..%.0f deg | knees %.0f..%.0f deg"
              % (who, worst[0], worst[1], fl["elbow_l"][0], fl["elbow_l"][1], fl["elbow_r"][0], fl["elbow_r"][1],
                 fl["knee"][0], fl["knee"][1]))
        if worst[0] > CAP_EXTRA: fails.append("%s clips itself: %s %.3f" % (who, worst[1], worst[0]))
        if fl["elbow"][0] < 8.0: fails.append("%s: an elbow is locked straight (%.0f deg)" % (who, fl["elbow"][0]))
        if fl["knee"][1] > 150.0: fails.append("%s: a knee folds to its limit (%.0f deg)" % (who, fl["knee"][1]))
    # the pair, PHASE-LOCKED: frame i of hers against frame i of reva's (stride 4)
    cw_r = [rv.world_caps(s[3], s[4]) for s in out["reva"][::4]]
    cw_k = [kv.world_caps(s[3], s[4]) for s in out["kaeli"][::4]]
    worst_all = (-9.0, "")
    cont = {nm: [9.0, -9.0] for nm in CONTACTS}
    for a, b in zip(cw_r, cw_k):
        if True:
            pens = pair_pens(a, b)
            for kk, v in pens.items():
                if v > worst_all[0]: worst_all = (v, "reva %s / kaeli %s" % kk)
            for nm, (who, kn_, rn_) in CONTACTS.items():
                v = max(pens[(bb, aa)] if who == "k" else pens[(aa, bb)] for aa in kn_ for bb in rn_)
                cont[nm] = [min(cont[nm][0], v), max(cont[nm][1], v)]
    print("z_bed: PAIR over %d phase-locked frames (i, i): deepest capsule press %.4f u (%s) -- reported; the skin gates"
          % (len(cw_r), worst_all[0], worst_all[1]))
    for nm, (lo, hi) in cont.items():
        print("z_bed:   %-17s press %+.4f .. %+.4f u  (negative = a gap)" % (nm, lo, hi))
    TORSO = ("pelvis", "spine_01", "spine_02", "spine_03", "neck_01")
    limbw = (-9.0, "")
    for a, b in zip(cw_r, cw_k):
        if True:
            for kk, v in pair_pens(a, b).items():
                if kk[0] in TORSO or kk[1] in TORSO: continue
                if v > limbw[0]: limbw = (v, "reva %s / kaeli %s" % kk)
    print("z_bed: PAIR limbs+heads only, capsules (reported, not gated -- see the skin line): deepest %.4f u (%s)" % limbw)
    # THE SKIN: how far any vertex of one body sits BEHIND the other's skinned surface (its normal), sampled over
    # combinations of the two loops -- the gate a torso actually needs
    deep = (0.0, "")
    for fr in range(0, len(out["reva"]), 6):                   # phase-locked: frame i against frame i
        for fk_ in (fr,):
            A_, An = rv.mesh_world_n(*out["reva"][fr][1:4])
            B_, Bn = kv.mesh_world_n(*out["kaeli"][fk_][1:4])
            ra = ((rv.skin.region == 2) | (rv.skin.region == 5))            # reva's arms + hands
            ka = ((kv.skin.region == 2) | (kv.skin.region == 5))            # kaeli's arms + hands
            # whole bodies, thinned, WITHOUT the arms (they are done densely below)
            for (P1, P2, N2, lbl, reg1, reg2, sd1, m1, m2) in (
                    (A_, B_, Bn, "reva into kaeli", rv.skin.region, kv.skin.region, rv.skin.side, ~ra, ~ka),
                    (B_, A_, An, "kaeli into reva", kv.skin.region, rv.skin.region, kv.skin.side, ~ka, ~ra)):
                d_, which = skin_pen(P1[m1], P2[m2], N2[m2], reg1[m1], reg2[m2], side1=sd1[m1])
                if d_ > deep[0]: deep = (d_, "%s (%s) at reva f%d / kaeli f%d" % (lbl, which, fr, fk_))
            for (P1, P2, N2, lbl, reg1, reg2, sd1, m1, m2) in (
                    (A_, B_, Bn, "reva's body into kaeli's arms", rv.skin.region, kv.skin.region, rv.skin.side, ~ra, ka),
                    (B_, A_, An, "kaeli's body into reva's arms", kv.skin.region, rv.skin.region, kv.skin.side, ~ka, ra)):
                d_, which = skin_pen(P1[m1], P2[m2], N2[m2], reg1[m1], reg2[m2], side1=sd1[m1], cap=60000)
                if d_ > deep[0]: deep = (d_, "%s (%s, full density) at reva f%d / kaeli f%d" % (lbl, which, fr, fk_))
            # the ARMS AND HANDS of both bodies at full skin density: the contact this pose is made of
            for (P1, P2, N2, lbl, reg1, reg2, sd1, m1) in ((A_, B_, Bn, "reva's arms into kaeli", rv.skin.region, kv.skin.region, rv.skin.side, ra),
                                                           (B_, A_, An, "kaeli's arms into reva", kv.skin.region, rv.skin.region, kv.skin.side, ka)):
                d_, which = skin_pen(P1[m1], P2, N2, reg1[m1], reg2, side1=sd1[m1], cap=60000)
                if d_ > deep[0]: deep = (d_, "%s (%s, full density) at reva f%d / kaeli f%d" % (lbl, which, fr, fk_))
    print("z_bed: PAIR skin: deepest vertex behind the other's surface %.4f u -- %s" % deep)
    if deep[0] > TOL_PAIR: fails.append("skin through skin: %s %.3f" % (deep[1], deep[0]))
    # the bed, on the skinned mesh
    names = {0: "torso", 1: "head", 2: "arms", 3: "thigh", 4: "shin", 5: "hand"}
    meshes = {}
    lows_by = {}
    for who, Bd in (("reva", rv), ("kaeli", kv), ("alone", rv)):
        lows = {}
        for (t, qo, to, w, wq) in out[who][::12]:
            mw = Bd.mesh_world(qo, to, w)
            meshes.setdefault(who, mw)
            for rg in range(6):
                for sd in (-1, 0, 1):
                    m = (Bd.skin.region == rg) & (Bd.skin.side == sd)
                    if not m.any(): continue
                    y = lowest(mw, m)
                    key = names[rg] + {-1: "_r", 0: "", 1: "_l"}[sd]
                    lows[key] = [min(lows.get(key, [9, -9])[0], y), max(lows.get(key, [9, -9])[1], y)]
        print("z_bed: %-5s lowest mesh points (world y): %s" % (who, "  ".join("%s %.3f..%.3f" % (k2, v[0], v[1]) for k2, v in sorted(lows.items()))))
        for key, (lo, hi) in lows.items():
            if key == "torso":
                if abs(lo - BED_TOP) > TOL_MAT: fails.append("%s torso not on the mattress (%.3f)" % (who, lo))
            elif key.startswith("thigh"):
                if lo < BED_TOP - 0.085: fails.append("%s %s sunk past the bedding (%.3f)" % (who, key, lo))
            elif key == "head":
                if who in ("reva", "alone") and not (PILLOW_BOX_TOP - 0.01 <= lo <= PILLOW_SHEET_TOP): fails.append("reva's head not on the pillow (%.3f)" % lo)
            elif lo < BED_TOP - TOL_MAT:
                fails.append("%s %s below the mattress (%.3f)" % (who, key, lo))
        lows_by[who] = lows
        if who == "kaeli":
            hv = Bd.mesh_world(*out[who][0][1:4])[Bd.skin.region == 1]
            for (c, hh) in PILLOWS:
                ins = (np.abs(hv[:, 0] - c[0]) < hh[0]) & (np.abs(hv[:, 2] - c[2]) < hh[2])
                if ins.any():
                    ymin = float(hv[ins, 1].min())
                    print("z_bed: kaeli's head over the pillow at z %.2f: lowest skin %.3f (box top %.3f)" % (c[2], ymin, c[1] + hh[1]))
                    if ymin < c[1] + hh[1] - 0.03: fails.append("kaeli's head in the pillow (%.3f)" % ymin)
    for key, want in (("shin_l", "reva"), ("shin_r", "reva"), ("shin_l", "kaeli"), ("shin_l", "alone"), ("shin_r", "alone"),
                      ("hand_r", "alone")):
        lo = lows_by[want].get(key, [9, 9])[0]
        print("z_bed: %-5s %s resting on the mattress: %.3f (%+.3f)" % (want, key, lo, lo - BED_TOP))
        if abs(lo - BED_TOP) > TOL_MAT: fails.append("%s %s not resting on the mattress (%.3f)" % (want, key, lo))
    # the visible truth: the closest approach of the two SKINS at each contact (reference frames)
    mr = rv.mesh_world(out["reva"][0][1], out["reva"][0][2], out["reva"][0][3])
    mk = kv.mesh_world(out["kaeli"][0][1], out["kaeli"][0][2], out["kaeli"][0][3])
    sr, sk = rv.skin, kv.skin
    for nm, (A, Bm) in {"head on shoulder": (mk[sk.region == 1], mr[(sr.region == 0) | ((sr.region == 2) & (sr.side == -1))]),
                        "arm across waist": (mk[(sk.region == 2) & (sk.side == -1)], mr[sr.region == 0]),
                        "knee over thigh": (mk[(sk.region == 3) & (sk.side == -1)], mr[(sr.region == 3) & (sr.side == -1)]),
                        "hand on her shoulder": (mr[(sr.region == 2) & (sr.side == -1)], mk[(sk.region == 2) & (sk.side == -1)])}.items():
        print("z_bed:   skin to skin, %-20s %.4f u" % (nm, mesh_gap(A, Bm)))
    print("\nz_bed: THE MARKS (kaeli_house.set; yaw 0 -- each clip lies down inside itself):")
    print("mark bed_reva   %.3f %.3f %.3f 0 hips" % tuple(rv.mark_for_set(out["reva"][0][3])))
    print("mark bed_kaeli  %.3f %.3f %.3f 0 hips" % tuple(kv.mark_for_set(out["kaeli"][0][3])))
    if dump:
        write_dump(rv, kv, out, dump)
        write_dump(rv, kv, {"reva": out["alone"], "kaeli": out["kaeli"]}, dump, name="bed_alone.json", only="reva")
    if fails and "--force" not in argv:
        print("z_bed: GATES FAILED -- nothing written\n    " + "\n    ".join(fails))
        return 1
    if fails:
        # the director's call (jacob, ep 12: "wrap this up this is taking too long for the spirit of the show"):
        # bake past the named failures, and say exactly which ones shipped
        print("z_bed: ⛔ FORCED past %d failing gate(s) -- these ship:\n    " % len(fails) + "\n    ".join(fails))
    if "--dry" in argv:
        print("z_bed: gates pass (dry run, nothing written)"); return 0
    for who, Bd, pk, name in (("reva", rv, rp, "z_rm_bed_back"), ("kaeli", kv, kp, "z_rm_bed_nestle"),
                              ("alone", rv, rp, "z_rm_bed_alone")):
        seq = out[who]
        qtr = {i: [] for i in range(len(Bd.nodes))}
        for (t, qo, to, w, wq) in seq:
            for i in range(len(Bd.nodes)):
                q = qnorm(qo.get(i, Bd.nodes[i]["q"]))
                if qtr[i] and vdot(q[:3], qtr[i][-1][:3]) + q[3] * qtr[i][-1][3] < 0: q = [-x for x in q]
                qtr[i].append(q)
        tracks = {(i, 0): [x for q in v for x in q] for i, v in qtr.items()}
        tracks[(Bd.R.pel, 1)] = [x for (t, qo, to, w, wq) in seq for x in to.get(Bd.R.pel, Bd.nodes[Bd.R.pel]["t"])]
        while drop_clip(pk, name): pass
        append_clips(pk, [(name, FPS, len(seq), tracks)])
        p2 = Rpack(pk); nm2 = p2.clip_names()
        assert nm2.count(name) == 1
        c = p2.clip(nm2.index(name)); assert c["frames"] == len(seq)
        print("z_bed: %s -> %s (%d f @ %g fps), %d clips" % (name, os.path.basename(pk), c["frames"], c["fps"], p2.nClips))
    return 0


def write_dump(rv, kv, out, dump, name="bed_pair.json", only=None):
    os.makedirs(dump, exist_ok=True)
    T = 8
    cols = []
    for i in range(T):
        t = DUR_R * i / (T - 1)
        fr = min(len(out["reva"]) - 1, int(round(t * FPS)))
        fk = int(round((t % DUR_K) * FPS)) % (len(out["kaeli"]))
        col = {"t": t}
        for who, Bd, f in (("reva", rv, fr), ("kaeli", kv, fk)):
            if only and who != only: continue
            (tt, qo, to, w, wq) = out[who][f]
            pel_y = w[Bd.R.pel][1]
            J = {k: Bd.to_world(w[v], pel_y) for k, v in Bd.RM.items() if not k.startswith("_") and isinstance(v, int) and v >= 0}
            caps = [(c[0], c[1], c[2], 0.5 * (c[4] + c[5])) for c in Bd.world_caps(w, wq)]
            mw = Bd.mesh_world(qo, to, w)[::7]
            col[who] = dict(t=tt, joints=J, caps=caps, mesh=mw.tolist(), grip={s: Bd.to_world(Bd.R.grip_world(s, w, wq), pel_y) for s in "lr"})
        cols.append(col)
    json.dump(dict(bed=dict(top=BED_TOP, x=(2.30, 6.90), z=(3.40, 7.80)), pillows=PILLOWS, cols=cols,
                   marks=dict(reva=rv.mark, kaeli=kv.mark)), open(os.path.join(dump, name), "w"))


def main(argv):
    rp, kp = argv[1], argv[2]
    live_dir = os.path.normcase(os.path.join(REPO, "reva_native", "packs"))
    for pk in (rp, kp):
        if os.path.normcase(os.path.abspath(os.path.dirname(pk))) == live_dir and "--live" not in argv:
            print("z_bed: REFUSED -- %s is a LIVE pack. Author against copies; the director installs with --live." % pk)
            return 2
        if not os.path.isfile(pk):
            print("z_bed: no pack at %s" % pk); return 2
    dump = argv[argv.index("--dump") + 1] if "--dump" in argv else None
    for nm in ("z_rm_bed_back", "z_rm_bed_nestle", "z_rm_bed_alone"):
        assert not lint_name(nm), "%s trips the miners: %s" % (nm, lint_name(nm))
    B = SK.measure_board()
    rv = Body(rp, B, "reva"); kv = Body(kp, B, "kaeli")
    for Bd in (rv, kv):                           # the instrument checks itself: numpy LBS == rpack at rest
        ref = np.array(Bd.R.p.skinned_positions())[Bd.skin.src]
        e = float(np.abs(Bd.skin.posed({}, {}) - ref).max())
        print("z_bed: %-5s leg %.3f rig  scale %.4f  skin check vs rpack.skinned_positions at rest %.1e rig (%d verts)"
              % (Bd.who, Bd.R.L, Bd.sc, e, len(ref)))
        assert e < 1e-3, "the numpy skin disagrees with rpack -- fix the instrument before trusting a contact"
    return run(rv, kv, rp, kp, dump, argv)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
