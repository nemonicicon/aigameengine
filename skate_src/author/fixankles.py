#!/usr/bin/env python3
"""fixankles.py -- repair a constant ankle-axis error baked into a pack's clips.

    fixankles.py <pack> [--target x,y,z]

The miko disease, correctly diagnosed on the second attempt: her REST pose is a
never-rendered oddity (ankles 0.67 behind the body -- the first fix operated on it
and proved only that surgery on the wrong patient is tidy and useless). The real
defect lives in the CLIPS: the old mocap transplant wrote every ankle with a constant
axis error, so all 18 dances hold the feet pointed down-and-BACKWARD (measured mean
ankle->toe (+-0.06, -0.47, -0.22) across the m_* set).

The repair measures each side's mean dance-time foot direction, computes the world
rotation to the target (forward, toe slightly down), and applies it to every frame of
every rotation track on the ankles -- conjugated through that frame's parent world
rotation, so the correction is exact per frame, not approximate. Track bytes are
rewritten in place; a .bak is written first if none exists.
"""
import sys, os, struct, math
sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0] if "\\" in __file__ or "/" in __file__ else ".")
from rpack import Rpack
from authorlib import fk_world, q_between, qmul, qinv, qnorm

def main(path, target=(0.0, -0.30, 0.95)):
    p = Rpack(path)
    names = p.node_names(); nodes = p.nodes()
    def find1(*subs):
        c = [i for i, n in enumerate(names) if all(s in n.lower() for s in subs)]
        return c[0] if c else -1
    sides = []
    for sk in ("left", "right"):
        a = find1(sk, "ankle") if find1(sk, "ankle") >= 0 else find1(sk, "foot")
        t = find1(sk, "toe")
        if a >= 0 and t >= 0: sides.append((sk, a, t))
    assert sides, "no ankle/toe pairs found"

    # measure the mean dance-time direction per side
    meas = {a: [0.0, 0.0, 0.0] for (_s, a, _t) in sides}
    n = 0
    clip_ids = [i for i, nm in enumerate(p.clip_names()) if nm.startswith(("m_", "c_", "p_"))]
    for ci in clip_ids:
        c = p.clip(ci)
        for frac in (0.2, 0.5, 0.8):
            f = int(c["frames"] * frac)
            qo, to = {}, {}
            for (node, typ, data) in p.tracks(ci):
                comp = 4 if typ == 0 else (1 if typ == 3 else 3)
                v = list(struct.unpack_from("<%df" % comp, data, f * comp * 4))
                if typ == 0: qo[node] = v
                elif typ == 1: to[node] = v
            wp, _ = fk_world(nodes, qo, to)
            for (_s, a, t) in sides:
                v = [wp[t][k] - wp[a][k] for k in range(3)]
                l = math.sqrt(sum(x * x for x in v)) or 1
                for k in range(3): meas[a][k] += v[k] / l
            n += 1
    corr = {}
    for (_s, a, _t) in sides:
        m = [x / n for x in meas[a]]
        corr[a] = q_between(m, list(target))
        print("side %s: mean (%.2f,%.2f,%.2f) -> corrective arc %.1f deg"
              % (_s, m[0], m[1], m[2],
                 math.degrees(2 * math.acos(max(-1, min(1, corr[a][3]))))))

    if not os.path.exists(path + ".bak"):
        open(path + ".bak", "wb").write(p.buf)
    buf = bytearray(p.buf)
    fixed = 0
    for ci in clip_ids:
        c = p.clip(ci)
        tracks = p.tracks(ci)
        trmeta = []
        base = c["offTracks"]
        for t9 in range(c["nTracks"]):
            node, typ, offD = struct.unpack_from("<III", p.buf, base + t9 * 12)
            trmeta.append((node, typ, offD))
        ankset = {a for (_s, a, _t) in sides}
        if not any(node in ankset and typ == 0 for (node, typ, _o) in trmeta):
            continue
        for f in range(c["frames"]):
            qo, to = {}, {}
            for (node, typ, offD) in trmeta:
                comp = 4 if typ == 0 else (1 if typ == 3 else 3)
                v = list(struct.unpack_from("<%df" % comp, p.buf, offD + f * comp * 4))
                if typ == 0: qo[node] = v
                elif typ == 1: to[node] = v
            _wp, wq = fk_world(nodes, qo, to)
            for (node, typ, offD) in trmeta:
                if typ != 0 or node not in ankset: continue
                par = nodes[node]["parent"]
                pw = wq[par] if par >= 0 else [0, 0, 0, 1]
                A = corr[node]
                loc = qo[node]
                newq = qnorm(qmul(qinv(pw), qmul(A, qmul(pw, loc))))
                struct.pack_into("<4f", buf, offD + f * 16, *newq)
                fixed += 1
    open(path, "wb").write(buf)
    print("%s: corrected %d ankle keyframes across %d clips" % (path, fixed, len(clip_ids)))

if __name__ == "__main__":
    a = sys.argv[1:]
    tgt = (0.0, -0.30, 0.95)
    if "--target" in a:
        tgt = tuple(float(x) for x in a[a.index("--target") + 1].split(","))
    main(a[0], tgt)
