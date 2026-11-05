#!/usr/bin/env python3
"""lod.py -- a prop's lighter copies, and a picture of it for far off (queue step 24; engine/set.inc setLod, engine/shadow.inc drawSet).

    python skate_src/author/lod.py reva_native/props/fol_tree.rpack [--lod1 0.45] [--lod2 0.14] [--far 128] [--no-far] [--body] [--out DIR]

Writes, beside the prop: <stem>~lod1.rpack and <stem>~lod2.rpack, the same prop in that share of its triangles, and <stem>~far.rpack, two crossed cards with a
picture of it from the front and from the side, cut out by the picture's alpha. A set draws whichever suits how big the prop is in the picture, with nothing
said in the set: the files being there is the asking. The prop itself is never changed, and stays the top detail.

How a copy is thinned: space is cut into cells, every vertex in a cell becomes one point of the prop's own (the one nearest the cell's middle), and a triangle
with two corners in one cell is gone. Vertices of a cell that are a seam of the texture apart, or face different ways across a hard edge, stay separate
vertices at the one point, so a texture doesn't smear and a box keeps its corners. The cell's size is searched for until the share is met.
A part too thin for the cell (a pole, a rail) is given a cell it survives in.

The far picture is the prop's own colours as seen from each side with the light from above worked in softly, so a canopy's lumps read. The engine still
lights the cards, by normals that lean up and outward, so both cards take the sun alike.

What it keeps: every node (a prop's markers: Open, NoCast, Glow), each mesh's texture and what it's made of, its bones and their shares.

A BODY (--body): a set actor's pack gets ~lod1 and ~lod2 the same way and no far picture, since a body is posed and a picture isn't. The engine draws a copy on
the actor's own pose, so the copy needs the body's bones and nothing else of it: its clips, its face's shapes and its marks stay in the body's own pack.
Each mesh keeps its name, so the copy wears what the body wears. Without --body a mesh with face shapes is refused, since a shape names vertices by number.

The copies go beside the pack, except that nothing is ever written into a folder called packs: a file there is in the crowd's roster. A body in packs/
has its copies written to the props/ folder next to it, which is where the engine looks for every copy.
"""
import io
import os
import re
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
import material
from packbody import PackBody
from rpackwrite import to_png, write_pack


def thin(pos, nrm, uv, cell, lo):
    """vertex clustering. Returns (which new vertex each old one becomes, the old vertex each new one takes its place from, the old vertex each new one takes
    its texture place and bones from, a normal for each new one)"""
    c = np.floor((pos - lo) / cell).astype(np.int64)
    _u, sp = np.unique(c, axis=0, return_inverse=True)
    sp = sp.reshape(-1)
    n_sp = int(sp.max()) + 1
    cnt = np.bincount(sp, minlength=n_sp).astype(np.float64)
    mean = np.stack([np.bincount(sp, weights=pos[:, k], minlength=n_sp) for k in range(3)], 1) / cnt[:, None]
    d = ((pos - mean[sp]) ** 2).sum(1)
    o = np.lexsort((d, sp))
    first = np.ones(len(o), dtype=bool)
    first[1:] = sp[o][1:] != sp[o][:-1]
    rep_sp = o[first]                                   # per cell, the vertex nearest its middle: the cell's point
    key = np.concatenate([sp[:, None], np.floor(uv * 16.0).astype(np.int64), np.rint(nrm * 1.2).astype(np.int64)], 1)
    _u, sub = np.unique(key, axis=0, return_inverse=True)
    sub = sub.reshape(-1)
    n_sub = int(sub.max()) + 1
    d2 = ((pos - pos[rep_sp][sp]) ** 2).sum(1)
    o = np.lexsort((d2, sub))
    first = np.ones(len(o), dtype=bool)
    first[1:] = sub[o][1:] != sub[o][:-1]
    rep_sub = o[first]                                  # per vertex kept, the old vertex nearest the cell's point on its own side of a seam
    acc = np.stack([np.bincount(sub, weights=nrm[:, k], minlength=n_sub) for k in range(3)], 1)
    ln = np.linalg.norm(acc, axis=1, keepdims=True)
    n2 = np.where(ln > 1e-9, acc / np.maximum(ln, 1e-12), nrm[rep_sub])
    return sub, rep_sp[sp[rep_sub]], rep_sub, n2


def thin_mesh(A, cell, lo):
    pos, nrm, uv, nodes, w, tris = A
    if not len(tris):
        return None
    ext = np.sort(pos.max(0) - pos.min(0))
    cell = min(cell, max(ext[1] * 0.6, 1e-6))           # a part thin two ways (a pole) keeps a cell it survives in
    sub, at, frm, n2 = thin(pos, nrm, uv, cell, lo)
    t = sub[tris]
    t = t[(t[:, 0] != t[:, 1]) & (t[:, 1] != t[:, 2]) & (t[:, 0] != t[:, 2])]
    if not len(t):
        return None
    p3 = pos[at]
    flat = np.linalg.norm(np.cross(p3[t[:, 1]] - p3[t[:, 0]], p3[t[:, 2]] - p3[t[:, 0]]), axis=1) > 1e-12      # corners in three cells, at one point: a seam's sliver
    t = t[flat]
    if not len(t):
        return None
    k = t.argmin(1)
    t = np.stack([t[np.arange(len(t)), (k + i) % 3] for i in range(3)], 1)       # said the same way round, smallest corner first, so a triangle said twice is found
    t = np.unique(t, axis=0)
    used = np.unique(t)
    back = np.full(len(at), -1, dtype=np.int64)
    back[used] = np.arange(len(used))
    return pos[at[used]], n2[used], uv[frm[used]], nodes[frm[used]], w[frm[used]], back[t]


class Prop:
    """a prop out of its pack: nodes, meshes as arrays at rest, textures as they are in the file, and what each mesh is made of"""

    def __init__(self, path, body=False):
        self.path = path
        self.B = B = PackBody(path)
        P = B.P
        self.nodes = [dict(name=nd["name"], parent=nd["parent"], kind=nd["kind"], t=list(nd["t"]), q=list(nd["q"]), s=list(nd["s"])) for nd in B.nodes]
        self.tex = []
        for t in range(P.nTex):
            o, n = struct.unpack_from("<II", P.buf, P.offTexTable + t * 16 + 8)
            self.tex.append(bytes(P.buf[o:o + n]))
        flags = {}
        for i in range(P.nMats):
            mesh, = struct.unpack_from("<I", P.buf, P.offMats + i * 36)
            fl, = struct.unpack_from("<I", P.buf, P.offMats + i * 36 + 16)
            flags.setdefault(mesh, fl)
        said = material.read(open(path, "rb").read())
        self.meshes = []
        for m in B.meshes:
            assert body or not B.has_morphs(m), "%s: %s has face shapes, which name its vertices by number: say --body if it's an actor's, and its copies will have no face that moves" % (path, m["name"])
            rec = said.get("%s@%d" % (m["name"], B.nth(m)), said.get(m["name"]))
            self.meshes.append(dict(name=m["name"], node=m["rec"][1], A=B.arrays(m), tex=B.tex_of.get(m["mi"], -1), ntex=B.ntex_of.get(m["mi"], -1),
                                    fl=flags.get(m["mi"], 0), blend=bool(m["rec"][5] & 8), mat=rec))
        allp = np.concatenate([q["A"][0] for q in self.meshes if len(q["A"][0])])
        self.lo, self.hi = allp.min(0), allp.max(0)
        self.tris = sum(len(q["A"][5]) for q in self.meshes)

    def out_mesh(self, q, A):
        pos, nrm, uv, nodes, w, tris = A
        used = np.unique(nodes[w > 0])
        assert len(used) <= 256, "%s rides %d bones, and a mesh takes 256" % (q["name"], len(used))
        slot = np.zeros(len(self.nodes), dtype=np.int64)
        slot[used] = np.arange(len(used))
        ibm = []
        for b in used:
            ibm += [float(v) for v in np.linalg.inv(np.array(self.B.W[b]).reshape(4, 4).T).T.reshape(-1)]
        return dict(name=q["name"], node=q["node"], pos=[float(v) for v in pos.reshape(-1)], nrm=[float(v) for v in nrm.reshape(-1)],
                    uv=[float(v) for v in uv.reshape(-1)], idx=[int(v) for v in tris.reshape(-1)],
                    joints=np.where(w > 0, slot[nodes], 0).tolist(), weights=w.tolist(), boneMap=[int(b) for b in used], ibm=ibm,
                    tex=q["tex"], ntex=q["ntex"], nearest=bool(q["fl"] & 1), flipv=bool(q["fl"] & 2), cutout=bool(q["fl"] & 4), blend=q["blend"])

    def thinned(self, cell):
        out = []
        for q in self.meshes:
            A = thin_mesh(q["A"], cell, self.lo)
            if A is not None:
                out.append((q, A))
        return out

    def find(self, share):
        """the copy with about this share of the prop's triangles: the cell's size is searched for"""
        want = max(4, int(self.tris * share))
        size = float(np.linalg.norm(self.hi - self.lo))
        a, b, best = size * 1e-4, size * 0.5, None
        for _ in range(18):
            c = (a * b) ** 0.5
            got = self.thinned(c)
            n = sum(len(A[5]) for _q, A in got)
            if best is None or abs(n - want) < abs(best[0] - want):
                best = (n, c, got)
            if abs(n - want) <= max(2, want * 0.04):
                break
            if n > want:
                a = c
            else:
                b = c
        return best

    def write(self, dst, got):
        meshes, table, seen = [], {}, {}
        names = [q["name"] for q, _A in got]
        for q, A in got:
            meshes.append(self.out_mesh(q, A))
            seen[q["name"]] = seen.get(q["name"], 0) + 1
            if q["mat"]:
                table[("%s@%d" % (q["name"], seen[q["name"]])) if names.count(q["name"]) > 1 else q["name"]] = q["mat"]
        write_pack(dst, self.nodes, meshes, [], self.tex, verbose=False, fresh=True)
        if table:
            data = material.write(open(dst, "rb").read(), table)       # read before the file's opened to write: opening it empties it
            open(dst, "wb").write(data)

    # ------------------------------------------------------------ the far picture
    def picture(self, size, axis, ss=3):
        """the prop seen along an axis from far off (0: from +x, looking back along x; 2: from +z), size x size, RGBA. Splatted: points on every triangle,
        the nearest kept at each place, then the picture brought down from `ss` times its size"""
        from PIL import Image
        n = size * ss
        u_ax = 2 if axis == 0 else 0                      # across the picture: z for the side, x for the front
        lo, hi = self.lo, self.hi
        span_u, span_y = max(hi[u_ax] - lo[u_ax], 1e-6), max(hi[1] - lo[1], 1e-6)
        R = np.random.default_rng(902)
        best_d = np.full(n * n, -np.inf)
        img = np.zeros((n * n, 4))
        for q in self.meshes:
            pos, nrm, uv, _nodes, _w, tris = q["A"]
            if not len(tris):
                continue
            a, b, c = pos[tris[:, 0]], pos[tris[:, 1]], pos[tris[:, 2]]
            pu = np.stack([(a[:, u_ax] - lo[u_ax]) / span_u, (b[:, u_ax] - lo[u_ax]) / span_u, (c[:, u_ax] - lo[u_ax]) / span_u], 1) * n
            py = np.stack([(a[:, 1] - lo[1]) / span_y, (b[:, 1] - lo[1]) / span_y, (c[:, 1] - lo[1]) / span_y], 1) * n
            area = 0.5 * np.abs((pu[:, 1] - pu[:, 0]) * (py[:, 2] - py[:, 0]) - (pu[:, 2] - pu[:, 0]) * (py[:, 1] - py[:, 0]))
            cnt = np.ceil(area * 4.0).astype(np.int64) + 1
            if cnt.sum() > 3_000_000:
                cnt = np.maximum(1, (cnt * (3_000_000.0 / cnt.sum())).astype(np.int64))
            ti = np.repeat(np.arange(len(tris)), cnt)
            r1, r2 = R.random(len(ti)), R.random(len(ti))
            s = np.sqrt(r1)
            w0, w1, w2 = 1.0 - s, s * (1.0 - r2), s * r2
            P3 = a[ti] * w0[:, None] + b[ti] * w1[:, None] + c[ti] * w2[:, None]
            UV = uv[tris[ti, 0]] * w0[:, None] + uv[tris[ti, 1]] * w1[:, None] + uv[tris[ti, 2]] * w2[:, None]
            N3 = nrm[tris[ti, 0]] * w0[:, None] + nrm[tris[ti, 1]] * w1[:, None] + nrm[tris[ti, 2]] * w2[:, None]
            N3 /= np.maximum(np.linalg.norm(N3, axis=1, keepdims=True), 1e-9)
            if q["tex"] >= 0:
                im = np.asarray(Image.open(io.BytesIO(self.tex[q["tex"]])).convert("RGBA"), dtype=np.float64) / 255.0
                h, w = im.shape[:2]
                v = UV[:, 1] if not q["fl"] & 2 else 1.0 - UV[:, 1]
                col = im[np.floor((v % 1.0) * h).astype(np.int64) % h, np.floor((UV[:, 0] % 1.0) * w).astype(np.int64) % w]
            else:
                m = re.search(r"#([0-9a-fA-F]{6})", q["name"])
                rgb = [int(m.group(1)[k:k + 2], 16) / 255.0 for k in (0, 2, 4)] if m else [0.78, 0.78, 0.78]
                col = np.tile(np.array(rgb + [1.0]), (len(ti), 1))
            ok = col[:, 3] >= 0.5 if q["fl"] & 4 else np.ones(len(ti), dtype=bool)
            shade = 0.62 + 0.38 * (0.5 + 0.5 * N3[:, 1])                           # the light from above, softly: the tops of its lumps bright, their undersides dark
            px = np.clip(np.floor((P3[:, u_ax] - lo[u_ax]) / span_u * n).astype(np.int64), 0, n - 1)
            if axis == 0:
                px = n - 1 - px                                                    # from +x, +z is to the left
            py2 = np.clip(np.floor((hi[1] - P3[:, 1]) / span_y * n).astype(np.int64), 0, n - 1)
            pix, dep = (py2 * n + px)[ok], P3[ok, axis]
            rgb = (col[ok, :3] * shade[ok, None])
            o = np.lexsort((dep, pix))
            last = np.ones(len(o), dtype=bool)
            last[:-1] = pix[o][1:] != pix[o][:-1]
            win = o[last]                                                          # at each place, this mesh's point nearest the eye
            nearer = dep[win] > best_d[pix[win]]
            win = win[nearer]
            best_d[pix[win]] = dep[win]
            img[pix[win], :3] = rgb[win]
            img[pix[win], 3] = 1.0
        img = img.reshape(n, n, 4)
        prem = img[:, :, :3] * img[:, :, 3:4]
        small_a = img[:, :, 3].reshape(size, ss, size, ss).mean((1, 3))
        small_c = prem.reshape(size, ss, size, ss, 3).mean((1, 3)) / np.maximum(small_a[:, :, None], 1e-6)
        out = np.concatenate([small_c, small_a[:, :, None]], 2)
        have, col = out[:, :, 3] > 1e-3, out[:, :, :3].copy()
        for _ in range(8):                                                         # colour spread into what's empty, so a filtered edge isn't dark
            if have.all():
                break
            acc, cnt = np.zeros_like(col), np.zeros(have.shape)
            hp, cp = np.pad(have, 1), np.pad(col, ((1, 1), (1, 1), (0, 0)))         # padded, so nothing comes round from the far edge
            for dy, dx in ((1, 2), (1, 0), (2, 1), (0, 1)):
                h = hp[dy:dy + size, dx:dx + size]
                acc += cp[dy:dy + size, dx:dx + size] * h[:, :, None]
                cnt += h
            fill = ~have & (cnt > 0)
            col[fill] = acc[fill] / cnt[fill, None]
            have = have | fill
        out[:, :, :3] = col
        return out

    def far(self, dst, size):
        """two crossed cards with its picture from the front and from the side"""
        from PIL import Image
        front, side = self.picture(size, 2), self.picture(size, 0)
        pad = 2
        tile = size + pad * 2
        atlas = np.zeros((tile, tile * 2, 4))
        atlas[pad:pad + size, pad:pad + size] = front
        atlas[pad:pad + size, tile + pad:tile + pad + size] = side
        px = np.clip(np.rint(atlas * 255.0), 0, 255).astype(np.uint8)
        lo, hi = self.lo, self.hi
        cx, cz = 0.5 * (lo[0] + hi[0]), 0.5 * (lo[2] + hi[2])
        u0, u1 = pad / (tile * 2.0), (pad + size) / (tile * 2.0)
        v0, v1 = pad / float(tile), (pad + size) / float(tile)
        pos, nrm, uv, idx = [], [], [], []

        def card(p00, p10, p11, p01, ua, ub):
            b = len(pos) // 3
            for p, (u, v) in zip((p00, p10, p11, p01), ((ua, v1), (ub, v1), (ub, v0), (ua, v0))):
                pos.extend(p)
                dx, dz = p[0] - cx, p[2] - cz
                ln = max((dx * dx + dz * dz) ** 0.5, 1e-9)
                top = p[1] > 0.5 * (lo[1] + hi[1])
                n = np.array([dx / ln * (0.5 if top else 0.8), 0.75 if top else 0.35, dz / ln * (0.5 if top else 0.8)])
                nrm.extend((n / np.linalg.norm(n)).tolist())
                uv.extend((u, v))
            idx.extend([b, b + 1, b + 2, b, b + 2, b + 3, b, b + 2, b + 1, b, b + 3, b + 2])       # and its back
        card((lo[0], lo[1], cz), (hi[0], lo[1], cz), (hi[0], hi[1], cz), (lo[0], hi[1], cz), u0, u1)                    # the front's picture, across x
        card((cx, lo[1], hi[2]), (cx, lo[1], lo[2]), (cx, hi[1], lo[2]), (cx, hi[1], hi[2]), 0.5 + u0, 0.5 + u1)        # the side's, across z: from +x, +z is on the left
        node = self.meshes[0]["node"]
        ibm = [float(v) for v in np.linalg.inv(np.array(self.B.W[node]).reshape(4, 4).T).T.reshape(-1)]
        n = len(pos) // 3
        mesh = dict(name="far_cards", node=node, pos=[float(v) for v in pos], nrm=[float(v) for v in nrm], uv=[float(v) for v in uv], idx=idx,
                    joints=[[0, 0, 0, 0]] * n, weights=[[1.0, 0.0, 0.0, 0.0]] * n, boneMap=[node], ibm=ibm, tex=0, cutout=True)
        write_pack(dst, self.nodes, [mesh], [], [to_png(px.tobytes(), tile * 2, tile)], verbose=False, fresh=True)
        material.set_materials(dst, {"far_cards": (0.9, 0.0, 0.0, 0.0)})
        return px


def make(path, lod1=0.45, lod2=0.14, far=128, say=print, body=False, out=None):
    assert "~" not in os.path.basename(path), "%s is already a copy" % path
    P = Prop(path, body)
    here = os.path.dirname(os.path.abspath(path))
    if out is None:
        out = os.path.join(os.path.dirname(here), "props") if os.path.basename(here).lower() == "packs" else here
    assert os.path.basename(os.path.abspath(out)).lower() != "packs", "nothing is written into packs/: a file there joins the crowd"
    assert os.path.isdir(out), "%s isn't there" % out
    stem = os.path.join(out, os.path.basename(path)[:-len(".rpack")])
    if body:
        far = 0
    say("%s: %d triangles" % (os.path.normpath(path), P.tris))
    for tag, share in (("~lod1", lod1), ("~lod2", lod2)):
        if share <= 0:
            continue
        n, cell, got = P.find(share)
        if n >= P.tris * 0.9 or n < 4:
            say("  %s: no lighter copy to be had (%d triangles at a cell of %.3f)" % (tag[1:], n, cell))
            if os.path.exists(stem + tag + ".rpack"):
                os.remove(stem + tag + ".rpack")
            continue
        P.write(stem + tag + ".rpack", got)
        say("  %s: %d triangles (%.0f in a hundred), cells of %.3f" % (tag[1:], n, 100.0 * n / P.tris, cell))
    if far > 0:
        px = P.far(stem + "~far.rpack", far)
        say("  far: two cards, a picture %d x %d, %.0f in a hundred of it filled" % (px.shape[1], px.shape[0], 100.0 * (px[:, :, 3] > 127).mean()))
    return P


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)

    def opt(name, default):
        return float(argv[argv.index(name) + 1]) if name in argv else default
    far = 0 if "--no-far" in argv else int(opt("--far", 128))
    body = "--body" in argv
    out = argv[argv.index("--out") + 1] if "--out" in argv else None
    for p in [a for i, a in enumerate(argv[1:], 1) if a.endswith(".rpack") and not argv[i - 1].startswith("--")]:
        make(p, opt("--lod1", 0.30 if body else 0.45), opt("--lod2", 0.08 if body else 0.14), far, body=body, out=out)


if __name__ == "__main__":
    main(sys.argv)
