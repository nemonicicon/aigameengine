"""merge_zombies.py -- fuse the 32 exported zombie clips onto one rig -> zombies.glb

    python skate_src/zombies/merge_zombies.py

In : skate_src/zombies/export/anim_*.glb   (32 GLBs, one clip each, from ue_export_zombies.py)
Out: skate_src/zombies/zombies.glb          (one rig, 32 named clips)

⭐ NO BLENDER. merge_mob1.py needed it because its source was FBX; these 32 came out of
one UE exporter against one skeleton, and MEASURED identical: 90 nodes, same names, same
order, 30fps, T/R/S on all 89 joints in every file. When the rigs are bit-identical the
merge is JSON surgery -- copy the base's mesh/skin/nodes once, then append each file's
animation with its node indices used verbatim. It also dodges the FBX import traps
(merge_mob1's note: the UE scale-compensation convention crushed kaeli's hips to 1e-4).

WHAT IS KEPT, and why -- measured off the clips, not assumed:

  * ROTATION on every joint. This is what the club's donor transfer actually replays.
  * PELVIS TRANSLATION. It is the HEIGHT SIGNAL and the bank is useless without it:
    pelvis y is 0.84 standing (Idle_A), 0.82-0.92 walking, and **0.11 on the belly
    crawls and 0.10 laying down**. Drop it and a crawling zombie stands up.
  * ROOT TRANSLATION IS ZEROED -- the clips go IN PLACE. Walk/Run/Idle already are
    (root travels 0.03 over a whole cycle); the four attacks are not (Attack_A lunges
    z -0.15..1.91). The actor system drives travel with `spd`, so a clip that also
    walks itself forward double-counts.
  * SCALE IS DROPPED ENTIRELY. The documented trap, from the kaeli bake.
  * TRANSLATION ON EVERY OTHER JOINT IS DROPPED. Bones do not stretch; their rest
    translations already live in the node hierarchy.

CLIP NAMES ARE THE ENGINE CONTRACT. Everything is `z_`-prefixed so it dodges every
matcher in crowd_kms.c: dnElect() elects on EXACT names (w_walk, v_walk*, w_run, idle,
x_fidget*, x_convo*, x_gunfire, x_down, x_flee*) and the pack-local walk finder at line
595 takes ANY name containing "alk". A zombie shamble must never be elected as the
crowd's walk -- the neighbours have somewhere to be.
"""
import json, struct, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "export")
OUT = os.path.join(HERE, "zombies.glb")

# UE asset name -> club clip name
NAME = {
    "anim_Walk_A": "z_walk_a", "anim_Walk_B": "z_walk_b", "anim_Walk_C": "z_walk_c",
    "anim_Run_A": "z_run",
    "anim_Idle_A": "z_idle_a", "anim_Idle_B": "z_idle_b",
    "anim_Attack_A": "z_atk_a", "anim_Attack_B": "z_atk_b",
    "anim_Attack_C": "z_atk_c", "anim_Attack_D": "z_atk_d",
    "anim_Belly_Crawling_A": "z_crawl_a", "anim_Belly_Crawling_B": "z_crawl_b",
    "anim_Laying_Idle_A": "z_lay_a", "anim_Laying_Idle_B": "z_lay_b",
    "anim_Taking_Damage_A": "z_hit_a", "anim_Taking_Damage_B": "z_hit_b",
    "anim_Taking_Damage_C": "z_hit_c", "anim_Taking_Damage_D": "z_hit_d",
    "anim_Taking_Damage_E": "z_hit_e", "anim_Taking_Damage_F": "z_hit_f",
    "anim_Impaled_A": "z_imp_a", "anim_Impaled_B": "z_imp_b",
    "anim_Burst_A": "z_burst_a", "anim_Burst_B": "z_burst_b", "anim_Burst_C": "z_burst_c",
    "anim_Burst_D": "z_burst_d", "anim_Burst_E": "z_burst_e", "anim_Burst_F": "z_burst_f",
    "anim_Burst_G": "z_burst_g", "anim_Burst_H": "z_burst_h", "anim_Burst_I": "z_burst_i",
    "anim_Burst_K": "z_burst_k",
}
# the base carries the mesh + skin + node hierarchy; a standing rest is the sane choice
BASE = "anim_Idle_A"

CT = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2), 5123: ('H', 2),
      5125: ('I', 4), 5126: ('f', 4)}
NC = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


def read_glb(path):
    d = open(path, 'rb').read()
    if d[:4] != b'glTF':
        raise SystemExit("not a GLB: " + path)
    off, js, bn = 12, None, b''
    while off < len(d):
        clen, ctype = struct.unpack('<I4s', d[off:off + 8])
        c = d[off + 8:off + 8 + clen]
        if ctype == b'JSON':
            js = json.loads(c)
        elif ctype[:3] == b'BIN':
            bn = c
        off += 8 + clen
        off += (-off) % 4
    return js, bn


def read_acc(js, bn, i):
    """Accessor -> list of tuples. Handles strides; these exports are tightly packed."""
    a = js['accessors'][i]
    fmt, sz = CT[a['componentType']]
    n = NC[a['type']]
    bv = js['bufferViews'][a['bufferView']]
    base = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
    stride = bv.get('byteStride') or sz * n
    return [struct.unpack_from('<' + fmt * n, bn, base + k * stride)
            for k in range(a['count'])]


def main():
    files = {}
    for f in sorted(os.listdir(SRC)):
        if f.endswith(".glb"):
            files[f[:-4]] = os.path.join(SRC, f)
    missing = [k for k in NAME if k not in files]
    if missing:
        print("MISSING source clips: " + ", ".join(missing))
    if BASE not in files:
        raise SystemExit("base clip %s not exported" % BASE)

    js, bn = read_glb(files[BASE])
    names = [n.get('name') for n in js['nodes']]
    root_i = names.index('root') if 'root' in names else -1
    pelvis_i = names.index('pelvis') if 'pelvis' in names else -1
    print("base %s: %d nodes, root=%d pelvis=%d" % (BASE, len(names), root_i, pelvis_i))

    # Start from the base's buffer; new animation data is appended to it.
    buf = bytearray(bn)
    out = dict(js)
    out['animations'] = []
    out['accessors'] = list(js['accessors'])
    out['bufferViews'] = list(js['bufferViews'])

    def add_acc(vals, ncomp, is_time):
        """Append float data, return the new accessor index."""
        while len(buf) % 4:
            buf.append(0)
        off = len(buf)
        flat = []
        for v in vals:
            flat.extend(v[:ncomp])
        buf.extend(struct.pack('<%df' % len(flat), *flat))
        out['bufferViews'].append({'buffer': 0, 'byteOffset': off,
                                   'byteLength': len(flat) * 4})
        a = {'bufferView': len(out['bufferViews']) - 1, 'componentType': 5126,
             'count': len(vals), 'type': 'SCALAR' if ncomp == 1 else 'VEC%d' % ncomp}
        if is_time:                      # glTF REQUIRES min/max on animation input
            ts = [v[0] for v in vals]
            a['min'] = [min(ts)]
            a['max'] = [max(ts)]
        out['accessors'].append(a)
        return len(out['accessors']) - 1

    kept = 0
    for ue, club in sorted(NAME.items(), key=lambda kv: kv[1]):
        if ue not in files:
            continue
        sjs, sbn = read_glb(files[ue])
        snames = [n.get('name') for n in sjs['nodes']]
        if snames != names:
            print("  SKIP %-12s rig differs from base" % club)
            continue
        anim = sjs['animations'][0]
        chans, samps = [], []
        nrot = ntr = 0
        for c in anim['channels']:
            t = c['target']
            path, node = t['path'], t['node']
            if path == 'scale':
                continue                                  # the kaeli trap
            if path == 'translation' and node != pelvis_i:
                continue                                  # in-place; bones don't stretch
            s = anim['samplers'][c['sampler']]
            times = read_acc(sjs, sbn, s['input'])
            vals = read_acc(sjs, sbn, s['output'])
            ncomp = 4 if path == 'rotation' else 3
            ti = add_acc(times, 1, True)
            vi = add_acc(vals, ncomp, False)
            samps.append({'input': ti, 'output': vi,
                          'interpolation': s.get('interpolation', 'LINEAR')})
            chans.append({'sampler': len(samps) - 1,
                          'target': {'node': node, 'path': path}})
            if path == 'rotation':
                nrot += 1
            else:
                ntr += 1
        out['animations'].append({'name': club, 'channels': chans, 'samplers': samps})
        secs = max(t[0] for t in read_acc(sjs, sbn, anim['samplers'][0]['input']))
        print("  %-12s <- %-24s %3d rot + %d pelvis-tr, %.2fs" % (club, ue, nrot, ntr, secs))
        kept += 1

    out['buffers'] = [{'byteLength': len(buf)}]
    js_bytes = json.dumps(out, separators=(',', ':')).encode('utf-8')
    js_bytes += b' ' * ((-len(js_bytes)) % 4)
    while len(buf) % 4:
        buf.append(0)
    with open(OUT, 'wb') as f:
        total = 12 + 8 + len(js_bytes) + 8 + len(buf)
        f.write(b'glTF' + struct.pack('<II', 2, total))
        f.write(struct.pack('<I', len(js_bytes)) + b'JSON' + js_bytes)
        f.write(struct.pack('<I', len(buf)) + b'BIN\x00' + bytes(buf))
    print("wrote %s: %d clips, %.1f MB" % (OUT, kept, os.path.getsize(OUT) / 1e6))


if __name__ == '__main__':
    main()
