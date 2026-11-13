#!/usr/bin/env python3
"""cook_game.py -- a game-grade body from a film pack, on the same skeleton (engine plan, Phase 2).

    cook_game.py packs/v1_kaeli_street.rpack packs/v1_kaeli_game.rpack --tris 20000 --atlas 512 --drop anima_panties,anima_bra

11-02: same rules and bodies everywhere, and no frame ever drops on the Pi. One film mesh on a game path was 95% of the zombie
game's frame, and a frame's budget is what holds it, so a game path gets a body cooked to a budget. Pack to pack, never a rebake: the nodes,
the strings and every clip are carried over byte for byte, so the rig and the moves are the film body's, and the sim can't tell. The film
pack isn't touched.

What goes:
- Every mesh is merged into one (two if the pack has garments, because the engine measures the body without them), with its bind baked in
  and its joints moved onto one bone table. Every mesh's IBMs must agree, and it stops if they don't.
- Triangles, by edge collapse (Garland-Heckbert) across the whole merged body, weighted by area. So the error is spent where it shows: an
  earring goes long before the silhouette does, and a hair card disappears as a whole.
- Textures, into one opaque atlas. A surviving triangle keeps the UVs of the original triangle it came from, so it shows a small patch of
  the texture from the place it stood. That's why heavy reduction doesn't scramble the picture.
- Morphs and normal maps, because a body 60 lines tall on a tube shows neither. Alpha, because a discard costs this GPU its early depth test.

--bc writes RPK2 (design/pack_format.md): each texture as BC1 or BC3 with its mips, in a TEXC chunk after the old body, ready for the GPU. It takes no decoding at load and an eighth of the memory.
"""
import argparse, heapq, io, os, struct, sys, zlib
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bc
import rpack

MR = struct.Struct("<15I")


def read_meshes(p):
    b = p.buf
    out = []
    for m in range(p.nMeshes):
        name, node, verts, idx, bones, flags, oP, oN, oU, oJ, oW, oI, oBM, oIBM, oB = MR.unpack_from(b, p.offMeshes + m * 60)
        d = dict(index=m, name=p.sname(name), nameId=name, node=node, verts=verts, flags=flags)
        d["pos"] = np.frombuffer(b, np.float32, verts * 3, oP).reshape(-1, 3).astype(np.float64)
        d["nrm"] = np.frombuffer(b, np.float32, verts * 3, oN).reshape(-1, 3) if flags & 1 else None
        d["uv"] = np.frombuffer(b, np.float32, verts * 2, oU).reshape(-1, 2) if flags & 2 else None
        d["joints"] = np.frombuffer(b, np.uint8, verts * 4, oJ).reshape(-1, 4)
        d["weights"] = np.frombuffer(b, np.uint8, verts * 4, oW).reshape(-1, 4)
        d["idx"] = np.frombuffer(b, np.uint32, idx, oI).reshape(-1, 3) if idx else np.arange(verts // 3 * 3, dtype=np.uint32).reshape(-1, 3)
        d["boneMap"] = np.frombuffer(b, np.uint16, bones, oBM)
        d["ibm"] = np.frombuffer(b, np.float32, bones * 16, oIBM).reshape(-1, 16)
        bind = np.frombuffer(b, np.float32, 16, oB).reshape(4, 4).T.astype(np.float64)   # column-major in the file
        if not np.allclose(bind, np.eye(4)):
            d["pos"] = (bind[:3, :3] @ d["pos"].T).T + bind[:3, 3]
        out.append(d)
    mats = {}
    for i in range(p.nMats):
        mesh = struct.unpack_from("<I", b, p.offMats + i * 36)[0]
        fl = struct.unpack_from("<I", b, p.offMats + i * 36 + 16)[0]
        tex = struct.unpack_from("<i", b, p.offMats + i * 36 + 24)[0]
        mats.setdefault(mesh, (tex, fl))
    for d in out:
        d["tex"], d["matFlags"] = mats.get(d["index"], (-1, 0))
    return out


def is_garment(name):
    return "~cloth" in name or "~worn" in name or name.startswith("wear~")


def tint_of(name):
    i = name.find("#")
    if i >= 0 and len(name) >= i + 7:
        try:
            return tuple(int(name[i + 1 + k * 2:i + 3 + k * 2], 16) for k in range(3))
        except ValueError:
            pass
    return (200, 200, 200)


def build_atlas(p, meshes, size):
    """one opaque atlas: each texture (and each untextured mesh's colour) gets a rectangle. Returns the image and mesh -> (x0, y0, w, h, flip) in pixels."""
    b = p.buf
    imgs = {}
    for d in meshes:
        t = d["tex"]
        if t >= 0 and t not in imgs and d["uv"] is not None:
            off, ln = struct.unpack_from("<II", b, p.offTexTable + t * 16 + 8)
            imgs[t] = Image.open(io.BytesIO(b[off:off + ln])).convert("RGBA")
    # the holes filled with the texture's own average, so a corner that lands in one still shows the right colour
    flat = {}
    for t, im in imgs.items():
        a = np.asarray(im, dtype=np.float32)
        al = a[:, :, 3:4] / 255.0
        s = al.sum()
        mean = (a[:, :, :3] * al).sum((0, 1)) / s if s > 0 else a[:, :, :3].mean((0, 1))
        o = a[:, :, :3] * al + mean * (1.0 - al)
        flat[t] = Image.fromarray(o.clip(0, 255).astype(np.uint8), "RGB")
    want = {t: max(im.size) for t, im in flat.items()}
    # shelves: the shrink that fits is found by trying
    for shrink in (4, 8, 16, 32, 64, 128):
        sizes = {t: max(8, int(w / shrink)) for t, w in want.items()}
        order = sorted(sizes, key=lambda t: -sizes[t])
        x = y = shelf = 0
        place = {}
        fit = True
        for t in order:
            s = sizes[t]
            if x + s > size:
                x = 0
                y += shelf
                shelf = 0
            if y + s > size:
                fit = False
                break
            place[t] = (x, y, s, s)
            x += s
            shelf = max(shelf, s)
        # a row left for the plain colours
        if fit and y + shelf + 8 <= size:
            break
    else:
        raise SystemExit("the atlas doesn't fit in %d" % size)
    atlas = Image.new("RGB", (size, size), (128, 128, 128))
    for t, (x, y, w, h) in place.items():
        atlas.paste(flat[t].resize((w, h), Image.BOX), (x, y))
    rect = {}
    cx, cy = 0, y + shelf
    for d in meshes:
        if d["tex"] in place and d["uv"] is not None:
            rect[d["index"]] = place[d["tex"]]
        else:
            atlas.paste(tint_of(d["name"]), (cx, cy, cx + 8, cy + 8))
            rect[d["index"]] = (cx, cy, 8, 8)
            d["plain"] = True
            cx += 8
            if cx + 8 > size:
                cx = 0
                cy += 8
    return atlas, rect


def decimate(P, faces, target):
    """edge collapse over welded positions. P: Nx3 array; faces: Mx3 array of corner ids. Returns (rep -> position, face list of [r0, r1, r2, o0, o1, o2])."""
    key = np.round(P, 5)
    uq, rep = np.unique(key, axis=0, return_inverse=True)
    rep = rep.reshape(-1)
    NR = len(uq)
    rf = rep[faces]
    keep = (rf[:, 0] != rf[:, 1]) & (rf[:, 1] != rf[:, 2]) & (rf[:, 0] != rf[:, 2])
    faces = faces[keep]
    rf = rf[keep]
    rpos = np.zeros((NR, 3))
    rpos[rep] = P
    print("  %d corners weld to %d points, %d triangles" % (len(P), NR, len(faces)), flush=True)
    # the quadrics, weighted by area
    p0, p1, p2 = rpos[rf[:, 0]], rpos[rf[:, 1]], rpos[rf[:, 2]]
    n = np.cross(p1 - p0, p2 - p0)
    ln = np.linalg.norm(n, axis=1)
    area = ln * 0.5
    n = n / np.maximum(ln, 1e-20)[:, None]
    d = -(n * p0).sum(1)
    nn = np.stack([n[:, 0], n[:, 1], n[:, 2], d], 1)
    iu = [(0, 0), (0, 1), (0, 2), (0, 3), (1, 1), (1, 2), (1, 3), (2, 2), (2, 3), (3, 3)]
    fq = np.stack([nn[:, a] * nn[:, c] * area for a, c in iu], 1)
    Q = np.zeros((NR, 10))
    for k in range(3):
        np.add.at(Q, rf[:, k], fq)
    # open edges keep their line: a plane through the edge, square to the face
    e = np.concatenate([rf[:, [0, 1]], rf[:, [1, 2]], rf[:, [2, 0]]])
    fi = np.concatenate([np.arange(len(rf))] * 3)
    lo, hi = e.min(1), e.max(1)
    code = lo.astype(np.int64) * NR + hi
    uqe, first, cnt = np.unique(code, return_index=True, return_counts=True)
    bd = cnt == 1
    be, bf = e[first[bd]], fi[first[bd]]
    a, c = rpos[be[:, 0]], rpos[be[:, 1]]
    ed = c - a
    m = np.cross(ed, n[bf])
    ml = np.maximum(np.linalg.norm(m, axis=1), 1e-20)
    w = (ed * ed).sum(1)
    m = m / ml[:, None]
    dm = -(m * a).sum(1)
    mm = np.stack([m[:, 0], m[:, 1], m[:, 2], dm], 1)
    bq = np.stack([mm[:, x] * mm[:, y] * w for x, y in iu], 1)
    np.add.at(Q, be[:, 0], bq)
    np.add.at(Q, be[:, 1], bq)
    print("  %d edges, %d of them open" % (len(uqe), int(bd.sum())), flush=True)

    def cost_of(q, x, y, z):
        return q[0] * x * x + 2 * q[1] * x * y + 2 * q[2] * x * z + 2 * q[3] * x + q[4] * y * y + 2 * q[5] * y * z + 2 * q[6] * y + q[7] * z * z + 2 * q[8] * z + q[9]

    Ql = Q.tolist()
    pos = rpos.tolist()
    F = np.concatenate([rf, faces], 1).tolist()
    alive = [True] * len(F)
    vf = [[] for _ in range(NR)]
    for i, f in enumerate(F):
        vf[f[0]].append(i)
        vf[f[1]].append(i)
        vf[f[2]].append(i)
    parent = list(range(NR))
    ver = [0] * NR

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    heap = []

    def push(e0, e1):
        q0, q1 = Ql[e0], Ql[e1]
        q = [q0[k] + q1[k] for k in range(10)]
        a0, a1 = pos[e0], pos[e1]
        x, y, z = (a0[0] + a1[0]) * 0.5, (a0[1] + a1[1]) * 0.5, (a0[2] + a1[2]) * 0.5
        heapq.heappush(heap, (cost_of(q, x, y, z), ver[e0], ver[e1], e0, e1))

    for co in uqe.tolist():
        e0, e1 = co // NR, co % NR
        q0, q1 = Ql[e0], Ql[e1]
        q = [q0[k] + q1[k] for k in range(10)]
        a0, a1 = pos[e0], pos[e1]
        heap.append((cost_of(q, (a0[0] + a1[0]) * 0.5, (a0[1] + a1[1]) * 0.5, (a0[2] + a1[2]) * 0.5), 0, 0, e0, e1))
    heapq.heapify(heap)
    ntris = len(F)
    step = 0
    while ntris > target and heap:
        c, v0, v1, e0, e1 = heapq.heappop(heap)
        if parent[e0] != e0 or parent[e1] != e1 or ver[e0] != v0 or ver[e1] != v1:
            continue
        parent[e1] = e0
        a0, a1 = pos[e0], pos[e1]
        pos[e0] = [(a0[0] + a1[0]) * 0.5, (a0[1] + a1[1]) * 0.5, (a0[2] + a1[2]) * 0.5]
        q0, q1 = Ql[e0], Ql[e1]
        Ql[e0] = [q0[k] + q1[k] for k in range(10)]
        ver[e0] += 1
        kept = []
        neigh = set()
        for lst in (vf[e0], vf[e1]):
            for fi2 in lst:
                if not alive[fi2]:
                    continue
                f = F[fi2]
                a2, b2, c2 = find(f[0]), find(f[1]), find(f[2])
                if a2 == b2 or b2 == c2 or a2 == c2:
                    alive[fi2] = False
                    ntris -= 1
                    continue
                f[0], f[1], f[2] = a2, b2, c2
                kept.append(fi2)
                if a2 != e0: neigh.add(a2)
                if b2 != e0: neigh.add(b2)
                if c2 != e0: neigh.add(c2)
        vf[e0] = kept
        vf[e1] = []
        for v in neigh:
            push(e0, v)
        step += 1
        if step % 100000 == 0:
            print("  %d triangles left" % ntris, flush=True)
    return pos, [f for i, f in enumerate(F) if alive[i]]


def rpk2_tail(b, chunks):
    """RPK2: chunks after the old body, a directory, a footer. A reader of the old format stops at `total` and never sees any of it. chunks: (tag, bytes, or a function of the chunk's offset that gives them)"""
    ents = []
    for tag, data in chunks:
        while len(b) % 16:
            b.append(0)
        off = len(b)
        data = data(off) if callable(data) else data
        b.extend(data)
        ents.append(struct.pack("<4sIII", tag, off, len(data), zlib.crc32(data) & 0xffffffff))
    while len(b) % 16:
        b.append(0)
    d = len(b)
    b.extend(b"".join(ents))
    b.extend(struct.pack("<III4s", d, len(ents), len(b) + 16, b"RPK2"))


def texc_chunk(items):
    """TEXC: a count, a record for each texture, then each one's levels, largest first, on a 16-byte line. items: (slot, format, w, h, levels, flags, data)"""
    def make(off):
        out = bytearray(struct.pack("<I", len(items)) + bytes(len(items) * 32))
        for i, (slot, fmt, w, h, levels, flags, data) in enumerate(items):
            while (off + len(out)) % 16:
                out.append(0)
            struct.pack_into("<8I", out, 4 + i * 32, slot, fmt, w, h, levels, flags, off + len(out), len(data))
            out.extend(data)
        return bytes(out)
    return make


USE_BC7 = False   # --bc7: the PC's cook. The Pi can't sample it, so a game cooks once for each


def compress_image(data, flip):
    """an image file to (format, w, h, levels, flags, bytes). flags 1: it has holes, so a draw needs the cutout pipeline. A material's flip is baked in here"""
    im = np.array(Image.open(io.BytesIO(data)).convert("RGBA"))
    if flip:
        im = im[::-1]
    a = im[:, :, 3]
    want = bool((a < 255).any())
    chain = bc.mips(im)
    if USE_BC7:
        return bc.BC7, im.shape[1], im.shape[0], len(chain), 1 if (a < 128).any() else 0, b"".join(bc.encode7(l) for l in chain)
    return bc.BC3 if want else bc.BC1, im.shape[1], im.shape[0], len(chain), 1 if (a < 128).any() else 0, b"".join(bc.encode(l, want) for l in chain)


class Out:
    """a pack being written front to back. Each part is placed once and the header filled in at the end."""

    def __init__(self):
        self.b = bytearray(b"RPK1" + b"\0" * 60)
        self.chunks = []

    def place(self, data):
        while len(self.b) % 4:
            self.b.append(0)
        o = len(self.b)
        self.b.extend(data)
        return o

    def carry(self, p):
        """what stays as it is: strings, nodes, the size block, the clips. Sets the offsets."""
        b = p.buf
        n_str = struct.unpack_from("<I", b, p.offStr)[0]
        ents = [struct.unpack_from("<II", b, p.offStr + 4 + i * 8) for i in range(n_str)]
        blob_len = max(o + l for o, l in ents) if ents else 0
        self.offStr = self.place(b[p.offStr: p.offStr + 4 + n_str * 8 + blob_len])
        self.offNodes = self.place(b[p.offNodes: p.offNodes + p.nNodes * 52])
        self.offSize = self.place(b[p.offSize: p.offSize + 48])
        recs = []
        for ci in range(p.nClips):
            c = p.clip(ci)
            tab = [struct.pack("<III", node, typ, self.place(data)) for node, typ, data in p.tracks(ci)]
            offT = self.place(b"".join(tab))
            recs.append(struct.pack("<IfIII", c["nameId"], c["fps"], c["frames"], c["nTracks"], offT))
        self.offClips = self.place(b"".join(recs))

    def finish(self, p, dst, nMeshes, nMats, nTex, offMeshes, offMats, offTex):
        total = len(self.b)
        struct.pack_into("<15I", self.b, 4, p.ver, p.nNodes, nMeshes, nMats, p.nClips, nTex, self.offNodes, offMeshes, offMats, self.offClips, offTex, self.offStr, self.offSize, total, 0)
        if self.chunks:
            rpk2_tail(self.b, self.chunks)
        open(dst, "wb").write(self.b)
        return len(self.b)


def cook_bank(p, dst):
    """a pack whose clips other bodies borrow: the skeleton and the clips, and one triangle to stand for a body nobody draws"""
    o = Out()
    o.carry(p)
    oP = o.place(struct.pack("<9f", 0, 0, 0, 0, 0.001, 0, 0.001, 0, 0))
    oN = o.place(struct.pack("<9f", 0, 1, 0, 0, 1, 0, 0, 1, 0))
    oU = o.place(struct.pack("<6f", 0, 0, 0, 0, 0, 0))
    oJ = o.place(bytes(12))
    oW = o.place(bytes([255, 0, 0, 0] * 3))
    oI = o.place(struct.pack("<3I", 0, 1, 2))
    oBM = o.place(struct.pack("<H", 0))
    eye = np.eye(4, dtype=np.float32).tobytes()
    oIBM = o.place(eye)
    oB = o.place(eye)
    first = MR.unpack_from(p.buf, p.offMeshes)
    offM = o.place(MR.pack(first[0], first[1], 3, 3, 1, 7, oP, oN, oU, oJ, oW, oI, oBM, oIBM, oB))
    rec = bytearray(36)
    struct.pack_into("<i", rec, 24, -1)
    offMat = o.place(bytes(rec))
    offTex = o.place(b"")
    total = o.finish(p, dst, 1, 1, 0, offM, offMat, offTex)
    print("[cook] %s: a bank: %d clips, %d nodes, %.1f MB (from %.1f MB)" % (os.path.basename(dst), p.nClips, p.nNodes, total / 1e6, len(p.buf) / 1e6))


def cook_retex(p, dst, limit, use_bc=False):
    """every mesh as it is, every texture no bigger than limit, each image only once. For what is already within its triangles: a town's props. use_bc: compressed, in RPK2"""
    b = p.buf
    o = Out()
    o.carry(p)
    recs = []
    for m in range(p.nMeshes):
        name, node, verts, idx, bones, flags, oP, oN, oU, oJ, oW, oI, oBM, oIBM, oB = MR.unpack_from(b, p.offMeshes + m * 60)
        part = lambda off, size: o.place(b[off:off + size]) if off else 0
        recs.append(MR.pack(name, node, verts, idx, bones, flags, part(oP, verts * 12), part(oN, verts * 12), part(oU, verts * 8), part(oJ, verts * 4), part(oW, verts * 4), part(oI, idx * 4), part(oBM, bones * 2), part(oIBM, bones * 64), part(oB, 64)))
    offM = o.place(b"".join(recs))
    offMat = o.place(b[p.offMats:p.offMats + p.nMats * 36])
    tab = bytearray()
    items = []
    flips, normals = set(), set()
    for r in range(p.nMats):
        fl = struct.unpack_from("<I", b, p.offMats + r * 36 + 16)[0]
        tex, ntex = struct.unpack_from("<ii", b, p.offMats + r * 36 + 24)
        if fl & 2:
            flips.update((tex, ntex - 1))
        normals.add(ntex - 1)
    for t in range(p.nTex):
        rec = bytearray(b[p.offTexTable + t * 16:p.offTexTable + t * 16 + 16])
        off, ln = struct.unpack_from("<II", rec, 8)
        data = b[off:off + ln]
        im = Image.open(io.BytesIO(data))
        if max(im.size) > limit:
            s = limit / max(im.size)
            im = im.convert("RGBA" if im.mode in ("RGBA", "LA", "P") else "RGB").resize((max(1, int(im.size[0] * s)), max(1, int(im.size[1] * s))), Image.BOX)
            bio = io.BytesIO()
            im.save(bio, "PNG", optimize=True)
            data = bio.getvalue()
        if use_bc and t not in normals:   # a normal map stays an image: BC1's colour endpoints would ruin it, and a game pack has none
            fmt, w, h, levels, flags, packed = compress_image(data, t in flips)
            items.append((t, fmt, w, h, levels, flags, packed))
            struct.pack_into("<II", rec, 8, 0, 0)
        else:
            struct.pack_into("<II", rec, 8, o.place(data), len(data))
        tab += rec
    offTex = o.place(bytes(tab))
    if items:
        o.chunks = [(b"TEXC", texc_chunk(items)), (b"NOTE", ("cooked with cook_game.py --bc from " + os.path.basename(p.path)).encode("utf-8"))]
    total = o.finish(p, dst, p.nMeshes, p.nMats, p.nTex, offM, offMat, offTex)
    print("[cook] %s: textures to %d, %.2f MB (from %.2f MB)" % (os.path.basename(dst), limit, total / 1e6, len(b) / 1e6))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--tris", type=int, default=20000)
    ap.add_argument("--atlas", type=int, default=512)
    ap.add_argument("--drop", default="", help="mesh names to leave out, comma separated: whatever can never be seen")
    ap.add_argument("--bank", action="store_true", help="the clips and skeleton only: a pack other bodies borrow from")
    ap.add_argument("--retex", type=int, default=0, help="change nothing but the textures, to this size at most")
    ap.add_argument("--bc", action="store_true", help="write RPK2: textures compressed, with their mips")
    ap.add_argument("--bc7", action="store_true", help="with --bc: BC7 instead of BC1 and BC3, a better picture for a PC's cook. The Pi can't sample it")
    a = ap.parse_args()
    if a.bc7:
        global USE_BC7
        USE_BC7 = True
    p = rpack.Rpack(a.src)
    os.makedirs(os.path.dirname(a.dst) or ".", exist_ok=True)
    if a.bank:
        return cook_bank(p, a.dst)
    if a.retex:
        return cook_retex(p, a.dst, a.retex, a.bc)
    meshes = read_meshes(p)
    drop = [s for s in a.drop.split(",") if s]
    meshes = [d for d in meshes if not any(s in d["name"] for s in drop)]
    print("%s: %d meshes, %d triangles" % (a.src, len(meshes), sum(len(d["idx"]) for d in meshes)), flush=True)

    # one bone table
    ibm_of = {}
    for d in meshes:
        for i, n in enumerate(d["boneMap"].tolist()):
            if n in ibm_of:
                if not np.allclose(ibm_of[n], d["ibm"][i], atol=1e-4):
                    raise SystemExit("%s: bone %d has two different inverse binds, so these meshes can't share a table" % (d["name"], n))
            else:
                ibm_of[n] = d["ibm"][i]
    bones = sorted(ibm_of)
    slot = {n: i for i, n in enumerate(bones)}

    atlas, rect = build_atlas(p, meshes, a.atlas)

    # merged corners
    Ps, Ns, UVs, Js, Ws, Fs, Cls = [], [], [], [], [], [], []
    base = 0
    for d in meshes:
        v = d["verts"]
        Ps.append(d["pos"])
        Ns.append(d["nrm"] if d["nrm"] is not None else np.tile([0.0, 1.0, 0.0], (v, 1)))
        x, y, w, h = rect[d["index"]]
        if d["uv"] is not None and not d.get("plain"):
            uv = d["uv"] - np.floor(d["uv"].min(0))        # a pack's tiles (u 2..3) to 0..1
            uv = np.where(uv > 1.0, uv - np.floor(uv), uv)  # and what wraps
            if d["matFlags"] & 2:
                uv = uv * [1.0, -1.0] + [0.0, 1.0]
            mu = (x + 0.5 + uv[:, 0] * (w - 1.0)) / a.atlas
            mv = (y + 0.5 + uv[:, 1] * (h - 1.0)) / a.atlas
        else:
            mu = np.full(v, (x + w * 0.5) / a.atlas)
            mv = np.full(v, (y + h * 0.5) / a.atlas)
        UVs.append(np.stack([mu, mv], 1))
        bm = np.array([slot[n] for n in d["boneMap"].tolist()], dtype=np.uint8)
        Js.append(bm[d["joints"]])
        Ws.append(d["weights"])
        Fs.append(d["idx"].astype(np.int64) + base)
        Cls.append(np.full(len(d["idx"]), 1 if is_garment(d["name"]) else 0, dtype=np.uint8))
        base += v
    P = np.concatenate(Ps)
    N = np.concatenate(Ns)
    UV = np.concatenate(UVs)
    J = np.concatenate(Js)
    W = np.concatenate(Ws)
    faces = np.concatenate(Fs)
    cls_of_corner = np.zeros(len(P), np.uint8)
    cls_of_corner[faces[:, 0]] = np.concatenate(Cls)

    pos, F = decimate(P, faces, a.tris)
    print("  %d triangles" % len(F), flush=True)

    # out: per class, a vertex for each point + UV seen, with a smooth normal off the surviving surface
    rn = {}
    for f in F:
        a0, a1, a2 = pos[f[0]], pos[f[1]], pos[f[2]]
        ux, uy, uz = a1[0] - a0[0], a1[1] - a0[1], a1[2] - a0[2]
        vx, vy, vz = a2[0] - a0[0], a2[1] - a0[1], a2[2] - a0[2]
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        for r in f[:3]:
            s = rn.setdefault(r, [0.0, 0.0, 0.0])
            s[0] += nx; s[1] += ny; s[2] += nz
    groups = []
    for cls in (0, 1):
        vmap = {}
        vp, vn, vu, vj, vw, ix = [], [], [], [], [], []
        for f in F:
            if int(cls_of_corner[f[3]]) != cls:
                continue
            tri = []
            for k in range(3):
                r, o = f[k], f[k + 3]
                key = (r, round(float(UV[o, 0]), 5), round(float(UV[o, 1]), 5))
                i = vmap.get(key)
                if i is None:
                    i = len(vp)
                    vmap[key] = i
                    vp.append(pos[r])
                    s = rn[r]
                    l = (s[0] * s[0] + s[1] * s[1] + s[2] * s[2]) ** 0.5
                    # a surface that folded to nothing keeps the normal it came with
                    vn.append([s[0] / l, s[1] / l, s[2] / l] if l > 1e-12 else N[o].tolist())
                    vu.append(UV[o].tolist())
                    vj.append(J[o].tolist())
                    vw.append(W[o].tolist())
                tri.append(i)
            ix.extend(tri)
        if ix:
            groups.append(dict(cls=cls, pos=vp, nrm=vn, uv=vu, j=vj, w=vw, idx=ix))

    # ---- the file: nodes, strings and clips as they are; the rest new. The string table is kept whole, so every name id still means the same thing. ----
    o = Out()
    o.carry(p)
    place = o.place
    b = p.buf
    body_name = next(i for d in meshes if not is_garment(d["name"]) for i in [d["nameId"]])
    worn_name = next((d["nameId"] for d in meshes if is_garment(d["name"])), body_name)
    mesh_recs = []
    for g in groups:
        nv = len(g["pos"])
        oP = place(np.array(g["pos"], np.float32).tobytes())
        oN = place(np.array(g["nrm"], np.float32).tobytes())
        oU = place(np.array(g["uv"], np.float32).tobytes())
        oJ = place(np.array(g["j"], np.uint8).tobytes())
        oW = place(np.array(g["w"], np.uint8).tobytes())
        oI = place(np.array(g["idx"], np.uint32).tobytes())
        oBM = place(np.array(bones, np.uint16).tobytes())
        oIBM = place(np.array([ibm_of[n] for n in bones], np.float32).tobytes())
        oB = place(np.eye(4, dtype=np.float32).tobytes())
        mesh_recs.append(MR.pack(worn_name if g["cls"] else body_name, meshes[0]["node"], nv, len(g["idx"]), len(bones), 7, oP, oN, oU, oJ, oW, oI, oBM, oIBM, oB))
    offMeshes = place(b"".join(mesh_recs))
    mats = bytearray()
    for i in range(len(groups)):
        r = bytearray(36)
        struct.pack_into("<I", r, 0, i)
        struct.pack_into("<i", r, 24, 0)
        struct.pack_into("<i", r, 28, 0)
        mats += r
    offMats = place(bytes(mats))
    png = io.BytesIO()
    atlas.save(png, "PNG", optimize=True)
    offPng = place(png.getvalue())
    tt = bytearray(16)
    struct.pack_into("<II", tt, 8, offPng, len(png.getvalue()))
    offTex = place(bytes(tt))
    total = o.finish(p, a.dst, len(groups), len(groups), 1, offMeshes, offMats, offTex)
    tris = sum(len(g["idx"]) // 3 for g in groups)
    print("[cook] %s: %d triangles, %d vertices, %d meshes, 1 texture %d, %d bones, %d clips, %.1f MB (from %.1f MB, %d triangles)" % (
        os.path.basename(a.dst), tris, sum(len(g["pos"]) for g in groups), len(groups), a.atlas, len(bones), p.nClips, total / 1e6, len(b) / 1e6, sum(len(d["idx"]) for d in meshes)))
    if a.bc:
        cook_retex(rpack.Rpack(a.dst), a.dst, 1 << 30, True)


if __name__ == "__main__":
    main()
