"""character.py -- a character by code, from parts to a shipped GLB: the machinery build_reva.py grew (09-29) made
general, so the next body -- the suit man, the lizard -- is a body module and a wardrobe, not a copy of 550 lines.

  Kit(name, out, atlas, final)          the character being built: its atlas, its materials, its objects
  kit.material(name, rough, ...)        a Principled material on the atlas (SSS for skin in the renders)
  kit.add(part, tag, space, kind, piece, outfits)   a Part -> a Blender object named by the WARDROBE TAGS
  kit.dress(skin_parts, pieces, head_parts, outward)   hide sets -> skin groups -> objects, pieces -> objects
  kit.bake(size, ao, floors, regions, ...)   one painted, shadowed texture (the "plasticky" cure, round 7)
  kit.rig(bones)                        the armature, every object parented and skinned
  kit.export()                          <name>.glb + <name>.blend
  kit.renders(outfits, poses, ...)      turnarounds and poses per outfit (Cycles, AgX Punchy)

THE WARDROBE TAGS (what the club engine's `wear=` and Godot's ClubWardrobe read off the mesh names):
  skin | skin~h=a,b | head | wear~<slot>~<piece>~<outfit>+<outfit>
"""
import math, os, time

import bpy
import bmesh
import numpy as np
from mathutils import Vector, Matrix, Quaternion

import atelier as at


def merge(parts, name):
    Q = at.Part(name, True)
    for P in parts:
        off = len(Q.V)
        Q.V += P.V; Q.W += P.W
        Q.F += [[off + i for i in f] for f in P.F]; Q.M += P.M; Q.UV += P.UV; Q.S += P.S
        Q.PRM.update({off + k: v for k, v in P.PRM.items()}); Q.XA.update({off + k: v for k, v in P.XA.items()})
        Q.weld = Q.weld or P.weld
    return Q


class Kit:
    def __init__(self, name, out, atlas, final=None, mat_default=None, box_fallback=None):
        self.name, self.out, self.A = name, out, atlas
        self.final = final or (lambda v, space="body": (v[0], v[1], v[2]))
        self.T0 = time.time()
        self.scene = bpy.context.scene
        self.MAT, self.MATS = {}, []
        self.mat_default = mat_default or {}
        self.box_fallback = box_fallback or {}
        self.OBJ = {}                         # object name -> (kind, piece/tag, outfits/hidden_by)
        self.IMG = bpy.data.images.new(name + "_paint", atlas.SZ, atlas.SZ, alpha=False)
        self.rig_ob = None
        os.makedirs(out, exist_ok=True)

    def log(self, msg):
        print("[%s] %s (%.0f s)" % (self.name, msg, time.time() - self.T0))

    # ------------------------------------------------------------------ materials
    def paint_done(self):
        """the atlas is painted: upload it (and save it, for a look)"""
        A = self.A
        self.IMG.pixels.foreach_set(np.concatenate([A.A.reshape(-1, 3), np.ones((A.SZ * A.SZ, 1), np.float32)], 1).ravel())
        self.IMG.filepath_raw = os.path.join(self.out, self.name + "_paint.png"); self.IMG.file_format = "PNG"
        self.IMG.save()

    def material(self, name, rough, metal=0.0, spec=0.3, emit=0.0, sss=False, default=None):
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        nt = m.node_tree
        b = nt.nodes.get("Principled BSDF")
        b.inputs["Roughness"].default_value = rough
        b.inputs["Metallic"].default_value = metal
        b.inputs["Specular IOR Level"].default_value = spec
        tn = nt.nodes.new("ShaderNodeTexImage"); tn.image = self.IMG; tn.interpolation = "Linear"
        nt.links.new(tn.outputs["Color"], b.inputs["Base Color"])
        if sss:
            b.inputs["Subsurface Weight"].default_value = 0.18
            b.inputs["Subsurface Radius"].default_value = (1.0, 0.42, 0.26)
            b.inputs["Subsurface Scale"].default_value = 0.010
        if emit > 0:
            nt.links.new(tn.outputs["Color"], b.inputs["Emission Color"])
            b.inputs["Emission Strength"].default_value = emit
        self.MAT[name] = m; self.MATS.append(name)
        if default: self.mat_default[name] = default
        return m

    def face_uvs(self, P, fi):
        A = self.A
        spec = P.UV[fi] or ("swatch", self.mat_default[P.M[fi]])
        n = len(P.F[fi])
        if spec[0] == "swatch":
            return [A.swatch_uv(spec[1])] * n
        if spec[0] == "uv":                                   # explicit per-corner atlas UVs (a sewn panel's pattern)
            return spec[1]
        name = spec[1]
        prm = [P.PRM.get(vi) for vi in P.F[fi]]
        fb = self.box_fallback.get(name, self.mat_default[P.M[fi]])
        if any(q is None for q in prm): return [A.swatch_uv(fb)] * n
        uvs = [A.box_uv(name, *q) for q in prm]
        x0, y0, x1, y1 = A.boxes[name][:4]
        if max(u for u, v in uvs) - min(u for u, v in uvs) > 0.5 * (x1 - x0) / A.SZ:
            return [A.swatch_uv(fb)] * n
        return uvs

    # ------------------------------------------------------------------ objects
    def to_object(self, P, name, space="body"):
        me = bpy.data.meshes.new(name)
        me.from_pydata([self.final(v, space) for v in P.V], [], P.F)
        for m in self.MATS: me.materials.append(self.MAT[m])
        me.polygons.foreach_set("material_index", [self.MATS.index(m) for m in P.M])
        me.polygons.foreach_set("use_smooth", P.S)
        uv = []
        for fi in range(len(P.F)):
            for u, v in self.face_uvs(P, fi): uv.extend((u, v))
        me.uv_layers.new(name="UVMap").data.foreach_set("uv", uv)
        me.update()
        bm = bmesh.new(); bm.from_mesh(me)
        if P.weld: bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        bm.to_mesh(me); bm.free()
        ob = bpy.data.objects.new(name, me)
        self.scene.collection.objects.link(ob)
        W = P.W
        if P.weld:
            byp = {tuple(round(c, 6) for c in self.final(v, space)): P.W[vi] for vi, v in enumerate(P.V)}
            W = [byp.get(tuple(round(c, 6) for c in v.co), {"Head": 1.0}) for v in me.vertices]
        groups = {}
        for vi, w in enumerate(W):
            for b, x in w.items(): groups.setdefault(b, {}).setdefault(round(x * 255) / 255.0, []).append(vi)
        for b, byw in groups.items():
            vg = ob.vertex_groups.new(name=b)
            for x, idx in byw.items(): vg.add(idx, x, "REPLACE")
        return ob

    def dress(self, skin_parts, head_parts, pieces, outward, outfits, head_space=()):
        """the skin split by what covers it, and every piece as its own object"""
        hs = at.hide_sets(skin_parts + head_parts, [pc for pc in pieces if pc.hides], outward)
        skin_groups = {}
        for P in skin_parts + head_parts:
            groups = {}
            for fi, s in enumerate(hs[P.name]): groups.setdefault(s, []).append(fi)
            for s, fids in groups.items():
                tag = ("head" if P in head_parts else "skin") + (("~h=" + ",".join(sorted(s))) if s else "")
                skin_groups.setdefault(tag, []).append((P, fids))
        for tag, lst in sorted(skin_groups.items()):
            n = sum(len(f) for _, f in lst)
            ob = self.to_object(merge([at.split_part(P, f, tag) for P, f in lst], tag), tag,
                                "head" if tag.startswith("head") else "body")
            hidden_by = set(tag.split("~h=")[1].split(",")) if "~h=" in tag else set()
            self.OBJ[ob.name] = ("skin", tag, hidden_by)
            print("[%s] %-40s %6d faces" % (self.name, tag, n))
        # a piece may COVER others (pc.covers = [names]): the faces of those that it covers are split off as their own
        # meshes, tagged ~h=<it> -- drawn only while it is not worn (a shirt's back under a jacket)
        covered_by = {}
        for pc in pieces:
            for nm in getattr(pc, "covers", ()):
                covered_by.setdefault(nm, []).append(pc)
        for pc in pieces:
            base = "wear~%s~%s~%s" % (pc.slot, pc.name, "+".join(o for o in outfits if o in pc.outfits))
            groups = {"": [(P, list(range(len(P.F)))) for P in pc.parts]}
            if pc.name in covered_by:
                out_g = getattr(self, "garment_outward", None) or outward
                # (a covering piece need not hide SKIN -- a jacket stands off the body -- but it covers the shirt)
                cov = [at.Piece(c.name, c.slot, c.outfits, c.parts, hides=True, maxd=max(c.maxd, 0.03))
                       for c in covered_by[pc.name]]
                hs = at.hide_sets(pc.parts, cov, out_g)
                groups = {}
                for P in pc.parts:
                    byset = {}
                    for fi, cov in enumerate(hs[P.name]): byset.setdefault(cov, []).append(fi)
                    for cov, fids in byset.items():
                        key = ("~h=" + ",".join(sorted(cov))) if cov else ""
                        groups.setdefault(key, []).append((P, fids))
            for key, lst in groups.items():
                tag = base + key
                parts = [P if len(f) == len(P.F) else at.split_part(P, f, tag) for P, f in lst]
                ob = self.to_object(merge(parts, tag), tag, "head" if pc.name in head_space else "body")
                self.OBJ[ob.name] = ("wear", pc.name, pc.outfits)
                if key: print("[%s] %-44s %6d faces" % (self.name, tag, sum(len(f) for _, f in lst)))
        self.log("%d objects, %d verts" % (len(self.OBJ), sum(len(bpy.data.objects[n].data.vertices) for n in self.OBJ)))

    def outfit_objects(self, outfit):
        """what is drawn in an outfit -- as the club engine and Godot's ClubWardrobe draw it: its pieces, but a
        piece's part tagged ~h=x not while x is worn; the skin nothing worn covers"""
        worn = {n for n, (k, pc, o) in self.OBJ.items() if k == "wear" and outfit in o}
        wnames = {self.OBJ[n][1] for n in worn}

        def covered(n):
            if "~h=" not in n: return False
            return bool(set(n.split("~h=")[1].split(".")[0].split(",")) & wnames)
        worn = {n for n in worn if not covered(n)}
        skin_ = {n for n, (k, t, hb) in self.OBJ.items() if k == "skin" and not (hb & wnames)}
        return worn | skin_

    # ------------------------------------------------------------------ THE BAKE
    def bake(self, size, ao_samples, floors, regions=(), ao_groups=(), outfits=(), warm=None, face_box=None,
             face_floor=0.80, ao_distance=0.07):
        """one painted, shadowed texture. regions: [(box names, scale)] -- islands whose UVs sit in those atlas boxes
        get more of the texture (a face x5, prints x2.6); ao_groups: [(names to bake, names visible)] baked AFTER the
        skin alone -- e.g. each hairstyle over the head; each outfit's garments are baked over the skin and each
        other. floors: material -> the darkest the occlusion may take it. warm(Pz) -> 0..1 where skin is warmer."""
        t0 = time.time()
        scene, A, OBJ, MAT, MATS = self.scene, self.A, self.OBJ, self.MAT, self.MATS

        def _img(nm):
            im = bpy.data.images.new(nm, size, size, alpha=True, float_buffer=True)
            im.colorspace_settings.name = "Non-Color"
            return im

        def _pix(im):
            a = np.empty(size * size * 4, np.float32); im.pixels.foreach_get(a)
            return a.reshape(size, size, 4)
        obs = [bpy.data.objects[n] for n in OBJ]
        boxes = [([[c / A.SZ for c in A.boxes[b][:4]] for b in names if b in A.boxes], k) for names, k in regions]
        for ob in obs:
            me = ob.data
            src = np.zeros(len(me.loops) * 2, np.float32); me.uv_layers["UVMap"].data.foreach_get("uv", src)
            src = src.reshape(-1, 2)
            reg = []
            for p in me.polygons:
                u, v = src[p.loop_start]
                r = 0
                for ri, (bxs, k) in enumerate(boxes):
                    if any(b[0] <= u <= b[2] and b[1] <= v <= b[3] for b in bxs): r = ri + 1; break
                reg.append(r)
            ob["reg"] = reg
            me.uv_layers.new(name="Baked"); me.uv_layers.active = me.uv_layers["Baked"]
        for o in scene.objects: o.select_set(False)
        for ob in obs: ob.select_set(True)
        bpy.context.view_layer.objects.active = obs[0]
        bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.smart_project(angle_limit=math.radians(62.0), island_margin=0.002, area_weight=0.0,
                                 correct_aspect=True, scale_to_bounds=False)
        bpy.ops.object.mode_set(mode="OBJECT")
        for ob in obs:
            me = ob.data
            dst = me.uv_layers["Baked"]                        # ⛔ looked up again: edit mode rebuilt the mesh data
            uv = np.zeros(len(me.loops) * 2, np.float32); dst.data.foreach_get("uv", uv); uv = uv.reshape(-1, 2)
            reg = ob["reg"]
            for ri, (bxs, k) in enumerate(boxes):
                loops = [li for p in me.polygons if reg[p.index] == ri + 1 for li in p.loop_indices]
                if loops:
                    c = uv[loops].mean(0); uv[loops] = c + (uv[loops] - c) * k
            dst.data.foreach_set("uv", uv.ravel())
        bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.pack_islands(rotate=True, margin=0.0025)
        bpy.ops.object.mode_set(mode="OBJECT")
        self.log("unwrapped %d pieces for the bake" % len(obs))
        scene.render.engine = "CYCLES"; scene.cycles.device = "CPU"
        scene.render.bake.margin = 6
        if scene.world is None: scene.world = bpy.data.worlds.new("bake")
        scene.world.light_settings.distance = ao_distance
        for m in MAT.values():
            nt = m.node_tree
            src = [n for n in nt.nodes if n.type == "TEX_IMAGE" and n.image == self.IMG][0]
            un = nt.nodes.new("ShaderNodeUVMap"); un.uv_map = "UVMap"; un.name = "SRC_UV"
            nt.links.new(un.outputs["UV"], src.inputs["Vector"])

        def target(im):
            for m in MAT.values():
                nt = m.node_tree
                t = nt.nodes.get("BAKE_T") or nt.nodes.new("ShaderNodeTexImage"); t.name = "BAKE_T"; t.image = im
                u = nt.nodes.get("BAKE_UV") or nt.nodes.new("ShaderNodeUVMap"); u.name = "BAKE_UV"; u.uv_map = "Baked"
                nt.links.new(u.outputs["UV"], t.inputs["Vector"])
                for n in nt.nodes: n.select = False
                t.select = True; nt.nodes.active = t

        def emit(fn):
            for mname, m in MAT.items():
                nt = m.node_tree
                e = nt.nodes.get("BAKE_E") or nt.nodes.new("ShaderNodeEmission"); e.name = "BAKE_E"
                nt.links.new(fn(nt, mname), e.inputs["Color"])
                nt.links.new(e.outputs["Emission"], nt.nodes["Material Output"].inputs["Surface"])

        def restore():
            for m in MAT.values():
                nt = m.node_tree
                nt.links.new(nt.nodes["Principled BSDF"].outputs["BSDF"], nt.nodes["Material Output"].inputs["Surface"])

        def colour(nt, mname):
            return [n for n in nt.nodes if n.type == "TEX_IMAGE" and n.image == self.IMG][0].outputs["Color"]

        def position(nt, mname):
            g = nt.nodes.get("BAKE_G") or nt.nodes.new("ShaderNodeNewGeometry"); g.name = "BAKE_G"
            return g.outputs["Position"]

        def matid(nt, mname):
            r = nt.nodes.get("BAKE_ID") or nt.nodes.new("ShaderNodeRGB"); r.name = "BAKE_ID"
            r.outputs["Color"].default_value = ((MATS.index(mname) + 0.5) / 16.0, 0.0, 0.0, 1.0)
            return r.outputs["Color"]
        fb = [c / A.SZ for c in A.boxes[face_box][:4]] if face_box in A.boxes else None

        def facemask(nt, mname):
            u = nt.nodes.get("BAKE_FU") or nt.nodes.new("ShaderNodeUVMap"); u.name = "BAKE_FU"; u.uv_map = "UVMap"
            sp = nt.nodes.get("BAKE_FS") or nt.nodes.new("ShaderNodeSeparateXYZ"); sp.name = "BAKE_FS"
            nt.links.new(u.outputs["UV"], sp.inputs["Vector"])
            out = None
            for i, (ax, op, val) in enumerate((("X", "GREATER_THAN", fb[0]), ("X", "LESS_THAN", fb[2]),
                                               ("Y", "GREATER_THAN", fb[1]), ("Y", "LESS_THAN", fb[3]))):
                m_ = nt.nodes.get("BAKE_F%d" % i) or nt.nodes.new("ShaderNodeMath"); m_.name = "BAKE_F%d" % i
                m_.operation = op; nt.links.new(sp.outputs[ax], m_.inputs[0]); m_.inputs[1].default_value = val
                if out is None: out = m_.outputs["Value"]
                else:
                    mm = nt.nodes.get("BAKE_M%d" % i) or nt.nodes.new("ShaderNodeMath"); mm.name = "BAKE_M%d" % i
                    mm.operation = "MULTIPLY"; nt.links.new(out, mm.inputs[0]); nt.links.new(m_.outputs["Value"], mm.inputs[1])
                    out = mm.outputs["Value"]
            return out

        def select(names, visible):
            for o in scene.objects:
                if o.name in OBJ: o.hide_render = o.name not in visible
                o.select_set(o.name in names)
            bpy.context.view_layer.objects.active = bpy.data.objects[next(iter(names))]
        allnames = set(OBJ)
        col, pos, ids, ao = _img("col"), _img("pos"), _img("id"), _img("ao")
        fm = _img("face") if fb else None
        select(allnames, allnames)
        scene.cycles.samples = 1
        scene.render.bake.use_clear = True
        for im, fn in ((col, colour), (pos, position), (ids, matid)) + (((fm, facemask),) if fm else ()):
            target(im); emit(fn); bpy.ops.object.bake(type="EMIT")
        restore(); target(ao)
        scene.cycles.samples = ao_samples
        skins = {n for n, v in OBJ.items() if v[0] == "skin"}
        select(skins, skins); bpy.ops.object.bake(type="AO")
        scene.render.bake.use_clear = False
        done = set()
        for names, visible in ao_groups:
            names = {n for n in names if n in OBJ}
            if not names: continue
            select(names, set(visible) | names); bpy.ops.object.bake(type="AO"); done |= names
        for o in outfits:
            vis = self.outfit_objects(o)
            todo = {n for n in vis if OBJ[n][0] == "wear" and n not in done}
            if not todo: continue
            select(todo, vis | skins); bpy.ops.object.bake(type="AO")
            done |= todo
        rest = {n for n, v in OBJ.items() if v[0] == "wear" and n not in done}
        if rest:
            select(rest, rest | skins); bpy.ops.object.bake(type="AO")
        for o in scene.objects: o.hide_render = False
        self.log("baked colour, position, material and occlusion")
        C = _pix(col)[..., :3].astype(np.float64)
        AO = _pix(ao)[..., 0].astype(np.float64)
        Pz = _pix(pos)[..., :3].astype(np.float64)
        K = np.floor(_pix(ids)[..., 0] * 16.0).astype(np.int32)
        shade = np.ones(AO.shape)
        for mname, lo in floors.items():
            if mname not in MATS: continue
            m = K == MATS.index(mname); shade[m] = lo + (1.0 - lo) * AO[m]
        if fm is not None and "M_Skin" in MATS:
            face = (_pix(fm)[..., 0] > 0.5) & (K == MATS.index("M_Skin"))
            shade[face] = face_floor + (1.0 - face_floor) * AO[face]
        C = C * shade[..., None]
        if warm is not None and "M_Skin" in MATS:
            skinm = K == MATS.index("M_Skin")
            wk = np.clip(warm(Pz), 0.0, 1.0)[..., None] * 0.30
            C = np.where(skinm[..., None], C * (1.0 - wk) + C * np.array((1.02, 0.86, 0.84)) * wk, C)
        Cs = np.where(C <= 0.0031308, C * 12.92, 1.055 * np.power(np.clip(C, 0.0031308, None), 1.0 / 2.4) - 0.055)
        out = np.concatenate([np.clip(Cs, 0.0, 1.0), np.ones(AO.shape + (1,))], -1).astype(np.float32)
        fin = bpy.data.images.new(self.name + "_atlas", size, size, alpha=False)
        fin.pixels.foreach_set(out.ravel())
        fin.filepath_raw = os.path.join(self.out, self.name + "_atlas.png"); fin.file_format = "PNG"; fin.save()
        for m in MAT.values():
            nt = m.node_tree
            for n in [n for n in nt.nodes if n.name.startswith("BAKE_") or n.name == "SRC_UV"]: nt.nodes.remove(n)
            [n for n in nt.nodes if n.type == "TEX_IMAGE" and n.image == self.IMG][0].image = fin
        for ob in obs:
            me = ob.data
            me.uv_layers.remove(me.uv_layers["UVMap"])
            me.uv_layers["Baked"].name = "UVMap"
            me.uv_layers.active = me.uv_layers["UVMap"]; me.uv_layers["UVMap"].active_render = True
            if "reg" in ob: del ob["reg"]
        for im in [col, pos, ids, ao] + ([fm] if fm else []): bpy.data.images.remove(im)
        self.log("the baked atlas: %s" % fin.filepath_raw)

    # ------------------------------------------------------------------ the skeleton
    def rig(self, bones):
        arm_data = bpy.data.armatures.new(self.name + "_rig")
        rig = bpy.data.objects.new(self.name + "_rig", arm_data)
        self.scene.collection.objects.link(rig)
        for o in self.scene.objects: o.select_set(False)
        bpy.context.view_layer.objects.active = rig
        bpy.ops.object.mode_set(mode="EDIT")
        E = arm_data.edit_bones
        for entry in bones:
            name, h, t, par, space = entry[:5]
            b = E.new(name)
            b.head = Vector(self.final(h, space)); b.tail = Vector(self.final(t, space))
            if par: b.parent = E[par]; b.use_connect = False
            # an optional 6th entry: the direction the bone's local +Z should face. A rotation about local +X by a
            # positive angle swings the bone toward its +Z -- so for a finger, +Z toward the PALM makes every curl
            # bend the right way (left to Blender's automatic roll, the fingers curled BACKWARDS: jacob, 10-18)
            if len(entry) > 5 and entry[5] is not None: b.align_roll(Vector(entry[5]))
        bpy.ops.object.mode_set(mode="OBJECT")
        missing = set()
        for n in self.OBJ:
            ob = bpy.data.objects[n]
            ob.parent = rig
            md = ob.modifiers.new("Armature", "ARMATURE"); md.object = rig
            missing |= {vg.name for vg in ob.vertex_groups if vg.name not in arm_data.bones}
        self.log("%d bones (%d chain bones); vertex groups with no bone: %s"
                 % (len(arm_data.bones), sum(1 for b in arm_data.bones if b.name.startswith(("Hair_", "Cloth_"))),
                    sorted(missing) or "none"))
        assert not missing
        self.rig_ob = rig
        return rig

    def export(self):
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(self.out, self.name + ".blend"))
        bpy.ops.export_scene.gltf(filepath=os.path.join(self.out, self.name + ".glb"), export_format="GLB",
                                  use_selection=False, export_skins=True, export_animations=False, export_yup=True)
        self.log("wrote %s" % os.path.join(self.out, self.name + ".glb"))

    # ------------------------------------------------------------------ posing and renders
    def rest(self):
        for pb in self.rig_ob.pose.bones:
            pb.rotation_mode = "QUATERNION"; pb.rotation_quaternion = (1, 0, 0, 0)
        bpy.context.view_layer.update()

    def aim(self, bname, target):
        rig = self.rig_ob
        if bname not in rig.pose.bones: return
        bpy.context.view_layer.update()
        pb = rig.pose.bones[bname]
        cur = (rig.matrix_world @ pb.tail - rig.matrix_world @ pb.head).normalized()
        tgt = Vector(target).normalized()
        axis = cur.cross(tgt)
        if axis.length < 1e-6: return
        R = Matrix.Rotation(cur.angle(tgt), 3, axis.normalized())
        M = (rig.matrix_world @ pb.matrix).to_3x3().normalized()
        par = M @ pb.rotation_quaternion.to_matrix().inverted()
        pb.rotation_quaternion = (par.inverted() @ R @ M).to_quaternion()

    def pose(self, aims, curls=()):
        self.rest()
        for bname, d in aims: self.aim(bname, d)
        for bname, deg in curls:
            if bname in self.rig_ob.pose.bones:
                pb = self.rig_ob.pose.bones[bname]
                pb.rotation_mode = "QUATERNION"; pb.rotation_quaternion = Quaternion((1, 0, 0), math.radians(deg))
        bpy.context.view_layer.update()

    def renders(self, outfits, poses, zc, height, face_z=None, face_scale=0.40, studio_bg=(0.80, 0.80, 0.84),
                view="AgX", exposure=0.0):
        """per outfit: a four-view turnaround (arms hanging) and each pose; poses = [(name, aims, curls)]"""
        scene = self.scene
        scene.render.engine = "CYCLES"; scene.cycles.device = "CPU"; scene.cycles.use_denoising = True
        try:
            scene.view_settings.view_transform = view                # "Standard": a sheet's flat colour, ungraded
            if view == "Standard":
                scene.view_settings.exposure = exposure
            if view == "AgX":
                for look in ("AgX - Punchy", "Punchy"):
                    try:
                        scene.view_settings.look = look; break
                    except TypeError:
                        pass
        except TypeError:
            scene.view_settings.view_transform = "Standard"
        world = bpy.data.worlds.new("w"); scene.world = world; world.use_nodes = True
        bg = world.node_tree.nodes["Background"]
        bg.inputs["Color"].default_value = studio_bg + (1.0,); bg.inputs["Strength"].default_value = 0.25
        if sum(studio_bg) > 2.8:                                    # a white sheet's studio: bright, soft
            bg.inputs["Strength"].default_value = 0.9
        cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); scene.collection.objects.link(cam)
        cam.data.type = "ORTHO"; scene.camera = cam

        def light(nm, col, en, loc, look=(0, 0, 1.0), size=3.0):
            L_ = bpy.data.objects.new(nm, bpy.data.lights.new(nm, "AREA")); scene.collection.objects.link(L_)
            L_.data.energy = en; L_.data.size = size; L_.data.color = col; L_.location = loc
            L_.rotation_mode = "QUATERNION"; L_.rotation_quaternion = (Vector(look) - Vector(loc)).to_track_quat("-Z", "Y")
        light("key", (1, 1, 1), 300, (-2.5, -3.0, 3.4)); light("fill", (1, 1, 1), 110, (3.0, 2.5, 2.2))
        light("rim", (0.85, 0.85, 1.0), 110, (1.8, 3.2, 2.6), (0, 0, 1.2), 2.0)

        def shoot(path, loc, look, res, ortho, samples=32):
            scene.render.resolution_x, scene.render.resolution_y = res
            scene.cycles.samples = samples
            cam.data.ortho_scale = ortho
            cam.location = loc
            cam.rotation_mode = "QUATERNION"
            cam.rotation_quaternion = (Vector(look) - Vector(loc)).to_track_quat("-Z", "Y")
            scene.render.filepath = path
            bpy.ops.render.render(write_still=True)

        def tile(paths, out):
            ims = [bpy.data.images.load(p) for p in paths]
            W = sum(i.size[0] for i in ims); Hh = max(i.size[1] for i in ims)
            canvas = np.zeros((Hh, W, 4), np.float32); x = 0
            for i in ims:
                a = np.empty(i.size[0] * i.size[1] * 4, np.float32); i.pixels.foreach_get(a)
                a = a.reshape(i.size[1], i.size[0], 4)
                canvas[:i.size[1], x:x + i.size[0]] = a; x += i.size[0]
                bpy.data.images.remove(i)
            im = bpy.data.images.new("tile", W, Hh, alpha=True)
            im.pixels.foreach_set(canvas.ravel()); im.filepath_raw = out; im.file_format = "PNG"; im.save()
            bpy.data.images.remove(im)
            for p in paths: os.remove(p)
        ortho = height * 1.12
        for o in outfits:
            vis = self.outfit_objects(o)
            for n in self.OBJ: bpy.data.objects[n].hide_render = n not in vis
            name0, aims0, curls0 = poses[0]
            self.pose(aims0, curls0)
            ps = []
            for nm, loc in (("front", (0, -6, zc)), ("side", (6, 0, zc)), ("back", (0, 6, zc)), ("threeq", (4.2, -4.2, zc + 0.4))):
                ps.append(os.path.join(self.out, "_%s_%s.png" % (o, nm))); shoot(ps[-1], loc, (0, 0, zc), (520, 1000), ortho)
            tile(ps, os.path.join(self.out, "%s_turn.png" % o))
            if face_z is not None:
                ps = [os.path.join(self.out, "_%s_f1.png" % o), os.path.join(self.out, "_%s_f2.png" % o)]
                shoot(ps[0], (0, -6, face_z), (0, 0, face_z), (600, 600), face_scale)
                shoot(ps[1], (3.4, -5.0, face_z + 0.10), (0, 0, face_z), (600, 600), face_scale)
                tile(ps, os.path.join(self.out, "%s_face.png" % o))
            ps = []
            for pname, aims, curls in poses[1:]:
                self.pose(aims, curls)
                ps.append(os.path.join(self.out, "_%s_%s.png" % (o, pname)))
                shoot(ps[-1], (2.2, -5.6, zc + 0.2), (0, 0, zc), (560, 1000), ortho, 48)
            if ps: tile(ps, os.path.join(self.out, "%s_pose.png" % o))
            self.log("%s rendered" % o)
        self.rest()
        for n in self.OBJ: bpy.data.objects[n].hide_render = False
