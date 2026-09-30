#!/usr/bin/env python3
"""glb2rpack_anim.py -- bake a SKINNED, ANIMATED glTF/GLB into the engine's RPK1 v2 format.

The static baker (glb2rpack.py) writes props: one bone, no clips. This one writes characters:
the full node hierarchy, a skin with its inverse bind matrices, and animation clips.

Two things about the target format drove the design:

  * Clips are stored BAKED PER FRAME, not as keyframes with times. The loader does
        f2 = ((unsigned)phase) % frames;  memcpy(Q[node], data + f2*4, 16);
    so every animated channel becomes a flat per-frame array. glTF samplers are therefore
    resampled onto a fixed frame rate here rather than interpolated at runtime.

  * The renderer binds joints and weights as GL_UNSIGNED_BYTE, and the shader has pal[64].
    So a skin may use at most 64 bones and every index must fit in a byte. Rigs routinely
    exceed that on finger bones alone, so bones are PRUNED by how much weight they actually
    carry, and a pruned bone's weight is handed to its nearest surviving ancestor -- which is
    what a finger would do anyway if you only had a hand.

Usage:
    glb2rpack_anim.py <in.glb> <out.rpack> [--fps 30] [--scale S] [--maxbones 64]
"""
import sys, os, json, struct, base64, argparse, math

# ⭐ MATCHES THE ENGINE'S MAXB, which was raised from 64 to 256 after MEASURING the
# hardware (MAX_VERTEX_UNIFORM_VECTORS=4095 = 1023 mat4). At 64 this pruned 57 of a Daz
# rig's 92 facial joints to fit, deepest-first -- which is most of a face.
MAXB = 256


# ----------------------------------------------------------------- glTF reading
def read_glb(path):
    d = open(path, 'rb').read()
    if d[:4] != b'glTF':
        raise SystemExit('not a binary GLB: ' + path)
    off, js, bin_ = 12, None, b''
    while off < len(d):
        clen, ctype = struct.unpack('<I4s', d[off:off + 8])
        chunk = d[off + 8:off + 8 + clen]
        if ctype == b'JSON':
            js = json.loads(chunk)
        elif ctype[:3] == b'BIN':
            bin_ = chunk
        off += 8 + clen
        off += (-off) % 4
    return js, bin_


CTYPE = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2), 5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
NCOMP = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


def accessor(js, bin_, idx):
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
        out.extend(struct.unpack_from('<' + fmt * n, bin_, base + i * stride))
    return out


# ----------------------------------------------------------------- math
def mat_mul(a, b):
    o = [0.0] * 16
    for c in range(4):
        for r in range(4):
            o[c * 4 + r] = sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4))
    return o


def node_trs(nd):
    """Return (t, q, s) for a node; a baked matrix is decomposed."""
    if 'matrix' in nd:
        m = nd['matrix']
        t = [m[12], m[13], m[14]]
        sx = math.sqrt(m[0] ** 2 + m[1] ** 2 + m[2] ** 2) or 1.0
        sy = math.sqrt(m[4] ** 2 + m[5] ** 2 + m[6] ** 2) or 1.0
        sz = math.sqrt(m[8] ** 2 + m[9] ** 2 + m[10] ** 2) or 1.0
        r = [m[0] / sx, m[1] / sx, m[2] / sx,
             m[4] / sy, m[5] / sy, m[6] / sy,
             m[8] / sz, m[9] / sz, m[10] / sz]
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
        return t, q, [sx, sy, sz]
    return (list(nd.get('translation', [0, 0, 0])),
            list(nd.get('rotation', [0, 0, 0, 1])),
            list(nd.get('scale', [1, 1, 1])))


def trs_matrix(t, q, s):
    x, y, z, w = q
    return [(1 - 2 * (y * y + z * z)) * s[0], (2 * (x * y + z * w)) * s[0], (2 * (x * z - y * w)) * s[0], 0.0,
            (2 * (x * y - z * w)) * s[1], (1 - 2 * (x * x + z * z)) * s[1], (2 * (y * z + x * w)) * s[1], 0.0,
            (2 * (x * z + y * w)) * s[2], (2 * (y * z - x * w)) * s[2], (1 - 2 * (x * x + y * y)) * s[2], 0.0,
            t[0], t[1], t[2], 1.0]


def qslerp(a, b, t):
    d = sum(a[i] * b[i] for i in range(4))
    if d < 0:
        b = [-v for v in b]
        d = -d
    if d > 0.9995:
        r = [a[i] + (b[i] - a[i]) * t for i in range(4)]
    else:
        th = math.acos(max(-1.0, min(1.0, d)))
        st = math.sin(th)
        w1, w2 = math.sin((1 - t) * th) / st, math.sin(t * th) / st
        r = [a[i] * w1 + b[i] * w2 for i in range(4)]
    L = math.sqrt(sum(v * v for v in r)) or 1.0
    return [v / L for v in r]


def sample_track(times, values, ncomp, t, is_quat):
    """Sample a glTF sampler at time t (LINEAR; STEP is close enough at 30fps)."""
    n = len(times)
    if n == 0:
        return None
    if t <= times[0]:
        return values[0:ncomp]
    if t >= times[-1]:
        return values[(n - 1) * ncomp:n * ncomp]
    lo, hi = 0, n - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if times[mid] <= t:
            lo = mid
        else:
            hi = mid
    span = times[hi] - times[lo]
    u = 0.0 if span <= 0 else (t - times[lo]) / span
    a = values[lo * ncomp:(lo + 1) * ncomp]
    b = values[hi * ncomp:(hi + 1) * ncomp]
    if is_quat:
        return qslerp(a, b, u)
    return [a[i] + (b[i] - a[i]) * u for i in range(ncomp)]


# ----------------------------------------------------------------- main
_ALPHA_CACHE = {}


def _image_has_alpha(js, bin_, imgIdx):
    """True only if the image carries an alpha channel that actually varies. Decoding is
    cached: a Daz character shares one 4096x4096 skin map across several primitives."""
    if imgIdx in _ALPHA_CACHE:
        return _ALPHA_CACHE[imgIdx]
    ok = False
    try:
        import io as _io
        from PIL import Image
        im = js['images'][imgIdx]
        blob = b''
        if 'bufferView' in im:
            bv = js['bufferViews'][im['bufferView']]
            o = bv.get('byteOffset', 0)
            blob = bin_[o:o + bv['byteLength']]
        if blob:
            pil = Image.open(_io.BytesIO(blob))
            if pil.mode in ('RGBA', 'LA', 'PA'):
                # ⛔ VARIANCE IS NOT ENOUGH EITHER. Daz SKIN maps carry a gently varying
                # alpha for translucency, so "min < 250" still flagged 15 of 28 meshes and put
                # her face in the transparent pass. What actually separates a hair CARD from
                # skin is how MUCH of the sheet is see-through: a card is mostly empty space
                # around a few strands, skin is opaque wall to wall.
                a = pil.getchannel('A').resize((64, 64))
                px = list(a.getdata())
                clear = sum(1 for v in px if v < 128) / float(len(px))
                ok = clear > 0.08
                if ok:
                    print('     alpha: image%d %.0f%% see-through -> transparent pass'
                          % (imgIdx, clear * 100))
    except Exception:
        ok = False
    _ALPHA_CACHE[imgIdx] = ok
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--fps', type=float, default=30.0)
    ap.add_argument('--scale', type=float, default=1.0)
    ap.add_argument('--maxbones', type=int, default=MAXB)
    ap.add_argument('--norigid', action='store_true', help='skip rigid meshes on animated nodes (the pre-09-30 behaviour)')
    ap.add_argument('--autoslice', action='store_true',
                    help='find the moves inside long reels automatically instead of being told where they are')
    ap.add_argument('--keepnames', action='store_true',
                    help='the GLB clip names already follow the engine convention (w_walk/p_*/t_trick); '
                         'measure and print, but do not reassign roles')
    ap.add_argument('--slice', action='append', default=[],
                    help='cut a range out of a clip: SUBSTR:START:END:ROLE (seconds). '
                         'A demo reel holds several performances back to back; this is how '
                         'one of them becomes a usable clip.')
    a = ap.parse_args()

    js, bin_ = read_glb(a.src)
    nodes = js.get('nodes', [])
    nN = len(nodes)

    # ---- parents ----
    parent = [-1] * nN
    for i, nd in enumerate(nodes):
        for c in nd.get('children', []):
            parent[c] = i

    # ---- PARENT-FIRST NODE ORDER, made true rather than assumed ----
    # Both this baker's FK walks and the engine's loader (crowd_kms.c world[] pass) iterate
    # nodes in array order and read world[parent] as already computed. glTF makes no such
    # promise -- Blender's exporter emitted a child before its parent and the walk read None.
    # Every shipped pack until now satisfied the order by luck of its exporter. So: reorder
    # topologically here and remap every node reference (parents, skin joints, anim targets).
    if any(parent[i] >= 0 and parent[i] > i for i in range(nN)):
        order, seen = [], [False] * nN
        for i in range(nN):
            chain, j = [], i
            while j >= 0 and not seen[j]:
                chain.append(j); j = parent[j]
            for j in reversed(chain):
                seen[j] = True; order.append(j)
        remap = {old: new for new, old in enumerate(order)}
        nodes = [nodes[o] for o in order]
        parent = [(remap[parent[o]] if parent[o] >= 0 else -1) for o in order]
        js['nodes'] = nodes
        for sk in js.get('skins', []):
            sk['joints'] = [remap[j] for j in sk['joints']]
        for an in js.get('animations', []):
            for ch in an.get('channels', []):
                tgt = ch.get('target', {})
                if tgt.get('node') is not None:
                    tgt['node'] = remap[tgt['node']]
        for nd in nodes:
            if 'children' in nd:
                nd['children'] = [remap[c] for c in nd['children']]
        print('  node order was child-before-parent; reordered %d nodes parent-first' % nN)

    # ---- EVERY skinned primitive, not just the biggest ----
    # The character arrived holding her own skateboard: a second skinned primitive, riding the
    # same animation. Baking only the largest mesh threw the board away and left her balancing
    # on a prop the animation knows nothing about. Non-skinned primitives (the ground planes
    # this file also carries) are skipped -- we have a park already.
    prims = []
    for mi, m in enumerate(js.get('meshes', [])):
        for pi, prim in enumerate(m.get('primitives', [])):
            att = prim.get('attributes', {})
            if 'JOINTS_0' not in att or 'POSITION' not in att:
                continue
            node, skinIdx = -1, None
            for ni, nd in enumerate(nodes):
                if nd.get('mesh') == mi:
                    node, skinIdx = ni, nd.get('skin')
                    break
            if skinIdx is None:
                continue
            prims.append({'mi': mi, 'pi': pi, 'prim': prim, 'att': att,
                          'node': node, 'skin': skinIdx,
                          'name': m.get('name', 'mesh%d' % mi),
                          'verts': js['accessors'][att['POSITION']]['count']})
    if not prims:
        raise SystemExit('no skinned primitive found')
    # ⭐ RIGID MESHES ON AN ANIMATED NODE (2026-09-30, ROBOT NEKO ep 15). The shipped characters carry
    # their skateboard as a mesh on its own node -- no JOINTS_0, animated per trick by that node's
    # translation/rotation channels. Skipped as "not skinned", both riders rode thin air for a whole
    # episode. A rigid mesh IS a skinned mesh with one bone: its own node, weight 1.0, inverse bind =
    # the inverse of the node's bind-pose world. Only nodes an animation actually moves qualify (the
    # ground planes some GLBs carry stay out); --norigid keeps the old behaviour.
    animNodes = set(ch['target'].get('node') for an in js.get('animations', []) for ch in an.get('channels', []))
    if not a.norigid:
        for mi, m in enumerate(js.get('meshes', [])):
            for pi, prim in enumerate(m.get('primitives', [])):
                att = prim.get('attributes', {})
                if 'JOINTS_0' in att or 'POSITION' not in att:
                    continue
                node = next((ni for ni, nd in enumerate(nodes) if nd.get('mesh') == mi), -1)
                if node < 0 or node not in animNodes:
                    continue
                nv = js['accessors'][att['POSITION']]['count']
                # the node's bind-pose world, up the parent chain, and its inverse (numpy, like the rest of the kit)
                import numpy as _np
                def _trs(nd):
                    if 'matrix' in nd:
                        return _np.array(nd['matrix'], dtype=float).reshape(4, 4).T
                    t = nd.get('translation', [0, 0, 0]); q = nd.get('rotation', [0, 0, 0, 1]); sc = nd.get('scale', [1, 1, 1])
                    x, y, z, w = q
                    R = _np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                                   [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                                   [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
                    M = _np.eye(4); M[:3, :3] = R * _np.array(sc)[None, :]; M[:3, 3] = t
                    return M
                W = _np.eye(4); n2 = node; guard = 0
                while n2 >= 0 and guard < 128:
                    W = _trs(nodes[n2]) @ W; n2 = parent[n2]; guard += 1
                ibm = _np.linalg.inv(W).T.reshape(-1).tolist()      # column-major, like glTF
                js.setdefault('skins', []).append({'joints': [node], 'inverseBindMatrices': None})
                prims.append({'mi': mi, 'pi': pi, 'prim': prim, 'att': att, 'node': node,
                              'skin': len(js['skins']) - 1, 'name': m.get('name', 'mesh%d' % mi), 'verts': nv,
                              'ji': [0] * (4 * nv), 'jw': [1.0, 0.0, 0.0, 0.0] * nv, 'ibmList': ibm})
                print('  rigid mesh %-20s on animated node %d (%s): one bone, weight 1' % (m.get('name', '?'), node, nodes[node].get('name', '?')))
    prims.sort(key=lambda q: -q['verts'])

    allJointNodes = set()
    for q in prims:
        allJointNodes |= set(js['skins'][q['skin']]['joints'])

    # ---- build each mesh ----
    built = []
    images = []          # (imageIndex) in the order embedded
    for q in prims:
        att, prim = q['att'], q['prim']
        skin = js['skins'][q['skin']]
        joints = skin['joints']
        ibm = (q['ibmList'] if 'ibmList' in q else
               accessor(js, bin_, skin['inverseBindMatrices']) if skin.get('inverseBindMatrices') is not None
               else [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1] * len(joints))
        pos = accessor(js, bin_, att['POSITION'])
        nrm = accessor(js, bin_, att['NORMAL']) if 'NORMAL' in att else None
        uv = accessor(js, bin_, att['TEXCOORD_0']) if 'TEXCOORD_0' in att else None
        ji = q['ji'] if 'ji' in q else accessor(js, bin_, att['JOINTS_0'])
        jw = q['jw'] if 'jw' in q else accessor(js, bin_, att['WEIGHTS_0'])
        idx = accessor(js, bin_, prim['indices']) if 'indices' in prim else list(range(len(pos) // 3))
        nv = len(pos) // 3
        if a.scale != 1.0:
            pos = [v * a.scale for v in pos]

        load = [0.0] * len(joints)
        for v in range(nv):
            for k in range(4):
                w = jw[v * 4 + k]
                if w > 0:
                    load[ji[v * 4 + k]] += w
        keep = set(range(len(joints)))
        if len(joints) > a.maxbones:
            # PRUNE LEAF-FIRST, NOT LIGHTEST-FIRST. Weight alone is the wrong axis: a wrist
            # carries very little of it, but every finger hangs off the wrist, so dropping the
            # lightest bones can gut a hand while the fingertips survive. Depth in the skeleton
            # is the right ordering -- the deepest bone is by definition the one nothing else
            # depends on -- with weight breaking ties among equals.
            depth = []
            for j in range(len(joints)):
                dpt, n2, guard = 0, joints[j], 0
                while n2 >= 0 and guard < 128:
                    n2 = parent[n2]; dpt += 1; guard += 1
                depth.append(dpt)
            # ZERO-WEIGHT BONES GO FIRST, and they cost nothing to lose. This rig is full of
            # control bones -- Hip.CTRL, Hand.CRTL, Finger.CTRL, Shoulde.CTRL -- which an
            # animator poses but which deform no vertices at all. Spending scarce bone slots on
            # them while pruning bones that actually carry skin is exactly backwards. Order:
            # deforms-nothing first, then deepest, then lightest.
            order = sorted(range(len(joints)),
                           key=lambda j: (load[j] > 1e-6, -depth[j], load[j]))
            dead = sum(1 for j in range(len(joints)) if load[j] <= 1e-6)
            print('  %d of %d joints deform nothing (control bones); %d slots must go'
                  % (dead, len(joints), len(joints) - a.maxbones))
            drop = len(joints) - a.maxbones
            for j in order:
                if drop <= 0:
                    break
                keep.discard(j)
                drop -= 1
        kept = [j for j in range(len(joints)) if j in keep]
        slot = {j: si for si, j in enumerate(kept)}
        jointNode = {j: joints[j] for j in range(len(joints))}
        nodeJoint = {joints[j]: j for j in range(len(joints))}

        def nearest_kept(j, jointNode=jointNode, nodeJoint=nodeJoint, slot=slot):
            n = jointNode[j]
            seen = 0
            while n >= 0 and seen < 128:
                if n in nodeJoint and nodeJoint[n] in slot:
                    return slot[nodeJoint[n]]
                n = parent[n]
                seen += 1
            return 0

        remap = {j: (slot[j] if j in slot else nearest_kept(j)) for j in range(len(joints))}
        jb = bytearray(nv * 4)
        wb = bytearray(nv * 4)
        for v in range(nv):
            acc = {}
            for k in range(4):
                w = jw[v * 4 + k]
                if w <= 0:
                    continue
                sl = remap[ji[v * 4 + k]]
                acc[sl] = acc.get(sl, 0.0) + w
            items = sorted(acc.items(), key=lambda x: -x[1])[:4]
            tot = sum(w for _, w in items) or 1.0
            for k in range(4):
                if k < len(items):
                    jb[v * 4 + k] = items[k][0] & 0xFF
                    wb[v * 4 + k] = max(0, min(255, int(round(items[k][1] / tot * 255))))
            fix = 255 - sum(wb[v * 4:v * 4 + 4])
            wb[v * 4] = max(0, min(255, wb[v * 4] + fix))

        boneMap = [joints[j] for j in kept]
        ibmOut = []
        for j in kept:
            mm = list(ibm[j * 16:(j + 1) * 16])
            if a.scale != 1.0:
                mm[12] *= a.scale; mm[13] *= a.scale; mm[14] *= a.scale
            ibmOut.extend(mm)

        # texture, resolved through THIS primitive's material
        imgIdx = None
        blend = False
        matIdx = prim.get('material')
        if matIdx is not None and matIdx < len(js.get('materials', [])):
            mat = js['materials'][matIdx]
            pbr = mat.get('pbrMetallicRoughness', {})
            ti = pbr.get('baseColorTexture', {}).get('index')
            if ti is not None and ti < len(js.get('textures', [])):
                imgIdx = js['textures'][ti].get('source')
            # ⭐ alphaMode BLEND is the source telling us this surface is SEMI-transparent
            # rather than cut out -- hair cards, lace, eyelashes. MASK is left alone: the
            # shader's own discard is exactly right for a cutout, and putting a cutout in the
            # sorted pass would only cost depth-writes for nothing.
            # ⛔ alphaMode ALONE IS TOO TRUSTING. A Daz export marks 17 of 28 materials
            # BLEND including the SKIN, and putting an opaque face in a depth-write-off pass
            # stops it occluding itself -- her nose drew through her cheek and the hair drew
            # over her eyes. The source's declaration is a hint, not evidence.
            # ⭐ THE EVIDENCE IS THE ALPHA CHANNEL. A surface is only really transparent
            # if its colour texture HAS an alpha channel and that channel actually varies.
            blend = False
            if mat.get('alphaMode') == 'BLEND' and imgIdx is not None:
                blend = _image_has_alpha(js, bin_, imgIdx)
        texSlot = -1
        if imgIdx is not None:
            if imgIdx not in images:
                images.append(imgIdx)
            texSlot = images.index(imgIdx)
        # ⭐ THE NORMAL MAP (09-11). The audit: "base colour and tint only; the UE assets carry
        # normal maps the exporter already knows about." Same texture table, one more slot,
        # written +1 into the material record's spare bytes so an old pack reads 0 = none.
        nSlot = -1
        if matIdx is not None and matIdx < len(js.get('materials', [])):
            nti = js['materials'][matIdx].get('normalTexture', {}).get('index')
            if nti is not None and nti < len(js.get('textures', [])):
                nimg = js['textures'][nti].get('source')
                if nimg is not None:
                    if nimg not in images:
                        images.append(nimg)
                    nSlot = images.index(nimg)

        built.append({'name': q['name'], 'node': q['node'], 'nv': nv, 'idx': idx,
                      'pos': pos, 'nrm': nrm, 'uv': uv, 'jb': jb, 'wb': wb,
                      'boneMap': boneMap, 'ibm': ibmOut, 'tex': texSlot, 'ntex': nSlot, 'blend': blend,
                      'joints': len(joints), 'kept': len(kept)})

    # ---- animations, resampled to a fixed frame rate ----
    clips = []
    for anim in js.get('animations', []):
        samplers = anim.get('samplers', [])
        dur = 0.0
        for sm in samplers:
            acc = js['accessors'][sm['input']]
            if acc.get('max'):
                dur = max(dur, float(acc['max'][0]))
        if dur <= 0:
            continue
        frames = max(1, int(round(dur * a.fps)))
        tracks = {}
        for ch in anim.get('channels', []):
            tgt = ch['target']
            nd, path = tgt.get('node'), tgt.get('path')
            if nd is None or path == 'weights':
                continue
            sm = samplers[ch['sampler']]
            times = accessor(js, bin_, sm['input'])
            vals = accessor(js, bin_, sm['output'])
            ncomp = 4 if path == 'rotation' else 3
            if sm.get('interpolation') == 'CUBICSPLINE':
                vals = [vals[i] for i in range(len(vals)) if (i // ncomp) % 3 == 1]
            out2 = []
            for f in range(frames):
                v = sample_track(times, vals, ncomp, f / a.fps, path == 'rotation')
                if v is None:
                    v = [0, 0, 0, 1][:ncomp] if path == 'rotation' else [0, 0, 0]
                if path == 'translation' and a.scale != 1.0:
                    v = [c * a.scale for c in v]
                out2.extend(v)
            tracks[(nd, {'rotation': 0, 'translation': 1, 'scale': 2}[path])] = out2
        if tracks:
            clips.append((anim.get('name', 'clip%d' % len(clips)), a.fps, frames, tracks))

    # ---- CLASSIFY THE CLIPS BY WHAT THEY DO, NOT WHAT THEY ARE CALLED ----
    # Clip names come from whoever exported the file. In this asset every clip is named after
    # the CHARACTER, so the names carry no information about the movement at all. What a clip
    # is, though, is measurable:
    #     * a clip whose last frame returns to its first is a LOOP; one that does not is a
    #       one-shot, which is what a trick is
    #     * feet that alternate are steps; feet that stay put are a ride
    #     * how far the body rises separates a hop from a stance
    # The clips are then renamed into the engine's own convention (w_walk / w_ski / t_trick),
    # so the state machine picks them up without needing to know anything about this file.
    def _mat(t, q, sc):
        x, y, z, w = q
        return [(1 - 2 * (y * y + z * z)) * sc[0], (2 * (x * y + z * w)) * sc[0], (2 * (x * z - y * w)) * sc[0], 0,
                (2 * (x * y - z * w)) * sc[1], (1 - 2 * (x * x + z * z)) * sc[1], (2 * (y * z + x * w)) * sc[1], 0,
                (2 * (x * z + y * w)) * sc[2], (2 * (y * z - x * w)) * sc[2], (1 - 2 * (x * x + y * y)) * sc[2], 0,
                t[0], t[1], t[2], 1]

    def _find(sub):
        for i, nd in enumerate(nodes):
            if sub.lower() in (nd.get('name', '') or '').lower():
                return i
        return -1

    hipN = -1
    for cand in ('Hip_', 'Hips', 'Pelvis', 'Hip'):
        hipN = _find(cand)
        if hipN >= 0:
            break
    footL = -1
    footR = -1
    for cand in ('Tarsal.L', 'Foot.L', 'LeftFoot', 'Ankle.L'):
        footL = _find(cand)
        if footL >= 0:
            break
    for cand in ('Tarsal.R', 'Foot.R', 'RightFoot', 'Ankle.R'):
        footR = _find(cand)
        if footR >= 0:
            break

    restT = []
    restQ = []
    restS = []
    for nd in nodes:
        t0, q0, s0 = node_trs(nd)
        if a.scale != 1.0:
            t0 = [v * a.scale for v in t0]
        restT.append(t0); restQ.append(q0); restS.append(s0)

    def _pose_at(tracks, f):
        T2 = [list(v) for v in restT]
        Q2 = [list(v) for v in restQ]
        S2 = [list(v) for v in restS]
        for (nd, typ), data in tracks.items():
            if typ == 0:
                Q2[nd] = data[f * 4:(f + 1) * 4]
            elif typ == 1:
                T2[nd] = data[f * 3:(f + 1) * 3]
            else:
                S2[nd] = data[f * 3:(f + 1) * 3]
        W2 = [None] * nN
        for i in range(nN):
            loc = _mat(T2[i], Q2[i], S2[i])
            W2[i] = loc if parent[i] < 0 else mat_mul(W2[parent[i]], loc)
        return W2

    meas = []
    for (nm, fps, frames, tracks) in clips:
        step = max(1, frames // 60)
        hy, hx, hz, dl = [], [], [], []
        for f in range(0, frames, step):
            W2 = _pose_at(tracks, f)
            if hipN >= 0:
                hx.append(W2[hipN][12]); hy.append(W2[hipN][13]); hz.append(W2[hipN][14])
            if footL >= 0 and footR >= 0:
                dl.append(W2[footL][13] - W2[footR][13])
        Wl = _pose_at(tracks, frames - 1)
        W0 = _pose_at(tracks, 0)
        if hipN >= 0:
            close = math.dist([W0[hipN][12], W0[hipN][13], W0[hipN][14]],
                              [Wl[hipN][12], Wl[hipN][13], Wl[hipN][14]])
            rise = (max(hy) - min(hy)) if hy else 0.0
        else:
            close, rise = 0.0, 0.0
        alt = sum(1 for i in range(1, len(dl)) if (dl[i] > 0) != (dl[i - 1] > 0))
        span = max(rise, 1e-6)
        meas.append({'name': nm, 'dur': frames / fps, 'close': close, 'rise': rise,
                     'alt': alt, 'loops': close < span * 0.25})

    # assign roles: the one-shot is the trick, the stepping loop is the push, the calm loop rides
    # A DEMO REEL IS NOT A STATE. Duration is the discriminator: a clip that drives a state is
    # one to four seconds -- a stride, a stance, a trick. A twenty-second "loop" is a showcase
    # the artist recorded, with several different actions inside it, and wiring it to a state
    # makes the character perform a routine while standing still. Long clips are kept in the
    # pack (they are worth having) but left out of the state machine.
    REEL = 15.0
    for m in meas:
        if m['dur'] > REEL:
            m['reel'] = True
    oneshots = [m for m in meas if not m['loops'] and not m.get('reel')]
    loops = [m for m in meas if m['loops'] and not m.get('reel')]
    role = {}
    if a.keepnames:
        oneshots, loops = [], []   # names are authored; measurement stays as a printed report only
    if oneshots:
        role[min(oneshots, key=lambda m: m['dur'])['name']] = 't_trick'
    steppers = sorted([m for m in loops if m['alt'] >= 2], key=lambda m: -m['alt'] / max(m['dur'], .01))
    if steppers:
        role[steppers[0]['name']] = 'w_walk'
    # A RIDE LOOP IS FLAT. A loop that lifts the body a long way is a trick being repeated,
    # not someone rolling along -- and wiring one to ROLL makes the character kickflip
    # continuously while simply moving. Measured against his own standing height.
    # A BODY WITH NO CLIPS IS A BODY: a character built to receive retargeted moves (Clanker, 09-27) arrives
    # with none, and max() of nothing killed the bake after the skin was already read.
    bodyH = max(1e-6, max((m['rise'] for m in meas), default=0.0))
    riders = sorted([m for m in loops if m['name'] not in role and m['rise'] < bodyH * 0.55],
                    key=lambda m: m['alt'])
    if riders:
        role[riders[0]['name']] = 'w_ski'
    for m in riders[1:]:
        role[m['name']] = 'p_' + m['name'][:20]
    for m in meas:
        if m.get('reel') and not a.keepnames:
            role[m['name']] = 'x_reel_' + m['name'][:16]   # kept, but no state points at it

    print('  clip roles, decided by measurement:')
    for m in meas:
        print('     %-30s %5.2fs  loops=%-5s alt=%-3d rise=%7.1f %s -> %s'
              % (m['name'], m['dur'], m['loops'], m['alt'], m['rise'],
                 'REEL' if m.get('reel') else '    ',
                 role.get(m['name'], '(unused)')))
    clips = [(role.get(nm, nm), fps, frames, tracks) for (nm, fps, frames, tracks) in clips]

    # ---- CUT REQUESTED RANGES OUT OF LONGER CLIPS ----
    # A reel is several performances recorded back to back, so the move you want lives inside it
    # rather than in a clip of its own. Slicing copies the per-frame data for a window, which is
    # trivial precisely because the clips are already baked per frame rather than keyframed.
    # ---- FIND THE MOVES INSIDE A REEL, AUTOMATICALLY ----
    # A reel is several performances back to back, and which is which is measurable, so the
    # baker should not need to be told. Three signals do the whole job:
    #   TRICK  a spike in hip height where horizontal speed collapses -- he is in the air, and
    #          in the air you cannot be travelling, so the two together are unambiguous
    #   IDLE   the calmest window: least variation in hip height, least travel
    #   PUSH   handled already by the loop/step classifier above
    # This is what makes the pipeline work for a character nobody has looked at yet.
    if a.autoslice:
        reels = [m for m in meas if m.get('reel')]
        for rm in reels:
            src = None
            for (n4, f4, r4, t4) in clips:
                if n4.startswith('x_reel') and rm['name'][:12] in n4:
                    src = (n4, f4, r4, t4); break
            if src is None:
                for (n4, f4, r4, t4) in clips:
                    if n4.startswith('x_reel'):
                        src = (n4, f4, r4, t4); break
            if src is None:
                continue
            n4, fps4, fr4, tr4 = src
            hy, hxz = [], []
            for f in range(fr4):
                W4 = _pose_at(tr4, f)
                hy.append(W4[hipN][13])
                hxz.append((W4[hipN][12], W4[hipN][14]))
            spd = [0.0] + [math.dist(hxz[i], hxz[i - 1]) * fps4 for i in range(1, fr4)]
            ground = sorted(hy)[len(hy) // 10]
            rise = [h - ground for h in hy]
            hi = max(rise)
            # airborne: high, and not moving forward
            cand = []
            f = 0
            while f < fr4:
                if rise[f] > hi * 0.55 and spd[f] < 60.0:
                    f0 = f
                    while f < fr4 and rise[f] > hi * 0.25:
                        f += 1
                    peak = max(rise[f0:f] or [0])
                    cand.append((f0, f, peak))
                f += 1
            # merge neighbours and keep the strongest, most separated few
            cand.sort(key=lambda c: -c[2])
            picked = []
            for (f0, f1, pk) in cand:
                if all(abs(f0 - g0) > fps4 for (g0, g1, _) in picked):
                    picked.append((f0, f1, pk))
                if len(picked) >= 2:
                    break
            roles = ['t_trick', 't_trick2']
            for ri, (f0, f1, pk) in enumerate(picked):
                pad = int(fps4 * 0.45)
                s0 = max(0, f0 - pad)
                s1 = min(fr4, f1 + pad)
                if s1 - s0 < 6:
                    continue
                a.slice.append('%s:%.3f:%.3f:%s' % (n4[:14], s0 / fps4, s1 / fps4, roles[ri]))
                print('  autoslice: air at %.2f-%.2fs (peak rise %.0f) -> %s'
                      % (s0 / fps4, s1 / fps4, pk, roles[ri]))
            # NO IDLE IS INVENTED. A calm stretch of a reel is still a performance -- the
            # rider is mid-run, not waiting -- so cutting one and calling it an idle made
            # him do tricks standing still. A character with no genuine idle holds its
            # ride loop instead.

    for spec in a.slice:
        try:
            substr, t0, t1, newrole = spec.split(':')
            t0, t1 = float(t0), float(t1)
        except ValueError:
            print('  !! bad --slice %r, want SUBSTR:START:END:ROLE' % spec)
            continue
        src = None
        for (nm2, fps2, fr2, tr2) in clips:
            if substr.lower() in nm2.lower():
                src = (nm2, fps2, fr2, tr2)
                break
        if src is None:
            print('  !! --slice found no clip matching %r' % substr)
            continue
        nm2, fps2, fr2, tr2 = src
        f0 = max(0, int(round(t0 * fps2)))
        f1 = min(fr2, int(round(t1 * fps2)))
        if f1 - f0 < 2:
            print('  !! --slice %s gives no frames' % spec)
            continue
        cut = {}
        for (nd, typ), data in tr2.items():
            nc = 4 if typ == 0 else 3
            cut[(nd, typ)] = data[f0 * nc:f1 * nc]
        # anything already holding this role steps aside
        # Demote to a name sharing NO keyword with the role. 'x_t_trick' still contains
        # 'trick', and the engine picks the SHORTEST match -- so the fragment we just
        # replaced got selected straight back up, and the slice never played.
        clips = [(('x_frag%d' % i3 if n3 == newrole else n3), f3, r3, t3)
                 for i3, (n3, f3, r3, t3) in enumerate(clips)]
        clips.append((newrole, fps2, f1 - f0, cut))
        print('  sliced %.2f-%.2fs out of %-24s -> %-10s (%d frames)'
              % (t0, t1, nm2, newrole, f1 - f0))

    # ---- image blobs ----
    texBlobs = []
    for imgIdx in images:
        im = js['images'][imgIdx]
        blob = b''
        if 'bufferView' in im:
            bv = js['bufferViews'][im['bufferView']]
            o = bv.get('byteOffset', 0)
            blob = bin_[o:o + bv['byteLength']]
        elif im.get('uri', '').startswith('data:'):
            blob = base64.b64decode(im['uri'].split(',', 1)[1])
        elif im.get('uri'):
            import urllib.parse
            side = os.path.join(os.path.dirname(os.path.abspath(a.src)), urllib.parse.unquote(im['uri']))
            if os.path.exists(side):
                blob = open(side, 'rb').read()
        texBlobs.append(blob)
    nTex = len(texBlobs)

    # ---- strings ----
    strs = []
    stab = {}

    def sid(t):
        t = (t or '').encode('utf8')[:120]
        if t not in stab:
            stab[t] = len(strs)
            strs.append(t)
        return stab[t]

    nodeName = [sid(nd.get('name', 'node%d' % i)) for i, nd in enumerate(nodes)]
    clipName = [sid(c[0]) for c in clips]
    meshNameId = [sid(b['name']) for b in built]

    nM = len(built)
    HDR = 4 + 15 * 4
    off = HDR
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

    for b in built:
        b['offPos'] = place(struct.pack('<%df' % len(b['pos']), *b['pos']))
        b['offNorm'] = place(struct.pack('<%df' % len(b['nrm']), *b['nrm'])) if b['nrm'] else 0
        b['offUV'] = place(struct.pack('<%df' % len(b['uv']), *b['uv'])) if b['uv'] else 0
        b['offJ'] = place(bytes(b['jb']))
        b['offW'] = place(bytes(b['wb']))
        b['offIdx'] = place(struct.pack('<%dI' % len(b['idx']), *b['idx']))
        b['offBM'] = place(struct.pack('<%dH' % len(b['boneMap']), *b['boneMap']))
        b['offIBM'] = place(struct.pack('<%df' % len(b['ibm']), *b['ibm']))
        b['offBind'] = place(struct.pack('<16f', 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1))
    texOff = [place(t) for t in texBlobs]

    clipTrackOff = []
    for (_, _, frames, tracks) in clips:
        entries = []
        for (nd, typ), data in sorted(tracks.items()):
            entries.append((nd, typ, place(struct.pack('<%df' % len(data), *data))))
        clipTrackOff.append(entries)
    clipTableOff = [place(b''.join(struct.pack('<III', nd, typ, o) for (nd, typ, o) in e))
                    for e in clipTrackOff]

    tbl = struct.pack('<I', len(strs))
    so = 0
    for t in strs:
        tbl += struct.pack('<II', so, len(t))
        so += len(t)
    tbl += b''.join(strs)
    offStr = place(tbl)

    szb = [0.0] * 12
    szb[7] = float('nan'); szb[9] = float('nan'); szb[10] = 0.0
    offSize = place(struct.pack('<12f', *szb))
    total = off

    out = bytearray()
    out += b'RPK1'
    out += struct.pack('<15I', 2, nN, nM, nM, len(clips), nTex,
                       offNodes, offMeshes, offMats, offClips, offTexTable,
                       offStr, offSize, total, 0)
    for i, nd in enumerate(nodes):
        t, q, sc = node_trs(nd)
        if a.scale != 1.0:
            t = [v * a.scale for v in t]
        kind = 1 if i in allJointNodes else (2 if any(b['node'] == i for b in built) else 0)
        out += struct.pack('<IiI3f4f3f', nodeName[i], parent[i], kind,
                           t[0], t[1], t[2], q[0], q[1], q[2], q[3], sc[0], sc[1], sc[2])
    for bi, b in enumerate(built):
        # ⭐ BIT 8 = THE MATERIAL ASKED FOR ALPHA BLENDING. The engine alpha-TESTS
        # everything (discard below 0.4), which is right for cutouts and wrong for hair:
        # hair is overlapping semi-transparent cards, and a hard threshold turns them
        # into a solid silhouette with a speckled fringe. This flag is what puts them in
        # the engine's transparent pass instead.
        flags = (1 if b['nrm'] else 0) | (2 if b['uv'] else 0) | 4 | (8 if b.get('blend') else 0)
        out += struct.pack('<15I', meshNameId[bi], b['node'], b['nv'], len(b['idx']),
                           len(b['boneMap']), flags,
                           b['offPos'], b['offNorm'], b['offUV'], b['offJ'], b['offW'],
                           b['offIdx'], b['offBM'], b['offIBM'], b['offBind'])
    for bi, b in enumerate(built):
        mat = bytearray(36)
        struct.pack_into('<I', mat, 0, bi)
        struct.pack_into('<I', mat, 16, 0)
        struct.pack_into('<i', mat, 24, b['tex'])
        struct.pack_into('<i', mat, 28, b.get('ntex', -1) + 1)   # normal map slot + 1; 0 = none (old packs)
        out += mat
    for ci, (nm, fps, frames, tracks) in enumerate(clips):
        out += struct.pack('<IfIII', clipName[ci], fps, frames,
                           len(clipTrackOff[ci]), clipTableOff[ci])
    for ti in range(nTex):
        t = bytearray(16)
        struct.pack_into('<I', t, 8, texOff[ti])
        struct.pack_into('<I', t, 12, len(texBlobs[ti]))
        out += t
    for b in blobs:
        out += b

    open(a.dst, 'wb').write(out)
    print(a.dst)
    print('  nodes %d   meshes %d   clips %d   textures %d' % (nN, nM, len(clips), nTex))
    for b in built:
        print('     mesh %-26s verts %6d  tris %6d  bones %2d/%-2d  tex %d'
              % (b['name'], b['nv'], len(b['idx']) // 3, b['kept'], b['joints'], b['tex']))
    for (nm, fps, frames, tracks) in clips:
        print('     clip %-26s %3d frames @ %gfps (%.2fs)  %d tracks'
              % (nm, frames, fps, frames / fps, len(tracks)))
    print('  file %d B' % len(out))


main()
