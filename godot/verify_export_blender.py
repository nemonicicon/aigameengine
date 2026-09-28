"""verify_export_blender.py -- does an exported .glb say what the pack says? An INDEPENDENT reader checks.

    blender --background --factory-startup --python godot/verify_export_blender.py -- \
        <pack.rpack> <moves.glb> [--render out_dir --clip z_rm_sk_kf --frames 0,8,14,20]

Two paths to the same numbers:
  pack --(this script's own forward kinematics)--> joint world positions
  pack --rpack2gltf.py--> .glb --(Blender's glTF importer, Blender's evaluator)--> joint world positions
Every animation is sampled at up to 8 frames and every joint compared; the report is the worst error in
metres. With --render, a Workbench still of the stick figure (and the board) at the chosen frames, from
a fixed three-quarter view, for a look -- a number that matches can still be a wrong convention shared
by both sides of the comparison, and only a picture against the club engine's own bench catches that.
"""
import math, os, struct, sys

import bpy
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
PACK, GLB = argv[0], argv[1]
opt = {argv[i]: argv[i + 1] for i in range(2, len(argv) - 1, 2)}
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "skate_src", "author"))
from rpack import Rpack


def q2m(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def trs(t, q, s):
    M = np.eye(4); M[:3, :3] = q2m(q) * np.array(s)[None, :]; M[:3, 3] = t
    return M


R = Rpack(PACK)
nodes = R.nodes()
names = [n["name"] for n in nodes]
clips = {R.clip(c)["name"]: c for c in range(R.nClips)}


def fk(ci, f):
    """joint world positions of clip ci at frame f, in glTF/pack axes"""
    loc = {i: (list(n["t"]), list(n["q"]), list(n["s"])) for i, n in enumerate(nodes)}
    for n, t, d in R.tracks(ci):
        D = np.frombuffer(d, "<f4")
        if t == 0: loc[n] = (loc[n][0], list(D[f * 4:f * 4 + 4]), loc[n][2])
        elif t == 1: loc[n] = (list(D[f * 3:f * 3 + 3]), loc[n][1], loc[n][2])
    W = {}
    for i, n in enumerate(nodes):
        L = trs(*loc[i])
        W[i] = L if n["parent"] < 0 else W[n["parent"]] @ L
    return {names[i]: W[i][:3, 3] for i, n in enumerate(nodes) if n["kind"] == 1}


bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.context.scene.render.fps = 30
bpy.ops.import_scene.gltf(filepath=os.path.abspath(GLB))
arm = [o for o in bpy.context.scene.objects if o.type == "ARMATURE"][0]
board = bpy.context.scene.objects.get("skateboard")
if arm.animation_data is None:
    arm.animation_data_create()


def assign(obj, act):
    if obj is None: return
    if obj.animation_data is None: obj.animation_data_create()
    ad = obj.animation_data
    ad.action = act
    if hasattr(ad, "action_slot") and hasattr(act, "slots") and len(act.slots):
        # the importer puts every object's curves in one action, one slot per object
        for sl in act.slots:
            if sl.target_id_type == "OBJECT" and (sl.name_display == obj.name or len(act.slots) == 1):
                ad.action_slot = sl; break


def key_frames(act):
    ks = set()
    fcs = []
    if hasattr(act, "layers") and len(act.layers):
        for lay in act.layers:
            for st in lay.strips:
                for cb in st.channelbags:
                    fcs += list(cb.fcurves)
    else:
        fcs = list(act.fcurves)
    for fc in fcs:
        for kp in fc.keyframe_points:
            ks.add(round(kp.co[0], 3))
    return sorted(ks)


worst_all = 0.0
report = []
for act in bpy.data.actions:
    nm = act.name.split("_skeleton")[0].split("|")[0]
    nm = nm if nm in clips else next((c for c in clips if act.name.startswith(c)), None)
    if nm is None:
        continue
    ci = clips[nm]
    c = R.clip(ci)
    assign(arm, act)
    kf = key_frames(act)
    if not kf:
        continue
    f0, step = kf[0], (bpy.context.scene.render.fps / c["fps"])
    worst = 0.0
    for f in sorted(set(int(round(x)) for x in np.linspace(0, c["frames"] - 1, min(8, c["frames"])))):
        fr = f0 + f * step
        bpy.context.scene.frame_set(int(math.floor(fr)), subframe=fr - math.floor(fr))
        ref = fk(ci, f)
        for pb in arm.pose.bones:
            if pb.name not in ref: continue
            h = arm.matrix_world @ pb.head
            g = np.array([h.x, h.z, -h.y])            # Blender Z-up -> glTF Y-up
            worst = max(worst, float(np.linalg.norm(g - ref[pb.name])))
    worst_all = max(worst_all, worst)
    report.append((nm, c["frames"], worst))

for nm, F, w in sorted(report):
    print("[verify] %-22s %4d f   worst joint error %.6f m" % (nm, F, w))
print("[verify] %d animations, worst joint error over all: %.6f m -> %s"
      % (len(report), worst_all, "PASS" if report and worst_all < 1e-3 else "FAIL"))

if "--render" in opt or "--render" in argv:
    out = opt.get("--render")
    clip = opt.get("--clip", "z_rm_sk_kf")
    frames = [int(x) for x in opt.get("--frames", "0,10,20").split(",")]
    act = next(a for a in bpy.data.actions if a.name.startswith(clip))
    assign(arm, act)
    if board is not None:
        assign(board, act)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"; sc.display.shading.color_type = "MATERIAL"
    sc.render.resolution_x, sc.render.resolution_y = 640, 720
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); sc.collection.objects.link(cam)
    cam.location = (2.6, -3.2, 1.6)                       # three-quarter front-right, Blender axes
    d = np.array([0.0, 0.0, 0.8]) - np.array(cam.location)
    cam.rotation_euler = (math.atan2(math.hypot(d[0], d[1]), -d[2]), 0.0, math.atan2(d[1], d[0]) - math.pi / 2)
    cam.data.lens = 40
    sc.camera = cam
    kf = key_frames(act); f0 = kf[0]; c = R.clip(clips[clip]); step = sc.render.fps / c["fps"]
    os.makedirs(out, exist_ok=True)
    for f in frames:
        fr = f0 + f * step
        sc.frame_set(int(math.floor(fr)), subframe=fr - math.floor(fr))
        sc.render.filepath = os.path.join(out, "%s_f%03d.png" % (clip, f))
        bpy.ops.render.render(write_still=True)
        print("[verify] rendered", sc.render.filepath)
