#!/usr/bin/env python3
"""preset.py -- a finished body's measurements as a person the creator can start from.

    python skate_src/people/preset.py PACK [--skin "name@N,name"] [--name reva]

It reads a body the way reva_native/anim_bench/bodycheck.py does (skin at rest, a tape round it, no window) and says it twice:

  as a person in cast.py's words    stature, head, the landmark heights as shares of stature, hip, waist and chest as a half-width and a depth forward and
                                    back of the body's axis, limb radii and lengths. person.py builds a body from that
  as an EngineBodySpec              the dozen numbers engine/people.inc builds a person from while a game runs, fitted with people.inc's own tables

The creator starts with R.E.V.A. (jacob, 11-04: "a character creator starting with reva"): cast.REVA and games/people's first person are this tool's answer
for props/v1_reva_hi.rpack, pasted in.

⛔ The tables in ENGINE below are engine/people.inc's (peopleBytes), which are cast.py's AMELIA: change them together, or a fitted person isn't the person built.
⛔ An EngineBodySpec has one `build` for the whole trunk, a width for hips and shoulders and a size for the waist, so it can't say every body: a narrow chest
under ordinary shoulders has no number. The fit says what it couldn't reach.
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "author"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "reva_native", "anim_bench"))
import numpy as np

import bodycheck as BC
from packbody import PackBody

SIDE = {"hipj": (("thigh_r", "RightUpLeg", "mixamorig:RightUpLeg"), ("thigh_l", "LeftUpLeg", "mixamorig:LeftUpLeg")),
        "knee": (("calf_r", "RightLeg", "mixamorig:RightLeg"), ("calf_l", "LeftLeg", "mixamorig:LeftLeg")),
        "ankle": (("foot_r", "RightFoot", "mixamorig:RightFoot"), ("foot_l", "LeftFoot", "mixamorig:LeftFoot")),
        "shoulder": (("upperarm_r", "RightArm", "mixamorig:RightArm"), ("upperarm_l", "LeftArm", "mixamorig:LeftArm")),
        "elbow": (("lowerarm_r", "RightForeArm", "mixamorig:RightForeArm"), ("lowerarm_l", "LeftForeArm", "mixamorig:LeftForeArm")),
        "wrist": (("hand_r", "RightHand", "mixamorig:RightHand"), ("hand_l", "LeftHand", "mixamorig:LeftHand"))}
NECK = ("neck_01", "Neck", "mixamorig:Neck")


def ring(S, y, axis_z, fwd, parts=(0,), mid=None):
    """a level cut as cast.py says a girth: (half-width, depth forward of the body's axis, depth back of it). mid: only this near the middle line, for a depth"""
    c = S.cut(float(y), parts=parts)
    z = (c[:, 1] - axis_z) * fwd
    m = np.abs(c[:, 0]) < mid if mid else np.ones(len(c), bool)
    return float(c[:, 0].max() - c[:, 0].min()) / 2.0, float(z[m].max()), float(-z[m].min())


def limb_r(P, a, b, t, slab=0.008):
    """a limb's radius at a share t of the way from joint a to joint b: the tape round the skin there, over 2 pi"""
    d = (b - a) / np.linalg.norm(b - a)
    s = a + t * (b - a)
    rel = P - s
    along = rel @ d
    near = (np.abs(along) < slab) & (np.linalg.norm(rel - np.outer(along, d), axis=1) < 0.16)
    if near.sum() < 8:
        return 0.0
    u = np.cross(d, [0.0, 0.0, 1.0])
    u /= np.linalg.norm(u)
    v = np.cross(d, u)
    return BC.tape(np.stack([rel[near] @ u, rel[near] @ v], axis=1)) / (2.0 * math.pi)


def measure(path, spec=None):
    """a body as cast.py's words, and the few numbers the engine's fit wants beside them"""
    B = PackBody(path)
    meshes, how = BC.skin_meshes(B, spec)
    M = BC.measure(B, meshes)
    S = BC.Skin(B, meshes)
    P, part, fl, H, fwd = S.P, S.part, M["floor"], M["stature"], M["faces"]
    J = {k: [B.node(*names) for names in sides] for k, sides in SIDE.items()}     # [right, left]
    axis_z = float(np.mean([j[2] for j in J["hipj"]]))
    yc, yw = fl + M["crotch"], fl + M["waist height"]
    h = lambda p: (float(p[1]) - fl) / H                                           # noqa: E731  a height as a share of stature
    K = H / 1.65
    armpit = (float(J["shoulder"][0][1]) - fl - 1.15 * limb_r(P[part == 1], J["shoulder"][0], J["elbow"][0], 0.30)) / H
    lm = {"ankle": h(J["ankle"][0]), "knee": h(J["knee"][0]), "crotch": M["crotch"] / H, "hipj": h(J["hipj"][0]), "waist": M["waist height"] / H,
          "bust": M["bust height"] / H, "armpit": armpit, "shoulder": h(J["shoulder"][0]) + 0.028 * K / H, "neckbase": h(B.node(*NECK))}
    # hips where the seat stands furthest back; its ring's back is the middle line's, and what the seat adds to that is `seat`
    band = (P[:, 1] > yc - 0.01 * H) & (P[:, 1] < yw) & np.isin(part, (0, 2)) & (np.abs(P[:, 0]) > 0.011 * H)
    yb = float(P[band][np.argmin(P[band][:, 2] * fwd), 1])
    hw, hf, hb_all = ring(S, yb, axis_z, fwd, parts=(0, 2))
    _w, _f, hb_mid = ring(S, yb, axis_z, fwd, parts=(0, 2), mid=0.012 * H)
    ww, wf, wb = ring(S, yw, axis_z, fwd)
    yb_ = fl + M["bust height"]
    cw, _cf, cb = ring(S, yb_, axis_z, fwd)
    _w2, cf_mid, _b2 = ring(S, yb_, axis_z, fwd, mid=0.012 * H)                    # the chest's front is the breastbone's: the bust is its own number
    arm = P[part == 1]
    sh, el, wr = J["shoulder"][0], J["elbow"][0], J["wrist"][0]                    # her right arm: on an android the left may be a machine's
    d = (wr - el) / np.linalg.norm(wr - el)
    beyond = (arm - wr) @ d
    hand = float(beyond[(arm[:, 0] * np.sign(wr[0]) > 0)].max())
    legs = P[part == 2]
    foot = legs[(legs[:, 1] < float(J["ankle"][0][1])) & (legs[:, 0] * np.sign(J["ankle"][0][0]) > 0)]
    head_y = fl + M["chin height"] + 0.72 * M["head"]
    hc = S.cut(head_y, parts=(3,))
    side = lambda y: S.girth(float(y), parts=(0, 2), xmin=0.0) / (2.0 * math.pi)   # noqa: E731
    # ⛔ A spec has no word for how a spine curves: its rings stand on one upright axis. Hers don't (her waist is 8 cm forward of her hip joints), so each
    # ring's whole depth is kept and shared fore and aft the way the builder's own body shares it (cast.AMELIA's rings)
    share = lambda t, f, b: (t[0], (t[1] + t[2]) * f / (f + b), (t[1] + t[2]) * b / (f + b))   # noqa: E731
    out = {"H": H, "head_h": M["head"], "lm": lm,
           "hip": share((hw, hf, hb_mid), 0.090, 0.108), "waist": share((ww, wf, wb), 0.078, 0.080), "chest": share((cw, cf_mid, cb), 0.084, 0.090),
           "shoulder_hw": abs(float(sh[0])) + 0.045 * K, "neck_r": M["neck"] / (2.0 * math.pi), "bust": M["bust reach"], "seat": max(0.0, hb_all - hb_mid),
           "leg": {"thigh": M["thigh"] / (2.0 * math.pi), "knee": side(float(J["knee"][1][1])), "calf": M["calf"] / (2.0 * math.pi), "ankle": M["ankle"] / (2.0 * math.pi)},
           "arm": {"upper": float(np.linalg.norm(el - sh)), "fore": float(np.linalg.norm(wr - el)), "deltoid": limb_r(arm, sh, el, 0.14),
                   "upper_r": limb_r(arm, sh, el, 0.55), "elbow_r": limb_r(arm, sh, el, 1.0), "fore_r": limb_r(arm, el, wr, 0.30), "wrist_r": limb_r(arm, el, wr, 0.95)},
           "hand": hand, "foot": (float(foot[:, 2].max() - foot[:, 2].min()), float(foot[:, 0].max() - foot[:, 0].min())),
           "head": {"breadth": float(hc[:, 0].max() - hc[:, 0].min()), "depth": float(hc[:, 1].max() - hc[:, 1].min())}}
    extra = {"waist girth": M["waist"], "hip girth": S.girth(yb, parts=(0, 2)), "joints apart": abs(float(J["shoulder"][0][0] - J["shoulder"][1][0])),
             "heel": M.get("heel off the floor", 0.0), "how": "%d skin meshes, %s" % (len(meshes), how)}
    return out, extra


# ---------------------------------------------------------------- the engine's person: people.inc's tables, the same numbers
def half_ellipses(a, f, b):
    """the tape round a ring that's a half-ellipse forward (a across, f deep) and another back (a across, b deep)"""
    def half(p, q):
        return 0.5 * math.pi * (3.0 * (p + q) - math.sqrt((3.0 * p + q) * (p + 3.0 * q)))
    return half(a, f) + half(a, b)


def engine_body(e, H):
    """what peopleBytes makes of a spec, as measurements: the same lines as engine/people.inc"""
    K, bd = H / 1.65, e["build"]
    mli = e["limbs"] * math.sqrt(bd)
    shX = 0.188 * K * (0.6 + 0.4 * bd) * e["shoulders"]
    return {"hip half-width": 0.176 * K * bd * e["hips"], "hip depth": (0.090 + 0.108) * K * bd,
            "waist girth": half_ellipses(0.128 * K * bd * e["waist"], 0.078 * K * bd * e["waist"], 0.080 * K * bd * e["waist"]),
            "chest half-width": 0.140 * K * bd * (0.5 + 0.5 * e["shoulders"]), "joints apart": 2.0 * (shX - 0.045 * K),
            "thigh radius": 0.080 * K * mli, "bust reach": 0.040 * K * e["bust"]}


def engine_fit(m, x):
    """an EngineBodySpec for a measured body. Stature, heads, legs, hips, waist, limbs and bust come straight out of the tables. One `build` and one `shoulders`
    then have to stand for her seat's depth, her chest's breadth and how far apart her arm joints are: they're fitted to all three, the joints counting double"""
    H = m["H"]
    K = H / 1.65
    want = {"hip half-width": m["hip"][0], "hip depth": m["hip"][1] + m["hip"][2] + m["seat"], "waist girth": x["waist girth"], "chest half-width": m["chest"][0],
            "joints apart": x["joints apart"], "thigh radius": m["leg"]["thigh"], "bust reach": m["bust"]}
    best = None
    for bd in np.arange(0.60, 1.60, 0.005):
        for msh in np.arange(0.50, 1.50, 0.005):
            e = {"build": float(bd), "shoulders": float(msh), "hips": 1.0, "waist": 1.0, "limbs": 1.0, "bust": 0.0}
            got = engine_body(e, H)
            err = math.log(got["hip depth"] / want["hip depth"]) ** 2 + math.log(got["chest half-width"] / want["chest half-width"]) ** 2 \
                + 2.0 * math.log(got["joints apart"] / want["joints apart"]) ** 2
            if best is None or err < best[0]:
                best = (err, float(bd), float(msh))
    _e, bd, msh = best
    e = {"stature": H, "heads": H / m["head_h"], "build": bd, "shoulders": msh, "legs": m["lm"]["crotch"],
         "hips": want["hip half-width"] / (0.176 * K * bd), "limbs": want["thigh radius"] / (0.080 * K) / math.sqrt(bd),
         "bust": min(1.0, want["bust reach"] / (0.040 * K))}
    e["waist"] = want["waist girth"] / half_ellipses(0.128 * K * bd, 0.078 * K * bd, 0.080 * K * bd)
    return e, want, engine_body(e, H)


def cast_text(name, m):
    f = lambda v: "%.3f" % v   # noqa: E731
    t3 = lambda t: "(%s)" % ", ".join(f(v) for v in t)   # noqa: E731
    lm = ", ".join('"%s": %s' % (k, f(v)) for k, v in m["lm"].items())
    return ("%s = {\n"
            '    "name": "%s", "H": %s, "head_h": %s,\n'
            '    "lm": {%s},\n'
            '    "hip": %s, "waist": %s, "chest": %s,\n'
            '    "shoulder_hw": %s, "neck_r": %s, "bust": %s, "seat": %s, "belly": 0.0, "shoulder_blades": 0.004,\n'
            '    "leg": {%s},\n'
            '    "arm": {%s},\n'
            '    "hand": %s, "foot": %s,\n'
            '    "head": {"breadth": %s, "depth": %s, "brow_u": 0.60, "jaw": 0.88, "nose": 0.010, "cheek": 0.002},\n'
            "}" % (name.upper(), name, f(m["H"]), f(m["head_h"]), lm, t3(m["hip"]), t3(m["waist"]), t3(m["chest"]), f(m["shoulder_hw"]), f(m["neck_r"]), f(m["bust"]),
                   f(m["seat"]), ", ".join('"%s": %s' % (k, f(v)) for k, v in m["leg"].items()), ", ".join('"%s": %s' % (k, f(v)) for k, v in m["arm"].items()),
                   f(m["hand"]), t3(m["foot"]), f(m["head"]["breadth"]), f(m["head"]["depth"])))


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    opt = {argv[i]: argv[i + 1] for i in range(2, len(argv) - 1) if argv[i].startswith("--")}
    name = opt.get("--name", os.path.basename(argv[1]).replace("v1_", "").split(".")[0])
    m, x = measure(argv[1], opt.get("--skin"))
    print("# %s, measured by preset.py (%s)%s" % (os.path.basename(argv[1]), x["how"], ("; her heels are %.1f cm off the floor" % (100 * x["heel"])) if x["heel"] > 0.005 else ""))
    print(cast_text(name, m))
    e, want, got = engine_fit(m, x)
    print("\n# as an EngineBodySpec (engine.h), fitted with people.inc's tables")
    print("{ .stature = %.3ff, .heads = %.2ff, .build = %.3ff, .shoulders = %.3ff, .waist = %.3ff, .hips = %.3ff, .limbs = %.3ff, .legs = %.4ff, .bust = %.2ff }"
          % (e["stature"], e["heads"], e["build"], e["shoulders"], e["waist"], e["hips"], e["limbs"], e["legs"], e["bust"]))
    print("\n# what that spec builds, against her                 hers     the spec's")
    for k in want:
        print("#   %-28s %8.1f cm %8.1f cm%s" % (k, 100 * want[k], 100 * got[k], "" if abs(got[k] / want[k] - 1.0) < 0.03 else "   <- not reached"))


if __name__ == "__main__":
    main(sys.argv)
