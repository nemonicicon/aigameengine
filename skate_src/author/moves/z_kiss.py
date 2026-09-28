#!/usr/bin/env python3
"""z_kiss.py -- THE KISS. The tightest shared contact yet: two LIP POINTS meeting at
one world point, with the noses passing on complementary head tilts.

jacob: "let see reva and kaeli kissing"

What is new here, on top of the hug's machinery:

THE FACE IS MEASURED. authorlib.face_points reads mouth/nose/forehead from the mesh
verts weighted to the head family, anchored to the nose bump of the front profile
(hair owns the crown on both club faces; no hairstyle owns the nose). The lip then
rides the head bone rigidly: its rest offset is taken into head-local space once and
carried through every posed frame.

THE MEETING HEIGHT IS NEGOTIATED, NOT ASSUMED. reva2's mouth stands 0.43u above
kaeli's. Each body solves lean L, neck pitch P, neck yaw and a HEIGHT MECHANISM H
(positive = rise onto the toes, z_tiptoe's solved mechanism; negative = a knee dip)
to bring its lip to the shared point (0, meetY, PGAP/2). meetY itself is found by
solving BOTH bodies over a center-out ladder and keeping the first height both can
reach -- the score opens the partner pack and runs the identical solve on her rig, so
the two bakes (this pack, then hers with the args mirrored) agree by construction.
The short partner comes up on her toes, which is what short partners do.

EVERY PAIR GATE MEASURES BOTH SOLVED BODIES: her hold pose is FK'd here, mirrored
into my frame ((x,y,z) -> (-x, y, PGAP-z), her own ground datum), and lip-to-lip,
nose clearance, forehead clearance, pairwise chest clearance and arm penetration are
all real two-body numbers, worst-in-hold.

The hands go where kiss hands go: my right to her left waist side, my left up to her
right shoulder side -- mirror symmetry interlocks them (her right lands on my left
waist, no clash), landing spots solved on her measured HOLD-pose capsules.

usage: z_kiss.py <pack> --scale S --arm A --pgap D --ppack P --pscale PS
    (same contract as z_hug: S/A this body, D the pair gap, P/PS the partner)
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips
from authorlib import (rig_map, fk_world, solve_limb, rotate_bone_world, profile,
                       ease_smooth, ease_anticipate, qnorm, declare, face_points,
                       body_dims, body_capsules, translate_node_world,
                       hold_world_rotation, qslerp, qmul, qrot, qinv, q_between)

FPS = 30.0
T_MEET, T_HOLD_END, T_END = 1.9, 3.5, 4.7
TILT = 22.0        # head roll to the body's own right; complementary by mirror symmetry

def _n(v):
    l = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / l for x in v]

class Body:
    """One rig with everything the kiss needs measured: base pose, datum, face,
    dims, capsules, and the machinery to pose it (lean/tilt/pitch/yaw + H)."""
    def __init__(self, path, scale):
        self.p = Rpack(path)
        self.S = scale
        self.RM = rig_map(self.p)
        assert not self.RM["_missing"], "%s rig incomplete: %s" % (path, self.RM["_missing"])
        self.nodes = self.p.nodes()
        names_c = self.p.clip_names()
        assert "z_upright" in names_c, "%s has no z_upright stand" % path
        self.base_q, self.base_t, self.base = {}, {}, {}
        ci = names_c.index("z_upright")
        for (node, typ, data) in self.p.tracks(ci):
            comp = 4 if typ == 0 else (1 if typ == 3 else 3)
            if typ >= 3: continue
            v = list(struct.unpack_from("<%df" % comp, data, 0))
            self.base[(node, typ)] = v
            if typ == 0: self.base_q[node] = v
            elif typ == 1: self.base_t[node] = v
        wr, wqr = fk_world(self.nodes)
        bones = [i for i in range(len(self.nodes)) if self.nodes[i].get("kind") == 1] \
                or list(range(len(self.nodes)))
        self.mny = min(wr[i][1] for i in bones)
        self.F = face_points(self.p, self.RM)
        self.dims = body_dims(self.p, self.RM)
        self.caps = body_capsules(self.p, self.RM)
        # face landmarks as HEAD-LOCAL offsets, rigid with the skull. Cumulative rest
        # scale at the head is honored both directions (kaeli's rig is not unit-1).
        hb = self.RM["head"]
        ws = [1.0, 1.0, 1.0]
        a = hb
        while a >= 0:
            s = self.nodes[a]["s"]
            ws = [ws[k] * s[k] for k in range(3)]
            a = self.nodes[a]["parent"]
        self.wsH = ws
        self.locF = {}
        for nm in ("mouth", "nose", "forehead"):
            d = [self.F[nm][k] - wr[hb][k] for k in range(3)]
            l = qrot(qinv(wqr[hb]), d)
            self.locF[nm] = [l[k] / (ws[k] or 1e-9) for k in range(3)]
        self.faceFwdLoc = qrot(qinv(wqr[hb]), [0.0, 0.0, 1.0])
        # tiptoe sign audit (z_tiptoe's law: probe the BALL, not the ankle)
        self.lat = [1.0, 0.0, 0.0]
        self.fwd = [0.0, 0.0, 1.0]
        wp0, _ = fk_world(self.nodes, self.base_q, self.base_t)
        self.wp0 = wp0
        self.b0 = {b: list(wp0[b]) for b in (self.RM["ball_l"], self.RM["ball_r"])}
        def probe(sgn):
            qo = dict(self.base_q)
            for b in (self.RM["foot_l"], self.RM["foot_r"]):
                qo[b] = rotate_bone_world(self.nodes, b, self.lat, sgn * math.radians(20), qo)
            w, _ = fk_world(self.nodes, qo, self.base_t)
            return self.b0[self.RM["ball_l"]][1] - w[self.RM["ball_l"]][1]
        self.tipSGN = 1.0 if probe(1.0) > probe(-1.0) else -1.0
        # knee-dip sign audits: the thigh sign that carries the KNEE forward, and the
        # calf sign that pulls the ANKLE backward -- knees bend one way only, and the
        # signs are per-rig facts, never assumptions
        def tprobe(sgn):
            qo = dict(self.base_q)
            qo[self.RM["thigh_l"]] = rotate_bone_world(self.nodes, self.RM["thigh_l"],
                                                       self.lat, sgn * math.radians(20), qo)
            w, _ = fk_world(self.nodes, qo, self.base_t)
            return w[self.RM["calf_l"]][2] - self.wp0[self.RM["calf_l"]][2]
        self.thSGN = 1.0 if tprobe(1.0) > tprobe(-1.0) else -1.0
        def kprobe(sgn):
            qo = dict(self.base_q)
            qo[self.RM["calf_l"]] = rotate_bone_world(self.nodes, self.RM["calf_l"],
                                                      self.lat, sgn * math.radians(24), qo)
            w, _ = fk_world(self.nodes, qo, self.base_t)
            return self.wp0[self.RM["foot_l"]][2] - w[self.RM["foot_l"]][2]
        self.dipSGN = 1.0 if kprobe(1.0) > kprobe(-1.0) else -1.0
        self._mechC = {}

    def gw(self, pt):
        """rig point -> grounded, engine-scaled world (this body's own frame)"""
        return [pt[0] * self.S, (pt[1] - self.mny) * self.S, pt[2] * self.S]

    def mech(self, H):
        """the HEIGHT MECHANISM at world offset H: overrides that rise on the toes
        (H>0, balls held to the floor, pelvis lifted by the measured ball drop) or dip
        at the knees (H<0, ankles re-planted, pelvis lowered). Solved, cached."""
        key = round(H, 4)
        if key in self._mechC: return self._mechC[key]
        qo, to = {}, dict(self.base_t)
        RM = self.RM
        if abs(H) < 1e-4:
            self._mechC[key] = (qo, to); return (qo, to)
        if H > 0:
            lo, hi = 0.0, math.radians(46.0)
            for _ in range(14):
                mid = 0.5 * (lo + hi)
                q2 = dict(self.base_q)
                for b in (RM["foot_l"], RM["foot_r"]):
                    q2[b] = rotate_bone_world(self.nodes, b, self.lat, self.tipSGN * mid, q2)
                w, _ = fk_world(self.nodes, q2, self.base_t)
                drop = sum(self.b0[b][1] - w[b][1] for b in self.b0) / 2.0
                if drop * self.S < H: lo = mid
                else: hi = mid
            rho = 0.5 * (lo + hi)
            q2 = dict(self.base_q)
            for b in (RM["foot_l"], RM["foot_r"]):
                q2[b] = rotate_bone_world(self.nodes, b, self.lat, self.tipSGN * rho, q2)
            w, _ = fk_world(self.nodes, q2, self.base_t)
            drop = sum(self.b0[b][1] - w[b][1] for b in self.b0) / 2.0
            to[RM["pelvis"]] = translate_node_world(self.nodes, RM["pelvis"],
                                                    [0.0, drop, 0.0],
                                                    self.base_q, self.base_t)
            for b in (RM["foot_l"], RM["foot_r"]):
                qo[b] = q2[b]
            # toes stay world-flat under the rotated feet
            _, wq0 = fk_world(self.nodes, self.base_q, self.base_t)
            for b in (RM["ball_l"], RM["ball_r"]):
                qo[b] = hold_world_rotation(self.nodes, b, wq0[b],
                                            {**self.base_q, **qo}, to)
        else:
            # a knee dip: thigh carries the knee forward (audited sign), calf folds it
            # back (audited sign), the FOOT is held to its base world orientation
            # exactly (the tiptoe-ball trick -- no sign algebra can drift). Folding
            # under a fixed pelvis lifts the feet; planting them back lowers the
            # pelvis by that lift, so the drop is solved, not tuned.
            _, wq0 = fk_world(self.nodes, self.base_q, self.base_t)
            def build(th):
                q2 = dict(self.base_q)
                for sd in ("l", "r"):
                    q2[RM["thigh_" + sd]] = rotate_bone_world(
                        self.nodes, RM["thigh_" + sd], self.lat, self.thSGN * th, q2)
                    q2[RM["calf_" + sd]] = rotate_bone_world(
                        self.nodes, RM["calf_" + sd], self.lat, self.dipSGN * 2 * th, q2)
                    q2[RM["foot_" + sd]] = hold_world_rotation(
                        self.nodes, RM["foot_" + sd], wq0[RM["foot_" + sd]],
                        q2, self.base_t)
                return q2
            lo, hi = 0.0, math.radians(40.0)
            for _ in range(14):
                mid = 0.5 * (lo + hi)
                w, _ = fk_world(self.nodes, build(mid), self.base_t)
                rise = sum(w[RM["foot_" + sd]][1] - self.wp0[RM["foot_" + sd]][1]
                           for sd in ("l", "r")) / 2.0
                if rise * self.S < -H: lo = mid
                else: hi = mid
            th = 0.5 * (lo + hi)
            q2 = build(th)
            for sd in ("l", "r"):
                for k in ("thigh_", "calf_", "foot_"):
                    qo[RM[k + sd]] = q2[RM[k + sd]]
            w, _ = fk_world(self.nodes, {**self.base_q, **qo}, self.base_t)
            drift = [sum(w[RM["foot_" + sd]][k] - self.wp0[RM["foot_" + sd]][k]
                         for sd in ("l", "r")) / 2.0 for k in range(3)]
            to[RM["pelvis"]] = translate_node_world(self.nodes, RM["pelvis"],
                                                    [-drift[0], -drift[1], -drift[2]],
                                                    self.base_q, self.base_t)
        self._mechC[key] = (qo, to)
        return (qo, to)

    def pose(self, L, P, YAW, H, u=1.0, press=0.0):
        """the kiss pose at blend u: mechanism H, spine lean L, neck pitch P, neck yaw,
        the complementary TILT roll. All angles degrees, all scaled by u."""
        RM = self.RM
        mq, mt = self.mech(H * u)
        qo = dict(self.base_q); qo.update(mq)
        to = dict(mt)
        spine = [b for b in (RM["spine_01"], RM["spine_02"], RM["spine_03"]) if b >= 0]
        Lr = math.radians((L + 0.4 * press) * u)
        qo[RM["pelvis"]] = rotate_bone_world(self.nodes, RM["pelvis"], self.lat, Lr * 0.3, qo)
        for b in spine:
            qo[b] = rotate_bone_world(self.nodes, b, self.lat, Lr / max(1, len(spine)), qo)
        nk, hd = RM["neck_01"], RM["head"]
        if nk >= 0:
            qo[nk] = rotate_bone_world(self.nodes, nk, self.fwd,
                                       math.radians(TILT * 0.6) * u, qo)
            qo[nk] = rotate_bone_world(self.nodes, nk, self.lat,
                                       math.radians(P) * u, qo)
            qo[nk] = rotate_bone_world(self.nodes, nk, [0.0, 1.0, 0.0],
                                       math.radians(YAW) * u, qo)
        qo[hd] = rotate_bone_world(self.nodes, hd, self.fwd,
                                   math.radians(TILT * 0.6) * u, qo)
        return qo, to

    def lip(self, nm, qo, to):
        """face landmark nm in grounded scaled world, riding the posed head"""
        hb = self.RM["head"]
        wp, wq = fk_world(self.nodes, qo, to)
        off = qrot(wq[hb], [self.locF[nm][k] * self.wsH[k] for k in range(3)])
        return self.gw([wp[hb][k] + off[k] for k in range(3)])

    def _try(self, L, P, H, tgt):
        """pose at (L,P,H) with the recentering yaw solved, -> (err, yaw, lip)"""
        qo, to = self.pose(L, P, 0.0, H)
        lp = self.lip("mouth", qo, to)
        hb = self.RM["head"]
        wp, _ = fk_world(self.nodes, qo, to)
        lever = max(0.05, lp[2] - wp[hb][2] * self.S)
        yaw = math.degrees(math.atan2(-lp[0], lever)) * 0.9
        yaw = max(-16.0, min(16.0, yaw))
        qo, to = self.pose(L, P, yaw, H)
        lp = self.lip("mouth", qo, to)
        return math.dist(lp, tgt), yaw, lp

    def solve(self, meetY, PGAP):
        """find (L, P, YAW, H) putting the MOUTH at (0, meetY, PGAP/2). A coarse
        effort-ordered ladder (no mechanism before mechanism, gentle lean before
        deep), then a fine hill-climb around the best -- the coarse steps move the
        lip more than the acceptance radius, so precision is earned locally."""
        tgt = [0.0, meetY, PGAP / 2.0]
        best = (1e9, None)
        for H in (0.0, 0.05, 0.10, 0.14, 0.18, -0.05, -0.10, -0.14):
            for L in (-8, -4, 0, 4, 8, 12, 16):
                for P in range(-32, 36, 4):
                    err, yaw, _ = self._try(L, P, H, tgt)
                    if err < best[0]:
                        best = (err, (L, P, yaw, H))
            if best[0] <= 0.035:
                break              # close enough for the hill-climb to finish the job
        err0, (L, P, YAW, H) = best
        for _ in range(3):
            moved = False
            for dL, dP, dH in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0),
                               (0, 0, 0.02), (0, 0, -0.02), (0, 2, 0), (0, -2, 0)):
                e2, y2, _ = self._try(L + dL, P + dP, H + dH, tgt)
                if e2 < best[0]:
                    best = (e2, (L + dL, P + dP, y2, H + dH))
                    L, P, YAW, H = best[1]
                    moved = True
            if not moved: break
        return best[1], best[0]

def cap_pen(px, py, pz, CAPS):
    worst = 0.0
    for (_nm, a9, b9, r9) in CAPS:
        ab = [b9[k] - a9[k] for k in range(3)]
        l2 = sum(v * v for v in ab) or 1e-12
        u9 = max(0.0, min(1.0, ((px - a9[0]) * ab[0] + (py - a9[1]) * ab[1]
                                + (pz - a9[2]) * ab[2]) / l2))
        cp = [a9[k] + ab[k] * u9 for k in range(3)]
        d9 = math.sqrt((px - cp[0]) ** 2 + (py - cp[1]) ** 2 + (pz - cp[2]) ** 2)
        if r9 - d9 > worst: worst = r9 - d9
    return worst

def main(pack_path, SCALE, ARMW, PGAP, PPACK, PSCALE):
    me = Body(pack_path, SCALE)
    if "z_kiss" in me.p.clip_names():
        print("z_kiss already present"); return 0
    her = Body(PPACK, PSCALE)

    myM = me.gw(me.F["mouth"])[1]
    herM = her.gw(her.F["mouth"])[1]
    print("z_kiss: mouth heights me %.3f her %.3f (gap %.3f)" % (myM, herM, myM - herM))

    # THE CHEST GATE at the stand (the lean gets its own per-frame pair gate below)
    MYFR = me.dims["front"] * SCALE
    HERFR = her.dims["front"] * PSCALE
    need = MYFR + HERFR - 0.05
    print("z_kiss: chest fronts me %.2f her %.2f  pgap %.2f (floor %.2f)"
          % (MYFR, HERFR, PGAP, need))
    assert PGAP >= need, "the bodies stand chest-inside-chest at this gap"

    # THE NEGOTIATED MEETING HEIGHT: center-out ladder, first height BOTH can reach.
    mid = 0.5 * (myM + herM)
    span = abs(myM - herM)
    cands = [mid]
    for k in range(1, 8):
        d = 0.045 * k
        cands += [mid - d, mid + d]
    cands = [y for y in cands if min(myM, herM) - 0.12 <= y <= max(myM, herM) + 0.12]
    sol = None
    for meetY in cands:
        (pa, ea) = me.solve(meetY, PGAP)
        if ea > 0.015:
            print("z_kiss: meetY %.3f: my lip misses by %.3f -- next" % (meetY, ea))
            continue
        (pb, eb) = her.solve(meetY, PGAP)
        if eb > 0.015:
            print("z_kiss: meetY %.3f: HER lip misses by %.3f -- next" % (meetY, eb))
            continue
        sol = (meetY, pa, pb, ea, eb)
        break
    assert sol, "no meeting height both bodies can reach"
    meetY, (L, P, YAW, H), herPrm, errA, errB = sol
    print("z_kiss: meetY %.3f  me (L %+d P %+d yaw %+.1f H %+.2f err %.3f)  "
          "her (L %+d P %+d yaw %+.1f H %+.2f err %.3f)"
          % (meetY, L, P, YAW, H, errA,
             herPrm[0], herPrm[1], herPrm[2], herPrm[3], errB))

    # both HOLD poses, and the partner's capsules at REST and at HOLD, mirrored into
    # my frame: (x,y,z)_her -> (-x, y, PGAP - z)_mine, grounded on her own datum
    qoH, toH = me.pose(L, P, YAW, H)
    hq, ht = her.pose(*herPrm)
    def her_caps(qo2, to2):
        w2, _ = fk_world(her.nodes, qo2, to2)
        out = []
        span2 = abs(her.wp0[her.RM["head"]][1] - her.wp0[her.RM["pelvis"]][1])
        for c in her.caps:
            b = her.RM.get(c["name"], -1)
            a = list(w2[b])
            kid = {"pelvis": "spine_01", "spine_01": "spine_02", "spine_02": "spine_03",
                   "spine_03": "neck_01", "neck_01": "head",
                   "thigh_l": "calf_l", "calf_l": "foot_l",
                   "thigh_r": "calf_r", "calf_r": "foot_r"}.get(c["name"])
            t = list(w2[her.RM[kid]]) if kid and her.RM.get(kid, -1) >= 0 \
                else [a[0], a[1] + 0.12 * span2, a[2]]
            a9 = [-a[0] * PSCALE, (a[1] - her.mny) * PSCALE, PGAP - a[2] * PSCALE]
            b9 = [-t[0] * PSCALE, (t[1] - her.mny) * PSCALE, PGAP - t[2] * PSCALE]
            out.append((c["name"], a9, b9, c["r"] * PSCALE))
        return out
    CAPS_R = her_caps(her.base_q, her.base_t)
    CAPS_H = her_caps(hq, ht)
    def pen_both(px, py, pz):
        return max(cap_pen(px, py, pz, CAPS_R), cap_pen(px, py, pz, CAPS_H))

    def spine_axis_at(yw, CAPS):
        best9 = None
        tor = [c for c in CAPS if c[0] in ("pelvis", "spine_01", "spine_02",
                                           "spine_03", "neck_01")]
        for (nm9, a9, b9, r9) in tor:
            mid9 = 0.5 * (a9[1] + b9[1])
            sc9 = abs(yw - mid9)
            if best9 is None or sc9 < best9[0]:
                dy9 = (b9[1] - a9[1]) or 1e-9
                u9 = max(0.0, min(1.0, (yw - a9[1]) / dy9))
                ax9 = [a9[k] + (b9[k] - a9[k]) * u9 for k in range(3)]
                best9 = (sc9, ax9, r9)
        return best9[1], best9[2]

    # HER geometry for the hand landings, from the HOLD capsules
    w2h, _ = fk_world(her.nodes, hq, ht)
    PSH = (sum(w2h[her.RM["upperarm_" + s]][1] for s in "lr") / 2 - her.mny) * PSCALE
    waistY = (0.5 * (w2h[her.RM["spine_01"]][1] + w2h[her.RM["pelvis"]][1])
              - her.mny) * PSCALE + 0.06
    armR = (me.RM["upperarm_r"], me.RM["lowerarm_r"], me.RM["hand_r"])
    armL = (me.RM["upperarm_l"], me.RM["lowerarm_l"], me.RM["hand_l"])
    wpl, _ = fk_world(me.nodes, qoH, toH)

    def hold_penetration(chain, tgt, pole):
        qh = dict(qoH)
        upd = solve_limb(me.nodes, chain, tgt, pole=pole, q_override=qh,
                         t_override=dict(toH))
        qh.update(upd)
        wh, _ = fk_world(me.nodes, qh, toH)
        worst = 0.0
        for a6, b6 in ((chain[0], chain[1]), (chain[1], chain[2])):
            for i6 in range(1, 8):
                q6 = i6 / 8.0
                px = (wh[a6][0] + (wh[b6][0] - wh[a6][0]) * q6) * SCALE
                py = (wh[a6][1] - me.mny + (wh[b6][1] - wh[a6][1]) * q6) * SCALE
                pz = (wh[a6][2] + (wh[b6][2] - wh[a6][2]) * q6) * SCALE
                d6 = cap_pen(px, py, pz, CAPS_H)
                if d6 > worst: worst = d6
        return worst

    def land(nm7, chain, yw, side, pole):
        """a hand landing ON her front-side surface: psi>90 = past the side toward her
        front. Deepest-front candidate first; falls back toward the side, then lower."""
        sh6 = wpl[chain[0]]
        bestFail = (1e9, "")
        for dy in (0.0, 0.05, 0.10, -0.06, -0.12, -0.18):
            for pdeg in range(122, 78, -6):
                psi = math.radians(pdeg * side)
                ax9, r9 = spine_axis_at(yw + dy, CAPS_H)
                cand = [(ax9[0] + (r9 + 0.01) * math.sin(psi)) / SCALE,
                        me.mny + (yw + dy) / SCALE,
                        (ax9[2] + (r9 + 0.01) * math.cos(psi)) / SCALE]
                d6 = math.dist(sh6, cand) * SCALE
                if d6 > 0.90 * ARMW:
                    if d6 < bestFail[0] + 1e9 and bestFail[1] == "":
                        bestFail = (1e9, "psi %+d dy %+.2f reach %.2f > %.2f"
                                    % (pdeg * side, dy, d6, 0.90 * ARMW))
                    continue
                pen9 = hold_penetration(chain, cand, pole)
                if pen9 <= 0.028:
                    print("z_kiss: %s lands psi %+d dy %+.2f (reach %.2f of %.2f)"
                          % (nm7, pdeg * side, dy, d6, ARMW))
                    return cand, psi
                if pen9 < bestFail[0]:
                    bestFail = (pen9, "psi %+d dy %+.2f pen %.3f"
                                % (pdeg * side, dy, pen9))
        raise AssertionError("no %s landing clears her body (best: %s)"
                             % (nm7, bestFail[1]))

    poleR = [-0.80, -0.30, 0.35]
    poleL = [+0.80, -0.15, 0.35]
    tgtR, psiR = land("waist-hand(R)", armR, waistY, -1, poleR)
    tgtL, psiL = land("shoulder-hand(L)", armL, PSH - 0.05, +1, poleL)

    def lay_dir(psi):
        # fingers continue AROUND her: the surface tangent at psi, biased downward
        tx, tz = math.cos(psi), -math.sin(psi)
        if tz < 0: tx, tz = -tx, -tz
        return _n([tx, -0.25, tz])
    layR, layL = lay_dir(psiR), lay_dir(psiL)
    _wqr0 = fk_world(me.nodes)[1]
    wr0, _ = fk_world(me.nodes)
    def _hdl(chain):
        fa = _n([wr0[chain[2]][k] - wr0[chain[1]][k] for k in range(3)])
        return qrot(qinv(_wqr0[chain[2]]), fa)
    hdirR, hdirL = _hdl(armR), _hdl(armL)

    # ---- the timeline ----
    frames = int(T_END * FPS)
    qtr, ttr = {}, {"pelvis": []}
    restR = list(me.wp0[armR[2]]); restL = list(me.wp0[armL[2]])
    worst_pen = [0.0, -1.0, ""]
    worst_lip = [0.0, -1.0]
    worst_chest = [1e9, -1.0]
    checked = False
    hb = me.RM["head"]
    for f in range(frames):
        t = f / FPS
        u = profile([(0.0, 0.0, ease_anticipate), (0.40, 0.06, ease_smooth),
                     (T_MEET, 1.0, ease_smooth), (T_HOLD_END, 1.0, ease_smooth),
                     (T_END - 0.15, 0.0, None)], t)
        ua = profile([(0.25, 0.0, ease_smooth), (T_MEET - 0.25, 1.0, ease_smooth),
                      (T_HOLD_END + 0.20, 1.0, ease_smooth),
                      (T_END - 0.05, 0.0, None)], t)
        press = 0.0
        if T_MEET <= t <= T_HOLD_END:
            press = math.sin(math.pi * (t - T_MEET) / (T_HOLD_END - T_MEET))
        qo, to = me.pose(L, P, YAW, H, u, press)
        hq2, ht2 = her.pose(herPrm[0], herPrm[1], herPrm[2], herPrm[3], u, press)

        # the arms ride out to her sides and land
        for nm2, chain, tgt, rest, pole in (("waistR", armR, tgtR, restR, poleR),
                                            ("shoulderL", armL, tgtL, restL, poleL)):
            sgn = 1.0 if tgt[0] >= 0 else -1.0
            cx = profile([(0.0, rest[0], ease_smooth),
                          (0.5, tgt[0] + sgn * 0.10 / SCALE, ease_smooth),
                          (1.0, tgt[0], None)], ua)
            cy = profile([(0.0, rest[1], ease_smooth), (1.0, tgt[1], None)], ua)
            cz = profile([(0.0, rest[2], ease_smooth),
                          (0.55, tgt[2] - 0.12 / SCALE, ease_smooth),
                          (1.0, tgt[2] + (0.010 * press) / SCALE, None)], ua)
            upd = solve_limb(me.nodes, chain, [cx, cy, cz], pole=pole,
                             q_override=qo, t_override=to)
            qo.update(upd)
        if ua > 0.60:
            _w, _wq = fk_world(me.nodes, qo, to)
            k5 = min(1.0, (ua - 0.60) / 0.30)
            for chain, dirv, hdl in ((armR, layR, hdirR), (armL, layL, hdirL)):
                f5 = _n(qrot(_wq[chain[2]], hdl))
                A5 = q_between(f5, dirv)
                wq_t = qmul(A5, _wq[chain[2]])
                qo[chain[2]] = hold_world_rotation(me.nodes, chain[2],
                                                   qslerp(_wq[chain[2]], wq_t, k5),
                                                   qo, to)

        w9, _ = fk_world(me.nodes, qo, to)
        wh9, _ = fk_world(her.nodes, hq2, ht2)

        # PAIR GATES, every frame against her POSED body
        for chain in (armR, armL):
            for a5, b5 in ((chain[0], chain[1]), (chain[1], chain[2])):
                for i5 in range(1, 8):
                    q5 = i5 / 8.0
                    px = (w9[a5][0] + (w9[b5][0] - w9[a5][0]) * q5) * SCALE
                    py = (w9[a5][1] - me.mny + (w9[b5][1] - w9[a5][1]) * q5) * SCALE
                    pz = (w9[a5][2] + (w9[b5][2] - w9[a5][2]) * q5) * SCALE
                    dep = pen_both(px, py, pz)
                    if dep > worst_pen[0]:
                        tag = ("R" if chain is armR else "L") + \
                              ("-upper" if a5 == chain[0] else "-fore")
                        worst_pen[:] = [dep, t, tag]
        # pairwise chest clearance at three torso heights
        for bn in ("spine_01", "spine_02", "spine_03"):
            mc = w9[me.RM[bn]]
            hc = wh9[her.RM[bn]]
            myz = mc[2] * SCALE + MYFR
            herz = PGAP - (hc[2] * PSCALE + HERFR)
            if herz - myz < worst_chest[0]:
                worst_chest[:] = [herz - myz, t]
        # lip-to-lip, worst inside the hold. SIGNED along z: two lip points that have
        # pressed PAST each other read as "apart" again on a plain distance -- the
        # violation is off-axis miss, a real gap, or a press deeper than lip flesh.
        if T_MEET + 0.12 <= t <= T_HOLD_END - 0.12:
            myL = me.lip("mouth", qo, to)
            hL = her.lip("mouth", hq2, ht2)
            hLm = [-hL[0], hL[1], PGAP - hL[2]]
            dz = myL[2] - hLm[2]              # positive = pressed into each other
            off = math.hypot(myL[0] - hLm[0], myL[1] - hLm[1])
            viol = max(off, -dz if dz < 0 else 0.0,
                       dz - 0.025 if dz > 0.025 else 0.0)
            if viol > worst_lip[0]: worst_lip[:] = [viol, t]
        if not checked and t >= 0.5 * (T_MEET + T_HOLD_END):
            checked = True
            myL = me.lip("mouth", qo, to)
            myN = me.lip("nose", qo, to)
            myF = me.lip("forehead", qo, to)
            hL = her.lip("mouth", hq2, ht2); hLm = [-hL[0], hL[1], PGAP - hL[2]]
            hN = her.lip("nose", hq2, ht2); hNm = [-hN[0], hN[1], PGAP - hN[2]]
            hF = her.lip("forehead", hq2, ht2); hFm = [-hF[0], hF[1], PGAP - hF[2]]
            dzL = myL[2] - hLm[2]
            offL = math.hypot(myL[0] - hLm[0], myL[1] - hLm[1])
            print("z_kiss: MID-HOLD lips off-axis %.3f press %+.3f  me (%.3f,%.3f,%.3f) her (%.3f,%.3f,%.3f)"
                  % (offL, dzL, *myL, *hLm))
            nd = math.dist(myN, hNm)
            nlat = abs(myN[0] - hNm[0])
            print("z_kiss: noses %.3fu apart (lateral %.3f)  foreheads %.3fu"
                  % (nd, nlat, math.dist(myF, hFm)))
            assert offL <= 0.020, "the lips miss sideways"
            assert -0.012 <= dzL <= 0.025, "the lips do not meet (gap or a mash)"
            assert nd >= 0.026 or nlat >= 0.020, "the noses collide"
            assert math.dist(myF, hFm) >= 0.030, "the foreheads collide"
            hx = (w9[hb][0] - me.wp0[hb][0]) * SCALE
            print("z_kiss: head lateral %+.3f (own right = negative)" % hx)
            assert hx < -0.006, "the head never tilted to its own right"
            mh = me.gw(w9[hb])
            hh = her.gw(wh9[her.RM["head"]])
            hhm = [-hh[0], hh[1], PGAP - hh[2]]
            print("z_kiss: PREDICT engine [kiss] head-head %.3fu  (me %.2f,%.2f,%.2f | her %.2f,%.2f,%.2f)"
                  % (math.dist(mh, hhm), *mh, *hhm))

        chain_all = ([me.RM[k] for k in ("pelvis", "neck_01", "head", "spine_01",
                                         "spine_02", "spine_03", "foot_l", "foot_r",
                                         "ball_l", "ball_r", "calf_l", "calf_r",
                                         "thigh_l", "thigh_r")]
                     + list(armR) + list(armL))
        for b in chain_all:
            if b is not None and b >= 0:
                qtr.setdefault(b, []).extend(
                    qnorm(qo.get(b, me.base_q.get(b, me.nodes[b]["q"]))))
        ttr["pelvis"].extend(to.get(me.RM["pelvis"],
                                    me.base_t.get(me.RM["pelvis"],
                                                  me.nodes[me.RM["pelvis"]]["t"])))

    print("z_kiss: worst arm penetration %.3fu at t=%.2f %s (skin 0.030)"
          % (worst_pen[0], worst_pen[1], worst_pen[2]))
    print("z_kiss: worst lip gap in hold %.3fu at t=%.2f" % (worst_lip[0], worst_lip[1]))
    print("z_kiss: tightest chest clearance %+.3fu at t=%.2f (squish floor -0.040)"
          % (worst_chest[0], worst_chest[1]))
    assert worst_pen[0] <= 0.030, "an arm passes THROUGH her"
    assert worst_lip[0] <= 0.045, "the lips drift apart inside the hold"
    assert worst_chest[0] >= -0.040, "the chests interpenetrate past a squish"

    tracks = {}
    for (node, typ), v in me.base.items():
        tracks[(node, typ)] = v * frames
    for b, vals in qtr.items(): tracks[(b, 0)] = vals
    tracks[(me.RM["pelvis"], 1)] = ttr["pelvis"]
    append_clips(pack_path, [("z_kiss", FPS, frames, tracks)])
    declare(pack_path, "z_kiss", {
        "fps": FPS, "frames": frames, "terminal": "base", "max_speed": 6.0,
        "contacts": [{"part": "fL", "t": [0.9, T_HOLD_END], "floor": False, "still_tol": 0.06},
                     {"part": "fR", "t": [0.9, T_HOLD_END], "floor": False, "still_tol": 0.06}],
    })
    print("z_kiss: appended (%d frames, %d tracks)" % (frames, len(tracks)))
    return 0

if __name__ == "__main__":
    a = sys.argv[1:]
    def arg(nm, d):
        return float(a[a.index(nm) + 1]) if nm in a else d
    pp = a[a.index("--ppack") + 1] if "--ppack" in a else ""
    sys.exit(main(a[0], arg("--scale", 1.0), arg("--arm", 1.0), arg("--pgap", 0.5),
                  pp, arg("--pscale", 1.0)))
