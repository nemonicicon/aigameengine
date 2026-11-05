# THE CLUB — engine + tools (content not included)

One engine (`reva_native/crowd_kms.c`, one translation unit: a table of contents over `reva_native/engine/*.inc`,
so search across `engine/` to find a function). A frame is plain data first (`gfx_packet.c`, the packet), and three
things take it: the Vulkan renderer (`gfx_vk.c`, `shaders/`), the engine's own rasterizer (`gfx_soft.c`: the same
frame on the processor at 320 x 240, no driver, the same pixels on every machine), and the no-display build, which
only hashes it. Around it: the games that prove it (`reva_native/games/`, each one C file against
`reva_native/engine.h`), the asset pipelines and authoring tools (`skate_src/`), the checks
(`reva_native/anim_bench/`, `skate_src/film/`, `skate_src/robot_neko/`), and the references (`design/`, `README.md`,
`CAMERA.md`, `ANIMATION.md`). `LICENSE` is MIT, and `NOTICES.md` says whose work is inside a build and what must
never be put in this repository. Characters, props, levels and audio are NOT here: the club prints `FATAL: no packs`
until a character is baked into `reva_native/packs/`. The games need nothing, so start there.

## Where this repository comes from
The engine is built in a private workbench and arrives here by a sync, one reviewed step at a time
(`.club_sync.json` holds the workbench commit and every synced file's hash). The files that are this repository's
own, and are edited here: `README.md`, `CLAUDE.md`, `AGENTS.md`, `ANIMATION.md`, `CAMERA.md`, `.gitignore`,
`.github/` and `godot/`. ⛔ An edit made here to any other file is caught by the next sync and has to be taken
home first, so make engine changes in the workbench.

## Build & run (Windows)
- MSYS2 MINGW64 with gcc, SDL2, the Vulkan headers and loader (`vulkan-devel`) and stb. ⛔ `mingw64/bin` MUST be
  on PATH or gcc dies SILENTLY with exit 1 and no output: `export PATH=/c/msys64/mingw64/bin:$PATH`. The driver
  sets it itself.
- `.claude/skills/run-reva/driver.py` carries every build line (`SRC`, `RENDER`, `DEFS`, `LIBS` in one place):
  `build` → `crowd_new.exe` (the club), `buildvk [--game X]` → `crowd_vk.exe` or `crowd_X_vk.exe` (one game),
  `buildsoft [--game X]` → `crowd_soft.exe` or `crowd_X_soft.exe` (the same on the engine's own rasterizer),
  `check [--game X]` → the receipt (no `--game` here is the sample), `promote` → `crowd_new.exe` over `crowd.exe`.
  It makes `crowd_win_rc.o` (the UTF-8 manifest; without it `fopen` dies on a pack with a non-ASCII name) when
  it's missing.
- By hand: `gcc -O2 -std=gnu17 -ffp-contract=off -DREVA_VK -DREVA_NODOORS -o crowd_rocks_vk.exe crowd_kms.c club_math.c club_audio.c club_save.c holly_fbo.c club_bridge.c club_net.c games/rocks/rocks.c crowd_win_rc.o gfx_packet.c gfx_vk.c -Dmain=SDL_main -lws2_32 -lmingw32 -lSDL2main -lSDL2 -lvulkan-1 -lm`
  (`club.h` names the units: the 4x4s, the audio mixer, the save store, the bridge and the wire are their own
  files.) The own rasterizer is `-DREVA_SOFT` with `gfx_soft.c` where `gfx_vk.c` is and no `-lvulkan-1`; the
  no-display build is `-DREVA_HEADLESS` with `gfx_packet.c` alone. A build with none of the three refuses to
  compile and says so: there is no other picture.
- Build to `crowd_new.exe`, verify on pixels, THEN `promote`.
- Run: `REVA_WINDOWED=1 ./crowd.exe 400 packs select` (fullscreen without WINDOWED). ⛔ argv[3] is a
  FLOW (`select`) or a body to load as the hero, never both. A game: `games/<name>/play.bat`.
- Headless: the driver (`lab`, `bench`, `face`, `scene`, `looks`, `up` + `--live`; `--soft` on a shot takes the
  picture through the own rasterizer with no window). One engine instance at a time.
- Shaders live in `reva_native/shaders/` and are prebuilt into `gfx_spv.h`; `python shaders/build_spv.py`
  regenerates it after a change (`glslc` from the Vulkan SDK). Its `ALSO` table builds a shader's variants.
- Python: a real Python 3 with numpy + Pillow (`CLUB_PYTHON` points the tools at it). ⛔ Never the
  Microsoft Store `python` alias. ⛔ The Windows console is cp1252: a script that prints a non-ASCII
  glyph dies unless it reconfigures stdout to UTF-8.
- `reva_native/toolchain.lock` is the compilers and libraries the receipts were last blessed with, by exact
  version; `python reva_native/anim_bench/toolcheck.py` says what's different on this machine.

## Build & run (Linux)
- `reva_native/build_linux.sh` → `crowd_linux`: Vulkan on SDL's window (`-DREVA_SDL -DREVA_VK`); `headless` →
  `crowd_headless`, the rules only; `soft` → `crowd_soft`, the engine's own rasterizer, which links no Vulkan.
  `GAME=games/<name>/<name>.c` picks the game (the sample when none is named). Deps: `libsdl2-dev libvulkan-dev
  libstb-dev`; with no GPU, `mesa-vulkan-drivers` (lavapipe, Vulkan on the CPU: every pass the club has, the same
  pixels every run) and `xvfb`.
- The receipt: `cd reva_native && python3 anim_bench/hashcheck.py --game sample --exe ./crowd_headless --soft ./crowd_soft`.
  `bash .github/ci.sh` is everything this repository's CI runs, on your own machine: every build here and the
  Pi's, five games' receipts, `netcheck.py`, and the fill-byte test with its control.
- Run: `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json REVA_WINDOWED=1 xvfb-run -a -s "-screen 0 1280x720x24" stdbuf -o0 ./crowd_linux 400 packs select`.
  ⛔ Without `stdbuf -o0` a piped log is block-buffered and ends at the kill with nothing in it.
  ⛔ As of 11-05 the Vulkan build compiles and links on Linux and has NOT been run there since the old renderer
  came out, and neither have `linux_lab.py` and `linux_scene.py`: the machine it's checked on has no display and
  no Vulkan driver. On Linux, what has run is the no-display build and the own rasterizer.
- Lab bench: `.claude/skills/run-reva/linux_lab.py <pack> <clip> <outdir>` — one body, the four lab cams,
  a PNG per angle. A scene take: `linux_scene.py OUT.mp4 --name <scene> --subject <hero> --partner <pack>
  --set <set> --seconds N --fps 8|60`. Voices where Kokoro cannot exist: `scenes/say_espeak.py` (a SCRATCH
  TRACK, same contract, says so in the log). `driver.py` itself is Windows-only (taskkill, crowd.exe).
- ⛔ `holly.hlm` (the venue-screen head) is content: a missing one logs `NO HEAD MODEL` and the floor
  runs with dark screens instead of dying.

## Build & run (Raspberry Pi 4)
- `reva_native/build_pi.sh vk|headless|soft|dev|game` (64-bit OS, `dtoverlay=vc4-kms-v3d`; `devconsole/setup_pi.sh`
  installs what it links against: `libdrm-dev libsdl2-dev libstb-dev libvulkan-dev`). `vk` → `crowd_vk`: the
  direct road, DRM master on the display card and Vulkan straight into its scanout buffers, flipped by the engine
  at the video clock (the kernel's flip timestamps are the `[frames]` ledger). `headless` → the receipt. `soft` →
  the own rasterizer, under a desktop or with no window. `dev` → the engine with no game in it; `game` → a game as
  a library a running `crowd_dev` picks up between ticks (`devconsole/`). From a PC, `anim_bench/pi/cross.sh
  vk|headless [game]` builds it in a container in about a minute and copies it over (`NOCOPY=1` leaves it there;
  `PI=user@host` names the Pi).
- The DRM card is FOUND (first card that reports a connector; `REVA_DRM=/dev/dri/cardN` pins it): on a Pi 4
  v3d and vc4 are two cards. The composite CRT runs at 480i or 240p; the first boot asks about the screen
  (`engine/display.inc`, `save/display.txt`; `REVA_VIDEO`, `REVA_NOSETUP`, a tape or `REVA_FIXEDSTEP` skip it).
  ⛔ 50 Hz stays behind `allow50 1` in that file until a PAL set has tested it.
- Where it stands: the engine as it was on 11-04 ran on a real Pi 4, on the tube at 240p and 480i, and the Pi's
  hashes were the PC's when the two were last compared (11-02). Nothing built since 11-04 has been on one: the
  ARM builds of 11-05 compile, and ran only under an emulator. Performance is the plan's last phase: no number
  here is a target yet.

## The tick, the receipt, the dice, the ladder (`design/platform_tiers.md`)
- **The sim runs one whole tick at a time.** `REVA_TICK=<hz>` (default 60); a frame owes 1..4 ticks from the
  wall time, snapped against vsync jitter, capped. A frame that owes k passes through the whole loop k times, one
  tick each, and only the last pass draws (`clockFrame`, `g_noDraw`), so the rules don't change with the frame
  rate. `simStepS()` is one tick, always. `REVA_FIXEDSTEP` is one tick per frame with the tick equal to the step;
  `REVA_CADENCE=1,1,1,1,2` forces a pattern of ticks per frame under it, and any cadence must print the same
  hashes. The stat line's `fps`/`work` and the HUD fps read `wall_ms()`; `now_ms()` is the tick clock, always.
- **The receipt: `driver.py check [--game X]`** (on Linux and the Pi: the headless build, then
  `python3 anim_bench/hashcheck.py [--game X]`). It runs the game with no input, with a tape, and at odd tick
  patterns, and compares every `[simhash]` line (the rules) and `[packet]` line (what's asked to be drawn)
  against `anim_bench/hash_refs/`. Seconds, no window, the same answer on any machine; Windows x86 and the Pi
  agree bit for bit. `--bless` only once you know why it changed. It says nothing has changed, not that anything
  is right, and nothing about how the picture looks: that's still eyes.
- **The receipt's pixels, two kinds.** `--soft <build>`: three frames of the engine's own rasterizer, drawn with
  no window (`*_soft` in `hash_refs/`). No driver is named beside them, because those pixels are the same on every
  machine: a difference anywhere is a difference. `--vk <build>`: one frame of the Vulkan build on Mesa's lavapipe
  (`REVA_LAVAPIPE=<its lvp_icd json>`, or the usual places), hashed (`*_pixels`, each naming the driver it was
  blessed on; another driver is reported, not failed). Without lavapipe that line is skipped and says so.
- **`REVA_SIMHASH=<every N frames>`** prints `[simhash] tick N exact … coarse …` and `[packet] tick N …` (and
  every `REVA_SHOT` frame does). Same inputs must print the same hashes. Build with `-ffp-contract=off` (the
  scripts do) or x86 and ARM disagree on the last bit. When a hash parts: `REVA_SIMHASH=1` finds the tick,
  `REVA_SIMDUMP=<tick>:<file>` names the value, `REVA_PACKETDUMP=<tick>:<file>` gives the frame as text to diff.
- **`rand()` is the club's own LCG** on every platform (`REVA_RAND=libc` for the library's), and the math
  the sim uses is the club's own (`club_math.c`), because each C library rounds its own way.
- **The uninitialised-memory test:** two runs with `MALLOC_PERTURB_=85` and `=170` must print identical
  `[simhash]` lines. `calloc` the arrays the sim reads.
- **The tape: `REVA_RECORD=<file>` / `REVA_REPLAY=<file>`.** The pads and keyboard after the polls, one
  line a tick, floats as exact bits, the seed in the header; a replay writes it back at the same point and
  ignores the hardware. Under `REVA_FIXEDSTEP` a replay IS the session, at any cadence. Any bug you can
  reproduce by hand is now a regression test: record it, keep the tape, assert the hash.
- **The HUD draws on a 1280-wide canvas** (`HW`/`HH`), never in screen pixels; text a player must read on a
  tube goes through `tvText` (a 35 x 24 grid inside the safe box, the same at 480i, 240p and on a PC).
- **`REVA_TIER=pi|low|mid|high|ultra`** (default `high`). A rung is a row of `engine/gfxset.inc`: the render
  scale, the sun map, bloom, anti-aliasing, the cloth substeps, materials, skin and fur, particles and the lens's
  passes (`REVA_GFX="msaa=1,hdr=0"` is each feature's off-switch, for an A/B). There is no `auto`: nothing
  measures itself and moves. At the `pi` rung the picture must stay the same, pixel for pixel, whatever's added
  above it. ⛔ The engine asks the same way on every machine: what a setting changes is how the renderer answers,
  never what goes in the packet, or the packet's hash would change with the machine.

## The boundary and the games
- `reva_native/engine.h` is everything a game sees of the engine (Quake II's shape): `EngineAPI` is what the
  engine offers, `GameExport` what a game does, both append-only behind a version. A game's tick writes its
  state, and its `draw` and `hud` get that state `const`. `gameEntry` is the one thing a game's file must
  define. The engine's own systems ask `GAME_ON`, never which game. A game needing something new adds a line to
  the table, not a reach past it.
- `games/sample` is TAG and the place to start (its one body is built by code: `make_body.py`); `rocks` is 2D
  (`.flat=1`, sprites from one sheet); `rally` is bats and a ball for one to four, and the place to start a game
  played across machines; `tower` 3D action (`climb` in a set makes its triangles walkable, `animgraph.inc` is a
  text file saying which clip plays and when, `E->views` is split screen); `herd` hundreds of bodies; `roam` a
  streaming world (`engine/stream.inc`); `talk` faces and conversations; `people` a person built from
  measurements while the game runs. ⛔ `roam`, `talk` and `people` make their packs with
  `games/<name>/make_assets.py` (numpy, Pillow) before they'll run: those aren't in git, and without them the
  game stops at `FATAL: no packs`. The other five have everything they read in git.
- In the table: objects and physics (`E->spawn`, `E->push`, `E->bodyLimp`), pads (four, `save/controls.txt`),
  menus (`engine/ui.inc`), level changes and paths (`E->level`, `E->path`), lights, particles and decals
  (`E->emitter*`, `E->decalAt`), sound and music (`E->sound`, `E->music`, `E->musicClock`), a save
  (`E->saveBegin`..`saveEnd`, `E->load`: `save/<game>_<slot>.txt`, every value bounds-checked on the way in),
  a language (`E->word`, below) and a session (`E->netHost`, `E->netJoin`, below).
- A game with cooked packs lists every file it loads in a `game.txt`, with a budget per kind, and
  `anim_bench/cookcheck.py` holds it to that. None of the games here has one: everything in them is made by code.
- A game's receipt is a list of scenarios in `anim_bench/hashcheck.py`, by the game's name: a new game adds its
  own there and blesses it once.
- `design/pack_format.md` is what's on disk; `anim_bench/packdump.py --check` holds a pack to it.

## Online play (`club_net.c`, `engine/net.inc`)
- Lockstep on the tick with a delay, not rollback: every machine works out the same world from the same dice and
  the same pads, so only pad state crosses the wire, over UDP through the host. `REVA_NET=host[:port[:pads]]` on
  one machine and `REVA_NET=join:<address>[:port]` on the others (`games/rally/host.bat`, `join.bat`), or a game's
  own menu through `E->netHost` and `E->netJoin`. `club_net.c`'s first comment says what a session is, and
  `design/engine_plan.md`, step 29, why it isn't rollback.
- ⛔ Nothing in a tick may read anything of the session, `E->netState` included: a session's tape has to replay
  with no wire, which is how an online fault gets found. `E->netHere` and the rest are for `draw` and `hud`.
- ⛔ Anything that differs between machines and reaches the rules parts a session. A save did (the host's are
  everyone's in a session) and so did the sound card's clock (the music clock is the tick's in one).
- `anim_bench/netcheck.py` is its check: two to four no-display engines on one machine, a wire made as bad as
  asked (`REVA_NETLAG`, `NETJITTER`, `NETLOSS`, `NETDUP`), every machine's `[simhash]` held against the others'.
  About a minute; `driver.py check --game rally` builds the engine it uses. ⛔ It's one machine's loopback: it
  says nothing about a real network. No two real machines have played yet.

## A file is checked before it's believed
- `engine/packcheck.inc`: `packCheck` reads the whole of a pack once before the loader touches it and holds every
  table, offset and index to the file's own length (`design/pack_format.md`'s rules). A pack that fails is
  refused by name with the reason. ⛔ Anything new the engine reads out of a pack by a number the pack supplies
  needs its line there first. `txtNumsOk` is the text readers' gate, and `langFmt` keeps a translated line only
  when its `%` directives are the game's, one for one.
- `anim_bench/fuzzcheck.py` (Linux: it needs gcc's sanitizers) breaks each kind of file a game reads hundreds of
  ways and runs the whole engine on every broken copy, under the memory and undefined-behaviour checkers:
  `--build` after an engine change, then with no flags. A new file reader gets a kind there.

## Text, and another language's words (built, and left off)
- Text is UTF-8 everywhere. The TV font is the only typeface: ASCII in `engine/tvfont.inc`, accented Latin, Greek
  and Cyrillic in `engine/tvfont2.inc`, which is WRITTEN BY `skate_src/author/tvfont_more.py` (edit that, never
  the `.inc`). Wide glyphs (Chinese, Japanese, Korean) are baked by `skate_src/author/fontbake.py` into
  `data/<name>.tvw` from a font the game's maker has the right to use. ⛔ A `*.tvw` is never committed here: a
  glyph file carries its font's licence.
- A language is `lang/<code>.txt` beside the game (`games/sample/lang/`, `games/talk/lang/`): lines of
  `"the game's text" = "theirs"`. `REVA_LANG=<code>` for one run, `E->language` from a game, `E->word("SCORE %d")`
  for a string a game builds. ⛔ Nothing of a language reaches `[simhash]`, and a game mustn't decide anything by
  what a string says after `E->word`. ⛔ The words in those files were written by the AI and nobody who reads the
  languages has checked them, which is why no game turns one on by itself.

## A game to hand to someone
`python reva_native/package.py --game X [--target pi|linux] [--version V]` writes `reva_native/dist/<game>-<version>-<machine>/`
and its archive: both renderers, exactly the files the game reads, the licences and a sha256 of every file, and
it replays the receipt's tape from inside the package before it's zipped. ⛔ It never packs a `*.tvw`, a save or
a source file. `devconsole/make_image.sh` writes a Pi OS image that boots into a packaged game: no card it wrote
has been booted yet.

## Headless verification instruments (env, all off by default)
`REVA_FIXEDSTEP=<fps>` replaces wall time with one virtual step per rendered frame (and pins the crowd's
seed) — it is what makes any capture comparable. `REVA_SHOT=<frame>` ARMS a capture (`REVA_SHOT_PATH`
must be a WINDOWS path on Windows; `REVA_SHOTN`/`REVA_SHOTEVERY` say how many), and a run armed with it ends
itself once its last frame is written; a harness still polls, for the run that never gets that far.
`REVA_NOWINDOW=1` runs the own rasterizer with no window at all. `REVA_CAMLOCK=1`, `REVA_NOSKY=1`,
`REVA_CLEAR=r,g,b`, `REVA_CLIPAUDIT=1` (limbs vs floor, set and each other), `REVA_NOWMS=1` (the Williams layer
off), `REVA_NOFINGERS=1`, `REVA_NORAGDOLL=1`, `REVA_RDDBG=1`, `REVA_MEMSAY=<ticks>` (`[mem]`),
`REVA_LODDBG=1`, `REVA_SOFTSAY=1` (what the own rasterizer drew, every 120 frames), `REVA_NETSAY=1`. A renderer
feature's A/B is its setting: `REVA_GFX="normals=0"`.
A screenshot prints what its frame cost the renderer. PPM output: convert and LOOK before claiming anything
renders. Boot costs ~250 frames; capture below ~300 and you shoot a loading screen.

## The laws (each one paid for)
- **Verify before narrate.** Log lines are not receipts; pixels or it didn't happen.
- **Test design first.** Before every test: the ONE question, what pass and fail look like (a number and
  a frame), VERIFIED preconditions (the build succeeded, the subject is where you think, every clip plays
  on that body, the lens frames it), and a control that differs only in the feature.
- **Judge a body alone, from four angles, in motion** (`driver.py bench`). Never judge a character, a
  garment or a retarget in a two-shot, and never from a still — a T-pose was clean while the ride put
  cloth between her knees.
- **A contact sheet and a full frame disagree → the full frame wins.**
- **A clip's name is not evidence.** Measure first-vs-last frame before believing what it is called.
- **An instrument that lies is worse than none.** A check that could not have failed was not a test; a
  check measuring what your own code placed is circular.
- **If the engine can't do something, build it** (`.claude/skills/build-the-capability`): measure the
  limit before believing it (64 bones was a guess; the hardware said 1023), look for it on another code
  path, build it as a general mechanism, make its failure LOUD.
- **Look at every tile before blessing a sheet.**
- **No compiler warnings, in any way it's built** (`anim_bench/warncheck.py`): a new one fails and says where.
- **A game's own receipt is run in the step that changes its file**, and a step that changes how a set is drawn
  runs the receipt of every game that draws one (`roam`, `tower`, `talk`, `herd`).
- ⛔ **A rebake keeps what's authored** now (clips, morphs and marks ride as chunks after the new body); what
  can't be kept, such as a garment, is listed out loud. Tools that change a finished pack append to it.
- ⛔ **Never scaffold the gitignored dirs** (`packs/ props/ aud/ aud2/ save/ shelf/`): creating them "to set
  up the tree" replaces live content with empty folders while `git status` stays clean.
- ⛔ **A quoted heredoc eats one level of backslash** here: write patch scripts to a FILE. Most repo files
  are CRLF: normalise before matching multi-line anchors, and assert every anchor's count before writing.
- ⛔ **The F310 chase cam** (in `camDirector`) fires before the shot switch whenever the pad drives —
  gate any new mode camera there first.

## Where the knowledge is
`design/engine_plan.md` (the queue of 29 steps: what each built, what each didn't, what was decided),
`design/engine_capabilities.md` (every capability and the failure that exposed it),
`design/pack_format.md`, `design/the_set.md`, `design/platform_tiers.md`, `design/character_standard.md`,
`design/tpose_import.md` (.brres / .dae in, any body into a T-pose, a statue rigged by measured marks),
`design/animation_principles.md`, `animation_authoring.md`, `skating.md`, `cloth.md`, `faces.md`,
`scene_direction.md`, `cinematography.md`, `stop_motion.md`; the skills in `.claude/skills/` (`run-reva`,
`film-craft`, `build-the-capability`); and the first comment of a file, which is where each system says what it
is (`club_net.c`, `gfx_soft.c`, `engine/packcheck.inc`).
