"""unbake.py -- SPLIT A MERGED MESH BACK INTO ITS PARTS, so a character can take her shoes off.

jacob: "she needs to be able to take her shoes off, does this pack allow for that?" ... "oh
wait if im reading what you are saying she is already like that we just need to un bake her?"

⭐ YES, AND THAT IS THE WHOLE POINT OF THIS FILE. A bake that "merges" objects CONCATENATES
their vertex buffers -- it does not STITCH their triangles. So every original shell is still
sitting there intact inside the merged mesh, and connected components find them again. Hina's
`merged_1` is 7061 vertices in **133 separate shells**, and they read straight off as anatomy:

    518 / 518 verts  Y 0.12..0.83   X +0.01..+0.15 / -0.15..-0.01   her two legs
    425 / 425 verts                 X out to +-0.23                 her two arms
    162 / 162 verts  Y 0.02..0.13   X +0.04..+0.12 / -0.18..-0.10   HER TWO SHOES

Two mirrored 162-vertex shells at ankle height. No re-download and no re-bake needed.

⛔ THE RPACK ITSELF REMEMBERS NOTHING. The `Object_6..Object_19` nodes still in the node list
are only leftover scene nodes -- they carry no mesh ranges, so there is no record to read back.
The split has to be RECOVERED from the geometry, which is what this does.

⭐ AND A SEPARATE MESH IS WORTH FAR MORE THAN TIDINESS: `REVA_HIDEMESH` drops a mesh by name at
load, the cloth solver claims a mesh by the `~cloth` marker in its name, and `meshTintOf` reads
a colour off it. Splitting the shoes out makes all three apply to the shoes alone.

    # what is in there
    python skate_src/author/unbake.py hina_scene_gltf --list

    # carve named parts out by height, everything else stays in the body mesh
    python skate_src/author/unbake.py hina_scene_gltf hina_split --part shoes:0.0:0.16
"""
import argparse
import os
import struct
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rpack import Rpack                                          # noqa: E402

MESHF = "<15I"


def shells(R, mi):
    """The connected components of one mesh, as lists of vertex indices."""
    rec = R.MESH.unpack_from(R.buf, R.offMeshes + mi * 60)
    nv, nidx, offIdx = rec[2], rec[3], rec[11]
    idx = struct.unpack_from("<%dI" % nidx, R.buf, offIdx)
    par = list(range(nv))

    def find(x):
        while par[x] != x:
            par[x] = par[par[x]]
            x = par[x]
        return x

    for t in range(0, nidx, 3):
        a, b, c = idx[t], idx[t + 1], idx[t + 2]
        for u, v in ((a, b), (b, c), (c, a)):
            ru, rv = find(u), find(v)
            if ru != rv:
                par[ru] = rv
    g = defaultdict(list)
    for v in range(nv):
        g[find(v)].append(v)
    return rec, idx, list(g.values())


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src")
    ap.add_argument("dst", nargs="?")
    ap.add_argument("--mesh", type=int, default=None,
                    help="which mesh to split (default: every mesh)")
    ap.add_argument("--list", action="store_true", help="just report the shells")
    ap.add_argument("--part", action="append", default=[],
                    help="name:ylo:yhi -- shells whose CENTRE falls in the band become a "
                         "mesh of their own. Repeatable.")
    ap.add_argument("--min-verts", type=int, default=24,
                    help="ignore shells smaller than this when listing")
    a = ap.parse_args(argv)

    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    src = os.path.join(root, "reva_native", "packs", "v1_%s.rpack" % a.src)
    R = Rpack(src)
    pts = R.skinned_positions()

    offs, at = [], 0
    for mi in range(R.nMeshes):
        rec = R.MESH.unpack_from(R.buf, R.offMeshes + mi * 60)
        offs.append(at)
        at += rec[2]

    if a.list or not a.dst:
        for mi in range(R.nMeshes):
            if a.mesh is not None and mi != a.mesh:
                continue
            rec, idx, gs = shells(R, mi)
            P = pts[offs[mi]:offs[mi] + rec[2]]
            print("mesh%d '%s': %d verts -> %d shells"
                  % (mi, R.sname(rec[0]), rec[2], len(gs)))
            for vs in sorted(gs, key=len, reverse=True):
                if len(vs) < a.min_verts:
                    continue
                ys = [P[v][1] for v in vs]
                xs = [P[v][0] for v in vs]
                print("     %6d verts   Y %.3f..%.3f   X %+.3f..%+.3f"
                      % (len(vs), min(ys), max(ys), min(xs), max(xs)))
        return 0

    parts = []
    for spec in a.part:
        try:
            nm, lo, hi = spec.split(":")
            parts.append((nm, float(lo), float(hi)))
        except ValueError:
            print("!! --part wants name:ylo:yhi, got %r" % spec)
            return 1
    if not parts:
        print("!! nothing to do -- give at least one --part name:ylo:yhi")
        return 1

    # ---- build the new mesh list ------------------------------------------------------
    out = bytearray(R.buf)
    tail = bytearray()
    base = len(out)

    def put(b):
        o = base + len(tail)
        tail.extend(b)
        while len(tail) % 4:
            tail.append(0)
        return o

    newmeshes = []
    strs = R.strings()
    made = 0
    for mi in range(R.nMeshes):
        rec, idx, gs = shells(R, mi)
        P = pts[offs[mi]:offs[mi] + rec[2]]
        if a.mesh is not None and mi != a.mesh:
            newmeshes.append(rec)
            continue
        # which part does each shell belong to? (-1 = stays with the body)
        owner = {}
        for si, vs in enumerate(gs):
            yc = sum(P[v][1] for v in vs) / len(vs)
            owner[si] = -1
            for pi, (nm, lo, hi) in enumerate(parts):
                if lo <= yc <= hi:
                    owner[si] = pi
                    break
        of_vert = {}
        for si, vs in enumerate(gs):
            for v in vs:
                of_vert[v] = owner[si]

        # ⛔ A TRIANGLE BELONGS WHOLLY TO ONE PART OR IT IS NOT A SHELL. Shells are
        # connected components, so every triangle's three vertices share an owner by
        # construction -- but assert it rather than trust it.
        groups = defaultdict(list)
        for t in range(0, rec[3], 3):
            o = of_vert[idx[t]]
            if of_vert[idx[t + 1]] != o or of_vert[idx[t + 2]] != o:
                print("!! a triangle spans two parts -- shells are not what they claim")
                return 1
            groups[o].append((idx[t], idx[t + 1], idx[t + 2]))

        for o, tris in sorted(groups.items()):
            used = sorted({v for tr in tris for v in tr})
            remap = {v: i for i, v in enumerate(used)}
            nv2 = len(used)

            def slice_of(off, stride, fmt=None):
                b = bytearray()
                for v in used:
                    b.extend(R.buf[off + v * stride: off + (v + 1) * stride])
                return bytes(b)

            oPos = put(slice_of(rec[6], 12))
            oNrm = put(slice_of(rec[7], 12)) if rec[7] else 0
            oUV = put(slice_of(rec[8], 8)) if rec[8] else 0
            oJ = put(slice_of(rec[9], 4)) if rec[9] else 0
            oW = put(slice_of(rec[10], 4)) if rec[10] else 0
            oI = put(b"".join(struct.pack("<3I", remap[x], remap[y], remap[z])
                              for x, y, z in tris))
            nm = R.sname(rec[0]) if o < 0 else parts[o][0]
            if o >= 0:
                strs = strs + [nm.encode("ascii")]
                nameId = len(strs) - 1
                made += 1
            else:
                nameId = rec[0]
            newmeshes.append((nameId, rec[1], nv2, len(tris) * 3, rec[4], rec[5],
                              oPos, oNrm, oUV, oJ, oW, oI, rec[12], rec[13], rec[14]))
            print("  %-14s %6d verts %6d tris" % (nm, nv2, len(tris)))

    if not made:
        print("!! no shell fell inside any --part band; nothing split")
        return 1

    # ⛔ THE STRING TABLE IS (count, count x (off,len), blob) -- NOT a run of length-
    # prefixed strings, which is what the first draft wrote, and the engine simply refused
    # the pack. Copied from makepajamas.py's writer, which works.
    ents, blob = [], b""
    for t in strs:
        ents.append((len(blob), len(t)))
        blob += t
    offStr = put(struct.pack("<I", len(strs))
                 + b"".join(struct.pack("<II", o, l) for (o, l) in ents) + blob)
    offMeshes = put(b"".join(struct.pack(MESHF, *m) for m in newmeshes))
    out.extend(tail)

    # ⛔ AND THE HEADER STARTS AT OFFSET 4, AFTER 'RPK1' -- 15 u32s, not 16 from zero.
    # Reading it from 0 took the magic as the first field, shifted every index by one, and
    # then wrote the magic back as a number. Rebuild it the way the writer that works does.
    from rpack import HDR
    total = len(out)
    hdr = b"RPK1" + HDR.pack(R.ver, R.nNodes, len(newmeshes), R.nMats, R.nClips, R.nTex,
                             R.offNodes, offMeshes, R.offMats, R.offClips,
                             R.offTexTable, offStr, R.offSize, total, 0)
    out[0:4 + HDR.size] = hdr

    dst = os.path.join(root, "reva_native", "packs", "v1_%s.rpack" % a.dst)
    open(dst, "wb").write(bytes(out))
    print("wrote %s  (%d meshes)" % (dst, len(newmeshes)))
    import packcheck
    bad = [m for sev, m in packcheck.check(dst) if sev == "FAIL"]
    for m in bad:
        print("   !! %s" % m)
    print("packcheck: %s" % ("FAILED" if bad else "clean"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
