#!/usr/bin/env python3
"""z_gait.py -- A GAIT AT A GIVEN SPEED, solved by two-bone IK on a body's own legs over any stand of its own: walk, jog or run, in place, for a
game that moves the body itself. z_walk.py is one walk at whatever speed its stride comes to; a game that has decided how fast its rider goes
(a short-legged robot in a game: 1.64, 3.35 and 6.6 m/s on a 0.5 m leg, 11-02) needs the cycle made FOR that speed, or a planted foot slides.

    z_gait.py <pack> <table.json> [--force] [--only z_gt_mid] [--sidecar out.json]

    {"gaits": {"mid": {"period": 0.5, "speed": 3.35, "duty": 0.3, "sit": 0.14, "lift": 0.5, "bob": 0.05, "lean": 7, "swing": 38, "elbow": 65}, ...},
     "stands": {"z_gt_stand": {"clip": "z_rm_sk_stepoff", "at": 0.96, "centre": true}, ...},
     "sets": {"": {"stand": "z_gt_stand", "gaits": ["slow", "mid", "fast"], "arms": "lr"},
              "_board": {"stand": "z_gt_stand_board", "gaits": ["slow", "mid"], "arms": "l"}}}

  period   the cycle, seconds; speed   m/s. Rounded to whole frames at 60 fps; a clip loops on its first frame.
  duty     how much of the cycle a foot is down. Under 0.5 both are off the ground between steps (a flight); over, both are down.
  sit      how much lower the hips ride than the stand; lift how high a foot comes up in its swing; bob the hips' rise and fall. All leg lengths.
  lean     degrees the back tips into the speed; swing, elbow   degrees of arm: shoulder either way, and the bend at the elbow.
  arms     which arms swing. The others keep the stand's pose: the hand carrying the board.

A stand is a clip's pose held and breathing, 3 s, first frame == last. centre puts its hips over the origin, for a clip whose root motion left the
body somewhere else (z_rm_ owns its root): a stand off centre would slide the whole body at every fade into a gait.

What makes it a gait and not a slide:
  * A foot on the ground goes back by exactly speed x time: S = v duty T over its stance. Rolled heel to toe: the heel leads in, the foot lies flat, then
    it turns over the ball of the foot, which is what keeps moving at the speed while the ankle rises (the toe bends flat under it).
  * The swing is a Hermite from the toe-off's velocity to a landing that has all but stopped against the body, and the ground takes it from there.
  * THE GATES: first frame == last; a planted foot on its line within 5 mm; nothing under the floor; every leg reaches (a stride the leg can't span is a
    refusal, not a stretched knee: lower the hips, shorten the stride, raise the duty).
The Williams layer (design/animation_principles.md): body down just after contact and up after passing, belt line tips toward the swinging leg, shoulders
turn against the hips, arms against the same side's leg, the head keeps its line.
"""
import json, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips, drop_clip, lint_name
from authorlib import (rig_map, fk_world, solve_limb, hold_world_rotation, rotate_bone_world, translate_node_world, clip_pose,
                       qnorm, qslerp, qmul, qrot, q_axis_angle, ease_smooth)

FPS = 60.0
UP = [0.0, 1.0, 0.0]
SIDES = ("l", "r")
PLANT_TOL = 0.005
# Degrees the foot rolls: toe up at the heel strike, heel up at the toe-off.
HEEL_DEG, TOE_DEG = 14.0, 32.0
# How much of the body's speed a foot has already picked up, going back, as it lands.
LAND_MATCH = 0.15
# A stand's breath: seconds, how far the chest lifts (degrees) and the hips sink (leg lengths).
BREATH_T, BREATH_DEG, BREATH_DIP = 3.0, 1.1, 0.006


def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def add(a, b, s=1.0): return [a[0] + b[0] * s, a[1] + b[1] * s, a[2] + b[2] * s]
def sub(a, b): return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
def scale(a, s): return [a[0] * s, a[1] * s, a[2] * s]
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def norm(a):
    l = math.sqrt(dot(a, a)) or 1.0
    return [a[0] / l, a[1] / l, a[2] / l]
def lerp(a, b, u): return a + (b - a) * u
def smooth(t, t0, t1):
    if t1 <= t0: return 1.0 if t >= t1 else 0.0
    return ease_smooth((t - t0) / (t1 - t0))
def hermite(p0, v0, p1, v1, dur, u):
    u2, u3 = u * u, u * u * u
    return (2 * u3 - 3 * u2 + 1) * p0 + (u3 - 2 * u2 + u) * dur * v0 + (-2 * u3 + 3 * u2) * p1 + (u3 - u2) * dur * v1


class Body:
    def __init__(self, p):
        self.p = p
        self.nodes = p.nodes()
        self.rm = rig_map(p)
        assert not self.rm["_missing"], "rig incomplete: %s" % self.rm["_missing"]
        rm = self.rm
        self.leg = {s: (rm["thigh_" + s], rm["calf_" + s], rm["foot_" + s]) for s in SIDES}
        self.toe = {s: rm["ball_" + s] for s in SIDES}
        wp, _ = fk_world(self.nodes)
        self.leg_len = sum(math.dist(wp[a], wp[b]) + math.dist(wp[b], wp[c]) for a, b, c in self.leg.values()) / 2.0


class Stand:
    """A pose of the body, and what its feet say about which way it faces."""
    def __init__(self, body, q, t):
        self.q, self.t = q, t
        wp, wq = fk_world(body.nodes, q, t)
        self.ankle = {s: wp[body.leg[s][2]] for s in SIDES}
        self.ball = {s: wp[body.toe[s]] for s in SIDES}
        self.foot_q = {s: wq[body.leg[s][2]] for s in SIDES}
        self.toe_world_q = {s: wq[body.toe[s]] for s in SIDES}
        self.toe_q = {s: q.get(body.toe[s], body.nodes[body.toe[s]]["q"]) for s in SIDES}
        along = add(sub(self.ball["l"], self.ankle["l"]), sub(self.ball["r"], self.ankle["r"]))
        along[1] = 0.0
        self.fwd = norm(along)
        self.left = norm(cross(UP, self.fwd))
        self.pelvis = wp[body.rm["pelvis"]]
        self.wp = wp


def centred(body, q, t):
    """The stand with its hips over the origin."""
    wp, _ = fk_world(body.nodes, q, t)
    pel = body.rm["pelvis"]
    t = dict(t)
    t[pel] = translate_node_world(body.nodes, pel, [-wp[pel][0], 0.0, -wp[pel][2]], q, t)
    return t


def foot_state(g, stand, s, phase, x0, y0):
    """Where a foot is at this phase of its own cycle: its ankle, world rotation, and whether it's down. Phase 0 is its landing."""
    d, T, v = g["duty"], g["T"], g["speed"]
    S = v * d * T
    fwd, left = stand.fwd, stand.left
    z0 = dot(stand.ankle[s], left)
    to_ball = sub(stand.ball[s], stand.ankle[s])
    ax, ay = dot(to_ball, fwd), to_ball[1]
    ground_ankle = y0

    def along(x, y):
        return add(add(scale(fwd, x), scale(left, z0)), scale(UP, y))

    def roll(angle):
        return qnorm(qmul(q_axis_angle(left, math.radians(angle)), stand.foot_q[s]))

    def stance_at(u):
        x = x0 + S * (0.5 - u)
        if u < 0.18:
            ang = -HEEL_DEG * (1.0 - ease_smooth(u / 0.18))
        elif u < 0.5:
            ang = 0.0
        else:
            ang = TOE_DEG * ease_smooth((u - 0.5) / 0.5)
        if ang > 0.0:
            # Over the ball of the foot: the ball keeps moving back at the speed, the ankle rises round it.
            c, sn = math.cos(math.radians(ang)), math.sin(math.radians(ang))
            bx = x + ax
            rx, ry = ax * c + ay * sn, -ax * sn + ay * c
            return along(bx - rx, ground_ankle + ay - ry), roll(ang), ang
        return along(x, ground_ankle), roll(ang), ang

    if phase < d:
        pos, q, ang = stance_at(phase / d)
        return pos, q, True, ang
    w = (phase - d) / (1.0 - d)
    dur = (1.0 - d) * T
    p0, _q0, a0 = stance_at(1.0)
    p9, _q9, _a9 = stance_at(0.999)
    p1, _q1, a1 = stance_at(0.0)
    du = 0.001 * d * T
    v0 = sub(p0, p9)
    v0 = scale(v0, 1.0 / du)
    # One curve from the toe-off to the landing. A foot in the air reaches out and all but stops against the body before it comes down, and the ground
    # takes it back from there: the alien's PR-29 cycles do it to the frame, and matching the speed in the air flings it out of the leg's reach.
    x = hermite(dot(p0, fwd), dot(v0, fwd), dot(p1, fwd), -v * LAND_MATCH, dur, w)
    # And it comes down from above, not skimming the floor on its way in.
    y = hermite(p0[1], v0[1], p1[1], -(0.5 + 0.25 * v), dur, w)
    lift = g["lift"] * g["leg"] * math.sin(math.pi * w) ** 2 * (1.25 - 0.5 * w)
    pos = along(x, y + lift)
    ang = lerp(a0, a1, ease_smooth(w)) + 26.0 * math.sin(math.pi * w) * (1.0 - w)
    return pos, roll(ang), False, ang


def author_gait(body, base, spec, leg, name, arms):
    T = round(float(spec["period"]) * FPS) / FPS
    frames = int(round(T * FPS))
    g = dict(spec, T=T, leg=leg)
    fwd, left = base.fwd, base.left
    rm = body.rm
    pel = rm["pelvis"]
    d = g["duty"]
    sit = g["sit"] * leg
    bob = g["bob"] * leg
    floor = {s: base.ankle[s][1] for s in SIDES}
    # The feet centred under the hips along the way it faces, as the stand has them across.
    x0 = {s: dot(base.pelvis, fwd) for s in SIDES}
    swing_arm = math.radians(g["swing"])
    elbow = math.radians(g["elbow"])
    lean = math.radians(g["lean"])
    hip_yaw = math.radians(4.0 + 0.8 * g["speed"])
    hip_roll = math.radians(3.0)
    qs, ts, checks = [], [], []
    for f in range(frames):
        ph = f / float(frames)
        q, t = dict(base.q), dict(base.t)
        # Down just after contact and up after passing: at a walk the low point is both feet down, in a flight it's the middle of the stance.
        low = 0.0 if d > 0.5 else d / 2.0
        wave = 0.5 * (1.0 + math.cos(4.0 * math.pi * (ph - low)))
        drift = 0.02 * leg * math.sin(2.0 * math.pi * ph)
        t[pel] = translate_node_world(body.nodes, pel, add(scale(UP, -sit - bob * wave), scale(left, drift)), q, t)
        s2 = math.sin(2.0 * math.pi * ph)
        q[pel] = rotate_bone_world(body.nodes, pel, UP, hip_yaw * s2, q)
        q[pel] = rotate_bone_world(body.nodes, pel, fwd, hip_roll * s2, q)
        # The back tips into the speed and turns against the hips; the head keeps its line.
        for j, share in (("spine_01", 0.4), ("spine_02", 0.35), ("spine_03", 0.25)):
            q[rm[j]] = rotate_bone_world(body.nodes, rm[j], left, lean * share, q)
            q[rm[j]] = rotate_bone_world(body.nodes, rm[j], UP, -hip_yaw * 1.5 * share * s2, q)
            q[rm[j]] = rotate_bone_world(body.nodes, rm[j], fwd, -hip_roll * share * s2, q)
        q[rm["head"]] = rotate_bone_world(body.nodes, rm["head"], left, -lean * 0.6, q)
        # The arms against the same side's leg, bent more the faster it goes.
        for s, sign in (("l", 1.0), ("r", -1.0)):
            if s not in arms:
                continue
            ua, la = rm["upperarm_" + s], rm["lowerarm_" + s]
            c2 = math.cos(2.0 * math.pi * ph)
            q[ua] = rotate_bone_world(body.nodes, ua, left, sign * swing_arm * c2 + swing_arm * 0.25, q)
            q[la] = rotate_bone_world(body.nodes, la, left, -(elbow + 0.3 * elbow * sign * c2), q)
        # The legs.
        for s, off in (("l", 0.0), ("r", 0.5)):
            pos, fq, down, ang = foot_state(g, base, s, (ph + off) % 1.0, x0[s], floor[s])
            leg_nodes = body.leg[s]
            wp, _ = fk_world(body.nodes, q, t)
            reach = math.dist(wp[leg_nodes[0]], pos)
            assert reach <= leg * 1.002, "%s: the %s leg can't span the stride at phase %.2f (%.3f of %.3f): lower the hips, shorten the stride or raise the duty" % (name, s, ph, reach, leg)
            pole = add(fwd, scale(left, (1.0 if s == "l" else -1.0) * 0.06))
            q.update(solve_limb(body.nodes, leg_nodes, pos, pole=pole, q_override=q, t_override=t))
            q[leg_nodes[2]] = hold_world_rotation(body.nodes, leg_nodes[2], fq, q, t)
            # The toe bends flat under the ball while the heel is up, and eases back in the air.
            toe = body.toe[s]
            flat = hold_world_rotation(body.nodes, toe, base.toe_world_q[s], q, t)
            q[toe] = qnorm(qslerp(base.toe_q[s], flat, smooth(ang, 0.0, 6.0) if down else 0.0))
            if down:
                checks.append((f, s, (ph + off) % 1.0, x0[s]))
        qs.append(q); ts.append(t)
    return bake(body, name, qs, ts, g, base, checks, loop=True), g


def author_stand(body, spec, name):
    p = body.p
    c = p.clip(p.clip_names().index(spec["clip"]))
    q0, t0 = clip_pose(p, spec["clip"], float(spec.get("at", 0.0)) * c["fps"])
    if spec.get("centre"):
        t0 = centred(body, q0, t0)
    frames = int(round(BREATH_T * 20.0))
    wp, _ = fk_world(body.nodes, q0, t0)
    rm = body.rm
    pel = rm["pelvis"]
    leg = body.leg_len
    base = Stand(body, q0, t0)
    qs, ts = [], []
    for f in range(frames):
        ph = f / float(frames)
        q, t = dict(q0), dict(t0)
        wave = 0.5 * (1.0 - math.cos(2.0 * math.pi * ph))
        t[pel] = translate_node_world(body.nodes, pel, [0.0, -BREATH_DIP * leg * wave, 0.0], q, t)
        for j, share in (("spine_02", 0.6), ("spine_03", 0.4)):
            q[rm[j]] = rotate_bone_world(body.nodes, rm[j], base.left, -math.radians(BREATH_DEG) * share * wave, q)
        q[rm["head"]] = rotate_bone_world(body.nodes, rm["head"], base.left, math.radians(BREATH_DEG) * 0.8 * wave, q)
        for s in SIDES:
            leg_nodes = body.leg[s]
            q.update(solve_limb(body.nodes, leg_nodes, base.ankle[s], pole=knee_pole(wp, leg_nodes), q_override=q, t_override=t))
            q[leg_nodes[2]] = hold_world_rotation(body.nodes, leg_nodes[2], base.foot_q[s], q, t)
        qs.append(q); ts.append(t)
    return tracks_of(body, name, qs, ts, 20.0), base


def knee_pole(wp, leg):
    a, b, c = wp[leg[0]], wp[leg[1]], wp[leg[2]]
    ac = norm(sub(c, a))
    return norm(sub(sub(b, a), scale(ac, dot(sub(b, a), ac))))


def bake(body, name, qs, ts, g, base, checks, loop):
    """THE SCORE IS THE TEST."""
    nodes = body.nodes
    w0, _ = fk_world(nodes, qs[0], ts[0])
    w1, _ = fk_world(nodes, qs[-1], ts[-1])
    # One step of the cycle from the last frame comes back to the first: it closes if that step is no longer than the others.
    close = max(math.dist(w0[i], w1[i]) for i in range(len(nodes)))
    worst = 0.0
    S = g["speed"] * g["duty"] * g["T"]
    for f, s, ph, x0 in checks:
        wp, _ = fk_world(nodes, qs[f], ts[f])
        u = ph / g["duty"]
        # The ball is what's planted once the heel is up: its forward speed is the body's.
        worst = max(worst, abs(dot(wp[body.leg[s][2]], base.fwd) - (x0 + S * (0.5 - u)))) if u < 0.5 else worst
    lows = [1e9, 1e9]
    for f in range(len(qs)):
        wp, _ = fk_world(nodes, qs[f], ts[f])
        for s in SIDES:
            lows[0] = min(lows[0], wp[body.leg[s][2]][1] - base.ankle[s][1])
            lows[1] = min(lows[1], wp[body.toe[s]][1] - base.ball[s][1])
    print("%-22s %3d frames, %.3f s, %.2f m/s  close %.3f  plant %.4f  under floor ankle %+.3f toe %+.3f"
          % (name, len(qs), g["T"], g["speed"], close, worst, lows[0], lows[1]))
    assert worst < PLANT_TOL, "%s: a planted foot left its line by %.4f" % (name, worst)
    assert lows[0] > -0.02 and lows[1] > -0.03, "%s: under the floor" % name
    return tracks_of(body, name, qs, ts, FPS)


def tracks_of(body, name, qs, ts, fps):
    nodes = body.nodes
    frames = len(qs)
    tracks = {}
    for i in range(len(nodes)):
        tracks[(i, 0)] = [float(x) for f in range(frames) for x in qnorm(qs[f].get(i, nodes[i]["q"]))]
    for i in set(k for t in ts for k in t):
        tracks[(i, 1)] = [float(x) for f in range(frames) for x in ts[f].get(i, nodes[i]["t"])]
    return (name, fps, frames, tracks)


def main(argv):
    pack, table_path = argv[1], argv[2]
    force = "--force" in argv
    only = argv[argv.index("--only") + 1].split(",") if "--only" in argv else None
    sidecar = argv[argv.index("--sidecar") + 1] if "--sidecar" in argv else None
    table = json.load(open(table_path))
    p = Rpack(pack)
    body = Body(p)
    print("z_gait: leg %.3f" % body.leg_len)
    out, info = [], {}
    stands = {}
    for name, spec in table["stands"].items():
        row, stand = author_stand(body, spec, name)
        stands[name] = stand
        assert not lint_name(name), "%s trips the engine's miners: %s" % (name, lint_name(name))
        out.append(row)
        info[name] = {"kind": "stand", "seconds": BREATH_T, "faces": stand.fwd}
    for set_name, spec in table["sets"].items():
        base = stands[spec["stand"]]
        for pace in spec["gaits"]:
            name = "z_gt_%s%s" % (pace, set_name)
            if only and name not in only:
                continue
            assert not lint_name(name), "%s trips the engine's miners: %s" % (name, lint_name(name))
            row, g = author_gait(body, base, table["gaits"][pace], body.leg_len, name, spec["arms"])
            out.append(row)
            info[name] = {"kind": "gait", "pace": pace, "seconds": g["T"], "speed": g["speed"], "foot_down": [0.0, 0.5]}
    for name in info:
        if name in p.clip_names():
            assert force, "%s already present (--force re-authors it)" % name
            drop_clip(pack, name)
    append_clips(pack, out, force=True)
    print("z_gait: appended %d clips to %s" % (len(out), os.path.basename(pack)))
    if sidecar:
        json.dump(info, open(sidecar, "w"), indent=1, sort_keys=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
