# Fab assets -> the club (ROBOT NEKO ep 9)

jacob's Fab downloads land in `models/VaultCache/FabLibrary/`. Two came into ep 9. Both packs are
GITIGNORED outputs (`packs/`, `props/`), so this file is how they come back after a rebake or a wipe.

## Southbank Undercroft -- `props/southbank.rpack`

A photogrammetry scan at FULL resolution: 1,444,021 verts, 1,822,630 tris, one 8192² atlas.

```bash
python skate_src/glb2rpack.py \
  models/VaultCache/FabLibrary/Southbank_Undercroft_Skatepark-2a5d91a1/glb/converted/southbank_undercroft_skatepark.glb \
  reva_native/props/southbank.rpack --scale 1.75
```

Verified byte-identical to the pack the episode was shot with (7 s). ⛔ The source is NOT metric:
its hall floor-to-ceiling is 1.31 raw units. `--scale 1.75` in the bake and `1.75` again on the
`room` line in `sets/southbank.set` are both in the shot; the scale was judged on pixels against
reva, not derived.

How it is placed (all measured, see the header of `sets/southbank.set`): its height is on local Z
(pitch -90 stands it up), the hall floor is local z 1.72 and level to 0.2 degrees, and it is a
`room` -- never a collider, drawn with its own photographed light.

## Kickflip -- `packs/v1_dude.rpack`

A cartoon skater whose every clip is on his board, and the board is skinned to his skeleton.
jacob: "dont use his animation for reva or kaeli" -- he plays only his own bank.

```bash
F=models/VaultCache/FabLibrary/Kickflip-4347bca6/fbx/kickflip_extracted
OUT=skate_src/fab/_out; mkdir -p $OUT
# the engine discards texels with alpha < 0.4: 20% of Luzer.UV.png, 25% of Skate.png
ffmpeg -i $F/textures/Luzer.UV.png -pix_fmt rgb24 $OUT/Luzer_rgb.png
ffmpeg -i $F/textures/Skate.png    -pix_fmt rgb24 $OUT/Skate_rgb.png
blender --background --factory-startup --python-exit-code 1 --python skate_src/fab/kickflip_export.py -- $OUT
python skate_src/glb2rpack_anim.py --keepnames $OUT/kickflip_tex.glb reva_native/packs/v1_dude.rpack
python skate_src/author/sliceclip.py dude kf_seg8 kf_idle --from 32 --to 43 --pingpong --force
```

Cast him as `partner3=dude@2.6` (a height in club units, not a scale). The engine elects his wheels
as ground contacts and measures that his board runs along his rig's Z, so he rolls to a mark along
his board; his head looks down the board's front (rig -z), so a mark yaw of 180 points his face at
a lens on +z.

## reva / kaeli board moves (authoring on top of a bake)

A rebake of `v1_r2moves` or `v1_anima_outfit` DROPS these; re-run both scores on each pack:

```bash
python skate_src/author/moves/z_board.py      reva_native/packs/v1_<pack>.rpack
python skate_src/author/moves/z_board_ride.py reva_native/packs/v1_<pack>.rpack
```

Each prints the numbers the set's board marks are derived from.

## ep 10 skate moves -- every clip carries its board as a channel (`<clip>_bd`)

The same rule: a rebake DROPS them. Both scripts gate every clip (feet on the deck, wheels on the floor, no capsule
or board clipping) and print the numbers.

```bash
# riding + tricks (ride push pop kf shove manual). It refuses reva_native/packs -- bake a COPY, then copy it in
python skate_src/author/moves/z_sk_ride.py  D:/reva902/robot_neko/ep10/A1/packs/v1_<pack>.rpack
# board handling (carry catchup stepoff dropin bail, and the pickup that still fails). --live allows the live pack;
# --force bakes past failed gates (ep 10 shipped with --force: see the robot-neko ledger for which gates and by how much)
python skate_src/author/moves/z_sk_board.py reva_native/packs/v1_<pack>.rpack --live --force
```

The channel is authored against the flat deck top (0.161 u at size 1.4), not the kick tips, and the engine reads each
rider's authored floor off its own `z_rm_sk_ride_bd` -- so run z_sk_ride.py before any scene uses `@board`.
