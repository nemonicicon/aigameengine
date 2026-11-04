"""garment_fold_read.py -- sharp folds in a garment at rest: edges whose two faces turn sharply against each other. A sharp fold is a shard on screen, and this reads the winding too, so wind a garment alike before believing it: on the alien, faces wound one by one read as 37 edges over 90 degrees where wound alike they read 8.

    blender -b --factory-startup --python skate_src/atelier/garment_fold_read.py -- <glb> <part of its name> [degrees to list from]"""
import sys

import bpy
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
glb, part = argv[0], argv[1]
limit = float(argv[2]) if len(argv) > 2 else 50.0
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
    if ob.type != "MESH" or part not in ob.name:
        continue
    me = ob.data
    V = np.array([tuple(ob.matrix_world @ v.co) for v in me.vertices])
    me.calc_loop_triangles()
    T = np.array([tuple(t.vertices) for t in me.loop_triangles])
    n = np.cross(V[T[:, 1]] - V[T[:, 0]], V[T[:, 2]] - V[T[:, 0]])
    area = np.linalg.norm(n, axis=1)
    n = n / np.maximum(area, 1e-12)[:, None]
    edges = {}
    for fi, t in enumerate(T):
        for i in range(3):
            a, b = int(t[i]), int(t[(i + 1) % 3])
            edges.setdefault((min(a, b), max(a, b)), []).append(fi)
    rows = []
    for (a, b), fs in edges.items():
        if len(fs) != 2:
            continue
        c = float(np.dot(n[fs[0]], n[fs[1]]))
        ang = np.degrees(np.arccos(np.clip(c, -1, 1)))
        rows.append((ang, (V[a] + V[b]) * 0.5, np.linalg.norm(V[a] - V[b])))
    rows.sort(key=lambda r: -r[0])
    angs = np.array([r[0] for r in rows])
    print("%s: %d vertices, %d faces; folds: over 30 deg %d, over 50 deg %d, over 70 deg %d, over 90 deg %d; smallest face %.1f mm2" % (ob.name, len(V), len(T), (angs > 30).sum(), (angs > 50).sum(), (angs > 70).sum(), (angs > 90).sum(), area.min() * 0.5 * 1e6))
    for ang, at, length in rows[:14]:
        if ang < limit:
            break
        print("  %.0f deg, %.1f cm long, at (%.3f, %.3f, %.3f)" % (ang, length * 100, at[0], at[1], at[2]))
