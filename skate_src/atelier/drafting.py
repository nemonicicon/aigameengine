"""drafting.py -- PATTERN DRAFTING: flat pattern pieces drawn from a body's own measurements, the way a tailor drafts a
bodice, a sleeve, a pair of trousers or a skirt -- and placed round that body's dress form, ready for the sewing
machine (sewing.py). The same draft on another body cuts THAT body's clothes: REVA's suit and the practice model's
come out of one set of functions.

A dress form here is any object with:
  torso            atelier.Surface (theta, z): theta 0 = front, 90 deg = the body's left (+X)
  leg[+1/-1]       atelier.Surface (phi, z): phi 0 = the leg's front, 90 deg = its outer side
  arm["Left"/"Right"]  atelier.Surface (angle, t): angle 0 = the arm's top/outer side, t along it from the shoulder
  armb[side]       {"joint", "d", "q", "fr"}: the arm's frame
  z                {"crotch", "waist", "hip", "armpit", "shoulder", "neck"} heights; "wrist" the wrist's t
Patterns are in metres, flat: x across (+ = the body's left, looking at its front), y up.
"""
import math

import numpy as np
from mathutils import Vector

from sewing import Panel, Seam


# ------------------------------------------------------------------ measuring the form
def ring(S, v, n=96, a0=0.0, a1=2 * math.pi):
    return [S.pt(a0 + (a1 - a0) * k / n, v) for k in range(n + 1)]


def arc(S, v, a0, a1, n=64):
    """the surface's length round its ring at v, from angle a0 to a1"""
    pts = ring(S, v, n, a0, a1)
    return sum((b - a).length for a, b in zip(pts[:-1], pts[1:]))


def circ(S, v, n=96):
    return arc(S, v, 0.0, 2 * math.pi, n)


class ArcMap:
    """arc length <-> angle round a surface's ring at v, from a centre angle (cached per height)"""
    def __init__(self, S, a_center, sign=1.0, n=72):
        self.S, self.ac, self.sg, self.n, self.cache = S, a_center, sign, n, {}

    def angle(self, v, s):
        key = round(v, 3)
        if key not in self.cache:
            a = np.linspace(0.0, math.pi * 1.05, self.n)
            pts = [self.S.pt(self.ac + self.sg * x, v) for x in a]
            cum = np.concatenate([[0.0], np.cumsum([(q - p).length for p, q in zip(pts[:-1], pts[1:])])])
            self.cache[key] = (a, cum)
        a, cum = self.cache[key]
        x = float(np.interp(abs(s), cum, a))
        return self.ac + self.sg * math.copysign(x, s)

    def arc(self, v, x):
        """the arc from the centre to the offset angle x (radians round from the centre)"""
        self.angle(v, 0.0)
        a, cum = self.cache[round(v, 3)]
        return float(np.interp(abs(x), a, cum))

    def half(self, v):
        """the arc from the centre to a quarter turn either side (half the ring's front or back)"""
        self.angle(v, 0.0)
        a, cum = self.cache[round(v, 3)]
        return float(np.interp(math.pi / 2, a, cum))


def _hull_idx(P):
    """the convex hull of 2D points (Andrew's monotone chain): their indices, counter-clockwise"""
    order = sorted(range(len(P)), key=lambda i: (P[i][0], P[i][1]))
    cross = lambda o, a, b: (P[a][0] - P[o][0]) * (P[b][1] - P[o][1]) - (P[a][1] - P[o][1]) * (P[b][0] - P[o][0])
    lo, hi = [], []
    for i in order:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], i) <= 0: lo.pop()
        lo.append(i)
    for i in reversed(order):
        while len(hi) >= 2 and cross(hi[-2], hi[-1], i) <= 0: hi.pop()
        hi.append(i)
    return lo[:-1] + hi[:-1]


class Bridged:
    """A BODY AS A TAPE MEASURE SEES IT (09-29): every horizontal ring of a surface replaced by its convex hull, the
    points of a hollow spread along the bridge. Every girth in a real pattern is a tape's (FreeSewing's chest, hips,
    neck: a tape across a cleavage does not go into it), and cloth with any ease hangs the same way.
    ⛔ Laid along REVA's own ring with the sweatshirt's 3.5 cm standoff, the offset of her 4.8 cm cleavage folded over
    itself: the centre-front column came out 3-5 cm longer than its neighbours, the pattern grew a TONGUE at the neck
    that sewed into a slit, and the spine's groove put a ridge down the centre back. Same (theta, v) as the surface."""
    def __init__(self, S, n=240, dz=0.002):
        self.S, self.n, self.dz, self.cache = S, n, dz, {}
        self.name = S.name + "~bridged"
        self.periodic = True
        self.outward, self.weights = S.outward, S.weights
        self.th = np.linspace(0.0, 2 * math.pi, n, endpoint=False)

    def _ring(self, k):
        R = self.cache.get(k)
        if R is None:
            v = k * self.dz
            P = np.array([tuple(self.S.pt(float(t), v)) for t in self.th])
            H = sorted(_hull_idx(P[:, :2]))                  # a simple ring's hull keeps the ring's own order
            R = P.copy()
            for a, b in zip(H, H[1:] + [H[0] + self.n]):
                if b - a < 2: continue
                pa, pb = P[a % self.n], P[b % self.n]
                for j in range(a + 1, b):
                    R[j % self.n] = pa + (pb - pa) * ((j - a) / (b - a))
            self.cache[k] = R
        return R

    def pt(self, u, v):
        f = v / self.dz; k = math.floor(f); t = f - k
        uu = (u % (2 * math.pi)) / (2 * math.pi) * self.n; i = int(math.floor(uu)) % self.n; s = uu - math.floor(uu)
        R0, R1 = self._ring(k), self._ring(k + 1)
        j = (i + 1) % self.n
        p = (R0[i] * (1 - s) + R0[j] * s) * (1 - t) + (R1[i] * (1 - s) + R1[j] * s) * t
        return Vector((float(p[0]), float(p[1]), float(p[2])))

    def normal(self, u, v, e=1e-4):
        a = self.pt(u + e, v) - self.pt(u - e, v)
        b = self.pt(u, v + e) - self.pt(u, v - e)
        n = a.cross(b)
        if n.length < 1e-12: return self.outward(self.pt(u, v)).normalized()
        n.normalize()
        return n if n.dot(self.outward(self.pt(u, v))) >= 0 else -n


def chain3d(panels, specs, step=0.008):
    """the line along chained panel edges ('panel:edge', '~' reversed) as the panels are PLACED before sewing --
    each edge densified in the pattern first (a two-point edge placed as a chord cuts through the body)"""
    by = {p.name: p for p in panels}
    out = []
    for sp in specs:
        pn, en = sp.rstrip("~").split(":")
        p = by[pn]
        pts = list(dict(p.edges)[en])
        if sp.endswith("~"): pts = pts[::-1]
        dense = [pts[0]]
        for a, b in zip(pts[:-1], pts[1:]):
            k = max(1, int(math.ceil(math.dist(a, b) / step)))
            dense += [(a[0] + (b[0] - a[0]) * i / k, a[1] + (b[1] - a[1]) * i / k) for i in range(1, k + 1)]
        P3 = [Vector(tuple(p.place(x, y))) for x, y in dense]
        if out and (P3[0] - out[-1]).length < 1e-6: P3 = P3[1:]
        out += P3
    return out


def band(name, panels, specs, length, height, direction, join="bottom", fabric=None, when=(0.55, 1.0), grip=False):
    """A RIB BAND or an ELASTIC CASING: a strip `length` long and `height` tall, closed into a ring and sewn along the
    chained edges `specs` (a ring round the body, in order) -- cut SHORTER than the opening it finishes, so the opening
    is gathered into it, the way every knit's cuffs, hem band and neckband are (FreeSewing's Sven: the band is the
    measurement plus its ease, LESS the rib's 15% stretch). join: the band's edge on the garment -- 'bottom' (it rises
    from the seam: a waistband, a neckband) or 'top' (it hangs from it: a hem band, a cuff); direction: the way it
    goes from the seam, a vector or f(p). grip: its free edge PINNED where the body is under it -- an elastic
    waistband's grip on skin (⛔ on the slope of her hips a free elastic ring crept down 5-8 cm, 24-36% stretched:
    a PBD ring is only as stiff as its iterations). Returns (panel, seams)."""
    line = chain3d(panels, specs)
    cum = np.concatenate([[0.0], np.cumsum([(b - a).length for a, b in zip(line[:-1], line[1:])])])
    L, h = float(length), float(height)
    edges = [("bottom", [(-L / 2, 0.0), (L / 2, 0.0)]), ("end_L", [(L / 2, 0.0), (L / 2, h)]),
             ("top", [(L / 2, h), (-L / 2, h)]), ("end_R", [(-L / 2, h), (-L / 2, 0.0)])]

    def at(s):
        d = min(max(s, 0.0), 1.0) * cum[-1]
        i = min(max(int(np.searchsorted(cum, d, side="right")) - 1, 0), len(line) - 2)
        t = 0.0 if cum[i + 1] - cum[i] < 1e-12 else (d - cum[i]) / (cum[i + 1] - cum[i])
        return line[i] * (1 - t) + line[i + 1] * t

    def place(x, y):
        s, off = ((x + L / 2) / L, y) if join == "bottom" else ((L / 2 - x) / L, h - y)
        p = at(s)
        dv = direction(p) if callable(direction) else Vector(tuple(direction))
        return p + dv.normalized() * off
    P = Panel(name, edges, place, fabric, pin=[("top" if join == "bottom" else "bottom")] if grip else None)
    # a band is narrow: at the body's 16 mm a neckband had two rows. ⛔ But no finer than it needs: a PBD ring gets
    # SOFTER with every vertex added, and a waistband cut at 10 mm stretched 55% and let the shorts down
    P.h = max(0.008, min(0.016, h / 2.5))
    seams = [Seam(name + ":" + join, list(specs), when=when, auto=False),
             Seam(name + ":end_L", name + ":end_R~", when=(when[0], when[0] + 0.3 * (when[1] - when[0])), auto=False)]
    print("[draft] %s: a band %.1f cm long on a %.1f cm opening (%.0f%%), %.1f cm tall"
          % (name, L * 100, cum[-1] * 100, 100 * L / max(cum[-1], 1e-9), h * 100))
    return P, seams


def smooth(pts, n=40):
    """a Catmull-Rom through pts, n points"""
    P = [np.array(p, float) for p in pts]
    c = [P[0]] + P + [P[-1]]
    out = []
    segs = len(P) - 1
    for i in range(n):
        s = i / (n - 1) * segs
        k = min(int(s), segs - 1); t = s - k
        p0, p1, p2, p3 = c[k], c[k + 1], c[k + 2], c[k + 3]
        out.append(tuple(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t
                                + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t)))
    return out


# ------------------------------------------------------------------ the arms, measured
def arm_radius(F, side, t):
    """the arm's largest radius at t along it (from its own surface)"""
    A = F.armb[side]; S = F.arm[side]
    tt = min(max(t, -0.05), F.z.get("wrist", 0.53))
    ax = A["joint"] + A["d"] * tt
    return max((S.pt(a, tt) - ax).length for a in (0.0, math.pi / 2, math.pi, 1.5 * math.pi))


def clear_of_arms(F, p, margin):
    for side, A in F.armb.items():
        r = p - A["joint"]; t = r.dot(A["d"])
        if t < -0.06: continue
        if (r - A["d"] * t).length < arm_radius(F, side, t) + margin: return False
    return True


# ------------------------------------------------------------------ a top: bodice panels, sleeves, a collar band
def top(F, name, hem, ease=0.08, neck_drop=0.05, back_drop=0.012, neck_w=0.075, sleeve_t=None, sleeve_ease=0.05,
        cuff_ease=0.03, scye=0.030, cap_ease=0.012, clear=0.030, clear_min=0.006, split_front=0.0, v_to=None,
        collar=0.0, drop_shoulder=0.0, fabric=None, bridge=True, hem_ease=None, rib=None, neck_fit=None):
    """a shirt, tee, sweatshirt, jacket or coat: front (or two fronts, split at the centre with split_front overlap)
    and back panels drafted off the form's own girth at every height plus EASE, set-in sleeves to sleeve_t (a t on the
    arm) or none, a band collar collar metres tall. v_to: a V neck down to that height (a jacket's opening).
    scye: how far under the armpit the side seam stops (the armhole's depth) -- DEEPENED by the draft until the
    armhole is long enough for the sleeve's bicep; cap_ease: how much longer the sleeve's cap is than the armhole.
    drop_shoulder: the shoulder seam carried out past the shoulder tip (a sweatshirt).
    bridge: measure and lay the panels on the body AS A TAPE SEES IT (Bridged), not into its hollows.
    hem_ease: the ease at the hem, blended to `ease` at the waist (a sweatshirt's body tapers to its band).
    neck_fit: the neckline FITTED to the neck's girth plus this share (FreeSewing's collar fit: Brian 5%, Sven 10%) by
    the front's drop, instead of a drop by hand.
    rib: KNIT FINISHES (FreeSewing's Sven): {"h": hem band height, "ease": the band's ease over the hips (0.08),
    "stretch": how much shorter than that the rib is cut (0.15), "cuff_h", "cuff_ease" (0.20 over the wrist),
    "neck_h" (a neckband, 0 = none), "neck_stretch", "fabric"}. `hem` and `sleeve_t` stay the FINISHED length: the
    body and sleeves end where their bands begin.

    Placement: every panel goes round the form ISOMETRICALLY as near as a body allows -- its width at a height laid
    round the ring there with the EASE as its standoff (a garment with 22 cm of ease stands 3.5 cm off her), and the
    edge above the side seam at the arm's CREASE, clear of the arm. ⛔ The first placement put every panel 3 cm off the
    body regardless: the sleeves started 27% stretched, the neck 60%, and the first frame of the sew was an explosion."""
    T = Bridged(F.torso) if bridge else F.torso
    z = F.z
    rib = dict(rib) if rib else None
    hem0, sleeve_t0 = hem, sleeve_t
    if rib:                                   # the body and the sleeves end where their bands begin
        hem = hem + rib.get("h", 0.045)
        if sleeve_t is not None: sleeve_t = sleeve_t - rib.get("cuff_h", rib.get("h", 0.045))
    front, back = ArcMap(T, 0.0, 1.0), ArcMap(T, math.pi, -1.0)
    # ⭐ DRAFTED OVER THE DRESSED FORM: a garment sewn over others is measured over them, as a tailor measures a jacket
    # over the shirt -- F.layers (optional) gives the mean offset of the garments already on at a height of the
    # torso, and round the arm at a t along it. ⛔ Measured on the bare body, the suit man's jacket was 3 mm clear of
    # the shirt at the chest and TIGHT over the shirt and waistband at the hips: it rode up his back like a small
    # sweater, 19 cm in the first third of a second
    lay = getattr(F, "layers", None)
    lay_t = (lambda v: lay.torso(v)) if lay is not None else (lambda v: 0.0)       # (Layers keeps the two apart)
    lay_a = (lambda t: lay.arm(t)) if lay is not None else (lambda t: 0.0)
    ease_at = (lambda v: ease) if hem_ease is None else \
        (lambda v: hem_ease + (ease - hem_ease) * smoothstep_(hem, z["waist"], v))
    half = lambda v: front.half(v) + math.pi / 2 * lay_t(v) + ease_at(v) / 4.0   # half the front (or back): over the layers, + ease/4
    sh_x = front.half(z["shoulder"]) * 0.92 + drop_shoulder
    sh_y = z["shoulder"] - hem - 0.012 - drop_shoulder * 0.30
    nk_y = z["neck"] - hem
    plen = lambda pts: sum(math.dist(a, b) for a, b in zip(pts[:-1], pts[1:]))

    def draft_armhole(sc):
        zs = np.linspace(hem, z["armpit"] - sc, 12)
        side = [(half(v), v - hem) for v in zs]
        ap = side[-1]
        depth = sh_y - ap[1]
        x_ac = min(ap[0], sh_x) - 0.016                      # across the chest: the armhole's hollow
        arm = smooth([ap, (ap[0] - 0.014, ap[1] + min(0.012, depth * 0.12)), (x_ac, ap[1] + depth * 0.42),
                      (x_ac + 0.004, ap[1] + depth * 0.76), (sh_x, sh_y)], 18)
        return side, arm

    # the sleeve decides how deep the armhole must be: its cap has to go round a bicep this wide
    bic_w = None
    if sleeve_t is not None:
        bic_w = circ(F.arm["Left"], 0.10) + 2 * math.pi * lay_a(0.10) + sleeve_ease
    sc = scye
    side, armhole = draft_armhole(sc)
    while bic_w is not None and 2 * plen(armhole) + cap_ease < bic_w * 1.06 and sc < 0.16:
        sc += 0.005
        side, armhole = draft_armhole(sc)
    if sc > scye + 1e-9:
        print("[draft] %s: armhole deepened %.0f -> %.0f mm under the arm, to take a %.0f cm bicep" % (
            name, scye * 1000, sc * 1000, bic_w * 100))
    ap = side[-1]
    shoulder = [(sh_x, sh_y), (neck_w, nk_y)]
    sc_v = hem + ap[1]                                         # the side seam's top, as a height
    sh_v = hem + sh_y

    # the panel's half-width at every pattern height: the side seam, then the armhole, then the shoulder line
    wy = [p[1] for p in side] + [p[1] for p in armhole[1:]] + [nk_y]
    wx = [p[0] for p in side] + [p[0] for p in armhole[1:]] + [neck_w]
    W = lambda y: float(np.interp(y, wy, wx))

    def neckline(drop, vz=None):
        if vz is not None:                                     # a V: straight down to the button point
            return [(neck_w, nk_y), (0.0, vz - hem)]
        return smooth([(neck_w, nk_y), (neck_w * 0.85, nk_y - drop * 0.55), (neck_w * 0.45, nk_y - drop * 0.95),
                       (0.0, nk_y - drop)], 12)

    def mirror(pts):
        return [(-x, y) for x, y in pts]

    edge_cache = {}

    def edge_angle(amap, v, sgn):
        """how far round from the panel's centre its edge may go at height v: to the side (a quarter turn) below
        the arms and on the shoulder's ridge; to the arm's CREASE beside the arm"""
        if v <= sc_v or v >= sh_v: return math.pi / 2
        key = (id(amap), round(v, 3), sgn)
        if key not in edge_cache:
            xa = math.pi / 2
            for xa in np.linspace(math.pi / 2, 0.25, 48):
                th = amap.ac + amap.sg * sgn * xa
                if clear_of_arms(F, T.pt(th, v) + T.normal(th, v) * (clear_min + 0.004), 0.010): break
            # ⛔ BLENDED into the side at both ends of the armhole: the edge JUMPED from the arm's crease to the side at
            # the shoulder point and the scye, and a jacket was laid 185% stretched across that step
            span = max(sh_v - sc_v, 1e-6)
            w = smoothstep_(sc_v, sc_v + 0.30 * span, v) * (1.0 - smoothstep_(sh_v - 0.40 * span, sh_v, v))
            edge_cache[key] = float(math.pi / 2 + (xa - math.pi / 2) * w)
        return edge_cache[key]

    def place_on(amap):
        def f(x, y):
            v = hem + y
            w = max(W(y), 1e-4)
            s = max(-1.0, min(1.0, x / w))
            xa = edge_angle(amap, v, 1.0 if s >= 0 else -1.0)
            arc_e = amap.arc(v, xa)
            th = amap.angle(v, s * arc_e)
            dd = max(clear_min, (w - arc_e) / (math.pi / 2))   # the ease, as a standoff
            return T.pt(th, v) + T.normal(th, v) * dd
        return f

    def place3d(amap, s_, v):
        """the design point (s_ of the half-width, height v) laid round the form -- and OUTSIDE whatever the form
        already wears there (F.layers): laid clear of the layers from the start, never pushed out through them"""
        w = max(W(v - hem), 1e-4)
        xa = edge_angle(amap, v, 1.0 if s_ >= 0 else -1.0)
        arc_e = amap.arc(v, xa)
        th = amap.angle(v, s_ * arc_e)
        dd = max(clear_min, (w - arc_e) / (math.pi / 2))
        p0, n0 = T.pt(th, v), T.normal(th, v)
        return p0 + n0 * dd            # (over layers, the girth already carries them: sewing.sew(layers=) grows them)

    top_v = hem + nk_y

    class Flat:
        """a panel FLATTENED OFF THE BODY: its pattern's y is the body's own length up each column from the hem,
        measured along the placement -- over the chest and on over the top of the shoulder to the ridge. ⛔ With y
        the HEIGHT, every yoke was cut short by the shoulder's own depth: the suit man's shirt hung 4 cm high and
        his jacket rode up to his chest, its seams 2.5 cm apart. The pattern a tailor draws is longer there than the
        height difference, and now so is this one"""
        def __init__(self, amap):
            # ⭐ COLUMNS AT A FIXED PATTERN x (09-29), not at a fixed SHARE of the width: the share-1 column climbed the
            # side seam, round the armhole and along the shoulder to the neck point, so the neckline's two ends were
            # measured up paths of wildly different length -- REVA's sweatshirt's neckline was cut 36% longer than the
            # neck it was sewn to, and it ruffled at both shoulders
            self.amap = amap
            self.xm = max(max(abs(x_) for x_ in wx), 1e-3) * 1.02
            self.X = np.linspace(-self.xm, self.xm, 61)
            self.V = np.linspace(hem, top_v, max(24, int((top_v - hem) / 0.005)))
            A = np.zeros((len(self.X), len(self.V)))
            for i, x_ in enumerate(self.X):
                pts = [place3d(amap, max(-1.0, min(1.0, float(x_) / max(W(float(v) - hem), 1e-4))), float(v))
                       for v in self.V]
                A[i, 1:] = np.cumsum([(b - a).length for a, b in zip(pts[:-1], pts[1:])])
            self.A = A

        def arc(self, x, v):
            fi = (min(max(x, -self.xm), self.xm) + self.xm) / (2.0 * self.xm) * (len(self.X) - 1)
            fj = (min(max(v, hem), top_v) - hem) / max(top_v - hem, 1e-9) * (len(self.V) - 1)
            i0 = min(int(fi), len(self.X) - 2); j0 = min(int(fj), len(self.V) - 2)
            ti, tj = fi - i0, fj - j0
            a = self.A
            return float((a[i0, j0] * (1 - tj) + a[i0, j0 + 1] * tj) * (1 - ti) + (a[i0 + 1, j0] * (1 - tj) + a[i0 + 1, j0 + 1] * tj) * ti)

        def to_pattern(self, x, yd):
            return (x, self.arc(x, hem + yd))

        def edge(self, pts, step=0.008):
            """an outline edge drawn in design space (x, height above the hem), densified, then flattened"""
            out = [pts[0]]
            for a, b in zip(pts[:-1], pts[1:]):
                k = max(1, int(math.ceil(math.dist(a, b) / step)))
                for i in range(1, k + 1):
                    t = i / k
                    out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
            return [self.to_pattern(x, y) for x, y in out]

        def edges(self, E):
            return [(n, self.edge(p)) for n, p in E]

        def place(self, x, y):
            lo, hi = hem, top_v
            for _ in range(36):
                mid = 0.5 * (lo + hi)
                if self.arc(x, mid) < y: lo = mid
                else: hi = mid
            v = 0.5 * (lo + hi)
            s_ = max(-1.0, min(1.0, x / max(W(v - hem), 1e-4)))
            return place3d(self.amap, s_, v)

    flat_b, flat_f = Flat(back), Flat(front)
    if neck_fit is not None and split_front == 0.0 and v_to is None:
        # ⭐ THE NECKLINE IS FITTED TO THE NECK (FreeSewing's Brian: the front's depth is moved until the whole opening
        # is the neck's girth plus the collar ease). ⛔ REVA's first sweatshirt took a 3.5 cm drop by hand: its opening
        # was short of her neck and the front pulled into a notch
        # the neck measured where the cloth is: its girth plus the garment's own standoff off it. ⛔ Against the bare
        # girth (32 cm) the fit asked for a neckline shorter than a cloth ring 6 mm off her neck can be, and quietly
        # settled on its lower bound: a 5 mm drop, a mock neck
        target = (circ(T, z["neck"]) + 2.0 * math.pi * clear_min) * (1.0 + neck_fit)
        bk_len = 2.0 * plen(flat_b.edge(neckline(back_drop)))
        lo_d, hi_d = 0.005, 0.16
        for _ in range(32):
            mid = 0.5 * (lo_d + hi_d)
            if bk_len + 2.0 * plen(flat_f.edge(neckline(mid))) < target: lo_d = mid
            else: hi_d = mid
        neck_drop = 0.5 * (lo_d + hi_d)
        if neck_drop < 0.006 or neck_drop > 0.155:
            print("[draft] %s: ⛔ THE NECK FIT HIT ITS BOUND (%.0f mm): no front drop gives a %.1f cm neckline -- "
                  "check neck_w against the body" % (name, neck_drop * 1000, target * 100))
        print("[draft] %s: neckline fitted to %.1f cm (the neck %.1f cm + %.0f%%): the front drops %.0f mm"
              % (name, target * 100, circ(T, z["neck"]) * 100, neck_fit * 100, neck_drop * 1000))

    panels, seams = [], []
    # the back: one panel, x mirrored (its +x is still the body's left, which is round to the RIGHT of its centre)
    bk_neck = neckline(back_drop)
    bedges = [("hem", [(-side[0][0], 0.0), (side[0][0], 0.0)]),
              ("side_L", side), ("arm_L", armhole), ("shoulder_L", shoulder),
              ("neck_L", bk_neck), ("neck_R", list(reversed(mirror(bk_neck)))),
              ("shoulder_R", list(reversed(mirror(shoulder)))), ("arm_R", list(reversed(mirror(armhole)))),
              ("side_R", list(reversed(mirror(side))))]
    panels.append(Panel(name + "_back", flat_b.edges(bedges), flat_b.place, fabric))
    if split_front == 0.0:
        fn = neckline(neck_drop, v_to)
        fedges = [("hem", [(-side[0][0], 0.0), (side[0][0], 0.0)]),
                  ("side_L", side), ("arm_L", armhole), ("shoulder_L", shoulder),
                  ("neck_L", fn), ("neck_R", list(reversed(mirror(fn)))),
                  ("shoulder_R", list(reversed(mirror(shoulder)))), ("arm_R", list(reversed(mirror(armhole)))),
                  ("side_R", list(reversed(mirror(side))))]
        panels.append(Panel(name + "_front", flat_f.edges(fedges), flat_f.place, fabric))
        fronts = [(name + "_front", "L"), (name + "_front", "R")]
    else:
        # two fronts: each from its side seam to past the centre by split_front (they overlap, like a jacket's)
        vz = v_to if v_to is not None else z["neck"] - neck_drop
        for sd, sg in (("L", 1.0), ("R", -1.0)):
            # the front edge: straight up to the top button, then the V -- a straight line to the neck point.
            # split_front < 0 leaves a GAP between the fronts (an open jacket hangs apart)
            edge = [(-split_front, 0.0), (-split_front, vz - hem), (neck_w, nk_y)]
            e2 = [(sg * x, y) for x, y in [(-split_front, 0.0), (side[0][0], 0.0)]]
            s2 = [(sg * x, y) for x, y in side]
            a2 = [(sg * x, y) for x, y in armhole]
            h2 = [(sg * x, y) for x, y in shoulder]
            c2 = [(sg * x, y) for x, y in reversed(edge)]
            edges = [("hem", e2), ("side_" + sd, s2), ("arm_" + sd, a2), ("shoulder_" + sd, h2), ("front_" + sd, c2)]
            if sg < 0:                                          # keep the outline counter-clockwise
                edges = [(n, list(reversed(p))) for n, p in reversed(edges)]
            panels.append(Panel(name + "_front" + sd, flat_f.edges(edges), flat_f.place, fabric))
        fronts = [(name + "_frontL", "L"), (name + "_frontR", "R")]
    for fp, sd in fronts:                                   # the order a tailor sews: shoulders, sleeves, sides
        seams.append(Seam(fp + ":side_" + sd, name + "_back:side_" + sd, when=(0.45, 1.0)))
        seams.append(Seam(fp + ":shoulder_" + sd, name + "_back:shoulder_" + sd, when=(0.0, 0.30)))
    if sleeve_t is not None:
        arm_f, arm_b = plen(flat_f.edge(armhole)), plen(flat_b.edge(armhole))   # the armholes as CUT (flattened)
        arm_len = 0.5 * (arm_f + arm_b)
        for sd in ("L", "R"):
            side_ = "Left" if sd == "L" else "Right"
            S = F.arm[side_]
            A = F.armb[side_]
            cuff = circ(S, sleeve_t) + 2 * math.pi * lay_a(sleeve_t) + cuff_ease
            w2 = bic_w / 2.0
            # the cap's top: where the bodice's shoulder point sits, along the arm (+ the drop)
            p_sh = T.pt(math.pi / 2 if sd == "L" else -math.pi / 2, sh_v)
            t_top = (p_sh - A["joint"]).dot(A["d"]) + drop_shoulder * 0.5
            L = sleeve_t - t_top
            target = 2 * arm_len + cap_ease                   # the cap is the armhole, front and back, plus its ease

            def cap_of(h):
                return smooth([(-w2, L - h), (-w2 * 0.62, L - h * 0.42), (0.0, L), (w2 * 0.62, L - h * 0.42),
                               (w2, L - h)], 28)
            lo_h, hi_h = 0.005, 0.30
            for _ in range(40):
                mid = 0.5 * (lo_h + hi_h)
                if plen(cap_of(mid)) < target: lo_h = mid
                else: hi_h = mid
            cap_h = 0.5 * (lo_h + hi_h)
            cap = cap_of(cap_h)
            print("[draft] %s sleeve %s: bicep %.0f cm, cuff %.0f cm, cap %.0f mm high, cap seam %.0f cm on a %.0f cm armhole"
                  % (name, sd, bic_w * 100, cuff * 100, cap_h * 1000, plen(cap) * 100, 2 * arm_len * 100))
            sedges = [("cuff", [(-cuff / 2, 0.0), (cuff / 2, 0.0)]), ("under_b", [(cuff / 2, 0.0), (w2, L - cap_h)]),
                      ("cap", list(reversed(cap))), ("under_a", [(-w2, L - cap_h), (-cuff / 2, 0.0)])]

            def sleeve_place(x, y, S=S, L=L, A=A, side_=side_, w2=w2, cuff=cuff, cap_h=cap_h, t_top=t_top):
                # rolled round the arm: the pattern's width at this height is the circumference (+x the FRONT, on
                # both arms), so nothing is stretched -- with the arm's own radius as the floor
                t = t_top + (L - y)
                yu = L - cap_h
                if y <= yu:
                    half_w = cuff / 2.0 + (w2 - cuff / 2.0) * max(0.0, y / max(yu, 1e-6))
                    R = max(half_w / math.pi, arm_radius(F, side_, t) + lay_a(t) + clear_min)
                else:                                          # the cap: the bicep's cylinder, over the top
                    R = max(w2 / math.pi, arm_radius(F, side_, t_top + cap_h) + lay_a(t_top + cap_h) + clear_min)
                a = x / R
                rad = A["q"] * math.cos(a) + A["fr"] * math.sin(a)
                return A["joint"] + A["d"] * t + rad * R
            panels.append(Panel(name + "_sleeve" + sd, sedges, sleeve_place, fabric))
            # ⛔ one front, or the fronts' own side: `split_front <= 0` meant "one front" until a gap (< 0) sewed
            # the right sleeve to the LEFT front's arm_R, an edge it does not have
            fp = fronts[0][0] if (sd == "L" or len({f for f, _ in fronts}) == 1) else fronts[1][0]
            # the armhole runs front underarm -> shoulder -> back underarm; the cap (reversed in its outline) runs
            # +x -> -x, front -> back: sewn as given
            if sd == "L":
                hole = [fp + ":arm_L", name + "_back:arm_L~"]
            else:
                hole = [fp + ":arm_R~", name + "_back:arm_R"]
            seams.append(Seam(name + "_sleeve" + sd + ":cap", hole, when=(0.20, 0.70), auto=False))
            seams.append(Seam(name + "_sleeve" + sd + ":under_a", name + "_sleeve" + sd + ":under_b", when=(0.45, 1.0)))
    if collar > 0.0:
        neck_len = sum(math.dist(a, b) for a, b in zip(bk_neck[:-1], bk_neck[1:])) * 2 + \
            (sum(math.dist(a, b) for a, b in zip(neckline(neck_drop)[:-1], neckline(neck_drop)[1:])) * 2 if split_front == 0.0 else 0.0)
        cedges = [("base", [(-neck_len / 2, 0.0), (neck_len / 2, 0.0)]), ("end_L", [(neck_len / 2, 0.0), (neck_len / 2, collar)]),
                  ("top", [(neck_len / 2, collar), (-neck_len / 2, collar)]), ("end_R", [(-neck_len / 2, collar), (-neck_len / 2, 0.0)])]

        def collar_place(x, y):
            v = z["neck"] + y * 0.8 - 0.004
            th = math.pi - x / (circ(T, v) / (2 * math.pi))
            return T.pt(th, v) + T.normal(th, v) * (clear * 0.6)
        panels.append(Panel(name + "_collar", cedges, collar_place, fabric))
        neckedges = [name + "_back:neck_L", name + "_back:neck_R"]
        if split_front == 0.0:
            neckedges = [name + "_front:neck_R", name + "_back:neck_R", name + "_back:neck_L", name + "_front:neck_L"]
        seams.append(Seam(name + "_collar:base", neckedges, when=(0.55, 1.0)))
    if rib:
        # ⭐ KNIT FINISHES, cut as FreeSewing's Sven cuts them: the hem band is the hips (as a tape measures them, over
        # what is worn under it) plus its ease, less the rib's stretch; each cuff the wrist plus 20%, less the stretch;
        # the neckband the neckline less its stretch. Each is shorter than what it is sewn to, and gathers it
        assert split_front == 0.0, "rib bands: a single front only"
        st = rib.get("stretch", 0.15)
        rf = rib.get("fabric", fabric)
        h = rib.get("h", 0.045)
        v_b = hem0 + h * 0.5
        hips = circ(T, v_b) + 2 * math.pi * lay_t(v_b)
        P_, S_ = band(name + "_hemband", panels, [name + "_front:hem", name + "_back:hem~"],
                      hips * (1.0 + rib.get("ease", 0.08)) * (1.0 - st), h, (0.0, 0.0, -1.0), join="top", fabric=rf)
        panels.append(P_); seams += S_
        if sleeve_t is not None:
            ch = rib.get("cuff_h", h)
            for sd in ("L", "R"):
                side_ = "Left" if sd == "L" else "Right"
                wrist = circ(F.arm[side_], sleeve_t0)
                P_, S_ = band(name + "_cuff" + sd, panels, [name + "_sleeve" + sd + ":cuff"],
                              wrist * (1.0 + rib.get("cuff_ease", 0.20)) * (1.0 - st), ch, Vector(F.armb[side_]["d"]),
                              join="top", fabric=rf)
                panels.append(P_); seams += S_
        if rib.get("neck_h", 0.0) > 0.0:
            nspec = [name + "_front:neck_R", name + "_back:neck_R~", name + "_back:neck_L~", name + "_front:neck_L"]
            nl = 2.0 * (plen(flat_f.edge(neckline(neck_drop))) + plen(flat_b.edge(neckline(back_drop))))
            # sewn on AFTER the shoulders are closed: gathered at once, it pulled the neck points 13 mm apart
            P_, S_ = band(name + "_neckband", panels, nspec, nl * (1.0 - rib.get("neck_stretch", st)),
                          rib["neck_h"], (0.0, 0.0, 1.0), join="bottom", fabric=rf, when=rib.get("neck_when", (0.70, 1.0)))
            panels.append(P_); seams += S_
    return panels, seams


# ------------------------------------------------------------------ trousers and shorts
def trousers(F, name, waist, hem, ease_hip=0.05, ease_leg=0.10, hem_w=None, ext_f=None, ext_b=None, clear_min=0.006,
             rise_ease=0.022, fabric=None, waistband=None):
    """four panels (front and back, each leg), drafted the way a pattern cutter drafts them: the CROTCH LINE is the
    hip's quarter plus a crotch extension (hip/16 in front, hip/8 behind -- the fabric that wraps between the legs),
    the leg narrows from there to the knee and the hem round a crease, the front a little narrower than the back;
    the waist PINNED to the body (a waistband). hem is a height -- the ankle for trousers, mid-thigh for shorts;
    hem_w: the half-hem's width (a straight leg), else the leg's own girth plus ease.
    ⛔ The first draft took the leg's width from the THIGH (0.33) and the hip's from the torso (0.26): the outline
    jogged 7 cm at the crotch line, the jog was laid round past the side seam, and both hips grew a fin.
    waistband: {"h", "stretch", "fabric"} -- an ELASTIC CASING instead of a pinned waist: a band the body's girth less
    its stretch, the waist gathered into it (Titan's waistband is 3% of waist-to-floor, snapped to an elastic's width).
    `waist` stays the top of the finished waistband."""
    T = F.torso
    wb = dict(waistband) if waistband else None
    if wb: waist = waist - wb.get("h", 0.030)          # the legs end where the band begins
    # ⛔ THE CROTCH LINE HAS EASE UNDER IT. Drafted AT the form's crotch height, the suit man's trousers were cut
    # 1.4 cm short of the bottom of his body and the four panels' corner could not pass under him: the inseams stayed
    # 31 mm open there. A tailor drops the crotch line below the body's own by the crotch ease (2-3 cm in a suit)
    zc = F.z["crotch"] - rise_ease
    hip = max(circ(T, v) for v in np.linspace(F.z["crotch"], F.z["waist"], 9))
    # the crotch extensions carry the cloth UNDER the body, front to back: together they are the body's own depth
    # there plus 2 cm, a third in front and two behind. ⛔ From the hip (hip/16, hip/8) they were 15 cm on a body
    # 16.4 cm deep at the crotch: the corner where the four panels meet could not close under him (17 mm)
    depth = T.pt(math.pi, F.z["crotch"]).y - T.pt(0.0, F.z["crotch"]).y
    ext_f = (depth + 0.020) / 3.0 if ext_f is None else ext_f
    ext_b = (depth + 0.020) * 2.0 / 3.0 if ext_b is None else ext_b
    y_c = zc - hem
    # the crotch point, under the body where the four panels meet -- MEASURED: the highest point below the crotch
    # where the thighs are apart enough for cloth to pass between them. ⛔ At a fixed 1.2 cm below, the suit man's
    # thighs still touched: the front and back crotch points were pushed out to either side of that wall and the
    # inseams stayed 47 mm open at the top
    pf, pb = T.pt(0.0, zc), T.pt(math.pi, zc)
    cz = zc - 0.012
    for k in range(40):
        z_ = zc - 0.004 - 0.002 * k
        gap = F.leg[1].pt(-math.pi / 2, z_).x - F.leg[-1].pt(-math.pi / 2, z_).x
        if gap >= 0.012:
            cz = min(zc - 0.012, z_ - 0.004); break
    # ...and a THIRD of the way back, not half: the front's extension is half the back's, and the corner where the four
    # panels meet lies where each can reach (at the middle, the fronts fell 17 mm short of it)
    C = Vector((0.0, pf.y + (pb.y - pf.y) * ext_f / (ext_f + ext_b), cz))
    panels, seams = [], []
    for sd, sg in (("L", 1.0), ("R", -1.0)):
        L = F.leg[int(sg)]
        for part, ctr, ext, share in (("front", 0.0, ext_f, 0.92), ("back", math.pi, ext_b, 1.08)):
            amap = ArcMap(T, ctr, sg if part == "front" else -sg)
            lmap_o = ArcMap(L, 0.0 if part == "front" else math.pi, 1.0 if part == "front" else -1.0)   # to the outside
            lmap_i = ArcMap(L, 0.0 if part == "front" else math.pi, -1.0 if part == "front" else 1.0)   # to the inside
            hq = lambda v, amap=amap: amap.half(v) + ease_hip / 4.0           # a quarter of the hips
            Wc = hq(zc) + ext                                                   # the crotch line
            xc = (hq(zc) - ext) / 2.0                                           # the crease
            leg_half = lambda v, lo=lmap_o, li=lmap_i: lo.half(v) + li.half(v)  # this half of the leg's girth

            def Wp(v, share=share, Wc=Wc, leg_half=leg_half):
                w = leg_half(v) + ease_leg / 2.0 * share
                if hem_w is not None: w = max(w, hem_w * share)
                k = smoothstep_(zc - 0.14, zc, v)
                return w * (1 - k) + Wc * k
            zs_lo = np.linspace(hem, zc, 14)
            out_lo = [(xc + Wp(v) / 2.0, v - hem) for v in zs_lo]
            in_lo = [(xc - Wp(v) / 2.0, v - hem) for v in zs_lo]
            zs_hi = np.linspace(zc, waist, 8)
            out_hi = [(hq(v), v - hem) for v in zs_hi]
            rise = waist - hem + (0.018 if part == "back" else 0.0)
            # ⛔ the curve's stations are SHARES of the rise: fixed offsets (0.110 up to the straight) fit REVA's 15 cm
            # rise and hooked back down on the suit man's 10.5 cm -- a kink the inseams could not close past (47 mm)
            rh = rise - y_c
            crotch = smooth([(-ext, y_c), (-ext * 0.45, y_c + 0.17 * rh), (-ext * 0.08, y_c + 0.45 * rh),
                             (0.0, y_c + 0.75 * rh), (0.0, rise)], 14)
            out = out_lo + out_hi[1:]          # the side seam ends at the waist; the BACK rises toward its centre
            edges = [("hem", [in_lo[0], out_lo[0]]), ("out", out), ("waist", [out[-1], (0.0, rise)]),
                     ("crotch", list(reversed(crotch))), ("inseam", list(reversed(in_lo)))]

            def place(x, y, amap=amap, L=L, part=part, sg=sg, ext=ext, Wp=Wp, xc=xc, hq=hq, lmap_o=lmap_o,
                      lmap_i=lmap_i, share=share):
                v = hem + y
                if v < zc:                                     # the leg: inseam -> front (or back) -> outseam
                    w = Wp(v)
                    s = max(0.0, min(1.0, (x - (xc - w / 2.0)) / max(w, 1e-4)))
                    ph = (-90.0 + 180.0 * s) if part == "front" else (270.0 - 180.0 * s)
                    u = math.radians(ph)
                    d = max(clear_min, (w - lmap_o.half(v) - lmap_i.half(v)) / math.pi)
                    return L.pt(u, v) + L.normal(u, v) * d
                if x >= 0.0:                                   # the hips: centre -> side seam
                    h = hq(v)
                    s = min(1.0, x / max(h, 1e-4))
                    qa = amap.half(v)
                    th = amap.angle(v, s * qa)
                    d = max(clear_min, (h - qa) / (math.pi / 2))
                    return T.pt(th, v) + T.normal(th, v) * d
                # the crotch extension: from the centre line, down and under to where the four panels meet
                base = T.pt(amap.ac, v) + T.normal(amap.ac, v) * clear_min
                f = min(1.0, -x / max(ext, 1e-4))
                return base + (C - base) * f
            # ⛔ PINNED, the sleep shorts' waist was squeezed onto the body point nearest each placed stitch: the
            # extra cloth gathered where the squeeze added up, and both hips grew a flap. An elastic casing gathers it
            # evenly (each stitch at the same share of the way round)
            panels.append(Panel("%s_%s%s" % (name, part, sd), edges, place, fabric, pin=[] if wb else ["waist"]))
        seams.append(Seam("%s_front%s:out" % (name, sd), "%s_back%s:out" % (name, sd), when=(0.0, 0.8)))
        seams.append(Seam("%s_front%s:inseam" % (name, sd), "%s_back%s:inseam" % (name, sd), when=(0.0, 0.8)))
    seams.append(Seam("%s_frontL:crotch" % name, "%s_frontR:crotch" % name, when=(0.2, 1.0)))
    seams.append(Seam("%s_backL:crotch" % name, "%s_backR:crotch" % name, when=(0.2, 1.0)))
    print("[draft] %s: hip %.0f cm, crotch extensions %.1f / %.1f cm" % (name, hip * 100, ext_f * 100, ext_b * 100))
    if wb:
        hb = wb.get("h", 0.030)
        P_, S_ = band(name + "_waistband", panels,
                      ["%s_frontL:waist~" % name, "%s_backL:waist" % name, "%s_backR:waist~" % name, "%s_frontR:waist" % name],
                      circ(T, waist + hb * 0.5) * (1.0 - wb.get("stretch", 0.08)), hb, (0.0, 0.0, 1.0), join="bottom",
                      fabric=wb.get("fabric", fabric), when=(0.5, 1.0), grip=wb.get("grip", True))
        panels.append(P_); seams += S_
    return panels, seams


def smoothstep_(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


# ------------------------------------------------------------------ a skirt
def skirt(F, name, waist, length, flare=1.0, clear=0.030, fabric=None):
    """two panels, front and back, each a sector of a ring (flare 0 = straight, 1 = a half circle each): the waist
    PINNED, the side seams sewn; it hangs"""
    T = F.torso
    wc = circ(T, waist) + 0.02
    ang = math.pi * (0.25 + 0.75 * flare) / 2.0             # each panel's half-angle
    r0 = (wc / 2.0) / (2 * ang)
    r1 = r0 + length
    panels, seams = [], []
    for part, ctr in (("front", 0.0), ("back", math.pi)):
        n = 24
        inner = [(r0 * math.sin(a), -r0 * math.cos(a)) for a in np.linspace(ang, -ang, n)]
        outer = [(r1 * math.sin(a), -r1 * math.cos(a)) for a in np.linspace(-ang, ang, n)]
        edges = [("hem", outer), ("side_L", [outer[-1], inner[0]]), ("waist", inner), ("side_R", [inner[-1], outer[0]])]
        amap = ArcMap(T, ctr, 1.0 if part == "front" else -1.0)

        def place(x, y, part=part, ctr=ctr):
            rr = math.hypot(x, y); a = math.atan2(x, -y)            # a: -ang .. ang across the panel
            th = ctr + (a / ang) * (math.pi / 2) * (1.0 if part == "front" else -1.0)
            v = waist - (rr - r0)
            pv = max(v, 0.80)
            p = T.pt(th, pv) + T.normal(th, pv) * (clear + max(0.0, rr - r0) * 0.25 * flare)
            p.z = v
            return p
        panels.append(Panel("%s_%s" % (name, part), edges, place, fabric, pin=["waist"]))
    seams.append(Seam(name + "_front:side_L", name + "_back:side_L"))
    seams.append(Seam(name + "_front:side_R", name + "_back:side_R"))
    return panels, seams


# ------------------------------------------------------------------ a tie
def tie(F, name, top_z, tip_z, clear=0.012, fabric=None):
    """a flat tie: narrow under the knot, widening to a point; its top PINNED at the collar -- it hangs on the shirt"""
    L = top_z - tip_z
    edges = [("tip_R", [(0.0, 0.0), (0.042, 0.040)]), ("side_L", [(0.042, 0.040), (0.018, L)]),
             ("top", [(0.018, L), (-0.018, L)]), ("side_R", [(-0.018, L), (-0.042, 0.040)]),
             ("tip_L", [(-0.042, 0.040), (0.0, 0.0)])]
    T = F.torso

    def place(x, y):
        v = tip_z + y
        th = x / (circ(T, v) / (2 * math.pi))
        return T.pt(th, v) + T.normal(th, v) * clear
    return [Panel(name, edges, place, fabric, pin=["top"])], []


# ------------------------------------------------------------------ tailoring on a garment already made
def _frame_along(line, surf_V, surf_N, away):
    """for each point of a 3D line lying on a sewn garment: the garment's normal there and the direction on its surface
    square to the line, turned toward `away` (a vector the direction must agree with)"""
    L = np.asarray(line, float)
    SV, SN = np.asarray(surf_V, float), np.asarray(surf_N, float)
    out = []
    for i, p in enumerate(L):
        k = np.argsort(np.linalg.norm(SV - p, axis=1))[:6]
        n = SN[k].mean(0); n /= max(np.linalg.norm(n), 1e-9)
        tg = L[min(i + 1, len(L) - 1)] - L[max(i - 1, 0)]; tg /= max(np.linalg.norm(tg), 1e-9)
        d = np.cross(n, tg); d /= max(np.linalg.norm(d), 1e-9)
        a = away(p) if callable(away) else np.asarray(away, float)
        if np.dot(d, a) < 0: d = -d
        out.append((n, d))
    return out


def _along(L, s):
    """the point of polyline L at arc share s (0..1)"""
    L = np.asarray(L, float)
    c = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(L, axis=0), axis=1))])
    x = s * c[-1]
    j = min(max(int(np.searchsorted(c, x, side="right")) - 1, 0), len(L) - 2)
    t = 0.0 if c[j + 1] - c[j] < 1e-12 else (x - c[j]) / (c[j + 1] - c[j])
    return j, t, L[j] * (1 - t) + L[j + 1] * t


def lapel(name, roll, surf_V, surf_N, side, width=0.062, notch=0.020, notch_h=0.030, lift=0.004, fabric=None):
    """a NOTCHED LAPEL: a jacket front's revers, folded back to lie on it -- a flat panel SEWN along its roll edge to
    the front's V edge (roll: its 3D points from the top button UP to the neck, off the sewn jacket), lying over the
    front toward the shoulder (side +1 = the body's left), its top cut by the notch where the collar meets it"""
    roll = np.asarray(roll, float)
    Lr = float(np.sum(np.linalg.norm(np.diff(roll, axis=0), axis=1)))
    fr = _frame_along(roll, surf_V, surf_N, (side, 0.0, 0.0))
    w = lambda y: width * min(1.0, (y / max(Lr - notch_h, 1e-6)) ** 0.75)      # widening from the button to the notch
    outer = [(w(y), y) for y in np.linspace(0.0, Lr - notch_h, 10)]
    top = [(width, Lr - notch_h), (width - notch, Lr - notch_h + notch * 0.4), (notch * 0.55, Lr), (0.0, Lr)]
    edges = [("outer", outer), ("notch", top), ("roll", [(0.0, Lr), (0.0, 0.0)])]
    roll_line = [tuple(p) for p in roll[::-1]]            # the roll edge runs top -> bottom, as the outline does

    def place(x, y):
        j, t, p = _along(roll, max(0.0, min(1.0, y / max(Lr, 1e-9))))
        n = fr[j][0] * (1 - t) + fr[j + 1][0] * t
        d = fr[j][1] * (1 - t) + fr[j + 1][1] * t
        n /= max(np.linalg.norm(n), 1e-9); d /= max(np.linalg.norm(d), 1e-9)
        return Vector(tuple(p + d * x + n * (lift + 0.2 * x * 0.0)))
    return Panel(name, edges, place, fabric, attach={"roll": roll_line})


def collar_band(name, neck, surf_V, surf_N, stand=0.018, fall=0.042, points=0.0, lift=0.004, fabric=None):
    """a COLLAR: a band sewn to a neckline (neck: its 3D line, front-right round the back to front-left, off the sewn
    garment), standing `stand` up the neck and folded down over the garment by `fall`; points > 0 draws a shirt
    collar's points at both front ends (that much longer)"""
    neck = np.asarray(neck, float)
    Ln = float(np.sum(np.linalg.norm(np.diff(neck, axis=0), axis=1)))
    ctr = neck.mean(0)
    fr = _frame_along(neck, surf_V, surf_N, lambda p: (p[0] - ctr[0], p[1] - ctr[1], 0.0))
    xs = np.linspace(0.0, Ln, 24)
    drop = lambda x: fall + points * max(0.0, 1.0 - min(x, Ln - x) / 0.035) ** 1.5    # the points, at the ends
    bottom = [(x, -drop(x)) for x in xs]
    edges = [("fall", bottom), ("end_L", [(Ln, -drop(Ln)), (Ln, 0.0)]),
             ("base", [(x, 0.0) for x in xs[::-1]]), ("end_R", [(0.0, 0.0), (0.0, -drop(0.0))])]

    def place(x, y):
        j, t, p = _along(neck, max(0.0, min(1.0, x / max(Ln, 1e-9))))
        n = fr[j][0] * (1 - t) + fr[j + 1][0] * t
        n /= max(np.linalg.norm(n), 1e-9)
        up = np.array((0.0, 0.0, 1.0))
        # the band goes UP the stand from the neckline, then folds DOWN over it: -y is the fall
        top = p + up * stand + n * (lift * 2.5)
        fold = -up * 0.55 + n * 0.83                          # folded down AND out, over the garment
        return Vector(tuple(top + fold / np.linalg.norm(fold) * (-y)))
    base_line = [tuple(p + np.array((0.0, 0.0, stand)) + fr[min(i, len(fr) - 1)][0] * lift * 2.5)
                 for i, p in enumerate(neck)][::-1]          # the base runs left -> right, as the outline does
    return Panel(name, edges, place, fabric, attach={"base": base_line})


def collar_stand(name, neck, surf_V, surf_N, height=0.030, points=0.040, lift=0.003, fabric=None):
    """a SHIRT COLLAR as it shows under a jacket: its stand, a band up the neck from the neckline (neck: 3D, front-right
    round the back to front-left), and its two POINTS, folded down onto the chest either side of the tie's knot"""
    neck = np.asarray(neck, float)
    Ln = float(np.sum(np.linalg.norm(np.diff(neck, axis=0), axis=1)))
    ctr = neck.mean(0)
    fr = _frame_along(neck, surf_V, surf_N, lambda p: (p[0] - ctr[0], p[1] - ctr[1], 0.0))
    tip = lambda x: points * max(0.0, 1.0 - min(x, Ln - x) / 0.045)      # the points, only near the front ends
    xs = np.linspace(0.0, Ln, 28)
    edges = [("top", [(x, height) for x in xs[::-1]]), ("end_R", [(0.0, height), (0.0, -tip(0.0))]),
             ("bottom", [(x, -tip(x)) for x in xs]), ("end_L", [(Ln, -tip(Ln)), (Ln, height)])]

    def place(x, y):
        j, t, p = _along(neck, max(0.0, min(1.0, x / max(Ln, 1e-9))))
        n = fr[j][0] * (1 - t) + fr[j + 1][0] * t
        n /= max(np.linalg.norm(n), 1e-9)
        up = np.array((0.0, 0.0, 1.0))
        if y >= 0:                                           # the stand: straight up the neck
            q = p + up * y + n * lift
        else:                                                # a point: folded down and out over the shirt
            fold = -up * 0.75 + n * 0.66
            q = p + n * (lift * 2) + fold / np.linalg.norm(fold) * (-y)
        return Vector(tuple(q))
    return Panel(name, edges, place, fabric)


# ------------------------------------------------------------------ what a tailored garment is built over
def shoulder_pads(F, width=0.075, depth=0.070, thick=0.014, reach=0.012):
    """SHOULDER PADS: a firm, flattened wedge on each shoulder top, thickest at the shoulder's point -- a layer a coat
    or a jacket is sewn over (sewing.sew(layers=...) or the field's extra), so its shoulder seam rests on a level ledge.
    ⛔ Without them REVA's long coat slid off her sloped shoulders and down her 45-degree arms, buttoned or not.
    Returns [(V, T), (V, T)] for the two pads."""
    T = F.torso
    out = []
    for sg in (1.0, -1.0):
        # the shoulder's ridge: the torso ring's side between the neck and the shoulder's point
        zs = np.linspace(F.z["shoulder"] - 0.03, F.z["neck"] - 0.012, 6)
        ridge = [T.pt(sg * math.pi / 2, z) for z in zs]
        V, Tr = [], []
        n_a, n_b = 6, 7
        for i, rp in enumerate(ridge):
            f = i / (len(ridge) - 1)                              # 0 at the shoulder's point, 1 at the neck
            th_ = thick * (1.0 - 0.75 * f)                        # thickest out at the point
            for j in range(n_b):
                b = (j / (n_b - 1) - 0.5) * 2.0                   # front (-1) .. back (+1)
                y = rp.y + b * depth * 0.5 * (1.0 - 0.3 * f)
                lift = th_ * max(0.0, 1.0 - b * b) + 0.004
                V.append((rp.x + sg * reach * (1.0 - f), y, rp.z + lift))
        for i in range(len(ridge) - 1):
            for j in range(n_b - 1):
                a = i * n_b + j
                Tr += [(a, a + 1, a + n_b + 1), (a, a + n_b + 1, a + n_b)]
        out.append((np.array(V), np.array(Tr)))
    return out
