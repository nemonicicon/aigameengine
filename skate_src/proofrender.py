#!/usr/bin/env python3
"""proofrender.py -- a BRIGHT, orthographic, lit render of a scene's triangles, no engine (2026-10-05).

jacob, on the lab frames of the first T-posed trophies: "doesent look like a t pose. and its dark."
The anim lab's night lighting, perspective cameras and 640-pixel tiles are no way to show a pose. This
is the proof sheet: front and side, orthographic, Lambert-lit from the camera, the body's own texture
sampled per vertex, on a light ground. The skeleton can be drawn over it.

    proofrender.py <pack.rpack> <out.png> [--bones] [--marks marks.txt] [--size 900]
    render(nodes, meshes, textures, path, bones=True, size=900)

numpy rasteriser: per triangle a bounding box, barycentric inside test, a z-buffer; a 36k-triangle
body takes a few seconds.
"""
import sys, os, io, math
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, 'author'))
import rpackwrite as RW


def skinned(nodes, meshes):
    """(positions N x 3, normals N x 3 | None, per-mesh index arrays, per-vertex colours N x 3)"""
    W = RW.world_matrices(nodes)
    P, N, C, T = [], [], [], []
    for m in meshes:
        pal = [np.array(RW.m4_mul(W[b], m['ibm'][k * 16:(k + 1) * 16])).reshape(4, 4).T for k, b in enumerate(m['boneMap'])]
        bind = np.array(m.get('bind') or RW.m4_identity()).reshape(4, 4).T
        pos = np.array(m['pos']).reshape(-1, 3)
        nv = len(pos)
        ph = np.c_[pos, np.ones(nv)] @ bind.T
        out = np.zeros((nv, 3))
        nrm = np.array(m['nrm']).reshape(-1, 3) if m.get('nrm') else None
        on = np.zeros((nv, 3))
        J = np.array(m['joints'], dtype=int).reshape(nv, 4)
        Wt = np.array(m['weights']).reshape(nv, 4)
        for k in range(4):
            w = Wt[:, k:k + 1]
            if not np.any(w > 0):
                continue
            M = np.stack([pal[j] for j in J[:, k]])          # nv x 4 x 4
            out += w * np.einsum('nij,nj->ni', M[:, :3, :], ph)
            if nrm is not None:
                on += w * np.einsum('nij,nj->ni', M[:, :3, :3], nrm)
        base = len(P) and sum(len(p) for p in P)
        P.append(out)
        N.append(on if nrm is not None else np.zeros((nv, 3)))
        T.append(np.array(m['idx'], dtype=int).reshape(-1, 3) + base)
        C.append(np.full((nv, 3), 0.72))
    return np.vstack(P), np.vstack(N), T, np.vstack(C)


def vertex_colours(meshes, textures):
    """The texture sampled at each vertex's uv (nearest), else a light grey; blended meshes lighter."""
    cols = []
    cache = {}
    for m in meshes:
        nv = len(m['pos']) // 3
        c = np.full((nv, 3), 0.72)
        ti = m.get('tex', -1)
        if ti is not None and ti >= 0 and ti < len(textures) and m.get('uv'):
            try:
                from PIL import Image
                if ti not in cache:
                    im = Image.open(io.BytesIO(bytes(textures[ti]))).convert('RGB')
                    cache[ti] = np.asarray(im, dtype=np.float32) / 255.0
                img = cache[ti]
                h, w = img.shape[:2]
                uv = np.array(m['uv']).reshape(-1, 2)
                u = np.mod(uv[:, 0], 1.0)
                v = np.mod(uv[:, 1], 1.0)
                xi = np.clip((u * w).astype(int), 0, w - 1)
                yi = np.clip((v * h).astype(int), 0, h - 1)
                c = img[yi, xi]
                c = 0.25 + 0.75 * c          # lift the darks: a proof sheet is read, not graded
            except Exception:
                pass
        cols.append(c)
    return np.vstack(cols)


def rasterise(P2, Z, tris, shade, size, bg):
    """P2: screen xy per vertex, Z: depth (bigger = nearer), shade: per-vertex rgb. -> image array"""
    img = np.empty((size, size, 3), dtype=np.float32)
    img[:] = bg
    zb = np.full((size, size), -1e30, dtype=np.float32)
    for tri in tris:
        for t in tri:
            a, b, c = t
            xs = P2[[a, b, c], 0]
            ys = P2[[a, b, c], 1]
            x0, x1 = int(max(0, np.floor(xs.min()))), int(min(size - 1, np.ceil(xs.max())))
            y0, y1 = int(max(0, np.floor(ys.min()))), int(min(size - 1, np.ceil(ys.max())))
            if x1 < x0 or y1 < y0:
                continue
            det = (xs[1] - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (ys[1] - ys[0])
            if abs(det) < 1e-9:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            l1 = ((xs[1] - gx) * (ys[2] - gy) - (xs[2] - gx) * (ys[1] - gy)) / det
            l2 = ((xs[2] - gx) * (ys[0] - gy) - (xs[0] - gx) * (ys[2] - gy)) / det
            l3 = 1.0 - l1 - l2
            inside = (l1 >= -1e-6) & (l2 >= -1e-6) & (l3 >= -1e-6)
            if not inside.any():
                continue
            z = l1 * Z[a] + l2 * Z[b] + l3 * Z[c]
            sub = zb[y0:y1 + 1, x0:x1 + 1]
            win = inside & (z > sub)
            if not win.any():
                continue
            col = (l1[..., None] * shade[a] + l2[..., None] * shade[b] + l3[..., None] * shade[c])
            sub[win] = z[win]
            img[y0:y1 + 1, x0:x1 + 1][win] = col[win]
    return img


def render(nodes, meshes, textures, path, bones=True, size=900, RM=None, marks=None):
    from PIL import Image, ImageDraw
    P, N, T, _ = skinned(nodes, meshes)
    C = vertex_colours(meshes, textures)
    lo, hi = P.min(0), P.max(0)
    span = (hi - lo).max() * 1.1
    cen = (lo + hi) * 0.5
    views = [('front', 0, 1, 2, 1.0, 1.0), ('back', 0, 1, 2, -1.0, -1.0), ('side', 2, 1, 0, -1.0, 1.0)]     # (name, across axis, up axis, depth axis, across sign, depth sign)
    W = RW.world_matrices(nodes)
    sheet = Image.new('RGB', (size * len(views) + 20 * (len(views) - 1), size + 40), (236, 236, 240))
    dr = ImageDraw.Draw(sheet)
    for vi, (vname, ax, ay, az, sx, sz) in enumerate(views):
        X = (P[:, ax] - cen[ax]) / span * sx * size + size * 0.5
        Y = size * 0.5 - (P[:, ay] - cen[ay]) / span * size
        Z = (P[:, az] - cen[az]) * sz
        # lighting: a head-on key with a little from the upper left, two-sided normals
        n = N / (np.linalg.norm(N, axis=1, keepdims=True) + 1e-9)
        L = np.zeros(3); L[az] = sz; L[ay] += 0.35; L[ax] -= 0.25 * sx
        L /= np.linalg.norm(L)
        lam = np.abs(n @ L)
        shade = C * (0.35 + 0.65 * lam[:, None])
        img = rasterise(np.c_[X, Y], Z, T, shade, size, np.array([0.925, 0.925, 0.94]))
        tile = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))
        d2 = ImageDraw.Draw(tile)
        if bones:
            def proj(p):
                return ((p[ax] - cen[ax]) / span * sx * size + size * 0.5, size * 0.5 - (p[ay] - cen[ay]) / span * size)
            for i, nd in enumerate(nodes):
                if nd['parent'] >= 0 and nd.get('kind', 1) == 1:
                    d2.line([proj(W[i][12:15]), proj(W[nd['parent']][12:15])], fill=(230, 90, 30), width=2)
            if RM:
                for key in ('upperarm_l', 'lowerarm_l', 'upperarm_r', 'lowerarm_r', 'thigh_l', 'calf_l', 'thigh_r', 'calf_r', 'pelvis', 'head'):
                    k = RM.get(key, -1)
                    if k >= 0:
                        u, v = proj(W[k][12:15])
                        d2.ellipse([u - 4, v - 4, u + 4, v + 4], outline=(200, 30, 30), width=2)
        if marks:
            from statue_marks import CHAIN
            def proj2(p):
                return ((p[ax] - cen[ax]) / span * sx * size + size * 0.5, size * 0.5 - (p[ay] - cen[ay]) / span * size)
            for (nm, par) in CHAIN:
                if nm in marks and par in marks:
                    d2.line([proj2(marks[par]), proj2(marks[nm])], fill=(20, 120, 230), width=3)
            for nm, p in marks.items():
                u, v = proj2(p)
                d2.ellipse([u - 4, v - 4, u + 4, v + 4], fill=(230, 40, 40))
                d2.text((u + 6, v - 7), nm, fill=(120, 20, 20))
        # a ground line at the lowest vertex and a 0.5-unit scale bar
        gy = size * 0.5 - (lo[1] - cen[ay]) / span * size
        d2.line([(0, gy), (size, gy)], fill=(150, 150, 160), width=1)
        bar = 0.5 / span * size
        d2.line([(20, size - 20), (20 + bar, size - 20)], fill=(60, 60, 70), width=3)
        d2.text((22, size - 36), '0.5 units', fill=(60, 60, 70))
        sheet.paste(tile, (vi * (size + 20), 30))
        dr.text((vi * (size + 20) + 8, 8), '%s view, orthographic, lit  --  %s' % (vname, {'front': 'seen from +z (the face should be here), x across, y up', 'back': 'seen from -z', 'side': '+z to the left, y up'}[vname]), fill=(40, 40, 50))
    sheet.save(path)
    return path


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    import tpose
    nodes, meshes, clips, textures = tpose._scene_from_pack(argv[1])
    size = int(argv[argv.index('--size') + 1]) if '--size' in argv else 900
    names = [nd['name'] for nd in nodes]
    RM = tpose.canon_map(nodes, names, report=None)
    marks = None
    if '--marks' in argv:
        from statue_marks import read_marks
        marks = read_marks(argv[argv.index('--marks') + 1])
    render(nodes, meshes, textures, argv[2], bones='--bones' in argv, size=size, RM=RM if '--bones' in argv else None, marks=marks)
    print('  ' + argv[2])
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
