# ANIMATION — the toolchain, from a rigged model to a checked move

Everything here is Python (numpy + Pillow) unless it says it drives the engine. Engine packs are the
club's own `.rpack` format (`skate_src/author/rpack.py` reads and append-writes it; the layout is in its
docstring). ⛔ There is **no `.rpack` → glTF/FBX exporter yet**: clips authored here play in this engine;
the portable form of a move is its SCORE (`skate_src/author/moves/*.py`), which is math on a skeleton.

## 1 · Skeletons: any humanoid, by bone name

The engine maps every rig onto a **46-slot canonical rig** at load (`canonOf`, crowd_kms.c ~369):
16 body bones (hips, 3 spine, neck, head, and per side shoulder / upper arm / forearm / thigh / shin) +
30 finger joints (5 fingers × 3 joints × 2 sides); hands and feet are found through the hierarchy.
Naming families it knows: **Mixamo** (with or without `mixamorig:`), **Unreal mannequin** (`pelvis`,
`spine_01`, `clavicle_l`, `upperarm_l`, `thigh_l`, `calf_l`, UE-style `index_01_l` fingers), **Character
Creator** (`CC_Base_*`), **Daz Genesis 9** (side on the left: `l_thigh`), **Rigify** (`shoulder.L`).
Twist bones, face bones, hair chains and accessories ride along uncanonised. A clip in ANY loaded pack
can be played by ANY body; it is retargeted through the canon (`REVA_NOFINGERS=1` drops the fingers).

## 2 · Getting bodies and clips in

| tool | does |
|---|---|
| `skate_src/glb2rpack_anim.py in.glb out.rpack --keepnames` | a rigged, animated GLB → a pack (skin, clips, textures, morphs with `--morphs`) |
| `skate_src/glb2rpack.py` | a static prop GLB → a pack |
| `skate_src/bvh2rpack.py` | BVH mocap → clips |
| `skate_src/kaeli/ue_export_gltf.py`, `ue_export_anim_gltf.py`, `skate_src/anima/` | HEADLESS Unreal Engine → glTF: any UE project's characters, weapons, props and animations (a shim project when the source project has no Python plugin) |
| `skate_src/reva2/rig_reva.py` | rig an UNRIGGED character mesh (a Meshy/AI-generated body): joints from the mesh's own cross-sections, weights, a preview (`REVA_PREVIEW=<dir>`) — read its comments on where the first recipe hung the arm joints |
| `skate_src/statue2body.py` | an unrigged statue → a body that plays the move library |
| `skate_src/author/retarget.py`, `autoweights.py` | retarget a clip between skeletons; weight a mesh to a skeleton |
| `skate_src/author/glb_morphs.py <glb> <pack> --rename "jawopen=jaw,…"` | inject ARKit-style blendshapes, renamed to the engine's face vocabulary (`jaw smile brow blink`) — ⛔ without `--rename` 92 morphs load and NONE is reachable |

## 3 · Authoring moves in code

`skate_src/author/authorlib.py` is the library: quaternions, FK (`fk_world`), `aim_bone`,
`rotate_bone_world`, two-bone IK with a pole (`solve_limb`), `translate_node_world`,
`hold_world_rotation`, and the timing that makes a move read — `ease_smooth`, `ease_settle` (overshoot
and come to rest), `ease_anticipate` (dip against the motion first), keyed `profile()`s.

A **score** (`moves/*.py`) declares a move as keys on a real skeleton, bakes it into the pack
(`rpack.append_clips`, append-only), and DECLARES what it must satisfy (`authorlib.declare` →
`<pack>.decl.json`: contacts, reach, floor, balance) — which `anim_bench/movecheck.py` then tests.
Worked examples: `z_sk_ride.py` / `z_sk_board.py` (a skateboard ride with the board as a channel of the
rider's own clip: pop, kickflip, shove-it, manual, drop-in, bail, the catch-up pickup), `z_board*.py`,
`z_hug.py`, `z_kiss.py`, `z_hi5.py`, `z_rm_cartwheel.py` (root motion: `z_rm_*` owns its root),
`z_bow.py`, `z_armsup.py`, `z_breathe.py`, `z_tiptoe.py`. `bake_moves.sh` bakes them all.
Also: `sliceclip.py` (cut a real LOOP out of a transition), `restclip.py`, `fingers.py`, `unbake.py`.

⛔ **A clip's name is not evidence**: a clip called `idle` was a 184-frame get-up and three elections
picked it by the word. Measure first-vs-last frame; the engine now refuses a non-looping idle.
⛔ In a scene, a body's own clip whose largest body-bone gap first-to-last exceeds 20° plays ONCE and
holds (a fall must not stand back up every four seconds).

## 4 · Faces

`face_marks.py` (landmark nodes measured from the render, not guessed), `face_calibrate.py`,
`calib2marks.py`, `jaw.py` (a jaw bone), `mouth_interior.py`, `mouth_dark.py`, `morphs.py`,
`face_morphs.py`. The engine: a hinge jaw on loudness (`mouth hinge`), visemes, brows, blinks, gaze
(`gaze=`), eyes that aim on a morph rig. Manual: `design/faces.md`.

## 5 · Clothing

`skate_src/author/dresspack.py` — a PREMADE garment (a Sketchfab / Marvelous Designer GLB) onto a
FINISHED pack: fitted by landmarks, pushed out of the skin (tops radially, then draped off the bust;
trousers cinched at the waist, tucked into boots), skinned with the weights of the SKIN under it, the
skin it covers REMOVED so nothing can poke through in any pose, thumbnail caps dropped. The engine draws
it as `~worn` (never measured as the body). Its comments record six fitting failures and their cures.
Also `dressglb.py` (the GLB-level fit), `fitcheck.py`, and the simulated cloth solver (`design/cloth.md`).

## 6 · Checks — the part that makes it work

Data only (no engine): `anim_bench/posturecheck.py` (is it STANDING or merely still), `motioncheck.py`,
`posecheck.py`, `movecheck.py` (the score's own declarations), `weightcheck.py`, `filmstrip.py`,
`perline.py`; `skate_src/film/shotgrammar.py` (Mascelli's grammar over every scene), `scenecheck.py`
(motion energy, dead stretches), `framecheck.py`; `skate_src/robot_neko/mp4steps.py` (held frames on a
video), `heldiff.py` (WHAT moves on a held frame), `verify.py`, `sfxcheck.py`, `scenesheet.py`; `foley.py`
(procedural foley: skate wheels, pops, landings, a slam).

Engine-driven (`.claude/skills/run-reva/driver.py`): `bench` — ONE body, four angles × N time columns
on a plain field, the read that settles a character, a garment or a retarget; `modelcheck.py` (the
roster, pixel-diffed against references), `ridecheck.py`, `facecheck.py` (incl. see-through-the-mouth),
`stepcheck.py`, `zombiecheck.py`, `REVA_CLIPAUDIT=1` (every limb against the floor, the set and every
other body, per scene line).

The rules that made the checks worth anything (`CLAUDE.md`, `design/animation_principles.md`):
animation is verified IN MOTION, never from a still; a bench sheet and a full frame disagree → the full
frame wins; a check measuring what your own code placed proves nothing (circular); every test states its
one question, its pass and fail, a control, and VERIFIED preconditions before it runs.
