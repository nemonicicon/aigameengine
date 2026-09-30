# THE CLUB — engine, animation tools and references

Firestrider Studios' game engine and everything built around it for making characters move and
filming them — **without the content**. No characters, props, levels, audio, episodes, story or
game-design documents are in this repository; the engine, the tools and the hard-won notes are.

It was built in the open with an AI pair (Claude Code) over a month of daily sessions: a working
game engine, a stop-motion cartoon shot in it, and a skate-filming look test. Every rule in the docs
was paid for by a real failure, and most of them say which one.

## What is here

| | |
|---|---|
| **The engine** | `reva_native/crowd_kms.c` — one C file (~19,600 lines): GLES2 via ANGLE on Windows, KMS/DRM on a Raspberry Pi. Skinned characters (LBS/DQS, 1023 bones), morph targets, layered animation, root motion, a 46-slot canonical rig that takes Mixamo / Unreal / Character Creator / Daz / Rigify skeletons by bone name, retargeting, verlet hair chains, cloth, a ragdoll, rigid bodies, walking collision against real triangles, a contact pass (limbs out of furniture and each other), actor minds and a nav grid, sun shadows, a post pass (bloom, FXAA, fog, tone shoulder), practical lights with reach, a stop-motion exposure clock, a scene/dialogue system, and the camera rigs (see [CAMERA.md](CAMERA.md)). |
| **The camera** | tripods, dollies, cranes, orbits, tracking and panning lenses, a hand-held phone that rides a character's wrist, a phone-screen feed, an equidistant fisheye, a shot ladder with guaranteed head room, and a stop-motion tripod. [CAMERA.md](CAMERA.md). |
| **Animation tools** | `skate_src/author/` — authoring moves IN CODE (FK, aim, two-bone IK, eases with anticipation and settle), the move scores themselves (`moves/`: skating, board carry, a hug, a kiss, a cartwheel, a bow…), clip slicing, retargeting, faces (marks, jaw, mouth interior, ARKit morphs), fingers, and fitting PREMADE clothing onto a character. [ANIMATION.md](ANIMATION.md). |
| **Asset pipelines** | `skate_src/` — rigged GLB → engine pack, BVH → pack, headless Unreal Engine → glTF export (any UE project's characters, weapons and props), an unrigged statue → a body that plays the move library, a procedural suburban street. |
| **Checks** | `reva_native/anim_bench/` + `skate_src/film/` + `skate_src/robot_neko/` — posture, motion, weight and step checks; a four-angle-by-time bench for any body; film-grammar and framing audits; held-frame and clipped-highlight checks on a delivered video; an audio-event check; procedural foley. |
| **References** | `design/` — animation principles (the Williams layer), authoring, skating, cloth, faces, scene direction, cinematography (Mascelli / Murch / Katz, distilled), the level format, and `engine_capabilities.md`: every capability with the failure that exposed it. `design/papers/README.md` lists the physics papers used (links, not copies). |
| **AI workflow** | `CLAUDE.md` and `.claude/skills/` — the instructions and skills the AI worked from: how to build, run and screenshot the engine (`run-reva`, with `driver.py`), film craft, "build the capability instead of working around it", and the ROBOT NEKO skill whose LESSONS LEDGER is fourteen episodes of mistakes and the rule each one left. |

## Using it with Godot 4 (or with any AI)

`AGENTS.md` is the brief for an AI assistant (GPT, Grok, Codex, Claude): what to read, in what order, and the
rules this engine was built by. `godot/` is the hand-off to a Godot 4 project:
- `godot/animations/*.glb` — our skate moves (with the skateboard animated per trick) and other authored moves,
  on a Mixamo-named skeleton, verified in Blender to 5 micrometres; `godot/ANIMATION_IMPORT.md` to retarget them.
- `godot/CAMERA_PORT.md` — every camera rig specified exactly; `club_camera.gd` + `club_camera_post.gdshader`
  implement them, VERIFIED in Godot 4.7.2 — `godot/` is itself a Godot project: open it and press F5.
- `godot/characters/` — **ready-to-play characters**, each ONE `.glb` with its body, texture and rig, the 14 skate
  moves already on its own skeleton and the board animated per trick (no retargeting): **CLANKER** the robot
  and **X3D** the skater girl, both made entirely by code from jacob's character sheets
  (`skate_src/robot/build_robot.py`, `skate_src/x3d/build_x3d.py`). They star in the demo: Clanker rides, x3d
  films him. `godot/characters/club_hair.gd`: `ClubHair.attach(instance)` gives any character's hair chains
  spring physics (Godot 4.4+); `godot/characters/README.md` lists them all.
- `skate_src/author/rpack2gltf.py` — export any pack's skeleton and authored clips to `.glb` (`--mesh`: with
  the pack's own skinned, textured body instead of the stick figure).

## What is NOT here

Characters (`reva_native/packs/` — the exceptions are CLANKER and X3D, ours and made by code, in
`godot/characters/`), props (`props/`), audio (`aud/`, `aud2/`), levels (all but one bench
fixture in `reva_native/sets/`), scenes and episodes (only silent engine test fixtures remain in
`reva_native/scenes/`), source models, the game-mode designs and the story. Those were third-party
assets (Fab, Sketchfab, Mixamo, Unreal marketplace projects) or the studio's own IP.

So **the engine boots to `FATAL: no packs` until you bake a character** (below).

## Quick start (Windows)

1. Install MSYS2 and, in its MINGW64 shell: `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-angleproject mingw-w64-x86_64-stb`
   (SDL2, ANGLE's libEGL/libGLESv2, and the stb headers — the engine includes `<stb/stb_image.h>`).
2. Build (⛔ `mingw64/bin` MUST be on PATH or gcc dies silently with exit 1):
   ```bash
   export PATH=/c/msys64/mingw64/bin:$PATH
   cd reva_native
   windres crowd_win.rc -O coff -o crowd_win_rc.o      # the UTF-8 manifest: without it, fopen dies on non-ASCII pack names
   gcc -O2 -o crowd.exe crowd_kms.c holly_fbo.c crowd_win_rc.o -Dmain=SDL_main -lmingw32 -lSDL2main -lSDL2 -lEGL -lGLESv2 -lm
   ```
3. Python 3 with `numpy` and `Pillow` for the tools. ⛔ Not the Microsoft Store `python` alias. Point
   the tools at a real interpreter with `CLUB_PYTHON=/path/to/python.exe` (default: the Python running the tool).
4. Bake a character: any rigged, animated GLB (a Mixamo export, a UE mannequin export, …):
   ```bash
   mkdir -p reva_native/packs
   python skate_src/glb2rpack_anim.py my_character.glb reva_native/packs/v1_mychar.rpack --keepnames
   ```
   The engine loads every `packs/v1_*.rpack` at boot and maps its skeleton by bone name.
5. Run: `REVA_WINDOWED=1 ./crowd.exe 400 packs select` (fullscreen without `REVA_WINDOWED`), or drive it
   headless with `.claude/skills/run-reva/driver.py` (`build`, `lab`, `bench`, `scene`, `up`/`--live`).

## Quick start (Linux)

The same file builds against SDL2 + Mesa with `-DREVA_SDL` (the Windows path was always "SDL2 supplies the
window and the GLES context"; only the `_execv`/PHOSPHOR handoff lines are Win32). No GPU needed: Mesa's
llvmpipe under Xvfb renders every pass the club has, at 30 fps fixed-step.

```bash
sudo apt-get install libsdl2-dev libgles-dev libegl-dev libstb-dev xvfb libgl1-mesa-dri
pip install numpy Pillow
mkdir -p reva_native/packs
python3 skate_src/glb2rpack_anim.py godot/characters/clanker/clanker.glb reva_native/packs/v1_clanker.rpack --keepnames
reva_native/build_linux.sh                        # -> reva_native/crowd_linux
cd reva_native && REVA_WINDOWED=1 xvfb-run -a -s "-screen 0 1280x720x24" stdbuf -o0 ./crowd_linux 400 packs select
# one body, four lab cameras, a PNG per angle:
python3 .claude/skills/run-reva/linux_lab.py reva_native/packs/v1_clanker.rpack z_rm_sk_ride out/clanker_lab
```

`REVA_SHOT`, `REVA_FIXEDSTEP` and the rest of the instruments work unchanged; `driver.py` is still the
Windows driver.

## Raspberry Pi 4

`reva_native/build_pi.sh` builds both routes onto the Pi's glass (64-bit Raspberry Pi OS, `dtoverlay=vc4-kms-v3d`):

- **`crowd_kms`** — the engine's original form: DRM master on the vc4 card, GBM/EGL, page-flips at the video
  clock, the F310 on `/dev/input/js0`. No compositor and no desktop; this is the composite-CRT show. The card
  is found at boot (`REVA_DRM=/dev/dri/cardN` pins it).
- **`crowd_linux`** — the SDL platform, same binary as desktop Linux: keyboard and mouse, the select screens,
  `REVA_SHOT` capture. On the Pi desktop it is a window; with no desktop, `SDL_VIDEODRIVER=kmsdrm` puts it
  straight on the glass.

Status: both compile and the SDL path renders (verified on x86, including with ARM's unsigned `char`); the
KMS path has not been run on a Pi 4 since the 09-11 audio work, which had broken its build until now.

## Where to start reading

- `CLAUDE.md` — the build, the instruments (`REVA_*` environment switches) and the laws.
- `design/animation_principles.md` → `design/animation_authoring.md` → `design/skating.md`.
- `CAMERA.md` → `design/scene_direction.md` → `design/cinematography.md`.
- `design/engine_capabilities.md` — what the engine can do, and what each thing cost.
- `.claude/skills/robot-neko/SKILL.md` — the LESSONS LEDGER: the most useful single page for anyone
  animating with an AI in the loop.

## Notes

- Paths in the tools are repo-relative; outputs default to `./out` (`CLUB_OUT`, `REVA_OUTROOT`).
- The docs quote the studio's director (jacob) verbatim; that is where the rules came from.
- ⛔ `packs/ props/ aud/ aud2/ save/ shelf/` are gitignored content folders: a script that "sets up the
  tree" by creating them empty over live ones wipes a working install with `git status` clean.
