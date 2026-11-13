#!/usr/bin/env python3
"""packetsum.py -- (11-07) what one frame asked the GPU for, by kind: the draws and indices of a REVA_PACKETDUMP frame, the main pass and the sun's
apart, and the crowd's bodies by owner. It's how the Pi's frame was taken apart for the street game: its street's people were 1.17 M of 1.77 M indices.

usage: packetsum.py DUMP.txt          (REVA_PACKETDUMP=<tick>:<file> on any build; on the Pi, copy the file back first)"""
import collections
import sys

KIND = {1: "hero", 2: "crowd", 3: "prop", 4: "cast", 5: "actor", 6: "object", 7: "far"}


def main(path):
    by = collections.defaultdict(lambda: [0, 0])
    crowd = collections.defaultdict(lambda: [0, 0])
    for ln in open(path, encoding="utf-8", errors="replace"):
        p = ln.split()
        if len(p) < 17 or p[1] != "id":
            continue
        kind, owner, idx, flags = int(p[4]), int(p[6]), int(p[10]), int(p[16])
        where = "sun" if flags & 1 else "main"
        k = (KIND.get(kind, str(kind)), where)
        by[k][0] += 1
        by[k][1] += idx
        if kind == 2 and where == "main":
            crowd[owner][0] += 1
            crowd[owner][1] += idx
    for where in ("main", "sun"):
        rows = [(k[0], v) for k, v in by.items() if k[1] == where]
        tot = sum(v[1] for _, v in rows)
        print("%s pass: %d draws, %d indices" % (where, sum(v[0] for _, v in rows), tot))
        for name, (d, i) in sorted(rows, key=lambda r: -r[1][1]):
            print("  %-7s %5d draws %9d indices  %4.1f%%" % (name, d, i, 100.0 * i / max(tot, 1)))
    if crowd:
        v = sorted(crowd.values(), key=lambda r: -r[1])
        print("crowd bodies drawn: %d, %d to %d indices each" % (len(v), v[-1][1], v[0][1]))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
