#!/usr/bin/env python3
"""z_hug.py -- P7b: THE HUG. The targets live ON THE OTHER BODY.

jacob: "can you have the two of them hug now?"

The five shared one point in the air between two bodies. A hug is the next class up:
each wrist must land on the PARTNER -- the right arm wraps over their left shoulder to
their upper back, the left arm around their waist side to their mid back -- and the two
bodies stand close enough that every centimetre is negotiated. Neither body can see the
other, so the partner exists in this score only as numbers: how far away their back
plane is, how high their shoulder and mid-back sit. Both come from the engine's own
geometry report ([hi5] lines), the same authority the five used.

MIRROR SYMMETRY DOES THE CHOREOGRAPHY. Both bodies play the same shape: right arm over,
left arm under. Facing each other, my right-over lands on their left side exactly where
their left-under leaves room, and vice versa -- over/under alternates around the ring by
construction, with no coordination and no second variant to author.

The heads tilt to the body's own right, so the two tilts are complementary and the
faces pass. That, too, is mirror symmetry doing the work.

usage: z_hug.py <pack> --scale S --arm A --pback D --pshoulder H --pmid M
    S: the ENGINE's scale for this pack (hero-path mesh sizing)
    A: this body's arm length, world units (engine [hi5] geometry line)
    D: world distance from this body's origin to the PARTNER'S BACK PLANE
       (the bench's pgap + ~0.12u of torso depth)
    H: the partner's shoulder height, world units
    M: the partner's mid-back height, world units
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips
from authorlib import (rig_map, fk_world, solve_limb, rotate_bone_world, profile,
                       ease_smooth, ease_anticipate, qnorm, declare,
                       hold_world_rotation, qslerp, qmul, qrot, q_between)

def qinv_(q):
    return [-q[0], -q[1], -q[2], q[3]]

FPS = 30.0
T_OPEN, T_WRAP, T_HOLD_END, T_END = 0.55, 1.25, 3.10, 4.20
LEAN = 6.0          # degrees forward -- close bodies; a deep lean would clash heads
TILT = 18.0         # head tilt to the body's own right; the two tilts are complementary

def _n(v):
    l = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / l for x in v]

def main(pack_path, SCALE, ARMW, PGAP, PPACK, PSCALE):
    p = Rpack(pack_path)
    if "z_hug" in p.clip_names():
        print("z_hug already present"); return 0
    nodes = p.nodes()
    RM = rig_map(p)
    assert not RM["_missing"], "rig incomplete: %s" % RM["_missing"]
    names_c = p.clip_names()
    base_clip = "z_upright" if "z_upright" in names_c else RM["_base_clip"]
    base_q, base_t, base = {}, {}, {}
    if base_clip:
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
    armR = (RM["upperarm_r"], RM["lowerarm_r"], RM["hand_r"])
    armL = (RM["upperarm_l"], RM["lowerarm_l"], RM["hand_l"])
    fwd = [0.0, 0.0, 1.0]        # pack-space forward: the engine's contract, on pixels
    lat = [1.0, 0.0, 0.0]        # the body's LEFT

    wp0, _ = fk_world(nodes, base_q, base_t)
    bones = [i for i in range(len(nodes)) if nodes[i].get("kind") == 1] or list(range(len(nodes)))
    wr, _ = fk_world(nodes)
    mny = min(wr[i][1] for i in bones)          # the engine's ground datum

    # Reach is owned by the CROSSING SOLVER below -- it slides each landing spot along
    # the back until it is both reachable (<= 0.90 arm) and clean of the proxy. The
    # older whole-target clamp that lived here pulled the target toward the shoulder
    # ALONG ITS OWN DIRECTION, which quietly shortened its DEPTH: on bgirl (1.131u to
    # a 0.99u arm) it dragged the landing spot from the back plane at 0.52 to 0.39,
    # and her hand then hit that mangled spot exactly -- err 0.000, wrap FAIL. A spot
    # that must move moves ACROSS the back, never off it.


    # v5 (homecoming H5): the partner is a set of MEASURED CAPSULES -- her own pack is
    # opened and body_capsules() reads segment+radius per canon bone from her skinned
    # vertices' dominant weights. No proxy constant survives in this score: front and
    # back extents, shoulder height, mid-back height and every collision answer come
    # from her body. The capsules arrive mirrored into MY frame (she faces me at pyaw
    # 180, her origin at PGAP): (x,y,z)_her -> (-x, y, PGAP - z)_mine, world units,
    # grounded on her own datum.
    from rpack import Rpack as _Rp
    from authorlib import body_capsules as _bc, rig_map as _rm2, fk_world as _fk2
    p2 = _Rp(PPACK)
    RM2 = _rm2(p2)
    nodes2 = p2.nodes()
    wr2, _ = _fk2(nodes2)
    mny2 = min(w[1] for w in wr2)
    CAPS = []
    for c9 in _bc(p2, RM2):
        a9 = [-c9["a"][0] * PSCALE, (c9["a"][1] - mny2) * PSCALE, PGAP - c9["a"][2] * PSCALE]
        b9 = [-c9["b"][0] * PSCALE, (c9["b"][1] - mny2) * PSCALE, PGAP - c9["b"][2] * PSCALE]
        CAPS.append((c9["name"], a9, b9, c9["r"] * PSCALE))
    def cap_pen(px, py, pz):
        """penetration depth (world u) of a point into the partner's capsule set"""
        worst9 = 0.0
        for (_n9, a9, b9, r9) in CAPS:
            ab = [b9[k] - a9[k] for k in range(3)]
            l2 = sum(v * v for v in ab) or 1e-12
            u9 = max(0.0, min(1.0, ((px - a9[0]) * ab[0] + (py - a9[1]) * ab[1] + (pz - a9[2]) * ab[2]) / l2))
            cp = [a9[k] + ab[k] * u9 for k in range(3)]
            d9 = math.sqrt((px - cp[0]) ** 2 + (py - cp[1]) ** 2 + (pz - cp[2]) ** 2)
            if r9 - d9 > worst9: worst9 = r9 - d9
        return worst9
    # her geometry, derived not passed: shoulder height, spine axis, torso extents
    PSH = ((wr2[RM2["upperarm_l"]][1] + wr2[RM2["upperarm_r"]][1]) / 2 - mny2) * PSCALE
    PMID = 0.80 * PSH
    tor = [c for c in CAPS if c[0] in ("pelvis", "spine_01", "spine_02", "spine_03")]
    PFR2 = max(PGAP - min(c[1][2] - c[3], c[2][2] - c[3]) for c in tor)   # toward me
    PBK2 = max(max(c[1][2] + c[3], c[2][2] + c[3]) for c in tor) - PGAP   # away
    RX2 = max(max(abs(c[1][0]), abs(c[2][0])) + c[3] for c in tor)
    # THE CHEST GATE uses the DIRECTIONAL band measure, not the capsules: a torso's
    # cross-section is not a circle, and a radius that must cover the wide sides
    # overstates the front by ~50% (kaeli read 0.31 front by capsule vs 0.19 by band).
    # Capsules keep the job they are honest at -- the wrap goes around the SIDES and
    # BACK, where the radius is real.
    from authorlib import body_dims as _bd
    dme, dher = _bd(p, RM), _bd(p2, RM2)
    MYFR2, PFRD = dme["front"] * SCALE, dher["front"] * PSCALE
    print("z_hug: partner capsules: %d  PSH %.2f  cap extents fr %.2f bk %.2f rx %.2f  chest fronts me %.2f her %.2f"
          % (len(CAPS), PSH, PFR2, PBK2, RX2, MYFR2, PFRD))

    # THE CHEST GATE, band-measured fronts
    need = MYFR2 + PFRD - 0.05 + 0.04
    print("z_hug: chest gate: pgap %.2f vs needed %.2f" % (PGAP, need))
    assert PGAP >= need, "the bodies stand chest-inside-chest at this gap"
    ZC, RX, RZB, RZF, PBACK = PGAP, RX2, PBK2, PFR2, PGAP + PBK2
    # transit clearance from the WIDEST capsule, not the torso: reva's skirt rides her
    # thigh bones and reaches 0.59u laterally -- a release swing routed at torso width
    # brushed straight through it (the gate caught 0.114u into the thigh capsule)
    SIDE_CL = max(max(abs(c[1][0]), abs(c[2][0])) + c[3] for c in CAPS) + 0.08

    def spine_axis_at(yw):
        """her spine axis position + local radius at world height yw, my frame"""
        best9 = None
        for (nm9, a9, b9, r9) in tor + [c for c in CAPS if c[0] == "neck_01"]:
            lo9, hi9 = min(a9[1], b9[1]), max(a9[1], b9[1])
            mid9 = 0.5 * (lo9 + hi9)
            sc9 = abs(yw - mid9)
            if best9 is None or sc9 < best9[0]:
                u9 = 0.0 if hi9 == lo9 else max(0.0, min(1.0, (yw - a9[1]) / ((b9[1] - a9[1]) or 1e-9)))
                ax9 = [a9[k] + (b9[k] - a9[k]) * u9 for k in range(3)]
                best9 = (sc9, ax9, r9)
        return best9[1], best9[2]

    # the seed targets carry only HEIGHTS; the solver owns x and z
    over  = [0.0, mny + (PSH - 0.06) / SCALE, (ZC + PBK2) / SCALE]
    under = [0.0, mny + PMID / SCALE,         (ZC + PBK2) / SCALE]

    # HOW FAR ACROSS CAN THIS ARM WRAP? Not a constant: kaeli's 0.84u arm cannot lie
    # flat to the far side of reva's back -- at any fixed far target her forearm chords
    # through the torso (the gate measured 0.083u through the mid-back). So the
    # crossing is SOLVED: try candidate under-arm landing spots from far-side to
    # near-side, solve the HOLD pose for each, measure the forearm against the proxy,
    # and keep the farthest spot whose worst penetration is skin-deep. Long arms wrap
    # far; short arms wrap less; both lie ON the body by construction.
    # the solver works from the LEANED pose -- the pose the hold actually plays. From
    # the upright stand, bgirl's high held-back shoulders put every landing spot past
    # her arm; the 6-degree lean-in carries them ~0.15u forward, which is exactly how
    # a real hugger closes the same distance.
    lean_q = dict(base_q)
    lean_q[pelvis] = rotate_bone_world(nodes, pelvis, lat, math.radians(LEAN * 0.3), lean_q)
    for _b in spine:
        lean_q[_b] = rotate_bone_world(nodes, _b, lat, math.radians(LEAN / max(1, len(spine))), lean_q)
    wpl, _ = fk_world(nodes, lean_q, base_t)

    def hold_penetration(chain, tgt, pole):
        qh = dict(lean_q)
        upd = solve_limb(nodes, chain, tgt, pole=pole, q_override=qh, t_override=dict(base_t))
        qh.update(upd)
        wh, _ = fk_world(nodes, qh, base_t)
        worst = 0.0
        for a6, b6 in ((chain[0], chain[1]), (chain[1], chain[2])):
            for i6 in range(1, 8):
                q6 = i6 / 8.0
                px = (wh[a6][0] + (wh[b6][0] - wh[a6][0]) * q6) * SCALE
                py = (wh[a6][1] - mny + (wh[b6][1] - wh[a6][1]) * q6) * SCALE
                pz = (wh[a6][2] + (wh[b6][2] - wh[a6][2]) * q6) * SCALE
                d6 = cap_pen(px, py, pz)
                if d6 > worst: worst = d6
        return worst

    def solve_crossing(nm7, chain, tgt, pole, side):
        """Where does this hand land? Candidates lie ON THE PARTNER'S SURFACE,
        parametrized by wrap angle psi: 0 = square on the spine, +/-90 = on her side.
        Sliding along the back PLANE could not express a short-reach hugger at all --
        bgirl's high wide shoulders (2.88u, x 0.26) put every point of the plane past
        her 0.99u arm, while the side-back corner of the partner's body was reachable
        and clean the whole time. Farthest-around candidate wins; landing higher on
        the back (dy) is tried before landing less far around. `side` = the sign of
        this arm's far-across direction."""
        sh6 = wpl[chain[0]]
        # farthest-around FIRST (psi most negative on this arm's side): a long arm
        # earns a deep wrap, a short one settles for the side-back corner
        # the dy ladder reaches +0.30: a TALL hugger's under-arm lands high on a
        # short partner's back -- bgirl's 2.89u shoulders over F05's 1.99u mid-back
        # leave a 0.90u drop that no wrap angle can shorten
        for dy in (0.0, 0.06, 0.12, 0.18, 0.24, 0.30):
            for psideg in range(72, -48, -8):
                psi = math.radians(-psideg * side)
                yw9 = (tgt[1] - mny) * SCALE + dy
                ax9, r9 = spine_axis_at(yw9)
                cand = [(ax9[0] + (r9 + 0.01) * math.sin(psi)) / SCALE,
                        tgt[1] + dy / SCALE,
                        (ax9[2] + (r9 + 0.01) * math.cos(psi)) / SCALE]
                d6 = math.dist(sh6, cand) * SCALE
                if d6 > 0.90 * ARMW: continue
                if hold_penetration(chain, cand, pole) <= 0.028:
                    tgt[:] = cand
                    print("z_hug: %s lands at psi=%+d deg dy=+%.2f (reach %.2f of %.2f arm)"
                          % (nm7, psideg * side, dy, d6, ARMW))
                    return psi
        raise AssertionError("no %s landing spot clears the partner's torso" % nm7)

    psiL = solve_crossing("under-arm", armL, under, [+0.70, -0.35, 0.75], -1)
    psiR = solve_crossing("over-arm", armR, over, [-0.60, 0.28, 0.55], +1)

    # THE HAND LIES ALONG THE SURFACE IT LANDED ON (jacob's close-up: the under hand
    # sat twisted on the spine). The old lay directions were constants tuned for
    # spine landings; the spots now move per body, so the fingers follow the wrap
    # TANGENT at the solved angle -- continuing around the torso, a touch downward --
    # and the palm presses along the inward normal by construction.
    def lay_dir(psi, side):
        tx, tz = RX * math.cos(psi), -RZB * math.sin(psi)
        tl = math.hypot(tx, tz) or 1.0
        tx, tz = tx / tl, tz / tl
        if tx * (-side) < 0: tx, tz = -tx, -tz     # fingers continue ACROSS, not back
        return _n([tx, -0.28, tz])
    layR, layL = lay_dir(psiR, +1), lay_dir(psiL, -1)

    # measured local hand directions (a neutral wrist continues the forearm)
    _wqr0 = fk_world(nodes)[1]
    def _hdl(chain):
        fa = _n([wr[chain[2]][k] - wr[chain[1]][k] for k in range(3)])
        return qrot(qinv_(_wqr0[chain[2]]), fa)
    hdirR, hdirL = _hdl(armR), _hdl(armL)

    frames = int(T_END * FPS)
    qtr = {}
    restR, restL = list(wp0[armR[2]]), list(wp0[armL[2]])
    checked = False
    worst_pen = [0.0, -1.0, ""]
    for f in range(frames):
        t = f / FPS
        # open the arms slightly on approach, wrap, hold with a breathing squeeze, release
        u = profile([(0.0, 0.0, ease_anticipate), (T_OPEN, 0.30, ease_smooth),
                     (T_WRAP, 1.0, ease_smooth), (T_HOLD_END, 1.0, ease_smooth),
                     (T_END - 0.12, 0.0, None)], t)
        squeeze = 0.0
        if T_WRAP <= t <= T_HOLD_END:
            squeeze = 0.5 - 0.5 * math.cos((t - T_WRAP) / 0.9 * 2 * math.pi)
        qo = dict(base_q); to = dict(base_t)
        qo[pelvis] = rotate_bone_world(nodes, pelvis, lat, math.radians(LEAN * 0.3) * u, qo)
        for b in spine:
            qo[b] = rotate_bone_world(nodes, b, lat,
                                      math.radians((LEAN + 1.5 * squeeze) / max(1, len(spine))) * u, qo)
        # the head: forward with the lean, tilted to its own right so the faces pass.
        # The tilt is a roll applied at the NECK, not the head -- rolling the head bone
        # about an axis through its own origin moves nothing measurable (the tiptoe law:
        # a bone cannot move by rotating about its own origin; the first bake proved it
        # again, head lateral -0.000).
        if neck >= 0:
            qo[neck] = rotate_bone_world(nodes, neck, lat, math.radians(4.0) * u, qo)
            qo[neck] = rotate_bone_world(nodes, neck, fwd, math.radians(TILT * 0.6) * u, qo)
        qo[headb] = rotate_bone_world(nodes, headb, fwd, math.radians(TILT * 0.6) * u, qo)

        # v2 ROUTED WRAPS (jacob: "collision is not being followed... how they lay
        # across the bodies"). The IK solves the wrist to a POINT; a straight-line
        # approach drags the FOREARM through the partner. Each arm now follows
        # per-axis waypoints: the over-arm climbs above the shoulder line before
        # crossing, the under-arm swings wide of the torso ellipse and only then
        # slides in across the back.
        # the poles pull the ELBOWS backward around her: a 2-bone chain is straight, so
        # a forearm can only lie flat ALONG the back if the elbow is behind her side
        # too -- with the elbow in front, the chord to a far-side target must clip the
        # torso's corner (the gate measured exactly that at the hold, 0.052/0.097u).
        for nm2, chain, tgt, rest, pole in (
                ("over", armR, over, restR, [-0.60, 0.28, 0.55]),
                ("under", armL, under, restL, [+0.70, -0.35, 0.75])):
            gz = tgt[2] + (0.015 * squeeze) / SCALE            # the squeeze presses in
            if nm2 == "over":
                # up in FRONT, forward past the back plane WHILE HIGH, then straight
                # down onto the upper back -- descending in front clipped the chest
                cx = profile([(0.0, rest[0], ease_smooth), (1.0, tgt[0], None)], u)
                cy = profile([(0.0, rest[1], ease_smooth),
                              (0.45, mny + (PSH + 0.16) / SCALE, ease_smooth),
                              (0.62, mny + (PSH + 0.14) / SCALE, ease_smooth),
                              (1.0, tgt[1], None)], u)
                cz = profile([(0.0, rest[2], ease_smooth),
                              (0.62, (PBACK + 0.03) / SCALE, ease_smooth),
                              (1.0, gz, None)], u)
            else:
                # wide of the ellipse, forward to BEYOND the back plane while still
                # wide, and only then across -- crossing inside the plane dragged the
                # forearm chord through the torso's corner
                cx = profile([(0.0, rest[0], ease_smooth),
                              (0.42, SIDE_CL / SCALE, ease_smooth),
                              (0.74, (SIDE_CL - 0.05) / SCALE, ease_smooth),
                              (1.0, tgt[0], None)], u)
                cy = profile([(0.0, rest[1], ease_smooth), (1.0, tgt[1], None)], u)
                cz = profile([(0.0, rest[2], ease_smooth),
                              (0.42, (ZC + 0.02) / SCALE, ease_smooth),
                              (0.74, (PBACK + 0.025) / SCALE, ease_smooth),
                              (1.0, gz, None)], u)
            upd = solve_limb(nodes, chain, [cx, cy, cz], pole=pole, q_override=qo, t_override=to)
            qo.update(upd)

        # THE HANDS LAY ACROSS THE BACK. At the hold each hand lies TANGENT to the
        # back surface -- fingers continuing the wrap direction, flat on the body,
        # not poking into it. Driven from the measured local hand direction (neither
        # rig has bones past the wrist), blended in with the wrap.
        if u > 0.55:
            _w, _wq = fk_world(nodes, qo, to)
            k5 = min(1.0, (u - 0.55) / 0.35)
            for chain, dirv, hdl in ((armR, layR, hdirR),
                                     (armL, layL, hdirL)):
                f5 = _n(qrot(_wq[chain[2]], hdl))
                A5 = q_between(f5, dirv)
                wq_t = qmul(A5, _wq[chain[2]])
                qo[chain[2]] = hold_world_rotation(
                    nodes, chain[2], qslerp(_wq[chain[2]], wq_t, k5), qo, to)

        w9, _ = fk_world(nodes, qo, to)

        # THE COLLISION GATE, every frame: both arms' segments against the torso
        # ellipse and the head cylinder. Skin contact is allowed -- laying ON a body
        # IS contact; passing THROUGH it is the defect.
        for chain in (armR, armL):
            for a5, b5 in ((chain[0], chain[1]), (chain[1], chain[2])):
                for i5 in range(1, 8):
                    q5 = i5 / 8.0
                    px = (w9[a5][0] + (w9[b5][0] - w9[a5][0]) * q5) * SCALE
                    py = (w9[a5][1] - mny + (w9[b5][1] - w9[a5][1]) * q5) * SCALE
                    pz = (w9[a5][2] + (w9[b5][2] - w9[a5][2]) * q5) * SCALE
                    tag = ("R" if chain is armR else "L") + \
                          ("-upper" if a5 == chain[0] else "-fore")
                    dep = cap_pen(px, py, pz)
                    if dep > worst_pen[0]:
                        worst_pen[:] = [dep, t, "%s at (%.2f,%.2f,%.2f)" % (tag, px, py, pz)]
        if not checked and t >= (T_WRAP + T_HOLD_END) / 2:
            checked = True
            # THE WRAP GATE: at the hold, each wrist is AT the partner's back plane --
            # beyond the space between the bodies, which is what "arms around" means --
            # and the head tilt really went to the body's own right.
            for nm, chain, tgt in (("over(R)", armR, over), ("under(L)", armL, under)):
                wz = w9[chain[2]][2] * SCALE
                err = math.dist(w9[chain[2]], tgt) * SCALE
                print("z_hug: %s wrist depth %.3fu (back plane %.3fu)  target err %.3fu"
                      % (nm, wz, PBACK, err))
                ax8, _r8 = spine_axis_at((w9[chain[2]][1] - mny) * SCALE)
                assert wz >= ax8[2] - 0.04, "%s does not wrap -- hand short of her spine line" % nm
                assert err <= 0.09, "%s missed its spot on the partner's back" % nm
            hx = (w9[headb][0] - wp0[headb][0]) * SCALE
            print("z_hug: head moved %.3fu lateral (want < 0 = its own right)" % hx)
            assert hx < -0.015, "head did not tilt to its own right; faces would clash"

        chain_all = [pelvis, neck, headb] + list(spine) + list(armR) + list(armL)
        for b in chain_all:
            if b is not None and b >= 0:
                qtr.setdefault(b, []).extend(qnorm(qo.get(b, base_q.get(b, nodes[b]["q"]))))

    print("z_hug: worst arm penetration %.3fu at t=%.2f %s (allowance 0.030 = skin)"
          % (worst_pen[0], worst_pen[1], worst_pen[2]))
    assert worst_pen[0] <= 0.030, "an arm passes THROUGH the partner, not across her"

    tracks = {}
    for (node, typ), v in base.items():
        tracks[(node, typ)] = v * frames
    for b, vals in qtr.items(): tracks[(b, 0)] = vals
    append_clips(pack_path, [("z_hug", FPS, frames, tracks)])
    declare(pack_path, "z_hug", {
        "fps": FPS, "frames": frames, "terminal": "base", "max_speed": 6.0,
        "contacts": [{"part": "fL", "t": [0.0, T_END], "floor": True, "still_tol": 0.06},
                     {"part": "fR", "t": [0.0, T_END], "floor": True, "still_tol": 0.06}],
    })
    print("z_hug: appended (%d frames, %d tracks)" % (frames, len(tracks)))
    return 0

if __name__ == "__main__":
    a = sys.argv[1:]
    def arg(nm, d):
        return float(a[a.index(nm) + 1]) if nm in a else d
    pp = a[a.index("--ppack") + 1] if "--ppack" in a else ""
    sys.exit(main(a[0], arg("--scale", 1.0), arg("--arm", 1.0), arg("--pgap", 0.5),
                  pp, arg("--pscale", 1.0)))
