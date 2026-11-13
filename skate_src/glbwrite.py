#!/usr/bin/env python3
"""glbwrite.py -- the club's scene shape (rpackwrite.py) as a binary glTF 2.0 (.glb) (2026-10-05).

    write_glb(path, nodes, meshes, clips, textures, names)

Why it exists: every importer written after 10-05 (dae2rpack, brres2rpack) builds the scene shape, and
the STATUE path needs a GLB -- statue2body.py rigs an unrigged body by landmarks on a GLB, autoweights.py
binds it, glb2rpack_anim.py bakes it. rpack2gltf.py exports a pack's AUTHORED clips and refuses a pack
with none, which is every statue. This writer is the plain form of the same thing:

  * the node hierarchy as glTF nodes (TRS), the scene roots as the scene
  * each mesh as its own top-level node carrying a mesh + a skin (joints = the mesh's bone map, the
    inverse binds as given); positions are RIG SPACE, which is what a skinned glTF primitive wants
    (a skinned node's own transform is ignored by the spec)
  * POSITION NORMAL TEXCOORD_0 COLOR_0 JOINTS_0 (u8) WEIGHTS_0 (f32) + u32 indices; min/max on POSITION
  * materials: baseColorTexture per mesh (PNG embedded), alphaMode BLEND where the mesh blends, MASK
    (0.4) where its texture has alpha, doubleSided; NEAREST samplers where the mesh asks for them
  * each clip as an animation: rotation / translation / scale channels, LINEAR, the clip's own fps
"""
import json, struct, io


def write_glb(path, nodes, meshes, clips, textures, names=None, report=print):
    names = names or [nd['name'] for nd in nodes]
    bin_ = bytearray()
    views, accs = [], []

    def view(data, target=None):
        while len(bin_) % 4:
            bin_.append(0)
        off = len(bin_)
        bin_.extend(data)
        v = {'buffer': 0, 'byteOffset': off, 'byteLength': len(data)}
        if target:
            v['target'] = target
        views.append(v)
        return len(views) - 1

    def acc(vals, fmt, ctype, atype, ncomp, target=None, minmax=False):
        data = struct.pack('<%d%s' % (len(vals), fmt), *vals)
        a = {'bufferView': view(data, target), 'componentType': ctype, 'count': len(vals) // ncomp, 'type': atype}
        if minmax:
            a['min'] = [min(vals[i::ncomp]) for i in range(ncomp)]
            a['max'] = [max(vals[i::ncomp]) for i in range(ncomp)]
        accs.append(a)
        return len(accs) - 1

    gj = {'asset': {'version': '2.0', 'generator': 'the club glbwrite.py'}, 'scene': 0, 'scenes': [{'nodes': []}],
          'nodes': [], 'meshes': [], 'skins': [], 'materials': [], 'textures': [], 'images': [], 'samplers': [],
          'accessors': accs, 'bufferViews': views, 'buffers': [{'byteLength': 0}]}
    for i, nd in enumerate(nodes):
        n = {'name': names[i], 'translation': list(nd['t']), 'rotation': list(nd['q']), 'scale': list(nd['s'])}
        gj['nodes'].append(n)
    for i, nd in enumerate(nodes):
        if nd['parent'] >= 0:
            gj['nodes'][nd['parent']].setdefault('children', []).append(i)
        else:
            gj['scenes'][0]['nodes'].append(i)
    # images / samplers / textures
    img_of_tex = {}
    for ti, blob in enumerate(textures):
        gj['images'].append({'bufferView': view(bytes(blob)), 'mimeType': 'image/png', 'name': 'tex%d' % ti})
    gj['samplers'] = [{'magFilter': 9729, 'minFilter': 9987, 'wrapS': 10497, 'wrapT': 10497},
                      {'magFilter': 9728, 'minFilter': 9728, 'wrapS': 10497, 'wrapT': 10497}]

    def texture_index(ti, nearest):
        key = (ti, bool(nearest))
        if key not in img_of_tex:
            gj['textures'].append({'source': ti, 'sampler': 1 if nearest else 0})
            img_of_tex[key] = len(gj['textures']) - 1
        return img_of_tex[key]

    def has_alpha(blob):
        try:
            from PIL import Image
            im = Image.open(io.BytesIO(bytes(blob)))
            if im.mode not in ('RGBA', 'LA', 'PA'):
                return False
            a = im.convert('RGBA').getchannel('A')
            lo, hi = a.getextrema()
            return lo < 250
        except Exception:
            return False

    for mi, m in enumerate(meshes):
        nv = len(m['pos']) // 3
        mat = {'name': m.get('name') or 'mat%d' % mi, 'doubleSided': True, 'pbrMetallicRoughness': {'metallicFactor': 0.0, 'roughnessFactor': 0.9}}
        if m.get('tex', -1) is not None and m.get('tex', -1) >= 0 and m['tex'] < len(textures):
            mat['pbrMetallicRoughness']['baseColorTexture'] = {'index': texture_index(m['tex'], m.get('nearest'))}
            if m.get('blend'):
                mat['alphaMode'] = 'BLEND'
            elif has_alpha(textures[m['tex']]):
                mat['alphaMode'] = 'MASK'
                mat['alphaCutoff'] = 0.4
        gj['materials'].append(mat)
        attrs = {'POSITION': acc(m['pos'], 'f', 5126, 'VEC3', 3, 34962, minmax=True)}
        if m.get('nrm'):
            attrs['NORMAL'] = acc(m['nrm'], 'f', 5126, 'VEC3', 3, 34962)
        if m.get('uv'):
            attrs['TEXCOORD_0'] = acc(m['uv'], 'f', 5126, 'VEC2', 2, 34962)
        if m.get('col'):
            nc = len(m['col']) // nv
            attrs['COLOR_0'] = acc(m['col'], 'f', 5126, 'VEC4' if nc == 4 else 'VEC3', nc, 34962)
        jb = [int(j) & 0xFF for v in range(nv) for j in m['joints'][v]]
        wb = []
        for v in range(nv):
            w4 = list(m['weights'][v])
            tot = sum(w4) or 1.0
            wb.extend([w / tot for w in w4])
        attrs['JOINTS_0'] = acc(jb, 'B', 5121, 'VEC4', 4, 34962)
        attrs['WEIGHTS_0'] = acc(wb, 'f', 5126, 'VEC4', 4, 34962)
        idx = acc(list(m['idx']), 'I', 5125, 'SCALAR', 1, 34963)
        gj['meshes'].append({'name': m.get('name') or 'mesh%d' % mi, 'primitives': [{'attributes': attrs, 'indices': idx, 'material': mi, 'mode': 4}]})
        ibm = acc(list(m['ibm']), 'f', 5126, 'MAT4', 16)
        gj['skins'].append({'joints': list(m['boneMap']), 'inverseBindMatrices': ibm, 'skeleton': _root_of(nodes, m['boneMap'][0])})
        gj['nodes'].append({'name': (m.get('name') or 'mesh%d' % mi), 'mesh': mi, 'skin': mi})
        gj['scenes'][0]['nodes'].append(len(gj['nodes']) - 1)
    # animations
    anims = []
    for (cn, fps, frames, tracks) in clips:
        an = {'name': cn, 'samplers': [], 'channels': []}
        times = acc([f / fps for f in range(frames)], 'f', 5126, 'SCALAR', 1, minmax=True)
        for (node, typ), data in sorted(tracks.items()):
            # ⛔ not `path`: that is the FILE, and a clip with a rotation track once sent the whole
            # GLB to a file called `rotation` in the working directory (10-05)
            cpath = {0: 'rotation', 1: 'translation', 2: 'scale'}[typ]
            out = acc(list(data), 'f', 5126, 'VEC4' if typ == 0 else 'VEC3', 4 if typ == 0 else 3)
            an['samplers'].append({'input': times, 'output': out, 'interpolation': 'LINEAR'})
            an['channels'].append({'sampler': len(an['samplers']) - 1, 'target': {'node': node, 'path': cpath}})
        if an['channels']:
            anims.append(an)
    if anims:
        gj['animations'] = anims
    for k in ('textures', 'images', 'samplers', 'skins', 'materials'):
        if not gj[k]:
            del gj[k]
    gj['buffers'][0]['byteLength'] = len(bin_)
    jb = json.dumps(gj, separators=(',', ':')).encode()
    while len(jb) % 4:
        jb += b' '
    while len(bin_) % 4:
        bin_.append(0)
    out = bytearray(b'glTF' + struct.pack('<II', 2, 12 + 8 + len(jb) + 8 + len(bin_)))
    out += struct.pack('<I', len(jb)) + b'JSON' + jb
    out += struct.pack('<I', len(bin_)) + b'BIN\x00' + bytes(bin_)
    open(path, 'wb').write(out)
    if report:
        report('%s: %d nodes, %d meshes, %d clips, %d textures, %d B' % (path, len(nodes), len(meshes), len(anims), len(textures), len(out)))
    return path


def _root_of(nodes, i):
    g = 0
    while nodes[i]['parent'] >= 0 and g < 512:
        i = nodes[i]['parent']
        g += 1
    return i
