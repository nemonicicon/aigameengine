"""ue_export_guns.py -- UE headless: AssetsvilleTown's weapon rack -> GLB props.

    UnrealEditor-Cmd.exe GunExport.uproject -run=pythonscript -script=ue_export_guns.py
        -stdout -FullStdOutLogOutput -unattended -nosplash -nullrhi

Same recipe as skate_src/kaeli/ue_export_gltf.py (UE 5.8 + GLTFExporter), pointed at
the static-mesh weapons copied out of CivicDefenseV2. Static meshes, so no skin weights:
each one lands as a rigless GLB that glb2rpack.py bakes into a prop rpack, which the
engine draws with drawProp() at whatever matrix the hand bone gives it.
"""
import unreal, os

OUT = r"C:\MyVeryOwnVideoGames\reva\skate_src\guns\export"
os.makedirs(OUT, exist_ok=True)

NAMES = [
    "SM_Pistol_01", "SM_Pistol_02", "SM_Rifle_01", "SM_Rifle_02", "SM_Rifle_03",
    "SM_Shotgun_01", "SM_Smg_01", "SM_MachineGun_01",
    "SM_BaseballBat_01", "SM_PoliceBat_01", "SM_Katana_01", "SM_Chainsaw_01",
    "SM_Golf_Club_01", "SM_ChoppingKnife_01", "SM_Shovel_01", "SM_Granade_01",
]

ok, bad = 0, 0
for n in NAMES:
    p = "/Game/AssetsvilleTown/Meshes/Weapons/" + n
    asset = unreal.EditorAssetLibrary.load_asset(p)
    if not asset:
        unreal.log_warning("GUNS: missing %s" % p)
        bad += 1
        continue
    opts = unreal.GLTFExportOptions()
    opts.export_material_variants = unreal.GLTFMaterialVariantMode.NONE
    msgs = unreal.GLTFExporter.export_to_gltf(asset, os.path.join(OUT, n + ".glb"), opts, set())
    unreal.log("GUNS: %s -> %s" % (n, "OK" if msgs else "FAILED"))
    ok += 1 if msgs else 0
    bad += 0 if msgs else 1

unreal.log("GUNS DONE: %d exported, %d failed" % (ok, bad))
