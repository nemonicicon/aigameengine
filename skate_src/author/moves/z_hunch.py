#!/usr/bin/env python3
"""z_hunch.py -- THE HUNCH: standing, the head bowed over the hands held together in front of the belly, the shoulders rounded, looking at what's in
them (a phone, a lighter, foil, a few bills), the weight shifting from foot to foot. A common way to stand on a street corner, after standing plain
(11-06, from photos of one on an ordinary day: what each person is doing, and their posture). On any rig the
canon maps (authorlib.rig_map), over a stand clip of its own.

    z_hunch.py <pack> [--stand Idle_Loop] [--name z_hunch] [--force]

The stand's first frame is held, and over it, frame by frame:
  * the pelvis tips forward 4 degrees and eases back; the three spine joints round forward together 14 to 17 degrees, the neck 20, the head 24 to 30,
    nodding a little now and then: the face is down toward the hands
  * the hands come together a hand's breadth in front of the belly (two-bone IK to a point off the lowest spine joint), the elbows down and a little out,
    and work there: a few centimetres, never still
  * the weight shifts from foot to foot: the hips a centimetre and a half to a side and back, a degree of sway; each leg is solved again so its foot stays
    exactly where the stand had it, flat
One cycle of 7 s at 30 fps, and it loops on its first frame.

THE GATES: every frame's feet within 5 mm of the stand's; the hands within 3 cm of where they're sent; the loop. Measured: it prints the feet's worst
error, the hands' worst miss and how far the head is over the hands."""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips, drop_clip  # noqa: E402
from authorlib import (rig_map, fk_world, solve_limb, hold_world_rotation, rotate_bone_world, translate_node_world, clip_pose)  # noqa: E402

FPS = 30.0
CYCLE = 7.0
PLANT_TOL = 0.005
HAND_TOL = 0.03


def unit(v):
    L = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / L for x in v]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def dot(a, b):
    return sum(a[k] * b[k] for k in range(3))


def sub(a, b):
    return [a[k] - b[k] for k in range(3)]


def add(a, b, s=1.0):
    return [a[k] + b[k] * s for k in range(3)]


def wave(t, period, phase=0.0):
    """a smooth wave over the cycle that loops: period a whole fraction of CYCLE"""
    return math.sin(2.0 * math.pi * (t / period + phase))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pack")
    ap.add_argument("--stand", default="Idle_Loop")
    ap.add_argument("--name", default="z_hunch")
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args()
    p = Rpack(a.pack)
    cn = p.clip_names()
    if a.name in cn:
        if not a.force:
            print("%s is already in %s (--force to make it again)" % (a.name, a.pack))
            return 0
        drop_clip(a.pack, a.name)
        p = Rpack(a.pack)
        cn = p.clip_names()
    if a.stand not in cn:
        sys.exit("no stand clip %s in %s (it has %s)" % (a.stand, a.pack, ", ".join(cn[:12])))
    RM = rig_map(p)
    need = ["pelvis", "spine_01", "spine_02", "spine_03", "neck_01", "head", "upperarm_l", "lowerarm_l", "upperarm_r", "lowerarm_r",
            "thigh_l", "calf_l", "foot_l", "thigh_r", "calf_r", "foot_r", "hand_l", "hand_r"]
    miss = [k for k in need if RM.get(k, -1) is None or RM.get(k, -1) < 0]
    if miss:
        sys.exit("the rig has no %s: the hunch needs them" % ", ".join(miss))
    nodes = p.nodes()
    q0, t0 = clip_pose(p, a.stand, 0.0)
    wp0, wq0 = fk_world(nodes, q0, t0)
    feet_mid = [(wp0[RM["foot_l"]][k] + wp0[RM["foot_r"]][k]) * 0.5 for k in range(3)]
    up = unit(sub(wp0[RM["head"]], feet_mid))
    toes = add(sub(wp0[RM["ball_l"]], wp0[RM["foot_l"]]), sub(wp0[RM["ball_r"]], wp0[RM["foot_r"]]))
    if RM["ball_l"] == RM["foot_l"] or math.sqrt(dot(toes, toes)) < 1e-6:
        sys.exit("no toes to say which way the body faces")
    left = unit(sub(wp0[RM["thigh_l"]], wp0[RM["thigh_r"]]))
    left = unit(sub(left, [u * dot(left, up) for u in up]))
    fwd = unit(cross(left, up))
    if dot(fwd, toes) < 0:
        fwd = [-x for x in fwd]
    side = unit(cross(up, fwd))
    leg = math.dist(wp0[RM["thigh_l"]], wp0[RM["calf_l"]]) + math.dist(wp0[RM["calf_l"]], wp0[RM["foot_l"]])
    height = dot(sub(wp0[RM["head"]], feet_mid), up)
    print("%s: a leg %.3f, the head %.3f over the feet" % (os.path.basename(a.pack), leg, height))
    frames = int(round(CYCLE * FPS))
    track_nodes = sorted(set(q0.keys()) | {RM[k] for k in need})
    rot = {i: [] for i in track_nodes}
    trs = {i: [] for i in set(t0.keys()) | {RM["pelvis"]}}
    worst, hand_miss, over = 0.0, 0.0, []
    r = math.radians
    for f in range(frames):
        t = f / FPS
        qo = dict(q0)
        to = dict(t0)
        shift = wave(t, CYCLE)                       # the weight from foot to foot, once a cycle
        nod = 0.5 + 0.5 * wave(t, CYCLE / 3.0, 0.1)   # three nods a cycle
        # the pelvis: tipped forward a little, eased back, shifted to a side with the weight
        qo[RM["pelvis"]] = rotate_bone_world(nodes, RM["pelvis"], side, r(4.0), qo)
        qo[RM["pelvis"]] = rotate_bone_world(nodes, RM["pelvis"], fwd, r(1.0 * shift), qo)
        to[RM["pelvis"]] = translate_node_world(nodes, RM["pelvis"], add([-x * 0.02 * leg for x in fwd], left, 0.015 * shift), qo, to)
        # the spine rounds forward, the neck and head bow toward the hands
        bend = 14.0 + 3.0 * nod
        for bone, share in (("spine_01", 0.30), ("spine_02", 0.33), ("spine_03", 0.37)):
            qo[RM[bone]] = rotate_bone_world(nodes, RM[bone], side, r(bend * share), qo)
        qo[RM["spine_01"]] = rotate_bone_world(nodes, RM["spine_01"], fwd, r(-1.0 * shift), qo)
        qo[RM["neck_01"]] = rotate_bone_world(nodes, RM["neck_01"], side, r(20.0), qo)
        qo[RM["head"]] = rotate_bone_world(nodes, RM["head"], side, r(24.0 + 6.0 * nod), qo)
        # the hands together a hand's breadth in front of the belly, working
        wp, _wq = fk_world(nodes, qo, to)
        belly = add(add(wp[RM["spine_01"]], fwd, 0.17 * leg), up, 0.06 * leg)
        fid = [0.012 * wave(t, CYCLE / 7.0), 0.01 * wave(t, CYCLE / 5.0, 0.3)]
        for sd, lr in ((1.0, "l"), (-1.0, "r")):
            ua, la, hd = RM["upperarm_" + lr], RM["lowerarm_" + lr], RM["hand_" + lr]
            target = add(add(add(belly, left, sd * 0.045), fwd, fid[0]), up, fid[1] * sd)
            wp, _wq = fk_world(nodes, qo, to)
            pole = add(add(add(wp[la], up, -1.0), left, sd * 0.5), fwd, -0.3)
            qo.update(solve_limb(nodes, (ua, la, hd), target, pole, qo, to))
            wp, _wq = fk_world(nodes, qo, to)
            hand_miss = max(hand_miss, math.dist(wp[hd], target))
        # the legs again, each foot where the stand had it, flat
        for lr in ("l", "r"):
            th, ca, ft = RM["thigh_" + lr], RM["calf_" + lr], RM["foot_" + lr]
            wp, _wq = fk_world(nodes, qo, to)
            qo.update(solve_limb(nodes, (th, ca, ft), wp0[ft], add(wp[ca], fwd, 1.0), qo, to))
            qo[ft] = hold_world_rotation(nodes, ft, wq0[ft], qo, to)
        wp, _wq = fk_world(nodes, qo, to)
        for ft in (RM["foot_l"], RM["foot_r"]):
            worst = max(worst, math.dist(wp[ft], wp0[ft]))
        hands = [(wp[RM["hand_l"]][k] + wp[RM["hand_r"]][k]) * 0.5 for k in range(3)]
        over.append(dot(sub(wp[RM["head"]], hands), fwd))
        for i in track_nodes:
            rot[i] += list(qo.get(i, nodes[i]["q"]))
        for i in trs:
            trs[i] += list(to.get(i, nodes[i]["t"]))
    if worst > PLANT_TOL:
        sys.exit("REFUSED: a foot moved %.1f mm off the stand's place (the gate is %.0f mm)" % (worst * 1000.0, PLANT_TOL * 1000.0))
    if hand_miss > HAND_TOL:
        sys.exit("REFUSED: a hand fell %.1f cm short of where it was sent (the gate is %.0f cm): the arms can't reach" % (hand_miss * 100.0, HAND_TOL * 100.0))
    tracks = {(i, 0): v for i, v in rot.items()}
    tracks.update({(i, 1): v for i, v in trs.items()})
    append_clips(a.pack, [(a.name, FPS, frames, tracks)])
    print("%s: %d frames at %.0f fps (%.1f s, looping), over %s's first frame; the feet held within %.2f mm, the hands within %.1f cm of their mark, "
          "the head %.0f to %.0f cm behind the hands along forward (over them when near 0)"
          % (a.name, frames, FPS, CYCLE, a.stand, worst * 1000.0, hand_miss * 100.0, min(over) * 100.0, max(over) * 100.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
