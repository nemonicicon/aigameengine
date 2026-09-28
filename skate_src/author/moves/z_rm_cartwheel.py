#!/usr/bin/env python3
"""z_rm_cartwheel.py -- P4a: THE CARTWHEEL. jacob's original probe, authored.

A right cartwheel: step onto the right foot, plant the RIGHT hand, then the LEFT hand,
pass through inversion with the legs split, land the left foot, then the right, rise.
The whole move derives from three authored curves plus the plant schedule:

    theta(t)  the body's roll about its own FORWARD axis, 0 -> 360 deg
    x(t)      pelvis travel along the body's RIGHT, 0 -> ~1.55 legs
    y(t)      pelvis height: dip, rise through inversion, land, recover

and every limb is SOLVED from them, correct by construction:
    body long axis at time t = the world DOWN vector rolled by theta(t)
    flight legs point along it (split +/- from it); flight arms point against it
    (arms "up" the body = toward the floor when inverted)
    planted hands/feet are IK'd to their declared floor points in their windows

Nothing in here is a hand-tuned quaternion. Directions come from the rig (travel = the
body's own right, roll axis = where the toes point), magnitudes are proportions of the
measured leg, the roll sign is audited at author time, and the score declares every
contact for movecheck. z_rm_: the clip owns the root -- P3's machinery carries the
inversion the auto-ground would have cancelled.

usage: z_rm_cartwheel.py <moves-pack>
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips
from authorlib import (fk_world, solve_limb, translate_node_world, hold_world_rotation,
                       rotate_bone_world, profile, ease_smooth, qnorm, qmul, qinv,
                       q_axis_angle, qrot, declare)

DUR, FPS = 3.0, 60.0   # 60: an inversion at 300 deg/s deserves halved chords
SPLIT = math.radians(28.0)      # each leg fans this far from the body axis in flight

def main(pack_path):
    p = Rpack(pack_path)
    if "z_rm_cartwheel" in p.clip_names():
        print("z_rm_cartwheel already present"); return 0
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
    spine = [names.index(n) for n in ("spine_01", "spine_02", "spine_03")]
    legL = tuple(names.index(n) for n in ("thigh_l", "calf_l", "foot_l"))
    legR = tuple(names.index(n) for n in ("thigh_r", "calf_r", "foot_r"))
    armL = tuple(names.index(n) for n in ("upperarm_l", "lowerarm_l", "hand_l"))
    armR = tuple(names.index(n) for n in ("upperarm_r", "lowerarm_r", "hand_r"))

    wp0, wq0 = fk_world(nodes, base_q, base_t)
    L = math.dist(wp0[legL[0]], wp0[legL[1]]) + math.dist(wp0[legL[1]], wp0[legL[2]])
    ankL0, ankR0 = list(wp0[legL[2]]), list(wp0[legR[2]])
    ballL, ballR = names.index("ball_l"), names.index("ball_r")
    floorY = min(wp0[ballL][1], wp0[ballR][1])
    pel0 = list(wp0[pelvis])

    fwd = [wp0[ballR][k] - wp0[legR[2]][k] for k in range(3)]; fwd[1] = 0.0
    fl = math.hypot(fwd[0], fwd[2]); fwd = [fwd[0] / fl, 0.0, fwd[2] / fl]
    rgt = [wp0[legR[0]][k] - wp0[legL[0]][k] for k in range(3)]; rgt[1] = 0.0
    rl = math.hypot(rgt[0], rgt[2]); rgt = [rgt[0] / rl, 0.0, rgt[2] / rl]

    # roll sign audit: rolling toward the body's right must carry the HEAD rightward
    headn = names.index("head")
    def head_shift(sgn):
        qo = dict(base_q)
        qo[pelvis] = rotate_bone_world(nodes, pelvis, fwd, sgn * 0.3, qo)
        w, _ = fk_world(nodes, qo, base_t)
        d = [w[headn][k] - wp0[headn][k] for k in range(3)]
        return d[0] * rgt[0] + d[2] * rgt[2]
    SGN = 1.0 if head_shift(1.0) > head_shift(-1.0) else -1.0

    # the plant map, along the travel line (proportions of the leg)
    handR_p = [ (ankL0[0] + ankR0[0]) / 2 + rgt[0] * 0.62 * L, floorY,
                (ankL0[2] + ankR0[2]) / 2 + rgt[2] * 0.62 * L ]
    handL_p = [ handR_p[0] + rgt[0] * 0.34 * L, floorY, handR_p[2] + rgt[2] * 0.34 * L ]
    landL_p = [ handL_p[0] + rgt[0] * 0.55 * L, ankL0[1], handL_p[2] + rgt[2] * 0.55 * L ]
    landR_p = [ landL_p[0] + rgt[0] * 0.34 * L, ankR0[1], landL_p[2] + rgt[2] * 0.34 * L ]

    # the three curves. hold base 0.4s; the wheel turns across [0.5 .. 2.2]; recover.
    th = [(0.00, 0.0, ease_smooth), (0.50, 0.0, ease_smooth), (0.85, 55.0, None),
          (1.25, 160.0, None), (1.60, 205.0, None), (2.00, 320.0, ease_smooth),
          (2.30, 360.0, ease_smooth), (3.00, 360.0, None)]
    # travel ends at 1.68: the MID of the landing feet (1.51 and 1.85), so the finish
    # stands centered -- 1.42 left the body 0.26 L off its own stance (terminal caught it)
    tx = [(0.00, 0.00, ease_smooth), (0.50, 0.00, ease_smooth), (0.95, 0.30, None),
          (1.45, 0.72, None), (1.90, 1.30, ease_smooth), (2.40, 1.68, ease_smooth),
          (3.00, 1.68, None)]
    ty = [(0.00, 0.00, ease_smooth), (0.50, 0.00, ease_smooth), (0.76, -0.08, None),
          (1.18, +0.42, None), (1.45, +0.50, None), (1.72, +0.42, None),
          (2.10, -0.08, ease_smooth), (2.45, -0.05, ease_smooth), (3.00, 0.00, None)]

    # limb schedules: [start, full, release-start, gone]
    def window(t, a, b, c, d):
        if t < a or t > d: return 0.0
        if t < b: return (t - a) / max(1e-6, b - a)
        if t < c: return 1.0
        return 1.0 - (t - c) / max(1e-6, d - c)
    W_FL0 = (-0.1, 0.00, 0.74, 1.00)     # left foot holds the ground into the reach
    W_FR0 = (-0.1, 0.00, 0.58, 0.85)     # right foot leaves first (it becomes the kick)
    # (ramps start BEFORE zero: at exactly t=0 a (0.00,0.01) ramp evaluated to 0 and
    #  frame 0 flashed a split-stance -- 0.69u of it, one frame, caught in the bake)
    W_HR  = (0.76, 0.98, 1.48, 1.68)     # right hand plant
    W_HL  = (1.10, 1.32, 1.78, 1.98)     # left hand plant
    W_FLl = (1.82, 2.02, 3.00, 3.10)     # left foot lands first... (pinned through the end:
    W_FRl = (2.05, 2.25, 3.00, 3.10)     # releasing to the FLY pose made the finish split-legged)
    W_ARMS = (0.36, 0.66, 2.15, 2.55)    # the overhead raise engages for the wheel only

    frames = int(DUR * FPS)
    qtr = {}; ttr = []
    checks = {"handR": 9e9, "handL": 9e9, "air": -9e9}
    # TEMPORAL POLE COHERENCE: each chain's bend plane follows its own previous frame.
    # Without it the elbow/knee plane re-derives from base every frame and FLIPS side
    # near collinear targets -- measured as 0.3-1.0u single-frame forearm/shin steps
    # (the raise and the kick both pass through the collinear zone).
    prev_mid = {}
    base_mid = {c: [wp0[c[1]][k] - wp0[c[0]][k] for k in range(3)]
                for c in (legL, legR, armL, armR)}
    def solve_coherent(chain, tg, qo, to, pole=None):
        upd = solve_limb(nodes, chain, tg, pole=pole or prev_mid.get(chain), q_override=qo, t_override=to)
        qo.update(upd)
        w_c, _ = fk_world(nodes, qo, to)
        prev_mid[chain] = [w_c[chain[1]][k] - w_c[chain[0]][k] for k in range(3)]
        return qo
    for f in range(frames):
        t = f / FPS
        theta = math.radians(profile(th, t)) * SGN
        px = profile(tx, t) * L
        py = profile(ty, t) * L

        def place(py_c):
            qo_c = dict(base_q); to_c = dict(base_t)
            to_c[pelvis] = translate_node_world(nodes, pelvis,
                                                [rgt[0] * px, py_c, rgt[2] * px], base_q, base_t)
            qo_c[pelvis] = rotate_bone_world(nodes, pelvis, fwd, theta, qo_c)
            # the arch lives INSIDE place: the height constraint must see the same
            # shoulders the final pose has (solving without it left the left hand 3cm
            # short -- the arch moves the shoulder ~6cm)
            arch_c = math.radians(8.0) * math.sin(min(math.pi, max(0.0, theta * SGN / 2)))
            for b_c in spine:
                qo_c[b_c] = rotate_bone_world(nodes, b_c, [rgt[0], 0, rgt[2]], -arch_c, qo_c)
            return qo_c, to_c

        # THE HEIGHT IS A CONSTRAINT, NOT A CURVE, wherever a hand is planted: the
        # authored ty kept the pelvis so high the hands missed the floor by half a leg
        # (the author-time assert caught it). Where a hand window is engaged, bisect the
        # pelvis DOWN until that shoulder can reach its plant with 0.88 arm. The free
        # curve rules everywhere else; the blend follows the window so there is no seam.
        wHR, wHL = window(t, *W_HR), window(t, *W_HL)
        py_final = py
        for chain, pin, weng in ((armR, handR_p, wHR), (armL, handL_p, wHL)):
            if weng <= 0.05: continue
            armlen = math.dist(wp0[chain[0]], wp0[chain[1]]) + math.dist(wp0[chain[1]], wp0[chain[2]])
            def sh_gap(py_c, chain=chain, pin=pin, armlen=armlen):
                qo_c, to_c = place(py_c)
                w_c, _ = fk_world(nodes, qo_c, to_c)
                return math.dist(w_c[chain[0]], pin) - 0.88 * armlen
            if sh_gap(py) > 0:
                lo_b, hi_b = py - 1.2 * L, py
                for _ in range(18):
                    mid_b = (lo_b + hi_b) / 2
                    if sh_gap(mid_b) > 0: hi_b = mid_b
                    else: lo_b = mid_b
                # a LOWER pelvis serves every planted hand: each engaged hand proposes,
                # weighted by its own window, and the lowest proposal rules
                py_final = min(py_final, py + (lo_b - py) * weng)
        py = py_final
        qo, to = place(py)
        # roll + arch applied by place()
        # body axis down-the-legs at this roll
        R = q_axis_angle(fwd, theta)
        down = qrot(R, [0.0, -1.0, 0.0])
        up = [-down[0], -down[1], -down[2]]
        w, _ = fk_world(nodes, qo, to)

        # LEGS: planted windows pin to their points; otherwise fly along the split axis
        for chain, w0, wl, pin0, pinl, split in (
                (legL, W_FL0, W_FLl, ankL0, landL_p, +SPLIT),
                (legR, W_FR0, W_FRl, ankR0, landR_p, -SPLIT)):
            a0 = window(t, *w0); al = window(t, *wl)
            Rs = q_axis_angle(fwd, theta + split * SGN)
            dirv = qrot(Rs, [0.0, -1.0, 0.0])
            hipp = w[chain[0]]
            flyt = [hipp[k] + dirv[k] * 0.96 * L for k in range(3)]
            if a0 >= al:
                tg = [flyt[k] + (pin0[k] - flyt[k]) * a0 for k in range(3)]
            else:
                tg = [flyt[k] + (pinl[k] - flyt[k]) * al for k in range(3)]
            # after the wheel the knees settle back to their BASE plane (a stale chained
            # pole left the end-pose knees 0.17u off the opening stand)
            # the pole BLENDS back to the base plane over [2.30..2.60] -- the instant
            # switch snapped the calf 0.54u in one frame
            bw = min(1.0, max(0.0, (t - 2.30) / 0.30))
            pol = None
            if bw > 0 and chain in prev_mid:
                pm, bm = prev_mid[chain], base_mid[chain]
                pol = [pm[k] + (bm[k] - pm[k]) * bw for k in range(3)]
            elif bw >= 1.0:
                pol = base_mid[chain]
            solve_coherent(chain, tg, qo, to, pole=pol)
            if max(a0, al) > 0.6:
                qo[chain[2]] = hold_world_rotation(nodes, chain[2], wq0[chain[2]], qo, to)

        # ARMS: raised along the body's up (overhead; floorward when inverted);
        # planted windows override with IK to the plant points
        w2, _ = fk_world(nodes, qo, to)
        ar = window(t, *W_ARMS)
        for chain, wh, pin in ((armR, W_HR, handR_p), (armL, W_HL, handL_p)):
            ah = window(t, *wh)
            sh = w2[chain[0]]
            armlen = math.dist(wp0[chain[0]], wp0[chain[1]]) + math.dist(wp0[chain[1]], wp0[chain[2]])
            # hang = the base arm pose carried along with the shoulder; raise engages for
            # the wheel; a plant window overrides both. Outside all three the arm is base.
            hang_t = [sh[k] + (wp0[chain[2]][k] - wp0[chain[0]][k]) for k in range(3)]
            # 0.85, not 0.92: at 0.92 the raise passes through full extension, where the
            # elbow's bend plane degenerates and wobbles on a cone (0.53u spikes in the
            # bake). A kept bend keeps the plane defined.
            raise_t = [sh[k] + up[k] * 0.85 * armlen for k in range(3)]
            tg = [hang_t[k] + (raise_t[k] - hang_t[k]) * ar for k in range(3)]
            tg = [tg[k] + (pin[k] - tg[k]) * ah for k in range(3)]
            if ar > 0.01 or ah > 0.01:
                # arms bend about the roll axis, always: a world-stable pole (the
                # elbows' plane never flips mid-raise)
                solve_coherent(chain, tg, qo, to, pole=fwd)
            else:
                prev_mid.pop(chain, None)   # re-derive from base when the arm is base

        # author-time checks at full plant / full flight
        w3, _ = fk_world(nodes, qo, to)
        if window(t, *W_HR) > 0.999:
            checks["handR"] = min(checks["handR"], -abs(w3[armR[2]][1] - floorY))
        if window(t, *W_HL) > 0.999:
            checks["handL"] = min(checks["handL"], -abs(w3[armL[2]][1] - floorY))
        if 1.25 <= t <= 1.55:
            checks["air"] = max(checks["air"],
                                (min(w3[legL[2]][1], w3[legR[2]][1]) - floorY) / L)

        for b in (list(spine) + list(legL) + list(legR) + list(armL) + list(armR)):
            qtr.setdefault(b, []).extend(qnorm(qo[b]))
        qtr.setdefault(pelvis, []).extend(qnorm(qo[pelvis]))
        ttr.extend(to[pelvis])

    print("z_rm_cartwheel: handR floor err %.3f  handL %.3f  min air foot clearance %.2f L"
          % (-checks["handR"], -checks["handL"], checks["air"]))
    assert -checks["handR"] < 0.02 * L and -checks["handL"] < 0.02 * L
    assert checks["air"] > 0.35, "the wheel never inverts"

    tracks = {}
    for (node, typ), v in base.items():
        tracks[(node, typ)] = v * frames
    for b, vals in qtr.items(): tracks[(b, 0)] = vals
    tracks[(pelvis, 1)] = ttr
    append_clips(pack_path, [("z_rm_cartwheel", FPS, frames, tracks)])
    declare(pack_path, "z_rm_cartwheel", {
        "fps": FPS, "frames": frames, "terminal": "base", "max_speed": 18.0,
        "contacts": [
            {"part": "fL",   "t": [0.00, 0.60], "floor": True, "still_tol": 0.05},
            {"part": "grp",  "t": [0.95, 1.35], "floor": True, "still_tol": 0.05, "floor_tol": 0.10},
            {"part": "grpL", "t": [1.25, 1.65], "floor": True, "still_tol": 0.05, "floor_tol": 0.10},
            {"part": "fL",   "t": [1.30, 1.55], "min_y": 0.30},
            {"part": "fR",   "t": [1.30, 1.55], "min_y": 0.30},
            {"part": "fL",   "t": [1.95, 3.00], "floor": True, "still_tol": 0.06},
            {"part": "fR",   "t": [2.20, 3.00], "floor": True, "still_tol": 0.06},
        ]})
    print("z_rm_cartwheel: appended (%d frames, %d tracks) + declaration" % (frames, len(tracks)))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
