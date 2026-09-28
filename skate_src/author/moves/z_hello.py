#!/usr/bin/env python3
"""z_hello.py -- P0.5, the hello pose: two arms, two DIFFERENT authored angles.

The first motion in the club that was DECIDED rather than captured, and deliberately the
smallest discriminating authoring act. The first cut aimed one arm at 90 deg -- and the
gate read 88.95 on BOTH arms, because manny's untracked rest is a T-POSE: rest was
already at ~89, so hitting 90 proved coincidence, not control. Two corrections, both of
which the plan needed anyway:

  1. Scores compose OVER A BASE POSE (a_idle frame 0), not over bind rest -- a partial
     clip now rides a natural stand instead of a T-pose underlay.
  2. The two arms get two different targets, neither anywhere near rest:
     ARM R -> 35 deg, ARM L -> 120 deg. Only deliberate control produces that split.

Gate: posturecheck ARM R 35 +/- 2, ARM L 120 +/- 2, body otherwise a_idle-like.

usage: z_hello.py <moves-pack>
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips
from authorlib import aim_bone, fk_world

TARGET_R, TARGET_L = 35.0, 120.0

def main(pack_path):
    p = Rpack(pack_path)
    if "z_hello" in p.clip_names():
        print("z_hello already present"); return 0
    nodes = p.nodes()
    names = p.node_names()
    idle = p.clip_names().index("a_idle")

    # ---- the base pose: a_idle frame 0, every track ----
    base = {}          # (node,type) -> frame-0 values
    base_q = {}        # node -> q, for FK
    for (node, typ, data) in p.tracks(idle):
        comp = 4 if typ == 0 else 3
        v = list(struct.unpack_from("<%df" % comp, data, 0))
        base[(node, typ)] = v
        if typ == 0: base_q[node] = v

    def aim(side_name, child_name, deg):
        ua, fo = names.index(side_name), names.index(child_name)
        pelvis = names.index("pelvis")
        wp, _ = fk_world(nodes, base_q)
        side = 1.0 if (wp[ua][0] - wp[pelvis][0]) >= 0 else -1.0
        a = math.radians(deg)
        target = [math.sin(a) * side, -math.cos(a), 0.0]     # in the lateral plane
        q = aim_bone(nodes, ua, fo, target, dict(base_q))
        # author-time verification -- the score is the test
        chk = dict(base_q); chk[ua] = q
        wp2, _ = fk_world(nodes, chk)
        seg = [wp2[fo][k] - wp2[ua][k] for k in range(3)]
        n = math.sqrt(sum(x * x for x in seg))
        got = math.degrees(math.acos(max(-1.0, min(1.0, -seg[1] / n))))
        print("z_hello: %s authored at %.2f deg (want %g)" % (side_name, got, deg))
        assert abs(got - deg) < 0.01, "authoring math off before it reached the engine"
        return ua, q

    uaR, qR = aim("upperarm_r", "lowerarm_r", TARGET_R)
    uaL, qL = aim("upperarm_l", "lowerarm_l", TARGET_L)

    frames = 30
    tracks = {}
    for (node, typ), v in base.items():                      # the stand, held
        tracks[(node, typ)] = v * frames
    tracks[(uaR, 0)] = list(qR) * frames                     # the two authored arms
    tracks[(uaL, 0)] = list(qL) * frames
    append_clips(pack_path, [("z_hello", 30.0, frames, tracks)])
    print("z_hello: appended (%d frames, %d tracks, base=a_idle f0)" % (frames, len(tracks)))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
