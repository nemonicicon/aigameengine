#!/usr/bin/env python3
"""z_hi5.py -- P7: THE HIGH FIVE. The first move whose target belongs to NEITHER body.

jacob: "lets see kaeli and reva interacting. how about a high five?"

Every authored move until now has been one body alone, and every contact has been with
the floor -- which is easy, because the floor is at a known height and never moves. A
five is the other kind: the contact point lives in the WORLD, between two bodies, and
each body can only be posed in its own rig space. Neither one can see the other.

The resolution is that both halves solve to the same rule rather than to each other:

    reach straight forward to REACH_F of your own arm's length,
    at CONTACT_H of your own shoulder height,
    and let the engine stand you REACH_reva + REACH_kaeli apart.

Each body measures its own proportions, so a tall reva and a shorter kaeli meet in the
middle without either score knowing the other's numbers. This script therefore prints
the WORLD reach it authored (rig reach x the pack's own scale, computed exactly as the
engine computes it) -- and the sum of the two prints is the `pgap=` the bench sets. The
engine then measures the actual hand-to-hand distance in flight (`hi5=1`), which is the
gate: the score claims a geometry, and the running game confirms or refuses it.

Timing: wind up, swing through, contact, recoil, settle. The contact is a MOMENT, not a
pose held -- the hands meet at CONTACT_T and separate again, so a still frame at the
wrong instant is not evidence either way.

usage: z_hi5.py <pack>      (authors 'z_hi5' into whichever body's pack it is given)
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips
from authorlib import (rig_map, fk_world, solve_limb, rotate_bone_world, profile,
                       ease_smooth, ease_settle, ease_anticipate, qnorm, declare,
                       find_hands, HandRig, hold_world_rotation, q_between, qmul, qrot,
                       qslerp)


def qinv_(q):
    return [-q[0], -q[1], -q[2], q[3]]

def _n(v):
    l = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / l for x in v]

def _cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]

def _dot(a, b):
    return sum(x*y for x, y in zip(a, b))

def hand_frame(w, spec, hand):
    """(fingers direction, across-palm direction) in world, from the hand's own bones."""
    fg = spec["fingers"]; tp = spec["tips"]
    lead = "middle" if "middle" in fg else sorted(k for k in fg if k != "thumb")[0]
    tip = tp.get(lead, fg[lead][-1])
    f = _n([w[tip][k] - w[hand][k] for k in range(3)])
    if "index" in fg and "pinky" in fg:
        a, b = fg["index"][0], fg["pinky"][0]
    else:
        ks = [v[0] for k, v in fg.items() if k != "thumb"]
        a, b = ks[0], ks[-1]
    s = _n([w[a][k] - w[b][k] for k in range(3)])
    return f, s

def palm_to(f0, n0, up, aim):
    """World rotation taking a hand whose fingers point f0 and whose palm faces n0 to
    fingers along `up` and palm along `aim`. Two shortest arcs: raise, then twist."""
    A1 = q_between(f0, up)
    n1 = qrot(A1, n0)
    n1p = _n([n1[k] - up[k] * _dot(n1, up) for k in range(3)])
    aimp = _n([aim[k] - up[k] * _dot(aim, up) for k in range(3)])
    return qmul(q_between(n1p, aimp), A1)

FPS = 30.0
T_WIND, T_CONTACT, T_END = 0.55, 1.00, 2.20      # seconds
REACH_F = 0.86        # fraction of the arm's own length, from the shoulder, at contact
ELEV = 28.0           # degrees above horizontal -- a five lands high, not straight out
LEAN = 7.0            # degrees of torso lean into the five

def main(pack_path, SCALE=1.0, HEIGHT=2.30, SHOULDER=0.0, ARMW=0.0):
    p = Rpack(pack_path)
    if "z_hi5" in p.clip_names():
        print("z_hi5 already present"); return 0
    nodes = p.nodes(); names = p.node_names()
    RM = rig_map(p)
    assert not RM["_missing"], "rig incomplete: %s" % RM["_missing"]
    # PREFER AN AUTHORED STAND. The miner's "idle" election is a name, not a promise:
    # kaeli's idle opens crouched, and a five performed from a kneel hits the right point
    # in the world while looking nothing like a five. z_upright is a stand by
    # construction (see z_upright.py), so take it whenever a body has one.
    names_c = p.clip_names()
    base_clip = "z_upright" if "z_upright" in names_c else RM["_base_clip"]
    base_q, base_t, base = {}, {}, {}
    if base_clip:
        idle = p.clip_names().index(base_clip)
        for (node, typ, data) in p.tracks(idle):
            comp = 4 if typ == 0 else 3
            v = list(struct.unpack_from("<%df" % comp, data, 0))
            base[(node, typ)] = v
            if typ == 0: base_q[node] = v
            elif typ == 1: base_t[node] = v

    pelvis = RM["pelvis"]
    spine = [b for b in (RM["spine_01"], RM["spine_02"], RM["spine_03"]) if b >= 0]
    neck, headb = RM["neck_01"], RM["head"]
    armR = (RM["upperarm_r"], RM["lowerarm_r"], RM["hand_r"])
    hands = find_hands(p, nodes, names, RM["hand_l"], RM["hand_r"])
    paw = HandRig(nodes, base_q, base_t, hands["R"], armR[1]) if "R" in hands and hands["R"]["fingers"] else None
    # THE HAND DIRECTION WITHOUT FINGER BONES. Neither body has a bone past the wrist
    # (kaeli's RightHand is a leaf; reva2's "HandEnd" was only ever a tail landmark), so
    # there is no child to aim. But the hand MESH extends along a direction that is
    # FIXED in the hand bone's local space, and in a rest pose with a neutral wrist that
    # direction is simply the forearm axis continued. Measure it at rest, carry it as a
    # local vector, and the wrist can be aimed at any pose.
    _wrr, _wqr = fk_world(nodes)
    _fa_rest = _n([_wrr[armR[2]][k] - _wrr[armR[1]][k] for k in range(3)])
    hand_dir_local = qrot(qinv_(_wqr[armR[2]]), _fa_rest)

    wp0, wq0 = fk_world(nodes, base_q, base_t)
    # her own proportions -- nothing here is a constant from another body
    ARM = math.dist(wp0[armR[0]], wp0[armR[1]]) + math.dist(wp0[armR[1]], wp0[armR[2]])
    SHY = wp0[armR[0]][1]
    footY = min(wp0[RM["foot_l"]][1], wp0[RM["foot_r"]][1])
    # FORWARD IS +Z IN PACK SPACE. Not derived, because deriving it is what keeps going
    # wrong: a foot->toe vector depends on a toe bone being where its name claims, and on
    # kaeli it points sideways -- which put her shoulder 0.48u "behind" her pelvis and
    # collapsed her half of the gap to 0.042u. It is also the same circular trap that
    # shipped reva facing backward.
    #
    # +Z is not an assumption here, it is the engine's contract, checked on pixels for
    # both bodies: the wall's panel 0 is a straight-on front camera at yaw 0, and it
    # shows reva's face and kaeli's face. Whatever a bone is named, THAT is where these
    # packs face.
    fwd = [0.0, 0.0, 1.0]
    lat = [1.0, 0.0, 0.0]        # up x fwd; a positive rotation about it leans forward

    # THE MEETING POINT. Two bodies of different sizes CANNOT each reach "so far above my
    # own shoulder" and expect to touch: reva's shoulder stands 2.59u off the ground with
    # a 0.97u arm, kaeli's 1.84u with 0.77u, and aiming by their own proportions put
    # their hands 1.14u apart -- 0.74 of that purely in height. A shared contact must be
    # specified in the WORLD they share, so the height comes in as a world number and
    # each body works out its own way to it.
    #
    # Given the height, the forward distance is not a choice: it is whatever the arm has
    # left after the climb, sqrt(reach^2 - rise^2). A tall body reaching slightly down
    # therefore stands further back than a short one reaching up, and the two horizontal
    # parts sum to the gap the engine must place them at. That is printed below.
    # Shoulder height and arm length come from the ENGINE, not from this script. Both
    # sides must measure "above the ground" from the same datum, and they did not: the
    # engine grounds a body on the lowest bone in its whole cloud, while this score was
    # using the lowest FOOT joint. On kaeli those differ by 57 rig-units of stray
    # skeleton, which read as a 0.9u shoulder here and 1.84u there -- and a contact
    # authored against the wrong datum cannot land however careful the rest of the maths.
    # THE GROUND DATUM, computed exactly as the engine computes it: the lowest BONE in
    # the rest-pose cloud, not the lowest foot joint. Two reasons it must be the rest
    # pose and this script's own base-pose FK on top of it:
    #   - kaeli's cloud runs far below her feet, so a foot-based datum disagreed with the
    #     engine by 57 rig-units;
    #   - the engine grounds the partner ONCE from her rest pose and never re-clamps, so
    #     a base pose that crouches (kaeli's idle does) genuinely lowers her in the
    #     world. Measuring the shoulder in the BASE pose against the REST datum is what
    #     makes the score's height agree with where she actually stands.
    wp_rest, _ = fk_world(nodes)
    bones = [i for i in range(len(nodes)) if nodes[i].get("kind") == 1] or list(range(len(nodes)))
    mny = min(wp_rest[i][1] for i in bones)
    shoulder = list(wp0[armR[0]])
    sh_world = (shoulder[1] - mny) * SCALE
    arm_world = ARMW if ARMW > 0 else ARM * SCALE
    reach_rig = (arm_world * REACH_F) / SCALE

    # THE CONTACT SITS ON THE MIDLINE. Reaching straight ahead from her own shoulder put
    # each hand out at her own right, and facing each other that is opposite sides -- they
    # missed by 0.73u sideways, the sum of two shoulder offsets. A five happens BETWEEN
    # two people, so the target is the body's centre line and the arm crosses to it.
    cy = mny + HEIGHT / SCALE
    dx = 0.0 - shoulder[0]
    dy = cy - shoulder[1]
    fsq = reach_rig * reach_rig - dx * dx - dy * dy
    assert fsq > 1e-6, (
        "cannot reach %.2fu: shoulder %.2fu, arm %.2fu -- the height is past this body's arm"
        % (HEIGHT, sh_world, arm_world))
    F = math.sqrt(fsq)                       # forward distance left after the climb
    contact = [0.0, cy, shoulder[2] + F]
    rest_hand = list(wp0[armR[2]])

    # WHICH WAY IS THE PALM? cross(fingers, across-palm) is the palm normal up to a sign
    # that depends on how this rig names and orders its knuckles, so it is measured, not
    # assumed: with the right hand held up palm-forward, the thumb points to the body's
    # LEFT. Try one sign, look at where the thumb lands, keep the one that agrees.
    nsign = 1.0
    if paw:
        f0, s0 = hand_frame(wp0, hands["R"], armR[2])
        best = None
        for cand in (1.0, -1.0):
            n0 = _n([cand * x for x in _cross(f0, s0)])
            A = palm_to(f0, n0, [0.0, 1.0, 0.0], fwd)
            th = hands["R"]["fingers"].get("thumb")
            if not th: break
            tb = wp0[th[0]]; tt = wp0[hands["R"]["tips"].get("thumb", th[-1])]
            d = qrot(A, _n([tt[k] - tb[k] for k in range(3)]))
            score = _dot(d, lat)              # lat = the body's LEFT
            if best is None or score > best[0]: best = (score, cand)
        if best:
            nsign = best[1]
            print("z_hi5: palm normal sign %+g (thumb-to-left %.2f)" % (nsign, best[0]))

    frames = int(T_END * FPS)
    qtr = {}
    prev_pole = None
    for f in range(frames):
        t = f / FPS
        # wind up (anticipate: the hand dips back first), swing, contact, recoil, settle
        u = profile([(0.0, 0.0, ease_anticipate), (T_WIND, 0.45, ease_smooth),
                     (T_CONTACT, 1.0, ease_smooth), (T_CONTACT + 0.18, 0.72, ease_smooth),
                     (T_END - 0.15, 0.0, None)], t)
        qo = dict(base_q); to = dict(base_t)
        # the body leans in with the arm and the head tracks the other person
        qo[pelvis] = rotate_bone_world(nodes, pelvis, lat, math.radians(LEAN * 0.25) * u, qo)
        for b in spine:
            qo[b] = rotate_bone_world(nodes, b, lat, math.radians(LEAN / max(1, len(spine))) * u, qo)
        if neck >= 0:
            qo[neck] = rotate_bone_world(nodes, neck, lat, math.radians(-LEAN * 0.5) * u, qo)
        tgt = [rest_hand[k] + (contact[k] - rest_hand[k]) * u for k in range(3)]
        # ELBOW DOWN, not out. The old pole carried a full -lat term, which winged the
        # elbow out to the side and dropped the forearm toward horizontal -- half of why
        # jacob read the pose as a salute. A five keeps the forearm near vertical, so the
        # elbow is driven DOWN with only a little outward flare.
        pole = [-lat[0] * 0.28, -1.0, -lat[2] * 0.28 - 0.12]
        upd = solve_limb(nodes, armR, tgt, pole=pole, q_override=qo, t_override=to)
        qo.update(upd)
        w9, wq9 = fk_world(nodes, qo, to)

        # THE HAND, ORIENTED. Nothing was setting the wrist at all: the IK places the
        # hand's POSITION and whatever rotation falls out of the arm chain is what shows,
        # which is the other half of the salute. A five presents an OPEN PALM to the
        # other person -- fingers up, palm forward -- so the wrist is driven there
        # explicitly and turned in as the arm rises.
        if paw:
            f0, s0 = hand_frame(w9, hands["R"], armR[2])
            n0 = [nsign * x for x in _cross(f0, s0)]
            A = palm_to(f0, _n(n0), [0.0, 1.0, 0.0], fwd)
            wq_t = qmul(A, wq9[armR[2]])
            qo[armR[2]] = hold_world_rotation(
                nodes, armR[2], qslerp(wq9[armR[2]], wq_t, u), qo, to)
            w9, wq9 = fk_world(nodes, qo, to)
        else:
            # fingerless: aim the measured local hand direction UP. The palm roll cannot
            # be chosen without knuckles, but the raised, upright hand is what reads.
            f0 = _n(qrot(wq9[armR[2]], hand_dir_local))
            A = q_between(f0, [0.0, 1.0, 0.0])
            wq_t = qmul(A, wq9[armR[2]])
            qo[armR[2]] = hold_world_rotation(
                nodes, armR[2], qslerp(wq9[armR[2]], wq_t, u), qo, to)
            w9, wq9 = fk_world(nodes, qo, to)
        prev_pole = [w9[armR[1]][k] - w9[armR[0]][k] for k in range(3)]
        if paw:                      # open palm for the slap, relaxed on the way back
            paw.pose(qo, curl=0.05 + 0.25 * (1.0 - u), spread=0.55 * u + 0.15)
        chain = [pelvis, neck, headb] + list(spine) + list(armR)
        if paw:
            for ch in paw.spec["fingers"].values(): chain += ch
        for b in chain:
            if b is not None and b >= 0:
                qtr.setdefault(b, []).extend(qnorm(qo.get(b, base_q.get(b, nodes[b]["q"]))))
        if abs(t - T_CONTACT) < 0.5 / FPS:
            got = w9[armR[2]]
            err = math.dist(got, contact)
            print("z_hi5: contact hand error %.4f rig-u (%.1f%% of arm)" % (err, 100 * err / ARM))
            assert err < 0.06 * ARM, "the hand did not reach the meeting point"
            # THE POSE GATE. jacob on v1: "looks kinda like they are saluting each
            # other." A salute and a five put the hand in nearly the same PLACE -- what
            # separates them is the forearm angle and where the palm looks. Both are now
            # measured at the contact instant, so the difference is a number.
            fa = _n([w9[armR[2]][k] - w9[armR[1]][k] for k in range(3)])
            vert = _dot(fa, [0.0, 1.0, 0.0])
            print("z_hi5: forearm rise %.2f (want >= 0.55, 1.0 = straight up)" % vert)
            assert vert >= 0.55, "forearm too flat -- that reads as a salute, not a five"
            if paw:
                f1, s1 = hand_frame(w9, hands["R"], armR[2])
                pn = _n([nsign * x for x in _cross(f1, s1)])
                up_d, fwd_d = _dot(f1, [0.0, 1.0, 0.0]), _dot(pn, fwd)
                print("z_hi5: fingers up %.2f (want >= 0.70)   palm forward %.2f (want >= 0.70)"
                      % (up_d, fwd_d))
                assert up_d >= 0.70, "fingers are not pointing up"
                assert fwd_d >= 0.70, "the palm is not facing the other body"
            else:
                f1 = _n(qrot(wq9[armR[2]], hand_dir_local))
                up_d = _dot(f1, [0.0, 1.0, 0.0])
                print("z_hi5: hand up %.2f (want >= 0.70; fingerless rig)" % up_d)
                assert up_d >= 0.70, "the hand is not upright"

    # the world reach this score just authored, in the engine's own units: the engine
    # sizes every pack by bone-cloud span (3.15 / span*1.13), so the bench can add the
    # two bodies' prints together and get the gap that makes their hands meet.
    ys = [wp0[i][1] for i in range(len(nodes)) if nodes[i]["kind"] == 1] or \
         [wp0[i][1] for i in range(len(nodes))]
    span = max(ys) - min(ys)
    scale = 3.15 / (span * 1.13) if span > 1e-4 else 1.0
    # The half-gap is measured from the body's ORIGIN (which is where the engine stands
    # her), not from her shoulder: contact_world_z = origin + SCALE * contact_rig_z, and
    # the partner's is mirrored by her 180 turn, so the two half-gaps sum to the pgap
    # that makes the hands share a point.
    print("z_hi5: shoulder %.2fu  arm %.2fu  rise %+.2f  ->  HALF-GAP %.3f  "
          "(pgap = this body's + the other's)"
          % (sh_world, arm_world, HEIGHT - sh_world, contact[2] * SCALE))

    tracks = {}
    for (node, typ), v in base.items():
        tracks[(node, typ)] = v * frames
    for b, vals in qtr.items(): tracks[(b, 0)] = vals
    append_clips(pack_path, [("z_hi5", FPS, frames, tracks)])
    declare(pack_path, "z_hi5", {
        "fps": FPS, "frames": frames, "terminal": "base", "max_speed": 9.0,
        "contacts": [{"part": "fL", "t": [0.0, T_END], "floor": True, "still_tol": 0.06},
                     {"part": "fR", "t": [0.0, T_END], "floor": True, "still_tol": 0.06}],
    })
    print("z_hi5: appended (%d frames, %d tracks)" % (frames, len(tracks)))
    return 0

if __name__ == "__main__":
    # --scale is the ENGINE's scale for this pack, printed at load ("[hi5] geometry").
    # The score cannot compute it reliably itself: the hero path sizes a pack by its
    # MESH and the partner path by its bone span, so one pack legitimately has two
    # scales depending on which role it is playing. Taking it from the engine is the
    # only way the two sides are talking about the same body.
    a = sys.argv[1:]
    pk = a[0]
    sc = float(a[a.index("--scale") + 1]) if "--scale" in a else 1.0
    hh = float(a[a.index("--height") + 1]) if "--height" in a else 2.30
    sd = float(a[a.index("--shoulder") + 1]) if "--shoulder" in a else 0.0
    aw = float(a[a.index("--arm") + 1]) if "--arm" in a else 0.0
    sys.exit(main(pk, sc, hh, sd, aw))
