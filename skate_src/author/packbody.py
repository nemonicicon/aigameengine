#!/usr/bin/env python3
"""packbody.py -- a body out of a pack, as arrays, and the way back in.

    B = PackBody("reva_native/props/v1_reva_hi.rpack")
    m = B.find("anima_body", 31589)[0]         # a mesh, by its name and its size
    pos, uv = B.rest(m)                        # where its vertices are at rest, in the rig's space (metres on a true-size rig)
    B.set_rest(m, new_pos)                     # the same vertices somewhere else: the pack's own positions are worked back through the skin
    t, im = B.image(m); B.put(t, im2)          # its texture, out and in
    B.drop([m, ...])                           # meshes that leave the pack
    B.add_mesh(name, like, pos, nrm, uv, nodes, weights, tris, tex)   # a mesh that joins it: a garment made for this body
    B.write()                                  # all of it written: positions and normals in place, textures appended, the tables rewritten

For tools that change a finished body: its shape, its paint, which of its meshes it keeps, and meshes made for it. A bake isn't needed and nothing authored on the
pack is lost: clips, morphs, marks and chunks stay (rpackchunks.save). What it doesn't do: change a mesh that's staying, other than where its vertices are.

⛔ Dropping a mesh moves the meshes after it. The materials' and the morphs' mesh numbers are rewritten here. A morph on a dropped mesh would move every morph
after it, and clips name morphs by number, so that's refused.
"""
import io
import struct

import numpy as np

import rpack as RP
from rpack import HDR, save_pack


class PackBody:
    def __init__(self, path):
        self.path = path
        self.P = P = RP.Rpack(path)
        self.strs = P.strings()
        self.nodes = P.nodes()
        self.names = [nd["name"] for nd in self.nodes]
        W = [None] * len(self.nodes)
        for i, nd in enumerate(self.nodes):
            L = RP._m4_trs(nd["t"], nd["q"], nd["s"])
            W[i] = L if nd["parent"] < 0 else RP._m4_mul(W[nd["parent"]], L)
        self.W = [list(w) for w in W]                # every node's place at rest, column-major, as the engine has it
        self.node_t = {}           # node number -> where it sits under its parent now (move_nodes)
        self.patches = []          # (offset, bytes) written over the pack's own bytes as they are (scale_track)
        self.meshes = []
        for mi in range(P.nMeshes):
            rec = P.MESH.unpack_from(P.buf, P.offMeshes + mi * 60)
            self.meshes.append(dict(mi=mi, name=self.strs[rec[0]].decode("utf-8", "replace"), verts=rec[2], rec=rec))
        self.tex_of, self.ntex_of = {}, {}
        for i in range(P.nMats):
            mesh, = struct.unpack_from("<I", P.buf, P.offMats + i * 36)
            tex, ntex = struct.unpack_from("<ii", P.buf, P.offMats + i * 36 + 24)
            self.tex_of.setdefault(mesh, tex)        # a mesh takes the first material row naming it
            self.ntex_of.setdefault(mesh, ntex - 1)
        self.new_tex = {}          # texture index -> PIL image to write
        self.new_pos = {}          # mesh index -> the pack's own positions to write
        self.new_nrm = {}
        self.dropped = set()
        self.added = []            # meshes to append: dicts
        self.added_nodes = []      # (name, parent, kind) to append
        self.added_tex = []        # PIL images to append as new textures
        self.sizes = None          # the size block's 12 floats, when they're to change
        self._rest = {}

    # ---------------------------------------------------------------- finding
    def find(self, name, verts=None):
        """the meshes of this name (and size), in the pack's order"""
        got = [m for m in self.meshes if m["name"] == name and (verts is None or m["verts"] == verts)]
        assert got, "no mesh %s%s in %s" % (name, "" if verts is None else " of %d vertices" % verts, self.path)
        return got

    def nth(self, m):
        """which mesh of its name this is, counted from 1: what material.py's <mesh>@N wants"""
        return 1 + sum(1 for q in self.meshes[:m["mi"]] if q["name"] == m["name"])

    def node(self, *names):
        """where the first of these nodes that exists is at rest: (x, y, z)"""
        for n in names:
            if n in self.names:
                return np.array(self.W[self.names.index(n)][12:15])
        raise KeyError("none of %s is a node of %s" % (", ".join(names), self.path))

    # ---------------------------------------------------------------- a mesh as arrays
    def tris(self, m):
        rec = m["rec"]
        return np.frombuffer(self.P.buf, dtype="<u4", count=rec[3], offset=rec[11]).reshape(-1, 3).astype(np.int64)

    def skin(self, m):
        """(palette (bones,4,4), slots (n,4), weights (n,4), nodes (bones,)): each bone's matrix at rest, each vertex's four bones as slots into this mesh's own
        list, and which node each slot is. None for a mesh with no bones"""
        P, rec = self.P, m["rec"]
        n, bc = rec[2], rec[4]
        if not bc:
            return None
        bm = np.array(struct.unpack_from("<%dH" % bc, P.buf, rec[12]), dtype=np.int64)
        pal = np.stack([np.array(RP._m4_mul(self.W[bm[b]], list(struct.unpack_from("<16f", P.buf, rec[13] + b * 64)))).reshape(4, 4).T for b in range(bc)])
        ji = np.frombuffer(P.buf, dtype=np.uint8, count=n * 4, offset=rec[9]).reshape(n, 4).astype(np.int64)
        jw = np.frombuffer(P.buf, dtype=np.uint8, count=n * 4, offset=rec[10]).reshape(n, 4).astype(np.float64) / 255.0
        return pal, np.minimum(ji, bc - 1), jw, bm

    def _blend(self, m):
        """each vertex's own matrix at rest: the 3x3 and the offset its position goes through"""
        n = m["rec"][2]
        bind = np.array(struct.unpack_from("<16f", self.P.buf, m["rec"][14])).reshape(4, 4).T if m["rec"][5] & 4 else np.eye(4)
        s = self.skin(m)
        if s is None:
            M = np.tile(bind, (n, 1, 1))
        else:
            pal, ji, jw, _bm = s
            M = np.einsum("nk,nkij->nij", jw, pal[ji]) @ bind
        return M[:, :3, :3], M[:, :3, 3]

    def rest(self, m):
        """a mesh's vertices at rest in the rig's space, and their UVs: (n,3) and (n,2)"""
        mi = m["mi"]
        if mi not in self._rest:
            P, rec = self.P, m["rec"]
            n = rec[2]
            raw = np.frombuffer(P.buf, dtype="<f4", count=n * 3, offset=rec[6]).reshape(n, 3).astype(np.float64)
            uv = np.frombuffer(P.buf, dtype="<f4", count=n * 2, offset=rec[8]).reshape(n, 2).astype(np.float64) if rec[5] & 2 else np.zeros((n, 2))
            A, t = self._blend(m)
            self._rest[mi] = (np.einsum("nij,nj->ni", A, raw) + t, uv)
        return self._rest[mi]

    def normals(self, m):
        """its normals at rest, in the rig's space"""
        rec = m["rec"]
        n = rec[2]
        if not rec[5] & 1:
            return None
        raw = np.frombuffer(self.P.buf, dtype="<f4", count=n * 3, offset=rec[7]).reshape(n, 3).astype(np.float64)
        A, _t = self._blend(m)
        out = np.einsum("nij,nj->ni", A, raw)
        return out / np.maximum(np.linalg.norm(out, axis=1, keepdims=True), 1e-12)

    def dominant(self, m):
        """the node each vertex rides most: (n,) node numbers, -1 for a mesh with no bones"""
        s = self.skin(m)
        if s is None:
            return np.full(m["rec"][2], -1, dtype=np.int64)
        _pal, ji, jw, bm = s
        return bm[ji[np.arange(len(ji)), jw.argmax(1)]]

    def weight_on(self, m, pred):
        """how much of each vertex rides the bones whose names pred() says yes to: (n,) 0..1"""
        s = self.skin(m)
        if s is None:
            return np.zeros(m["rec"][2])
        _pal, ji, jw, bm = s
        yes = np.array([bool(pred(self.names[b])) for b in bm])
        return (jw * yes[ji]).sum(1)

    # ---------------------------------------------------------------- changing it
    def set_rest(self, m, new, normals=None):
        """the mesh's vertices at rest are to be `new` (n,3), in the rig's space. The pack keeps positions as they are before the skin moves them, so each is
        worked back through its own blend of bones. normals: its normals at rest to match, or None to leave them"""
        A, t = self._blend(m)
        Ai = np.linalg.inv(A)
        self.new_pos[m["mi"]] = np.einsum("nij,nj->ni", Ai, np.asarray(new, dtype=np.float64) - t).astype("<f4")
        uv = self.rest(m)[1]
        self._rest[m["mi"]] = (np.asarray(new, dtype=np.float64).copy(), uv)
        if normals is not None and m["rec"][5] & 1:
            raw = np.einsum("nij,nj->ni", Ai, normals)
            self.new_nrm[m["mi"]] = (raw / np.maximum(np.linalg.norm(raw, axis=1, keepdims=True), 1e-12)).astype("<f4")

    def move_nodes(self, where):
        """nodes at rest somewhere else in the rig's space: {node number: (x, y, z)}. Each keeps the way it's turned and its size, so every clip still plays;
        what changes is where it sits under its parent. A node that isn't named goes with its parent. Meshes don't follow by themselves: a mesh's own
        positions are worked through its bones, so say where its vertices are with set_rest AFTER this"""
        W = [np.array(w, dtype=np.float64).reshape(4, 4).T for w in self.W]
        new = [w.copy() for w in W]
        for i, nd in enumerate(self.nodes):
            p = nd["parent"]
            if i in where:
                new[i][:3, 3] = np.asarray(where[i], dtype=np.float64)
            elif p >= 0:
                new[i] = new[p] @ (np.linalg.inv(W[p]) @ W[i])
            t = np.append(new[i][:3, 3], 1.0) if p < 0 else np.linalg.inv(new[p]) @ np.append(new[i][:3, 3], 1.0)
            self.node_t[i] = tuple(float(v) for v in t[:3])
        self.W = [[float(v) for v in m.T.reshape(-1)] for m in new]
        self._rest.clear()

    def scale_track(self, node, k):
        """every clip's own carrying of a node (its translation track), scaled by k: a body on shorter legs carries its hips lower and strides shorter"""
        P, seen = self.P, set()
        for ci in range(P.nClips):
            _name, _fps, frames, n_tracks, off_tracks = RP.CLIP.unpack_from(P.buf, P.offClips + ci * 20)
            for t in range(n_tracks):
                nd, typ, off = struct.unpack_from("<III", P.buf, off_tracks + t * 12)
                if nd == node and typ == 1 and off not in seen:
                    seen.add(off)
                    a = np.frombuffer(P.buf, dtype="<f4", count=frames * 3, offset=off).astype(np.float64) * k
                    self.patches.append((off, a.astype("<f4").tobytes()))
        return len(seen)

    def image(self, m):
        t = self.tex_of[m["mi"]]
        if t not in self.new_tex:
            o, n = struct.unpack_from("<II", self.P.buf, self.P.offTexTable + t * 16 + 8)
            from PIL import Image
            self.new_tex[t] = Image.open(io.BytesIO(bytes(self.P.buf[o:o + n]))).convert("RGBA")
        return t, self.new_tex[t]

    def put(self, t, im):
        self.new_tex[t] = im

    def drop(self, meshes):
        for m in meshes:
            self.dropped.add(m["mi"])

    def arrays(self, m):
        """a mesh's whole geometry at rest, as arrays a tool can cut up and hand back to add_mesh: positions, normals, UVs, the four nodes each vertex
        rides (node numbers, not slots), their weights, and the triangles"""
        pos, uv = self.rest(m)
        nrm = self.normals(m)
        s = self.skin(m)
        n = len(pos)
        if s is None:
            nodes, w = np.full((n, 4), m["rec"][1], dtype=np.int64), np.tile([1.0, 0.0, 0.0, 0.0], (n, 1))
        else:
            nodes, w = s[3][s[1]], s[2]
        return pos.copy(), (nrm if nrm is not None else np.tile([0.0, 1.0, 0.0], (n, 1))).copy(), uv.copy(), nodes, w.copy(), self.tris(m)

    def add_node(self, name, parent=-1, kind=0):
        """a new node at rest on its parent: a switch the engine reads by name (VertexCollide, TrueScale), or a bone to hang something on. Its number"""
        if name in self.names:
            return self.names.index(name)
        self.added_nodes.append((name, parent, kind))
        return len(self.nodes) + len(self.added_nodes) - 1

    def has_morphs(self, m):
        """does this mesh carry shapes (a face's): a tool mustn't cut it up, because a shape names its vertices by number"""
        off = HDR.unpack_from(self.P.buf, 4)[14]
        if not off:
            return False
        n, = struct.unpack_from("<I", self.P.buf, off)
        return any(struct.unpack_from("<I", self.P.buf, off + 4 + i * 16)[0] == m["mi"] for i in range(n))

    def add_texture(self, im):
        """a new texture in the pack: the number it will have"""
        self.added_tex.append(im)
        return self.P.nTex + len(self.added_tex) - 1

    def add_mesh(self, name, like, pos, nrm, uv, nodes, weights, tris, tex=None, blend=False):
        """a new mesh in the pack. like: a mesh of this pack whose node it hangs on (and whose texture it takes when tex is None; tex -1 is none at all).
        pos and nrm at rest in the rig's space, nodes (n,4) the node numbers each vertex rides and weights (n,4) how much"""
        assert len(tris), "%s has no triangles" % name
        self.added.append(dict(name=name, node=like["rec"][1], pos=np.asarray(pos, dtype=np.float64), nrm=np.asarray(nrm, dtype=np.float64), uv=np.asarray(uv, dtype=np.float64),
                               nodes=np.asarray(nodes, dtype=np.int64), w=np.asarray(weights, dtype=np.float64), tris=np.asarray(tris, dtype=np.int64),
                               tex=self.tex_of.get(like["mi"], -1) if tex is None else tex, blend=blend))

    def size_block(self):
        return list(struct.unpack_from("<12f", self.P.buf, self.P.offSize))

    # ---------------------------------------------------------------- writing it
    def write(self):
        """everything changed, written, and the pack saved with its chunks"""
        P = self.P
        o = bytearray(P.buf)

        def place(b):
            while len(o) % 4:
                o.append(0)
            off = len(o)
            o.extend(b)
            return off
        for mi, raw in self.new_pos.items():
            o[self.meshes[mi]["rec"][6]:self.meshes[mi]["rec"][6] + raw.size * 4] = raw.tobytes()
        for i, t in self.node_t.items():
            struct.pack_into("<3f", o, P.offNodes + i * 52 + 12, *t)
        for off, b in self.patches:
            o[off:off + len(b)] = b
        for mi, raw in self.new_nrm.items():
            o[self.meshes[mi]["rec"][7]:self.meshes[mi]["rec"][7] + raw.size * 4] = raw.tobytes()
        for t, im in sorted(self.new_tex.items()):
            buf = io.BytesIO()
            im.save(buf, format="PNG")
            blob = buf.getvalue()
            struct.pack_into("<II", o, P.offTexTable + t * 16 + 8, place(blob), len(blob))
        h = list(HDR.unpack_from(o, 4))
        if self.sizes is not None:
            h[12] = place(struct.pack("<12f", *self.sizes))
        if self.added_tex:
            table = bytearray(o[P.offTexTable:P.offTexTable + P.nTex * 16])
            for im in self.added_tex:
                buf = io.BytesIO()
                im.save(buf, format="PNG")
                blob = buf.getvalue()
                table += struct.pack("<4I", 0, 0, place(blob), len(blob))
            h[10], h[5] = place(bytes(table)), P.nTex + len(self.added_tex)
        strs = list(self.strs)
        if self.added_nodes:
            rows = bytes(o[P.offNodes:P.offNodes + P.nNodes * 52])
            for name, parent, kind in self.added_nodes:
                strs.append(name.encode("utf-8"))
                rows += struct.pack("<IiI3f4f3f", len(strs) - 1, parent, kind, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0)
            h[6], h[1] = place(rows), P.nNodes + len(self.added_nodes)
        if self.dropped or self.added:
            keep = [mi for mi in range(P.nMeshes) if mi not in self.dropped]
            to = {old: new for new, old in enumerate(keep)}
            mats = b""
            for i in range(P.nMats):
                row = bytearray(o[P.offMats + i * 36:P.offMats + (i + 1) * 36])
                mesh, = struct.unpack_from("<I", row, 0)
                if mesh in to:
                    struct.pack_into("<I", row, 0, to[mesh])
                    mats += bytes(row)
            if h[14]:
                n, = struct.unpack_from("<I", o, h[14])
                rows = [list(struct.unpack_from("<4I", o, h[14] + 4 + i * 16)) for i in range(n)]
                gone = [r for r in rows if r[0] not in to]
                assert not gone, "%d morphs are on meshes being dropped: clips name morphs by number, so they can't go" % len(gone)
                h[14] = place(struct.pack("<I", n) + b"".join(struct.pack("<4I", to[r[0]], r[1], r[2], r[3]) for r in rows))
            recs = [bytes(o[P.offMeshes + mi * 60:P.offMeshes + (mi + 1) * 60]) for mi in keep]
            for g in self.added:
                n = len(g["pos"])
                used = np.unique(g["nodes"][g["w"] > 0])
                assert len(used) <= 256, "%s rides %d bones, and a mesh takes 256" % (g["name"], len(used))
                slot = np.zeros(len(self.nodes), dtype=np.int64)
                slot[used] = np.arange(len(used))
                w8 = np.floor(g["w"] / np.maximum(g["w"].sum(1, keepdims=True), 1e-12) * 255.0 + 0.5).astype(np.int64)
                big = w8.argmax(1)
                w8[np.arange(n), big] += 255 - w8.sum(1)                # a vertex's weights add to 255 exactly
                j8 = np.where(w8 > 0, slot[g["nodes"]], 0)
                ibm = b"".join(np.linalg.inv(np.array(self.W[b]).reshape(4, 4).T).T.astype("<f4").tobytes() for b in used)
                strs.append(g["name"].encode("utf-8"))
                rec = [len(strs) - 1, g["node"], n, g["tris"].size, len(used), 1 | 2 | 4 | (8 if g["blend"] else 0),
                       place(g["pos"].astype("<f4").tobytes()), place(g["nrm"].astype("<f4").tobytes()), place(g["uv"].astype("<f4").tobytes()),
                       place(j8.astype(np.uint8).tobytes()), place(np.clip(w8, 0, 255).astype(np.uint8).tobytes()), place(g["tris"].astype("<u4").tobytes()),
                       place(used.astype("<u2").tobytes()), place(ibm), place(np.eye(4).astype("<f4").tobytes())]
                mats += struct.pack("<6I", len(recs), 0, 0, 0, 0, 0) + struct.pack("<ii", g["tex"], 0) + struct.pack("<I", 0)
                recs.append(struct.pack("<15I", *rec))
            h[7] = place(b"".join(recs))
            h[8] = place(mats)
            h[2], h[3] = len(recs), len(mats) // 36
        if len(strs) > len(self.strs):
            table, so = struct.pack("<I", len(strs)), 0
            for t in strs:
                table += struct.pack("<II", so, len(t))
                so += len(t)
            h[11] = place(table + b"".join(strs))
        h[13] = len(o)
        HDR.pack_into(o, 4, *h)
        save_pack(self.path, bytes(o))


def weld(pos, eps=1e-5):
    """which vertices share a place: (n,) a number per place. A mesh is cut along its texture's seams, and both sides of a cut are one point of the surface"""
    q = np.round(np.asarray(pos) / eps).astype(np.int64)
    _u, inv = np.unique(q, axis=0, return_inverse=True)
    return inv.reshape(-1)


def smooth_normals(pos, tris, ids=None):
    """a normal per vertex from the triangles round it, by area, the same on both sides of a seam"""
    ids = weld(pos) if ids is None else ids
    fn = np.cross(pos[tris[:, 1]] - pos[tris[:, 0]], pos[tris[:, 2]] - pos[tris[:, 0]])
    acc = np.zeros((ids.max() + 1, 3))
    for k in range(3):
        np.add.at(acc, ids[tris[:, k]], fn)
    n = acc[ids]
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
