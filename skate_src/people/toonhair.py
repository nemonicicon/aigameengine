"""toonhair.py -- THE WORLD AT THE END OF TIME's hair: sculpted masses that MOVE (jacob, 10-18: "we need to make use
of hair physics"). A swinging lock carries its own Hair_* chain and a bob hangs on a ring of them; the club engine
and Godot's ClubHair mine any bone named *hair* whose parent is not (crowd_kms.c H1: 32 chains a body, 14 bones a
chain), and swing them with inertia, drag and world-down gravity.

    hair_part(person) -> Part "hair"; person.CHAINS gets [(name, head, tail, parent, space)] for the rig

spec hair: the shell (person.build_hair's keys) plus
    "locks":   [(a0, u0, a1, u1, width, thick, lift[, tip_out[, swing]])] -- swing 0 = stiff (a cowlick), default 1
    "curtain": {"open", "u_top", "u_cut", "front_drop", "thick", "flare", "chains"} -- a bob
    "curls":   {"r": radius, "n": how many} -- coily hair: small rounded masses over the shell and its edge
"""
import math

from mathutils import Vector

import atelier as at
from atelier import norm_w, Part

HEAD = {"Head": 1.0}


def _chain(person, name, joints, parent="Head"):
    for k in range(len(joints) - 1):
        person.CHAINS.append(("%s%d" % (name, k + 1), tuple(joints[k]), tuple(joints[k + 1]),
                              parent if k == 0 else "%s%d" % (name, k), "body"))


def wedge(P, base, tip, width, thick, across, mat="M_Hair", ws=None, bend=None):
    """a sculpted lock: a flat wedge from a wide base to a point. ws = weights for its four stations
    (base, 40%, 75%, tip)"""
    ws = ws or [HEAD] * 4
    d = (tip - base).normalized()
    ac = across - d * across.dot(d); ac.normalize()
    nm = d.cross(ac).normalized()
    secs = []
    for (t, wsc, tsc), w in zip(((0.0, 0.5, 0.5), (0.40, 0.42, 0.45), (0.75, 0.24, 0.30)), ws[:3]):
        c = base + (tip - base) * t
        if bend is not None: c = c + bend * math.sin(math.pi * t)
        secs.append([P.v(c + ac * (width * wsc), w), P.v(c + nm * (thick * tsc), w), P.v(c - ac * (width * wsc), w),
                     P.v(c - nm * (thick * tsc), w)])
    for A, B in zip(secs[:-1], secs[1:]):
        for j in range(4):
            P.f((A[j], A[(j + 1) % 4], B[(j + 1) % 4], B[j]), mat)
    c0 = P.v(base - d * 0.004, ws[0])
    for j in range(4): P.f((c0, secs[0][(j + 1) % 4], secs[0][j]), mat)
    tp = P.v(tip, ws[3])
    for j in range(4): P.f((tp, secs[-1][j], secs[-1][(j + 1) % 4]), mat)


def locks(person, P):
    """spec hair["locks"]: a lock from the scalp at (a0, u0) to its tip at (a1, u1), standing `lift` off the head
    at its middle; one that SWINGS gets a two-bone chain, base -> middle -> tip"""
    for n, lk in enumerate(person.S["hair"].get("locks", ())):
        a0, u0, a1, u1, wd, th, lift = lk[:7]
        tip_out = lk[7] if len(lk) > 7 else 0.0              # a lock that STANDS UP (a cowlick): its tip pushed out
        swing = lk[8] if len(lk) > 8 else (0.0 if tip_out > 0 else 1.0)
        base = person.face_xyz(a0, min(u0, 0.97), person.S["hair"]["thick"] * 0.6)
        tip = person.face_xyz(a1, min(u1, 0.97), 0.003 + th * 0.4 + tip_out * 0.45) + Vector((0, 0, tip_out * 0.9))
        mid = person.face_xyz(0.5 * (a0 + a1), min(0.5 * (u0 + u1), 0.97), 0.0)
        o = Vector((mid.x, mid.y - person.head_cy, 0.0)).normalized()
        across = Vector((math.cos(math.radians(0.5 * (a0 + a1))), math.sin(math.radians(0.5 * (a0 + a1))), 0.0))
        bend = o * lift
        ws = None
        if swing > 0:
            nm = "Hair_Lock_%d_" % n
            jm = base + (tip - base) * 0.5 + bend
            _chain(person, nm, [base, jm, tip])
            b1, b2 = nm + "1", nm + "2"
            ws = [norm_w({"Head": 0.6, b1: 0.4}), {b1: 1.0}, norm_w({b1: 0.35, b2: 0.65}), {b2: 1.0}]
        wedge(P, base, tip, wd, th, across, ws=ws, bend=bend)


def curtain(person, P, cut):
    """hair that HANGS below the skull (a bob): a solid sheet round the sides and back, falling straight from the
    head's widest ring to a cut line, on a ring of chains (cut["chains"], default 8) so it swings as hair does"""
    hh, zc = person.S["head_h"], person.Z["chin"]
    a0 = cut["open"]
    NA, NZ = 28, 9
    angs = [a0 + (360.0 - 2 * a0) * i / NA for i in range(NA + 1)]

    def radial(a, u):
        """the head's widest radius at angle a from u up to its brow, and the direction"""
        best, dirv = 0.0, None
        for uu in [u + (0.72 - u) * k / 6.0 for k in range(7)]:
            q = person.head_pt(math.radians(a), zc + max(0.0, uu) * hh, plain=True)
            r = Vector((q.x, q.y - person.head_cy, 0.0))
            if r.length > best: best, dirv = r.length, r.normalized()
        return best, dirv

    def ucut_at(a):
        frontness = max(0.0, math.cos(math.radians(a)))
        return cut["u_cut"] - cut.get("front_drop", 0.0) * frontness ** 0.6
    # THE CHAINS: evenly round the curtain, each from a little below its top (the top stays on the head) to the hem
    NC = cut.get("chains", 8)
    ch_a = [a0 + (360.0 - 2 * a0) * (k + 0.5) / NC for k in range(NC)]
    ch_j = []
    for k, a in enumerate(ch_a):
        # ⛔ A BOB IS STIFF HAIR: chains over its whole drop let the curtain flap 10 cm on a 13 cm bob in a run and
        # swing across her face. Only the LOWER half swings (its root half way down to the cut)
        u_root = ucut_at(a) + cut.get("swing_share", 0.55) * (cut["u_top"] - ucut_at(a))
        us = [u_root + (ucut_at(a) - u_root) * i / 3.0 for i in range(4)]
        js = []
        for u in us:
            r, dv = radial(a, u)
            js.append(Vector((0.0, person.head_cy, zc + u * hh)) + dv * (r + cut["thick"] * 0.5))
        _chain(person, "Hair_StiffBob_%d_" % k, js)
        ch_j.append(js)

    def w_at(a, p):
        """across: the two nearest chains by angle; down: along each chain's joints (the top fades into the head)"""
        if a <= ch_a[0]: pair = [(0, 1.0)]
        elif a >= ch_a[-1]: pair = [(NC - 1, 1.0)]
        else:
            k = max(i for i in range(NC) if ch_a[i] <= a)
            s = (a - ch_a[k]) / (ch_a[k + 1] - ch_a[k])
            pair = [(k, 1.0 - s), (k + 1, s)]
        w = {}
        for k, f in pair:
            # the two chains beside the face swing half as far: half of them is held by the head
            hold = 0.5 if k in (0, NC - 1) else 0.0
            for b, v in at.chain_weights(p, ch_j[k], "Hair_StiffBob_%d_" % k, HEAD, fade=0.03).items():
                if b == "Head": w[b] = w.get(b, 0.0) + v * f
                else:
                    w[b] = w.get(b, 0.0) + v * f * (1.0 - hold)
                    w["Head"] = w.get("Head", 0.0) + v * f * hold
        return norm_w(w)
    outer, inner = [], []
    for a in angs:
        ucut = ucut_at(a)
        oc, ic = [], []
        for j in range(NZ + 1):
            t = j / NZ
            u = cut["u_top"] + (ucut - cut["u_top"]) * t
            r, dv = radial(a, u)
            th = cut["thick"] * (0.6 + 0.4 * t)
            fl = cut.get("flare", 0.0) * t ** 2
            z = zc + u * hh
            base = Vector((0.0, person.head_cy, z))
            po = base + dv * (r + th + fl)
            oc.append(P.v(po, w_at(a, po)))
            # the inner layer hugs the SKULL at the top (no gap to look down into), the widest line below it
            q = person.head_pt(math.radians(a), z, plain=True)
            rs = Vector((q.x, q.y - person.head_cy, 0.0)).length
            ri = rs + (r - rs) * min(1.0, t * 3.0)
            pi_ = base + dv * (ri + 0.0015 + fl * 0.6)
            ic.append(P.v(pi_, w_at(a, pi_)))
        outer.append(oc); inner.append(ic)
    for i in range(NA):
        for j in range(NZ):
            P.f((outer[i][j], outer[i + 1][j], outer[i + 1][j + 1], outer[i][j + 1]), "M_Hair")
            P.f((inner[i][j], inner[i][j + 1], inner[i + 1][j + 1], inner[i + 1][j]), "M_Hair")
        P.f((outer[i][NZ], outer[i + 1][NZ], inner[i + 1][NZ], inner[i][NZ]), "M_Hair")      # the hem
        P.f((outer[i][0], inner[i][0], inner[i + 1][0], outer[i + 1][0]), "M_Hair")          # the top, closed
    for i in (0, NA):                                                                       # the front edges
        for j in range(NZ):
            q = (outer[i][j], outer[i][j + 1], inner[i][j + 1], inner[i][j])
            P.f(q if i == 0 else tuple(reversed(q)), "M_Hair")


def curls(person, P):
    """coily hair: small rounded masses scattered over the shell and its hairline, so the silhouette is soft and
    bumpy instead of a smooth cap"""
    c = person.S["hair"].get("curls")
    if not c: return
    hr = person.S["hair"]
    r, n = c["r"], c["n"]
    rng = __import__("random").Random(7)
    golden = math.pi * (3 - math.sqrt(5))
    for i in range(n):
        # a spiral over the upper head; skip the face's opening
        y = 1.0 - (i + 0.5) / n * 0.95
        # jittered off the spiral: regular rows read as a powdered wig, not as coils
        a = math.degrees(i * golden) % 360.0 - 180.0 + rng.uniform(-9.0, 9.0)
        u = 0.40 + 0.60 * y ** 0.8 + rng.uniform(-0.02, 0.02)
        front = math.cos(math.radians(a))
        if front > 0.35 and u < hr["line_front"]: continue
        if front > -0.2 and u < hr["line_temple"] - 0.06: continue
        if u < hr["line_nape"]: continue
        q = person.face_xyz(a, min(u, 0.985), hr["thick"] * rng.uniform(0.45, 0.80))   # half sunk: a soft cloud
        rr = r * (0.75 + 0.5 * rng.random())
        rings = []
        for ii in range(1, 5):
            t = math.pi * ii / 5
            rings.append([P.v(q + Vector((math.sin(t) * math.cos(2 * math.pi * j / 8), math.sin(t) * math.sin(2 * math.pi * j / 8),
                                          math.cos(t))) * rr, HEAD) for j in range(8)])
        at.loft(P, rings, "M_Hair", cap0=P.v(q + Vector((0, 0, rr)), HEAD), cap1=P.v(q - Vector((0, 0, rr)), HEAD))


def beard(person, P):
    """spec hair["beard"] = {n, spread, len, width, thick, chin_u}: a WISPY beard -- n thin strands from the chin
    (spread degrees either side of the middle), the middle longest, each on a two-bone chain so it flies about
    (Captain Bu in the drum, p32: "laughing and shouting, his wispy beard flying about")"""
    b = person.S["hair"].get("beard")
    if not b: return
    n, sp, ln = b["n"], b["spread"], b["len"]
    for i in range(n):
        s = -1.0 + 2.0 * i / max(1, n - 1)                       # -1 .. 1 across the chin
        a = s * sp
        L = ln * (1.0 - 0.45 * s * s)
        base = person.face_xyz(a, b.get("chin_u", 0.04), 0.002)
        # down, a little forward, and fanned out at the sides
        tip = base + Vector((0.010 * s, -0.018 - 0.010 * (1 - abs(s)), -L))
        jm = base + (tip - base) * 0.5 + Vector((0.0, -0.006, 0.0))
        nm = "Hair_Beard_%d_" % i
        _chain(person, nm, [base, jm, tip])
        b1, b2 = nm + "1", nm + "2"
        ws = [norm_w({"Head": 0.7, b1: 0.3}), {b1: 1.0}, norm_w({b1: 0.35, b2: 0.65}), {b2: 1.0}]
        across = Vector((math.cos(math.radians(a)), -math.sin(math.radians(a)) * 0.2, 0.0))
        wedge(P, base, tip, b["width"] * (1.0 - 0.3 * abs(s)), b["thick"], across, ws=ws,
              bend=Vector((0.0, -0.004, 0.0)))


def hair_part(person):
    """the hair: the shell over the skull (none on a BALD head: spec hair["bald"]), a bob's curtain, locks, curls,
    a beard"""
    person.CHAINS = []
    hr = person.S["hair"]
    P = Part("hair", smooth=True) if hr.get("bald") else person.build_hair()
    if "curtain" in hr: curtain(person, P, hr["curtain"])
    locks(person, P)
    curls(person, P)
    beard(person, P)
    return P
