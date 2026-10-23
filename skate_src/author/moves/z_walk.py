#!/usr/bin/env python3
"""z_walk.py -- w_walk: a WALK CYCLE in code, solved by two-bone IK on the body's own legs, over REST via
rig_map -- written for ROBOT NEKO ep 16 so its two code-built bodies can WALK TO A MARK (the engine's
walk election takes the first `w_` clip in a body's bank; without one a body glides).

    z_walk.py <pack> [--force] [--stride 0.55] [--bob 0.025]

The cycle (1.20 s, 36 frames, 2 steps, in place -- the engine drives the root along the path):
  * each foot: STANCE for 60% of its cycle, sliding straight back on the floor from +S/2 to -S/2 at the
    walk's own speed (so a planted foot does not skate under a moving root), then SWING for 40%, an arc
    forward with a lift of 0.12 leg; the two legs half a cycle apart
  * the pelvis: a bob of `bob` leg (lowest at each double support), a sway of 0.02 leg toward the stance
    leg, a yaw of 4 degrees against the swinging leg; the spine counter-rotates
  * the arms: opposite to the same-side leg, 20 degrees at the shoulder, the elbow soft
  * the feet held FLAT in world (hold_world_rotation), the knees poled forward (+z)
  * first frame == last, asserted; the floor never crossed, asserted
S = stride as a fraction of the leg (hip to ankle at rest). Rig units throughout.
"""
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips, drop_clip
from authorlib import rig_map, fk_world, solve_limb, hold_world_rotation, rotate_bone_world, translate_node_world, qnorm, aim_bone

FPS, DUR = 30.0, 1.20
X, Y, Z = [1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]

def foot_path(phase, S, lift):
    """phase 0..1 -> (dz along +z, dy up) for one foot; stance 0..0.6 back on the floor, swing 0.6..1 forward in an arc"""
    if phase < 0.6:
        u = phase / 0.6
        return (S / 2 - S * u, 0.0)
    u = (phase - 0.6) / 0.4
    us = u * u * (3 - 2 * u)
    return (-S / 2 + S * us, lift * math.sin(math.pi * u))

def main(argv):
    pack = argv[1]
    force = "--force" in argv
    stride = float(argv[argv.index("--stride") + 1]) if "--stride" in argv else 0.70
    bobf = float(argv[argv.index("--bob") + 1]) if "--bob" in argv else 0.025
    p = Rpack(pack)
    if "w_walk" in p.clip_names():
        if not force: print("w_walk already present (--force re-authors it)"); return 0
        drop_clip(pack, "w_walk"); p = Rpack(pack)
    nodes = p.nodes(); RM = rig_map(p)
    assert not RM["_missing"], "rig incomplete: %s" % RM["_missing"]
    wp, wq = fk_world(nodes)
    pel = RM["pelvis"]
    legs = {"l": (RM["thigh_l"], RM["calf_l"], RM["foot_l"]), "r": (RM["thigh_r"], RM["calf_r"], RM["foot_r"])}
    L = sum(math.dist(wp[th], wp[ft]) for th, _c, ft in legs.values()) / 2.0
    S, lift, bob = stride * L, 0.15 * L, bobf * L
    floor = min(wp[legs["l"][2]][1], wp[legs["r"][2]][1])
    rest_foot = {s: list(wp[legs[s][2]]) for s in legs}
    rest_fq = {s: list(wq[legs[s][2]]) for s in legs}
    print("w_walk: leg %.3f rig  stride %.3f  lift %.3f  bob %.3f  floor %.3f" % (L, S, lift, bob, floor))
    # ⭐ (10-22, reva) --hang DEG: the arms swing from HANGING, not from an A-pose rest (see z_gest.py)
    base = {}
    if "--hang" in argv:
        hd = math.radians(float(argv[argv.index("--hang") + 1]))
        for s_, sx_ in (("l", 1.0), ("r", -1.0)):
            ua_, la_, ha_ = RM["upperarm_" + s_], RM["lowerarm_" + s_], RM["hand_" + s_]
            base[ua_] = aim_bone(nodes, ua_, la_, [math.sin(hd) * sx_, -math.cos(hd), 0.03], base)
            base[la_] = aim_bone(nodes, la_, ha_, [math.sin(hd) * sx_ * 0.8, -math.cos(math.radians(12.0)), math.sin(math.radians(12.0))], base)
        print("w_walk: arms hang %.0f degrees off the body (--hang)" % math.degrees(hd))
    frames = int(round(DUR * FPS)) + 1          # the last frame IS the first (the club's loops carry it, like z_rm_sk_ride)
    qs, ts = [], []
    for f in range(frames):
        ph = f / float(frames - 1)
        q, t = dict(base), {}
        # the pelvis: bob (lowest at double support, ph 0 and 0.5), sway toward the stance leg, a little yaw
        dy = -bob * 0.5 * (1.0 + math.cos(4 * math.pi * ph))
        sway = 0.02 * L * math.sin(2 * math.pi * ph)          # +x at ph 0.25 (left leg in stance from 0..0.6 -> weight left)
        t[pel] = translate_node_world(nodes, pel, [sway, dy, 0.0], q, t)
        q[pel] = rotate_bone_world(nodes, pel, Y, math.radians(4.0) * math.sin(2 * math.pi * ph), q)
        for j, a in (("spine_01", 2.0), ("spine_02", 2.0)):
            q[RM[j]] = rotate_bone_world(nodes, RM[j], Y, math.radians(-a) * math.sin(2 * math.pi * ph), q)
        # the legs
        for s, off in (("l", 0.0), ("r", 0.5)):
            th, ca, ft = legs[s]
            dz, up = foot_path((ph + off) % 1.0, S, lift)
            tgt = [rest_foot[s][0], rest_foot[s][1] + up, rest_foot[s][2] + dz]
            ik = solve_limb(nodes, (th, ca, ft), tgt, pole=[0.0, 0.0, 1.0], q_override=q, t_override=t)
            q.update(ik)
            q[ft] = hold_world_rotation(nodes, ft, rest_fq[s], q, t)
        # the arms: against the same-side leg
        for s, sx, off in (("l", 1.0, 0.0), ("r", -1.0, 0.5)):
            ua, la = RM["upperarm_" + s], RM["lowerarm_" + s]
            swing = math.radians(26.0) * math.cos(2 * math.pi * (ph + off))   # the leg is forward at ph+off ~ 0 -> the arm back
            q[ua] = rotate_bone_world(nodes, ua, X, swing, q)
            q[la] = rotate_bone_world(nodes, la, X, math.radians(-14.0) + 0.4 * abs(swing) * -1.0, q)
        qs.append(q); ts.append(t)
    # THE SCORE IS THE TEST
    w0, _ = fk_world(nodes, qs[0], ts[0]); w1, _ = fk_world(nodes, qs[-1], ts[-1])
    gap = max(math.dist(w0[i], w1[i]) for i in range(len(nodes)))
    low = min(fk_world(nodes, qs[f], ts[f])[0][legs[s][2]][1] for f in range(frames) for s in legs)
    print("w_walk: first-vs-last %.4f rig u (loop), lowest ankle %.4f vs rest floor %.4f" % (gap, low, floor))
    assert gap < 0.02 * L, "the cycle does not close"
    assert low > floor - 0.01 * L, "a foot went under the floor"
    tracks = {}
    for i in range(len(nodes)):
        tracks[(i, 0)] = [float(x) for f in range(frames) for x in qnorm(qs[f].get(i, nodes[i]["q"]))]
    tracks[(pel, 1)] = [float(x) for f in range(frames) for x in ts[f].get(pel, nodes[pel]["t"])]
    bi = next((i for i, nm in enumerate(p.node_names()) if "skateboard" in nm.lower() or "board" in nm.lower()), -1)
    if bi >= 0: tracks[(bi, 1)] = [0.0, -50.0, 0.0] * frames      # the board goes back in the folder (see z_gest.py)
    append_clips(pack, [("w_walk", FPS, frames, tracks)], force=True)
    print("w_walk: appended (%d frames) to %s" % (frames, os.path.basename(pack)))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
