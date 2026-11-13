"""packdump.py -- a pack as text, and whether it is what design/pack_format.md says a pack is. Two packs' dumps diff line by line, so a change to a pack can be read before anything draws it.
packcheck.py (skate_src/author) checks what a body means: the bind, the hands, the weights. This checks the file: every offset, count and index the loader will trust.

usage: packdump.py PACK                   the whole pack as text (arrays as a crc, not their numbers)
       packdump.py PACK --brief           the header and the counts
       packdump.py --check PACK|DIR ...   nothing but what's wrong; exit 1 if anything is. A folder means every .rpack in it
       packdump.py --selftest PACK        breaks that pack in memory, every way --check claims to catch, and says if one got through
Unaligned data isn't an error, because every pack before the cook has it: --check counts it, since a strict reader must not assume alignment."""
import glob
import os
import struct
import sys
import zlib

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
HDR = struct.Struct("<15I")
NODE = struct.Struct("<IiI3f4f3f")
MESH = struct.Struct("<15I")
CLIP = struct.Struct("<IfIII")
TRACK = struct.Struct("<III")
MORPH = struct.Struct("<4I")
KIND = {0: "plain", 1: "bone", 2: "mesh"}
TRACKTYPE = {0: "rot", 1: "trans", 2: "scale", 3: "weight"}
COMP = {0: 4, 1: 3, 2: 3, 3: 1}


def crc(b):
    return "%08x" % (zlib.crc32(bytes(b)) & 0xffffffff)


def image_info(b):
    """the kind and size of a texture blob, from its own header"""
    if b[:8] == b"\x89PNG\r\n\x1a\n" and len(b) >= 24:
        w, h = struct.unpack(">II", b[16:24])
        return "png", w, h
    if b[:2] == b"\xff\xd8":
        i = 2
        while i + 9 < len(b):
            if b[i] != 0xff:
                i += 1
                continue
            m = b[i + 1]
            if m in (0xc0, 0xc1, 0xc2):
                h, w = struct.unpack(">HH", b[i + 5:i + 9])
                return "jpeg", w, h
            if m in (0xd8, 0x01) or 0xd0 <= m <= 0xd7:
                i += 2
                continue
            i += 2 + struct.unpack(">H", b[i + 2:i + 4])[0]
        return "jpeg", 0, 0
    if b[:2] == b"BM":
        return "bmp", 0, 0
    return "?", 0, 0


class Pack:
    def __init__(self, path, data=None):
        self.path = path
        self.b = data if data is not None else open(path, "rb").read()
        self.err = []
        self.unaligned = 0
        self.lines = []
        self.counts = {}
        self.read()

    def bad(self, msg):
        self.err.append(msg)

    def inside(self, off, size, what):
        """a range the loader will read: is it in the file? """
        if off < 0 or off + size > len(self.b):
            self.bad("%s: %d bytes at %d runs past the end of the file (%d)" % (what, size, off, len(self.b)))
            return False
        return True

    def aligned(self, off):
        if off % 4:
            self.unaligned += 1

    def out(self, s):
        self.lines.append(s)

    def name(self, i):
        if 0 <= i < len(self.strs):
            return self.strs[i].decode("utf-8", "replace")
        self.bad("a name uses string %d of %d" % (i, len(self.strs)))
        return "?%d" % i

    def read(self):
        b = self.b
        if len(b) < 64 or b[:4] != b"RPK1":
            self.bad("not an RPK1 pack (first bytes %r)" % b[:4])
            return
        ver, nN, nM, nMat, nC, nT, oN, oM, oMat, oC, oT, oS, oZ, total, oMorph = HDR.unpack_from(b, 4)
        self.h = dict(ver=ver, nodes=nN, meshes=nM, mats=nMat, clips=nC, tex=nT, total=total)
        self.counts = dict(nodes=nN, meshes=nM, mats=nMat, clips=nC, tex=nT, morphs=0, verts=0, tris=0, keys=0, texbytes=0)
        if ver != 2:
            self.bad("version %d, this reader knows 2" % ver)
        # RPK2: a footer at the very end, or the file ends at the header's total
        self.chunks = []
        dirOff = None
        if len(b) >= 80 and b[-4:] == b"RPK2":
            dirOff, nDir, fsize = struct.unpack_from("<III", b, len(b) - 16)
            if fsize != len(b):
                self.bad("the footer says the file is %d bytes, it's %d" % (fsize, len(b)))
            if not self.inside(dirOff, nDir * 16, "chunk directory") or dirOff + nDir * 16 != len(b) - 16:
                self.bad("the chunk directory doesn't end at the footer")
                nDir = 0
            if total > dirOff:
                self.bad("the old body's total, %d, runs into the directory at %d" % (total, dirOff))
            for i in range(nDir):
                tag, off, size, want = struct.unpack_from("<4sIII", b, dirOff + i * 16)
                name = tag.decode("latin-1")
                if off % 16:
                    self.bad("chunk %s at %d isn't on a 16-byte line" % (name, off))
                if off < total:
                    self.bad("chunk %s at %d is inside the old body, which ends at %d" % (name, off, total))
                if self.inside(off, size, "chunk " + name):
                    if zlib.crc32(b[off:off + size]) & 0xffffffff != want:
                        self.bad("chunk %s's crc is wrong" % name)
                    self.chunks.append((name, off, size))
                    self.out("chunk %s %d bytes crc %08x" % (name, size, want))
        elif total != len(b):
            self.bad("the header's total says %d, the file is %d" % (total, len(b)))

        # the strings first: everything else names things through them
        self.strs = []
        if self.inside(oS, 4, "strings"):
            self.aligned(oS)
            n = struct.unpack_from("<I", b, oS)[0]
            if self.inside(oS + 4, n * 8, "string table"):
                blob = oS + 4 + n * 8
                for i in range(n):
                    so, sl = struct.unpack_from("<II", b, oS + 4 + i * 8)
                    if self.inside(blob + so, sl, "string %d" % i):
                        self.strs.append(b[blob + so:blob + so + sl])
                    else:
                        self.strs.append(b"")

        # nodes: the loader walks them once, so a parent comes before its children
        self.out("nodes %d" % nN)
        self.parents = []
        self.nodeNames = []
        if self.inside(oN, nN * 52, "node table"):
            self.aligned(oN)
            for i in range(nN):
                nm, par, kind, tx, ty, tz, qx, qy, qz, qw, sx, sy, sz = NODE.unpack_from(b, oN + i * 52)
                self.parents.append(par)
                if par >= i:
                    self.bad("node %d's parent is %d: a parent has to come first" % (i, par))
                if par < -1:
                    self.bad("node %d's parent is %d" % (i, par))
                if kind not in KIND:
                    self.bad("node %d has kind %d" % (i, kind))
                self.nodeNames.append(self.name(nm))
                self.out('node %d "%s" parent %d %s t %.5g %.5g %.5g q %.5g %.5g %.5g %.5g s %.5g %.5g %.5g' % (i, self.name(nm), par, KIND.get(kind, kind), tx, ty, tz, qx, qy, qz, qw, sx, sy, sz))

        # meshes
        self.verts = []
        self.meshNames = []
        if self.inside(oM, nM * 60, "mesh table"):
            self.aligned(oM)
            for i in range(nM):
                nm, node, v, ix, bones, fl, oP, oNr, oU, oJ, oW, oI, oBM, oIB, oBd = MESH.unpack_from(b, oM + i * 60)
                what = 'mesh %d "%s"' % (i, self.name(nm))
                self.verts.append(v)
                self.meshNames.append(self.name(nm))
                self.counts["verts"] += v
                self.counts["tris"] += ix // 3
                if node >= nN:
                    self.bad("%s: node %d of %d" % (what, node, nN))
                if ix % 3:
                    self.bad("%s: %d indices is not whole triangles" % (what, ix))
                if fl & ~15:
                    self.bad("%s: flags %#x has bits nothing reads" % (what, fl))
                parts = [("pos", oP, v * 12, True), ("nrm", oNr, v * 12, fl & 1), ("uv", oU, v * 8, fl & 2), ("idx", oI, ix * 4, True)]
                if bones:
                    parts += [("joints", oJ, v * 4, True), ("weights", oW, v * 4, True), ("map", oBM, bones * 2, True), ("ibm", oIB, bones * 64, True)]
                parts += [("bind", oBd, 64, fl & 4)]
                sums = []
                for pn, off, size, want in parts:
                    if not want:
                        continue
                    if not off:
                        self.bad("%s: no %s" % (what, pn))
                        continue
                    if self.inside(off, size, "%s %s" % (what, pn)):
                        if pn not in ("joints", "weights"):
                            self.aligned(off)
                        sums.append("%s %s" % (pn, crc(b[off:off + size])))
                if oI and self.inside(oI, ix * 4, what + " idx") and ix:
                    top = max(struct.unpack_from("<%dI" % ix, b, oI))
                    if top >= v:
                        self.bad("%s: index %d of %d vertices" % (what, top, v))
                if bones and oBM and self.inside(oBM, bones * 2, what + " map"):
                    bm = struct.unpack_from("<%dH" % bones, b, oBM)
                    if max(bm) >= nN:
                        self.bad("%s: bone map names node %d of %d" % (what, max(bm), nN))
                    if oJ and self.inside(oJ, v * 4, what + " joints") and v:
                        top = max(b[oJ:oJ + v * 4])
                        if top >= bones:
                            self.bad("%s: a vertex uses joint %d of %d" % (what, top, bones))
                self.out("%s node %d verts %d tris %d bones %d flags %d %s" % (what, node, v, ix // 3, bones, fl, " ".join(sums)))

        # materials
        if self.inside(oMat, nMat * 36, "material table"):
            self.aligned(oMat)
            for i in range(nMat):
                mesh = struct.unpack_from("<I", b, oMat + i * 36)[0]
                fl = struct.unpack_from("<I", b, oMat + i * 36 + 16)[0]
                tex, ntex = struct.unpack_from("<ii", b, oMat + i * 36 + 24)
                if mesh >= nM:
                    self.bad("material %d is for mesh %d of %d" % (i, mesh, nM))
                if tex >= nT or ntex - 1 >= nT:
                    self.bad("material %d uses texture %d, normal map %d, of %d" % (i, tex, ntex - 1, nT))
                self.out("material %d mesh %d flags %d tex %d normal %d" % (i, mesh, fl, tex, ntex - 1))

        # clips
        if self.inside(oC, nC * 20, "clip table"):
            self.aligned(oC)
            for i in range(nC):
                nm, fps, frames, nTr, oTr = CLIP.unpack_from(b, oC + i * 20)
                what = 'clip %d "%s"' % (i, self.name(nm))
                self.counts["keys"] += frames
                if not fps > 0:
                    self.bad("%s: fps %g" % (what, fps))
                if not self.inside(oTr, nTr * 12, what + " tracks"):
                    continue
                self.aligned(oTr)
                c = zlib.crc32(b"")
                for t in range(nTr):
                    node, typ, off = TRACK.unpack_from(b, oTr + t * 12)
                    if typ not in COMP:
                        self.bad("%s: track %d has type %d" % (what, t, typ))
                        continue
                    if typ != 3 and node >= nN:
                        self.bad("%s: track %d moves node %d of %d" % (what, t, node, nN))
                    size = frames * COMP[typ] * 4
                    if self.inside(off, size, "%s track %d" % (what, t)):
                        self.aligned(off)
                        c = zlib.crc32(struct.pack("<II", node, typ), c)
                        c = zlib.crc32(b[off:off + size], c)
                self.out("%s fps %g frames %d tracks %d crc %08x" % (what, fps, frames, nTr, c & 0xffffffff))

        # compressed textures, from TEXC
        cooked = {}
        for name, coff, csize in self.chunks:
            if name == "NOTE":
                self.out("note " + b[coff:coff + csize].decode("utf-8", "replace"))
            if name != "TEXC":
                continue
            n = struct.unpack_from("<I", b, coff)[0]
            if not self.inside(coff + 4, n * 32, "TEXC table") or 4 + n * 32 > csize:
                self.bad("TEXC's table of %d doesn't fit its chunk" % n)
                continue
            for i in range(n):
                slot, fmt, w, h, levels, flags, off, size = struct.unpack_from("<8I", b, coff + 4 + i * 32)
                what = "compressed texture %d" % slot
                if slot >= nT:
                    self.bad("%s: %d textures in the table" % (what, nT))
                if slot in cooked:
                    self.bad(what + " is there twice")
                cooked[slot] = 1
                if fmt not in (1, 3, 7):
                    self.bad("%s: format %d" % (what, fmt))
                    continue
                if w < 1 or h < 1 or levels < 1 or (w >> levels) or (h >> levels):
                    self.bad("%s: %dx%d with %d levels" % (what, w, h, levels))
                    continue
                if flags & ~1:
                    self.bad("%s: flags %#x has bits nothing reads" % (what, flags))
                want = sum(((max(w >> l, 1) + 3) // 4) * ((max(h >> l, 1) + 3) // 4) * (8 if fmt == 1 else 16) for l in range(levels))
                if size != want:
                    self.bad("%s: %d bytes, %dx%d with %d levels is %d" % (what, size, w, h, levels, want))
                if off % 16:
                    self.bad("%s isn't on a 16-byte line" % what)
                if off < coff or off + size > coff + csize:
                    self.bad("%s isn't inside its chunk" % what)
                    continue
                self.counts["texbytes"] += size
                self.out("texture %d bc%d %dx%d %d levels %d bytes flags %d crc %s" % (slot, fmt, w, h, levels, size, flags, crc(b[off:off + size])))

        # textures
        if self.inside(oT, nT * 16, "texture table"):
            for i in range(nT):
                off, ln = struct.unpack_from("<II", b, oT + i * 16 + 8)
                if i in cooked:
                    if ln:
                        self.bad("texture %d is both an image and compressed" % i)
                    continue
                if ln == 0:
                    self.out("texture %d empty" % i)
                    continue
                if self.inside(off, ln, "texture %d" % i):
                    kind, w, h = image_info(b[off:off + ln])
                    if kind == "?":
                        self.bad("texture %d isn't an image this reader knows (%r)" % (i, b[off:off + 8]))
                    self.counts["texbytes"] += ln
                    self.out("texture %d %s %dx%d %d bytes crc %s" % (i, kind, w, h, ln, crc(b[off:off + ln])))

        # the size block: twelve floats, of which the loader reads three
        if self.inside(oZ, 48, "size block"):
            self.aligned(oZ)
            sz = struct.unpack_from("<12f", b, oZ)
            self.out("size sole %.5g height %.5g measured %.5g" % (sz[7], sz[9], sz[10]))

        # the morph table: header field 14, 0 for none
        self.morphPairs = []
        if oMorph and oMorph < len(b):
            nm = struct.unpack_from("<I", b, oMorph)[0]
            self.counts["morphs"] = nm
            self.aligned(oMorph)
            if self.inside(oMorph + 4, nm * 16, "morph table"):
                for i in range(nm):
                    mesh, name, sparse, off = MORPH.unpack_from(b, oMorph + 4 + i * 16)
                    what = 'morph %d "%s"' % (i, self.name(name))
                    self.morphPairs.append((self.meshRefs()[mesh] if mesh < len(self.meshNames) else "?", self.name(name)))
                    if mesh >= nM:
                        self.bad("%s is for mesh %d of %d" % (what, mesh, nM))
                    if self.inside(off, sparse * 16, what):
                        self.aligned(off)
                        if sparse and mesh < len(self.verts):
                            top = max(struct.unpack_from("<I", b, off + k * 16)[0] for k in range(sparse))
                            if top >= self.verts[mesh]:
                                self.bad("%s moves vertex %d of %d" % (what, top, self.verts[mesh]))
                        self.out("%s mesh %d verts %d crc %s" % (what, mesh, sparse, crc(b[off:off + sparse * 16])))
        elif oMorph:
            self.bad("morph table at %d is past the end of the file" % oMorph)
        self.authored()

    def meshRefs(self):
        """what a chunk calls each mesh: its name, and for the second of a name, which it is"""
        seen, out = {}, []
        for n in self.meshNames:
            k = seen.get(n, 0)
            seen[n] = k + 1
            out.append(n if not k else "%s\x01%d" % (n, k))
        return out

    def cname(self, coff, csize, at, what):
        """a name in a chunk: a length, then its characters"""
        if at + 4 > csize:
            self.bad("%s: its name at %d is past the chunk" % (what, at))
            return ""
        n = struct.unpack_from("<I", self.b, coff + at)[0]
        if at + 4 + n > csize:
            self.bad("%s: its name runs past the chunk" % what)
            return ""
        return self.b[coff + at + 4:coff + at + 4 + n].decode("utf-8", "replace")

    def authored(self):
        """FACE, MRPH and CLIP: named, so every name has to mean something in this body, and each chunk's offsets stay inside it"""
        b = self.b
        want = {n: (o, z) for n, o, z in self.chunks}
        names = set(self.nodeNames)
        morphs = set(self.morphPairs)
        if "FACE" in want:
            coff, csize = want["FACE"]
            n = struct.unpack_from("<I", b, coff)[0] if csize >= 4 else 0
            if csize < 4 or n > (csize - 4) // 52:
                self.bad("FACE's table of %d doesn't fit its chunk" % n)
                n = 0
            for i in range(n):
                nm, par, kind = struct.unpack_from("<III", b, coff + 4 + i * 52)
                name = self.cname(coff, csize, nm, "mark %d" % i)
                if kind not in KIND:
                    self.bad('mark "%s" has kind %d' % (name, kind))
                if par != 0xffffffff:
                    parent = self.cname(coff, csize, par, 'mark "%s"' % name)
                    if parent not in names:
                        self.bad('mark "%s": its parent "%s" isn\'t here' % (name, parent))
                names.add(name)
                self.out('chunk mark "%s" crc %s' % (name, crc(b[coff + 4 + i * 52 + 8:coff + 4 + i * 52 + 52])))
            self.counts["nodes"] += n
        if "MRPH" in want:
            coff, csize = want["MRPH"]
            n = struct.unpack_from("<I", b, coff)[0] if csize >= 4 else 0
            if csize < 4 or n > (csize - 4) // 20:
                self.bad("MRPH's table of %d doesn't fit its chunk" % n)
                n = 0
            for i in range(n):
                nm, mn, verts, sparse, off = struct.unpack_from("<5I", b, coff + 4 + i * 20)
                name, mesh = self.cname(coff, csize, nm, "morph %d" % i), self.cname(coff, csize, mn, "morph %d" % i)
                what = 'chunk morph "%s"' % name
                if not any(m == mesh and v == verts for m, v in zip(self.meshRefs(), self.verts)):
                    self.bad('%s is for a mesh "%s" of %d vertices, which this body hasn\'t got' % (what, mesh, verts))
                if off % 4 or off + sparse * 16 > csize:
                    self.bad("%s: %d entries at %d don't fit the chunk" % (what, sparse, off))
                    continue
                if sparse and max(struct.unpack_from("<I", b, coff + off + k * 16)[0] for k in range(sparse)) >= verts:
                    self.bad("%s moves a vertex past its mesh" % what)
                morphs.add((mesh, name))
                self.out('%s on "%s" verts %d crc %s' % (what, mesh, sparse, crc(b[coff + off:coff + off + sparse * 16])))
            self.counts["morphs"] += n
        if "CLIP" in want:
            coff, csize = want["CLIP"]
            n = struct.unpack_from("<I", b, coff)[0] if csize >= 4 else 0
            if csize < 4 or n > (csize - 4) // 20:
                self.bad("CLIP's table of %d doesn't fit its chunk" % n)
                n = 0
            for i in range(n):
                nm, fps, frames, nTr, oTr = CLIP.unpack_from(b, coff + 4 + i * 20)
                name = self.cname(coff, csize, nm, "clip %d" % i)
                what = 'chunk clip "%s"' % name
                self.counts["keys"] += frames
                if not fps > 0:
                    self.bad("%s: fps %g" % (what, fps))
                if oTr + nTr * 12 > csize:
                    self.bad("%s: %d tracks at %d don't fit the chunk" % (what, nTr, oTr))
                    continue
                c = zlib.crc32(b"")
                for t in range(nTr):
                    tn, typ, off = TRACK.unpack_from(b, coff + oTr + t * 12)
                    tname = self.cname(coff, csize, tn, "%s track %d" % (what, t))
                    if typ not in COMP:
                        self.bad("%s: track %d has type %d" % (what, t, typ))
                        continue
                    parts = tname.split("\t")
                    if (typ == 3 and (len(parts) != 3 or (parts[0], parts[1]) not in morphs)) or (typ != 3 and parts[0] not in names):
                        self.bad('%s: track %d moves "%s", which this body hasn\'t got' % (what, t, tname.replace("\t", " ")))
                    size = frames * COMP[typ] * 4
                    if off % 4 or off + size > csize:
                        self.bad("%s: track %d's data at %d doesn't fit the chunk" % (what, t, off))
                        continue
                    c = zlib.crc32(tname.encode("utf-8"), c)
                    c = zlib.crc32(b[coff + off:coff + off + size], c)
                self.out("%s fps %g frames %d tracks %d crc %08x" % (what, fps, frames, nTr, c & 0xffffffff))
            self.counts["clips"] += n

    def head(self):
        c = self.counts
        return "%s: %d bytes, %d nodes, %d meshes (%d verts, %d tris), %d materials, %d clips (%d frames), %d textures (%d bytes), %d morphs%s" % (os.path.basename(self.path), len(self.b), c.get("nodes", 0), c.get("meshes", 0), c.get("verts", 0), c.get("tris", 0), c.get("mats", 0), c.get("clips", 0), c.get("keys", 0), c.get("tex", 0), c.get("texbytes", 0), c.get("morphs", 0), ", %d unaligned arrays" % self.unaligned if self.unaligned else "")


def check(paths):
    files = []
    for p in paths:
        files += sorted(glob.glob(os.path.join(p, "*.rpack"))) if os.path.isdir(p) else [p]
    bad = unal = 0
    for f in files:
        P = Pack(f)
        unal += P.unaligned > 0
        if P.err:
            bad += 1
            print(os.path.basename(f) + ":")
            for e in P.err[:12]:
                print("   " + e)
            if len(P.err) > 12:
                print("   ... and %d more" % (len(P.err) - 12))
    print("%d packs, %d wrong, %d with unaligned arrays" % (len(files), bad, unal))
    return 1 if bad else 0


def selftest(path):
    """every way of breaking a pack that --check claims to catch, on a real one with meshes, clips and textures. A check that can't fail isn't one."""
    good = open(path, "rb").read()
    base = Pack(path, good)
    if base.err:
        sys.exit("%s is already wrong: %s" % (path, base.err[0]))
    ver, nN, nM, nMat, nC, nT, oN, oM, oMat, oC, oT, oS, oZ, total, oMorph = HDR.unpack_from(good, 4)
    blob = struct.unpack_from("<I", good, oT + 12)[0]
    if not (nN > 1 and nM and nMat and (nC or base.counts["clips"]) and nT):
        sys.exit("%s hasn't one of everything: it needs nodes, a mesh, a material, a clip and a texture" % path)
    m = MESH.unpack_from(good, oM)
    c = CLIP.unpack_from(good, oC) if nC else None

    def poke(off, fmt, *vals):
        def f(b):
            struct.pack_into(fmt, b, off, *vals)
        return f

    cases = [
        ("not a pack", poke(0, "<4s", b"RPK9")),
        ("an unknown version", poke(4, "<I", 3)),
        ("a total that isn't the file's size", poke(4 + 13 * 4, "<I", len(good) + 1)),
        ("a table past the end of the file", poke(4 + 7 * 4, "<I", len(good))),
        ("a string table past the end", poke(oS, "<I", 0x7fffffff)),
        ("a node's name out of range", poke(oN, "<I", 0x7fffffff)),
        ("a child before its parent", poke(oN + 4, "<i", 1)),
        ("a parent below -1", poke(oN + 52 + 4, "<i", -5)),
        ("a node of an unknown kind", poke(oN + 8, "<I", 9)),
        ("a mesh on a node that isn't there", poke(oM + 4, "<I", nN)),
        ("indices that aren't whole triangles", poke(oM + 12, "<I", m[3] - 1)),
        ("mesh flags nothing reads", poke(oM + 20, "<I", m[5] | 16)),
        ("positions past the end", poke(oM + 24, "<I", len(good))),
        ("a missing index array", poke(oM + 44, "<I", 0)),
        ("an index past the vertices", poke(m[11], "<I", m[2])),
        ("a material for a mesh that isn't there", poke(oMat, "<I", nM)),
        ("a material's texture out of range", poke(oMat + 24, "<i", nT)),
        ("a size block past the end", poke(4 + 12 * 4, "<I", len(good))),
        ("a morph table past the end", poke(4 + 14 * 4, "<I", len(good) + 4)),
    ]
    if nC:
        cases += [("a clip at zero fps", poke(oC + 4, "<f", 0.0)),
                  ("a clip's tracks past the end", poke(oC + 16, "<I", len(good))),
                  ("a track of an unknown type", poke(c[4] + 4, "<I", 7)),
                  ("a track moving a node that isn't there", poke(c[4], "<I", nN)),
                  ("a track's data past the end", poke(c[4] + 8, "<I", len(good)))]
    if blob:
        cases += [("a texture past the end", poke(oT + 8, "<I", len(good))),
                  ("a texture that isn't an image", lambda b: b.__setitem__(slice(struct.unpack_from("<I", good, oT + 8)[0], struct.unpack_from("<I", good, oT + 8)[0] + 8), b"notimage"))]
    if good[-4:] == b"RPK2":
        dirOff, nDir, _ = struct.unpack_from("<III", good, len(good) - 16)
        ents = {good[dirOff + i * 16:dirOff + i * 16 + 4]: struct.unpack_from("<4sIII", good, dirOff + i * 16) + (i,) for i in range(nDir)}
        first = struct.unpack_from("<4sIII", good, dirOff)

        def recrc(tag, mutate):
            """change a chunk and keep its crc right, so the record's own check is what has to catch it"""
            e = ents[tag]

            def f(b):
                mutate(b)
                struct.pack_into("<I", b, dirOff + e[4] * 16 + 12, zlib.crc32(bytes(b[e[1]:e[1] + e[2]])) & 0xffffffff)
            return f
        cases += [("a footer with the wrong size", poke(len(good) - 8, "<I", len(good) + 1)),
                  ("a directory past the end", poke(len(good) - 16, "<I", len(good))),
                  ("a directory that doesn't end at the footer", poke(len(good) - 12, "<I", nDir + 1)),
                  ("an old body running into the directory", poke(4 + 13 * 4, "<I", dirOff + 1)),
                  ("a chunk off its line", poke(dirOff + 4, "<I", first[1] + 1)),
                  ("a chunk past the end", poke(dirOff + 8, "<I", len(good))),
                  ("a chunk whose crc is wrong", lambda b: b.__setitem__(first[1] + 2, b[first[1] + 2] ^ 1))]
        if b"TEXC" in ents:
            off = ents[b"TEXC"][1]
            T9 = lambda mutate: recrc(b"TEXC", mutate)
            cases += [("a compressed texture for a slot that isn't there", T9(poke(off + 4, "<I", nT))),
                      ("an unknown compressed format", T9(poke(off + 8, "<I", 2))),
                      ("too many levels", T9(poke(off + 20, "<I", 30))),
                      ("flags nothing reads", T9(poke(off + 24, "<I", 8))),
                      ("the wrong size for its levels", T9(poke(off + 32, "<I", 8))),
                      ("a compressed texture off its line", T9(poke(off + 28, "<I", struct.unpack_from("<I", good, off + 28)[0] + 1))),
                      ("a texture that's both", poke(oT + 12, "<I", 8))]
        if b"CLIP" in ents:
            off, size = ents[b"CLIP"][1], ents[b"CLIP"][2]
            nm9, _fps, _fr, _nT, oT9 = CLIP.unpack_from(good, off + 4)
            tn9, _t, od9 = TRACK.unpack_from(good, off + oT9)
            C9 = lambda mutate: recrc(b"CLIP", mutate)
            cases += [("a chunk's clip table too big for it", C9(poke(off, "<I", 0x7ffffff))),
                      ("a chunk's clip at zero fps", C9(poke(off + 4 + 4, "<f", 0.0))),
                      ("a chunk's clip name past the chunk", C9(poke(off + 4, "<I", size))),
                      ("a chunk's tracks past the chunk", C9(poke(off + 4 + 16, "<I", size))),
                      ("a chunk's track of an unknown type", C9(poke(off + oT9 + 4, "<I", 7))),
                      ("a chunk's track data past the chunk", C9(poke(off + oT9 + 8, "<I", size))),
                      ("a chunk's track moving something this body hasn't got", C9(lambda b: b.__setitem__(off + tn9 + 4, 1)))]
        if b"MRPH" in ents:
            off, size = ents[b"MRPH"][1], ents[b"MRPH"][2]
            _n, _m, verts9, sp9, od9 = struct.unpack_from("<5I", good, off + 4)
            M9 = lambda mutate: recrc(b"MRPH", mutate)
            cases += [("a chunk's morph table too big for it", M9(poke(off, "<I", 0x7ffffff))),
                      ("a chunk's morph for a mesh of another size", M9(poke(off + 4 + 8, "<I", verts9 + 1))),
                      ("a chunk's morph data past the chunk", M9(poke(off + 4 + 16, "<I", size))),
                      ("a chunk's morph moving a vertex past its mesh", M9(poke(off + od9, "<I", 0x7fffffff)))]
        if b"FACE" in ents:
            off, size = ents[b"FACE"][1], ents[b"FACE"][2]
            _n, par9, _k = struct.unpack_from("<III", good, off + 4)
            F9 = lambda mutate: recrc(b"FACE", mutate)
            cases += [("a chunk's mark table too big for it", F9(poke(off, "<I", 0x7ffffff))),
                      ("a chunk's mark of an unknown kind", F9(poke(off + 4 + 8, "<I", 9))),
                      ("a chunk's mark name past the chunk", F9(poke(off + 4, "<I", size))),
                      ("a chunk's mark whose parent isn't here", F9(lambda b: b.__setitem__(off + par9 + 4, 1)))]
    if m[4]:
        cases += [("a bone map naming a node that isn't there", poke(m[12], "<H", nN)),
                  ("a vertex using a joint that isn't in the map", lambda b: b.__setitem__(m[9], m[4]))]
    if oMorph and struct.unpack_from("<I", good, oMorph)[0]:
        e = MORPH.unpack_from(good, oMorph + 4)
        cases += [("a morph for a mesh that isn't there", poke(oMorph + 4, "<I", nM)),
                  ("a morph moving a vertex that isn't there", poke(e[3], "<I", 0x7fffffff))]
    missed = 0
    for name, fn in cases:
        b = bytearray(good)
        fn(b)
        try:
            P = Pack(path, bytes(b))
            caught = P.err[0] if P.err else None
        except Exception as ex:
            caught = None
            print("  CRASHED on %s: %r" % (name, ex))
        print("  %-46s %s" % (name, caught[:80] if caught else "⛔ GOT THROUGH"))
        missed += caught is None
    print("%d ways of breaking %s, %d got through" % (len(cases), os.path.basename(path), missed))
    return 1 if missed else 0


def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    if "--selftest" in a:
        a.remove("--selftest")
        sys.exit(selftest(a[0]))
    if "--check" in a:
        a.remove("--check")
        sys.exit(check(a))
    brief = "--brief" in a
    if brief:
        a.remove("--brief")
    P = Pack(a[0])
    print(P.head())
    if not brief:
        print("\n".join(P.lines))
    for e in P.err:
        print("WRONG: " + e)
    sys.exit(1 if P.err else 0)


if __name__ == "__main__":
    main()
