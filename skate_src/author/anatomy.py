#!/usr/bin/env python3
"""anatomy.py -- a rigged body's anatomy set by measurements: longer or shorter legs, wider shoulders, a thicker waist, its bones moved to match, nothing rebaked.

    python skate_src/author/anatomy.py PACK --to survey [--skin "name@N,name"] [--out PACK2]      the U.S. Army's 2012 survey's average woman
    python skate_src/author/anatomy.py PACK --to table.json ...                                   any table of the same measurements, in centimetres

HOW. Every bone gets a size of its own: a trunk bone a breadth, a height and a depth; a limb bone a length and a girth; a hand or a foot one number. A joint
goes where its parent's size takes it, so the skeleton stays under the skin. A vertex goes where its own bones take it, in the shares it already rides them by,
so the skin stays one skin and its seams stay shut. The bones keep the way they're turned, so every clip still plays; only where each sits under its parent
changes, and the one track that carries the hips' height is scaled with the legs.

THE FIT. A table of measurements is the target (TARGETS below is what each row means and how it's taken on the mesh: bodycheck.py's tape, at the places an
anthropometric survey takes them). Each size answers to one measurement, they're all moved a little toward their targets, the body is measured again, and
again, until it's there. What can't be reached is said.

⛔ The head isn't touched: a face is who she is, and a survey's head would be someone else's. Her stature is reached with her own head on.
⛔ It measures skin, so do it to a bare body, before anything is cut from the skin or stood on it.
⛔ Feet made for a heel stay made for a heel: heights are taken from the floor she stands on with her heels up, so the survey's (barefoot, flat) have what her
ankle is lifted by added to them.
"""
import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "reva_native", "anim_bench"))
import numpy as np

import bodycheck as BC
from packbody import PackBody, smooth_normals, weld

ROLES = {"pelvis": ("pelvis", "Hips", "mixamorig:Hips"), "spine1": ("spine_01", "Spine", "mixamorig:Spine"), "spine2": ("spine_02", "Spine1", "mixamorig:Spine1"),
         "spine3": ("spine_03", "Spine2", "mixamorig:Spine2"), "neck": ("neck_01", "Neck", "mixamorig:Neck"), "head": ("head", "Head", "mixamorig:Head")}
SIDED = {"clav": ("clavicle_%s", "%sShoulder", "mixamorig:%sShoulder"), "upperarm": ("upperarm_%s", "%sArm", "mixamorig:%sArm"),
         "lowerarm": ("lowerarm_%s", "%sForeArm", "mixamorig:%sForeArm"), "hand": ("hand_%s", "%sHand", "mixamorig:%sHand"),
         "thigh": ("thigh_%s", "%sUpLeg", "mixamorig:%sUpLeg"), "calf": ("calf_%s", "%sLeg", "mixamorig:%sLeg"), "foot": ("foot_%s", "%sFoot", "mixamorig:%sFoot")}
TRUNK = ("pelvis", "spine1", "spine2", "spine3")
LIMB = ("clav", "upperarm", "lowerarm", "thigh", "calf", "neck")       # a length along the bone and a girth across it
WHOLE = ("hand", "foot")                                               # one number
CUP = 17.0                                                             # a C, in the standard's centimetres: what her bust stands over her ribs by

# The survey's average woman: NATICK/TR-15/007's means for its 1,986 women, in centimetres, by the names bodycheck.py --women uses and a few more.
SURVEY = {"stature": 162.85, "crotch height": 78.23, "knee height": 44.90, "navel height": 98.01, "bust height": 117.16, "shoulder top height": 133.51,
          "hip breadth": 35.38, "seat depth": 23.29, "waist breadth at the navel": 29.99, "waist depth at the navel": 21.30, "chest breadth": 26.93,
          "bust girth": 94.69, "shoulders, arms down": 45.03, "thigh girth": 61.61, "calf girth": 37.33, "neck girth": 32.98,
          "upper arm, shoulder top to elbow": 31.12, "forearm, elbow to wrist": 24.13, "hand length": 18.11, "foot length": 24.63,
          # the survey takes these two flexed; relaxed is about two centimetres and one less (said as an estimate wherever they're shown)
          "upper arm girth": 30.56 - 2.0, "forearm girth": 26.41 - 1.0,
          # not sizes of their own, but shown beside what comes out
          "hip girth": 102.12, "waist girth at the navel": 86.09, "ankle girth": 21.57}
TARGETS = """stature                            the crown's height, her own head on
crotch height, knee height         the lowest skin on her middle line; her knee's joint
navel height, bust height          the pit in her belly; her bust's point
shoulder top height                the top of her shoulder over the arm's joint (the survey's acromion)
hip breadth, seat depth            across and through her where her seat stands furthest back
waist breadth/depth at the navel   across and through her at her navel
chest breadth                      across her ribs at her bust's height
bust girth                         round her bust; her ribs are sized so that a C cup over them comes to it
shoulders, arms down               across her upper arms with her arms hanging
thigh, calf, neck girth            a tape round each
upper arm, forearm                 shoulder top to elbow joint; elbow joint to wrist joint; and a tape round the middle of each
hand length, foot length           wrist to fingertip; heel to toe"""


def limb_tape(P, a, b, t, slab=0.008, reach=0.16):
    """the tape round a limb at a share t of the way from joint a to joint b"""
    d = (b - a) / np.linalg.norm(b - a)
    rel = P - (a + t * (b - a))
    along = rel @ d
    near = (np.abs(along) < slab) & (np.linalg.norm(rel - np.outer(along, d), axis=1) < reach)
    if near.sum() < 8:
        return 0.0
    u = np.cross(d, [0.0, 0.0, 1.0])
    u /= np.linalg.norm(u)
    return BC.tape(np.stack([rel[near] @ u, rel[near] @ np.cross(d, u)], axis=1))


class Anatomy:
    def __init__(self, B, skin):
        self.B, self.skin = B, skin
        self.par = [nd["parent"] for nd in B.nodes]
        self.C0 = np.array([B.W[i][12:15] for i in range(len(B.nodes))], dtype=np.float64)
        self.W0 = [list(w) for w in B.W]
        self.role = {}
        for r, names in ROLES.items():
            got = [n for n in names if n in B.names]
            if got:
                self.role[B.names.index(got[0])] = r
        self.bone = {}
        for r, pats in SIDED.items():
            for side, (lo, hi) in (("l", ("l", "Left")), ("r", ("r", "Right"))):
                got = [p % (lo if "_%s" in p else hi) for p in pats if (p % (lo if "_%s" in p else hi)) in B.names]
                if got:
                    self.role[B.names.index(got[0])] = r
                    self.bone[(r, side)] = B.names.index(got[0])
        for r in ROLES:
            got = [i for i, v in self.role.items() if v == r]
            if got:
                self.bone[(r, "")] = got[0]
        # every node answers to the nearest bone above it that has a size: a twist bone to its limb, a finger to its hand, a face bone to the head
        self.of = []
        for i in range(len(self.par)):
            j = i
            while j >= 0 and j not in self.role:
                j = self.par[j]
            self.of.append(j)
        # a limb's own line: from its joint to the joint of the next bone down it
        nxt = {"clav": "upperarm", "upperarm": "lowerarm", "lowerarm": "hand", "thigh": "calf", "calf": "foot", "neck": "head"}
        self.axis = {}
        for (r, side), i in self.bone.items():
            if r in nxt and (nxt[r], side) in self.bone:
                d = self.C0[self.bone[(nxt[r], side)]] - self.C0[i]
                self.axis[i] = d / np.linalg.norm(d)
        self.meshes = []
        for m in B.meshes:
            pos, nrm, _uv, nodes, w, tris = B.arrays(m)
            self.meshes.append((m, pos, nrm, nodes, w / np.maximum(w.sum(1, keepdims=True), 1e-9), tris))
        self.floor0 = min(float(B.rest(m)[0][:, 1].min()) for m in skin)
        # which leg a vertex is of: more than half of it rides that leg's bones. Thighs that meet press flat against each other on her middle line; they
        # don't pass through (the survey's average thighs on its average hips do meet: two thighs are wider than the hips over them)
        leg_of = np.zeros(len(self.par))
        for side, sg in (("l", 1.0), ("r", -1.0)):
            if ("thigh", side) in self.bone:
                top = self.bone[("thigh", side)]
                for i in range(len(self.par)):
                    j = i
                    while j >= 0:
                        if j == top:
                            leg_of[i] = sg
                            break
                        j = self.par[j]
        self.leg = [(w * leg_of[nodes]).sum(1) for _m, _pos, _nrm, nodes, w, _t in self.meshes]
        self.leg_x = float(np.sign(self.C0[self.bone[("thigh", "l")]][0])) if ("thigh", "l") in self.bone else 1.0
        under = set()
        if ("pelvis", "") in self.bone:
            for i in range(len(self.par)):
                j = i
                while j >= 0:
                    if j == self.bone[("pelvis", "")]:
                        under.add(i)
                        break
                    j = self.par[j]
        self.under = np.array([i in under for i in range(len(self.par))])
        # ⛔ LANDMARKS ARE POINTS OF HER OWN SKIN, FOUND ONCE. Looked for again on every body the fit tries, a pit that's 2 mm deep is lost the first time the
        # belly round it is stretched, the size that answered to it stops moving, and the rest run away with her (the first fit made her 2.24 m tall)
        M = BC.measure(B, skin)
        S = BC.Skin(B, skin)
        P, part, fl, H, fwd = S.P, S.part, M["floor"], M["stature"], M["faces"]
        self.fwd, self.mark = fwd, {"crown": int(np.argmax(P[:, 1]))}
        c = (np.abs(P[:, 0]) < 0.008) & (part == 0) & (P[:, 1] > fl + 0.30 * H) & (P[:, 1] < fl + 0.62 * H)
        if c.any():
            self.mark["crotch"] = int(np.where(c)[0][np.argmin(P[c][:, 1])])
        self.right_leg = np.concatenate([self.leg[[i for i, (mm, *_r) in enumerate(self.meshes) if mm["mi"] == m["mi"]][0]] * self.leg_x < -0.5 for m in skin])
        # where a thigh's tape goes: the fold under her seat, which on her is 3% of her height under her crotch (the back of her stops curving in there). ⛔ Any
        # higher and the tape takes in her seat: 62 cm at 2 cm under her crotch, 58 at the fold
        if "crotch" in self.mark and self.right_leg.any():
            yf = float(P[self.mark["crotch"], 1]) - 0.03 * H
            c = self.right_leg & (np.abs(P[:, 1] - yf) < 0.004)
            if c.any():
                self.mark["furrow"] = int(np.where(c)[0][np.argmin(P[c][:, 2] * fwd)])
        rows = {key: v for _label, v, key in BC.against_women(B, skin, M)}
        front = (P[:, 2] - np.median(P[part == 0][:, 2])) * fwd
        if "navel height" in rows:
            c = (np.abs(P[:, 0]) < 0.004 * H) & (part == 0) & (front > 0)
            self.mark["navel"] = int(np.where(c)[0][np.argmin(np.abs(P[c][:, 1] - (fl + rows["navel height"])))])
        band = (part == 0) & (P[:, 1] > fl + 0.62 * H) & (P[:, 1] < fl + 0.80 * H)
        self.mark["bust"] = int(np.where(band)[0][np.argmax(P[band][:, 2] * fwd)])
        if ("upperarm", "r") in self.bone:
            sh = self.C0[self.bone[("upperarm", "r")]]
            c = (np.abs(P[:, 0] - sh[0]) < 0.012) & (P[:, 1] > sh[1]) & (P[:, 1] < sh[1] + 0.12) & (np.abs(P[:, 2] - sh[2]) < 0.06)
            if c.any():
                self.mark["shoulder top"] = int(np.where(c)[0][np.argmax(P[c][:, 1])])
        head = (part == 3) & (front > 0.03)
        if head.any():
            self.mark["chin"] = int(np.where(head)[0][np.argmin(P[head][:, 1])])
        self.sizes = {r: np.ones(3) for r in TRUNK}
        self.sizes.update({r: np.ones(2) for r in LIMB})
        self.sizes.update({r: np.ones(1) for r in WHOLE})

    def matrix(self, i):
        """the size of the bone node i answers to, as a 3x3 in the rig's own space at rest"""
        j = self.of[i]
        if j < 0:
            return np.eye(3)
        r = self.role[j]
        z = self.sizes.get(r)
        if z is None:
            return np.eye(3)
        if r in TRUNK:
            return np.diag(z)
        if r in WHOLE:
            return np.eye(3) * z[0]
        a = self.axis.get(j)
        if a is None:
            return np.eye(3) * z[1]
        return z[1] * np.eye(3) + (z[0] - z[1]) * np.outer(a, a)

    def place(self):
        """every joint and every vertex where these sizes put them: (joints (n,3), [positions per mesh]). Her feet stay on her floor"""
        n = len(self.par)
        S = np.stack([self.matrix(i) for i in range(n)])
        C1 = self.C0.copy()
        for i in range(n):
            p = self.par[i]
            if p >= 0:
                C1[i] = C1[p] + S[p] @ (self.C0[i] - self.C0[p])
        out = []
        for _m, pos, _nrm, nodes, w, _t in self.meshes:
            acc = np.zeros_like(pos)
            for k in range(4):
                nk = nodes[:, k]
                acc += w[:, k:k + 1] * (C1[nk] + np.einsum("nij,nj->ni", S[nk], pos - self.C0[nk]))
            out.append(acc)
        low = min(float(out[k][:, 1].min()) for k, (m, *_r) in enumerate(self.meshes) if any(m is s or m["mi"] == s["mi"] for s in self.skin))
        dy = self.floor0 - low
        C1[self.under, 1] += dy
        for k, (_m, _pos, _nrm, nodes, w, _t) in enumerate(self.meshes):
            out[k][:, 1] += dy * (w * self.under[nodes]).sum(1)
        for k in range(len(out)):                                           # each leg's skin stays on its own side of her middle
            sg = np.sign(self.leg[k]) * self.leg_x
            mine = np.abs(self.leg[k]) > 0.5
            x = out[k][:, 0]
            over = mine & (x * sg < 0.0004)
            x[over] = 0.0004 * sg[over]
        return C1, out

    def show(self):
        """put the body as these sizes make it into the PackBody's memory, so anything that measures it measures this"""
        C1, out = self.place()
        for i in range(len(self.par)):
            self.B.W[i][12:15] = [float(v) for v in C1[i]]
        for k, (m, *_r) in enumerate(self.meshes):
            self.B._rest[m["mi"]] = (out[k], self.B.rest(m)[1] if m["mi"] in self.B._rest else self._uv(m))
        return C1, out

    def _uv(self, m):
        rec = m["rec"]
        n = rec[2]
        return np.frombuffer(self.B.P.buf, dtype="<f4", count=n * 2, offset=rec[8]).reshape(n, 2).astype(np.float64) if rec[5] & 2 else np.zeros((n, 2))

    # ---------------------------------------------------------------- measuring
    def measures(self):
        """the body as it stands in memory, in centimetres, by TARGETS' names"""
        B = self.B
        M = BC.measure(B, self.skin)
        rows = {key: 100.0 * v for _label, v, key in BC.against_women(B, self.skin, M)}
        S = BC.Skin(B, self.skin)
        P, part = S.P, S.part
        fl = float(P[:, 1].min())
        at = lambda k: float(P[self.mark[k], 1])   # noqa: E731
        ext = lambda c: (100.0 * float(c[:, 0].max() - c[:, 0].min()), 100.0 * float(c[:, 1].max() - c[:, 1].min()))   # noqa: E731
        got = {"stature": 100.0 * (at("crown") - fl), "crotch height": 100.0 * (at("crotch") - fl) if "crotch" in self.mark else rows["crotch height"],
               "hip breadth": rows["hip breadth"], "hip girth": rows["hip girth"],
               "seat depth": rows["seat depth"], "bust girth": 100.0 * M["bust"], "thigh girth": rows["thigh girth"], "bust height": 100.0 * (at("bust") - fl),
               "underbust girth": 100.0 * M["underbust"], "waist, at its narrowest": 100.0 * M["waist"], "heel off the floor": 100.0 * M.get("heel off the floor", 0.0)}
        got["chest breadth"] = ext(S.cut(at("bust"), parts=(0,)))[0]
        if "neck" in M:
            got["neck girth"] = 100.0 * M["neck"]
        if "shoulders, arms down" in rows:
            got["shoulders, arms down"] = rows["shoulders, arms down"]
        if "navel" in self.mark:
            c = S.cut(at("navel"), parts=(0,))
            got["navel height"] = 100.0 * (at("navel") - fl)
            got["waist breadth at the navel"], got["waist depth at the navel"] = ext(c)
            got["waist girth at the navel"] = 100.0 * BC.tape(c)
        if "chin" in self.mark:
            got["chin height"] = 100.0 * (at("chin") - fl)
            got["head"] = 100.0 * (at("crown") - at("chin"))
        J = lambda r, s_: np.array(B.W[self.bone[(r, s_)]][12:15])   # noqa: E731
        if ("calf", "r") in self.bone and ("foot", "r") in self.bone:
            kn, an = J("calf", "r"), J("foot", "r")
            got["knee height"] = 100.0 * (float(kn[1]) - fl)
            got["ankle joint height"] = 100.0 * (float(an[1]) - fl)
            leg = P[self.right_leg] if np.sign(an[0]) * self.leg_x < 0 else P[(part == 2) & (P[:, 0] * np.sign(an[0]) > 0)]
            if "furrow" in self.mark:                                      # round ONE thigh, level, at the fold under her seat: where the survey's tape goes
                sl = leg[np.abs(leg[:, 1] - at("furrow")) < 0.004]
                if len(sl) > 8:
                    got["thigh girth"] = 100.0 * BC.tape(sl[:, [0, 2]])
            foot = leg[leg[:, 1] < float(an[1])]
            got["foot length"] = 100.0 * float(foot[:, 2].max() - foot[:, 2].min())
            got["calf girth"] = 100.0 * max(limb_tape(leg, kn, an, t) for t in np.arange(0.10, 0.56, 0.05))     # between her joints, not at a share of her height
            tapes = [limb_tape(leg, kn, an, t) for t in np.arange(0.78, 0.96, 0.03)]
            got["ankle girth"] = 100.0 * min(t for t in tapes if t > 0) if any(t > 0 for t in tapes) else 0.0
        if ("upperarm", "r") in self.bone:
            sh, el, wr = J("upperarm", "r"), J("lowerarm", "r"), J("hand", "r")
            arm = P[part == 1]
            cap = (at("shoulder top") - float(sh[1])) if "shoulder top" in self.mark else 0.04
            got["shoulder top height"] = 100.0 * (float(sh[1]) + cap - fl)
            got["shoulder cap"] = 100.0 * cap
            got["upper arm, shoulder top to elbow"] = 100.0 * (cap + float(np.linalg.norm(el - sh)))
            got["forearm, elbow to wrist"] = 100.0 * float(np.linalg.norm(wr - el))
            got["upper arm girth"] = 100.0 * limb_tape(arm, sh, el, 0.55)
            got["forearm girth"] = 100.0 * limb_tape(arm, el, wr, 0.30)
            d = (wr - el) / np.linalg.norm(wr - el)
            got["hand length"] = 100.0 * float(((arm - wr) @ d)[arm[:, 0] * np.sign(wr[0]) > 0].max())
            got["joints apart"] = 100.0 * abs(float(J("upperarm", "r")[0] - J("upperarm", "l")[0]))
        return got

    # ---------------------------------------------------------------- the fit
    def fit(self, want, rounds=26, say=print, start=None):
        """sizes that bring the body to `want` (centimetres, by TARGETS' names; what isn't named is left as it is). Returns (before, after) as measured"""
        z = self.sizes
        self.show()
        first = self.measures()
        for k, v in (start or {}).items():                              # sizes found before: a fit from them is there in a round or two
            if k in z and len(v) == len(z[k]):
                z[k][:] = v
        if start:
            self.show()
        lift = 0.75 * first.get("heel off the floor", 0.0)                # what a heel lifts her ankle by: the survey's women stood flat
        tall = lambda k: want[k] + lift                                    # noqa: E731
        got = first
        for rnd in range(rounds):
            g = 0.7

            def to(v, target, cur, lo=0.4, hi=2.5):
                """a size moved toward what it answers to, by no more than an eighth a round: they all answer to each other a little"""
                if cur <= 1e-6 or target <= 1e-6:
                    return v
                return float(np.clip(v * float(np.clip((target / cur) ** g, 0.875, 1.14)), lo, hi))
            w = lambda k: k in want and k in got   # noqa: E731
            if w("foot length"):
                z["foot"][0] = to(z["foot"][0], want["foot length"], got["foot length"])
            if w("hand length"):
                z["hand"][0] = to(z["hand"][0], want["hand length"], got["hand length"])
            if "knee height" in want and "knee height" in got:
                z["calf"][0] = to(z["calf"][0], tall("knee height") - got["ankle joint height"], got["knee height"] - got["ankle joint height"])
            # ⛔ A STRETCH OF HER ANSWERS TO THE TABLE'S OWN STRETCH, never to where the part under it happens to be this round: her hips' bone sits above her
            # crotch, so making her hips taller pushes her crotch DOWN, and a waist that chased "the navel over the crotch as it is" grew her hips to a metre
            if w("crotch height") and "knee height" in got and "knee height" in want:
                z["thigh"][0] = to(z["thigh"][0], want["crotch height"] - want["knee height"], got["crotch height"] - got["knee height"])
            elif w("crotch height") and "knee height" in got:
                z["thigh"][0] = to(z["thigh"][0], tall("crotch height") - got["knee height"], got["crotch height"] - got["knee height"])
            if w("navel height") and "crotch height" in want:
                f = to(1.0, want["navel height"] - want["crotch height"], got["navel height"] - got["crotch height"])
                z["pelvis"][1] *= f
                z["spine1"][1] *= f
            # ⛔ HER RIBS ARE ONE LENGTH, navel to shoulder. Sized as two, to put her bust's point at the table's height as well, the lower half grew by a
            # fifth and the upper shrank by a fifth: her breasts were pulled long underneath and squashed on top, and her shoulders came down to within a
            # hand of her bust. Where a bust's point sits is how a breast hangs, which isn't a bone's length: it's shown, and falls where it falls
            if w("shoulder top height") and "navel height" in want and "navel height" in got:
                f = to(1.0, want["shoulder top height"] - want["navel height"], got["shoulder top height"] - got["navel height"])
                z["spine2"][1] *= f
                z["spine3"][1] *= f
            if w("stature") and "shoulder top height" in want and got.get("chin height", 0.0) > 0.0:
                z["neck"][0] = to(z["neck"][0], want["stature"] - got["head"] - want["shoulder top height"], got["chin height"] - got["shoulder top height"], 0.3, 3.0)
            if w("hip breadth"):
                z["pelvis"][0] = to(z["pelvis"][0], want["hip breadth"], got["hip breadth"])
            if w("seat depth"):
                z["pelvis"][2] = to(z["pelvis"][2], want["seat depth"], got["seat depth"])
            if w("waist breadth at the navel"):
                z["spine1"][0] = to(z["spine1"][0], want["waist breadth at the navel"], got["waist breadth at the navel"])
            if w("waist depth at the navel"):
                z["spine1"][2] = to(z["spine1"][2], want["waist depth at the navel"], got["waist depth at the navel"])
            if w("chest breadth"):
                z["spine2"][0] = z["spine3"][0] = to(z["spine3"][0], want["chest breadth"], got["chest breadth"])
            if "bust girth" in want and "underbust girth" in got:           # her ribs, so that a C over them is the bust asked for
                z["spine2"][2] = z["spine3"][2] = to(z["spine3"][2], want["bust girth"] - CUP, got["underbust girth"])
            if w("shoulders, arms down") and "joints apart" in got:        # a clavicle's length is what the arms' joints stand apart by
                i = self.bone[("clav", "r")]
                L = float(np.linalg.norm(self.C0[self.bone[("upperarm", "r")]] - self.C0[i])) * 100.0 * z["clav"][0]
                z["clav"][0] = float(np.clip(z["clav"][0] * float(np.clip((L + 0.5 * g * (want["shoulders, arms down"] - got["shoulders, arms down"])) / max(L, 1e-6), 0.875, 1.14)), 0.4, 3.0))
            if w("upper arm, shoulder top to elbow"):
                z["upperarm"][0] = to(z["upperarm"][0], want["upper arm, shoulder top to elbow"] - got["shoulder cap"], got["upper arm, shoulder top to elbow"] - got["shoulder cap"])
            if w("forearm, elbow to wrist"):
                z["lowerarm"][0] = to(z["lowerarm"][0], want["forearm, elbow to wrist"], got["forearm, elbow to wrist"])
            if w("upper arm girth"):
                z["upperarm"][1] = to(z["upperarm"][1], want["upper arm girth"], got["upper arm girth"])
            if w("forearm girth"):
                z["lowerarm"][1] = to(z["lowerarm"][1], want["forearm girth"], got["forearm girth"])
            if w("thigh girth"):
                z["thigh"][1] = to(z["thigh"][1], want["thigh girth"], got["thigh girth"])
            if w("calf girth"):
                z["calf"][1] = to(z["calf"][1], want["calf girth"], got["calf girth"])
            if w("neck girth"):
                z["neck"][1] = to(z["neck"][1], want["neck girth"], got["neck girth"])
            self.show()
            got = self.measures()
            heights = ("stature", "crotch height", "knee height", "navel height", "bust height", "shoulder top height")
            off = max(abs(got[k] - (tall(k) if k in heights else want[k])) / want[k] for k in want if k in got and k not in ("bust girth", "hip girth", "waist girth at the navel", "ankle girth", "bust height"))
            if say:
                say("  round %2d: furthest from its target by %.1f%%" % (rnd + 1, 100.0 * off))
            if off < 0.005:
                break
        self.lift = lift
        return first, got

    # ---------------------------------------------------------------- into the pack
    def commit(self):
        """the body as these sizes make it, written into the PackBody: its joints, its skin and everything else that rides its bones, its normals, and the hips'
        height in every clip. B.write() saves it"""
        B = self.B
        for i in range(len(self.par)):                                      # back to the pack's own joints, so each mesh's own blend is read as it was
            B.W[i][12:15] = self.W0[i][12:15]
        B._rest.clear()
        C1, out = self.place()
        hip = self.bone.get(("pelvis", ""))
        k_leg = float((C1[hip][1] - self.floor0) / max(self.C0[hip][1] - self.floor0, 1e-9)) if hip is not None else 1.0
        # normals: each mesh's own, turned by what its surface did. Every mesh at once, so two meshes that meet at a seam agree across it
        allP0 = np.concatenate([pos for _m, pos, *_r in self.meshes])
        allP1 = np.concatenate(out)
        base, T = 0, []
        for _m, pos, _nrm, _nodes, _w, tris in self.meshes:
            T.append(tris + base)
            base += len(pos)
        T = np.concatenate(T)
        ids = weld(allP0)
        dN = smooth_normals(allP1, T, ids) - smooth_normals(allP0, T, ids)
        B.move_nodes({i: C1[i] for i in range(len(self.par))})
        base = 0
        for k, (m, pos, nrm, _nodes, _w, _tris) in enumerate(self.meshes):
            n1 = nrm + dN[base:base + len(pos)]
            n1 /= np.maximum(np.linalg.norm(n1, axis=1, keepdims=True), 1e-12)
            if float(np.abs(out[k] - pos).max()) > 1e-6:
                B.set_rest(m, out[k], normals=n1)
            base += len(pos)
        if hip is not None:
            B.scale_track(hip, k_leg)
        return k_leg


def report(first, got, want, lift, say=print):
    heights = ("stature", "crotch height", "knee height", "navel height", "bust height", "shoulder top height")
    say("%-36s %9s %9s %9s" % ("", "she was", "she is", "the table"))
    for k in ("stature", "crotch height", "knee height", "navel height", "bust height", "shoulder top height", "hip breadth", "seat depth", "hip girth",
              "waist breadth at the navel", "waist depth at the navel", "waist girth at the navel", "waist, at its narrowest", "chest breadth", "underbust girth",
              "bust girth", "shoulders, arms down", "joints apart", "neck girth", "upper arm, shoulder top to elbow", "upper arm girth", "forearm, elbow to wrist",
              "forearm girth", "hand length", "thigh girth", "calf girth", "ankle girth", "foot length"):
        if k in got:
            t = want.get(k)
            say("%-36s %6.1f cm %6.1f cm %s" % (k, first.get(k, float("nan")), got[k], ("%6.1f cm%s" % (t, (" (+%.1f for her heels)" % lift) if k in heights and lift > 0.05 else "")) if t else ""))


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    opt = {argv[i]: argv[i + 1] for i in range(2, len(argv) - 1) if argv[i].startswith("--")}
    src, dst = argv[1], opt.get("--out", argv[1])
    want = dict(SURVEY) if opt.get("--to", "survey") == "survey" else json.load(open(opt["--to"]))
    if dst != src:
        import shutil
        shutil.copyfile(src, dst)
    B = PackBody(dst)
    skin, how = BC.skin_meshes(B, opt.get("--skin"))
    print("%s: %d skin meshes (%s)" % (os.path.basename(dst), len(skin), how))
    A = Anatomy(B, skin)
    first, got = A.fit(want)
    report(first, got, want, A.lift)
    A.commit()
    B.write()
    print("wrote %s" % dst)


if __name__ == "__main__":
    main(sys.argv)
