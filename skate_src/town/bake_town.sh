#!/usr/bin/env bash
# bake_town.sh -- AssetsvilleTown -> prop rpacks, for SEVENTY-FOUR.
#
#   bash skate_src/town/bake_town.sh [--reexport]
#
# 1. UE 5.8 headless exports 112 meshes + the palette PNGs (ue_export_town.py, ~2 min)
# 2. glb2rpack.py bakes each into a prop rpack the set renderer loads on demand
#
# SCALE. The meshes come out in real-world METRES -- measured off the GLBs: a mailbox is
# 1.42 tall, a wall module 3.00 x 4.00, a sedan 5.40 long, a road tile 16 x 16. The club
# runs 3.15 units per 1.8m body, so --scale 1.75 lands the whole town at true size beside
# reva. No --center and no --ground: these are kit pieces and their authored origins are
# the snapping points.
#
# ⭐ THE PALETTE. The gun run found that UE materials do not survive the GLTF export, and
# every weapon ended up wearing a flat engine tint. A tinted TOWN would be a grey town --
# but this kit is built the low-poly way, with every mesh UV'd into one shared 2048 colour
# palette. Exporting that single image and passing it to --tex colours all 112 meshes
# correctly. The road tiles are the exception: they UV into their own T_roadSet sheet.
#
# ⛔ AND THE ALPHA IS NOT OPACITY. The club's mesh shader alpha-tests
# (`if(useTex>0.5 && tx.a<0.4) discard`), and this kit uses the palette's alpha
# channel as a MASK for something else entirely -- 59% of its 2048x2048 pixels sit
# below that threshold. Handed over as exported, the palette does not tint the town,
# it DELETES it: the set loaded 18 instances, drawSet ran with valid uniforms and
# correct matrices, every pack reported drawable, and the frame came back empty.
# Flatten to RGB before baking.
#
# ⛔ AND DO NOT FLIP IT. An earlier version of this script transposed every sheet
# vertically, on the reasoning that glTF's UV origin is top-left and GL's is
# bottom-left. That reasoning was wrong for this engine -- crowd_kms.c uploads the
# image top-row-first and samples it the glTF way, so the flip put every palette-UV'd
# mesh on the wrong swatch. PROVEN ON PIXELS, the same mailbox baked both ways and
# shot one at a time: flipped it is MAGENTA, unflipped it is a navy USPS box with
# white bolts and a white envelope decal. Mailboxes, pickups and window frames were
# all wearing the wrong half of the palette.
#
# The measurement that originally argued for the flip (mean luminance 8.6 "as stored"
# vs 124.8 "flipped" over SM_mailbox_1's UVs) was real -- but it was measured with a
# probe whose own sampling convention already differed from the engine's, so it
# agreed with itself. The thing that actually rescued the town in that same change
# was the ALPHA FLATTEN below; the flip rode along as cargo for four days.
#
# rpacks are gitignored (recipes in, artifacts out) -- this script IS the record.
set -e

UE="/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
PY="${CLUB_PYTHON:-python}"
T="skate_src/town"
# ⛔ FLATTEN THE ALPHA FIRST. see the note below.
PAL="$T/export/T_colorPalette2048_opaque.png"
ROAD="$T/export/T_roadSet_opaque.png"
"$PY" -c "import glob,os;from PIL import Image;[Image.open(f).convert('RGB').save(f[:-4]+'_opaque.png') for f in glob.glob(r'$T/export/*.png') if not f.endswith('_opaque.png')]"

if [ ! -d "$T/export" ] || [ "$1" = "--reexport" ]; then
    echo "== UE export =="
    "$UE" "C:\\MyVeryOwnVideoGames\\reva\\skate_src\\guns\\GunExport\\GunExport.uproject" \
        -run=pythonscript -script="C:\\MyVeryOwnVideoGames\\reva\\skate_src\\town\\ue_export_town.py" \
        -stdout -FullStdOutLogOutput -unattended -nosplash -nullrhi > "$T/ue_town.log" 2>&1
    grep -c "TOWN: .* -> OK" "$T/ue_town.log"
fi

mkdir -p reva_native/props
n=0; fail=0
for g in "$T"/export/*.glb; do
    b=$(basename "$g" .glb)
    # v1_town_<lowercased, SM_/SK_ stripped>
    out=$(echo "$b" | sed -e 's/^SM_//' -e 's/^SK_//' | tr 'A-Z' 'a-z')
    # the road set UVs into its own sheet; everything else into the shared palette
    # ONE PALETTE IS NOT ENOUGH: the kit palette-UVs its PROPS, but walls UV into tiled
    # siding sheets and foliage into leaf sheets. Give a wall the palette and the house
    # wears the swatch grid; give a tree the palette and it wears confetti.
    # ⭐ THE SPECIFIC CASES COME FIRST, and they come from ue_texmap.py rather than from
    # the shape of a filename. skate_src/town/texaudit.py replays this case statement
    # against what UE actually reports and prints every disagreement; these are the ones
    # where UE named a SINGLE sheet and the pattern rule was simply wrong:
    #   SM_floor_01     uses T_planks_01 ONLY -- it is floorboards, and it was wearing
    #                   the colour palette, which is why the Act V floor was a flat tint
    #   SM_road_sign_*  and SM_road_cone are PALETTE props that the "SM_road_*" pattern
    #                   was handing the tarmac sheet
    #   the trees each have their own leaf sheet; tree_01 is not tree_02
    case "$b" in
        SM_floor_01)            tex="$T/export/T_planks_01_opaque.png" ;;
        SM_road_sign_*|SM_road_cone) tex="$PAL" ;;
        SM_rock_01)             tex="$ROAD" ;;
        SM_chimney_*)           tex="$T/export/T_brickSmall_01_white_opaque.png" ;;
        SM_tree_01)             tex="$T/export/T_leaf_01a_opaque.png" ;;
        SM_tree_03|SM_tree_04)  tex="$T/export/T_leaf_02_opaque.png" ;;
        SM_treeLowPoly_01)      tex="$PAL" ;;
        SM_road_*|SM_sidewalk_*|SM_parcel_*|SM_dirt_*|SM_grass_00) tex="$ROAD" ;;
        # ⛔ A MESH WHOSE UVs SPAN 0..1 IS NOT A PALETTE PROP. Measured: SM_doorHouse_01
        # sits at u 0.155..0.790 (a swatch), while SM_wall_01 and every SM_window_*
        # run the full 0..1 -- they TILE a dedicated sheet. Handed the palette, a window
        # wears the whole 2048 swatch grid, which is the dark chequered rectangle that
        # was on every house in the first street wide.
        SM_wall_*|SM_wallDoor_*|SM_wallGarageDoor_*|SM_wallAttic_*|SM_window_*)
            tex="$T/export/T_panels_01_white_opaque.png" ;;
        SM_tree_*|SM_treeLowPoly_*|SM_bush_*|SM_cornPlant)
            tex="$T/export/T_leaf_01_opaque.png" ;;
        SM_grassVerticalSingle) tex="$T/export/T_grassAlpha_opaque.png" ;;
        SK_veh_PoliceCar*)      tex="$T/export/T_PoliceCar_01_opaque.png" ;;
        *) tex="$PAL" ;;
    esac
    if "$PY" skate_src/glb2rpack.py "$g" "reva_native/props/v1_town_$out.rpack" \
            --scale 1.75 --tex "$tex" >/dev/null 2>&1; then
        n=$((n+1))
    else
        echo "  FAILED $b"; fail=$((fail+1))
    fi
done
# SIDING VARIANTS. One rpack carries one texture, so a street of different-coloured
# houses needs the wall module baked once per colour.
for v in brown:T_panels_01_brown grey:T_panels_01_grey red:T_brickSmall_01_red          yellow:T_brickSmall_01_yellow; do
    nm="${v%%:*}"; tx="${v##*:}"
    for w in SM_wall_01 SM_wallDoor_01 SM_wallGarageDoor_01; do
        out=$(echo "$w" | sed 's/^SM_//' | tr 'A-Z' 'a-z')
        "$PY" skate_src/glb2rpack.py "$T/export/$w.glb"             "reva_native/props/v1_town_${out}_$nm.rpack"             --scale 1.75 --tex "$T/export/${tx}_opaque.png" >/dev/null 2>&1 && n=$((n+1))
    done
done
echo "baked $n prop rpacks ($fail failed)"
