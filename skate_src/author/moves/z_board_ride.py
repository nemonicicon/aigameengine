#!/usr/bin/env python3
"""z_board_ride.py -- ROBOT NEKO ep 9: standing on a skateboard, badly, then less badly.

  z_board_stepon  front foot up onto the board, then the back foot -- HOLD on the deck
  z_board_wobble  the first time on a board: knees nearly locked, arms windmilling, hips hunting (loop)
  z_board_steady  after "knees.": bent, arms out, small corrections (loop)
  z_board_push    one push: back foot down to the floor, sweep, back up onto the deck (loop)
  z_board_bail    the board goes and she sits down hard -- HOLD

Authored from each rig's own proportions (rig_map), over the same stand as z_board.py.

THE DECK CONTRACT (crowd_kms.c, ep 9). `@deck` draws the board at the body's ROOT with its long
axis ACROSS the body, and LIFTS THE WHOLE BODY by the board's measured height. So:
  * a riding clip keeps a foot that is ON the deck at the rig's own stand-ankle height;
  * a foot that reaches the real FLOOR goes BELOW that by the deck height in rig units --
    which depends on the body's engine scale, measured here from its mesh.
  * z_board_stepon is the one riding clip played while the board is still on a MARK (no lift
    yet), so it climbs the deck height itself, and the board lies STEP_F ahead of her. It ends
    EXACTLY on z_board_wobble's first frame plus (STEP_F forward, deck height up) -- and the next
    line snaps her mark STEP_F forward and puts the board @deck, which is that offset. The
    numbers for the set are printed.
A skater stands SIDEWAYS: the board runs along her left-right, her LEFT leads (regular), and
she looks along the board toward the lead. The push is the back (right) foot, and its sweep
speed along the floor is printed: the engine's LOCO_RIDE is set to it, so a planted foot stays
planted in the world while the body rolls.

usage: z_board_ride.py <pack>
"""
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rpack import Rpack, append_clips
from authorlib import (rig_map, fk_world, aim_bone, solve_limb, rotate_bone_world, translate_node_world,
                       hold_world_rotation, profile, ease_smooth, qnorm, qrot, q_axis_angle)
import z_board as ZB

FPS = 30.0
DECK_H_WORLD = 0.223        # skateboard.rpack held @1.4: its measured height, club units
STANCE_HALF = 0.24          # each foot's distance from the board's centre along it, in LEGS
DECK_Z = -0.03              # the ankles sit this far behind the root (rig u): the feet straddle the deck's centreline
STEP_F = 0.32               # rig u the board lies ahead of the root before the step-on (clear of her toes)
PUSH_SWEEP = 0.45           # the push foot's sweep along the floor, in LEGS
PUSH_T = (0.45, 0.95)       # the push foot is planted on the floor for this part of the 1.6 s cycle
SIT_H = 0.12                # pelvis above the floor when she has sat down hard, in LEGS
UP = [0.0, 1.0, 0.0]


def main(pack_path):
    p = Rpack(pack_path)
    nodes = p.nodes()
    RM = rig_map(p)
    assert not RM["_missing"], "rig incomplete: %s" % RM["_missing"]
    have = set(p.clip_names())

    base_q = ZB.stand_q(nodes, RM)
    pel = RM["pelvis"]; spine = [RM["spine_01"], RM["spine_02"], RM["spine_03"]]
    neck, head = RM["neck_01"], RM["head"]
    legL = (RM["thigh_l"], RM["calf_l"], RM["foot_l"]); legR = (RM["thigh_r"], RM["calf_r"], RM["foot_r"])
    armL = (RM["upperarm_l"], RM["lowerarm_l"], RM["hand_l"]); armR = (RM["upperarm_r"], RM["lowerarm_r"], RM["hand_r"])

    wp0, wq0 = fk_world(nodes, base_q)
    L = math.dist(wp0[legL[0]], wp0[legL[1]]) + math.dist(wp0[legL[1]], wp0[legL[2]])
    ankL0, ankR0 = list(wp0[legL[2]]), list(wp0[legR[2]])
    ankY = (ankL0[1] + ankR0[1]) / 2
    floorY = min(wp0[RM["ball_l"]][1], wp0[RM["ball_r"]][1])
    hip0 = wp0[pel][1]
    lat = ZB.flat([wp0[legL[0]][k] - wp0[legR[0]][k] for k in range(3)])     # her LEFT = the lead
    fwd = ZB.flat([wp0[RM["ball_r"]][k] - wp0[RM["foot_r"]][k] for k in range(3)])
    ys = [v[1] for v in p.skinned_positions()]
    scale = 3.45 / (max(ys) - min(ys))
    deckH = DECK_H_WORLD / scale
    print("z_board_ride: leg %.3f  engine scale %.4f  deck height %.4f rig u" % (L, scale, deckH))

    def spot(side_lat, along_fwd, y):
        return [lat[0] * side_lat + fwd[0] * along_fwd, y, lat[2] * side_lat + fwd[2] * along_fwd]

    deck = {"l": spot(+STANCE_HALF * L, DECK_Z, ankY), "r": spot(-STANCE_HALF * L, DECK_Z, ankY)}

    def build(t_hip, drop, lean, roll, twist, feet, arms=None, hands=None, look_turn=0.0, look_down=0.0, knee_pole=None):
        qo = dict(base_q); to = {}
        off = [lat[k] * t_hip[0] * L + fwd[k] * t_hip[1] * L for k in range(3)]
        off[1] = -drop * L
        to[pel] = translate_node_world(nodes, pel, off)
        qo[pel] = rotate_bone_world(nodes, pel, lat, math.radians(lean) * 0.4, qo)
        for b in spine:
            qo[b] = rotate_bone_world(nodes, b, lat, math.radians(lean) * 0.2, qo)
            qo[b] = rotate_bone_world(nodes, b, fwd, math.radians(roll) / 3.0, qo)
            qo[b] = rotate_bone_world(nodes, b, UP, math.radians(twist) / 3.0, qo)
        kp = knee_pole or fwd
        for s, chain in (("l", legL), ("r", legR)):
            qo.update(solve_limb(nodes, chain, feet[s], pole=kp, q_override=qo, t_override=to))
            qo[chain[2]] = hold_world_rotation(nodes, chain[2], wq0[chain[2]], qo, to)
        for s, chain, sg in (("l", armL, 1.0), ("r", armR, -1.0)):
            if hands and s in hands:
                pole = [-fwd[0] + lat[0] * sg * 0.6, 0.0, -fwd[2] + lat[2] * sg * 0.6]
                qo.update(solve_limb(nodes, chain, hands[s], pole=pole, q_override=qo, t_override=to))
            elif arms:
                abd, swing = arms[s]
                a = math.radians(abd)
                d0 = [lat[0] * sg * math.sin(a), -math.cos(a), lat[2] * sg * math.sin(a)]
                d = qrot(q_axis_angle(lat, -math.radians(swing)), d0)            # + swing = forward
                qo[chain[0]] = aim_bone(nodes, chain[0], chain[1], d, qo, to)
                qo[chain[1]] = aim_bone(nodes, chain[1], chain[2], [d[0] + fwd[0] * 0.45, d[1] + 0.15, d[2] + fwd[2] * 0.45], qo, to)
        if look_turn or look_down:
            tr = math.radians(look_turn)
            qo[neck] = rotate_bone_world(nodes, neck, UP, tr * 0.35, qo)
            qo[head] = rotate_bone_world(nodes, head, UP, tr * 0.65, qo)
            qo[head] = rotate_bone_world(nodes, head, qrot(q_axis_angle(UP, tr), lat), math.radians(look_down), qo)
        return qo, to

    # ---- the scores ---------------------------------------------------------------------------
    def wobble(t, D):
        w1 = math.sin(2 * math.pi * 3 * t / D); w2 = math.sin(2 * math.pi * 5 * t / D + 1.1); w3 = math.sin(2 * math.pi * 2 * t / D + 0.4)
        return dict(t_hip=[0.035 * w1 + 0.012 * w2, 0.018 * w3], drop=0.05 + 0.012 * math.sin(2 * math.pi * 4 * t / D),
                    lean=6.0 + 5.0 * w3, roll=8.0 * w1, twist=12.0, feet=deck,
                    arms={"l": (72 + 34 * w1, 22 * w2), "r": (72 - 34 * w1, -22 * w2)}, look_turn=25.0, look_down=30.0)

    def steady(t, D):
        w1 = math.sin(2 * math.pi * 2 * t / D); w2 = math.sin(2 * math.pi * 3 * t / D + 0.7)
        return dict(t_hip=[0.010 * w1, 0.006 * w2], drop=0.14 + 0.008 * math.sin(2 * math.pi * t / D), lean=9.0,
                    roll=3.0 * w1, twist=22.0, feet=deck,
                    arms={"l": (40 + 6 * w1, 10.0), "r": (40 - 6 * w1, 10.0)}, look_turn=60.0, look_down=8.0)

    floor0 = spot(0.05 * L, 0.18 * L, ankY - deckH)
    floor1 = [floor0[0] - lat[0] * PUSH_SWEEP * L, floor0[1], floor0[2] - lat[2] * PUSH_SWEEP * L]

    def push(t, D):
        u = t % D
        dR = deck["r"]
        if u < 0.20:
            foot = list(dR)
        elif u < PUSH_T[0]:
            s = ease_smooth((u - 0.20) / (PUSH_T[0] - 0.20)); foot = ZB.lerp(dR, floor0, s); foot[1] += 0.10 * L * math.sin(math.pi * s)
        elif u < PUSH_T[1]:
            s = (u - PUSH_T[0]) / (PUSH_T[1] - PUSH_T[0]); foot = ZB.lerp(floor0, floor1, s)       # LINEAR: a planted foot
        elif u < 1.30:
            s = ease_smooth((u - PUSH_T[1]) / (1.30 - PUSH_T[1])); foot = ZB.lerp(floor1, dR, s); foot[1] += 0.12 * L * math.sin(math.pi * s)
        else:
            foot = list(dR)
        k = profile([(0.0, 0.0, ease_smooth), (0.30, 1.0, None), (0.95, 1.0, ease_smooth), (1.40, 0.0, None)], u)
        s0 = steady(0.0, 4.0)
        return dict(t_hip=[s0["t_hip"][0] + 0.07 * k, s0["t_hip"][1] + 0.03 * k], drop=s0["drop"] + 0.12 * k,
                    lean=s0["lean"] + 10.0 * k, roll=-4.0 * k, twist=22.0, feet={"l": deck["l"], "r": foot},
                    arms={"l": (40 - 12 * k, 10.0 - 30 * k), "r": (40 + 4 * k, 10.0 + 24 * k)}, look_turn=60.0, look_down=8.0)

    def stepon(t, D):
        w0 = wobble(0.0, 4.0)
        sl = profile([(0.35, 0.0, ease_smooth), (0.85, 1.0, None)], t)
        sr = profile([(1.00, 0.0, ease_smooth), (1.55, 1.0, None)], t)
        b = profile([(0.25, 0.0, ease_smooth), (0.90, 0.5, ease_smooth), (1.65, 1.0, None)], t)
        lift = profile([(0.95, 0.0, ease_smooth), (1.60, 1.0, None)], t)
        hi = {s: [deck[s][0] + fwd[0] * STEP_F, deck[s][1] + deckH, deck[s][2] + fwd[2] * STEP_F] for s in "lr"}
        fl = ZB.lerp(ankL0, hi["l"], sl); fl[1] += 0.10 * L * math.sin(math.pi * sl)
        fr = ZB.lerp(ankR0, hi["r"], sr); fr[1] += 0.10 * L * math.sin(math.pi * sr)
        H = ZB.HANG
        return dict(t_hip=[0.05 * math.sin(math.pi * b) + w0["t_hip"][0] * b, (STEP_F / L) * b + w0["t_hip"][1] * b],
                    drop=w0["drop"] * b - (deckH / L) * lift, lean=w0["lean"] * b, roll=w0["roll"] * b, twist=w0["twist"] * b,
                    feet={"l": fl, "r": fr},
                    arms={s: (H + (w0["arms"][s][0] - H) * b, w0["arms"][s][1] * b) for s in "lr"},
                    look_turn=w0["look_turn"] * b, look_down=w0["look_down"] * b)

    behind = {"l": spot(+0.24 * L, -0.42 * L, floorY + 0.03), "r": spot(-0.24 * L, -0.42 * L, floorY + 0.03)}

    def bail(t, D):
        w0 = wobble(0.0, 4.0)
        f = profile([(0.00, 0.0, ease_smooth), (0.18, 0.10, None), (0.55, 1.0, ease_smooth), (0.66, 0.93, ease_smooth), (0.80, 1.0, None)], t)
        flail = profile([(0.0, 0.0, ease_smooth), (0.15, 1.0, None), (0.45, 1.0, ease_smooth), (0.75, 0.0, None)], t)
        sit = (hip0 - (floorY + SIT_H * L)) / L
        out = {s: [deck[s][0] * 0.7 + fwd[0] * 0.55 * L, ankY, deck[s][2] * 0.7 + fwd[2] * 0.55 * L] for s in "lr"}
        kp = ZB.flat([fwd[k] * (1 - f) + (0.0 if k != 1 else 0.0) for k in range(3)]) if f < 0.5 else None
        pole = [fwd[0] * (1 - f), f * 1.5 + 0.01, fwd[2] * (1 - f)]
        return dict(t_hip=[w0["t_hip"][0] * (1 - f), w0["t_hip"][1] * (1 - f) - 0.22 * f], drop=w0["drop"] + (sit - w0["drop"]) * f,
                    lean=w0["lean"] + (-25.0 - w0["lean"]) * f, roll=w0["roll"] * (1 - f), twist=w0["twist"] * (1 - f),
                    feet={s: ZB.lerp(deck[s], out[s], f) for s in "lr"},
                    arms={s: (w0["arms"][s][0] + (150.0 - w0["arms"][s][0]) * flail, 0.0) for s in "lr"},
                    look_turn=w0["look_turn"] * (1 - f), look_down=w0["look_down"] + (15.0 - w0["look_down"]) * f,
                    knee_pole=pole, _hands_k=profile([(0.50, 0.0, ease_smooth), (0.85, 1.0, None)], t))

    # ⛔ z_rm_: the step-on CLIMBS the deck height, and the hero is ankle-grounded every frame
    # unless her clip owns its Y -- without the prefix she would step up and be pulled back down
    clips = [("z_rm_board_stepon", 4.0, stepon, "hold"), ("z_board_wobble", 4.0, wobble, "loop"),
             ("z_board_steady", 4.0, steady, "loop"), ("z_board_push", 1.6, push, "loop"),
             ("z_board_bail", 4.0, bail, "hold")]

    def frame(fn, t, D):
        a = fn(t, D)
        hk = a.pop("_hands_k", 0.0)
        qo, to = build(**a)
        if hk > 0.0:                               # the bail's hands go back to the floor behind her
            w, _ = fk_world(nodes, qo, to)
            a["hands"] = {s: ZB.lerp(w[(armL if s == "l" else armR)[2]], behind[s], hk) for s in "lr"}
            qo, to = build(**a)
        return a, qo, to

    out = []
    for name, D, fn, kind in clips:
        if name in have:
            print("z_board_ride: %s already present" % name); continue
        frames = int(round(D * FPS))
        qtr = {}; ttr = []
        worst_feet = 0.0; floor_err = 0.0
        for f in range(frames):
            t = f / FPS
            a, qo, to = frame(fn, t, D)
            w, _ = fk_world(nodes, qo, to)
            worst_feet = max(worst_feet, math.dist(w[legL[2]], a["feet"]["l"]), math.dist(w[legR[2]], a["feet"]["r"]))
            if name == "z_board_push" and PUSH_T[0] + 0.02 <= (t % D) <= PUSH_T[1] - 0.02:
                floor_err = max(floor_err, abs(w[legR[2]][1] - (ankY - deckH)))
            for i in range(len(nodes)):
                qtr.setdefault(i, []).extend(qnorm(qo.get(i, nodes[i]["q"])))
            ttr.extend(to[pel])
        # THE SCORE IS THE TEST
        msg = "z_board_ride: %-15s %3d f  worst foot-to-target %.4f L" % (name, frames, worst_feet / L)
        assert worst_feet / L < 0.012, "%s: a foot did not reach where the score put it (%.4f L)" % (name, worst_feet / L)
        if kind == "loop":
            _, q0, t0 = frame(fn, 0.0, D); _, q1, t1 = frame(fn, D, D)
            gap = max(max(abs(x - y) for x, y in zip(qnorm(q0.get(i, nodes[i]["q"])), qnorm(q1.get(i, nodes[i]["q"])))) for i in range(len(nodes)))
            msg += "   loop gap %.5f" % gap
            assert gap < 1e-3, "%s does not loop" % name
        if name == "z_board_push":
            msg += "   push foot on the floor to %.4f L" % (floor_err / L)
            assert floor_err / L < 0.012, "the push foot is not on the floor while it pushes"
        if name == "z_board_bail":
            a, qo, to = frame(fn, D - 1.0 / FPS, D)
            w, _ = fk_world(nodes, qo, to)
            msg += "   seated pelvis %.3f L over the floor" % ((w[pel][1] - floorY) / L)
            assert w[pel][1] < floorY + 0.30 * L, "she did not sit down"
        print(msg)
        tracks = {(i, 0): v for i, v in qtr.items()}
        tracks[(pel, 1)] = ttr
        out.append((name, FPS, frames, tracks))

    if out:
        append_clips(pack_path, out)
        print("z_board_ride: appended %s" % ", ".join(o[0] for o in out))
    sweep = PUSH_SWEEP * L / (PUSH_T[1] - PUSH_T[0]) * scale
    print("z_board_ride: the push foot sweeps the floor at %.2f u/s in the world -- LOCO_RIDE must match" % sweep)
    print("z_board_ride: step-on geometry: the board mark is %.3f u FORWARD of her step-on mark (yaw + 90); "
          "the riding mark is that same spot, same yaw as her -- snap=1, board @deck" % (STEP_F * scale))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
