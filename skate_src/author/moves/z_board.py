#!/usr/bin/env python3
"""z_board.py -- ROBOT NEKO ep 9: picking a skateboard up off the floor, and putting it down.

jacob, after ep 8: "no one made the animation to bend over and pick the tv up". Nobody in the
club could take anything off the ground -- every held prop was simply in a hand after a cut.
These are the verbs, authored from each rig's own proportions (rig_map), so the same score is
reva's pickup and kaeli's:

  z_board_reach   stand -> squat and hinge, right hand down onto the board -- HOLD
  z_board_lift    hand on the board -> rise with it into the carry -- HOLD
  z_board_carry   the carry stand (a line's clip, or a `pover=` layer over a walk)
  z_board_lower   the carry -> down again, the board back onto the floor -- HOLD
  z_board_rise    hand empty at the floor -> back up to the stand -- HOLD

SPLIT AT THE GRASP on purpose: until the line that plays z_board_lift, the board is a prop on a
MARK (`pprop=skateboard@1.4@0@m_board`); that line puts it `@hand`. The swap is a line boundary,
cut on the action. Every clip HOLDS its last pose for the rest of its four seconds, because a
scene loops a clip and a pickup must not happen twice in one line (z_armsup's convention).

THE BOARD'S SPOT IS DERIVED, never placed by eye: REACH_F forward and REACH_R right of the feet,
in the rig's own leg lengths, on its floor. The script prints the same spot in WORLD units for
the body's engine scale, which is where the set's board mark goes.

Composed over REST with z_upright's relaxed arms, so it needs no base clip -- anima_outfit ships
exactly one clip (z_rest). The feet stay planted (both legs IK'd to their stand ankles every
frame, feet held flat -- z_touch's lesson), the HIPS hinge (spine-only folding never reaches
the floor), the knees bend FORWARD (an explicit pole: a stand's legs are straight, and a
straight chain has no plane of its own to bend in). The score asserts its own contract.

⛔ THE FIRST RUN FAILED ITS OWN ASSERT ON BOTH RIGS, AND THE ASSERT WAS RIGHT. A half squat
(pelvis down 0.40 legs) folded 140 degrees carries the shoulder FORWARD, not down: reva's right
shoulder ended at y 0.61 over a grip at 0.16 -- 0.45 away against a 0.44 limit. People do not
pick things up by bowing from a half squat; they squat DEEP and fold less. And the reach test
only checked the fold's endpoint, when the distance can bottom out before it -- the fold is now
scanned for its closest approach first, and bisected below that.

usage: z_board.py <pack>
"""
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips
from authorlib import (rig_map, fk_world, aim_bone, solve_limb, rotate_bone_world,
                       translate_node_world, hold_world_rotation, profile, ease_smooth, qnorm)

FPS = 30.0
DUR = 4.0                                   # every clip: the move, then a hold
HANG, ELBOW_OUT, ELBOW_FWD = 14.0, 16.0, 8.0          # z_upright's relaxed arms
REACH_F, REACH_R = 0.36, 0.18               # the board: forward / right of the feet, in LEGS
BOARD_TOP = 0.11                            # rig units: the deck of a 1.4u board beside a 3.45u body
DROP, BACK = 0.50, 0.14                     # at the bottom the pelvis sinks, and sits back, in LEGS
CARRY_OUT, CARRY_FWD = 0.20, 0.06           # the carry: hand out from the right hip / forward, in LEGS
SHOULDER_REACH = 0.85                       # the fold is solved until the shoulder is this many arms from the board


def stand_q(nodes, RM):
    q = {}
    for side, sx in (("l", 1.0), ("r", -1.0)):
        ua, la, ha = RM["upperarm_" + side], RM["lowerarm_" + side], RM["hand_" + side]
        a = math.radians(HANG)
        q[ua] = aim_bone(nodes, ua, la, [math.sin(a) * sx, -math.cos(a), 0.0], q)
        b = math.radians(ELBOW_OUT)
        q[la] = aim_bone(nodes, la, ha, [math.sin(b) * sx, -math.cos(b), math.sin(math.radians(ELBOW_FWD))], q)
    return q


def flat(v):
    v = [v[0], 0.0, v[2]]
    n = math.hypot(v[0], v[2])
    return [v[0] / n, 0.0, v[2] / n]


def lerp(a, b, u):
    return [a[k] + (b[k] - a[k]) * u for k in range(3)]


def main(pack_path):
    p = Rpack(pack_path)
    nodes = p.nodes()
    RM = rig_map(p)
    assert not RM["_missing"], "rig incomplete: %s" % RM["_missing"]
    have = set(p.clip_names())

    base_q = stand_q(nodes, RM)
    pel = RM["pelvis"]
    spine = [RM["spine_01"], RM["spine_02"], RM["spine_03"]]
    legL = (RM["thigh_l"], RM["calf_l"], RM["foot_l"])
    legR = (RM["thigh_r"], RM["calf_r"], RM["foot_r"])
    armR = (RM["upperarm_r"], RM["lowerarm_r"], RM["hand_r"])

    wp0, wq0 = fk_world(nodes, base_q)
    L = math.dist(wp0[legL[0]], wp0[legL[1]]) + math.dist(wp0[legL[1]], wp0[legL[2]])
    armLen = math.dist(wp0[armR[0]], wp0[armR[1]]) + math.dist(wp0[armR[1]], wp0[armR[2]])
    ankL0, ankR0 = list(wp0[legL[2]]), list(wp0[legR[2]])
    floorY = min(wp0[RM["ball_l"]][1], wp0[RM["ball_r"]][1])
    lat = flat([wp0[legL[0]][k] - wp0[legR[0]][k] for k in range(3)])       # toward her LEFT
    fwd = flat([wp0[RM["ball_r"]][k] - wp0[RM["foot_r"]][k] for k in range(3)])
    right = [-lat[0], 0.0, -lat[2]]
    mid = [(ankL0[k] + ankR0[k]) / 2 for k in range(3)]
    head0, hip0 = wp0[RM["head"]][1], wp0[pel][1]
    assert head0 > hip0 > floorY, "this rest pose is not a stand"

    board = [mid[0] + (fwd[0] * REACH_F + right[0] * REACH_R) * L, floorY + BOARD_TOP,
             mid[2] + (fwd[2] * REACH_F + right[2] * REACH_R) * L]
    grip = [board[0], board[1] + 0.03, board[2]]                              # the wrist just over the deck
    hipR = wp0[legR[0]]
    carry = [hipR[0] + (right[0] * CARRY_OUT + fwd[0] * CARRY_FWD) * L, hipR[1],
             hipR[2] + (right[2] * CARRY_OUT + fwd[2] * CARRY_FWD) * L]

    def crouch(cr, fold):
        qo = dict(base_q); to = {}
        to[pel] = translate_node_world(nodes, pel, [-fwd[0] * BACK * L * cr, -DROP * L * cr, -fwd[2] * BACK * L * cr])
        a = math.radians(fold) * cr
        qo[pel] = rotate_bone_world(nodes, pel, lat, a * 0.55, qo)
        for b in spine:
            qo[b] = rotate_bone_world(nodes, b, lat, a * 0.15, qo)
        return qo, to

    def sh_dist(fold):
        qo, to = crouch(1.0, fold)
        w, _ = fk_world(nodes, qo, to)
        return math.dist(w[armR[0]], grip)

    # SOLVE THE FOLD to the metric (z_bow / z_touch): the sign by which way brings the shoulder
    # closer; then SCAN for the closest approach (the distance is not monotone out to 150 --
    # fold far enough and the shoulder passes the board), and bisect below it
    sign = 1.0 if sh_dist(40.0) < sh_dist(-40.0) else -1.0
    scan = [(d, sh_dist(sign * d)) for d in range(0, 151, 5)]
    best_d, best = min(scan, key=lambda e: e[1])
    assert best <= SHOULDER_REACH * armLen, (
        "the board is out of reach: the closest the shoulder gets is %.2f arms, folded %d degrees"
        % (best / armLen, best_d))
    lo, hi = 0.0, float(best_d)
    for _ in range(40):
        m = (lo + hi) / 2
        if sh_dist(sign * m) > SHOULDER_REACH * armLen: lo = m
        else: hi = m
    FOLD = sign * (lo + hi) / 2
    print("z_board: leg %.3f arm %.3f (rig u) -- closest approach %.2f arms at %d deg; fold solved at %.1f deg"
          % (L, armLen, best / armLen, best_d, FOLD))

    knee_pole = fwd                                                           # knees bend forward
    elbow_pole = [-fwd[0] + right[0] * 0.6, 0.0, -fwd[2] + right[2] * 0.6]    # right elbow back and out
    elbowL_pole = [-fwd[0] + lat[0] * 0.6, 0.0, -fwd[2] + lat[2] * 0.6]       # left elbow back and out
    armL = (RM["upperarm_l"], RM["lowerarm_l"], RM["hand_l"])
    armLenL = math.dist(wp0[armL[0]], wp0[armL[1]]) + math.dist(wp0[armL[1]], wp0[armL[2]])

    def world_hang(w, side):
        """⛔ THE ARMS HANG IN THE WORLD, NOT IN THE TORSO. The first bench, on both rigs: the
        stand's relaxed arms were LOCAL rotations, so a 120-degree fold swung the free arm up
        behind her like a diver. A hanging arm points at the floor from wherever the shoulder
        has got to."""
        s, n, ua = (1.0, armLenL, armL[0]) if side == "l" else (-1.0, armLen, armR[0])
        a = math.radians(HANG)
        d = [lat[0] * math.sin(a) * s, -math.cos(a), lat[2] * math.sin(a) * s]
        return [w[ua][k] + d[k] * n * 0.97 for k in range(3)]

    def pose(cr, hand_target):
        qo, to = crouch(cr, FOLD)
        for chain, tgt in ((legL, ankL0), (legR, ankR0)):
            qo.update(solve_limb(nodes, chain, tgt, pole=knee_pole, q_override=qo, t_override=to))
            qo[chain[2]] = hold_world_rotation(nodes, chain[2], wq0[chain[2]], qo, to)
        # the free LEFT hand hangs while she stands and BRACES ON HER LEFT KNEE in the squat
        w, _ = fk_world(nodes, qo, to)
        kn = w[legL[1]]
        brace = [kn[0] + (fwd[0] * 0.08 + lat[0] * 0.03) * L, kn[1] + 0.07 * L, kn[2] + (fwd[2] * 0.08 + lat[2] * 0.03) * L]
        qo.update(solve_limb(nodes, armL, lerp(world_hang(w, "l"), brace, cr), pole=elbowL_pole, q_override=qo, t_override=to))
        if hand_target is not None:
            qo.update(solve_limb(nodes, armR, hand_target, pole=elbow_pole, q_override=qo, t_override=to))
        return qo, to

    def hang_hand(cr):
        qo, to = crouch(cr, FOLD)
        w, _ = fk_world(nodes, qo, to)
        return world_hang(w, "r")

    down = [(0.00, 0.0, ease_smooth), (0.25, 0.0, ease_smooth), (1.15, 1.0, None)]
    up = [(0.00, 1.0, ease_smooth), (0.15, 1.0, ease_smooth), (1.05, 0.0, None)]
    moves = {
        # name: (crouch profile, hand target as a function of (t, cr), what the hold must hit)
        "z_board_reach": (down, lambda t, cr: lerp(hang_hand(cr), grip, profile([(0.45, 0.0, ease_smooth), (1.15, 1.0, None)], t)), grip),
        "z_board_lift":  (up,   lambda t, cr: lerp(grip, carry, profile([(0.15, 0.0, ease_smooth), (1.05, 1.0, None)], t)), carry),
        "z_board_carry": ([(0.0, 0.0, None)], lambda t, cr: carry, carry),
        "z_board_lower": ([(0.00, 0.0, ease_smooth), (0.15, 0.0, ease_smooth), (1.05, 1.0, None)],
                          lambda t, cr: lerp(carry, grip, profile([(0.15, 0.0, ease_smooth), (1.05, 1.0, None)], t)), grip),
        "z_board_rise":  (up,   lambda t, cr: lerp(grip, hang_hand(cr), profile([(0.15, 0.0, ease_smooth), (0.85, 1.0, None)], t)), None),
    }

    out = []
    for name, (crp, hand_fn, must) in moves.items():
        if name in have:
            print("z_board: %s already present" % name); continue
        dur = 2.0 if name == "z_board_carry" else DUR
        frames = int(dur * FPS)
        qtr = {}; ttr = []
        worst_foot = 0.0; hold_err = 0.0
        for f in range(frames):
            tt = f / FPS
            cr = profile(crp, tt)
            qo, to = pose(cr, hand_fn(tt, cr))
            w, _ = fk_world(nodes, qo, to)
            worst_foot = max(worst_foot, math.dist(w[legL[2]], ankL0), math.dist(w[legR[2]], ankR0))
            if must is not None and tt >= 1.3:
                hold_err = max(hold_err, math.dist(w[armR[2]], must))
            for i in range(len(nodes)):
                qtr.setdefault(i, []).extend(qnorm(qo.get(i, nodes[i]["q"])))
            ttr.extend(to.get(pel, nodes[pel]["t"]))
        # THE SCORE IS THE TEST. The carry is at the edge of a straight arm on purpose, so it is
        # held to a looser tolerance than the board, which the hand must actually be on.
        tol = 0.12 if must is carry else 0.02
        print("z_board: %-14s %3d f   worst foot drift %.4f L   hand at the hold %.4f L (tol %.2f)"
              % (name, frames, worst_foot / L, hold_err / L, tol))
        assert worst_foot / L < 0.003, "%s: a planted foot moved" % name
        assert hold_err / L < tol, "%s: the hand is not where the hold says" % name
        tracks = {(i, 0): v for i, v in qtr.items()}
        tracks[(pel, 1)] = ttr
        out.append((name, FPS, frames, tracks))

    if out:
        append_clips(pack_path, out)
        print("z_board: appended %s" % ", ".join(o[0] for o in out))

    # WHERE THE SET PUTS THE BOARD, in world units for this body's engine scale (the cast
    # loader sizes every body to 3.45 by its mesh)
    ys = [v[1] for v in p.skinned_positions()]
    scale = 3.45 / (max(ys) - min(ys))
    df = sum((board[k] - mid[k]) * fwd[k] for k in (0, 2)) * scale
    dr = sum((board[k] - mid[k]) * right[k] for k in (0, 2)) * scale
    print("z_board: engine scale %.4f -> the board mark is %.2f u FORWARD and %.2f u RIGHT of the body's mark;"
          " a board lying ACROSS her is that mark's yaw = body yaw + 90" % (scale, df, dr))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
