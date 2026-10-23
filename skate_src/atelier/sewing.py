"""sewing.py -- THE SEWING MACHINE: a garment's flat pattern pieces, placed round the dress form, sewn together by a
cloth simulation seam by seam -- stitch by stitch -- and settled onto the body under gravity.

jacob, 09-29: "some way for reva to actually be making and designing clothes stich by stich" -- and then "engage with
the next steps of the fabrication mode". This is step 2 of design/atelier.md: the tailor's second backend. A panel
cut on the dress form (atelier.region) fits by construction; a panel cut FLAT fits because the cloth finds the 3D shape
that keeps the flat pattern's lengths -- which is what a real garment does -- so ease, drape, a sleeve's hang and a
skirt's flare come out of the pattern, not out of a sculpt.

  Panel     a flat pattern piece: its outline as named EDGES (each a run of 2D points, metres), and a place(x, y) that
            puts a point of it round the form, a few centimetres out, before sewing
  Seam      two edges sewn together (their directions are matched by the machine)
  sew()     cuts every panel into cloth (a constrained Delaunay fill, ~1.5 cm), and simulates:
              stretch   each cloth edge keeps its FLAT length (the pattern is the rest shape)
              bending   each fold resists a little (the fabric's hand: silk soft, denim stiff)
              seams     springs between the two edges' matching points, closing STITCH BY STITCH along the seam
              pins      points held to the body (a waistband)
              collision the body's signed distance field (+ the garments sewn under this one), with friction
              gravity   9.8 m/s^2
            -- position-based dynamics, Jacobi, vectorised in numpy; frames recorded for Fabrication Mode's
            on-screen sewing. Returns the settled cloth per panel, its 2D pattern (its UVs), and the stretch left in
            every edge (FABRIC STRESS, the sheet's readout).
"""
import math

import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.geometry import delaunay_2d_cdt


# ------------------------------------------------------------------ the pattern
class Panel:
    def __init__(self, name, edges, place, fabric=None, pin=None, attach=None):
        """edges: [(edge_name, [(x, y), ...]), ...] chained end to start round the outline, counter-clockwise;
        place(x, y) -> Vector: where that point of the flat panel goes round the form before sewing;
        pin: names of edges held to the body (a waistband)"""
        self.name, self.edges, self.place, self.fabric, self.pin = name, edges, place, fabric or {}, set(pin or ())
        # attach: {edge: [(x, y, z), ...]} -- that edge is SEWN TO A GARMENT ALREADY MADE, along the given line
        # (a lapel to a jacket's front edge, a collar to a neckline): each of its points held at the same share of
        # the way along it
        self.attach = dict(attach or {})


class Seam:
    def __init__(self, a, b, stiff=1.0, when=(0.0, 1.0), auto=True):
        """a, b: 'panel:edge' -- or a list of them, one run after another (an armhole across two panels);
        when: the share of the sewing time this seam's needle runs from and to -- the ORDER a tailor sews in
        (a top: the shoulders first, so it hangs on her before anything else is joined)"""
        self.a = a if isinstance(a, (list, tuple)) else [a]
        self.b = b if isinstance(b, (list, tuple)) else [b]
        self.stiff, self.when, self.auto = stiff, when, auto
        # auto=False: the runs are sewn exactly as given ('panel:edge~' = that edge reversed), end to end, A's start to
        # B's start. ⛔ An armhole's two runs have BOTH ends side by side (the side seam's top and the shoulder point
        # both join front to back), and a sleeve cap's two ends are one point under the arm: guessed by proximity, a
        # sleeve could go in turned half way round the arm


def _resample(pts, h):
    """a polyline resampled at ~h (its end points kept)"""
    P = [np.array(p, float) for p in pts]
    L = sum(np.linalg.norm(b - a) for a, b in zip(P[:-1], P[1:]))
    n = max(1, int(math.ceil(L / h)))
    cum = [0.0]
    for a, b in zip(P[:-1], P[1:]): cum.append(cum[-1] + np.linalg.norm(b - a))
    out = []
    for k in range(n + 1):
        s = L * k / n
        i = min(np.searchsorted(cum, s, side="right") - 1, len(P) - 2)
        seg = cum[i + 1] - cum[i]
        t = 0.0 if seg < 1e-12 else (s - cum[i]) / seg
        out.append(P[i] * (1 - t) + P[i + 1] * t)
    return out


def cut(panel, h=0.015):
    """the panel as cloth: 2D vertices (the pattern), triangles, and for every edge its run of vertex indices"""
    ring, runs = [], {}
    for name, pts in panel.edges:
        rs = _resample(pts, h)
        start = len(ring)
        ring += rs[:-1]                                     # the next edge starts where this one ends
        runs[name] = list(range(start, start + len(rs)))    # (its last index is the next edge's first)
    nb = len(ring)
    for name in runs:
        runs[name] = [i % nb for i in runs[name]]
    B = np.array(ring)
    lo, hi = B.min(0), B.max(0)
    # the inside: a triangular lattice, kept clear of the outline
    pts = [tuple(p) for p in B]
    dy = h * math.sqrt(3) / 2
    y = lo[1] + dy * 0.5
    row = 0
    while y < hi[1]:
        x = lo[0] + (h * 0.5 if row % 2 else 0.0) + h * 0.5
        while x < hi[0]:
            if _inside(B, x, y) and _dist_poly(B, x, y) > 0.55 * h:
                pts.append((x, y))
            x += h
        y += dy; row += 1
    res = delaunay_2d_cdt([Vector((p[0], p[1])) for p in pts], [(i, (i + 1) % nb) for i in range(nb)],
                          [list(range(nb))], 1, 1e-9, True)
    verts, edges_out, faces, orig_verts = res[0], res[1], res[2], res[3]
    V2 = np.array([(v.x, v.y) for v in verts])
    # the CDT may renumber: map our boundary indices to its vertices
    remap = {}
    for new_i, origs in enumerate(orig_verts):
        for o in origs: remap[o] = new_i
    runs = {k: [remap[i] for i in r] for k, r in runs.items()}
    tris = []
    for f in faces:
        for k in range(1, len(f) - 1): tris.append((f[0], f[k], f[k + 1]))
    return V2, np.array(tris, int), runs


def _inside(B, x, y):
    n, c = len(B), False
    for i in range(n):
        xa, ya = B[i]; xb, yb = B[(i + 1) % n]
        if (ya > y) != (yb > y) and x < xa + (y - ya) * (xb - xa) / (yb - ya): c = not c
    return c


def _dist_poly(B, x, y):
    p = np.array((x, y)); best = 1e9
    for i in range(len(B)):
        a, b = B[i], B[(i + 1) % len(B)]
        ab = b - a; t = max(0.0, min(1.0, float(np.dot(p - a, ab) / max(np.dot(ab, ab), 1e-12))))
        best = min(best, float(np.linalg.norm(p - (a + ab * t))))
    return best


# ------------------------------------------------------------------ the body, as a field
LAYER_IN = 0.025        # the deepest "inside" a garment LAYER can have (its own thickness and what lies under it)


class Field:
    """the signed distance to the body (and whatever is already sewn over it), on a grid: negative inside.
    ⛔ ONE PART AT A TIME, the union the least: with every part in one BVH, a point inside the arm's root but nearer
    the torso's hidden side took the TORSO's sign -- outside -- and cloth walked into her shoulder"""
    def __init__(self, groups, res=0.010, pad=0.12, bounds=None, box=None, base=None):
        """box: the grid's (lo, hi) exactly; base: a Field on the same grid to start from (its union continues)"""
        if box is not None:
            lo, hi = np.array(box[0], float), np.array(box[1], float)
        else:
            allV = np.concatenate([np.array(g[0], float) for g in groups])
            lo = allV.min(0) - pad; hi = allV.max(0) + pad
            if bounds is not None:                                 # only where the garment will go
                lo = np.maximum(lo, np.array(bounds[0], float)); hi = np.minimum(hi, np.array(bounds[1], float))
        self.lo, self.res = lo, res
        self.n = np.ceil((hi - lo) / res).astype(int) + 1
        nx, ny, nz = self.n
        D = base.D.copy() if base is not None else np.full((nx, ny, nz), pad, np.float32)
        for grp in groups:
            V, F, normal_of = grp[:3]
            on_edge = grp[3] if len(grp) > 3 else None
            Vv = [Vector(v) for v in V]
            bvh = BVHTree.FromPolygons(Vv, F)
            A = np.array(V, float)
            i0 = np.clip(np.floor((A.min(0) - pad - lo) / res).astype(int), 0, self.n - 1)
            i1 = np.clip(np.ceil((A.max(0) + pad - lo) / res).astype(int), 0, self.n - 1)
            for i in range(i0[0], i1[0] + 1):
                for j in range(i0[1], i1[1] + 1):
                    for k in range(i0[2], i1[2] + 1):
                        p = Vector((lo[0] + i * res, lo[1] + j * res, lo[2] + k * res))
                        loc, nrm, idx, d = bvh.find_nearest(p, pad)
                        if loc is None: continue
                        if on_edge is not None and on_edge(idx, loc):
                            sd = d                                  # nearest its open EDGE: outside, whatever side
                        else:
                            sd = d if (p - loc).dot(normal_of(idx)) >= 0 else -d
                        # ⛔ A LAYER IS THIN. Its sign is one face's half-space, and far from it that means nothing:
                        # a point 11 cm from a bra strap, behind her neck, was "inside the bra" by 107 mm, and the
                        # whole coat sewn over her latex set was lifted 5 cm up her body. Under a layer lies at most
                        # the layers and the skin under it -- never more than LAYER_IN
                        if on_edge is not None and sd < -LAYER_IN: sd = d
                        if sd < D[i, j, k]: D[i, j, k] = sd
        self.D = D
        g = np.gradient(D, res)
        self.G = np.stack(g, -1).astype(np.float32)

    def sample(self, P):
        """distance and gradient at points P (n, 3), trilinear"""
        f = (P - self.lo) / self.res
        out = np.any((f < 0) | (f > self.n - 1), axis=1)       # ⛔ outside the grid is FAR, not its edge cell's value
        i0 = np.clip(np.floor(f).astype(int), 0, self.n - 2)
        t = np.clip(f - i0, 0.0, 1.0)
        d = np.zeros(len(P)); g = np.zeros((len(P), 3))
        for dx in (0, 1):
            for dy in (0, 1):
                for dz in (0, 1):
                    w = (t[:, 0] if dx else 1 - t[:, 0]) * (t[:, 1] if dy else 1 - t[:, 1]) * (t[:, 2] if dz else 1 - t[:, 2])
                    ii = i0[:, 0] + dx; jj = i0[:, 1] + dy; kk = i0[:, 2] + dz
                    d += w * self.D[ii, jj, kk]
                    g += w[:, None] * self.G[ii, jj, kk]
        d[out] = 1.0; g[out] = 0.0
        return d, g


def outward_by_part(body_parts, outward):
    """out of the body at p, by the NEAREST body part's own rule: ⛔ every garment face was turned out by the torso's
    rule (away from the body's centre line), which is BACKWARDS under a sleeve -- the space between a shirt's sleeve
    and the torso counted as inside the shirt, and a jacket sewn over it was shoved round to his back"""
    trees = []
    for P in body_parts:
        V = [Vector(v) for v in P.V]
        trees.append((P.name, BVHTree.FromPolygons(V, [list(f) for f in P.F])))

    def f(p, _name=None):
        p = Vector(p)
        best, name = 1e9, "skin_body"
        for nm, t in trees:
            loc, n_, i_, d = t.find_nearest(p)
            if loc is not None and d < best: best, name = d, nm
        return outward(p, name)
    return f


def body_field(parts, outward, extra=(), res=0.010, bounds=None, body_parts=None):
    """a Field from the skin parts (atelier.Part, build space) and any garment meshes (V, F) already sewn; the
    garments' faces are turned out of the body by its nearest part (body_parts, default the parts given)"""
    def skin_groups(Ps):
        out = []
        for P in Ps:
            FN = []
            for f in P.F:
                a, b, c = Vector(P.V[f[0]]), Vector(P.V[f[1]]), Vector(P.V[f[2]])
                n = (b - a).cross(c - a)
                n = n.normalized() if n.length > 1e-12 else Vector((0, 0, 1))
                o = outward((a + b + c) / 3.0, P.name)
                FN.append(n if n.dot(o) >= 0 else -n)
            out.append((P.V, [list(f) for f in P.F], (lambda FN_: (lambda idx: FN_[idx]))(FN)))
        return out
    groups = skin_groups(parts)
    if not extra:
        return Field(groups, res=res, bounds=bounds)
    bparts = body_parts if body_parts is not None else parts
    pad = 0.12
    allV = np.concatenate([np.array(P.V, float) for P in bparts] + [np.asarray(Vg, float) for Vg, _ in extra])
    lo = allV.min(0) - pad; hi = allV.max(0) + pad
    if bounds is not None:
        lo = np.maximum(lo, np.array(bounds[0], float)); hi = np.minimum(hi, np.array(bounds[1], float))
    # ⭐ A LAYER IS SIGNED BY THE BODY, NOT BY ITS OWN FACES (09-29): out of a layer is out of the BODY there -- the
    # gradient of the body's own field at the face. ⛔ Signed by each face's normal (turned away from the body), a
    # WALL along the body -- a piece's lip down to the skin, the side of a bra strap -- had no outward side, and whole
    # regions beside it came out 7-11 cm INSIDE the bra: the coat sewn over her latex set tore 19-22 cm open
    same = body_parts is None or [id(P) for P in bparts] == [id(P) for P in parts]
    Fb = Field(groups if same else skin_groups(bparts), res=res, box=(lo, hi))
    for Vg, Fg in extra:
        V = [tuple(v) for v in Vg]
        F = [[int(i) for i in f] for f in Fg]
        Va = np.asarray(V, float)
        C = np.array([Va[f].mean(0) for f in F]) if F else np.zeros((0, 3))
        _, gC = Fb.sample(C) if len(C) else (None, np.zeros((0, 3)))
        FN = []
        for k, f in enumerate(F):
            o = Vector(tuple(gC[k]))
            if o.length < 1e-6:                                 # off the body's grid: the old rule
                a, b, c = Vector(V[f[0]]), Vector(V[f[1]]), Vector(V[f[2]])
                o = outward((a + b + c) / 3.0, "skin_body")
            FN.append(o.normalized())
        # ⛔ A SHEET'S SIGN ENDS AT ITS EDGE. Signed by the nearest face's normal, every point under a shirt's hem --
        # the trousers' hips, a tie's knot -- came out INSIDE the shirt and was shoved through it: the trousers'
        # crotch tore 355 mm open. Nearest an open edge, a point is outside.
        cnt = {}
        for f in F:
            for i, j in ((f[0], f[1]), (f[1], f[2]), (f[2], f[0])):
                key = (min(i, j), max(i, j)); cnt[key] = cnt.get(key, 0) + 1
        bnd = {k for k, c in cnt.items() if c == 1}

        def on_edge(idx, loc, V=V, F=F, bnd=bnd):
            f = F[idx]
            for i, j in ((f[0], f[1]), (f[1], f[2]), (f[2], f[0])):
                if (min(i, j), max(i, j)) not in bnd: continue
                a, b = Vector(V[i]), Vector(V[j])
                ab = b - a
                t = max(0.0, min(1.0, (loc - a).dot(ab) / max(ab.dot(ab), 1e-12)))
                if (a + ab * t - loc).length < 1e-4: return True
            return False
        groups.append((V, F, (lambda FN_: (lambda idx: FN_[idx]))(FN), on_edge))
    if same:        # the body's union is already in Fb: continue it with the layers
        return Field(groups[len(parts):], res=res, box=(lo, hi), base=Fb)
    return Field(groups, res=res, box=(lo, hi))


def color_edges(E, N):
    """a greedy edge colouring: lists of edge indices, no two edges in one list sharing a vertex"""
    used = [set() for _ in range(N)]
    col = np.zeros(len(E), int)
    for k, (i, j) in enumerate(E):
        c = 0
        while c in used[i] or c in used[j]: c += 1
        col[k] = c; used[i].add(c); used[j].add(c)
    return [np.where(col == c)[0] for c in range(col.max() + 1)] if len(E) else []


# ------------------------------------------------------------------ the machine
def sew(panels, seams, field, pins=None, steps=320, iters=18, dt=1.0 / 120.0, thick=0.004, stitch_time=1.0,
        fabric=None, record_every=0, gravity=9.8, friction=0.6, settle=0.5, gravity_at=1.0, weld=0.0025,
        sew_damp=0.25, relax=40, layers=None, grow=0.6, grow_depth=0.03, tethers=(), debug=True):
    """sew the panels together on the form; returns {panel: (V3 settled, V2 pattern, tris, runs)}, the edge stretch
    per panel, and the recorded frames [(t, {panel: V3})]"""
    fab = {"stretch": 1.0, "bend": 0.06, "damp": 0.985, "mass": 1.0}
    fab.update(fabric or {})
    cloth = {}
    X, X2, off = [], [], {}
    for p in panels:
        V2, T, runs = cut(p, getattr(p, "h", 0.015))       # a panel may carry its own resolution (a low-poly suit)
        o = sum(len(a) for a in X)                      # ⛔ vertices so far -- len(X) was the PANEL count
        off[p.name] = o
        X2.append(V2)
        X.append(np.array([tuple(p.place(x, y)) for x, y in V2]))
        cloth[p.name] = (V2, T, runs)
    X = np.concatenate(X); X2 = np.concatenate(X2)
    N = len(X)

    def zspan(tag):
        if not debug: return
        out = []
        for p in panels:
            o_ = off[p.name]; n_ = len(cloth[p.name][0])
            out.append("%s %.3f..%.3f" % (p.name.split("_", 1)[-1], X[o_:o_ + n_, 2].min(), X[o_:o_ + n_, 2].max()))
        print("[sew] z %-10s %s" % (tag, "  ".join(out)))
    zspan("placed")
    # the placement, pushed clear of the body and the layers under it -- AS A SHEET: each point's push is smoothed
    # over the cloth (its neighbours come with it), so a panel laid under an earlier garment comes out whole
    # instead of tearing along that garment's hem (the placement stretch went to 571% on the suit's jacket)
    PE = set()
    for p in panels:
        o_ = off[p.name]
        for a, b, c in cloth[p.name][1]:
            for i, j in ((a, b), (b, c), (c, a)): PE.add((o_ + min(i, j), o_ + max(i, j)))
    PE = np.array(sorted(PE), int)
    pdeg = np.bincount(PE.ravel(), minlength=N).astype(float); pdeg[pdeg == 0] = 1.0
    # ...along the PANEL'S OWN NORMAL (the field's gradient turns every which way over the folds of a sagged shirt:
    # pushed along it, the suit's jacket went out 83-161% stretched and was crushed into crumples)
    PT = np.concatenate([np.asarray(cloth[p.name][1]) + off[p.name] for p in panels])
    moved = 0
    for _ in range(80):
        d0, g0 = field.sample(X)
        need = np.clip(thick * 2.0 - d0, 0.0, 0.008)
        bad = need > 1e-5
        if not bad.any(): break
        fn = np.cross(X[PT[:, 1]] - X[PT[:, 0]], X[PT[:, 2]] - X[PT[:, 0]])
        Nv = np.zeros_like(X)
        for k in range(3): np.add.at(Nv, PT[:, k], fn)
        Nv /= np.maximum(np.linalg.norm(Nv, axis=1), 1e-12)[:, None]
        # outward: agree with the field's gradient where it has one (a panel's winding says nothing)
        side = np.sign(np.sum(Nv * g0, axis=1)); side[side == 0] = 1.0
        for p in panels:                                           # one panel, one side
            o_ = off[p.name]; n_ = len(cloth[p.name][0])
            if np.sum(side[o_:o_ + n_]) < 0: Nv[o_:o_ + n_] *= -1
        m_ = need.copy()
        for _s in range(4):                                        # the push, smoothed: max(own, neighbours' mean)
            S_ = np.zeros(N); np.add.at(S_, PE[:, 0], m_[PE[:, 1]]); np.add.at(S_, PE[:, 1], m_[PE[:, 0]])
            m_ = np.maximum(m_, 0.5 * (m_ + S_ / pdeg))
        X += Nv * m_[:, None]
        moved = max(moved, int(bad.sum()))
    if debug and moved: print("[sew] placement: up to %d points pushed clear of the body and the layers under it" % moved)
    zspan("pushed")
    # stretch constraints: every triangle edge, its FLAT length
    E = set()
    TRI = []
    for p in panels:
        V2, T, runs = cloth[p.name]
        o = off[p.name]
        for a, b, c in T:
            TRI.append((o + a, o + b, o + c))
            for i, j in ((a, b), (b, c), (c, a)):
                E.add((o + min(i, j), o + max(i, j)))
    E = np.array(sorted(E), int)
    L0 = np.linalg.norm(X2[E[:, 0]] - X2[E[:, 1]], axis=1)
    # bending: across every interior edge, the two opposite corners keep their flat distance (softly)
    opp = {}
    for a, b, c in TRI:
        for i, j, k in ((a, b, c), (b, c, a), (c, a, b)):
            opp.setdefault((min(i, j), max(i, j)), []).append(k)
    BE = np.array([v for v in opp.values() if len(v) == 2], int).reshape(-1, 2)
    BL0 = np.linalg.norm(X2[BE[:, 0]] - X2[BE[:, 1]], axis=1) if len(BE) else np.zeros(0)
    # ⭐ EACH PANEL'S OWN CLOTH (09-29): a panel's fabric -- `stiff` (how hard it resists stretching: a rib band gives
    # where a fleece body will not), `bend`, `damp` -- over the sew's own. ⛔ Until today every panel's fabric was
    # IGNORED: only sew(fabric=) was read and no garment passed one, so the coat's wool (bend 0.16), the suit's silk tie
    # and every jersey were all sewn as the one default cloth
    vfab = []
    for p in panels:
        f_ = dict(fab); f_.update(p.fabric or {})
        vfab += [f_] * len(cloth[p.name][0])
    KS = np.array([float(vfab[i].get("stiff", 1.0)) for i in E[:, 0]])
    # ⭐ CLOTH BUCKLES, IT DOES NOT PUSH BACK (09-29): `compress` is how hard a fabric resists being SHORTENED, as a
    # share of its stretch stiffness (Vellum's and Marvelous Designer's separate compression). ⛔ Two-sided, REVA's
    # sleep shorts could not be gathered into their elastic waistband: the jersey held its length, the band stretched
    # 21% to meet it, stopped gripping her and the shorts slid off her to the floor
    KC = KS * np.array([float(vfab[i].get("compress", 1.0)) for i in E[:, 0]])
    # ⛔ A STIFFNESS IN PBD COMPOUNDS OVER THE ITERATIONS: 0.2 applied 44 times is 1 - 0.8^44 = 0.99994, and REVA's
    # "soft" jersey held its length against the waistband, which stretched 36% and slid 8 cm down onto her hips. These
    # two are MEANT per sew (the effective share after all the iterations): each iteration takes
    # 1 - (1 - k)^(1/iters) (Mueller et al. 2007, Position Based Dynamics, section 3.3). (`bend` is still per
    # iteration, as every garment so far was tuned)
    KS = 1.0 - (1.0 - np.clip(KS, 0.0, 1.0)) ** (1.0 / max(iters, 1))
    KC = 1.0 - (1.0 - np.clip(KC, 0.0, 1.0)) ** (1.0 / max(iters, 1))
    KB = np.array([float(vfab[i]["bend"]) for i in BE[:, 0]]) if len(BE) else np.zeros(0)
    DV = np.array([float(f_["damp"]) for f_ in vfab])
    # seams: every vertex of one side to the point at the same share of the way along the other side
    def run_of(sp):
        pn, en = sp.rstrip("~").split(":")
        r = [off[pn] + i for i in cloth[pn][2][en]]
        return r[::-1] if sp.endswith("~") else r

    def run_pts(spec, auto=True):
        """one side of a seam: its edges end to end -- each turned to CONTINUE from where the last one ended (an
        armhole is the front's armpit-to-shoulder and the back's, and chained head to tail they jumped from the
        shoulder straight back to the armpit: every sleeve was sewn to a twisted path)"""
        idx = []
        for s in spec:
            r = run_of(s)
            if not auto:
                idx += r if not idx or r[0] != idx[-1] else r[1:]
                continue
            if idx:
                if np.linalg.norm(X[r[-1]] - X[idx[-1]]) < np.linalg.norm(X[r[0]] - X[idx[-1]]):
                    r = r[::-1]
            elif len(spec) > 1:                               # the first run points toward the second
                r2 = run_of(spec[1])
                d_end = min(np.linalg.norm(X[r[-1]] - X[r2[0]]), np.linalg.norm(X[r[-1]] - X[r2[-1]]))
                d_start = min(np.linalg.norm(X[r[0]] - X[r2[0]]), np.linalg.norm(X[r[0]] - X[r2[-1]]))
                if d_start < d_end: r = r[::-1]
            idx += r
        return idx
    SA, SB0, SB1, ST, SD = [], [], [], [], []
    seam_of = []
    vpanel = np.concatenate([np.full(len(cloth[p.name][0]), k) for k, p in enumerate(panels)])

    def chain_cum(S_):
        """the length along one side of a seam, in the PATTERN. ⛔ A side chained across panels (a neckline: front,
        back, back, front) counted the step from one panel's pattern to the next as seam: REVA's neckband gave 12 of
        its 40 cm to the two neck points, pulled them 13 mm open and rumpled; every sleeve cap put ~2 cm of itself on
        the shoulder point. A run's end and the next panel's run's start are the same point of the seam"""
        S_ = np.asarray(S_)
        st_ = np.linalg.norm(np.diff(X2[S_], axis=0), axis=1)
        st_[vpanel[S_[:-1]] != vpanel[S_[1:]]] = 0.0
        return np.concatenate([[0.0], np.cumsum(st_)])
    for si, s in enumerate(seams):
        A = run_pts(s.a, s.auto); Bv = run_pts(s.b, s.auto)
        # match directions: the ends that start nearer each other go together (unless the seam says how it runs)
        if s.auto and (np.linalg.norm(X[A[0]] - X[Bv[0]]) + np.linalg.norm(X[A[-1]] - X[Bv[-1]]) >
                       np.linalg.norm(X[A[0]] - X[Bv[-1]]) + np.linalg.norm(X[A[-1]] - X[Bv[0]])):
            Bv = Bv[::-1]
        # ⛔ SEWN FROM THE TOP DOWN, both sides alike: the needle followed each run's own order, so a jacket's left
        # side seam was sewn from the hem up and its right from the armpit down -- and the left front crumpled
        if s.auto and X[A[0]][2] < X[A[-1]][2]:
            A = A[::-1]; Bv = Bv[::-1]
        for S1, S2 in ((A, Bv), (Bv, A)):
            c2 = chain_cum(S2)
            c1 = chain_cum(S1)
            for k, v in enumerate(S1):
                s_ = c1[k] / max(c1[-1], 1e-9) * c2[-1]
                i = min(max(np.searchsorted(c2, s_, side="right") - 1, 0), len(S2) - 2)
                t = 0.0 if c2[i + 1] - c2[i] < 1e-12 else (s_ - c2[i]) / (c2[i + 1] - c2[i])
                SA.append(v); SB0.append(S2[i]); SB1.append(S2[i + 1]); ST.append(t)
                w0, w1 = s.when                              # this stitch's moment: its share of ITS seam's window
                SD.append(w0 + (w1 - w0) * c1[k] / max(c1[-1], 1e-9)); seam_of.append(si)
    SA, SB0, SB1, ST, SD = map(np.array, (SA, SB0, SB1, ST, SD))
    SK = np.array([seams[i].stiff for i in seam_of]) if len(seam_of) else np.zeros(0)
    # pins: vertices of pinned edges held where the body is under them
    W = np.ones(N)
    pin_pos = {}
    for p in panels:
        for en in p.pin:
            for i in cloth[p.name][2][en]:
                v = off[p.name] + i
                d, g = field.sample(X[v:v + 1])
                gn = g[0] / max(np.linalg.norm(g[0]), 1e-9)
                pin_pos[v] = X[v] - gn * (d[0] - thick * 1.5)
    for v, q in (pins or {}).items(): pin_pos[v] = q
    for p in panels:                                           # edges sewn to a garment already made
        for en, line in getattr(p, "attach", {}).items():
            L_ = np.asarray(line, float)
            cl = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(L_, axis=0), axis=1))])
            run = cloth[p.name][2][en]
            V2r = cloth[p.name][0][run]
            cr = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(V2r, axis=0), axis=1))])
            for k, i in enumerate(run):
                sh = cr[k] / max(cr[-1], 1e-9) * cl[-1]
                j = min(max(int(np.searchsorted(cl, sh, side="right")) - 1, 0), len(L_) - 2)
                t_ = 0.0 if cl[j + 1] - cl[j] < 1e-12 else (sh - cl[j]) / (cl[j + 1] - cl[j])
                pin_pos[off[p.name] + i] = L_[j] * (1 - t_) + L_[j + 1] * t_
    PV = np.array(sorted(pin_pos), int)
    PQ = np.array([pin_pos[v] for v in PV]) if len(PV) else np.zeros((0, 3))
    colors = color_edges(E, N)
    bcolors = color_edges(BE, N) if len(BE) else []
    Wm = np.ones(N)
    if len(PV): Wm[PV] = 0.0                                  # a pinned point does not move for a constraint
    STEP_MAX, VMAX = 0.004, 2.5
    made = np.zeros(len(SA), bool)                          # a stitch, once it has closed, is SEWN: held shut
    # TETHERS: a button with slack, a drawstring -- two edges' points (each the one nearest a given 3D point) held
    # no more than a distance apart: [("panel:edge", (x, y, z), "panel:edge", (x, y, z), max_dist), ...]
    TA, TB, TL = [], [], []
    for ea, pa, eb, pb, md in tethers:
        ids = []
        for spec, pt in ((ea, pa), (eb, pb)):
            pn, en = spec.split(":")
            run = [off[pn] + i for i in cloth[pn][2][en]]
            ids.append(run[int(np.argmin(np.linalg.norm(X[run] - np.asarray(pt, float), axis=1)))])
        TA.append(ids[0]); TB.append(ids[1]); TL.append(md)
    TA, TB, TL = np.array(TA, int), np.array(TB, int), np.array(TL, float)
    print("[sew] %d particles, %d edges in %d colours, %d bends, %d stitches, %d pinned"
          % (N, len(E), len(colors), len(BE), len(SA), len(PV)))
    # ⭐ THE CLOTH IS LAID OUT BEFORE IT IS SEWN: stretch and collision only -- no needle, no gravity -- so a panel
    # pushed out over the layers under it arrives at its own size, lying on them, and the stitches start from rest
    for it in range(relax):
        for ci in colors:
            i0, i1 = E[ci, 0], E[ci, 1]
            d = X[i1] - X[i0]
            l = np.linalg.norm(d, axis=1) + 1e-12
            wsum = Wm[i0] + Wm[i1] + 1e-12
            c = ((l - L0[ci]) / l / wsum * KS[ci])[:, None] * d
            X[i0] += c * Wm[i0][:, None]
            X[i1] -= c * Wm[i1][:, None]
        dist, grad = field.sample(X)
        hit = dist < thick
        if hit.any():
            gn = grad[hit] / np.maximum(np.linalg.norm(grad[hit], axis=1), 1e-9)[:, None]
            X[hit] += gn * (thick - dist[hit])[:, None]
    zspan("relaxed")
    if debug:                                                    # where the cloth starts INSIDE something
        d_, g_ = field.sample(X)
        for p in panels:
            o_ = off[p.name]; n_ = len(cloth[p.name][0])
            m_ = d_[o_:o_ + n_] < 0.0
            if m_.any():
                Q = X[o_:o_ + n_][m_]
                print("[sew] inside at the start: %-16s %3d  z %.2f..%.2f  x %+.2f..%+.2f  y %+.2f..%+.2f  (depth to %.1f mm)"
                      % (p.name, int(m_.sum()), Q[:, 2].min(), Q[:, 2].max(), Q[:, 0].min(), Q[:, 0].max(),
                         Q[:, 1].min(), Q[:, 1].max(), -1000 * d_[o_:o_ + n_][m_].min()))
    V = np.zeros_like(X)
    st0 = np.linalg.norm(X[E[:, 1]] - X[E[:, 0]], axis=1) / L0 - 1.0
    for p in panels:                                       # how far the placement stretched each panel
        o = off[p.name]; sel = (E[:, 0] >= o) & (E[:, 0] < o + len(cloth[p.name][0]))
        print("[sew] %-18s placed: stretch median %+.0f%%, 95th %+.0f%%" % (
            p.name, 100 * np.median(st0[sel]), 100 * np.percentile(st0[sel], 95)))
    frames = []
    total_t = steps * dt
    t_grow = stitch_time * gravity_at                          # the layers start growing once it is sewn

    def collide(Pq, t_now):
        """the body, and (once it is sewn) the layers under it, grown out of the body over `grow` seconds"""
        d_, g_ = field.sample(Pq)
        if layers is None or t_now < t_grow: return d_, g_
        depth = grow_depth * max(0.0, 1.0 - (t_now - t_grow) / max(grow, 1e-6))
        dl_, gl_ = layers.sample(Pq)
        dl_ = dl_ + depth
        use = dl_ < d_
        d_ = np.where(use, dl_, d_); g_ = np.where(use[:, None], gl_, g_)
        return d_, g_
    cnt_e = np.bincount(E.ravel(), minlength=N).astype(float)
    for step in range(steps):
        t = step * dt
        # stitch by stitch: a seam point is sewn once the needle has reached its share of the seam
        stitched = SD <= (t / stitch_time) if len(SD) else np.zeros(0, bool)
        # ⛔ gravity waits for the needle: with it at full strength by 0.6 s and the shoulder seams' neck ends sewn
        # at 1.0 s, the first sweatshirt slid down her body as an open tube and was sewn shut round her shins
        tg = t - stitch_time * gravity_at
        g_on = gravity * min(1.0, max(0.0, tg / max(stitch_time * settle, 1e-6))) if stitch_time > 0 else gravity
        V[:, 2] -= g_on * dt
        P = X + V * dt
        if len(PV):
            P[PV] = X[PV] + (PQ - X[PV]) * min(1.0, (t + dt) / (stitch_time * 0.5))
        for it in range(iters):
            # STRETCH, colour by colour: no two edges of a colour share a vertex, so each colour is projected at
            # once, in full -- Gauss-Seidel's convergence, vectorised (averaged Jacobi propagated one edge per
            # iteration and the stitches tore their neighbours to 3x rather than move the panels)
            for ci in colors:
                i0, i1 = E[ci, 0], E[ci, 1]
                d = P[i1] - P[i0]
                l = np.linalg.norm(d, axis=1) + 1e-12
                wsum = Wm[i0] + Wm[i1] + 1e-12
                Lr = L0[ci] * fab["stretch"]
                c = ((l - Lr) / l / wsum * np.where(l < Lr, KC[ci], KS[ci]))[:, None] * d
                P[i0] += c * Wm[i0][:, None]
                P[i1] -= c * Wm[i1][:, None]
            if len(BE) and it % 2 == 0:
                for ci in bcolors:
                    i0, i1 = BE[ci, 0], BE[ci, 1]
                    d = P[i1] - P[i0]
                    l = np.linalg.norm(d, axis=1) + 1e-12
                    wsum = Wm[i0] + Wm[i1] + 1e-12
                    c = ((l - BL0[ci]) / l / wsum * KB[ci])[:, None] * d
                    P[i0] += c * Wm[i0][:, None]
                    P[i1] -= c * Wm[i1][:, None]
            if stitched.any():                               # THE STITCHES: a needle's pull, a few mm at a time
                m = stitched
                q = P[SB0[m]] * (1 - ST[m])[:, None] + P[SB1[m]] * ST[m][:, None]
                d = (q - P[SA[m]]) * (0.5 * SK[m])[:, None]
                dl = np.linalg.norm(d, axis=1)
                # ⛔ a stitch that has closed is SEWN -- pulled all the way, every iteration: a needle's pull that
                # stayed a few mm at a time let the sleeves and gravity tear the shoulder seam 134 mm open
                lim = np.where(made[m], 1.0, np.minimum(1.0, STEP_MAX / np.maximum(dl, 1e-12)))
                d *= lim[:, None]
                dA = np.zeros_like(P); nA = np.zeros(N)
                np.add.at(dA, SA[m], d); np.add.at(nA, SA[m], 1.0)
                np.add.at(dA, SB0[m], -d * (1 - ST[m])[:, None]); np.add.at(nA, SB0[m], (1 - ST[m]))
                np.add.at(dA, SB1[m], -d * ST[m][:, None]); np.add.at(nA, SB1[m], ST[m])
                P += dA / np.maximum(nA, 1.0)[:, None] * Wm[:, None]
            if len(TA):                                      # the tethers: pulled in only past their slack
                d_ = P[TB] - P[TA]; l_ = np.linalg.norm(d_, axis=1) + 1e-12
                ex = np.maximum(0.0, l_ - TL) / l_ * 0.5
                P[TA] += d_ * ex[:, None]; P[TB] -= d_ * ex[:, None]
            if len(PV): P[PV] = X[PV] + (PQ - X[PV]) * min(1.0, (t + dt) / (stitch_time * 0.5))
            dist, grad = collide(P, t)
            hit = dist < thick
            if hit.any():
                gn = grad[hit] / np.maximum(np.linalg.norm(grad[hit], axis=1), 1e-9)[:, None]
                P[hit] += gn * (thick - dist[hit])[:, None]
        Vn = (P - X) / dt * DV[:, None]
        if t < stitch_time * gravity_at:                      # sewing is QUASI-STATIC: the needle, not a whip
            Vn *= sew_damp
        vl = np.linalg.norm(Vn, axis=1)
        Vn *= np.minimum(1.0, VMAX / np.maximum(vl, 1e-12))[:, None]     # nothing whips
        dist, grad = collide(P, t)
        touch = dist < thick * 1.6
        if touch.any():                                       # friction: contact bleeds the sliding speed
            gn = grad[touch] / np.maximum(np.linalg.norm(grad[touch], axis=1), 1e-9)[:, None]
            vn = (Vn[touch] * gn).sum(1)[:, None] * gn
            Vn[touch] = vn + (Vn[touch] - vn) * (1.0 - friction)
        X, V = P, Vn
        if stitched.any():
            q = X[SB0] * (1 - ST)[:, None] + X[SB1] * ST[:, None]
            made |= stitched & (np.linalg.norm(X[SA] - q, axis=1) < weld)
        if debug and step % 40 == 0:
            st = np.linalg.norm(X[E[:, 1]] - X[E[:, 0]], axis=1) / L0 - 1.0
            gp = 0.0
            if stitched.any():
                q = X[SB0] * (1 - ST)[:, None] + X[SB1] * ST[:, None]
                gp = float(np.linalg.norm(X[SA] - q, axis=1)[stitched].mean())
            dd, _ = field.sample(X)
            print("[sew]   step %3d: stretch median %+.0f%% 95th %+.0f%%, stitched %d/%d (sewn %d), mean gap %.1f mm, %d inside, |v| max %.2f"
                  % (step, 100 * np.median(st), 100 * np.percentile(st, 95), int(stitched.sum()), len(SD), int(made.sum()), gp * 1000,
                     int((dd < 0).sum()), float(np.linalg.norm(V, axis=1).max())))
            for p in panels:
                o_ = off[p.name]; Xp = X[o_:o_ + len(cloth[p.name][0])]
                print("[sew]      %-16s z %.2f..%.2f  x %.2f..%.2f  y %.2f..%.2f" % (p.name, Xp[:, 2].min(), Xp[:, 2].max(),
                      Xp[:, 0].min(), Xp[:, 0].max(), Xp[:, 1].min(), Xp[:, 1].max()))
        if record_every and step % record_every == 0:
            frames.append((t, {p.name: X[off[p.name]:off[p.name] + len(cloth[p.name][0])].copy() for p in panels},
                           made.copy() if len(SD) else None))
    # how well each seam closed (before they are snapped shut)
    gaps = {}
    if len(SA):
        q = X[SB0] * (1 - ST)[:, None] + X[SB1] * ST[:, None]
        gap = np.linalg.norm(X[SA] - q, axis=1)
        gap_at = {}
        for si in range(len(seams)):
            m = np.array(seam_of) == si
            if m.any():
                key = "%s = %s" % ("+".join(seams[si].a), "+".join(seams[si].b))
                gaps[key] = float(gap[m].max())
                k = np.where(m)[0][int(np.argmax(gap[m]))]
                w0, w1 = seams[si].when
                share = (SD[k] - w0) / max(w1 - w0, 1e-9)       # how far along the seam the worst gap is
                gap_at[key] = (float(share), tuple(float(c) for c in X[SA[k]]))
        X[SA] = (X[SA] + q) * 0.5                            # the seams, closed: each sewn point on its partner
    stretch = np.linalg.norm(X[E[:, 1]] - X[E[:, 0]], axis=1) / L0 - 1.0
    out = {}
    for p in panels:
        V2, T, runs = cloth[p.name]
        o = off[p.name]
        sel = (E[:, 0] >= o) & (E[:, 0] < o + len(V2))
        out[p.name] = {"V3": X[o:o + len(V2)].copy(), "V2": V2, "T": T, "runs": runs,
                       "stress": float(np.percentile(np.abs(stretch[sel]), 95)) if sel.any() else 0.0}
    seamlines = {"A": SA, "B0": SB0, "B1": SB1, "T": ST, "D": SD, "seam": np.array(seam_of), "off": off, "gaps": gaps,
                 "gap_at": gap_at if len(SA) else {}}
    return out, frames, seamlines


# ------------------------------------------------------------------ a sewn garment, WORN: weights and a Part
def garment_normals(V, T, outward=None):
    """per-vertex normals of a triangle soup (area-weighted), each face turned out of the body first if outward"""
    V = np.asarray(V, float); T = np.asarray(T, int)
    n = np.cross(V[T[:, 1]] - V[T[:, 0]], V[T[:, 2]] - V[T[:, 0]])
    if outward is not None:
        ctr = V[T].mean(1)
        o = np.array([tuple(outward(Vector(c))) for c in ctr])
        flip = (n * o).sum(1) < 0
        n[flip] *= -1
    N = np.zeros_like(V)
    for k in range(3): np.add.at(N, T[:, k], n)
    L = np.linalg.norm(N, axis=1); L[L < 1e-12] = 1.0
    return N / L[:, None]


def transfer_weights(V, T, links, skin_parts, outward, dmax=0.06, angle_max=35.0, iters=600, debug=True, name=""):
    """ROBUST SKIN WEIGHT TRANSFER (after Abdrashitov et al., "Robust Skin Weights Transfer via Weight Inpainting",
    SIGGRAPH Asia 2023): a garment vertex takes the body's weights only where it MATCHES the body -- the nearest skin
    is within dmax and faces the same way (within angle_max) -- and every other vertex is INPAINTED: the weights
    that minimise the garment's own Dirichlet energy with the matched ones held (a harmonic fill over the cloth, its
    seams included via links). A skirt's cloth between the legs then blends both legs instead of sticking to the
    nearer one; a sleeve's underarm, far from any one bone, is filled from the sleeve and the bodice around it.
    V (n,3), T (m,3) the garment; links: extra (i, j) pairs (seams); returns [dict bone -> w] per vertex."""
    from mathutils.bvhtree import BVHTree as _B
    V = np.asarray(V, float); T = np.asarray(T, int); n = len(V)
    BV, BF, owner = [], [], []
    for P in skin_parts:
        off = len(BV)
        BV += [Vector(v) for v in P.V]
        for fi, f in enumerate(P.F):
            if fi in getattr(P, "nobvh", ()): continue
            BF.append([off + i for i in f]); owner.append((P, fi))
    bvh = _B.FromPolygons(BV, BF)
    Ng = garment_normals(V, T, outward_by_part(skin_parts, outward))
    bones, W0, ok = {}, [], np.zeros(n, bool)
    rows = []
    cos_max = math.cos(math.radians(angle_max))
    for i in range(n):
        p = Vector(V[i])
        loc, nrm, idx, dist = bvh.find_nearest(p)
        P, fi = owner[idx]
        acc, tot = {}, 0.0
        for vi in P.F[fi]:
            wv = 1.0 / (0.002 + (Vector(P.V[vi]) - loc).length); tot += wv
            for bn, x in P.W[vi].items(): acc[bn] = acc.get(bn, 0.0) + x * wv
        w = {k: v / tot for k, v in acc.items()}
        nb = nrm.normalized()
        if nb.dot(outward(loc, P.name)) < 0: nb = -nb
        c = float(np.dot(Ng[i], tuple(nb)))
        ok[i] = dist < dmax and c > cos_max
        for k in w: bones.setdefault(k, len(bones))
        rows.append(w)
    Wm = np.zeros((n, len(bones)))
    for i, w in enumerate(rows):
        for k, x in w.items(): Wm[i, bones[k]] = x
    # the cloth's graph: triangle edges and the seams (cotangent weights would be the paper's; uniform is enough
    # on a near-uniform cloth mesh)
    E = set()
    for a, b, c in T:
        for i, j in ((a, b), (b, c), (c, a)): E.add((min(i, j), max(i, j)))
    for i, j in links:
        if i != j: E.add((min(i, j), max(i, j)))
    E = np.array(sorted(E), int)
    deg = np.bincount(E.ravel(), minlength=n).astype(float); deg[deg == 0] = 1.0
    free = ~ok
    X = Wm.copy()
    for it in range(iters):                                 # Jacobi on the free vertices, the matched held
        S = np.zeros_like(X)
        np.add.at(S, E[:, 0], X[E[:, 1]]); np.add.at(S, E[:, 1], X[E[:, 0]])
        Xn = S / deg[:, None]
        delta = np.abs(Xn[free] - X[free]).max() if free.any() else 0.0
        X[free] = Xn[free]
        if delta < 1e-5: break
    names = sorted(bones, key=bones.get)
    out = []
    for i in range(n):
        w = {names[k]: float(X[i, k]) for k in np.argsort(-X[i])[:4] if X[i, k] > 0.004}
        s_ = sum(w.values()) or 1.0
        out.append({k: v / s_ for k, v in w.items()})
    if debug:
        print("[wear] %s: %d vertices, %d matched to the body (%.0f%%), %d inpainted over the cloth (%d iterations)"
              % (name, n, int(ok.sum()), 100.0 * ok.mean(), int(free.sum()), it + 1))
    return out


def solidify(V, T, N, thick):
    """a cloth sheet given thickness: the outer surface, an inner one thick under it (wound the other way), and a
    rim along every open edge -- a garment's hems and cuffs have an edge, and its inside can be seen"""
    V = np.asarray(V, float); T = np.asarray(T, int); n = len(V)
    Vi = V - np.asarray(N) * thick
    VV = np.concatenate([V, Vi])
    TT = [tuple(t) for t in T] + [(c + n, b + n, a + n) for a, b, c in T]
    cnt = {}
    for a, b, c in T:
        for i, j in ((a, b), (b, c), (c, a)): cnt[(i, j)] = cnt.get((i, j), 0) + 1
    rim = 0
    for (i, j), k in cnt.items():
        if (j, i) not in cnt:                               # an open edge (one triangle uses it, the other way none)
            TT.append((j, i, i + n)); TT.append((j, i + n, j + n)); rim += 1
    return VV, TT, rim


def to_part(res, seamlines, skin_parts, outward, name, mat, uv=None, thick=0.0025, dmax=0.06, angle_max=35.0,
            box=None, flat=False, even=0, skirt=None):
    """the sewing machine's result -> an atelier Part: the settled cloth (its seams snapped shut), outward-wound,
    weighted by the robust transfer (seams link the panels, so both sides of a seam carry the same weights), with
    thickness; each panel's PATTERN coordinates ride along as PRM (u, v in metres) for prints. uv: a paint spec for
    every face, e.g. ("swatch", "suit"); box: a callable panel -> (box name, (u0, v0, u1, v1)) to map a panel's
    pattern into an atlas box instead"""
    from atelier import Part
    names = list(res.keys())
    off, Vs, Ts, V2s = {}, [], [], []
    o = 0
    for k in names:
        r = res[k]
        off[k] = o; Vs.append(r["V3"]); Ts.append(np.asarray(r["T"]) + o); V2s.append(r["V2"]); o += len(r["V3"])
    V = np.concatenate(Vs); T = np.concatenate(Ts); V2 = np.concatenate(V2s)
    # the sewing machine's own vertex numbering is the same concatenation (panels in order): its seam pairs link
    links = []
    A_, B0, B1, ST = seamlines["A"], seamlines["B0"], seamlines["B1"], seamlines["T"]
    for a, b0, b1, t in zip(A_, B0, B1, ST):
        links.append((int(a), int(b0 if t < 0.5 else b1)))
    # outward winding, per face -- by the nearest body part (a sleeve's underside faces AWAY from the arm)
    n_ = np.cross(V[T[:, 1]] - V[T[:, 0]], V[T[:, 2]] - V[T[:, 0]])
    ctr = V[T].mean(1)
    ob = outward_by_part(skin_parts, outward)
    ow = np.array([tuple(ob(Vector(c))) for c in ctr])
    flip = (n_ * ow).sum(1) < 0
    T[flip] = T[flip][:, [0, 2, 1]]
    if even:                                   # the simulation's ragged hems and necklines, evened along themselves
        # ⛔ ONLY THE GARMENT'S OWN EDGES, never its seams: here every panel is still apart, so every seam is a "boundary"
        # too, and a curved one (an armhole round the top of her shoulder) evened along itself shrinks INTO her -- the
        # coat came out with her skin showing through on top of both shoulders and along the back of her collar
        seam_v = set(int(i) for k in ("A", "B0", "B1") for i in seamlines[k])
        cnt = {}
        for a, b, c in T:
            for i, j in ((a, b), (b, c), (c, a)): cnt[(min(i, j), max(i, j))] = cnt.get((min(i, j), max(i, j)), 0) + 1
        nb = {}
        for (i, j), c_ in cnt.items():
            if c_ == 1: nb.setdefault(i, []).append(j); nb.setdefault(j, []).append(i)
        ids = np.array([i for i, js in nb.items() if len(js) == 2 and i not in seam_v], int)
        J = np.array([nb[i] for i in ids], int)
        for _ in range(even):
            V[ids] = V[ids] * 0.5 + (V[J[:, 0]] + V[J[:, 1]]) * 0.25
    W = transfer_weights(V, T, links, skin_parts, outward, dmax=dmax, angle_max=angle_max, name=name)
    if skirt is not None:
        # ⭐ A SKIRT DOES NOT SPLIT (09-30): below `skirt` (a height) each leg bone's weight is shared equally between
        # the two legs, blended in over 10 cm -- the skirt moves as one piece with both thighs. ⛔ Matched to the
        # nearest leg, REVA's long coat followed each thigh on its own: in the skate crouch it parted into two
        # ballooning trouser legs. (A skinned skirt still lets a knee through in a deep stride; the next step is a
        # skirt on cloth chains with leg collision -- design/atelier.md)
        n_sym = 0
        for i, w in enumerate(W):
            t = min(1.0, max(0.0, (skirt - V[i][2]) / 0.10))
            if t <= 0.0: continue
            t = t * t * (3 - 2 * t)
            for base in ("UpLeg", "Leg", "Foot", "ToeBase"):
                l, r = w.get("Left" + base, 0.0), w.get("Right" + base, 0.0)
                if l == 0.0 and r == 0.0: continue
                m = 0.5 * (l + r)
                w["Left" + base] = l + (m - l) * t
                w["Right" + base] = r + (m - r) * t
            n_sym += 1
        print("[wear] %s: a skirt below z %.2f -- %d vertices share both legs" % (name, skirt, n_sym))
    N = garment_normals(V, T)
    if thick > 0:
        VV, TT, rim = solidify(V, T, N, thick)
        WW = W + W
        PP = np.concatenate([V2, V2])
        panel_of = np.concatenate([np.full(len(r["V3"]), i) for i, r in enumerate(res[k] for k in names)])
        panel_of = np.concatenate([panel_of, panel_of])
    else:
        VV, TT, WW, PP = V, [tuple(t) for t in T], W, V2
        panel_of = np.concatenate([np.full(len(res[k]["V3"]), i) for i, k in enumerate(names)])
    P = Part(name, smooth=not flat)
    for i, p in enumerate(VV):
        P.v(p, WW[i], (float(PP[i][0]), float(PP[i][1])))
    for t in TT:
        spec = uv
        if box is not None:
            b = box(names[int(panel_of[t[0]])])
            if b is not None: spec = ("box", b)
        P.f(t, mat, spec)
    return P


class Layers:
    """THE DRESSED FORM's thickness: how far the garments already sewn stand off the body -- the mean, round the
    torso at a height (torso(v)) and round an arm at a t along it (arm(t)) -- measured by rays out of the body's own
    surfaces along their normals to the farthest garment within reach. A draft over them measures its girths there."""
    def __init__(self, form, torso_meshes, arm_meshes=(), reach=0.06, n=24):
        """torso_meshes: what is worn round the body (a shirt's body, trousers, a tie); arm_meshes: round the arms
        (its sleeves). ⛔ ONE stack for both: a ray out of the torso beside the armhole met the shirt's SLEEVE and laid
        the jacket's body 5-9 cm out over it -- 630% stretch"""
        def tree(meshes):
            V, F = [], []
            for Vg, Tg in meshes:
                o = len(V); V += [Vector(tuple(v)) for v in Vg]; F += [[o + int(i) for i in t] for t in Tg]
            return BVHTree.FromPolygons(V, F) if F else None
        self.trees = {"torso": tree(torso_meshes), "arm": tree(arm_meshes)}
        self.F, self.reach, self.n, self.ct, self.ca = form, reach, n, {}, {}

    def _far(self, p, d, which="torso"):
        """the farthest garment along the ray (the outermost layer), 0 if none"""
        best, q, left = 0.0, p + d * 0.0005, self.reach
        bvh = self.trees.get(which)
        while left > 0 and bvh is not None:
            loc, nrm, idx, dist = bvh.ray_cast(q, d, left)
            if loc is None: break
            best = (loc - p).length
            q = loc + d * 0.0005; left = self.reach - best
        return best

    def at(self, p, n, which="torso"):
        """how far the outermost garment stands off the body at p, along its normal n"""
        return self._far(Vector(p), Vector(n).normalized(), which)

    def torso(self, v):
        key = round(v, 3)
        if key not in self.ct:
            T = self.F.torso
            offs = [self._far(T.pt(2 * math.pi * k / self.n, v), T.normal(2 * math.pi * k / self.n, v))
                    for k in range(self.n)]
            self.ct[key] = float(np.mean(offs)) if offs else 0.0
        return self.ct[key]

    def arm(self, t):
        key = round(t, 3)
        if key not in self.ca:
            S = self.F.arm["Left"]
            offs = [self._far(S.pt(2 * math.pi * k / 12, t), S.normal(2 * math.pi * k / 12, t), "arm") for k in range(12)]
            self.ca[key] = float(np.mean(offs)) if offs else 0.0
        return self.ca[key]


def lay(panels, h=None):
    """TRIMS LAID, NOT SEWN: each panel cut into cloth and placed exactly where its place() puts it -- for small, stiff
    pieces that sit on a garment already made (a lapel on a jacket front, a collar round a neckline): their shape is
    the placement's, and a simulation of a 6 cm stiff flap only buckles it (the first sewn collar came back 281%
    stretched, a band cut straight but folded round a cone). Returns the same {panel: {V3, V2, T, runs, stress}} as
    sew(), and seamlines with nothing sewn."""
    out = {}
    for p in panels:
        V2, T, runs = cut(p, h if h is not None else getattr(p, "h", 0.015))
        V3 = np.array([tuple(p.place(x, y)) for x, y in V2])
        out[p.name] = {"V3": V3, "V2": V2, "T": T, "runs": runs, "stress": 0.0}
    empty = np.zeros(0, int)
    return out, {"A": empty, "B0": empty, "B1": empty, "T": np.zeros(0), "D": np.zeros(0), "seam": empty,
                 "off": {}, "gaps": {}, "gap_at": {}}
