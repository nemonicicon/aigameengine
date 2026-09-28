#!/usr/bin/env python3
"""rpack.py -- read and APPEND-WRITE the club's RPK1 v2 packs.

The authoring spine's bottom rung (P0 of design/animation_authoring.md): the first path
that exists from motion data I generate to a pack the engine plays. Everything here is
append-only by design: new track data, a rebuilt string table and a rebuilt clip table go
at the END of the file and four header fields are repointed (nClips, offClips, offStr,
total). The bytes of nodes, meshes, materials, textures and every existing clip are never
touched, so a bug in this writer cannot corrupt a working pack -- and the engine's
reload=1 hot swap stays valid, because everything the GPU uploaded at boot is where it
was.

Layout (mirrors glb2rpack_anim.py's writer and crowd_kms.c's loadPack):
  'RPK1' + 15 x u32: ver nNodes nMeshes nMats nClips nTex
                     offNodes offMeshes offMats offClips offTexTable offStr offSize total 0
  clip record  '<IfIII': nameId fps frames nTracks offTracks
  track table  nTracks x '<III': node type offData        (type 0=rot(quat) 1=trans 2=scale)
  track data   frames x (4 floats rot / 3 floats trans|scale)
  string table u32 count, count x '<II' (off,len), blob   (all names are INDICES into this)

usage:
  rpack.py info <pack>                       header + clip census
  rpack.py copyclip <src> <clip> <dst> <as>  append a byte-copy of a clip (the P0 gate)
  rpack.py lint <name> [...]                 would these clip names trip the engine's miners?

library:
  p = Rpack(path); p.clip_names(); p.tracks(ci)
  append_clips(path, [(name, fps, frames, {(node,type): [floats...]}), ...])
"""
import struct, sys, os

HDR = struct.Struct("<15I")
CLIP = struct.Struct("<IfIII")

# ---- the engine's name miners, mirrored ------------------------------------------
# An authored clip that matches one of these SILENTLY changes behaviour -- a role
# election on this pack, or (via dnElect) on all 133. The z_ prefix keeps clear of the
# exact-name rules and the dance prefixes; the substrings still need checking because
# they bite anywhere in the name (z_crunch contains "run").
MINER_SUBSTRINGS = [
    "alk",                                   # walkIdx: w_ prefix OR contains 'alk'
    "run", "sprint", "flee",                 # runIdx (+ hero: jog)
    "jog",
    "attack", "punch", "kick", "slash", "swing", "strike", "jab",   # atkIdx
    "death", "dying", "dead", "fall", "down", "defeat",             # downIdx
    "idle", "stand",                         # idleIdx / hero foot-idle
    "kickflip", "flip", "trick", "ollie",    # hero trick roles
]
MINER_EXACT = ["idle", "w_walk", "w_run", "w_fly", "w_ski",
               "x_gunfire", "x_down", "x_flee", "x_flee2"]
MINER_PREFIX = ["x_fidget", "x_convo", "v_walk", "p_", "m_", "c_", "w_"]

def lint_name(name):
    """Return a list of miner rules this clip name would trip. Empty = safe."""
    lo = name.lower()
    hits = []
    if not lo.startswith("z_"):
        hits.append("not z_ prefixed (reserved authored namespace)")
    for s in MINER_SUBSTRINGS:
        if s in lo:
            hits.append("substring '%s'" % s)
    if lo in MINER_EXACT:
        hits.append("exact donor name '%s'" % lo)
    for p in MINER_PREFIX:
        if p != "z_" and lo.startswith(p):
            hits.append("prefix '%s'" % p)
    return hits

class Rpack:
    def __init__(self, path):
        self.path = path
        self.buf = open(path, "rb").read()
        if self.buf[:4] != b"RPK1":
            raise ValueError("%s: not an RPK1 pack" % path)
        h = HDR.unpack_from(self.buf, 4)
        (self.ver, self.nNodes, self.nMeshes, self.nMats, self.nClips, self.nTex,
         self.offNodes, self.offMeshes, self.offMats, self.offClips,
         self.offTexTable, self.offStr, self.offSize, self.total, _r) = h

    # ---- strings ----
    def strings(self):
        n = struct.unpack_from("<I", self.buf, self.offStr)[0]
        ents = [struct.unpack_from("<II", self.buf, self.offStr + 4 + i * 8) for i in range(n)]
        blob = self.offStr + 4 + n * 8
        return [self.buf[blob + o: blob + o + l] for (o, l) in ents]

    def sname(self, idx):
        s = self.strings()
        return s[idx].decode("utf-8", "replace") if idx < len(s) else "?%d" % idx

    # ---- clips ----
    def clip(self, ci):
        nameId, fps, frames, nTracks, offTracks = CLIP.unpack_from(self.buf, self.offClips + ci * 20)
        return dict(name=self.sname(nameId), nameId=nameId, fps=fps,
                    frames=frames, nTracks=nTracks, offTracks=offTracks)

    def clip_names(self):
        return [self.clip(i)["name"] for i in range(self.nClips)]

    def tracks(self, ci):
        """[(node, type, bytes-of-frame-data), ...] for clip ci."""
        c = self.clip(ci)
        out = []
        for t in range(c["nTracks"]):
            node, typ, offD = struct.unpack_from("<III", self.buf, c["offTracks"] + t * 12)
            comp = 4 if typ == 0 else (1 if typ == 3 else 3)
            data = self.buf[offD: offD + c["frames"] * comp * 4]
            out.append((node, typ, data))
        return out

    def node_names(self):
        # node record '<IiI3f4f3f' = 52 bytes; name id is the first u32
        return [self.sname(struct.unpack_from("<I", self.buf, self.offNodes + i * 52)[0])
                for i in range(self.nNodes)]

    def nodes(self):
        """Full node records for FK: [{name, parent, kind, t, q, s}] -- q is xyzw,
        exactly as m4_trs consumes it."""
        out = []
        for i in range(self.nNodes):
            nameId, parent, kind, tx, ty, tz, qx, qy, qz, qw, sx, sy, sz = \
                struct.unpack_from("<IiI3f4f3f", self.buf, self.offNodes + i * 52)
            out.append(dict(name=self.sname(nameId), parent=parent, kind=kind,
                            t=[tx, ty, tz], q=[qx, qy, qz, qw], s=[sx, sy, sz]))
        return out

    def node_index(self, name):
        for i, n in enumerate(self.node_names()):
            if n == name: return i
        raise KeyError("no node named %r" % name)

    # ---- meshes ----
    MESH = struct.Struct("<15I")   # name,node,verts,idxCount,boneCount,flags,offPos,...

    def mesh_positions(self):
        """All meshes' RAW vertex positions -- MESH space, which is NOT rig space for
        every pack (a UE export's buffer read +/-105cm deep on a slim NPC). For
        measuring bodies use skinned_positions."""
        out = []
        for mi in range(self.nMeshes):
            rec = self.MESH.unpack_from(self.buf, self.offMeshes + mi * 60)
            verts, offPos = rec[2], rec[6]
            out.extend(struct.iter_unpack("<3f", self.buf[offPos: offPos + verts * 12]))
        return out

    def skinned_positions(self, with_bones=False):
        """BIND-POSE vertex positions in RIG space: each vertex through its bones'
        world x inverse-bind, weighted -- exactly the GPU's skinning at rest. On a
        pack whose buffer already sits in rig space this is the identity; on one
        with a mesh-node transform (UE exports) it is the only correct frame."""
        # rest-pose world matrices, parent-first (the baker guarantees the order)
        nodes = self.nodes()
        W = [None] * len(nodes)
        for i, nd in enumerate(nodes):
            L = _m4_trs(nd["t"], nd["q"], nd["s"])
            W[i] = L if nd["parent"] < 0 else _m4_mul(W[nd["parent"]], L)
        out = []
        for mi in range(self.nMeshes):
            rec = self.MESH.unpack_from(self.buf, self.offMeshes + mi * 60)
            verts, boneCount = rec[2], rec[4]
            offPos, offJoints, offWeights = rec[6], rec[9], rec[10]
            offBoneMap, offIBM = rec[12], rec[13]
            if not boneCount:
                for v in struct.iter_unpack("<3f", self.buf[offPos: offPos + verts * 12]):
                    out.append((v[0], v[1], v[2], -1) if with_bones else v)
                continue
            bm = struct.unpack_from("<%dH" % boneCount, self.buf, offBoneMap)
            pal = []
            for b in range(boneCount):
                ibm = struct.unpack_from("<16f", self.buf, offIBM + b * 64)
                pal.append(_m4_mul(W[bm[b]], list(ibm)))
            pos = self.buf[offPos: offPos + verts * 12]
            jnt = self.buf[offJoints: offJoints + verts * 4]
            wgt = self.buf[offWeights: offWeights + verts * 4]
            for v in range(verts):
                x, y, z = struct.unpack_from("<3f", pos, v * 12)
                ox = oy = oz = 0.0
                dom, domw = -1, 0
                for k in range(4):
                    w = wgt[v * 4 + k] / 255.0
                    if w < 1e-3: continue
                    if w > domw: domw, dom = w, bm[jnt[v * 4 + k]]
                    m = pal[jnt[v * 4 + k]]
                    ox += w * (m[0] * x + m[4] * y + m[8] * z + m[12])
                    oy += w * (m[1] * x + m[5] * y + m[9] * z + m[13])
                    oz += w * (m[2] * x + m[6] * y + m[10] * z + m[14])
                out.append((ox, oy, oz, dom) if with_bones else (ox, oy, oz))
        return out

def _m4_trs(t, q, s):
    x, y, z, w = q
    m = [0.0] * 16
    m[0] = (1 - 2 * (y * y + z * z)) * s[0]; m[1] = (2 * (x * y + z * w)) * s[0]; m[2] = (2 * (x * z - y * w)) * s[0]
    m[4] = (2 * (x * y - z * w)) * s[1]; m[5] = (1 - 2 * (x * x + z * z)) * s[1]; m[6] = (2 * (y * z + x * w)) * s[1]
    m[8] = (2 * (x * z + y * w)) * s[2]; m[9] = (2 * (y * z - x * w)) * s[2]; m[10] = (1 - 2 * (x * x + y * y)) * s[2]
    m[12], m[13], m[14], m[15] = t[0], t[1], t[2], 1.0
    return m

def _m4_mul(a, b):
    o = [0.0] * 16
    for c in range(4):
        for r in range(4):
            o[c * 4 + r] = sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4))
    return o

def append_clips(path, clips, force=False):
    """Append clips = [(name, fps, frames, {(node,type): [floats]}), ...] to the pack at
    path, in place. Refuses names that trip the engine's miners unless force."""
    for (name, _f, _n, _t) in clips:
        hits = lint_name(name)
        if hits and not force:
            raise ValueError("clip '%s' would trip the miners: %s" % (name, "; ".join(hits)))
    p = Rpack(path)
    out = bytearray(p.buf)

    def place(b):
        off = len(out); out.extend(b); return off

    # 1. track data + per-clip track tables, appended
    newRecs = []
    strs = p.strings()
    for (name, fps, frames, tracks) in clips:
        ents = []
        for (node, typ), data in sorted(tracks.items()):
            comp = 4 if typ == 0 else (1 if typ == 3 else 3)
            if isinstance(data, (bytes, bytearray)):
                if len(data) != frames * comp * 4:
                    raise ValueError("clip '%s' node %d type %d: %dB != %d frames x %d floats"
                                     % (name, node, typ, len(data), frames, comp))
                off = place(bytes(data))
            else:
                if len(data) != frames * comp:
                    raise ValueError("clip '%s' node %d type %d: %d floats != %d x %d"
                                     % (name, node, typ, len(data), frames, comp))
                off = place(struct.pack("<%df" % len(data), *data))
            ents.append((node, typ, off))
        offTracks = place(b"".join(struct.pack("<III", n, t, o) for (n, t, o) in ents))
        nameId = len(strs); strs.append(name.encode())
        newRecs.append(CLIP.pack(nameId, float(fps), frames, len(ents), offTracks))

    # 2. clip table: old records verbatim (their nameIds and offsets stay valid) + new
    oldClips = p.buf[p.offClips: p.offClips + p.nClips * 20]
    newOffClips = place(bytes(oldClips) + b"".join(newRecs))

    # 3. string table, rebuilt at the end: same entries in the same order + the new
    #    names appended, so every existing index still means what it meant
    tbl = struct.pack("<I", len(strs)); so = 0
    for s in strs:
        tbl += struct.pack("<II", so, len(s)); so += len(s)
    tbl += b"".join(strs)
    newOffStr = place(tbl)

    # 4. repoint four header fields; everything else keeps its bytes
    h = list(HDR.unpack_from(out, 4))
    h[4] = p.nClips + len(clips)      # nClips
    h[9] = newOffClips                # offClips
    h[11] = newOffStr                 # offStr
    h[13] = len(out)                  # total
    HDR.pack_into(out, 4, *h)
    open(path, "wb").write(out)
    return len(out)

def drop_clip(path, name):
    """Remove a clip by NAME so a score can be re-baked. Append-and-repoint like
    everything else here: a new clip table (minus that record) goes at the end and the
    header points at it. The dropped clip's track bytes stay in the file as harmless
    orphans -- nothing else moves, so no existing offset can be invalidated by this.
    Without it, re-authoring a move meant restoring the whole pack from a copy."""
    p = Rpack(path)
    keep = [i for i in range(p.nClips) if p.clip(i)["name"] != name]
    if len(keep) == p.nClips:
        return False
    out = bytearray(p.buf)
    while len(out) % 4: out.append(0)
    off = len(out)
    for i in keep:
        out.extend(p.buf[p.offClips + i * 20: p.offClips + (i + 1) * 20])
    h = list(HDR.unpack_from(out, 4))
    h[4] = len(keep); h[9] = off; h[13] = len(out)
    HDR.pack_into(out, 4, *h)
    open(path, "wb").write(out)
    return True

def main(argv):
    if len(argv) < 3:
        print(__doc__); return 2
    cmd = argv[1]
    if cmd == "info":
        p = Rpack(argv[2])
        print("%s: v%d  nodes %d  meshes %d  clips %d  tex %d  %dB"
              % (argv[2], p.ver, p.nNodes, p.nMeshes, p.nClips, p.nTex, len(p.buf)))
        for i in range(p.nClips):
            c = p.clip(i)
            print("  [%2d] %-28s %4df @ %gfps  %d tracks" % (i, c["name"], c["frames"], c["fps"], c["nTracks"]))
        return 0
    if cmd == "lint":
        bad = 0
        for name in argv[2:]:
            hits = lint_name(name)
            print("%-24s %s" % (name, "OK" if not hits else "TRIPS: " + "; ".join(hits)))
            bad += bool(hits)
        return 1 if bad else 0
    if cmd == "dropclips":
        # remove clips whose names start with the prefix: rebuild the clip table
        # without them at the end (their data becomes dead bytes -- append-only safety)
        path, prefix = argv[2], argv[3]
        p2 = Rpack(path)
        keep = [i for i in range(p2.nClips) if not p2.clip(i)["name"].startswith(prefix)]
        if len(keep) == p2.nClips:
            print("%s: nothing to drop" % path); return 0
        out = bytearray(p2.buf)
        recs = b"".join(p2.buf[p2.offClips + i * 20: p2.offClips + i * 20 + 20] for i in keep)
        newOff = len(out); out.extend(recs)
        h = list(HDR.unpack_from(out, 4))
        h[4] = len(keep); h[9] = newOff; h[13] = len(out)
        HDR.pack_into(out, 4, *h)
        open(path, "wb").write(out)
        print("%s: dropped %d clip(s), %d remain" % (path, p2.nClips - len(keep), len(keep)))
        return 0
    if cmd == "copyclip":
        src, clipName, dst, newName = argv[2:6]
        s = Rpack(src)
        names = s.clip_names()
        if clipName not in names:
            print("no clip '%s' in %s (have: %s)" % (clipName, src, ", ".join(names))); return 1
        ci = names.index(clipName)
        c = s.clip(ci)
        tracks = {(n, t): d for (n, t, d) in s.tracks(ci)}
        append_clips(dst, [(newName, c["fps"], c["frames"], tracks)])
        d = Rpack(dst)
        print("%s: '%s' (%df, %d tracks) appended as '%s' -> %d clips, %dB"
              % (dst, clipName, c["frames"], len(tracks), newName, d.nClips, len(d.buf)))
        return 0
    print("unknown command %r" % cmd); return 2

if __name__ == "__main__":
    sys.exit(main(sys.argv))
