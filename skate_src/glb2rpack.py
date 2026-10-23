#!/usr/bin/env python3
"""glb2rpack.py — bake a STATIC glTF/GLB mesh into the engine's RPK1 v2 format.

Written 2026-08-21 by reading loadPack() in crowd_kms.c, because the original baker
lived on reva and was not copied before she was powered down.

RPK1 v2 layout, as the loader reads it:
    "RPK1"                     4 bytes
    Hd[0]  version             (u32, from pk+4 onward)
    Hd[1]  nNodes
    Hd[2]  nMeshes
    Hd[3]  nMats
    Hd[4]  nClips
    Hd[5]  nTex
    Hd[6]  offNodes    -> PNode[]  packed 52B: name u32, parent i32, kind u32, t[3] q[4] s[3] f32
    Hd[7]  offMeshes   -> PMesh[]  15 x u32: name node verts idxCount boneCount flags
                                   offPos offNorm offUV offJoints offWeights offIdx
                                   offBoneMap offIBM offBind
    Hd[8]  offMats     -> 36B records: mesh u32 @0, flags u32 @16, tex i32 @24
    Hd[9]  offClips    -> PClip[]  packed 20B  (none here: static prop)
    Hd[10] offTexTable -> 16B entries: off u32 @8, len u32 @12  (image bytes embedded whole)

Mesh flags: bit0 = has normals, bit1 = has UVs.
Vertex data: pos verts*12, norm verts*12, uv verts*8, idx idxCount*4 (GL_UNSIGNED_INT).
Joints/weights are bound unconditionally by the renderer, so a static prop still needs
them — every vertex is pinned to node 0 with full weight.

Usage:
    glb2rpack.py <in.glb> <out.rpack> [--scale S] [--yup] [--tex FILE]
"""
import sys, os, json, struct, base64, argparse

def read_glb(path):
    d = open(path, 'rb').read()
    if d[:4] != b'glTF':
        # plain .gltf + external buffers is not supported here on purpose: the assets
        # we bake are all single-file GLB, and supporting both doubles the failure modes.
        raise SystemExit("not a binary GLB: " + path)
    off, js, bin_ = 12, None, b''
    while off < len(d):
        clen, ctype = struct.unpack('<I4s', d[off:off+8])
        chunk = d[off+8:off+8+clen]
        if ctype == b'JSON': js = json.loads(chunk)
        elif ctype[:3] == b'BIN': bin_ = chunk
        off += 8 + clen
        off += (-off) % 4
    return js, bin_

CTYPE = {5120:('b',1), 5121:('B',1), 5122:('h',2), 5123:('H',2), 5125:('I',4), 5126:('f',4)}
NCOMP = {'SCALAR':1, 'VEC2':2, 'VEC3':3, 'VEC4':4, 'MAT4':16}

def accessor(js, bin_, idx):
    """Return a flat list of numbers for accessor idx, honouring byteStride."""
    a = js['accessors'][idx]
    fmt, sz = CTYPE[a['componentType']]
    n = NCOMP[a['type']]
    count = a['count']
    if 'bufferView' not in a:
        return [0] * (count * n)
    bv = js['bufferViews'][a['bufferView']]
    base = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
    stride = bv.get('byteStride') or (sz * n)
    out = []
    for i in range(count):
        o = base + i * stride
        out.extend(struct.unpack_from('<' + fmt * n, bin_, o))
    return out

def node_matrix(nd):
    """Local matrix for a glTF node, column-major 4x4 as a flat list."""
    if 'matrix' in nd:
        return list(nd['matrix'])
    import math
    t = nd.get('translation', [0, 0, 0])
    r = nd.get('rotation', [0, 0, 0, 1])   # xyzw
    s = nd.get('scale', [1, 1, 1])
    x, y, z, w = r
    m = [
        (1-2*(y*y+z*z))*s[0], (2*(x*y+z*w))*s[0],   (2*(x*z-y*w))*s[0],   0,
        (2*(x*y-z*w))*s[1],   (1-2*(x*x+z*z))*s[1], (2*(y*z+x*w))*s[1],   0,
        (2*(x*z+y*w))*s[2],   (2*(y*z-x*w))*s[2],   (1-2*(x*x+y*y))*s[2], 0,
        t[0], t[1], t[2], 1]
    return m

def mul(a, b):
    o = [0.0]*16
    for c in range(4):
        for r in range(4):
            o[c*4+r] = sum(a[k*4+r] * b[c*4+k] for k in range(4))
    return o

def xform(m, p):
    x, y, z = p
    return (m[0]*x + m[4]*y + m[8]*z + m[12],
            m[1]*x + m[5]*y + m[9]*z + m[13],
            m[2]*x + m[6]*y + m[10]*z + m[14])

def xform_dir(m, p):
    x, y, z = p
    return (m[0]*x + m[4]*y + m[8]*z,
            m[1]*x + m[5]*y + m[9]*z,
            m[2]*x + m[6]*y + m[10]*z)

def collect(js, bin_):
    """Flatten every primitive in the scene into one merged mesh, applying node transforms."""
    P, N, UV, IDX = [], [], [], []
    have_n = have_uv = False
    scene = js.get('scenes', [{}])[js.get('scene', 0)]
    stack = [(r, [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]) for r in scene.get('nodes', [])]
    while stack:
        ni, parent = stack.pop()
        nd = js['nodes'][ni]
        world = mul(parent, node_matrix(nd))
        if 'mesh' in nd:
            for prim in js['meshes'][nd['mesh']].get('primitives', []):
                if prim.get('mode', 4) != 4:      # triangles only
                    continue
                att = prim.get('attributes', {})
                if 'POSITION' not in att: continue
                pos = accessor(js, bin_, att['POSITION'])
                cnt = len(pos)//3
                base = len(P)//3
                for i in range(cnt):
                    P.extend(xform(world, pos[i*3:i*3+3]))
                if 'NORMAL' in att:
                    have_n = True
                    nor = accessor(js, bin_, att['NORMAL'])
                    for i in range(cnt):
                        d = xform_dir(world, nor[i*3:i*3+3])
                        L = (d[0]*d[0]+d[1]*d[1]+d[2]*d[2]) ** 0.5 or 1.0
                        N.extend((d[0]/L, d[1]/L, d[2]/L))
                else:
                    N.extend([0.0, 1.0, 0.0] * cnt)
                if 'TEXCOORD_0' in att:
                    have_uv = True
                    UV.extend(accessor(js, bin_, att['TEXCOORD_0']))
                else:
                    UV.extend([0.0, 0.0] * cnt)
                if 'indices' in prim:
                    IDX.extend(i + base for i in accessor(js, bin_, prim['indices']))
                else:
                    IDX.extend(range(base, base + cnt))
        for c in nd.get('children', []):
            stack.append((c, world))
    return P, N, UV, IDX, have_n, have_uv

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src'); ap.add_argument('dst')
    ap.add_argument('--scale', type=float, default=1.0)
    ap.add_argument('--tex', default=None, help='external image to embed instead of the GLB one')
    ap.add_argument('--ground', action='store_true', help='shift so min Y sits at 0')
    ap.add_argument('--center', action='store_true', help='center on X/Z')
    # a MARKER is a node the engine reads by name (loadPack): ToonStyle (the WATEOT toon look), ToonSet (that look
    # lit as a room is lit), Glow (unlit at its own colour), NoCast (out of the sun's shadow map). Each is a kind-1
    # node at the origin under root, so the bounds (span 0, scale 1) and the grounding are what they were.
    ap.add_argument('--marker', action='append', default=[], help='a named marker node (repeatable)')
    a = ap.parse_args()

    js, bin_ = read_glb(a.src)
    P, N, UV, IDX, have_n, have_uv = collect(js, bin_)
    nv = len(P)//3
    if nv == 0: raise SystemExit("no triangle geometry found")

    # scale, then optional ground/center — done here so the engine gets it baked in
    if a.scale != 1.0:
        P = [v * a.scale for v in P]
    mnx = min(P[0::3]); mxx = max(P[0::3])
    mny = min(P[1::3]); mxy = max(P[1::3])
    mnz = min(P[2::3]); mxz = max(P[2::3])
    dx = -(mnx+mxx)/2 if a.center else 0.0
    dy = -mny if a.ground else 0.0
    dz = -(mnz+mxz)/2 if a.center else 0.0
    if dx or dy or dz:
        for i in range(nv):
            P[i*3] += dx; P[i*3+1] += dy; P[i*3+2] += dz

    # texture: prefer an explicit file, else the first embedded image
    tex_bytes = b''
    if a.tex:
        tex_bytes = open(a.tex, 'rb').read()
    elif js.get('images'):
        im = js['images'][0]
        if 'bufferView' in im:
            bv = js['bufferViews'][im['bufferView']]
            o = bv.get('byteOffset', 0)
            tex_bytes = bin_[o:o+bv['byteLength']]
        elif im.get('uri', '').startswith('data:'):
            tex_bytes = base64.b64decode(im['uri'].split(',', 1)[1])
        elif im.get('uri'):
            # external image, resolved next to the GLB -- glTF's normal case for
            # scanned assets, and silently baked as no texture before this branch
            import urllib.parse
            side = os.path.join(os.path.dirname(os.path.abspath(a.src)),
                                urllib.parse.unquote(im['uri']))
            if os.path.exists(side):
                tex_bytes = open(side, 'rb').read()
    nTex = 1 if tex_bytes else 0

    # bit0 normals, bit1 UVs, bit2 DRAWABLE -- the renderer skips any mesh without
    # bit2 and reports the pack as INVISIBLE, so it is not optional.
    flags = (1 if have_n else 0) | (2 if have_uv else 0) | 4

    # ---- assemble ----
    blobs = []           # (name, bytes) appended after the tables
    def add(b):
        blobs.append(b); return b

    pos_b = struct.pack('<%df' % (nv*3), *P)
    nor_b = struct.pack('<%df' % (nv*3), *N)
    uv_b  = struct.pack('<%df' % (nv*2), *UV)
    idx_b = struct.pack('<%dI' % len(IDX), *IDX)
    joi_b = bytes(nv*4)                       # every vertex -> node 0
    wei_b = bytes([255,0,0,0]) * nv           # full weight on that one bone
    bmap_b = struct.pack('<I', 0)             # bone map: one entry, node 0
    ident = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
    ibm_b = struct.pack('<16f', *ident)
    bind_b = struct.pack('<16f', *ident)

    # The header is 15 words after the magic, not 11: every shipped pack has
    # offNodes == 64. Hd[11] string table, Hd[12] sizing block, Hd[13] total size.
    HDR = 4 + 15*4
    off = HDR
    nNodes = 1 + len(a.marker)
    offNodes = off; off += 52 * nNodes
    offMeshes = off; off += 60
    offMats = off; off += 36
    offClips = off                                  # zero clips: table is empty
    offTexTable = off; off += 16*nTex

    def place(b):
        nonlocal off
        o = off; off += len(b)
        return o
    offPos = place(pos_b); offNorm = place(nor_b); offUV = place(uv_b)
    offJoints = place(joi_b); offWeights = place(wei_b); offIdx = place(idx_b)
    offBoneMap = place(bmap_b); offIBM = place(ibm_b); offBind = place(bind_b)
    offTexData = place(tex_bytes) if nTex else 0

    # String table, exactly as packStr() reads it: u32 nStr, then nStr pairs of
    # {u32 byte-offset, u32 length}, then the characters. Node/clip `name` fields
    # are INDICES into this table, not byte offsets.
    names = [b'root'] + [m.encode('ascii') for m in a.marker]
    tbl = struct.pack('<I', len(names))
    so = 0
    for n in names:
        tbl += struct.pack('<II', so, len(n)); so += len(n)
    tbl += b''.join(names)
    offStr = place(tbl)

    # Sizing block: 12 floats of bench-measured rig data. A baked prop has none,
    # so NaN the two the loader reads -- szb[7] bind-Y sole and szb[9] mesh height,
    # both guarded by x==x -- and leave the measured flag szb[10] clear, which keeps
    # the loader on its ordinary bounds-derived grounding.
    szb = [0.0]*12
    szb[7] = float('nan'); szb[9] = float('nan'); szb[10] = 0.0
    szb_b = struct.pack('<12f', *szb)
    offSize = place(szb_b)
    total = off

    out = bytearray()
    out += b'RPK1'
    out += struct.pack('<15I', 2, nNodes, 1, 1, 0, nTex,
                       offNodes, offMeshes, offMats, offClips, offTexTable,
                       offStr, offSize, total, 0)
    # One node: identity TRS at the origin, kind=1. kind 1 means 'joint', and the
    # loader derives pack bounds from the kind-1 nodes alone -- a kind-0 node leaves
    # the bounds at their +-1e9 sentinels, which poisons scale and grounding. One
    # joint at the origin gives span 0, so the loader's height normalisation is
    # skipped (scale stays 1.0) and the prop keeps the size it was baked at.
    out += struct.pack('<IiI3f4f3f', 0, -1, 1, 0,0,0, 0,0,0,1, 1,1,1)
    for k in range(len(a.marker)):
        out += struct.pack('<IiI3f4f3f', k + 1, 0, 1, 0,0,0, 0,0,0,1, 1,1,1)
    # one mesh
    out += struct.pack('<15I', 0, 0, nv, len(IDX), 1, flags,
                       offPos, offNorm, offUV, offJoints, offWeights, offIdx,
                       offBoneMap, offIBM, offBind)
    # one material: mesh 0, no special flags, texture 0 (or -1 when untextured)
    mat = bytearray(36)
    struct.pack_into('<I', mat, 0, 0)
    struct.pack_into('<I', mat, 16, 0)
    struct.pack_into('<i', mat, 24, 0 if nTex else -1)
    out += mat
    if nTex:
        t = bytearray(16)
        struct.pack_into('<I', t, 8, offTexData)
        struct.pack_into('<I', t, 12, len(tex_bytes))
        out += t
    for b in (pos_b, nor_b, uv_b, joi_b, wei_b, idx_b, bmap_b, ibm_b, bind_b):
        out += b
    if nTex: out += tex_bytes
    out += tbl
    out += szb_b

    open(a.dst, 'wb').write(out)
    print(f"{a.dst}")
    print(f"  vertices {nv:,}   triangles {len(IDX)//3:,}   texture {len(tex_bytes):,} B")
    print(f"  bounds   X {min(P[0::3]):.2f}..{max(P[0::3]):.2f}"
          f"   Y {min(P[1::3]):.2f}..{max(P[1::3]):.2f}"
          f"   Z {min(P[2::3]):.2f}..{max(P[2::3]):.2f}")
    print(f"  flags    normals={bool(flags&1)} uvs={bool(flags&2)}   file {len(out):,} B"
          + (f"   markers {' '.join(a.marker)}" if a.marker else ""))

main()
