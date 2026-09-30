# THE CLUB — engine + tools (content not included)

One engine (`reva_native/crowd_kms.c`, a single C file, GLES2 via ANGLE on Windows / KMS on a Pi), the
asset pipelines and authoring tools around it (`skate_src/`), the measuring instruments
(`reva_native/anim_bench/`, `skate_src/film/`, `skate_src/robot_neko/`), and the references (`design/`,
`README.md`, `CAMERA.md`, `ANIMATION.md`). Characters, props, levels and audio are NOT in the repo:
the engine prints `FATAL: no packs` until a character is baked into `reva_native/packs/`.

## Build & run (Windows)
- MSYS2 MINGW64 with gcc, SDL2, ANGLE (libEGL/libGLESv2) and stb. ⛔ `mingw64/bin` MUST be on PATH or
  gcc dies SILENTLY with exit 1 and no output: `export PATH=/c/msys64/mingw64/bin:$PATH`.
- `windres crowd_win.rc -O coff -o crowd_win_rc.o` once (the UTF-8 manifest: without it, fopen dies on a
  pack with a non-ASCII name), then
  `gcc -O2 -ffp-contract=off -o crowd_new.exe crowd_kms.c club_math.c club_audio.c club_save.c holly_fbo.c crowd_win_rc.o -Dmain=SDL_main -lmingw32 -lSDL2main -lSDL2 -lEGL -lGLESv2 -lm`
  (`club.h` names the units: the split started 09-30 — the 4x4s and the audio mixer are their own files.)
- Build to `crowd_new.exe`, verify on pixels, THEN copy over `crowd.exe`.
- Run: `REVA_WINDOWED=1 ./crowd.exe 400 packs select` (fullscreen without WINDOWED). ⛔ argv[3] is a
  FLOW (`select`) or a body to load as the hero, never both.
- Headless: `.claude/skills/run-reva/driver.py` (`build`, `lab`, `bench`, `face`, `scene`, `up` + `--live`).
  One engine instance at a time.
- Python: a real Python 3 with numpy + Pillow (`CLUB_PYTHON` points the tools at it). ⛔ Never the
  Microsoft Store `python` alias. ⛔ The Windows console is cp1252: a script that prints a non-ASCII
  glyph dies unless it reconfigures stdout to UTF-8.

## Build & run (Linux, 2026-09-30)
- `reva_native/build_linux.sh` → `crowd_linux`: the SDL platform (`-DREVA_SDL`) on Mesa. `_WIN32` implies
  `REVA_SDL`; without either the file is the Pi's KMS renderer. Deps: `libsdl2-dev libgles-dev libegl-dev
  libstb-dev`; headless adds `xvfb libgl1-mesa-dri` (llvmpipe renders every pass the club has).
- Run: `REVA_WINDOWED=1 xvfb-run -a -s "-screen 0 1280x720x24" stdbuf -o0 ./crowd_linux 400 packs select`.
  ⛔ Without `stdbuf -o0` a piped log is block-buffered and ends at the kill with nothing in it.
- Lab bench: `.claude/skills/run-reva/linux_lab.py <pack> <clip> <outdir>` — one body, the four lab cams,
  a PNG per angle. `driver.py` is still Windows-only (taskkill, crowd.exe).
- ⛔ `holly.hlm` (the venue-screen head) is content: a missing one now logs `NO HEAD MODEL` and the floor
  runs with dark screens instead of dying in `load_model`.

## Build & run (Raspberry Pi 4, 2026-09-30)
- `reva_native/build_pi.sh [kms|sdl|both]`. Two routes onto the glass from the same file: `crowd_kms` (the
  original: DRM master, GBM page flips, js0 pad, the composite CRT) and `crowd_linux` (the SDL platform:
  keyboard, select screens, `REVA_SHOT`; `SDL_VIDEODRIVER=kmsdrm` with no desktop). 64-bit OS,
  `dtoverlay=vc4-kms-v3d`.
- ⛔ The KMS path had been DEAD since 09-11: the audio mixer and scene WAVs are SDL audio and were only ever
  compiled on Windows. SDL is now included on every platform; the KMS build inits it for AUDIO only.
- The DRM card is FOUND (first card that reports a connector; `REVA_DRM=/dev/dri/cardN` pins it) — on a
  Pi 4 v3d and vc4 are two cards and "card1" was a guess. The context is ES3 with an ES2 floor, because
  holly's shaders are `#version 300 es` and its compiler `exit(1)`s; on ES2 the head now declines loudly.
- ⛔ NOT YET RUN ON A PI. Verified here: both paths compile on x86, the SDL path renders, and a
  `-funsigned-char` build (ARM's `char`) renders a pixel-identical frame. The rest waits for hardware.

## The tick, the receipt, the dice, the ladder (2026-09-30 — `design/platform_tiers.md`)
- **The sim runs on whole ticks.** `REVA_TICK=<hz>` (default 60); a frame gets 1..4 ticks from the wall
  time it owes, snapped against vsync jitter, capped. `simStepS()` is the frame's sim step — never a
  constant 1/60 again. `REVA_FIXEDSTEP` is one tick per frame with the tick equal to the step. The stat
  line's `fps`/`work` and the HUD fps read `wall_ms()`; `now_ms()` is the tick clock, always.
- **`REVA_SIMHASH=<every N frames>`** prints `[simhash] tick N exact … coarse …` (and every `REVA_SHOT`
  frame does). Same inputs must print the same hashes; a mismatch is a determinism regression. Build with
  `-ffp-contract=off` (the scripts do) or x86 and ARM disagree on the last bit.
- **`rand()` is the club's own LCG** on every platform (`REVA_RAND=libc` for the library's). ⛔ Any capture
  blessed before 09-30 that has a crowd in it re-rolls.
- **The uninitialised-memory test:** two runs with `MALLOC_PERTURB_=85` and `=170` must print identical
  `[simhash]` lines. They did not, once: the crowd slots were `malloc`'d and three fields were read before
  spawn ever set them. `calloc` the arrays the sim reads; the build matrix runs the A/B on every push.
- **`REVA_TIER=pi|low|mid|high|ultra|auto`** (default `high` = the old look). A rung is the 3D render scale
  (a sub-rect of the post target, upsampled by the post quad), the sun map (boot only), bloom, FXAA and the
  cloth substeps. `auto` measures and moves; never under `REVA_FIXEDSTEP`. The lab always draws 1:1.

## Headless verification instruments (env, all off by default)
`REVA_FIXEDSTEP=<fps>` replaces wall time with one virtual step per rendered frame (and pins the crowd's
seed) — it is what makes any capture comparable. `REVA_SHOT=<frame>` ARMS a capture (`REVA_SHOT_PATH`
must be a WINDOWS path; `REVA_SHOTN`/`REVA_SHOTEVERY` say how many) and ⛔ the engine never exits when
its quota is met — a harness polls and kills it. `REVA_CAMLOCK=1`, `REVA_NOSKY=1`, `REVA_CLEAR=r,g,b`,
`REVA_CLIPAUDIT=1` (limbs vs floor, set and each other), `REVA_NOWMS=1` (the Williams layer off),
`REVA_NONORMAL=1`, `REVA_NOFINGERS=1`, `REVA_NORAGDOLL=1`, `REVA_RDDBG=1`. PPM output: convert and LOOK
before claiming anything renders. Boot costs ~250 frames; capture below ~300 and you shoot a loading screen.

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
- ⛔ **A rebake drops the authoring**: a pack re-baked from its source loses the face marks, morph
  renames, jaw, mouth interior and every clip authored onto it. Tools that change a finished pack append
  to it (`rpack.py`, `dresspack.py`) and keep header field 14 (the morph table).
- ⛔ **Never scaffold the gitignored dirs** (`packs/ props/ aud/ aud2/ save/ shelf/`): creating them "to set
  up the tree" replaces live content with empty folders while `git status` stays clean.
- ⛔ **A quoted heredoc eats one level of backslash** here: write patch scripts to a FILE. Most repo files
  are CRLF: normalise before matching multi-line anchors, and assert every anchor's count before writing.
- ⛔ **The F310 chase cam** (in `camDirector`) fires before the shot switch whenever the pad drives —
  gate any new mode camera there first.

## Where the knowledge is
`design/engine_capabilities.md` (every capability and the failure that exposed it),
`design/animation_principles.md`, `animation_authoring.md`, `skating.md`, `cloth.md`, `faces.md`,
`scene_direction.md`, `cinematography.md`, `the_set.md`, `stop_motion.md`; the skills in
`.claude/skills/` (run-reva, film-craft, build-the-capability, robot-neko — its LESSONS LEDGER).
