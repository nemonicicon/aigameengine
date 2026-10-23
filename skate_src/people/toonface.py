"""toonface.py -- THE WORLD AT THE END OF TIME's faces: Atlantis-leaning design, Futurama rendering (design/
wateot_series.md, THE LOOK). A face is SHAPES lying a hair above a Person's head: the eye's white, the iris and
pupil CLIPPED to it, the upper lid's ink, a crease, the brows, the mouth's line, the nostrils, and -- for the old --
a handful of ink lines where the years are. Everything is placed in FACE COORDINATES (a = degrees round the head
from the middle of the face, u = share of head height from the chin), so a blink, a brow or a look is moving
points, and a shape stays crisp at any size.

    face_part(person)   -> Part "face" (materials M_Paint; swatches named in the spec's "face" palette keys)
    hair_locks(person, P) adds the spec's sculpted locks to a hair Part

spec["face"] = {"eye_w": half-width (deg), "eye_up"/"eye_lo": opening above/below the eye line (u), "tilt": outer
corner lift (u), "iris": share of the opening's height, "look": (da, du) where the eyes look, "lid": upper-lid ink
width (m), "crease": 0/1, "brow": {...}, "mouth_w": half-width (deg), "age": 0..1 (the lines), "blush": 0/1}
"""
import math

from mathutils import Vector

from atelier import Part

HEAD = {"Head": 1.0}


# ------------------------------------------------------------------ metric face space: (a, u) <-> (x, y) in metres
class FaceSpace:
    def __init__(self, person):
        self.p = person
        self.R = person.HB * 0.98                    # the face's radius round its axis, near the eyes
        self.hh = person.S["head_h"]

    def m(self, a, u):
        return (math.radians(a) * self.R, u * self.hh)

    def au(self, x, y):
        return (math.degrees(x / self.R), y / self.hh)


def _orient(P, tri_or_quad, person):
    """wind a face so its normal points out of the head"""
    pts = [Vector(P.V[i]) for i in tri_or_quad]
    n = (pts[1] - pts[0]).cross(pts[2] - pts[0])
    c = sum(pts, Vector()) / len(pts)
    o = Vector((c.x, c.y - person.head_cy, c.z - (person.Z["chin"] + 0.5 * person.S["head_h"])))
    return tri_or_quad if n.dot(o) >= 0 else tuple(reversed(tri_or_quad))


def shape(P, person, poly_au, off, sw, rings=3, mat="M_Paint"):
    """a filled shape: poly in (a, u), laid on the head `off` metres out, as concentric rings to its centre (so it
    follows the head's curve)"""
    if len(poly_au) < 3: return
    ca = sum(a for a, u in poly_au) / len(poly_au)
    cu = sum(u for a, u in poly_au) / len(poly_au)
    loops = []
    for k in range(rings, 0, -1):
        t = k / rings
        loops.append([P.v(person.face_xyz(ca + (a - ca) * t, cu + (u - cu) * t, off), HEAD) for a, u in poly_au])
    c = P.v(person.face_xyz(ca, cu, off), HEAD)
    n = len(poly_au)
    for A, B in zip(loops[:-1], loops[1:]):
        for j in range(n):
            P.f(_orient(P, (A[j], A[(j + 1) % n], B[(j + 1) % n], B[j]), person), mat, ("swatch", sw))
    for j in range(n):
        P.f(_orient(P, (loops[-1][j], loops[-1][(j + 1) % n], c), person), mat, ("swatch", sw))


def strip(P, person, line_au, widths, off, sw, mat="M_Paint"):
    """an INK LINE: a polyline in (a, u), each point its own width in metres (a taper is a list)"""
    F = FaceSpace(person)
    M = [F.m(a, u) for a, u in line_au]
    if isinstance(widths, (int, float)): widths = [widths] * len(M)
    L, Rr = [], []
    for i, (x, y) in enumerate(M):
        xa, ya = M[max(i - 1, 0)]; xb, yb = M[min(i + 1, len(M) - 1)]
        tx, ty = xb - xa, yb - ya
        ln = math.hypot(tx, ty) or 1.0
        nx, ny = -ty / ln, tx / ln
        w = widths[i] / 2.0
        L.append(P.v(person.face_xyz(*F.au(x + nx * w, y + ny * w), off), HEAD))
        Rr.append(P.v(person.face_xyz(*F.au(x - nx * w, y - ny * w), off), HEAD))
    for i in range(len(M) - 1):
        P.f(_orient(P, (L[i], L[i + 1], Rr[i + 1], Rr[i]), person), mat, ("swatch", sw))


def taper(n, w0, wmax, w1, peak=0.5):
    out = []
    for i in range(n):
        t = i / (n - 1)
        if t <= peak: out.append(w0 + (wmax - w0) * math.sin(0.5 * math.pi * t / peak))
        else: out.append(w1 + (wmax - w1) * math.sin(0.5 * math.pi * (1 - t) / (1 - peak)))
    return out


def clip(subject, clipper):
    """Sutherland-Hodgman: a convex `clipper` cuts `subject` (both lists of (x, y), counter-clockwise or not)"""
    def inside(p, a, b, sgn):
        return sgn * ((b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0])) >= 0

    def inter(p, q, a, b):
        x1, y1, x2, y2, x3, y3, x4, y4 = p[0], p[1], q[0], q[1], a[0], a[1], b[0], b[1]
        d = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4)
        if abs(d) < 1e-15: return q
        t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / d
        return (x1 + t * (x2 - x1), y1 + t * (y2 - y1))
    area = sum(clipper[i][0] * clipper[(i + 1) % len(clipper)][1] - clipper[(i + 1) % len(clipper)][0] * clipper[i][1]
               for i in range(len(clipper)))
    sgn = 1 if area > 0 else -1
    out = list(subject)
    for i in range(len(clipper)):
        a, b = clipper[i], clipper[(i + 1) % len(clipper)]
        inp, out = out, []
        if not inp: break
        s_ = inp[-1]
        for e in inp:
            if inside(e, a, b, sgn):
                if not inside(s_, a, b, sgn): out.append(inter(s_, e, a, b))
                out.append(e)
            elif inside(s_, a, b, sgn):
                out.append(inter(s_, e, a, b))
            s_ = e
    return out


# ------------------------------------------------------------------ the eye
def eye_outline(person, sg, f, mk, blink=0.0):
    """the eye's opening in (a, u): upper edge inner corner -> outer, then lower edge back. sg +1 = the body's left
    eye (+a). blink 0..1 brings the upper lid down to the lower"""
    ea, eu = mk["eye_a"], mk["eu"]
    w, up, lo, tilt = f["eye_w"], f["eye_up"], f["eye_lo"], f.get("tilt", 0.0)
    N = 16
    upper, lower = [], []
    for i in range(N + 1):
        s = i / N                                   # 0 inner corner, 1 outer
        a = sg * (ea - w + 2.0 * w * s)
        base = eu - 0.004 + tilt * s
        lo_ = base - lo * math.sin(math.pi * s ** 1.15)
        up_ = base + up * math.sin(math.pi * s ** 0.80) * (1.0 - blink) + (lo_ - base) * blink * 0.95
        upper.append((a, up_))
        lower.append((a, lo_))
    return upper, lower


def build_eye(P, person, sg, f, mk, pal):
    F = FaceSpace(person)
    upper, lower = eye_outline(person, sg, f, mk)
    almond = upper + lower[::-1][1:-1]
    shape(P, person, almond, 0.0007, "eye_white", rings=2)
    # the iris and pupil: circles in METRES, clipped to the opening
    ea, eu = mk["eye_a"], mk["eu"]
    la, lu = f.get("look", (0.0, 0.0))
    hgt = (f["eye_up"] + f["eye_lo"]) * F.hh
    cx, cy = F.m(sg * ea + sg * la, eu + lu - 0.15 * f["eye_lo"])
    am = [F.m(a, u) for a, u in almond]

    def disc(r, n=24, dy=0.0):
        return [(cx + r * math.cos(2 * math.pi * k / n), cy + dy + r * math.sin(2 * math.pi * k / n)) for k in range(n)]
    for r, sw, off in ((f["iris"] * hgt * 0.62, "iris", 0.0009), (f["iris"] * hgt * 0.30, "pupil", 0.0011)):
        c = clip(disc(r), am)
        if len(c) >= 3: shape(P, person, [F.au(x, y) for x, y in c], off, sw, rings=2)
    # the catchlight: a small dot up and toward the key light's side of the iris
    hl = clip([(x + f["iris"] * hgt * 0.22 * -sg, y + f["iris"] * hgt * 0.22)
               for x, y in disc(f["iris"] * hgt * 0.11, 10)], am)
    if len(hl) >= 3: shape(P, person, [F.au(x, y) for x, y in hl], 0.0013, "eye_white", rings=1)
    # the upper lid: a heavy ink line along the whole upper edge, thick in the middle, a flick past the outer corner
    lid = f.get("lid", 0.0018) * person.S["head_h"] / 0.22
    ext = upper + [(upper[-1][0] + sg * f["eye_w"] * 0.16, upper[-1][1] + f["eye_up"] * 0.10)]
    strip(P, person, ext, taper(len(ext), lid * 0.35, lid, lid * 0.25, 0.55), 0.0015, "ink")
    # the lower lid: a short thin line on the outer half
    lw = lower[len(lower) // 2:]
    strip(P, person, lw, taper(len(lw), lid * 0.15, lid * 0.40, lid * 0.10, 0.6), 0.0015, "lid_lo")
    if f.get("crease", 0):
        cr = [(a, u + f["eye_up"] * (0.95 + 0.25 * math.sin(math.pi * i / (len(upper) - 1))))
              for i, (a, u) in enumerate(upper)][3:-2]
        strip(P, person, cr, taper(len(cr), lid * 0.08, lid * 0.30, lid * 0.06, 0.5), 0.0012, "crease")


def build_brow(P, person, sg, f, mk):
    b = f["brow"]
    ea, eu = mk["eye_a"], mk["eu"]
    a0, a1 = sg * (ea - f["eye_w"] * b.get("in", 1.05)), sg * (ea + f["eye_w"] * b.get("out", 1.10))
    u0 = eu + f["eye_up"] + b["gap"]
    N = 10
    line = []
    for i in range(N + 1):
        s = i / N
        pk = b.get("peak", 0.65)
        lift = b["arch"] * (s / pk if s < pk else 1.0 - (s - pk) / (1 - pk) * b.get("drop", 1.3))
        line.append((a0 + (a1 - a0) * s, u0 + lift - b.get("tilt", 0.0) * s))
    th = b["thick"] * person.S["head_h"] / 0.22
    strip(P, person, line, taper(N + 1, th * 1.0, th * 0.95, th * 0.25, 0.25), 0.0016, "brow")


def build_mouth(P, person, f, mk):
    mu = mk["mu"]
    mw = f.get("mouth_w", 20.0)
    lf = f.get("lip_fill")                          # (upper height, lower height) in u: full lips, filled
    if lf:
        up, lo = lf
        upper = [(-mw, mu)] + [(-mw + 2 * mw * i / 10, mu + up * (math.sin(math.pi * i / 10) ** 0.6)
                                - up * 0.25 * math.exp(-((i - 5) / 1.2) ** 2)) for i in range(1, 10)] + [(mw, mu)]
        lower = [(mw * 0.92, mu)] + [(mw * 0.92 - 1.84 * mw * i / 10, mu - lo * math.sin(math.pi * i / 10) ** 0.7)
                                     for i in range(1, 10)] + [(-mw * 0.92, mu)]
        shape(P, person, upper, 0.0009, "lip", rings=2)
        shape(P, person, lower, 0.0009, "lip", rings=2)
    N = 12
    line = [(-mw + 2 * mw * i / N, mu - f.get("smile", 0.0) * (1 - (2 * i / N - 1) ** 2) * -1.0) for i in range(N + 1)]
    lw = f.get("mouth_ink", 0.0011) * person.S["head_h"] / 0.22
    strip(P, person, line, taper(N + 1, lw * 0.35, lw, lw * 0.35, 0.5), 0.0012, "ink")
    # the lower lip: a short shadow stroke under the middle
    ll = [(-mw * 0.40 + 0.8 * mw * i / 6, mu - f.get("lower_lip", 0.045)) for i in range(7)]
    strip(P, person, ll, taper(7, lw * 0.2, lw * 0.9, lw * 0.2, 0.5), 0.0010, "lip_shadow")


def build_nostrils(P, person, f, mk):
    """the nose as ONE stroke, the way Atlantis drew it: down the bridge on one side, round under the tip, and
    into a single nostril -- plus a small wing on the far side. (jacob, 10-18, "their noses look weird": a bridge
    line, a squiggle of nostril ticks and an under-stroke were three marks where a drawing makes one.)"""
    ut = mk["ut"]
    k = person.S["head"].get("nose_wing", 11.0) / 11.0
    lw = 0.0011 * person.S["head_h"] / 0.22
    line = [(3.0, ut + 0.075), (3.8, ut + 0.045), (4.9, ut + 0.016), (5.4 * k, ut - 0.002), (4.6 * k, ut - 0.014),
            (2.4 * k, ut - 0.020), (0.0, ut - 0.021), (-2.6 * k, ut - 0.018)]
    strip(P, person, line, taper(len(line), lw * 0.05, lw * 0.95, lw * 0.35, 0.55), 0.0011, "ink")
    wing = [(-5.2 * k, ut - 0.004), (-6.2 * k, ut - 0.012), (-5.2 * k, ut - 0.019)]
    strip(P, person, wing, taper(3, lw * 0.15, lw * 0.6, lw * 0.15, 0.5), 0.0011, "ink")


def build_age(P, person, f, mk):
    """the years as Atlantis drew them: a few lines, each one placed"""
    age = f.get("age", 0.0)
    if age <= 0: return
    ea, eu, ut, mu = mk["eye_a"], mk["eu"], mk["ut"], mk["mu"]
    lw = 0.00085 * person.S["head_h"] / 0.22
    for sg in (1, -1):
        # the nasolabial fold: from the nose's wing round the cheek to beside the mouth's corner
        nl = [(sg * (11.0 + 9.0 * t ** 1.2), ut - 0.015 - (ut - mu + 0.005) * t) for t in [i / 8 for i in range(9)]]
        strip(P, person, nl, taper(9, lw * 0.3, lw * 1.2, lw * 0.4, 0.4), 0.0012, "age")
        # crow's feet: three short lines fanning from the outer corner
        oa = sg * (ea + f["eye_w"] + 3.0)
        for k, (da, du) in enumerate(((5.0, 0.020), (6.0, 0.0), (5.0, -0.018))):
            ln = [(oa, eu + du * 0.3), (oa + sg * da, eu + du)]
            strip(P, person, ln, [lw * 0.7, lw * 0.2], 0.0012, "age")
        # the bag under the eye: an arc below the lower lid
        bag = [(sg * (ea - f["eye_w"] * 0.6 + 1.4 * f["eye_w"] * t), eu - f["eye_lo"] - 0.030
                - 0.010 * math.sin(math.pi * t)) for t in [i / 8 for i in range(9)]]
        strip(P, person, bag, taper(9, lw * 0.1, lw * 0.8, lw * 0.1, 0.55), 0.0012, "age")
        # the marionette line: from the mouth's corner toward the jaw
        mw = f.get("mouth_w", 20.0)
        ml = [(sg * (mw + 1.0), mu - 0.004), (sg * (mw + 2.0), mu - 0.040), (sg * (mw + 1.0), mu - 0.075)]
        strip(P, person, ml, [lw * 0.8, lw * 0.6, lw * 0.15], 0.0012, "age")
    # the forehead: two long shallow lines
    for du, span in ((0.20, 26.0), (0.25, 20.0)):
        fl = [(-span + 2 * span * i / 10, mk["eu"] + du + 0.008 * math.sin(math.pi * i / 10)) for i in range(11)]
        strip(P, person, fl, taper(11, lw * 0.1, lw * 0.7, lw * 0.1, 0.5), 0.0012, "age")


def build_blush(P, person, f, mk):
    if not f.get("blush", 0): return
    ea, eu = mk["eye_a"], mk["eu"]
    for sg in (1, -1):
        cx, cu = sg * (ea + 6.0), eu - 0.11
        poly = [(cx + 9.0 * math.cos(2 * math.pi * k / 16), cu + 0.028 * math.sin(2 * math.pi * k / 16)) for k in range(16)]
        shape(P, person, poly, 0.0005, "blush", rings=2)


def face_part(person):
    f = person.S["face"]
    mk = person.marks()
    P = Part("face", smooth=False)
    pal = person.S.get("palette", {})
    build_blush(P, person, f, mk)
    for sg in (1, -1):
        build_eye(P, person, sg, f, mk, pal)
        build_brow(P, person, sg, f, mk)
    build_mouth(P, person, f, mk)
    build_nostrils(P, person, f, mk)
    build_age(P, person, f, mk)
    return P


