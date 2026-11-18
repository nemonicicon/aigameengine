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

def collect_parts(js, bin_):
    """(11-16) every glTF MESH as a part of its own, named as the mesh is: [(name, P, N, UV, IDX, have_n, have_uv)], in the order the scene
    first reaches each. A prop made of more than one material (round glasses: plastic rims, and lenses that are glass) is one pack of several
    meshes, each with its own name, so the engine knows what each is made of by its words (engine/load.inc packMaterials). collect() above
    still merges everything, and a .glb of one mesh is written exactly as before"""
    order, parts = [], {}
    scene = js.get('scenes', [{}])[js.get('scene', 0)]
    stack = [(r, [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]) for r in reversed(scene.get('nodes', []))]
    mats = js.get('materials', [])
    while stack:
        ni, parent = stack.pop()
        nd = js['nodes'][ni]
        world = mul(parent, node_matrix(nd))
        if 'mesh' in nd:
            mi = nd['mesh']
            # (11-16) a glTF mesh of several materials is a part per material (the skate game's board: its deck is grip tape, maple and the deck's
            # picture, three materials and two pictures, and as one part it took the grip's picture all over, so its underside was black). A mesh of
            # one material is one part, named as it was
            used = sorted({p.get('material', -1) for p in js['meshes'][mi].get('primitives', []) if p.get('mode', 4) == 4})
            for prim in js['meshes'][mi].get('primitives', []):
                if prim.get('mode', 4) != 4: continue
                att = prim.get('attributes', {})
                if 'POSITION' not in att: continue
                mt = prim.get('material', -1)
                key = (mi, mt if len(used) > 1 else None)
                if key not in parts:
                    nm = js['meshes'][mi].get('name') or ('mesh%d' % mi)
                    if len(used) > 1:
                        nm += '_' + ''.join(ch if ch.isalnum() else '_' for ch in (mats[mt].get('name', 'mat%d' % mt) if 0 <= mt < len(mats) else 'mat%d' % mt))
                    parts[key] = dict(name=nm, P=[], N=[], UV=[], IDX=[], have_n=False, have_uv=False, mat=mt, split=len(used) > 1); order.append(key)
                g = parts[key]
                pos = accessor(js, bin_, att['POSITION']); cnt = len(pos)//3; base = len(g['P'])//3
                for i in range(cnt): g['P'].extend(xform(world, pos[i*3:i*3+3]))
                if 'NORMAL' in att:
                    g['have_n'] = True; nor = accessor(js, bin_, att['NORMAL'])
                    for i in range(cnt):
                        d = xform_dir(world, nor[i*3:i*3+3]); L = (d[0]*d[0]+d[1]*d[1]+d[2]*d[2]) ** 0.5 or 1.0
                        g['N'].extend((d[0]/L, d[1]/L, d[2]/L))
                else: g['N'].extend([0.0, 1.0, 0.0] * cnt)
                if 'TEXCOORD_0' in att: g['have_uv'] = True; g['UV'].extend(accessor(js, bin_, att['TEXCOORD_0']))
                else: g['UV'].extend([0.0, 0.0] * cnt)
                if 'indices' in prim: g['IDX'].extend(i + base for i in accessor(js, bin_, prim['indices']))
                else: g['IDX'].extend(range(base, base + cnt))
        for c in reversed(nd.get('children', [])): stack.append((c, world))
    return [(parts[m]['name'], parts[m]['P'], parts[m]['N'], parts[m]['UV'], parts[m]['IDX'], parts[m]['have_n'], parts[m]['have_uv'], parts[m]['mat'], parts[m]['split'])
            for m in order if parts[m]['P']]


def image_bytes(js, bin_, ii, src):
    """an image of the file, its bytes: in the binary, a data URI, or a file beside the .glb"""
    im = js['images'][ii]
    if 'bufferView' in im:
        bv = js['bufferViews'][im['bufferView']]; o = bv.get('byteOffset', 0); return bin_[o:o+bv['byteLength']]
    if im.get('uri', '').startswith('data:'):
        return base64.b64decode(im['uri'].split(',', 1)[1])
    if im.get('uri'):
        import urllib.parse
        side = os.path.join(os.path.dirname(os.path.abspath(src)), urllib.parse.unquote(im['uri']))
        if os.path.exists(side): return open(side, 'rb').read()
    return b''


def part_textures(a, js, bin_, parts):
    """(11-16) each part's picture: its own material's, as the file says, one copy of each picture in the pack. A part of a split mesh with no picture
    of its own is drawn in its material's colour (the file's is linear light, a name's as it's seen, as glb2rpack_anim.py writes it); a part of a
    mesh of one material with none takes the file's first picture, as every part did before. --tex is the one picture for every part, as before.
    Returns (pictures, each part's picture or -1, the parts renamed)"""
    if a.tex:
        return [open(a.tex, 'rb').read()], [0] * len(parts), parts
    mats, texs, pics, seen = js.get('materials', []), js.get('textures', []), [], {}
    def pic(ii):
        if ii not in seen:
            b = image_bytes(js, bin_, ii, a.src)
            seen[ii] = len(pics) if b else -1
            if b: pics.append(b)
        return seen[ii]
    out, renamed = [], []
    for (n, P, N, UV, I, hn, hu, mt, split) in parts:
        m = mats[mt] if 0 <= mt < len(mats) else {}
        pbr = m.get('pbrMetallicRoughness', {})
        ti = pbr.get('baseColorTexture', {}).get('index')
        src = texs[ti].get('source') if ti is not None and ti < len(texs) else None
        t = pic(src) if (src is not None and hu) else -1
        if t < 0 and not split and hu and js.get('images'):
            t = pic(0)
        if t < 0 and split and '#' not in n:
            fac = pbr.get('baseColorFactor', [1.0, 1.0, 1.0, 1.0])
            n = '%s#%02x%02x%02x' % ((n,) + tuple(max(0, min(255, int(round((max(0.0, c) ** (1.0 / 2.2)) * 255)))) for c in fac[:3]))
        out.append(t); renamed.append((n, P, N, UV, I, hn, hu))
    return pics, out, renamed


def write_parts(a, parts, pics, ptex):
    """(11-16) a pack of several meshes, one per part, each named as its glTF mesh is. A part whose name carries a colour (`#rrggbb`) is drawn in that
    colour, untextured; `#rrggbbAA` with AA under ff makes it sheer (flag 8, drawn blended, last: engine/load.inc); everything else takes its
    picture (pics[ptex[k]], part_textures). The rest is the one-mesh pack's: one joint at the origin, every vertex on it, the markers"""
    import re
    if a.scale != 1.0: parts = [(n, [v * a.scale for v in P], N, UV, I, hn, hu) for (n, P, N, UV, I, hn, hu) in parts]
    allP = [v for p in parts for v in p[1]]
    mnx, mxx, mny, mxy, mnz, mxz = min(allP[0::3]), max(allP[0::3]), min(allP[1::3]), max(allP[1::3]), min(allP[2::3]), max(allP[2::3])
    dx = -(mnx+mxx)/2 if a.center else 0.0; dy = -mny if a.ground else 0.0; dz = -(mnz+mxz)/2 if a.center else 0.0
    if dx or dy or dz: parts = [(n, [v + (dx, dy, dz)[i % 3] for i, v in enumerate(P)], N, UV, I, hn, hu) for (n, P, N, UV, I, hn, hu) in parts]
    nTex = len(pics)
    names = [b'root'] + [m.encode('ascii') for m in a.marker] + [p[0].encode('ascii') for p in parts]
    nM = len(parts); maxv = max(len(p[1])//3 for p in parts)
    HDR = 4 + 15*4; off = HDR; nNodes = 1 + len(a.marker)
    offNodes = off; off += 52 * nNodes
    offMeshes = off; off += 60 * nM
    offMats = off; off += 36 * nM
    offClips = off; offTexTable = off; off += 16*nTex
    body = []
    def place(b):
        nonlocal off
        o = off; off += len(b); body.append(b); return o
    recs, mats = [], []
    joi_b = bytes(maxv*4); wei_b = bytes([255,0,0,0]) * maxv   # one block each, as long as the longest part: every vertex on the one joint
    offJoints = place(joi_b); offWeights = place(wei_b)
    ident = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
    offBoneMap = place(struct.pack('<I', 0)); offIBM = place(struct.pack('<16f', *ident)); offBind = place(struct.pack('<16f', *ident))
    for k, (n, P, N, UV, I, hn, hu) in enumerate(parts):
        nv = len(P)//3; m = re.search(r'#([0-9a-fA-F]{6})([0-9a-fA-F]{2})?', n)
        textured = bool(ptex[k] >= 0 and hu and not m)
        sheer = bool(m and m.group(2) and int(m.group(2), 16) < 255)
        flags = (1 if hn else 0) | (2 if textured else 0) | 4 | (8 if sheer else 0)
        oP = place(struct.pack('<%df' % (nv*3), *P)); oN = place(struct.pack('<%df' % (nv*3), *N)); oU = place(struct.pack('<%df' % (nv*2), *UV)); oI = place(struct.pack('<%dI' % len(I), *I))
        recs.append(struct.pack('<15I', 1 + len(a.marker) + k, 0, nv, len(I), 1, flags, oP, oN, oU, offJoints, offWeights, oI, offBoneMap, offIBM, offBind))
        mat = bytearray(36); struct.pack_into('<I', mat, 0, k); struct.pack_into('<I', mat, 16, 0); struct.pack_into('<i', mat, 24, ptex[k] if textured else -1); mats.append(bytes(mat))
        print(f"  part {k}: {n}   vertices {nv:,}   triangles {len(I)//3:,}" + ("   sheer" if sheer else "") + (f"   picture {ptex[k]}" if textured else "   its name's colour"))
    offTexData = [place(b) for b in pics]
    tbl = struct.pack('<I', len(names)); so = 0
    for nm in names: tbl += struct.pack('<II', so, len(nm)); so += len(nm)
    tbl += b''.join(names); offStr = place(tbl)
    szb = [0.0]*12; szb[7] = float('nan'); szb[9] = float('nan'); offSize = place(struct.pack('<12f', *szb))
    out = bytearray(b'RPK1')
    out += struct.pack('<15I', 2, nNodes, nM, nM, 0, nTex, offNodes, offMeshes, offMats, offClips, offTexTable, offStr, offSize, off, 0)
    out += struct.pack('<IiI3f4f3f', 0, -1, 1, 0,0,0, 0,0,0,1, 1,1,1)
    for k in range(len(a.marker)): out += struct.pack('<IiI3f4f3f', k + 1, 0, 1, 0,0,0, 0,0,0,1, 1,1,1)
    for r in recs: out += r
    for m in mats: out += m
    for i, b in enumerate(pics): t = bytearray(16); struct.pack_into('<I', t, 8, offTexData[i]); struct.pack_into('<I', t, 12, len(b)); out += t
    assert len(out) == HDR + 52 * nNodes + 60 * nM + 36 * nM + 16 * nTex
    for b in body: out += b
    assert len(out) == off
    open(a.dst, 'wb').write(out)
    print(f"{a.dst}\n  {nM} meshes   file {len(out):,} B" + (f"   markers {' '.join(a.marker)}" if a.marker else ""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src'); ap.add_argument('dst')
    ap.add_argument('--scale', type=float, default=1.0)
    ap.add_argument('--tex', default=None, help='external image to embed instead of the GLB one')
    ap.add_argument('--ground', action='store_true', help='shift so min Y sits at 0')
    ap.add_argument('--center', action='store_true', help='center on X/Z')
    # a MARKER is a node the engine reads by name (loadPack): ToonStyle (the space series' toon look), ToonSet (that look
    # lit as a room is lit), Glow (unlit at its own colour), NoCast (out of the sun's shadow map). Each is a kind-1
    # node at the origin under root, so the bounds (span 0, scale 1) and the grounding are what they were.
    ap.add_argument('--marker', action='append', default=[], help='a named marker node (repeatable)')
    a = ap.parse_args()

    js, bin_ = read_glb(a.src)
    parts = collect_parts(js, bin_)
    if len(parts) > 1:   # (11-16) a prop of several materials keeps its meshes apart, each with its own picture
        pics, ptex, parts = part_textures(a, js, bin_, parts)
        write_parts(a, parts, pics, ptex); return
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
