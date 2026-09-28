#!/usr/bin/env python3
"""z_breathe.py -- H6's gate fixture: the first clip that drives a MORPH.

The idle stand held as rotations, plus one type-3 track cycling the 'breath' morph
0 -> 1 -> 0 every 3.5 seconds: her chest actually swells, surface motion no bone can
make. The purr's real form lives this way too.

usage: z_breathe.py <pack>    (the pack must already carry the 'breath' morph)
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips, HDR
from authorlib import rig_map

def main(pack_path):
    p = Rpack(pack_path)
    if "z_breathe" in p.clip_names():
        print("z_breathe already present"); return 0
    offM = HDR.unpack_from(p.buf, 4)[14]
    assert offM, "no morph block -- run morphs.py breathe first"
    nM = struct.unpack_from("<I", p.buf, offM)[0]
    names = [p.sname(struct.unpack_from("<4I", p.buf, offM + 4 + i * 16)[1]) for i in range(nM)]
    mi = names.index("breath")
    RM = rig_map(p)
    ci = p.clip_names().index("z_upright" if "z_upright" in p.clip_names() else RM["_base_clip"])
    base = {}
    for (node, typ, data) in p.tracks(ci):
        comp = 4 if typ == 0 else (1 if typ == 3 else 3)
        base[(node, typ)] = list(struct.unpack_from("<%df" % comp, data, 0))
    FPS, DUR = 30.0, 7.0
    frames = int(FPS * DUR)
    tracks = {}
    for (node, typ), v in base.items():
        tracks[(node, typ)] = v * frames
    w = []
    for f in range(frames):
        t = f / FPS
        w.append(0.5 - 0.5 * math.cos(2 * math.pi * t / 3.5))
    tracks[(mi, 3)] = w
    append_clips(pack_path, [("z_breathe", FPS, frames, tracks)])
    print("z_breathe: appended (%d frames, morph #%d driven 0..1 at 3.5s period)" % (frames, mi))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
