#!/usr/bin/env python3
"""statue_marks.py -- the measuring instrument for rigging a STATUE by hand (2026-10-05).

statue2body.py places a skeleton by landmark LAWS written for an upright human scan: the arm band is
the widest slab between 60-90% height, the legs split by the sign of x. A Metroid hunter ripped mid-
stance (Noxus hunched with his blade-arms curled up, Kanden with one arm out) breaks the laws: the
band lands on the chest and the T-pose straightens legs under curled arms (10-05, both of them). So
the joints are MEASURED instead -- off these sheets and this census -- and handed to the rigger as a
marks file (`statue2body.py rig in.glb out.glb --marks marks.txt`). Mark and paths go where the map
says, not where a guess says.

    statue_marks.py sheet <pack.rpack> <outstem>          outstem_front.png, _side.png, _top.png: the
                                                          skinned bind cloud, one colour per mesh, a
                                                          labelled 0.1 grid in the pack's own frame
    statue_marks.py slabs <pack.rpack> [--step 0.05] [--gap 0.06] [--axis x|z]
                                                          per height slab: the clusters along x (or z):
                                                          count, range, the other axis' mean, the mesh
    statue_marks.py check <pack.rpack> <marks.txt> <outstem>   the sheets with the marks and the chain
                                                          drawn over them -- LOOK before rigging

marks.txt: `Name x y z` per line (# comments), names from statue2body's CHAIN (Hips Spine Spine1 Spine2
Neck Head HeadEnd Left/RightShoulder Arm ForeArm Hand HandEnd Left/RightUpLeg Leg Foot ToeBase ToeEnd),
in THIS frame (the pack's rig space, what the sheets show). Unlisted joints keep the rigger's laws.
"""
import sys, os, math
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, 'author'))
import rpackwrite as RW

PALETTE = [(120, 200, 255), (255, 170, 80), (140, 255, 140), (255, 120, 180), (220, 220, 120), (170, 140, 255), (255, 255, 255), (120, 255, 230)]
CHAIN = [('Hips', None), ('Spine', 'Hips'), ('Spine1', 'Spine'), ('Spine2', 'Spine1'), ('Neck', 'Spine2'), ('Head', 'Neck'), ('HeadEnd', 'Head'),
         ('LeftShoulder', 'Spine2'), ('LeftArm', 'LeftShoulder'), ('LeftForeArm', 'LeftArm'), ('LeftHand', 'LeftForeArm'), ('LeftHandEnd', 'LeftHand'),
         ('RightShoulder', 'Spine2'), ('RightArm', 'RightShoulder'), ('RightForeArm', 'RightArm'), ('RightHand', 'RightForeArm'), ('RightHandEnd', 'RightHand'),
         ('LeftUpLeg', 'Hips'), ('LeftLeg', 'LeftUpLeg'), ('LeftFoot', 'LeftLeg'), ('LeftToeBase', 'LeftFoot'), ('LeftToeEnd', 'LeftToeBase'),
         ('RightUpLeg', 'Hips'), ('RightLeg', 'RightUpLeg'), ('RightFoot', 'RightLeg'), ('RightToeBase', 'RightFoot'), ('RightToeEnd', 'RightToeBase')]


def cloud(nodes, meshes):
    """[(x, y, z, mesh index)] of the skinned bind pose."""
    W = RW.world_matrices(nodes)
    pts = []
    for mi, m in enumerate(meshes):
        pal = [RW.m4_mul(W[b], m['ibm'][k * 16:(k + 1) * 16]) for k, b in enumerate(m['boneMap'])]
        bind = m.get('bind') or RW.m4_identity()
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
            pts.append((ox, oy, oz, mi))
    return pts


def read_marks(path):
    out = {}
    for ln in open(path):
        ln = ln.split('#', 1)[0].strip()
        if not ln:
            continue
        parts = ln.split()
        if len(parts) != 4:
            raise SystemExit('marks: bad line %r (want: Name x y z)' % ln)
        out[parts[0]] = (float(parts[1]), float(parts[2]), float(parts[3]))
    return out


def sheets(pts, nodes, outstem, size=1000, marks=None, names=None):
    from PIL import Image, ImageDraw
    lo = [min(p[k] for p in pts) for k in range(3)]
    hi = [max(p[k] for p in pts) for k in range(3)]
    span = max(hi[k] - lo[k] for k in range(3)) * 1.12 or 1.0
    cen = [(lo[k] + hi[k]) * 0.5 for k in range(3)]
    views = [('front', 0, 1, 1, 'x', 'y'), ('side', 2, 1, -1, 'z (left is +z)', 'y'), ('top', 0, 2, -1, 'x', 'z (up is +z)')]
    outs = []
    for (vname, a, b, sgn, la, lb) in views:
        im = Image.new('RGB', (size, size), (18, 18, 22))
        dr = ImageDraw.Draw(im)

        def proj(p):
            u = (p[a] - cen[a]) / span * sgn * size + size * 0.5
            v = size * 0.5 - (p[b] - cen[b]) / span * size
            return u, v
        # grid every 0.1, bright every 0.5, labelled
        step = 0.1
        for axis in (a, b):
            g0 = math.floor((cen[axis] - span * 0.5) / step) * step
            g = g0
            while g <= cen[axis] + span * 0.5:
                bright = abs(g / 0.5 - round(g / 0.5)) < 1e-6
                col = (70, 70, 80) if bright else (36, 36, 44)
                if abs(g) < 1e-9:
                    col = (150, 60, 60)
                if axis == a:
                    u, _ = proj([g if a == 0 else 0, g if a == 1 else 0, g if a == 2 else 0])
                    dr.line([(u, 0), (u, size)], fill=col)
                    if bright:
                        dr.text((u + 2, size - 14), '%.1f' % g, fill=(170, 170, 180))
                else:
                    _, v = proj([g if b == 0 else 0, g if b == 1 else 0, g if b == 2 else 0])
                    dr.line([(0, v), (size, v)], fill=col)
                    if bright:
                        dr.text((2, v - 12), '%.1f' % g, fill=(170, 170, 180))
                g += step
        for p in pts:
            u, v = proj(p)
            if 0 <= u < size and 0 <= v < size:
                im.putpixel((int(u), int(v)), PALETTE[p[3] % len(PALETTE)])
        if marks:
            pos = {}
            for (nm, par) in CHAIN:
                if nm in marks:
                    pos[nm] = proj(marks[nm])
            for (nm, par) in CHAIN:
                if nm in pos and par in pos:
                    dr.line([pos[par], pos[nm]], fill=(255, 200, 40), width=2)
            for nm, (u, v) in pos.items():
                dr.line([(u - 5, v), (u + 5, v)], fill=(255, 60, 60), width=2)
                dr.line([(u, v - 5), (u, v + 5)], fill=(255, 60, 60), width=2)
                dr.text((u + 6, v - 6), nm, fill=(255, 220, 120))
        dr.text((8, 6), '%s: %s across, %s up   grid 0.1 / 0.5   %d points' % (vname, la, lb, len(pts)), fill=(230, 230, 230))
        if names:
            for mi, nm in enumerate(names):
                dr.text((8, 24 + mi * 13), '%s' % nm, fill=PALETTE[mi % len(PALETTE)])
        fn = '%s_%s.png' % (outstem, vname)
        im.save(fn)
        outs.append(fn)
    return outs


def slabs(pts, step=0.05, gap=0.06, axis='x'):
    a = 0 if axis == 'x' else 2
    o = 2 if axis == 'x' else 0
    ymin, ymax = min(p[1] for p in pts), max(p[1] for p in pts)
    y = ymin
    print('  slab census along %s, %.2f steps, clusters split at gaps > %.2f: y: [n %s-range | %s-mean | mesh] ...' % (axis, step, gap, axis, 'z' if axis == 'x' else 'x'))
    while y < ymax:
        sl = sorted((p for p in pts if y <= p[1] < y + step), key=lambda p: p[a])
        y += step
        if not sl:
            continue
        clusters, cur = [], [sl[0]]
        for p in sl[1:]:
            if p[a] - cur[-1][a] > gap:
                clusters.append(cur)
                cur = [p]
            else:
                cur.append(p)
        clusters.append(cur)
        row = []
        for c in clusters:
            if len(c) < 4:
                continue
            mesh = max(set(p[3] for p in c), key=lambda m: sum(1 for p in c if p[3] == m))
            row.append('[%4d %+.2f..%+.2f | %+.2f | m%d]' % (len(c), c[0][a], c[-1][a], sum(p[o] for p in c) / len(c), mesh))
        print('  y %.2f: %s' % (y - step, '  '.join(row)))


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    import tpose
    cmd = argv[1]
    nodes, meshes, clips, textures = tpose._scene_from_pack(argv[2])
    pts = cloud(nodes, meshes)
    names = [m.get('name', 'mesh%d' % i) for i, m in enumerate(meshes)]
    if cmd == 'sheet':
        for fn in sheets(pts, nodes, argv[3], names=names):
            print('  ' + fn)
        lo = [min(p[k] for p in pts) for k in range(3)]
        hi = [max(p[k] for p in pts) for k in range(3)]
        print('  cloud: x %.3f..%.3f  y %.3f..%.3f  z %.3f..%.3f  meshes %s' % (lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], ', '.join('m%d=%s' % (i, n) for i, n in enumerate(names))))
        return 0
    if cmd == 'slabs':
        step = float(argv[argv.index('--step') + 1]) if '--step' in argv else 0.05
        gap = float(argv[argv.index('--gap') + 1]) if '--gap' in argv else 0.06
        axis = argv[argv.index('--axis') + 1] if '--axis' in argv else 'x'
        slabs(pts, step, gap, axis)
        return 0
    if cmd == 'check':
        marks = read_marks(argv[3])
        for fn in sheets(pts, nodes, argv[4], marks=marks, names=names):
            print('  ' + fn)
        missing = [nm for (nm, _p) in CHAIN if nm not in marks]
        print('  %d marks; the rigger\'s laws place the rest: %s' % (len(marks), ' '.join(missing) or 'none'))
        return 0
    print('unknown command %r' % cmd)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
