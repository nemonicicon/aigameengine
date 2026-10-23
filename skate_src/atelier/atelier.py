"""atelier.py -- THE ATELIER: the club's tailor. A garment is a DESIGN -- panels cut on a dress form, a fabric, the
ease it sits off the skin with, trims, prints, the free edges that swing -- and the tailor sews it onto a body as
its OWN PIECE: a skinned mesh on the body's skeleton that knows exactly which skin it covers, with cloth chains
where it hangs free.

jacob, 09-29: "id like the clothing to be modular, reva is also a seamstress, she loves making new clothes ... some
way for reva to actually be making and designing clothes stich by stich that work natively in these game and
animation engines. i want cloth physics, and boob jiggle physic" -- and of her CLOTHING FABRICATION MODE sheet (a
holographic dress form, a pattern on the cutting table, fabric layers, embellishments, a live fit preview, a mood
wardrobe, "DEPLOY OUTFIT?"): "thats what this pic is about, reva making different outfits".

Used by skate_src/reva902/build_reva.py (Blender 5.1, headless); nothing here is REVA's -- any character's builder
can dress its body with it:
  Part       geometry with bone weights, and per face a material and a paint spec
  Surface    one of a body's parametric surfaces -- the DRESS FORM: pt(u, v), its normal, its weights
  region()   a PANEL cut on a surface: g(u, v) < 0 inside, its boundary snapped onto the outline (Newton, in metres
             along the surface), lifted by the fabric's EASE, a LIP where it ends (no gap under an edge)
  Form       the body's skin as one BVH: strap() lays a ribbon on it, on_skin() finds the skin under a point
  Piece      a wearable: its slot, the outfits it belongs to, its parts, its cloth chains
  hide_sets  which garments cover each skin face -- measured by rays from the skin -- so a worn garment hides the
             skin under it (nothing pokes through) and the skin is whole again when it comes off
  chains     bones down a garment's free panel (Cloth_*): the club's chain solver and Godot's SpringBoneSimulator3D
             both swing them natively
  Atlas      one painted texture: flat swatches, and BOXES a garment's own (u, v) maps into, for its prints --
             vector text from any font (Blender's own curves, so the kanji come out crisp at any size)
"""
import math

import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree


# ------------------------------------------------------------------ small maths
def smoothstep(e0, e1, x):
    t = (x - e0) / (e1 - e0)
    t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
    return t * t * (3.0 - 2.0 * t)


def lerp_table(table, z):
    """linear interpolation of every column of a (z, ...) table at z (clamped at the ends; ascending z)"""
    if z <= table[0][0]: return list(table[0][1:])
    if z >= table[-1][0]: return list(table[-1][1:])
    for i in range(len(table) - 1):
        if table[i][0] <= z <= table[i + 1][0]:
            t = (z - table[i][0]) / (table[i + 1][0] - table[i][0])
            return [a + (b - a) * t for a, b in zip(table[i][1:], table[i + 1][1:])]


def piece(table, t):
    return lerp_table(table, t)[0]


def ring_xy(theta, hw, f, b, cy, p, cx=0.0):
    """a superellipse section: theta 0 = the front (-Y), 90 deg = the body's left (+X)"""
    s, c = math.sin(theta), math.cos(theta)
    return cx + hw * math.copysign(abs(s) ** p, s), cy - (f if c >= 0 else b) * math.copysign(abs(c) ** p, c)


def tdeg(theta):
    """|theta| in degrees, 0 at the front, 180 at the back"""
    return abs(math.degrees(math.atan2(math.sin(theta), math.cos(theta))))


def norm_w(w):
    w = {k: v for k, v in w.items() if v > 0.004}
    top = sorted(w.items(), key=lambda kv: -kv[1])[:4]
    s = sum(v for _, v in top) or 1.0
    return {k: v / s for k, v in top}


def mix_w(a, b, t):
    out = {k: v * (1.0 - t) for k, v in a.items()}
    for k, v in b.items(): out[k] = out.get(k, 0.0) + v * t
    return norm_w(out)


def resample(pts, step):
    out = [pts[0]]
    for a, b in zip(pts[:-1], pts[1:]):
        n = max(1, int(math.ceil((b - a).length / step)))
        for k in range(1, n + 1): out.append(a.lerp(b, k / n))
    return out


def curve(ctrl, n):
    """a Catmull-Rom through ctrl, n points"""
    c = [ctrl[0]] + list(ctrl) + [ctrl[-1]]
    out, segs = [], len(ctrl) - 1
    for i in range(n):
        s = i / (n - 1) * segs
        k = min(int(s), segs - 1); t = s - k
        p0, p1, p2, p3 = c[k], c[k + 1], c[k + 2], c[k + 3]
        out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t
                          + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t))
    return out


# ------------------------------------------------------------------ geometry
class Part:
    """vertices with bone weights; faces with a material and a paint spec each"""
    def __init__(self, name, smooth=False):
        self.name, self.smooth, self.weld = name, smooth, False
        self.V, self.W, self.F, self.M, self.UV, self.S = [], [], [], [], [], []
        self.nobvh = set()       # faces the form's BVH skips (lips: walls, not surface)
        self.XA = {}             # vertex -> across its lock (-1 .. 1), for hair strands
        self.PRM = {}            # vertex -> its garment coordinates (u, v), for prints

    def v(self, p, w, prm=None):
        self.V.append((float(p[0]), float(p[1]), float(p[2])))
        self.W.append(w)
        if prm is not None: self.PRM[len(self.V) - 1] = prm
        return len(self.V) - 1

    def f(self, idx, mat, uv=None, smooth=None):
        out = []
        for i in idx:                              # a quad that pinches is a triangle
            if i not in out: out.append(i)
        if len(out) < 3: return -1
        self.F.append(out); self.M.append(mat); self.UV.append(uv)
        self.S.append(self.smooth if smooth is None else smooth)
        return len(self.F) - 1


def loft(P, rings, mat, closed=True, uv=None, cap0=None, cap1=None, smooth=None):
    for a, b in zip(rings[:-1], rings[1:]):
        n = len(a)
        for j in range(n if closed else n - 1):
            k = (j + 1) % n
            P.f((a[j], a[k], b[k], b[j]), mat, uv, smooth)
    if cap0 is not None:
        r = rings[0]; n = len(r)
        for j in range(n): P.f((cap0, r[(j + 1) % n], r[j]), mat, uv, smooth)
    if cap1 is not None:
        r = rings[-1]; n = len(r)
        for j in range(n): P.f((cap1, r[j], r[(j + 1) % n]), mat, uv, smooth)


def box(P, c, ax, ay, az, mat, w, uv=None, smooth=False):
    v = [P.v(c + ax * sx + ay * sy + az * sz, w) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
    for fc in [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]:
        P.f([v[i] for i in fc], mat, uv, smooth)


def torus(P, c, axis, R, r, mat, w, uv=None, n1=12, n2=5, smooth=True):
    axis = axis.normalized()
    a = Vector((1, 0, 0)) if abs(axis.x) < 0.9 else Vector((0, 1, 0))
    u = axis.cross(a).normalized(); v = axis.cross(u).normalized()
    rings = []
    for i in range(n1):
        ang = 2 * math.pi * i / n1
        rad = u * math.cos(ang) + v * math.sin(ang)
        rings.append([P.v(c + rad * R + rad * (r * math.cos(2 * math.pi * j / n2)) + axis * (r * math.sin(2 * math.pi * j / n2)), w)
                      for j in range(n2)])
    for i in range(n1):
        a_, b_ = rings[i], rings[(i + 1) % n1]
        for j in range(n2):
            P.f((a_[j], a_[(j + 1) % n2], b_[(j + 1) % n2], b_[j]), mat, uv, smooth)


def tube(P, pts, radii, mat, weights, nseg=6, cap_end=True, uv=None, up=None, smooth=None, tip=0.6):
    rings = []
    for i, c in enumerate(pts):
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        a = up if up is not None else Vector((0, 0, 1))
        if abs(a.dot(t)) > 0.9: a = Vector((1, 0, 0))
        u = t.cross(a).normalized(); v = t.cross(u).normalized()
        rings.append([P.v(c + (u * math.cos(2 * math.pi * k / nseg) + v * math.sin(2 * math.pi * k / nseg)) * radii[i],
                          weights[i]) for k in range(nseg)])
    c0 = P.v(pts[0] - (pts[1] - pts[0]).normalized() * radii[0] * 0.5, weights[0])
    c1 = P.v(pts[-1] + (pts[-1] - pts[-2]).normalized() * radii[-1] * tip, weights[-1]) if cap_end else None
    loft(P, rings, mat, uv=uv, cap0=c0, cap1=c1, smooth=smooth)
    return rings


# ------------------------------------------------------------------ the dress form
class Surface:
    """one of the body's parametric surfaces: pt(u, v) -> Vector; outward(p) -> an outward direction (for the
    normal's sign); weights(p) -> the skin's bone weights at p; u periodic (radians) unless told otherwise"""
    def __init__(self, name, pt, outward, weights, periodic=True):
        self.name, self.pt, self.outward, self.weights, self.periodic = name, pt, outward, weights, periodic

    def normal(self, u, v, e=1e-4):
        a = self.pt(u + e, v) - self.pt(u - e, v)
        b = self.pt(u, v + e) - self.pt(u, v - e)
        n = a.cross(b)
        if n.length < 1e-12: return self.outward(self.pt(u, v)).normalized()
        n.normalize()
        return n if n.dot(self.outward(self.pt(u, v))) >= 0 else -n

    def metric(self, u, v, e=1e-4):
        """metres per unit of u and of v here"""
        return ((self.pt(u + e, v) - self.pt(u - e, v)).length / (2 * e),
                (self.pt(u, v + e) - self.pt(u, v - e)).length / (2 * e))


def region(P, S, g, us, vs, lift, mat, uv=None, weights=None, lip=True, lockv=(), hang=None, prm=True, lipmat=None):
    """A PANEL cut on surface S, inside g(u, v) < 0, over the grid us x vs (us closed round when S is periodic):
    each quad inside is kept, the boundary vertices snapped onto g's zero line, the panel lifted off the skin by
    lift(u, v) (the fabric's ease), a LIP where it ends -- a wall down to the skin, so no pose shows a gap under an
    edge. hang(p, n, u, v) may move a lifted point (fabric hanging straight off a bulge). Returns the kept quads'
    grid keys. lockv: v rows whose height may not move (where the panel meets another)."""
    nu = len(us)
    per = S.periodic
    par = {(i, j): [us[j], vs[i]] for i in range(len(vs)) for j in range(nu)}
    quads = []
    for i in range(len(vs) - 1):
        for j in range(nu if per else nu - 1):
            k = (j + 1) % nu
            uc = (us[j] + (us[k] if k > j else us[k] + 2 * math.pi)) / 2.0 if per else (us[j] + us[k]) / 2.0
            vc = (vs[i] + vs[i + 1]) / 2.0
            if g(uc, vc) < 0.0:
                quads.append([(i, j), (i, k), (i + 1, k), (i + 1, j)])
    inside = set(frozenset(map(tuple, q)) for q in quads)
    use = {}
    for q in quads:
        for key in q: use[key] = use.get(key, 0) + 1
    edge = {}
    for q in quads:
        for a, b in zip(q, q[1:] + q[:1]):
            e = frozenset((a, b)); edge[e] = edge.get(e, 0) + 1
    bnd_edges = [tuple(e) for e, c in edge.items() if c == 1]
    bverts = set(k for e in bnd_edges for k in e)
    for key in bverts:                                          # snap the outline (Newton, in metres)
        u, v = par[key]
        fixv = vs[key[0]] in lockv
        for _ in range(14):
            gv = g(u, v)
            if abs(gv) < 1e-6: break
            mu, mv = S.metric(u, v)
            e = 1e-5
            gu = (g(u + e, v) - g(u - e, v)) / (2 * e) / max(mu, 1e-6)
            gvv = 0.0 if fixv else (g(u, v + e) - g(u, v - e)) / (2 * e) / max(mv, 1e-6)
            n2 = gu * gu + gvv * gvv
            if n2 < 1e-12: break
            u -= gv * gu / n2 / max(mu, 1e-6)
            v -= gv * gvv / n2 / max(mv, 1e-6)
        par[key] = [u, v]
    vid = {}
    for key in use:
        u, v = par[key]
        p0 = S.pt(u, v); n = S.normal(u, v)
        p = p0 + n * lift(u, v)
        if hang: p = hang(p, n, u, v)
        w = weights(p0) if weights else S.weights(p0)
        vid[key] = P.v(p, w, (u, v) if prm else None)
    for q in quads:
        P.f([vid[k] for k in q], mat, uv)
    if lip:
        base = {}
        for key in bverts:
            u, v = par[key]
            p0 = S.pt(u, v) + S.normal(u, v) * 0.0006
            base[key] = P.v(p0, weights(p0) if weights else S.weights(p0), (u, v) if prm else None)
        for a, b in bnd_edges:
            fi = P.f((vid[a], vid[b], base[b], base[a]), lipmat or mat, uv, smooth=False)
            P.nobvh.add(fi)
    return {"quads": quads, "vid": vid, "par": par, "boundary": bnd_edges}


class Form:
    """the body's skin as one BVH: what straps lie on, and what the garments are measured against"""
    def __init__(self, parts, outward):
        """outward(p, part_name): which way is out of the body at p"""
        self.parts = parts
        V, F, owner = [], [], []
        for P in parts:
            off = len(V)
            V += [Vector(v) for v in P.V]
            for fi, f in enumerate(P.F):
                if fi in P.nobvh: continue
                F.append([off + i for i in f]); owner.append((P, fi))
        self.V, self.F, self.owner = V, F, owner
        self.bvh = BVHTree.FromPolygons(V, F)
        self.outward = outward

    def on_skin(self, g):
        """the nearest point of the skin under g: position, outward normal, the weights of the face it lies on"""
        loc, nrm, idx, dist = self.bvh.find_nearest(Vector(g))
        P, fi = self.owner[idx]
        acc, tot = {}, 0.0
        for vi in P.F[fi]:
            wv = 1.0 / (0.002 + (Vector(P.V[vi]) - loc).length)
            tot += wv
            for bn, x in P.W[vi].items(): acc[bn] = acc.get(bn, 0.0) + x * wv
        n = nrm.normalized()
        if n.dot(self.outward(loc, P.name)) < 0: n = -n
        return loc, n, norm_w({k: v / tot for k, v in acc.items()})

    def strap(self, P, guide, width, lift, th, mat, uv=None, closed=False, step=0.007, over=None):
        """a flat strap along a guide polyline, projected onto the skin, lifted (over(p) adds a garment's ease under
        it), with two edges and a top"""
        g = [Vector(p) for p in guide]
        pts = resample(g + [g[0]] if closed else g, step)
        if closed: pts = pts[:-1]
        S = [self.on_skin(p) for p in pts]
        m = len(S)
        rings = []
        for i, (c, n, w) in enumerate(S):
            a0 = S[(i - 1) % m][0] if closed else S[max(i - 1, 0)][0]
            a1 = S[(i + 1) % m][0] if closed else S[min(i + 1, m - 1)][0]
            tg = (a1 - a0).normalized()
            ac = tg.cross(n).normalized()
            base = c + n * (lift + (over(c) if over else 0.0))
            rings.append([P.v(base - ac * width / 2, w), P.v(base - ac * width / 2 + n * th, w),
                          P.v(base + ac * width / 2 + n * th, w), P.v(base + ac * width / 2, w)])
        for i in range(m if closed else m - 1):
            r0, r1 = rings[i], rings[(i + 1) % m]
            P.f((r0[1], r1[1], r1[2], r0[2]), mat, uv)
            P.f((r0[0], r1[0], r1[1], r0[1]), mat, uv)
            P.f((r0[2], r1[2], r1[3], r0[3]), mat, uv)
        if not closed:
            for r in (rings[0], rings[-1]): P.f((r[0], r[1], r[2], r[3]), mat, uv)
        return S


# ------------------------------------------------------------------ pieces, and the skin each one hides
class Piece:
    """a wearable (or a part of the body): slot, the outfits it is in, its parts, and whether the skin it covers
    is hidden while it is worn (tight garments: yes; loose drapes that swing off the body: no)"""
    def __init__(self, name, slot, outfits, parts, hides=True, maxd=0.012):
        self.name, self.slot, self.outfits, self.parts, self.hides, self.maxd = name, slot, set(outfits), parts, hides, maxd


def vertex_normals(P, outward):
    """per vertex: the sum of its faces' normals, each turned to face out of the body first (a part's faces need
    not agree on their winding)"""
    N = [Vector((0, 0, 0)) for _ in P.V]
    for f in P.F:
        a, b, c = (Vector(P.V[i]) for i in f[:3])
        n = (b - a).cross(c - a)
        ctr = sum((Vector(P.V[i]) for i in f), Vector((0, 0, 0))) / len(f)
        if n.dot(outward(ctr, P.name)) < 0: n = -n
        for i in f: N[i] += n
    return [n.normalized() if n.length > 1e-12 else Vector((0, 0, 1)) for n in N]


def hide_sets(skin_parts, pieces, outward):
    """for every skin face: the set of garments that cover ALL of its vertices (a ray from the skin, along its
    outward normal, meets the garment within its maxd). A face is hidden only when every corner is covered, so the
    skin is never missing beyond a garment's edge -- the garment's lip covers the seam."""
    trees = []
    for pc in pieces:
        if not pc.hides: continue
        V, F = [], []
        for P in pc.parts:
            off = len(V)
            V += [Vector(v) for v in P.V]
            F += [[off + i for i in f] for f in P.F]
        if F: trees.append((pc, BVHTree.FromPolygons(V, F)))
    out = {}
    for P in skin_parts:
        N = vertex_normals(P, outward)
        cov = []
        for vi, v in enumerate(P.V):
            p = Vector(v); n = N[vi]
            hit = set()
            for pc, t in trees:
                # pc.hide_where(p): where this piece may hide skin at all (a long coat hides her torso and arms, never
                # the legs its skirt swings away from)
                hw = getattr(pc, "hide_where", None)
                if hw is not None and not hw(p): continue
                loc, nn, idx, d = t.ray_cast(p - n * 0.002, n, pc.maxd + 0.002)
                if loc is None:
                    # ⛔ AND OUT FROM THE BODY'S AXIS: at a crotch or an armpit the skin's own normal points down or
                    # in, its ray slips between the legs, and the skin was left drawn -- poking through the suit
                    # man's trousers. A second ray, straight out from the body there
                    o = outward(p, P.name); o = Vector(o)
                    if o.length > 1e-9:
                        o.normalize()
                        loc, nn, idx, d = t.ray_cast(p - o * 0.002, o, pc.maxd + 0.002)
                if loc is None:
                    # ⛔ AND JUST UNDER IT: a sewn garment's triangles cut through a rounded bump of the body (a
                    # thigh) though its points stay outside -- the skin there covers the garment and must go
                    loc, nn, idx, d = t.ray_cast(p + n * 0.002, -n, 0.015)
                if loc is not None: hit.add(pc.name)
            cov.append(hit)
        faces = []
        for f in P.F:
            s = set(cov[f[0]])
            for i in f[1:]: s &= cov[i]
            faces.append(frozenset(s))
        out[P.name] = faces
    return out


def split_part(P, face_ids, name):
    """a new part holding only these faces of P (vertices renumbered)"""
    Q = Part(name, P.smooth)
    remap = {}
    for fi in face_ids:
        idx = []
        for vi in P.F[fi]:
            if vi not in remap:
                remap[vi] = Q.v(P.V[vi], P.W[vi], P.PRM.get(vi))
                if vi in P.XA: Q.XA[remap[vi]] = P.XA[vi]
            idx.append(remap[vi])
        Q.F.append(idx); Q.M.append(P.M[fi]); Q.UV.append(P.UV[fi]); Q.S.append(P.S[fi])
    return Q


# ------------------------------------------------------------------ cloth chains: the free panels swing
def chain_weights(p, joints, prefix, root_w, fade=0.020):
    """weights along a hanging chain whose joints run from its root down (their order), by the point's height
    along it: between two joints' midpoints a vertex blends two bones; above the first joint it fades into root_w"""
    zs = [j.z for j in joints]
    mids = [(zs[k] + zs[k + 1]) / 2.0 for k in range(len(zs) - 1)]
    z = p.z
    if z >= mids[0]: t = {0: 1.0}
    elif z <= mids[-1]: t = {len(mids) - 1: 1.0}
    else:
        t = {0: 1.0}
        for k in range(len(mids) - 1):
            if mids[k] >= z >= mids[k + 1]:
                s = (mids[k] - z) / (mids[k] - mids[k + 1]); t = {k: 1.0 - s, k + 1: s}
    hd = smoothstep(zs[0] - fade * 0.6, zs[0] + fade, z)
    w = {"%s%d" % (prefix, k + 1): v * (1.0 - hd) for k, v in t.items()}
    for k, v in root_w.items(): w[k] = w.get(k, 0.0) + v * hd
    return norm_w(w)


# ------------------------------------------------------------------ the atlas
class Atlas:
    """one painted texture: swatches (flat colours, placed as they are named), and BOXES -- rectangles a
    garment's own (u, v) maps into, so a print is painted in the garment's coordinates"""
    def __init__(self, size=4096, reserved_rows=0):
        self.SZ = size
        self.A = np.zeros((size, size, 3), np.float32)
        self.sw = {}                     # name -> (index, rgb)
        self.boxes = {}                  # name -> (x0, y0, x1, y1 in pixels, u0, u1, v0, v1 in garment units)
        self.cell = size // 32           # a swatch cell
        self.shelf = [0, reserved_rows, 0]   # x, y, row height, for placing boxes
        self.wraps = set()

    def swatch(self, name, rgb=None):
        if name not in self.sw:
            k = len(self.sw)
            self.sw[name] = (k, rgb)
            if rgb is not None:
                c, r = k % 32, k // 32
                self.A[r * self.cell:(r + 1) * self.cell, c * self.cell:(c + 1) * self.cell] = np.array(rgb) / 255.0
        return self.sw[name][0]

    def swatch_uv(self, name):
        k = self.sw[name][0]
        return ((k % 32) * self.cell + self.cell / 2.0) / self.SZ, ((k // 32) * self.cell + self.cell / 2.0) / self.SZ

    def box(self, name, w, h, u0, u1, v0, v1, fill=None, wrap=False):
        """a w x h pixel rectangle for a print, mapping garment (u0..u1, v0..v1) onto it; wrap: u is an angle and
        wraps round into the box (its seam where u0 is)"""
        if name in self.boxes: return self.boxes[name]
        if wrap: self.wraps.add(name)
        x, y, rh = self.shelf
        top = 4 * self.cell                          # the swatches' rows
        if y < top: y = top
        if x + w > self.SZ: x, y, rh = 0, y + rh + 4, 0
        if y + h > self.SZ: raise RuntimeError("atlas full: %s" % name)
        self.boxes[name] = (x, y, x + w, y + h, u0, u1, v0, v1)
        self.shelf = [x + w + 4, y, max(rh, h)]
        if fill is not None:
            self.A[y:y + h, x:x + w] = np.array(fill) / 255.0
        return self.boxes[name]

    def box_uv(self, name, u, v):
        x0, y0, x1, y1, u0, u1, v0, v1 = self.boxes[name]
        tu = ((u - u0) % (u1 - u0)) / (u1 - u0) if name in self.wraps else (u - u0) / (u1 - u0)
        tv = (v - v0) / (v1 - v0)
        return (x0 + tu * (x1 - x0)) / self.SZ, (y0 + tv * (y1 - y0)) / self.SZ

    def box_px(self, name):
        """(u, v) in the box's garment units -> atlas pixels, for painting"""
        x0, y0, x1, y1, u0, u1, v0, v1 = self.boxes[name]
        if name in self.wraps:
            return lambda u, v: (x0 + ((u - u0) % (u1 - u0)) / (u1 - u0) * (x1 - x0), y0 + (v - v0) / (v1 - v0) * (y1 - y0))
        return lambda u, v: (x0 + (u - u0) / (u1 - u0) * (x1 - x0), y0 + (v - v0) / (v1 - v0) * (y1 - y0))

    # -- painting
    def coverage(self, polys, pad=2, ss=3):
        """anti-aliased coverage of one or more closed contours (even-odd: holes in glyphs stay holes), in pixels,
        over a local window; returns (y0, x0, cov)"""
        if polys and not isinstance(polys[0][0], (list, tuple)): polys = [polys]
        xs = [p[0] for c in polys for p in c]; ys = [p[1] for c in polys for p in c]
        x0, x1 = max(0, int(min(xs)) - pad), min(self.SZ, int(max(xs)) + pad + 1)
        y0, y1 = max(0, int(min(ys)) - pad), min(self.SZ, int(max(ys)) + pad + 1)
        if x1 <= x0 or y1 <= y0: return 0, 0, np.zeros((0, 0), np.float32)
        X, Y = np.meshgrid(x0 + (np.arange((x1 - x0) * ss) + 0.5) / ss, y0 + (np.arange((y1 - y0) * ss) + 0.5) / ss)
        inside = np.zeros(X.shape, bool)
        for c in polys:
            n = len(c)
            for i in range(n):
                xa, ya = c[i]; xb, yb = c[(i + 1) % n]
                if ya == yb: continue
                cond = (ya > Y) != (yb > Y)
                xint = xa + (Y - ya) * (xb - xa) / (yb - ya)
                inside ^= cond & (X < xint)
        return y0, x0, inside.reshape(y1 - y0, ss, x1 - x0, ss).mean(axis=(1, 3)).astype(np.float32)

    def paint(self, polys, rgb, alpha=1.0, soft=0):
        y0, x0, cov = self.coverage(polys, pad=2 + 3 * soft)
        if cov.size == 0: return
        if soft: cov = blur(cov, soft)
        cov = cov * alpha
        h, w = cov.shape
        c = np.array(rgb, np.float32) / 255.0
        reg = self.A[y0:y0 + h, x0:x0 + w]
        reg[:] = reg * (1 - cov[..., None]) + c * cov[..., None]

    def stroke(self, a, b, wd, rgb, alpha=1.0):
        d = (b[0] - a[0], b[1] - a[1]); L = math.hypot(*d) or 1.0
        n = (-d[1] / L * wd / 2, d[0] / L * wd / 2); e = (d[0] / L * wd / 2, d[1] / L * wd / 2)
        self.paint([(a[0] - e[0] + n[0], a[1] - e[1] + n[1]), (b[0] + e[0] + n[0], b[1] + e[1] + n[1]),
                    (b[0] + e[0] - n[0], b[1] + e[1] - n[1]), (a[0] - e[0] - n[0], a[1] - e[1] - n[1])], rgb, alpha)


def blur(a, r):
    """three box passes (close to a gaussian)"""
    out = a
    for _ in range(3):
        c = np.cumsum(np.pad(out, ((0, 0), (r + 1, r))), axis=1); out = (c[:, 2 * r + 1:] - c[:, :-2 * r - 1]) / (2 * r + 1)
        c = np.cumsum(np.pad(out, ((r + 1, r), (0, 0))), axis=0); out = (c[2 * r + 1:, :] - c[:-2 * r - 1, :]) / (2 * r + 1)
    return out


def text_contours(bpy, s, font_path, size=1.0, align="CENTER", spacing=1.0):
    """a string as closed contours in font units (baseline at y 0), through Blender's own text curves -- any font,
    the kanji included. Returns (contours, (xmin, ymin, xmax, ymax))."""
    cu = bpy.data.curves.new("txt", type="FONT")
    cu.body = s
    cu.font = bpy.data.fonts.load(font_path, check_existing=True)
    cu.size = size
    cu.align_x = align
    cu.space_character = spacing
    ob = bpy.data.objects.new("txt", cu)
    bpy.context.scene.collection.objects.link(ob)
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    c2 = ev.to_curve(dg)                          # a text object's glyphs, as Bezier outlines
    conts = []
    for sp in c2.splines:
        pts = []
        bp = list(sp.bezier_points)
        if not bp: continue
        n = len(bp)
        for i in range(n if sp.use_cyclic_u else n - 1):
            a, b = bp[i], bp[(i + 1) % n]
            p0, p1, p2, p3 = a.co, a.handle_right, b.handle_left, b.co
            for k in range(8):
                t = k / 8.0
                q = p0 * (1 - t) ** 3 + p1 * 3 * t * (1 - t) ** 2 + p2 * 3 * t * t * (1 - t) + p3 * t ** 3
                pts.append((q.x, q.y))
        if len(pts) >= 3: conts.append(pts)
    ev.to_curve_clear()
    bpy.data.objects.remove(ob); bpy.data.curves.remove(cu)
    xs = [p[0] for c in conts for p in c] or [0]; ys = [p[1] for c in conts for p in c] or [0]
    return conts, (min(xs), min(ys), max(xs), max(ys))


def paint_text(A, bpy, s, font, to_px, x, y, h, rgb, align="CENTER", spacing=1.0, width=None):
    """paint s with its cap height ~h (garment units) centred at x (or starting there, align LEFT) on baseline y;
    width squeezes/stretches it to that width. to_px maps garment (u, v) -> atlas pixels."""
    conts, (x0, y0, x1, y1) = text_contours(bpy, s, font, 1.0, align, spacing)
    if not conts: return
    sc = h / max(1e-6, (y1 - max(0.0, y0)) if y1 > 0 else 1.0)
    sx = (width / max(1e-6, x1 - x0)) if width else sc
    cx = (x0 + x1) / 2.0 if align == "CENTER" else x0
    A.paint([[to_px(x + (px - cx) * sx, y + py * sc) for px, py in c] for c in conts], rgb)


# ------------------------------------------------------------------ stretch bones: a drape held between two limbs
def stretch_constraints(rig):
    """the club's STRETCH BONES, live in Blender: every Stretch_<x> gets a Stretch To aimed at the head of Anchor_<x>
    (the club engine's stretchTick and Godot's ClubStretch run the same rig at runtime; glTF carries only the bones)"""
    n = 0
    for pb in rig.pose.bones:
        if not pb.name.startswith("Stretch_"): continue
        an = "Anchor_" + pb.name[len("Stretch_"):]
        if an not in rig.pose.bones:
            print("[atelier] ⛔ %s has no %s" % (pb.name, an)); continue
        c = pb.constraints.new("STRETCH_TO")
        c.target = rig; c.subtarget = an; c.head_tail = 0.0
        c.rest_length = rig.data.bones[pb.name].length
        c.volume = "NO_VOLUME"; c.keep_axis = "SWING_Y"
        n += 1
    if n: print("[atelier] %d stretch bone(s) constrained to their anchors" % n)
    return n
