#!/usr/bin/env python3
"""z_cuddleseat.py -- reva's half of the curl-up: sit on the floor, rest a hand on the
sleeper, breathe.

jacob's original sentence, first session: "have kaeili crawl around cute like a cat,
then curl up around reva an fall asleep, purring." This is reva's part. She sits down
cross-legged, looks down at the body curling up against her, lowers a hand onto the
sleeper's flank, and settles into a slow breath for the rest of the clip.

The hand-rest is the same class of contact as the hug's landings: a point on the OTHER
body, passed in as world numbers (--restx/y/z, from the partner's curled geometry), and
gated for reach from the SEATED shoulder.

usage: z_cuddleseat.py <pack> --scale S --arm A --restx X --resty Y --restz Z
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips
from authorlib import (rig_map, fk_world, solve_limb, rotate_bone_world, profile,
                       ease_smooth, qnorm, declare, translate_node_world)

FPS = 30.0
T_SIT, T_REST, T_END = 1.6, 2.8, 7.3
SEAT_H = 0.32         # world: seated pelvis height

def main(pack_path, SCALE, ARMW, REST):
    p = Rpack(pack_path)
    if "z_rm_cuddleseat" in p.clip_names():
        print("z_cuddleseat already present"); return 0
    nodes = p.nodes()
    RM = rig_map(p)
    assert not RM["_missing"], "rig incomplete: %s" % RM["_missing"]
    names_c = p.clip_names()
    ci = names_c.index("z_upright" if "z_upright" in names_c else RM["_base_clip"])
    base_q, base_t, base = {}, {}, {}
    for (node, typ, data) in p.tracks(ci):
        comp = 4 if typ == 0 else 3
        v = list(struct.unpack_from("<%df" % comp, data, 0))
        base[(node, typ)] = v
        if typ == 0: base_q[node] = v
        elif typ == 1: base_t[node] = v

    pelvis = RM["pelvis"]
    spine = [b for b in (RM["spine_01"], RM["spine_02"], RM["spine_03"]) if b >= 0]
    neck, headb = RM["neck_01"], RM["head"]
    legs = {"L": (RM["thigh_l"], RM["calf_l"], RM["foot_l"]),
            "R": (RM["thigh_r"], RM["calf_r"], RM["foot_r"])}
    armR = (RM["upperarm_r"], RM["lowerarm_r"], RM["hand_r"])
    armL = (RM["upperarm_l"], RM["lowerarm_l"], RM["hand_l"])

    wp0, _ = fk_world(nodes, base_q, base_t)
    wr, _ = fk_world(nodes)
    mny = min(w[1] for w in wr)
    pelv_y0 = wp0[pelvis][1]
    drop = (pelv_y0 - mny) - SEAT_H / SCALE          # how far the pelvis descends

    # seated shoulder reach check for the hand-rest
    sh = wp0[armR[0]]
    sh_seated = [sh[0], sh[1] - drop, sh[2]]
    rest_rig = [REST[0] / SCALE, mny + REST[1] / SCALE, REST[2] / SCALE]
    d = math.dist(sh_seated, rest_rig) * SCALE
    print("z_rm_cuddleseat: hand-rest reach %.2fu of %.2fu arm (seated)" % (d, ARMW))
    assert d <= 0.94 * ARMW, "the resting hand cannot reach the sleeper from the seat"

    frames = int(T_END * FPS)
    qtr = {}; ttr = []
    for f in range(frames):
        t = f / FPS
        sit = profile([(0.1, 0.0, ease_smooth), (T_SIT, 1.0, None)], t)
        rest = profile([(T_SIT + 0.2, 0.0, ease_smooth), (T_REST, 1.0, None)], t)
        # slow sleepy breath once settled, shoulders-led
        br = math.sin(2 * math.pi * (t - T_REST) / 3.4) if t > T_REST else 0.0
        qo = dict(base_q); to = dict(base_t)
        to[pelvis] = translate_node_world(nodes, pelvis, [0.0, -drop * sit, 0.0],
                                          base_q, base_t)
        # settle the torso: a soft slump, head inclined toward the sleeper
        qo[pelvis] = rotate_bone_world(nodes, pelvis, [1, 0, 0], math.radians(6.0) * sit, qo)
        for b in spine:
            qo[b] = rotate_bone_world(nodes, b, [1, 0, 0],
                                      math.radians(3.0 * sit + 1.1 * br * rest), qo)
        if neck >= 0:
            qo[neck] = rotate_bone_world(nodes, neck, [1, 0, 0], math.radians(14.0) * rest, qo)
        qo[headb] = rotate_bone_world(nodes, headb, [1, 0, 0], math.radians(10.0) * rest, qo)

        # CROSS-LEGGED: feet tuck in front of the pelvis near the midline, knees OUT
        for s, sx in (("L", +1.0), ("R", -1.0)):
            chain = legs[s]
            a0 = wp0[chain[2]]
            tgt = [a0[0] + (sx * 0.10 / SCALE - a0[0]) * sit,
                   a0[1] + (mny + 0.06 / SCALE - a0[1]) * sit,
                   a0[2] + (0.28 / SCALE - a0[2]) * sit]
            upd = solve_limb(nodes, chain, tgt, pole=[sx * 1.0, 0.15, 0.35],
                             q_override=qo, t_override=to)
            qo.update(upd)

        # the RIGHT hand lowers onto the sleeper's flank and rides her breath faintly
        rest_t = [rest_rig[0], rest_rig[1] + (0.008 * br * rest) / SCALE, rest_rig[2]]
        h0 = wp0[armR[2]]
        tgt = [h0[0] + (rest_t[0] - h0[0]) * rest,
               h0[1] - drop * sit + (rest_t[1] - (h0[1] - drop * sit)) * rest,
               h0[2] + (rest_t[2] - h0[2]) * rest]
        upd = solve_limb(nodes, armR, tgt, pole=[-0.8, -0.5, 0.3], q_override=qo, t_override=to)
        qo.update(upd)
        # the LEFT hand settles onto her own knee
        k0 = wp0[armL[2]]
        tgt2 = [k0[0] + (0.16 / SCALE - k0[0]) * sit,
                k0[1] - drop * sit + (mny + 0.42 / SCALE - (k0[1] - drop * sit)) * sit,
                k0[2] + (0.30 / SCALE - k0[2]) * sit]
        upd = solve_limb(nodes, armL, tgt2, pole=[0.9, -0.4, 0.2], q_override=qo, t_override=to)
        qo.update(upd)

        chain_all = [pelvis, neck, headb] + list(spine) + list(armR) + list(armL)
        for s in "LR": chain_all += list(legs[s])
        for b in chain_all:
            if b is not None and b >= 0:
                qtr.setdefault(b, []).extend(qnorm(qo.get(b, base_q.get(b, nodes[b]["q"]))))
        ttr.extend(to[pelvis])

    tracks = {}
    for (node, typ), v in base.items():
        tracks[(node, typ)] = v * frames
    for b, vals in qtr.items(): tracks[(b, 0)] = vals
    tracks[(pelvis, 1)] = ttr
    append_clips(pack_path, [("z_rm_cuddleseat", FPS, frames, tracks)])
    declare(pack_path, "z_rm_cuddleseat", {
        "fps": FPS, "frames": frames, "terminal": "hold", "max_speed": 5.0,
        "contacts": [{"part": "fL", "t": [T_SIT + 0.3, T_END - 0.1], "floor": True, "still_tol": 0.05},
                     {"part": "fR", "t": [T_SIT + 0.3, T_END - 0.1], "floor": True, "still_tol": 0.05}]})
    print("z_rm_cuddleseat: appended (%d frames, %d tracks)" % (frames, len(tracks)))
    return 0

if __name__ == "__main__":
    a = sys.argv[1:]
    def arg(nm, d):
        return float(a[a.index(nm) + 1]) if nm in a else d
    sys.exit(main(a[0], arg("--scale", 1.0), arg("--arm", 1.0),
                  [arg("--restx", -0.08), arg("--resty", 0.34), arg("--restz", 0.42)]))
