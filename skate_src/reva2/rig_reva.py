"""rig_reva.py -- Blender headless: the Meshy REVA statue -> rigged, textured GLB.

    blender --background --factory-startup --python rig_reva.py

The Holographic Neon REVA ships as a 397k-tri STATIC T-pose mesh with a full PBR set
and no rig. This recipe makes her a body: ground the origin, decimate, keep the 4K base
color (emission screen-blended in), build a mixamo-named armature from MEASURED T-POSE
LANDMARKS (all positions derived from the mesh's own slices -- nothing hand-placed),
bind on a watertight proxy and transfer the weights, add a 2-frame rest action so the
anim baker has a clip, and export GLB. canonOf speaks the bone names.

Units: ONE system end to end (the kaeli chimera lesson) -- transforms applied, metres.

REBUILT 2026-09-10 (jacob: "revas model got messed up at some point. her shoulders, her
texture, somthing is not right"). What the first recipe got wrong, all measured on the source:
  * THE ARM JOINTS SAT ON THE UNDERSIDE OF THE ARM. The arm height came from the WIDEST
    slab -- which is the drooping FINGERTIPS at 0.760H, not the arm (axis 0.805H) -- and
    each joint took the mean z of that slab's points: shoulder, elbow and wrist ~5-6 cm
    below the arm's axis. A shoulder pivoting from under the arm throws the deltoid up and
    out when the arm comes down: the hunched ball shoulders in every standing shot.
  * THE SHOULDERS WERE PLACED ABOUT THE BOUNDING BOX, NOT THE BODY. The cybernetic left
    arm is longer, so the bbox centre sits 1.8 cm off her torso midline, and the fixed
    0.185 shoulder x put her left joint 5.6 cm and her right 2 cm outside the armpit.
  * THE HEAD JOINT WAS DRAGGED BACK BY THE PONYTAIL. Its y came from the centroid of
    everything above the neck, hair included: neck->head leaned 26.7 deg BACKWARD, and
    every borrowed clip aligned that to a donor's forward-leaning neck -- her chin on her
    chest. The neck and head now sit on the neck column (throat and nape surfaces).
  * THE TEXTURE WAS CUT TO 1024 (a 16x loss on a chip atlas) and the mesh to 24k tris.
  * Blender's auto-weights were run on the raw Meshy surface (self-intersecting shells,
    the cyber arm a separate one): weights now solve on a voxel proxy and transfer.
"""
import sys  # club-engine kit: machine-independent paths
import bpy, os, math, mathutils
import numpy as np

D = os.path.join(os.environ.get("REVA_SRC", "models/reva_meshy"),
                 "Meshy_AI_REVA_Holographic_Neon_0629075151_image-to-3d-texture_fbx")
FBX = os.path.join(D, "Meshy_AI_REVA_Holographic_Neon_0629075151_image-to-3d-texture.fbx")
OUT = os.environ.get("REVA_OUT", r"C:\MyVeryOwnVideoGames\reva\skate_src\reva2\reva2.glb")
TRI_TARGET = int(os.environ.get("REVA_TRIS", "100000"))
TEX = int(os.environ.get("REVA_TEX", "4096"))
VOXEL = float(os.environ.get("REVA_VOXEL", "0.010"))
PREVIEW = os.environ.get("REVA_PREVIEW", "")       # a folder: rig overlay renders land there

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=FBX)
ob = next(o for o in bpy.data.objects if o.type == "MESH")
bpy.context.view_layer.objects.active = ob
ob.select_set(True)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
me = ob.data

def coords():
    a = np.empty(len(me.vertices) * 3, dtype=np.float64)
    me.vertices.foreach_get("co", a)
    return a.reshape(-1, 3)

def shift(dx, dy, dz):
    a = coords(); a[:, 0] -= dx; a[:, 1] -= dy; a[:, 2] -= dz
    me.vertices.foreach_set("co", a.reshape(-1).astype(np.float32)); me.update()

# ---- ground + center ---------------------------------------------------------------
# NO turn. Meshy ships her facing Blender -Y, and the glTF Y-up export maps -Y to +Z,
# which is club forward. She goes in as she arrives. (The first cut turned her 180 on a
# CIRCULAR measurement -- a toe bone this script had itself placed -- and jacob caught it
# on sight: "she is backward... her back is her front." Facing is asserted below from
# anatomy no bone placement can move.) Facing -Y with +Z up, her left is +X.
P = coords()
shift((P[:, 0].min() + P[:, 0].max()) / 2, (P[:, 1].min() + P[:, 1].max()) / 2, P[:, 2].min())
P = coords()
H = P[:, 2].max()

def slab(z0, z1, pts=None):
    q = P if pts is None else pts
    return q[(q[:, 2] >= z0) & (q[:, 2] <= z1)]

# ⭐ THE TORSO MIDLINE, not the bounding box: the cyber arm is longer than the flesh arm.
# Under the arms (0.70-0.75H) only the ribcage is in the slab; its two flanks are the body.
mids = []
for f in np.arange(0.700, 0.752, 0.005):
    s = slab(H * f - H * 0.003, H * f + H * 0.003)
    s = s[np.abs(s[:, 0]) < 0.40]
    if len(s) > 50: mids.append((s[:, 0].max() + s[:, 0].min()) / 2)
mid = float(np.median(mids))
shift(mid, 0.0, 0.0)
P = coords()
print("torso midline was %+.4f off the bbox centre -- recentred" % mid)

_chest = slab(H * 0.70, H * 0.78)[:, 1].mean() - slab(H * 0.60, H * 0.66)[:, 1].mean()
_lft = slab(0.0, H * 0.05); _lft = _lft[_lft[:, 0] > 0.02]
_lcf = slab(H * 0.22, H * 0.30); _lcf = _lcf[_lcf[:, 0] > 0.02]
_foot = _lft[:, 1].mean() - _lcf[:, 1].mean()
print("facing: chest %+.4f  foot %+.4f  (both must be negative: forward is -Y)" % (_chest, _foot))
assert _chest < 0 and _foot < 0, \
    "source model does not face -Y; the export mapping below would ship her backward"

# ---- landmarks: the ARMS, off their own cross-sections -----------------------------
ARMBAND = P[P[:, 2] > H * 0.65]

def arm_slice(x, half=0.004):
    s = ARMBAND[np.abs(ARMBAND[:, 0] - x) < half]
    if len(s) < 8: return None
    return dict(z=(s[:, 2].min() + s[:, 2].max()) / 2, y=(s[:, 1].min() + s[:, 1].max()) / 2,
                t=s[:, 2].max() - s[:, 2].min())

def torso_edge(sx):
    """the ribcage flank just under the arm: where the armpit is"""
    e = []
    for f in np.arange(0.735, 0.760, 0.005):
        s = slab(H * f - H * 0.003, H * f + H * 0.003)
        s = s[np.abs(s[:, 0]) < 0.40]
        e.append(s[:, 0].max() if sx > 0 else -s[:, 0].min())
    return float(np.median(e))

landmark = {}
for side, sx in (("Left", +1), ("Right", -1)):
    tip = float(P[:, 0].max()) if sx > 0 else float(-P[:, 0].min())
    edge = torso_edge(sx)
    shx = edge + 0.012                        # the humeral head sits just outside the flank
    near = [arm_slice(sx * x) for x in np.arange(shx + 0.02, shx + 0.065, 0.005)]
    near = [n for n in near if n]
    sz = float(np.median([n["z"] for n in near])); sy = float(np.median([n["y"] for n in near]))
    span = tip - shx

    def along(fr):
        x = shx + span * fr
        n = [arm_slice(sx * (x + dx)) for dx in (-0.01, -0.005, 0.0, 0.005, 0.01)]
        n = [k for k in n if k]
        if not n: return (sx * x, sy, sz)
        return (sx * x, float(np.median([k["y"] for k in n])), float(np.median([k["z"] for k in n])))
    landmark[side + "Shoulder"] = (sx * shx * 0.30, sy, sz)
    landmark[side + "Arm"]      = (sx * shx, sy, sz)
    landmark[side + "ForeArm"]  = along(0.43)        # anatomical: upper arm 0.186H of 0.44H
    landmark[side + "Hand"]     = along(0.755)       # forearm 0.146H
    landmark[side + "HandEnd"]  = along(0.985)
    print("%-5s arm: flank %.4f  shoulder x %.4f z %.4f (%.3fH) y %+.4f  tip %.4f" % (
        side, edge, shx, sz, sz / H, sy, tip))

# ---- landmarks: the LEGS (unchanged method; now split about the torso midline) -----
def leg_center(sx, zfrac):
    s = slab(H * zfrac - H * 0.02, H * zfrac + H * 0.02)
    s = s[(s[:, 0] > 0.02) if sx > 0 else (s[:, 0] < -0.02)]
    return tuple(float(v) for v in s.mean(axis=0)) if len(s) else (sx * 0.1, 0.0, H * zfrac)

for side, sx in (("Left", +1), ("Right", -1)):
    landmark[side + "UpLeg"] = leg_center(sx, 0.52)
    landmark[side + "Leg"]   = leg_center(sx, 0.285)
    landmark[side + "Foot"]  = leg_center(sx, 0.045)
    fp = slab(0.0, H * 0.05); fp = fp[(fp[:, 0] > 0.02) if sx > 0 else (fp[:, 0] < -0.02)]
    toe_y = float(fp[:, 1].min()) if len(fp) else -0.1
    fx = landmark[side + "Foot"][0]
    landmark[side + "ToeBase"] = (fx, toe_y * 0.75, H * 0.012)
    landmark[side + "ToeEnd"]  = (fx, toe_y * 0.98, H * 0.010)

hipC = ((landmark["LeftUpLeg"][0] + landmark["RightUpLeg"][0]) / 2,
        (landmark["LeftUpLeg"][1] + landmark["RightUpLeg"][1]) / 2, H * 0.555)
landmark["Hips"]   = hipC
landmark["Spine"]  = (hipC[0], hipC[1], H * 0.62)
landmark["Spine1"] = (hipC[0], hipC[1], H * 0.70)
landmark["Spine2"] = (hipC[0], hipC[1], H * 0.78)

# ---- landmarks: the NECK COLUMN ------------------------------------------------------
# Along the midline and BELOW THE CHIN the neck is exactly two surfaces -- throat (under
# choker and straps) and nape -- with nothing between; above the chin the front surface is
# her JAW. ⛔ The first rebuild read the column at 0.860H: that "throat" was her chin and the
# axis came back 42 deg forward. So the chin is found first and the column read under it.
# ⛔ And the old fractions (Neck 0.865H, Head 0.90H) put the neck joint at her CHIN and the
# head joint at her EYES -- a nod swung her face round her eyes.
ml = P[np.abs(P[:, 0]) < 0.02]
ahead = ml[(ml[:, 1] < -0.06) & (ml[:, 2] > H * 0.83) & (ml[:, 2] < H * 0.92)]
chinZ = float(ahead[:, 2].min())

def surfaces(f):
    s = ml[np.abs(ml[:, 2] - H * f) < H * 0.0025]
    ys = np.sort(s[:, 1]); out = []
    if len(ys):
        st = pv = ys[0]
        for y in ys[1:]:
            if y - pv > 0.012: out.append((st, pv)); st = y
            pv = y
        out.append((st, pv))
    return out

col = []
for f in np.arange(0.825, chinZ / H - 0.004, 0.005):
    c = surfaces(f)
    if len(c) == 2:                                    # throat | nape, nothing else
        col.append((H * f, (c[0][0] + c[0][1] + c[1][0] + c[1][1]) / 4))
assert len(col) >= 2, "no clean two-surface neck column under the chin"
col = np.array(col)
leanMeas = math.degrees(math.atan(-np.polyfit(col[:, 0], col[:, 1], 1)[0]))
tops = []                                              # top of the trapezius beside the neck;
for sx in (+1, -1):                                    # the lower side is the one hair is not on
    c = P[(np.abs(P[:, 0] - sx * 0.10) < 0.01) & (P[:, 2] < H * 0.87) & (np.abs(P[:, 1]) < 0.08)]
    if len(c): tops.append(float(c[:, 2].max()))
neckZ = min(tops) if tops else H * 0.83
headZ = chinZ + H * 0.008                              # the skull pivots just above the chin line
yN = float(np.interp(neckZ, col[:, 0], col[:, 1]))
# The LEAN is the donor library's, because the P5 transfer aligns exactly this segment to
# theirs (measured off the rest skeletons: kmoves 6.5, moves 11.8, anima 14.5 forward); the
# column's own reading is printed beside it and has agreed to within a few degrees.
LEAN = math.radians(11.8)
slope = -math.tan(LEAN)                                # dy/dz; forward is -y
landmark["Neck"]    = (0.0, yN, neckZ)
landmark["Head"]    = (0.0, yN + slope * (headZ - neckZ), headZ)
landmark["HeadEnd"] = (0.0, yN + slope * (headZ - neckZ), H)
print("neck column: %d clean levels, axis y %+.4f, measured lean %.1f deg forward; chin %.3fH"
      % (len(col), yN, leanMeas, chinZ / H))
print("Neck %.3fH  Head %.3fH  neck->head leans %.1f deg forward (donor median)"
      % (neckZ / H, headZ / H, math.degrees(LEAN)))

# ---- the proxy the weights solve on (from the FULL mesh, before decimation) --------
px = ob.copy(); px.data = ob.data.copy(); px.name = "proxy"
bpy.context.scene.collection.objects.link(px)
px.data.materials.clear()
bpy.ops.object.select_all(action="DESELECT")
bpy.context.view_layer.objects.active = px; px.select_set(True)
rm = px.modifiers.new("vox", "REMESH"); rm.mode = "VOXEL"; rm.voxel_size = VOXEL
bpy.ops.object.modifier_apply(modifier="vox")
print("proxy: voxel %.3f -> %d verts" % (VOXEL, len(px.data.vertices)))

# ---- decimate --------------------------------------------------------------------------
bpy.ops.object.select_all(action="DESELECT")
bpy.context.view_layer.objects.active = ob; ob.select_set(True)
tris = len(me.polygons)
mod = ob.modifiers.new("dec", "DECIMATE")
mod.ratio = min(1.0, TRI_TARGET / max(1, tris))
bpy.ops.object.modifier_apply(modifier="dec")
print("DECIMATED %d -> %d tris" % (tris, len(me.polygons)))

# ---- textures: ONE map -- emission screen-blended into base color -----------------
# The engine has no emission channel and its baker takes the GLB's FIRST image as THE
# pack texture -- the first export shipped three (emission first: she rendered
# near-black). Identify maps by MATERIAL WIRING, never by name: the FBX importer names
# images Image_0/2/3. ⭐ Pixels move through foreach_get/set: a 4K image as a Python
# list is 67M floats.
def socket_image(mat, names):
    for nd in mat.node_tree.nodes:
        if nd.type != "BSDF_PRINCIPLED": continue
        for sn in names:
            s = nd.inputs.get(sn)
            if not s: continue
            for ln in s.links:
                n = ln.from_node
                while n is not None:
                    if n.type == "TEX_IMAGE": return n.image
                    n = n.inputs[0].links[0].from_node if n.inputs and n.inputs[0].links else None
    return None

def pixels(img):
    a = np.empty(img.size[0] * img.size[1] * 4, dtype=np.float32)
    img.pixels.foreach_get(a)
    return a.reshape(-1, 4)

mat = me.materials[0]
baseImg = socket_image(mat, ["Base Color"])
emitImg = socket_image(mat, ["Emission Color", "Emission"])
assert baseImg is not None, "no base color image wired in the source material"
if baseImg.size[0] != TEX: baseImg.scale(TEX, TEX)
b = pixels(baseImg)
if emitImg is not None:
    if tuple(emitImg.size) != tuple(baseImg.size): emitImg.scale(baseImg.size[0], baseImg.size[1])
    e = pixels(emitImg)
    b[:, :3] = 1.0 - (1.0 - b[:, :3]) * (1.0 - e[:, :3])   # screen: glow, no clipping
    baseImg.pixels.foreach_set(b.reshape(-1))
    print("emission baked into base color (screen blend)")
baseImg.pack()
m2 = bpy.data.materials.new("reva2")
m2.use_nodes = True
bsdf = next(n for n in m2.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
tex = m2.node_tree.nodes.new("ShaderNodeTexImage")
tex.image = baseImg
m2.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
me.materials.clear(); me.materials.append(m2)
for img in list(bpy.data.images):
    if img is not baseImg: bpy.data.images.remove(img)
print("material rebuilt: 1 image (%dx%d)" % (baseImg.size[0], baseImg.size[1]))

# ---- the armature -----------------------------------------------------------------
arm = bpy.data.armatures.new("rig")
rig = bpy.data.objects.new("rig", arm)
bpy.context.scene.collection.objects.link(rig)
bpy.ops.object.select_all(action="DESELECT")
bpy.context.view_layer.objects.active = rig; rig.select_set(True)
bpy.ops.object.mode_set(mode="EDIT")
CHAIN = [
    ("Hips", None, "Spine"), ("Spine", "Hips", "Spine1"), ("Spine1", "Spine", "Spine2"),
    ("Spine2", "Spine1", "Neck"), ("Neck", "Spine2", "Head"), ("Head", "Neck", "HeadEnd"),
    ("LeftShoulder", "Spine2", "LeftArm"), ("LeftArm", "LeftShoulder", "LeftForeArm"),
    ("LeftForeArm", "LeftArm", "LeftHand"), ("LeftHand", "LeftForeArm", "LeftHandEnd"),
    ("RightShoulder", "Spine2", "RightArm"), ("RightArm", "RightShoulder", "RightForeArm"),
    ("RightForeArm", "RightArm", "RightHand"), ("RightHand", "RightForeArm", "RightHandEnd"),
    ("LeftUpLeg", "Hips", "LeftLeg"), ("LeftLeg", "LeftUpLeg", "LeftFoot"),
    ("LeftFoot", "LeftLeg", "LeftToeBase"), ("LeftToeBase", "LeftFoot", "LeftToeEnd"),
    ("RightUpLeg", "Hips", "RightLeg"), ("RightLeg", "RightUpLeg", "RightFoot"),
    ("RightFoot", "RightLeg", "RightToeBase"), ("RightToeBase", "RightFoot", "RightToeEnd"),
]
ebs = {}
for name, parent, tail in CHAIN:
    eb = arm.edit_bones.new(name)
    eb.head = mathutils.Vector(landmark[name])
    eb.tail = mathutils.Vector(landmark[tail])
    if parent: eb.parent = ebs[parent]; eb.use_connect = False
    ebs[name] = eb
bpy.ops.object.mode_set(mode="OBJECT")
print("armature: %d bones" % len(arm.bones))
for k in ("LeftArm", "RightArm", "LeftForeArm", "RightForeArm", "LeftHand", "RightHand", "Neck", "Head"):
    print("   %-13s %s" % (k, tuple(round(v, 4) for v in landmark[k])))

# ---- bind: heat weights on the proxy, transferred to the body ---------------------
bpy.ops.object.select_all(action="DESELECT")
px.select_set(True); rig.select_set(True)
bpy.context.view_layer.objects.active = rig
bpy.ops.object.parent_set(type="ARMATURE_AUTO")
pw = np.zeros(len(px.data.vertices))
for v in px.data.vertices:
    pw[v.index] = sum(g.weight for g in v.groups)
print("proxy bound: %d vgroups, %d of %d verts UNWEIGHTED" % (len(px.vertex_groups), int((pw < 1e-4).sum()), len(pw)))

# ⛔ data_transfer copies from the ACTIVE object to the SELECTED ones -- the other way
# round silently transfers nothing, and the body arrives at the next step with no groups.
bpy.ops.object.select_all(action="DESELECT")
px.select_set(True); ob.select_set(True)
bpy.context.view_layer.objects.active = px
bpy.ops.object.data_transfer(data_type="VGROUP_WEIGHTS", vert_mapping="POLYINTERP_NEAREST",
                             layers_select_src="ALL", layers_select_dst="NAME", use_create=True)
bpy.ops.object.select_all(action="DESELECT")
ob.select_set(True); bpy.context.view_layer.objects.active = ob
assert len(ob.vertex_groups) >= 22, "weight transfer created %d groups" % len(ob.vertex_groups)
# the head and the hair ride the head: nothing above the neck joint, inside the shoulders,
# takes an arm or clavicle weight (hanging locks sit a hand's width from a deltoid)
KEEP = {"Head", "Neck", "Spine2"}
gname = {g.index: g.name for g in ob.vertex_groups}
zN = landmark["Neck"][2]
for v in me.vertices:
    if v.co.z > zN and abs(v.co.x) < 0.20:
        for g in v.groups:
            if gname[g.group] not in KEEP: g.weight = 0.0
bpy.ops.object.vertex_group_clean(group_select_mode="ALL", limit=0.01)
bpy.ops.object.vertex_group_limit_total(group_select_mode="ALL", limit=4)
# ⛔ NO VERTEX LEAVES UNWEIGHTED. A vertex with no weights exports as joint 0 at weight 0 and
# collapses to the origin in the engine. The first bind left 259 -- hanging locks that had
# only clavicle weight before the head filter took it. Each takes its NEAREST BONE, whole.
segs = [(name, np.array(landmark[name]), np.array(landmark[tail])) for name, parent, tail in CHAIN]
gidx = {g.name: g.index for g in ob.vertex_groups}
orphan = 0
for v in me.vertices:
    if sum(g.weight for g in v.groups) > 1e-4: continue
    p = np.array(v.co[:]); best, bn = 1e9, "Hips"
    for name, h0, t0 in segs:
        d = t0 - h0; u = max(0.0, min(1.0, float(np.dot(p - h0, d) / max(1e-12, np.dot(d, d)))))
        dist = float(np.linalg.norm(p - (h0 + u * d)))
        if dist < best: best, bn = dist, name
    for g in list(v.groups): g.weight = 0.0
    ob.vertex_groups[bn].add([v.index], 1.0, "REPLACE"); orphan += 1
print("orphan verts given their nearest bone: %d" % orphan)
bpy.ops.object.vertex_group_normalize_all(group_select_mode="ALL", lock_active=False)
co = coords()
print("vertex density: x>+0.20 %d   x<-0.20 %d   (the arm weight counts below should follow this)"
      % (int((co[:, 0] > 0.20).sum()), int((co[:, 0] < -0.20).sum())))
bpy.data.objects.remove(px, do_unlink=True)
ob.parent = rig
am = ob.modifiers.new("Armature", "ARMATURE"); am.object = rig
cnt = {g.name: 0 for g in ob.vertex_groups}; zero = 0
for v in me.vertices:
    if not v.groups: zero += 1
    for g in v.groups:
        if g.weight > 0.2: cnt[gname[g.group]] += 1
print("body bound: %d verts, %d unweighted" % (len(me.vertices), zero))
for s in ("Shoulder", "Arm", "ForeArm", "Hand"):
    print("   %-9s left %5d  right %5d" % (s, cnt.get("Left" + s, 0), cnt.get("Right" + s, 0)))

# ---- a 2-frame rest action so the anim baker has a clip ---------------------------
rig.animation_data_create()
act = bpy.data.actions.new("tpose")
rig.animation_data.action = act
for pb in rig.pose.bones:
    pb.keyframe_insert("rotation_quaternion", frame=1)
    pb.keyframe_insert("location", frame=1)
    pb.keyframe_insert("rotation_quaternion", frame=2)
    pb.keyframe_insert("location", frame=2)

# ---- export -----------------------------------------------------------------------
bpy.ops.object.select_all(action="SELECT")
props = bpy.ops.export_scene.gltf.get_rna_type().properties.keys()
kw = dict(filepath=OUT, export_format="GLB", export_animations=True, export_skins=True, export_yup=True)
for k, v in (("export_image_format", "JPEG"), ("export_image_quality", 95), ("export_jpeg_quality", 95)):
    if k in props: kw[k] = v
bpy.ops.export_scene.gltf(**kw)
print("EXPORTED %s  (%s)" % (OUT, {k: v for k, v in kw.items() if "image" in k or "jpeg" in k}))

# ---- the rig drawn over the body: LOOK before baking --------------------------------
if PREVIEW:
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "FLAT"; sc.display.shading.color_type = "MATERIAL"
    sc.display.shading.show_xray = True; sc.display.shading.xray_alpha = 0.35
    sc.render.resolution_x = 1000; sc.render.resolution_y = 1000
    sc.world = bpy.data.worlds.new("w"); sc.world.color = (0.08, 0.08, 0.10)
    m2.diffuse_color = (0.75, 0.70, 0.85, 1.0)
    dot = bpy.data.materials.new("dot"); dot.diffuse_color = (1.0, 0.15, 0.1, 1.0)
    for name in landmark:
        bpy.ops.mesh.primitive_uv_sphere_add(radius=0.012, location=landmark[name], segments=10, ring_count=6)
        bpy.context.active_object.data.materials.append(dot)
    cd = bpy.data.cameras.new("c"); cd.type = "ORTHO"
    cam = bpy.data.objects.new("c", cd); sc.collection.objects.link(cam); sc.camera = cam
    def shoot(tag, ang, centre, scale):
        a = math.radians(ang)
        pos = mathutils.Vector(centre) + mathutils.Vector((4.0 * math.sin(a), -4.0 * math.cos(a), 0.0))
        cam.location = pos
        cam.rotation_euler = (mathutils.Vector(centre) - pos).to_track_quat("-Z", "Y").to_euler()
        cd.ortho_scale = scale
        sc.render.filepath = os.path.join(PREVIEW, "rig_%s.png" % tag)
        bpy.ops.render.render(write_still=True)
    shoot("front", 0, (0, 0, H / 2), H * 1.02)
    shoot("upper", 0, (0, 0, H * 0.80), 0.75)
    shoot("side", 90, (0, 0, H * 0.80), 0.75)
    print("rig overlays -> %s" % PREVIEW)
