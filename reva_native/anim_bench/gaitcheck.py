#!/usr/bin/env python3
"""gaitcheck.py -- what makes a gait read as natural, measured the same way on any body, in leg lengths and degrees (11-02).

    gaitcheck.py <pack>#<clip[:cycle seconds]> ...

Foot slide says a foot doesn't skate. It says nothing about whether a body walks like one: the DontGetDropped alien's hand-keyed gaits slid under 6 cm and were
plainly unnatural at a glance. A clean-footed gait can still run on two rails with its hands up and its chest square, and this bench reads that off:
how far apart the feet run, how wide and high the hands go, how much the shoulders turn against the hips and the belt line tips, the lean, the knees, the foot's lift.
The control is real mocap on the same bench (v1_mob1's w_walk, w_jog, w_run: a known-good, measure-only). One caveat from running it: that pack's clips hold the hips
still, so its bobs read zero and its speeds are off. Compare the rest. A number near the control doesn't make a gait good; a person's eye does. A number far off is a thing to look at.
Forward comes from the way a planted foot slides against the body. The cycle's true length matters for a clip baked at another rate than it was keyed.
"""
import math
import sys

import os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "skate_src", "author"))
from rpack import Rpack
from authorlib import rig_map, fk_world, clip_pose


def sub(a, b): return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def length(a): return math.sqrt(dot(a, a))
def norm(a):
    l = length(a) or 1.0
    return [a[0] / l, a[1] / l, a[2] / l]
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def angle(a, b): return math.degrees(math.acos(max(-1.0, min(1.0, dot(norm(a), norm(b))))))
def spread(v): return (max(v) - min(v)) / 2.0
def mean(v): return sum(v) / len(v)


def measure(p, clip, seconds=None):
    nodes = p.nodes()
    RM = rig_map(p)
    c = p.clip(p.clip_names().index(clip))
    frames, fps = c["frames"], c["fps"]
    period = seconds * fps if seconds else frames
    steps = 60
    rows = []
    for i in range(steps):
        q, t = clip_pose(p, clip, period * i / steps, loop=period)
        w, _ = fk_world(nodes, q, t)
        rows.append(w)
    wp, _ = fk_world(nodes)
    leg = (length(sub(wp[RM["thigh_l"]], wp[RM["calf_l"]])) + length(sub(wp[RM["calf_l"]], wp[RM["foot_l"]])))
    # Back is the way a low foot goes.
    drift = [0.0, 0.0, 0.0]
    floor = min(r[RM["foot_l"]][1] for r in rows)
    for i in range(steps):
        r, n = rows[i], rows[(i + 1) % steps]
        if r[RM["foot_l"]][1] < floor + 0.06 * leg:
            d = sub(n[RM["foot_l"]], r[RM["foot_l"]])
            drift = [drift[k] + d[k] for k in range(3)]
    drift[1] = 0.0
    fwd = norm([-drift[0], 0.0, -drift[2]])
    up = [0.0, 1.0, 0.0]
    left = norm(cross(up, fwd))
    seconds_long = period / fps
    def horizontal_yaw(v):
        return math.degrees(math.atan2(dot(v, fwd), dot(v, left)))
    def series(fn):
        return [fn(r) for r in rows]
    lean = series(lambda r: math.degrees(math.atan2(dot(sub(r[RM["neck_01"]], r[RM["pelvis"]]), fwd), sub(r[RM["neck_01"]], r[RM["pelvis"]])[1])))
    hips = series(lambda r: horizontal_yaw(sub(r[RM["thigh_l"]], r[RM["thigh_r"]])))
    hip_tilt = series(lambda r: math.degrees(math.asin(max(-1, min(1, sub(r[RM["thigh_l"]], r[RM["thigh_r"]])[1] / length(sub(r[RM["thigh_l"]], r[RM["thigh_r"]])))))))
    shoulders = series(lambda r: horizontal_yaw(sub(r[RM["upperarm_l"]], r[RM["upperarm_r"]])))
    out = {
        "lean": mean(lean),
        "hip_turn": spread(hips),
        "hip_tilt": spread(hip_tilt),
        "shoulder_turn": spread(shoulders),
        "hip_bob": spread(series(lambda r: r[RM["pelvis"]][1])) / leg,
        "head_bob": spread(series(lambda r: r[RM["head"]][1])) / leg,
    }
    arm_out, elbow, swing, hand_up, hand_wide = [], [], [], [], []
    for s, sx in (("l", 1.0), ("r", -1.0)):
        ua, la, ha = RM["upperarm_" + s], RM["lowerarm_" + s], RM["hand_" + s]
        arm_out.append(mean(series(lambda r: math.degrees(math.atan2(sx * dot(sub(r[la], r[ua]), left), -sub(r[la], r[ua])[1])))))
        elbow.append(series(lambda r: 180.0 - angle(sub(r[ua], r[la]), sub(r[ha], r[la]))))
        swing.append(spread(series(lambda r: dot(sub(r[ha], r[RM["pelvis"]]), fwd))) / leg)
        hand_up.append(max(series(lambda r: (r[ha][1] - r[ua][1]))) / leg)
        hand_wide.append(max(series(lambda r: sx * dot(sub(r[ha], r[ua]), left))) / leg)
    out["arm_out"] = mean(arm_out)
    out["elbow"] = mean([mean(e) for e in elbow])
    out["elbow_range"] = mean([spread(e) for e in elbow])
    out["hand_swing"] = mean(swing)
    out["hand_vs_shoulder"] = max(hand_up)
    out["hand_wide"] = max(hand_wide)
    knee = series(lambda r: 180.0 - angle(sub(r[RM["thigh_l"]], r[RM["calf_l"]]), sub(r[RM["foot_l"]], r[RM["calf_l"]])))
    low = [i for i in range(steps) if rows[i][RM["foot_l"]][1] < floor + 0.04 * leg]
    out["knee_down"] = mean([knee[i] for i in low]) if low else 0.0
    out["knee_top"] = max(knee)
    out["foot_top"] = (max(series(lambda r: r[RM["foot_l"]][1])) - floor) / leg
    feet = series(lambda r: abs(dot(sub(r[RM["foot_l"]], r[RM["foot_r"]]), left)))
    out["feet_apart"] = mean(feet) / leg
    toe_out = []
    for s, sx in (("l", 1.0), ("r", -1.0)):
        toe_out.append(mean([math.degrees(math.atan2(sx * dot(sub(rows[i][RM["ball_" + s]], rows[i][RM["foot_" + s]]), left), dot(sub(rows[i][RM["ball_" + s]], rows[i][RM["foot_" + s]]), fwd))) for i in low]) if low else 0.0)
    out["toe_out"] = mean(toe_out)
    out["seconds"] = seconds_long
    # The stride, from how far a low foot slides over the cycle's stance at its speed.
    slide = [dot(sub(rows[(i + 1) % steps][RM["foot_l"]], rows[i][RM["foot_l"]]), [-fwd[0], 0.0, -fwd[2]]) * steps / seconds_long for i in low]
    out["speed"] = (sorted(slide)[len(slide) // 2] if slide else 0.0)
    out["step_legs"] = out["speed"] * seconds_long / 2.0 / leg
    out["leg"] = leg
    return out


KEYS = [("seconds", "cycle s", 2), ("leg", "leg m", 2), ("speed", "speed", 2), ("step_legs", "step/leg", 2), ("lean", "lean deg", 1), ("hip_turn", "hip turn", 1), ("hip_tilt", "hip tilt", 1), ("shoulder_turn", "shldr turn", 1),
        ("hip_bob", "hip bob", 3), ("head_bob", "head bob", 3), ("arm_out", "arm out", 1), ("elbow", "elbow", 0), ("elbow_range", "elbow +-", 0), ("hand_swing", "hand swing", 2),
        ("hand_vs_shoulder", "hand>shldr", 2), ("hand_wide", "hand wide", 2), ("knee_down", "knee down", 0), ("knee_top", "knee top", 0), ("foot_top", "foot lift", 2), ("feet_apart", "feet apart", 2), ("toe_out", "toe out", 0)]
results = []
for spec in sys.argv[1:]:
    pack, clip = spec.split("#", 1)
    seconds = None
    if ":" in clip:
        clip, sec = clip.rsplit(":", 1)
        seconds = float(sec)
    results.append((clip, measure(Rpack(pack), clip, seconds)))
print("%-12s" % "" + "".join("%20s" % r[0][:19] for r in results))
for key, label, digits in KEYS:
    print("%-12s" % label + "".join("%20.*f" % (digits, r[1][key]) for r in results))
