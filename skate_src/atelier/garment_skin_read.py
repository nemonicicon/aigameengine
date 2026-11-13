"""garment_skin_read.py -- which skin stays drawn with a garment on, between two heights: faces of the pieces it doesn't hide. Goes by the tailor's names, a skin piece being skin...=<what hides it>,<what hides it>. A face only hides with all its corners covered, so the web of an armpit can stay drawn under a top, and come through it as scraps when he lifts his arm.

    blender -b --factory-startup --python skate_src/atelier/garment_skin_read.py -- <glb> <garment> [height from] [height to]"""
import sys

import bpy
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
glb, garment = argv[0], argv[1]
z_from = float(argv[2]) if len(argv) > 2 else 1.05
z_to = float(argv[3]) if len(argv) > 3 else 1.25
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=glb)
rig = next((o for o in bpy.context.scene.objects if o.type == "ARMATURE"), None)
if rig and rig.animation_data:
    rig.animation_data.action = None
if rig:
    for pb in rig.pose.bones:
        pb.matrix_basis.identity()
bpy.context.view_layer.update()
for ob in bpy.context.scene.objects:
    if ob.type != "MESH" or not ob.name.startswith("skin"):
        continue
    hidden_by = ob.name.split("=")[1].split(",") if "=" in ob.name else []
    me = ob.data
    V = np.array([tuple(ob.matrix_world @ v.co) for v in me.vertices])
    me.calc_loop_triangles()
    T = np.array([tuple(t.vertices) for t in me.loop_triangles])
    C = V[T].mean(1)
    print("%-22s %4d faces, z %.3f..%.3f" % (ob.name, len(T), V[:, 2].min() if len(V) else 0, V[:, 2].max() if len(V) else 0))
    if garment not in hidden_by:
        for t, c in zip(T, C):
            if z_from < c[2] < z_to:
                print("    drawn with it on: centre (%.3f, %.3f, %.3f), corners z %.3f %.3f %.3f, x %.3f %.3f %.3f" % (c[0], c[1], c[2], V[t[0], 2], V[t[1], 2], V[t[2], 2], V[t[0], 0], V[t[1], 0], V[t[2], 0]))
