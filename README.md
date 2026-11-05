# THE CLUB — engine, animation tools and references

Firestrider Studios' game engine and everything built around it for making characters move and
filming them — **without the content**. No characters, props, levels, audio, episodes, story or
game-design documents are in this repository; the engine, the games that prove it, the tools and the
hard-won notes are.

It was built with an AI pair (Claude Code) over ten weeks of daily sessions: a working game engine on
Vulkan, a second renderer of its own that needs no driver at all, eight small games, a stop-motion
cartoon shot in it, a music video, and a skate-filming look test. Every rule in the docs was paid for
by a real failure, and most of them say which one.

The engine and its tools are under the MIT licence ([LICENSE](LICENSE)). [NOTICES.md](NOTICES.md) says
whose work is inside a build besides ours, and what must never be put in this repository.

## What is here

| | |
|---|---|
| **The engine** | `reva_native/crowd_kms.c` — one translation unit, a table of contents over `reva_native/engine/*.inc`. A frame is plain data first (`gfx_packet.c`: the packet), and three things take it. The Vulkan 1.3 renderer (`gfx_vk.c`, the shaders in `shaders/`) draws it on SDL's window on a PC, and straight into the display card's buffers on a Raspberry Pi 4 with a composite CRT on the end of it. The engine's own rasterizer (`gfx_soft.c`) draws the same frame on the processor at 320 x 240, with no graphics driver, and the same pixels on every machine. The no-display build only hashes it. What it does: skinned characters (up to 1023 bones), morph targets, layered animation and an animation graph in a text file, root motion, a 46-slot canonical rig that takes Mixamo / Unreal / Character Creator / Daz / Rigify skeletons by bone name, retargeting, hair chains, cloth, a ragdoll, loose objects with physics, walking on real triangles, a contact pass (limbs out of furniture and each other), actor minds and paths, faces and conversations, people built from measurements while a game runs, a streaming world, sun shadows and up to 256 lights, materials (highlights, reflections, normal maps), skin, hair, eyes and fur, particles, decals, level of detail, water, foliage, a post pass (bloom, the tone knee, a grade, depth of field, a tube's look on a flat panel), sound, music and saves, menus and text that read on a tube, text past ASCII and a language as a file, four pads and split screen, a stop-motion exposure clock, a scene and dialogue system, the camera rigs (see [CAMERA.md](CAMERA.md)), and online play: one game on two to four machines. |
| **The games** | `reva_native/games/` — a game is one C file against `reva_native/engine.h` (`EngineAPI` is what the engine offers, `GameExport` what a game does, both append-only behind a version). `sample` is TAG and the place to start one, `rocks` a 2D game with no bodies, `rally` bats and a ball for one to four and the place to start an online game, `tower` 3D action with walkable triangles, `herd` a crowd of hundreds, `roam` a streaming world, `talk` a talking game, `people` a character built while the game runs. Each has its own receipt, and everything in them is made by code. |
| **The camera** | tripods, dollies, cranes, orbits, tracking and panning lenses, a hand-held phone that rides a character's wrist, a phone-screen feed, an equidistant fisheye, a shot ladder with guaranteed head room, and a stop-motion tripod. [CAMERA.md](CAMERA.md). |
| **Animation tools** | `skate_src/author/` — authoring moves IN CODE (FK, aim, two-bone IK, eases with anticipation and settle), the move scores themselves (`moves/`: skating, board carry, a hug, a kiss, a cartwheel, a bow…), clip slicing, retargeting, faces (marks, jaw, mouth interior, ARKit morphs), fingers, a body's anatomy set from a table of measurements, and fitting PREMADE clothing onto a character. [ANIMATION.md](ANIMATION.md). |
| **Asset pipelines** | `skate_src/` — rigged GLB → engine pack, BVH → pack, headless Unreal Engine → glTF export (any UE project's characters, weapons and props), an unrigged statue → a body that plays the move library, a procedural suburban street, a game's own cook (`cook_game.py`: a film pack to a game pack, textures compressed), lighter copies of a prop or a body for distance (`lod.py`), what a surface is made of (`material.py`), and the TV font's art (`tvfont_more.py`). |
| **Checks** | `reva_native/anim_bench/` + `skate_src/film/` + `skate_src/robot_neko/`. The receipt (`hashcheck.py`): the rules' hashes, the frame's, one frame's pixels on Mesa's software Vulkan, and three frames of the engine's own rasterizer, which must be the same on every machine. `netcheck.py`: two to four engines in one online session on a wire made as bad as asked, every machine's hashes held against the others'. `fuzzcheck.py`: every kind of file a game reads, broken hundreds of ways, under the compiler's memory checkers. `warncheck.py`: no compiler warnings, any way it's built. Then the pack format's checker, posture, motion, weight and step checks; a four-angle-by-time bench for any body; film-grammar and framing audits; held-frame and clipped-highlight checks on a delivered video; an audio-event check; procedural foley. |
| **References** | `design/` — the engine plan (`engine_plan.md`: its queue of 29 steps, what each built, what each didn't, and what was decided), animation principles (the Williams layer), authoring, skating, cloth, faces, scene direction, cinematography (Mascelli / Murch / Katz, distilled), the level format, the pack format, the character standard, and `engine_capabilities.md`: every capability with the failure that exposed it. `design/papers/README.md` lists the physics papers used (links, not copies). |
| **AI workflow** | `CLAUDE.md` and `.claude/skills/` — the instructions and skills the AI worked from: how to build, run and screenshot the engine (`run-reva`, with `driver.py`), film craft, and "build the capability instead of working around it". `AGENTS.md` is the same brief for any other assistant. |

## Using it with Godot 4 (or with any AI)

`AGENTS.md` is the brief for an AI assistant (GPT, Grok, Codex, Claude): what to read, in what order, and the
rules this engine was built by. `godot/` is the hand-off to a Godot 4 project:
- `godot/animations/*.glb` — our skate moves (with the skateboard animated per trick) and other authored moves,
  on a Mixamo-named skeleton, verified in Blender to 5 micrometres; `godot/ANIMATION_IMPORT.md` to retarget them.
- `godot/CAMERA_PORT.md` — every camera rig specified exactly; `club_camera.gd` + `club_camera_post.gdshader`
  implement them, VERIFIED in Godot 4.7.2 — `godot/` is itself a Godot project: open it and press F5.
- `godot/characters/` — **ready-to-play characters**, each ONE `.glb` with its body, texture and rig, the 14 skate
  moves already on its own skeleton and the board animated per trick (no retargeting): **CLANKER** the robot,
  **X3D** the skater girl, **R.E.V.A. 902** with her four outfits, and **the Suit Man**, all four made entirely
  by code (the first two's builders are here: `skate_src/robot/build_robot.py`, `skate_src/x3d/build_x3d.py`).
  Clanker and x3d star in the demo: he rides, she films him. `godot/characters/club_hair.gd`:
  `ClubHair.attach(instance)` gives any character's hair chains spring physics (Godot 4.4+);
  `club_wardrobe.gd`: `ClubWardrobe.attach(instance, "<outfit>")` dresses one that has a wardrobe;
  `godot/characters/README.md` lists them all.
- `skate_src/author/rpack2gltf.py` — export any pack's skeleton and authored clips to `.glb` (`--mesh`: with
  the pack's own skinned, textured body instead of the stick figure).

## What is NOT here

Characters (`reva_native/packs/` — the exceptions are the ones in `godot/characters/`, ours and made by code,
and the games' own small bodies, built by code by each game's `make_body.py` or `make_assets.py`), props
(`props/`), audio (`aud/`, `aud2/`), levels (`reva_native/sets/` keeps the bench's fixture, three small demo
sets made by code, and one short episode's two), scenes and episodes (`reva_native/scenes/` keeps the engine's
test fixtures, four songs for the singing voice that's made by math, and `one_take` and `this_morning`: the one
episode that was made in this repository, with the two characters that are ours and made by code), source
models, the game-mode designs and the story. Those were third-party assets (Fab, Sketchfab, Mixamo,
Unreal marketplace projects) or the studio's own IP. The only typeface is the engine's own TV font; the wide
glyphs a Chinese, Japanese or Korean game needs are baked from a font its maker has the right to use
(`skate_src/author/fontbake.py`), and none ships.

So **the club itself boots to `FATAL: no packs` until you bake a character** (below). The games in
`reva_native/games/` need nothing: start there.

## Quick start (Windows)

1. Install MSYS2 and, in its MINGW64 shell: `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-vulkan-devel mingw-w64-x86_64-stb`
   (SDL2, the Vulkan headers and loader, and the stb headers — the engine includes `<stb/stb_image.h>`).
   A Vulkan driver comes with your GPU's own driver. Shaders are prebuilt (`reva_native/gfx_spv.h`);
   `glslc` from the Vulkan SDK is only needed to change one (`python reva_native/shaders/build_spv.py`).
   `reva_native/toolchain.lock` names the exact versions it was last built and blessed with, and
   `python reva_native/anim_bench/toolcheck.py` says what's different on your machine.
2. Python 3 with `numpy` and `Pillow` for the tools. ⛔ Not the Microsoft Store `python` alias. Point
   the tools at a real interpreter with `CLUB_PYTHON=/path/to/python.exe` (default: the Python running the tool).
3. Build and play a game (⛔ `mingw64/bin` MUST be on PATH or gcc dies silently with exit 1; the driver sets it):
   ```bash
   python .claude/skills/run-reva/driver.py buildvk --game rocks     # -> reva_native/crowd_rocks_vk.exe
   reva_native/games/rocks/play.bat
   python .claude/skills/run-reva/driver.py check --game rocks       # the receipt: the same hashes as blessed, no window, seconds
   ```
   `--game sample` is TAG, and `rally`, `tower`, `herd`, `roam`, `talk`, `people` are the others (`roam`, `talk`
   and `people` want `python reva_native/games/<name>/make_assets.py` first: their packs are made by code and
   aren't kept in git). `buildvk` alone builds the club, as `reva_native/crowd_vk.exe`. By hand, the whole line
   is one gcc call:
   `gcc -O2 -std=gnu17 -ffp-contract=off -DREVA_VK -DREVA_NODOORS -o crowd_rocks_vk.exe crowd_kms.c club_math.c club_audio.c club_save.c holly_fbo.c club_bridge.c club_net.c games/rocks/rocks.c crowd_win_rc.o gfx_packet.c gfx_vk.c -Dmain=SDL_main -lws2_32 -lmingw32 -lSDL2main -lSDL2 -lvulkan-1 -lm`
   (`crowd_win_rc.o` is the UTF-8 manifest, `windres crowd_win.rc -O coff -o crowd_win_rc.o`; without it `fopen` dies on a non-ASCII pack name. `-DREVA_HEADLESS` with `gfx_packet.c` alone, no `gfx_vk.c` and no Vulkan, is the no-display build the receipt runs.)
4. **No Vulkan driver, or none you trust?** The same game on the engine's own rasterizer, which links no
   graphics library at all:
   ```bash
   python .claude/skills/run-reva/driver.py buildsoft --game rocks   # -> reva_native/crowd_rocks_soft.exe
   cd reva_native/games/rocks && REVA_WINDOWED=1 ../../crowd_rocks_soft.exe 0 packs
   ```
   It's the budget picture: 320 x 240, no shadows, mirrors, fur or materials. `REVA_NOWINDOW=1` draws with no
   window at all, which is how a picture is taken on a machine with no screen.
5. Bake a character for the club itself: any rigged, animated GLB (a Mixamo export, a UE mannequin export, …):
   ```bash
   mkdir -p reva_native/packs
   python skate_src/glb2rpack_anim.py my_character.glb reva_native/packs/v1_mychar.rpack --keepnames
   ```
   The engine loads every `packs/v1_*.rpack` at boot and maps its skeleton by bone name.
6. Run the club: `python .claude/skills/run-reva/driver.py build` (-> `crowd_new.exe`; look at pixels, then
   `promote`), then `REVA_WINDOWED=1 ./crowd.exe 400 packs select` (fullscreen without `REVA_WINDOWED`), or
   drive it headless with the driver (`lab`, `bench`, `scene`, `looks`, `up`/`--live`).

## Two machines, one game

Online play is lockstep on the engine's tick: every machine works out the same world from the same dice and
the same pads, so all that crosses the wire is pad state, and a Pi and a PC can share a game. A button is
played a few ticks after it's pressed, on every machine, and a slow wire holds the picture for a moment
instead of changing the game. It is not rollback; `design/engine_plan.md`, step 29, says why.

```bash
python .claude/skills/run-reva/driver.py buildvk --game rally
reva_native/games/rally/host.bat                  # on one machine: it waits, and shows its address
reva_native/games/rally/join.bat 192.168.1.20     # on the other (join.bat 127.0.0.1: a second window on the same one)
python .claude/skills/run-reva/driver.py check --game rally     # builds the no-display engine netcheck uses
python reva_native/anim_bench/netcheck.py         # ten cases with no window, about a minute
```

`REVA_NET=host[:port[:pads]]` or `REVA_NET=join:address[:port]` makes any game's run a session, and a game's
own menu can ask for the same (`E->netHost`, `E->netJoin`; `games/rally/rally.c` shows it). UDP, port 7777 on
the host, which has to be reachable: the same network, or a port opened on a router. Nothing on the wire is
secret or signed. It's for friends.

## A game to hand to someone

```bash
python reva_native/package.py --game rocks        # -> reva_native/dist/rocks-<version>-windows-x64/ and its .zip
```

The folder holds the game on Vulkan, the same game on the engine's own rasterizer for a machine whose driver
won't start, the libraries Windows hasn't got, exactly the files the game reads, the licences, and every
file's sha256. Before it's zipped, the packaged build replays the game's receipt from inside its own folder.
`--target pi` and `--target linux` build in a container (`docker`, a Debian image with the cross compiler).

## Quick start (Linux)

The same files build against SDL2 and Vulkan with `-DREVA_SDL -DREVA_VK`. No GPU needed: Mesa's lavapipe
(Vulkan on the CPU) renders every pass the club has, and the engine's own rasterizer needs no Vulkan at all.

```bash
sudo apt-get install libsdl2-dev libvulkan-dev libstb-dev mesa-vulkan-drivers xvfb
pip install numpy Pillow
reva_native/build_linux.sh                        # -> reva_native/crowd_linux   (GAME=games/rocks/rocks.c picks a game)
reva_native/build_linux.sh headless               # -> reva_native/crowd_headless: the rules only, no display
reva_native/build_linux.sh soft                   # -> reva_native/crowd_soft: the engine's own rasterizer
cd reva_native && python3 anim_bench/hashcheck.py --game sample --exe ./crowd_headless --soft ./crowd_soft   # the receipt
# with no GPU and no desktop: lavapipe under Xvfb
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json REVA_WINDOWED=1 xvfb-run -a -s "-screen 0 1280x720x24" stdbuf -o0 ./crowd_linux 400 packs select
```

`bash .github/ci.sh` is what this repository's CI runs, on your own machine: every build above and the Pi's,
five games' receipts, online play's ten cases on loopback, and the receipt again with fresh memory filled two
ways. About five minutes where it was tried, with no GPU, no display and no window.

`driver.py` is still the Windows driver, and `.claude/skills/run-reva/linux_lab.py` / `linux_scene.py` are
its twins here. `anim_bench/fuzzcheck.py` and `anim_bench/netcheck.py` run here as they are (the fuzzer needs
gcc's sanitizers, which is why it's a Linux tool).

## Raspberry Pi 4

`reva_native/build_pi.sh` (64-bit Raspberry Pi OS, `dtoverlay=vc4-kms-v3d`; `anim_bench/pi/cross.sh` builds
the same from a PC in a container, about 45 seconds):

- **`vk`** → `crowd_vk`: the direct road. DRM master on the display card, Vulkan straight into its scanout
  buffers, flipped by the engine itself at the video clock, so every frame has the kernel's flip timestamp. The
  composite CRT at 480i or 240p (`engine/display.inc` asks about the screen on the first boot, out loud).
- **`headless`** → `crowd_headless`: the rules only, for the receipt (`python3 anim_bench/hashcheck.py`). Windows
  x86 and the Pi agree bit for bit.
- **`soft`** → `crowd_soft`: the engine's own rasterizer, under a desktop or with no window.
- **`dev`** and **`game`**: the dev console (`reva_native/devconsole/`). `crowd_dev` is the engine with no game in
  it; `game` builds a game as a library that a running `crowd_dev` picks up between ticks; `REVA_BRIDGE=<port>`
  and `anim_bench/bridge.py` drive a running engine with a line of JSON each way.
- A game on a card: `python reva_native/package.py --game sample --target pi`, then
  `reva_native/devconsole/make_image.sh` writes a Raspberry Pi OS image that boots into it.

## Where it stands (11-05), and what nobody has checked

The plan's queue of 29 steps is built. What is true of it, plainly:

- **On a Windows PC:** every game's receipt is the same as blessed, no build has a compiler warning, and the
  pictures have been looked at.
- **On Linux (x86-64) and for the Pi's ARM:** everything compiles and links, five games' receipts and online
  play's check agree with Windows, and the engine's own rasterizer draws the same pixels. The ARM builds ran
  under an emulator. The Vulkan build has not been RUN on Linux since the old renderer came out: the machine
  this was checked on has no display and no Vulkan driver. `roam`, `talk` and `people` have only made their
  packs on Windows.
- **The CI** (`.github/workflows/club.yml`) has not been seen to pass on GitHub in its present form. Every step
  of it was run from this repository's files in a Debian container first, on another version of gcc than the
  runner's.
- **On a real Raspberry Pi 4:** the engine as it was on 11-04 ran on the direct road at 240p and 480i on a
  composite tube. The Pi's hashes were the PC's when the two were last compared, on 11-02. Nothing built since
  11-04 has been on one. Performance on the Pi is the plan's last phase, and it hasn't started.
- **Online play** has only been across one PC's own loopback, with the wire's faults made by the engine. No
  two real machines have played.
- **The Pi image builder** has never written a card that was booted.
- **The other languages' words** (`lang/es.txt`, `lang/ja.txt`) were written by the AI and nobody who reads
  those languages has checked them, which is why languages are built and left off.

## Where to start reading

- `CLAUDE.md` — the build, the instruments (`REVA_*` environment switches) and the laws.
- `design/engine_plan.md` — what's built, step by step, what each step didn't build, and what was decided.
- `reva_native/engine.h` and `reva_native/games/sample/sample.c` — the boundary, and the smallest game on it.
  `reva_native/games/rally/rally.c` is the smallest one that's played across machines.
- `design/animation_principles.md` → `design/animation_authoring.md` → `design/skating.md`.
- `CAMERA.md` → `design/scene_direction.md` → `design/cinematography.md`.
- `design/engine_capabilities.md` — what the engine can do, and what each thing cost.
- `NOTICES.md` — what the licence covers, and what's somebody else's.

## Notes

- Paths in the tools are repo-relative; outputs default to `./out` (`CLUB_OUT`, `REVA_OUTROOT`).
- The docs quote the studio's director (jacob) verbatim; that is where the rules came from. Some of the
  design notes also say where a take was written on the machine they were made on: that's history, not a
  path you need.
- ⛔ `packs/ props/ aud/ aud2/ save/ shelf/` are gitignored content folders: a script that "sets up the
  tree" by creating them empty over live ones wipes a working install with `git status` clean.
