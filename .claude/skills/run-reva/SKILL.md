---
name: run-reva
description: Build, launch, screenshot and drive THE CLUB (reva) — the Firestrider game engine in reva_native/crowd.exe. Use when asked to run, start, build, compile, test, screenshot, capture, record, shoot or verify the club/reva/the game/the engine/the anim lab/the face rig/a set/a scene/the film, or to check whether a character, garment, animation, mode, set or face change actually renders.
---

# Run THE CLUB (reva)

One C file (`reva_native/crowd_kms.c`, ~11k lines) compiled to a fullscreen GLES2/ANGLE
game. **It has no CLI surface** — no `--version`, no headless mode, no test runner that
proves anything about what you'd see. Every answer it has comes out as pixels.

So the harness is a screenshot driver: **`.claude/skills/run-reva/driver.py`**. It
builds, launches, drives the menus / the lab / a set / a scene via a control file,
captures frames, tiles four-angle contact sheets, and records a scene to MP4 with audio.

All paths below are relative to the repo root (`C:\MyVeryOwnVideoGames\reva`).
Platform: **Windows 11, native** — not a container, not WSL. There's a real GPU.

## Prerequisites

Already installed on this machine; verified present this session:

```bash
ls /c/msys64/mingw64/bin/gcc.exe
/c/Users/jacob/AppData/Local/Python/pythoncore-3.14-64/python.exe -c "import PIL; print(PIL.__version__)"
```

`ffmpeg` (WinGet, `Gyan.FFmpeg`) is only needed for `scene`. The driver finds it itself.

⛔ **Use that absolute python.** Bare `python` is the Microsoft Store alias: it shadows
APPDATA and poisons results. Set it once per shell:

```bash
PY=/c/Users/jacob/AppData/Local/Python/pythoncore-3.14-64/python.exe
D=.claude/skills/run-reva/driver.py
```

## Build

```bash
$PY $D build
```

~20s, writes `reva_native/crowd_new.exe`. Warnings are filtered; only real errors print.
Once you have **looked at pixels** and they're right:

```bash
$PY $D promote
```

That order is the repo's law: `crowd.exe` is what `dance.bat` and any live window run.

## Run — the agent path

Every command leaves a PNG or MP4 and tells you to look at it. **Look at it.** The
engine's law is "pixels or it didn't happen"; a clean exit code proves nothing.

### 1. Judging a BODY — start here, not with a wide shot

```bash
$PY $D bench /tmp/body.png --subject kaeli_pj --clip idle --secs 5 --cols 6
```

Four angles (side / front / 3-4 / low) × N time columns, **one body, nothing else in
frame**. Rows are angles, columns are time. This is the read that matters for a
character, a garment, a retarget or a pose, and it is the first thing to reach for.

jacob, watching me judge a garment off a two-shot with a second character in it:
*"you need to isolate kaelie and the pajamas to get a clear read on them. use the multi
and framing we have been using. comeo n now, this is our test bench."* One sheet then
settled six rounds of guessing and exposed five separate bugs in a row.

⭐ **The control run.** `--env REVA_NOCLOTH=1` shoots the same body with the cloth solver
off. Comparing the two separates "the simulation is wrong" from "the mesh is wrong" in a
single run — it answered in one shot what four rounds of tuning had not. Reach for a
control run *before* tuning anything.

`bench` writes `reva_native/sets/_bench.set` itself (overwritten every run) and calls
`anim_bench/ridecheck.py`. `--host` is the lab's hero, parked off-camera at z −150; it
is **not** the subject. `--clip` may name a clip in *any* loaded bank — `bankFind`
resolves across packs, so a citizen can play a zombie clip.

### 2. The club, the lab, a set

```bash
# THE CLUB — boots mode select -> variant -> character select -> play
$PY $D shot /tmp/club.png --mode 0 --frame 320
```

```bash
# THE ANIM LAB — one or two bodies, a control line, your choice of camera
$PY $D lab /tmp/lab.png --subject r2moves \
  --ctl "face=0 grid=4 dqs=0 clip=z_kiss partner=kmoves pclip=z_kiss hi5=3 aimy=1.5" \
  --grep hi5
```

```bash
# A SET — the world. Same command; the ctl carries the level, the lighting and the mark.
$PY $D lab /tmp/street.png --subject r2moves --frame 480 \
  --ctl "face=0 grid=0 dqs=0 set=parsons stage=30,-20,0 partner=kaeli_pj pclip=idle pgap=1.4 pyaw=150 sun=40,150,1.2,1.0,0.96,0.90 sky=0.44,0.50,0.62"
```

`reva_native/sets/*.set` is the level format — `prop`, `propr`, `actor`, `path`, `cue`,
`light`, `mark`, `look`, `include`. `design/the_set.md` is the manual. `stage=x,z,yaw`
stands the scene pair inside the set and the camera follows them.

⭐ **KEEP ONE WINDOW UP for a run of lab/face shots.** jacob, watching a window open and close
for every frame: *"why do you have to open and close reva everytime?"* — it never had to. Every
`lab`/`face` call boots a fresh engine (~1 min through 140+ packs), while ctl `shot=<path>` has
always grabbed the next frame of a RUNNING window and kept it running.

```bash
$PY $D up --new --subject r2moves          # boot once (measured 23 s); it answers a probe shot
$PY $D lab /tmp/a.png --live --new --subject r2moves --ctl "face=0 grid=0 dqs=0 set=_bench cam=3.4,2.7,0,0,2.6,0,26"
$PY $D face /tmp/f.png --live --new --subject r2moves --jaw 0.7     # 4-5 s a shot
$PY $D down
```

`--live` relaunches by itself, and prints why, when the live window cannot answer: none is up,
the subject/exe/`--env` differ, or **any pack or set prop on disk is newer than the boot** (a
running window keeps the bodies it loaded, and caches set props by NAME). A rewritten `.set` is
picked up by sending `set=off` first — the engine only loads a set whose NAME changed. `--settle`
(2.5 s) is the pause between the ctl line and the grab. ⛔ `bench` and `scene` still boot their
own engine (their captures are armed by env vars at launch) and `kill` every window first, the
live one included.

⭐ **A SET FROM EVERY CAMERA IN ONE BOOT: `looks`.** jacob (10-22): *"we should batch more work into
builds to save on overall build time"* — a five-camera look had cost five boots (~2 min). `looks`
reuses or boots ONE live window, grabs every camera (~1.5 s each after the first), and writes
`<out>_<name>.png` per camera plus a labelled `<out>_sheet.png`. Batch the other way too: several
patches per `build`, one bake per batch of builder changes, one scene take per batch of fixes.

```bash
$PY $D looks /tmp/ship.png --new --set space_mayflower --metres --light "shadow=0" \
  --cams "q34=160,70,1332.9,0,0,1112.9,40;side=-468,97,1102.9,0,0,1102.9,40"
```

```bash
# THE FACE RIG — 8 angles + mouth and eye cameras, face marks DRAWN IN
$PY $D face /tmp/face.png --subject r2moves --jaw 0.7 --panel mouth --zoom 1.6
```

```bash
# A CONVERSATION SCENE, recorded with its audio muxed in
$PY $D scene /tmp/talk.mp4 --name porch --seconds 60 --fps 24 --set parsons --stage 30,-20,0
```

Scenes live in `reva_native/scenes/` (12 of them: `porch`, `denial`, `coldopen`,
`the_spot`, `boarding`, `three_lawns`, `it_starts`, `tables`, `dogwalk`, `theleash`,
`club_night`, `mouthtest`). `<name>.txt` is the script — dialogue, voice direction and
**staging**. `say_piper.py` speaks it; `compile_scene.py` mixes one wav and writes the
`.scn`. `design/scene_direction.md` is the manual. The scene system seats **three**
speakers (`--partner2`, `p2gap=`/`p2clip=`/…).

⛔ `--crop` is a 2x centre crop, for when the framing is not deliberate. A scene that
carries its own `shot=` direction should be shot full-frame.

### 3. The film

`skate_src/film/shoot.py` shoots SEVENTY-FOUR — every scene twice (an A
conversation lens and a B lens pointed at what the scene is *about*) and cuts to an EDL.

```bash
$PY skate_src/film/shoot.py --dry            # what it would do
$PY skate_src/film/shoot.py --only denial    # one scene, both cameras, cut
```

Its checkers: `framecheck.py` (is the camera pointed at them, and are they big enough in
frame), `scenecheck.py` (motion energy per take, dead stretches), `emptycheck.py`.

### The control line

`lab` and `face` write `reva_native/anim_bench/lab_ctl.txt` and the engine polls it, so
this also steers a **running** window.

| key | effect |
|---|---|
| `clip=<name>` | what the hero plays (`packs <name>` lists clip names) |
| `partner=<subject>` + `pclip=<clip>` | a second body — **both are required** |
| `pyaw= pgap= pside=` | where the partner stands and which way she faces |
| `set=<name>` | load `reva_native/sets/<name>.set` |
| `stage=x,z,yaw` | stand the scene pair in the set; ⛔ applied AFTER the lab's floor clamps |
| `cam=ex,ey,ez,ax,ay,az[,fov]` | place the lens explicitly |
| `sun=elev,azim,intensity[,r,g,b]` · `sky=r,g,b` | lighting |
| `tod=<hours>` + `todrate=` | a whole day on one curve (night is a weak blue directional, **not** `sun=off`) |
| `face=10` | the face rig (implies `grid=10`) |
| `grid=N` | multi-cam wall, N panels |
| `jaw=0..1` / `-1` | park the mouth open / hand back to the scene |
| `look=0/1` | the listener's gaze at the speaker |
| `dqs=0` | force LBS skinning (DQS balloons reva2's shoulders) |
| `hi5=1..4` | print a two-body contact gate (1 hands, 3 kiss, 4 massage) |
| `scene=<name>` | play `scenes/<name>.scn` (its staging then owns clip/pgap/pyaw/look) |
| `aimy=` | close-up aim height |

⛔ **The engine only re-reads the file when `seq=` CHANGES.** The driver stamps a fresh
`seq` per write; hand-edit it and you must bump `seq` yourself or nothing happens.

### Instruments (env vars, all off by default)

| var | what it answers |
|---|---|
| `REVA_FIXEDSTEP=<fps>` | a virtual clock, one step per rendered frame — makes runs comparable and pins the crowd RNG |
| `REVA_CLOTHDBG=1` | the cloth sim's bounding box vs the skinned pose it hangs on |
| `REVA_NOCLOTH=1` | cloth off — **the control run** |
| `REVA_PDBG=1` | the partner's world position, for framing her |
| `REVA_CAMLOCK=1` · `REVA_NOSKY=1` · `REVA_CLEAR=r,g,b` | measurement lens · ground the flyers · impossible backdrop |
| `REVA_STAGEDBG=1` | is the partner *walking* to her mark or gliding to it |

Pass any of them with `--env K=V`.

### Direct invocation — the authoring tools

Most character changes are Python, not C, and don't need the engine. These read and
rewrite `.rpack` files in place (append-and-repoint; each writes a `.bak`):

```bash
$PY skate_src/author/face_marks.py reva_native/packs/v1_r2moves.rpack --report
$PY skate_src/author/rpack.py info reva_native/packs/v1_kmoves.rpack
$PY skate_src/author/makepajamas.py        # authors + skins a garment against a pack's rest skeleton
```

`--report` measures and prints without writing — always run that first. Others:
`jaw.py`, `mouth_interior.py`, `fingers.py`, `subdivide.py`, `morphs.py`, `retarget.py`,
`autoweights.py`, `makeprops.py`.

## Run — the human path

```bash
cd reva_native && ./dance.bat        # full flow: mode select -> variant -> character
cd reva_native && ./skate.bat        # straight to the skate bench (⛔ see Gotchas)
```

A window opens and stays open. `ESC` is the start menu. Useless for verification — you
can't screenshot it from here — but it's what jacob watches. To leave one running on his
screen, launch it in the background with `REVA_WINDOWED=1` **and pass
`dangerouslyDisableSandbox: true`** or he won't see the window. Headless capture through
the driver does *not* need that.

## Test

There is no unit-test suite for the engine. The bench instruments are the tests:

```bash
$PY reva_native/anim_bench/facecheck.py                # the face, incl. the see-through test
$PY reva_native/anim_bench/posturecheck.py <log>       # is it STANDING, or merely still
```

⛔⛔ **`shotsweep.py` IS RETIRED — DO NOT RUN IT.** jacob, 10-18: *"the strict regression sweep is HOT GARBAGE
it cant tell you ANYTHING and should never be used again in its current state."* To show a change stayed where
it belongs, run a **control** on the body it could have touched — the change's own switch off (`--env
REVA_NOTOON=1`, `--env REVA_CHCOLLIDE=0`, `REVA_NOCLOTH=1`…) against on, on a `bench` sheet — and look.

(What it was, for the record:) ⛔ **`shotsweep.py` measures SAMENESS, not correctness.** It proves you didn't change
what you weren't touching. It cannot tell you a garment is a sack or a walk is wrong —
only your eyes on a `bench` sheet can. jacob: *"why do i find the pixel regression to be
pointless... just fuckin look at some of your subjects."* Both are needed; don't mistake
a green sweep for a good frame.

Animation is verified **in motion, never from a still** (a standing law in `CLAUDE.md`).
Use `bench`, or a burst: `--env REVA_SHOTN=12 --env REVA_SHOTEVERY=3`.

## Gotchas

- ⛔ **`gcc` is not on PATH.** The login shell's `/mingw64/bin` entry doesn't resolve, so
  `gcc` is simply absent. `CLAUDE.md` also warns it can die *silently* with exit 1 and no
  output. Use `driver.py build`.
- ⛔ **`gcc ... | head` reports HEAD's exit status, not gcc's.** A failed build reads as
  `EXIT=0` and you then measure a stale binary. Use `${PIPESTATUS[0]}`, or `driver.py build`.
- ⛔ **A QUOTED HEREDOC (`<<'PYEOF'`) EATS ONE LEVEL OF BACKSLASH.** `\\n` arrives as a
  real newline and writes broken C/Python. Hit again this session while patching this very
  driver. **Write patch scripts to a FILE** (Write tool), or build strings with `chr(10)`.
- ⛔ **Most repo files are CRLF.** A multi-line anchor written with `\n` matches nothing.
  Normalise to LF, patch, write back as CRLF — every patch script here does this.
- ⛔ **`python3` is the Store stub too.** Like bare `python`, it is the Microsoft Store alias -- and in a background
  shell it never returns: a two-file syntax check sat waiting six hours (10-22). Only the absolute python.
- ⛔ **`driver.py scene` gives every scene the partner kmoves.** A scene with one person passes `--partner off`.
- ⭐ **Read a take's LOG before its frames**: `STUCK` (a walk the furniture held, placed), `NOT FOUND`, `standing body
  out of obstacle` (the contact pass moved a body), a `[walk] ... -> mark` with no `arrived`, `[set] actor N ... wears`.
  Then `skate_src/robot_neko/scenesheet.py MP4 SCN OUT.png` -- one frame per line -- and LOOK at every tile;
  `reva_native/scenes/join_chapter.py` joins a chapter's takes in order with an index of where each scene starts
  (`--take`/`--script`/`--label` name the files by `{n}`; its docstring has chapter 2's whole line).
- ⛔ **Judge a POSE in profile, at eye level, beside a standing body.** From a high wide I reported seated extras
  as standing and a fix was asked for; a profile lens showed all seven sitting on their stools (10-23).
- ⛔ **On Windows, `subprocess` resolves the exe from the PARENT's PATH**, not the `env=`
  you pass. Name `gcc.exe` absolutely.
- ⛔ **`crowd_win_rc.o` is not optional** — the UTF-8 manifest; without it `fopen` dies on
  the ninjatō pack, whose filename is non-ASCII.
- ⛔ **argv[3] is a FLOW, not a body.** `crowd.exe 400 packs select` runs the menus;
  `crowd.exe 400 packs r2moves` loads that body as the hero and skips them. Get it wrong
  and `REVA_SHOT` fires on a frame the capture path never reaches: **no file, no error.**
- ⛔ **`REVA_SHOT` is what ARMS a capture.** `SHOTN`/`SHOTEVERY` only say how many and how
  often. And **the engine never exits when its quota is met** — a harness must poll for
  the frames and then kill it.
- ⛔ **The engine needs a WINDOWS path for `REVA_SHOT_PATH`.** A `/c/...` path gets
  `shot FAILED to open ... (a native exe needs a Windows path)`.
- ⛔ **Only one instance at a time.** Two fight over the GPU and the ctl file. The driver
  kills stragglers; `$PY $D kill` if one is stuck.
- ⛔ **Boot costs ~250 frames** (140+ packs, ~400 dancers, donor retargeting). Capture
  below ~300 and you shoot a loading screen. Default is 420; a set wants ~480.
- ⛔ **`partner=<name>` alone loads a body and draws nothing.** No `pclip=`, no partner —
  and her face marks are never published, so the conversation camera silently no-ops.
- ⛔ **`REVA_SCENEFPS` alone fixes only the SCENE clock.** Clip phases still run on `dt`,
  so the picture and wav stay in sync while the *bodies* animate at capture speed. Set
  `REVA_FIXEDSTEP` to the same rate (`driver.py scene` does).
- ⛔ **A close-up's numbers are in CLUB UNITS** — ~1.75x metric (heads at y 2.62/3.08),
  near plane 0.8 outside a scene. A lens placed at human-scale close-up distance ends up
  *inside* the head and the frame comes back empty with nothing logged. Derive distances
  from a shot that already works.
- ⛔ **An untextured mesh takes its colour from `#rrggbb` in its MESH NAME.** Every draw
  path must switch to the tint path for it; three of the four (hero, multi-cam, actor,
  partner) had this and one didn't, and a blue garment rendered as an invisible
  alpha-discarded ghost. If authored geometry is missing, suspect the tint path.
- ⛔ **A contact sheet and a full frame can disagree — THE FULL FRAME WINS.**
  `zombiecheck.py` twice reported clips broken that a full frame showed were fine.
- ⛔ **Run it from a NORMAL shell.** In a scrubbed environment (`env -i`) ANGLE cannot
  create its D3D11 device: `No available renderers` → `SDL_CreateWindow: Could not
  initialize EGL`. The driver reports only `NO FRAME WRITTEN`; re-run with `--verbose`.
- ⛔ **The skate PARK is broken** for every body on disk, and `skate.bat` names packs that
  do not exist. Only the ACTOR skate path works (`design/skating.md`).
- ⛔ **`crowd.exe 0 packs select` SEGFAULTS** — a zero-dancer crowd is unguarded.
- **The lab stage is dark.** Face work photographs badly; brighten in post rather than
  concluding something failed to render.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `gcc: command not found` | Not using `driver.py build`. |
| Build "succeeded" but nothing changed | `\| head` swallowed the failure — `${PIPESTATUS[0]}`. |
| `SyntaxError: unterminated string literal` in a patch script | Heredoc ate a backslash. Write the script to a file. |
| A patch anchor matches 0 times | File is CRLF; normalise to LF first. |
| `NO FRAME WRITTEN at ...` | Wrong argv[3] flow, `--frame` below ~300, or a crash. `--verbose`. |
| `shot FAILED to open ... (a native exe needs a Windows path)` | `REVA_SHOT_PATH` was POSIX. |
| Ctl changes do nothing | `seq=` didn't change. |
| Partner missing from a two-body shot | No `pclip=`. |
| Authored geometry invisible, sim reports fine | Tint path — see Gotchas. `REVA_CLOTHDBG=1` to confirm it's simulating. |
| `--grep` finds nothing | It's a regex now; it used to be a plain substring, so old patterns with `\|` silently matched nothing. |
| `no crowd.exe -- run driver.py build first` | Build, then `promote` (or pass `--new`). |
| `no frames for angle ... -- is sets/<n>.set present?` | `ls reva_native/sets/` |
| Black or garbage PNG | Another instance was running: `$PY $D kill`, retry. |
| `Could not initialize EGL` / `No available renderers` | Scrubbed environment — use a normal shell. |

## Where things are

```
reva_native/            crowd_kms.c (the engine), packs/ (140+ .rpack bodies),
                        sets/ (19 .set levels), scenes/ (12, dialogue+wav+.scn),
                        anim_bench/ (16 instruments), dance.bat / skate.bat
skate_src/author/       .rpack authoring (marks, jaw, interior, garments, props)
skate_src/film/         shoot.py (SEVENTY-FOUR) + its checkers
skate_src/town/         the UE asset artery (bake_town.sh, texaudit.py, propdims.py)
design/                 one doc per mode + the_set.md, scene_direction.md, faces.md,
                        skating.md, seventy_four.md, ROADMAP.md
CLAUDE.md               the standing laws — read it before changing anything
```
