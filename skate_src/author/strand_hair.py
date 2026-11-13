#!/usr/bin/env python3
"""strand_hair.py -- a pack's hair becomes STRAND HAIR: the engine moves every lock itself (engine/strands.inc).

    strand_hair.py PACK [--points 10] [--report]

(11-06) REVA's hair on the engine's 22 hair chains moved in sections, a few strands at a time, where the physics should move the whole
head of hair, strand by strand. Her hair is built lock by lock (every lock a ribbon of 2 x N vertices in a row, the two edges of each of its
N points, root first). Here, in place and nothing else touched (order, materials,
textures, clips, morphs): every vertex of each hair mesh rides her HEAD alone -- that's where its lock is pinned and what its shape is
kept against -- and a `HairStrandsN` node (N points a lock) tells the engine to simulate them. ⛔ A mesh whose vertex count isn't whole
locks of N points, or whose bones don't include the head, is left as it is and said so. --report says what it would do."""
import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from packbody import PackBody  # noqa: E402


def main(argv):
    path = argv[1]
    npts = int(argv[argv.index("--points") + 1]) if "--points" in argv else 10
    B = PackBody(path)
    hn = B.names.index("head")
    done = 0
    for m in B.meshes:
        nm = m["name"]
        if "hair" not in nm.lower():
            continue
        s = B.skin(m)
        if s is None:
            print("  %-48s skipped: no bones" % nm); continue
        bm = list(s[3])
        if hn not in bm:
            print("  %-48s skipped: the head isn't one of its bones" % nm); continue
        nv = m["rec"][2]
        if nv % (2 * npts):
            print("  %-48s skipped: %d vertices aren't whole locks of %d points" % (nm, nv, npts)); continue
        on_head = float(B.weight_on(m, lambda b: b == "head").mean())
        print("  %-48s %d locks of %d points; %.0f%% of its weight on the head now" % (nm, nv // (2 * npts), npts, 100 * on_head))
        J = np.zeros((nv, 4), np.uint8); J[:, 0] = bm.index(hn)
        W = np.zeros((nv, 4), np.uint8); W[:, 0] = 255
        B.patches.append((m["rec"][9], J.tobytes())); B.patches.append((m["rec"][10], W.tobytes()))
        done += 1
    if "--report" in argv or not done:
        print("nothing written" if not done else "--report: nothing written"); return 0 if done else 1
    B.add_node("HairStrands%d" % npts, hn, 0)
    B.write()
    print("%s: %d hair meshes on the head alone, and a HairStrands%d node" % (path, done, npts))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
