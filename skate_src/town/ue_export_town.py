"""ue_export_town.py -- UE headless: AssetsvilleTown -> GLB, for SEVENTY-FOUR.

    UnrealEditor-Cmd.exe GunExport.uproject -run=pythonscript -script=ue_export_town.py
        -stdout -FullStdOutLogOutput -unattended -nosplash -nullrhi

Same artery as skate_src/guns/ue_export_guns.py (which proved the shim-project pattern
on 16 weapons in ~40s), pointed at the rest of the town: the ground the street is made
of, the tile kit the houses are built from, the yard and street dressing, the trees, the
cars, and enough interior for the night the windows get boarded.

⭐ AND THE PALETTE. The gun run learned that UE materials do not survive the GLTF export
-- 0 images in the GLBs -- and every weapon ended up wearing an engine tint. That would
have been fatal here, because a tinted town is a grey town. But this kit is built the
low-poly way: every mesh in it UVs into ONE shared 2048 colour palette
(T_colorPalette2048), so exporting that single texture and handing it to glb2rpack.py
--tex colours the entire set correctly. One image, 840 meshes.
"""
import unreal, os

OUT = r"C:\MyVeryOwnVideoGames\reva\skate_src\town\export"
os.makedirs(OUT, exist_ok=True)
ROOT = "/Game/AssetsvilleTown/Meshes/"

# ---- the set list. Grouped the way the film uses them. ----------------------------
GROUND = ["SM_road_00", "SM_road_01", "SM_road_02", "SM_road_03", "SM_road_04",
          "SM_sidewalk_01", "SM_sidewalk_01_corner", "SM_sidewalk_01_driveIn",
          "SM_grass_00", "SM_dirt_00", "SM_parcel_00", "SM_parcel_01"]

# the house KIT -- suburbia is not four house meshes, it is one wall repeated
HOUSE = ["SM_wall_01", "SM_wall_01_half", "SM_wall_02", "SM_wall_02_half",
         "SM_wallDoor_01", "SM_wallDoor_02", "SM_wallGarageDoor_01",
         "SM_wallAttic_01", "SM_wallAttic_01_corner", "SM_wallAttic_01_half",
         "SM_roof_01", "SM_roof_01_corner", "SM_roof_01_side",
         "SM_roof_02", "SM_roof_02_end",
         "SM_doorHouse_01", "SM_doorHouse_02", "SM_doorFrame_01",
         "SM_window_01", "SM_window_03", "SM_window_05", "SM_window_06",
         "SM_floor_01", "SM_pillar_01", "SM_drainPipe", "SM_cornice_01"]

YARD = ["SM_mailbox_1", "SM_mailbox_2", "SM_grill", "SM_hose_01", "SM_hydrant",
        "SM_shed_01", "SM_bench", "SM_Bicycle_01", "SM_Bicycle_02",
        "SM_trash_bin_1", "SM_trash_bag", "SM_grassCutter", "SM_wheelbarrow",
        "SM_ladder_01", "SM_newspaperSingle", "SM_newspaperStack", "SM_Flagpole",
        "SM_chimney_01", "SM_chimney_02", "SM_airCondition_01", "SM_box",
        "SM_barrel_1", "SM_crate_1", "SM_carton_box", "SM_woodenShack_01",
        "SM_fence_101", "SM_fence_101end", "SM_fence_101gate",
        "SM_fence_102a", "SM_fence_102end", "SM_fence_102gate",
        "SM_fence_104a", "SM_fence_104end", "SM_fence_104gate"]

STREET = ["SM_utility_pole_1", "SM_utility_pole_5", "SM_utility_pole_5_line1",
          "SM_powerCables_1", "SM_lantern", "SM_public_phone", "SM_parking_meter",
          "SM_road_sign_1", "SM_road_sign_2", "SM_speed_limit_25",
          "SM_traffic_lights_1", "SM_sewerCover", "SM_road_cone",
          "SM_wreck_01", "SM_wreck_02"]

NATURE = ["SM_tree_01", "SM_tree_02", "SM_tree_03", "SM_tree_04",
          "SM_treeLowPoly_01", "SM_bush_01", "SM_grassVerticalSingle",
          "SM_rock_01", "SM_background_Mountains_01"]

# skeletal, not static -- they carry a rig for doors and wheels. Exported anyway: the
# film needs them parked, and a parked car is a rest pose.
VEHICLES = ["SK_veh_Sedan_01", "SK_veh_Pickup_01", "SK_veh_SUV_01",
            "SK_veh_Van_01", "SK_veh_PoliceCarSedan_01", "SK_veh_Muscle_01"]

INTERIOR = ["SM_Armchair_01", "SM_Bed_01", "SM_Bookcase_02", "SM_Lamp_01",
            "SM_Lamp_03", "SM_Carpet_01", "SM_Chair_01", "SM_Cupboard_01",
            "SM_Mirror_01", "SM_Basket_01"]

SETS = [("GroundTilset", GROUND), ("BuildingTilset", HOUSE), ("StreetProps", YARD),
        ("StreetProps", STREET), ("Nature", NATURE), ("Vehicles", VEHICLES),
        ("InteriorProps/House", INTERIOR)]

ok, bad, missing = 0, 0, []
for folder, names in SETS:
    for n in names:
        p = ROOT + folder + "/" + n
        asset = unreal.EditorAssetLibrary.load_asset(p)
        if not asset:
            missing.append(n)
            bad += 1
            continue
        opts = unreal.GLTFExportOptions()
        opts.export_material_variants = unreal.GLTFMaterialVariantMode.NONE
        msgs = unreal.GLTFExporter.export_to_gltf(asset, os.path.join(OUT, n + ".glb"), opts, set())
        unreal.log("TOWN: %s -> %s" % (n, "OK" if msgs else "FAILED"))
        ok += 1 if msgs else 0
        bad += 0 if msgs else 1

# ---- THE PALETTE. One image for the whole kit. ------------------------------------
# ONE PALETTE IS NOT ENOUGH. The kit is palette-UV'd for props -- but the WALLS UV into
# tiled brick and panel sheets, and the FOLIAGE into leaf sheets, so handing those meshes
# the colour palette makes a house wear the swatch grid and a tree wear confetti.
TEX = ["Base/T_colorPalette2048", "Base/T_roadSet",
       "Tiled/T_brickSmall_01_white", "Tiled/T_brickSmall_01_red",
       "Tiled/T_brickSmall_01_yellow", "Tiled/T_brickLarge_01_white",
       "Tiled/T_panels_01_white", "Tiled/T_panels_01_grey", "Tiled/T_panels_01_brown",
       "Foliage/T_leaf_01", "Foliage/T_leaf_02", "Foliage/T_leaf_03",
       "Foliage/T_grassAlpha", "T_fenceMetal_01", "T_PoliceCar_01"]
for tex in ["/Game/AssetsvilleTown/Textures/" + t for t in TEX]:
    t = unreal.EditorAssetLibrary.load_asset(tex)
    if not t:
        unreal.log_warning("TOWN: no texture %s" % tex)
        continue
    task = unreal.AssetExportTask()
    task.set_editor_property("object", t)
    task.set_editor_property("filename", os.path.join(OUT, tex.rsplit("/", 1)[-1] + ".png"))
    task.set_editor_property("automated", True)
    task.set_editor_property("prompt", False)
    task.set_editor_property("exporter", unreal.TextureExporterPNG())
    r = unreal.Exporter.run_asset_export_task(task)
    unreal.log("TOWN: texture %s -> %s" % (tex, "OK" if r else "FAILED"))

unreal.log("TOWN DONE: %d exported, %d failed" % (ok, bad))
if missing:
    unreal.log_warning("TOWN MISSING: " + ", ".join(missing))
