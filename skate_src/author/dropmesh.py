"""dropmesh.py -- take meshes OFF a body by name, in place.

jacob, 2026-09-03: "take animas bra and panties off, i want to get a good baseline here."

    python skate_src/author/dropmesh.py anima_fx anima_bra anima_panties
    python skate_src/author/dropmesh.py anima_fx --list
    python skate_src/author/dropmesh.py anima_fx --restore

⭐ WHY THIS IS A TOOL AND NOT A ONE-OFF. Every garment this project fits wants a clean base
body underneath it -- `autoweights.py`, `fitcheck.py` and the cloth sim all assume the thing
they are fitting TO is the body, not the body plus whatever underwear the donor model
happened to ship wearing. Baked-in underwear is why a fitted garment shows a seam through
itself. So: strip by name, keep a backup, put it back with --restore.

⛔ IT IS A MESH-TABLE EDIT, NOT A GEOMETRY EDIT. Same append-and-repoint idiom as
`drop_clip`: a new mesh table (minus the named records) goes at the END of the file and the
header points at it. The dropped meshes' vertex data stays in the file as harmless orphans,
nothing else moves, and no existing offset can be invalidated by this. That is also why
--restore is exact rather than approximate -- the .bak is a byte copy.

⛔ MESH NAMES ARE NOT UNIQUE. anima_fx carries `anima_body` FOURTEEN times (one record per
material chunk), so a name selects every record that carries it. That is what you want --
dropping "anima_bra" must drop all of its chunks or you get half a bra.
"""
import argparse
import os
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rpack import Rpack, HDR   # noqa: E402


def mesh_names(R):
    """The node name each mesh record hangs off, in record order."""
    nn = R.node_names()
    out = []
    for mi in range(R.nMeshes):
        rec = R.MESH.unpack_from(R.buf, R.offMeshes + mi * 60)
        node = rec[0]
        out.append(nn[node] if node < len(nn) else "?node%d" % node)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("subject", help="packs/v1_<subject>.rpack, edited IN PLACE")
    ap.add_argument("meshes", nargs="*", help="node names to remove")
    ap.add_argument("--list", action="store_true", help="just print what is on this body")
    ap.add_argument("--restore", action="store_true", help="put the .bak back")
    a = ap.parse_args(argv)

    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    path = os.path.join(root, "reva_native", "packs", "v1_%s.rpack" % a.subject)
    bak = path + ".predrop"

    if a.restore:
        if not os.path.exists(bak):
            print("no backup at %s" % bak); return 1
        shutil.copyfile(bak, path)
        print("%s: restored from %s" % (a.subject, os.path.basename(bak)))
        return 0

    R = Rpack(path)
    names = mesh_names(R)
    if a.list or not a.meshes:
        seen = {}
        for i, n in enumerate(names):
            seen.setdefault(n, []).append(i)
        print("%s: %d mesh records over %d distinct names" % (a.subject, len(names), len(seen)))
        for n in sorted(seen):
            print("   %-24s x%-3d  records %s" % (n, len(seen[n]), seen[n]))
        return 0

    want = set(a.meshes)
    missing = want - set(names)
    if missing:
        print("!! not on this body: %s" % ", ".join(sorted(missing)))
        print("   (--list shows what is)")
        return 1
    keep = [i for i, n in enumerate(names) if n not in want]
    dropped = [i for i, n in enumerate(names) if n in want]

    if not os.path.exists(bak):
        shutil.copyfile(path, bak)

    out = bytearray(R.buf)
    while len(out) % 4:
        out.append(0)
    off = len(out)
    for i in keep:
        out.extend(R.buf[R.offMeshes + i * 60: R.offMeshes + (i + 1) * 60])
    h = list(HDR.unpack_from(out, 4))
    h[2] = len(keep)      # nMeshes
    h[7] = off            # offMeshes
    h[13] = len(out)      # total
    HDR.pack_into(out, 4, *h)
    open(path, "wb").write(bytes(out))

    R2 = Rpack(path)
    print("%s: dropped %d record(s) -- %s" % (a.subject, len(dropped), ", ".join(sorted(want))))
    print("   meshes %d -> %d   backup: %s" % (len(names), R2.nMeshes, os.path.basename(bak)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
