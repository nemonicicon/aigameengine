"""garment_cuff_read.py -- a top's sleeves at rest: how far from the shoulder joint each cuff's points are. A sleeve's end goes across an arm, not level, so where to stop drawing the arm's skin goes by this distance, not by a height. Any mesh whose name starts wear~top, the tailor's name for a top.

    blender -b --factory-startup --python skate_src/atelier/garment_cuff_read.py -- <glb> [left upper arm bone] [right upper arm bone]"""
import sys
import bpy
import numpy as np
argv = sys.argv[sys.argv.index("--") + 1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=argv[0])
rig = next(o for o in bpy.context.scene.objects if o.type == "ARMATURE")
if rig.animation_data:
    rig.animation_data.action = None
for pb in rig.pose.bones:
    pb.matrix_basis.identity()
bpy.context.view_layer.update()
JL = np.array(rig.matrix_world @ rig.data.bones[argv[1] if len(argv) > 1 else "upper_arm_L"].head_local)
JR = np.array(rig.matrix_world @ rig.data.bones[argv[2] if len(argv) > 2 else "upper_arm_R"].head_local)
for ob in bpy.context.scene.objects:
    if ob.type != "MESH" or not ob.name.startswith("wear~top"):
        continue
    me = ob.data
    V = np.array([tuple(ob.matrix_world @ v.co) for v in me.vertices])
    me.calc_loop_triangles()
    count = {}
    for t in me.loop_triangles:
        a, b, c = t.vertices
        for p, q in ((a, b), (b, c), (c, a)):
            k = (min(p, q), max(p, q))
            count[k] = count.get(k, 0) + 1
    open_pts = set()
    for k, n in count.items():
        if n == 1:
            open_pts.update(k)
    pts = np.array(sorted(open_pts))
    for name, J, sel in (("L", JL, V[pts, 0] > 0.15), ("R", JR, V[pts, 0] < -0.15)):
        q = V[pts[sel]]
        q = q[q[:, 2] > 0.7]
        d = np.linalg.norm(q - J, axis=1)
        print("%s %s cuff: %d points, from the shoulder joint %.3f..%.3f, mean %.3f; z %.3f..%.3f, x %.3f..%.3f" % (ob.name, name, len(q), d.min(), d.max(), d.mean(), q[:, 2].min(), q[:, 2].max(), q[:, 0].min(), q[:, 0].max()))
print("joint L", JL)
