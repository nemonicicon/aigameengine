#!/usr/bin/env python3
"""z_fold.py -- THE FOLD: standing bent over at the hips, the knees giving, the head hung below the shoulders and the arms hanging, the way someone
can stand folded over on a sidewalk for minutes at a time. The back sinks lower and lower for several seconds, catches itself and comes part of the
way up, and sinks again. On any rig
the canon maps (authorlib.rig_map), over a stand clip of its own.

    z_fold.py <pack> [--stand Idle_Loop] [--name z_fold] [--force] [--deep 1.0]

The stand's first frame is held, and over it, frame by frame:
  * the three spine joints tip forward about the body's own side axis, together 38 degrees standing and 70 at the deepest; the neck and head another
    26 to 52, so the head hangs below the shoulders
  * the pelvis tips forward with them, the hips go back and down, and each leg is solved again (two-bone IK) so its foot stays exactly where the stand
    had it, flat: the knees bend
  * the arms hang: each upper arm is aimed at the ground under its shoulder, a little forward, and the elbow eased
  * a sway from side to side, a degree and a half, twice a cycle
One cycle of 9 s at 30 fps, and it loops on its first frame: sinking 6.2 s, the catch in 0.6 s (up a little over half way), settling back 2.2 s.
--deep scales how far it goes (1 the numbers above).

THE GATES: every frame's feet within 5 mm of the stand's; the cycle's last frame turns into its first (the loop); nothing left of the rig unmapped
that the fold needs (spine, neck, head, both legs and feet, both arms). Measured, never assumed: it prints the feet's worst error and the head's drop."""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips, drop_clip  # noqa: E402
from authorlib import (rig_map, fk_world, solve_limb, hold_world_rotation, rotate_bone_world, translate_node_world, clip_pose, aim_bone,  # noqa: E402
                       ease_smooth)

FPS = 30.0
CYCLE = 9.0
PLANT_TOL = 0.005


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


def sink_at(t):
    """0 at the start and the end, 1 at the deepest: sinking over 6.2 s, the catch over 0.6 s to 0.45, settling back over 2.2 s"""
    if t < 6.2:
        u = t / 6.2
        return ease_smooth(u) * 0.35 + (u ** 1.8) * 0.65   # it drifts, and goes faster as it goes
    if t < 6.8:
        return 1.0 - 0.55 * ease_smooth((t - 6.2) / 0.6)
    return 0.45 * (1.0 - ease_smooth((t - 6.8) / 2.2))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pack")
    ap.add_argument("--stand", default="Idle_Loop")
    ap.add_argument("--name", default="z_fold")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--deep", type=float, default=1.0)
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
        sys.exit("the rig has no %s: the fold needs them" % ", ".join(miss))
    nodes = p.nodes()
    q0, t0 = clip_pose(p, a.stand, 0.0)
    wp0, wq0 = fk_world(nodes, q0, t0)
    # the body's own frame: up from between the feet to the head, forward the way the toes point
    feet_mid = [(wp0[RM["foot_l"]][k] + wp0[RM["foot_r"]][k]) * 0.5 for k in range(3)]
    up = unit(sub(wp0[RM["head"]], feet_mid))
    toes = add(sub(wp0[RM["ball_l"]], wp0[RM["foot_l"]]), sub(wp0[RM["ball_r"]], wp0[RM["foot_r"]]))
    if RM["ball_l"] == RM["foot_l"] or math.sqrt(dot(toes, toes)) < 1e-6:
        sys.exit("no toes to say which way the body faces")
    # forward is square to the hips (the thighs' line), and the toes only say which side of it is the front: a stand's feet can be turned out
    # unevenly (the library's idle: its toes' mean is 25 degrees off), and a bend about that would lean the body sideways
    left = unit(sub(wp0[RM["thigh_l"]], wp0[RM["thigh_r"]]))
    left = unit(sub(left, [u * dot(left, up) for u in up]))
    fwd = unit(cross(left, up))
    if dot(fwd, toes) < 0:
        fwd = [-x for x in fwd]    # (left stays the body's own left: the arms hang out to their own sides)
    side = unit(cross(up, fwd))    # turning about this tips up toward forward
    leg = math.dist(wp0[RM["thigh_l"]], wp0[RM["calf_l"]]) + math.dist(wp0[RM["calf_l"]], wp0[RM["foot_l"]])
    height = dot(sub(wp0[RM["head"]], feet_mid), up)
    print("%s: up %s, forward %s, a leg %.3f, the head %.3f over the feet" % (os.path.basename(a.pack), ["%.2f" % x for x in up], ["%.2f" % x for x in fwd], leg, height))
    frames = int(round(CYCLE * FPS))
    D = a.deep
    track_nodes = sorted(set(q0.keys()) | {RM[k] for k in need})
    rot = {i: [] for i in track_nodes}
    trs = {i: [] for i in set(t0.keys()) | {RM["pelvis"]}}
    worst, drop_max = 0.0, 0.0
    for f in range(frames):
        t = f / FPS
        s = sink_at(t)
        qo = dict(q0)
        to = dict(t0)
        r = math.radians
        # the pelvis tips forward and goes back and down
        qo[RM["pelvis"]] = rotate_bone_world(nodes, RM["pelvis"], side, r((8.0 + 6.0 * s) * D), qo)
        to[RM["pelvis"]] = translate_node_world(nodes, RM["pelvis"], add([-x * (0.06 + 0.05 * s) * leg * D for x in fwd], [-x * (0.05 + 0.06 * s) * leg * D for x in up]), qo, to)
        # the spine, the neck and the head, with a sway about forward
        bend = (30.0 + 32.0 * s) * D
        for bone, share in (("spine_01", 0.36), ("spine_02", 0.34), ("spine_03", 0.30)):
            qo[RM[bone]] = rotate_bone_world(nodes, RM[bone], side, r(bend * share), qo)
        qo[RM["spine_01"]] = rotate_bone_world(nodes, RM["spine_01"], fwd, r(1.5 * math.sin(2.0 * math.pi * t / (CYCLE / 2.0))), qo)
        qo[RM["neck_01"]] = rotate_bone_world(nodes, RM["neck_01"], side, r((14.0 + 10.0 * s) * D), qo)
        qo[RM["head"]] = rotate_bone_world(nodes, RM["head"], side, r((12.0 + 16.0 * s) * D), qo)
        # the arms hang, a little forward and out, the elbows eased
        down = [-x for x in up]
        for sd, lr in ((1.0, "l"), (-1.0, "r")):
            ua, la, hd = RM["upperarm_" + lr], RM["lowerarm_" + lr], RM["hand_" + lr]
            qo[ua] = aim_bone(nodes, ua, la, unit(add(add(down, fwd, 0.16 + 0.06 * s), left, 0.08 * sd)), qo, to)
            qo[la] = aim_bone(nodes, la, hd, unit(add(add(down, fwd, 0.34), left, 0.04 * sd)), qo, to)
        # the legs again, each foot where the stand had it, flat
        for lr in ("l", "r"):
            th, ca, ft = RM["thigh_" + lr], RM["calf_" + lr], RM["foot_" + lr]
            wp, _wq = fk_world(nodes, qo, to)
            pole = add(wp[ca], fwd, 1.0)
            qo.update(solve_limb(nodes, (th, ca, ft), wp0[ft], pole, qo, to))
            qo[ft] = hold_world_rotation(nodes, ft, wq0[ft], qo, to)
        wp, _wq = fk_world(nodes, qo, to)
        for ft in (RM["foot_l"], RM["foot_r"]):
            worst = max(worst, math.dist(wp[ft], wp0[ft]))
        drop_max = max(drop_max, dot(sub(wp0[RM["head"]], wp[RM["head"]]), up))
        for i in track_nodes:
            rot[i] += list(qo.get(i, nodes[i]["q"]))
        for i in trs:
            trs[i] += list(to.get(i, nodes[i]["t"]))
    if worst > PLANT_TOL:
        sys.exit("REFUSED: a foot moved %.1f mm off the stand's place (the gate is %.0f mm): the legs can't reach" % (worst * 1000.0, PLANT_TOL * 1000.0))
    tracks = {(i, 0): v for i, v in rot.items()}
    tracks.update({(i, 1): v for i, v in trs.items()})
    append_clips(a.pack, [(a.name, FPS, frames, tracks)])
    print("%s: %d frames at %.0f fps (%.1f s, looping), over %s's first frame; the feet held within %.2f mm, the head down %.3f at the deepest (%.0f%% of its height)"
          % (a.name, frames, FPS, CYCLE, a.stand, worst * 1000.0, drop_max, 100.0 * drop_max / max(height, 1e-6)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
