"""ue_export_textures.py -- pull Anima's TEXTURES out of the ZAMNRemake UE project as PNG.

Run headless, in the artery's proven shape (see skate_src/guns/bake_guns.sh):

    "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \\
        "C:\\MyVeryOwnVideoGames\\Unreal Projects\\ZAMNRemake\\ZAMNRemake.uproject" \\
        -run=pythonscript \\
        -script="C:\\MyVeryOwnVideoGames\\reva\\skate_src\\anima\\ue_export_textures.py" \\
        -stdout -FullStdOutLogOutput -unattended -nosplash -nullrhi

⛔ THE PATHS ARE HARDCODED AND THE INVOCATION USES BACKSLASHES ON PURPOSE. A first attempt
passed the source and destination as arguments after `-script=` with forward slashes and the
script never ran at all -- no LogPython output whatsoever, no error. bake_guns.sh has the
working form and every deviation from it is a way to get silence.

⭐ WHY THIS EXISTS: Anima's Fab download shipped the FBX and the .max and NO TEXTURE FOLDER --
all 66 of her image datablocks are `packed=False, exists=False` and stay 0x0 even after a
forced reload, so she came into the club on flat per-mesh tints. jacob: "the only other option
for me was to download her to an unreal project, zamnremake, check it out, her texture may be
ther". They are: 201 texture assets, and all 29 diffuse maps -- T_Body_Diffuse, T_Head_Diffuse,
T_Arm_Diffuse, T_Leg_Diffuse, T_Hair1/2/3_Diffuse, T_Bra_Diffuse, T_Boot_Diffuse,
T_Cloth_Diffuse -- named after exactly the materials her FBX carries.

⛔ AND THIS EXPORTS TEXTURES, NOT THE MESH, DELIBERATELY. The artery already established that
UE MATERIALS DO NOT SURVIVE A GLTF EXPORT -- 0 images in the GLBs, measured on the weapon
batch. So the geometry comes from the FBX we already have and the COLOUR comes from here, and
Blender marries them by material name.
"""
import os

import unreal

SRC = "/Game/Anima/Characters/Textures"
OUT = r"C:\MyVeryOwnVideoGames\reva\models\_out\anima_tex"


def wanted(name):
    """Colour and cut-out only. The set also carries ORM, MicroN, SSSMap, RGBAMask,
    Tessellation and Normal maps -- 201 assets against 29 that are colour -- and exporting
    the lot would hand the Blender matcher a dozen wrong candidates per material."""
    low = name.lower()
    return ("diffuse" in low or "basecolor" in low or "opacity" in low)


def main():
    os.makedirs(OUT, exist_ok=True)
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = ar.get_assets_by_path(SRC, recursive=True)
    unreal.log("ANIMATEX: scanning %s -- %d asset(s)" % (SRC, len(assets)))

    ok = skip = fail = 0
    for a in assets:
        name = str(a.asset_name)
        if not wanted(name):
            skip += 1
            continue
        path = "%s.%s" % (str(a.package_name), name)
        obj = unreal.EditorAssetLibrary.load_asset(path)
        if obj is None:
            unreal.log_warning("ANIMATEX: could not load %s" % path)
            fail += 1
            continue
        task = unreal.AssetExportTask()
        task.set_editor_property("object", obj)
        task.set_editor_property("filename", os.path.join(OUT, name + ".png"))
        task.set_editor_property("automated", True)
        task.set_editor_property("prompt", False)
        task.set_editor_property("replace_identical", True)
        task.set_editor_property("exporter", unreal.TextureExporterPNG())
        if unreal.Exporter.run_asset_export_task(task):
            ok += 1
            unreal.log("ANIMATEX: %s -> OK" % name)
        else:
            fail += 1
            unreal.log_warning("ANIMATEX: %s -> FAILED" % name)

    unreal.log("ANIMATEX DONE: %d exported, %d failed, %d skipped (non-colour) -> %s"
               % (ok, fail, skip, OUT))


main()
