#!/usr/bin/env python3
"""z_massage.py -- THE MASSAGE. The first pair move where the two bodies play
DIFFERENT parts of the same action: reva works, kaeli receives.

jacob: "id like to see reva massaging kaeli"

Every pair move before this one was mirror-symmetric (hug, kiss: same clip on both
bodies, the choreography interlocking by construction) or two independent poses that
merely shared a spot (cuddle + curl). A massage is neither. It is a GIVER and a TAKER
bound by a phase contract: the hands knead on a cycle, and her shoulders have to yield
on that same cycle or the whole thing reads as two people miming near each other.

    z_massage.py <pack> --role give --scale S --arm A --ppack P --pscale PS [--pgap D]
    z_massage.py <pack> --role take --scale S

THE STAGING IS FROM BEHIND (`pyaw=0`, not the 180 every earlier pair move used): she
faces away, so her rig axes and mine agree and her points map (x,y,z) -> (x, y, PGAP+z).
Getting that mapping wrong does not error, it just puts the hands on the wrong side of
her body, so the transform is written once here and used by every gate.

What the giver's score knows about her, it MEASURES:
  * THE TRAPEZIUS IS FOUND, NOT GUESSED. A hand aimed at the shoulder JOINT lands
    inside her neck-to-shoulder flesh; a hand aimed at "shoulder height" floats above
    it. The contact spot is the p80 surface of the vertex column standing over the
    neck->shoulder line (percentiles, not maxima -- the law hair keeps re-teaching).
  * THE SPOT MOVES WITH HER. The landmark is carried rigidly by the CLAVICLE, which is
    the bone her yield rotates -- so when she gives under the press, the hand follows
    her down instead of hanging in the air where her shoulder used to be.
  * PGAP IS SOLVED, NOT PICKED: the distance that puts the working hand at ~0.62 of the
    giver's arm (elbows bent, weight over the hands -- a masseuse stands close), then
    gated against both measured torsos so the two bodies never share space.

Signs are audited, never assumed (the sway direction, the nod direction, the shoulder
yield direction) -- each is applied to the rig and the resulting landmark movement is
measured, because "positive roll about +Z leans right" is a fact about a rig, not about
geometry in general.

The whole move is ROTATION-ONLY on both sides: a lateral pelvis shift would have been
the natural way to rock the giver's weight onto the working hand, and it would have
dragged her planted feet sideways -- the sway lives in the spine, where the shoulders
rock over the hands and the feet stay where they are standing.
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips
from authorlib import (rig_map, fk_world, solve_limb, rotate_bone_world, profile,
                       ease_smooth, qnorm, declare, body_dims, body_capsules,
                       hold_world_rotation, qslerp, qmul, qrot, qinv, q_between)

FPS = 30.0
T_REACH, T_WORK_END, T_END = 1.10, 6.20, 7.00
KNEAD = 1.20            # seconds per knead cycle
PRESS = 0.024           # world units the palm sinks into the trap at the press peak
LIFT = 0.010            # and lifts off it between presses
ORB_Z, ORB_X = 0.034, 0.026     # the stroke: forward-back, and in toward the neck
LEAN_G, SWAY_G, NOD_G = 8.0, 4.5, 16.0          # giver: lean in, rock, watch her hands
NOD_T, ROLL_T, SLUMP_T = 14.0, 7.0, 5.0         # taker: head hangs, rolls, spine softens
YIELD_DROP = 0.025      # world units her pressed shoulder gives (angle solved to hit it)
REACH_F = 0.62          # of the giver's arm at the neutral stroke -- elbows bent

def _n(v):
    l = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / l for x in v]

def work_env(t):
    """0 before the hands arrive, 1 through the working stretch, 0 after they leave."""
    return profile([(0.0, 0.0, ease_smooth), (T_REACH, 1.0, ease_smooth),
                    (T_WORK_END, 1.0, ease_smooth), (T_END - 0.15, 0.0, None)], t)

def knead(t, side):
    """the stroke phase for one hand, 0..1 pressing. side 0 = right, 1 = left: the two
    hands run half a cycle apart, which is what kneading looks like and what keeps her
    from being pushed straight forward."""
    ph = t / KNEAD + (0.5 if side else 0.0)
    return 0.5 - 0.5 * math.cos(2.0 * math.pi * ph)

def orbit(t, side):
    return 2.0 * math.pi * (t / KNEAD + (0.5 if side else 0.0))

class Body:
    def __init__(self, path, scale):
        self.p = Rpack(path)
        self.S = scale
        self.RM = rig_map(self.p)
        assert not self.RM["_missing"], "%s rig incomplete: %s" % (path, self.RM["_missing"])
        self.nodes = self.p.nodes()
        cn = self.p.clip_names()
        assert "z_upright" in cn, "%s has no z_upright stand" % path
        self.base_q, self.base_t, self.base = {}, {}, {}
        for (node, typ, data) in self.p.tracks(cn.index("z_upright")):
            if typ >= 3: continue
            comp = 4 if typ == 0 else 3
            v = list(struct.unpack_from("<%df" % comp, data, 0))
            self.base[(node, typ)] = v
            if typ == 0: self.base_q[node] = v
            elif typ == 1: self.base_t[node] = v
        wr, wqr = fk_world(self.nodes)
        bones = [i for i in range(len(self.nodes)) if self.nodes[i].get("kind") == 1] \
                or list(range(len(self.nodes)))
        self.mny = min(wr[i][1] for i in bones)
        self.wr, self.wqr = wr, wqr
        self.wp0, _ = fk_world(self.nodes, self.base_q, self.base_t)
        self.lat = [1.0, 0.0, 0.0]      # the body's LEFT
        self.fwd = [0.0, 0.0, 1.0]      # club forward, the engine's pixel-verified contract
        self.dims = body_dims(self.p, self.RM)
        self.caps = body_capsules(self.p, self.RM)
        self.traps = self._trapezius()
        # the trap landmarks ride the CLAVICLE (the bone the yield rotates), so a
        # yielding shoulder carries its own contact spot down with it
        self.trapLoc = {}
        for side, pt in self.traps.items():
            cb = self._carrier(side)
            d = [pt[k] - wr[cb][k] for k in range(3)]
            self.trapLoc[side] = qrot(qinv(wqr[cb]), d)

    def _carrier(self, side):
        c = self.RM.get("clavicle_" + side, -1)
        return c if c >= 0 else self.RM["spine_03"]

    def _trapezius(self):
        """the shoulder-top SURFACE per side, measured off the mesh: the p80 height of
        the vertex column standing over the neck->shoulder line. A guessed height puts
        the hands in her neck or in the air above it."""
        wr = self.wr
        neck = wr[self.RM["neck_01"]]
        verts = self.p.skinned_positions()
        out = {}
        for side in ("r", "l"):
            sh = wr[self.RM["upperarm_" + side]]
            base = [neck[k] + (sh[k] - neck[k]) * 0.55 for k in range(3)]
            span = math.dist(neck, sh)
            rad = 0.30 * span
            col = [v for v in verts
                   if abs(v[0] - base[0]) < rad and abs(v[2] - base[2]) < rad * 1.6
                   and sh[1] - rad < v[1] < sh[1] + 2.2 * rad]
            assert col, "no flesh over the %s trapezius" % side
            ys = sorted(v[1] for v in col)
            top = ys[min(len(ys) - 1, int(len(ys) * 0.80))]
            # ONLY THE HEIGHT COMES FROM THE MESH. Taking x and z from the flesh too
            # put the spot 0.09u forward of her shoulder joint -- on the collarbone,
            # with hair and costume voting -- and left/right came out asymmetric
            # (0.115 vs 0.217 off the midline), so the arms converged and the forearm
            # swept across her FACE. The trapezius sits between the neck and the
            # shoulder joint, and those are BONES: they cannot be dragged by hair.
            out[side] = [base[0], top, base[2]]
        return out

    def gw(self, pt):
        """rig point -> grounded, engine-scaled world, in this body's own frame"""
        return [pt[0] * self.S, (pt[1] - self.mny) * self.S, pt[2] * self.S]

    def rig(self, w):
        """the inverse: my grounded world point -> my rig space (for the IK)"""
        return [w[0] / self.S, w[1] / self.S + self.mny, w[2] / self.S]

def her_to_me(w, PGAP, PYAW):
    """her grounded world point, in MY frame. pyaw 0 = she faces away (massage from
    behind, axes agree); pyaw 180 = face to face (the hug/kiss mirror)."""
    if abs(PYAW) < 1.0:
        return [w[0], w[1], PGAP + w[2]]
    return [-w[0], w[1], PGAP - w[2]]

# ---------------------------------------------------------------- the taker (kaeli)
def taker_setup(K):
    """audited signs + the solved yield angle. Returns (rollSign, nodSign, yieldDeg)."""
    RM = K.RM
    def roll_probe(sgn):
        qo = dict(K.base_q)
        b = K._carrier("r")
        qo[b] = rotate_bone_world(K.nodes, b, K.fwd, sgn * math.radians(10), qo)
        w, _ = fk_world(K.nodes, qo, K.base_t)
        return K.wp0[RM["upperarm_r"]][1] - w[RM["upperarm_r"]][1]     # + = dropped
    rollS = 1.0 if roll_probe(1.0) > roll_probe(-1.0) else -1.0
    def nod_probe(sgn):
        qo = dict(K.base_q)
        qo[RM["neck_01"]] = rotate_bone_world(K.nodes, RM["neck_01"], K.lat,
                                              sgn * math.radians(12), qo)
        w, wq = fk_world(K.nodes, qo, K.base_t)
        return -qrot(wq[RM["head"]], [0.0, 0.0, 1.0])[1]               # + = looking down
    nodS = 1.0 if nod_probe(1.0) > nod_probe(-1.0) else -1.0
    lo, hi = 0.0, math.radians(26.0)
    for _ in range(16):
        mid = 0.5 * (lo + hi)
        qo = dict(K.base_q)
        b = K._carrier("r")
        qo[b] = rotate_bone_world(K.nodes, b, K.fwd, rollS * mid, qo)
        w, _ = fk_world(K.nodes, qo, K.base_t)
        drop = (K.wp0[RM["upperarm_r"]][1] - w[RM["upperarm_r"]][1]) * K.S
        if drop < YIELD_DROP: lo = mid
        else: hi = mid
    return rollS, nodS, 0.5 * (lo + hi)

def taker_pose(K, t, cfg):
    """kaeli receiving: head hangs and rolls slowly, the spine softens, and each
    shoulder yields on ITS OWN hand's press -- the phase contract, her half."""
    rollS, nodS, yieldA = cfg
    u = work_env(t)
    RM = K.RM
    qo = dict(K.base_q); to = dict(K.base_t)
    spine = [b for b in (RM["spine_01"], RM["spine_02"], RM["spine_03"]) if b >= 0]
    breath = math.sin(2.0 * math.pi * t / 3.8)
    for b in spine:
        qo[b] = rotate_bone_world(K.nodes, b, K.lat,
                                  math.radians(SLUMP_T * u + 0.8 * breath) / len(spine), qo)
    if RM["neck_01"] >= 0:
        qo[RM["neck_01"]] = rotate_bone_world(K.nodes, RM["neck_01"], K.lat,
                                              nodS * math.radians(NOD_T) * u, qo)
        qo[RM["neck_01"]] = rotate_bone_world(
            K.nodes, RM["neck_01"], K.fwd,
            math.radians(ROLL_T) * u * math.sin(2.0 * math.pi * t / (KNEAD * 4.0)), qo)
    for side, sd in (("r", 0), ("l", 1)):
        b = K._carrier(side)
        sgn = rollS if side == "r" else -rollS
        qo[b] = rotate_bone_world(K.nodes, b, K.fwd, sgn * yieldA * knead(t, sd) * u, qo)
    return qo, to

# ---------------------------------------------------------------- the giver (reva)
def giver_setup(G):
    def sway_probe(sgn):
        qo = dict(G.base_q)
        for b in (G.RM["spine_01"], G.RM["spine_02"], G.RM["spine_03"]):
            if b >= 0: qo[b] = rotate_bone_world(G.nodes, b, G.fwd, sgn * math.radians(6), qo)
        w, _ = fk_world(G.nodes, qo, G.base_t)
        return G.wp0[G.RM["upperarm_r"]][1] - w[G.RM["upperarm_r"]][1]
    swayS = 1.0 if sway_probe(1.0) > sway_probe(-1.0) else -1.0
    def nod_probe(sgn):
        qo = dict(G.base_q)
        qo[G.RM["neck_01"]] = rotate_bone_world(G.nodes, G.RM["neck_01"], G.lat,
                                                sgn * math.radians(12), qo)
        _w, wq = fk_world(G.nodes, qo, G.base_t)
        return -qrot(wq[G.RM["head"]], [0.0, 0.0, 1.0])[1]
    nodS = 1.0 if nod_probe(1.0) > nod_probe(-1.0) else -1.0
    return swayS, nodS

def giver_body(G, t, cfg):
    """her torso, before the arms: lean in, rock onto the working hand (SPINE, so the
    planted feet never slide), watch her hands."""
    swayS, nodS = cfg
    u = work_env(t)
    RM = G.RM
    qo = dict(G.base_q); to = dict(G.base_t)
    spine = [b for b in (RM["spine_01"], RM["spine_02"], RM["spine_03"]) if b >= 0]
    rock = knead(t, 0) - knead(t, 1)
    for b in spine:
        qo[b] = rotate_bone_world(G.nodes, b, G.lat, math.radians(LEAN_G) * u / len(spine), qo)
        qo[b] = rotate_bone_world(G.nodes, b, G.fwd,
                                  swayS * math.radians(SWAY_G) * u * rock / len(spine), qo)
    if RM["neck_01"] >= 0:
        qo[RM["neck_01"]] = rotate_bone_world(G.nodes, RM["neck_01"], G.lat,
                                              nodS * math.radians(NOD_G) * u, qo)
    return qo, to

def main(pack, ROLE, SCALE, ARMW, PGAP, PPACK, PSCALE, PYAW):
    name = "z_massage" if ROLE == "give" else "z_massage_take"
    G = Body(pack, SCALE)
    if name in G.p.clip_names():
        print("%s already present" % name); return 0

    # ---------------------------------------------------------------- the taker side
    if ROLE == "take":
        cfg = taker_setup(G)
        print("z_massage[take]: roll sign %+g  nod sign %+g  yield %.1f deg for %.3fu drop"
              % (cfg[0], cfg[1], math.degrees(cfg[2]), YIELD_DROP))
        frames = int(T_END * FPS)
        qtr = {}
        shy = {"r": [], "l": []}
        headw = []
        for f in range(frames):
            t = f / FPS
            qo, to = taker_pose(G, t, cfg)
            w, wq = fk_world(G.nodes, qo, to)
            for side in ("r", "l"):
                shy[side].append(G.gw(w[G.RM["upperarm_" + side]])[1])
            headw.append(G.gw(w[G.RM["head"]]))
            bones = [G.RM[k] for k in ("pelvis", "spine_01", "spine_02", "spine_03",
                                       "neck_01", "head", "clavicle_l", "clavicle_r")]
            for b in bones:
                if b is not None and b >= 0:
                    qtr.setdefault(b, []).extend(qnorm(qo.get(b, G.base_q.get(b, G.nodes[b]["q"]))))
        # GATES: she must actually yield, and yield on the right beat
        for side, sd in (("r", 0), ("l", 1)):
            amp = max(shy[side]) - min(shy[side])
            # BOTH SIGNALS ARE PERIODIC: comparing absolute frame numbers compares
            # different cycles and means nothing (the first cut of this gate failed a
            # motion that was exactly right). The yield is judged in PHASE.
            per = KNEAD * FPS
            work = [i for i in range(frames) if work_env(i / FPS) > 0.95]
            lowF = min(work, key=lambda i: shy[side][i])
            pressF = max(work, key=lambda i: knead(i / FPS, sd))
            dphase = (lowF - pressF) % per
            off = min(dphase, per - dphase)
            print("z_massage[take]: %s shoulder yields %.3fu, its low sits %.1f frames "
                  "off that hand's press peak (period %.0f)" % (side, amp, off, per))
            assert amp >= 0.012, "the %s shoulder does not give under the hand" % side
            assert off <= 2.0, "the %s shoulder yields off the beat" % side
        hr = max(h[0] for h in headw) - min(h[0] for h in headw)
        hz = max(h[2] for h in headw) - min(h[2] for h in headw)
        print("z_massage[take]: head travel lateral %.3fu forward %.3fu" % (hr, hz))
        assert hr >= 0.010 and hz >= 0.010, "her head is not relaxing at all"
        tracks = {}
        for (node, typ), v in G.base.items(): tracks[(node, typ)] = v * frames
        for b, vals in qtr.items(): tracks[(b, 0)] = vals
        append_clips(pack, [(name, FPS, frames, tracks)])
        declare(pack, name, {"fps": FPS, "frames": frames, "terminal": "base",
                             "max_speed": 4.0,
                             "contacts": [{"part": "fL", "t": [0.0, T_END], "floor": True, "still_tol": 0.05},
                                          {"part": "fR", "t": [0.0, T_END], "floor": True, "still_tol": 0.05}]})
        print("z_massage[take]: appended (%d frames, %d tracks)" % (frames, len(tracks)))
        return 0

    # ---------------------------------------------------------------- the giver side
    assert PPACK, "the giver needs --ppack (she has to measure the body she is working on)"
    K = Body(PPACK, PSCALE)
    kcfg = taker_setup(K)
    gcfg = giver_setup(G)
    print("z_massage[give]: sway sign %+g  nod sign %+g | her yield %.1f deg"
          % (gcfg[0], gcfg[1], math.degrees(kcfg[2])))

    # HER TRAPEZIUS, in my frame, at rest -- the spot the hands are aimed at
    trapRest = {s: her_to_me(K.gw(K.traps[s]), 0.0, PYAW) for s in ("r", "l")}
    myFront = G.dims["front"] * SCALE
    herBack = (K.dims["back"] if abs(PYAW) < 1.0 else K.dims["front"]) * PSCALE
    # SOLVE THE STANDING DISTANCE: the neutral stroke should sit at REACH_F of the arm
    wl, _ = fk_world(G.nodes, *giver_body(G, T_REACH + KNEAD * 0.25, gcfg))
    shR = G.gw(wl[G.RM["upperarm_r"]])
    want = REACH_F * ARMW
    dx, dy = trapRest["r"][0] - shR[0], trapRest["r"][1] - shR[1]
    rem = want * want - dx * dx - dy * dy
    assert rem > 1e-4, "her shoulder is past this body's arm before any gap at all"
    solved = math.sqrt(rem) + shR[2] - trapRest["r"][2]
    if PGAP <= 0: PGAP = round(solved, 3)
    print("z_massage[give]: SET pgap=%.3f  (solved %.3f for %.0f%% of a %.2fu arm)"
          % (PGAP, solved, REACH_F * 100, ARMW))
    gap = PGAP - herBack - myFront
    print("z_massage[give]: body clearance %.3fu (my front %.2f + her back %.2f vs gap %.2f)"
          % (gap, myFront, herBack, PGAP))
    assert gap >= 0.05, "she would be standing inside the giver's chest"
    for s in ("r", "l"):
        lat = abs(trapRest[s][0])
        assert lat >= 0.06, "the %s hand lands on her neck, not her shoulder" % s

    # HER HEAD CAPSULE IS A HAIRSTYLE. body_capsules takes the p90 perpendicular
    # distance of a bone's flesh -- right for a ribcage, wrong for a head with hair:
    # kaeli's reads 0.280u, a blob that swallows her own shoulders (the measured
    # contact spot sits 0.085u INSIDE it), so an arm laid on her shoulder gets
    # convicted of head collision while never coming near her skull. The skull and
    # neck get LEAN radii here -- p30 of the same flesh, which tracks bone, not hair --
    # and the hair stays passable, the way the hug already treats hair and cloth.
    kverts = K.p.skinned_positions(with_bones=True)
    def skull_radius(q=0.30):
        """the SKULL inside the hair: head-family flesh, p30 of the distance from the
        head bone (hair is the outer majority, so a low percentile finds bone)."""
        fam = {K.RM["head"]}
        grew = True
        while grew:
            grew = False
            for i9, nd9 in enumerate(K.nodes):
                if nd9["parent"] in fam and i9 not in fam:
                    fam.add(i9); grew = True
        ds = sorted(math.dist(v[:3], K.wr[K.RM["head"]]) for v in kverts if v[3] in fam)
        assert ds, "no head flesh"
        return ds[int(len(ds) * q)] * PSCALE
    def neck_radius(q=0.60):
        """the NECK is a narrow cylinder, so it is measured as one: only neck-weighted
        flesh, perpendicular to the neck->head axis. Walking the neck's DESCENDANTS put
        her entire head into a sphere sitting between her shoulders, and that sphere
        then swallowed the very spot the hands are supposed to work."""
        a9 = K.wr[K.RM["neck_01"]]; b9 = K.wr[K.RM["head"]]
        ab = [b9[k] - a9[k] for k in range(3)]
        l2 = sum(x * x for x in ab) or 1e-12
        ds = []
        for v in kverts:
            if v[3] != K.RM["neck_01"]: continue
            t9 = max(0.0, min(1.0, sum((v[k] - a9[k]) * ab[k] for k in range(3)) / l2))
            cp = [a9[k] + ab[k] * t9 for k in range(3)]
            ds.append(math.dist(v[:3], cp))
        if not ds: return 0.5 * skull_radius()
        ds.sort()
        return ds[int(len(ds) * q)] * PSCALE
    SKULL_R = skull_radius()
    NECK_R = neck_radius()
    hairR = [c["r"] * PSCALE for c in K.caps if c["name"] == "head"][0]
    print("z_massage[give]: her skull %.3fu / neck %.3fu (the p90 head capsule is "
          "%.3fu -- that is hair, and arms may pass through it)"
          % (SKULL_R, NECK_R, hairR))
    CAPS_REST = []
    for c in K.caps:
        a9 = her_to_me(K.gw(c["a"]), PGAP, PYAW)
        b9 = her_to_me(K.gw(c["b"]), PGAP, PYAW)
        CAPS_REST.append((c["name"], a9, b9, c["r"] * PSCALE))

    def her_frame(t):
        """her posed body in MY frame: capsules + the two trap spots, carried by the
        clavicles so the contact points travel with her yield."""
        qo, to = taker_pose(K, t, kcfg)
        w, wq = fk_world(K.nodes, qo, to)
        caps = []
        kid = {"pelvis": "spine_01", "spine_01": "spine_02", "spine_02": "spine_03",
               "spine_03": "neck_01", "neck_01": "head", "thigh_l": "calf_l",
               "calf_l": "foot_l", "thigh_r": "calf_r", "calf_r": "foot_r"}
        span2 = abs(K.wp0[K.RM["head"]][1] - K.wp0[K.RM["pelvis"]][1])
        for c in K.caps:
            if c["name"] in ("head", "neck_01"): continue      # hair-inflated; below
            b = K.RM[c["name"]]
            a = list(w[b])
            ch = kid.get(c["name"])
            tp = list(w[K.RM[ch]]) if ch and K.RM.get(ch, -1) >= 0 \
                else [a[0], a[1] + 0.12 * span2, a[2]]
            caps.append((c["name"], her_to_me(K.gw(a), PGAP, PYAW),
                         her_to_me(K.gw(tp), PGAP, PYAW), c["r"] * PSCALE))
        sk9 = her_to_me(K.gw(list(w[K.RM["head"]])), PGAP, PYAW)
        caps.append(("skull", sk9, sk9, SKULL_R))
        nk9 = her_to_me(K.gw(list(w[K.RM["neck_01"]])), PGAP, PYAW)
        caps.append(("neck", nk9, sk9, NECK_R))
        traps = {}
        for s in ("r", "l"):
            cb = K._carrier(s)
            off = qrot(wq[cb], K.trapLoc[s])
            traps[s] = her_to_me(K.gw([w[cb][k] + off[k] for k in range(3)]), PGAP, PYAW)
        return caps, traps, w

    def cap_pen(px, py, pz, CAPS, skip=()):
        worst, who = 0.0, ""
        for (nm9, a9, b9, r9) in CAPS:
            if nm9 in skip: continue
            ab = [b9[k] - a9[k] for k in range(3)]
            l2 = sum(v * v for v in ab) or 1e-12
            u9 = max(0.0, min(1.0, ((px - a9[0]) * ab[0] + (py - a9[1]) * ab[1]
                                    + (pz - a9[2]) * ab[2]) / l2))
            cp = [a9[k] + ab[k] * u9 for k in range(3)]
            d9 = math.dist([px, py, pz], cp)
            if r9 - d9 > worst: worst, who = r9 - d9, nm9
        return worst, who

    armR = (G.RM["upperarm_r"], G.RM["lowerarm_r"], G.RM["hand_r"])
    armL = (G.RM["upperarm_l"], G.RM["lowerarm_l"], G.RM["hand_l"])
    poleR, poleL = [-0.85, 0.70, -0.40], [0.85, 0.70, -0.40]
    _wqr = G.wqr
    def hand_local_dir(chain):
        fa = _n([G.wr[chain[2]][k] - G.wr[chain[1]][k] for k in range(3)])
        return qrot(qinv(_wqr[chain[2]]), fa)
    hdirR, hdirL = hand_local_dir(armR), hand_local_dir(armL)
    # HOW LONG IS THE HAND? On both club rigs the "hand" node IS THE WRIST -- a leaf,
    # with the whole palm hanging off it as flesh only. IK-ing that node onto her
    # trapezius parked the WRIST on the muscle and swung the entire palm past it, over
    # her collarbone and across her FACE. The palm is what touches, so its offset is
    # measured from the giver's own hand cloud and the IK target is pulled back by it.
    def hand_len(side):
        hb9 = G.RM["hand_" + side]; fb9 = G.RM["lowerarm_" + side]
        cl9 = [v[:3] for v in G.p.skinned_positions(with_bones=True) if v[3] == hb9]
        if not cl9: return 0.0
        f9 = _n([G.wr[hb9][k] - G.wr[fb9][k] for k in range(3)])
        return max(sum((v[k] - G.wr[hb9][k]) * f9[k] for k in range(3)) for v in cl9)
    PALM = {s9: 0.45 * hand_len(s9) * SCALE for s9 in ("r", "l")}
    print("z_massage[give]: palm centre sits %.3fu / %.3fu past the wrist (r/l) -- the "
          "IK target is pulled back by that so the PALM lands on her trapezius"
          % (PALM["r"], PALM["l"]))
    restR, restL = list(G.wp0[armR[2]]), list(G.wp0[armL[2]])

    frames = int(T_END * FPS)
    qtr = {}
    worst_pen = [0.0, -1.0, ""]
    contact = {"r": [], "l": []}
    travel = {"r": 0.0, "l": 0.0}
    prevh = {"r": None, "l": None}
    reported = False
    for f in range(frames):
        t = f / FPS
        u = work_env(t)
        qo, to = giver_body(G, t, gcfg)
        caps, traps, _kw = her_frame(t)
        tgtw = {}
        for side, sd, out in (("r", 0, -1.0), ("l", 1, +1.0)):
            th = orbit(t, sd)
            p = knead(t, sd)
            base = traps[side]
            hover = 0.16 * (1.0 - u)            # the hands come in from above, not through
            palm = [base[0] + out * ORB_X * math.cos(th),
                    base[1] - PRESS * p * u + LIFT * (1.0 - p) * u + hover,
                    base[2] + ORB_Z * math.sin(th) * u]
            aim9 = _n([0.12 * out, -0.45, 0.88])
            tgtw[side] = [palm[k] - PALM[side] * aim9[k] * u for k in range(3)]
        for side, chain, rest, pole in (("r", armR, restR, poleR), ("l", armL, restL, poleL)):
            w_t = tgtw[side]
            rigt = G.rig(w_t)
            cx = profile([(0.0, rest[0], ease_smooth), (1.0, rigt[0], None)], u)
            cy = profile([(0.0, rest[1], ease_smooth), (1.0, rigt[1], None)], u)
            cz = profile([(0.0, rest[2], ease_smooth), (1.0, rigt[2], None)], u)
            qo.update(solve_limb(G.nodes, chain, [cx, cy, cz], pole=pole,
                                 q_override=qo, t_override=to))
        # the hands lie ALONG the top of her shoulders: fingers forward over her
        # collarbone, tilted down -- driven from the measured local hand direction
        # (both club rigs are fingerless, so the palm roll cannot be audited from
        # knuckles the way z_hi5 does it; the drape direction is what reads)
        if u > 0.35:
            _w, _wq = fk_world(G.nodes, qo, to)
            k5 = min(1.0, (u - 0.35) / 0.40)
            for side, chain, hdl in (("r", armR, hdirR), ("l", armL, hdirL)):
                out = -1.0 if side == "r" else 1.0
                aim = _n([0.12 * out, -0.45, 0.88])
                f5 = _n(qrot(_wq[chain[2]], hdl))
                wq_t = qmul(q_between(f5, aim), _wq[chain[2]])
                qo[chain[2]] = hold_world_rotation(
                    G.nodes, chain[2], qslerp(_wq[chain[2]], wq_t, k5), qo, to)

        w9, _ = fk_world(G.nodes, qo, to)
        # ---- gates ----
        _wq9 = fk_world(G.nodes, qo, to)[1]
        for side, chain in (("r", armR), ("l", armL)):
            hdl9 = hdirR if side == "r" else hdirL
            fwd9 = _n(qrot(_wq9[chain[2]], hdl9))
            hw = G.gw(w9[chain[2]])
            hw = [hw[k] + PALM[side] * fwd9[k] for k in range(3)]
            if prevh[side] is not None and T_REACH <= t <= T_WORK_END:
                travel[side] += math.dist(hw, prevh[side])
            prevh[side] = hw
            if T_REACH + 0.1 <= t <= T_WORK_END - 0.1:
                contact[side].append((t, hw[1] - traps[side][1],
                                      math.hypot(hw[0] - traps[side][0], hw[2] - traps[side][2])))
            # the ARM must not pass through her; the last 15% is the hand, which is
            # supposed to be in contact, so it is measured by the contact gate instead
            for a5, b5 in ((chain[0], chain[1]), (chain[1], chain[2])):
                for i5 in range(1, 8):
                    q5 = i5 / 8.0 * (0.85 if b5 == chain[2] else 1.0)
                    px = G.gw([w9[a5][k] + (w9[b5][k] - w9[a5][k]) * q5 for k in range(3)])
                    dep, who = cap_pen(px[0], px[1], px[2], caps)
                    if dep > worst_pen[0]:
                        seg9 = "upper" if b5 == chain[1] else "fore"
                        worst_pen[:] = [dep, t, "%s %sarm into her %s" % (side, seg9, who)]
        if not reported and t >= T_REACH + KNEAD * 0.5:
            reported = True
            for side in ("r", "l"):
                hw = G.gw(w9[armR[2] if side == "r" else armL[2]])
                print("z_massage[give]: %s hand (%.3f,%.3f,%.3f) vs her trap (%.3f,%.3f,%.3f)"
                      % (side, hw[0], hw[1], hw[2], traps[side][0], traps[side][1], traps[side][2]))
            hh = her_to_me(K.gw(_kw[K.RM["upperarm_r"]]), PGAP, PYAW)
            mh = G.gw(w9[armR[2]])
            print("z_massage[give]: PREDICT engine [rub] hand-to-her-shoulder %.3fu"
                  % math.dist(mh, hh))
        bones = [G.RM[k] for k in ("pelvis", "spine_01", "spine_02", "spine_03",
                                   "neck_01", "head")] + list(armR) + list(armL)
        for b in bones:
            if b is not None and b >= 0:
                qtr.setdefault(b, []).extend(qnorm(qo.get(b, G.base_q.get(b, G.nodes[b]["q"]))))

    for side in ("r", "l"):
        vy = [c[1] for c in contact[side]]
        lat = max(c[2] for c in contact[side])
        print("z_massage[give]: %s palm vs her skin: %+.3f .. %+.3f u (want inside "
              "[-0.04,+0.02]), worst lateral slip %.3fu, stroke travel %.3fu"
              % (side, min(vy), max(vy), lat, travel[side]))
        assert min(vy) >= -0.045, "the %s hand presses through her shoulder" % side
        assert max(vy) <= 0.025, "the %s hand floats above her shoulder" % side
        assert lat <= 0.075, "the %s hand slides off her trapezius" % side
        assert travel[side] >= 0.25, "the %s hand is resting, not kneading" % side
    print("z_massage[give]: worst arm penetration %.3fu at t=%.2f %s (skin 0.030)"
          % (worst_pen[0], worst_pen[1], worst_pen[2]))
    assert worst_pen[0] <= 0.030, "an arm passes THROUGH her, not over her"

    tracks = {}
    for (node, typ), v in G.base.items(): tracks[(node, typ)] = v * frames
    for b, vals in qtr.items(): tracks[(b, 0)] = vals
    append_clips(pack, [(name, FPS, frames, tracks)])
    declare(pack, name, {"fps": FPS, "frames": frames, "terminal": "base",
                         "max_speed": 4.0,
                         "contacts": [{"part": "fL", "t": [0.0, T_END], "floor": True, "still_tol": 0.05},
                                      {"part": "fR", "t": [0.0, T_END], "floor": True, "still_tol": 0.05}]})
    print("z_massage[give]: appended (%d frames, %d tracks)" % (frames, len(tracks)))
    return 0

if __name__ == "__main__":
    a = sys.argv[1:]
    def arg(nm, d):
        return float(a[a.index(nm) + 1]) if nm in a else d
    role = a[a.index("--role") + 1] if "--role" in a else "give"
    pp = a[a.index("--ppack") + 1] if "--ppack" in a else ""
    if role == "give" and not pp:
        print("z_massage: pair score -- the giver needs --ppack; skipping")
        sys.exit(0)
    sys.exit(main(a[0], role, arg("--scale", 1.0), arg("--arm", 1.0), arg("--pgap", 0.0),
                  pp, arg("--pscale", 1.0), arg("--pyaw", 0.0)))
