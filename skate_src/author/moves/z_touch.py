#!/usr/bin/env python3
"""z_touch.py -- P2's gate move: crouch and touch the floor, feet planted throughout.

The first CONTACT-declared move. Three declarations, and each one exercises a different
piece of the P2 machinery:
  - both FEET planted for the whole move  -> author-time grounding: as the pelvis drops,
    both legs are IK-solved every frame so the ankles hold their base positions exactly
  - the right HAND on the floor mid-move  -> arm IK to a world target + a timing window
  - terminal "base"                       -> the move hands back where it started

The pelvis drop is a real ROOT trajectory: a translation override on the pelvis, which
is how authored root motion exists in this format (a type-1 track). Every target is a
proportion of the rig's own measured leg -- nothing absolute (the P2a lesson).

usage: z_touch.py <moves-pack>
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips
from authorlib import (fk_world, solve_limb, rotate_bone_world, translate_node_world,
                       hold_world_rotation, profile, ease_smooth, qnorm, declare)

DUR, FPS = 2.2, 30.0

def main(pack_path):
    p = Rpack(pack_path)
    if "z_touch" in p.clip_names():
        print("z_touch already present"); return 0
    nodes = p.nodes()
    names = p.node_names()
    idle = p.clip_names().index("a_idle")

    base_q, base_t, base = {}, {}, {}
    for (node, typ, data) in p.tracks(idle):
        comp = 4 if typ == 0 else 3
        v = list(struct.unpack_from("<%df" % comp, data, 0))
        base[(node, typ)] = v
        if typ == 0: base_q[node] = v
        elif typ == 1: base_t[node] = v

    pelvis = names.index("pelvis")
    spine = [names.index(n) for n in ("spine_01", "spine_02", "spine_03")]
    legL = tuple(names.index(n) for n in ("thigh_l", "calf_l", "foot_l"))
    legR = tuple(names.index(n) for n in ("thigh_r", "calf_r", "foot_r"))
    armR = tuple(names.index(n) for n in ("upperarm_r", "lowerarm_r", "hand_r"))
    foot, ball = names.index("foot_r"), names.index("ball_r")

    wp0, wq0 = fk_world(nodes, base_q, base_t)
    L = (math.dist(wp0[legL[0]], wp0[legL[1]]) + math.dist(wp0[legL[1]], wp0[legL[2]]))
    ankL0, ankR0 = list(wp0[legL[2]]), list(wp0[legR[2]])
    # THE FLOOR IS WHERE THE TOES ARE. The first declaration used ankle height as
    # "floor" -- and the ankle JOINT sits ~10 rig-cm above the sole, so the engine
    # faithfully played a touch that stopped short of the ground. movecheck caught a
    # WRONG DECLARATION, not a wrong playback: elbow 116.3 deg on both sides.
    ballL, ballR = names.index("ball_l"), names.index("ball_r")
    floorY = min(wp0[ballL][1], wp0[ballR][1])
    lat = [wp0[legL[0]][k] - wp0[legR[0]][k] for k in range(3)]
    fwd = [wp0[ball][k] - wp0[foot][k] for k in range(3)]; fwd[1] = 0.0
    fl = math.hypot(fwd[0], fwd[2]); fwd = [fwd[0] / fl, 0.0, fwd[2] / fl]

    # the floor point the hand touches: forward and a little to the right, at ankle height
    touch = [ (ankL0[0] + ankR0[0]) / 2 + fwd[0] * 0.30 * L + (wp0[armR[0]][0] - wp0[pelvis][0]) * 0.5,
              floorY,
              (ankL0[2] + ankR0[2]) / 2 + fwd[2] * 0.30 * L ]
    armLen = (math.dist(wp0[armR[0]], wp0[armR[1]]) + math.dist(wp0[armR[1]], wp0[armR[2]]))
    DROP = 0.45 * L

    def crouch_pose(cr, fold_deg):
        """Drop + HIP HINGE. Spine-only folding can never reach the floor here --
        measured: shoulder starts 1.64 legs up with a 0.69-leg arm, and even 135 deg of
        spine drops it to ~0.8 legs. Humans toe-touch by hinging at the HIPS: the pelvis
        takes 55% of the fold (its child legs are re-solved to keep the feet planted),
        the three spine joints share the rest."""
        qo = dict(base_q); to = dict(base_t)
        to[pelvis] = translate_node_world(nodes, pelvis, [0.0, -DROP * cr, 0.0], base_q, base_t)
        a = math.radians(fold_deg) * cr
        qo[pelvis] = rotate_bone_world(nodes, pelvis, lat, a * 0.55, qo)
        for b in spine:
            qo[b] = rotate_bone_world(nodes, b, lat, a * 0.15, qo)
        return qo, to

    # SOLVE THE FOLD (author-to-the-metric, as the bow): total fold angle such that at
    # full crouch the SHOULDER is within 0.85 arm-reach (the arm IK closes the rest; a straight-arm reach is how a real toe-touch looks) of the touch point. Sign audited
    # by which direction brings it closer.
    def sh_dist(fold):
        qo, to = crouch_pose(1.0, fold)
        w, _ = fk_world(nodes, qo, to)
        return math.dist(w[armR[0]], touch)
    sign = 1.0 if sh_dist(40.0) < sh_dist(-40.0) else -1.0
    lo, hi = 0.0, 150.0
    if sh_dist(sign * hi) > 0.85 * armLen:
        raise AssertionError("even a 150deg hip fold cannot reach the touch point")
    for _ in range(40):
        mid = (lo + hi) / 2
        if sh_dist(sign * mid) > 0.85 * armLen: lo = mid
        else: hi = mid
    PITCH = sign * (lo + hi) / 2
    print("z_touch: fold solved at %.1f deg total (shoulder-to-touch %.2f = %.2f arm)"
          % (PITCH, sh_dist(PITCH), sh_dist(PITCH) / armLen))

    crouch = [(0.00, 0.0, ease_smooth), (0.60, 1.0, None),
              (1.40, 1.0, ease_smooth), (2.10, 0.0, None)]
    reach  = [(0.30, 0.0, ease_smooth), (0.62, 1.0, None),
              (1.35, 1.0, ease_smooth), (1.75, 0.0, None)]

    frames = int(DUR * FPS)
    qtr = {}; ttr = {pelvis: []}; ptr = []
    worst_foot, worst_hand = 0.0, 0.0
    for f in range(frames):
        t = f / FPS
        cr, rc = profile(crouch, t), profile(reach, t)
        qo, to = crouch_pose(cr, PITCH)
        # legs: hold both ankles at their base positions -- author-time grounding --
        # then hold each FOOT's world orientation flat (movecheck's first catch: pinning
        # the ankle is not pinning the foot; bent knees plunged the toes through the
        # floor and the engine's floor clamp lifted the whole body off its contacts)
        for chain, tgt in ((legL, ankL0), (legR, ankR0)):
            qo.update(solve_limb(nodes, chain, tgt, q_override=qo, t_override=to))
            qo[chain[2]] = hold_world_rotation(nodes, chain[2], wq0[chain[2]], qo, to)
        # arm: blend the hand from where it hangs to the touch point
        if rc > 1e-4:
            w, _ = fk_world(nodes, qo, to)
            hang = w[armR[2]]
            tgt = [hang[k] + (touch[k] - hang[k]) * rc for k in range(3)]
            qo.update(solve_limb(nodes, armR, tgt, q_override=qo, t_override=to))
        # author-time assertions -- the score is the test
        w, _ = fk_world(nodes, qo, to)
        worst_foot = max(worst_foot, math.dist(w[legL[2]], ankL0), math.dist(w[legR[2]], ankR0))
        if rc > 0.999:
            worst_hand = max(worst_hand, math.dist(w[armR[2]], touch))
        for b in spine: qtr.setdefault(b, []).extend(qnorm(qo[b]))
        for b in (legL[0], legL[1], legL[2], legR[0], legR[1], legR[2], armR[0], armR[1]):
            qtr.setdefault(b, []).extend(qnorm(qo[b]))
        ttr[pelvis].extend(to[pelvis])
        ptr.extend(qnorm(qo[pelvis]))

    print("z_touch: author-time worst foot drift %.5f L, hand err at hold %.5f L"
          % (worst_foot / L, worst_hand / L))
    assert worst_foot / L < 0.002 and worst_hand / L < 0.002

    tracks = {}
    for (node, typ), v in base.items():
        tracks[(node, typ)] = v * frames
    for b, vals in qtr.items(): tracks[(b, 0)] = vals
    tracks[(pelvis, 1)] = ttr[pelvis]
    tracks[(pelvis, 0)] = ptr
    append_clips(pack_path, [("z_touch", FPS, frames, tracks)])

    declare(pack_path, "z_touch", {
        "fps": FPS, "frames": frames, "terminal": "base",
        "contacts": [
            {"part": "fL", "t": [0.0, DUR], "floor": True, "still_tol": 0.03, "floor_tol": 0.06},
            {"part": "fR", "t": [0.0, DUR], "floor": True, "still_tol": 0.03, "floor_tol": 0.06},
            {"part": "grp", "t": [0.72, 1.30], "floor": True, "still_tol": 0.03, "floor_tol": 0.10},
        ]})
    print("z_touch: appended (%d frames, %d tracks) + declaration" % (frames, len(tracks)))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
