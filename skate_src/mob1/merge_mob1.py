"""merge_mob1.py -- Blender headless: fuse the CivicDefenseV2 Source/FBX mocap set
onto one UE-mannequin armature -> mob1.glb (the club's second donor bank).

    blender --background --factory-startup --python merge_mob1.py

Input : C:\\MyVeryOwnVideoGames\\Unreal Projects\\CivicDefenseV2\\Source\\FBX\\...
Output: skate_src/mob1/mob1.glb  (one rig, 20 named clips, stub mesh for the baker)

Clip names ARE the engine contract (donor election is by EXACT name; the census
miner also reads substrings, so parked clips use x_/v_ prefixes that dodge the
locomotion matchers -- the kaeli lesson, see merge_cleo.py).

Rotation-only on purpose: location+scale curves are dropped from every action.
The UE scale-compensation convention crushed kaeli's first bake to a point
(hips scale 1e-4), and the club's donor transfer is rotation-only anyway.
"""
import bpy, os

SRC = r"C:\MyVeryOwnVideoGames\Unreal Projects\CivicDefenseV2\Source\FBX"
OUT = r"C:\MyVeryOwnVideoGames\reva\skate_src\mob1\mob1.glb"

NAME = {
    r"Mobility\In_Place\MOB1_Walk_F_IPC.fbx":                    "w_walk",
    r"Mobility\In_Place\MOB1_Run_F_IPC.fbx":                     "w_run",
    r"Mobility\In_Place\MOB1_Jog_F_IPC.fbx":                     "w_jog",
    r"Mobility\In_Place\MOB1_Stand_Relaxed_Idle_v2_IPC.fbx":     "idle",
    r"Mobility\In_Place\MOB1_Stand_Relaxed_Death_B_IPC.fbx":     "x_down",
    r"Mobility\In_Place\MOB1_Stand_Relaxed_Fgt_v1_IPC.fbx":      "x_fidget1",
    r"Mobility\In_Place\MOB1_Stand_Relaxed_Fgt_v4_IPC.fbx":      "x_fidget2",
    r"Mobility\In_Place\MOB1_Crouch_Idle_V2_IPC.fbx":            "x_crouchidle",
    r"Mobility\In_Place\MOB1_CrouchWalk_F_IPC.fbx":              "x_crouchwalk",
    r"Mobility\In_Place\MOB1_Jog_F_to_Stand_Relaxed_RU_IPC.fbx": "x_jogstop",
    r"Mobility\In_Place\MOB1_Run_F_to_Stand_Relaxed_RU_IPC.fbx": "x_runstop",
    r"Scared\In_Place\SCR_Walk_Scared_Fwd_Look_Left_Loop_IP.fbx":  "x_flee",
    r"Scared\In_Place\SCR_Walk_Scared_Fwd_Look_Right_Loop_IP.fbx": "x_flee2",
    r"Walks\In_Place\Walk_02_Cheerful_Loop_IP.fbx":              "v_walk1",
    r"Walks\In_Place\Walk_04_Texting_Loop_IP.fbx":               "v_walk2",
    r"Walks\In_Place\Walk_06_Look_Around_Loop_IP.fbx":           "v_walk3",
    r"Walks\In_Place\Walk_08_Listen_Music_Loop_IP.fbx":          "v_walk4",
    r"Walks\In_Place\Walk_13_Power_Walk_Loop_IP.fbx":            "v_walk5",
    r"Conversations\Root_Motion\Convo_01_Low_Key_Loop.fbx":      "x_convo1",
    r"Conversations\Root_Motion\Convo_11_Listening_Loop.fbx":    "x_convo2",
}

def log(s):
    print("[mob1] " + s, flush=True)

def action_fcurves(a):
    if getattr(a, "fcurves", None) is not None:
        return list(a.fcurves)
    out = []
    for layer in a.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                out.extend(bag.fcurves)
    return out

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.context.scene.render.fps = 30

# ---- 1. armature from the first import ----
first = os.path.join(SRC, list(NAME.keys())[0])
bpy.ops.import_scene.fbx(filepath=first)
arm = next((o for o in bpy.data.objects if o.type == "ARMATURE"), None)
if not arm:
    raise SystemExit("no armature in " + first)
log("armature %s: %d bones" % (arm.name, len(arm.data.bones)))
arm.animation_data_clear()
if arm.animation_data is None:
    arm.animation_data_create()
donor_bones = set(b.name for b in arm.data.bones)
for a in list(bpy.data.actions):
    bpy.data.actions.remove(a)

# ---- 2. steal each FBX's action ----
kept = []
for rel, clip in sorted(NAME.items(), key=lambda kv: kv[1]):
    p = os.path.join(SRC, rel)
    if not os.path.exists(p):
        log("MISSING: " + p)
        continue
    before_o = set(bpy.data.objects)
    before_a = set(bpy.data.actions)
    bpy.ops.import_scene.fbx(filepath=p)
    new_o = [o for o in bpy.data.objects if o not in before_o]
    new_a = [a for a in bpy.data.actions if a not in before_a]
    if not new_a:
        log("NO ACTION in " + rel)
    else:
        act = max(new_a, key=lambda a: a.frame_range[1] - a.frame_range[0])
        act.name = clip
        act.use_fake_user = True
        n_drop = 0
        for fc in list(action_fcurves(act)):
            if not (fc.data_path.endswith(".location") or fc.data_path.endswith(".scale")):
                continue
            try:
                fc.id_data.fcurves.remove(fc)
            except AttributeError:
                for layer in act.layers:
                    for strip in layer.strips:
                        for bag in strip.channelbags:
                            if fc in list(bag.fcurves):
                                bag.fcurves.remove(fc)
            n_drop += 1
        miss = set()
        for fc in action_fcurves(act):
            dp = fc.data_path
            if dp.startswith('pose.bones["'):
                bn = dp.split('"')[1]
                if bn not in donor_bones:
                    miss.add(bn)
        f0, f1 = act.frame_range
        log("clip %-12s <- %-46s %4d frames, -%d loc/scale%s" %
            (clip, os.path.basename(rel), int(f1 - f0 + 1), n_drop,
             ("  !! unmatched: %s" % sorted(miss)[:3]) if miss else ""))
        for a in new_a:
            if a is not act:
                bpy.data.actions.remove(a)
        kept.append((clip, act))
    for o in new_o:
        bpy.data.objects.remove(o, do_unlink=True)

# stray import-time pose must not leak into sampled export
for pb in arm.pose.bones:
    pb.location = (0.0, 0.0, 0.0)
    pb.scale = (1.0, 1.0, 1.0)

# ---- 3. stub mesh skinned to pelvis (the baker sizes packs by mesh; donor is never drawn) ----
import bmesh
me = bpy.data.meshes.new("stub")
bm = bmesh.new()
bmesh.ops.create_cube(bm, size=0.4)
bm.to_mesh(me)
bm.free()
ob = bpy.data.objects.new("stub", me)
bpy.context.scene.collection.objects.link(ob)
ob.location = (0, 0, 1.0)
vg = ob.vertex_groups.new(name="pelvis")
vg.add(range(len(me.vertices)), 1.0, 'REPLACE')
mod = ob.modifiers.new("arm", 'ARMATURE')
mod.object = arm
ob.parent = arm

# ---- 4. NLA stash + export ----
for clip, act in kept:
    tr = arm.animation_data.nla_tracks.new()
    tr.name = clip
    st = tr.strips.new(clip, int(act.frame_range[0]), act)
    if getattr(act, "slots", None) and len(act.slots) and hasattr(st, "action_slot"):
        st.action_slot = act.slots[0]
    tr.mute = True
arm.animation_data.action = None

bpy.ops.export_scene.gltf(
    filepath=OUT,
    export_format='GLB',
    export_animations=True,
    export_animation_mode='NLA_TRACKS',
    export_skins=True,
    export_yup=True,
)
log("wrote %s (%d clips)" % (OUT, len(kept)))
