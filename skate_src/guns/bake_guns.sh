#!/usr/bin/env bash
# bake_guns.sh -- the whole weapon artery, end to end. Run from the repo root.
#
#   bash skate_src/guns/bake_guns.sh
#
# 1. UE 5.8 headless exports AssetsvilleTown's weapon rack to GLB (16 meshes, ~40s)
# 2. glb2rpack.py bakes each into a prop rpack the engine loads at boot
#
# rpacks are gitignored (repo convention: recipes in, artifacts out), so this script IS
# the record. The meshes are real-world metres and the club runs 3.15 units per 1.8m
# body, so --scale 1.75 lands them at true size; --center puts the origin in the hand.
#
# The staging project (GunExport/) is a copy of the kaeli shim with the weapon uassets
# dropped into its Content -- see ue_export_guns.py. Materials do NOT survive the
# export (0 images in the GLBs); the engine tints each weapon instead.
set -e

UE="/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
PY="${CLUB_PYTHON:-python}"
GUNS="skate_src/guns"

if [ ! -d "$GUNS/export" ] || [ "$1" = "--reexport" ]; then
    echo "== UE export =="
    "$UE" "C:\\MyVeryOwnVideoGames\\reva\\skate_src\\guns\\GunExport\\GunExport.uproject" \
        -run=pythonscript -script="C:\\MyVeryOwnVideoGames\\reva\\skate_src\\guns\\ue_export_guns.py" \
        -stdout -FullStdOutLogOutput -unattended -nosplash -nullrhi > "$GUNS/ue_guns.log" 2>&1
    grep -c "GUNS: .* -> OK" "$GUNS/ue_guns.log"
fi

echo "== bake =="
bake(){ "$PY" skate_src/glb2rpack.py "$GUNS/export/$1.glb" "reva_native/props/v1_wpn_$2.rpack" \
        --scale 1.75 --center >/dev/null && echo "  $2"; }

# MELEE set (7) -- the birthright, and the bike door's Road Rash arsenal
bake SM_BaseballBat_01   baseballbat
bake SM_PoliceBat_01     policebat
bake SM_Katana_01        katana
bake SM_Golf_Club_01     golfclub
bake SM_ChoppingKnife_01 knife
bake SM_Shovel_01        shovel
bake SM_Chainsaw_01      chainsaw
# PISTOL (2) / SPREAD (2) / BEAM (4)
bake SM_Pistol_01        pistol
bake SM_Pistol_02        pistol2
bake SM_Shotgun_01       shotgun
bake SM_Rifle_01         rifle
bake SM_Smg_01           smg
bake SM_Rifle_02         rifle2
bake SM_Rifle_03         rifle3
bake SM_MachineGun_01    machinegun
# parked: thrown, not carried
bake SM_Granade_01       grenade

echo "== done: $(ls reva_native/props/v1_wpn_*.rpack | wc -l) weapons =="
