"""shapes.py -- the city cook's geometry, all of it numpy: lines made dense, bands laid along them, walls stood on them, polygons cut into
triangles, and a buffer that sorts what's made into the cells it falls in.

Coordinates in here are the map's own: e (east) and n (north) in metres from the origin, h in metres above sea level. The buffer turns them
into the engine's: x = e * U, y = (h - floor) * U, z = -n * U, U units a metre. z is south because the engine's y is up and its hand is right."""
import math

import numpy as np


def densify(P, step):
    """a polyline (k, 2) with no piece longer than step. Gives the points and, for each, its place along the original as a float index (2.5 is half
    way from point 2 to point 3), so anything known at the original points can be laid along the new ones"""
    P = np.asarray(P, dtype=np.float64)
    if len(P) < 2:
        return P.copy(), np.zeros(len(P))
    d = np.hypot(*(P[1:] - P[:-1]).T)
    n = np.maximum(1, np.ceil(d / step).astype(int))
    pts, at = [], []
    for i in range(len(P) - 1):
        f = np.arange(n[i]) / n[i]
        pts.append(P[i] + (P[i + 1] - P[i]) * f[:, None])
        at.append(i + f)
    pts.append(P[-1:])
    at.append(np.array([len(P) - 1.0]))
    return np.concatenate(pts), np.concatenate(at)


def along(values, at):
    """values known at a polyline's points, at float indices along it (from densify)"""
    v = np.asarray(values, dtype=np.float64)
    i = np.minimum(np.floor(at).astype(int), len(v) - 2) if len(v) > 1 else np.zeros(len(at), int)
    f = at - i
    if len(v) == 1:
        return np.full(len(at), v[0])
    return v[i] * (1 - f) + v[i + 1] * f


def arclen(P):
    P = np.asarray(P, dtype=np.float64)
    if len(P) < 2:
        return np.zeros(len(P))
    return np.concatenate([[0.0], np.cumsum(np.hypot(*(P[1:] - P[:-1]).T))])


def smooth(values, s, radius):
    """a moving average over arc length: each point the mean of the points within radius of it along the line. That's the road's grade: the
    elevation's every bump under the asphalt goes, and the hills stay"""
    v = np.asarray(values, dtype=np.float64)
    if radius <= 0 or len(v) < 3:
        return v.copy()
    c = np.concatenate([[0.0], np.cumsum(v)])
    lo = np.searchsorted(s, s - radius, side="left")
    hi = np.searchsorted(s, s + radius, side="right")
    return (c[hi] - c[lo]) / (hi - lo)


def rights(C):
    """the unit vector to the right of a line's way at each point, (k, 2): east of a line going north"""
    C = np.asarray(C, dtype=np.float64)
    t = np.zeros_like(C)
    if len(C) < 2:
        t[:, 1] = 1.0
    else:
        d = C[1:] - C[:-1]
        L = np.hypot(*d.T)[:, None]
        d = d / np.maximum(L, 1e-9)
        t[:-1] += d
        t[1:] += d
        L = np.hypot(*t.T)[:, None]
        bad = L[:, 0] < 1e-6
        t = t / np.maximum(L, 1e-9)
        if bad.any():
            t[bad] = np.concatenate([d, d[-1:]])[bad]
    return np.stack([t[:, 1], -t[:, 0]], axis=1)


class Field:
    """the ground: heights on a grid of res metres, rows going north from n0, columns east from e0"""

    def __init__(self, z, e0, n0, res):
        self.z, self.e0, self.n0, self.res = z, float(e0), float(n0), float(res)

    def sample(self, e, n):
        z = self.z
        c = np.clip((np.asarray(e, dtype=np.float64) - self.e0) / self.res, 0, z.shape[1] - 1.000001)
        r = np.clip((np.asarray(n, dtype=np.float64) - self.n0) / self.res, 0, z.shape[0] - 1.000001)
        c0 = np.floor(c).astype(int)
        r0 = np.floor(r).astype(int)
        fc, fr = c - c0, r - r0
        return (z[r0, c0] * (1 - fc) + z[r0, c0 + 1] * fc) * (1 - fr) + (z[r0 + 1, c0] * (1 - fc) + z[r0 + 1, c0 + 1] * fc) * fr


def quads_oriented(A, B, C, D, hint):
    """quads (m, 3) each corner, in the engine's coordinates, turned so the two triangles 0 1 2 and 0 2 3 face along hint (m, 3) or (3,).
    Gives the four corners in the right order and each quad's normal"""
    n = np.cross(C - A, D - B)
    L = np.linalg.norm(n, axis=1)
    n = n / np.maximum(L, 1e-12)[:, None]
    hint = np.broadcast_to(np.asarray(hint, dtype=np.float64), n.shape)
    flip = (n * hint).sum(axis=1) < 0
    n[flip] = -n[flip]
    B2 = np.where(flip[:, None], D, B)
    D2 = np.where(flip[:, None], B, D)
    return A, B2, C, D2, n, L > 1e-10


class Cells:
    """what's made, sorted into cells as it's made. A piece goes to the cell its middle is in, whole, so nothing is cut at a cell's edge but the
    ground, which is made cell by cell. Pieces outside the cooked box are dropped"""

    def __init__(self, e0, n0, size, ni, nj, U, floor):
        self.e0, self.n0, self.size, self.ni, self.nj, self.U, self.floor = e0, n0, size, ni, nj, U, floor
        self.parts = {}   # (i, j) -> {kind: [(pos, nrm, idx)]}

    def xyz(self, e, n, h):
        return np.stack([e * self.U, (h - self.floor) * self.U, -n * self.U], axis=-1)

    def cell_of(self, e, n):
        i = np.floor((e - self.e0) / self.size).astype(int)
        j = np.floor((n - self.n0) / self.size).astype(int)
        ok = (i >= 0) & (i < self.ni) & (j >= 0) & (j < self.nj)
        return i, j, ok

    def add(self, i, j, kind, pos, nrm, idx):
        self.parts.setdefault((int(i), int(j)), {}).setdefault(kind, []).append((pos, nrm, idx))

    def add_quads(self, kind, A, B, C, D, hint, me, mn):
        """quads in engine coordinates, with their middles in map metres (me, mn) to say whose cell they're in"""
        if not len(A):
            return 0
        A, B, C, D, n, good = quads_oriented(A, B, C, D, hint)
        i, j, ok = self.cell_of(me, mn)
        ok &= good
        if not ok.any():
            return 0
        A, B, C, D, n, i, j = A[ok], B[ok], C[ok], D[ok], n[ok], i[ok], j[ok]
        key = i * self.nj + j
        order = np.argsort(key, kind="stable")
        key = key[order]
        cuts = np.nonzero(np.diff(key))[0] + 1
        for grp in np.split(order, cuts):
            m = len(grp)
            pos = np.stack([A[grp], B[grp], C[grp], D[grp]], axis=1).reshape(-1, 3)
            nrm = np.repeat(n[grp], 4, axis=0)
            base = (np.arange(m) * 4)[:, None]
            idx = (base + np.array([0, 1, 2, 0, 2, 3])[None, :]).reshape(-1)
            self.add(i[grp[0]], j[grp[0]], kind, pos, nrm, idx)
        return int(ok.sum())

    def add_tris(self, kind, T, ci, cj):
        """triangles (m, 3, 3) in engine coordinates, all to one cell, each turned to face up (y)"""
        if not len(T):
            return
        n = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])
        flip = n[:, 1] < 0
        T = T.copy()
        T[flip, 1], T[flip, 2] = T[flip, 2].copy(), T[flip, 1].copy()
        n[flip] = -n[flip]
        n = n / np.maximum(np.linalg.norm(n, axis=1), 1e-12)[:, None]
        self.add(ci, cj, kind, T.reshape(-1, 3), np.repeat(n, 3, axis=0), np.arange(len(T) * 3))


def band(field, C, a, b, lift, hc=None, across=3.0):
    """a strip along centreline C (k, 2) from a metres right of it to b metres right (a < b; negative is left). Its height is the ground's under each
    point plus lift, or, given hc (k,), the line's own height plus lift, flat across (a bridge's deck). Gives its corners as (k, m+1) grids of e, n, h"""
    R = rights(C)
    m = max(1, int(math.ceil((b - a) / across)))
    o = a + (b - a) * np.arange(m + 1) / m
    E = C[:, 0:1] + R[:, 0:1] * o[None, :]
    N = C[:, 1:2] + R[:, 1:2] * o[None, :]
    if hc is None:
        H = field.sample(E, N) + lift
    else:
        H = np.repeat(np.asarray(hc)[:, None], m + 1, axis=1) + lift
    return E, N, H


def grid_quads(cells, kind, E, N, H, hint):
    """the quads of a (k, m) grid of corners"""
    if E.shape[0] < 2 or E.shape[1] < 2:
        return 0
    X = cells.xyz(E, N, H)
    A, B, C, D = X[:-1, :-1], X[1:, :-1], X[1:, 1:], X[:-1, 1:]
    me = 0.25 * (E[:-1, :-1] + E[1:, :-1] + E[1:, 1:] + E[:-1, 1:])
    mn = 0.25 * (N[:-1, :-1] + N[1:, :-1] + N[1:, 1:] + N[:-1, 1:])
    h = np.asarray(hint, dtype=np.float64)
    if h.ndim > 1:
        h = h.reshape(-1, 3)
    return cells.add_quads(kind, A.reshape(-1, 3), B.reshape(-1, 3), C.reshape(-1, 3), D.reshape(-1, 3), h, me.reshape(-1), mn.reshape(-1))


def wall_along(cells, kind, E, N, Htop, Hbot, out_e, out_n):
    """a wall standing on a line of points (k,): from Hbot up to Htop, facing (out_e, out_n) in map terms, per point or one for all"""
    if len(E) < 2:
        return 0
    T = cells.xyz(E, N, Htop)
    Bm = cells.xyz(E, N, Hbot)
    A, B, C, D = Bm[:-1], Bm[1:], T[1:], T[:-1]
    oe = np.broadcast_to(np.asarray(out_e, dtype=np.float64), E.shape)
    on = np.broadcast_to(np.asarray(out_n, dtype=np.float64), E.shape)
    hint = np.stack([0.5 * (oe[:-1] + oe[1:]), np.zeros(len(E) - 1), -0.5 * (on[:-1] + on[1:])], axis=1)
    me, mn = 0.5 * (E[:-1] + E[1:]), 0.5 * (N[:-1] + N[1:])
    return cells.add_quads(kind, A, B, C, D, hint, me, mn)


def box(cells, kind, e, n, half, h0, h1):
    """a small upright box, square in plan, half metres from its middle: a post"""
    E = np.array([e - half, e + half, e + half, e - half, e - half])
    N = np.array([n - half, n - half, n + half, n + half, n - half])
    k = 0
    for s in range(4):
        de, dn = E[s + 1] - E[s], N[s + 1] - N[s]
        out = (dn, -de)   # this ring goes counter-clockwise, so outward is to the right
        k += wall_along(cells, kind, E[s:s + 2], N[s:s + 2], np.full(2, h1), np.full(2, h0), out[0], out[1])
    T = cells.xyz(E[:4], N[:4], np.full(4, h1))
    k += cells.add_quads(kind, T[0:1], T[1:2], T[2:3], T[3:4], np.array([0.0, 1.0, 0.0]), np.array([e]), np.array([n]))
    return k


# ---------------------------------------------------------------- polygons
def ring_clean(P):
    """a closed ring (k, 2) without its repeated last point, repeated neighbours or straight-through points, counter-clockwise"""
    P = np.asarray(P, dtype=np.float64)
    if len(P) > 1 and np.allclose(P[0], P[-1]):
        P = P[:-1]
    keep = [P[0]] if len(P) else []
    for p in P[1:]:
        if np.hypot(*(p - keep[-1])) > 0.05:
            keep.append(p)
    if len(keep) > 1 and np.hypot(*(keep[0] - keep[-1])) <= 0.05:
        keep.pop()
    P = np.array(keep)
    changed = True
    while changed and len(P) > 3:
        changed = False
        q = np.roll(P, -1, axis=0)
        r = np.roll(P, 1, axis=0)
        cr = (P[:, 0] - r[:, 0]) * (q[:, 1] - r[:, 1]) - (P[:, 1] - r[:, 1]) * (q[:, 0] - r[:, 0])
        straight = np.abs(cr) < 1e-6
        if straight.any():
            P = P[~straight]
            changed = True
    if len(P) >= 3 and area(P) < 0:
        P = P[::-1].copy()
    return P


def area(P):
    x, y = P[:, 0], P[:, 1]
    return 0.5 * float(np.sum(x * np.roll(y, -1) - np.roll(x, -1) * y))


def earclip(P):
    """a counter-clockwise ring cut into triangles, as index triples. Ear by ear; if it can't find one (a ring that crosses itself), what's left is
    a fan, and it says so by giving back False as well"""
    n = len(P)
    idx = list(range(n))
    tris = []
    clean = True
    guard = 0
    while len(idx) > 3 and guard < 4 * n * n:
        guard += 1
        found = False
        m = len(idx)
        for k in range(m):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % m]
            a, b, c = P[i0], P[i1], P[i2]
            cr = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
            if cr <= 1e-9:
                continue
            ok = True
            for j in idx:
                if j in (i0, i1, i2):
                    continue
                p = P[j]
                d1 = (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0])
                d2 = (c[0] - b[0]) * (p[1] - b[1]) - (c[1] - b[1]) * (p[0] - b[0])
                d3 = (a[0] - c[0]) * (p[1] - c[1]) - (a[1] - c[1]) * (p[0] - c[0])
                if d1 >= 0 and d2 >= 0 and d3 >= 0:
                    ok = False
                    break
            if ok:
                tris.append((i0, i1, i2))
                del idx[k]
                found = True
                break
        if not found:
            clean = False
            break
    if len(idx) >= 3:
        for k in range(1, len(idx) - 1):
            tris.append((idx[0], idx[k], idx[k + 1]))
    return tris, clean


def inside(px, py, P):
    """are the points inside ring P? (ray crossing)"""
    px = np.asarray(px, dtype=np.float64)
    py = np.asarray(py, dtype=np.float64)
    res = np.zeros(px.shape, dtype=bool)
    x0, y0 = P[:, 0], P[:, 1]
    x1, y1 = np.roll(x0, -1), np.roll(y0, -1)
    for a, b, c, d in zip(x0, y0, x1, y1):
        cond = (b > py) != (d > py)
        xs = a + (py - b) * (c - a) / np.where(d - b == 0, 1e-12, d - b)
        res ^= cond & (px < xs)
    return res


def seg_dist(px, py, ax, ay, bx, by):
    """distance from points (m,) to segments (s,), (m, s), and how far along each segment the nearest point is"""
    dx, dy = bx - ax, by - ay
    L2 = np.maximum(dx * dx + dy * dy, 1e-12)
    t = np.clip(((px[:, None] - ax[None, :]) * dx[None, :] + (py[:, None] - ay[None, :]) * dy[None, :]) / L2[None, :], 0, 1)
    qx = ax[None, :] + t * dx[None, :]
    qy = ay[None, :] + t * dy[None, :]
    return np.hypot(px[:, None] - qx, py[:, None] - qy), t
