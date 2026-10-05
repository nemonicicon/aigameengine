#!/usr/bin/env python3
"""rpackwrite.py -- the ONE writer for RPK1 v2 packs that every new importer shares.

glb2rpack.py (props) and glb2rpack_anim.py (characters) each carry their own copy of the writer.
Those two are proven on pixels and are left alone. Anything written after 2026-10-05 (dae2rpack,
brres2rpack, ...) builds its scene as plain Python here and calls write_pack(), so the layout is
in one place and a loader change is one edit.

Layout (crowd_kms.c loadPack, mirrored by skate_src/author/rpack.py):
  'RPK1' + 15 x u32: ver nNodes nMeshes nMats nClips nTex
                     offNodes offMeshes offMats offClips offTexTable offStr offSize total morphTable
  node   '<IiI3f4f3f'  name parent kind t[3] q[3+w] s[3]       kind 0 plain, 1 bone, 2 mesh node
  mesh   '<15I'        name node verts idxCount boneCount flags offPos offNorm offUV offJoints
                       offWeights offIdx offBoneMap offIBM offBind
                       flags: 1 normals, 2 uvs, 4 drawable, 8 alpha-blend pass
  mat    36 B          mesh u32 @0, flags u32 @16 (1 nearest, 2 flip-v), tex i32 @24, normalTex+1 i32 @28
  clip   '<IfIII'      name fps frames nTracks offTracks;  track '<III' node type(0 rot 1 trans 2 scale) off
  tex    16 B          off u32 @8, len u32 @12 -- an image file (PNG) stb_image decodes at load

The scene handed in:
  nodes    [{name, parent, kind, t:[3], q:[x,y,z,w], s:[3]}]        PARENT-FIRST (asserted)
  meshes   [{name, node, pos:[3n], nrm:[3n]|None, uv:[2n]|None, idx:[k], joints:[[4]*n], weights:[[4]*n],
             boneMap:[node...], ibm:[16*len(boneMap)] (column-major), tex:int|-1, ntex:int|-1,
             blend:bool, bind:[16]|None, nearest:bool, flipv:bool}]
           joints index boneMap; weights sum to 1 (renormalised here); a prop is one bone with weight 1
  clips    [(name, fps, frames, {(node, type): [floats]})]
  textures [bytes]  image files, PNG preferred (stb decodes png/jpg/tga/bmp)

skin_from_influences(nv, infl, parent, maxbones) turns per-vertex [(node, w), ...] lists into the
(joints, weights, boneMap) the mesh record wants, pruning past the engine's MAXB the way
glb2rpack_anim.py does (deforms-nothing first, then deepest, then lightest; a pruned bone's weight
climbs to its nearest surviving ancestor).
"""
import struct, math

MAXB = 256          # the engine's MAXB (measured: 1023 mat4 fit; 256 is the cap the loader enforces)


# ----------------------------------------------------------------- small matrix kit (column-major 16)
def m4_identity():
    return [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]


def m4_mul(a, b):
    o = [0.0] * 16
    for c in range(4):
        for r in range(4):
            o[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3]
    return o


def m4_trs(t, q, s):
    x, y, z, w = q
    return [(1 - 2 * (y * y + z * z)) * s[0], (2 * (x * y + z * w)) * s[0], (2 * (x * z - y * w)) * s[0], 0.0,
            (2 * (x * y - z * w)) * s[1], (1 - 2 * (x * x + z * z)) * s[1], (2 * (y * z + x * w)) * s[1], 0.0,
            (2 * (x * z + y * w)) * s[2], (2 * (y * z - x * w)) * s[2], (1 - 2 * (x * x + y * y)) * s[2], 0.0,
            t[0], t[1], t[2], 1.0]


def m4_inverse(m):
    """General 4x4 inverse (column-major). Returns identity on a singular matrix, loudly."""
    a = m
    inv = [0.0] * 16
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10]
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10]
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9]
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9]
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10]
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10]
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9]
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9]
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6]
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6]
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5]
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5]
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6]
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6]
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5]
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5]
    det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12]
    if abs(det) < 1e-30:
        print('  !! singular matrix inverted -> identity')
        return m4_identity()
    return [v / det for v in inv]


def m4_decompose(m):
    """Column-major 4x4 -> (t, q[xyzw], s). Shear is dropped; a negative determinant flips sz."""
    t = [m[12], m[13], m[14]]
    sx = math.sqrt(m[0] ** 2 + m[1] ** 2 + m[2] ** 2) or 1.0
    sy = math.sqrt(m[4] ** 2 + m[5] ** 2 + m[6] ** 2) or 1.0
    sz = math.sqrt(m[8] ** 2 + m[9] ** 2 + m[10] ** 2) or 1.0
    det = (m[0] * (m[5] * m[10] - m[6] * m[9]) - m[4] * (m[1] * m[10] - m[2] * m[9]) + m[8] * (m[1] * m[6] - m[2] * m[5]))
    if det < 0:
        sz = -sz
    r = [m[0] / sx, m[1] / sx, m[2] / sx, m[4] / sy, m[5] / sy, m[6] / sy, m[8] / sz, m[9] / sz, m[10] / sz]
    tr = r[0] + r[4] + r[8]
    if tr > 0:
        S = math.sqrt(tr + 1.0) * 2
        q = [(r[5] - r[7]) / S, (r[6] - r[2]) / S, (r[1] - r[3]) / S, 0.25 * S]
    elif r[0] > r[4] and r[0] > r[8]:
        S = math.sqrt(1.0 + r[0] - r[4] - r[8]) * 2
        q = [0.25 * S, (r[3] + r[1]) / S, (r[6] + r[2]) / S, (r[5] - r[7]) / S]
    elif r[4] > r[8]:
        S = math.sqrt(1.0 + r[4] - r[0] - r[8]) * 2
        q = [(r[3] + r[1]) / S, 0.25 * S, (r[7] + r[5]) / S, (r[6] - r[2]) / S]
    else:
        S = math.sqrt(1.0 + r[8] - r[0] - r[4]) * 2
        q = [(r[6] + r[2]) / S, (r[7] + r[5]) / S, 0.25 * S, (r[1] - r[3]) / S]
    L = math.sqrt(sum(v * v for v in q)) or 1.0
    return t, [v / L for v in q], [sx, sy, sz]


def q_from_euler_xyz(rx, ry, rz):
    """Degrees, applied X then Y then Z (R = Rz * Ry * Rx) -- Maya / NW4R order."""
    cx, sx = math.cos(math.radians(rx) / 2), math.sin(math.radians(rx) / 2)
    cy, sy = math.cos(math.radians(ry) / 2), math.sin(math.radians(ry) / 2)
    cz, sz = math.cos(math.radians(rz) / 2), math.sin(math.radians(rz) / 2)
    return [sx * cy * cz - cx * sy * sz,
            cx * sy * cz + sx * cy * sz,
            cx * cy * sz - sx * sy * cz,
            cx * cy * cz + sx * sy * sz]


def world_matrices(nodes):
    W = [None] * len(nodes)
    for i, nd in enumerate(nodes):
        L = m4_trs(nd['t'], nd['q'], nd['s'])
        p = nd['parent']
        if p >= 0:
            assert p < i, 'node %d (%s) comes before its parent %d: order the nodes parent-first' % (i, nd['name'], p)
            W[i] = m4_mul(W[p], L)
        else:
            W[i] = L
    return W


def order_parent_first(nodes):
    """Reorder so every parent precedes its children; returns (nodes, remap old->new)."""
    n = len(nodes)
    if all(nd['parent'] < i for i, nd in enumerate(nodes)):
        return nodes, {i: i for i in range(n)}
    order, seen = [], [False] * n
    for i in range(n):
        chain, j = [], i
        while j >= 0 and not seen[j]:
            chain.append(j)
            j = nodes[j]['parent']
        for j in reversed(chain):
            seen[j] = True
            order.append(j)
    remap = {old: new for new, old in enumerate(order)}
    out = []
    for o in order:
        nd = dict(nodes[o])
        nd['parent'] = remap[nd['parent']] if nd['parent'] >= 0 else -1
        out.append(nd)
    return out, remap


# ----------------------------------------------------------------- skinning
def skin_from_influences(nv, infl, parent, maxbones=MAXB, report=print):
    """infl: per vertex a list of (node, weight). Returns (joints [[4]*nv], weights [[4]*nv], boneMap)
    with the bone map pruned to maxbones the way glb2rpack_anim.py prunes."""
    load = {}
    for lst in infl:
        for (nd, w) in lst:
            if w > 0:
                load[nd] = load.get(nd, 0.0) + w
    used = sorted(load)
    keep = set(used)
    if len(used) > maxbones:
        def depth(nd):
            d, j, g = 0, nd, 0
            while j >= 0 and g < 256:
                j = parent[j]
                d += 1
                g += 1
            return d
        order = sorted(used, key=lambda j: (load[j] > 1e-6, -depth(j), load[j]))
        drop = len(used) - maxbones
        for j in order:
            if drop <= 0:
                break
            keep.discard(j)
            drop -= 1
        report('  skin: %d bones carry weight, cap %d -- %d pruned (deepest and lightest first)'
               % (len(used), maxbones, len(used) - len(keep)))
    boneMap = sorted(keep)
    slot = {nd: i for i, nd in enumerate(boneMap)}

    def nearest(nd):
        j, g = nd, 0
        while j >= 0 and g < 256:
            if j in slot:
                return slot[j]
            j = parent[j]
            g += 1
        return 0

    joints, weights = [], []
    for lst in infl:
        acc = {}
        for (nd, w) in lst:
            if w <= 0:
                continue
            sl = slot[nd] if nd in slot else nearest(nd)
            acc[sl] = acc.get(sl, 0.0) + w
        items = sorted(acc.items(), key=lambda x: -x[1])[:4]
        if not items:
            items = [(0, 1.0)]
        tot = sum(w for _, w in items) or 1.0
        j4, w4 = [0, 0, 0, 0], [0.0, 0.0, 0.0, 0.0]
        for k, (sl, w) in enumerate(items):
            j4[k], w4[k] = sl, w / tot
        joints.append(j4)
        weights.append(w4)
    return joints, weights, boneMap


def rigid_skin(nv, node):
    return [[0, 0, 0, 0]] * nv, [[1.0, 0.0, 0.0, 0.0]] * nv, [node]


# ----------------------------------------------------------------- the writer
def write_pack(dst, nodes, meshes, clips, textures, verbose=True):
    nN = len(nodes)
    for i, nd in enumerate(nodes):
        assert nd['parent'] < i, 'node %d (%s) is not parent-first' % (i, nd.get('name'))
    strs, stab = [], {}

    def sid(t):
        t = (t or '').encode('utf8')[:120]
        if t not in stab:
            stab[t] = len(strs)
            strs.append(t)
        return stab[t]

    nodeName = [sid(nd.get('name') or 'node%d' % i) for i, nd in enumerate(nodes)]
    clipName = [sid(c[0]) for c in clips]
    meshName = [sid(m.get('name') or 'mesh%d' % i) for i, m in enumerate(meshes)]
    nM, nTex = len(meshes), len(textures)

    HDRSZ = 4 + 15 * 4
    off = HDRSZ
    offNodes = off; off += 52 * nN
    offMeshes = off; off += 60 * nM
    offMats = off; off += 36 * nM
    offClips = off; off += 20 * len(clips)
    offTexTable = off; off += 16 * nTex
    blobs = []

    def place(b):
        nonlocal off
        o = off
        off += len(b)
        blobs.append(b)
        return o

    for m in meshes:
        nv = len(m['pos']) // 3
        m['_nv'] = nv
        jb, wb = bytearray(nv * 4), bytearray(nv * 4)
        for v in range(nv):
            j4, w4 = m['joints'][v], m['weights'][v]
            tot = sum(w4) or 1.0
            for k in range(4):
                jb[v * 4 + k] = int(j4[k]) & 0xFF
                wb[v * 4 + k] = max(0, min(255, int(round(w4[k] / tot * 255))))
            wb[v * 4] = max(0, min(255, wb[v * 4] + (255 - sum(wb[v * 4:v * 4 + 4]))))
        assert len(m['ibm']) == 16 * len(m['boneMap']), 'mesh %s: %d ibm floats for %d bones' % (m.get('name'), len(m['ibm']), len(m['boneMap']))
        assert len(m['boneMap']) <= MAXB, 'mesh %s: %d bones > MAXB %d' % (m.get('name'), len(m['boneMap']), MAXB)
        m['_offPos'] = place(struct.pack('<%df' % len(m['pos']), *m['pos']))
        m['_offNorm'] = place(struct.pack('<%df' % len(m['nrm']), *m['nrm'])) if m.get('nrm') else 0
        m['_offUV'] = place(struct.pack('<%df' % len(m['uv']), *m['uv'])) if m.get('uv') else 0
        m['_offJ'] = place(bytes(jb))
        m['_offW'] = place(bytes(wb))
        m['_offIdx'] = place(struct.pack('<%dI' % len(m['idx']), *m['idx']))
        m['_offBM'] = place(struct.pack('<%dH' % len(m['boneMap']), *m['boneMap']))
        m['_offIBM'] = place(struct.pack('<%df' % len(m['ibm']), *m['ibm']))
        m['_offBind'] = place(struct.pack('<16f', *(m.get('bind') or m4_identity())))
    texOff = [place(t) for t in textures]

    clipTrackOff = []
    for (_, _, frames, tracks) in clips:
        ents = []
        for (nd, typ), data in sorted(tracks.items()):
            comp = 4 if typ == 0 else 3
            assert len(data) == frames * comp, 'clip track node %d type %d: %d floats != %d x %d' % (nd, typ, len(data), frames, comp)
            ents.append((nd, typ, place(struct.pack('<%df' % len(data), *data))))
        clipTrackOff.append(ents)
    clipTableOff = [place(b''.join(struct.pack('<III', nd, typ, o) for (nd, typ, o) in e)) for e in clipTrackOff]

    tbl = struct.pack('<I', len(strs))
    so = 0
    for t in strs:
        tbl += struct.pack('<II', so, len(t))
        so += len(t)
    tbl += b''.join(strs)
    offStr = place(tbl)
    szb = [0.0] * 12
    szb[7] = float('nan'); szb[9] = float('nan')
    offSize = place(struct.pack('<12f', *szb))
    total = off

    out = bytearray(b'RPK1')
    out += struct.pack('<15I', 2, nN, nM, nM, len(clips), nTex, offNodes, offMeshes, offMats, offClips,
                       offTexTable, offStr, offSize, total, 0)
    for i, nd in enumerate(nodes):
        t, q, s = nd['t'], nd['q'], nd['s']
        out += struct.pack('<IiI3f4f3f', nodeName[i], nd['parent'], nd.get('kind', 0),
                           t[0], t[1], t[2], q[0], q[1], q[2], q[3], s[0], s[1], s[2])
    for mi, m in enumerate(meshes):
        flags = (1 if m.get('nrm') else 0) | (2 if m.get('uv') else 0) | 4 | (8 if m.get('blend') else 0)
        out += struct.pack('<15I', meshName[mi], m['node'], m['_nv'], len(m['idx']), len(m['boneMap']), flags,
                           m['_offPos'], m['_offNorm'], m['_offUV'], m['_offJ'], m['_offW'], m['_offIdx'],
                           m['_offBM'], m['_offIBM'], m['_offBind'])
    for mi, m in enumerate(meshes):
        mat = bytearray(36)
        struct.pack_into('<I', mat, 0, mi)
        struct.pack_into('<I', mat, 16, (1 if m.get('nearest') else 0) | (2 if m.get('flipv') else 0))
        struct.pack_into('<i', mat, 24, m.get('tex', -1) if m.get('tex') is not None else -1)
        struct.pack_into('<i', mat, 28, (m.get('ntex', -1) if m.get('ntex') is not None else -1) + 1)
        out += mat
    for ci, (nm, fps, frames, tracks) in enumerate(clips):
        out += struct.pack('<IfIII', clipName[ci], float(fps), frames, len(clipTrackOff[ci]), clipTableOff[ci])
    for ti in range(nTex):
        t = bytearray(16)
        struct.pack_into('<I', t, 8, texOff[ti])
        struct.pack_into('<I', t, 12, len(textures[ti]))
        out += t
    assert len(out) == HDRSZ + 52 * nN + 60 * nM + 36 * nM + 20 * len(clips) + 16 * nTex
    for b in blobs:
        out += b
    assert len(out) == total
    open(dst, 'wb').write(out)
    if verbose:
        print(dst)
        print('  nodes %d   meshes %d   clips %d   textures %d   %d B' % (nN, nM, len(clips), nTex, len(out)))
        bones = sum(1 for nd in nodes if nd.get('kind') == 1)
        if bones:
            print('  bones %d' % bones)
        for m in meshes:
            print('     mesh %-26s verts %6d  tris %6d  bones %3d  tex %2d%s' % (
                (m.get('name') or '?')[:26], m['_nv'], len(m['idx']) // 3, len(m['boneMap']), m.get('tex', -1),
                '  BLEND' if m.get('blend') else ''))
        for (nm, fps, frames, tracks) in clips:
            print('     clip %-26s %4d frames @ %gfps (%.2fs)  %d tracks' % (nm[:26], frames, fps, frames / fps, len(tracks)))
    return len(out)


def scale_scene(nodes, meshes, clips, s):
    """Uniformly scale a rig-space scene (positions, rest translations, inverse-bind translations,
    translation tracks) by s. Rotations and weights are untouched."""
    for nd in nodes:
        nd['t'] = [v * s for v in nd['t']]
    for m in meshes:
        m['pos'] = [v * s for v in m['pos']]
        ibm = m['ibm']
        for k in range(len(ibm) // 16):
            ibm[k * 16 + 12] *= s
            ibm[k * 16 + 13] *= s
            ibm[k * 16 + 14] *= s
        if m.get('bind'):
            m['bind'][12] *= s; m['bind'][13] *= s; m['bind'][14] *= s
    for (_n, _f, _fr, tracks) in clips:
        for (node, typ), data in list(tracks.items()):
            if typ == 1:
                tracks[(node, typ)] = [v * s for v in data]


def rotate_scene(nodes, meshes, clips, q):
    """Rotate a rig-space scene by the world quaternion q (xyzw): the roots' rest transforms, every
    mesh's positions and normals, the inverse binds (ibm' = ibm * R^-1), and the roots' tracks."""
    R = m4_trs([0, 0, 0], q, [1, 1, 1])
    Rinv = m4_inverse(R)

    def qmul(a, b):
        ax, ay, az, aw = a
        bx, by, bz, bw = b
        return [aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
                aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz]

    def rot3(v):
        return [R[0] * v[0] + R[4] * v[1] + R[8] * v[2], R[1] * v[0] + R[5] * v[1] + R[9] * v[2], R[2] * v[0] + R[6] * v[1] + R[10] * v[2]]
    roots = [i for i, nd in enumerate(nodes) if nd['parent'] < 0]
    for i in roots:
        nodes[i]['t'] = rot3(nodes[i]['t'])
        nodes[i]['q'] = qmul(q, nodes[i]['q'])
    for m in meshes:
        p = m['pos']
        for v in range(len(p) // 3):
            p[v * 3:v * 3 + 3] = rot3(p[v * 3:v * 3 + 3])
        if m.get('nrm'):
            n = m['nrm']
            for v in range(len(n) // 3):
                n[v * 3:v * 3 + 3] = rot3(n[v * 3:v * 3 + 3])
        ibm = m['ibm']
        for k in range(len(ibm) // 16):
            ibm[k * 16:(k + 1) * 16] = m4_mul(ibm[k * 16:(k + 1) * 16], Rinv)
    for (_n, _f, frames, tracks) in clips:
        for (node, typ), data in list(tracks.items()):
            if node not in roots:
                continue
            if typ == 1:
                tracks[(node, typ)] = [c for f in range(frames) for c in rot3(data[f * 3:f * 3 + 3])]
            elif typ == 0:
                tracks[(node, typ)] = [c for f in range(frames) for c in qmul(q, data[f * 4:f * 4 + 4])]


def fit_height(nodes, meshes, clips, height, report=print):
    """Scale the scene so the skinned bind cloud stands `height` tall; returns the factor."""
    lo, hi = bounds_report(nodes, meshes, report=None)
    h = hi[1] - lo[1]
    if h <= 1e-9:
        return 1.0
    s = height / h
    scale_scene(nodes, meshes, clips, s)
    if report:
        report('  height: %.3f -> %.3f (x%.4f)' % (h, height, s))
    return s


def bounds_report(nodes, meshes, report=print):
    """Rig-space bounds of the skinned bind pose: the number a lab frame is checked against."""
    W = world_matrices(nodes)
    lo = [1e30] * 3
    hi = [-1e30] * 3
    n = 0
    for m in meshes:
        pal = [m4_mul(W[b], m['ibm'][k * 16:(k + 1) * 16]) for k, b in enumerate(m['boneMap'])]
        bind = m.get('bind') or m4_identity()
        pos = m['pos']
        for v in range(len(pos) // 3):
            x, y, z = pos[v * 3], pos[v * 3 + 1], pos[v * 3 + 2]
            bx = bind[0] * x + bind[4] * y + bind[8] * z + bind[12]
            by = bind[1] * x + bind[5] * y + bind[9] * z + bind[13]
            bz = bind[2] * x + bind[6] * y + bind[10] * z + bind[14]
            ox = oy = oz = 0.0
            for k in range(4):
                w = m['weights'][v][k]
                if w <= 0:
                    continue
                M = pal[m['joints'][v][k]]
                ox += w * (M[0] * bx + M[4] * by + M[8] * bz + M[12])
                oy += w * (M[1] * bx + M[5] * by + M[9] * bz + M[13])
                oz += w * (M[2] * bx + M[6] * by + M[10] * bz + M[14])
            for i, c in enumerate((ox, oy, oz)):
                lo[i] = min(lo[i], c)
                hi[i] = max(hi[i], c)
            n += 1
    if n and report:
        report('  skinned bind pose: x %.3f..%.3f  y %.3f..%.3f  z %.3f..%.3f  (%d verts)'
               % (lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], n))
    return lo, hi


def to_png(rgba, w, h):
    """RGBA bytes -> PNG bytes (Pillow when present, else a stored zlib PNG)."""
    try:
        from PIL import Image
        import io
        im = Image.frombytes('RGBA', (w, h), bytes(rgba))
        bio = io.BytesIO()
        im.save(bio, 'PNG')
        return bio.getvalue()
    except ImportError:
        import zlib
        raw = b''.join(b'\x00' + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))

        def chunk(tag, data):
            return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
        return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
