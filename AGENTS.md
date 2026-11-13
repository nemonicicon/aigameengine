# AGENTS.md — for any AI assistant working with this repository

(GPT, Grok, Codex, Claude or anything else. Plain markdown on purpose: a chat assistant that cannot
open the repo can be handed this file and the one brief it needs.)

## What this is

A game engine built with an AI in the loop, plus everything around it for making characters move and
filming them. The engine is `reva_native/crowd_kms.c`, one translation unit over `reva_native/engine/*.inc`.
A frame is plain data first, and three things take it: the Vulkan renderer, the engine's own rasterizer
(no graphics driver, 320 x 240, the same pixels on every machine), and a no-display build that only hashes
it. The games that prove it are in `reva_native/games/` (one C file each against `engine.h`), and one game
can be played on two to four machines. Then the animation authoring tools, the checks and the references.
**No content** (characters, levels, audio) is included; the games need none. The licence is MIT (`LICENSE`),
and `NOTICES.md` says what in a build is somebody else's. Maps: `README.md` (written for people: its
Quick start builds and plays a game in two commands), `CLAUDE.md` (the build, the switches and the laws), `design/engine_plan.md`,
`ANIMATION.md`, `CAMERA.md`. This engine does not run inside another engine — what travels to Godot is the
**animations** (as glTF) and the **camera rigs** (as a spec to port), both prepared for Godot 4 in `godot/`.

## If you are making a game on this engine — start here

| task | read | then |
|---|---|---|
| start a game | `reva_native/engine.h` (everything a game sees of the engine), then `reva_native/games/sample/sample.c` (TAG, 3D, one body built by code) or `games/rocks/rocks.c` (2D, no bodies) | a game is a folder in `reva_native/games/` with one C file of the same name: `python .claude/skills/run-reva/driver.py buildvk --game <name>`, then its `play.bat` |
| know a game still keeps its own time | `CLAUDE.md`, "The tick, the check, the dice, the ladder" | `driver.py check --game sample`: the game against itself, the same hashes at odd frame pacings, no window, seconds. Nothing is stored or blessed. A new game adds its scenarios to `reva_native/anim_bench/hashcheck.py` |
| play it across machines | `reva_native/games/rally/rally.c`, and the first comment of `reva_native/club_net.c` | `REVA_NET=host` on one machine, `REVA_NET=join:<address>` on the others. ⛔ A game's tick never reads anything of the session. `python reva_native/anim_bench/netcheck.py` is the check |
| run it with no graphics driver | `README.md`, "No graphics driver?" | `driver.py buildsoft --game <name>` |
| show the person a picture, or play the game yourself, with no window | `CLAUDE.md`, "Headless verification instruments" | the `buildsoft` build with `REVA_NOWINDOW=1` and `REVA_SHOT`, and the bridge (`REVA_BRIDGE`, `reva_native/anim_bench/bridge.py`) |
| hand it to someone | `README.md`, "A game to hand to someone" | `python reva_native/package.py --game <name>` |
| put it on an Android phone | `reva_native/android/README.md` | `python reva_native/android/make_apk.py --game <name> --image IMAGE --sdk-volume SDKVOL --sdl-volume SDLVOL`, in a container the user provides; the APKs build, and none has been started since the scripts took their present form: say so |

## If you are helping a Godot 4 project — start here

| task | read | then |
|---|---|---|
| use the READY character (R.E.V.A. 902: the skate moves on her own skeleton, the board animated) | `godot/characters/README.md` | instance `reva902.glb`, play `z_rm_sk_ride`; a trick = `play("z_rm_sk_kf")` then `queue("z_rm_sk_ride")`; keep the `.import` (the AnimationPlayer's optimizer OFF); hair physics: `ClubHair.attach(instance)`; an outfit: `ClubWardrobe.attach(instance, "<outfit>")` |
| use our skate moves and body moves | `godot/ANIMATION_IMPORT.md` | import `godot/animations/*.glb`, retarget with a `BoneMap` on `SkeletonProfileHumanoid`, set loop modes from the table, parent the real board under the `skateboard` node |
| use the cameras (tripod, dolly/crane, track, pan, orbit, the hand-held phone, shake, fisheye, the phone-screen frame) | `godot/CAMERA_PORT.md` — exact math with line numbers into the C | `godot/club_camera.gd` + `godot/club_camera_post.gdshader`, **verified in Godot 4.7.2**: open `godot/` as a project and press F5 (`demo/main.gd` sets every rig up in code); `demo/score_rigs.py` re-measures them |
| build or change a CITY map (Seattle, from OpenStreetMap and USGS elevation) | `skate_src/citymap/MAP_GUIDE.md`, its part 2 is for you; then `skate_src/citymap/README.md` | download the data outside the repo, `python -I skate_src/citymap/citymap.py <config> --raw <data> --out <scratch> --godot <folder>`, drop `SM_<name>.glb` and `.park.json` into the game; config first, code second; never Google Street View |
| bring a Godot scene, or a Godot shader, INTO the engine | the first comment of each file in `skate_src/godot/` | run `engine_export.gd` in Godot (`godot --headless --path <project> -s <path to engine_export.gd> -- <res://scene.tscn> <out folder> [name]`), then `python skate_src/godot/godot2set.py <out folder> <name> <game folder>` writes a set and a pack; it calls `godot_shader.py` for the scene's shader materials, and what can't be carried is said and drawn with its plain material; `reva_native/godot/gd_math.h` is Godot's math types in C, for game code brought across. They were written against one project: expect to extend them |
| check a result | `godot/CAMERA_PORT.md` §5 | record with Godot's Movie Maker (`--write-movie out.avi --fixed-fps 60`), `ffmpeg -i out.avi out.mp4`, run `skate_src/robot_neko/mp4steps.py`, `heldiff.py`, `scenesheet.py`, `sfxcheck.py` on it — they read any mp4 |

Suggested order: (1) import the animations and get `z_rm_sk_ride` looping on a character with the board
under her feet; (2) the tripod and the tracking lens; (3) the hand-held phone rig on a filmer's hand bone;
(4) shake; (5) the fisheye and the phone frame; (6) the orbit and the dolly moves.

## How this engine was built — the rules (each one cost a real failure)

1. **Verify on pixels.** A log line is not a receipt; look at a full frame of the thing before saying it
   works. When a contact sheet and a full frame disagree, the full frame wins.
2. **Design the test first:** the ONE question; what pass and fail look like (a number AND a frame);
   preconditions VERIFIED, not assumed (the build succeeded, the object is where you think, the clip plays
   on that body, the camera frames it); a control that differs only in the thing tested. If a test could
   not have failed, it was not a test.
3. **Judge a character alone, from four angles, in motion** — never in a group shot and never from a still.
   A garment was clean in the T-pose and put cloth between her knees the moment she crouched.
4. **A clip's name is not evidence.** An `idle` was a 184-frame get-up; a `cheer` was a walk in place.
   Measure what a clip does before casting it.
5. **When a fix does not change the picture, A/B one layer at a time.** Four one-shot A/Bs (skin hidden,
   normal map off, shadows off, caps removed) killed four wrong theories about one artefact before the
   fifth found it.
6. **An instrument that lies is worse than none.** A check measuring what your own code placed is circular;
   a checker that flags correct work gets ignored. Keep the tools honest and say when one was wrong.
7. **If the engine cannot do something, build it** — measure the limit first (a "64 bone" cap was a guess;
   the hardware said 1023), look for it already existing on another code path, build it as a general
   mechanism, make its failure LOUD.
8. **Two rounds on a stubborn pose, then change the staging.** Animation can eat unlimited time; a
   contact that keeps moving is a pose that should change.
9. **Keep a ledger:** every mistake becomes a one-line rule for next time. The ⛔ lines in `CLAUDE.md` and
   `design/engine_capabilities.md` (every capability, with the failure that exposed it) are this engine's.
10. **A check doesn't say anything is right.** `check` holds a game against itself, not against what it did
    yesterday: the engine isn't finished and keeps changing. How the picture looks is still eyes on a full frame.

## Paste-ready starter (for a chat assistant that cannot read the repo)

> I'm using camera rigs ported from a custom C engine in **Godot 4** (GDScript). Below is the exact spec
> (CAMERA_PORT.md) and the implementation (club_camera.gd, club_camera_post.gdshader), verified in Godot
> 4.7.2 by a demo scene. Help me wire it into my own scene and characters, then give me:
> a minimal test scene (a moving target on a path, a tripod, a tracking camera and a hand-held camera
> parented to a `BoneAttachment3D`), and the steps to record it with Movie Maker at a fixed 60 fps so I
> can check that the hand-held eye bobs while its aim lags, the tripod holds still, and the orbit arrives
> exactly on its end heading. Keep the rule "the bob is the shot": spring the phone's AIM, never its EYE.
>
> *(then paste CAMERA_PORT.md, club_camera.gd and club_camera_post.gdshader)*

For the animations: paste `godot/ANIMATION_IMPORT.md` and ask for the retargeting steps on your own
character's skeleton.
