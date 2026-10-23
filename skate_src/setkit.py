"""setkit.py -- SETS BUILT BY CODE: the kit every set builder draws with (split out of reva's sick bay builder, 10-23).

A set built by code is numpy + PIL, no Blender. Geometry in METRES; ONE texture ATLAS for the whole set (flat colour
swatches, and boxes painted with PIL); each pack written as a .glb, baked by skate_src/glb2rpack.py at 1.75 units a
metre (TrueScale) with the MARKER nodes that tell the engine what the pack is (ToonSet: the toon pass lit as a room;
Glow: unlit; NoCast; Open: never a collider; AlphaNN: sheer -- engine_capabilities.md section 38), placed by a .set.

THE RULE THE KIT IS BUILT ROUND: EVERY POSITION CARRIES ONE NORMAL. The toon pass inks with an inverted hull (each
vertex pushed out along its normal), and the hull cracks open wherever one position carries two normals. So: rounded
boxes (flat faces joined by quarter rounds), lathes with SUPPORT LOOPS at profile corners (the corner's bisector normal
confined to a sliver, so the faces either side still shade flat under a hard cel cut), capsule-ended rods, tubes swept
by parallel transport. A flat quad is for what the ink never sees (a wall's face, a screen's glow).

    import setkit as K
    K.PAL.update({"hull": (196, 186, 164), "trim": (64, 146, 156)})   # the swatches (name -> rgb) the set paints with
    K.A = K.Atlas()                                     # a fresh atlas: every helper draws into K.A (rebind, never alias)
    K.A.box("panel", 320, 200, painter)                 # a painted box: painter(ImageDraw, W, H), drawn 4x, box-filtered
    g = K.Geo()
    K.rbox(g, centre, half_extents, radius, "hull")     # rbox lathe quad disc_tex tube rod: geometry + its atlas uvs
    P, N, T, I = g.arrays()                             # merged, unit normals, zero-area tris dropped, wound for the ink
    K.write_glb("hull.glb", "hull", P, N, T, I, K.A.png())
    # python skate_src/glb2rpack.py hull.glb props/hull.rpack --scale 1.75 --marker ToonSet

A ROOM in the Star Trek style: walls() (inward faces in bands -- kick, lower wall, rail, two-tone bays, light strip,
cove, ceiling, floor -- cut round the doors), door_frame(), screen() (a bezel and its glowing face), and the LCARS
painters for the faces (lcars_frame, rr, the LC colours). Pass the room's own PAL entries to recolour it.

    python skate_src/setkit.py --selftest       (each promise above against a control that breaks it; temp files only)
"""
import io, json, math, os, struct, sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

__all__ = ["U", "PAL", "LC", "Atlas", "A", "FONT", "font", "roty", "rotx", "Geo", "rbox", "support", "lathe", "quad",
           "disc_tex", "tube", "rod", "rr", "lcars_frame", "paint_cove", "KICK", "RAIL0", "RAIL1", "STRIP0", "STRIP1",
           "COVE", "DOOR_H", "walls", "door_frame", "screen", "write_glb"]

U = 1.75                                  # club units a metre (TrueScale)

# ---------------------------------------------------------------------------------------------- the palette
PAL = {   # the kit's own swatches (the room's); a builder adds its own, or recolours these, with PAL.update
    # the shell
    "floor": (60, 74, 100), "floor_edge": (42, 52, 72), "kick": (34, 36, 46), "wall_lo": (178, 168, 154),
    "rail": (64, 146, 156), "wall": (216, 208, 192), "wall_b": (200, 191, 175), "rib": (148, 156, 170),
    # the ceiling faces down, so the room's light puts it in the show's cool shadow tint (x 0.50, 0.47, 0.66): a
    # yellowish base lands it on a neutral warm grey there instead of the purple a grey base became
    "strip_back": (60, 62, 72), "ceiling": (232, 226, 178), "frame": (94, 102, 120), "threshold": (70, 76, 92),
    "bezel": (54, 58, 70),
    # glow (drawn unlit)
    "strip": (252, 240, 212), "doorlight": (255, 170, 60),
}
LC = {"bg": (12, 12, 20), "orange": (255, 153, 0), "lilac": (204, 153, 204), "violet": (153, 153, 204),
      "red": (204, 102, 102), "peach": (255, 204, 153), "blue": (153, 153, 255), "tan": (255, 153, 102),
      "teal": (90, 210, 220), "green": (120, 230, 140), "white": (240, 240, 250)}


# ---------------------------------------------------------------------------------------------- the atlas
class Atlas:
    """one texture for the whole set: flat swatches (32 px cells, a face's UV at its cell's centre) and BOXES the
    screens, the emitter, the fluid and the drain are painted into"""
    def __init__(self, size=2048, cell=32, rows=4):
        self.S, self.cell, self.per, self.rows = size, cell, size // cell, rows
        self.img = Image.new("RGB", (size, size), (255, 0, 255))
        self.sw, self.boxes = {}, {}
        self.shelf = [0, rows * cell, 0]

    def uv(self, name):
        if name not in self.sw:
            k = len(self.sw); c, r = k % self.per, k // self.per
            assert r < self.rows, "swatch rows full"
            rgb = PAL[name]
            self.img.paste(tuple(rgb), (c * self.cell, r * self.cell, (c + 1) * self.cell, (r + 1) * self.cell))
            self.sw[name] = ((c + 0.5) * self.cell / self.S, (r + 0.5) * self.cell / self.S)
        return self.sw[name]

    def box(self, name, w, h, painter):
        if name in self.boxes: return
        x, y, rh = self.shelf
        if x + w > self.S: x, y, rh = 0, y + rh + 8, 0
        assert y + h <= self.S, "atlas full at " + name
        self.boxes[name] = (x, y, w, h)
        self.shelf = [x + w + 8, y, max(rh, h)]
        ss = 4
        im = Image.new("RGB", (w * ss, h * ss), (0, 0, 0))
        painter(ImageDraw.Draw(im), w * ss, h * ss)
        self.img.paste(im.resize((w, h), Image.BOX), (x, y))
        # bleed the box's edge pixels outward 4 px, so the mip chain does not mix in a neighbour
        reg = np.array(self.img)
        for k in range(1, 5):
            if y - k >= 0: reg[y - k, x:x + w] = reg[y, x:x + w]
            if y + h - 1 + k < self.S: reg[y + h - 1 + k, x:x + w] = reg[y + h - 1, x:x + w]
        for k in range(1, 5):
            if x - k >= 0: reg[max(0, y - 4):y + h + 4, x - k] = reg[max(0, y - 4):y + h + 4, x]
            if x + w - 1 + k < self.S: reg[max(0, y - 4):y + h + 4, x + w - 1 + k] = reg[max(0, y - 4):y + h + 4, x + w - 1]
        self.img = Image.fromarray(reg)

    def buv(self, name, s, t):
        """(s, t) in 0..1 across a box (t = 0 at its top) -> atlas uv"""
        x, y, w, h = self.boxes[name]
        return ((x + 0.5 + s * (w - 1)) / self.S, (y + 0.5 + t * (h - 1)) / self.S)

    def png(self):
        b = io.BytesIO(); self.img.save(b, "PNG", optimize=True); return b.getvalue()


A = Atlas()


FONT = os.environ.get("CLUB_FONT", r"C:\Windows\Fonts\bahnschrift.ttf")   # Bahnschrift: its condensed cuts read as LCARS


def font(px, style="Bold Condensed"):
    """a face px pixels tall, at its variable font's `style` where it has one; where FONT is missing, Pillow's own face
    (set CLUB_FONT to any .ttf)"""
    try: f = ImageFont.truetype(FONT, int(px))
    except OSError:
        try: return ImageFont.load_default(int(px))
        except TypeError: return ImageFont.load_default()          # Pillow before 10.1: one size
    try: f.set_variation_by_name(style)
    except Exception: pass
    return f


# ---------------------------------------------------------------------------------------------- geometry
def roty(yaw_deg):
    """the engine's prop yaw: local (x, z) -> (c x + s z, -s x + c z)"""
    a = math.radians(yaw_deg); c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]], float)


def rotx(deg):
    a = math.radians(deg); c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]], float)


class Geo:
    def __init__(self):
        self.parts = []

    def add(self, P, N, UV, I):
        self.parts.append((np.asarray(P, float).reshape(-1, 3), np.asarray(N, float).reshape(-1, 3),
                           np.asarray(UV, float).reshape(-1, 2), np.asarray(I, np.int64).reshape(-1, 3)))

    def arrays(self):
        P, N, T, I, base = [], [], [], [], 0
        for p, n, t, i in self.parts:
            P.append(p); N.append(n); T.append(t); I.append(i + base); base += len(p)
        P, N, T, I = np.concatenate(P), np.concatenate(N), np.concatenate(T), np.concatenate(I)
        N = N / (np.linalg.norm(N, axis=1, keepdims=True) + 1e-12)
        a, b, c = P[I[:, 0]], P[I[:, 1]], P[I[:, 2]]
        fn = np.cross(b - a, c - a)
        keep = np.linalg.norm(fn, axis=1) > 1e-12            # the lathe's axis collapses a ring to a point
        I, fn = I[keep], fn[keep]
        nn = N[I[:, 0]] + N[I[:, 1]] + N[I[:, 2]]
        flip = (fn * nn).sum(1) < 0                          # CCW around the normal: the ink culls FRONT faces
        I[flip] = I[flip][:, [0, 2, 1]]
        return P, N, T, I


def _uvs(uv, n):
    return np.tile(np.asarray(uv, float), (n, 1))


def rbox(g, c, h, r, sw, seg=3, R=None):
    """a rounded box: centre c, half extents h, edge radius r; the faces' flat parts are one quad each and the edges
    are quarter rounds, so every position carries one normal"""
    h = np.asarray(h, float); r = min(r, float(h.min()) * 0.98); ii = h - r
    def samples(k):
        out = [(-ii[k], -math.tan(math.radians(45.0 * j / seg))) for j in range(seg, 0, -1)]
        out += [(-ii[k], 0.0), (ii[k], 0.0)]
        out += [(ii[k], math.tan(math.radians(45.0 * j / seg))) for j in range(1, seg + 1)]
        return out
    S = [samples(k) for k in range(3)]
    uv = A.uv(sw) if isinstance(sw, str) else sw
    for a in range(3):
        b, cc = (a + 1) % 3, (a + 2) % 3
        for sg in (-1.0, 1.0):
            nb, nc = len(S[b]), len(S[cc])
            P = np.zeros((nb, nc, 3)); Nn = np.zeros((nb, nc, 3))
            for i, (qb, tb) in enumerate(S[b]):
                for j, (qc, tc) in enumerate(S[cc]):
                    d = np.zeros(3); d[a] = sg; d[b] = tb; d[cc] = tc; d /= np.linalg.norm(d)
                    q = np.zeros(3); q[a] = sg * ii[a]; q[b] = qb; q[cc] = qc
                    P[i, j] = q + r * d; Nn[i, j] = d
            idx = np.arange(nb * nc).reshape(nb, nc); tris = []
            for i in range(nb - 1):
                for j in range(nc - 1):
                    tris += [(idx[i, j], idx[i + 1, j], idx[i + 1, j + 1]), (idx[i, j], idx[i + 1, j + 1], idx[i, j + 1])]
            P = P.reshape(-1, 3); Nn = Nn.reshape(-1, 3)
            if R is not None: P = P @ R.T; Nn = Nn @ R.T
            g.add(P + np.asarray(c, float), Nn, _uvs(uv, len(P)), tris)


def support(prof, e=0.004):
    """support loops: a point flagged as a CORNER gets two neighbours e away along its edges, so its bisector normal is
    confined to a sliver and the faces either side shade flat (a cel cut would otherwise run across them)"""
    out = []
    n = len(prof)
    for k, p in enumerate(prof):
        r, y, sw = p[0], p[1], p[2]
        corner = len(p) < 4 or p[3] != "s"
        if corner and 0 < k < n - 1:
            pa, pb = np.array(prof[k - 1][:2], float), np.array(prof[k + 1][:2], float); q = np.array([r, y], float)
            da, db = np.linalg.norm(pa - q), np.linalg.norm(pb - q)
            ea, eb = min(e, 0.3 * da), min(e, 0.3 * db)
            a1 = q + (pa - q) / da * ea; b1 = q + (pb - q) / db * eb
            out.append((a1[0], a1[1], prof[k - 1][2], "s"))
            out.append((r, y, sw, "c"))
            out.append((b1[0], b1[1], sw, "s"))
        else:
            out.append((r, y, sw, "s" if not corner else "c"))
    return out


def lathe(g, prof, c=(0, 0, 0), segs=64, a0=0.0, a1=2 * math.pi, R=None, uvfn=None, e=0.004):
    """a surface of revolution about y. prof: [(r, y, swatch[, 's'])...] running UP the outside (the normal is the
    profile's tangent turned -90 deg); a swatch names the edge FROM its point to the next; 's' marks a point on a curve
    (no support loop). uvfn(P) -> uv overrides the swatches (a painted disc)."""
    prof = support(prof, e)
    pts = np.array([(p[0], p[1]) for p in prof], float); n = len(pts)
    Nrm = np.zeros((n, 2))
    for k in range(n):
        t = pts[min(k + 1, n - 1)] - pts[max(k - 1, 0)]; t /= (np.linalg.norm(t) + 1e-12)
        Nrm[k] = (t[1], -t[0])
    angs = np.linspace(a0, a1, segs + 1); m = len(angs)
    for k in range(n - 1):
        if np.linalg.norm(pts[k + 1] - pts[k]) < 1e-9: continue
        Ps, Ns = [], []
        for kk in (k, k + 1):
            r, y = pts[kk]; nr, ny = Nrm[kk]
            Ps.append(np.stack([r * np.sin(angs), np.full(m, y), r * np.cos(angs)], 1))
            Ns.append(np.stack([nr * np.sin(angs), np.full(m, ny), nr * np.cos(angs)], 1))
        P = np.concatenate(Ps); N = np.concatenate(Ns)
        tris = []
        for i in range(m - 1):
            tris += [(i, i + 1, m + i + 1), (i, m + i + 1, m + i)]
        if R is not None: P = P @ R.T; N = N @ R.T
        P = P + np.asarray(c, float)
        UV = np.array([uvfn(p) for p in P]) if uvfn else _uvs(A.uv(prof[k][2]), len(P))
        g.add(P, N, UV, tris)


def quad(g, o, ex, ey, sw=None, box=None, n=None, flip_t=False):
    """a flat rectangle from corner o along ex (s) and ey (t); normal n (default ex x ey); a swatch or a painted box"""
    o, ex, ey = (np.asarray(v, float) for v in (o, ex, ey))
    nn = np.cross(ex, ey) if n is None else np.asarray(n, float); nn = nn / np.linalg.norm(nn)
    P = np.array([o, o + ex, o + ex + ey, o + ey])
    if box:
        st = [(0, 1), (1, 1), (1, 0), (0, 0)] if not flip_t else [(0, 0), (1, 0), (1, 1), (0, 1)]
        UV = np.array([A.buv(box, s, t) for s, t in st])
    else:
        UV = _uvs(A.uv(sw), 4)
    g.add(P, np.tile(nn, (4, 1)), UV, [(0, 1, 2), (0, 2, 3)])


def disc_tex(g, c, r, box, up=True, segs=72, rings=6):
    """a flat disc painted from a box: the box's centre at the disc's centre"""
    c = np.asarray(c, float); P, UV = [c.copy()], [A.buv(box, 0.5, 0.5)]
    for k in range(1, rings + 1):
        rr = r * k / rings
        for i in range(segs):
            a = 2 * math.pi * i / segs; s, t = math.sin(a), math.cos(a)
            P.append(c + np.array([rr * s, 0.0, rr * t])); UV.append(A.buv(box, 0.5 + 0.5 * s * k / rings, 0.5 + 0.5 * t * k / rings))
    tris = []
    for i in range(segs): tris.append((0, 1 + i, 1 + (i + 1) % segs))
    for k in range(1, rings):
        b0, b1 = 1 + (k - 1) * segs, 1 + k * segs
        for i in range(segs):
            j = (i + 1) % segs
            tris += [(b0 + i, b1 + i, b1 + j), (b0 + i, b1 + j, b0 + j)]
    nn = np.array([0, 1.0, 0]) if up else np.array([0, -1.0, 0])
    g.add(np.array(P), np.tile(nn, (len(P), 1)), np.array(UV), tris)


def tube(g, pts, radii, sw, segs=10):
    """a tube swept along a polyline (rings carried by parallel transport), closed at both ends"""
    pts = np.asarray(pts, float); n = len(pts)
    T = np.gradient(pts, axis=0); T /= np.linalg.norm(T, axis=1, keepdims=True)
    a = np.array([1.0, 0, 0]) if abs(T[0][0]) < 0.9 else np.array([0, 0, 1.0])
    Ns = [np.cross(T[0], a) / np.linalg.norm(np.cross(T[0], a))]
    for k in range(1, n):
        v = Ns[-1] - T[k] * (Ns[-1] @ T[k]); Ns.append(v / np.linalg.norm(v))
    P, Nn = [], []
    for k in range(n):
        B = np.cross(T[k], Ns[k])
        for i in range(segs):
            th = 2 * math.pi * i / segs; d = Ns[k] * math.cos(th) + B * math.sin(th)
            P.append(pts[k] + radii[k] * d); Nn.append(d)
    tris = []
    for k in range(n - 1):
        for i in range(segs):
            j = (i + 1) % segs
            tris += [(k * segs + i, (k + 1) * segs + i, (k + 1) * segs + j), (k * segs + i, (k + 1) * segs + j, k * segs + j)]
    c0 = len(P); P.append(pts[0]); Nn.append(-T[0])
    c1 = len(P); P.append(pts[-1]); Nn.append(T[-1])
    for i in range(segs):
        j = (i + 1) % segs
        tris += [(c0, j, i), (c1, (n - 1) * segs + i, (n - 1) * segs + j)]
    g.add(np.array(P), np.array(Nn), _uvs(A.uv(sw), len(P)), tris)


def rod(g, a, b, r, sw, segs=20):
    """a capsule-ended rod from a to b (vertical or not)"""
    a, b = np.asarray(a, float), np.asarray(b, float); L = np.linalg.norm(b - a); d = (b - a) / L
    prof = [(0.0, -r, sw, "s")]
    for k in range(1, 6):
        t = math.pi / 2 * (1 - k / 6.0); prof.append((r * math.cos(t), -r * math.sin(t), sw, "s"))
    prof += [(r, 0.0, sw, "s"), (r, L, sw, "s")]
    for k in range(1, 6):
        t = math.pi / 2 * k / 6.0; prof.append((r * math.cos(t), L + r * math.sin(t), sw, "s"))
    prof.append((0.0, L + r, sw, "s"))
    up = np.array([0, 1.0, 0])
    v = np.cross(up, d); s = np.linalg.norm(v); cth = float(up @ d)
    if s < 1e-9: R = np.eye(3) if cth > 0 else np.diag([1, -1, -1.0])
    else:
        vx = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
        R = np.eye(3) + vx + vx @ vx * ((1 - cth) / (s * s))
    lathe(g, prof, c=a, segs=segs, R=R)


# ---------------------------------------------------------------------------------------------- the LCARS screens
def rr(d, box, rad, fill):
    d.rounded_rectangle(box, radius=rad, fill=fill)


def lcars_frame(d, W, H, col_top, col_side, col_bot, bar=0.10):
    """the LCARS frame: a top elbow and a bottom elbow (a big rounded OUTER corner, a small concave INNER one) joined
    by a side bar of another colour, black gaps between"""
    BG = LC["bg"]
    sw = int(W * bar); th = int(H * 0.11); ro = int(min(sw * 1.3, H * 0.3)); ri = int(th * 0.9); gap = max(4, int(H * 0.014))
    y_top, y_bot = int(H * 0.38), int(H * 0.62)
    # the top elbow
    d.rounded_rectangle((0, 0, 2 * ro, 2 * ro), radius=ro, fill=col_top)
    d.rectangle((ro, 0, int(W * 0.55), th), fill=col_top)
    d.rectangle((0, ro, sw, y_top), fill=col_top)
    d.rectangle((sw, th, 2 * ro + 2, 2 * ro + 2), fill=BG)
    d.rectangle((sw, th, sw + ri, th + ri), fill=col_top)
    d.pieslice((sw, th, sw + 2 * ri, th + 2 * ri), 180, 270, fill=BG)
    # the bottom elbow
    d.rounded_rectangle((0, H - 2 * ro, 2 * ro, H), radius=ro, fill=col_bot)
    d.rectangle((ro, H - th, int(W * 0.40), H), fill=col_bot)
    d.rectangle((0, y_bot, sw, H - ro), fill=col_bot)
    d.rectangle((sw, H - 2 * ro - 2, 2 * ro + 2, H - th), fill=BG)
    d.rectangle((sw, H - th - ri, sw + ri, H - th), fill=col_bot)
    d.pieslice((sw, H - th - 2 * ri, sw + 2 * ri, H - th), 90, 180, fill=BG)
    # the side bar between them
    d.rectangle((0, y_top + gap, sw, y_bot - gap), fill=col_side)
    return sw, th


def paint_cove(d, W, H):
    """the cove over the light strip, lit from below by it: bright at the strip, falling to the ceiling's tone"""
    for y in range(H):
        t = y / (H - 1.0)                      # 0 at the ceiling, 1 at the strip
        k = t ** 1.8
        c = tuple(int(a + (b - a) * k) for a, b in zip((124, 114, 122), (250, 236, 206)))
        d.line((0, y, W, y), fill=c)


# ---------------------------------------------------------------------------------------------- a room
# metres; x right, y up, z toward the front (the engine's axes): the heights of the bands every wall is cut into
KICK, RAIL0, RAIL1, STRIP0, STRIP1, COVE = 0.13, 0.90, 0.98, 2.20, 2.27, 0.35
DOOR_H = 2.05


def walls(g, glow, x0, x1, z0, z1, doors, ribs_every=2.5, keep=None, ceil=2.9):
    """four inward-facing walls in bands -- kick, lower wall, the rail's backing, the wall in alternating bays, the
    strip's backing -- cut round the doors; ribs at the corners and every ~2.5 m; the light strip (glow); the cove and
    the ceiling (at ceil); the floor with its border. doors: [(side, s0, s1)], side back|right|front|left, s along
    that wall from its first corner (back: from x0, right: from z0, front: from x1, left: from z1). keep: {side:
    [(s0, s1)]}, spans no rib may stand in (a screen's place)"""
    A.box("cove", 16, 128, paint_cove)                 # the cove's uplight, painted once per atlas
    W = [  # origin, along, inward, length, which doors
        ((x0, z0), (1, 0), (0, 1), x1 - x0, "back"),
        ((x1, z0), (0, 1), (-1, 0), z1 - z0, "right"),
        ((x1, z1), (-1, 0), (0, -1), x1 - x0, "front"),
        ((x0, z1), (0, -1), (1, 0), z1 - z0, "left"),
    ]
    for (ox, oz), (ax, az), (nx, nz), L, side in W:
        ops = [(d0, d1) for (sd, d0, d1) in doors if sd == side]
        def P3(s, y, off=0.0):
            return np.array([ox + ax * s + nx * off, y, oz + az * s + nz * off])
        ex = np.array([ax, 0, az], float); nrm = np.array([nx, 0, nz], float)
        def spans(cut):
            if not cut or not ops: return [(0.0, L)]
            out, s = [], 0.0
            for d0, d1 in sorted(ops):
                if d0 > s: out.append((s, d0))
                s = max(s, d1)
            if s < L: out.append((s, L))
            return out
        # ribs: the corners, then every ~2.5 m, never in a door
        nr = max(1, int(round(L / ribs_every)))
        rs = [0.12] + [L * k / nr for k in range(1, nr)] + [L - 0.12]
        rs = [s for s in rs if all(not (d0 - 0.3 < s < d1 + 0.3) for d0, d1 in ops)]
        rs = [s for s in rs if all(not (k0 - 0.18 < s < k1 + 0.18) for k0, k1 in (keep or {}).get(side, []))]
        bays = sorted(set([0.0, L] + [s for s in rs if 0.5 < s < L - 0.5]))
        bands = [(0.0, KICK, "kick"), (KICK, RAIL1, "wall_lo"), (RAIL1, DOOR_H, None), (DOOR_H, STRIP0, None),
                 (STRIP0, STRIP1, "strip_back")]
        for y0, y1, sw in bands:
            cut = y1 <= DOOR_H + 1e-6
            for s0, s1 in spans(cut):
                if sw:
                    quad(g, P3(s0, y0), ex * (s1 - s0), np.array([0, y1 - y0, 0]), sw, n=nrm)
                else:   # the wall: bays in two tones
                    for k in range(len(bays) - 1):
                        b0, b1 = max(s0, bays[k]), min(s1, bays[k + 1])
                        if b1 - b0 > 1e-4:
                            quad(g, P3(b0, y0), ex * (b1 - b0), np.array([0, y1 - y0, 0]), "wall" if k % 2 == 0 else "wall_b", n=nrm)
        # the rail (stands out: ink), cut at the doors and the ribs
        for s0, s1 in spans(True):
            c = P3((s0 + s1) / 2, (RAIL0 + RAIL1) / 2, 0.025)
            hl = (s1 - s0) / 2 - 0.01
            if hl > 0.05:
                h3 = np.abs(ex) * hl + np.array([0, (RAIL1 - RAIL0) / 2, 0]) + np.abs(nrm) * 0.03
                rbox(g, c, h3, 0.02, "rail")
        for s in rs:
            c = P3(s, STRIP0 / 2, 0.04)
            rbox(g, c, np.abs(ex) * 0.12 + np.array([0, STRIP0 / 2 - 0.002, 0]) + np.abs(nrm) * 0.05, 0.025, "rib")
        # the light strip, in front of its backing
        for s0, s1 in spans(False):
            quad(glow, P3(s0 + 0.02, STRIP0 + 0.012, 0.004), ex * (s1 - s0 - 0.04), np.array([0, STRIP1 - STRIP0 - 0.024, 0]), "strip", n=nrm)
    # the cove: four trapezoids from the top of the strip to the ceiling, mitred at the corners -- UPLIT by the strip
    # (glow, a painted gradient), as a Trek cove is; under the room's top light it was a band of shadow tone
    o = [(x0, z0), (x1, z0), (x1, z1), (x0, z1)]
    i = [(x0 + COVE, z0 + COVE), (x1 - COVE, z0 + COVE), (x1 - COVE, z1 - COVE), (x0 + COVE, z1 - COVE)]
    for k in range(4):
        a, b = o[k], o[(k + 1) % 4]; ia, ib = i[k], i[(k + 1) % 4]
        P = np.array([[a[0], STRIP1, a[1]], [b[0], STRIP1, b[1]], [ib[0], ceil, ib[1]], [ia[0], ceil, ia[1]]])
        n = np.cross(P[1] - P[0], P[3] - P[0]); n /= np.linalg.norm(n)
        if n[1] > 0: n = -n
        UV = np.array([A.buv("cove", 0.5, 1.0), A.buv("cove", 0.5, 1.0), A.buv("cove", 0.5, 0.0), A.buv("cove", 0.5, 0.0)])
        glow.add(P, np.tile(n, (4, 1)), UV, [(0, 1, 2), (0, 2, 3)])
    quad(g, (i[0][0], ceil, i[0][1]), (i[1][0] - i[0][0], 0, 0), (0, 0, i[3][1] - i[0][1]), "ceiling", n=(0, -1, 0))
    # the floor and its border
    e = 0.30
    quad(g, (x0 + e, 0, z0 + e), (x1 - x0 - 2 * e, 0, 0), (0, 0, z1 - z0 - 2 * e), "floor", n=(0, 1, 0))
    for (ax0, az0, ax1, az1) in [(x0, z0, x1, z0 + e), (x0, z1 - e, x1, z1), (x0, z0 + e, x0 + e, z1 - e), (x1 - e, z0 + e, x1, z1 - e)]:
        quad(g, (ax0, 0, az0), (ax1 - ax0, 0, 0), (0, 0, az1 - az0), "floor_edge", n=(0, 1, 0))


def door_frame(g, glow, xa, xb, z, inward, depth=0.22):
    """jambs and a header standing out of the wall (both sides of a doorway), and the amber light over it"""
    for x in (xa - 0.06, xb + 0.06):
        rbox(g, (x, (DOOR_H + 0.12) / 2, z), (0.06, (DOOR_H + 0.12) / 2, depth / 2), 0.02, "frame")
    rbox(g, ((xa + xb) / 2, DOOR_H + 0.06, z), ((xb - xa) / 2 + 0.12, 0.06, depth / 2), 0.02, "frame")
    rbox(g, ((xa + xb) / 2, 0.006, z), ((xb - xa) / 2, 0.006, depth / 2), 0.004, "threshold")
    quad(glow, (xa + 0.25, DOOR_H + 0.03, z + inward * (depth / 2 + 0.003)), (xb - xa - 0.5, 0, 0), (0, 0.05, 0),
         "doorlight", n=(0, 0, inward))


def screen(g, glow, c, right, w, h, box, depth=0.03, border=0.05):
    """a bezel standing out of a wall and its LCARS face (glow): c on the wall, right = the wall's along vector"""
    c = np.asarray(c, float); right = np.asarray(right, float); up = np.array([0, 1.0, 0])
    n = np.cross(right, up)
    h3 = np.abs(right) * (w / 2 + border) + up * (h / 2 + border) + np.abs(n) * depth
    rbox(g, c + n * depth, h3, 0.02, "bezel")
    o = c + n * (2 * depth + 0.003) - right * w / 2 - up * h / 2
    quad(glow, o, right * w, up * h, box=box, n=n)


# ---------------------------------------------------------------------------------------------- output
def write_glb(path, name, P, N, T, I, png, generator="setkit.py", material="atlas"):
    """one mesh, one material, the atlas embedded: POSITION NORMAL TEXCOORD_0 + u32 indices"""
    P, N, T = P.astype("<f4"), N.astype("<f4"), T.astype("<f4"); Iu = I.astype("<u4").reshape(-1)
    blobs = [P.tobytes(), N.tobytes(), T.tobytes(), Iu.tobytes(), png]
    views, binb = [], b""
    for k, b in enumerate(blobs):
        binb += b"\0" * ((-len(binb)) % 4)
        v = {"buffer": 0, "byteOffset": len(binb), "byteLength": len(b)}
        if k < 3: v["target"] = 34962
        elif k == 3: v["target"] = 34963
        views.append(v); binb += b
    binb += b"\0" * ((-len(binb)) % 4)
    nv = len(P)
    js = {"asset": {"version": "2.0", "generator": generator}, "scene": 0, "scenes": [{"nodes": [0]}],
          "nodes": [{"mesh": 0, "name": name}],
          "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]}],
          "materials": [{"name": material, "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0, "roughnessFactor": 1.0}}],
          "textures": [{"source": 0, "sampler": 0}], "samplers": [{"magFilter": 9729, "minFilter": 9987}],
          "images": [{"bufferView": 4, "mimeType": "image/png"}],
          "accessors": [{"bufferView": 0, "componentType": 5126, "count": nv, "type": "VEC3", "min": P.min(0).tolist(), "max": P.max(0).tolist()},
                        {"bufferView": 1, "componentType": 5126, "count": nv, "type": "VEC3"},
                        {"bufferView": 2, "componentType": 5126, "count": nv, "type": "VEC2"},
                        {"bufferView": 3, "componentType": 5125, "count": len(Iu), "type": "SCALAR"}],
          "bufferViews": views, "buffers": [{"byteLength": len(binb)}]}
    jb = json.dumps(js).encode("utf-8"); jb += b" " * ((-len(jb)) % 4)
    out = struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(jb) + 8 + len(binb)) + struct.pack("<I4s", len(jb), b"JSON") + jb \
          + struct.pack("<I4s", len(binb), b"BIN\0") + binb
    open(path, "wb").write(out)


# ---------------------------------------------------------------------------------------------- the self-test
def _spread(P, N):
    """the widest angle (degrees) between the normals of vertices at one position"""
    key = np.round(P, 6)
    order = np.lexsort(key.T[::-1]); k, n = key[order], N[order]
    worst, s = 0.0, 0
    for e in range(1, len(k) + 1):
        if e == len(k) or (k[e] != k[s]).any():
            if e - s > 1: worst = max(worst, float(np.degrees(np.arccos(np.clip(n[s:e] @ n[s], -1, 1).min()))))
            s = e
    return worst


def _hit(o, d, P, I):
    """does the ray o + t d, t > 0, cross a triangle (Moller-Trumbore)"""
    o, d = np.asarray(o, float), np.asarray(d, float)
    a, b, c = P[I[:, 0]], P[I[:, 1]], P[I[:, 2]]
    e1, e2 = b - a, c - a
    p = np.cross(d, e2); det = (e1 * p).sum(1)
    ok = np.abs(det) > 1e-12; inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
    s = o - a; u = (s * p).sum(1) * inv
    q = np.cross(s, e1); v = (q * d).sum(1) * inv; t = (q * e2).sum(1) * inv
    return bool((ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 1e-9)).any())


def selftest():
    """the kit's promises, each beside a control that breaks it; nothing is written outside a temp folder"""
    import shutil, subprocess, tempfile
    global A
    A = Atlas()
    PAL.update({"_t_red": (220, 40, 40), "_t_blue": (40, 60, 220)})
    bad = []

    def check(what, ok, got=""):
        print("  %-66s %s  %s" % (what, "ok  " if ok else "FAIL", got))
        if not ok: bad.append(what)

    # 1. one normal per position: a rounded box, turned. The control, a cube of six flat quads, cracks at every edge.
    g = Geo(); rbox(g, (0.3, 0.5, -0.2), (0.4, 0.25, 0.15), 0.05, "_t_red", R=roty(30)); P, N, T, I = g.arrays()
    s = _spread(P, N); check("rbox: one normal per position", s < 0.01, "%.4f deg" % s)
    g = Geo()
    for ax in range(3):
        for sg in (-1.0, 1.0):
            n = np.zeros(3); n[ax] = sg; u = np.zeros(3); u[(ax + 1) % 3] = 1.0; w = np.cross(n, u)
            quad(g, n * 0.5 - u * 0.5 - w * 0.5, u, w, "_t_red", n=n)
    P, N, T, I = g.arrays(); s = _spread(P, N)
    check("control: six flat quads (a hard cube) crack", abs(s - 90.0) < 0.01, "%.1f deg" % s)

    # 2. a lathe's corner: the support loop keeps the wall under it flat (shading 1 cm below the corner); without the
    #    loop (every point flagged 's') the corner's bisector bends the whole wall
    prof = [(0.5, 0.0, "_t_red"), (0.5, 0.3, "_t_red"), (0.2, 0.3, "_t_blue"), (0.2, 0.25, "_t_blue")]
    for loops in (True, False):
        g = Geo(); lathe(g, prof if loops else [p + ("s",) for p in prof], segs=48); P, N, T, I = g.arrays()
        on = (np.abs(P[:, 0]) < 1e-12) & (np.abs(P[:, 2] - 0.5) < 1e-9)       # the outer wall along its seam
        ys, ns = P[on, 1], N[on]
        lo, hi = ys[ys <= 0.29].max(), ys[ys > 0.29].min(); f = (0.29 - lo) / (hi - lo)
        n = (1 - f) * ns[ys == lo][0] + f * ns[ys == hi][0]; n /= np.linalg.norm(n)
        tilt = float(np.degrees(np.arcsin(min(1.0, abs(n[1])))))
        if loops:
            s = _spread(P, N); check("lathe: one normal per position", s < 0.01, "%.4f deg" % s)
            check("lathe: the wall 1 cm under a support-looped corner shades flat", tilt < 0.01, "%.4f deg" % tilt)
        else:
            check("control: without the loop that wall bends toward the corner", tilt > 30, "%.1f deg" % tilt)

    # 3. a rod is a capsule round its own segment in any direction (straight down is its own case in the turn)
    for a, b in (((0.1, 0.2, 0.3), (0.9, 1.4, -0.2)), ((0.0, 2.0, 0.0), (0.0, 0.5, 0.0))):
        g = Geo(); rod(g, a, b, 0.05, "_t_blue"); P, N, T, I = g.arrays()
        a_, d = np.array(a, float), np.array(b, float) - np.array(a, float)
        t = np.clip(((P - a_) @ d) / (d @ d), 0, 1); r = np.linalg.norm(P - (a_ + t[:, None] * d), axis=1)
        check("rod %s -> %s: every vertex 0.05 off the segment" % (a, b), np.abs(r - 0.05).max() < 1e-9,
              "%.2g" % np.abs(r - 0.05).max())

    # 4. arrays(): a triangle wound against its normals is turned round, a zero-area one dropped
    g = Geo(); g.add([(0, 0, 0), (0, 1, 0), (1, 0, 0), (2, 0, 0)], [(0, 0, 1)] * 4, [(0, 0)] * 4, [(0, 1, 2), (0, 2, 3)])
    I = g.arrays()[3]; check("arrays: CW turned CCW round its normal, zero-area dropped", I.tolist() == [[0, 2, 1]], str(I.tolist()))

    # 5. the atlas: a swatch's uv is its colour; a box's uv is what was painted there; the box's edge bleeds 4 px
    def halves(d, W, H):
        d.rectangle((0, 0, W // 2 - 1, H), fill=PAL["_t_red"]); d.rectangle((W // 2, 0, W, H), fill=PAL["_t_blue"])
    A.box("_t_halves", 64, 32, halves)
    px = lambda uv: A.img.getpixel((int(uv[0] * A.S), int(uv[1] * A.S)))
    check("atlas: a swatch's uv lands on its colour", px(A.uv("_t_blue")) == PAL["_t_blue"], str(px(A.uv("_t_blue"))))
    check("atlas: a box's uvs land on what was painted there",
          px(A.buv("_t_halves", 0.25, 0.5)) == PAL["_t_red"] and px(A.buv("_t_halves", 0.75, 0.5)) == PAL["_t_blue"])
    x, y, w, h = A.boxes["_t_halves"]
    edge = all(A.img.getpixel((x + w - 1 + k, y + h // 2)) == A.img.getpixel((x + w - 1, y + h // 2)) and
               A.img.getpixel((x + w // 4, y - k)) == A.img.getpixel((x + w // 4, y)) for k in range(1, 5))
    check("atlas: a box's edge bleeds 4 px out (no neighbour in its mips)", edge)

    # 6. a room: the doorway is OPEN (a ray through it from inside hits nothing), the wall beside it and the lintel
    #    over it are whole, the floor and the ceiling are there, the ceiling at ceil facing down
    shell, glow = Geo(), Geo()
    walls(shell, glow, -2.0, 2.0, -1.5, 1.5, [("back", 1.0, 2.0)], ceil=2.6)
    door_frame(shell, glow, -1.0, 0.0, -1.5, +1)
    A.box("_t_lcars", 160, 100, lambda d, W, H: lcars_frame(d, W, H, LC["orange"], LC["lilac"], LC["violet"]))
    screen(shell, glow, (1.2, 1.5, -1.5), (1, 0, 0), 0.6, 0.4, "_t_lcars")
    P, N, T, I = shell.arrays()
    check("walls: a ray through the doorway leaves the room", not _hit((-0.5, 1.0, 0.0), (0, 0, -1), P, I))
    check("walls: a ray beside the door hits the wall", _hit((-1.6, 1.0, 0.0), (0, 0, -1), P, I))
    check("walls: a ray over the door hits the lintel", _hit((-0.5, 2.15, 0.0), (0, 0, -1), P, I))
    check("walls: the floor below, the ceiling above", _hit((0.5, 1.0, 0.5), (0, -1, 0), P, I) and _hit((0.5, 1.0, 0.5), (0, 1, 0), P, I))
    top = P[:, 1] > 2.6 - 1e-9
    check("walls: the ceiling at ceil, facing down", abs(P[:, 1].max() - 2.6) < 1e-9 and (N[top, 1] < -0.999).all(),
          "top %.3f" % P[:, 1].max())
    fh = font(40).getbbox("LCARS")
    check("font: a face the size asked for", 15 < fh[3] - fh[1] < 60, "%d px tall (FONT %s)" % (fh[3] - fh[1], FONT))

    # 7. the glb reads back: header, chunks, counts, the positions bit for bit, the atlas; 8. the engine's baker takes it
    tmp = tempfile.mkdtemp(prefix="setkit_")
    try:
        gp = os.path.join(tmp, "room.glb"); write_glb(gp, "room", P, N, T, I, A.png())
        raw = open(gp, "rb").read()
        magic, ver, length = struct.unpack_from("<4sII", raw, 0)
        jl, jt = struct.unpack_from("<I4s", raw, 12); js = json.loads(raw[20:20 + jl])
        bl, bt = struct.unpack_from("<I4s", raw, 20 + jl); bn = raw[28 + jl:28 + jl + bl]
        view = lambda k: bn[js["bufferViews"][k]["byteOffset"]:js["bufferViews"][k]["byteOffset"] + js["bufferViews"][k]["byteLength"]]
        check("glb: header, chunks and accessor counts", magic == b"glTF" and ver == 2 and length == len(raw) and jt == b"JSON"
              and bt == b"BIN\0" and js["accessors"][0]["count"] == len(P) and js["accessors"][3]["count"] == I.size)
        check("glb: the positions read back bit for bit", np.array_equal(np.frombuffer(view(0), "<f4").reshape(-1, 3), P.astype("<f4")))
        check("glb: the atlas rides in it", Image.open(io.BytesIO(view(4))).size == (A.S, A.S))
        bake = os.path.join(os.path.dirname(os.path.abspath(__file__)), "glb2rpack.py")
        if os.path.exists(bake):
            rp = os.path.join(tmp, "room.rpack")
            r = subprocess.run([sys.executable, bake, gp, rp, "--scale", str(U), "--marker", "ToonSet", "--marker", "Open"],
                               capture_output=True, text=True)
            ok = r.returncode == 0 and os.path.exists(rp) and open(rp, "rb").read(4) == b"RPK1"
            check("glb2rpack bakes it to a pack (RPK1)", ok, "" if ok else (r.stderr or r.stdout).strip()[-160:])
        else:
            print("  (no glb2rpack.py beside this file: the bake is not checked)")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("SELFTEST", "PASS" if not bad else "FAIL: " + "; ".join(bad))
    return not bad


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="the set kit: import it (see the docstring); --selftest checks its promises")
    ap.add_argument("--selftest", action="store_true")
    if ap.parse_args().selftest: sys.exit(0 if selftest() else 1)
    ap.print_help()
