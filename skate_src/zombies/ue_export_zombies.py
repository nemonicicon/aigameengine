"""ue_export_zombies.py -- UE headless: the ZAMN zombie clip bank -> GLB, for Z4.

    UnrealEditor-Cmd.exe GunExport.uproject -run=pythonscript -script=ue_export_zombies.py
        -stdout -FullStdOutLogOutput -unattended -nosplash -nullrhi

Source: ZAMNRemake/Content/ZombieAnimationPack/Animations/Mannequin_UE5 (32 AnimSequences,
copied into the shim). Same artery as ue_export_town.py and skate_src/kaeli's
ue_export_anim_gltf.py -- exporting an AnimSequence through GLTFExporter brings its
skeletal mesh along with it, so each GLB is one rig + one clip.

⭐ THESE ARRIVE IN THE CLUB'S OWN BONE LANGUAGE. The canonical map in crowd_kms.c is
written against UE names -- "pelvis", "spine_", "clavicle_l", "upperarm_l", "thigh_l",
"calf_l" -- because that is the convention the whole retarget was authored to. The UE5
mannequin these clips ride is therefore not "a rig we must map"; it is the reference rig.
Nothing here needs a bone-name translation table.

The 32 GLBs are then fused onto one armature by merge_zombies.py (Blender headless), the
way merge_mob1.py fused the CivicDefenseV2 mocap -- one rig, named clips, one rpack.
"""
import unreal, os

OUT = r"C:\MyVeryOwnVideoGames\reva\skate_src\zombies\export"
os.makedirs(OUT, exist_ok=True)
ROOT = "/Game/ZombieAnimationPack/Animations/Mannequin_UE5"

opts = unreal.GLTFExportOptions()
opts.export_vertex_skin_weights = True
opts.export_material_variants = unreal.GLTFMaterialVariantMode.NONE

paths = unreal.EditorAssetLibrary.list_assets(ROOT, recursive=True, include_folder=False)
n = 0
fail = 0
for p in sorted(paths):
    asset = unreal.EditorAssetLibrary.load_asset(p)
    if not isinstance(asset, unreal.AnimSequence):
        continue
    name = asset.get_name()                       # anim_Walk_A
    f = os.path.join(OUT, name + ".glb")
    ok = unreal.GLTFExporter.export_to_gltf(asset, f, opts, set())
    unreal.log("ZOMB: %s -> %s" % (name, "OK" if ok else "FAILED"))
    if ok:
        n += 1
    else:
        fail += 1
unreal.log("ZOMBIE ANIMS DONE: %d ok, %d failed" % (n, fail))
