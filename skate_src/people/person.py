"""person.py -- A PERSON BY MEASUREMENT: one parametric human body for a whole cast (10-18, THE WORLD AT THE END OF
TIME, ep 1 scene 2.1: a twelve-year-old, a hundred-year-old woman, a mother, two five-year-olds).

The suit man and REVA are body MODULES: every height in them is a number measured off one sheet, so neither can be
a child. Here a body is a SPEC of the measurements a tailor or an anthropometry table gives -- stature, head height,
the LANDMARK heights as fractions of stature (that is where age lives: a five-year-old stands ~5.5 heads, a
twelve-year-old ~7, an adult ~7.5), girths as half-widths and depths, limb radii, bust, belly -- and Person(spec)
lofts the skin from them on the atelier's machinery (Part, Surface, loft), so the wardrobe, hide sets and the sewing
machine work on these bodies exactly as on REVA's:

  Person(spec).TORSO / LEG[+1/-1] / ARM["Left"/"Right"]    the dress form (atelier Surfaces)
  .build_skin() .build_arm_skin(sg) .build_head() .build_eyes() .build_hair()    Parts
  .bones()      Mixamo names (the club's canon and Godot's humanoid); A-pose, arms 45 degrees down
  .outward(p, part_name)                                    which way is out of the body (hide sets, garments)

Built on z = 0 (bare soles), z up, -Y forward, +X the body's left. Metres.
"""
import math

from mathutils import Vector

import atelier as at
from atelier import smoothstep, lerp_table, ring_xy, norm_w, Part, loft, Surface

ARM_DOWN = math.radians(45.0)
_DONOR = {}


def donor_hand(path=None):
    """Anima's left hand in her wrist's frame (skate_src/people/extract_hand.py -> models/_out/anima_hand_l.npz)"""
    import os
    import numpy as np
    path = path or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "models", "_out",
                                "anima_hand_l.npz")
    if path not in _DONOR:
        z = np.load(path)
        V = z["V"]
        wr = V[(V[:, 1] > -0.10) & (V[:, 1] < -0.05)]           # the wrist's section, measured
        _DONOR[path] = {"V": V, "F": z["F"], "W": z["W"], "WI": z["WI"], "names": [str(n) for n in z["names"]],
                        "j": {str(k): v for k, v in zip(z["jn"], z["jp"])},
                        "wx": float(0.5 * (wr[:, 0].min() + wr[:, 0].max())),      # its centre, toward the thumb
                        "whx": float(0.5 * (wr[:, 0].max() - wr[:, 0].min())),      # half-width across the knuckles
                        "whz": float(0.5 * (wr[:, 2].max() - wr[:, 2].min()))}      # half-depth, back to palm
    return _DONOR[path]

# the head's shape, chin (u 0) to crown (u 1), in units of the head's half-breadth (hw) and half-depth (yf forward of
# the head's axis, negative; yb behind it): an adult's. A child's face takes less of the head (spec "brow_u")
HEAD_T = [  # u, hw, yf, yb, squareness
    (0.000, 0.16, -0.70, -0.52, 1.00), (0.035, 0.36, -0.80, -0.20, 0.96), (0.090, 0.55, -0.86, 0.12, 0.92),
    (0.160, 0.70, -0.90, 0.40, 0.90), (0.230, 0.80, -0.92, 0.60, 0.88), (0.310, 0.86, -0.92, 0.78, 0.86),
    (0.400, 0.93, -0.91, 0.90, 0.86), (0.490, 0.97, -0.90, 0.97, 0.86), (0.600, 1.00, -0.94, 1.01, 0.86),
    (0.690, 1.00, -0.92, 1.03, 0.86), (0.780, 0.96, -0.82, 1.00, 0.88), (0.860, 0.86, -0.66, 0.90, 0.90),
    (0.930, 0.66, -0.44, 0.66, 0.94), (0.975, 0.38, -0.20, 0.36, 1.00), (1.000, 0.06, -0.02, 0.04, 1.00)]
FACE_BROW_U = 0.60          # in HEAD_T, the brow sits at u 0.60: everything below it is FACE


def _g(x, s):
    return math.exp(-(x / s) ** 2)


def _pchip_slopes(xs, ys):
    """Fritsch-Carlson slopes: a monotone cubic through the points (no wobble between rows)"""
    n = len(xs)
    d = [(ys[i + 1] - ys[i]) / (xs[i + 1] - xs[i]) for i in range(n - 1)]
    m = [0.0] * n
    m[0], m[-1] = d[0], d[-1]
    for i in range(1, n - 1):
        if d[i - 1] * d[i] <= 0: m[i] = 0.0
        else:
            w1 = 2 * (xs[i + 1] - xs[i]) + (xs[i] - xs[i - 1]); w2 = (xs[i + 1] - xs[i]) + 2 * (xs[i] - xs[i - 1])
            m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i])
    return m


_SMOOTH = {}


def smooth_table(table, x, cache=True):
    """every column of a (x, ...) table at x by a monotone cubic -- C1, so a surface built from it has NO CREASE at
    a row. (The head's table was read with lerp_table: a stack of bands, a crease at every row, and the toon's
    hard cel cut turned each crease into a LEDGE down the face -- a staircase in the engine, shadows on or off.)"""
    key = id(table)
    if not cache or key not in _SMOOTH:
        xs = [r[0] for r in table]
        cols = []
        for c in range(1, len(table[0])):
            ys = [r[c] for r in table]
            cols.append((ys, _pchip_slopes(xs, ys)))
        if not cache: _tab = (xs, cols)
        else: _SMOOTH[key] = (xs, cols)
    xs, cols = _tab if not cache else _SMOOTH[key]
    if x <= xs[0]: return [c[0][0] for c in cols]
    if x >= xs[-1]: return [c[0][-1] for c in cols]
    i = 0
    while xs[i + 1] < x: i += 1
    h = xs[i + 1] - xs[i]; t = (x - xs[i]) / h
    h00 = 2 * t ** 3 - 3 * t ** 2 + 1; h10 = t ** 3 - 2 * t ** 2 + t; h01 = -2 * t ** 3 + 3 * t ** 2; h11 = t ** 3 - t ** 2
    return [h00 * ys[i] + h10 * h * m[i] + h01 * ys[i + 1] + h11 * h * m[i + 1] for ys, m in cols]


class Person:
    def __init__(self, spec):
        self.S = S = dict(spec)
        H = S["H"]
        L = S["lm"]
        self.H = H
        self.Z = Z = {k: v * H for k, v in L.items()}
        Z["crown"] = H
        Z["chin"] = H - S["head_h"]
        Z["neck_top"] = Z["chin"] + 0.24 * S["head_h"]          # inside the head: the neck's skin ends there, capped
        self.name = S["name"]
        hip, wst, chest = S["hip"], S["waist"], S["chest"]
        sh_hw, nr = S["shoulder_hw"], S["neck_r"]
        dr = S["arm"]["deltoid"]
        self.joint_x = sh_hw - dr * 0.95
        self.joint_z = Z["shoulder"] - 0.55 * dr
        zc, zw, za, zs, zn = Z["crotch"], Z["waist"], Z["armpit"], Z["shoulder"], Z["neckbase"]
        zb = Z["bust"]
        cyb = S.get("back_cy", 0.0)
        # ---------------------------------------------------------------- the torso's sections, crotch to neck
        under = zw + 0.62 * (zb - zw)
        self.T_BODY = sorted([
            (zc, hip[0] * 0.90, hip[1] * 0.86, hip[2] * 0.88, 0.006, 0.90),
            (zc + 0.30 * (zw - zc), hip[0], hip[1], hip[2], 0.008, 0.88),
            (zc + 0.62 * (zw - zc), 0.5 * (hip[0] + wst[0]) + 0.05 * (hip[0] - wst[0]), 0.5 * (hip[1] + wst[1]),
             0.5 * (hip[2] + wst[2]), 0.006, 0.90),
            (zw, wst[0], wst[1], wst[2], 0.004, 0.94),
            (under, 0.5 * (wst[0] + chest[0]) + 0.2 * (chest[0] - wst[0]), 0.5 * (wst[1] + chest[1]),
             0.5 * (wst[2] + chest[2]), 0.002 + cyb * 0.3, 0.92),
            (zb, chest[0], chest[1], chest[2], 0.0 + cyb * 0.6, 0.90),
            (za, chest[0] * 1.02, chest[1] * 0.94, chest[2] * 1.00, 0.002 + cyb, 0.88),
            (za + 0.50 * (zs - za), self.joint_x + 0.30 * dr, chest[1] * 0.84, chest[2] * 0.96, 0.006 + cyb, 0.86),
            (zs, self.joint_x + 0.10 * dr, chest[1] * 0.72, chest[2] * 0.84, 0.010 + cyb, 0.86),
            # the trapezius: from the shoulder's point it RISES to the neck's side, ~4% of stature above it (zn is
            # the sternal notch at the front, which sits about level with the shoulders)
            (zs + 0.012 * H, 0.70 * (self.joint_x + 0.1 * dr) + 0.30 * nr * 1.6, chest[1] * 0.64, chest[2] * 0.78,
             0.012 + cyb, 0.90),
            (zs + 0.024 * H, 0.38 * (self.joint_x + 0.1 * dr) + 0.62 * nr * 1.4, chest[1] * 0.56, chest[2] * 0.68,
             0.014 + cyb, 0.94),
            (zs + 0.036 * H, nr * 1.22, nr * 1.05, nr * 1.15, 0.016 + cyb, 0.98),
            (zs + 0.050 * H, nr * 1.04, nr * 0.98, nr * 1.04, 0.018 + cyb, 1.00),
            (Z["neck_top"], nr * 0.98, nr * 0.95, nr * 1.00, 0.020 + cyb, 1.00)])
        step = 0.022 * H
        zs_ = [zc]
        while zs_[-1] + step < Z["neck_top"]: zs_.append(zs_[-1] + step)
        zs_ += [zc + 0.010 * H, zc + 0.022 * H, zw, zb, za, zs, zs + 0.012 * H, zs + 0.024 * H, zs + 0.036 * H,
                Z["neck_top"]]
        zs_ = sorted(set(round(z, 5) for z in zs_))
        self.ZB = [z for i, z in enumerate(zs_) if i == 0 or z - zs_[i - 1] > 0.006 * H]
        if self.ZB[-1] != round(Z["neck_top"], 5): self.ZB.append(round(Z["neck_top"], 5))
        self.NB = 28
        # ---------------------------------------------------------------- the legs, ankle to crotch
        lg = S["leg"]
        zk, zak = Z["knee"], Z["ankle"]
        top_hw = hip[0] * 0.90 * 0.50
        self.LEGX = {"hip": hip[0] * 0.90 * 0.52, "knee": lg.get("knee_x", hip[0] * 0.48),
                     "ankle": lg.get("ankle_x", hip[0] * 0.50)}
        hx, kx, ax = self.LEGX["hip"], self.LEGX["knee"], self.LEGX["ankle"]
        th, kn, cf, an = lg["thigh"], lg["knee"], lg["calf"], lg["ankle"]

        def cx(z):
            if z >= zk: return kx + (hx - kx) * (z - zk) / (zc - zk)
            return ax + (kx - ax) * (z - zak) / (zk - zak)
        legrows = [
            (zak, an * 1.00, an * 0.95, an * 1.05, 0.020 * H / 1.7),
            (zak + 0.14 * (zk - zak), an * 1.12, an * 1.02, an * 1.25, 0.017 * H / 1.7),
            (zak + 0.40 * (zk - zak), 0.5 * (an + cf) * 1.05, cf * 0.82, cf * 1.05, 0.012 * H / 1.7),
            (zak + 0.66 * (zk - zak), cf, cf * 0.86, cf * 1.18, 0.006 * H / 1.7),
            (zk - 0.07 * (zk - zak), kn * 1.02, kn * 0.98, kn * 1.02, -0.002),
            (zk, kn, kn * 1.04, kn * 0.96, -0.006 * H / 1.7),
            (zk + 0.14 * (zc - zk), kn * 1.12, kn * 1.08, kn * 1.04, -0.005 * H / 1.7),
            (zk + 0.50 * (zc - zk), 0.35 * kn + 0.65 * th, 0.35 * kn + 0.65 * th, 0.35 * kn + 0.65 * th * 1.05, -0.003),
            (zc - 0.035 * H, th, th * 1.00, th * 1.06, 0.000),
            (zc - 0.010 * H, top_hw * 0.99, hip[1] * 0.86 * 0.96, hip[2] * 0.88 * 0.96, 0.004)]
        self.T_LEGS = [(z, cx(z), cy, hw, f, b) for z, hw, f, b, cy in legrows]
        ztop = zc - 0.010 * H
        self.LZ = sorted({round(r[0], 5) for r in legrows} | {round(zak + (ztop - zak) * k / 16.0, 5) for k in range(17)},
                         reverse=True)
        self.NLEG = 14
        # ---------------------------------------------------------------- the arms
        a = S["arm"]
        U, F = a["upper"], a["fore"]
        self.UPPER, self.FORE, self.WRIST = U, F, U + F
        ur, er, fr, wr = a["upper_r"], a["elbow_r"], a["fore_r"], a["wrist_r"]
        self.T_ARM = [
            (-0.95 * dr, 0.30 * dr, 0.30 * dr), (-0.62 * dr, 0.82 * dr, 0.80 * dr), (-0.20 * dr, 0.98 * dr, 0.95 * dr),
            (0.20 * U, 0.96 * dr, 0.92 * dr), (0.45 * U, ur, ur * 0.98), (0.75 * U, ur * 0.92, ur * 0.94),
            # the forearm flattens toward the wrist IN THE PALM'S PLANE (d, fr): a relaxed A-pose hand faces the thigh
            (0.97 * U, er, er * 0.92), (U + 0.22 * F, fr * 0.95, fr * 0.95), (U + 0.60 * F, 0.5 * (fr + wr) * 0.82,
                                                                             0.5 * (fr + wr) * 1.05),
            (U + F, wr * 0.74, wr * 1.22), (U + F + 0.012 * H / 1.7, wr * 0.70, wr * 1.25)]
        if S.get("hand_donor"):
            # the forearm ENDS in the donor's wrist: her section, at this hand's size (a 7 mm step otherwise,
            # worst on the children). rq runs along q (back of hand to palm), rf along the front (across)
            D = donor_hand(); hl = S["hand"]
            wq, wf = D["whz"] * hl * 1.02, D["whx"] * hl * 1.02
            self.T_ARM[-2] = (U + F, wq, wf)
            self.T_ARM[-1] = (U + F + 0.012 * H / 1.7, wq, wf)
            r9 = self.T_ARM[-3]
            self.T_ARM[-3] = (r9[0], 0.5 * (r9[1] + wq), 0.5 * (r9[2] + wf))
        self.ARMB = {}
        for sg in (1, -1):
            side = "Left" if sg > 0 else "Right"
            d = Vector((sg * math.cos(ARM_DOWN), 0.0, -math.sin(ARM_DOWN)))
            q = Vector((sg * math.sin(ARM_DOWN), 0.0, math.cos(ARM_DOWN)))
            fr_ = Vector((0.0, -1.0, 0.0))
            J = Vector((sg * self.joint_x, 0.008 + cyb, self.joint_z))
            self.ARMB[side] = {"joint": J, "elbow": J + d * U, "wrist": J + d * (U + F), "d": d, "q": q, "fr": fr_,
                               "sg": sg}
        # ---------------------------------------------------------------- the dress form
        self.TORSO = Surface("torso", self.body_pt, self.torso_out, lambda p: self.w_body(p.x, p.y, p.z))
        self.LEG = {sg: Surface("leg%+d" % sg, (lambda sg_: (lambda u, v: self.leg_pt(sg_, math.degrees(u), v)))(sg),
                                self.leg_out(sg), (lambda sg_: (lambda p: self.w_leg(sg_, p.z)))(sg)) for sg in (1, -1)}
        self.ARM = {side: Surface("arm" + side, self.arm_pt_of(A), self.arm_out_of(A),
                                  (lambda s: (lambda p: self.w_arm((p - self.ARMB[s]["joint"]).dot(self.ARMB[s]["d"]),
                                                                   s)))(side))
                    for side, A in self.ARMB.items()}
        # ---------------------------------------------------------------- the head's frame
        hd = S["head"]
        self.HB, self.HD = hd["breadth"] / 2.0, hd["depth"] / 2.0
        self.head_cy = 0.020 + cyb - 0.10 * self.HD + hd.get("cy", 0.0)

    # ================================================================== the torso
    def body_pt(self, theta, z):
        S = self.S
        hw, f, b, cy, p = lerp_table(self.T_BODY, z)
        x, y = ring_xy(theta, hw, f, b, cy, p)
        c = math.cos(theta)
        if c > 0:
            bust = S.get("bust", 0.0)                          # forward reach of each breast, metres
            if bust > 0:
                bz, bx = self.Z["bust"], S.get("bust_x", 0.42 * S["chest"][0])
                for sg in (1, -1):
                    d = math.exp(-((x - sg * bx) / (0.36 * S["chest"][0])) ** 2
                                 - ((z - bz) / ((0.050 if z < bz else 0.070) * self.H / 1.65)) ** 2)
                    y -= bust * d * c ** 0.5
            belly = S.get("belly", 0.0)                        # the tummy: forward of the waist's front, metres
            if belly > 0:
                nz = self.Z["waist"] - 0.035 * self.H
                y -= belly * c ** 1.5 * _g(z - nz, 0.075 * self.H) * _g(x, 0.75 * S["waist"][0])
            zn = self.Z["neckbase"]
            y += 0.18 * S["neck_r"] * c ** 3 * _g(z - zn, 0.012 * self.H) * _g(x, 0.6 * S["neck_r"])
        else:
            seat = S.get("seat", 0.0)
            if seat > 0:
                hz = self.Z["crotch"] + 0.35 * (self.Z["waist"] - self.Z["crotch"])
                for bx in (0.45 * S["hip"][0], -0.45 * S["hip"][0]):
                    y += seat * (-c) ** 0.5 * _g(x - bx, 0.42 * S["hip"][0]) * _g(z - hz, 0.07 * self.H)
            sb = S.get("shoulder_blades", 0.0)
            if sb > 0:
                bz = self.Z["armpit"] + 0.2 * (self.Z["shoulder"] - self.Z["armpit"])
                for bx in (0.45 * S["chest"][0], -0.45 * S["chest"][0]):
                    y += sb * (-c) * _g(x - bx, 0.30 * S["chest"][0]) * _g(z - bz, 0.06 * self.H)
        return Vector((x, y, z))

    def torso_out(self, p):
        cy = lerp_table(self.T_BODY, p.z)[3]
        return Vector((p.x, p.y - cy, 0.0))

    def w_body(self, x, y, z):
        Z = self.Z
        zh = Z["crotch"] + 0.62 * (Z["waist"] - Z["crotch"])
        z1 = Z["waist"] + 0.40 * (Z["bust"] - Z["waist"])
        z2 = Z["bust"] - 0.10 * (Z["bust"] - Z["waist"])
        zn, zt = Z["neckbase"], Z["neck_top"]
        e = 0.020 * self.H
        hips = 1.0 - smoothstep(zh - e, zh + e, z)
        sp = smoothstep(zh - e, zh + e, z) * (1.0 - smoothstep(z1 - e, z1 + e, z))
        sp1 = smoothstep(z1 - e, z1 + e, z) * (1.0 - smoothstep(z2 - e, z2 + e, z))
        sp2 = smoothstep(z2 - e, z2 + e, z) * (1.0 - smoothstep(zn - 0.012 * self.H, zn + 0.004 * self.H, z))
        nk = smoothstep(zn - 0.012 * self.H, zn + 0.004 * self.H, z) * (1.0 - smoothstep(zt - 0.03 * self.H, zt, z))
        hd = smoothstep(zt - 0.03 * self.H, zt, z)
        side = "Left" if x > 0 else "Right"
        sh = 0.75 * smoothstep(0.45 * self.joint_x, 0.80 * self.joint_x, abs(x)) \
            * smoothstep(Z["armpit"], Z["armpit"] + 0.5 * (Z["shoulder"] - Z["armpit"]), z) \
            * (1.0 - smoothstep(Z["shoulder"] + 0.2 * (zn - Z["shoulder"]), zn, z))
        legs = 0.7 * smoothstep(Z["crotch"] + 0.30 * (Z["waist"] - Z["crotch"]), Z["crotch"], z)
        sl = min(1.0, max(0.0, 0.5 + x / (0.30 * self.S["hip"][0])))
        return norm_w({"Hips": hips * (1.0 - legs), "LeftUpLeg": legs * sl, "RightUpLeg": legs * (1.0 - sl),
                       "Spine": sp, "Spine1": sp1, "Spine2": sp2 * (1.0 - sh), side + "Shoulder": sp2 * sh,
                       "Neck": nk, "Head": hd})

    # ================================================================== the legs
    def leg_phi(self, sg, k):
        step = 360.0 / self.NLEG
        return (-90.0 + step / 2.0 + k * step) if sg > 0 else (270.0 - step / 2.0 - k * step)

    def leg_pt(self, sg, phi_deg, z):
        cx, cy, hw, f, b = lerp_table(self.T_LEGS, z)
        ph = math.radians(phi_deg)
        s, c = math.sin(ph), math.cos(ph)
        x = sg * cx + sg * hw * math.copysign(abs(s) ** 0.9, s)
        y = cy - (f if c >= 0 else b) * math.copysign(abs(c) ** 0.9, c)
        return Vector((x, y, z))

    def leg_out(self, sg):
        def f(p):
            cx, cy = lerp_table(self.T_LEGS, p.z)[:2]
            return Vector((p.x - sg * cx, p.y - cy, 0.0))
        return f

    def w_leg(self, sg, z):
        Z = self.Z
        side = "Left" if sg > 0 else "Right"
        e = 0.03 * self.H
        knee = smoothstep(Z["knee"] + e, Z["knee"] - e, z)
        hip = 0.3 * smoothstep(Z["crotch"] - 0.05 * self.H, Z["crotch"], z)
        foot = smoothstep(Z["ankle"] + 0.025 * self.H, Z["ankle"] - 0.002, z)
        return norm_w({"Hips": hip, side + "UpLeg": (1 - hip) * (1 - knee), side + "Leg": (1 - hip) * knee * (1 - foot),
                       side + "Foot": knee * foot})

    # ================================================================== the arms
    def arm_r(self, t):
        return lerp_table(self.T_ARM, t)

    def w_arm(self, t, side):
        U, W = self.UPPER, self.WRIST
        sh = 0.55 * smoothstep(0.20 * U, -0.15 * U, t)
        fore = smoothstep(0.90 * U, 1.08 * U, t)
        hand = smoothstep(W - 0.012 * self.H / 1.7, W + 0.004, t)
        return norm_w({side + "Shoulder": sh, side + "Arm": (1 - sh) * (1 - fore),
                       side + "ForeArm": fore * (1 - hand), side + "Hand": hand})

    def arm_pt_of(self, A):
        def f(a, t):
            rq, rf = self.arm_r(t)
            return A["joint"] + A["d"] * t + A["q"] * (rq * math.cos(a)) + A["fr"] * (rf * math.sin(a))
        return f

    def arm_out_of(self, A):
        def f(p):
            r = p - A["joint"]
            return r - A["d"] * r.dot(A["d"])
        return f

    # ================================================================== the skin
    def build_skin(self):
        """torso, hips and legs as ONE skin (the torso's bottom ring splits into the two thighs), and the feet"""
        P = Part("skin_body", smooth=True)
        NB = self.NB
        TH = [2.0 * math.pi * j / NB for j in range(NB)]
        vid = {}
        for i, z in enumerate(self.ZB):
            for j, th in enumerate(TH):
                p = self.body_pt(th, z)
                vid[(i, j)] = P.v(p, self.w_body(p.x, p.y, p.z), (th, z))
        for i in range(len(self.ZB) - 1):
            for j in range(NB):
                k = (j + 1) % NB
                P.f((vid[(i, j)], vid[(i, k)], vid[(i + 1, k)], vid[(i + 1, j)]), "M_Skin")
        top = len(self.ZB) - 1                                  # the neck's top is inside the head: capped
        c = P.v(Vector((0.0, lerp_table(self.T_BODY, self.ZB[-1])[3], self.ZB[-1])), {"Head": 1.0})
        for j in range(NB): P.f((vid[(top, j)], vid[(top, (j + 1) % NB)], c), "M_Skin")
        tops = {}
        for sg in (1, -1):
            rings = [[P.v(self.leg_pt(sg, self.leg_phi(sg, k), z), self.w_leg(sg, z),
                          (math.radians(self.leg_phi(sg, k)), z)) for k in range(self.NLEG)] for z in self.LZ]
            for a, b in zip(rings[:-1], rings[1:]):
                for k in range(self.NLEG):
                    k2 = (k + 1) % self.NLEG
                    P.f((a[k], a[k2], b[k2], b[k]) if sg > 0 else (a[k], b[k], b[k2], a[k2]), "M_Skin")
            tops[sg] = rings[0]
            self.build_foot(P, sg, rings[-1])
        crotch = [vid[(0, j)] for j in range(NB)]
        half = NB // 2
        for sg in (1, -1):
            hr = [crotch[j % NB] for j in (range(0, half + 1) if sg > 0 else range(half, NB + 1))]
            self._bridge(P, hr, tops[sg])
        return P

    def _bridge(self, P, H, leg_ring):
        """stitch a half ring of the hips (front -> side -> back) to a thigh's top ring (its outer arc), and close
        the inner arc across to a crotch point"""
        Hp = [Vector(P.V[i]) for i in H]
        n = len(leg_ring)
        Lp = [Vector(P.V[i]) for i in leg_ring]
        i0 = min(range(n), key=lambda k: (Lp[k] - Hp[0]).length)
        i1 = min(range(n), key=lambda k: (Lp[k] - Hp[-1]).length)
        arcA = [(i0 + k) % n for k in range((i1 - i0) % n + 1)]
        arcB = [(i0 - k) % n for k in range((i0 - i1) % n + 1)]
        outer = max((arcA, arcB), key=lambda arc: sum(abs(Lp[k].x) for k in arc) / len(arc))
        inner = arcB if outer is arcA else arcA
        a, b = 0, 0
        L = [leg_ring[k] for k in outer]
        while a < len(H) - 1 or b < len(L) - 1:
            if b == len(L) - 1 or (a < len(H) - 1 and a / max(len(H) - 1, 1) <= b / max(len(L) - 1, 1)):
                tri = (H[a], H[a + 1], L[b]); a += 1
            else:
                tri = (H[a], L[b + 1], L[b]); b += 1
            self._face_out(P, tri)
        cz = Vector((0.0, 0.5 * (Hp[0].y + Hp[-1].y), Hp[0].z - 0.004))
        c = P.v(cz, {"Hips": 1.0})
        ins = [leg_ring[k] for k in inner]
        for k in range(len(ins) - 1):
            self._face_out(P, (ins[k], ins[k + 1], c))
        self._face_out(P, (H[0], c, ins[0]))
        self._face_out(P, (H[-1], ins[-1], c))

    def _face_out(self, P, tri):
        a, b, c = (Vector(P.V[i]) for i in tri)
        n = (b - a).cross(c - a)
        ctr = (a + b + c) / 3.0
        o = Vector((ctr.x, ctr.y, -0.5)) if ctr.z < self.Z["crotch"] else Vector((ctr.x, ctr.y, 0.0))
        if abs(ctr.x) < 0.15 * self.S["hip"][0] and ctr.z < self.Z["crotch"] + 0.004: o = Vector((0, 0, -1))
        P.f(tri if n.dot(o) >= 0 else (tri[0], tri[2], tri[1]), "M_Skin")

    # ---------------------------------------------------------------- the feet
    def foot_turn(self, sg, p, deg=7.0):
        ax, ay = sg * self.LEGX["ankle"], 0.030 * self.S["foot"][0] / 0.24
        a = math.radians(deg) * sg
        dx, dy = p[0] - ax, p[1] - ay
        return (ax + dx * math.cos(a) - dy * math.sin(a), ay + dx * math.sin(a) + dy * math.cos(a), p[2])

    def w_foot(self, sg, y):
        side = "Left" if sg > 0 else "Right"
        fl = self.S["foot"][0]
        toe = smoothstep(-0.30 * fl, -0.47 * fl, y)
        return norm_w({side + "Foot": 1 - toe, side + "ToeBase": toe})

    def foot_rows(self):
        fl, fw = self.S["foot"]
        k, w = fl / 0.245, fw / 0.090
        ah = self.Z["ankle"]
        return [(0.068 * k, 0.024 * w, 0.70 * ah), (0.050 * k, 0.031 * w, 0.98 * ah), (0.020 * k, 0.035 * w, 1.02 * ah),
                (-0.020 * k, 0.039 * w, 0.88 * ah), (-0.065 * k, 0.043 * w, 0.66 * ah), (-0.105 * k, 0.045 * w, 0.48 * ah),
                (-0.140 * k, 0.043 * w, 0.38 * ah), (-0.165 * k, 0.036 * w, 0.30 * ah)]

    def build_foot(self, P, sg, ankle_ring):
        cx = sg * self.LEGX["ankle"]
        rows = self.foot_rows()
        rings = []
        for y, hw, top in rows:
            ring = []
            for k in range(10):
                a = 2 * math.pi * k / 10
                zz = top * 0.5 + top * 0.5 * math.cos(a) + 0.002
                xx = cx + hw * math.sin(a) * (1.0 if math.cos(a) < 0.3 else 0.92)
                ring.append(P.v(self.foot_turn(sg, (xx, y, zz)), self.w_foot(sg, y)))
            rings.append(ring)
        fl = self.S["foot"][0]
        loft(P, rings, "M_Skin", cap0=P.v(self.foot_turn(sg, (cx, rows[0][0] + 0.008 * fl / 0.24, 0.45 * rows[0][2])),
                                          self.w_foot(sg, 0.08)),
             cap1=P.v(self.foot_turn(sg, (cx, rows[-1][0] - 0.013 * fl / 0.24, 0.45 * rows[-1][2])),
                      self.w_foot(sg, -0.5 * fl)))
        c = P.v(self.foot_turn(sg, (cx, 0.025 * fl / 0.24, 0.85 * self.Z["ankle"])), self.w_foot(sg, 0.02))
        n = len(ankle_ring)
        for k in range(n):
            P.f((ankle_ring[k], ankle_ring[(k + 1) % n], c), "M_Skin")

    # ---------------------------------------------------------------- arms and hands
    def arm_ring(self, P, A, t, grow, ns, w, prm=False):
        rq, rf = self.arm_r(t)
        c = A["joint"] + A["d"] * t
        out = []
        for k in range(ns):
            a = 2 * math.pi * k / ns
            p = c + A["q"] * ((rq + grow) * math.cos(a)) + A["fr"] * ((rf + grow) * math.sin(a))
            out.append(P.v(p, w, ((a + math.pi) % (2 * math.pi) - math.pi, t) if prm else None))
        return out

    def build_arm_skin(self, sg):
        side = "Left" if sg > 0 else "Right"
        A = self.ARMB[side]
        P = Part("skin_arm_" + side, smooth=True)
        ts = sorted({r[0] for r in self.T_ARM} | {self.UPPER * k / 6.0 for k in range(1, 6)}
                    | {self.UPPER + self.FORE * k / 6.0 for k in range(1, 6)})
        ts = [t for t in ts if t <= self.WRIST + 1e-6]
        rings = [self.arm_ring(P, A, t, 0.0, 14, self.w_arm(t, side), prm=True) for t in ts]
        loft(P, rings, "M_Skin", cap0=P.v(A["joint"] + A["d"] * (self.T_ARM[0][0] - 0.004), self.w_arm(-0.05, side)))
        self.hand(P, A, side, rings[-1])
        return P

    # ---------------------------------------------------------------- THE DONOR HAND (spec "hand_donor")
    def _hand_frame(self, A):
        """the wrist's frame as extract_hand.py stores the donor's: +Y along the hand, +Z the BACK of the hand
        (+q: the A-pose palm faces down and in), +X = Y x Z -- which is the FRONT on the left, the back on the right,
        so the right hand is the left one mirrored in X"""
        Y = A["d"].normalized(); Z = A["q"].normalized(); X = Y.cross(Z)
        return A["wrist"], X, Y, Z, (1.0 if A["sg"] > 0 else -1.0), self.S["hand"]

    def _donor_pt(self, A, lp):
        W0, X, Y, Z, mx, L = self._hand_frame(A)
        lx = float(lp[0]) - donor_hand()["wx"]                # her wrist centred on the forearm's axis
        return W0 + (X * (lx * mx) + Y * float(lp[1]) + Z * float(lp[2])) * L

    def _donor_paths(self, A):
        D = donor_hand()
        out = {}
        for f in ("Index", "Middle", "Ring", "Pinky", "Thumb"):
            out[f] = [self._donor_pt(A, D["j"]["%s%d" % (f, i)]) for i in (1, 2, 3)] +                      [self._donor_pt(A, D["j"]["%sTip" % f])]
        return out

    def _donor_hand(self, P, A, side, wrist_ring, mat):
        D = donor_hand()
        n = len(wrist_ring)                                 # the forearm closed at the wrist: the hand's stub is inside
        c = P.v(A["wrist"] + A["d"] * 0.004, {side + "ForeArm": 0.5, side + "Hand": 0.5})
        for j in range(n): P.f((wrist_ring[j], wrist_ring[(j + 1) % n], c), mat)
        names = D["names"]
        bone = {nm: side + ("Hand" if nm == "Hand" else "ForeArm" if nm == "ForeArm" else "Hand" + nm)
                for nm in names}
        base = len(P.V)
        for i, lp in enumerate(D["V"]):
            w = {}
            for k in range(4):
                if D["W"][i, k] > 0: w[bone[names[D["WI"][i, k]]]] = w.get(bone[names[D["WI"][i, k]]], 0.0) + float(D["W"][i, k])
            P.v(self._donor_pt(A, lp), norm_w(w))
        mirror = A["sg"] < 0
        for f in D["F"]:
            idx = [base + int(i) for i in f]
            P.f(tuple(reversed(idx)) if mirror else tuple(idx), mat)

    def finger_paths(self, A):
        """each finger's joints from the knuckles, in a relaxed curl toward the palm"""
        if self.S.get("hand_donor"): return self._donor_paths(A)
        k = self.S["hand"] / 0.185                               # an adult woman's hand, wrist crease to fingertip
        d, q, fr, J = A["d"], A["q"], A["fr"], A["joint"]
        W = self.WRIST
        knuck = J + d * (W + 0.090 * k)
        out = {}
        # chunky fingers side by side (a drawn hand reads as a few solid masses, never sticks)
        # slimmer than last round ("hands look clunky"): a little air between the fingers, a touch longer
        for name, off, segs, spread in (("Index", 0.0255, (0.041, 0.025, 0.020), 0.02),
                                        ("Middle", 0.0085, (0.044, 0.028, 0.021), 0.0),
                                        ("Ring", -0.0085, (0.042, 0.026, 0.020), -0.02),
                                        ("Pinky", -0.0245, (0.033, 0.020, 0.018), -0.04)):
            base = knuck + fr * (off * k) - d * (0.004 * k if name in ("Index", "Pinky") else 0.0)
            if name == "Pinky": base = base - d * 0.008 * k
            dcur = (d + fr * spread).normalized()
            pts, cur = [base], base
            for s in segs:
                dcur = (dcur - q * 0.22).normalized()
                cur = cur + dcur * (s * k)
                pts.append(cur)
            out[name] = pts
        # the thumb lies DOWN along the front of the index, turned a little toward the palm (it stuck out sideways
        # like a peg: jacob, 10-18, "hands are no good")
        tb = J + d * (W + 0.026 * k) + fr * (0.030 * k) - q * (0.006 * k)
        tdir = (d * 0.85 + fr * 0.30 - q * 0.40).normalized()
        pts, cur, dcur = [tb], tb, tdir
        for s in (0.034, 0.028, 0.024):
            dcur = (dcur + d * 0.10 - q * 0.18).normalized()
            cur = cur + dcur * (s * k)
            pts.append(cur)
        out["Thumb"] = pts
        return out

    def hand(self, P, A, side, wrist_ring, mat="M_Skin"):
        """the palm lofted on from the wrist's ring and ROUNDED over at the knuckles, and five fingers that grow out
        of it. jacob, 10-18, three rounds: "hands are no good" / "clunky" / "not good, too clunky" -- each finger was
        four hexagonal rings, one ON each joint: straight prisms that folded at a single ring like bent pipe, cut
        flat at the tip, butted onto a slab. A finger is now a smooth tube: 10 sides, rings all along each bone,
        weight shared only near a knuckle, a rounded tip, its root sunk inside the palm (no seam).
        A fourth "still looking weird" -> spec "hand_donor": Anima's own hand, refitted (extract_hand.py)."""
        if self.S.get("hand_donor"): return self._donor_hand(P, A, side, wrist_ring, mat)
        k = self.S["hand"] / 0.185
        d, q, fr, J = A["d"], A["q"], A["fr"], A["joint"]
        W = self.WRIST
        ns = len(wrist_ring)
        rings = [wrist_ring]
        for t, hq, hf in ((W + 0.030 * k, 0.0165 * k, 0.034 * k), (W + 0.062 * k, 0.0150 * k, 0.038 * k),
                          (W + 0.086 * k, 0.0135 * k, 0.038 * k), (W + 0.094 * k, 0.0110 * k, 0.033 * k),
                          (W + 0.099 * k, 0.0065 * k, 0.022 * k)):
            w = self.w_arm(min(t, W + 0.02), side)
            ring = []
            for j_ in range(ns):
                a = 2 * math.pi * j_ / ns
                ca, sa = math.cos(a), math.sin(a)
                # a rounded rectangle across the palm (superellipse), not an ellipse: the back of a hand is flat
                ring.append(P.v(J + d * t + q * (hq * math.copysign(abs(ca) ** 0.8, ca))
                                + fr * (hf * math.copysign(abs(sa) ** 0.7, sa)), w))
            rings.append(ring)
        loft(P, rings, mat, cap1=P.v(J + d * (W + 0.101 * k), {side + "Hand": 1.0}))
        fp = self.finger_paths(A)
        A["fingers"] = fp
        NS = 10
        for name, pts in fp.items():
            bones = ["%sHand%s%d" % (side, name, i) for i in (1, 2, 3)]
            r0 = (0.0130 if name == "Thumb" else {"Index": 0.0098, "Middle": 0.0101, "Ring": 0.0095,
                                                "Pinky": 0.0084}[name]) * k
            rad = [r0 * (1.15 if name == "Thumb" else 1.0), r0 * 0.92, r0 * 0.84, r0 * 0.74]
            # the centreline: the root sunk 1.3 radii back inside the palm, then 5 stations along each bone
            st = []                                         # (point, radius, weights)
            d0 = (pts[1] - pts[0]).normalized()
            st.append((pts[0] - d0 * rad[0] * 1.3, rad[0], {side + "Hand": 1.0}))
            for b in range(3):
                prev = side + "Hand" if b == 0 else bones[b - 1]
                for t in (0.0, 0.18, 0.40, 0.65, 0.88):
                    c = pts[b] + (pts[b + 1] - pts[b]) * t
                    r = rad[b] + (rad[b + 1] - rad[b]) * t
                    if t < 0.18: w = {prev: 0.5 * (1 - t / 0.18), bones[b]: 0.5 + 0.5 * t / 0.18}
                    else: w = {bones[b]: 1.0}
                    st.append((c, r, norm_w(w)))
            st.append((pts[3], rad[3], {bones[2]: 1.0}))
            # rings by parallel transport (no twist), slightly flattened across the nail
            prev_u = None
            rings = []
            for i_, (c, r, w) in enumerate(st):
                a_ = st[max(i_ - 1, 0)][0]; b_ = st[min(i_ + 1, len(st) - 1)][0]
                tg = (b_ - a_).normalized()
                u = (prev_u - tg * prev_u.dot(tg)).normalized() if prev_u is not None else tg.cross(q).normalized()
                prev_u = u
                v = tg.cross(u).normalized()
                rings.append([P.v(c + (u * math.cos(2 * math.pi * j_ / NS) + v * (0.86 * math.sin(2 * math.pi * j_ / NS))) * r, w)
                              for j_ in range(NS)])
            loft(P, rings, mat, cap0=P.v(st[0][0] + (st[0][0] - st[1][0]).normalized() * rad[0] * 0.5, st[0][2]))
            # the fingertip: a dome
            c, r, w = st[-1]
            tg = (st[-1][0] - st[-2][0]).normalized()
            last = rings[-1]
            for f_ in (0.55, 0.88):
                cc = c + tg * r * f_
                rr = math.sqrt(max(0.0, 1 - f_ * f_))
                ring = [P.v(cc + (Vector(P.V[last[j_]]) - c) * rr, w) for j_ in range(NS)]
                loft(P, [last, ring], mat)
                last = ring
            tip = P.v(c + tg * r, w)
            for j_ in range(NS): P.f((tip, last[j_], last[(j_ + 1) % NS]), mat)

    # ================================================================== the head
    def head_u(self, z):
        return (z - self.Z["chin"]) / self.S["head_h"]

    def _tmpl_u(self, u):
        """the head's u -> the template's: a child's face is a smaller share of the head"""
        bu = self.S["head"].get("brow_u", FACE_BROW_U)
        # a monotone cubic through (0,0), (brow_u, FACE_BROW_U), (1,1): no kink at the brow (a kink is a crease)
        return smooth_table(((0.0, 0.0), (bu, FACE_BROW_U), (1.0, 1.0)), u, cache=False)[0]

    def head_pt(self, theta, z, plain=False):
        hd = self.S["head"]
        u = min(1.0, max(0.0, self.head_u(z)))
        tu = self._tmpl_u(u)
        hw, yf, yb, p = smooth_table(HEAD_T, tu)
        jaw = hd.get("jaw", 1.0)                                # <1 a narrower jaw (a child's, a woman's)
        if tu < 0.40: hw *= 1.0 - (1.0 - jaw) * smoothstep(0.40, 0.10, tu)
        if "jaw_p" in hd:                                       # a CUT jaw: squarer sections low in the face
            p = p + (hd["jaw_p"] - p) * smoothstep(0.42, 0.12, tu)
        hw *= self.HB; yf *= self.HD; yb *= self.HD
        cy = self.head_cy
        x, y = ring_xy(theta, hw, -yf, yb, cy, p)
        c, s = math.cos(theta), math.sin(theta)
        if c > 0 and not plain:              # plain: the skull and jaw alone (the toon look shades by it)
            a = math.degrees(math.atan2(s, c))                  # 0 the middle of the face, +-90 the sides
            bu = hd.get("brow_u", FACE_BROW_U)
            nose = hd.get("nose", 0.020)
            eu = hd.get("eye_u", bu - 0.115)
            ub, ut, un = bu - 0.02, eu - 0.165 * (bu / FACE_BROW_U), eu - 0.215 * (bu / FACE_BROW_U)
            # the nose: a ridge from between the brows out to its tip, then back in to its base
            if un - 0.03 < u < ub:
                if u >= ut: prof = 0.20 + 0.80 * ((ub - u) / (ub - ut)) ** 1.3
                else: prof = (u - (un - 0.03)) / (ut - (un - 0.03))
                wd = 8.5 + 7.0 * smoothstep(ub, ut, u)
                y -= nose * prof * _g(a, wd)
                # the wings of the nose either side of its tip
                nwg = hd.get("nose_wing", 11.0)
                y -= 0.30 * nose * _g(u - (ut + 0.01), 0.025) * (_g(a - nwg, 6.0) + _g(a + nwg, 6.0))
            hh = self.S["head_h"]
            eye_a = hd.get("eye_a", 26.0)
            for sg in (1, -1):
                # the eye socket under the brow, and the brow over it
                y += hd.get("socket", 0.0060) * hh / 0.225 * _g(a - sg * eye_a, 12.0) * _g(u - eu, 0.040)
                # the cheekbone: a plane the cel shadow can turn on
                y -= hd.get("cheekbone", 0.0) * _g(a - sg * 46.0, 12.0) * _g(u - (eu - 0.08), 0.05)
                y -= hd.get("brow", 0.0035) * hh / 0.225 * _g(a - sg * (eye_a + 2.0), 15.0) * _g(u - (eu + 0.065), 0.030)
                # cheeks: a child's are full
                y -= hd.get("cheek", 0.002) * _g(a - sg * 36.0, 16.0) * _g(u - (eu - 0.12), 0.07)
            # the mouth's mound, the lips, and the chin
            mu = hd.get("mouth_u", un - 0.095 * (bu / FACE_BROW_U))
            lips = hd.get("lips", 1.0)
            y -= 0.0045 * hh / 0.225 * _g(a, 22.0) * _g(u - mu, 0.045)
            y -= lips * 0.0020 * hh / 0.225 * _g(a, 14.0) * (_g(u - (mu + 0.018), 0.012) + _g(u - (mu - 0.015), 0.012))
            y += lips * 0.0020 * hh / 0.225 * _g(a, 18.0) * _g(u - mu, 0.006)     # the line between the lips
            y -= hd.get("chin", 0.0035) * _g(a, 18.0) * _g(u - 0.06, 0.035)
            self._face = (eu, mu, eye_a)
        return Vector((x, y, z))

    def marks(self):
        """the face's landmarks in (angle, head-height share): eye line, eye angle, nose tip and base, mouth"""
        hd = self.S["head"]
        bu = hd.get("brow_u", FACE_BROW_U)
        eu = hd.get("eye_u", bu - 0.115)
        ut, un = eu - 0.165 * (bu / FACE_BROW_U), eu - 0.215 * (bu / FACE_BROW_U)
        mu = hd.get("mouth_u", un - 0.095 * (bu / FACE_BROW_U))
        return {"eu": eu, "eye_a": hd.get("eye_a", 26.0), "ut": ut, "un": un, "mu": mu, "bu": bu}

    def face_xyz(self, a_deg, u, off=0.0):
        """the head surface at angle a (0 the middle of the face) and head-height share u, pushed out by off"""
        z = self.Z["chin"] + u * self.S["head_h"]
        p = self.head_pt(math.radians(a_deg), z)
        o = Vector((p.x, p.y - self.head_cy, 0.0)).normalized()
        return p + o * off

    def build_head(self):
        P = Part("head", smooth=True)
        NH = 72
        us = sorted(set([i / 72.0 for i in range(73)] + [0.012, 0.988, 0.995]))
        rings = []
        for u in us[1:-1]:
            z = self.Z["chin"] + u * self.S["head_h"]
            rings.append([P.v(self.head_pt(2 * math.pi * j / NH, z), {"Head": 1.0}) for j in range(NH)])
        chin = P.v(self.head_pt(0.0, self.Z["chin"] + 0.004 * self.S["head_h"]) + Vector((0, 0.010, -0.003)), {"Head": 1.0})
        crown = P.v((0.0, self.head_cy + 0.06 * self.HD, self.Z["crown"]), {"Head": 1.0})
        loft(P, rings, "M_Skin", cap0=chin, cap1=crown)
        self.build_ears(P)
        return P

    def build_ears(self, P):
        hd = self.S["head"]
        hh = self.S["head_h"]
        eu = hd.get("eye_u", hd.get("brow_u", FACE_BROW_U) - 0.115)
        k = hd.get("ear", 1.0) * hh / 0.225
        for sg in (1, -1):
            zc_ = self.Z["chin"] + (eu - 0.075) * hh
            side = self.head_pt(math.radians(sg * 90.0), zc_)
            cx, cy0 = side.x - sg * 0.004, side.y + 0.010
            base, rim, lip = [], [], []
            for j in range(12):
                a = 2 * math.pi * j / 12
                dy, dz = 0.016 * k * math.cos(a), 0.031 * k * math.sin(a) * (1.0 if math.sin(a) > 0 else 0.85)
                base.append(P.v((cx - sg * 0.004, cy0 + dy * 0.6, zc_ + dz * 0.80), {"Head": 1.0}))
                stand = 0.006 * k + 0.006 * k * max(0.0, math.cos(a))      # cos > 0: the back edge
                rim.append(P.v((cx + sg * stand, cy0 + 0.002 + dy * 1.15, zc_ + dz * 1.05), {"Head": 1.0}))
                lip.append(P.v((cx + sg * stand * 0.55, cy0 + 0.002 + dy * 0.80, zc_ + dz * 0.78), {"Head": 1.0}))
            inner = P.v((cx + sg * 0.001 * k, cy0 + 0.002, zc_), {"Head": 1.0})
            for j in range(12):
                j2 = (j + 1) % 12
                quads = ((base[j], base[j2], rim[j2], rim[j]), (rim[j], rim[j2], lip[j2], lip[j]))
                for qd in quads:
                    P.f(qd if sg > 0 else tuple(reversed(qd)), "M_Skin")
                tri = (lip[j], lip[j2], inner)
                P.f(tri if sg > 0 else tuple(reversed(tri)), "M_Skin")

    def eye_centres(self):
        hd = self.S["head"]
        eu = hd.get("eye_u", hd.get("brow_u", FACE_BROW_U) - 0.115)
        out = []
        r = hd.get("eye_r", 0.0118) * self.S["head_h"] / 0.225
        for sg in (1, -1):
            p = self.face_xyz(sg * hd.get("eye_a", 26.0), eu)
            o = Vector((p.x, p.y - self.head_cy, 0.0)).normalized()
            out.append((sg, p - o * (r * 0.62), r))
        return out

    def build_eyes(self):
        """two eyeballs set into the sockets: white, an iris and a pupil painted on as rings of the ball itself"""
        P = Part("eyes", smooth=True)
        for sg, c, r in self.eye_centres():
            fwd = Vector((sg * 0.08, -1.0, 0.0)).normalized()
            up = Vector((0, 0, 1))
            rt = fwd.cross(up).normalized(); up = rt.cross(fwd).normalized()
            rings = []
            NR, NS = 12, 16
            for i in range(1, NR):
                t = math.pi * i / NR                           # 0 at the front pole
                ring = []
                for j in range(NS):
                    a = 2 * math.pi * j / NS
                    p = c + (fwd * math.cos(t) + (rt * math.cos(a) + up * math.sin(a)) * math.sin(t)) * r
                    ring.append(P.v(p, {"Head": 1.0}))
                rings.append((t, ring))
            front = P.v(c + fwd * r, {"Head": 1.0})
            back = P.v(c - fwd * r, {"Head": 1.0})
            for j in range(NS):
                P.f((front, rings[0][1][j], rings[0][1][(j + 1) % NS]), "M_Eye", ("swatch", "pupil"))
            for (t0, a), (t1, b) in zip(rings[:-1], rings[1:]):
                sw = "pupil" if t1 <= 0.30 else ("iris" if t1 <= 0.62 else "eye_white")
                for j in range(NS):
                    j2 = (j + 1) % NS
                    P.f((a[j], b[j], b[j2], a[j2]), "M_Eye", ("swatch", sw))
            for j in range(NS):
                P.f((back, rings[-1][1][(j + 1) % NS], rings[-1][1][j]), "M_Eye", ("swatch", "eye_white"))
        return P

    # ================================================================== the hair
    def _skull(self, theta, z):
        return self.head_pt(theta, min(z, self.Z["crown"] - 0.004))

    def _hair_out(self, theta, z):
        p = self.head_pt(theta, min(z, self.Z["crown"] - 0.01))
        o = Vector((p.x, p.y - self.head_cy, 0.0))
        o = o.normalized() if o.length > 1e-9 else Vector((0, 0, 1))
        up = smoothstep(self.Z["chin"] + 0.80 * self.S["head_h"], self.Z["crown"], z)
        return (o * (1 - up) + Vector((0, 0, 1)) * up).normalized()

    def build_hair(self):
        """a hair shell over the skull: spec hair = {line_front, line_temple, line_nape (head-height shares),
        thick (m), crown (extra on top), bun (radius, or 0), fringe (how far down the forehead)}"""
        hr = self.S["hair"]
        hh = self.S["head_h"]
        P = Part("hair", smooth=True)
        coil = hr.get("coil", 0.0)                       # coiled hair: the shell's surface in rounded bumps
        N = 120 if coil else 32
        TH = [2 * math.pi * (j + 0.5) / N for j in range(N)]
        zc, zt = self.Z["chin"], self.Z["crown"]

        hd = self.S["head"]
        eu = hd.get("eye_u", hd.get("brow_u", FACE_BROW_U) - 0.115)
        f_, t_, n_ = hr["line_front"], hr["line_temple"], hr["line_nape"]
        sb = hr.get("sideburn", eu - 0.06)                    # in front of the ear, down to about the cheekbone
        oe = hr.get("over_ear", eu + 0.10)                    # the line arcs over the ear's top
        # the hairline round the head: (degrees from the middle of the face, head-height share)
        LINE = [(0.0, f_), (28.0, f_ - 0.015), (48.0, t_), (64.0, 0.5 * (t_ + sb)), (72.0, sb), (80.0, sb + 0.01),
                (86.0, oe), (102.0, oe), (112.0, 0.5 * (oe + n_) + 0.04), (135.0, n_ + 0.05), (180.0, n_)]

        def bottom(th):
            a = abs(math.degrees(math.atan2(math.sin(th), math.cos(th))))
            z = zc + lerp_table(LINE, a)[0] * hh
            if coil: z -= coil * 1.4 * abs(math.sin(th * 11.0))           # a scalloped hairline
            return z

        def thick(th, z):
            c = math.cos(th)
            return hr["thick"] * (1.0 + 0.25 * max(0.0, -c)) + hr.get("crown", 0.0) * smoothstep(zc + 0.78 * hh, zt, z)
        ZR = [0.0, 0.04, 0.10, 0.18, 0.28, 0.42, 0.60, 0.76, 0.88, 0.96]
        if coil: ZR = [0.0, 0.02] + [0.04 + 0.92 * k / 27.0 for k in range(28)]
        rings = []
        for f in ZR:
            ring = []
            for th in TH:
                zb = bottom(th)
                z = zb + (zt - zb) * f
                # thin at the hairline (hair grows out of the skin there), full a little way in: no helmet's rim
                q = self._skull(th, z) + self._hair_out(th, z) * (0.0015 + thick(th, z) * min(1.0, f * 3.2) ** 0.7)
                if coil:                          # rounded lumps, offset row to row (a coil's popcorn, not a grid)
                    b = abs(math.sin(th * 11.0 + f * 9.0)) * abs(math.sin(f * 21.0 + 1.7 * math.sin(th * 5.0)))
                    q = q + self._hair_out(th, z) * coil * b ** 0.6 * min(1.0, f * 8.0)
                ring.append(P.v(q, {"Head": 1.0}))
            rings.append(ring)
        crown = P.v((0.0, self.head_cy + 0.05 * self.HD, zt + thick(0.0, zt) * 0.95), {"Head": 1.0})
        loft(P, rings, "M_Hair", cap1=crown)
        inner = [P.v(self._skull(th, bottom(th) + 0.010) + self._hair_out(th, bottom(th)) * 0.001, {"Head": 1.0})
                 for th in TH]
        for j in range(N):
            k = (j + 1) % N
            P.f((inner[j], inner[k], rings[0][k], rings[0][j]), "M_Hair")
        b = hr.get("bun", 0.0)
        if b > 0:                                               # a bun at the back of the head
            bz = zc + hr.get("bun_u", 0.55) * hh
            back = self._skull(math.pi, bz) + Vector((0, b * 0.75 + hr["thick"], 0))
            rings, NBn = [], 14
            for i in range(1, 10):
                t = math.pi * i / 10
                rings.append([P.v(back + Vector((b * math.sin(t) * math.cos(2 * math.pi * j / NBn),
                                                 -b * 0.85 * math.cos(t),
                                                 b * 0.9 * math.sin(t) * math.sin(2 * math.pi * j / NBn))),
                                  {"Head": 1.0}) for j in range(NBn)])
            loft(P, rings, "M_Hair", cap0=P.v(back + Vector((0, -b * 0.85, 0)), {"Head": 1.0}),
                 cap1=P.v(back + Vector((0, b * 0.85, 0)), {"Head": 1.0}))
        return P

    # ================================================================== the rig
    def bones(self):
        """[(name, head, tail, parent, space)] -- Mixamo names"""
        Z, H = self.Z, self.H
        cyb = self.S.get("back_cy", 0.0)
        hipj = Z["hipj"]
        zw, zb, zn = Z["waist"], Z["bust"], Z["neckbase"]
        s1 = zw + 0.45 * (zb - zw)
        s2 = zb - 0.05 * (zb - zw)
        B = [("Hips", (0, 0.006, hipj), (0, 0.005, zw), None, "body"),
             ("Spine", (0, 0.005, zw), (0, 0.004 + cyb * 0.3, s1), "Hips", "body"),
             ("Spine1", (0, 0.004 + cyb * 0.3, s1), (0, 0.004 + cyb * 0.6, s2), "Spine", "body"),
             ("Spine2", (0, 0.004 + cyb * 0.6, s2), (0, 0.012 + cyb, zn - 0.006 * H), "Spine1", "body"),
             ("Neck", (0, 0.012 + cyb, zn - 0.006 * H), (0, 0.018 + cyb, Z["neck_top"] - 0.02 * H), "Spine2", "body"),
             ("Head", (0, 0.018 + cyb, Z["neck_top"] - 0.02 * H), (0, 0.018 + cyb, Z["crown"]), "Neck", "body")]
        for sg, side in ((1, "Left"), (-1, "Right")):
            A = self.ARMB[side]
            B.append((side + "Shoulder", (sg * 0.13 * self.joint_x, 0.006 + cyb, Z["shoulder"] - 0.004 * H),
                      tuple(A["joint"] - Vector((sg * 0.012, 0, -0.003))), "Spine2", "body"))
            B.append((side + "Arm", tuple(A["joint"]), tuple(A["elbow"]), side + "Shoulder", "body"))
            B.append((side + "ForeArm", tuple(A["elbow"]), tuple(A["wrist"]), side + "Arm", "body"))
            k = self.S["hand"] / 0.185
            B.append((side + "Hand", tuple(A["wrist"]), tuple(A["joint"] + A["d"] * (self.WRIST + 0.090 * k)),
                      side + "ForeArm", "body"))
            for name, pts in self.finger_paths(A).items():
                prev = side + "Hand"
                for i in (1, 2, 3):
                    nm = "%sHand%s%d" % (side, name, i)
                    # +Z toward the palm (the palm faces -q, down and in): a positive curl closes the hand
                    # (the THUMB closes across the palm toward the little finger: its +Z there, not straight in)
                    rz = -A["q"] if name != "Thumb" else (-A["q"] * 0.6 - A["fr"] * 0.8)
                    B.append((nm, tuple(pts[i - 1]), tuple(pts[i]), prev, "body", tuple(rz))); prev = nm
            hx, kx, ax = sg * self.LEGX["hip"], sg * self.LEGX["knee"], sg * self.LEGX["ankle"]
            B.append((side + "UpLeg", (hx, 0.0, hipj), (kx, -0.006, Z["knee"]), "Hips", "body"))
            B.append((side + "Leg", (kx, -0.006, Z["knee"]), (ax, 0.026, Z["ankle"]), side + "UpLeg", "body"))
            fl = self.S["foot"][0]
            ball = self.foot_turn(sg, (ax, -0.40 * fl, 0.022 * fl / 0.24))
            toe = self.foot_turn(sg, (ax, -0.70 * fl, 0.022 * fl / 0.24))
            B.append((side + "Foot", (ax, 0.026, Z["ankle"]), ball, side + "Leg", "body"))
            B.append((side + "ToeBase", ball, toe, side + "Foot", "body"))
        return B

    def outward(self, p, part_name):
        if part_name.startswith(("head", "eyes", "hair", "face")):
            return Vector((p.x, p.y - self.head_cy, p.z - (self.Z["chin"] + 0.5 * self.S["head_h"])))
        if part_name.startswith("skin_arm"):
            A = self.ARMB["Left" if p.x > 0 else "Right"]
            r = p - A["joint"]
            return r - A["d"] * r.dot(A["d"])
        if p.z > self.Z["crotch"]: return self.torso_out(p)
        return self.leg_out(1 if p.x > 0 else -1)(p)

    def report(self):
        Z, H, hh = self.Z, self.H, self.S["head_h"]
        return ("%s: %.2f m, head %.3f m = %.2f heads; crotch %.2f (%.0f%%), shoulders %.2f wide, hips %.2f wide"
                % (self.name, H, hh, H / hh, Z["crotch"], 100 * Z["crotch"] / H, 2 * self.S["shoulder_hw"],
                   2 * self.S["hip"][0]))
