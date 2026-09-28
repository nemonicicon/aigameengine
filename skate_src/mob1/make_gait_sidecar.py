"""make_gait_sidecar.py -- write the AUTHORED speed of every locomotion clip.

The club plays in-place cycles, so playback rate has to be derived: rate = desired speed
/ the speed the cycle was authored at. crowd_kms.c estimates that by walking the clip
with forward kinematics and measuring the contact foot -- which measured 16% slow on the
one clip that could be checked, and 16% slow means the body outruns its feet.

MoCap Online ships a root-motion sibling of every in-place cycle, and its root
displacement IS the authored speed. This measures each one and writes a sidecar the
engine loads next to the rpack:  <clip name> <club units per second>

    blender --background --factory-startup --python make_gait_sidecar.py
"""
import bpy, os, math

SRC = r"C:\MyVeryOwnVideoGames\Unreal Projects\CivicDefenseV2\Source\FBX"
OUT = r"C:\MyVeryOwnVideoGames\reva\reva_native\v1_mob1.gait"
CLUB = 1.75   # club units per metre

# clip name in the pack  ->  its root-motion sibling
PAIRS = {
    "w_walk":       r"Mobility\Root_Motion\MOB1_Walk_F.fbx",
    "w_jog":        r"Mobility\Root_Motion\MOB1_Jog_F.fbx",
    "w_run":        r"Mobility\Root_Motion\MOB1_Run_F.fbx",
    "x_crouchwalk": r"Mobility\Root_Motion\MOB1_CrouchWalk_F.fbx",
    "v_walk1":      r"Walks\Root_Motion\Walk_02_Cheerful_Loop.fbx",
    "v_walk2":      r"Walks\Root_Motion\Walk_04_Texting_Loop.fbx",
    "v_walk3":      r"Walks\Root_Motion\Walk_06_Look_Around_Loop.fbx",
    "v_walk4":      r"Walks\Root_Motion\Walk_08_Listen_Music_Loop.fbx",
    "v_walk5":      r"Walks\Root_Motion\Walk_13_Power_Walk_Loop.fbx",
    "x_flee":       r"Scared\Root_Motion\SCR_Walk_Scared_Fwd_Look_Left_Loop.fbx",
    "x_flee2":      r"Scared\Root_Motion\SCR_Walk_Scared_Fwd_Look_Right_Loop.fbx",
}

rows = []
for clip, rel in sorted(PAIRS.items()):
    p = os.path.join(SRC, rel)
    if not os.path.exists(p):
        print("  MISSING %s" % rel); continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=p)
    arm = next((o for o in bpy.data.objects if o.type == "ARMATURE"), None)
    if not arm or not arm.animation_data or not arm.animation_data.action:
        print("  no action in %s" % rel); continue
    root = arm.pose.bones.get("root") or arm.pose.bones[0]
    act = arm.animation_data.action
    f0, f1 = int(act.frame_range[0]), int(act.frame_range[1])
    sc = bpy.context.scene
    pts = []
    for f in range(f0, f1 + 1):
        sc.frame_set(f)
        m = arm.matrix_world @ root.matrix
        pts.append((m.translation.x, m.translation.y, m.translation.z))
    dist = sum(math.dist(pts[i-1], pts[i]) for i in range(1, len(pts)))
    secs = (f1 - f0) / sc.render.fps
    ups = (dist / secs) * CLUB if secs else 0.0
    rows.append((clip, ups, f1 - f0, dist))
    print("  %-14s %.3f u/s  (%d frames, root %.3fm)" % (clip, ups, f1 - f0, dist))

with open(OUT, "w") as f:
    f.write("# authored gait speed, club units/sec, measured from the root-motion sibling\n")
    for clip, ups, fr, d in rows:
        f.write("%s %.4f\n" % (clip, ups))
print("wrote %s (%d clips)" % (OUT, len(rows)))
