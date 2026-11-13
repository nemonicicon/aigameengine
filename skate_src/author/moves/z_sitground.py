#!/usr/bin/env python3
"""z_sitground.py -- SITTING ON THE GROUND, the back against a fence or a wall: the knees up, the feet flat on the sidewalk in front, the forearms over
the knees and the hands hanging past them, the head down and nodding off, catching itself and going down again. The way a row of people sit along the
lot's fence (11-06, from photos of a street corner: what each person is doing, and their posture). On any rig
the canon maps (authorlib.rig_map), over a stand clip of its own. A graph plays it as `low` (the engine lets the body down by the clip's own hip drop).

    z_sitground.py <pack> [--stand Idle_Loop] [--name z_sitground] [--force]

The stand's first frame is held, and over it, frame by frame:
  * the hips go down until the hip joints are 11 cm over the ground, a little back; the pelvis rolls back 20 degrees (slumped), the spine leans back a
    little against what's behind it and rounds forward at the shoulders
  * each foot is put flat on the ground in front, 0.70 of a leg out from its hip and a little wider (two-bone IK, the knee up)
  * each hand is put on its knee, the elbow down and out: the forearms lie along the thighs, the hands on the knees
  * the head hangs: it sinks for five seconds, comes up in half a second, settles; the chest breathes
One cycle of 8 s at 30 fps, and it loops on its first frame.

THE GATES: the feet within 1 cm of where they're put and the hands within 3 cm; the hip joints within 1 cm of their height over the ground; the loop.
Measured: it prints each, and the head's height over the ground."""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips, drop_clip  # noqa: E402
from authorlib import (rig_map, fk_world, solve_limb, hold_world_rotation, rotate_bone_world, translate_node_world, clip_pose, ease_smooth)  # noqa: E402

FPS = 30.0
CYCLE = 8.0
SEAT = 0.11        # the hip joints over the ground, seated
FOOT_TOL = 0.01
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


def nod_at(t):
    """0 at the start and the end, 1 with the head down furthest: sinking over 5 s, up in 0.5 s, settling 2.5 s"""
    if t < 5.0:
        return ease_smooth(t / 5.0)
    if t < 5.5:
        return 1.0 - 0.85 * ease_smooth((t - 5.0) / 0.5)
    return 0.15 * (1.0 - ease_smooth((t - 5.5) / 2.5))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pack")
    ap.add_argument("--stand", default="Idle_Loop")
    ap.add_argument("--name", default="z_sitground")
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
            "thigh_l", "calf_l", "foot_l", "thigh_r", "calf_r", "foot_r", "hand_l", "hand_r", "ball_l", "ball_r"]
    miss = [k for k in need if RM.get(k, -1) is None or RM.get(k, -1) < 0]
    if miss:
        sys.exit("the rig has no %s: sitting needs them" % ", ".join(miss))
    nodes = p.nodes()
    q0, t0 = clip_pose(p, a.stand, 0.0)
    wp0, wq0 = fk_world(nodes, q0, t0)
    feet_mid = [(wp0[RM["foot_l"]][k] + wp0[RM["foot_r"]][k]) * 0.5 for k in range(3)]
    up = unit(sub(wp0[RM["head"]], feet_mid))
    toes = add(sub(wp0[RM["ball_l"]], wp0[RM["foot_l"]]), sub(wp0[RM["ball_r"]], wp0[RM["foot_r"]]))
    left = unit(sub(wp0[RM["thigh_l"]], wp0[RM["thigh_r"]]))
    left = unit(sub(left, [u * dot(left, up) for u in up]))
    fwd = unit(cross(left, up))
    if dot(fwd, toes) < 0:
        fwd = [-x for x in fwd]
    side = unit(cross(up, fwd))
    leg = math.dist(wp0[RM["thigh_l"]], wp0[RM["calf_l"]]) + math.dist(wp0[RM["calf_l"]], wp0[RM["foot_l"]])
    ground = min(dot(wp0[RM["ball_l"]], up), dot(wp0[RM["ball_r"]], up)) - 0.03      # the balls of the feet sit about 3 cm over the ground
    ankle_h = 0.5 * (dot(wp0[RM["foot_l"]], up) + dot(wp0[RM["foot_r"]], up)) - ground
    hip_h = 0.5 * (dot(wp0[RM["thigh_l"]], up) + dot(wp0[RM["thigh_r"]], up)) - ground
    print("%s: a leg %.3f, the hips %.3f and the ankles %.3f over the ground" % (os.path.basename(a.pack), leg, hip_h, ankle_h))
    frames = int(round(CYCLE * FPS))
    track_nodes = sorted(set(q0.keys()) | {RM[k] for k in need})
    rot = {i: [] for i in track_nodes}
    trs = {i: [] for i in set(t0.keys()) | {RM["pelvis"]}}
    foot_miss, hand_miss, seat_miss, head_lo, head_hi = 0.0, 0.0, 0.0, 9.0, -9.0
    r = math.radians
    for f in range(frames):
        t = f / FPS
        nd = nod_at(t)
        br = math.sin(2.0 * math.pi * t / (CYCLE / 2.0))
        qo = dict(q0)
        to = dict(t0)
        # down to the ground, a little back, the pelvis rolled back
        qo[RM["pelvis"]] = rotate_bone_world(nodes, RM["pelvis"], side, r(-20.0), qo)
        to[RM["pelvis"]] = translate_node_world(nodes, RM["pelvis"], add([-x * (hip_h - SEAT) for x in up], fwd, -0.05), qo, to)
        wp, _wq = fk_world(nodes, qo, to)
        hip_now = 0.5 * (dot(wp[RM["thigh_l"]], up) + dot(wp[RM["thigh_r"]], up)) - ground
        to[RM["pelvis"]] = translate_node_world(nodes, RM["pelvis"], [-x * (hip_now - SEAT) for x in up], qo, to)   # the roll moved the hips: put them back
        # the back against what's behind, rounding at the shoulders; the head hangs
        for bone, ang in (("spine_01", -5.0), ("spine_02", 8.0 + 1.0 * br), ("spine_03", 14.0)):
            qo[RM[bone]] = rotate_bone_world(nodes, RM[bone], side, r(ang), qo)
        qo[RM["neck_01"]] = rotate_bone_world(nodes, RM["neck_01"], side, r(20.0 + 6.0 * nd), qo)
        qo[RM["head"]] = rotate_bone_world(nodes, RM["head"], side, r(18.0 + 16.0 * nd), qo)
        # the feet flat on the ground in front, the knees up
        knees = {}
        for sd, lr in ((1.0, "l"), (-1.0, "r")):
            th, ca, ft = RM["thigh_" + lr], RM["calf_" + lr], RM["foot_" + lr]
            wp, _wq = fk_world(nodes, qo, to)
            hipj = wp[th]
            flat = sub(hipj, [u * (dot(hipj, up) - ground) for u in up])       # the hip's point on the ground
            target = add(add(add(flat, fwd, 0.70 * leg), left, sd * 0.06), up, ankle_h)
            pole = add(add(add(hipj, fwd, 0.3), up, 1.0), left, sd * 0.3)
            qo.update(solve_limb(nodes, (th, ca, ft), target, pole, qo, to))
            qo[ft] = hold_world_rotation(nodes, ft, wq0[ft], qo, to)
            wp, _wq = fk_world(nodes, qo, to)
            foot_miss = max(foot_miss, math.dist(wp[ft], target))
            knees[lr] = wp[ca]
        # the hands on the knees, the forearms along the thighs
        for sd, lr in ((1.0, "l"), (-1.0, "r")):
            ua, la, hd = RM["upperarm_" + lr], RM["lowerarm_" + lr], RM["hand_" + lr]
            target = add(add(add(knees[lr], fwd, 0.0), up, 0.05), left, -sd * 0.02)
            wp, _wq = fk_world(nodes, qo, to)
            pole = add(add(wp[la], left, sd * 0.6), up, -0.5)
            qo.update(solve_limb(nodes, (ua, la, hd), target, pole, qo, to))
            wp, _wq = fk_world(nodes, qo, to)
            hand_miss = max(hand_miss, math.dist(wp[hd], target))
        wp, _wq = fk_world(nodes, qo, to)
        seat_miss = max(seat_miss, abs(0.5 * (dot(wp[RM["thigh_l"]], up) + dot(wp[RM["thigh_r"]], up)) - ground - SEAT))
        hh = dot(wp[RM["head"]], up) - ground
        head_lo, head_hi = min(head_lo, hh), max(head_hi, hh)
        for i in track_nodes:
            rot[i] += list(qo.get(i, nodes[i]["q"]))
        for i in trs:
            trs[i] += list(to.get(i, nodes[i]["t"]))
    if foot_miss > FOOT_TOL:
        sys.exit("REFUSED: a foot fell %.1f cm short of the ground in front (the gate is %.0f cm)" % (foot_miss * 100.0, FOOT_TOL * 100.0))
    if hand_miss > HAND_TOL:
        sys.exit("REFUSED: a hand fell %.1f cm short of its knee (the gate is %.0f cm)" % (hand_miss * 100.0, HAND_TOL * 100.0))
    if seat_miss > 0.01:
        sys.exit("REFUSED: the hips ended %.1f cm off their seat" % (seat_miss * 100.0))
    tracks = {(i, 0): v for i, v in rot.items()}
    tracks.update({(i, 1): v for i, v in trs.items()})
    append_clips(a.pack, [(a.name, FPS, frames, tracks)])
    print("%s: %d frames at %.0f fps (%.1f s, looping), over %s's first frame; the hips %.0f cm over the ground (within %.1f mm), the feet within %.1f cm "
          "of their place, the hands within %.1f cm of their knees, the head %.0f to %.0f cm over the ground"
          % (a.name, frames, FPS, CYCLE, a.stand, SEAT * 100.0, seat_miss * 1000.0, foot_miss * 100.0, hand_miss * 100.0, head_lo * 100.0, head_hi * 100.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
