#!/usr/bin/env python3
"""daelib.py -- COLLADA (.dae) in and out, to and from the club's scene shape (2026-10-05).

READ  scene = read_dae(path, report)  ->  dict(nodes, meshes, clips, textures, names)
      in rpackwrite's shape, with every mesh PRE-BAKED INTO RIG SPACE at the rest pose:
      pos = the skinned rest position, bind = identity, ibm = inv(world rest of each bone),
      a rigid mesh = one bone (its node) at weight 1. That is the one contract every later
      stage (tpose.py, rpackwrite, the engine) agrees on, and it means a .dae's up axis, units,
      bind-shape matrix and node transforms are all settled HERE, once.
      Handles: 1.4.1 and 1.5 namespaces; Y_UP / Z_UP / X_UP; <unit meter>; sources with
      accessor stride/offset; <vertices>; triangles / polylist / polygons / tristrips /
      trifans (fanned to triangles); skins (Name_array or IDREF_array joints, bind_shape_matrix,
      vcount/v weights); node transforms in document order (matrix translate rotate scale);
      instance_node; materials -> effects -> sampler -> surface -> image (file beside the dae,
      converted to PNG), <transparent>/<transparency> as a blend hint; animations targeting
      a transform element's sid (.X .ANGLE (r)(c)) or the whole element or 'transform',
      resampled to a fixed fps; library_animation_clips when present, else one clip.
WRITE write_dae(path, nodes, meshes, clips, textures, names) -- Y_UP, metres, joints as
      type="JOINT" nodes with sid = name, a skin controller per mesh, textures as PNG files
      beside the .dae, animations as baked per-frame matrices on 'node/transform' (the form
      Blender's importer reads without complaint).
"""
import os, re, math, struct, io
import xml.etree.ElementTree as ET
HERE = os.path.dirname(os.path.abspath(__file__))
import sys
sys.path.insert(0, HERE)
import rpackwrite as RW


# ----------------------------------------------------------------- xml helpers
def _strip_ns(root):
    for el in root.iter():
        if isinstance(el.tag, str) and el.tag.startswith('{'):
            el.tag = el.tag.split('}', 1)[1]
    return root


def _floats(text):
    return [float(x) for x in (text or '').replace(',', ' ').split()]


def _ints(text):
    return [int(x) for x in (text or '').split()]


def _ref(url):
    return (url or '').lstrip('#')


def _rowmajor_to_cm(m):
    """16 numbers as COLLADA writes them (row by row) -> column-major 16."""
    return [m[r * 4 + c] for c in range(4) for r in range(4)]


def _cm_to_rowmajor(m):
    return [m[c * 4 + r] for r in range(4) for c in range(4)]


def _ncname(s, used):
    s = re.sub(r'[^A-Za-z0-9_.\-]', '_', s or 'n')
    if not s or s[0].isdigit() or s[0] in '.-':
        s = 'n_' + s
    base, k = s, 1
    while s in used:
        k += 1
        s = '%s_%d' % (base, k)
    used.add(s)
    return s


# ----------------------------------------------------------------- reading
class _Src:
    """A <source>: floats or names, with its accessor stride."""
    def __init__(self, el):
        self.id = el.get('id')
        fa = el.find('float_array')
        na = el.find('Name_array')
        ia = el.find('IDREF_array')
        self.floats = _floats(fa.text) if fa is not None else None
        self.names = (na.text or '').split() if na is not None else ((ia.text or '').split() if ia is not None else None)
        acc = el.find('technique_common/accessor')
        self.stride = int(acc.get('stride', '1')) if acc is not None else 1
        self.offset = int(acc.get('offset', '0')) if acc is not None else 0
        self.count = int(acc.get('count', '0')) if acc is not None else (len(self.floats or self.names or []) // self.stride)

    def vec(self, i, n=None):
        n = n or self.stride
        b = self.offset + i * self.stride
        return self.floats[b:b + n]


def _transform_matrix(kind, v):
    if kind == 'matrix':
        return _rowmajor_to_cm(v)
    if kind == 'translate':
        return RW.m4_trs(v[:3], [0, 0, 0, 1], [1, 1, 1])
    if kind == 'scale':
        return RW.m4_trs([0, 0, 0], [0, 0, 0, 1], v[:3])
    if kind == 'rotate':
        ax = v[:3]
        L = math.sqrt(sum(x * x for x in ax)) or 1.0
        s = math.sin(math.radians(v[3]) / 2)
        q = [ax[0] / L * s, ax[1] / L * s, ax[2] / L * s, math.cos(math.radians(v[3]) / 2)]
        return RW.m4_trs([0, 0, 0], q, [1, 1, 1])
    return RW.m4_identity()


def _node_local(transforms):
    M = RW.m4_identity()
    for kind, _sid, v in transforms:
        M = RW.m4_mul(M, _transform_matrix(kind, v))
    return M


def read_dae(path, report=print, fps=30.0, images=True, up=None, unit=None):
    tree = ET.parse(path)
    root = _strip_ns(tree.getroot())
    base = os.path.dirname(os.path.abspath(path))
    rep = report or (lambda *a, **k: None)

    # ---- asset
    up_file = (root.findtext('asset/up_axis') or 'Y_UP').strip().upper()
    unit_el = root.find('asset/unit')
    unit_file = float(unit_el.get('meter', '1')) if unit_el is not None else 1.0
    # the asset block is what the EXPORTER claims; a DS ripper wrote Y_UP over Z-up data (Kanden,
    # 10-05), so the caller may overrule both the axis and the unit after looking at the cloud
    up = (up or up_file).upper().replace('_UP', '') + '_UP'
    unit = unit if unit is not None else unit_file
    rep('  asset: %s, 1 unit = %g m%s' % (up, unit, '' if (up == up_file and unit == unit_file) else '  (file says %s, %g m -- OVERRIDDEN)' % (up_file, unit_file)))

    # ---- geometries
    geoms = {}
    for g in root.findall('library_geometries/geometry'):
        mesh = g.find('mesh')
        if mesh is None:
            continue
        srcs = {s.get('id'): _Src(s) for s in mesh.findall('source')}
        verts = {}
        for ve in mesh.findall('vertices'):
            verts[ve.get('id')] = [(i.get('semantic'), _ref(i.get('source'))) for i in ve.findall('input')]
        prims = []
        for pe in list(mesh):
            if pe.tag not in ('triangles', 'polylist', 'polygons', 'tristrips', 'trifans'):
                continue
            inputs = []
            for i in pe.findall('input'):
                sem, src, off, st = i.get('semantic'), _ref(i.get('source')), int(i.get('offset', '0')), int(i.get('set', '0'))
                if sem == 'VERTEX':
                    for (vs, vsrc) in verts.get(src, []):
                        inputs.append((vs, vsrc, off, st))
                else:
                    inputs.append((sem, src, off, st))
            stride = max((off for (_s, _src, off, _st) in inputs), default=0) + 1
            polys = []           # each a list of corner tuples (one index per offset)
            if pe.tag == 'polylist':
                vc = _ints(pe.findtext('vcount'))
                p = _ints(pe.findtext('p'))
                k = 0
                for n in vc:
                    polys.append([tuple(p[(k + j) * stride:(k + j + 1) * stride]) for j in range(n)])
                    k += n
            elif pe.tag in ('polygons',):
                for pp in pe.findall('p'):
                    p = _ints(pp.text)
                    polys.append([tuple(p[j * stride:(j + 1) * stride]) for j in range(len(p) // stride)])
                for ph in pe.findall('ph'):
                    p = _ints(ph.findtext('p'))
                    polys.append([tuple(p[j * stride:(j + 1) * stride]) for j in range(len(p) // stride)])
            elif pe.tag == 'triangles':
                p = []
                for pp in pe.findall('p'):
                    p += _ints(pp.text)
                for t in range(len(p) // (3 * stride)):
                    polys.append([tuple(p[(t * 3 + j) * stride:(t * 3 + j + 1) * stride]) for j in range(3)])
            elif pe.tag in ('tristrips', 'trifans'):
                for pp in pe.findall('p'):
                    p = _ints(pp.text)
                    corners = [tuple(p[j * stride:(j + 1) * stride]) for j in range(len(p) // stride)]
                    for j in range(2, len(corners)):
                        if pe.tag == 'trifans':
                            polys.append([corners[0], corners[j - 1], corners[j]])
                        elif j % 2 == 0:
                            polys.append([corners[j - 2], corners[j - 1], corners[j]])
                        else:
                            polys.append([corners[j - 1], corners[j - 2], corners[j]])
            prims.append(dict(material=pe.get('material'), inputs=inputs, polys=polys))
        geoms[g.get('id')] = dict(name=g.get('name') or g.get('id'), srcs=srcs, prims=prims)

    # ---- controllers (skins)
    skins = {}
    for c in root.findall('library_controllers/controller'):
        sk = c.find('skin')
        if sk is None:
            continue
        srcs = {s.get('id'): _Src(s) for s in sk.findall('source')}
        bsm = _rowmajor_to_cm(_floats(sk.findtext('bind_shape_matrix'))) if sk.find('bind_shape_matrix') is not None else RW.m4_identity()
        jnames, ibms = [], []
        for i in sk.findall('joints/input'):
            s = srcs.get(_ref(i.get('source')))
            if s is None:
                continue
            if i.get('semantic') == 'JOINT':
                jnames = s.names or []
            elif i.get('semantic') == 'INV_BIND_MATRIX':
                ibms = [_rowmajor_to_cm(s.vec(k, 16)) for k in range(s.count)]
        vw = sk.find('vertex_weights')
        infl = []
        if vw is not None:
            joff = woff = 0
            wsrc = None
            for i in vw.findall('input'):
                if i.get('semantic') == 'JOINT':
                    joff = int(i.get('offset', '0'))
                    js = srcs.get(_ref(i.get('source')))
                    if js is not None and js.names and not jnames:
                        jnames = js.names
                elif i.get('semantic') == 'WEIGHT':
                    woff = int(i.get('offset', '0'))
                    wsrc = srcs.get(_ref(i.get('source')))
            stride = max(joff, woff) + 1
            vc = _ints(vw.findtext('vcount'))
            v = _ints(vw.findtext('v'))
            k = 0
            for n in vc:
                lst = []
                for j in range(n):
                    ji = v[(k + j) * stride + joff]
                    wi = v[(k + j) * stride + woff]
                    w = wsrc.floats[wi] if wsrc is not None else 1.0
                    if ji >= 0:
                        lst.append((ji, w))
                infl.append(lst)
                k += n
        skins[c.get('id')] = dict(geom=_ref(sk.get('source')), bsm=bsm, joints=jnames, ibm=ibms, infl=infl, name=c.get('name') or c.get('id'))

    # ---- materials -> images
    images = {}
    for im in root.findall('library_images/image'):
        init = im.find('init_from')
        p = ''
        if init is not None:
            p = (init.findtext('ref') or init.text or '').strip()
        images[im.get('id')] = p
    effects = {}
    for fx in root.findall('library_effects/effect'):
        params = {}
        for np_ in fx.iter('newparam'):
            sid = np_.get('sid')
            surf = np_.find('surface')
            samp = np_.find('sampler2D')
            if surf is not None:
                params[sid] = ('surface', (surf.findtext('init_from') or '').strip())
            elif samp is not None:
                params[sid] = ('sampler', (samp.findtext('source') or samp.findtext('instance_image') and samp.find('instance_image').get('url') or '').strip())
        tex_img, blend = None, False
        for t in fx.iter('texture'):
            ref = t.get('texture')
            kind, val = params.get(ref, (None, None))
            if kind == 'sampler':
                kind2, val2 = params.get(_ref(val), (None, None))
                val = val2 if kind2 == 'surface' else _ref(val)
            elif kind is None:
                val = _ref(ref)
            if val in images:
                tex_img = val
                break
        # transparency: 1.4 exporters disagree on A_ONE/RGB_ZERO; a declared <transparency> below 1
        # or a <transparent> that names a texture is the hint; the baker still verifies the alpha
        tr = fx.find('.//transparency/float')
        if tr is not None:
            try:
                tv = float(tr.text)
                blend = (tv < 0.999 and tv > 0.0)
            except ValueError:
                pass
        if fx.find('.//transparent/texture') is not None:
            blend = True
        effects[fx.get('id')] = dict(image=tex_img, blend=blend)
    materials = {}
    for m in root.findall('library_materials/material'):
        ie = m.find('instance_effect')
        materials[m.get('id')] = effects.get(_ref(ie.get('url')) if ie is not None else '', dict(image=None, blend=False))

    # ---- nodes: the visual scene, flattened parent-first
    lib_nodes = {n.get('id'): n for n in root.findall('library_nodes/node')}
    scene = root.find('library_visual_scenes/visual_scene')
    if scene is None:
        raise SystemExit('%s: no visual_scene' % path)
    nodes, names, byid, bysid, instances = [], [], {}, {}, []

    def walk(el, parent):
        i = len(nodes)
        nid, sid, nm = el.get('id'), el.get('sid'), el.get('name')
        tr = []
        for ch in el:
            if ch.tag in ('matrix', 'translate', 'rotate', 'scale'):
                tr.append((ch.tag, ch.get('sid'), _floats(ch.text)))
        nodes.append(dict(name=nm or sid or nid or 'node%d' % i, parent=parent, kind=1 if el.get('type') == 'JOINT' else 0,
                          _id=nid, _sid=sid, _tr=tr, _mat=_node_local(tr)))
        names.append(nodes[-1]['name'])
        if nid:
            byid[nid] = i
        if sid and sid not in bysid:
            bysid[sid] = i
        if nm and nm not in bysid:
            bysid.setdefault(nm, i)
        for ig in el.findall('instance_geometry'):
            instances.append(dict(node=i, geom=_ref(ig.get('url')), ctrl=None, mats=_bind_mats(ig)))
        for ic in el.findall('instance_controller'):
            instances.append(dict(node=i, geom=None, ctrl=_ref(ic.get('url')), mats=_bind_mats(ic),
                                  skel=[_ref(s.text) for s in ic.findall('skeleton')]))
        for ch in el.findall('node'):
            walk(ch, i)
        for inn in el.findall('instance_node'):
            tgt = lib_nodes.get(_ref(inn.get('url')))
            if tgt is not None:
                walk(tgt, i)

    for top in scene.findall('node'):
        walk(top, -1)

    # up axis and unit: folded into the ROOT nodes' locals, so every world matrix below is Y-up metres
    R = RW.m4_identity()
    if up == 'Z_UP':
        R = [1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1]   # (x,y,z) -> (x, z, -y)
    elif up == 'X_UP':
        R = [0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]   # (x,y,z) -> (-y, x, z)
    S = RW.m4_trs([0, 0, 0], [0, 0, 0, 1], [unit, unit, unit])
    fold = RW.m4_mul(S, R)
    for nd in nodes:
        if nd['parent'] < 0:
            nd['_mat'] = RW.m4_mul(fold, nd['_mat'])
    for nd in nodes:
        t, q, s = RW.m4_decompose(nd['_mat'])
        nd['t'], nd['q'], nd['s'] = t, q, s
    W = RW.world_matrices(nodes)

    def find_node(ref):
        if ref in byid:
            return byid[ref]
        if ref in bysid:
            return bysid[ref]
        return -1

    # ---- meshes, baked to rig space
    meshes, textures, tex_index = [], [], {}

    def tex_slot(img_id):
        if not img_id or not images.get(img_id):
            return -1
        if img_id in tex_index:
            return tex_index[img_id]
        p = images[img_id]
        if p.startswith('file://'):
            p = p[7:]
            if re.match(r'^/[A-Za-z]:', p):
                p = p[1:]
        from urllib.parse import unquote
        p = unquote(p)
        cand = [p, os.path.join(base, p), os.path.join(base, os.path.basename(p))]
        blob = b''
        for c in cand:
            if os.path.isfile(c):
                try:
                    from PIL import Image
                    im = Image.open(c).convert('RGBA')
                    bio = io.BytesIO()
                    im.save(bio, 'PNG')
                    blob = bio.getvalue()
                except Exception as e:
                    rep('  !! image %s: %s' % (c, e))
                break
        if not blob:
            rep('  !! image %s not found beside the dae -- untextured' % p)
            tex_index[img_id] = -1
            return -1
        tex_index[img_id] = len(textures)
        textures.append(blob)
        return tex_index[img_id]

    for inst in instances:
        gid = inst['geom']
        skin = skins.get(inst['ctrl']) if inst['ctrl'] else None
        if skin:
            gid = skin['geom']
        g = geoms.get(gid)
        if g is None:
            rep('  !! instance of unknown geometry %s' % gid)
            continue
        node = inst['node']
        # the joint nodes of this skin, resolved by sid (Name_array) then id (IDREF_array)
        jnodes = []
        if skin:
            for jn in skin['joints']:
                k = find_node(jn)
                if k < 0:
                    rep('  !! skin %s: joint %s is not a node in the scene' % (skin['name'], jn))
                jnodes.append(k)
        for pi, pr in enumerate(g['prims']):
            inp = {}
            for (sem, src, off, st) in pr['inputs']:
                key = sem if st == 0 or sem not in inp else '%s%d' % (sem, st)
                inp.setdefault(sem, (src, off)) if sem not in inp else None
                inp[key] = (src, off)
            if 'POSITION' not in inp:
                continue
            psrc, poff = inp['POSITION']
            nsrc, noff = inp.get('NORMAL', (None, 0))
            tsrc, toff = inp.get('TEXCOORD', (None, 0))
            P, N, T = g['srcs'].get(psrc), g['srcs'].get(nsrc), g['srcs'].get(tsrc)
            uniq, pos, nrm, uv, idx, srcv = {}, [], [], [], [], []
            stride = max((off for (_s, _src, off, _st) in pr['inputs']), default=0) + 1
            if stride == 1 and (not N or N.count == P.count) and (not T or T.count == P.count):
                # ONE index stream: the vertex order is the file's own, kept as is (a round trip
                # through write_dae is then the identity, which is what makes it testable)
                for v in range(P.count):
                    uniq[(v, v if N else -1, v if T else -1)] = v
                    srcv.append(v)
                    pos.extend(P.vec(v, 3))
                    if N:
                        nrm.extend(N.vec(v, 3))
                    if T:
                        tv = T.vec(v, 2)
                        uv.extend([tv[0], 1.0 - tv[1]])
            for poly in pr['polys']:
                corners = []
                for c in poly:
                    key = (c[poff], c[noff] if N else -1, c[toff] if T else -1)
                    if key not in uniq:
                        uniq[key] = len(srcv)
                        srcv.append(c[poff])
                        pos.extend(P.vec(c[poff], 3))
                        if N:
                            nrm.extend(N.vec(c[noff], 3))
                        if T:
                            tv = T.vec(c[toff], 2)
                            uv.extend([tv[0], 1.0 - tv[1]])     # COLLADA v is bottom-up; the engine samples top-down
                    corners.append(uniq[key])
                for j in range(1, len(corners) - 1):
                    idx.extend([corners[0], corners[j], corners[j + 1]])
            nv = len(srcv)
            if nv == 0:
                continue
            mat = materials.get(_ref(inst['mats'].get(pr['material'], '')), None) or materials.get(pr['material'], None) or dict(image=None, blend=False)
            # bake to rig space
            if skin and skin['infl']:
                bsm = skin['bsm']
                pal = [RW.m4_mul(W[j], skin['ibm'][k]) if j >= 0 and k < len(skin['ibm']) else RW.m4_identity() for k, j in enumerate(jnodes)]
                pal = [RW.m4_mul(M, bsm) for M in pal]
                infl_v = []
                newp, newn = [], []
                for v in range(nv):
                    si = srcv[v]
                    lst = [(jnodes[j], w) for (j, w) in (skin['infl'][si] if si < len(skin['infl']) else []) if j < len(jnodes) and jnodes[j] >= 0]
                    tot = sum(w for _, w in lst)
                    if tot <= 0:
                        lst = [(node, 1.0)]
                        tot = 1.0
                    infl_v.append([(j, w / tot) for (j, w) in lst])
                    x, y, z = pos[v * 3:v * 3 + 3]
                    ox = oy = oz = 0.0
                    nx = ny = nz = 0.0
                    for (j, w) in (skin['infl'][si] if si < len(skin['infl']) else []):
                        if j >= len(jnodes) or jnodes[j] < 0:
                            continue
                        M = pal[j]
                        w = w / tot
                        ox += w * (M[0] * x + M[4] * y + M[8] * z + M[12])
                        oy += w * (M[1] * x + M[5] * y + M[9] * z + M[13])
                        oz += w * (M[2] * x + M[6] * y + M[10] * z + M[14])
                        if N:
                            a, b, c2 = nrm[v * 3:v * 3 + 3]
                            nx += w * (M[0] * a + M[4] * b + M[8] * c2)
                            ny += w * (M[1] * a + M[5] * b + M[9] * c2)
                            nz += w * (M[2] * a + M[6] * b + M[10] * c2)
                    if not lst or lst == [(node, 1.0)] and tot == 1.0 and not skin['infl'][si]:
                        M = W[node]
                        ox, oy, oz = M[0] * x + M[4] * y + M[8] * z + M[12], M[1] * x + M[5] * y + M[9] * z + M[13], M[2] * x + M[6] * y + M[10] * z + M[14]
                    newp.extend([ox, oy, oz])
                    if N:
                        L = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
                        newn.extend([nx / L, ny / L, nz / L])
                joints, weights, boneMap = RW.skin_from_influences(nv, infl_v, [nd['parent'] for nd in nodes], report=rep)
                for b in boneMap:
                    nodes[b]['kind'] = 1
                meshes.append(dict(name=g['name'], node=node, pos=newp, nrm=newn if N else None, uv=uv if T else None, idx=idx,
                                   joints=joints, weights=weights, boneMap=boneMap,
                                   ibm=[c for b in boneMap for c in RW.m4_inverse(W[b])], bind=RW.m4_identity(),
                                   tex=tex_slot(mat['image']), ntex=-1, blend=False, _blendhint=mat['blend']))
            else:
                M = W[node]
                newp, newn = [], []
                for v in range(nv):
                    x, y, z = pos[v * 3:v * 3 + 3]
                    newp.extend([M[0] * x + M[4] * y + M[8] * z + M[12], M[1] * x + M[5] * y + M[9] * z + M[13], M[2] * x + M[6] * y + M[10] * z + M[14]])
                    if N:
                        a, b, c2 = nrm[v * 3:v * 3 + 3]
                        nx, ny, nz = M[0] * a + M[4] * b + M[8] * c2, M[1] * a + M[5] * b + M[9] * c2, M[2] * a + M[6] * b + M[10] * c2
                        L = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
                        newn.extend([nx / L, ny / L, nz / L])
                joints, weights, boneMap = RW.rigid_skin(nv, node)
                meshes.append(dict(name=g['name'], node=node, pos=newp, nrm=newn if N else None, uv=uv if T else None, idx=idx,
                                   joints=joints, weights=weights, boneMap=boneMap, ibm=RW.m4_inverse(W[node]), bind=RW.m4_identity(),
                                   tex=tex_slot(mat['image']), ntex=-1, blend=False, _blendhint=mat['blend']))
            if nodes[node]['kind'] != 1:
                nodes[node]['kind'] = 2
    # blend: the hint plus the evidence (an alpha channel that is substantially see-through)
    for m in meshes:
        if m.pop('_blendhint', False) and m['tex'] >= 0:
            m['blend'] = _png_is_cutout(textures[m['tex']])
            if m['blend']:
                rep('  mesh %s: material asks for transparency and the texture is see-through -> blend pass' % m['name'])

    # ---- animations
    clips = _read_animations(root, nodes, byid, bysid, fold, fps, rep)
    rep('  read: %d nodes (%d joints), %d meshes, %d clips, %d textures' % (
        len(nodes), sum(1 for nd in nodes if nd['kind'] == 1), len(meshes), len(clips), len(textures)))
    for nd in nodes:
        for k in ('_id', '_sid', '_tr', '_mat'):
            nd.pop(k, None)
    return dict(nodes=nodes, meshes=meshes, clips=clips, textures=textures, names=names)


def _bind_mats(inst):
    out = {}
    for im in inst.findall('bind_material/technique_common/instance_material'):
        out[im.get('symbol')] = im.get('target')
    return out


def _png_is_cutout(blob):
    try:
        from PIL import Image
        im = Image.open(io.BytesIO(blob))
        if im.mode not in ('RGBA', 'LA', 'PA'):
            return False
        a = im.convert('RGBA').getchannel('A').resize((64, 64))
        px = list(a.getdata())
        return sum(1 for v in px if v < 128) / float(len(px)) > 0.08
    except Exception:
        return False


def _read_animations(root, nodes, byid, bysid, fold, fps, rep):
    """Every <channel> becomes (node, element sid, member, times, values, stride); the node's
    transform stack is re-evaluated per frame and decomposed."""
    chans = []

    def find_node(ref):
        return byid.get(ref, bysid.get(ref, -1))

    def walk(an, top):
        srcs = {s.get('id'): _Src(s) for s in an.findall('source')}
        samplers = {}
        for sm in an.findall('sampler'):
            d = {}
            for i in sm.findall('input'):
                d[i.get('semantic')] = srcs.get(_ref(i.get('source')))
            samplers[sm.get('id')] = d
        for ch in an.findall('channel'):
            sm = samplers.get(_ref(ch.get('source')))
            if not sm or 'INPUT' not in sm or 'OUTPUT' not in sm or sm['INPUT'] is None or sm['OUTPUT'] is None:
                continue
            tgt = ch.get('target', '')
            if '/' not in tgt:
                continue
            nref, rest = tgt.split('/', 1)
            node = find_node(nref)
            if node < 0:
                continue
            m = re.match(r'^([^.()]+)(?:\.(\w+))?(?:\((\d+)\)(?:\((\d+)\))?)?$', rest)
            if not m:
                continue
            sid, member, r, c = m.group(1), m.group(2), m.group(3), m.group(4)
            chans.append(dict(node=node, sid=sid, member=member, r=r, c=c, times=sm['INPUT'].floats,
                              vals=sm['OUTPUT'].floats, stride=sm['OUTPUT'].stride, name=an.get('id') or an.get('name') or '',
                              top=top))
        for sub in an.findall('animation'):
            walk(sub, top)

    tops = root.findall('library_animations/animation')
    for an in tops:
        walk(an, an)
    if not chans:
        return []
    # clips: library_animation_clips when present; else each top-level animation that CONTAINS
    # animations is a clip (write_dae's form, Blender's action containers); else one clip
    groups = []
    for cl in root.findall('library_animation_clips/animation_clip'):
        ids = set(_ref(ia.get('url')) for ia in cl.findall('instance_animation'))
        sel = [ch for ch in chans if ch['name'] in ids or (ch['top'].get('id') in ids)]
        if sel:
            groups.append((cl.get('name') or cl.get('id') or 'clip%d' % len(groups), sel, cl.get('start'), cl.get('end')))
    if not groups:
        containers = [an for an in tops if an.find('animation') is not None]
        if len(containers) > 1 and len(containers) == len(tops):
            for an in containers:
                sel = [ch for ch in chans if ch['top'] is an]
                if sel:
                    groups.append((an.get('name') or an.get('id') or 'clip%d' % len(groups), sel, None, None))
    if not groups:
        groups.append(('anim', chans, None, None))
    clips = []
    for (nm, sel, t0, t1) in groups:
        tmin = min(min(ch['times']) for ch in sel if ch['times'])
        tmax = max(max(ch['times']) for ch in sel if ch['times'])
        if t0 is not None:
            tmin = float(t0)
        if t1 is not None:
            tmax = float(t1)
        frames = max(2, int(round((tmax - tmin) * fps)) + 1)
        by_node = {}
        for ch in sel:
            by_node.setdefault(ch['node'], []).append(ch)
        tracks = {}
        for node, chs in by_node.items():
            nd = nodes[node]
            tr = [(k, s, list(v)) for (k, s, v) in nd['_tr']]
            rot, tra, sca = [], [], []
            for f in range(frames):
                t = tmin + f / fps
                cur = [(k, s, list(v)) for (k, s, v) in tr]
                whole = None
                for ch in chs:
                    val = _sample(ch['times'], ch['vals'], ch['stride'], t)
                    if ch['sid'] == 'transform' and ch['stride'] == 16 and not any(s == 'transform' for (_k, s, _v) in cur):
                        whole = _rowmajor_to_cm(val)
                        continue
                    for (k, s, v) in cur:
                        if s != ch['sid']:
                            continue
                        if ch['member']:
                            mi = {'X': 0, 'Y': 1, 'Z': 2, 'W': 3, 'ANGLE': 3, 'S': 0, 'T': 1, 'P': 2, 'Q': 3}.get(ch['member'].upper())
                            if mi is not None and mi < len(v):
                                v[mi] = val[0]
                        elif ch['r'] is not None and ch['c'] is not None:
                            v[int(ch['r']) * 4 + int(ch['c'])] = val[0]
                        elif ch['r'] is not None:
                            v[int(ch['r'])] = val[0]
                        else:
                            n = min(len(v), len(val))
                            v[:n] = val[:n]
                M = whole if whole is not None else _node_local(cur)
                if nd['parent'] < 0:
                    M = RW.m4_mul(fold, M)
                t3, q4, s3 = RW.m4_decompose(M)
                if rot and sum(a * b for a, b in zip(rot[-4:], q4)) < 0:
                    q4 = [-x for x in q4]
                rot.extend(q4)
                tra.extend(t3)
                sca.extend(s3)
            tracks[(node, 0)] = rot
            tracks[(node, 1)] = tra
            if any(abs(sca[i] - nd['s'][i % 3]) > 1e-4 for i in range(len(sca))):
                tracks[(node, 2)] = sca
        clips.append((nm, fps, frames, tracks))
        rep('  clip %s: %.2fs -> %d frames @ %g, %d nodes driven' % (nm, tmax - tmin, frames, fps, len(by_node)))
    return clips


def _sample(times, vals, stride, t):
    n = len(times)
    if n == 0:
        return [0.0] * stride
    if t <= times[0]:
        return vals[0:stride]
    if t >= times[-1]:
        return vals[(n - 1) * stride:n * stride]
    lo, hi = 0, n - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if times[mid] <= t:
            lo = mid
        else:
            hi = mid
    span = times[hi] - times[lo]
    u = 0.0 if span <= 0 else (t - times[lo]) / span
    a = vals[lo * stride:(lo + 1) * stride]
    b = vals[hi * stride:(hi + 1) * stride]
    return [a[i] + (b[i] - a[i]) * u for i in range(stride)]


# ----------------------------------------------------------------- writing
def write_dae(path, nodes, meshes, clips, textures, names=None, report=print):
    names = names or [nd['name'] for nd in nodes]
    used = set()
    nid = [_ncname(n, used) for n in names]
    base = os.path.splitext(os.path.basename(path))[0]
    outdir = os.path.dirname(os.path.abspath(path))
    X = []
    X.append('<?xml version="1.0" encoding="utf-8"?>')
    X.append('<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">')
    X.append('  <asset><contributor><authoring_tool>the club daelib.py</authoring_tool></contributor>'
             '<unit name="meter" meter="1"/><up_axis>Y_UP</up_axis></asset>')
    # images + effects + materials
    texfiles = []
    for ti, blob in enumerate(textures):
        fn = '%s_tex%d.png' % (base, ti)
        open(os.path.join(outdir, fn), 'wb').write(blob)
        texfiles.append(fn)
    if texfiles:
        X.append('  <library_images>')
        for ti, fn in enumerate(texfiles):
            X.append('    <image id="img%d" name="img%d"><init_from>%s</init_from></image>' % (ti, ti, fn))
        X.append('  </library_images>')
    X.append('  <library_effects>')
    for mi, m in enumerate(meshes):
        X.append('    <effect id="fx%d"><profile_COMMON>' % mi)
        if m.get('tex', -1) >= 0 and m['tex'] < len(texfiles):
            X.append('      <newparam sid="surf%d"><surface type="2D"><init_from>img%d</init_from></surface></newparam>' % (mi, m['tex']))
            X.append('      <newparam sid="samp%d"><sampler2D><source>surf%d</source></sampler2D></newparam>' % (mi, mi))
            diffuse = '<texture texture="samp%d" texcoord="UVMap"/>' % mi
        else:
            diffuse = '<color>0.8 0.8 0.8 1</color>'
        X.append('      <technique sid="common"><lambert><diffuse>%s</diffuse>%s</lambert></technique>' % (
            diffuse, '<transparent opaque="A_ONE">%s</transparent><transparency><float>1</float></transparency>' % diffuse if m.get('blend') else ''))
        X.append('    </profile_COMMON></effect>')
    X.append('  </library_effects>')
    X.append('  <library_materials>')
    for mi in range(len(meshes)):
        X.append('    <material id="mat%d" name="mat%d"><instance_effect url="#fx%d"/></material>' % (mi, mi, mi))
    X.append('  </library_materials>')
    # geometries
    X.append('  <library_geometries>')
    for mi, m in enumerate(meshes):
        nv = len(m['pos']) // 3
        X.append('    <geometry id="geom%d" name="%s"><mesh>' % (mi, _xml(m.get('name') or 'mesh%d' % mi)))
        X.append('      <source id="geom%d_pos"><float_array id="geom%d_pos_a" count="%d">%s</float_array>'
                 '<technique_common><accessor source="#geom%d_pos_a" count="%d" stride="3"><param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common></source>'
                 % (mi, mi, nv * 3, _fmt(m['pos']), mi, nv))
        inputs = ['<input semantic="VERTEX" source="#geom%d_v" offset="0"/>' % mi]
        if m.get('nrm'):
            X.append('      <source id="geom%d_nrm"><float_array id="geom%d_nrm_a" count="%d">%s</float_array>'
                     '<technique_common><accessor source="#geom%d_nrm_a" count="%d" stride="3"><param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common></source>'
                     % (mi, mi, nv * 3, _fmt(m['nrm']), mi, nv))
            inputs.append('<input semantic="NORMAL" source="#geom%d_nrm" offset="0"/>' % mi)
        if m.get('uv'):
            uvs = [m['uv'][i] if i % 2 == 0 else 1.0 - m['uv'][i] for i in range(len(m['uv']))]
            X.append('      <source id="geom%d_uv"><float_array id="geom%d_uv_a" count="%d">%s</float_array>'
                     '<technique_common><accessor source="#geom%d_uv_a" count="%d" stride="2"><param name="S" type="float"/><param name="T" type="float"/></accessor></technique_common></source>'
                     % (mi, mi, nv * 2, _fmt(uvs), mi, nv))
            inputs.append('<input semantic="TEXCOORD" source="#geom%d_uv" offset="0" set="0"/>' % mi)
        if m.get('col'):
            nc = len(m['col']) // nv
            X.append('      <source id="geom%d_col"><float_array id="geom%d_col_a" count="%d">%s</float_array>'
                     '<technique_common><accessor source="#geom%d_col_a" count="%d" stride="%d">%s</accessor></technique_common></source>'
                     % (mi, mi, len(m['col']), _fmt(m['col']), mi, nv, nc, ''.join('<param name="%s" type="float"/>' % ch for ch in 'RGBA'[:nc])))
            inputs.append('<input semantic="COLOR" source="#geom%d_col" offset="0" set="0"/>' % mi)
        X.append('      <vertices id="geom%d_v"><input semantic="POSITION" source="#geom%d_pos"/></vertices>' % (mi, mi))
        X.append('      <triangles material="mat%d" count="%d">%s<p>%s</p></triangles>' % (
            mi, len(m['idx']) // 3, ''.join(inputs), ' '.join(str(i) for i in m['idx'])))
        X.append('    </mesh></geometry>')
    X.append('  </library_geometries>')
    # controllers: one skin per mesh
    X.append('  <library_controllers>')
    for mi, m in enumerate(meshes):
        nv = len(m['pos']) // 3
        bm = m['boneMap']
        X.append('    <controller id="skin%d" name="skin%d"><skin source="#geom%d">' % (mi, mi, mi))
        X.append('      <bind_shape_matrix>%s</bind_shape_matrix>' % _fmt(_cm_to_rowmajor(m.get('bind') or RW.m4_identity())))
        X.append('      <source id="skin%d_j"><Name_array id="skin%d_j_a" count="%d">%s</Name_array>'
                 '<technique_common><accessor source="#skin%d_j_a" count="%d" stride="1"><param name="JOINT" type="name"/></accessor></technique_common></source>'
                 % (mi, mi, len(bm), ' '.join(nid[b] for b in bm), mi, len(bm)))
        ibm_rm = []
        for k in range(len(bm)):
            ibm_rm.extend(_cm_to_rowmajor(m['ibm'][k * 16:(k + 1) * 16]))
        X.append('      <source id="skin%d_ibm"><float_array id="skin%d_ibm_a" count="%d">%s</float_array>'
                 '<technique_common><accessor source="#skin%d_ibm_a" count="%d" stride="16"><param name="TRANSFORM" type="float4x4"/></accessor></technique_common></source>'
                 % (mi, mi, len(bm) * 16, _fmt(ibm_rm), mi, len(bm)))
        wts, vcount, vv = [], [], []
        for v in range(nv):
            n = 0
            for k in range(4):
                w = m['weights'][v][k]
                if w > 1e-6:
                    vv.extend([m['joints'][v][k], len(wts)])
                    wts.append(w)
                    n += 1
            if n == 0:
                vv.extend([0, len(wts)])
                wts.append(1.0)
                n = 1
            vcount.append(n)
        X.append('      <source id="skin%d_w"><float_array id="skin%d_w_a" count="%d">%s</float_array>'
                 '<technique_common><accessor source="#skin%d_w_a" count="%d" stride="1"><param name="WEIGHT" type="float"/></accessor></technique_common></source>'
                 % (mi, mi, len(wts), _fmt(wts), mi, len(wts)))
        X.append('      <joints><input semantic="JOINT" source="#skin%d_j"/><input semantic="INV_BIND_MATRIX" source="#skin%d_ibm"/></joints>' % (mi, mi))
        X.append('      <vertex_weights count="%d"><input semantic="JOINT" source="#skin%d_j" offset="0"/><input semantic="WEIGHT" source="#skin%d_w" offset="1"/>'
                 '<vcount>%s</vcount><v>%s</v></vertex_weights>' % (nv, mi, mi, ' '.join(str(c) for c in vcount), ' '.join(str(i) for i in vv)))
        X.append('    </skin></controller>')
    X.append('  </library_controllers>')
    # animations: baked matrices per driven node
    if clips:
        X.append('  <library_animations>')
        for ci, (cn, fps, frames, tracks) in enumerate(clips):
            cid = _ncname(cn, used)
            X.append('    <animation id="%s" name="%s">' % (cid, _xml(cn)))
            driven = sorted(set(n for (n, _t) in tracks))
            times = _fmt([f / fps for f in range(frames)])
            for n in driven:
                nd = nodes[n]
                mats = []
                for f in range(frames):
                    q = tracks[(n, 0)][f * 4:f * 4 + 4] if (n, 0) in tracks else nd['q']
                    t = tracks[(n, 1)][f * 3:f * 3 + 3] if (n, 1) in tracks else nd['t']
                    s = tracks[(n, 2)][f * 3:f * 3 + 3] if (n, 2) in tracks else nd['s']
                    mats.extend(_cm_to_rowmajor(RW.m4_trs(t, q, s)))
                a = '%s_%s' % (cid, nid[n])
                X.append('      <animation id="%s"><source id="%s_in"><float_array id="%s_in_a" count="%d">%s</float_array>'
                         '<technique_common><accessor source="#%s_in_a" count="%d" stride="1"><param name="TIME" type="float"/></accessor></technique_common></source>'
                         % (a, a, a, frames, times, a, frames))
                X.append('      <source id="%s_out"><float_array id="%s_out_a" count="%d">%s</float_array>'
                         '<technique_common><accessor source="#%s_out_a" count="%d" stride="16"><param name="TRANSFORM" type="float4x4"/></accessor></technique_common></source>'
                         % (a, a, frames * 16, _fmt(mats), a, frames))
                X.append('      <source id="%s_interp"><Name_array id="%s_interp_a" count="%d">%s</Name_array>'
                         '<technique_common><accessor source="#%s_interp_a" count="%d" stride="1"><param name="INTERPOLATION" type="name"/></accessor></technique_common></source>'
                         % (a, a, frames, ' '.join(['LINEAR'] * frames), a, frames))
                X.append('      <sampler id="%s_s"><input semantic="INPUT" source="#%s_in"/><input semantic="OUTPUT" source="#%s_out"/><input semantic="INTERPOLATION" source="#%s_interp"/></sampler>'
                         '<channel source="#%s_s" target="%s/transform"/></animation>' % (a, a, a, a, a, nid[n]))
            X.append('    </animation>')
        X.append('  </library_animations>')
    # the scene
    X.append('  <library_visual_scenes><visual_scene id="Scene" name="Scene">')
    children = {}
    for i, nd in enumerate(nodes):
        children.setdefault(nd['parent'], []).append(i)
    mesh_on = {}
    for mi, m in enumerate(meshes):
        mesh_on.setdefault(m['node'], []).append(mi)

    def emit(i, depth):
        nd = nodes[i]
        ind = '    ' + '  ' * depth
        joint = nd.get('kind', 0) == 1
        X.append('%s<node id="%s" name="%s" sid="%s" type="%s">' % (ind, nid[i], _xml(names[i]), nid[i], 'JOINT' if joint else 'NODE'))
        X.append('%s  <matrix sid="transform">%s</matrix>' % (ind, _fmt(_cm_to_rowmajor(RW.m4_trs(nd['t'], nd['q'], nd['s'])))))
        for c in children.get(i, []):
            emit(c, depth + 1)
        X.append('%s</node>' % ind)

    for r in children.get(-1, []):
        emit(r, 0)
    # the meshes as their own top-level nodes carrying the skins (positions are rig space: identity node)
    roots = [i for i, nd in enumerate(nodes) if nd['parent'] < 0]
    for mi, m in enumerate(meshes):
        X.append('    <node id="meshnode%d" name="%s" type="NODE"><instance_controller url="#skin%d">%s'
                 '<bind_material><technique_common><instance_material symbol="mat%d" target="#mat%d"><bind_vertex_input semantic="UVMap" input_semantic="TEXCOORD" input_set="0"/></instance_material></technique_common></bind_material>'
                 '</instance_controller></node>' % (mi, _xml(m.get('name') or 'mesh%d' % mi), mi,
                                                     ''.join('<skeleton>#%s</skeleton>' % nid[r] for r in roots), mi, mi))
    X.append('  </visual_scene></library_visual_scenes>')
    X.append('  <scene><instance_visual_scene url="#Scene"/></scene>')
    X.append('</COLLADA>')
    open(path, 'w', encoding='utf-8').write('\n'.join(X))
    if report:
        report('%s: %d nodes, %d meshes, %d clips, %d texture file(s)' % (path, len(nodes), len(meshes), len(clips), len(texfiles)))
    return path


def _fmt(vals):
    return ' '.join(('%.6g' % v) if isinstance(v, float) else str(v) for v in vals)


def _xml(s):
    return (s or '').replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;')
