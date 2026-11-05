#!/usr/bin/env python3
"""rpackchunks.py -- what's authored on a body, kept in chunks after it, so a new bake can't lose it (engine plan, queue step 3; design/pack_format.md).

A bake starts over: it writes a new body from a glb, and whatever was authored onto the old pack went with it. That cost us the whole cast's faces once. Now a bake keeps
whatever the old pack had and the new one hasn't. Clips, morphs and marks go in chunks after the new body, named rather than numbered, so they don't care where a bone moved to.
The engine and Rpack fill in at load whatever the body hasn't got: a name in the body beats a name in a chunk. Old bytes never move, and an engine that doesn't know a tag skips it.

  CLIP  clips, each track naming its node, or for a type 3 track its morph. Names aren't unique: a rig can have two nodes of a name, and a morph's name can be on several meshes or twice on
        one. So a track says which it means: `name`, or `name<tab>1` for the second of that name, and a morph is always `mesh<tab>name<tab>which`. Two meshes can share a name and a size
        as well, so a mesh is `name`, or `name<ctrl-A>1` for the second
  MRPH  morphs, each naming its mesh and how many vertices that mesh has
  FACE  marks: nodes nothing's skinned to, such as face_mouth
  TOOL  lines saying which tools to run again after a bake: what they made can't be carried, because it's in the mesh (a jaw's weights, the inside of a mouth)

usage:
  rpackchunks.py list PACK               what's in its chunks
  rpackchunks.py sum PACK                a hash of what the pack means once it's merged. REVA_PACKSUM=1 makes the engine print the same one
  rpackchunks.py tochunks SRC DST        DST holds SRC's clips, morphs and trailing marks in chunks and not in its body: the same pack, the long way round. For testing the reader
  rpackchunks.py carry OLD NEW           what a bake does: keep in NEW what OLD had and NEW hasn't
"""
import os, shutil, struct, subprocess, sys, zlib

HDR = struct.Struct("<15I")
NODE = struct.Struct("<IiI3f4f3f")
CLIPREC = struct.Struct("<IfIII")
TRACK = struct.Struct("<III")
MORPH = struct.Struct("<IIII")
AUTHORED = (b"FACE", b"MRPH", b"CLIP")
NO_PARENT = 0xffffffff
COMP = {0: 4, 1: 3, 2: 3, 3: 1}
# nodes that are switches: the bake decides those, so taking one off the glb takes it off the body
SWITCHES = ("ToonStyle", "ToonSet", "Glow", "NoCast", "Open", "MirrorOnly", "Floor", "TrueScale", "VertexCollide")


def is_switch(name):
    return name in SWITCHES or (name.startswith("Alpha") and name[5:].isdigit())


# ---- the file ---------------------------------------------------------------------------
def split(data):
    """a pack's bytes: (the old body, [(tag, where it was, its bytes)]). A broken footer reads as no chunks, and packdump says why."""
    data = bytes(data)
    if len(data) < 80 or data[-4:] != b"RPK2":
        return data, []
    dirOff, nDir, size = struct.unpack_from("<III", data, len(data) - 16)
    if size != len(data) or dirOff + nDir * 16 != len(data) - 16:
        return data, []
    chunks = []
    for i in range(nDir):
        tag, off, n, _crc = struct.unpack_from("<4sIII", data, dirOff + i * 16)
        chunks.append((tag, off, data[off:off + n]))
    return data[:HDR.unpack_from(data, 4)[13]], chunks


def join(body, chunks):
    """an old body and its chunks as a file. chunks: (tag, where it was or None, its bytes). TEXC's offsets count from the start of the file, so they move with it."""
    out = bytearray(body)
    ents = []
    for tag, was, data in chunks:
        while len(out) % 16:
            out.append(0)
        off = len(out)
        if tag == b"TEXC" and was is not None and off != was:
            data = bytearray(data)
            for i in range(struct.unpack_from("<I", data, 0)[0]):
                o = struct.unpack_from("<I", data, 4 + i * 32 + 24)[0]
                struct.pack_into("<I", data, 4 + i * 32 + 24, o + off - was)
            data = bytes(data)
        out += data
        ents.append(struct.pack("<4sIII", tag, off, len(data), zlib.crc32(data) & 0xffffffff))
    while len(out) % 16:
        out.append(0)
    d = len(out)
    out += b"".join(ents)
    out += struct.pack("<III4s", d, len(ents), len(out) + 16, b"RPK2")
    return bytes(out)


class Blob:
    """a chunk being written: a table up front, then names and numbers. Every offset counts from the chunk's start, and everything sits on a 4-byte line"""

    def __init__(self, head):
        self.b = bytearray(head)

    def data(self, d):
        while len(self.b) % 4:
            self.b.append(0)
        off = len(self.b)
        self.b += d
        return off

    def name(self, s):
        s = s.encode("utf-8") if isinstance(s, str) else s
        return self.data(struct.pack("<I", len(s)) + s)


def _name(c, at):
    n = struct.unpack_from("<I", c, at)[0]
    return c[at + 4:at + 4 + n].decode("utf-8", "replace")


# ---- what's in a chunk ------------------------------------------------------------------
def clips_of(c):
    """CLIP: [(name, fps, frames, [(track's name, type, its floats as bytes)])]"""
    out = []
    for i in range(struct.unpack_from("<I", c, 0)[0]):
        nm, fps, frames, nT, oT = CLIPREC.unpack_from(c, 4 + i * 20)
        tracks = []
        for t in range(nT):
            tn, typ, off = TRACK.unpack_from(c, oT + t * 12)
            tracks.append((_name(c, tn), typ, c[off:off + frames * COMP[typ] * 4]))
        out.append((_name(c, nm), fps, frames, tracks))
    return out


def clips_chunk(clips):
    b = Blob(b"\0" * (4 + len(clips) * 20))
    struct.pack_into("<I", b.b, 0, len(clips))
    for i, (name, fps, frames, tracks) in enumerate(clips):
        rows = [(b.name(tn), typ, b.data(data)) for tn, typ, data in tracks]
        oT = b.data(b"".join(TRACK.pack(*r) for r in rows))
        CLIPREC.pack_into(b.b, 4 + i * 20, b.name(name), fps, frames, len(rows), oT)
    return bytes(b.b)


def morphs_of(c):
    """MRPH: [(name, the mesh's name, how many vertices that mesh has, the sparse entries as bytes)]"""
    out = []
    for i in range(struct.unpack_from("<I", c, 0)[0]):
        nm, mn, verts, n, off = struct.unpack_from("<5I", c, 4 + i * 20)
        out.append((_name(c, nm), _name(c, mn), verts, c[off:off + n * 16]))
    return out


def morphs_chunk(morphs):
    b = Blob(b"\0" * (4 + len(morphs) * 20))
    struct.pack_into("<I", b.b, 0, len(morphs))
    for i, (name, mesh, verts, data) in enumerate(morphs):
        struct.pack_into("<5I", b.b, 4 + i * 20, b.name(name), b.name(mesh), verts, len(data) // 16, b.data(data))
    return bytes(b.b)


def faces_of(c):
    """FACE: [(name, the parent's name or None, kind, the 10 floats of its rest pose)]"""
    out = []
    for i in range(struct.unpack_from("<I", c, 0)[0]):
        nm, par, kind = struct.unpack_from("<III", c, 4 + i * 52)
        out.append((_name(c, nm), None if par == NO_PARENT else _name(c, par), kind, struct.unpack_from("<10f", c, 4 + i * 52 + 12)))
    return out


def faces_chunk(nodes):
    b = Blob(b"\0" * (4 + len(nodes) * 52))
    struct.pack_into("<I", b.b, 0, len(nodes))
    for i, (name, parent, kind, trs) in enumerate(nodes):
        struct.pack_into("<III10f", b.b, 4 + i * 52, b.name(name), NO_PARENT if parent is None else b.name(parent), kind, *trs)
    return bytes(b.b)


# ---- a body, read ------------------------------------------------------------------------
class Img:
    """an old body: what its tables say, by name"""

    def __init__(self, b):
        self.b = b
        h = self.h = list(HDR.unpack_from(b, 4))
        self.nNodes, self.nMeshes, self.nClips = h[1], h[2], h[4]
        n = struct.unpack_from("<I", b, h[11])[0]
        blob = h[11] + 4 + n * 8
        self.raw = [b[blob + o:blob + o + l] for o, l in (struct.unpack_from("<II", b, h[11] + 4 + i * 8) for i in range(n))]
        self.strs = [s.decode("utf-8", "replace") for s in self.raw]
        self.nodes = [NODE.unpack_from(b, h[6] + i * 52) for i in range(h[1])]
        self.names = [self.strs[nd[0]] for nd in self.nodes]
        self.meshes = [struct.unpack_from("<15I", b, h[7] + i * 60) for i in range(h[2])]
        self.morphs = []
        if h[14]:
            for i in range(struct.unpack_from("<I", b, h[14])[0]):
                self.morphs.append(MORPH.unpack_from(b, h[14] + 4 + i * 16))

    def mesh_name(self, i):
        return self.strs[self.meshes[i][0]]

    def morph_name(self, i):
        return self.strs[self.morphs[i][1]]

    def clips(self):
        """[(name, fps, frames, [(node or morph slot, type, its floats as bytes)])]"""
        out = []
        for i in range(self.nClips):
            nm, fps, frames, nT, oT = CLIPREC.unpack_from(self.b, self.h[9] + i * 20)
            tracks = []
            for t in range(nT):
                node, typ, off = TRACK.unpack_from(self.b, oT + t * 12)
                tracks.append((node, typ, self.b[off:off + frames * COMP[typ] * 4]))
            out.append((self.strs[nm], fps, frames, tracks))
        return out

    def skinned(self):
        """which nodes a mesh's bone map names"""
        out = set()
        for m in self.meshes:
            if m[4]:
                out.update(struct.unpack_from("<%dH" % m[4], self.b, m[12]))
        return out

    def node_refs(self):
        """what a track calls each node"""
        return refs_of(self.names)

    def mesh_refs(self):
        """what a chunk calls each mesh"""
        return refs_of([self.mesh_name(i) for i in range(self.nMeshes)], "\x01")

    def morph_refs(self):
        """what a track calls each morph"""
        ms = self.mesh_refs()
        return morph_refs_of([(ms[m[0]], self.strs[m[1]]) for m in self.morphs])


def refs_of(names, sep="\t"):
    seen, out = {}, []
    for n in names:
        k = seen.get(n, 0)
        seen[n] = k + 1
        out.append(n if not k else "%s%s%d" % (n, sep, k))
    return out


def morph_refs_of(pairs):
    seen, out = {}, []
    for mesh, name in pairs:
        k = seen.get((mesh, name), 0)
        seen[(mesh, name)] = k + 1
        out.append("%s\t%s\t%d" % (mesh, name, k))
    return out


def merged(data):
    """a pack as the engine sees it once it's loaded: what the body hasn't got, added from the chunks by appending and repointing, as packChunksMerge does in the engine. The result has no chunks"""
    body, chunks = split(data)
    got = {t: c for t, _w, c in chunks}
    if not any(t in got for t in AUTHORED):
        return body if chunks else bytes(data)
    img = Img(body)
    h, out, raw = list(img.h), bytearray(body), list(img.raw)

    def place(d):
        while len(out) % 4:
            out.append(0)
        off = len(out)
        out.extend(d)
        return off

    def string(s):
        raw.append(s.encode("utf-8"))
        return len(raw) - 1

    rows = [body[h[6] + i * 52:h[6] + (i + 1) * 52] for i in range(h[1])]
    allNames = list(img.names)
    names = {}
    for i, n in enumerate(allNames):
        names.setdefault(n, i)
    if b"FACE" in got:
        for name, parent, kind, trs in faces_of(got[b"FACE"]):
            if name in names or (parent is not None and parent not in names):
                continue
            names[name] = len(rows)
            allNames.append(name)
            rows.append(NODE.pack(string(name), -1 if parent is None else names[parent], kind, *trs))
    nodeRef = {r: i for i, r in enumerate(refs_of(allNames))}
    h[1] = len(rows)
    h[6] = place(b"".join(rows))
    morphs = list(img.morphs)
    bodyMorphs = {(m[0], img.strs[m[1]]) for m in img.morphs}
    meshRefs = img.mesh_refs()
    pairs = [(meshRefs[m[0]], img.strs[m[1]]) for m in img.morphs]
    if b"MRPH" in got:
        for name, mesh, verts, data in morphs_of(got[b"MRPH"]):
            mi = meshRefs.index(mesh) if mesh in meshRefs else -1
            if mi < 0 or img.meshes[mi][2] != verts or (mi, name) in bodyMorphs:
                continue
            pairs.append((mesh, name))
            morphs.append((mi, string(name), len(data) // 16, place(data)))
        h[14] = place(struct.pack("<I", len(morphs)) + b"".join(MORPH.pack(*m) for m in morphs))
    slot = {r: i for i, r in enumerate(morph_refs_of(pairs))}
    recs = [body[h[9] + i * 20:h[9] + (i + 1) * 20] for i in range(h[4])]
    if b"CLIP" in got:
        have = {img.strs[CLIPREC.unpack_from(body, img.h[9] + i * 20)[0]] for i in range(h[4])}
        for name, fps, frames, tracks in clips_of(got[b"CLIP"]):
            if name in have:
                continue
            tr = []
            for tn, typ, d in tracks:
                idx = slot.get(tn) if typ == 3 else nodeRef.get(tn)
                if idx is not None:
                    tr.append(TRACK.pack(idx, typ, place(d)))
            if not tr and tracks:
                continue
            recs.append(CLIPREC.pack(string(name), fps, frames, len(tr), place(b"".join(tr))))
        h[4] = len(recs)
        h[9] = place(b"".join(recs))
    tbl = bytearray(struct.pack("<I", len(raw)))
    so = 0
    for s in raw:
        tbl += struct.pack("<II", so, len(s))
        so += len(s)
    tbl += b"".join(raw)
    h[11] = place(tbl)
    h[13] = len(out)
    HDR.pack_into(out, 4, *h)
    return bytes(out)


def fnv(h, d):
    for c in d:
        h = ((h ^ c) * 1099511628211) & 0xffffffffffffffff
    return h


def pack_sum(data):
    """what a pack means once it's merged: its nodes, clips and morphs in order. The engine's packSum is the same sum, so the two readers can be held against each other"""
    img = Img(merged(data))
    h = 14695981039346656037
    for nd, name in zip(img.nodes, img.names):
        h = fnv(h, name.encode("utf-8"))
        h = fnv(h, NODE.pack(0, *nd[1:]))
    for name, fps, frames, tracks in img.clips():
        h = fnv(h, name.encode("utf-8"))
        h = fnv(h, struct.pack("<fII", fps, frames, len(tracks)))
        for node, typ, d in tracks:
            h = fnv(h, struct.pack("<II", node, typ))
            h = fnv(h, d)
    for mi, nm, n, off in img.morphs:
        h = fnv(h, img.strs[nm].encode("utf-8"))
        h = fnv(h, struct.pack("<II", mi, n))
        h = fnv(h, img.b[off:off + n * 16])
    return img.nNodes, img.nClips, len(img.morphs), h


# ---- a bake ------------------------------------------------------------------------------
def lift(old, new):
    """what the old pack has and the new one hasn't: (chunks, what's kept, what can't be)"""
    lost, keep = [], {"clips": 0, "morphs": 0, "marks": 0}
    skinned = old.skinned()
    have = set(new.names)
    oldRefs, newRefs = old.node_refs(), set(new.node_refs())
    faces = []
    for i, nd in enumerate(old.nodes):
        name = old.names[i]
        if name in have or nd[2] == 2 or is_switch(name):   # a mesh's node goes with its mesh
            continue
        if i in skinned:
            lost.append("the bone %s, which a mesh is skinned to" % name)
            continue
        parent = old.names[nd[1]] if nd[1] >= 0 else None
        if parent is not None and parent not in have:
            lost.append("the mark %s, because its parent %s has gone" % (name, parent))
            continue
        faces.append((name, parent, nd[2], nd[3:]))
        have.add(name)
        newRefs.add(oldRefs[i])
    oldMeshRefs, newMeshRefs = old.mesh_refs(), new.mesh_refs()
    newMeshes = {r: i for i, r in enumerate(newMeshRefs)}
    for i in range(old.nMeshes):
        if oldMeshRefs[i] not in newMeshes:
            lost.append("the mesh %s" % old.mesh_name(i))
    newMorphs = {(newMeshRefs[m[0]], new.morph_name(j)) for j, m in enumerate(new.morphs)}
    morphs = []
    oldMorphRefs = old.morph_refs()
    for j, (mi, nm, n, off) in enumerate(old.morphs):
        name, mesh = old.strs[nm], oldMeshRefs[mi]
        if (mesh, name) in newMorphs:
            continue
        if mesh not in newMeshes:
            lost.append("the morph %s, because its mesh %s has gone" % (name, mesh.replace("\x01", " number ")))
            continue
        data = old.b[off:off + n * 16]
        a, b = old.meshes[mi], new.meshes[newMeshes[mesh]]
        pa, pb = old.b[a[6]:a[6] + a[2] * 12], new.b[b[6]:b[6] + b[2] * 12]
        if pa != pb:   # the mesh changed. A vertex that's still in the same place keeps its delta; if any isn't there, this morph doesn't fit any more
            where = {}
            for v in range(b[2]):
                where.setdefault(pb[v * 12:v * 12 + 12], []).append(v)
            mapped = []
            for k in range(n):
                v, dx, dy, dz = struct.unpack_from("<I3f", data, k * 16)
                for w in where.get(pa[v * 12:v * 12 + 12], []):
                    mapped.append((w, dx, dy, dz))
                if pa[v * 12:v * 12 + 12] not in where:
                    mapped = None
                    break
            if mapped is None:
                lost.append("the morph %s, because mesh %s changed under it" % (name, mesh.replace("\x01", " number ")))
                continue
            data = b"".join(struct.pack("<I3f", *r) for r in mapped)
        morphs.append((name, mesh, b[2], data))
    morphsThere = set(new.morph_refs()) | set(morph_refs_of([(m[1], m[0]) for m in morphs]))
    clips = []
    newClips = {c[0] for c in new.clips()}
    dropped = 0
    for name, fps, frames, tracks in old.clips():
        if name in newClips:
            continue
        tr = []
        for idx, typ, d in tracks:
            tn = (oldMorphRefs[idx] if idx < len(oldMorphRefs) else None) if typ == 3 else oldRefs[idx]
            if tn is None or (typ == 3 and tn not in morphsThere) or (typ != 3 and tn not in newRefs):
                dropped += 1
                continue
            tr.append((tn, typ, d))
        if tracks and not tr:
            lost.append("the clip %s: none of what it moves is here" % name)
            continue
        clips.append((name, fps, frames, tr))
    if dropped:
        lost.append("%d clip tracks moving things this body hasn't got" % dropped)
    keep["clips"], keep["morphs"], keep["marks"] = len(clips), len(morphs), len(faces)
    chunks = []
    if faces:
        chunks.append((b"FACE", None, faces_chunk(faces)))
    if morphs:
        chunks.append((b"MRPH", None, morphs_chunk(morphs)))
    if clips:
        chunks.append((b"CLIP", None, clips_chunk(clips)))
    return chunks, keep, lost


def tools_of(data):
    for tag, _w, c in split(data)[1]:
        if tag == b"TOOL":
            return [l for l in c.decode("utf-8", "replace").splitlines() if l.strip()]
    return []


def carry(path, old_data, report=print):
    """path has just been baked over old_data: keep in it what old_data had and it hasn't. Nothing to keep leaves the file exactly as the bake wrote it"""
    body = open(path, "rb").read()
    chunks, keep, lost = lift(Img(merged(old_data)), Img(merged(body)))
    # what each mesh is made of is said by mesh name, so it comes across as it is: a record whose mesh has gone finds nothing, and does no harm
    matl = next((c for t, _w, c in split(old_data)[1] if t == b"MATL"), None)
    if matl is not None and not any(t == b"MATL" for t, _w, _c in split(body)[1]):
        chunks.append((b"MATL", None, matl))
        keep["materials"] = struct.unpack_from("<I", matl, 0)[0] if len(matl) >= 4 else 0
    tools = tools_of(old_data)
    if tools:   # what a tool made comes back when the tool runs again
        chunks.append((b"TOOL", None, ("\n".join(tools) + "\n").encode("utf-8")))
    # the file work first: nothing about printing may leave a bake half done
    before = path + ".before_bake"
    if lost:
        open(before, "wb").write(old_data)
    if chunks:
        split_body, existing = split(body)
        open(path, "wb").write(join(split_body, existing + chunks))
        report("[carry] kept from the old pack: %d clips, %d morphs, %d marks%s" % (keep["clips"], keep["morphs"], keep["marks"], ", and what %d meshes are made of" % keep["materials"] if keep.get("materials") else ""))
    here = os.path.dirname(os.path.abspath(__file__))
    for line in tools:
        parts = line.split("\t")
        report("[carry] running %s again" % parts[0])
        r = subprocess.run([sys.executable, os.path.join(here, "author", parts[0]), path] + parts[1:], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
        if r.returncode:
            report("[carry] %s FAILED: %s" % (parts[0], r.stdout[-400:]))
    if lost and tools:   # what the tools made isn't lost after all
        now = Img(merged(open(path, "rb").read()))
        meshes = {now.mesh_name(i) for i in range(now.nMeshes)}
        lost = [l for l in lost if not ((l.startswith("the bone ") and l.split()[2].rstrip(",") in now.names) or (l.startswith("the mesh ") and l[9:] in meshes))]
        if not lost and os.path.exists(before):
            os.remove(before)
    if lost:
        report("[carry] NOT KEPT, and the old pack is at %s:" % before)
        for l in lost[:24]:
            report("    " + l)
        if len(lost) > 24:
            report("    and %d more" % (len(lost) - 24))
        if not tools and any(l.startswith("the bone") or l.startswith("the mesh") for l in lost):
            report("    a jaw's weights and the inside of a mouth come from skate_src/author/jaw.py and mouth_interior.py, which can be run again on this pack")
    return keep, lost


def bake_write(dst, out, fresh=False):
    """a baker's last step. A bake keeps what was authored on the pack it replaces, unless it's told to start over"""
    old = None
    if not fresh and os.path.exists(dst):
        old = open(dst, "rb").read()
        if old[:4] != b"RPK1":
            old = None
    open(dst, "wb").write(out)
    if old:
        carry(dst, old)


# ---- tools that rewrite a body -----------------------------------------------------------
def save(path, out, like=None, forget=()):
    """a tool's new body, written with the chunks the pack already had. What's now in the body comes out of the chunks, and so does what the tool dropped (forget: names). like: read the chunks from here, for a tool that writes a temporary file first"""
    src = like or path
    chunks = split(open(src, "rb").read())[1] if os.path.exists(src) else []
    if not chunks:
        open(path, "wb").write(out)
        return
    img = Img(bytes(out))
    bodyClips = {c[0] for c in img.clips()}
    bodyMorphs = {img.morph_name(j) for j in range(len(img.morphs))}
    keep = []
    for tag, was, c in chunks:
        if tag == b"CLIP":
            rows = [r for r in clips_of(c) if r[0] not in bodyClips and r[0] not in forget]
            c = clips_chunk(rows) if rows else None
        elif tag == b"MRPH":
            rows = [r for r in morphs_of(c) if r[0] not in bodyMorphs and r[0] not in forget]
            c = morphs_chunk(rows) if rows else None
        elif tag == b"FACE":
            rows = [r for r in faces_of(c) if r[0] not in img.names]
            c = faces_chunk(rows) if rows else None
        if c is not None:
            keep.append((tag, was, c))
    body = bytearray(out)
    h = list(HDR.unpack_from(body, 4))
    h[13] = len(body)
    HDR.pack_into(body, 4, *h)
    open(path, "wb").write(join(bytes(body), keep))


def note_tool(path, tool, args=()):
    """this pack's body now holds something this tool made that a bake can't carry: run it again after a bake"""
    data = open(path, "rb").read()
    body, chunks = split(data)
    lines = tools_of(data)
    line = "\t".join([tool] + [str(a) for a in args])
    lines = [l for l in lines if l.split("\t")[0] != tool] + [line]
    chunks = [c for c in chunks if c[0] != b"TOOL"] + [(b"TOOL", None, ("\n".join(lines) + "\n").encode("utf-8"))]
    open(path, "wb").write(join(body, chunks))


def add_clips(path, clips):
    """clips = [(name, fps, frames, {(node, type): floats or bytes})] onto a pack that has chunks, in a chunk, so its body and whatever's compressed stay exactly as they are. node is the merged pack's node or morph slot"""
    data = open(path, "rb").read()
    body, chunks = split(data)
    img = Img(merged(data))
    nodeRefs, morphRefs = img.node_refs(), img.morph_refs()
    rows = []
    for name, fps, frames, tracks in clips:
        tr = []
        for (node, typ), d in sorted(tracks.items()):
            if not isinstance(d, (bytes, bytearray)):
                d = struct.pack("<%df" % len(d), *d)
            if len(d) != frames * COMP[typ] * 4:
                raise ValueError("clip '%s' node %d type %d: %d bytes isn't %d frames" % (name, node, typ, len(d), frames))
            tr.append((morphRefs[node] if typ == 3 else nodeRefs[node], typ, bytes(d)))
        rows.append((name, float(fps), frames, tr))
    old = next((c for c in chunks if c[0] == b"CLIP"), None)
    rows = (clips_of(old[2]) if old else []) + rows
    chunks = [c for c in chunks if c[0] != b"CLIP"] + [(b"CLIP", None, clips_chunk(rows))]
    open(path, "wb").write(join(body, chunks))


# ---- a test of the reader, the long way round --------------------------------------------
def tochunks(src, dst):
    """the same pack with its clips, morphs and trailing marks in chunks and not in its body. Everything keeps its index, so anything that reads it must behave exactly as it did on the original"""
    data = open(src, "rb").read()
    img = Img(merged(data))
    body = bytearray(img.b)
    h = list(img.h)
    skinned = img.skinned()
    meshNodes = {m[1] for m in img.meshes}
    n = img.nNodes
    moved = []
    while n > 0:
        i = n - 1
        nd = img.nodes[i]
        if i in skinned or i in meshNodes or nd[2] == 2 or is_switch(img.names[i]) or not img.names[i].startswith("face_"):
            break
        moved.insert(0, (img.names[i], img.names[nd[1]] if nd[1] >= 0 else None, nd[2], nd[3:]))
        n -= 1
    meshRefs = img.mesh_refs()
    morphs = [(img.strs[nm], meshRefs[mi], img.meshes[mi][2], img.b[off:off + c * 16]) for mi, nm, c, off in img.morphs]
    clips = []
    nodeRefs, morphRefs = img.node_refs(), img.morph_refs()
    for name, fps, frames, tracks in img.clips():
        clips.append((name, fps, frames, [(morphRefs[idx] if typ == 3 else nodeRefs[idx], typ, d) for idx, typ, d in tracks]))
    h[1], h[4], h[14] = n, 0, 0
    HDR.pack_into(body, 4, *h)
    chunks = [(t, w, c) for t, w, c in split(data)[1] if t not in AUTHORED]
    if moved:
        chunks.append((b"FACE", None, faces_chunk(moved)))
    if morphs:
        chunks.append((b"MRPH", None, morphs_chunk(morphs)))
    if clips:
        chunks.append((b"CLIP", None, clips_chunk(clips)))
    open(dst, "wb").write(join(bytes(body), chunks) if chunks else bytes(body))
    return len(clips), len(morphs), len(moved)


def main(a):
    if len(a) < 2:
        print(__doc__)
        return 2
    cmd = a[0]
    if cmd == "list":
        _b, chunks = split(open(a[1], "rb").read())
        for tag, off, c in chunks:
            what = ""
            if tag == b"CLIP":
                what = ", ".join(r[0] for r in clips_of(c)[:12])
            elif tag == b"MRPH":
                what = ", ".join("%s on %s" % (r[0], r[1]) for r in morphs_of(c)[:12])
            elif tag == b"FACE":
                what = ", ".join(r[0] for r in faces_of(c)[:12])
            elif tag == b"TOOL":
                what = c.decode("utf-8", "replace").strip().replace("\n", " | ")
            print("%s at %d, %d bytes  %s" % (tag.decode("latin-1"), off, len(c), what))
        return 0
    if cmd == "sum":
        print("nodes %d clips %d morphs %d %016x" % pack_sum(open(a[1], "rb").read()))
        return 0
    if cmd == "tochunks":
        print("%d clips, %d morphs, %d marks moved into chunks" % tochunks(a[1], a[2]))
        return 0
    if cmd == "carry":
        carry(a[2], open(a[1], "rb").read())
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(main(sys.argv[1:]))
