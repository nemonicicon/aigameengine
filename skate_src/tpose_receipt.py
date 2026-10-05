#!/usr/bin/env python3
"""tpose_receipt.py -- the NUMBERS behind a T-pose proof sheet (2026-10-05, Kanden round 2).

jacob, on the first Kanden proof: "Kanden is still not in a T-pose." Its solver had reported every
limb 0.0 deg off its axis -- the BONES were a perfect T while the arm MESH hung off them. An angle
table measures what the solver placed; this measures where the mesh went.

    tpose_receipt.py <tposed.rpack> [--tol 0.08] [--mirror 0.05]

Per limb bone, both sides (upper arm, forearm, hand, thigh, shin):
  * BOUND: the vertices the bone dominates (largest weight) -- their centroid's perpendicular
    distance from the bone's own line (head -> tail; a hand's tail is its End node), and where along
    the bone it falls (0 = head, 1 = tail). PASS under --tol.
  * MIRROR: left vs right centroid, |dy| and |dz| (the T-pose's own symmetry). PASS under --mirror.
  * PIECES (binding-independent): the welded mesh pieces each limb's vertices belong to, as a whole
    -- for each side the arm (or leg) pieces' principal axis, its angle off +-x (or -y), and every
    piece's centroid distance from the side's bone line. A forearm that droops off a straight bone
    shows here whatever the weights say.
The bone axes themselves (off +-x / -y, in degrees) are printed for reference, not judged: they are
the solver's own numbers.
"""
import sys, os, math
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, 'author'))
import tpose, proofrender
import rpackwrite as RW

LIMBS = [('upper arm', 'upperarm', 'lowerarm'), ('forearm', 'lowerarm', 'hand'), ('hand', 'hand', '@end'),
         ('thigh', 'thigh', 'calf'), ('shin', 'calf', 'foot')]


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    tol = float(argv[argv.index('--tol') + 1]) if '--tol' in argv else 0.08
    mtol = float(argv[argv.index('--mirror') + 1]) if '--mirror' in argv else 0.05
    nodes, meshes, clips, textures = tpose._scene_from_pack(argv[1])
    names = [nd['name'] for nd in nodes]
    RM = tpose.canon_map(nodes, names, report=None)
    W = RW.world_matrices(nodes)
    pos = lambda i: np.array(W[i][12:15])
    P, N, T, _ = proofrender.skinned(nodes, meshes)
    dom = []
    for m in meshes:
        J = np.array(m['joints']).reshape(-1, 4)
        Wt = np.array(m['weights']).reshape(-1, 4)
        dom.append(np.array(m['boneMap'])[J[np.arange(len(J)), Wt.argmax(1)]])
    dom = np.concatenate(dom)
    tris = np.vstack(T)
    print('%s: %d vertices, %d triangles, %d nodes' % (os.path.basename(argv[1]), len(P), len(tris), len(nodes)))

    def ends(key, s):
        a = RM.get(key + '_' + s, -1)
        return a

    def tail_of(child, s, head):
        if child == '@end':
            return tpose._far_child(nodes, names, head, 'end')
        return RM.get(child + '_' + s, -1)

    rows, fails = {}, []
    print('\n  BOUND -- the vertices each bone dominates, against the bone\'s own line (tol %.2f)' % tol)
    print('  %-10s %-5s %-14s %6s  %-26s %-9s %-7s %s' % ('limb', 'side', 'bone', 'verts', 'centroid', 'perp', 'along', 'bone axis off'))
    for (label, hk, ck) in LIMBS:
        for s, side in (('l', 'left'), ('r', 'right')):
            h = RM.get(hk + '_' + s, -1)
            t = tail_of(ck, s, h) if h >= 0 else -1
            if h < 0 or t < 0:
                print('  %-10s %-5s (no bone / no tail resolved)' % (label, side))
                fails.append('%s %s unresolved' % (label, side))
                continue
            a, b = pos(h), pos(t)
            u = (b - a) / np.linalg.norm(b - a)
            sel = dom == h
            if not sel.any():
                print('  %-10s %-5s %-14s     0  -- the bone deforms nothing' % (label, side, names[h]))
                fails.append('%s %s binds nothing' % (label, side))
                continue
            C = P[sel].mean(0)
            r = C - a
            perp = float(np.linalg.norm(r - (r @ u) * u))
            along = float((r @ u) / np.linalg.norm(b - a))
            axis = [1.0 if s == 'l' else -1.0, 0, 0] if 'arm' in hk or hk == 'hand' else [0, -1.0, 0]
            # the side's own sign: where the hand IS (tpose's rule), not the name
            if axis[1] == 0:
                hand = RM.get('hand_' + s, -1)
                pel = RM.get('pelvis', -1)
                if hand >= 0 and pel >= 0:
                    axis = [1.0 if pos(hand)[0] >= pos(pel)[0] else -1.0, 0, 0]
            off = math.degrees(math.acos(max(-1.0, min(1.0, float(u @ np.array(axis))))))
            ok = perp <= tol
            rows[(label, s)] = C
            if not ok:
                fails.append('%s %s perp %.3f' % (label, side, perp))
            print('  %-10s %-5s %-14s %6d  (%+.3f %+.3f %+.3f)  %.3f %s  %5.2f   %.1f deg' % (
                label, side, names[h], int(sel.sum()), C[0], C[1], C[2], perp, 'PASS' if ok else 'FAIL', along, off))
    print('\n  MIRROR -- left vs right centroids (tol %.2f in y and z)' % mtol)
    for (label, hk, ck) in LIMBS:
        if (label, 'l') in rows and (label, 'r') in rows:
            L, R = rows[(label, 'l')], rows[(label, 'r')]
            dy, dz = abs(L[1] - R[1]), abs(L[2] - R[2])
            ok = dy <= mtol and dz <= mtol
            if not ok:
                fails.append('%s mirror dy %.3f dz %.3f' % (label, dy, dz))
            print('  %-10s |dy| %.3f  |dz| %.3f  %s     (x: %+.3f / %+.3f)' % (label, dy, dz, 'PASS' if ok else 'FAIL', L[0], R[0]))

    # ---- PIECES: binding-independent
    nv = len(P)
    parent = np.arange(nv)
    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for a_, b_, c_ in tris:
        for y_ in (b_, c_):
            ra, ry = find(a_), find(y_)
            if ra != ry:
                parent[ry] = ra
    # welded: seam copies (within 0.1 mm of each other) are one piece. A radius search over a grid
    # and its 26 neighbours -- rounding alone splits a pair that straddles a cell boundary
    raw = np.concatenate([np.array(m['pos']).reshape(-1, 3) for m in meshes])
    cell = 1e-4
    grid = {}
    for i, k in enumerate(map(tuple, np.floor(raw / cell).astype(np.int64))):
        grid.setdefault(k, []).append(i)
    offs = [(a, b, c) for a in (-1, 0, 1) for b in (-1, 0, 1) for c in (-1, 0, 1)]
    for k, members in grid.items():
        for o in offs:
            other = grid.get((k[0] + o[0], k[1] + o[1], k[2] + o[2]))
            if not other:
                continue
            for i in members:
                for j in other:
                    if j > i and np.abs(raw[i] - raw[j]).max() < cell:
                        ri, rj = find(i), find(j)
                        if ri != rj:
                            parent[rj] = ri
    root = np.array([find(i) for i in range(nv)])
    print('\n  PIECES -- binding-independent: %d welded pieces; each limb side = the pieces whose vertices its bones dominate most' % len(set(root)))
    for chain, keys, axis_name in (('arm', ('upperarm', 'lowerarm', 'hand'), 'x'), ('leg', ('thigh', 'calf'), '-y')):
        for s, side in (('l', 'left'), ('r', 'right')):
            bones = [RM.get(k + '_' + s, -1) for k in keys]
            if min(bones) < 0:
                continue
            pts = [pos(b) for b in bones]
            last = tpose._far_child(nodes, names, bones[-1], 'end') if chain == 'arm' else RM.get('foot_' + s, -1)
            if last >= 0:
                pts.append(pos(last))
            members = [r for r in set(root) if np.isin(np.bincount(dom[root == r]).argmax(), bones)]
            V = np.vstack([P[root == r] for r in members])
            c = V.mean(0)
            w, v = np.linalg.eigh(np.cov((V - c).T))
            ax = v[:, -1]
            want = np.array([1.0, 0, 0]) if chain == 'arm' else np.array([0, 1.0, 0])
            ang = math.degrees(math.acos(min(1.0, abs(float(ax @ want)))))
            # each piece's centroid against the side's polyline of bone joints
            worst = 0.0
            for r in members:
                pc = P[root == r].mean(0)
                d = min(_seg_dist(pc, pts[k], pts[k + 1]) for k in range(len(pts) - 1))
                worst = max(worst, d)
            print('  %-4s %-5s %2d pieces %5d verts: principal axis %.1f deg off %s; worst piece centroid %.3f from the bone line' % (
                chain, side, len(members), len(V), ang, axis_name, worst))
    print('\n  RESULT: %s' % ('PASS -- every limb\'s mesh lies along its bone and the two sides mirror' if not fails else 'FAIL: ' + '; '.join(fails)))
    return 0 if not fails else 1


def _seg_dist(p, a, b):
    ab = b - a
    u = np.clip((p - a) @ ab / (ab @ ab), 0.0, 1.0)
    return float(np.linalg.norm(p - (a + u * ab)))


if __name__ == '__main__':
    sys.exit(main(sys.argv))
