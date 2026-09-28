"""ue_export_cleo.py -- run inside UnrealEditor-Cmd to pull ZAMN_Cleo back OUT of Unreal.

The original Meshy FBX downloads are gone; the imported uassets are the only copy of the
character. This exports, from /Game/TopDown/Models/ZAMN_Cleo:
  * every AnimSequence            -> anim/<folder>.fbx   (skeleton + keys, no mesh)
  * ONE SkeletalMesh (Walking's)  -> mesh/cleo_mesh.fbx  (skin + bind pose)
  * that folder's two textures    -> tex/texture_0.tga, texture_01.tga
Blender then merges the actions onto the one armature and emits a single GLB.
"""
import unreal, os

OUT = r"C:\MyVeryOwnVideoGames\reva\skate_src\kaeli\export"
ROOT = "/Game/TopDown/Models/ZAMN_Cleo"
MESH_DONOR = "Animation_Walking_withSkin"   # every folder carries the same skin; one donor is enough

for sub in ("anim", "mesh", "tex"):
    os.makedirs(os.path.join(OUT, sub), exist_ok=True)

def export(asset, filename, options=None):
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = filename
    task.automated = True
    task.replace_identical = True
    task.prompt = False
    if options is not None:
        task.options = options
    ok = unreal.Exporter.run_asset_export_task(task)
    unreal.log("EXPORT %s -> %s : %s" % (asset.get_name(), filename, "OK" if ok else "FAILED"))
    return ok

fbx_anim = unreal.FbxExportOption()
fbx_anim.ascii = False
fbx_anim.export_morph_targets = False
fbx_anim.export_preview_mesh = False
fbx_anim.map_skeletal_motion_to_root = False

fbx_mesh = unreal.FbxExportOption()
fbx_mesh.ascii = False
fbx_mesh.export_morph_targets = False
fbx_mesh.export_preview_mesh = False
fbx_mesh.level_of_detail = False

paths = unreal.EditorAssetLibrary.list_assets(ROOT, recursive=True, include_folder=False)
unreal.log("CLEO EXPORT: %d assets under %s" % (len(paths), ROOT))

n_anim = n_mesh = n_tex = 0
for p in sorted(paths):
    asset = unreal.EditorAssetLibrary.load_asset(p)
    if asset is None:
        unreal.log_warning("could not load " + p)
        continue
    # /Game/TopDown/Models/ZAMN_Cleo/<ANIM_FOLDER>/SkeletalMeshes/<asset>.<asset>
    parts = p.split("/")
    folder = parts[-3] if len(parts) >= 3 else ""
    if isinstance(asset, unreal.AnimSequence):
        # one anim per folder; name the file after the folder = the movement's name
        f = os.path.join(OUT, "anim", folder + ".fbx")
        if export(asset, f, fbx_anim):
            n_anim += 1
    elif isinstance(asset, unreal.SkeletalMesh) and folder == MESH_DONOR:
        if export(asset, os.path.join(OUT, "mesh", "cleo_mesh.fbx"), fbx_mesh):
            n_mesh += 1
    elif isinstance(asset, unreal.Texture2D) and MESH_DONOR in p:
        f = os.path.join(OUT, "tex", asset.get_name() + ".tga")
        if export(asset, f):
            n_tex += 1

unreal.log("CLEO EXPORT DONE: %d anims, %d mesh, %d textures" % (n_anim, n_mesh, n_tex))
