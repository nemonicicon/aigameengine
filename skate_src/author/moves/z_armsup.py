#!/usr/bin/env python3
"""z_armsup.py -- both arms up over the head and HELD there, hands waving: a body showing off
its shoulders.

ROBOT NEKO ep 3 (UPDATE), 2026-09-10. reva's rig had hung her arm joints under her arms, and her
line 4 is "Raising them is not supported." After the rebuild, the payoff line is "I appear to
have functional shoulders" -- and the first cut of it played her conversational x_convo gestures,
which on a wide are arms-at-the-sides with a bent elbow: nobody could see that anything had
changed. A payoff the frame cannot show is not a payoff. This is the one move a broken shoulder
could never make: full abduction, held, with the hands going.

Composed over REST like z_upright (so it needs no base clip and resolves on any rig rig_map
reads): the upper arms sweep from the z_upright hang out through the T to overhead, eased, in the
first RAISE seconds; then they hold, the forearms waving. It does not come back down -- the next
line's clip crossfades her out of it -- and it is long enough that one line never sees it loop.

The name avoids every miner substring (stand/idle/walk/run...), so it changes no role election.

usage: z_armsup.py <pack>
"""
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips
from authorlib import rig_map, fk_world, aim_bone, qnorm

HANG, UP = 14.0, 164.0       # upper arm, degrees from hanging straight down (180 = straight up)
FWD = 0.12                   # a little forward of the lateral plane, the way arms really rise
RAISE = 0.55                 # seconds to get them up
WAVE_HZ, WAVE_DEG = 2.6, 11.0
FRAMES, FPS = 120, 30.0      # 4 s: longer than any one line, so the loop is never seen


def ease(t):
    t = max(0.0, min(1.0, t)); return t * t * (3.0 - 2.0 * t)


def main(pack_path):
    p = Rpack(pack_path)
    if "z_armsup" in p.clip_names():
        print("z_armsup already present"); return 0
    nodes = p.nodes()
    RM = rig_map(p)
    assert not RM["_missing"], "rig incomplete: %s" % RM["_missing"]

    frames_q = []
    for f in range(FRAMES):
        t = f / FPS
        u = ease(t / RAISE)
        th = math.radians(HANG + (UP - HANG) * u)
        hold = ease((t - RAISE) / 0.25)                      # the wave starts once they are up
        wave = math.radians(WAVE_DEG) * hold * math.sin(2 * math.pi * WAVE_HZ * (t - RAISE))
        q = {}
        for side, sx in (("l", +1.0), ("r", -1.0)):
            ua, la, ha = RM["upperarm_" + side], RM["lowerarm_" + side], RM["hand_" + side]
            if min(ua, la, ha) < 0: continue
            q[ua] = aim_bone(nodes, ua, la, [math.sin(th) * sx, -math.cos(th), FWD * math.sin(th)], q)
            # the forearm carries on a little past the upper arm (a soft elbow), and waves --
            # the two hands in opposite phase, so it reads as waving and not as a clap
            tf = th + math.radians(10.0) + wave * sx
            q[la] = aim_bone(nodes, la, ha, [math.sin(tf) * sx, -math.cos(tf), FWD * math.sin(tf)], q)
        frames_q.append(q)

    # THE SCORE IS THE TEST: held, both hands are above the head, and she is still standing
    hq = frames_q[int((RAISE + 0.6) * FPS)]
    wp, _ = fk_world(nodes, hq)
    head = wp[RM["head"]][1]; hip = wp[RM["pelvis"]][1]
    foot = min(wp[RM["foot_l"]][1], wp[RM["foot_r"]][1])
    for side in ("l", "r"):
        hy = wp[RM["hand_" + side]][1]
        print("z_armsup: hand_%s %+.3f over the head (rig units)" % (side, hy - head))
        assert hy > head, "the arms did not get above the head"
    assert head > hip > foot, "this rest pose is not a stand"

    tracks = {}
    for i in range(len(nodes)):
        vals = []
        for q in frames_q:
            vals += [float(x) for x in qnorm(q.get(i, nodes[i]["q"]))]
        tracks[(i, 0)] = vals
    append_clips(pack_path, [("z_armsup", FPS, FRAMES, tracks)])
    print("z_armsup: appended (%d frames, %d tracks, base=REST)" % (FRAMES, len(tracks)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
