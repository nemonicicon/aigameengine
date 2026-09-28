# AGENTS.md — for any AI assistant working with this repository

(GPT, Grok, Codex, Claude or anything else. Plain markdown on purpose: a chat assistant that cannot
open the repo can be handed this file and the one brief it needs.)

## What this is

A game engine built with an AI in the loop, plus everything around it for making characters move and
filming them: `reva_native/crowd_kms.c` (one C file, GLES2), animation authoring tools, measuring
instruments, and references. **No content** (characters, levels, audio) is included. Maps: `README.md`,
`ANIMATION.md`, `CAMERA.md`. This engine does not run inside another engine — what travels is the
**animations** (as glTF) and the **camera rigs** (as a spec to port), both prepared for Godot 4 in `godot/`.

## If you are helping a Godot 4 project — start here

| task | read | then |
|---|---|---|
| use a READY character (a rusty robot with the skate moves on his own skeleton and the board animated) | `godot/characters/clanker/README.md` | instance `clanker.glb`, play `z_rm_sk_ride`; a trick = `play("z_rm_sk_kf")` then `queue("z_rm_sk_ride")`; keep the `.import` (the AnimationPlayer's optimizer OFF) |
| use our skate moves and body moves | `godot/ANIMATION_IMPORT.md` | import `godot/animations/*.glb`, retarget with a `BoneMap` on `SkeletonProfileHumanoid`, set loop modes from the table, parent the real board under the `skateboard` node |
| use the cameras (tripod, dolly/crane, track, pan, orbit, the hand-held phone, shake, fisheye, the phone-screen frame) | `godot/CAMERA_PORT.md` — exact math with line numbers into the C | `godot/club_camera.gd` + `godot/club_camera_post.gdshader`, **verified in Godot 4.7.2**: open `godot/` as a project and press F5 (`demo/main.gd` sets every rig up in code); `demo/score_rigs.py` re-measures them |
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
9. **Keep a ledger:** every mistake becomes a one-line rule for next time (`.claude/skills/robot-neko/SKILL.md`
   is fourteen episodes of them).

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
