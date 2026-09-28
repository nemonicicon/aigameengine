#!/usr/bin/env python3
"""idle_reva2.py -- REVA's stand: the first authored clip on her new body.

Her pack ships with only the rig recipe's 2-frame T-pose, so unlike every moves score
this one composes over REST and builds the stand itself: arms aimed down to a natural
hang (14 deg out, elbows softened toward a slight forward bend), then a 3-second breath
loop -- spine pitch with neck counter-phase and a lagged arm swing, the crawl style-pass
lesson (motion reads through counter-phase) applied to standing still.

The name "idle" TRIPS THE LINT ON PURPOSE: this is the one kind of clip that is meant
to be mined -- it becomes her idleIdx, so the club (and rig_map's _base_clip on every
later moves score) elects it as the base stand. force=True is the visible record of
that intent.

usage: idle_reva2.py [pack]        (default reva_native/packs/v1_reva2.rpack)
"""
import sys, os, math

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "author"))
from rpack import Rpack, append_clips
from authorlib import rig_map, fk_world, aim_bone, rotate_bone_world, declare

HANG = 14.0        # upperarm: degrees out from vertical, lateral plane
ELBOW_OUT = 16.0   # forearm: a touch more outward ...
ELBOW_FWD = 8.0    # ... and slightly forward = relaxed, not locked
FRAMES, FPS = 90, 30.0

def main(pack_path):
    p = Rpack(pack_path)
    if "idle" in p.clip_names():
        print("idle already present"); return 0
    nodes = p.nodes(); names = p.node_names()
    m = rig_map(p)

    # ---- the hang: aim each arm chain down from the T ----
    q = {}
    for side, sx in (("l", +1.0), ("r", -1.0)):
        ua, la, ha = m["upperarm_" + side], m["lowerarm_" + side], m["hand_" + side]
        a = math.radians(HANG)
        q[ua] = aim_bone(nodes, ua, la, [math.sin(a) * sx, -math.cos(a), 0.0], q)
        b = math.radians(ELBOW_OUT)
        q[la] = aim_bone(nodes, la, ha,
                         [math.sin(b) * sx, -math.cos(b), math.sin(math.radians(ELBOW_FWD))], q)

    # author-time verification -- the score is the test
    wp, _ = fk_world(nodes, q)
    for side, sx in (("l", +1.0), ("r", -1.0)):
        ua, la = m["upperarm_" + side], m["lowerarm_" + side]
        seg = [wp[la][k] - wp[ua][k] for k in range(3)]
        n = math.sqrt(sum(x * x for x in seg))
        got = math.degrees(math.acos(max(-1.0, min(1.0, -seg[1] / n))))
        print("idle: arm_%s hang %.2f deg (want %g)" % (side, got, HANG))
        assert abs(got - HANG) < 0.1, "hang authoring off before it reached the engine"

    # ---- the breath: 3s loop, counter-phased ----
    sp2, neck = m["spine_02"], m["neck_01"]
    LAT = [1.0, 0.0, 0.0]                       # world lateral axis = pitch rotations
    frames_q = []
    for f in range(FRAMES):
        t = f / float(FRAMES)                   # exact loop: sin(2*pi*t), f=FRAMES==f=0
        w = math.sin(2 * math.pi * t)
        wl = math.sin(2 * math.pi * t - 0.5)    # the arms lag the ribcage
        fq = dict(q)
        fq[sp2] = rotate_bone_world(nodes, sp2, LAT, math.radians(1.2) * w, fq)
        fq[neck] = rotate_bone_world(nodes, neck, LAT, math.radians(-0.55) * w, fq)
        for side in ("l", "r"):
            ua = m["upperarm_" + side]
            fq[ua] = rotate_bone_world(nodes, ua, LAT, math.radians(0.9) * wl, fq)
        frames_q.append(fq)

    # breath is real but subtle: head travel at peak vs rest-phase, in metres
    wp0, _ = fk_world(nodes, frames_q[0])
    wp1, _ = fk_world(nodes, frames_q[FRAMES // 4])
    dz = abs(wp1[m["head"]][2] - wp0[m["head"]][2])
    print("idle: breath head travel %.4f (want 0.002..0.030)" % dz)
    assert 0.002 < dz < 0.030, "breath amplitude out of the subtle band"

    # ---- tracks: every node's rotation every frame; translations hold at rest ----
    tracks = {}
    for i in range(len(nodes)):
        vals = []
        for fq in frames_q:
            vals += [float(x) for x in fq.get(i, nodes[i]["q"])]
        tracks[(i, 0)] = vals
    append_clips(pack_path, [("idle", FPS, FRAMES, tracks)], force=True)
    print("idle: appended (%d frames, %d rot tracks, base=rest)" % (FRAMES, len(tracks)))

    declare(pack_path, "idle", {
        "contacts": [{"node": "foot_l", "frames": [0, FRAMES - 1], "still": True, "floor": True},
                     {"node": "foot_r", "frames": [0, FRAMES - 1], "still": True, "floor": True}],
        "max_speed": 0.5,
        "loops": True,
    })
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "reva_native/packs/v1_reva2.rpack"))
