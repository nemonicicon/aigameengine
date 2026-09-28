"""build_robot.py -- jacob's yellow robot, built in Blender from primitives, rigged, and turned round for a look.

    blender --background --factory-startup --python-exit-code 1 --python skate_src/robot/build_robot.py -- \
        --out <dir> [--rust-cover 18] [--ref <reference sheet image>]

Writes clanker.glb (the rigged body), clanker.blend, clanker_basecolor.png and a Cycles turnaround (turn_*.png)
into <dir> (default skate_src/robot/out). Named on 09-27: "His name is Clanker."

jacob, 09-27: "I want you to try to make a model for me" -- and a character sheet: a rusty yellow robot, capsule
head with a glowing visor slot and an antenna, a chamfered box torso with a panel front and back, dark ball
joints, thin upper arms and wider forearms, pincer claws, thin thighs into wide faceted boots, chunky feet.

A HARD-SURFACE ROBOT IS BUILT, NOT SCULPTED: every part is a primitive shaped by numbers measured off the sheet's
front and side views (1 px = 2.375 mm, sole to head top 640 px = 1.52 m, antenna tip 1.71 m), and every part is
RIGID -- 100% on one bone -- so no pose can bend a panel. The skeleton is Mixamo-named (Hips, Spine, Spine1,
Spine2, Neck, Head, LeftShoulder, LeftArm, LeftForeArm, LeftHand, LeftUpLeg, LeftLeg, LeftFoot, LeftToeBase,
two-joint claw fingers as LeftHandIndex1/2 + LeftHandThumb1/2, and Antenna1/2 for a spring), so the club's canon
and Godot's humanoid retargeting both drive it with the move library we already have.
Faces -Y in Blender (= +Z in glTF, the club's and Godot's forward). Bevels per the hard-surface tiers: primary
edges 5-20 mm, 2-3 segments; flat-shaded shells (the sheet's faceting), smooth joints.
"""
import math, os, sys

import bpy
from mathutils import Vector, Matrix

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
opt = {argv[i]: argv[i + 1] for i in range(0, len(argv) - 1, 2)}
OUT = opt.get("--out", os.path.join(os.path.dirname(os.path.abspath(__file__)), "out"))
os.makedirs(OUT, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

# ------------------------------------------------------------------ materials
def material(name, rgb, rough=0.6, metal=0.0, emit=None):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes.get("Principled BSDF")
    b.inputs["Base Color"].default_value = (*rgb, 1.0)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if emit:
        b.inputs["Emission Color"].default_value = (*emit, 1.0)
        b.inputs["Emission Strength"].default_value = 0.30
    m.diffuse_color = (*rgb, 1.0)          # what Workbench shows
    return m

M_PAINT = material("M_Paint", (0.60, 0.26, 0.015), rough=0.55)   # LINEAR: the sheet's golden ochre, sRGB ~(204, 140, 31)
M_METAL = material("M_Metal", (0.035, 0.035, 0.04), rough=0.35, metal=0.7)
M_VISOR = material("M_Visor", (0.004, 0.004, 0.005), rough=0.15)
M_GLOW = material("M_Glow", (0.92, 0.66, 0.26), rough=0.3, emit=(1.0, 0.78, 0.36))
M_SOLE = material("M_Sole", (0.02, 0.02, 0.02), rough=0.8)

PARTS = []   # (object, bone)


def finish(obj, bone, mat, bevel=None, flat=True, segments=2):
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    if bevel:
        md = obj.modifiers.new("Bevel", "BEVEL")
        md.width = bevel
        md.segments = segments
        md.limit_method = "ANGLE"
        md.angle_limit = math.radians(35)
        md.harden_normals = False
    with bpy.context.temp_override(object=obj, active_object=obj, selected_objects=[obj], selected_editable_objects=[obj]):
        for md in list(obj.modifiers):
            bpy.ops.object.modifier_apply(modifier=md.name)
        if flat:
            bpy.ops.object.shade_flat()
        else:
            bpy.ops.object.shade_smooth()
    vg = obj.vertex_groups.new(name=bone)
    vg.add(list(range(len(obj.data.vertices))), 1.0, "REPLACE")
    PARTS.append((obj, bone))
    return obj


def cyl(name, r, h, loc, verts=16, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=h, location=loc, rotation=rot)
    o = bpy.context.active_object; o.name = name
    return o


def cone(name, r1, r2, h, loc, verts=16):
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r1, radius2=r2, depth=h, location=loc)
    o = bpy.context.active_object; o.name = name
    return o


def sphere(name, r, loc):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=10, radius=r, location=loc)
    o = bpy.context.active_object; o.name = name
    return o


def box(name, size, loc):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=loc)
    o = bpy.context.active_object; o.name = name
    o.scale = size
    with bpy.context.temp_override(object=o, active_object=o, selected_objects=[o], selected_editable_objects=[o]):
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return o


def between(name, a, b, r1, r2=None, verts=16):
    """a cylinder (or cone) from point a to point b"""
    a, b = Vector(a), Vector(b)
    d = b - a
    o = cone(name, r1, r2 if r2 is not None else r1, d.length, (a + b) / 2, verts)
    o.rotation_mode = "QUATERNION"
    o.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(d.normalized())
    with bpy.context.temp_override(object=o, active_object=o, selected_objects=[o], selected_editable_objects=[o]):
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    return o


def wrapped_pill(name, width, height, radius, zc, nu=40, nz=8, ahead=-1, thickness=0.008):
    """a pill (a rounded-end strip) wrapped onto a vertical cylinder of `radius`, centred on the front (-Y).
    ⛔ ROUND 1 FANNED FLAT TRIANGLES FROM THE CENTRE to an outline wrapped on the cylinder: every triangle was a
    CHORD, the widest cut 5.6 cm inside the head, and the visor rendered as a pinched bow-tie. Rows of vertices
    on the cylinder itself, quads between them, and the surface stays on the surface."""
    import bmesh
    bm = bmesh.new()
    hw, hh = width / 2, height / 2
    rows = []
    for j in range(nz + 1):
        z = -hh + 2 * hh * j / nz
        w = hw - hh + math.sqrt(max(hh * hh - z * z, 0.0))      # fully rounded ends
        row = []
        for i in range(nu + 1):
            u = -w + 2 * w * i / nu
            a = u / radius
            row.append(bm.verts.new(Vector((radius * math.sin(a), ahead * radius * math.cos(a), zc + z))))
        rows.append(row)
    for j in range(nz):
        for i in range(nu):
            bm.faces.new((rows[j][i], rows[j][i + 1], rows[j + 1][i + 1], rows[j + 1][i]))
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new(name, me); scene.collection.objects.link(o)
    bpy.context.view_layer.objects.active = o
    sd = o.modifiers.new("Solid", "SOLIDIFY"); sd.thickness = thickness; sd.offset = 1.0
    return o


# ------------------------------------------------------------------ the body (metres; Z up; facing -Y)
Z_HIP, Z_TORSO_TOP, Z_HEAD_BOT, Z_HEAD_TOP = 0.61, 1.16, 1.18, 1.52
X_SH, Z_SH = 0.195, 1.085

# head: a 16-sided cylinder, its top edge rounded into a faceted dome
h = cyl("head", 0.145, Z_HEAD_TOP - Z_HEAD_BOT, (0, 0, (Z_HEAD_BOT + Z_HEAD_TOP) / 2))
import bmesh
bm = bmesh.new(); bm.from_mesh(h.data)
top = [e for e in bm.edges if all(v.co.z > (Z_HEAD_TOP - Z_HEAD_BOT) / 2 - 1e-4 for v in e.verts)]
bmesh.ops.bevel(bm, geom=top, offset=0.085, segments=4, affect="EDGES", profile=0.5)
bot = [e for e in bm.edges if all(v.co.z < -(Z_HEAD_TOP - Z_HEAD_BOT) / 2 + 1e-4 for v in e.verts)]
bmesh.ops.bevel(bm, geom=bot, offset=0.012, segments=2, affect="EDGES", profile=0.5)
bm.to_mesh(h.data); bm.free()
finish(h, "Head", M_PAINT)
v = wrapped_pill("visor", 0.255, 0.088, 0.146, 1.335)
finish(v, "Head", M_VISOR, flat=False)
g = wrapped_pill("visor_glow", 0.190, 0.040, 0.155, 1.335, thickness=0.003)
finish(g, "Head", M_GLOW, flat=False)
finish(cyl("antenna_base", 0.024, 0.018, (0, 0, Z_HEAD_TOP + 0.004), verts=12), "Head", M_METAL, bevel=0.004)
finish(cyl("antenna_stalk_lo", 0.0075, 0.085, (0, 0, Z_HEAD_TOP + 0.055), verts=8), "Antenna1", M_METAL)
finish(cyl("antenna_stalk_hi", 0.0070, 0.085, (0, 0, Z_HEAD_TOP + 0.135), verts=8), "Antenna2", M_METAL)
finish(sphere("antenna_ball", 0.022, (0, 0, 1.70)), "Antenna2", M_PAINT, flat=False)
finish(cyl("neck", 0.105, 0.03, (0, 0, (Z_TORSO_TOP + Z_HEAD_BOT) / 2), verts=16), "Neck", M_METAL)

# torso: a box chamfered on its vertical edges (the sheet's octagonal section), top and bottom rounded off
t = box("torso", (0.310, 0.290, Z_TORSO_TOP - Z_HIP), (0, 0, (Z_HIP + Z_TORSO_TOP) / 2))
bm = bmesh.new(); bm.from_mesh(t.data)
vert = [e for e in bm.edges if abs(e.verts[0].co.z - e.verts[1].co.z) > 0.1]
bmesh.ops.bevel(bm, geom=vert, offset=0.050, segments=2, affect="EDGES", profile=0.5)
for vv in bm.verts:                               # the sheet's torso narrows a little toward the hips
    if vv.co.z < 0: vv.co.x *= 0.94; vv.co.y *= 0.95
bm.to_mesh(t.data); bm.free()
finish(t, "Spine2", M_PAINT, bevel=0.042, segments=3)
# the front panel (a raised door with a dark seam round it) and the back panel with its two vents
finish(box("panel_front_seam", (0.160, 0.010, 0.190), (0, -0.146, 0.885)), "Spine2", M_METAL)
finish(box("panel_front", (0.148, 0.010, 0.178), (0, -0.151, 0.885)), "Spine2", M_PAINT, bevel=0.006)
finish(box("panel_back_seam", (0.220, 0.010, 0.400), (0, 0.146, 0.930)), "Spine2", M_METAL)
finish(box("panel_back", (0.208, 0.010, 0.388), (0, 0.151, 0.930)), "Spine2", M_PAINT, bevel=0.006)
for vx in (-0.030, 0.030):
    for vz in (0.765, 0.785):
        finish(box("vent", (0.040, 0.008, 0.008), (vx, 0.157, vz)), "Spine2", M_METAL)

# hip joints under the torso
for s, X in (("Left", 0.072), ("Right", -0.072)):
    finish(sphere(s + "_hipball", 0.032, (X, 0, 0.600)), s + "UpLeg", M_METAL, flat=False)

# ---- arms: shoulder ball, thin upper arm, elbow ball, wider forearm, wrist ball, a palm and a pincer claw
ARM = {}
for s, sg in (("Left", 1), ("Right", -1)):
    sh = Vector((sg * X_SH, 0, Z_SH)); el = Vector((sg * 0.238, 0, 0.815)); wr = Vector((sg * 0.258, 0, 0.575))
    ARM[s] = (sh, el, wr)
    finish(sphere(s + "_shoulderball", 0.037, sh), s + "Arm", M_METAL, flat=False)
    finish(between(s + "_upperarm", sh + Vector((0, 0, -0.030)), el + Vector((0, 0, 0.020)), 0.026), s + "Arm", M_PAINT, bevel=0.004)
    finish(sphere(s + "_elbowball", 0.028, el), s + "ForeArm", M_METAL, flat=False)
    finish(between(s + "_forearm", el + Vector((0, 0, -0.018)), wr + Vector((0, 0, 0.022)), 0.044, 0.050), s + "ForeArm", M_PAINT, bevel=0.006)
    finish(sphere(s + "_wristball", 0.028, wr), s + "Hand", M_METAL, flat=False)
    palm = wr + Vector((sg * 0.004, 0, -0.050))
    finish(box(s + "_palm", (0.084, 0.060, 0.050), palm), s + "Hand", M_METAL, bevel=0.012)
    # the claw: an outer finger and an inner thumb, two segments each, curling toward each other
    for fname, side_out in (("Index", 1), ("Thumb", -1)):
        x0 = palm.x + sg * side_out * 0.032
        k1 = Vector((x0, 0, palm.z - 0.020))
        k2 = Vector((x0 + sg * side_out * 0.034, 0, k1.z - 0.058))
        tip = Vector((x0 - sg * side_out * 0.010, 0, k2.z - 0.056))
        finish(between("%s_%s1" % (s, fname.lower()), k1, k2, 0.025, 0.022, verts=8), "%sHand%s1" % (s, fname), M_PAINT, bevel=0.003)
        finish(between("%s_%s2" % (s, fname.lower()), k2, tip, 0.022, 0.013, verts=8), "%sHand%s2" % (s, fname), M_PAINT, bevel=0.003)
        finish(sphere("%s_%sknuckle" % (s, fname.lower()), 0.023, k2), "%sHand%s2" % (s, fname), M_METAL, flat=False)
        ARM[s + fname] = (k1, k2, tip)

# ---- legs: thin thigh, knee ball, a wide faceted boot-shaft, a chunky foot with a dark sole
LEG = {}
for s, sg in (("Left", 1), ("Right", -1)):
    hp = Vector((sg * 0.072, 0, 0.600)); kn = Vector((sg * 0.090, 0, 0.372)); an = Vector((sg * 0.095, 0, 0.100))
    LEG[s] = (hp, kn, an)
    finish(between(s + "_thigh", hp, kn + Vector((0, 0, 0.02)), 0.022), s + "UpLeg", M_PAINT, bevel=0.004)
    finish(sphere(s + "_kneeball", 0.026, kn), s + "Leg", M_METAL, flat=False)   # inside the boot top
    finish(between(s + "_shin", kn + Vector((0, 0, 0.030)), an + Vector((0, 0, -0.005)), 0.054, 0.066, verts=10), s + "Leg", M_PAINT, bevel=0.008)
    foot = box(s + "_foot", (0.150, 0.300, 0.090), (an.x, -0.050, 0.052))
    bm = bmesh.new(); bm.from_mesh(foot.data)
    for vv in bm.verts:                       # the toe drops and narrows, as the sheet's boots do
        f = max(0.0, (-vv.co.y - 0.02) / 0.13)
        if vv.co.z > 0: vv.co.z -= 0.030 * f
        vv.co.x *= 1.0 - 0.10 * f
    bm.to_mesh(foot.data); bm.free()
    finish(foot, s + "Foot", M_PAINT, bevel=0.022, segments=3)
    finish(box(s + "_sole", (0.155, 0.306, 0.016), (an.x, -0.050, 0.008)), s + "Foot", M_SOLE, bevel=0.004)

# ------------------------------------------------------------------ the skeleton (Mixamo names)
arm_data = bpy.data.armatures.new("robot_rig")
rig = bpy.data.objects.new("robot_rig", arm_data)
scene.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode="EDIT")
E = arm_data.edit_bones


def bone(name, head, tail, parent=None):
    b = E.new(name); b.head = Vector(head); b.tail = Vector(tail)
    if parent: b.parent = E[parent]; b.use_connect = False
    return b

bone("Hips", (0, 0, Z_HIP), (0, 0, 0.70))
bone("Spine", (0, 0, 0.70), (0, 0, 0.85), "Hips")
bone("Spine1", (0, 0, 0.85), (0, 0, 1.00), "Spine")
bone("Spine2", (0, 0, 1.00), (0, 0, Z_TORSO_TOP), "Spine1")
bone("Neck", (0, 0, Z_TORSO_TOP), (0, 0, Z_HEAD_BOT), "Spine2")
bone("Head", (0, 0, Z_HEAD_BOT), (0, 0, Z_HEAD_TOP), "Neck")
bone("Antenna1", (0, 0, Z_HEAD_TOP + 0.012), (0, 0, Z_HEAD_TOP + 0.097), "Head")
bone("Antenna2", (0, 0, Z_HEAD_TOP + 0.097), (0, 0, 1.722), "Antenna1")
for s, sg in (("Left", 1), ("Right", -1)):
    sh, el, wr = ARM[s]
    bone(s + "Shoulder", (sg * 0.06, 0, Z_SH), tuple(sh), "Spine2")
    bone(s + "Arm", tuple(sh), tuple(el), s + "Shoulder")
    bone(s + "ForeArm", tuple(el), tuple(wr), s + "Arm")
    bone(s + "Hand", tuple(wr), tuple(wr + Vector((0, 0, -0.065))), s + "ForeArm")
    for fname in ("Index", "Thumb"):
        k1, k2, tip = ARM[s + fname]
        bone("%sHand%s1" % (s, fname), tuple(k1), tuple(k2), s + "Hand")
        bone("%sHand%s2" % (s, fname), tuple(k2), tuple(tip), "%sHand%s1" % (s, fname))
    hp, kn, an = LEG[s]
    bone(s + "UpLeg", tuple(hp), tuple(kn), "Hips")
    bone(s + "Leg", tuple(kn), tuple(an), s + "UpLeg")
    bone(s + "Foot", tuple(an), (an.x, -0.12, 0.025), s + "Leg")
    bone(s + "ToeBase", (an.x, -0.12, 0.025), (an.x, -0.20, 0.025), s + "Foot")
bpy.ops.object.mode_set(mode="OBJECT")

# ------------------------------------------------------------------ one mesh, rigid on its bones
objs = [p[0] for p in PARTS]
with bpy.context.temp_override(active_object=objs[0], object=objs[0], selected_objects=objs, selected_editable_objects=objs):
    bpy.ops.object.join()
body = objs[0]; body.name = "robot_body"
# ⛔ THE JOIN KEEPS THE FIRST PART'S ORIGIN -- the head's, 1.35 m up. The rust's "low down" and "at the feet"
# terms read OBJECT coordinates, so everything below the head counted as the feet: round 3 rusted 80%, the next
# 92%. Applied, the body's origin is the ground at his centre, which is also what a skin wants.
with bpy.context.temp_override(object=body, active_object=body, selected_objects=[body], selected_editable_objects=[body]):
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
print("[robot] body origin", tuple(round(x, 3) for x in body.location), "z range %.3f..%.3f"
      % (min(v.co.z for v in body.data.vertices), max(v.co.z for v in body.data.vertices)))
body.parent = rig
md = body.modifiers.new("Armature", "ARMATURE"); md.object = rig
nb = len(arm_data.bones); nv = len(body.data.vertices)
missing = [vg.name for vg in body.vertex_groups if vg.name not in arm_data.bones]
print("[robot] %d parts -> one mesh: %d verts, %d tris; %d bones; groups with no bone: %s"
      % (len(PARTS), nv, sum(len(p.vertices) - 2 for p in body.data.polygons), nb, missing or "none"))
assert not missing


# ------------------------------------------------------------------ rust and paint wear, baked to one atlas
# The sheet's finish: yellow paint, brown rust in patches, heavier at edges and low down, dark grime on the
# joints. Built as a node tree in rig space (so a patch crosses part seams naturally) and BAKED to one
# 2048 colour image -- glTF, the club and Godot all want a texture, not a node tree.
def mk(nt, kind, **inputs):
    n = nt.nodes.new(kind)
    for k, v in inputs.items():
        n.inputs[k].default_value = v
    return n


def ramp(nt, src, stops):
    r = nt.nodes.new("ShaderNodeValToRGB")
    els = r.color_ramp.elements
    els[0].position, els[0].color = stops[0][0], (*stops[0][1], 1.0)
    els[1].position, els[1].color = stops[1][0], (*stops[1][1], 1.0)
    for pos, col in stops[2:]:
        e = els.new(pos); e.color = (*col, 1.0)
    nt.links.new(src, r.inputs["Fac"])
    return r


def rust_tree(mat, paint, rust_amount):
    nt = mat.node_tree; L = nt.links
    bsdf = nt.nodes.get("Principled BSDF")
    co = nt.nodes.new("ShaderNodeTexCoord")
    patches = mk(nt, "ShaderNodeTexNoise", Scale=9.0, Detail=12.0, Roughness=0.66)
    L.new(co.outputs["Object"], patches.inputs["Vector"])
    speck = mk(nt, "ShaderNodeTexNoise", Scale=38.0, Detail=6.0, Roughness=0.7)
    L.new(co.outputs["Object"], speck.inputs["Vector"])
    stretch = nt.nodes.new("ShaderNodeMapping"); stretch.inputs["Scale"].default_value = (14.0, 14.0, 1.3)
    L.new(co.outputs["Object"], stretch.inputs["Vector"])
    streak = mk(nt, "ShaderNodeTexNoise", Scale=2.0, Detail=4.0, Roughness=0.5)
    L.new(stretch.outputs["Vector"], streak.inputs["Vector"])
    # edges: where the bevelled normal leaves the true one
    bev = nt.nodes.new("ShaderNodeBevel"); bev.samples = 8; bev.inputs["Radius"].default_value = 0.010
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    dot = nt.nodes.new("ShaderNodeVectorMath"); dot.operation = "DOT_PRODUCT"
    L.new(bev.outputs["Normal"], dot.inputs[0]); L.new(geo.outputs["Normal"], dot.inputs[1])
    edge = nt.nodes.new("ShaderNodeMapRange"); edge.inputs["From Min"].default_value = 0.985; edge.inputs["From Max"].default_value = 0.9995
    edge.inputs["To Min"].default_value = 1.0; edge.inputs["To Max"].default_value = 0.0
    L.new(dot.outputs["Value"], edge.inputs["Value"])
    # low down rusts more: feet, boots, the bottom of the torso
    sep = nt.nodes.new("ShaderNodeSeparateXYZ"); L.new(co.outputs["Object"], sep.inputs["Vector"])
    low = nt.nodes.new("ShaderNodeMapRange"); low.inputs["From Min"].default_value = 0.0; low.inputs["From Max"].default_value = 0.45
    low.inputs["To Min"].default_value = 1.0; low.inputs["To Max"].default_value = 0.0
    L.new(sep.outputs["Z"], low.inputs["Value"])
    # mask = patches*a + edges*b + low*c + streaks*d + speckle, then a sharp-ish ramp
    def mul(a, k):
        m = nt.nodes.new("ShaderNodeMath"); m.operation = "MULTIPLY"; m.inputs[1].default_value = k
        L.new(a, m.inputs[0]); return m.outputs[0]
    def add(a, b):
        m = nt.nodes.new("ShaderNodeMath"); m.operation = "ADD"
        L.new(a, m.inputs[0]); L.new(b, m.inputs[1]); return m.outputs[0]
    # ⛔ ROUND 3 RUSTED 80% OF HIM: the threshold (0.83) sat BELOW the mask's mean (~0.8), so rust was the
    # base and paint the patches -- the sheet is the other way round. The mask's mean is ~0.7 now and the
    # threshold sits ~1 std above it; `RUST_THR` moves it, and the bake reports the coverage it produced.
    # the sheet rusts at the EDGES and the TOES first: the boots' toe caps are the rustiest thing on him
    feet = nt.nodes.new("ShaderNodeMapRange"); feet.inputs["From Min"].default_value = 0.0; feet.inputs["From Max"].default_value = 0.12
    feet.inputs["To Min"].default_value = 1.0; feet.inputs["To Max"].default_value = 0.0
    L.new(sep.outputs["Z"], feet.inputs["Value"])
    # ⛔ THE HEIGHT TERMS OUTRANKED THE NOISE: composited by coverage, the "top 18%" landed on the bottom faces of
    # the torso and the feet (solid rust rectangles nobody sees) and left the body clean. The patch NOISE decides
    # where rust goes; edges and toes only nudge it.
    acc = add(mul(patches.outputs["Fac"], 1.0), mul(edge.outputs["Result"], 0.15 * rust_amount))
    acc = add(acc, mul(low.outputs["Result"], 0.0))
    acc = add(acc, mul(feet.outputs["Result"], 0.06 * rust_amount))
    acc = add(acc, mul(streak.outputs["Fac"], 0.02 * rust_amount))
    acc = add(acc, mul(speck.outputs["Fac"], 0.12))
    thr = float(opt.get("--rust-thr", "0.84")) if rust_amount > 0 else 9.0
    mask = ramp(nt, acc, [(min(thr, 0.99), (0, 0, 0)), (min(thr + 0.05, 1.0), (1, 1, 1))])
    # rust colour varies light to dark with the speckle; paint picks up a little grime
    rust = ramp(nt, speck.outputs["Fac"], [(0.35, (0.055, 0.017, 0.005)), (0.65, (0.21, 0.065, 0.016))])   # the sheet's deep orange-brown
    grime = ramp(nt, patches.outputs["Fac"], [(0.3, paint), (0.8, tuple(c * 0.78 for c in paint))])
    half = nt.nodes.new("ShaderNodeMath"); half.operation = "MULTIPLY"; half.inputs[1].default_value = 0.5
    L.new(acc, half.inputs[0])
    L.new(grime.outputs["Color"], bsdf.inputs["Base Color"])
    return {"acc": half.outputs[0], "base": grime.outputs["Color"], "rust": rust.outputs["Color"]}

LAYERS = {M_PAINT.name: rust_tree(M_PAINT, (0.60, 0.26, 0.015), 1.0)}
LAYERS[M_METAL.name] = rust_tree(M_METAL, (0.035, 0.035, 0.04), 0.0)      # the sheet's joints are clean dark metal
LAYERS[M_SOLE.name] = rust_tree(M_SOLE, (0.02, 0.02, 0.02), 0.0)

bpy.ops.object.select_all(action="DESELECT")
body.select_set(True); bpy.context.view_layer.objects.active = body
bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.004)
bpy.ops.object.mode_set(mode="OBJECT")
# ⭐ RUST BY COVERAGE, NOT BY THRESHOLD. Six rounds of tuning a threshold inside the node tree gave 80%, 92%, 27%
# and 21% -- each "21%" mostly grime, no splotch in sight. So the tree's parts are baked as LAYERS (the rust mask
# before its threshold, the paint, the rust colour, which texels are paint) and composited here: the threshold is
# the percentile that rusts exactly RUST_COVER of the paint, and each patch gets a thin dark dirty rim.
RUST_COVER = float(opt.get("--rust-cover", "18"))
scene.render.engine = "CYCLES"; scene.cycles.device = "CPU"; scene.cycles.samples = 8
scene.render.bake.margin = 6


def layer_image(name, noncolor=False):
    im = bpy.data.images.new(name, 2048, 2048)
    if noncolor: im.colorspace_settings.name = "Non-Color"
    return im


def bake_emit(img, pick):
    """pick(material) -> an output socket to emit, or an RGB tuple"""
    for m in (M_PAINT, M_METAL, M_VISOR, M_GLOW, M_SOLE):
        nt = m.node_tree
        out = nt.nodes.get("Material Output")
        em = nt.nodes.get("BAKE_EMIT") or nt.nodes.new("ShaderNodeEmission"); em.name = "BAKE_EMIT"
        for l in list(em.inputs["Color"].links): nt.links.remove(l)
        v = pick(m)
        if isinstance(v, tuple): em.inputs["Color"].default_value = (*v, 1.0)
        else: nt.links.new(v, em.inputs["Color"])
        nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
        tn = nt.nodes.get("BAKE_IMG") or nt.nodes.new("ShaderNodeTexImage"); tn.name = "BAKE_IMG"
        tn.image = img; nt.nodes.active = tn
    bpy.ops.object.bake(type="EMIT", use_clear=True)


def base_of(m):
    if m.name in LAYERS: return LAYERS[m.name]["base"]
    return tuple(m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value[:3])

A_ = layer_image("L_acc", True); bake_emit(A_, lambda m: LAYERS[M_PAINT.name]["acc"] if m is M_PAINT else (0.0, 0.0, 0.0))
P_ = layer_image("L_paint", True); bake_emit(P_, lambda m: (1.0, 1.0, 1.0) if m is M_PAINT else (0.0, 0.0, 0.0))
B_ = layer_image("L_base"); bake_emit(B_, base_of)
R_ = layer_image("L_rust"); bake_emit(R_, lambda m: LAYERS[M_PAINT.name]["rust"] if m is M_PAINT else base_of(m))
import numpy as np


def arr(img):
    a = np.empty(len(img.pixels), np.float32); img.pixels.foreach_get(a); return a.reshape(-1, 4)
acc = arr(A_)[:, 0] * 2.0; isp = arr(P_)[:, 0] > 0.5; base = arr(B_)[:, :3]; rustc = arr(R_)[:, :3]
thr = float(np.percentile(acc[isp], 100.0 - RUST_COVER))
m_ = np.clip((acc - (thr - 0.010)) / 0.020, 0.0, 1.0) * isp
rim = np.clip((acc - (thr - 0.050)) / 0.040, 0.0, 1.0) * isp * (1.0 - m_)
col = base * (1.0 - 0.28 * rim[:, None])
col = col * (1.0 - m_[:, None]) + rustc * m_[:, None]
IMG = bpy.data.images.new("clanker_basecolor", 2048, 2048)
IMG.pixels.foreach_set(np.concatenate([col, np.ones((len(col), 1), np.float32)], 1).ravel())
IMG.filepath_raw = os.path.join(OUT, "clanker_basecolor.png"); IMG.file_format = "PNG"; IMG.save()
print("[robot] rust: %.0f%% of the paint at threshold %.3f (rim %.0f%%); baked %s"
      % (100.0 * m_[isp].mean(), thr, 100.0 * rim[isp].mean(), IMG.filepath_raw))
for m in (M_PAINT, M_METAL, M_VISOR, M_GLOW, M_SOLE):
    nt = m.node_tree
    for n in list(nt.nodes):
        if n.name in ("BAKE_EMIT",): nt.nodes.remove(n)
    tn = nt.nodes.get("BAKE_IMG"); tn.image = IMG
    nt.links.new(nt.nodes["Principled BSDF"].outputs["BSDF"], nt.nodes["Material Output"].inputs["Surface"])
# the export materials: the baked atlas as base colour (the glow keeps its emission for Godot)
for m in (M_PAINT, M_METAL, M_VISOR, M_GLOW, M_SOLE):
    nt = m.node_tree
    for n in list(nt.nodes):
        if n.type not in ("BSDF_PRINCIPLED", "OUTPUT_MATERIAL", "TEX_IMAGE"):
            nt.nodes.remove(n)
    img = [n for n in nt.nodes if n.type == "TEX_IMAGE"][0]
    b = nt.nodes.get("Principled BSDF")
    nt.links.new(img.outputs["Color"], b.inputs["Base Color"])

# ------------------------------------------------------------------ save, export, and turn it round
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "clanker.blend"))
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, "clanker.glb"), export_format="GLB", use_selection=False,
                          export_skins=True, export_animations=False, export_yup=True)
print("[robot] wrote", os.path.join(OUT, "clanker.glb"))

scene.render.engine = "CYCLES"
scene.view_settings.view_transform = "Standard"
scene.cycles.samples = 24
scene.cycles.use_denoising = True
scene.render.resolution_x, scene.render.resolution_y = 520, 900
key = bpy.data.objects.new("key", bpy.data.lights.new("key", "AREA")); scene.collection.objects.link(key)
key.data.energy = 900; key.data.size = 3.0; key.location = (-2.5, -3.0, 3.2)
key.rotation_mode = "QUATERNION"; key.rotation_quaternion = (Vector((0, 0, 0.9)) - key.location).to_track_quat("-Z", "Y")
fill = bpy.data.objects.new("fill", bpy.data.lights.new("fill", "AREA")); scene.collection.objects.link(fill)
fill.data.energy = 350; fill.data.size = 3.0; fill.location = (3.0, 2.5, 2.0)
fill.rotation_mode = "QUATERNION"; fill.rotation_quaternion = (Vector((0, 0, 0.9)) - fill.location).to_track_quat("-Z", "Y")
world = bpy.data.worlds.new("w"); scene.world = world; world.use_nodes = True
world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.85, 0.85, 0.85, 1.0)
world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.55
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); scene.collection.objects.link(cam)
cam.data.type = "ORTHO"; cam.data.ortho_scale = 1.95
scene.camera = cam
views = [("front", (0, -6, 0.86)), ("side", (6, 0, 0.86)), ("back", (0, 6, 0.86)), ("threeq", (4.2, -4.2, 1.5))]
for nm, loc in views:
    cam.location = loc
    d = Vector((0, 0, 0.86)) - Vector(loc)
    cam.rotation_mode = "QUATERNION"
    cam.rotation_quaternion = d.to_track_quat("-Z", "Y")
    scene.render.filepath = os.path.join(OUT, "turn_%s.png" % nm)
    bpy.ops.render.render(write_still=True)
print("[robot] turnaround rendered")
