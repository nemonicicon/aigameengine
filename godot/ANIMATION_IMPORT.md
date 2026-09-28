# OUR MOVES IN GODOT 4 — the animation library and how to use it

Two files in `godot/animations/`, exported from the club engine by `skate_src/author/rpack2gltf.py`:

| file | what |
|---|---|
| `reva_skate_moves.glb` | 14 skate moves, each with the **skateboard's own motion** (the board flips, pops and is caught — it is animated per trick, not attached) |
| `reva_moves.glb` | 18 other authored moves (board handling, arms up, a crawl, lying down, …) |

⭐ **And a character ready to use: `godot/characters/clanker/clanker.glb`** — CLANKER, the studio's own robot
(made entirely by code), with the same 14 skate moves already baked onto HIS skeleton and his real body and
texture. No retargeting: instance him and play `z_rm_sk_ride`. See `godot/characters/clanker/README.md`.

**Skeleton:** reva's — Mixamo-style names (`Hips Spine Spine1 Spine2 Neck Head LeftShoulder LeftArm
LeftForeArm LeftHand LeftUpLeg LeftLeg LeftFoot LeftToeBase`, the same on the right, and `face_jaw`), 23
bones, no fingers, **metres, Y up, facing +Z**, rest pose standing. Rigged in-house; the moves were
authored by our own tools (`skate_src/author/moves/`). **No mesh from any asset**: each file carries a
generated STICK FIGURE (left side blue, right side red) so a move can be looked at before retargeting —
hide or delete it.

**Verified:** Blender's glTF importer re-read both files and every joint of every animation, at eight
sampled frames each, landed within **5 micrometres** of the source (`godot/verify_export_blender.py`).
Every animation keys EVERY bone (rest pose held where the move is silent), so a move never inherits the
last move's pose — the check found exactly that on the first export (an arm left in the air, 2.04 m out).

**In Godot 4.7.2** (`demo/check_pose.gd` + `demo/check_pose_compare.py`): the files import with no error;
posed at fixed frames, EVERY joint and the board land within **3 micrometres** of the club engine and the
board's nose axis within 0.02 degrees — reva's skate and body moves and Clanker's alike. Switching from one
move to another never leaves a bone where the last move put it. The demo project's `.import` files carry
the loop modes and the optimizer setting below.

⛔ **Godot's animation OPTIMIZER is on by default, and it is an option of the `AnimationPlayer` NODE, not of
each animation.** On, it drops keys it judges redundant (Clanker's ride kept 40 of its 62 hip keys) and the
pose drifts 1–3 mm off the authored one. The first version of this kit set `optimizer/enabled` per
animation — Godot carries that key along and never reads it — so the optimizer stayed on, and the 2.7 mm it
left on the kickflip was written up here as "not the optimizer, cause not isolated": toggling a dead
setting cannot move anything. `demo/set_import.py` now writes it where Godot reads it
(`"nodes": {"PATH:AnimationPlayer": {"optimizer/enabled": false}}`).

## Import (Godot 4)

1. Put the `.glb` in the project. Godot imports it as a scene: a `Skeleton3D` (+ the stick figure, and the
   `skateboard` node in the skate file) and an `AnimationPlayer` holding the moves. In its *Advanced Import
   Settings*, select the **`AnimationPlayer`** node and turn the **Optimizer** off (see ⛔ above) — or copy
   our `.import` file beside the `.glb`.
2. **Retarget onto your character** (Godot's own humanoid retargeting): open the `.glb`'s *Advanced Import
   Settings*, select the `Skeleton3D`, and under *Retarget* create a `BoneMap` with the
   `SkeletonProfileHumanoid` profile — Mixamo-style names map automatically. Do the same on your
   character's model. Both then share the profile's bone names, and the moves play on your character from
   its `AnimationPlayer` (save the animations to files from the Animation tab and add them as a library).
   Godot's *Rest Fixer* options (e.g. *Overwrite Axis*) help when the two rigs' rest orientations differ.
3. **Loop modes** (Animation tab, per animation): set **Linear** loop on the LOOP rows below. A file's
   animation `extras.seamless` says only that its first and last frames match (every trick does: it starts
   and ends in the riding stance) — the table says how each is MEANT to be played.
4. **Root motion:** the `z_rm_*` moves own their root — travel is on `Hips` translation (the club's
   convention: `z_rm_` = root motion). Point `AnimationMixer.root_motion_track` at the bone that carries it
   after retargeting, or keep the travel in the animation for a first look.
5. **The board** (`reva_skate_moves.glb`): the `skateboard` node is a sibling of the skeleton, in the
   character's own space. Its frame: origin = **the centre of the deck's top**, **+X = the nose** (the lead
   foot's end), **+Y = the deck's up**. Parent your board model under it, scaled so its deck top sits at the
   origin and its nose points along +X, and delete the generated deck. Keep it next to the character when
   you retarget — it is animated in the rider's space, so it rides wherever the character root goes.

## The moves

**Skate** (`reva_skate_moves.glb`, all 30 fps, all with the board):

| move | play | what |
|---|---|---|
| `z_rm_sk_ride` | LOOP 2.0 s | the riding stance, rolling (bent knees, weight over the trucks — judged from behind) |
| `z_rm_sk_push` | once, over the ride | a push: back foot down to the ground and back onto the tail |
| `z_rm_sk_pop` | once | an ollie: pop the tail, the board rises to the feet, land |
| `z_rm_sk_kf` | once | a kickflip: pop, the front foot flicks, the board rolls under her, caught, land |
| `z_rm_sk_shove` | once | a shove-it: the board spins under her feet |
| `z_rm_sk_manual` | once | a manual: balanced on the back wheels, then down |
| `z_rm_sk_grab` | once | an INDY: knees tucked, the back hand down between the feet onto the toe rail, hold, land |
| `z_rm_sk_method` | once | a METHOD: the front hand on the heel rail, knees bent back, the board up behind her |
| `z_rm_sk_film` | LOOP 2.0 s | riding with the camera arm out — the filmer (pair with `godot/club_camera.gd`'s hand-held rig) |
| `z_rm_sk_carry` | LOOP 2.0 s | walking with the board: right hand on the back rail by the front truck, nose up |
| `z_rm_sk_catchup` | once 1.2 s | the skater's pickup: step on the tail, the nose pops into the hand |
| `z_rm_sk_dropin` | once 1.4 s | carry → step back, set the board down across, front foot, back foot |
| `z_rm_sk_stepoff` | once 1.0 s | ride stance → back foot off first, stand behind the board |
| `z_rm_sk_bail` | once 1.8 s | the board shoots out toward the nose and she sits down hard (for a real fall, a ragdoll is better — see `design/engine_capabilities.md` §22) |

**Other** (`reva_moves.glb`; no board node — for the `z_board_*` moves attach a board to the hand bone):

| move | play | what |
|---|---|---|
| `z_board_reach` `z_board_lift` `z_board_lower` `z_board_rise` | once | bending to a board on the floor, lifting it, putting it down, standing back up |
| `z_board_carry` | LOOP | standing, holding a board |
| `z_rm_board_stepon` | once | front foot up onto a board, then the back foot — hold on the deck |
| `z_board_wobble` `z_board_steady` | LOOP | standing on a board badly, then less badly |
| `z_board_push` | LOOP | pushing off on a board (the first lesson's push) |
| `z_board_bail` | once | falling off a board |
| `z_armsup` | once | both arms up over the head and held, hands waving |
| `z_upright` | LOOP | a plain standing pose |
| `z_rm_crawlcat` (60 fps) | once | crawling on all fours like a cat (root motion) |
| `z_rm_skiphand` (60 fps) | LOOP | skipping along (authored hand in hand with a partner) |
| `z_rm_cuddleseat` `z_rm_curlsleep` `z_rm_bed_back` `z_rm_bed_alone` | once / LOOP | sitting close, curling up asleep, lying on her back (authored for a two-person scene) |

## Make more of them

`skate_src/author/moves/*.py` are the SCORES these came from — keys on a real skeleton, eased with
anticipation and settle, with DECLARED contacts (a foot on the deck, a hand on a rail) that a checker tests
(`ANIMATION.md` §3). They write into the club's pack format; `rpack2gltf.py` brings the result to glTF.
The method (a move is a score + its declared contacts + a check that could fail) works in any engine.
