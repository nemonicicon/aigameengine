"""merge_cleo.py -- Blender headless: fuse the 21 UE anim exports onto one armature -> kaeli.glb

    blender --background --factory-startup --python merge_cleo.py

Input  (skate_src/kaeli/export/):  mesh/cleo_mesh.fbx, anim/*.fbx, tex/*.tga
Output (skate_src/kaeli/):         kaeli.glb  (one rig, one skin, 21 named clips, basecolor embedded)

Clip names ARE the engine contract (crowd_kms.c hero clip-map, read 2026-08-25):
  exact w_walk/w_run -> locomotion; substring idle -> foot idle; trick/flip substr -> tricks;
  p_* -> the dance rotation. Parked clips use x_ names chosen to dodge every substring the
  matcher knows (run/jog/sprint/walk/idle/stand/flip/trick/ollie/alk), or they would clobber
  a locomotion slot -- x_runshoot contains "run" and would have STOLEN g_rvRun.
"""
import bpy, os, sys

BASE = r"C:\MyVeryOwnVideoGames\reva\skate_src\kaeli"
EXP = os.path.join(BASE, "export")

NAME = {
    "Animation_Walking_withSkin":                            "w_walk",
    "Animation_Running_withSkin":                            "w_run",
    "Animation_RunFast_withSkin":                            "x_dashfast",
    "Animation_Unsteady_Walk_withSkin":                      "x_unsteady",   # 'alk' only matters for crowd walkIdx; hero exacts win first -- still renamed clear
    "Animation_Axe_Stance_withSkin":                         "idle",
    "Animation_Backflip_withSkin":                           "t_trick",
    "Animation_Backflip_Jump_withSkin":                      "t_trick2",
    "Animation_Back_Jump_withSkin":                          "x_backhop",
    "Animation_All_Night_Dance_withSkin":                    "p_allnight",
    "Animation_Bass_Beats_withSkin":                         "p_bassbeats",
    "Animation_Breakdance_1990_withSkin":                    "p_break1990",
    "Animation_Gangnam_Groove_withSkin":                     "p_gangnam",
    "Animation_Confident_Strut_withSkin":                    "p_strut",
    "Animation_Angry_Ground_Stomp_withSkin":                 "p_stomp",
    "Animation_Angry_To_Tantrum_Sit_withSkin":               "x_tantrum",
    "Animation_Chair_Sit_Idle_F_withSkin":                   "x_chairsat",   # not 'idle': that substring would steal the foot-idle slot from the real idle
    "Animation_Crawl_and_Look_Back_withSkin":                "x_crawl",
    "Animation_Fall_Dead_from_Abdominal_Injury_withSkin":    "x_down",
    "Animation_Forward_Reload_Subtle_withSkin":              "x_reload",
    "Animation_Forward_Roll_and_Fire_withSkin":              "x_tumble",
    "Animation_Run_and_Shoot_withSkin":                      "x_gunfire",
}

def log(s):
    print("[merge] " + s, flush=True)

def action_fcurves(a):
    """Blender 5.x actions are slotted/layered; Action.fcurves is gone."""
    if getattr(a, "fcurves", None) is not None:
        return list(a.fcurves)
    out = []
    for layer in a.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                out.extend(bag.fcurves)
    return out

def clean_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)

def import_glb(path):
    """Everything (mesh AND anims) rides the UE glTF exporter + this one importer, so the
    skeleton conventions match on both sides by construction -- the FBX path asserted in a
    commandlet (SkinnedMeshComponent MeshObject) and mixing importers risks twisted rests."""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    return [o for o in bpy.data.objects if o not in before]

clean_scene()
bpy.context.scene.render.fps = 60   # Meshy clips are 60fps; keys land on integer frames

# ---- 1. the donor mesh: armature + skin ----
mesh_glb = os.path.join(EXP, "mesh", "cleo_mesh.glb")
objs = import_glb(mesh_glb)
arm = next((o for o in objs if o.type == "ARMATURE"), None)
meshes = [o for o in objs if o.type == "MESH"]
if not arm or not meshes:
    raise SystemExit("donor import gave no armature/mesh: %r" % ([o.type for o in objs]))
log("donor: armature %s (%d bones), %d mesh objects, %s" %
    (arm.name, len(arm.data.bones), len(meshes),
     ", ".join("%s(%dv)" % (m.name, len(m.data.vertices)) for m in meshes)))
arm.animation_data_clear()
if arm.animation_data is None:
    arm.animation_data_create()
donor_bones = set(b.name for b in arm.data.bones)

# the donor FBX may carry its own walking take -- drop every action so only named clips ship
for a in list(bpy.data.actions):
    bpy.data.actions.remove(a)

# ---- 2. steal each anim FBX's action ----
kept = []
for folder, clip in sorted(NAME.items(), key=lambda kv: kv[1]):
    p = os.path.join(EXP, "anim_glb", folder + ".glb")
    if not os.path.exists(p):
        log("MISSING anim glb: " + p)
        continue
    before_actions = set(bpy.data.actions)
    objs2 = import_glb(p)
    new_actions = [a for a in bpy.data.actions if a not in before_actions]
    if not new_actions:
        log("NO ACTION in " + folder)
    else:
        act = max(new_actions, key=lambda a: a.frame_range[1] - a.frame_range[0])
        act.name = clip
        act.use_fake_user = True
        # THE COLLAPSED-SKELETON BUG (2026-08-25, rpack probe: every clip frame lands on a
        # single point). MEASURED in the merged GLB: the clip carries Hips scale = 1e-4
        # every frame -- UE's scale-compensation rig convention -- which crushes the whole
        # subtree to a dot on our flat-centimeter rig; the Hips location track is in that
        # convention's units too. Bone lengths are rig geometry, not animation, so v1 ships
        # ROTATION-ONLY clips: drop every location and scale curve, pin hips at rest. The
        # exporter SAMPLES the pose (fcurve surgery alone is not enough), so the pose-bone
        # fallbacks are reset after the steal as well.
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
        if n_drop:
            log("  %s: dropped %d location/scale curves (rotation-only retarget)" % (clip, n_drop))
        miss = set()
        for fc in action_fcurves(act):
            dp = fc.data_path
            if dp.startswith('pose.bones["'):
                bn = dp.split('"')[1]
                if bn not in donor_bones:
                    miss.add(bn)
        f0, f1 = act.frame_range
        log("clip %-12s <- %-52s  %4d frames%s" %
            (clip, folder, int(f1 - f0 + 1),
             ("  !! %d bones unmatched: %s" % (len(miss), sorted(miss)[:4])) if miss else ""))
        for a in new_actions:
            if a is not act:
                bpy.data.actions.remove(a)
        kept.append((clip, act))
    for o in objs2:
        bpy.data.objects.remove(o, do_unlink=True)

# the sampled exporter falls back to current pose-bone values for missing channels;
# a stray import-time pose (the 1e-4 hips scale) must not leak into every clip
for pb in arm.pose.bones:
    pb.location = (0.0, 0.0, 0.0)
    pb.scale = (1.0, 1.0, 1.0)

# ---- 3. stash every clip as an NLA track (glTF NLA_TRACKS mode: track name = animation name) ----
for clip, act in kept:
    tr = arm.animation_data.nla_tracks.new()
    tr.name = clip
    st = tr.strips.new(clip, int(act.frame_range[0]), act)
    if getattr(act, "slots", None) and len(act.slots) and hasattr(st, "action_slot"):
        st.action_slot = act.slots[0]   # slotted actions (Blender 4.4+): the strip must name its slot
    tr.mute = True
arm.animation_data.action = None

# ---- 4. material: basecolor from the exported texture ----
texs = sorted(os.path.join(EXP, "tex", f) for f in os.listdir(os.path.join(EXP, "tex")))
log("textures: " + ", ".join(os.path.basename(t) for t in texs))
base_tex = None
for t in texs:
    if "texture_0." in os.path.basename(t):    # texture_0 = basecolor by Meshy convention
        base_tex = t
        break
if base_tex is None and texs:
    base_tex = texs[0]
if base_tex:
    img = bpy.data.images.load(base_tex)
    mat = bpy.data.materials.new("cleo_mat")
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    texnode = mat.node_tree.nodes.new("ShaderNodeTexImage")
    texnode.image = img
    mat.node_tree.links.new(texnode.outputs["Color"], bsdf.inputs["Base Color"])
    for m in meshes:
        m.data.materials.clear()
        m.data.materials.append(mat)
    # inspection copies as PNG (save_render forces the pixel read; plain save() on a
    # freshly loaded image dies with 'does not have any image data')
    for t in texs:
        i2 = bpy.data.images.load(t, check_existing=False)
        i2.save_render(os.path.join(BASE, os.path.splitext(os.path.basename(t))[0] + ".png"))

# ---- 5. export one GLB ----
out = os.path.join(BASE, "kaeli.glb")
bpy.ops.export_scene.gltf(
    filepath=out,
    export_format='GLB',
    export_animations=True,
    export_animation_mode='NLA_TRACKS',
    export_skins=True,
    export_yup=True,
    export_image_format='AUTO',
)
log("wrote %s (%d clips)" % (out, len(kept)))
