#!/usr/bin/env python3
"""z_rm_skiphand.py -- P8: SKIPPING HAND IN HAND. Movement + a SUSTAINED contact.

jacob: "lets try some movement now.. lets see reva skipping along hand in hand with
kaeli."

Every inter-body contact so far was a MOMENT (the five) or a held POSE (the hug). This
is the third class: two bodies TRAVELING with root motion while one contact point --
the held hands -- must hold on every frame of the journey. The resolution is the same
trick that made the five land: both bodies author toward the same WORLD-SPACE curve.
The meet point rides the shared travel, swings fore-and-aft with the skip, and each
body solves its INNER arm to it each frame from its own proportions. If both scores
integrate the same travel and the same swing, the hands coincide for the whole clip --
verified in flight by the engine's [hold] meter, which judges a hold by its WORST gap.

THE SKIP. A step-hop on alternating feet, da-DUM da-DUM: each half-cycle one leg
supports (plants, pushes, hops, lands on the SAME foot) while the other drives its
knee up through the air, then they trade. Feet land on analytic per-step spots (the
crawl's closure-by-construction), the pelvis bounces twice per cycle, and travel is
strictly monotonic -- skipping goes somewhere.

Sidedness: the hero (reva) has the partner on her RIGHT (bench pside NEGATIVE), so her
inner arm is the RIGHT; the partner (kaeli) mirrors with her LEFT. --inner picks.

usage: z_rm_skiphand.py <pack> --scale S --arm A --inner r|l --sidegap G --meeth H
    S: the engine's scale for this pack
    A: this body's arm length, world units
    G: the pair's lateral separation (bench pside magnitude); the meet point sits at
       G/2 toward the partner
    H: the held hands' height, world units (must be reachable by BOTH arms)
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips
from authorlib import (rig_map, fk_world, solve_limb, rotate_bone_world,
                       hold_world_rotation, profile, ease_smooth, qnorm, declare,
                       translate_node_world)

FPS = 60.0
T_SET, T_CYC, N_CYC, T_OUT = 0.70, 0.80, 3, 0.70
DUR = T_SET + N_CYC * T_CYC + T_OUT
SPEED = 0.85          # world units/second of travel during the skip
BOUNCE = 0.055        # world bounce amplitude (two per cycle)
KNEE_LIFT = 0.30      # world: how high the swing foot arcs
SWING = 0.16          # world: fore-aft swing of the held hands

def _n(v):
    l = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / l for x in v]

def main(pack_path, SCALE, ARMW, INNER, SIDEGAP, MEETH):
    p = Rpack(pack_path)
    if "z_rm_skiphand" in p.clip_names():
        print("z_rm_skiphand already present"); return 0
    nodes = p.nodes()
    RM = rig_map(p)
    assert not RM["_missing"], "rig incomplete: %s" % RM["_missing"]
    names_c = p.clip_names()
    base_clip = "z_upright" if "z_upright" in names_c else RM["_base_clip"]
    base_q, base_t, base = {}, {}, {}
    ci = names_c.index(base_clip)
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
    balls = {"L": RM["ball_l"], "R": RM["ball_r"]}
    inner = arms[INNER]
    outer = arms["l" if INNER == "r" else "r"]
    # the meet point is toward the PARTNER: inner=r means she is on my right (-x)
    meet_x_w = -SIDEGAP / 2.0 if INNER == "r" else +SIDEGAP / 2.0

    wp0, wq0 = fk_world(nodes, base_q, base_t)
    wr, _ = fk_world(nodes)
    bones = list(range(len(nodes)))
    mny = min(wr[i][1] for i in bones)
    footY = min(wp0[legs["L"][2]][1], wp0[legs["R"][2]][1])
    ankles = {s: list(wp0[legs[s][2]]) for s in "LR"}
    hips_y0 = wp0[pelvis][1]

    # the meet point must be reachable at the FARTHEST swing
    sh_in = wp0[inner[0]]
    worst = math.dist(sh_in, [meet_x_w / SCALE, mny + MEETH / SCALE,
                              (0.10 + SWING) / SCALE]) * SCALE
    print("z_rm_skiphand: inner=%s  meet reach at worst swing %.2fu of %.2fu arm"
          % (INNER, worst, ARMW))
    assert worst <= 0.92 * ARMW, "the held hand is past this arm -- lower it or narrow the pair"

    TRAV_CYC = SPEED * T_CYC          # world travel per cycle
    def travel_w(t):                  # world travel, monotone, eased at both ends
        t0, t1 = T_SET, T_SET + N_CYC * T_CYC
        if t < t0: return 0.0
        if t > t1: return N_CYC * TRAV_CYC
        return (t - t0) * SPEED

    # analytic foot spots: the support foot of half-cycle k plants at its own share of
    # the travel; L supports even halves, R odd
    def spot(side, k):
        base_a = ankles[side]
        return [base_a[0], footY, base_a[2] + (travel_w(T_SET) + (k + 0.5) * TRAV_CYC / 2.0) / SCALE]

    frames = int(DUR * FPS)
    qtr = {}; ttr = []
    checksP = []
    for f in range(frames):
        t = f / FPS
        t0, t1 = T_SET, T_SET + N_CYC * T_CYC
        inskip = t0 <= t <= t1
        u = (t - t0) / (T_CYC / 2.0) if inskip else 0.0     # half-cycles
        k = int(u); ph = u - k                              # phase inside this half
        cr = profile([(0.10, 0.0, ease_smooth), (T_SET * 0.9, 1.0, None)], t) if t < t0 else \
             (profile([(t1 + 0.05, 1.0, ease_smooth), (DUR - 0.10, 0.0, None)], t) if t > t1 else 1.0)

        trav = travel_w(t) / SCALE
        # the BOUNCE: loaded at the step (ph 0.15), apex mid-hop (ph 0.72)
        bnc = (math.sin(math.pi * ((ph - 0.15) / 0.57)) if 0.15 <= ph <= 0.72 else 0.0) if inskip else 0.0
        dy = (BOUNCE / SCALE) * bnc * cr
        qo = dict(base_q); to = dict(base_t)
        # ROOT: forward + bounce, small upright lean-back (the skip posture)
        # translate in WORLD axes conjugated through the parent -- raw local t
        # assumes the root frame aligns with the world, which a UE scene root's
        # rotation breaks (the crawl learned this first)
        to[pelvis] = translate_node_world(nodes, pelvis, [0.0, dy, trav], base_q, base_t)
        qo[pelvis] = rotate_bone_world(nodes, pelvis, [1, 0, 0], math.radians(-3.0) * cr, qo)
        for b in spine:
            qo[b] = rotate_bone_world(nodes, b, [1, 0, 0], math.radians(-1.2) * cr, qo)

        # LEGS: support plants at its spot; swing arcs to the NEXT spot with the knee up
        for s in "LR":
            sup_half = (k % 2 == 0) == (s == "L")           # L supports even halves
            chain = legs[s]
            if not inskip:
                tgt = [ankles[s][0], footY, ankles[s][2] + trav]
            elif sup_half:
                sp = spot(s, k)
                if ph < 0.55: tgt = sp                       # planted: step + push
                elif ph < 0.85:                              # the hop: same foot, small air arc
                    h9 = math.sin(math.pi * (ph - 0.55) / 0.30)
                    tgt = [sp[0], footY + (0.10 / SCALE) * h9, sp[2] + (0.06 / SCALE) * (ph - 0.55) / 0.30]
                else: tgt = [sp[0], footY, sp[2] + 0.06 / SCALE]
            else:
                a9 = spot(s, k - 1); b9 = spot(s, k + 1)     # swing: prev spot to next
                sw = min(1.0, ph / 0.9)
                tgt = [a9[0] + (b9[0] - a9[0]) * sw,
                       footY + (KNEE_LIFT / SCALE) * math.sin(math.pi * sw) * 0.55,
                       a9[2] + (b9[2] - a9[2]) * sw]
            upd = solve_limb(nodes, chain, tgt, pole=[0, -0.3, 1.0], q_override=qo, t_override=to)
            qo.update(upd)
            bl = balls[s]
            if bl >= 0 and inskip and not sup_half:          # toes point in the air
                qo[bl] = rotate_bone_world(nodes, bl, [1, 0, 0], math.radians(18.0) * math.sin(math.pi * min(1.0, ph / 0.9)), qo)

        # THE HELD HAND: the meet point rides the SHARED world curve -- travel plus the
        # fore-aft swing, one formula in both bodies' scores
        sw_w = SWING * math.sin(2 * math.pi * (t - t0) / T_CYC) * cr if t > 0.1 else 0.0
        # anchored to the BODY ORIGIN both scores share through the bench placement --
        # anchoring to each body's own shoulder left a 0.11u constant gap between the
        # two curves (their shoulders sit at different rest z)
        meet = [meet_x_w / SCALE,
                mny + (MEETH + 0.03 * bnc) / SCALE,
                (0.10 + travel_w(t) + sw_w) / SCALE]
        rest_in = wp0[inner[2]]
        tgt_in = [rest_in[0] + (meet[0] - rest_in[0]) * cr,
                  rest_in[1] + (meet[1] - rest_in[1]) * cr,
                  rest_in[2] + trav + (meet[2] - rest_in[2] - trav) * cr]
        upd = solve_limb(nodes, inner, tgt_in,
                         pole=[(-1.0 if INNER == "r" else 1.0) * 0.4, -1.0, 0.15],
                         q_override=qo, t_override=to)
        qo.update(upd)
        # OUTER arm swings with the opposite phase, shoulder-led
        oa = outer[0]
        qo[oa] = rotate_bone_world(nodes, oa, [1, 0, 0],
                                   math.radians(22.0) * math.sin(2 * math.pi * (t - t0) / T_CYC + math.pi) * cr, qo)

        w9, _ = fk_world(nodes, qo, to)
        if inskip and f % 7 == 0:
            err = math.dist(w9[inner[2]], meet) * SCALE
            checksP.append(err)

        chain_all = [pelvis, neck, headb] + list(spine)
        for s in "LR": chain_all += list(legs[s]) + ([balls[s]] if balls[s] >= 0 else [])
        chain_all += list(inner) + list(outer)
        for b in chain_all:
            if b is not None and b >= 0:
                qtr.setdefault(b, []).extend(qnorm(qo.get(b, base_q.get(b, nodes[b]["q"]))))
        ttr.extend(to[pelvis])

    worst_hold = max(checksP) if checksP else 9.9
    print("z_rm_skiphand: hand-on-curve worst %.3fu over %d samples (want <= 0.050)"
          % (worst_hold, len(checksP)))
    assert worst_hold <= 0.050, "the held hand leaves the shared curve"

    tracks = {}
    for (node, typ), v in base.items():
        tracks[(node, typ)] = v * frames
    for b, vals in qtr.items(): tracks[(b, 0)] = vals
    tracks[(pelvis, 1)] = ttr
    append_clips(pack_path, [("z_rm_skiphand", FPS, frames, tracks)])
    t0, t1 = T_SET, T_SET + N_CYC * T_CYC
    declare(pack_path, "z_rm_skiphand", {
        "fps": FPS, "frames": frames, "terminal": "base", "max_speed": 16.0,
        "periodic": {"t0": t0 + 0.1, "t1": t1 - 0.1, "period": T_CYC},
        "contacts": [
            {"part": "fL", "t": [0.0, 0.30], "floor": True, "still_tol": 0.05},
            {"part": "fL", "t": [t0 + 0.05, t0 + 0.20], "floor": True, "still_tol": 0.06},
        ]})
    print("z_rm_skiphand: appended (%d frames, %d tracks)" % (frames, len(tracks)))
    return 0

if __name__ == "__main__":
    a = sys.argv[1:]
    def arg(nm, d):
        return float(a[a.index(nm) + 1]) if nm in a else d
    inn = a[a.index("--inner") + 1] if "--inner" in a else "r"
    sys.exit(main(a[0], arg("--scale", 1.0), arg("--arm", 1.0), inn,
                  arg("--sidegap", 0.52), arg("--meeth", 1.75)))
