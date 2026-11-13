#!/usr/bin/env python3
"""z_rm_hop.py -- P3's gate move: a small hop. Dip, leave the ground, land, recover.

The name is the declaration: the z_rm_ prefix tells the engine this clip OWNS the
root's Y -- the ground offset freezes at the stand value and the pelvis translation
track does the flying. Under the old auto-ground this exact clip would CANCEL (hips
rise, ankles rise, model drops: net zero); the gate is that the feet measurably leave
the floor in the air window. Frame 0 must be a stand-equivalent pose: the engine
captures its frozen ground from the first frame.

usage: z_rm_hop.py <moves-pack>
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips
from authorlib import (fk_world, solve_limb, translate_node_world, hold_world_rotation,
                       profile, ease_smooth, qnorm, declare)

DUR, FPS = 1.8, 30.0

def main(pack_path):
    p = Rpack(pack_path)
    if "z_rm_hop" in p.clip_names():
        print("z_rm_hop already present"); return 0
    nodes = p.nodes(); names = p.node_names()
    idle = p.clip_names().index("a_idle")
    base_q, base_t, base = {}, {}, {}
    for (node, typ, data) in p.tracks(idle):
        comp = 4 if typ == 0 else 3
        v = list(struct.unpack_from("<%df" % comp, data, 0))
        base[(node, typ)] = v
        if typ == 0: base_q[node] = v
        elif typ == 1: base_t[node] = v

    pelvis = names.index("pelvis")
    legL = tuple(names.index(n) for n in ("thigh_l", "calf_l", "foot_l"))
    legR = tuple(names.index(n) for n in ("thigh_r", "calf_r", "foot_r"))
    wp0, wq0 = fk_world(nodes, base_q, base_t)
    L = math.dist(wp0[legL[0]], wp0[legL[1]]) + math.dist(wp0[legL[1]], wp0[legL[2]])
    ankL0, ankR0 = list(wp0[legL[2]]), list(wp0[legR[2]])

    # pelvis height profile, in legs relative to base: dip, fly, land-dip, recover
    # 0.4s of held base first: the lab's entry crossfade (0.35s) lands on stillness,
    # so movecheck's head reference and the dip window stay clean
    hp = [(0.00,  0.00, ease_smooth), (0.40,  0.00, ease_smooth), (0.62, -0.12, None),
          (0.82, +0.28, ease_smooth), (0.95, +0.30, None),
          (1.20, -0.10, ease_smooth), (1.35, -0.08, None),
          (1.75,  0.00, None)]
    air = [(0.00, 0.0, ease_smooth), (0.70, 0.0, None), (0.82, 1.0, ease_smooth),
           (1.06, 1.0, None), (1.18, 0.0, ease_smooth), (1.75, 0.0, None)]

    frames = int(DUR * FPS)
    qtr = {}; ttr = []
    apex_clearL = 9e9
    for f in range(frames):
        t = f / FPS
        h, av = profile(hp, t), profile(air, t)
        qo = dict(base_q); to = dict(base_t)
        to[pelvis] = translate_node_world(nodes, pelvis, [0.0, h * L, 0.0], base_q, base_t)
        # ONE continuous formula for the feet (the branch version popped 0.216u at the
        # pin->tuck switch): target = pinned spot blended toward ride-with-body + tuck
        for chain, tgt in ((legL, ankL0), (legR, ankR0)):
            fly = [tgt[0], tgt[1] + (h + 0.10) * L, tgt[2]]
            tg = [tgt[k] + (fly[k] - tgt[k]) * av for k in range(3)]
            qo.update(solve_limb(nodes, chain, tg, q_override=qo, t_override=to))
            if av < 0.5:
                qo[chain[2]] = hold_world_rotation(nodes, chain[2], wq0[chain[2]], qo, to)
        w, _ = fk_world(nodes, qo, to)
        if 0.85 <= t <= 1.02:
            apex_clearL = min(apex_clearL,
                              (min(w[legL[2]][1], w[legR[2]][1]) - min(ankL0[1], ankR0[1])) / L)
        for b in (legL + legR):
            qtr.setdefault(b, []).extend(qnorm(qo[b]))
        ttr.extend(to[pelvis])

    print("z_rm_hop: authored apex foot clearance %.3f L" % apex_clearL)
    assert apex_clearL > 0.15, "the hop never leaves the ground at author time"

    tracks = {}
    for (node, typ), v in base.items():
        tracks[(node, typ)] = v * frames
    for b, vals in qtr.items(): tracks[(b, 0)] = vals
    tracks[(pelvis, 1)] = ttr
    append_clips(pack_path, [("z_rm_hop", FPS, frames, tracks)])
    declare(pack_path, "z_rm_hop", {
        "fps": FPS, "frames": frames, "terminal": "base",
        "contacts": [
            {"part": "fL", "t": [0.00, 0.66], "floor": True, "still_tol": 0.04},
            {"part": "fR", "t": [0.00, 0.66], "floor": True, "still_tol": 0.04},
            {"part": "fL", "t": [0.85, 1.02], "min_y": 0.10},
            {"part": "fR", "t": [0.85, 1.02], "min_y": 0.10},
            {"part": "fL", "t": [1.40, 1.80], "floor": True, "still_tol": 0.05},
            {"part": "fR", "t": [1.40, 1.80], "floor": True, "still_tol": 0.05},
        ]})
    print("z_rm_hop: appended (%d frames, %d tracks) + declaration" % (frames, len(tracks)))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
