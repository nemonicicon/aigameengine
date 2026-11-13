#!/usr/bin/env python3
"""lowpoly.py -- a body's low-poly copy for a small machine: its meshes thinned to a count of triangles, and its hair cards baked into one solid shell.

    python skate_src/author/lowpoly.py PACK OUT.rpack [--tris 9000] [--hair hair,braid] [--voxel 0.0075] [--shell 2600] [--keep-hair]

(11-07) On the Pi, a game's rider needed her model decimated to low poly and her hair baked as one merged mesh, to cut triangles and
what they cost. Her lighter copy (lod.py --body, ~lod2) was 42,794 triangles, 25,098 of them hair cards, drawn
see-through and stacked: from behind her they wrote about 214,000 blended pixels a frame on the Pi (the own rasterizer's count), and a light body in her
place ran the same street at 29 frames a second against her 24.

What it makes, from the body's own pack (which is never changed):
  - every mesh that isn't hair, thinned as lod.py thins (cells, a vertex of the mesh's own for each, seams and hard edges kept), the cell searched for until
    all of them together are about --tris triangles;
  - the hair meshes (names with any of --hair's words in them) replaced by one SHELL: points laid over every card where its texture shows hair (alpha at
    least a half), dropped into a grid of --voxel of the body's height, the grid closed (grown, filled inside, shrunk back) so the cards become one volume,
    its surface found (surface nets), smoothed, thinned to about --shell triangles, each corner taking the bones of the nearest hair it came from, and
    drawn solid in one colour, the hair texture's own average where it shows. Solid, it writes each pixel once and early, where the cards wrote dozens.
The copy is a body copy as lod.py's are: the same nodes, so the engine draws it on the body's pose; no face shapes, no chains of its own.
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
import lod
import material
from rpackwrite import write_pack


def say(*a):
    print(*a)
    sys.stdout.flush()


# ------------------------------------------------------------------ the body
def thin_all(P, meshes, want):
    """the cell over all these meshes at which their thinned triangles come to about `want`"""
    size = float(np.linalg.norm(P.hi - P.lo))
    a, b, best = size * 1e-4, size * 0.5, None
    for _ in range(22):
        c = (a * b) ** 0.5
        got = []
        for q in meshes:
            A = lod.thin_mesh(q["A"], c, P.lo)
            if A is not None:
                got.append((q, A))
        n = sum(len(A[5]) for _q, A in got)
        if best is None or abs(n - want) < abs(best[0] - want):
            best = (n, c, got)
        if abs(n - want) <= max(2, want * 0.03):
            break
        if n > want:
            a = c
        else:
            b = c
    return best


# ------------------------------------------------------------------ the hair's shell
def texture(P, q):
    import io
    from PIL import Image
    if q["tex"] < 0:
        return None
    return np.asarray(Image.open(io.BytesIO(P.tex[q["tex"]])).convert("RGBA"), dtype=np.float32) / 255.0


def hair_points(P, hair, step):
    """points over every card where its texture shows hair: their places, colours, and the vertex of the card nearest each (whose bones it takes)"""
    R = np.random.default_rng(902)
    allp, allc, alln, allw = [], [], [], []
    for q in hair:
        pos, _nrm, uv, nodes, w, tris = q["A"]
        if not len(tris):
            continue
        im = texture(P, q)
        a, b, c = pos[tris[:, 0]], pos[tris[:, 1]], pos[tris[:, 2]]
        area = 0.5 * np.linalg.norm(np.cross(b - a, c - a), axis=1)
        cnt = np.ceil(area / (step * step) * 6.0).astype(np.int64) + 1
        if cnt.sum() > 6_000_000:
            cnt = np.maximum(1, (cnt * (6_000_000.0 / cnt.sum())).astype(np.int64))
        ti = np.repeat(np.arange(len(tris)), cnt)
        r1, r2 = R.random(len(ti)), R.random(len(ti))
        s = np.sqrt(r1)
        bc = np.stack([1.0 - s, s * (1.0 - r2), s * r2], 1)
        p3 = a[ti] * bc[:, 0:1] + b[ti] * bc[:, 1:2] + c[ti] * bc[:, 2:3]
        corner = tris[ti, bc.argmax(1)]
        if im is not None:
            UV = uv[tris[ti, 0]] * bc[:, 0:1] + uv[tris[ti, 1]] * bc[:, 1:2] + uv[tris[ti, 2]] * bc[:, 2:3]
            h, wd = im.shape[:2]
            v = UV[:, 1] if not q["fl"] & 2 else 1.0 - UV[:, 1]
            col = im[np.floor((v % 1.0) * h).astype(np.int64) % h, np.floor((UV[:, 0] % 1.0) * wd).astype(np.int64) % wd]
            keep = col[:, 3] >= 0.5
        else:
            col = np.tile([0.2, 0.15, 0.12, 1.0], (len(ti), 1))
            keep = np.ones(len(ti), dtype=bool)
        say("  %s: %d cards' triangles, %d of %d points show hair" % (q["name"], len(tris), int(keep.sum()), len(ti)))
        allp.append(p3[keep])
        allc.append(col[keep, :3])
        alln.append(nodes[corner[keep]])
        allw.append(w[corner[keep]])
    return np.concatenate(allp), np.concatenate(allc), np.concatenate(alln), np.concatenate(allw)


def shift(g, d, axis):
    """g moved d cells along an axis, with what comes in empty"""
    out = np.zeros_like(g)
    if d > 0:
        sl_o, sl_i = [slice(None)] * 3, [slice(None)] * 3
        sl_o[axis], sl_i[axis] = slice(d, None), slice(None, -d)
        out[tuple(sl_o)] = g[tuple(sl_i)]
    else:
        sl_o, sl_i = [slice(None)] * 3, [slice(None)] * 3
        sl_o[axis], sl_i[axis] = slice(None, d), slice(-d, None)
        out[tuple(sl_o)] = g[tuple(sl_i)]
    return out


def grow(g, n):
    for _ in range(n):
        h = g.copy()
        for ax in range(3):
            h |= shift(g, 1, ax) | shift(g, -1, ax)
        g = h
    return g


def shrink(g, n):
    return ~grow(~g, n)


def fill_inside(g):
    """what can't be reached from the grid's edge through empty cells is inside"""
    out = np.zeros_like(g)
    out[0, :, :] = out[-1, :, :] = out[:, 0, :] = out[:, -1, :] = out[:, :, 0] = out[:, :, -1] = True
    out &= ~g
    while True:
        h = out.copy()
        for ax in range(3):
            h |= shift(out, 1, ax) | shift(out, -1, ax)
        h &= ~g
        if (h == out).all():
            break
        out = h
    return g | ~out


def blur(f):
    for ax in range(3):
        f = (shift(f, 1, ax) + f * 2.0 + shift(f, -1, ax)) * 0.25
    return f


def surface_nets(F, iso=0.5):
    """the surface of a field at a level: a vertex in every cell the surface crosses, at the mean of where it crosses the cell's edges, and a quad across
    every grid edge it crosses. Returns grid-space vertices and triangles wound with the inside behind them"""
    ins = F >= iso
    nx, ny, nz = F.shape
    C = [(dx, dy, dz) for dz in (0, 1) for dy in (0, 1) for dx in (0, 1)]
    corners = [F[dx:nx - 1 + dx, dy:ny - 1 + dy, dz:nz - 1 + dz] for dx, dy, dz in C]
    cin = [c >= iso for c in corners]
    nin = sum(c.astype(np.int64) for c in cin)
    active = (nin > 0) & (nin < 8)
    E = [(a, b) for a in range(8) for b in range(a + 1, 8) if sum(abs(C[a][k] - C[b][k]) for k in range(3)) == 1]
    acc = np.zeros(F[:-1, :-1, :-1].shape + (3,))
    cnt = np.zeros(F[:-1, :-1, :-1].shape)
    for a, b in E:
        fa, fb = corners[a], corners[b]
        x = cin[a] != cin[b]
        t = np.where(x, (iso - fa) / np.where(np.abs(fb - fa) > 1e-12, fb - fa, 1.0), 0.0)
        for k in range(3):
            acc[..., k] += np.where(x, C[a][k] + t * (C[b][k] - C[a][k]), 0.0)
        cnt += x
    idx = np.full(active.shape, -1, dtype=np.int64)
    cells = np.argwhere(active)
    idx[active] = np.arange(len(cells))
    verts = cells + acc[active] / np.maximum(cnt[active], 1.0)[:, None]
    quads = []
    for d in range(3):
        u, v = (d + 1) % 3, (d + 2) % 3
        lo = [slice(None)] * 3
        hi = [slice(None)] * 3
        lo[d], hi[d] = slice(0, -1), slice(1, None)
        cross = ins[tuple(lo)] != ins[tuple(hi)]
        p = np.argwhere(cross)
        p = p[(p[:, u] >= 1) & (p[:, v] >= 1) & (p[:, u] < (F.shape[u] - 1)) & (p[:, v] < (F.shape[v] - 1))]
        if not len(p):
            continue
        eu, ev = np.zeros(3, dtype=np.int64), np.zeros(3, dtype=np.int64)
        eu[u], ev[v] = 1, 1
        c0, c1, c2, c3 = p - eu - ev, p - ev, p, p - eu
        q = np.stack([idx[tuple(c.T)] for c in (c0, c1, c2, c3)], 1)
        first_in = ins[tuple(p.T)]
        q[first_in] = q[first_in][:, ::-1]
        quads.append(q[(q >= 0).all(1)])
    q = np.concatenate(quads)
    tris = np.concatenate([q[:, [0, 1, 2]], q[:, [0, 2, 3]]])
    return verts, tris


def vertex_normals(v, t):
    fn = np.cross(v[t[:, 1]] - v[t[:, 0]], v[t[:, 2]] - v[t[:, 0]])
    n = np.zeros_like(v)
    for k in range(3):
        np.add.at(n, t[:, k], fn)
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)


def smooth(v, t, rounds=4, k=0.5):
    e = np.concatenate([t[:, [0, 1]], t[:, [1, 2]], t[:, [2, 0]]])
    e = np.concatenate([e, e[:, ::-1]])
    deg = np.bincount(e[:, 0], minlength=len(v)).astype(np.float64)
    for _ in range(rounds):
        s = np.zeros_like(v)
        np.add.at(s, e[:, 0], v[e[:, 1]])
        v = v + k * (s / np.maximum(deg, 1.0)[:, None] - v)
    return v


def shell(P, hair, voxel_share, want, close=2, blurs=2):
    H = float(P.hi[1] - P.lo[1])
    vox = H * voxel_share
    say("the hair's shell: a grid of %.4f (the body's height is %.3f), about %d triangles wanted" % (vox, H, want))
    pts, col, pn, pw = hair_points(P, hair, vox * 0.5)
    pad = 4
    lo = pts.min(0) - vox * pad
    dims = np.ceil((pts.max(0) + vox * pad - lo) / vox).astype(np.int64) + 1
    g = np.zeros(tuple(dims), dtype=bool)
    ijk = np.floor((pts - lo) / vox).astype(np.int64)
    g[tuple(ijk.T)] = True
    say("  %d points in a grid of %d x %d x %d, %d cells hold hair" % (len(pts), dims[0], dims[1], dims[2], int(g.sum())))
    g = shrink(fill_inside(grow(g, close)), close)  # the cards become one volume: grown together, filled, and brought back to their own outline
    F = g.astype(np.float64)
    for _ in range(blurs):
        F = blur(F)
    v, t = surface_nets(F)
    v = lo + (v + 0.5) * vox
    v = smooth(v, t)
    say("  its surface: %d triangles before thinning" % len(t))
    n = vertex_normals(v, t)
    if (((v - v.mean(0)) * n).sum(1)).mean() < 0:   # wound so its normals face out
        t = t[:, ::-1]
        n = -n
    # thinned as a body's meshes are, to about `want`
    A0 = (v, n, np.zeros((len(v), 2)), np.zeros((len(v), 4), dtype=np.int64), np.tile([1.0, 0.0, 0.0, 0.0], (len(v), 1)), t)
    a, b, best = vox * 0.2, vox * 20.0, (len(t), 0.0, A0)
    if len(t) > want:
        for _ in range(22):
            c = (a * b) ** 0.5
            A = lod.thin_mesh(A0, c, lo)
            m = 0 if A is None else len(A[5])
            if A is not None and abs(m - want) < abs(best[0] - want):
                best = (m, c, A)
            if abs(m - want) <= max(2, want * 0.04):
                break
            if m > want:
                a = c
            else:
                b = c
    v, _n, _uv, _nodes, _w, t = best[2]
    n = vertex_normals(v, t)
    # each corner rides the bones of the nearest hair it was made from
    sub = np.random.default_rng(1).choice(len(pts), size=min(len(pts), 20000), replace=False)
    ps = pts[sub]
    near = np.zeros(len(v), dtype=np.int64)
    for s in range(0, len(v), 128):
        d = ((v[s:s + 128, None, :] - ps[None, :, :]) ** 2).sum(2)
        near[s:s + 128] = sub[d.argmin(1)]
    nodes, w = pn[near], pw[near]
    rgb = np.clip(col.mean(0), 0.0, 1.0)
    hexc = "".join("%02X" % int(round(c * 255)) for c in rgb)
    say("  the shell: %d triangles, %d corners, its colour #%s" % (len(t), len(v), hexc))
    return (v, n, np.zeros((len(v), 2)), nodes, w, t), hexc


# ------------------------------------------------------------------ the copy
def make(path, dst, tris=9000, hair_words=("hair", "braid"), voxel=0.0075, shell_tris=2600, keep_hair=False, close=2, blurs=2):
    assert os.path.abspath(dst) != os.path.abspath(path), "the copy can't be the body itself"
    assert os.path.basename(os.path.dirname(os.path.abspath(dst))).lower() != "packs", "nothing is written into packs/: a file there joins the crowd"
    P = lod.Prop(path, body=True)
    is_hair = lambda q: any(wd in q["name"].lower() for wd in hair_words) and "scalp" not in q["name"].lower()
    hair = [q for q in P.meshes if is_hair(q)]
    rest = [q for q in P.meshes if not is_hair(q)] + (hair if keep_hair else [])
    say("%s: %d triangles; hair %d in %s; the rest %d" % (os.path.normpath(path), P.tris, sum(len(q["A"][5]) for q in hair),
                                                         ", ".join(q["name"] for q in hair) or "nothing", sum(len(q["A"][5]) for q in rest)))
    n, cell, got = thin_all(P, rest, tris)
    say("the body: %d triangles (cells of %.4f)" % (n, cell))
    meshes, table, seen = [], {}, {}
    names = [q["name"] for q, _A in got]
    for q, A in got:
        meshes.append(P.out_mesh(q, A))
        seen[q["name"]] = seen.get(q["name"], 0) + 1
        if q["mat"]:
            table[("%s@%d" % (q["name"], seen[q["name"]])) if names.count(q["name"]) > 1 else q["name"]] = q["mat"]
    if hair and not keep_hair:
        A, hexc = shell(P, hair, voxel, shell_tris, close, blurs)
        q = dict(name="hair_shell#%s" % hexc, node=hair[0]["node"], tex=-1, ntex=-1, fl=0, blend=False, mat=None)
        meshes.append(P.out_mesh(q, A))
    write_pack(dst, P.nodes, meshes, [], P.tex, verbose=False, fresh=True)
    if table:
        data = material.write(open(dst, "rb").read(), table)
        open(dst, "wb").write(data)
    total = sum(len(m["idx"]) // 3 for m in meshes)
    say("wrote %s: %d triangles in %d meshes" % (dst, total, len(meshes)))


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)

    def opt(name, default, kind=float):
        return kind(argv[argv.index(name) + 1]) if name in argv else default
    make(argv[1], argv[2], tris=opt("--tris", 9000, int), hair_words=tuple(opt("--hair", "hair,braid", str).split(",")),
         voxel=opt("--voxel", 0.0075), shell_tris=opt("--shell", 2600, int), keep_hair="--keep-hair" in argv,
         close=opt("--close", 2, int), blurs=opt("--blur", 2, int))


if __name__ == "__main__":
    main(sys.argv)
