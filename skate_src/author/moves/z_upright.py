#!/usr/bin/env python3
"""z_upright.py -- author a STANDING base pose on any rig, from the rest skeleton.

Scores compose over a base pose, and until now that base was whatever clip the miner
elected as the body's "idle". On kaeli that election is a trap: her `idle` clip opens
CROUCHED -- frame 0 lifts her feet 52 rig-units toward her pelvis and drops her head by
16 -- so every move authored on top of it began from a kneel. Her five reached the right
point in the world and still looked wrong, because she was doing it from all fours.

Her REST pose, meanwhile, stands perfectly well. So this score builds the stand the
honest way: take the rest skeleton, hang the arms, and write it as `z_upright`. Any
score can then ask for a base that is known to be standing instead of trusting a name.

The name deliberately avoids every miner substring (`stand` and `idle` are both mined),
so appending it changes no role election on any of the 133 packs -- it is available to
scores that ask for it and invisible to everything else.

usage: z_upright.py <pack>
"""
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips
from authorlib import rig_map, fk_world, aim_bone, qnorm

HANG = 14.0        # upper arm, degrees out from vertical
ELBOW_OUT = 16.0   # forearm a touch wider ...
ELBOW_FWD = 8.0    # ... and slightly forward: relaxed, not locked
FRAMES, FPS = 8, 30.0

def main(pack_path):
    p = Rpack(pack_path)
    if "z_upright" in p.clip_names():
        print("z_upright already present"); return 0
    nodes = p.nodes()
    RM = rig_map(p)
    assert not RM["_missing"], "rig incomplete: %s" % RM["_missing"]

    q = {}
    for side, sx in (("l", +1.0), ("r", -1.0)):
        ua, la, ha = RM["upperarm_" + side], RM["lowerarm_" + side], RM["hand_" + side]
        if min(ua, la, ha) < 0: continue
        a = math.radians(HANG)
        q[ua] = aim_bone(nodes, ua, la, [math.sin(a) * sx, -math.cos(a), 0.0], q)
        b = math.radians(ELBOW_OUT)
        q[la] = aim_bone(nodes, la, ha,
                         [math.sin(b) * sx, -math.cos(b), math.sin(math.radians(ELBOW_FWD))], q)

    wp, _ = fk_world(nodes, q)
    for side in ("l", "r"):
        ua, la = RM["upperarm_" + side], RM["lowerarm_" + side]
        if min(ua, la) < 0: continue
        seg = [wp[la][k] - wp[ua][k] for k in range(3)]
        n = math.sqrt(sum(x * x for x in seg))
        got = math.degrees(math.acos(max(-1.0, min(1.0, -seg[1] / n))))
        print("z_upright: arm_%s hangs %.2f deg (want %g)" % (side, got, HANG))
        assert abs(got - HANG) < 0.5, "hang authoring off before it reached the engine"

    # it must actually STAND: head above hips, hips well above the feet
    hip = wp[RM["pelvis"]][1]; head = wp[RM["head"]][1]
    foot = min(wp[RM["foot_l"]][1], wp[RM["foot_r"]][1])
    print("z_upright: head %+.1f  hips %+.1f  feet %+.1f (rig units)" % (head, hip, foot))
    assert head > hip > foot, "this rest pose is not a stand"

    tracks = {}
    for i in range(len(nodes)):
        tracks[(i, 0)] = [float(x) for x in qnorm(q.get(i, nodes[i]["q"]))] * FRAMES
    append_clips(pack_path, [("z_upright", FPS, FRAMES, tracks)])
    print("z_upright: appended (%d frames, %d tracks, base=REST)" % (FRAMES, len(tracks)))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
