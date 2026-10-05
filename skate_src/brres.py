#!/usr/bin/env python3
"""brres.py -- a reader for Nintendo's BRRES (NW4R) model archives: the Wii's .brres (2026-10-05).

    brres.py info <file.brres|.szs|.arc>          the tree: models, bones, objects, textures, animations
    brres.py tex  <file> <outdir>                 every TEX0 decoded to PNG (the proof the decoder works)
    brres.py scene <file> [model]                 -> dict(nodes, meshes, clips, textures, names) in the club's
                                                  shape (see rpackwrite.py), meshes pre-baked to rig space;
                                                  brres2rpack.py and brres2dae.py are thin over this

Everything is big-endian. The file is a tree of INDEX GROUPS (a count, then 16-byte entries: id,
left, right, name offset, data offset -- offsets from the group's own start; entry 0 is the tree's
root reference and names nothing). The root group's folders are the resource types:
3DModels(NW4R) -> MDL0, Textures(NW4R) -> TEX0, Palettes(NW4R) -> PLT0, AnmChr(NW4R) -> CHR0, and
the rest (AnmVis, AnmClr, AnmTexSrt, AnmShp, AnmScn, External) are read as names only.

MDL0: a header of section offsets (11 for version <= 9, 13 from 10: fur vectors and layers were
inserted after the UVs), each an index group of resources:
  Definitions   bytecode. 0x02 NodeTree (bone, parent matrix), 0x03 NodeMix (matrix id = a weighted
                mix of bone matrices), 0x04 DrawOpa / DrawXlu (material, object, bone, priority),
                0x05 MtxDup, 0x01 end. The mixes are the skinning: a vertex's matrix index is either a
                bone or a mix of bones with weights
  Bones         name, index, matrix id, flags, scale / rotation (deg, XYZ) / translation, parent /
                child / sibling offsets, a 4x3 world matrix and its inverse. The STORED matrices are
                taken as the truth; locals come from inv(parent) * world (sidesteps the Maya segment-
                scale rules)
  Vertices / Normals / Colors / UVs   arrays: a format (u8 s8 u16 s16 float, a fixed-point divisor), a
                component count, a stride, a count
  Materials     the layer list (texture name, wrap, filters) and the xlu flag
  Objects       a GX display list: 0x08 CP loads (the VCD: which attributes each vertex carries and
                how, 8- or 16-bit indices), 0x10 XF, 0x20/0x28 indexed matrix loads (which palette
                matrix sits in which XF slot), then 0x80..0xB8 draw commands (quads, triangles, strips,
                fans) whose vertices are tuples of indices into the arrays. A single-bound object's
                vertices are in its bone's space; a multi-bound object's are in model space and each
                vertex names its matrix through the PNMTXIDX byte (XF slot * 3)
TEX0: width, height, format, mip count, then the image in GX tiles: I4 I8 IA4 IA8 RGB565 RGB5A3 RGBA8
  C4 C8 C14X2 (palette from the PLT0 of the same name) CMPR (DXT1 in 8x8 tiles of four 4x4 blocks,
  big-endian colour words).
CHR0: per bone a flag word (which of S/R/T are identity, isotropic, fixed) and either fixed values
  or keyframe tracks: I12 (frame, value, tangent floats), I6 / I4 (packed), L1 / L2 / L4 (one value a
  frame). Hermite between interpolated keys. Resampled to the engine's fixed frame rate by the caller.

Written from the format as documented by the BrawlBox / BrawlCrate and Wiimm (wszst) projects, and
checked on jacob's files -- the receipts are in the ledger below as they land.
"""
import sys, os, struct, math, io
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rpackwrite as RW

u8 = struct.Struct('>B')
u16 = struct.Struct('>H')
s16 = struct.Struct('>h')
u32 = struct.Struct('>I')
s32 = struct.Struct('>i')
f32 = struct.Struct('>f')


def U8(b, o): return b[o]
def U16(b, o): return u16.unpack_from(b, o)[0]
def S16(b, o): return s16.unpack_from(b, o)[0]
def U32(b, o): return u32.unpack_from(b, o)[0]
def S32(b, o): return s32.unpack_from(b, o)[0]
def F32(b, o): return f32.unpack_from(b, o)[0]


def cstr(b, o):
    e = b.find(b'\0', o)
    return b[o:e if e >= 0 else len(b)].decode('shift_jis', 'replace')


# ----------------------------------------------------------------- containers: Yaz0, U8
def yaz0_decode(b):
    if b[:4] != b'Yaz0':
        return b
    size = U32(b, 4)
    out = bytearray()
    src = 16
    code, bits = 0, 0
    while len(out) < size:
        if bits == 0:
            code = b[src]
            src += 1
            bits = 8
        if code & 0x80:
            out.append(b[src])
            src += 1
        else:
            b1, b2 = b[src], b[src + 1]
            src += 2
            dist = ((b1 & 0x0F) << 8) | b2
            n = b1 >> 4
            if n == 0:
                n = b[src] + 0x12
                src += 1
            else:
                n += 2
            start = len(out) - (dist + 1)
            for k in range(n):
                out.append(out[start + k])
        code <<= 1
        bits -= 1
    return bytes(out[:size])


def u8_files(b):
    """A U8 archive (.arc / .szs content) -> [(path, bytes)]."""
    if b[:4] != b'U\xaa8-':
        return []
    root = U32(b, 4)
    fst = root
    nNodes = U32(b, fst + 8)
    strtab = fst + nNodes * 12
    out = []
    stack = []   # (end index, dirname)
    path = []
    i = 1
    while i < nNodes:
        t = b[fst + i * 12]
        nameoff = U32(b, fst + i * 12) & 0xFFFFFF
        a, c = U32(b, fst + i * 12 + 4), U32(b, fst + i * 12 + 8)
        name = cstr(b, strtab + nameoff)
        while stack and i >= stack[-1][0]:
            stack.pop()
            path.pop()
        if t == 1:
            stack.append((c, name))
            path.append(name)
        else:
            out.append(('/'.join(path + [name]), b[a:a + c]))
        i += 1
    return out


def load_brres_bytes(path, pick=None):
    """The .brres bytes out of a .brres, a Yaz0 .szs or a U8 .arc (the first model archive, or the
    one whose path contains `pick`)."""
    b = open(path, 'rb').read()
    b = yaz0_decode(b)
    if b[:4] == b'bres':
        return b, os.path.basename(path)
    files = u8_files(b)
    cands = [(p, d) for (p, d) in files if d[:4] == b'bres']
    if pick:
        cands = [(p, d) for (p, d) in cands if pick in p] or cands
    if not cands:
        raise SystemExit('%s: no BRRES inside (%d files: %s)' % (path, len(files), ', '.join(p for p, _ in files[:12])))
    if len(cands) > 1:
        print('  %d brres inside %s, taking %s (--pick chooses): %s' % (len(cands), os.path.basename(path), cands[0][0], ', '.join(p for p, _ in cands)))
    return cands[0][1], cands[0][0]


# ----------------------------------------------------------------- index groups
def index_group(b, off):
    """-> [(name, data offset absolute)] for the group at `off` (entry 0, the root ref, skipped)."""
    n = U32(b, off + 4)
    out = []
    for i in range(1, n + 1):
        e = off + 8 + i * 16
        nameOff, dataOff = U32(b, e + 8), U32(b, e + 12)
        name = cstr(b, off + nameOff) if nameOff else ''
        out.append((name, off + dataOff if dataOff else 0))
    return out


class Brres:
    def __init__(self, data, label=''):
        self.b = data
        self.label = label
        b = data
        assert b[:4] == b'bres', 'not a BRRES'
        self.bom = U16(b, 4)
        self.version = U16(b, 6)
        self.length = U32(b, 8)
        rootOff = U16(b, 12)
        self.nSections = U16(b, 14)
        assert b[rootOff:rootOff + 4] == b'root', 'no root section'
        self.folders = {}
        for name, off in index_group(b, rootOff + 8):
            self.folders[name] = index_group(b, off) if off else []
        self.models = {n: o for (n, o) in self.folders.get('3DModels(NW4R)', [])}
        self.textures = {n: o for (n, o) in self.folders.get('Textures(NW4R)', [])}
        self.palettes = {n: o for (n, o) in self.folders.get('Palettes(NW4R)', [])}
        self.chr = {n: o for (n, o) in self.folders.get('AnmChr(NW4R)', [])}

    # ------------------------------------------------------------- MDL0
    def model(self, name=None):
        if not self.models:
            raise SystemExit('%s: no 3DModels(NW4R)' % self.label)
        if name is None:
            name = next(iter(self.models))
        return Mdl0(self.b, self.models[name], name)

    # ------------------------------------------------------------- TEX0 / PLT0
    def texture(self, name):
        off = self.textures.get(name)
        if off is None:
            return None
        return Tex0(self.b, off, name, self)

    def palette(self, name):
        off = self.palettes.get(name)
        if off is None:
            return None
        return Plt0(self.b, off, name)

    def chr0(self, name):
        return Chr0(self.b, self.chr[name], name)


# ----------------------------------------------------------------- MDL0
VTX_FMT = {0: ('B', 1, False), 1: ('b', 1, False), 2: ('H', 2, False), 3: ('h', 2, False), 4: ('f', 4, True)}


class Mdl0:
    def __init__(self, b, off, name):
        self.b, self.off, self.name = b, off, name
        assert b[off:off + 4] == b'MDL0', 'MDL0 expected at %#x' % off
        self.length = U32(b, off + 4)
        self.version = U32(b, off + 8)
        # section offsets: 11 up to v9 (Brawl), 13 at v10 (fur vectors / fur layers after the UVs),
        # 14 at v11 (Mario Kart Wii and everything Maya-era: + user data)
        nSec = 11 if self.version <= 9 else (13 if self.version == 10 else 14)
        secs = [S32(b, off + 16 + i * 4) for i in range(nSec)]
        keys = ['defs', 'bones', 'verts', 'normals', 'colors', 'uvs'] + (['furv', 'furl'] if nSec >= 13 else []) + ['materials', 'shaders', 'objects', 'texlinks', 'pltlinks', 'userdata']
        self.sections = {}
        for k, s in zip(keys, secs):
            self.sections[k] = index_group(b, off + s) if s > 0 else []
        for k in keys:
            self.sections.setdefault(k, [])
        self.stringOff = S32(b, off + 16 + nSec * 4)
        hdr = off + 16 + nSec * 4 + 4          # the model info block (RiiStudio BinaryModelInfo)
        self.info = dict(scalingRule=U32(b, hdr + 8), texMtxMode=U32(b, hdr + 12), nVerts=U32(b, hdr + 16), nTris=U32(b, hdr + 20),
                         nViewMtx=U32(b, hdr + 28), needNrmMtx=U8(b, hdr + 32), needTexMtx=U8(b, hdr + 33), envMtxMode=U8(b, hdr + 35),
                         min=[F32(b, hdr + 40 + k * 4) for k in range(3)], max=[F32(b, hdr + 52 + k * 4) for k in range(3)])
        self.hdrOff = hdr
        lutOff = S32(b, hdr + 36)
        self.lut = []
        if 0 < lutOff < 0x10000:
            n = U32(b, hdr + lutOff)
            if n < 4096:
                self.lut = [S32(b, hdr + lutOff + 4 + i * 4) for i in range(n)]      # matrix id -> bone id (-1: a mix)
        self._read_bones()
        self._read_defs()
        self.verts = [self._read_array(n, o, 'verts') for (n, o) in self.sections['verts']]
        self.normals = [self._read_array(n, o, 'normals') for (n, o) in self.sections['normals']]
        self.colors = [self._read_colors(n, o) for (n, o) in self.sections['colors']]
        self.uvs = [self._read_array(n, o, 'uvs') for (n, o) in self.sections['uvs']]
        self.materials = [self._read_material(n, o) for (n, o) in self.sections['materials']]
        self.objects = [self._read_object(n, o) for (n, o) in self.sections['objects']]

    # ---- bones
    def _read_bones(self):
        b = self.b
        self.bones = []
        byoff = {}
        for (name, o) in self.sections['bones']:
            bn = dict(name=name, off=o, index=U32(b, o + 12), mtx=U32(b, o + 16), flags=U32(b, o + 20), billboard=U32(b, o + 24),
                      s=[F32(b, o + 32 + k * 4) for k in range(3)], r=[F32(b, o + 44 + k * 4) for k in range(3)],
                      t=[F32(b, o + 56 + k * 4) for k in range(3)],
                      parentOff=S32(b, o + 92), childOff=S32(b, o + 96), nextOff=S32(b, o + 100), prevOff=S32(b, o + 104))
            m = [F32(b, o + 112 + k * 4) for k in range(12)]       # 3 rows x 4 (row-major 3x4)
            inv = [F32(b, o + 160 + k * 4) for k in range(12)]
            bn['world'] = _m34_to_cm(m)
            bn['inv'] = _m34_to_cm(inv)
            byoff[o] = len(self.bones)
            self.bones.append(bn)
        for bn in self.bones:
            bn['parent'] = byoff.get(bn['off'] + bn['parentOff'], -1) if bn['parentOff'] else -1
        # parent-first order (the file's bone list is usually already so; made true)
        order, seen = [], set()
        for i in range(len(self.bones)):
            chain, j = [], i
            while j >= 0 and j not in seen:
                chain.append(j)
                j = self.bones[j]['parent']
            for j in reversed(chain):
                seen.add(j)
                order.append(j)
        remap = {o: n for n, o in enumerate(order)}
        self.bones = [self.bones[o] for o in order]
        for bn in self.bones:
            bn['parent'] = remap[bn['parent']] if bn['parent'] >= 0 else -1
        self.mtx2bone = {bn['mtx']: i for i, bn in enumerate(self.bones)}
        # the model's own matrix->bone table, when it has one, is the authority (bone ids are the
        # file's `index` field; the list was re-ordered parent-first above, so go through it)
        byid = {bn['index']: i for i, bn in enumerate(self.bones)}
        for mid, bid in enumerate(self.lut):
            if bid >= 0 and bid in byid:
                self.mtx2bone[mid] = byid[bid]
        self.name2bone = {bn['name']: i for i, bn in enumerate(self.bones)}

    # ---- definitions bytecode
    def _read_defs(self):
        b = self.b
        self.mixes = {}       # matrix id -> [(bone index, weight)]
        self.draws = []       # (material index, object index, bone index, priority, xlu)
        self.nodetree = []
        for (name, o) in self.sections['defs']:
            p = o
            xlu = 'Xlu' in name
            while p < len(b):
                cmd = b[p]
                p += 1
                if cmd == 0x01:
                    break
                if cmd == 0x02:
                    self.nodetree.append((U16(b, p), U16(b, p + 2)))
                    p += 4
                elif cmd == 0x03:
                    mid, n = U16(b, p), b[p + 2]
                    p += 3
                    lst = []
                    for k in range(n):
                        lst.append((U16(b, p), F32(b, p + 2)))
                        p += 6
                    self.mixes[mid] = lst
                elif cmd == 0x04:
                    self.draws.append((U16(b, p), U16(b, p + 2), U16(b, p + 4), b[p + 6], xlu))
                    p += 7
                elif cmd == 0x05:
                    to, frm = U16(b, p), U16(b, p + 2)
                    p += 4
                    self.mixes[to] = [('dup', frm)]
                elif cmd == 0x06:
                    p += 4
                else:
                    break
        # resolve dups
        for mid, lst in list(self.mixes.items()):
            if lst and lst[0][0] == 'dup':
                src = lst[0][1]
                self.mixes[mid] = self.mixes.get(src, [(src, 1.0)])

    def matrix_influences(self, mid):
        """matrix id -> [(bone index, weight)]"""
        if mid in self.mixes:
            out = []
            for (m2, w) in self.mixes[mid]:
                bi = self.mtx2bone.get(m2)
                if bi is not None:
                    out.append((bi, w))
            if out:
                return out
        bi = self.mtx2bone.get(mid)
        return [(bi, 1.0)] if bi is not None else []

    # ---- arrays
    def _read_array(self, name, o, kind):
        b = self.b
        dataOff = S32(b, o + 8)
        comp = U32(b, o + 20)            # verts: 0 XY 1 XYZ; normals: 0 N 1 NBT 2 NBT3; uvs: 0 S 1 ST
        fmt = U32(b, o + 24)
        divisor = U8(b, o + 28)
        stride = U8(b, o + 29)
        count = U16(b, o + 30)
        ncomp = {'verts': 2 if comp == 0 else 3, 'normals': 3 if comp == 0 else 9, 'uvs': 1 if comp == 0 else 2}[kind]
        if kind == 'normals' and comp == 2:
            ncomp = 3
        ch, sz, isf = VTX_FMT[fmt]
        scale = 1.0 if isf else 1.0 / (1 << divisor)
        st = struct.Struct('>' + ch * ncomp)
        data = []
        base = o + dataOff
        for i in range(count):
            vals = st.unpack_from(b, base + i * stride)
            data.append([v * scale for v in vals][:3 if kind == 'normals' else ncomp])
        return dict(name=name, count=count, ncomp=ncomp, data=data, index=U32(b, o + 16))

    def _read_colors(self, name, o):
        b = self.b
        dataOff = S32(b, o + 8)
        hasA = U32(b, o + 20)
        fmt = U32(b, o + 24)
        stride = U8(b, o + 28)
        count = U16(b, o + 30)
        base = o + dataOff
        data = []
        for i in range(count):
            p = base + i * stride
            if fmt == 0:
                v = U16(b, p)
                data.append([((v >> 11) & 31) / 31.0, ((v >> 5) & 63) / 63.0, (v & 31) / 31.0, 1.0])
            elif fmt in (1, 2):
                data.append([b[p] / 255.0, b[p + 1] / 255.0, b[p + 2] / 255.0, 1.0])
            elif fmt == 3:
                v = U16(b, p)
                data.append([((v >> 12) & 15) / 15.0, ((v >> 8) & 15) / 15.0, ((v >> 4) & 15) / 15.0, (v & 15) / 15.0])
            elif fmt == 4:
                v = (b[p] << 16) | (b[p + 1] << 8) | b[p + 2]
                data.append([((v >> 18) & 63) / 63.0, ((v >> 12) & 63) / 63.0, ((v >> 6) & 63) / 63.0, (v & 63) / 63.0])
            else:
                data.append([b[p] / 255.0, b[p + 1] / 255.0, b[p + 2] / 255.0, b[p + 3] / 255.0])
        return dict(name=name, count=count, data=data)

    # ---- materials
    def _read_material(self, name, o):
        b = self.b
        flags = U32(b, o + 16)
        nLayers = U32(b, o + 44)
        layerOff = S32(b, o + 48)
        layers = []
        if 0 < nLayers <= 8 and 0 < layerOff < U32(b, o):
            for i in range(nLayers):
                L = o + layerOff + i * 52
                tn, pn = S32(b, L), S32(b, L + 4)
                layers.append(dict(tex=cstr(b, L + tn) if tn else '', plt=cstr(b, L + pn) if pn else '',
                                   wrapS=U32(b, L + 24), wrapT=U32(b, L + 28), minF=U32(b, L + 32), magF=U32(b, L + 36)))
        return dict(name=name, index=U32(b, o + 12), xlu=bool(flags & 0x80000000), layers=layers, cull=U32(b, o + 24))

    # ---- objects (polygons)
    def _read_object(self, name, o):
        # layout per RiiStudio PolygonIO (readA / two DlHandles / readB):
        #   0x08 currentMatrix (a MATRIX id; -1 = weighted)   0x0C..0x14 the VCD/VAT cache
        #   0x18 setup DL   {buf_size, cmd_size, ofs}  data at 0x18 + ofs
        #   0x24 prim DL    {buf_size, cmd_size, ofs}  data at 0x24 + ofs
        #   0x30 vcd bitfield  0x34 flag  0x38 name  0x3C id  0x40 nVerts  0x44 nTris
        #   0x48 pos s16  0x4A nrm s16  0x4C col[2] s16  0x50 uv[8] s16  [v10+: 0x60 furVec 0x62 furPos]
        #   then the matrix-usage table offset (s32)
        b = self.b
        size = U32(b, o)
        nodeId = S32(b, o + 8)
        vcdLo, vcdHi = U32(b, o + 12), U32(b, o + 16)
        defSize, defOff = U32(b, o + 24), S32(b, o + 32)
        primSize, primOff = U32(b, o + 36), S32(b, o + 44)
        vertId, nrmId = S16(b, o + 72), S16(b, o + 74)
        colIds = [S16(b, o + 76 + k * 2) for k in range(2)]
        uvIds = [S16(b, o + 80 + k * 2) for k in range(8)]
        defStart = o + 24 + defOff
        primStart = o + 36 + primOff
        # the def display list is the authority on the VCD and VAT
        vat = {}
        vcd = (vcdLo, vcdHi)
        p = defStart
        end = min(defStart + defSize, primStart) if primStart > defStart else defStart + defSize
        while p < end:
            cmd = b[p]
            if cmd == 0x08:
                addr, val = b[p + 1], U32(b, p + 2)
                if addr == 0x50:
                    vcd = (val, vcd[1])
                elif addr == 0x60:
                    vcd = (vcd[0], val)
                elif 0x70 <= addr < 0xA0:
                    vat.setdefault(addr & 7, {})[(addr >> 4) - 7] = val
                p += 6
            elif cmd == 0x10:
                n = U16(b, p + 1) + 1
                p += 5 + 4 * n
            elif cmd == 0x00:
                p += 1
            else:
                break
        return dict(name=name, index=U32(b, o + 60), nodeId=nodeId, vcd=vcd, vat=vat, vertId=vertId, nrmId=nrmId, colIds=colIds, uvIds=uvIds,
                    prim=(primStart, primSize), nVerts=U32(b, o + 64), nTris=U32(b, o + 68), off=o)

    # ---- the display list -> triangles
    def triangles(self, obj):
        """-> dict(tris=[(c0,c1,c2)...] of corner tuples (mtx, pos, nrm, col0, uv0..7 indices), slots)
        Each corner: dict with 'mtx' (matrix id or -1), 'pos', 'nrm', 'col', 'uv' (index or None)."""
        b = self.b
        vcdLo, vcdHi = obj['vcd']
        attrs = []       # (kind, mode, sub)   mode: 1 direct, 2 idx8, 3 idx16
        if vcdLo & 1:
            attrs.append(('pnmtx', 1, 0))
        for t in range(8):
            if (vcdLo >> (1 + t)) & 1:
                attrs.append(('texmtx', 1, t))
        attrs.append(('pos', (vcdLo >> 9) & 3, 0))
        attrs.append(('nrm', (vcdLo >> 11) & 3, 0))
        attrs.append(('col', (vcdLo >> 13) & 3, 0))
        attrs.append(('col', (vcdLo >> 15) & 3, 1))
        for t in range(8):
            attrs.append(('uv', (vcdHi >> (2 * t)) & 3, t))
        vat = obj['vat']

        def direct_size(kind, sub, fmtIdx):
            A = vat.get(fmtIdx, {}).get(0, 0)
            B = vat.get(fmtIdx, {}).get(1, 0)
            C = vat.get(fmtIdx, {}).get(2, 0)
            if kind == 'pos':
                cnt, fmt = A & 1, (A >> 1) & 7
                return (2 if cnt == 0 else 3) * (1, 1, 2, 2, 4)[fmt]
            if kind == 'nrm':
                cnt, fmt = (A >> 9) & 1, (A >> 10) & 7
                return (3 if cnt == 0 else 9) * (1, 1, 2, 2, 4)[fmt]
            if kind == 'col':
                fmt = ((A >> 14) & 7) if sub == 0 else ((A >> 18) & 7)
                return (2, 3, 4, 2, 3, 4)[fmt]
            if kind == 'uv':
                if sub == 0:
                    cnt, fmt = (A >> 21) & 1, (A >> 22) & 7
                elif sub < 4:
                    sh = (sub - 1) * 9
                    cnt, fmt = (B >> sh) & 1, (B >> (sh + 1)) & 7
                else:
                    sh = (sub - 4) * 9
                    cnt, fmt = (C >> sh) & 1, (C >> (sh + 1)) & 7
                return (1 if cnt == 0 else 2) * (1, 1, 2, 2, 4)[fmt]
            return 1

        p, end = obj['prim']
        end = p + end
        slots = {}         # XF position-matrix slot -> matrix id
        tris = []
        while p < end:
            cmd = b[p]
            if cmd == 0x00:
                p += 1
                continue
            if cmd == 0x20 or cmd == 0x28 or cmd == 0x30 or cmd == 0x38:
                idx = U16(b, p + 1)
                w = U16(b, p + 3)
                addr = w & 0xFFF
                if cmd == 0x20:
                    slots[addr // 12] = idx
                p += 5
                continue
            if cmd == 0x08:
                p += 6
                continue
            if cmd == 0x10:
                n = U16(b, p + 1) + 1
                p += 5 + 4 * n
                continue
            if cmd == 0x61:
                p += 5
                continue
            if cmd < 0x80:
                break
            prim, fmtIdx = cmd & 0xF8, cmd & 7
            count = U16(b, p + 1)
            p += 3
            verts = []
            for v in range(count):
                c = dict(mtx=-1, pos=None, nrm=None, col=None, uv=[None] * 8)
                for (kind, mode, sub) in attrs:
                    if mode == 0:
                        continue
                    if kind == 'pnmtx':
                        c['mtx'] = slots.get(b[p] // 3, -1) if slots else b[p] // 3
                        p += 1
                        continue
                    if kind == 'texmtx':
                        p += 1
                        continue
                    if mode == 1:
                        p += direct_size(kind, sub, fmtIdx)
                        continue
                    idx = b[p] if mode == 2 else U16(b, p)
                    p += 1 if mode == 2 else 2
                    if kind == 'pos':
                        c['pos'] = idx
                    elif kind == 'nrm':
                        c['nrm'] = idx
                    elif kind == 'col' and sub == 0:
                        c['col'] = idx
                    elif kind == 'uv':
                        c['uv'][sub] = idx
                verts.append(c)
            if prim == 0x90:
                for i in range(0, len(verts) - 2, 3):
                    tris.append((verts[i], verts[i + 1], verts[i + 2]))
            elif prim == 0x98:
                for i in range(2, len(verts)):
                    if i % 2 == 0:
                        tris.append((verts[i - 2], verts[i - 1], verts[i]))
                    else:
                        tris.append((verts[i - 1], verts[i - 2], verts[i]))
            elif prim == 0xA0:
                for i in range(2, len(verts)):
                    tris.append((verts[0], verts[i - 1], verts[i]))
            elif prim == 0x80 or prim == 0x88:
                for i in range(0, len(verts) - 3, 4):
                    tris.append((verts[i], verts[i + 1], verts[i + 2]))
                    tris.append((verts[i], verts[i + 2], verts[i + 3]))
            # lines / points: not drawn
        return tris


def _m34_to_cm(m):
    """3 rows x 4 (row-major, translation in the 4th column) -> column-major 16."""
    return [m[0], m[4], m[8], 0.0, m[1], m[5], m[9], 0.0, m[2], m[6], m[10], 0.0, m[3], m[7], m[11], 1.0]


# ----------------------------------------------------------------- TEX0 / PLT0 decoding
TEX_FMT = {0: 'I4', 1: 'I8', 2: 'IA4', 3: 'IA8', 4: 'RGB565', 5: 'RGB5A3', 6: 'RGBA8', 8: 'C4', 9: 'C8', 10: 'C14X2', 14: 'CMPR'}


def _rgb565(v):
    return (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)


def _rgb5a3(v):
    if v & 0x8000:
        return (((v >> 10) & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, (v & 31) * 255 // 31, 255)
    return (((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, ((v >> 12) & 7) * 255 // 7)


class Plt0:
    def __init__(self, b, o, name):
        self.name = name
        dataOff = S32(b, o + 16)
        self.fmt = U32(b, o + 24)
        self.n = U16(b, o + 28)
        base = o + dataOff
        self.colors = []
        for i in range(self.n):
            v = U16(b, base + i * 2)
            if self.fmt == 0:        # IA8
                self.colors.append((v >> 8, v >> 8, v >> 8, v & 255))
            elif self.fmt == 1:
                r, g, bb = _rgb565(v)
                self.colors.append((r, g, bb, 255))
            else:
                self.colors.append(_rgb5a3(v))


class Tex0:
    def __init__(self, b, o, name, arc=None):
        self.b, self.o, self.name = b, o, name
        self.length = U32(b, o + 4)
        self.version = U32(b, o + 8)
        self.dataOff = S32(b, o + 16)
        self.hasPalette = U32(b, o + 24)
        self.width, self.height = U16(b, o + 28), U16(b, o + 30)
        self.fmt = U32(b, o + 32)
        self.nMips = U32(b, o + 36)
        self.arc = arc

    def decode(self, palette=None):
        """-> (rgba bytes, w, h). Palette formats take the PLT0 of the same name from the archive."""
        b, w, h, fmt = self.b, self.width, self.height, self.fmt
        base = self.o + self.dataOff
        out = bytearray(w * h * 4)

        def put(x, y, rgba):
            if x < w and y < h:
                i = (y * w + x) * 4
                out[i], out[i + 1], out[i + 2], out[i + 3] = rgba

        if fmt in (8, 9, 10):
            pal = palette or (self.arc.palette(self.name) if self.arc else None)
            if pal is None:
                print('  !! texture %s is paletted (%s) and no PLT0 of that name exists -- grey' % (self.name, TEX_FMT.get(fmt)))
                cols = [(128, 128, 128, 255)] * 16384
            else:
                cols = pal.colors + [(0, 0, 0, 255)] * (16384 - len(pal.colors))
        p = base
        if fmt == 0:           # I4: 8x8 tiles, 4 bits
            for ty in range(0, h, 8):
                for tx in range(0, w, 8):
                    for y in range(8):
                        for x in range(0, 8, 2):
                            v = b[p]
                            p += 1
                            a, c = (v >> 4) * 17, (v & 15) * 17
                            put(tx + x, ty + y, (a, a, a, 255))
                            put(tx + x + 1, ty + y, (c, c, c, 255))
        elif fmt == 1:         # I8: 8x4
            for ty in range(0, h, 4):
                for tx in range(0, w, 8):
                    for y in range(4):
                        for x in range(8):
                            v = b[p]
                            p += 1
                            put(tx + x, ty + y, (v, v, v, 255))
        elif fmt == 2:         # IA4: 8x4
            for ty in range(0, h, 4):
                for tx in range(0, w, 8):
                    for y in range(4):
                        for x in range(8):
                            v = b[p]
                            p += 1
                            i, a = (v & 15) * 17, (v >> 4) * 17
                            put(tx + x, ty + y, (i, i, i, a))
        elif fmt == 3:         # IA8: 4x4
            for ty in range(0, h, 4):
                for tx in range(0, w, 4):
                    for y in range(4):
                        for x in range(4):
                            a, i = b[p], b[p + 1]
                            p += 2
                            put(tx + x, ty + y, (i, i, i, a))
        elif fmt == 4:         # RGB565: 4x4
            for ty in range(0, h, 4):
                for tx in range(0, w, 4):
                    for y in range(4):
                        for x in range(4):
                            r, g, bb = _rgb565(U16(b, p))
                            p += 2
                            put(tx + x, ty + y, (r, g, bb, 255))
        elif fmt == 5:         # RGB5A3: 4x4
            for ty in range(0, h, 4):
                for tx in range(0, w, 4):
                    for y in range(4):
                        for x in range(4):
                            put(tx + x, ty + y, _rgb5a3(U16(b, p)))
                            p += 2
        elif fmt == 6:         # RGBA8: 4x4, AR block then GB block
            for ty in range(0, h, 4):
                for tx in range(0, w, 4):
                    for y in range(4):
                        for x in range(4):
                            k = y * 4 + x
                            a, r = b[p + k * 2], b[p + k * 2 + 1]
                            g, bb = b[p + 32 + k * 2], b[p + 32 + k * 2 + 1]
                            put(tx + x, ty + y, (r, g, bb, a))
                    p += 64
        elif fmt == 8:         # C4: 8x8
            for ty in range(0, h, 8):
                for tx in range(0, w, 8):
                    for y in range(8):
                        for x in range(0, 8, 2):
                            v = b[p]
                            p += 1
                            put(tx + x, ty + y, cols[v >> 4])
                            put(tx + x + 1, ty + y, cols[v & 15])
        elif fmt == 9:         # C8: 8x4
            for ty in range(0, h, 4):
                for tx in range(0, w, 8):
                    for y in range(4):
                        for x in range(8):
                            put(tx + x, ty + y, cols[b[p]])
                            p += 1
        elif fmt == 10:        # C14X2: 4x4
            for ty in range(0, h, 4):
                for tx in range(0, w, 4):
                    for y in range(4):
                        for x in range(4):
                            put(tx + x, ty + y, cols[U16(b, p) & 0x3FFF])
                            p += 2
        elif fmt == 14:        # CMPR: 8x8 tiles of four 4x4 DXT1 blocks
            for ty in range(0, h, 8):
                for tx in range(0, w, 8):
                    for by in (0, 4):
                        for bx in (0, 4):
                            c0, c1 = U16(b, p), U16(b, p + 2)
                            bits = U32(b, p + 4)
                            p += 8
                            r0, g0, b0 = _rgb565(c0)
                            r1, g1, b1 = _rgb565(c1)
                            if c0 > c1:
                                pal4 = [(r0, g0, b0, 255), (r1, g1, b1, 255),
                                        ((2 * r0 + r1) // 3, (2 * g0 + g1) // 3, (2 * b0 + b1) // 3, 255),
                                        ((r0 + 2 * r1) // 3, (g0 + 2 * g1) // 3, (b0 + 2 * b1) // 3, 255)]
                            else:
                                pal4 = [(r0, g0, b0, 255), (r1, g1, b1, 255),
                                        ((r0 + r1) // 2, (g0 + g1) // 2, (b0 + b1) // 2, 255), (0, 0, 0, 0)]
                            for y in range(4):
                                for x in range(4):
                                    sel = (bits >> (30 - 2 * (y * 4 + x))) & 3
                                    put(tx + bx + x, ty + by + y, pal4[sel])
        else:
            print('  !! texture %s: format %d unknown -- magenta' % (self.name, fmt))
            for i in range(w * h):
                out[i * 4:i * 4 + 4] = b'\xff\x00\xff\xff'
        return bytes(out), w, h

    def png(self):
        rgba, w, h = self.decode()
        return RW.to_png(rgba, w, h)


# ----------------------------------------------------------------- CHR0
class Chr0:
    """A bone animation. Layout per RiiStudio AnimChrIO: v5 = CHR0, size, version, ofsBrres, ofsDict,
    ofsUserData, name, sourcePath, u16 frames, u16 nodes, u32 wrap, u32 scaleRule; v4 lacks ofsUserData.
    A node: name (offset from the node start), flags, then per present attribute (sx sy sz rx ry rz
    tx ty tz, OMIT masks below) a float constant or an s32 offset (from the node start) to a track."""
    ENABLED, SRT_IDENTITY, RT_ZERO, SCL_ONE, SCL_ISO, ROT_ZERO, TRANS_ZERO = 1, 2, 4, 8, 16, 32, 64
    SCL_MODEL, ROT_MODEL, TRANS_MODEL = 128, 256, 512
    OMIT = {0: SRT_IDENTITY | SCL_ONE | SCL_MODEL, 1: SRT_IDENTITY | SCL_ONE | SCL_ISO | SCL_MODEL, 2: SRT_IDENTITY | SCL_ONE | SCL_ISO | SCL_MODEL,
            3: SRT_IDENTITY | RT_ZERO | ROT_ZERO | ROT_MODEL, 4: SRT_IDENTITY | RT_ZERO | ROT_ZERO | ROT_MODEL, 5: SRT_IDENTITY | RT_ZERO | ROT_ZERO | ROT_MODEL,
            6: SRT_IDENTITY | RT_ZERO | TRANS_ZERO | TRANS_MODEL, 7: SRT_IDENTITY | RT_ZERO | TRANS_ZERO | TRANS_MODEL, 8: SRT_IDENTITY | RT_ZERO | TRANS_ZERO | TRANS_MODEL}

    def __init__(self, b, o, name):
        self.b, self.o, self.name = b, o, name
        self.version = U32(b, o + 8)
        dictOff = S32(b, o + 16)
        hdr = o + 32 if self.version >= 5 else o + 28
        self.nFrames = U16(b, hdr)
        self.nEntries = U16(b, hdr + 2)
        self.loop = U32(b, hdr + 4)
        self.scalingRule = U32(b, hdr + 8)
        self.entries = {}
        self._tracks = {}
        for (bname, eo) in index_group(b, o + dictOff):
            if eo:
                self.entries[bname] = self._entry(eo, bname)

    def _entry(self, e, bname):
        b = self.b
        flags = U32(b, e + 4)
        p = e + 8
        attrs = []
        sFmt, rFmt, tFmt = (flags >> 25) & 3, (flags >> 27) & 7, (flags >> 30) & 3
        for a in range(9):
            if flags & self.OMIT[a]:
                attrs.append(None)
                continue
            fmt = sFmt if a < 3 else (rFmt if a < 6 else tFmt)
            if (flags >> (13 + a)) & 1 or fmt == 0:
                attrs.append(('fixed', F32(b, p)))
            else:
                off = e + S32(b, p)
                if off not in self._tracks:
                    self._tracks[off] = self._track(off, fmt)
                attrs.append(('keys', self._tracks[off]))
            p += 4
        if flags & self.SCL_ISO and attrs[0] is not None:
            attrs[1] = attrs[2] = attrs[0]
        return dict(name=bname, flags=flags, attrs=attrs)

    def _track(self, p, fmt):
        """fmt 1/2/3 keyed (32/48/96-bit keys), 4/5/6 baked (8/16/32-bit per frame)."""
        b = self.b
        if fmt <= 3:
            n = U16(b, p)
            p += 8                        # n, pad, step
            if fmt == 1:
                scale, offset = F32(b, p), F32(b, p + 4)
                keys = []
                for i in range(n):
                    x = U32(b, p + 8 + i * 4)
                    slope = x & 0xFFF
                    if slope & 0x800:
                        slope -= 0x1000
                    keys.append((float(x >> 24), ((x >> 12) & 0xFFF) * scale + offset, slope / 32.0))
                return ('I', keys)
            if fmt == 2:
                scale, offset = F32(b, p), F32(b, p + 4)
                keys = [(S16(b, p + 8 + i * 6) / 32.0, U16(b, p + 10 + i * 6) * scale + offset, S16(b, p + 12 + i * 6) / 256.0) for i in range(n)]
                return ('I', keys)
            keys = [(F32(b, p + i * 12), F32(b, p + 4 + i * 12), F32(b, p + 8 + i * 12)) for i in range(n)]
            return ('I', keys)
        n = self.nFrames + 1
        if fmt == 4:
            scale, offset = F32(b, p), F32(b, p + 4)
            return ('L', [b[p + 8 + i] * scale + offset for i in range(n)])
        if fmt == 5:
            scale, offset = F32(b, p), F32(b, p + 4)
            return ('L', [U16(b, p + 8 + i * 2) * scale + offset for i in range(n)])
        return ('L', [F32(b, p + i * 4) for i in range(n)])

    @staticmethod
    def eval_track(tr, f):
        if tr[0] == 'fixed':
            return tr[1]
        kind, data = tr[1]
        if kind == 'L':
            i = int(f)
            if i >= len(data) - 1:
                return data[-1]
            return data[i] + (data[i + 1] - data[i]) * (f - i)
        keys = data
        if not keys:
            return 0.0
        if f <= keys[0][0]:
            return keys[0][1]
        if f >= keys[-1][0]:
            return keys[-1][1]
        lo, hi = 0, len(keys) - 1
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if keys[mid][0] <= f:
                lo = mid
            else:
                hi = mid
        f0, v0, t0 = keys[lo]
        f1, v1, t1 = keys[hi]
        d = f1 - f0
        if d <= 0:
            return v0
        u = (f - f0) / d
        u2, u3 = u * u, u * u * u
        return (2 * u3 - 3 * u2 + 1) * v0 + (u3 - 2 * u2 + u) * t0 * d + (-2 * u3 + 3 * u2) * v1 + (u3 - u2) * t1 * d

    def pose(self, bname, f, rest_s, rest_r, rest_t):
        """(scale, rotation degrees xyz, translation) of a bone at frame f; the bone's rest where the
        animation says 'model'; identity / zero / one where it says so."""
        e = self.entries.get(bname)
        if e is None:
            return list(rest_s), list(rest_r), list(rest_t)
        fl = e['flags']
        out = []
        for a in range(9):
            tr = e['attrs'][a]
            if tr is not None:
                out.append(self.eval_track(tr, f))
                continue
            if a < 3:
                out.append(1.0 if fl & (self.SRT_IDENTITY | self.SCL_ONE) else rest_s[a])
            elif a < 6:
                out.append(0.0 if fl & (self.SRT_IDENTITY | self.RT_ZERO | self.ROT_ZERO) else rest_r[a - 3])
            else:
                out.append(0.0 if fl & (self.SRT_IDENTITY | self.RT_ZERO | self.TRANS_ZERO) else rest_t[a - 6])
        return out[0:3], out[3:6], out[6:9]


# ----------------------------------------------------------------- the scene
def scene_from_brres(path, model=None, fps=30.0, report=print, pick=None, scale=1.0):
    rep = report or (lambda *a, **k: None)
    data, label = load_brres_bytes(path, pick)
    arc = Brres(data, label)
    rep('  %s: %d models %s, %d textures, %d palettes, %d chr0' % (label, len(arc.models), list(arc.models)[:4], len(arc.textures), len(arc.palettes), len(arc.chr)))
    M = arc.model(model)
    rep('  MDL0 %s v%d: %d bones, %d objects, %d materials, vert arrays %d, %d mixes, scalingRule %d' % (
        M.name, M.version, len(M.bones), len(M.objects), len(M.materials), len(M.verts), len(M.mixes), M.info['scalingRule']))
    # nodes from the bones; locals from the stored worlds
    nodes = []
    for i, bn in enumerate(M.bones):
        Wp = M.bones[bn['parent']]['world'] if bn['parent'] >= 0 else RW.m4_identity()
        L = RW.m4_mul(RW.m4_inverse(Wp), bn['world'])
        t, q, s = RW.m4_decompose(L)
        nodes.append(dict(name=bn['name'], parent=bn['parent'], kind=1, t=[v * scale for v in t], q=q, s=s))
    W = RW.world_matrices(nodes)
    # the stored matrix vs the SRT: a receipt that the bone block was read right
    worst = 0.0
    for i, bn in enumerate(M.bones):
        Wc = RW.m4_trs([v * scale for v in bn['t']], RW.q_from_euler_xyz(*bn['r']), bn['s'])
        if bn['parent'] >= 0:
            Wc = RW.m4_mul(W[bn['parent']], Wc)
        worst = max(worst, max(abs(Wc[k] - W[i][k]) for k in (12, 13, 14)))
    rep('  bones: stored world vs SRT-from-hierarchy, worst translation gap %.4f (large = Maya scale rule or a misread)' % worst)
    # objects -> meshes
    meshes, textures, texIndex = [], [], {}
    draw_of = {}
    for (mat, obj, bone, prio, xlu) in M.draws:
        draw_of.setdefault(obj, (mat, bone, xlu))

    def tex_slot(name):
        if not name:
            return -1
        if name in texIndex:
            return texIndex[name]
        T = arc.texture(name)
        if T is None:
            rep('  !! material texture %s is not in Textures(NW4R)' % name)
            texIndex[name] = -1
            return -1
        texIndex[name] = len(textures)
        textures.append(T.png())
        rep('  texture %-24s %4dx%-4d %s' % (name, T.width, T.height, TEX_FMT.get(T.fmt, T.fmt)))
        return texIndex[name]

    for oi, obj in enumerate(M.objects):
        if obj['vertId'] < 0 or obj['vertId'] >= len(M.verts):
            rep('  !! object %s: vertex array %d missing' % (obj['name'], obj['vertId']))
            continue
        tris = M.triangles(obj)
        if not tris:
            rep('  !! object %s: no triangles decoded' % obj['name'])
            continue
        P = M.verts[obj['vertId']]['data']
        N = M.normals[obj['nrmId']]['data'] if 0 <= obj['nrmId'] < len(M.normals) else None
        uvId = next((u for u in obj['uvIds'] if u >= 0), -1)
        UV = M.uvs[uvId]['data'] if 0 <= uvId < len(M.uvs) else None
        uvSlot = obj['uvIds'].index(uvId) if uvId >= 0 else 0
        colId = obj['colIds'][0]
        COL = M.colors[colId]['data'] if 0 <= colId < len(M.colors) else None
        col = []
        mat, drawBone, xlu = draw_of.get(obj['index'], (None, 0, False))
        material = M.materials[mat] if mat is not None and mat < len(M.materials) else None
        single = obj['nodeId'] >= 0
        singleBone = M.mtx2bone.get(obj['nodeId'], None) if single else None
        if single and singleBone is None:
            # the node id is a matrix id that might be a mix; or the draw's bone
            infl0 = M.matrix_influences(obj['nodeId'])
            singleBone = infl0[0][0] if infl0 else drawBone
        uniq, pos, nrm, uv, idx, infl = {}, [], [], [], [], []
        for tri in tris:
            corners = []
            for c in tri:
                key = (c['mtx'], c['pos'], c['nrm'], c['uv'][uvSlot] if UV else None, c['col'] if COL else None)
                if key not in uniq:
                    uniq[key] = len(infl)
                    p3 = P[c['pos']] if c['pos'] is not None and c['pos'] < len(P) else [0.0, 0.0, 0.0]
                    p3 = (p3 + [0.0, 0.0, 0.0])[:3]
                    if single:
                        lst = [(singleBone, 1.0)]
                        Wb = W[singleBone]
                        x, y, z = [v * scale for v in p3]
                        pos.extend([Wb[0] * x + Wb[4] * y + Wb[8] * z + Wb[12], Wb[1] * x + Wb[5] * y + Wb[9] * z + Wb[13], Wb[2] * x + Wb[6] * y + Wb[10] * z + Wb[14]])
                        if N and c['nrm'] is not None and c['nrm'] < len(N):
                            a, bb, cc = N[c['nrm']][:3]
                            nx, ny, nz = Wb[0] * a + Wb[4] * bb + Wb[8] * cc, Wb[1] * a + Wb[5] * bb + Wb[9] * cc, Wb[2] * a + Wb[6] * bb + Wb[10] * cc
                            L = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
                            nrm.extend([nx / L, ny / L, nz / L])
                        elif N:
                            nrm.extend([0.0, 1.0, 0.0])
                    else:
                        lst = M.matrix_influences(c['mtx']) if c['mtx'] >= 0 else [(drawBone, 1.0)]
                        if not lst:
                            lst = [(drawBone if drawBone < len(nodes) else 0, 1.0)]
                        pos.extend([v * scale for v in p3])
                        if N and c['nrm'] is not None and c['nrm'] < len(N):
                            nrm.extend(N[c['nrm']][:3])
                        elif N:
                            nrm.extend([0.0, 1.0, 0.0])
                    if UV:
                        u = UV[c['uv'][uvSlot]] if c['uv'][uvSlot] is not None and c['uv'][uvSlot] < len(UV) else [0.0, 0.0]
                        uv.extend([u[0], u[1] if len(u) > 1 else 0.0])
                    if COL:
                        col.extend(COL[c['col']] if c['col'] is not None and c['col'] < len(COL) else [1.0, 1.0, 1.0, 1.0])
                    infl.append(lst)
                corners.append(uniq[key])
            idx.extend(corners)
        nv = len(infl)
        joints, weights, boneMap = RW.skin_from_influences(nv, infl, [nd['parent'] for nd in nodes], report=rep)
        texName = material['layers'][0]['tex'] if material and material['layers'] else ''
        nearest = bool(material and material['layers'] and material['layers'][0]['magF'] == 0)
        meshes.append(dict(name=obj['name'], node=boneMap[0], pos=pos, nrm=nrm if N else None, uv=uv if UV else None, idx=idx,
                           col=col if COL else None,
                           joints=joints, weights=weights, boneMap=boneMap, ibm=[c for bb in boneMap for c in RW.m4_inverse(W[bb])],
                           bind=RW.m4_identity(), tex=tex_slot(texName), ntex=-1, blend=False, nearest=nearest,
                           _xlu=xlu, _mat=material['name'] if material else ''))
        rep('  object %-22s %5d verts %5d tris  %s  bones %d  mat %s  tex %s%s' % (
            obj['name'][:22], nv, len(idx) // 3, 'single:%s' % nodes[singleBone]['name'] if single else 'weighted', len(boneMap),
            material['name'] if material else '-', texName or '-', '  XLU' if xlu else ''))
    for m in meshes:
        if m.pop('_xlu', False) and m['tex'] >= 0:
            m['blend'] = _png_cutout(textures[m['tex']])
    clips = clips_from_archive(arc, M, fps=fps, scale=scale, report=rep)
    return dict(nodes=nodes, meshes=meshes, clips=clips, textures=textures, names=[nd['name'] for nd in nodes], arc=arc, model=M)


def clips_from_archive(arc, M, fps=30.0, scale=1.0, report=print):
    """Every CHR0 in `arc` (a Brres or a path) that names bones of model M -> clips."""
    rep = report or (lambda *a, **k: None)
    if not isinstance(arc, Brres):
        data, label = load_brres_bytes(arc)
        arc = Brres(data, label)
    clips = []
    for cname in arc.chr:
        C = arc.chr0(cname)
        hit = [bn for bn in C.entries if bn in M.name2bone]
        if not hit:
            continue
        frames = max(2, int(round(C.nFrames * fps / 60.0)) + 1) if fps != 60.0 else C.nFrames + 1
        tracks = {}
        for bname in hit:
            bi = M.name2bone[bname]
            bn = M.bones[bi]
            rot, tra, sca = [], [], []
            for f in range(frames):
                sf = f * 60.0 / fps if fps != 60.0 else float(f)
                sf = min(sf, float(C.nFrames))
                s, r, t = C.pose(bname, sf, bn['s'], bn['r'], bn['t'])
                q = RW.q_from_euler_xyz(*r)
                if rot and sum(a * b2 for a, b2 in zip(rot[-4:], q)) < 0:
                    q = [-x for x in q]
                rot.extend(q)
                tra.extend([v * scale for v in t])
                sca.extend(s)
            tracks[(bi, 0)] = rot
            tracks[(bi, 1)] = tra
            if any(abs(sca[i] - 1.0) > 1e-4 for i in range(len(sca))):
                tracks[(bi, 2)] = sca
        clips.append((cname, fps, frames, tracks))
        rep('  chr0 %-24s %d frames @60 -> %d @ %g, %d of %d bones, loop %d' % (cname, C.nFrames, frames, fps, len(hit), len(M.bones), C.loop))
    return clips


def _png_cutout(blob):
    try:
        from PIL import Image
        im = Image.open(io.BytesIO(blob)).convert('RGBA')
        a = im.getchannel('A').resize((64, 64))
        px = list(a.getdata())
        return sum(1 for v in px if v < 128) / float(len(px)) > 0.08
    except Exception:
        return False


# ----------------------------------------------------------------- CLI
def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd, path = argv[1], argv[2]
    data, label = load_brres_bytes(path)
    arc = Brres(data, label)
    if cmd == 'info':
        print('%s: BRRES v%d, %d sections, %d B' % (label, arc.version, arc.nSections, len(data)))
        for fname, ents in arc.folders.items():
            print('  %s: %s' % (fname, ', '.join(n for n, _ in ents)[:400]))
        for mname in arc.models:
            M = arc.model(mname)
            print('  MDL0 %s v%d: %d bones, %d objects, %d materials, %d verts, %d tris (header), %d mixes, scalingRule %d' % (
                mname, M.version, len(M.bones), len(M.objects), len(M.materials), M.info['nVerts'], M.info['nTris'], len(M.mixes), M.info['scalingRule']))
            for i, bn in enumerate(M.bones):
                d, a = 0, bn['parent']
                while a >= 0:
                    d += 1
                    a = M.bones[a]['parent']
                print('    %3d %s%-24s mtx %3d  t (%.2f %.2f %.2f) r (%.1f %.1f %.1f) s (%.2f %.2f %.2f)' % (
                    i, '  ' * d, bn['name'][:24], bn['mtx'], *bn['t'], *bn['r'], *bn['s']))
            for obj in M.objects:
                tris = M.triangles(obj)
                print('    object %-24s vtx %2d nrm %2d uv %s  node %3d  vcd %08x/%08x  %d tris' % (
                    obj['name'][:24], obj['vertId'], obj['nrmId'], obj['uvIds'][:2], obj['nodeId'], obj['vcd'][0], obj['vcd'][1], len(tris)))
            for mt in M.materials:
                print('    material %-24s xlu %d  layers %s' % (mt['name'][:24], mt['xlu'], [l['tex'] for l in mt['layers']]))
        for tname in arc.textures:
            T = arc.texture(tname)
            print('  TEX0 %-24s %4dx%-4d %-7s mips %d%s' % (tname, T.width, T.height, TEX_FMT.get(T.fmt, T.fmt), T.nMips, '  (palette)' if T.hasPalette else ''))
        for cname in arc.chr:
            C = arc.chr0(cname)
            print('  CHR0 %-24s v%d %d frames, %d bones, loop %d' % (cname, C.version, C.nFrames, C.nEntries, C.loop))
        return 0
    if cmd == 'tex':
        outdir = argv[3]
        os.makedirs(outdir, exist_ok=True)
        for tname in arc.textures:
            T = arc.texture(tname)
            fn = os.path.join(outdir, tname + '.png')
            open(fn, 'wb').write(T.png())
            print('  %s %dx%d %s -> %s' % (tname, T.width, T.height, TEX_FMT.get(T.fmt, T.fmt), fn))
        return 0
    if cmd == 'scene':
        sc = scene_from_brres(path, argv[3] if len(argv) > 3 else None)
        print('  nodes %d meshes %d clips %d textures %d' % (len(sc['nodes']), len(sc['meshes']), len(sc['clips']), len(sc['textures'])))
        return 0
    print('unknown command %r' % cmd)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
