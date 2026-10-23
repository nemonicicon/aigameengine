"""garment_posecheck.py -- does a character's clothing SURVIVE THE RIG? Every wardrobe piece of a built character,
posed through a set of test poses: how far each garment's fabric STRETCHES against its rest shape, how far it DRIFTS
off the body (a flap standing out of her back reads as drift), and how much of it ends up INSIDE the body -- per
piece, per pose -- plus one heat-map sheet (fabric coloured by stretch, the body grey) that is the verdict.

jacob's friend, 09-29, of REVA's off-shoulder jacket in the sheet pose: "its sticking to the rig its not the model
its the rig thats messing it up". A garment's weights are the rig's business, and a turnaround in the rest pose
cannot see them at all: this is the bench that can.

    blender --background --factory-startup --python-exit-code 1 --python skate_src/atelier/garment_posecheck.py -- \
        --blend <character.blend> --out <dir> [--poses sheet,down,forward,up,stride,squat] [--outfit street]

Reads meshes named by the wardrobe tags (build_reva.py): skin / skin~h=... / head... are the body;
wear~<slot>~<piece>~<outfits> are the pieces. Writes <out>/garments_<outfit>.png and prints the table.
"""
import math, os, sys, time

import bpy
import bmesh
import numpy as np
from mathutils import Vector, Matrix, Quaternion
from mathutils.bvhtree import BVHTree

T0 = time.time()
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
opt = {argv[i]: argv[i + 1] for i in range(0, len(argv) - 1, 2)}
BLEND = opt["--blend"]
OUT = opt.get("--out", os.path.dirname(BLEND)); os.makedirs(OUT, exist_ok=True)
POSES = opt.get("--poses", "rest,sheet,down,forward,up,stride,squat").split(",")
OUTFIT = opt.get("--outfit", "")
bpy.ops.wm.open_mainfile(filepath=BLEND)
scene = bpy.context.scene
rig = [o for o in scene.objects if o.type == "ARMATURE"][0]

meshes = [o for o in scene.objects if o.type == "MESH" and o.parent == rig]
body = [o for o in meshes if o.name.startswith(("skin", "head"))]
ALLBODY = list(body)          # ⛔ distances are to the WHOLE body, the skin under the clothes included: measured against
                              # only the skin an outfit leaves visible, the shorts "drifted" 17 cm -- off the thigh below
wear = [o for o in meshes if o.name.startswith("wear~")]


def outfits_of(o):
    return set(o.name.split("~")[3].split("+")) if o.name.count("~") >= 3 else set()


if OUTFIT:
    wear = [o for o in wear if OUTFIT in outfits_of(o)]
    hidden = set()
    for o in wear: hidden.add(o.name.split("~")[2])
    body = [o for o in body if "~h=" not in o.name or not (set(o.name.split("~h=")[1].split(",")) & hidden)]
print("[pose] %s: %d body meshes, %d pieces%s" % (os.path.basename(BLEND), len(body), len(wear),
                                                   (" (outfit %s)" % OUTFIT) if OUTFIT else ""))


# ------------------------------------------------------------------ the poses (Mixamo-style names)
def rest():
    for pb in rig.pose.bones:
        pb.rotation_mode = "QUATERNION"; pb.rotation_quaternion = (1, 0, 0, 0); pb.location = (0, 0, 0)
    bpy.context.view_layer.update()


def aim(bname, target):
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


POSE = {
    "rest": [],
    # the holographic sheet's stance (build_reva.py sheet_pose): right hand on the hip, left forearm up
    "sheet": [("Spine2", (0.02, 0.0, 1.0)), ("Neck", (-0.04, -0.05, 1.0)), ("Head", (-0.10, -0.03, 1.0)),
              ("RightUpLeg", (0.01, 0.0, -1.0)), ("RightLeg", (0.0, 0.02, -1.0)),
              ("LeftUpLeg", (-0.13, -0.28, -0.95)), ("LeftLeg", (-0.05, 0.10, -0.99)), ("LeftFoot", (0.0, -0.55, -0.83)),
              ("RightArm", (-0.25, -0.22, -0.94)), ("RightForeArm", (-0.22, -0.35, 0.91)), ("RightHand", (-0.25, -0.20, 0.95)),
              ("LeftArm", (0.50, 0.25, -0.83)), ("LeftForeArm", (-0.62, -0.40, -0.65)), ("LeftHand", (-0.25, -0.35, -0.90))],
    "down": [("LeftArm", (0.20, 0.02, -0.98)), ("RightArm", (-0.20, 0.02, -0.98)),
             ("LeftForeArm", (0.24, -0.05, -0.97)), ("RightForeArm", (-0.24, -0.05, -0.97))],
    "forward": [("LeftArm", (0.15, -0.98, -0.10)), ("RightArm", (-0.15, -0.98, -0.10)),
                ("LeftForeArm", (0.10, -0.99, 0.05)), ("RightForeArm", (-0.10, -0.99, 0.05))],
    "up": [("LeftArm", (0.35, 0.0, 0.94)), ("RightArm", (-0.35, 0.0, 0.94)),
           ("LeftForeArm", (0.20, 0.0, 0.98)), ("RightForeArm", (-0.20, 0.0, 0.98))],
    "stride": [("LeftUpLeg", (0.05, -0.50, -0.86)), ("LeftLeg", (0.03, 0.15, -0.99)),
               ("RightUpLeg", (-0.05, 0.40, -0.92)), ("RightLeg", (-0.03, 0.55, -0.83)),
               ("LeftArm", (0.25, 0.35, -0.90)), ("RightArm", (-0.25, -0.40, -0.88)),
               ("LeftForeArm", (0.15, 0.10, -0.98)), ("RightForeArm", (-0.10, -0.75, -0.65))],
    "squat": [("Spine", (0.0, -0.35, 0.94)), ("LeftUpLeg", (0.18, -0.95, -0.25)), ("RightUpLeg", (-0.18, -0.95, -0.25)),
              ("LeftLeg", (0.08, 0.30, -0.95)), ("RightLeg", (-0.08, 0.30, -0.95)),
              ("LeftArm", (0.30, -0.90, -0.30)), ("RightArm", (-0.30, -0.90, -0.30))],
}


def pose(name):
    rest()
    for bname, d in POSE[name]: aim(bname, d)
    bpy.context.view_layer.update()


def evaluated(o):
    """the posed mesh: world vertices and triangles"""
    dg = bpy.context.evaluated_depsgraph_get()
    ev = o.evaluated_get(dg)
    me = ev.to_mesh()
    V = np.array([(o.matrix_world @ v.co)[:] for v in me.vertices])
    me.calc_loop_triangles()
    T = np.array([t.vertices[:] for t in me.loop_triangles], int)
    E = np.array([e.vertices[:] for e in me.edges], int)
    ev.to_mesh_clear()
    return V, T, E


def body_tree():
    V, F, off = [], [], 0
    for o in ALLBODY:
        Vb, Tb, _ = evaluated(o)
        V += [Vector(v) for v in Vb]
        F += [[off + int(i) for i in t] for t in Tb]
        off += len(Vb)
    return BVHTree.FromPolygons(V, F), V


def signed(tree, P):
    """distance to the body, negative inside (by the nearest face's normal, faces wound outward by the build)"""
    d = np.empty(len(P))
    for i, p in enumerate(P):
        loc, n, idx, dist = tree.find_nearest(Vector(p), 1.0)
        if loc is None: d[i] = 1.0; continue
        d[i] = dist if (Vector(p) - loc).dot(n) >= 0 else -dist
    return d


# ------------------------------------------------------------------ measure
rest()
bt0, _ = body_tree()
REST = {}
SKIN0 = {}
for o in ALLBODY:
    V, T, E = evaluated(o)
    SKIN0[o.name] = (E, np.linalg.norm(V[E[:, 1]] - V[E[:, 0]], axis=1))
for o in wear:
    V, T, E = evaluated(o)
    L0 = np.linalg.norm(V[E[:, 1]] - V[E[:, 0]], axis=1)
    REST[o.name] = (V, T, E, L0, signed(bt0, V))
res = {}
heat = {}
for pn in POSES:
    pose(pn)
    bt, _ = body_tree()
    sk = []
    for o in ALLBODY:
        E, L0 = SKIN0[o.name]
        V, _, _ = evaluated(o)
        ok = L0 > 5e-4
        sk.append(np.abs(np.linalg.norm(V[E[ok, 1]] - V[E[ok, 0]], axis=1) / L0[ok] - 1.0))
    sk = np.concatenate(sk)
    print("[pose] %-8s the skin itself: stretch p95 %.1f%%, p99 %.1f%%" % (pn, 100 * np.percentile(sk, 95), 100 * np.percentile(sk, 99)))
    for o in wear:
        V0, T, E, L0, d0 = REST[o.name]
        V, _, _ = evaluated(o)
        L = np.linalg.norm(V[E[:, 1]] - V[E[:, 0]], axis=1)
        ok = L0 > 5e-4                                   # rims and lips have sub-mm edges: ratios of nothing
        st = np.zeros(len(E)); st[ok] = L[ok] / L0[ok] - 1.0
        d = signed(bt, V)
        drift = d - d0
        vst = np.zeros(len(V)); np.maximum.at(vst, E[:, 0], np.abs(st)); np.maximum.at(vst, E[:, 1], np.abs(st))
        heat[(pn, o.name)] = vst
        res[(pn, o.name)] = (np.percentile(np.abs(st), 95), np.abs(st).max(), np.percentile(drift, 99), drift.max(),
                             100.0 * np.mean((d < -0.002) & (d0 >= -0.002)))
    print("[pose] %-8s measured (%.0f s)" % (pn, time.time() - T0))

print("\n[pose] %-8s %-34s %9s %9s %9s %9s %8s" % ("pose", "piece", "str p95", "str max", "drift99", "driftmax", "inside"))
worst = {}
for (pn, on), (s95, smax, d99, dmax, ins) in sorted(res.items(), key=lambda kv: (kv[0][1], POSES.index(kv[0][0]))):
    piece = on.split("~")[2]
    print("[pose] %-8s %-34s %8.1f%% %8.0f%% %7.1fcm %7.1fcm %7.1f%%" % (pn, piece, 100 * s95, 100 * smax, 100 * d99, 100 * dmax, ins))
    w = worst.setdefault(piece, [0, 0, 0])
    w[0] = max(w[0], s95); w[1] = max(w[1], d99); w[2] = max(w[2], ins)
print("\n[pose] WORST PER PIECE (over the poses): stretch p95 / drift p99 / inside")
for piece, (s, d, i) in sorted(worst.items(), key=lambda kv: -kv[1][0]):
    print("[pose]   %-24s %6.1f%%  %6.1f cm  %5.1f%%" % (piece, 100 * s, 100 * d, i))

# ------------------------------------------------------------------ the sheet: fabric by stretch, the body grey
scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.light = "STUDIO"
scene.display.shading.color_type = "VERTEX"
scene.display.shading.show_cavity = False
for o in scene.objects:
    if o.type == "MESH": o.hide_render = not (o in body or o in wear)
for o in body:
    ca = o.data.color_attributes.get("PC") or o.data.color_attributes.new("PC", "FLOAT_COLOR", "POINT")
    ca.data.foreach_set("color", np.tile([0.62, 0.60, 0.58, 1.0], len(o.data.vertices)).astype(np.float32))
    o.data.color_attributes.active_color = ca


def heat_rgb(s):
    """0 % stretch = blue-violet, 10 % = amber, 30 %+ = red"""
    t = np.clip(s / 0.30, 0.0, 1.0)
    return np.stack([0.30 + 0.70 * np.clip(t * 2.0, 0, 1), 0.35 + 0.45 * np.clip(1.0 - np.abs(t - 0.35) * 2.2, 0, 1),
                     0.85 * (1.0 - t), np.ones_like(t)], -1)


cam = bpy.data.objects.new("pc_cam", bpy.data.cameras.new("pc_cam")); scene.collection.objects.link(cam)
cam.data.type = "ORTHO"; scene.camera = cam
zc = sum((rig.matrix_world @ b.head_local).z for b in rig.data.bones if b.name in ("Hips", "Head")) / 2.0
top = max((rig.matrix_world @ b.tail_local).z for b in rig.data.bones)
cam.data.ortho_scale = top * 1.12
scene.render.resolution_x, scene.render.resolution_y = 360, 620
tiles = []
for pn in POSES:
    pose(pn)
    for o in wear:
        ca = o.data.color_attributes.get("PC") or o.data.color_attributes.new("PC", "FLOAT_COLOR", "POINT")
        ca.data.foreach_set("color", heat_rgb(heat[(pn, o.name)]).astype(np.float32).ravel())
        o.data.color_attributes.active_color = ca
    for vn, loc in (("front", (0, -8, zc)), ("back", (0, 8, zc)), ("side", (8, 0, zc))):
        cam.location = loc
        cam.rotation_mode = "QUATERNION"
        cam.rotation_quaternion = (Vector((0, 0, zc)) - Vector(loc)).to_track_quat("-Z", "Y")
        p = os.path.join(OUT, "_pc_%s_%s.png" % (pn, vn)); scene.render.filepath = p
        bpy.ops.render.render(write_still=True)
        tiles.append((pn, vn, p))
rest()
W, H = scene.render.resolution_x, scene.render.resolution_y
cols = 3
sheet = np.zeros((H * len(POSES), W * cols, 4), np.float32)
for k, (pn, vn, p) in enumerate(tiles):
    im = bpy.data.images.load(p)
    a = np.empty(W * H * 4, np.float32); im.pixels.foreach_get(a)
    r, c = POSES.index(pn), k % cols
    sheet[(len(POSES) - 1 - r) * H:(len(POSES) - r) * H, c * W:(c + 1) * W] = a.reshape(H, W, 4)
    bpy.data.images.remove(im); os.remove(p)
im = bpy.data.images.new("pc_sheet", W * cols, H * len(POSES), alpha=True)
im.pixels.foreach_set(sheet.ravel())
outp = os.path.join(OUT, "garments_%s.png" % (OUTFIT or "all"))
im.filepath_raw = outp; im.file_format = "PNG"; im.save()
print("[pose] sheet: %s (rows = %s; columns = front, back, side) (%.0f s)" % (outp, ", ".join(POSES), time.time() - T0))
