"""meshform.py -- A DRESS FORM MEASURED OFF A FINISHED BODY: the tailor's forms were written out by hand with the body
they dress (suit_body.py's T_BODY); this one is read off any character that already exists -- a rigged mesh somebody
modelled, on a skeleton that is not ours -- so the atelier can dress it.

11-02, the skate game's alien: he is taken without his clothing and his clothes are made again here,
which gives them physics and makes them easier to change. He came as one glb: a closed body, a
shirt, shorts and shoes on 60 bones, none of it ours.

    form = MeshForm(rig, [body_object], ROLES)
    form.torso, form.leg[+1/-1], form.arm["Left"/"Right"], form.armb, form.z     what drafting.py asks of a form
    form.parts      the skin as atelier Parts, the body, each arm and the head apart (sewing.Field: one part at a time)
    form.outward    which way is out of the body at a point

Every surface is a table of radii, taken by rays from inside the bone out to the first skin they meet: the torso's
round its axis at each height, a leg's round the line of its bones, an arm's round its bone. A body's parts run into
each other, so each table says where it stops: below the crotch the torso keeps the crotch's ring (cloth goes round both
legs there); between the armpit and the shoulder it stops at the shoulder's joint (the rays would run on out along
the arm); up the arm's root the arm tapers into the shoulder.

Built on Blender's axes, as the rest of the atelier: z up, -Y the body's front, +X its left. The body is measured as the rig is POSED
now: pose its arms out first (arms_out). A rig's rest pose may have the arms hanging nearly straight down, and between such an arm and the
body there is no room to close an underarm seam: the alien's tee came out crumpled and 13 cm open at the side. Sewn with the arms out, a garment goes
back to the rest pose through the inverse of its own skinning (unpose), and skinning carries it from there like any other.

ROLES says which bone is which: hips, spine, chest, neck, head, upper_arm_L, forearm_L, hand_L, thigh_L, shin_L, foot_L (and _R).
"""
import math

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

from atelier import Part, Surface, smoothstep

# Which part of the body a bone belongs to, by what its name says.
ARM_WORDS = ("arm", "hand", "thumb", "index", "middle", "ring", "pinky", "grip", "finger", "shoulder", "clavicle")
HEAD_WORDS = ("head", "eye", "antenna", "jaw", "tongue", "ear")

TORSO_N = 96
TORSO_DZ = 0.01
LEG_N = 64
LEG_DZ = 0.01
ARM_N = 48
ARM_DT = 0.01


def _smooth(table, passes=2, wrap_axis=1):
    """the facets of a low-poly body taken off the table: (1, 2, 1) round the ring, and along it"""
    t = np.asarray(table, float)
    for _ in range(passes):
        t = (np.roll(t, 1, wrap_axis) + 2 * t + np.roll(t, -1, wrap_axis)) / 4.0
        s = t.copy()
        s[1:-1] = (t[:-2] + 2 * t[1:-1] + t[2:]) / 4.0
        t = s
    return t


class _Table:
    """radii on (ring angle, station): bilinear, the angle periodic, the station clamped"""
    def __init__(self, rows, v0, dv):
        self.r, self.v0, self.dv = np.asarray(rows, float), v0, dv
        self.nv, self.nu = self.r.shape

    def at(self, u, v):
        f = (v - self.v0) / self.dv
        f = max(0.0, min(self.nv - 1.001, f))
        i = int(f)
        t = f - i
        g = (u / (2 * math.pi)) % 1.0 * self.nu
        j = int(g) % self.nu
        s = g - int(g)
        k = (j + 1) % self.nu
        return (self.r[i, j] * (1 - s) + self.r[i, k] * s) * (1 - t) + (self.r[i + 1, j] * (1 - s) + self.r[i + 1, k] * s) * t


def _polyline(points, key):
    """the point on a polyline at this value of `key(point)`, the ends held"""
    pts = sorted(points, key=key)

    def f(x):
        if x <= key(pts[0]): return pts[0].copy()
        if x >= key(pts[-1]): return pts[-1].copy()
        for a, b in zip(pts[:-1], pts[1:]):
            if key(a) <= x <= key(b):
                return a + (b - a) * ((x - key(a)) / max(key(b) - key(a), 1e-9))
        return pts[-1].copy()
    return f


class MeshForm:
    def __init__(self, rig, bodies, roles=None, cut_by=None):
        """rig: the armature object in its rest pose; bodies: mesh objects that together are the skin; roles: {role: bone}
        when the rig's own names differ from the roles'; cut_by: part name -> a test on bone names, to override the words"""
        self.roles = roles or {}
        self.rig = rig
        self.head = {}
        for role in ("hips", "spine", "chest", "neck", "head", "upper_arm_L", "upper_arm_R", "forearm_L", "forearm_R", "hand_L", "hand_R",
                     "thigh_L", "thigh_R", "shin_L", "shin_R", "foot_L", "foot_R"):
            self.head[role] = rig.matrix_world @ rig.pose.bones[self.roles.get(role, role)].head.copy()
        self.V, self.V_rest, self.W, self.F, self.M, self.S = [], [], [], [], [], []
        for ob in bodies:
            self._take(ob)
        self._wind_out()
        self.bvh = BVHTree.FromPolygons([Vector(v) for v in self.V], [list(f) for f in self.F])
        self._spine = _polyline([self.head[k] for k in ("hips", "spine", "chest", "neck", "head")], lambda p: p.z)
        self._measure()
        self._build_torso()
        self._build_legs()
        self._build_arms()
        self.parts = self._cut_parts(self.V)
        self.rest_parts = self._cut_parts(self.V_rest)
        # the whole skin as one closed shell: what cloth collides with. Cut into parts their cut ends are open, and the sign of a Field goes by the nearest face: beside an arm's open
        # root the outside of his chest read as inside his arm, and a tee was pushed up his chest
        self.whole = Part("skin_body")
        for i, v in enumerate(self.V):
            self.whole.v(v, self.W[i])
        for f, m, s_ in zip(self.F, self.M, self.S):
            self.whole.f(list(f), m or "M_Skin", None, s_)
        print("[form] crotch %.3f hip %.3f waist %.3f armpit %.3f shoulder %.3f neck %.3f wrist t %.3f" % (
            self.z["crotch"], self.z["hip"], self.z["waist"], self.z["armpit"], self.z["shoulder"], self.z["neck"], self.z["wrist"]))

    # ------------------------------------------------------------------ the skin
    def _take(self, ob):
        """welded: a glb splits its vertices at every flat edge, and a form wants the one surface"""
        me = ob.data
        mw = ob.matrix_world
        groups = {g.index: g.name for g in ob.vertex_groups}
        at = {}
        remap = []
        posed = ob.evaluated_get(bpy.context.evaluated_depsgraph_get()).to_mesh()
        for v, q in zip(me.vertices, posed.vertices):
            r = mw @ v.co
            key = tuple(round(c, 4) for c in r)
            if key not in at:
                at[key] = len(self.V)
                w = {groups[g.group]: g.weight for g in v.groups if g.weight > 1e-4 and g.group in groups}
                s = sum(w.values()) or 1.0
                p = mw @ q.co
                self.V.append((p.x, p.y, p.z))
                self.V_rest.append((r.x, r.y, r.z))
                self.W.append({k: x / s for k, x in w.items()})
            remap.append(at[key])
        ob.evaluated_get(bpy.context.evaluated_depsgraph_get()).to_mesh_clear()
        me.calc_loop_triangles()
        for t in me.loop_triangles:
            f = [remap[i] for i in t.vertices]
            if len(set(f)) == 3:
                self.F.append(f)
                self.M.append(me.materials[t.material_index].name if me.materials else "")
                self.S.append(False)

    def _wind_out(self):
        """every face wound to face out of its shell: a glb's faces may be drawn double-sided and wound any way"""
        bm = bmesh.new()
        verts = [bm.verts.new(v) for v in self.V]
        order = []
        for f in self.F:
            try:
                order.append(bm.faces.new([verts[i] for i in f]))
            except ValueError:                     # the same three corners twice
                order.append(None)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        bm.verts.index_update()
        F, M, S = [], [], []
        for f, m, s_ in zip(order, self.M, self.S):
            if f is not None:
                F.append([v.index for v in f.verts])
                M.append(m)
                S.append(s_)
        self.F, self.M, self.S = F, M, S
        bm.free()

    def _first_hit(self, origin, direction, reach):
        loc, n, i, d = self.bvh.ray_cast(origin, direction, reach)
        return d if loc is not None else None

    def _dominant(self, f):
        acc = {}
        for i in f:
            for k, x in self.W[i].items():
                acc[k] = acc.get(k, 0.0) + x
        return max(acc, key=acc.get) if acc else ""

    def _cut_parts(self, positions):
        """the body, each arm and the head apart: a garment's cloth takes its sign from the nearest part's own rule"""
        names = {"skin_body": Part("skin_body"), "skin_armLeft": Part("skin_armLeft"), "skin_armRight": Part("skin_armRight"), "head": Part("head")}
        maps = {k: {} for k in names}
        for fi, f in enumerate(self.F):
            bone = self._dominant(f).lower()
            cen = sum((Vector(self.V[i]) for i in f), Vector()) / 3.0
            if any(w in bone for w in HEAD_WORDS):
                part = "head"
            elif any(w in bone for w in ARM_WORDS):
                part = "skin_armLeft" if cen.x > 0 else "skin_armRight"
            else:
                part = "skin_body"
            P, m = names[part], maps[part]
            idx = []
            for i in f:
                if i not in m:
                    m[i] = P.v(positions[i], self.W[i])
                idx.append(m[i])
            P.f(idx, self.M[fi] or "M_Skin", None, self.S[fi])
        return [names["skin_body"], names["skin_armLeft"], names["skin_armRight"], names["head"]]

    # ------------------------------------------------------------------ the heights
    def _measure(self):
        V = np.array(self.V)
        mid = V[np.abs(V[:, 0]) < 0.012]
        crotch = float(mid[:, 2].min())
        neck_z, head_z = self.head["neck"].z, self.head["head"].z
        self.z = {"crotch": crotch}
        cy = lambda z: self._spine(z).y
        # the armpit: the highest height a ray out of the side leaves the body and meets the arm after a gap
        arm_top = self.head["upper_arm_L"].z
        armpit = None
        for k in range(0, 60):
            z = arm_top - 0.005 * k
            o = Vector((0.0, cy(z), z))
            d = self._first_hit(o, Vector((1, 0, 0)), 0.6)
            if d is None: continue
            p = o + Vector((d + 0.003, 0, 0))
            d2 = self._first_hit(p, Vector((1, 0, 0)), 0.2)
            # inside the arm's root, the next surface is the arm's far side, a long way on; past the armpit it's the gap to the arm's near side
            if d2 is not None:
                q = p + Vector((d2 + 0.003, 0, 0))
                d3 = self._first_hit(q, Vector((1, 0, 0)), 0.2)
                if d3 is not None and d2 > 0.004:
                    armpit = z
                    break
        self.z["armpit"] = armpit if armpit is not None else arm_top - 0.08
        # the shoulder: how high the body is above its joint
        J = self.head["upper_arm_L"]
        d = self._first_hit(Vector((J.x, J.y, J.z + 0.3)), Vector((0, 0, -1)), 0.6)
        self.z["shoulder"] = J.z + 0.3 - d if d is not None else J.z + 0.03
        # the neck's foot: going down from under the chin, where the ring stops being the neck's
        def half_at(z):
            o = Vector((0.0, cy(z), z))
            d_ = self._first_hit(o, Vector((1, 0, 0)), 0.6)
            return d_ if d_ is not None else 0.0
        hw = half_at((neck_z + head_z) * 0.5)
        z = (neck_z + head_z) * 0.5
        while z > neck_z - 0.1 and half_at(z) < hw * 1.12:
            z -= 0.002
        self.z["neck"] = z
        self.z["chin"] = head_z
        self.z["crown"] = float(V[:, 2].max())
        # the hip: the widest below the arms; the waist: the narrowest above it
        rows = [(half_at(z_), z_) for z_ in np.arange(crotch + 0.01, self.z["armpit"] - 0.02, 0.01)]
        low = [r for r in rows if r[1] < crotch + 0.20]
        self.z["hip"] = max(low)[1] if low else crotch + 0.04
        high = [r for r in rows if r[1] > self.z["hip"]]
        self.z["waist"] = min(high)[1] if high else self.z["hip"] + 0.06
        W = self.head["hand_L"]
        self.z["wrist"] = (W - J).length

    # ------------------------------------------------------------------ the torso
    def _build_torso(self):
        # from a little above the crotch: at the crease itself the ring is pinched between the thighs, and a tape goes round it
        z0, z1 = self.z["crotch"] + 0.03, self.head["head"].z
        zs = np.arange(z0, z1 + TORSO_DZ, TORSO_DZ)
        xcap = self.head["upper_arm_L"].x + 0.012
        rows = []
        for z in zs:
            o = Vector((0.0, self._spine(float(z)).y, float(z)))
            row = []
            for j in range(TORSO_N):
                th = 2 * math.pi * j / TORSO_N
                s, c = math.sin(th), math.cos(th)
                d = self._first_hit(o, Vector((s, -c, 0.0)), 0.6)
                r = d if d is not None else 0.0
                # between the armpit and the shoulder the ray runs on along the arm: the torso stops at the joint
                if z > self.z["armpit"] - 0.02 and abs(s) > 1e-3:
                    r = min(r, xcap / abs(s))
                row.append(r)
            row = np.array(row)
            bad = row <= 0.0
            if bad.any() and not bad.all():
                row[bad] = np.interp(np.flatnonzero(bad), np.flatnonzero(~bad), row[~bad], period=TORSO_N)
            rows.append(row)
        self.torso_table = _Table(_smooth(rows), float(zs[0]), TORSO_DZ)
        tb, spine = self.torso_table, self._spine
        z_lo = float(zs[0])

        def pt(theta, z):
            z_ = max(z, z_lo)                    # at and below the crotch: the ring above it, round both legs
            r = tb.at(theta, z_)
            return Vector((math.sin(theta) * r, spine(z_).y - math.cos(theta) * r, z))

        def out(p):
            return Vector((p.x, p.y - spine(max(p.z, z_lo)).y, 0.0))
        self.torso = Surface("torso", pt, out, self.weights)

    # ------------------------------------------------------------------ the legs
    def _build_legs(self):
        self.leg = {}
        self._leg_axis = {}
        for sg, sd in ((1, "L"), (-1, "R")):
            axis = _polyline([self.head["thigh_" + sd], self.head["shin_" + sd], self.head["foot_" + sd]], lambda p: p.z)
            self._leg_axis[sg] = axis
            z0, z1 = self.head["foot_" + sd].z, self.z["crotch"] + 0.05
            zs = np.arange(z0, z1 + LEG_DZ, LEG_DZ)
            rows = []
            for z in zs:
                o = axis(float(z))
                o.z = float(z)
                row = []
                for j in range(LEG_N):
                    ph = 2 * math.pi * j / LEG_N
                    d = self._first_hit(o, Vector((sg * math.sin(ph), -math.cos(ph), 0.0)), 0.35)
                    row.append(d if d is not None else 0.0)
                row = np.array(row)
                bad = row <= 0.0
                if bad.any() and not bad.all():
                    row[bad] = np.interp(np.flatnonzero(bad), np.flatnonzero(~bad), row[~bad], period=LEG_N)
                elif bad.all():
                    row[:] = 0.02
                rows.append(row)
            tb = _Table(_smooth(rows, 1), float(zs[0]), LEG_DZ)

            def pt(ph, z, tb=tb, axis=axis, sg=sg):
                r = tb.at(ph, z)
                o = axis(z)
                return Vector((o.x + sg * math.sin(ph) * r, o.y - math.cos(ph) * r, z))

            def out(p, axis=axis):
                o = axis(p.z)
                return Vector((p.x - o.x, p.y - o.y, 0.0))
            self.leg[sg] = Surface("leg%+d" % sg, pt, out, self.weights)

    # ------------------------------------------------------------------ the arms
    def _build_arms(self):
        self.arm, self.armb = {}, {}
        for sg, side, sd in ((1, "Left", "L"), (-1, "Right", "R")):
            J, E, W = self.head["upper_arm_" + sd], self.head["forearm_" + sd], self.head["hand_" + sd]
            d = (W - J).normalized()
            # the arm's frame as the drafts know it: q up and out across it, fr toward the front
            fr = Vector((0.0, -1.0, 0.0))
            fr = (fr - d * fr.dot(d)).normalized()
            q = fr.cross(d) * (1.0 if sg < 0 else -1.0)
            if q.z < 0:
                q = -q
            lenu, lenf = (E - J).length, (W - E).length
            # the bone's line, so a slight bend at the elbow stays inside the arm
            def axis(t, J=J, E=E, W=W, d=d, lenu=lenu, lenf=lenf):
                if t <= 0: return J + d * t
                if t <= lenu: return J + (E - J) * (t / lenu)
                if t <= lenu + lenf: return E + (W - E) * ((t - lenu) / lenf)
                return W + d * (t - lenu - lenf)
            ts = np.arange(-0.05, lenu + lenf + 0.03, ARM_DT)
            rows = []
            for t in ts:
                o = axis(float(t))
                row = []
                for j in range(ARM_N):
                    a = 2 * math.pi * j / ARM_N
                    ray = q * math.cos(a) + fr * math.sin(a)
                    dist = self._first_hit(o, ray, 0.12)
                    row.append(dist if dist is not None else 0.0)
                row = np.array(row)
                bad = row <= 0.0
                if bad.any() and not bad.all():
                    row[bad] = np.interp(np.flatnonzero(bad), np.flatnonzero(~bad), row[~bad], period=ARM_N)
                elif bad.all():
                    row[:] = 0.02
                rows.append(row)
            rows = np.array(rows)
            # the arm's root runs into the body: down there the rays run on into the armpit's web and the chest, and the arm the drafts keep clear of is the arm's own
            # size, tapering to nothing from the joint in, as a built body's does. Taken as measured, it read 5.4 cm under a 3 cm arm, and the armhole was drawn round it, 27 cm a side
            at0 = rows[int(round((0.14 - ts[0]) / ARM_DT))]
            for i, t in enumerate(ts):
                if t < 0.14:
                    rows[i] = np.minimum(rows[i], at0 * 1.1)
                if t < 0.04:
                    rows[i] = rows[i] * (0.25 + 0.75 * smoothstep(-0.05, -0.01, float(t)))
            tb = _Table(_smooth(rows, 1), float(ts[0]), ARM_DT)

            def pt(a, t, tb=tb, axis=axis, q=q, fr=fr):
                r = tb.at(a, t)
                return axis(t) + q * (r * math.cos(a)) + fr * (r * math.sin(a))

            def out(p, J=J, d=d):
                r = p - J
                return r - d * r.dot(d)
            self.arm[side] = Surface("arm" + side, pt, out, self.weights)
            self.armb[side] = {"joint": J.copy(), "elbow": E.copy(), "wrist": W.copy(), "d": d, "q": q, "fr": fr, "sg": sg}

    # ------------------------------------------------------------------ the pose it was measured in
    def unpose(self, P):
        """a Part made in this pose, its weights on, brought to the rig's rest: each vertex through the inverse of the blend of its bones' moves from rest"""
        rig = self.rig
        moved = {}
        for pb in rig.pose.bones:
            moved[pb.name] = pb.matrix @ rig.data.bones[pb.name].matrix_local.inverted()
        for i, v in enumerate(P.V):
            acc = Matrix(((0, 0, 0, 0), (0, 0, 0, 0), (0, 0, 0, 0), (0, 0, 0, 0)))
            tot = 0.0
            for b, x in P.W[i].items():
                m = moved.get(b)
                if m is None: continue
                acc += m * x
                tot += x
            if tot <= 0.0: continue
            acc = acc * (1.0 / tot)
            q = acc.inverted_safe() @ Vector(v)
            P.V[i] = (q.x, q.y, q.z)

    # ------------------------------------------------------------------ what the tailor reads off it
    def weights(self, p):
        """the skin's bone weights at a point: its nearest face's, by nearness"""
        loc, n, i, d = self.bvh.find_nearest(Vector(p))
        f = self.F[i]
        acc, tot = {}, 0.0
        for vi in f:
            wv = 1.0 / (0.002 + (Vector(self.V[vi]) - loc).length)
            tot += wv
            for k, x in self.W[vi].items():
                acc[k] = acc.get(k, 0.0) + x * wv
        return {k: x / tot for k, x in acc.items()}

    def outward(self, p, part_name=None):
        """which way is out of the body at p: the way the nearest skin faces. A body written by hand can say it by a rule; on a finished mesh the crease of a crotch or
        an armpit faces down or in, and a rule that turned those faces away from the axis flipped them, and the space beside them read as inside"""
        loc, n, i, d = self.bvh.find_nearest(Vector(p))
        return n
