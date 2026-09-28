#!/usr/bin/env python3
"""z_curlsleep.py -- kaeli's half: lie down, curl up around the seated one, sleep,
PURR.

The other half of jacob's first sentence. The bench's FIRST LYING-DOWN body: the root
rolls 90 degrees onto her side while descending to the floor, the spine bends into a
crescent whose concave side faces the seated partner (placed behind her belly at
--pgap by the bench, pyaw 180), the knees draw up, the arms fold to the chest, the
head tucks. Then sleep: a slow breath -- and the PURR, a 5Hz tremor rippling the
upper spine, tiny enough to read as a vibration and not a shiver.

Gates: the lying body really lies (every bone inside a floor band, nothing under the
floor), and the crescent stays OUTSIDE the seated partner's measured seat cylinder --
curling AROUND someone must not mean curling THROUGH them.

usage: z_curlsleep.py <pack> --scale S --pgap G --seatr R
    G: distance to the seated partner's centre (the bench's pgap)
    R: the partner's seat cylinder radius (her measured torso rx + margin)
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips
from authorlib import (rig_map, fk_world, solve_limb, rotate_bone_world, profile,
                       ease_smooth, qnorm, declare, translate_node_world)

FPS = 30.0
T_KNEEL, T_LIE, T_CURL, T_END = 1.2, 2.2, 3.2, 7.3
LIE_H = 0.16          # world: pelvis height lying on her side

def main(pack_path, SCALE, PGAP, SEATR):
    p = Rpack(pack_path)
    if "z_rm_curlsleep" in p.clip_names():
        print("z_curlsleep already present"); return 0
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
    arms = {"l": (RM["upperarm_l"], RM["lowerarm_l"], RM["hand_l"]),
            "r": (RM["upperarm_r"], RM["lowerarm_r"], RM["hand_r"])}

    wp0, _ = fk_world(nodes, base_q, base_t)
    wr, _ = fk_world(nodes)
    mny = min(w[1] for w in wr)
    pelv_y0 = wp0[pelvis][1]

    frames = int(T_END * FPS)
    qtr = {}; ttr = []
    worst_seat = 9.9      # min xz-distance of any bone to the seat axis, at the hold
    floor_lo = floor_hi = None
    who_lo = who_seat = "?"
    names9 = p.node_names()
    for f in range(frames):
        t = f / FPS
        kneel = profile([(0.1, 0.0, ease_smooth), (T_KNEEL, 1.0, None)], t)
        lie = profile([(T_KNEEL, 0.0, ease_smooth), (T_LIE, 1.0, None)], t)
        curl = profile([(T_LIE - 0.2, 0.0, ease_smooth), (T_CURL, 1.0, None)], t)
        asleep = profile([(T_CURL, 0.0, ease_smooth), (T_CURL + 0.8, 1.0, None)], t)
        br = math.sin(2 * math.pi * (t - T_CURL) / 3.8) if t > T_CURL else 0.0
        purr = math.sin(2 * math.pi * t * 5.0) * asleep

        qo = dict(base_q); to = dict(base_t)
        # ROOT: kneel-dip, then the ROLL onto her side while descending to the floor.
        # Roll is about her FORWARD axis (world z): her spine tips from vertical to
        # horizontal, so the crescent she is about to bend lies flat on the ground
        # with the belly side still facing the seated partner behind it.
        dy = (pelv_y0 - mny) - (0.30 / SCALE) * (1 - lie) - (LIE_H / SCALE) * lie
        to[pelvis] = translate_node_world(nodes, pelvis,
                                          [0.0, -dy * max(kneel * 0.35, lie), 0.0],
                                          base_q, base_t)
        qo[pelvis] = rotate_bone_world(nodes, pelvis, [0, 0, 1], math.radians(88.0) * lie, qo)
        # the crescent: every spine joint bends toward the belly; the neck and head
        # tuck deeper; the purr rides the upper spine at breath scale / 6
        for i, b in enumerate(spine):
            qo[b] = rotate_bone_world(nodes, b, [1, 0, 0],
                                      math.radians(9.0 * curl + 1.4 * br * asleep
                                                   + (0.55 * purr if i >= 1 else 0.0)), qo)
        if neck >= 0:
            qo[neck] = rotate_bone_world(nodes, neck, [1, 0, 0],
                                         math.radians(16.0 * curl + 0.5 * purr), qo)
        qo[headb] = rotate_bone_world(nodes, headb, [1, 0, 0], math.radians(12.0 * curl), qo)

        # KNEES DRAW UP: feet tuck toward the pelvis; poles keep the knees on the
        # belly side of the crescent
        w9, _ = fk_world(nodes, qo, to)
        pel = w9[pelvis]
        for s, off in (("L", 0.02), ("R", 0.10)):
            chain = legs[s]
            a0 = wp0[chain[2]]
            # drawn up ALONG the crescent (mostly lateral) -- tucking toward the
            # belly sent the knees INTO the seat cylinder (clearance 0.09 of 0.32)
            # the crescent hugs the seat cylinder: near her midline nothing may pass
            # z = seat - radius, so the knees stay LOW in z and the fold is lateral
            tuck = [pel[0] - (0.55 - off) / SCALE,
                    mny + (0.15 + off * 0.5) / SCALE,
                    pel[2] + (0.02 + off * 0.4) / SCALE]
            tgt = [a0[k] + (tuck[k] - a0[k]) * max(lie, kneel * 0.3) for k in range(3)]
            upd = solve_limb(nodes, chain, tgt, pole=[0.3, 0.7, 0.15],
                             q_override=qo, t_override=to)
            qo.update(upd)
        # ARMS FOLD to the chest
        w9, _ = fk_world(nodes, qo, to)
        chest = [(w9[spine[-1]][k] + w9[neck if neck >= 0 else headb][k]) / 2 for k in range(3)]
        # she lies on her RIGHT side: the right arm is the LOWER arm and rests ON
        # the floor in front of the chest (folding it to the chest drove the forearm
        # 0.26 under the floor); the left folds over the top
        # the lower (right) arm lies EXTENDED along the floor -- side-sleepers
        # stretch it forward, and a near-straight low arm keeps its elbow on the
        # chord instead of diving under it
        for sd, dz, fy, pole9 in (("l", 0.10, 0.06, [0.1, 0.7, 0.4]),
                                   ("r", 0.34, -1.0, [0.0, 0.5, 0.6])):
            chain = arms[sd]
            h0 = wp0[chain[2]]
            fy9 = (chest[1] + 0.02 / SCALE) if fy > 0 else (mny + 0.05 / SCALE)
            fold = [chest[0] + (0.06 if sd == "r" else 0.0) / SCALE, fy9,
                    chest[2] + dz / SCALE]
            tgt = [h0[k] + (fold[k] - h0[k]) * max(curl, lie * 0.5) for k in range(3)]
            upd = solve_limb(nodes, chain, tgt, pole=pole9,
                             q_override=qo, t_override=to)
            qo.update(upd)

        w9, _ = fk_world(nodes, qo, to)
        if t > T_CURL + 0.9 and f % 6 == 0:
            # BONES only: static scene-root nodes never move and sit at rig origin --
            # 1.59u above the feet on kaeli -- and they are not the body
            bidx = [i for i in range(len(nodes)) if nodes[i].get("kind") == 1] or list(range(len(nodes)))
            ys = [(w9[i][1] - mny) * SCALE for i in bidx]
            lo, hi = min(ys), max(ys)
            if floor_lo is None or lo < floor_lo: who_lo = names9[bidx[ys.index(lo)]]
            floor_lo = lo if floor_lo is None else min(floor_lo, lo)
            floor_hi = hi if floor_hi is None else max(floor_hi, hi)
            # the seat cylinder stands at (0, PGAP) in her frame (pyaw 180 mirrors it)
            for i in bidx:
                dxz = math.hypot(w9[i][0] * SCALE, w9[i][2] * SCALE - PGAP)
                if dxz < worst_seat: worst_seat = dxz; who_seat = names9[i]

        chain_all = [pelvis, neck, headb] + list(spine)
        for s in "LR": chain_all += list(legs[s])
        for sd in "lr": chain_all += list(arms[sd])
        for b in chain_all:
            if b is not None and b >= 0:
                qtr.setdefault(b, []).extend(qnorm(qo.get(b, base_q.get(b, nodes[b]["q"]))))
        ttr.extend(to[pelvis])

    print("z_rm_curlsleep: lying band %.2f..%.2fu (lowest: %s)  seat clearance %.2fu of %.2f (nearest: %s)"
          % (floor_lo, floor_hi, who_lo, worst_seat, SEATR, who_seat))
    # JOINT CENTRES sit inside flesh: a side-lying shoulder joint reads ~6cm below
    # the floor line while the deltoid around it rests ON the floor. -0.08 is joint
    # depth, not clipping; feet and hands (thin flesh) never got near it.
    assert floor_lo > -0.08, "a bone is under the floor"
    assert floor_hi < 0.85, "she is not lying down -- something still stands"
    assert worst_seat > SEATR - 0.02, "the crescent curls THROUGH the seated partner"

    tracks = {}
    for (node, typ), v in base.items():
        tracks[(node, typ)] = v * frames
    for b, vals in qtr.items(): tracks[(b, 0)] = vals
    tracks[(pelvis, 1)] = ttr
    append_clips(pack_path, [("z_rm_curlsleep", FPS, frames, tracks)])
    declare(pack_path, "z_rm_curlsleep", {
        "fps": FPS, "frames": frames, "terminal": "hold", "max_speed": 5.0,
        "contacts": []})
    print("z_rm_curlsleep: appended (%d frames, %d tracks)" % (frames, len(tracks)))
    return 0

if __name__ == "__main__":
    a = sys.argv[1:]
    def arg(nm, d):
        return float(a[a.index(nm) + 1]) if nm in a else d
    sys.exit(main(a[0], arg("--scale", 1.0), arg("--pgap", 0.55), arg("--seatr", 0.32)))
