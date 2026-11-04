"""garment_split_read.py -- where a pair of legs parts, on the finished mesh, in the rig's rest pose: the lowest point it crosses the middle plane at, then the gap between its legs at each height, front, middle and back. Any mesh with pants, short or legs in its name, in each glb.

jacob, of the alien's shorts, 11-03: "they do separate lower than regular shorts". Legs hanging through each other read 6.6 cm under his crotch, and regular shorts part a few cm under it. The game holds this to 4 cm (verify_clothes).

    blender -b --factory-startup --python skate_src/atelier/garment_split_read.py -- <glb> [<glb> ...]"""
import sys

import bpy
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]


def read(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=path)
    print("\n=====", path)
    rig = next((o for o in bpy.context.scene.objects if o.type == "ARMATURE"), None)
    if rig and rig.animation_data:
        rig.animation_data.action = None
    if rig:
        for pb in rig.pose.bones:
            pb.matrix_basis.identity()
    bpy.context.view_layer.update()
    print("meshes:", [(o.name, len(o.data.vertices)) for o in bpy.context.scene.objects if o.type == "MESH"])
    for ob in bpy.context.scene.objects:
        if ob.type != "MESH":
            continue
        low = ob.name.lower()
        if not any(w in low for w in ("pants", "short", "legs")):
            continue
        me = ob.data
        V = np.array([tuple(ob.matrix_world @ v.co) for v in me.vertices])
        me.calc_loop_triangles()
        T = np.array([tuple(t.vertices) for t in me.loop_triangles])
        a, b, c = V[T[:, 0]], V[T[:, 1]], V[T[:, 2]]
        print("\n--", ob.name, "z %.3f..%.3f, x %.3f..%.3f y %.3f..%.3f" % (V[:, 2].min(), V[:, 2].max(), V[:, 0].min(), V[:, 0].max(), V[:, 1].min(), V[:, 1].max()))
        # where the mesh crosses the middle plane x=0: a line round the crotch, front to back. Its lowest point is where the legs join.
        cross = []
        for p, q in ((a, b), (b, c), (c, a)):
            hit = p[:, 0] * q[:, 0] < 0
            t = (0.0 - p[hit, 0]) / (q[hit, 0] - p[hit, 0])
            cross.append(p[hit] + (q[hit] - p[hit]) * t[:, None])
        cross = np.vstack(cross)
        if len(cross):
            i = cross[:, 2].argmin()
            print("crosses the middle at lowest z %.3f, y %.3f" % (cross[i, 2], cross[i, 1]))
            for y0 in np.arange(-0.15, 0.16, 0.03):
                m = cross[np.abs(cross[:, 1] - y0) < 0.015]
                if len(m):
                    print("   y %+.2f: middle's lowest z %.3f" % (y0, m[:, 2].min()))
        # at each height, the inner wall each side at the front, the middle, and the back
        print("  height: gap between the legs' cloth in x, at each of front, middle and back; widest")
        for z in np.arange(V[:, 2].min() + 0.005, min(V[:, 2].max(), 0.80), 0.02):
            segs = []
            for p, q in ((a, b), (b, c), (c, a)):
                hit = (p[:, 2] - z) * (q[:, 2] - z) < 0
                t = (z - p[hit, 2]) / (q[hit, 2] - p[hit, 2])
                segs.append(p[hit] + (q[hit] - p[hit]) * t[:, None])
            s = np.vstack(segs)
            if not len(s):
                continue
            line = []
            for lo, hi in ((-1, -0.03), (-0.03, 0.03), (0.03, 1)):
                m = s[(s[:, 1] >= lo) & (s[:, 1] < hi)]
                l, r = m[m[:, 0] > 0], m[m[:, 0] < 0]
                line.append("%.3f" % (l[:, 0].min() - r[:, 0].max()) if len(l) and len(r) else "-----")
            print("  z %.3f: front %s middle %s back %s; widest %.3f; y %.3f..%.3f" % (z, line[0], line[1], line[2], np.abs(s[:, 0]).max(), s[:, 1].min(), s[:, 1].max()))


for g in argv:
    read(g)
