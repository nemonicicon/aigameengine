#!/usr/bin/env python3
"""z_twisttest.py -- H2's gate fixture: the candy-wrapper stressor.

Holds a pose built to make linear blend skinning fail where everyone can see it: the
right elbow bent 90 degrees and the forearm TWISTED 110 degrees about its own axis --
the classic wrist-twist that pinches an LBS forearm into a paper straw. Dual-quaternion
skinning holds the volume. Shot on the close-up wall under REVA_LBS=1 and without, the
silhouette difference IS the gate.

usage: z_twisttest.py <pack>
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips
from authorlib import rig_map, fk_world, rotate_bone_world, q_axis_angle, qmul, qnorm

def main(pack_path):
    p = Rpack(pack_path)
    if "z_twisttest" in p.clip_names():
        print("z_twisttest already present"); return 0
    nodes = p.nodes()
    RM = rig_map(p)
    names_c = p.clip_names()
    ci = names_c.index("z_upright" if "z_upright" in names_c else RM["_base_clip"])
    base_q, base_t, base = {}, {}, {}
    for (node, typ, data) in p.tracks(ci):
        comp = 4 if typ == 0 else 3
        v = list(struct.unpack_from("<%df" % comp, data, 0))
        base[(node, typ)] = v
        if typ == 0: base_q[node] = v
        elif typ == 1: base_t[node] = v

    ua, la, ha = RM["upperarm_r"], RM["lowerarm_r"], RM["hand_r"]
    qo = dict(base_q)
    # raise the arm forward and bend the elbow 90
    qo[ua] = rotate_bone_world(nodes, ua, [1, 0, 0], math.radians(-70.0), qo)
    qo[la] = rotate_bone_world(nodes, la, [1, 0, 0], math.radians(-90.0), qo)
    # the TWIST: rotate the forearm about its own bone axis
    wp, wq = fk_world(nodes, qo, base_t)
    axis = [wp[ha][k] - wp[la][k] for k in range(3)]
    qo[la] = rotate_bone_world(nodes, la, axis, math.radians(110.0), qo)
    print("z_twisttest: elbow bent 90, forearm twisted 110 about its own axis")

    frames = 30
    tracks = {}
    for (node, typ), v in base.items():
        tracks[(node, typ)] = v * frames
    for b in (ua, la):
        tracks[(b, 0)] = [float(x) for x in qnorm(qo[b])] * frames
    append_clips(pack_path, [("z_twisttest", 30.0, frames, tracks)])
    print("z_twisttest: appended")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
