import unreal, os
OUT = r"C:\MyVeryOwnVideoGames\reva\skate_src\kaeli\export\mesh"
os.makedirs(OUT, exist_ok=True)
p = "/Game/TopDown/Models/ZAMN_Cleo/Animation_Walking_withSkin/SkeletalMeshes/Animation_Walking_withSkin"
asset = unreal.EditorAssetLibrary.load_asset(p)
opt = unreal.FbxExportOption()
opt.ascii = False
opt.export_morph_targets = False
opt.export_preview_mesh = False
opt.level_of_detail = False
task = unreal.AssetExportTask()
task.object = asset; task.filename = os.path.join(OUT, "cleo_mesh.fbx")
task.options = opt; task.automated = True; task.replace_identical = True; task.prompt = False
ok = unreal.Exporter.run_asset_export_task(task)
unreal.log("MESH EXPORT: %s" % ("OK" if ok else "FAILED"))
