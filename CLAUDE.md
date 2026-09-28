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
  `gcc -O2 -o crowd_new.exe crowd_kms.c holly_fbo.c crowd_win_rc.o -Dmain=SDL_main -lmingw32 -lSDL2main -lSDL2 -lEGL -lGLESv2 -lm`
- Build to `crowd_new.exe`, verify on pixels, THEN copy over `crowd.exe`.
- Run: `REVA_WINDOWED=1 ./crowd.exe 400 packs select` (fullscreen without WINDOWED). ⛔ argv[3] is a
  FLOW (`select`) or a body to load as the hero, never both.
- Headless: `.claude/skills/run-reva/driver.py` (`build`, `lab`, `bench`, `face`, `scene`, `up` + `--live`).
  One engine instance at a time.
- Python: a real Python 3 with numpy + Pillow (`CLUB_PYTHON` points the tools at it). ⛔ Never the
  Microsoft Store `python` alias. ⛔ The Windows console is cp1252: a script that prints a non-ASCII
  glyph dies unless it reconfigures stdout to UTF-8.

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
