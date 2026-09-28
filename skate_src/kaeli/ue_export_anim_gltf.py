import unreal, os
OUT = r"C:\MyVeryOwnVideoGames\reva\skate_src\kaeli\export\anim_glb"
os.makedirs(OUT, exist_ok=True)
ROOT = "/Game/TopDown/Models/ZAMN_Cleo"
opts = unreal.GLTFExportOptions()
opts.export_vertex_skin_weights = True
opts.export_material_variants = unreal.GLTFMaterialVariantMode.NONE
paths = unreal.EditorAssetLibrary.list_assets(ROOT, recursive=True, include_folder=False)
n = 0
for p in sorted(paths):
    if not p.endswith("_Anim." + p.split("/")[-1].split(".")[-1]):
        pass
    asset = unreal.EditorAssetLibrary.load_asset(p)
    if not isinstance(asset, unreal.AnimSequence):
        continue
    parts = p.split("/")
    folder = parts[-3] if len(parts) >= 3 else asset.get_name()
    f = os.path.join(OUT, folder + ".glb")
    ok = unreal.GLTFExporter.export_to_gltf(asset, f, opts, set())
    unreal.log("GLTF ANIM %s : %s" % (folder, "OK" if ok else "FAILED"))
    if ok: n += 1
unreal.log("GLTF ANIMS DONE: %d" % n)
