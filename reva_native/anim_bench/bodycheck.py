#!/usr/bin/env python3
"""bodycheck.py -- a body's measurements, read off its pack the way a tailor takes them: heights off the floor, girths round the body with a tape. No window.

    python reva_native/anim_bench/bodycheck.py PACK [--skin "name@N,name"] [--against PACK2 [--skin2 "..."]]
    python reva_native/anim_bench/bodycheck.py PACK [--skin "..."] --women      where its hips, waist, chest, shoulders and legs fall among 1,986 measured women

What's measured is skin, at rest: the meshes the pack says are skin (skate_src/author/material.py, kind `skin`), or --skin (a mesh name, or name@N for the Nth of
that name), or meshes named skin~..., or failing all of those every mesh that isn't a garment, hair, an eye or teeth by name. Clothes, hair and shoes never count.

A girth is the length of a tape pulled snug round the body at one height: it bridges hollows (between the breasts, down the spine), as a tape does. Arms are left
out of the trunk's girths, and each leg is measured alone below the crotch.

The cup is the standard's (EN 13402): bust girth minus under-bust girth. AA 10-12 cm, A 12-14, B 14-16, C 16-18, D 18-20, E 20-22, F 22-24, G 24-26.
That's what a shop's 32C or 34C means. ⛔ A newer UK fitting method (band = the under-bust itself, an inch a letter) calls the same body about four letters
bigger: its "C" is a 7.6 cm difference. This tool prints both so nobody argues from the wrong chart.

--women measures the body where an anthropometric survey measures a woman (hips where the seat is furthest back, the waist at the navel, shoulders across the
upper arms with the arms down) and says how many in a hundred of the U.S. Army's 1,986 women of 2012 each is more than, as she is and at their height.
⛔ A body with limbs that aren't skin (an android's) is named whole with --skin, or its floor is wherever its skin stops.

It says how big a body is. It doesn't say how it looks.
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "skate_src"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "skate_src", "author"))
import numpy as np

import material
from packbody import PackBody

ARM = ("upperarm", "lowerarm", "forearm", "hand", "thumb", "index", "middle", "ring_", "pinky", "finger", "leftarm", "rightarm", "arm_l", "arm_r")
LEG = ("thigh", "calf", "foot", "ball", "toe", "upleg", "leg", "shin")
HEAD = ("head", "neck", "jaw", "facial", "eye", "teeth", "tongue")
NOT_SKIN = ("wear~", "~worn", "~cloth", "~skirt~", "hair", "braid", "pony", "lash", "brow", "eye", "teeth", "tooth", "tongue", "boot", "shoe", "bra", "pant",
            "sleeve", "strap", "earring", "chocker", "choker", "bandana", "nail")
EN_CUPS = ("AA", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K")
UK_CUPS = ("AA", "A", "B", "C", "D", "DD", "E", "F", "FF", "G", "GG", "H", "HH", "J", "JJ", "K")


def en_cup(diff_cm):
    k = int(math.floor((diff_cm - 10.0) / 2.0))
    return "under AA" if k < 0 else EN_CUPS[min(k, len(EN_CUPS) - 1)]


def uk_cup(diff_cm):
    return UK_CUPS[max(0, min(int(diff_cm / 2.54 + 0.5), len(UK_CUPS) - 1))]


def cup_cm(letter):
    """the middle of a cup's range in the standard: C -> 17.0"""
    return 11.0 + 2.0 * EN_CUPS.index(letter.upper())


_DIRS = np.stack([np.cos(np.arange(48) * (2.0 * math.pi / 48.0)), np.sin(np.arange(48) * (2.0 * math.pi / 48.0))], axis=1)


def hull(pts):
    """the convex outline of points on a plane, in order round it"""
    q = np.round(np.asarray(pts, dtype=np.float64).reshape(-1, 2), 6)
    if len(q) > 64:
        # The outline is walked a point at a time, which is slow for thousands of them. So first, all at once: the furthest point each of 48 ways is on the
        # outline for certain; whatever lies well inside the shape those make can't be, and is dropped. What's left is walked exactly as before
        far = q[np.unique((q @ _DIRS.T).argmax(0))]
        if len(far) >= 3:
            c = far.mean(0)
            far = far[np.argsort(np.arctan2(far[:, 1] - c[1], far[:, 0] - c[0]))]
            a, b = far, np.roll(far, -1, axis=0)
            e = b - a
            # inside every edge by more than a hair (a point on an edge may still be the outline's)
            side = e[None, :, 0] * (q[:, None, 1] - a[None, :, 1]) - e[None, :, 1] * (q[:, None, 0] - a[None, :, 0])
            keep = ~(side > 1e-12).all(1)
            q = q[keep]
    p = sorted(set((float(x), float(y)) for x, y in q))
    if len(p) < 3:
        return p

    def turn(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, up = [], []
    for q in p:
        while len(lo) >= 2 and turn(lo[-2], lo[-1], q) <= 0:
            lo.pop()
        lo.append(q)
    for q in reversed(p):
        while len(up) >= 2 and turn(up[-2], up[-1], q) <= 0:
            up.pop()
        up.append(q)
    return lo[:-1] + up[:-1]


def tape(pts):
    h = hull(pts)
    return sum(math.hypot(h[i][0] - h[i - 1][0], h[i][1] - h[i - 1][1]) for i in range(len(h))) if len(h) >= 3 else 0.0


class Skin:
    """every skin mesh of a body as one soup of triangles at rest, each vertex with the part of the body it rides"""

    def __init__(self, B, meshes):
        self.B = B
        # which part of the body a node is: 0 trunk, 1 arm, 2 leg, 3 head. Worked out once a node, not once a vertex (a body has a quarter of a million)
        code_of = np.zeros(len(B.names) + 1, dtype=np.int8)        # the last is for a vertex that rides nothing
        for i, name in enumerate(B.names):
            n = name.lower()
            if any(w in n for w in ARM) and "clav" not in n and "shoulder" not in n:
                code_of[i] = 1
            elif any(w in n for w in LEG):
                code_of[i] = 2
            elif any(w in n for w in HEAD):
                code_of[i] = 3
        pos, tri, part, base = [], [], [], 0
        for m in meshes:
            p, _uv = B.rest(m)
            pos.append(p)
            tri.append(B.tris(m) + base)
            part.append(code_of[B.dominant(m)])
            base += len(p)
        self.T = np.concatenate(tri)
        self.part = np.concatenate(part)
        self.P = np.concatenate(pos)
        self.floor = float(self.P[:, 1].min())
        self.crown = float(self.P[:, 1].max())
        self.H = self.crown - self.floor

    # Her points can be put somewhere else (a reshaping tries a shape and tapes it): the slabs below are made again when they are
    @property
    def P(self):
        return self._P

    @P.setter
    def P(self, v):
        self._P = v
        self._slabs = None

    def _index(self):
        """triangles by the heights they span, in slabs 5 mm deep. ⛔ A cut used to walk every triangle of the body, 460,000 of them, 450 times a measuring:
        twelve seconds, on one core, which a fit of her anatomy did twenty times over"""
        P, T = self._P, self.T
        ty = P[:, 1][T]
        lo, hi = ty.min(1), ty.max(1)
        y0, h = float(lo.min()), 0.005
        b0, b1 = np.floor((lo - y0) / h).astype(np.int64), np.floor((hi - y0) / h).astype(np.int64)
        cnt = b1 - b0 + 1
        which = np.repeat(np.arange(len(T)), cnt)
        bins = np.repeat(b0, cnt) + (np.arange(len(which)) - np.repeat(np.cumsum(cnt) - cnt, cnt))
        order = np.argsort(bins, kind="stable")
        self._slabs = (y0, h, which[order], np.searchsorted(bins[order], np.arange(int(bins.max()) + 2)))
        self._ok = {}

    def cut(self, y, parts=(0, 2, 3), xmin=None, xmax=None):
        """where the skin crosses a height: points (x, z) on the level plane. parts: which parts of the body count"""
        if self._slabs is None:
            self._index()
        P, T = self._P, self.T
        y0, h, which, start = self._slabs
        k = int(math.floor((y - y0) / h))
        if k < 0 or k + 1 >= len(start):
            return np.zeros((0, 2))
        key = tuple(parts)
        if key not in self._ok:
            self._ok[key] = np.isin(self.part[T], parts).all(1)
        cand = which[start[k]:start[k + 1]]
        cand = cand[self._ok[key][cand]]
        a, b, c = P[T[cand, 0]], P[T[cand, 1]], P[T[cand, 2]]
        out = []
        for p, q in ((a, b), (b, c), (c, a)):
            s = (p[:, 1] - y) * (q[:, 1] - y) < 0
            t = (y - p[s, 1]) / (q[s, 1] - p[s, 1])
            out.append(p[s][:, (0, 2)] + t[:, None] * (q[s][:, (0, 2)] - p[s][:, (0, 2)]))
        pts = np.concatenate(out) if out else np.zeros((0, 2))
        if xmin is not None:
            pts = pts[pts[:, 0] >= xmin]
        if xmax is not None:
            pts = pts[pts[:, 0] <= xmax]
        return pts

    def girth(self, y, **kw):
        return tape(self.cut(y, **kw))


def skin_meshes(B, spec=None):
    """the meshes that are skin, and how that was decided"""
    if spec:
        out = []
        for s in [x.strip() for x in spec.split(",") if x.strip()]:
            name, _, nth = s.partition("@")
            got = B.find(name)
            out += [got[int(nth) - 1]] if nth.isdigit() else got
        return out, "named"
    said = material.read(open(B.path, "rb").read())
    out = [m for m in B.meshes if int(said.get("%s@%d" % (m["name"], B.nth(m)), said.get(m["name"], (0, 0, 0, 0, 0)))[4] + 0.5) == 1]
    if out:
        return out, "the pack says"
    out = [m for m in B.meshes if m["name"].startswith("skin")]
    if out:
        return out, "named skin~"
    return [m for m in B.meshes if not any(w in m["name"].lower() for w in NOT_SKIN)], "everything not a garment by name"


def fold(S, bust_y, apex_x, fwd):
    """the fold under the breasts, where a tape for the under-bust lies: its height, and how far the breast's point stands forward of it.
    Down the line through a breast's point the front falls back, and at the fold it stops: below there it's ribs, which run straight down or come forward a
    little. So the fold is the first level, going down, where the front is no further forward than it is 1.5 cm lower. (Two earlier rules failed on a smaller
    breast: a threshold on the slope stopped high on its gentler underside, and the sharpest corner of a smoothed line landed 8 mm above the real one.)"""
    step, look = 0.002, 0.015
    ys = np.arange(bust_y, bust_y - 0.12 * S.H - look, -step)
    cuts = [S.cut(float(y), parts=(0,)) for y in ys]
    n = int(round(look / step))
    best = None
    # The fold is a curve under the breast, lowest under its middle and rising to both sides, and the tape goes under its lowest part. So it's looked for down
    # five lines across the breast, not one: a smaller breast's front-most point is nearer her middle, where the fold is half a centimetre higher
    for dx in (-0.025, -0.0125, 0.0, 0.0125, 0.025):
        front = np.array([float((c[np.abs(np.abs(c[:, 0]) - (apex_x + dx)) < 0.008][:, 1] * fwd).max()) if (np.abs(np.abs(c[:, 0]) - (apex_x + dx)) < 0.008).any() else np.nan for c in cuts])
        ok = ~np.isnan(front)
        if ok.sum() < len(front) // 2:
            continue
        front = np.interp(np.arange(len(front)), np.where(ok)[0], front[ok])
        k = next((j for j in range(3, len(ys) - n) if front[j] - front[j + n] < 0.002), len(ys) - n - 1)
        if best is None or ys[k] < best[0]:
            best = (float(ys[k]), float(front[:3].max() - front[k]))
    # ⛔ The tape doesn't lie IN the fold. Right there a breast's underside overhangs, and the girth changes a centimetre for every millimetre of height
    # (71.7 cm at one level, 78.0 six millimetres up, on the bought character). It lies on the ribs just under it, 6 mm down, where a millimetre is worth a tenth of that
    return best[0] - 0.006, best[1]


def measure(B, meshes):
    """a dict of a body's measurements in metres (heights from the floor, girths round)"""
    S = Skin(B, meshes)
    H, fl = S.H, S.floor
    M = {"stature": H, "floor": fl}
    P = S.P
    trunk = (S.part == 0)
    # the crotch: the lowest skin of the TRUNK on the body's middle line. ⛔ Not the lowest skin there: on a body whose thighs touch, that's where they stop
    # touching, halfway to the knee
    mid = (np.abs(P[:, 0]) < 0.008) & (P[:, 1] > fl + 0.30 * H) & (P[:, 1] < fl + 0.62 * H) & trunk
    if not mid.any():
        mid = (np.abs(P[:, 0]) < 0.008) & (P[:, 1] > fl + 0.30 * H) & (P[:, 1] < fl + 0.62 * H)
    crotch = float(P[mid, 1].min()) if mid.any() else fl + 0.47 * H
    M["crotch"] = crotch - fl
    # which way she faces: the toes point there
    low = P[P[:, 1] < fl + 0.04 * H]
    fwd = 1.0 if low[:, 2].max() + low[:, 2].min() > 2.0 * np.median(P[trunk, 2]) else -1.0
    M["faces"] = fwd
    # the bust: the trunk's front-most point between the waist and the shoulders
    band = trunk & (P[:, 1] > fl + 0.62 * H) & (P[:, 1] < fl + 0.80 * H)
    i = np.where(band)[0][np.argmax(P[band, 2] * fwd)]
    bust_y, apex_x = float(P[i, 1]), abs(float(P[i, 0]))
    M["bust height"] = bust_y - fl
    # round the fullest part, which isn't always the points' height. ⛔ And never as high as the armpit: on a short chest 5 cm over the bust's point is the
    # root of the arms, half a metre round, and a bust taped there reads 98 cm whatever her breasts are
    top = bust_y + 0.05
    try:
        top = min(top, float(B.node("upperarm_l", "LeftArm", "mixamorig:LeftArm", "lShldrBend", "Left_UpperArm")[1]) - 0.075 * H / 1.70)
    except KeyError:
        pass
    M["bust"] = max(S.girth(float(y), parts=(0,)) for y in np.arange(bust_y - 0.05, max(top, bust_y + 0.004), 0.004))
    M["apex apart"] = 2.0 * apex_x
    under_y, reach = fold(S, bust_y, apex_x, fwd)
    M["underbust height"] = under_y - fl
    M["underbust"] = S.girth(under_y, parts=(0,))
    M["bust reach"] = reach
    M["cup difference"] = M["bust"] - M["underbust"]
    # the waist: the trunk's smallest girth between the crotch and the under-bust
    ys = np.arange(crotch + 0.06 * H, under_y, 0.004)
    g = np.array([S.girth(y, parts=(0,)) for y in ys])
    g[g <= 0] = 9.0
    w = int(g.argmin())
    M["waist height"], M["waist"] = float(ys[w]) - fl, float(g[w])
    c = S.cut(float(ys[w]), parts=(0,))
    M["waist width"] = float(c[:, 0].max() - c[:, 0].min())
    # the hips: the largest girth over the seat, legs and trunk together, hands left out. It starts a little above the crotch: at the crotch itself a tape
    # would go round two thighs standing apart, which is no measurement of anything
    ys2 = np.arange(crotch + 0.03 * H, float(ys[w]), 0.004)
    g2 = np.array([S.girth(y, parts=(0, 2)) for y in ys2])
    hp = int(g2.argmax())
    M["hip height"], M["hips"] = float(ys2[hp]) - fl, float(g2[hp])
    c = S.cut(float(ys2[hp]), parts=(0, 2))
    M["hip width"] = float(c[:, 0].max() - c[:, 0].min())
    # a thigh, just under the crotch; a calf at its fullest; an ankle at its least
    side = lambda y: S.girth(y, parts=(0, 2), xmin=0.0)
    M["thigh"] = side(crotch - 0.03 * H)
    knee = fl + 0.27 * H
    ys3 = np.arange(fl + 0.10 * H, knee, 0.004)
    M["calf"] = max(side(y) for y in ys3)
    ys4 = np.arange(fl + 0.035 * H, fl + 0.12 * H, 0.004)
    M["ankle"] = min(g for g in (side(y) for y in ys4) if g > 0)
    # shoulders: across the skin at the height of the arm joints; the neck at its least
    try:
        sh = B.node("upperarm_l", "LeftArm", "mixamorig:LeftArm", "lShldrBend", "Left_UpperArm")
        sr = B.node("upperarm_r", "RightArm", "mixamorig:RightArm", "rShldrBend", "Right_UpperArm")
        M["shoulder height"] = float(sh[1]) - fl
        M["shoulders"] = abs(float(sh[0] - sr[0]))        # between the arm joints: with her arms out to the sides, the skin's width there is her whole span
    except KeyError:
        pass
    # her feet: flat, or made for a heel
    foot = P[(S.part == 2) & (P[:, 1] < fl + 0.08 * H)]
    if len(foot):
        back = foot[foot[:, 2] * fwd < np.percentile(foot[:, 2] * fwd, 15)]
        M["heel off the floor"] = float(back[:, 1].min()) - fl
    M["profile"] = [(float(y) - fl, S.girth(float(y), parts=(0,)), S.girth(float(y), parts=(0, 2))) for y in np.arange(crotch - 0.10, fl + 0.86 * H, 0.02)]
    head = S.part == 3
    if head.any():
        chin = P[head & (P[:, 2] * fwd > np.median(P[head, 2] * fwd) + 0.03)]
        if len(chin):
            M["chin height"] = float(chin[:, 1].min()) - fl
            M["head"] = S.crown - float(chin[:, 1].min())
        ys5 = np.arange(fl + M.get("shoulder height", 0.82 * H) + 0.02, fl + M.get("chin height", 0.87 * H) - 0.01, 0.004)
        g5 = [S.girth(y, parts=(0, 3)) for y in ys5]
        g5 = [g for g in g5 if g > 0]
        if g5:
            M["neck"] = min(g5)
    return M


ROWS = [("stature", "stature", "m"), ("head", "head, chin to crown", "cm"), ("shoulder height", "shoulders, height", "cm"), ("shoulders", "between the arm joints", "cm"),
        ("bust height", "bust, height", "cm"), ("bust", "bust", "cm"), ("underbust", "under the bust", "cm"), ("cup difference", "bust less under-bust", "cm"),
        ("bust reach", "how far a breast stands off the ribs", "cm"), ("apex apart", "between the breasts' points", "cm"),
        ("waist height", "waist, height", "cm"), ("waist", "waist", "cm"), ("waist width", "waist, across", "cm"),
        ("hip height", "hips, height", "cm"), ("hips", "hips", "cm"), ("hip width", "hips, across", "cm"),
        ("crotch", "crotch, height (inside leg)", "cm"), ("thigh", "thigh", "cm"), ("calf", "calf", "cm"), ("ankle", "ankle", "cm"), ("neck", "neck", "cm"),
        ("heel off the floor", "heel off the floor", "cm")]


def show(v, unit):
    if v is None or (isinstance(v, float) and math.isnan(v)):
        return "-"
    return "%.3f m" % v if unit == "m" else "%.1f cm" % (v * 100.0)


def derived(M):
    out = []
    if "head" in M:
        out.append(("heads tall", "%.2f" % (M["stature"] / M["head"])))
    out.append(("legs, of her height", "%.1f%%" % (100.0 * M["crotch"] / M["stature"])))
    out.append(("waist to hips", "%.2f" % (M["waist"] / M["hips"])))
    if "shoulders" in M:
        out.append(("arm joints to hips, across", "%.2f" % (M["shoulders"] / M["hip width"])))
    d = M["cup difference"] * 100.0
    out.append(("cup, the standard's (shops)", "%s  (%d%s)" % (en_cup(d), int(round(M["underbust"] * 100.0 / 5.0)) * 5, en_cup(d))))
    band = int(round(M["underbust"] * 100.0 / 2.54 / 2.0)) * 2
    out.append(("cup, by the UK fitting method", "%d%s" % (band, uk_cup(d))))
    return out


# ---------------------------------------------------------------- against real women
# The U.S. Army's 2012 survey of its own people measured 1,986 women by hand, 93 ways each: "2012 Anthropometric Survey of U.S. Army Personnel: Methods and Summary
# Statistics", Gordon et al., NATICK/TR-15/007, December 2014 (approved for public release, distribution unlimited). These rows are its women's, in centimetres, as
# the report prints them: the mean, the standard deviation, and the 1st, 5th, 10th, 25th, 50th, 75th, 90th, 95th and 99th percentiles. ⛔ They're soldiers of 2012:
# fit, and mostly young. And it's a table of sizes: it says where a body falls among real women, never whether a character should be there.
PCT = (1, 5, 10, 25, 50, 75, 90, 95, 99)
WOMEN = {
    "stature":                     (162.85,  6.42, (148.0, 152.5, 154.7, 158.6, 162.6, 167.2, 171.3, 174.0, 178.1)),   # Stature
    "hip breadth":                 ( 35.38,  2.67, (29.3, 31.1, 32.0, 33.6, 35.3, 37.0, 38.7, 40.0, 42.3)),            # Hip Breadth
    "hip girth":                   (102.12,  7.59, (86.7, 90.1, 92.6, 96.9, 101.8, 106.9, 111.7, 115.6, 120.8)),       # Buttock Circumference
    "seat depth":                  ( 23.29,  2.43, (18.5, 19.6, 20.4, 21.6, 23.1, 24.8, 26.5, 27.7, 30.0)),            # Buttock Depth
    "waist girth at the navel":    ( 86.09,  9.99, (66.5, 71.0, 73.9, 79.0, 85.2, 92.5, 99.5, 104.0, 112.1)),          # Waist Circumference (Omphalion)
    "waist breadth at the navel":  ( 29.99,  3.30, (23.2, 25.0, 26.0, 27.6, 29.8, 32.1, 34.4, 35.6, 38.5)),            # Waist Breadth
    "bust girth":                  ( 94.69,  8.27, (78.4, 82.4, 84.5, 88.9, 94.0, 99.9, 105.7, 109.4, 116.1)),         # Chest Circumference
    "chest breadth":               ( 26.93,  1.86, (23.2, 24.1, 24.6, 25.7, 26.8, 28.1, 29.4, 30.2, 31.4)),            # Chest Breadth
    "shoulders, arms down":        ( 45.03,  2.87, (38.8, 40.6, 41.5, 43.0, 45.0, 46.9, 48.8, 49.9, 52.2)),            # Bideltoid Breadth
    "thigh girth":                 ( 61.61,  5.58, (49.5, 52.7, 54.4, 57.9, 61.3, 65.1, 68.8, 71.1, 75.7)),            # Thigh Circumference
    "crotch height":               ( 78.23,  4.46, (68.5, 71.2, 72.7, 75.3, 78.0, 81.2, 84.1, 85.7, 88.9)),            # Crotch Height
    "navel height":                ( 98.01,  5.00, (86.7, 90.0, 91.8, 94.7, 97.7, 101.3, 104.6, 106.4, 110.1)),        # Waist Height (Omphalion)
}


def among(key, cm):
    """how many in a hundred of the survey's women a measurement is more than, in words"""
    t = WOMEN[key][2]
    if cm < t[0]:
        return "under 1 in 100"
    if cm > t[-1]:
        return "over 99 in 100"
    for i in range(len(t) - 1):
        if t[i] <= cm <= t[i + 1]:
            return "%.0f in 100" % (PCT[i] + (PCT[i + 1] - PCT[i]) * (cm - t[i]) / max(t[i + 1] - t[i], 1e-9))
    return "-"


def arms_down(B, meshes, off=10.0):
    """every skin point with both arms turned down about their shoulder joints to hang `off` degrees from her sides, each point through its own bones: a body at
    rest stands with its arms out, and the survey's women were measured with theirs down. None for a body whose arms aren't found"""
    par = [nd["parent"] for nd in B.nodes]
    kids = {}
    for i, p in enumerate(par):
        kids.setdefault(p, []).append(i)

    def under(r):
        out, todo = [], [r]
        while todo:
            j = todo.pop()
            out.append(j)
            todo += kids.get(j, [])
        return out
    turns = []
    for names in (("upperarm_l", "LeftArm", "mixamorig:LeftArm", "lShldrBend", "Left_UpperArm"), ("upperarm_r", "RightArm", "mixamorig:RightArm", "rShldrBend", "Right_UpperArm")):
        got = [n for n in names if n in B.names]
        if not got:
            return None
        r = B.names.index(got[0])
        sub = under(r)
        elbow = max(kids.get(r, [r]), key=lambda j: len(under(j)))          # the child the forearm and hand hang from, not a twist bone
        c, e = np.array(B.W[r][12:15]), np.array(B.W[elbow][12:15])
        side = 1.0 if c[0] > 0 else -1.0
        a0 = math.atan2(e[1] - c[1], e[0] - c[0])
        a1 = math.atan2(-math.cos(math.radians(off)), side * math.sin(math.radians(off)))
        th = (a1 - a0 + math.pi) % (2.0 * math.pi) - math.pi
        turns.append((sub, c, np.array([[math.cos(th), -math.sin(th), 0.0], [math.sin(th), math.cos(th), 0.0], [0.0, 0.0, 1.0]])))
    out = []
    for m in meshes:
        pos, _n, _uv, nodes, w, _t = B.arrays(m)
        p2 = pos.copy()
        for sub, c, Rz in turns:
            wa = (w * np.isin(nodes, sub)).sum(1)
            p2 += wa[:, None] * ((pos - c) @ Rz.T + c - pos)
        out.append(p2)
    return np.concatenate(out)


def against_women(B, meshes, M):
    """[(label, metres, the survey's row)]: a body measured where and how the survey measured its women. Its hips are taken where the seat stands furthest
    back, its waist at the navel (not at the narrowest, which is this tool's `waist`), its shoulders across the upper arms with the arms hanging"""
    S = Skin(B, meshes)
    P, part, fl, H, fwd = S.P, S.part, M["floor"], M["stature"], M["faces"]
    yc, yw = fl + M["crotch"], fl + M["waist height"]
    rows = [("height", H, "stature"), ("crotch height", M["crotch"], "crotch height")]
    band = (P[:, 1] > yc - 0.01 * H) & (P[:, 1] < yw) & np.isin(part, (0, 2)) & (np.abs(P[:, 0]) > 0.011 * H)
    yb = float(P[band][np.argmin(P[band][:, 2] * fwd), 1])
    c = S.cut(yb, parts=(0, 2))
    rows += [("hip breadth, where her seat is furthest back", float(c[:, 0].max() - c[:, 0].min()), "hip breadth"),
             ("hip girth there", tape(c), "hip girth"),
             ("her seat there, front to back", float(c[:, 1].max() - c[:, 1].min()), "seat depth")]
    # the navel: the pit in the front of the belly on the middle line, a little under the waist. ⛔ Looked for lower, the belly's own curve into the crotch wins
    mid = (np.abs(P[:, 0]) < 0.003 * H) & (P[:, 1] > yw - 0.09 * H) & (P[:, 1] < yw + 0.022 * H) & (part == 0) & ((P[:, 2] - np.median(P[part == 0][:, 2])) * fwd > 0)
    if mid.sum() > 40:
        py, pz = P[mid][:, 1], P[mid][:, 2] * fwd
        bins = np.arange(py.min(), py.max(), 0.002)
        zf = np.array([pz[(py >= b) & (py < b + 0.002)].min() if ((py >= b) & (py < b + 0.002)).any() else np.nan for b in bins])
        ok = ~np.isnan(zf)
        bins, zf = bins[ok], zf[ok]
        dip = np.polyval(np.polyfit(bins, zf, 3), bins) - zf
        inr = (bins > yw - 0.056 * H) & (bins < yw - 0.017 * H)
        if inr.any() and dip[inr].max() > 0.0008:
            yn = float(bins[np.where(inr)[0][int(np.argmax(dip[inr]))]] + 0.001)
            c = S.cut(yn, parts=(0,))
            rows += [("navel height", yn - fl, "navel height"), ("waist girth at her navel", tape(c), "waist girth at the navel"),
                     ("waist breadth at her navel", float(c[:, 0].max() - c[:, 0].min()), "waist breadth at the navel")]
    rows.append(("bust girth", M["bust"], "bust girth"))
    c = S.cut(fl + M["bust height"], parts=(0,))
    rows.append(("chest breadth, at her bust's height", float(c[:, 0].max() - c[:, 0].min()), "chest breadth"))
    Q = arms_down(B, meshes)
    if Q is not None and "shoulder height" in M:
        ys = fl + M["shoulder height"]
        lvl = (Q[:, 1] < ys - 0.012 * H) & (Q[:, 1] > ys - 0.08 * H)
        rows.append(("across her shoulders, arms down", float(Q[lvl][:, 0].max() - Q[lvl][:, 0].min()), "shoulders, arms down"))
    rows.append(("thigh girth", M["thigh"], "thigh girth"))
    return rows


def women_report(path, spec):
    B = PackBody(path)
    meshes, how = skin_meshes(B, spec)
    M = measure(B, meshes)
    rows = against_women(B, meshes, M)
    k = WOMEN["stature"][0] / (100.0 * M["stature"])
    print("%s (%d skin meshes, %s) among the 1,986 women the U.S. Army measured in 2012 (NATICK/TR-15/007)." % (os.path.basename(path), len(meshes), how))
    print("'In 100' is how many of those women she's more than. 'At their height' is her whole body brought to their average height, %.1f cm:" % WOMEN["stature"][0])
    print("her shape with her size taken out.\n")
    print("%-46s %9s   %-18s %-17s %s" % ("", "hers", "women: mean (sd)", "as she is", "at their height"))
    got = {}
    for label, v, key in rows:
        cm = 100.0 * v
        got[key] = cm
        print("%-46s %6.1f cm   %6.1f (%4.1f)       %-17s %s" % (label, cm, WOMEN[key][0], WOMEN[key][1], among(key, cm), "" if key == "stature" else among(key, cm * k)))
    print()
    for label, a, b in (("hips to shoulders, across", "hip breadth", "shoulders, arms down"), ("waist at the navel to hips, round", "waist girth at the navel", "hip girth"),
                        ("hips to bust, round", "hip girth", "bust girth"), ("legs, of her height", "crotch height", "stature")):
        if a in got and b in got:
            print("%-46s %6.2f      women (of their means) %.2f" % (label, got[a] / got[b], WOMEN[a][0] / WOMEN[b][0]))
    if "heel off the floor" in M and M["heel off the floor"] > 0.005:
        print("\nHer heels are %.1f cm off the floor: flat on her feet she's a little shorter than this, and the survey's women were barefoot." % (100.0 * M["heel off the floor"]))


def report(packs):
    cols = []
    for path, spec in packs:
        B = PackBody(path)
        meshes, how = skin_meshes(B, spec)
        cols.append((os.path.basename(path), measure(B, meshes), "%d skin meshes (%s)" % (len(meshes), how)))
    w = 38
    print("%-*s" % (w, "") + "".join("%-26s" % c[0][:25] for c in cols))
    print("%-*s" % (w, "skin") + "".join("%-26s" % c[2][:25] for c in cols))
    for key, label, unit in ROWS:
        print("%-*s" % (w, label) + "".join("%-26s" % show(c[1].get(key), unit) for c in cols))
    ders = [derived(c[1]) for c in cols]
    for i, (label, _v) in enumerate(ders[0]):
        print("%-*s" % (w, label) + "".join("%-26s" % (dict(d).get(label, "-")) for d in ders))
    return cols


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    opt = {argv[i]: argv[i + 1] for i in range(2, len(argv) - 1) if argv[i].startswith("--")}
    if "--women" in argv:
        women_report(argv[1], opt.get("--skin"))
        return
    packs = [(argv[1], opt.get("--skin"))]
    if "--against" in opt:
        packs.append((opt["--against"], opt.get("--skin2")))
    cols = report(packs)
    if "--profile" in argv:
        print("\nheight      trunk's girth   with the legs      (every 2 cm, %s)" % cols[0][0])
        for y, g, g2 in cols[0][1]["profile"]:
            print("%6.1f cm   %7.1f cm     %7.1f cm" % (y * 100.0, g * 100.0, g2 * 100.0))


if __name__ == "__main__":
    main(sys.argv)
