#!/usr/bin/env python3
"""bvh2rpack.py -- retarget CMU mocap (BVH) onto a character already in RPK1 form.

The engine can synthesise motion procedurally, and it has to, because most characters ship
with nothing. But procedural motion is a floor, not a ceiling: real locomotion is easier to
believe when it came off a real body. The CMU archive is 2500 clips of exactly the verbs this
needs -- walk, run, jump, crouch -- so the job is retargeting, not authoring.

RETARGETING, in one paragraph. Two skeletons never share a rest pose, so a bone's local
rotation means different things on each. What DOES transfer is how far a bone has turned
away from its own rest. So for every frame this computes the source bone's world rotation
relative to its own rest world rotation -- a delta that is rig-independent -- and applies that
same delta to the target bone's rest world rotation, then converts back into the target's
local space. Bones are matched by CANONICAL ROLE (hips, spine, left thigh...), using the same
naming rules the engine uses, so no per-rig mapping table is needed.

    bvh2rpack.py <in.rpack> <out.rpack> --add <file.bvh>:<role>[:start:end] ...
"""
import sys, os, struct, math, argparse, re

# ----------------------------------------------------------------- canonical bones
# The ENGINE only needs 16 canonical bones, because its procedural layer only drives 16.
# Retargeting is a different job: every bone that can be matched should be driven, or the ones
# left behind keep their bind pose while everything above them animates. MEASURED on reva --
# with only the engine's 16, her ankles, toes and wrists stayed frozen, which is exactly why
# her hands flared and her feet never planted.
EX = {'pelvis': 0, 'hips': 0, 'hip': 0,
      'spine': 1, 'spine01': 2, 'spine1': 2, 'spine02': 3, 'spine2': 3, 'spine03': 3,
      'chest': 2, 'lowerback': 1, 'upperback': 2, 'thorax': 3,
      'neck': 4, 'neck1': 4, 'head': 5,
      'leftshoulder': 6, 'lshoulder': 6, 'clavicle_l': 6, 'shoulderl': 6,
      'leftarm': 7, 'upperarm_l': 7, 'upper_arml': 7,
      'leftforearm': 8, 'lowerarm_l': 8, 'forearml': 8,
      'lefthand': 16, 'handl': 16,
      'rightshoulder': 9, 'rshoulder': 9, 'clavicle_r': 9, 'shoulderr': 9,
      'rightarm': 10, 'upperarm_r': 10, 'upper_armr': 10,
      'rightforearm': 11, 'lowerarm_r': 11, 'forearmr': 11,
      'righthand': 17, 'handr': 17,
      'leftupleg': 12, 'thigh_l': 12, 'thighl': 12, 'lefthip': 12,
      'leftleg': 13, 'calf_l': 13, 'shinl': 13, 'leftknee': 13,
      'leftfoot': 18, 'footl': 18, 'leftankle': 18, 'tarsall': 18,
      'lefttoebase': 20, 'lefttoe': 20, 'toel': 20,
      'rightupleg': 14, 'thigh_r': 14, 'thighr': 14, 'righthip': 14,
      'rightleg': 15, 'calf_r': 15, 'shinr': 15, 'rightknee': 15,
      'rightfoot': 19, 'footr': 19, 'rightankle': 19, 'tarsalr': 19,
      'righttoebase': 21, 'righttoe': 21, 'toer': 21}
NCANON = 22


def canon_of(raw):
    n = ''.join(c.lower() for c in (raw or '') if c not in ': ._-')
    if n.startswith('mixamorig'):
        n = n[9:]
    if n in EX:                       # try the FULL name first: Spine01 and Spine02 are
        return EX[n]                  # different bones, and stripping digits collapses them
    m = n
    while m and m[-1].isdigit():
        m = m[:-1]
    if m in EX:
        return EX[m]
    for k in sorted(EX, key=len, reverse=True):
        if n.endswith(k):
            return EX[k]
    return -1


# ----------------------------------------------------------------- quaternion / matrix
def qmul(a, b):
    return [a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
            a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
            a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
            a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]]


def qconj(q):
    return [-q[0], -q[1], -q[2], q[3]]


def qnorm(q):
    L = math.sqrt(sum(v * v for v in q)) or 1.0
    return [v / L for v in q]


def qaxis(ax, ang):
    s = math.sin(ang * 0.5)
    return [ax[0] * s, ax[1] * s, ax[2] * s, math.cos(ang * 0.5)]


def qmat(q):
    x, y, z, w = q
    return [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
            2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
            2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]


def mat_q(m):
    """3x3 (column-major, as above) to quaternion."""
    t = m[0] + m[4] + m[8]
    if t > 0:
        S = math.sqrt(t + 1.0) * 2
        return qnorm([(m[5] - m[7]) / S, (m[6] - m[2]) / S, (m[1] - m[3]) / S, 0.25 * S])
    if m[0] > m[4] and m[0] > m[8]:
        S = math.sqrt(1.0 + m[0] - m[4] - m[8]) * 2
        return qnorm([0.25 * S, (m[3] + m[1]) / S, (m[6] + m[2]) / S, (m[5] - m[7]) / S])
    if m[4] > m[8]:
        S = math.sqrt(1.0 + m[4] - m[0] - m[8]) * 2
        return qnorm([(m[3] + m[1]) / S, 0.25 * S, (m[7] + m[5]) / S, (m[6] - m[2]) / S])
    S = math.sqrt(1.0 + m[8] - m[0] - m[4]) * 2
    return qnorm([(m[6] + m[2]) / S, (m[7] + m[5]) / S, 0.25 * S, (m[1] - m[3]) / S])


# ----------------------------------------------------------------- BVH
class BVH:
    def __init__(self, path):
        txt = open(path, 'r', errors='replace').read()
        self.names, self.parent, self.offset, self.channels = [], [], [], []
        toks = txt.replace('\n', ' \n ').split()
        i = 0
        stack = []
        motion_at = None
        while i < len(toks):
            t = toks[i]
            if t in ('ROOT', 'JOINT'):
                self.names.append(toks[i + 1])
                self.parent.append(stack[-1] if stack else -1)
                self.offset.append([0.0, 0.0, 0.0])
                self.channels.append([])
                stack.append(len(self.names) - 1)
                i += 2
            elif t == 'End':
                # End Site: skip its block
                depth = 0
                while i < len(toks):
                    if toks[i] == '{':
                        depth += 1
                    elif toks[i] == '}':
                        depth -= 1
                        if depth == 0:
                            break
                    i += 1
                i += 1
            elif t == 'OFFSET':
                self.offset[stack[-1]] = [float(toks[i + 1]), float(toks[i + 2]), float(toks[i + 3])]
                i += 4
            elif t == 'CHANNELS':
                n = int(toks[i + 1])
                self.channels[stack[-1]] = toks[i + 2:i + 2 + n]
                i += 2 + n
            elif t == '}':
                if stack:
                    stack.pop()
                i += 1
            elif t == 'MOTION':
                motion_at = i
                break
            else:
                i += 1
        # motion
        j = motion_at
        while toks[j] != 'Frames:':
            j += 1
        nframes = int(toks[j + 1])
        while toks[j] != 'Time:':
            j += 1
        self.dt = float(toks[j + 1])
        vals = [float(v) for v in toks[j + 2:]]
        width = sum(len(c) for c in self.channels)
        self.frames = []
        for f in range(nframes):
            row = vals[f * width:(f + 1) * width]
            if len(row) < width:
                break
            self.frames.append(row)
        self.nframes = len(self.frames)

    def local_quats(self, f):
        """Per-bone local rotation quaternion for frame f."""
        row = self.frames[f]
        out = [[0.0, 0.0, 0.0, 1.0] for _ in self.names]
        k = 0
        AX = {'Xrotation': (1, 0, 0), 'Yrotation': (0, 1, 0), 'Zrotation': (0, 0, 1)}
        for b, chans in enumerate(self.channels):
            q = [0.0, 0.0, 0.0, 1.0]
            for c in chans:
                v = row[k]; k += 1
                if c in AX:
                    q = qmul(q, qaxis(AX[c], math.radians(v)))
            out[b] = qnorm(q)
        return out

    def world_quats(self, locals_):
        out = [None] * len(self.names)
        for b in range(len(self.names)):
            p = self.parent[b]
            out[b] = locals_[b] if p < 0 else qmul(out[p], locals_[b])
        return out


# ----------------------------------------------------------------- rpack
class Pack:
    def __init__(self, path):
        self.d = bytearray(open(path, 'rb').read())
        assert self.d[:4] == b'RPK1'
        self.H = list(struct.unpack_from('<15I', self.d, 4))
        self.nN, self.nMesh, self.nMat, self.nClip, self.nTex = self.H[1], self.H[2], self.H[3], self.H[4], self.H[5]
        self.offN, self.offMe, self.offMa, self.offCl, self.offTx = self.H[6], self.H[7], self.H[8], self.H[9], self.H[10]
        self.offStr, self.offSz, self.total = self.H[11], self.H[12], self.H[13]
        nstr = struct.unpack_from('<I', self.d, self.offStr)[0]
        self.nstr = nstr
        self.strs = []
        base = self.offStr + 4 + nstr * 8
        for i in range(nstr):
            so, sl = struct.unpack_from('<II', self.d, self.offStr + 4 + i * 8)
            self.strs.append(bytes(self.d[base + so:base + so + sl]))
        self.names, self.par, self.kind, self.rest = [], [], [], []
        for i in range(self.nN):
            nm, par, kind = struct.unpack_from('<IiI', self.d, self.offN + i * 52)
            t = list(struct.unpack_from('<3f', self.d, self.offN + i * 52 + 12))
            q = list(struct.unpack_from('<4f', self.d, self.offN + i * 52 + 24))
            s = list(struct.unpack_from('<3f', self.d, self.offN + i * 52 + 40))
            self.names.append(self.strs[nm].decode('utf8', 'replace') if nm < nstr else '?')
            self.par.append(par); self.kind.append(kind); self.rest.append((t, q, s))
        self.clips = []
        for c in range(self.nClip):
            cn, fps, fr, nt, off = struct.unpack_from('<IfIII', self.d, self.offCl + c * 20)
            tracks = []
            for t in range(nt):
                node, typ, offD = struct.unpack_from('<III', self.d, off + t * 12)
                n = 4 if typ == 0 else 3
                data = list(struct.unpack_from('<%df' % (fr * n), self.d, offD))
                tracks.append((node, typ, data))
            self.clips.append([self.strs[cn].decode('utf8', 'replace') if cn < nstr else '?',
                               fps, fr, tracks])

    def rest_world_q(self):
        out = [None] * self.nN
        for i in range(self.nN):
            q = self.rest[i][1]
            p = self.par[i]
            out[i] = list(q) if p < 0 else qmul(out[p], q)
        return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src'); ap.add_argument('dst')
    ap.add_argument('--add', action='append', default=[],
                    help='FILE.bvh:ROLE[:START:END]  (START/END in seconds)')
    ap.add_argument('--fps', type=float, default=30.0)
    a = ap.parse_args()

    P = Pack(a.src)
    print('%s: %d nodes, %d clips' % (os.path.basename(a.src), P.nN, P.nClip))

    # target: canonical -> node
    tgt = {}
    for i, nm in enumerate(P.names):
        c = canon_of(nm)
        if c >= 0 and c not in tgt:
            tgt[c] = i
    print('  target canonical bones: %d/%d' % (len(tgt), NCANON))

    Prw = P.rest_world_q()
    newclips = []
    for spec in a.add:
        # A Windows path carries its own colon (C:\...), so the spec is parsed from the RIGHT:
        # the trailing fields are the role and, optionally, a start and end time.
        def _num(x):
            try: float(x); return True
            except ValueError: return False
        bits = spec.split(':')
        if len(bits) >= 4 and _num(bits[-1]) and _num(bits[-2]):
            t0, t1 = float(bits[-2]), float(bits[-1])
            role = bits[-3]; path = ':'.join(bits[:-3])
        else:
            t0, t1 = 0.0, 1e9
            role = bits[-1]; path = ':'.join(bits[:-1])
        B = BVH(path)
        src = {}
        for i, nm in enumerate(B.names):
            c = canon_of(nm)
            if c >= 0 and c not in src:
                src[c] = i
        shared = sorted(set(src) & set(tgt))
        if not shared:
            print('  !! %s: no shared bones' % os.path.basename(path))
            continue
        # source rest world (frame-independent: BVH rest is the identity pose)
        Brw = [None] * len(B.names)
        for b in range(len(B.names)):
            p = B.parent[b]
            Brw[b] = [0.0, 0.0, 0.0, 1.0] if p < 0 else list(Brw[p])

        step = max(1, int(round((1.0 / a.fps) / B.dt))) if B.dt > 0 else 1
        f0 = max(0, int(t0 / B.dt)) if B.dt > 0 else 0
        f1 = min(B.nframes, int(t1 / B.dt)) if B.dt > 0 else B.nframes
        frames = list(range(f0, f1, step))
        if len(frames) < 2:
            print('  !! %s: no frames in range' % os.path.basename(path))
            continue

        tracks = {}
        for f in frames:
            lq = B.local_quats(f)
            for c in shared:
                sb, tb = src[c], tgt[c]
                # ---- THE RETARGETING FORMULA ----
                #   trgLocal = invBindTrgWorldParent * bindSrcWorldParent
                #              * srcLocal
                #              * invBindSrcWorld * bindTrgWorld
                #
                # The point of the bind terms on BOTH sides is that they carry a bone's local
                # rotation out of the source's rest frame and into the target's. My first
                # attempt used the source bone's ANIMATED world rotation and the target's
                # RETARGETED parent, which quietly assumes both skeletons share a rest pose.
                # They do not: reva's bind has her arms out and asymmetric, so every frame came
                # out as correct motion riding on top of a distorted pose.
                #
                # For BVH the bind pose is the hierarchy with all rotations zero, so every
                # source bind world rotation is identity and those two terms drop out.
                pn = P.par[tb]
                invBindTrgWP = qconj(Prw[pn]) if pn >= 0 else [0.0, 0.0, 0.0, 1.0]
                sp = B.parent[sb]
                bindSrcWP = Brw[sp] if sp >= 0 else [0.0, 0.0, 0.0, 1.0]
                invBindSrcW = qconj(Brw[sb])
                q = qmul(invBindTrgWP, bindSrcWP)
                q = qmul(q, lq[sb])
                q = qmul(q, invBindSrcW)
                q = qnorm(qmul(q, Prw[tb]))
                tracks.setdefault((tb, 0), []).extend(q)
        # ---- THE BODY'S RISE AND FALL ----
        # A walk is not just rotations: the body lifts over the planted leg and drops through
        # the pass. MEASURED on the first attempt, her hips sat at exactly 108.6 for all 69
        # frames -- perfectly rigid -- because rotation was all I carried across. The source
        # keeps that motion as hip TRANSLATION, so it has to come too. Only the VERTICAL part:
        # the horizontal travel belongs to the physics, which is what actually moves her.
        hipT = None
        for c in shared:
            if c != 0:
                continue
            sb, tb = src[0], tgt[0]
            ycol = None
            k = 0
            for b2, chans in enumerate(B.channels):
                for ch in chans:
                    if b2 == sb and ch == 'Yposition':
                        ycol = k
                    k += 1
            if ycol is None:
                break
            ys = [B.frames[f][ycol] for f in frames]
            mean = sum(ys) / len(ys)
            # scale the bob by the ratio of the two skeletons' hip heights
            srcHip = mean if abs(mean) > 1e-6 else 1.0
            tgtHip = 0.0
            n2 = tgt[0]
            while n2 >= 0:
                tgtHip += P.rest[n2][0][1]
                n2 = P.par[n2]
            ratio = (tgtHip / srcHip) if abs(srcHip) > 1e-6 else 1.0
            base = list(P.rest[tb][0])
            # A bone's local Y is only 'up' if nothing above it is rotated. Hers sits under an
            # Armature node and the whole rig is yawed, so writing the bob straight into local Y
            # pushed her sideways instead of lifting her -- the hip height measured dead constant
            # while the track was demonstrably present. Rotate world-up into the parent frame.
            pq = Prw[P.par[tb]] if P.par[tb] >= 0 else [0.0, 0.0, 0.0, 1.0]
            iq = qconj(pq)
            def rot(q, v):
                t = [2.0 * (q[1] * v[2] - q[2] * v[1]),
                     2.0 * (q[2] * v[0] - q[0] * v[2]),
                     2.0 * (q[0] * v[1] - q[1] * v[0])]
                return [v[0] + q[3] * t[0] + q[1] * t[2] - q[2] * t[1],
                        v[1] + q[3] * t[1] + q[2] * t[0] - q[0] * t[2],
                        v[2] + q[3] * t[2] + q[0] * t[1] - q[1] * t[0]]
            upl = rot(iq, [0.0, 1.0, 0.0])
            hipT = []
            for y in ys:
                b = (y - mean) * ratio
                hipT.extend([base[0] + upl[0] * b, base[1] + upl[1] * b, base[2] + upl[2] * b])
            tracks[(tb, 1)] = hipT
            print('     hip bob: %+.1f..%+.1f source units -> x%.3f' %
                  (min(ys) - mean, max(ys) - mean, ratio))
        nf = len(frames)
        newclips.append([role, a.fps, nf, [(k[0], k[1], v) for k, v in tracks.items()]])
        print('  %-28s -> %-10s %3d frames (%.2fs)  %d bones' %
              (os.path.basename(path), role, nf, nf / a.fps, len(shared)))

    # ---- rewrite the pack with the extra clips ----
    clips = P.clips + newclips
    strs = list(P.strs)

    def sid(s):
        b = s.encode('utf8')[:120]
        if b not in strs:
            strs.append(b)
        return strs.index(b)

    nodeName = [sid(n) for n in P.names]
    clipName = [sid(c[0]) for c in clips]

    HDR = 4 + 15 * 4
    off = HDR
    offN = off; off += 52 * P.nN
    offMe = off; off += 60 * P.nMesh
    offMa = off; off += 36 * P.nMat
    offCl = off; off += 20 * len(clips)
    offTx = off; off += 16 * P.nTex
    blobs = []

    def place(b):
        nonlocal off
        o = off; off += len(b); blobs.append(b); return o

    # mesh buffers copied verbatim, with their offsets rebased
    meshes = []
    for m in range(P.nMesh):
        f = list(struct.unpack_from('<15I', P.d, P.offMe + m * 60))
        nv, idxc, nb, fl = f[2], f[3], f[4], f[5]
        def cp(o, n):
            return place(bytes(P.d[o:o + n])) if o and n else 0
        f[6] = cp(f[6], nv * 12)
        f[7] = cp(f[7], nv * 12) if (fl & 1) else 0
        f[8] = cp(f[8], nv * 8) if (fl & 2) else 0
        f[9] = cp(f[9], nv * 4)
        f[10] = cp(f[10], nv * 4)
        f[11] = cp(f[11], idxc * 4)
        f[12] = cp(f[12], nb * 2)
        f[13] = cp(f[13], nb * 64)
        f[14] = cp(f[14], 64)
        f[0] = sid(P.strs[f[0]].decode('utf8', 'replace')) if f[0] < P.nstr else 0
        meshes.append(f)
    texs = []
    for t in range(P.nTex):
        o, n = struct.unpack_from('<II', P.d, P.offTx + t * 16 + 8)
        texs.append((place(bytes(P.d[o:o + n])), n))

    trackTabs = []
    for (nm, fps, fr, tracks) in clips:
        ents = []
        for (node, typ, data) in tracks:
            ents.append((node, typ, place(struct.pack('<%df' % len(data), *data))))
        trackTabs.append(ents)
    tabOff = [place(b''.join(struct.pack('<III', n, t, o) for (n, t, o) in e)) for e in trackTabs]

    tbl = struct.pack('<I', len(strs))
    so = 0
    for s in strs:
        tbl += struct.pack('<II', so, len(s)); so += len(s)
    tbl += b''.join(strs)
    offStr = place(tbl)
    offSz = place(bytes(P.d[P.offSz:P.offSz + 48]))
    total = off

    out = bytearray(b'RPK1')
    out += struct.pack('<15I', 2, P.nN, P.nMesh, P.nMat, len(clips), P.nTex,
                       offN, offMe, offMa, offCl, offTx, offStr, offSz, total, 0)
    for i in range(P.nN):
        t, q, s = P.rest[i]
        out += struct.pack('<IiI3f4f3f', nodeName[i], P.par[i], P.kind[i],
                           t[0], t[1], t[2], q[0], q[1], q[2], q[3], s[0], s[1], s[2])
    for f in meshes:
        out += struct.pack('<15I', *f)
    out += bytes(P.d[P.offMa:P.offMa + 36 * P.nMat])
    for ci, (nm, fps, fr, tracks) in enumerate(clips):
        out += struct.pack('<IfIII', clipName[ci], fps, fr, len(trackTabs[ci]), tabOff[ci])
    for (o, n) in texs:
        t = bytearray(16); struct.pack_into('<I', t, 8, o); struct.pack_into('<I', t, 12, n)
        out += t
    for b in blobs:
        out += b

    open(a.dst, 'wb').write(out)
    print('%s: %d clips, %d bytes' % (os.path.basename(a.dst), len(clips), len(out)))
    for (nm, fps, fr, tracks) in clips:
        print('    %-22s %3d frames @ %gfps  %d tracks' % (nm[:22], fr, fps, len(tracks)))


main()
