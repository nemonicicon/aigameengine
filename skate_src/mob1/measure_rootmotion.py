"""measure_rootmotion.py -- ground truth for authored gait speed.

The club plays IN-PLACE clips and derives playback rate by measuring how fast the
contact foot travels (clipStride in crowd_kms.c). That is an ESTIMATE. MoCap Online
ships a root-motion sibling of every in-place cycle, and its root displacement over the
take is the authored speed as a FACT. This prints it so the estimator can be checked
against it instead of trusted.
"""
import bpy, os, math

SRC = r"C:\MyVeryOwnVideoGames\Unreal Projects\CivicDefenseV2\Source\FBX"
PAIRS = [
    ("Walk_02_Cheerful",  r"Walks\Root_Motion\Walk_02_Cheerful_Loop.fbx"),
    ("Walk_13_Power",     r"Walks\Root_Motion\Walk_13_Power_Walk_Loop.fbx"),
    ("MOB1_Walk_F",       r"Mobility\Root_Motion\MOB1_Walk_F.fbx"),
    ("MOB1_Jog_F",        r"Mobility\Root_Motion\MOB1_Jog_F.fbx"),
    ("MOB1_Run_F",        r"Mobility\Root_Motion\MOB1_Run_F.fbx"),
]
CLUB = 1.75   # club units per metre (3.15u tall body ~ 1.8m); same constant the props use

print("=== AUTHORED GAIT SPEED (root motion, ground truth) ===")
for name, rel in PAIRS:
    p = os.path.join(SRC, rel)
    if not os.path.exists(p):
        print("  MISSING %s" % rel); continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=p)
    arm = next((o for o in bpy.data.objects if o.type == "ARMATURE"), None)
    if not arm:
        print("  no armature in %s" % rel); continue
    root = arm.pose.bones.get("root") or arm.pose.bones.get("Root") or arm.pose.bones[0]
    sc = bpy.context.scene
    # the ACTION's range, not the scene's: the scene defaults to 1-250 and the take is
    # usually far shorter, so averaging over the scene range buries the motion in
    # stillness (it reported a run at 0.43 m/s -- a slow amble).
    act = arm.animation_data.action if arm.animation_data else None
    if act is not None:
        f0, f1 = int(act.frame_range[0]), int(act.frame_range[1])
    else:
        f0, f1 = sc.frame_start, sc.frame_end
    pts = []
    for f in range(f0, f1 + 1):
        sc.frame_set(f)
        m = arm.matrix_world @ root.matrix
        pts.append((m.translation.x, m.translation.y, m.translation.z))
    dist = sum(math.dist(pts[i-1], pts[i]) for i in range(1, len(pts)))
    frames = f1 - f0
    fps = sc.render.fps
    secs = frames / fps if fps else 0
    mps = dist / secs if secs else 0
    print("  %-18s %3d frames @%dfps = %.2fs   root %.3fm -> %.3f m/s = %.3f u/s  (%.4f u/frame)  action=%s"
          % (name, frames, fps, secs, dist, mps, mps * CLUB, mps * CLUB / fps if fps else 0, act.name if act else "?"))
