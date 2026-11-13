import unreal, os
OUT = "out/kaeli/mesh"   # machine-independent paths
os.makedirs(OUT, exist_ok=True)
p = "/Game/TopDown/Models/ZAMN_Cleo/Animation_Walking_withSkin/SkeletalMeshes/Animation_Walking_withSkin"
asset = unreal.EditorAssetLibrary.load_asset(p)
opts = unreal.GLTFExportOptions()
opts.export_vertex_skin_weights = True
opts.export_material_variants = unreal.GLTFMaterialVariantMode.NONE
msgs = unreal.GLTFExporter.export_to_gltf(asset, os.path.join(OUT, "cleo_mesh.glb"), opts, set())
unreal.log("GLTF MESH EXPORT: %s" % ("OK" if msgs else "FAILED"))
