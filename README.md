# aigameengine

An AI-first game engine in C

It is a small, complete engine. It draws 3D characters with animation, cloth, hair and faces, and 2D
sprites. It has physics, sound, menus, saves, split screen for four pads, and play across machines. It runs
on a Windows or Linux PC, and on a Raspberry Pi 4 plugged into an old tube TV. With it come eight small
games, the tools for bringing characters in and animating them, and the notes on how all of it was built.

**AI-first** means it is made to be worked on by an AI assistant, with a person directing. You say what you
want. Your assistant builds it, runs it, checks it and shows you a picture. For that to work, everything
here runs from a command line, a game can be played and photographed with no window on the screen, and a
check that takes seconds says whether a change altered the game. What stays yours is the judging: nothing
here decides whether a picture looks right or a game feels good.

Nothing from the studio's own games is included: no characters, levels, audio or story. The eight games
make everything they need by code. The licence is MIT ([LICENSE](LICENSE), [NOTICES.md](NOTICES.md)).

## Where to go

| You want to | Read |
|---|---|
| Play one of the games, or change one | [Quick start](#quick-start-windows) |
| Work on it with an AI assistant | [Working with an AI assistant](#working-with-an-ai-assistant) |
| Make your own game | [Make your own game](#make-your-own-game) |
| Use the characters, moves or cameras in Godot 4 | [Using it with Godot 4](#using-it-with-godot-4) |
| Animate or film a character of your own | [Bring your own character](#bring-your-own-character) |
| Run it on Linux, or on a Raspberry Pi and a TV | [Other machines](#other-machines) |
| Know what has been tested and what hasn't | [How far it has been checked](#how-far-it-has-been-checked) |

## Quick start (Windows)

Install three things, once:

1. **MSYS2**, where it installs itself (`C:\msys64`). Then, in its MINGW64 shell:
   `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-vulkan-devel mingw-w64-x86_64-stb`
2. **Python 3** from python.org, then `pip install numpy Pillow`. The `python` that opens the Microsoft Store
   is not Python.
3. **A graphics driver with Vulkan.** Any recent graphics card's driver has it. With none, see
   [No graphics driver?](#no-graphics-driver)

The commands on this page are for a bash shell, such as Git Bash, run from the repository's top folder.

```bash
python .claude/skills/run-reva/driver.py buildvk --game rocks
reva_native/games/rocks/play.bat
```

That builds and starts `rocks`, a 2D shooter. A and D or the stick move, Space or the A button shoots. Leave
it alone and it plays itself.

Now check that the game keeps its own time:

```bash
python .claude/skills/run-reva/driver.py check --game rocks
```

It plays the game with no window several times over, at even and at uneven frame pacing, and compares what
happened tick by tick. `all same` means the rules gave the same answer every time, which is what a replay and
online play depend on. Nothing is compared with a recorded answer: the engine isn't finished, and what it
prints changes as it's improved.

### The eight games

Put another name in the same two commands.

| Game | What it is | The place to start |
|---|---|---|
| `sample` | Tag: run the others down before the clock runs out | any game. It is the smallest |
| `rocks` | a 2D shooter for up to four | a 2D game |
| `rally` | bats and a ball for one to four | a game played across machines |
| `tower` | climb, punch and light three beacons, with split screen | a 3D action game |
| `herd` | hundreds of bodies on a field: gather yours, turn theirs | a crowd or strategy game |
| `roam` | a world bigger than memory, loaded as you run across it | an open world |
| `talk` | two people and a conversation, with faces that move | a game whose people talk |
| `people` | a character creator: a person made from measurements | a game where players make themselves |

`roam`, `talk` and `people` make their files before their first build:
`python reva_native/games/<name>/make_assets.py`. The first lines of each game's C file say what it shows
and how it's played.

### No graphics driver?

The engine has a second renderer of its own. It draws on the processor and links no graphics library.

```bash
python .claude/skills/run-reva/driver.py buildsoft --game rocks
cd reva_native/games/rocks
REVA_WINDOWED=1 ../../crowd_rocks_soft.exe 0 packs
```

It is the plain picture: 320 x 240, no shadows and no materials. Its pixels are the same on every machine.

### If it doesn't work

- **The build ends at once and says nothing.** gcc isn't where the driver looks. MSYS2 has to be at `C:\msys64`.
- **`FATAL: no packs`.** That game makes its files first: see the line under the table above.
- **Python opens the Microsoft Store, or a tool hangs.** That is the Store's stand-in. Install real Python.

## Working with an AI assistant

- **Claude Code:** open this folder. It reads [CLAUDE.md](CLAUDE.md) and the skills in `.claude/skills/` by
  itself.
- **Any other assistant** (ChatGPT, Codex, Grok and the rest): give it [AGENTS.md](AGENTS.md) first. It is
  plain text, written to be pasted into a chat.

Then ask in your own words. These work today, and this is what the engine gives your assistant to do them:

| You ask | It uses |
|---|---|
| "Build rocks and show me a picture of it." | the engine's own renderer, which draws with no window |
| "Play ten seconds holding right and shooting, and show me where it ends up." | the bridge: a running game takes commands |
| "Does rocks still play the same at any frame rate?" | the check: the game run at several frame pacings, with no window |
| "Add a second kind of rock." | one C file, and one header that lists everything the engine offers |

You don't type the commands below. They are here so you know what your assistant can do.

```bash
# a picture of a game with no window: it is drawn in memory, written to a file, and the run ends
# (crowd_rocks_soft.exe is the build from "No graphics driver?" above)
cd reva_native/games/rocks
REVA_NOWINDOW=1 REVA_FIXEDSTEP=60 REVA_SHOT=500 REVA_SHOT_PATH='C:\somewhere\rocks.ppm' ../../crowd_rocks_soft.exe 0 packs

# a running game, driven: stop its clock, press a button, run sixty ticks, ask where things stand
REVA_NOWINDOW=1 REVA_BRIDGE=7766 ../../crowd_rocks_soft.exe 0 packs &
python ../../anim_bench/bridge.py hold
python ../../anim_bench/bridge.py press a 30
python ../../anim_bench/bridge.py step 60
python ../../anim_bench/bridge.py get_state
python ../../anim_bench/bridge.py quit
```

Two things to hold your assistant to:

- **A check says nothing has changed. It never says anything is right.** Look at a full picture, and play
  the game, before you believe it works.
- **If the engine can't do something, it gets built**, not worked around. `.claude/skills/build-the-capability`
  is how.

## Make your own game

A game is one C file that includes one header.

1. Copy `reva_native/games/sample` to `reva_native/games/mygame`, and rename `sample.c` in it to `mygame.c`.
2. Build it with `--game mygame`, exactly as above. In the copied `play.bat`, change `crowd_sample_vk.exe`
   to `crowd_mygame_vk.exe`.
3. Change the game. `tick` runs sixty times a second and changes the game's state. `draw` and `hud` put
   that state on the screen and cannot change it. [reva_native/engine.h](reva_native/engine.h) is
   everything the engine offers a game, and a game uses nothing else.
4. When it's worth protecting, add it to the check: a few lines in `reva_native/anim_bench/hashcheck.py`
   beside the other games'. Your assistant can add them.

Start from `rocks` for 2D and from `rally` for a game played across machines.

## What's in the repository

| Folder | What's in it |
|---|---|
| `reva_native/` | the engine. `engine.h` is what a game sees, and `engine/` with the `.c` files beside it is the engine |
| `reva_native/games/` | the eight games |
| `reva_native/anim_bench/` | the checks: a game against itself, the online check, the file-reader fuzzer and more |
| `reva_native/devconsole/` | making a game on a Raspberry Pi with only a TV |
| `reva_native/android/` | a game as an Android APK: the two scripts and SDL2's Java |
| `skate_src/` | the tools: bringing characters in, writing animation in code, building levels, and bringing a Godot project's scenes and shaders across (`skate_src/godot/`) |
| `godot/` | what a Godot 4 project can take: characters, moves, camera rigs |
| `design/` | the notes: the engine's plan step by step, the file formats, and the craft of animation and film |
| `.claude/skills/` | what an AI assistant loads: how to build, run and photograph the engine, and film craft |
| `AGENTS.md`, `CLAUDE.md` | the briefs an AI assistant reads first |

About the names: `reva_native`, `skate_src`, `crowd_*.exe`, the `REVA_` switches and files called `club_*`
all come from the project this engine grew out of. They are only names, kept so nothing breaks.

## What the engine does

- **Characters:** skinned bodies of up to 1023 bones, face and body shapes, layered animation, an animation
  graph in a text file, hair, cloth, a ragdoll, talking faces, and people built from measurements while a
  game runs. Skeletons from Mixamo, Unreal, Character Creator, Daz and Rigify are recognised by bone name.
- **Worlds:** levels as text files, walking on a level's real triangles, loose objects with physics, crowds
  of hundreds, and a world that loads as you cross it.
- **The picture:** a Vulkan renderer with sun shadows, up to 256 lights, materials, skin, hair, eyes and
  fur, particles, decals, water, foliage, depth of field and a tube TV's look on a flat panel. Beside it,
  the engine's own renderer, which needs no graphics driver.
- **What a game needs:** four pads, split screen, menus and text that read on a tube TV, sound and music,
  saves, online play for two to four machines, and text in other languages, which is built and switched off.
- **Film:** scenes and dialogue as text files, camera rigs, and a stop-motion clock. [CAMERA.md](CAMERA.md)
  and [ANIMATION.md](ANIMATION.md) are the guides.

`design/engine_plan.md` says what each part does, and also what each part doesn't.

## Other machines

### Linux

```bash
sudo apt-get install libsdl2-dev libvulkan-dev libstb-dev libdrm-dev
cd reva_native
./build_linux.sh                  # the Vulkan build: crowd_linux
./build_linux.sh soft             # the engine's own renderer: crowd_soft
./build_linux.sh headless         # no display at all, for the check: crowd_headless
python3 anim_bench/hashcheck.py --game sample --exe ./crowd_headless
```

`GAME=games/rocks/rocks.c` in front of a build picks the game. `bash .github/ci.sh` from the top folder runs
everything this repository's CI runs, in about five minutes, with no graphics card and no window.

### Raspberry Pi 4

On a fresh 64-bit Raspberry Pi OS Lite, from `reva_native`:

```bash
sudo bash devconsole/setup_pi.sh
```

It installs what the engine needs, builds it, builds the sample game and runs its check. The script is
written and has not yet been tried on a fresh card. `build_pi.sh` has the builds one at a time. The Pi draws straight to the screen with no desktop under it.
It has been run on a composite tube TV, at 240p and 480i: `setup_pi.sh --composite` turns that output on, and
HDMI off. On its first start the engine asks about your screen, out loud.
[reva_native/devconsole/README.md](reva_native/devconsole/README.md) is about making a game on the Pi itself,
with your assistant in a chat box on the TV.

### Android

```bash
python reva_native/android/make_apk.py --game sample --image IMAGE --sdk-volume SDKVOL --sdl-volume SDLVOL
```

It makes a signed test APK under `reva_native/dist/android/`: the engine and the game on SDL2, with the
game's data inside. It builds in a Docker container that you provide, holding the Android SDK and NDK and
SDL2 built for Android. No script here makes that container:
[reva_native/android/README.md](reva_native/android/README.md) says what it must hold, and what the engine
does on a phone. The APKs build. None has been started on a phone since the scripts took their present form.

## Two machines, one game

```bash
python .claude/skills/run-reva/driver.py buildvk --game rally
reva_native/games/rally/host.bat                  # on one machine: it waits, and shows its address
reva_native/games/rally/join.bat 192.168.1.20     # on the others, with the host's address
```

`join.bat 127.0.0.1` in a second window is two players on one PC. Every machine works out the same game from
the same pad presses, so only the pads cross the wire, and a PC and a Pi can share a game. A button is played
a few ticks after it's pressed, on every machine alike. The host needs UDP port 7777 reachable: the same
network, or a port opened on a router. Nothing on the wire is secret or signed. It is for friends.

## A game to hand to someone

```bash
python reva_native/package.py --game rocks
```

It writes a folder and a zip under `reva_native/dist/`: the game on both renderers, exactly the files it
reads, and the licences. Before it zips, it replays a recorded game from inside the folder.

`--target linux` and `--target pi` do the same for those machines. They build inside a Docker container
called `revalinux`: Debian 13 with the ARM cross compiler, and `reva_native` mounted at `/src`. Making that
container isn't scripted yet. The first lines of `reva_native/anim_bench/pi/cross.sh` say what goes in it.

## Bring your own character

The engine also has an animation lab and a scene player, which the animation and film tools use. They need a
character, and none is included. Any rigged, animated GLB will do. This bakes the character that ships in
`godot/` and takes a picture of her from four sides, with no window:

```bash
mkdir -p reva_native/packs
python skate_src/glb2rpack_anim.py godot/characters/reva902/reva902.glb reva_native/packs/v1_reva902.rpack --keepnames
python .claude/skills/run-reva/driver.py buildsoft
python .claude/skills/run-reva/driver.py lab out/reva902.png --soft --subject reva902 --ctl "face=0 grid=4 dqs=0 clip=z_rm_sk_ride partner=off"
```

Put your own file in place of hers. What you bake lands in folders git ignores, `packs/` and `props/`. It is
yours and not in the repository, so keep your own copy. [ANIMATION.md](ANIMATION.md) goes on from here:
writing moves in code, retargeting, faces, fingers and fitting clothes.

## Using it with Godot 4

The engine does not run inside Godot. What a Godot 4 project can take from `godot/`:

- **A ready character:** R.E.V.A. 902, one `.glb` with her rig, her texture, her wardrobe and fourteen skate
  moves on her own skeleton. [godot/characters/README.md](godot/characters/README.md) has her page.
- **The moves** as glTF, with the steps to put them on your own character:
  [godot/ANIMATION_IMPORT.md](godot/ANIMATION_IMPORT.md).
- **The camera rigs:** a tripod, a dolly, a tracking lens, an orbit, a hand-held phone and a fisheye, as a
  script and a shader with their exact spec: [godot/CAMERA_PORT.md](godot/CAMERA_PORT.md). The `godot`
  folder is itself a Godot project. Open it and press F5.
- **Helpers:** scripts for hair physics and wardrobes, and the cloth solver as a Godot extension, which has
  to be built first (`reva_native/clubcloth/`).
- **A real city as a park:** the city cook (`skate_src/citymap/`) turns OpenStreetMap and the USGS's one-metre
  elevation into one `.glb` and a park file, at true scale. Seattle's International District is the worked
  example. [skate_src/citymap/MAP_GUIDE.md](skate_src/citymap/MAP_GUIDE.md) says what to download and how to
  cook and change it, first for people and then for an AI.

The other way, from a Godot project into the engine, is `skate_src/godot/`. `engine_export.gd`, run in
Godot, writes a scene out. `godot2set.py` makes that a level and a pack. `godot_shader.py` turns a Godot
spatial or sky shader into one the engine draws with. `gd_tuning2c.py` turns exported numbers into a C
struct, and `reva_native/godot/gd_math.h` is Godot's math types in C, for game code brought across.

## What isn't here

- **Characters, props, levels and audio** from the studio's games. They were bought assets or the studio's
  own. The exceptions are ours and made by code: the eight games' small files and the one Godot character.
- **Fonts.** The only typeface is the engine's own TV font. The wide glyphs that Chinese, Japanese and Korean
  need are baked from a font you have the right to use (`skate_src/author/fontbake.py`), and none ships.
- **The studio's game designs and stories.**

## How far it has been checked

Plainly, as of 2026-11-13.

| | What has been run | What has not |
|---|---|---|
| **Windows PC** | every game's check, online play between engines on one PC, and the pictures looked at | two real machines playing each other |
| **Linux PC** | five games' checks and the online check, on every push | the Vulkan build: it compiles and links, and nobody has started it on Linux since Vulkan became the engine's only GPU renderer on 2026-11-04 |
| **Raspberry Pi 4** | the engine as it was on 2026-11-04, on a real Pi and a tube TV at 240p and 480i | what has changed since, which has run under an ARM emulator at most. `setup_pi.sh` on a fresh card, and the card image builder |
| **Android** | the sample game built into a signed APK for `arm64-v8a` and `x86_64`, on 2026-11-13 | either APK started. One game's APK ran on an emulator on 2026-11-06, before the scripts and the phone's start-up took their present form. Nothing has run on a real phone |
| **Godot 4 hand-off** | the demo in Godot 4.7.2 on 2026-11-13: R.E.V.A. 902 riding, filmed by the tracking camera, 150 frames with no script error | the demo's other cameras that day. The tools in `skate_src/godot/` on any project but the one they were written for |

Two more things to know. The Spanish and Japanese words in the games' `lang/` folders were written by an AI
and no reader of those languages has checked them, which is why languages are built and switched off. And
performance on the Pi is the plan's last phase, which hasn't started.

## Words you'll meet

| Word | What it means here |
|---|---|
| **tick** | one step of a game's rules. Sixty a second, and the same on every machine |
| **the check** | a game run several times with no window, at even and uneven frame pacing: a fingerprint of its rules and of each frame, which has to come out the same every time. Nothing recorded is compared |
| **tape** | a recording of the pads, tick by tick. Playing it back replays a session exactly |
| **pack** | a character or a prop baked into the engine's own file (`.rpack`): mesh, skeleton, animation, textures |
| **set** | a level, as a text file (`.set`) |
| **scene** | a directed conversation or a film shot, as a text file |
| **rung** | a picture quality level, from `pi` up to `ultra` |
| **the lab** | the engine's built-in animation lab: one character, any clip, several cameras |
| **the bridge** | a running engine's command port: stop the clock, step it, press a button, take a picture |

## More to read

- [CLAUDE.md](CLAUDE.md): every build, every switch, and the rules this engine was built by.
- `design/engine_plan.md`: what was built, step by step, and what was decided along the way.
- `design/engine_capabilities.md`: each thing the engine can do, and the failure that led to it.
- [reva_native/games/sample/sample.c](reva_native/games/sample/sample.c): the smallest whole game.


