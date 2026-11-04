#!/usr/bin/env python3
"""z_ft.py -- FOOT TRANSITIONS: the step that gets a body from standing into its gait and from its gait back to standing,
solved on the body's OWN idle and cycles, for a game that moves the body itself and plays the gait in place. Written for
DontGetDropped's alien (11-02), jacob: "there needs to be a real walk/run state tree".

    z_ft.py <pack> <table.json> [--only z_ft_stop_mid_l] [--force] [--sidecar out.json]

The table names the body's clips and the game's numbers:
    {"accel": 20.0,                      # m/s^2: how fast the game changes the body's speed. A planted foot slides back by exactly that
     "paces": {"mid": {"period": 0.625, "speed": 3.35, "game_speed": 3.35}, ...},         # period: the cycle's true length, seconds. speed: the clip's own.
                                                                                          # game_speed: what the game moves the body at, if not that
     "sets":  {"": {"idle": "GAME_IDLE_OFF", "paces": {"mid": "ALIEN_JOG", ...}},         # "" is the plain body
               "_board": {"idle": "ALIEN_IDLE_CARRY", "paces": {"mid": "ALIEN_JOG_CARRY"}}, ...}}
    A table shared with z_gait.py keeps these sets under "steps".

For every pace of every set, four clips (`rpack.py lint` keeps the names clear of the engine's miners, so no "walk", "jog", "run"):
  z_ft_start_<pace>_<side><set>   stand -> gait: <side> steps first. Ends on the frame of the cycle where that foot lands.
  z_ft_stop_<pace>_<side><set>    gait -> stand: begins on the frame of the cycle where <side> lands; that foot takes the brake.

THE ONE RULE: A PLANTED FOOT DOES NOT MOVE OVER THE GROUND. The clip is in place and the game moves the body, so a planted foot
slides BACK against the body by exactly how far the body travels, which comes from the speed ramp: S(t) = v0 t - a t^2 / 2. Being a
function of the game's own accel, not a look, it is a gate: first frame == the pose it comes from, last frame == the pose it goes
to, planted feet within 5 mm of S(t), nothing under the floor. A foot's height and roll while planted are read off the gait's own
stance, as a function of how far back the foot is, so the rollover is the body's own. Knees bend in the plane the source pose has
them in. Forward, floor and contacts are measured from the cycles, never assumed: the alien's on-foot clips face -X.
The Williams layer (design/animation_principles.md): the weight goes over the standing foot before the step, the body dips into a
braking plant and leans against the change of speed, the arms and the head arrive late.
"""
import json, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips, drop_clip, lint_name
from authorlib import (rig_map, fk_world, solve_limb, hold_world_rotation, rotate_bone_world, translate_node_world, clip_pose,
                       qnorm, qslerp, qmul, qinv, qrot, q_axis_angle, ease_smooth)

FPS = 60.0
UP = [0.0, 1.0, 0.0]
SIDES = ("l", "r")
OTHER = {"l": "r", "r": "l"}
# How near the lowest a foot's toe counts as on the ground, in leg lengths.
CONTACT = 0.022
# Keep a pose's first and last frames this close to what they join (leg lengths), and a planted foot this close to its line (metres).
JOIN_TOL = 0.015
PLANT_TOL = 0.012


def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def add(a, b, s=1.0): return [a[0] + b[0] * s, a[1] + b[1] * s, a[2] + b[2] * s]
def sub(a, b): return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
def scale(a, s): return [a[0] * s, a[1] * s, a[2] * s]
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def norm(a):
    l = math.sqrt(dot(a, a)) or 1.0
    return [a[0] / l, a[1] / l, a[2] / l]
def lerp(a, b, u): return a + (b - a) * u
def lerpv(a, b, u): return [lerp(a[k], b[k], u) for k in range(3)]
def smooth(t, t0, t1):
    if t1 <= t0: return 1.0 if t >= t1 else 0.0
    return ease_smooth((t - t0) / (t1 - t0))
def bump(t, t0, t1):
    """0 at t0 and t1, 1 midway, smooth at both ends"""
    if t <= t0 or t >= t1: return 0.0
    return math.sin(math.pi * (t - t0) / (t1 - t0)) ** 2
def hermite(p0, v0, p1, v1, dur, u):
    u2, u3 = u * u, u * u * u
    return ((2 * u3 - 3 * u2 + 1) * p0 + (u3 - 2 * u2 + u) * dur * v0 + (-2 * u3 + 3 * u2) * p1 + (u3 - u2) * dur * v1)


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
        self.leg_len = sum(math.dist(wp[a], wp[c]) for a, _b, c in self.leg.values()) / 2.0
        # Everything hung under the legs follows them; the rest of the body blends.
        self.leg_bones = set()
        for s in SIDES:
            self.leg_bones |= set(self.leg[s]) | {self.toe[s]}

    def wrap_pose(self, q, t):
        return fk_world(self.nodes, q, t)


class Frozen:
    """A standing clip's one pose, in the terms a foot is planned in."""
    def __init__(self, body, clip, frame=0.0):
        self.q, self.t = clip_pose(body.p, clip, frame)
        wp, wq = fk_world(body.nodes, self.q, self.t)
        self.ankle = {s: wp[body.leg[s][2]] for s in SIDES}
        self.foot_q = {s: wq[body.leg[s][2]] for s in SIDES}
        self.toe_q = {s: self.q.get(body.toe[s], body.nodes[body.toe[s]]["q"]) for s in SIDES}
        self.pole = {s: knee_pole(wp, body.leg[s]) for s in SIDES}
        self.wp = wp


def knee_pole(wp, leg):
    """Which way the knee points off the line from the hip to the ankle."""
    a, b, c = wp[leg[0]], wp[leg[1]], wp[leg[2]]
    ac = norm(sub(c, a))
    return norm(sub(sub(b, a), scale(ac, dot(sub(b, a), ac))))


class Gait:
    """A cycle and what its feet do, measured."""
    STEPS = 240

    def __init__(self, body, clip, period, speed):
        self.body, self.clip, self.period, self.speed = body, clip, period, speed
        c = body.p.clip(body.p.clip_names().index(clip))
        self.clip_fps = c["fps"]
        self.period_frames = period * self.clip_fps
        self.rows = [self._row(i * period / self.STEPS) for i in range(self.STEPS)]
        self.floor_toe = {s: min(r["toe"][s][1] for r in self.rows) for s in SIDES}
        self.floor_ankle = {s: min(r["ankle"][s][1] for r in self.rows) for s in SIDES}
        limit = CONTACT * body.leg_len
        self.down = {s: [r["toe"][s][1] < self.floor_toe[s] + limit for r in self.rows] for s in SIDES}
        # A foot on the ground is one long stretch. A real swing skims the floor by a centimetre on the way through, and that's no landing.
        for s in SIDES:
            flags = self.down[s]
            best = (0, 0)
            for a in range(self.STEPS):
                if flags[a] and not flags[a - 1]:
                    k = 0
                    while k < self.STEPS and flags[(a + k) % self.STEPS]:
                        k += 1
                    if k > best[1]:
                        best = (a, k)
            self.down[s] = [((i - best[0]) % self.STEPS) < best[1] for i in range(self.STEPS)]
        self.land, self.off = {}, {}
        for s in SIDES:
            d = self.down[s]
            n = self.STEPS
            self.land[s] = next(i for i in range(n) if d[i] and not d[i - 1]) * period / n
            self.off[s] = next(i for i in range(n) if not d[i] and d[i - 1]) * period / n
        # Back is the way a planted foot goes against the body.
        drift = [0.0, 0.0, 0.0]
        for s in SIDES:
            for i in range(self.STEPS):
                if self.down[s][i] and self.down[s][(i + 1) % self.STEPS]:
                    drift = add(drift, sub(self.rows[(i + 1) % self.STEPS]["ankle"][s], self.rows[i]["ankle"][s]))
        drift[1] = 0.0
        self.back = norm(drift)
        self.fwd = scale(self.back, -1.0)
        self.left = norm(cross(UP, self.fwd))
        # A planted foot's height, roll and side position, by how far back it is, read off this foot's own stance.
        self.stance = {}
        for s in SIDES:
            i0 = int(round(self.land[s] / period * self.STEPS)) % self.STEPS
            i1 = int(round(self.off[s] / period * self.STEPS)) % self.STEPS
            count = (i1 - i0) % self.STEPS
            self.stance[s] = [self._stance_row(s, (i0 + k) % self.STEPS) for k in range(count + 1)]

    def _row(self, t):
        body = self.body
        q, tr = self.pose(t)
        wp, wq = fk_world(body.nodes, q, tr)
        return dict(toe={s: wp[body.toe[s]] for s in SIDES}, ankle={s: wp[body.leg[s][2]] for s in SIDES},
                    foot_q={s: wq[body.leg[s][2]] for s in SIDES},
                    toe_q={s: q.get(body.toe[s], body.nodes[body.toe[s]]["q"]) for s in SIDES},
                    pole={s: knee_pole(wp, body.leg[s]) for s in SIDES})

    def _stance_row(self, s, i):
        r = self.rows[i]
        return dict(x=dot(r["ankle"][s], self.back), y=r["ankle"][s][1], z=dot(r["ankle"][s], self.left), foot_q=r["foot_q"][s], toe_q=r["toe_q"][s])

    def pose(self, t):
        return clip_pose(self.body.p, self.clip, t * self.clip_fps, loop=self.period_frames)

    def at(self, t):
        """The measured row nearest this time of the cycle, interpolated for positions."""
        f = (t % self.period) / self.period * self.STEPS
        i = int(f) % self.STEPS
        j = (i + 1) % self.STEPS
        u = f - int(f)
        a, b = self.rows[i], self.rows[j]
        out = dict(ankle={}, foot_q={}, toe_q={}, pole={}, vel={})
        for s in SIDES:
            out["ankle"][s] = lerpv(a["ankle"][s], b["ankle"][s], u)
            out["foot_q"][s] = qnorm(qslerp(a["foot_q"][s], b["foot_q"][s], u))
            out["toe_q"][s] = qnorm(qslerp(a["toe_q"][s], b["toe_q"][s], u))
            out["pole"][s] = norm(lerpv(a["pole"][s], b["pole"][s], u))
            out["vel"][s] = scale(sub(b["ankle"][s], a["ankle"][s]), self.STEPS / self.period)
        return out

    def planted(self, s, x):
        """A planted foot's (y, z, foot_q, toe_q) with its ankle x along back."""
        rows = self.stance[s]
        if x <= rows[0]["x"]:
            r = rows[0]
            return r["y"], r["z"], r["foot_q"], r["toe_q"]
        for a, b in zip(rows, rows[1:]):
            if a["x"] <= x <= b["x"] and b["x"] > a["x"]:
                u = (x - a["x"]) / (b["x"] - a["x"])
                return lerp(a["y"], b["y"], u), lerp(a["z"], b["z"], u), qnorm(qslerp(a["foot_q"], b["foot_q"], u)), qnorm(qslerp(a["toe_q"], b["toe_q"], u))
        r = max(rows, key=lambda e: e["x"])
        return r["y"], r["z"], r["foot_q"], r["toe_q"]

    def last_x(self, s):
        return max(e["x"] for e in self.stance[s])


class Ramp:
    """How far the body has travelled, t seconds into a change of speed from v0 to v1 at accel."""
    def __init__(self, v0, v1, accel):
        self.v0, self.v1 = v0, v1
        self.a = accel if v1 >= v0 else -accel
        self.t_end = abs(v1 - v0) / accel

    def v(self, t):
        return self.v0 + self.a * min(t, self.t_end)

    def s(self, t):
        u = min(t, self.t_end)
        return self.v0 * u + 0.5 * self.a * u * u + self.v1 * max(0.0, t - self.t_end)

    def time_of(self, s):
        lo, hi = 0.0, 5.0
        for _ in range(60):
            mid = (lo + hi) / 2.0
            if self.s(mid) < s: lo = mid
            else: hi = mid
        return hi


def blend_pose(nodes, qa, ta, qb, tb, w):
    """An untracked bone holds rest: one clip may key the camera bone and another not."""
    q, t = {}, {}
    for k in set(qa) | set(qb):
        q[k] = qnorm(qslerp(qa.get(k, nodes[k]["q"]), qb.get(k, nodes[k]["q"]), w))
    for k in set(ta) | set(tb):
        t[k] = lerpv(ta.get(k, nodes[k]["t"]), tb.get(k, nodes[k]["t"]), w)
    return q, t


def late_upper(body, q, qa, qb, w):
    """Everything above the hips but the back gets there on its own, later clock: arms, head, antennae."""
    rm = body.rm
    for i in list(q):
        if i in body.leg_bones or i in (rm["pelvis"], rm["spine_01"], rm["spine_02"]):
            continue
        q[i] = qnorm(qslerp(qa.get(i, body.nodes[i]["q"]), qb.get(i, body.nodes[i]["q"]), w))


def lean(body, q, axis, angle):
    """The back bends by this much, most of it low, so the head comes along."""
    rm = body.rm
    for j, share in (("spine_01", 0.55), ("spine_02", 0.45)):
        if abs(angle) > 1e-5:
            q[rm[j]] = rotate_bone_world(body.nodes, rm[j], axis, angle * share, q)


def toe_clear(body, g, s, ankle, foot_q):
    """The ankle, lifted as far as it takes to keep this foot's toe off the floor as it's turned."""
    toe = add(ankle, qrot(foot_q, body.nodes[body.toe[s]]["t"]))
    under = g.floor_toe[s] - toe[1]
    return add(ankle, UP, under) if under > 0.0 else ankle


def place_leg(body, q, t, s, ankle, foot_q, toe_q, pole):
    leg = body.leg[s]
    q.update(solve_limb(body.nodes, leg, ankle, pole=pole, q_override=q, t_override=t))
    q[leg[2]] = hold_world_rotation(body.nodes, leg[2], foot_q, q, t)
    q[body.toe[s]] = toe_q


def foot_from(g, x, y, z):
    return add(add(scale(g.back, x), scale(g.left, z)), scale(UP, y))


def author_stop(body, stand, g, side, accel, name):
    """The body brakes on `side`, landing now; the other foot comes up beside it. Past what the planted foot can reach behind the
    hips (a stop from a run travels a metre), that foot lifts on the way and the other takes over, which makes it two steps."""
    ramp = Ramp(g.game_speed, 0.0, accel)
    other = OTHER[side]
    t_e = g.land[side]
    start = g.at(t_e)
    # What each foot does along back.
    x_f0 = dot(start["ankle"][side], g.back)
    x_b0 = dot(start["ankle"][other], g.back)
    x_fi = dot(stand.ankle[side], g.back)
    x_bi = dot(stand.ankle[other], g.back)
    # At a game speed past the clip's own, the cycle plays that much faster.
    half = ((g.land[other] - t_e) % g.period) * g.speed / g.game_speed
    # The other foot lands a little sooner than the cycle would have it: it's coming down short, not reaching.
    t_b = half * 0.85
    reach = g.last_x(side)
    travel = ramp.s(ramp.t_end)
    t_lift = None
    if x_f0 + travel > reach:
        t_lift = ramp.time_of(reach - x_f0)
    t_b = max(t_b, 0.12)
    x_b_land = x_bi - (travel - ramp.s(t_b))
    if t_lift is not None:
        # The braking foot leaves for its place by the other, once that one has landed.
        t_f0, t_f1 = t_lift, max(t_b + half * 0.75, t_lift + half)
        x_f_end = x_fi
        step_lift = 0.14 * body.leg_len
    else:
        d = x_f0 + travel - x_fi
        t_f0, t_f1 = t_b + 0.03, t_b + 0.03 + (0.16 + 0.35 * abs(d))
        x_f_end = x_fi
        step_lift = (0.035 + 0.25 * abs(d)) * body.leg_len / 0.68
    T = max(t_f1, t_b, ramp.t_end + 0.1) + 0.14
    frames = int(math.ceil(T * FPS)) + 1
    T = (frames - 1) / FPS
    # Getting the planted foot's end on the same rig units as the table.
    q0, t0 = g.pose(t_e)
    vs = min(g.game_speed / 3.35, 1.4)
    lean_back = math.radians(9.0) * vs
    dip = 0.035 * body.leg_len * vs
    pel = body.rm["pelvis"]
    qs, ts, planted_check = [], [], []
    for f in range(frames):
        now = f / FPS
        # The gait carries on underneath for a moment, the arms still swinging, as it gives way to the stand.
        carry = min(now, ramp.t_end) * 0.7
        qa, ta = g.pose(t_e + carry)
        w = smooth(now, 0.0, T * 0.72)
        q, t = blend_pose(body.nodes, qa, ta, stand.q, stand.t, w)
        # The arms and head get there late.
        late_upper(body, q, qa, stand.q, smooth(now, 0.05, T))
        q, t = settle_into(body, q, t, g, stand, t_e, side, now, T, ramp, dip)
        # Lean against the change of speed, then a little past upright.
        ang = -lean_back * bump(now, 0.0, min(T, ramp.t_end * 1.5 + 0.1)) + lean_back * 0.3 * bump(now, ramp.t_end + 0.08, T)
        lean(body, q, g.left, ang)
        # The braking foot.
        if t_lift is not None and now > t_lift:
            ankle, fq, tq = long_step(g, stand, side, now, t_f0, t_f1, x_f0 + ramp.s(t_lift), ramp.v(t_lift), x_f_end, step_lift)
        elif t_lift is None and now >= t_f0:
            ankle, fq, tq = small_step(g, stand, side, now, t_f0, t_f1, x_f0 + ramp.s(t_f0), x_f_end, step_lift, body)
        else:
            x = x_f0 + ramp.s(now)
            y, z, fq, tq = g.planted(side, x)
            if t_lift is None:
                # Flat to where the stand has it, as the body comes to rest over it.
                w_s = smooth(now, t_b * 0.6, t_f0)
                y = lerp(y, stand.ankle[side][1], w_s)
                z = lerp(z, dot(stand.ankle[side], g.left), w_s)
                fq = qnorm(qslerp(fq, stand.foot_q[side], w_s))
                tq = qnorm(qslerp(tq, stand.toe_q[side], w_s))
            ankle = foot_from(g, x, y, z)
            planted_check.append((f, side, x))
        pole = norm(lerpv(start["pole"][side], stand.pole[side], w))
        place_leg(body, q, t, side, ankle, fq, tq, pole)
        # The other foot.
        if now < t_b:
            u = now / t_b
            v_land = ramp.v(t_b)
            x = hermite(x_b0, dot(start["vel"][other], g.back), x_b_land, v_land, t_b, u)
            y0 = start["ankle"][other][1]
            y1 = stand.ankle[other][1]
            y = hermite(y0, start["vel"][other][1], y1, -0.15, t_b, u) + 0.015 * math.sin(math.pi * u)
            y = max(y, y1)
            z = lerp(dot(start["ankle"][other], g.left), dot(stand.ankle[other], g.left), ease_smooth(u))
            ankle = foot_from(g, x, y, z)
            fq = qnorm(qslerp(start["foot_q"][other], stand.foot_q[other], ease_smooth(u)))
            tq = qnorm(qslerp(start["toe_q"][other], stand.toe_q[other], ease_smooth(u)))
            ankle = toe_clear(body, g, other, ankle, fq)
        else:
            x = x_b_land + ramp.s(now) - ramp.s(t_b)
            ankle = foot_from(g, x, stand.ankle[other][1], dot(stand.ankle[other], g.left))
            fq, tq = stand.foot_q[other], stand.toe_q[other]
            planted_check.append((f, other, x))
        pole = norm(lerpv(start["pole"][other], stand.pole[other], w))
        place_leg(body, q, t, other, ankle, fq, tq, pole)
        qs.append(q); ts.append(t)
    return bake(body, name, qs, ts, g, stand, q0, t0, stand.q, stand.t, planted_check, {
        "kind": "stop", "side": side, "gait": g.clip, "entry_phase": t_e / g.period, "seconds": T, "brake_ends": ramp.t_end})


def settle_into(body, q, t, g, stand, t_e, side, now, T, ramp, dip):
    """The hips dip into the plant and come up over the feet."""
    pel = body.rm["pelvis"]
    t = dict(t)
    t[pel] = translate_node_world(body.nodes, pel, [0.0, -dip * bump(now, 0.0, min(T, ramp.t_end + 0.3)), 0.0], q, t)
    return q, t


def small_step(g, stand, side, now, t0, t1, x0, x1, lift, body):
    """A short step to its place in the stand."""
    u = min(1.0, (now - t0) / max(1e-6, t1 - t0))
    e = ease_smooth(u)
    x = lerp(x0, x1, e)
    y = stand.ankle[side][1] + lift * math.sin(math.pi * u)
    z = dot(stand.ankle[side], g.left)
    return foot_from(g, x, y, z), stand.foot_q[side], stand.toe_q[side]


def long_step(g, stand, side, now, t0, t1, x0, v0, x1, lift):
    """A foot leaving the ground behind, its stride carrying it up and through to its place in the stand."""
    u = min(1.0, (now - t0) / max(1e-6, t1 - t0))
    x = hermite(x0, v0, x1, 0.0, t1 - t0, u)
    y0, z0, fq0, tq0 = g.planted(side, x0)
    y1 = stand.ankle[side][1]
    y = lerp(y0, y1, ease_smooth(u)) + lift * math.sin(math.pi * u)
    z = lerp(z0, dot(stand.ankle[side], g.left), ease_smooth(u))
    fq = qnorm(qslerp(fq0, stand.foot_q[side], ease_smooth(u)))
    tq = qnorm(qslerp(tq0, stand.toe_q[side], ease_smooth(u)))
    return foot_from(g, x, y, z), fq, tq


def author_start(body, stand, g, side, accel, name):
    """From standing: the weight goes over the other foot, `side` steps out and comes down on the cycle's own line into its landing,
    and the cycle takes over from there. The standing foot stays where it is on the ground while the body goes on past it."""
    ramp = Ramp(0.0, g.game_speed, accel)
    other = OTHER[side]
    t_j = g.land[side]
    end = g.at(t_j)
    x_b0 = dot(stand.ankle[other], g.back)
    x_f0 = dot(stand.ankle[side], g.back)
    # The cycle takes the standing foot from when it leaves the ground, if that's before the step lands (a flight); else it's planted to the end.
    off_before = (t_j - g.off[other]) % g.period
    flight = off_before < g.period * 0.5
    if flight:
        x_off = g.last_x(other)
        t_off = ramp.time_of(x_off - x_b0)
        T = ramp.time_of(ramp.s(t_off) + off_before * g.speed)
    else:
        T = ramp.time_of(dot(end["ankle"][other], g.back) - x_b0)
        t_off = None
    # The cycle's own clock: it runs as far as the body has travelled, and reads the landing at T.
    def clock(t):
        return t_j - (ramp.s(T) - ramp.s(t)) / g.speed
    frames = int(math.ceil(T * FPS)) + 1
    q1, t1_ = g.pose(t_j)
    vs = min(g.game_speed / 3.35, 1.4)
    drive = math.radians(10.0) * vs
    pel = body.rm["pelvis"]
    lift_at = 0.02
    # The last of the swing is the cycle's own, from this far before the landing.
    t_join = max(lift_at + 0.08, T - 0.11)
    r_join = g.at(clock(t_join))
    rate_join = ramp.v(t_join) / g.speed
    dur = t_join - lift_at
    swing_top = max(0.05 * body.leg_len, 0.25 * (r_join["ankle"][side][1] - stand.ankle[side][1]))
    qs, ts, planted_check = [], [], []
    for f in range(frames):
        now = min(f / FPS, T)
        c = clock(now)
        qa, ta = g.pose(c)
        w = smooth(now, 0.0, T)
        q, t = blend_pose(body.nodes, stand.q, stand.t, qa, ta, w)
        late_upper(body, q, stand.q, qa, smooth(now, 0.02, T))
        # The weight over the standing foot first, a slight dip, and the body drives off it.
        s_other = 1.0 if other == "l" else -1.0
        over = scale(g.left, s_other * 0.045 * body.leg_len * bump(now, 0.0, T * 0.8))
        drop = scale(UP, -0.018 * body.leg_len * bump(now, 0.0, T * 0.6))
        t[pel] = translate_node_world(body.nodes, pel, add(over, drop), q, t)
        lean(body, q, g.left, drive * bump(now, 0.0, T))
        # The standing foot.
        if t_off is None or now <= t_off:
            x = x_b0 + ramp.s(now)
            y, z, fq, tq = g.planted(other, x)
            w_s = smooth(now, 0.0, 0.1)
            y = lerp(stand.ankle[other][1], y, w_s)
            z = lerp(dot(stand.ankle[other], g.left), z, w_s)
            fq = qnorm(qslerp(stand.foot_q[other], fq, w_s))
            tq = qnorm(qslerp(stand.toe_q[other], tq, w_s))
            ankle = foot_from(g, x, y, z)
            planted_check.append((f, other, x))
        else:
            r = g.at(c)
            ankle, fq, tq = r["ankle"][other], r["foot_q"][other], r["toe_q"][other]
        pole = norm(lerpv(stand.pole[other], end["pole"][other], w))
        place_leg(body, q, t, other, ankle, fq, tq, pole)
        # The stepping foot.
        if now <= lift_at:
            ankle, fq, tq = stand.ankle[side], stand.foot_q[side], stand.toe_q[side]
        elif now < t_join:
            u = (now - lift_at) / dur
            e = ease_smooth(u)
            x = hermite(x_f0, 0.0, dot(r_join["ankle"][side], g.back), dot(r_join["vel"][side], g.back) * rate_join, dur, u)
            y = hermite(stand.ankle[side][1], 0.0, r_join["ankle"][side][1], r_join["vel"][side][1] * rate_join, dur, u)
            y += swing_top * math.sin(math.pi * u) ** 2
            z = lerp(dot(stand.ankle[side], g.left), dot(r_join["ankle"][side], g.left), e)
            ankle = foot_from(g, x, y, z)
            fq = qnorm(qslerp(stand.foot_q[side], r_join["foot_q"][side], e))
            # Toe down as it comes off the ground.
            fq = qnorm(qmul(q_axis_angle(g.left, math.radians(22.0) * math.sin(math.pi * u) * (1.0 - e)), fq))
            tq = qnorm(qslerp(stand.toe_q[side], r_join["toe_q"][side], e))
            ankle = toe_clear(body, g, side, ankle, fq)
        else:
            r = g.at(c)
            ankle, fq, tq = r["ankle"][side], r["foot_q"][side], r["toe_q"][side]
        pole = norm(lerpv(stand.pole[side], end["pole"][side], w))
        place_leg(body, q, t, side, ankle, fq, tq, pole)
        qs.append(q); ts.append(t)
    return bake(body, name, qs, ts, g, stand, stand.q, stand.t, q1, t1_, planted_check, {
        "kind": "start", "side": side, "gait": g.clip, "join_phase": t_j / g.period, "seconds": T})


def bake(body, name, qs, ts, g, stand, q_from, t_from, q_to, t_to, planted_check, info):
    """THE SCORE IS THE TEST. The joins, the planted feet, the floor."""
    nodes = body.nodes
    leg = body.leg_len
    def gap(qa, ta, qb, tb):
        wa, _ = fk_world(nodes, qa, ta)
        wb, _ = fk_world(nodes, qb, tb)
        worst, at = 0.0, -1
        for i in range(len(nodes)):
            d = math.dist(wa[i], wb[i])
            if d > worst: worst, at = d, i
        return worst, at
    first, at0 = gap(qs[0], ts[0], q_from, t_from)
    last, at1 = gap(qs[-1], ts[-1], q_to, t_to)
    names = body.p.node_names()
    worst_plant = 0.0
    for f, s, x in planted_check:
        wp, _ = fk_world(nodes, qs[f], ts[f])
        worst_plant = max(worst_plant, abs(dot(wp[body.leg[s][2]], g.back) - x))
    low_a, low_t, fast = 1e9, 1e9, 0.0
    visible = [i for i in range(len(nodes)) if not any(w in names[i].lower() for w in ("prop", "grip", "board"))]
    prev = None
    for f in range(len(qs)):
        wp, _ = fk_world(nodes, qs[f], ts[f])
        for s in SIDES:
            low_a = min(low_a, wp[body.leg[s][2]][1] - g.floor_ankle[s])
            low_t = min(low_t, wp[body.toe[s]][1] - g.floor_toe[s])
        if prev is not None:
            fast = max(fast, max(math.dist(wp[i], prev[i]) for i in visible) * FPS)
        prev = wp
    print("%-28s %5.2f s  first %.3f (%s) last %.3f (%s)  plant %.4f  under floor ankle %+.3f toe %+.3f  fastest joint %.1f m/s"
          % (name, (len(qs) - 1) / FPS, first, names[at0], last, names[at1], worst_plant, low_a, low_t, fast))
    assert first < JOIN_TOL * 3, "%s does not begin on the pose it comes from (%s %.3f)" % (name, names[at0], first)
    assert last < JOIN_TOL * 3, "%s does not end on the pose it goes to (%s %.3f)" % (name, names[at1], last)
    assert worst_plant < PLANT_TOL, "%s: a planted foot left its line by %.4f" % (name, worst_plant)
    assert low_a > -0.03 and low_t > -0.03, "%s: under the floor" % name
    assert fast < 9.0 + 1.5 * g.game_speed, "%s: a joint hits %.1f m/s, far past a stride at %.1f m/s: a pop" % (name, fast, g.game_speed)
    tracks = {}
    frames = len(qs)
    for i in range(len(nodes)):
        tracks[(i, 0)] = [float(x) for f in range(frames) for x in qnorm(qs[f].get(i, nodes[i]["q"]))]
    for i in set(k for t in ts for k in t):
        tracks[(i, 1)] = [float(x) for f in range(frames) for x in ts[f].get(i, nodes[i]["t"])]
    return (name, FPS, frames, tracks), info


def main(argv):
    pack, table_path = argv[1], argv[2]
    force = "--force" in argv
    only = argv[argv.index("--only") + 1].split(",") if "--only" in argv else None
    sidecar = argv[argv.index("--sidecar") + 1] if "--sidecar" in argv else None
    table = json.load(open(table_path))
    accel = float(table["accel"])
    p = Rpack(pack)
    body = Body(p)
    sets = table.get("steps", table["sets"])
    print("z_ft: leg %.3f, %d sets, accel %.1f" % (body.leg_len, len(sets), accel))
    out, info = [], {}
    for set_name, spec in sets.items():
        stand = Frozen(body, spec["idle"], float(spec.get("idle_frame", 0.0)))
        for pace, clip in spec["paces"].items():
            pace_spec = table["paces"][pace]
            g = Gait(body, clip, float(pace_spec["period"]), float(pace_spec["speed"]))
            g.game_speed = float(pace_spec.get("game_speed", pace_spec["speed"]))
            if set_name == next(iter(sets)):
                print("  %s: back %s, L down %.3f..%.3f, R down %.3f..%.3f of %.3f s" % (clip, [round(x, 2) for x in g.back], g.land["l"], g.off["l"], g.land["r"], g.off["r"], g.period))
            for side in SIDES:
                for kind, fn in (("start", author_start), ("stop", author_stop)):
                    name = "z_ft_%s_%s_%s%s" % (kind, pace, side, set_name)
                    if only and name not in only:
                        continue
                    bad = lint_name(name)
                    assert not bad, "%s trips the engine's miners: %s" % (name, bad)
                    clip_row, row_info = fn(body, stand, g, side, accel, name)
                    row_info["pace"] = pace
                    out.append(clip_row)
                    info[name] = row_info
    for name in info:
        if name in p.clip_names():
            assert force, "%s already present (--force re-authors it)" % name
            drop_clip(pack, name)
    append_clips(pack, out, force=True)
    print("z_ft: appended %d clips to %s" % (len(out), os.path.basename(pack)))
    if sidecar:
        json.dump(info, open(sidecar, "w"), indent=1, sort_keys=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
