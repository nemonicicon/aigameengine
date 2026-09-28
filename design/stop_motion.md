# STOP MOTION — the Robot Chicken mode

*(banked 2026-08-28. jacob, watching the CLUB NIGHT conversation on the new face rig:
"lmao it kinda looks like robot chicken" → "i kinda dig it, maybe bank it as a special
mode")*

An accident worth keeping. The first talking-face pass read as stop-motion animation —
action figures on a tabletop — and jacob liked it enough to want it on purpose.

## Why it looked like that

This matters more than the observation, because it means the look is **reproducible on
demand** rather than something to be recaptured by luck. Four things produced it, and we
know all four:

1. **The jaw is a hinge driven by loudness alone.** The scene envelope is a single
   amplitude per frame, so the mouth opens and shuts on volume with no lip shaping — no
   closure on /m/ /b/ /p/, no rounding on /o/ /u/, no spread on /i/. A puppet's hinged
   jaw does exactly this.
2. **The eyes do not move.** Neither club woman can aim an eyeball — reva's are painted
   and kaeli's are flat lid/lash shells (see `faces.md`) — so the gaze is carried by the
   head alone. Dead eyes over a moving mouth is the single strongest puppet cue.
3. **The bodies are nearly still.** Both were standing in `z_upright` with only a head
   layer over the top; no weight shift, no gesture, no breath at conversational scale.
4. **The frame rate reads as stepped.** Clips are sampled nearest-frame with no
   interpolation, so an 18fps bake held against a 60fps render already steps.

## The lever we now have

`REVA_FIXEDSTEP=<fps>` (added 08-28 for the pixel regression) advances the whole engine
clock by a fixed step per rendered frame. **That is the stop-motion dial.** Render at 60
and step the animation clock at 12–15 and the whole world moves in the stepped cadence
Robot Chicken has, deliberately and reproducibly — while the camera itself stays smooth,
which is exactly how the show looks.

So the mode is mostly *composition of things that already exist*:

    REVA_FIXEDSTEP=12        the stepped cadence
    jaw on the raw envelope  keep the hinge, do NOT add visemes here
    look=0                   no gaze tracking; the head snaps between poses instead
    crowd clocked out        the standing law already benches the crowd outside DANCER

## What it would need to become a door

- **A step rate that is a MODE parameter, not an env var** — and applied to the animation
  clock only, so the lens keeps moving smoothly. A camera that steps too reads as a
  dropped frame, not as stop motion.
- **Pose snapping**: hold each animation frame and cut, rather than crossfade. The
  crossfade layer (`rvXf`, ~0.35s) is the enemy of this look and wants a bypass.
- **A tiny bit of jitter** — real stop motion has the animator's hand in it. A per-hold
  sub-degree wobble on the root would sell it more than any amount of polish.
- ⛔ **Do not fix the faces for this mode.** The hinged jaw and the dead eyes are the
  look. Anything done to make the talking faces more believable (visemes, eye bones, a
  gaze layer) has to be switchable OFF here.

## Where it sits

A variant rather than a door, most likely — the same worlds and characters, played on a
stepped clock. `MODE_ARCADE` already carries cabs as variants and is the closest
precedent. Mode select is the spine: every new game is a door or a variant, never a
bypass.

## BUILT (2026-09-10) — the exposure clock

*(jacob: "yes robot chicken style, show me what you got!")*

⛔ **`REVA_FIXEDSTEP` could not be the dial after all.** It steps the WHOLE engine, the scene
clock included — so rendering at 24 with the bodies at 12 put the picture at double speed
against the wav. The mode needed a second clock, which is what this section's "applied to
the animation clock only" was asking for.

⭐ **Two clocks.** `now_ms()` is now the EXPOSURE clock: under ctl `step=<fps>` it advances
only in whole steps (floor, never round — at 24/12 the pattern is hold, expose, hold,
expose), so every frame in between re-exposes the same instant: dt is zero, clip phases
hold, staging eases hold, set actors and their cues hold. `now_raw_ms()` is the lens's clock
and `camDirector` alone is handed it, so the camera stays smooth. The scene clock keeps its
own fixed step, which keeps the wav in sync; the jaw, head drift, brows and blinks read
`scnBodyT()`, the scene clock latched once per exposure. `step=0` is on ones and changes
nothing: `now_ms()` IS `now_raw_ms()`.

⛔ **Verlet does not care about dt.** A held frame has dt 0, the chain solver clamps it to
1e-4, and the inertia term keeps the hair swinging between exposures while the body stands
still. Chains and cloth skip integration on a held frame (the write-back still runs).

⛔ **AND NEITHER DOES A FEEDBACK LOOP** (ROBOT NEKO ep 6). The rule above was written for the
chains and the cloth, and the FACE was never brought under it. Not for a clock reason — the
head layer has run on `scnBodyT()` since ep 1 — but because `lookTarget()` aims each head at
the OTHER body's **published eye mark**, and that mark is republished out of last frame's FK.
One rendered frame is one more iteration of that solve, so a held exposure goes on converging
a pose the shutter has already photographed: a fraction of a degree at the neck, which on a
close-up is a visibly crawling hairline. So the face solve is **latched to the exposure that
owns it** — on a held frame `scnHeadLayer` replays that exposure's own result verbatim, joints
and morph weights both, which catches the gaze loop, the eye aim, the eye shapes, the jaw and
the joint boil in one statement. Keyed **per cast slot** (`g_castI`), never per `who`: the
function is called with `who=1` for every cast member, so a `who`-keyed cache is ep 2's
kaeli-and-Parsons collision wearing a fix's clothes. `REVA_NOHOLDLATCH=1` is the control run,
and the latch announces itself (`[hold] slot N: face solve replayed on … held exposure
frame(s)`) so a control take can be told from a dead feature.

⚠ **Measured honestly: on today's bodies this changes nothing.** Re-shooting ep 5's own scene
with the latch OFF gives held pairs of **0.01–0.24%**, against the delivered ep 5's
**1.57–2.88%**. The residue lived in the OLD BAKED PACKS — reva and kaeli were rebuilt from
source between the two takes — and the rebake is what removed it. The latch is kept because
the reasoning is right and the loop is real, not because it was shown to help.

⭐ **`step=` is a STAGING KEY** — per line, sticky, and it changes on the cut like the lens.
A scene starts and ends on the ctl's rate. **`mouth hinge`** in a script drops the viseme
track (the recipe above: jaw on loudness alone). **`boil=`** (ctl, default 1) is the
animator's hand: each exposure lands the hero and each partner a hash-seeded ±0.006 rad and
±0.005 units off their marks, so a held frame holds its error too and a take repeats.

⭐ **`anim_bench/stepcheck.py`** is the receipt: the same street through a PINNED lens,
consecutive frames, once at `step=12` and once as a control at `step=0`. First run: every
held pair **0.000%** changed, every exposed pair 0.04–0.06%, period 2; the control moves on
every pair. (The lens is pinned on purpose — a smooth camera would make every pair differ
and hide whether the bodies held.)

**Still owed from the list above:** the crossfade bypass (pose snapping) and a variant
door. The first thing shot on it is `scenes/twelve.txt`.

### ⭐ ROBOT NEKO, episode 1: TWELVE (2026-09-10)

*(jacob, on the first showing: "hey thats funny, and honestly? i like this. thats episode of
robot neko")* — the mode has a show now. **ROBOT NEKO** is the name; TWELVE is episode 1.

54 seconds, `D:\reva902\sketch\twelve.mp4`. Reva and kaeli notice they are on twelve frames a
second ("Dancers get sixty. Leads get twelve. The crowd gets a photograph."), and the button is
the mode itself: a car alarm, the neighbours turn, `step=24` lands on the cut and the zombies
charge in the first smooth motion of the piece — then `step=12` again on "Put us back on
twelve." Set `sets/twelve.set` (parsons + nine neighbours with cues and paths), mark
`34,-17.8,270`, kaeli cast as `anima_outfit`, reva on `z_upright`.

**Receipt, off the delivered mp4** (% of pixels changed between consecutive frames, >10/255):

| t | step | pairs |
|---|---|---|
| 6.0 | 12 | 9.3 · 2.8 · 10.6 · 1.9 · 9.0 · 1.0 · 8.5 · 4.1 — alternating |
| 49.5 | 24 | 4.1 · 3.2 · 4.9 · 7.2 · 6.9 · 4.6 · 3.5 — every pair moves |
| 53.1 | 12 | 0.0 · 5.2 · 0.9 · 6.1 · 1.0 · 6.8 · 0.5 — alternating again |

(Held pairs are not zero in a scene because the conversation lens is still easing — which is
the rule: the camera is not a puppet.)

⛔ **What shooting it exposed, none of it stop-motion's fault:** the film's mark stands reva in
a bench; `x_convo1/2` bow reva2's head onto her chest; the two/wide lens cut the taller head
(now a guarantee — `cinematography.md`); `anima_fx` is the nude fitting base. jacob's eyes are
owed on the sketch.

### ⭐ ROBOT NEKO, episode 2: CONTINUITY (2026-09-10)

*(jacob: "this should be the kind of cartoon that gets better everytime, as you are developing
your skill and tools")* — so the episode shipped with its pipeline: `.claude/skills/robot-neko/`
(the process and a LESSONS LEDGER) and `skate_src/robot_neko/` (title card + sting, foley,
per-line contact sheet, on-twos checker).

Built for it: `tod=` / `tone=` / non-sticky `snap=1` as scene keys, an off-screen slot-3 voice,
boil in the joints, sfx resampled to the scene rate, loud held-prop refusals, `@lhand`, prop
tints, **held sizes that are world sizes** (every held prop had been drawn at a tenth of its size),
and — the ones that decide whether it LOOKS like stop motion:

- ⭐ **The tripod lens.** Under stop motion the conversation lens settles 0.45s after a cut and then
  holds unless a body travels. It had been chasing eye marks every frame, which turns a
  twelve-a-second image into a moving one.
- ⛔ **State shared between bodies.** With the lens locked the bodies still moved on held frames.
  Every ease in the Williams layer holds at dt=0 — the bug was ONE `static WmsState` (and chain
  state sized for two) used by every partner, so slot 1 and slot 2 overwrote each other. Found by
  A/B (`REVA_NOWMS=1`), not by reading. Per cast slot now; the film's three-handers had it too.

Receipt, off the delivered mp4 (% pixels changed between consecutive frames): 16s
`9.32 · 0.23 · 9.03 · 0.19 · 7.52 · 0.10`, 46s `7.92 · 0.24 · 7.58 · 0.25 · 5.60 · 0.24` —
held pairs under half a percent. Clipped highlights 0.0% (ep 1: 25–41%).

### ⭐ ROBOT NEKO, episode 3: UPDATE (2026-09-10)

*(jacob, mid-episode, with her original Meshy files: "revas model got messed up at some point. her
shoulders, her texture, somthing is not right" — and, on the first A/B, "reva on the right is
looking MUCH better". On the delivered episode: "coool i like it".)* So the episode became her firmware update. reva had never moved because her
rig really was broken; everyone's voice comes out of her locked body until "Update complete.", and
the payoff is the one move a broken shoulder could not make — both arms over her head.

Built for it:
- ⭐ **reva rebuilt from source** (`skate_src/reva2/rig_reva.py`): the recipe had hung her arm joints
  UNDER her arms (the widest slab is her fingertips, not her arms), placed the shoulders about the
  bounding box, dragged her head joint back with her ponytail (every borrowed clip: chin on chest),
  and cut the texture to 1024. Now measured landmarks, 100k tris, 4K, heat weights on a voxel proxy;
  face marks, jaw and mouth interior re-derived (`design/faces.md`). `x_convo` on the new rig: head up.
- ⭐ **`voice=<person>`** per line (`say_kokoro.py`, not sticky): another person's voice through her
  jaw. `stranger` (bm_george) is the voice nobody in the cast has heard.
- `update_chime` (`foley.py`): a two-tone device blip whose second note stutters.
- `moves/z_armsup.py`: arms overhead, held, hands waving — cast after the 8fps take showed that
  `hclip=auto` gestures read as nothing on a wide.
- ⭐ **Head room clears RAISED HANDS, and the tripod lets go when the frame must grow.** With the
  arms-up clip cast, the wide still cut her hands off at the forearm: head room measured the eyes,
  and the tripod had locked 0.45 s after the cut, before her arms were up. Two-line fixture
  `scenes/armsup_test.txt`: `top raised hand 3.81 needs the frame to 4.11 -- distance 5.62 -> 6.69`,
  hands in frame from ~0.5 s after the raise.
- `skate_src/robot_neko/verify.py`: the delivered-file checks (twos, clipped %, a frame sheet, is each
  sfx audible) in one command.
- ⭐ **`driver.py up` / `--live`**: one engine window kept up and steered through the ctl file
  (jacob: "why do you have to open and close reva everytime?"). Boot 23 s, then 4–5 s a shot.
- A writers' room of subagents: three Sonnet drafts (stock animation / nature documentary / voice
  swap) and a Sonnet punch-up of the chosen one around the real rebuild; Haiku took the prop, clip
  and sfx inventory and traced her pack's provenance. Engine work, GPU takes and every visual
  verdict stayed in the main session.

Receipt, off the delivered mp4 (`verify.py`, exit 0; 77 s, `D:\reva902\robot_neko\episodes\ep03_update.mp4`):
consecutive-frame change at 6 s `4.95 · 0.17 · 19.82 · 1.36 · 5.54 · 0.05`, at 58.5 s (the payoff)
`1.85 · 0.05 · 1.85 · 0.02 · 2.59 · 0.03` — on twos throughout (70 s is the lens settling after a cut,
then `EhEh` again); clipped highlights 0.00% in all three channels at 1.3 / 20 / 45 / 58.5 s; all three
update chimes PRESENT (0.40 of the spectrum at 880/1318 Hz against ≤0.02 just before). The payoff in
the log: `[headroom] line 15 wide: top raised hand 3.67 ... distance 5.64 -> 6.33` and `[tripod] line 15
... easing out` — her hand is in frame from 58.2 s.

## Status

Idea banked with its recipe. Nothing implemented. jacob's word: "i kinda dig it, maybe
bank it as a special mode" — a maybe, not a commission.
