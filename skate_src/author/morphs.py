#!/usr/bin/env python3
"""morphs.py -- rpack v3: MORPH TARGETS, in our own format (homecoming H6).

The format is ours, so faces and breathing come home. The header's reserved slot
(u32 #14) becomes offMorphs -- zero on every existing pack, so old files load
unchanged and old loaders ignore the block entirely. Layout, appended like everything
else (nodes/meshes/clips stay byte-identical):

    u32 nMorphs
    per morph: u32 meshIdx, u32 nameId, u32 nSparse, u32 offData
    data: nSparse x (u32 vertexIdx, 3f delta)      (positions, rig/mesh space)

Clip tracks gain type 3: node field = MORPH INDEX, one float per frame = the weight.

usage:
  morphs.py breathe <pack>     inject a measured chest-swell morph named "breath"
                               (chest-band vertices pushed along their own normals)
  morphs.py info <pack>        list the pack's morphs
"""
import sys, struct
sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0] if "\\" in __file__ or "/" in __file__ else ".")
from rpack import Rpack, HDR

def add_morph(path, mesh_idx, name, sparse):
    """sparse = [(vidx, dx, dy, dz), ...]; appends the morph, repoints header slot 14."""
    p = Rpack(path)
    out = bytearray(p.buf)
    # existing morphs survive: copy the old table if present
    old = []
    offM = HDR.unpack_from(p.buf, 4)[14]
    if offM:
        n = struct.unpack_from("<I", p.buf, offM)[0]
        for i in range(n):
            old.append(struct.unpack_from("<4I", p.buf, offM + 4 + i * 16))
    # new morph's data
    offData = len(out)
    for (v, dx, dy, dz) in sparse:
        out.extend(struct.pack("<I3f", v, dx, dy, dz))
    # name into the string table (rebuilt at the end, same order + one)
    strs = p.strings()
    nameId = len(strs); strs.append(name.encode())
    tbl = struct.pack("<I", len(strs)); so = 0
    for s2 in strs:
        tbl += struct.pack("<II", so, len(s2)); so += len(s2)
    tbl += b"".join(strs)
    newOffStr = len(out); out.extend(tbl)
    # the morph table
    newOffM = len(out)
    ents = old + [(mesh_idx, nameId, len(sparse), offData)]
    out.extend(struct.pack("<I", len(ents)))
    for e in ents:
        out.extend(struct.pack("<4I", *e))
    h = list(HDR.unpack_from(out, 4))
    h[11] = newOffStr
    h[13] = len(out)
    h[14] = newOffM
    HDR.pack_into(out, 4, *h)
    open(path, "wb").write(out)
    print("%s: morph '%s' (%d sparse deltas) -> %d morphs, %dB"
          % (path, name, len(sparse), len(ents), len(out)))
    return len(ents) - 1

def breathe(path):
    """A measured chest-swell: verts in the ribs band, near the front, pushed along
    their own normals with a smooth falloff. The purr and the breath become SURFACE
    motion instead of bone rotation."""
    import math
    from authorlib import rig_map, fk_world
    p = Rpack(path)
    RM = rig_map(p)
    nodes = p.nodes()
    wp, _ = fk_world(nodes)
    hipY, shY = wp[RM["pelvis"]][1], (wp[RM["upperarm_l"]][1] + wp[RM["upperarm_r"]][1]) / 2
    span = shY - hipY
    lo, hi = hipY + 0.45 * span, hipY + 0.95 * span
    rec = p.MESH.unpack_from(p.buf, p.offMeshes)
    verts, offPos, offNorm = rec[2], rec[6], rec[7]
    amp = 0.022 * span
    sparse = []
    for v in range(verts):
        x, y, z = struct.unpack_from("<3f", p.buf, offPos + v * 12)
        if not (lo <= y <= hi): continue
        if z < 0: continue                       # the front half breathes
        nx, ny, nz = struct.unpack_from("<3f", p.buf, offNorm + v * 12)
        mid = (lo + hi) / 2
        fall = max(0.0, 1.0 - abs(y - mid) / (span * 0.30)) * max(0.2, min(1.0, z / (span * 0.2)))
        if fall < 0.05: continue
        sparse.append((v, nx * amp * fall, ny * amp * fall, nz * amp * fall))
    print("breathe: %d of %d verts in the chest band, amp %.4f rig-u" % (len(sparse), verts, amp))
    return add_morph(path, 0, "breath", sparse)

def drop_morphs(path, names):
    """Remove morphs by name so a face can be re-authored. Append-and-repoint like
    add_morph: a new morph table (minus those entries) goes at the end and header slot
    14 points at it; the dropped deltas stay as harmless orphans. Iterating a face
    without this meant rebuilding the whole pack."""
    p = Rpack(path)
    offM = HDR.unpack_from(p.buf, 4)[14]
    if not offM: return 0
    n = struct.unpack_from("<I", p.buf, offM)[0]
    recs = [struct.unpack_from("<4I", p.buf, offM + 4 + i * 16) for i in range(n)]
    keep = [r for r in recs if p.sname(r[1]) not in names]
    if len(keep) == n: return 0
    out = bytearray(p.buf)
    while len(out) % 4: out.append(0)
    off = len(out)
    out.extend(struct.pack("<I", len(keep)))
    for r in keep: out.extend(struct.pack("<4I", *r))
    h = list(HDR.unpack_from(out, 4)); h[14] = off; h[13] = len(out)
    HDR.pack_into(out, 4, *h)
    open(path, "wb").write(out)
    return n - len(keep)

def main():
    cmd, path = sys.argv[1], sys.argv[2]
    if cmd == "breathe":
        breathe(path)
    elif cmd == "info":
        p = Rpack(path)
        offM = HDR.unpack_from(p.buf, 4)[14]
        if not offM:
            print("no morphs"); return
        n = struct.unpack_from("<I", p.buf, offM)[0]
        for i in range(n):
            m, nid, ns, od = struct.unpack_from("<4I", p.buf, offM + 4 + i * 16)
            print("  [%d] mesh %d '%s' %d deltas" % (i, m, p.sname(nid), ns))

if __name__ == "__main__":
    main()
