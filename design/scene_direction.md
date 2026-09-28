# DIRECTING A SCENE — staging, coverage and light for two bodies talking

*(written 2026-08-28, out of THE SPOT — jacob: "lets see a video, a scenario where kaeli
is mad at reva. make this a showcase of your skills. start by writing a script.")*

The conversation system could already speak: two Piper voices with per-line direction,
one mixed wav, a 30Hz mouth envelope driving a real jaw bone, subtitles, blinks and an
over-the-shoulder that cuts on the line (`faces.md`). What it could not do was **act**.
Both bodies stood in `z_upright` for the length of the scene, at one distance, facing
each other, looking at each other, under one lens rule and one lamp — so the picture said
the same thing on every line while the words escalated from a cold open to a walk-out.

A scene is now DIRECTED. The dialogue file carries staging next to the dialogue, and the
engine acts on it.

## The score

`reva_native/scenes/<name>.txt` — one line per line of dialogue:

    speaker[ key=value ...]|text          0 = reva (hero), 1 = kaeli (partner)

| | keys | what it does |
|---|---|---|
| **voice** | `rate` `var` `pitch` | pace, how loose the rhythm is, how far the intonation swings (`say_piper.py`) |
| **time** | `beat` | extra silence BEFORE this line, on top of the uniform gap |
| **space** | `pgap` `pyaw` `pside` | where the partner stands and which way she faces |
| **gaze** | `hlook` `plook` | whether each will meet the other's eyes |
| **lens** | `shot` | `wide` · `medium` · `two` · `ots` · `tight` · `solo` (widest to tightest) |
| **side** | `reverse` | 1 = mirror the lens to the OTHER side of the pair's axis (see below) |
| **jaw** | `jawgain` | how far the `mouth hinge` puppet jaw opens; 1.0 is everything shot so far |
| **angle** | `hgt` | camera height off the speaker's eyeline: − looks up at her, + looks down |
| **body** | `hclip` `pclip` | what each one plays |
| **face** | `hbrow` `pbrow` | −1..1, or `auto` |
| **clock** | `step` | exposures/sec from this line on — STOP MOTION (`stop_motion.md`); changes on the cut |
| **light** | `tod` `tone` | the hour the shot was taken (applied ON THE CUT, via the day curve) and the highlight knee (0 = off) |
| **jump** | `snap=1` | NOT sticky: this line's marks (`hx/hz`, `pgap/pyaw/pside`, slot 2's) LAND on the cut instead of being eased and walked to |
| **voice** | speaker `3` | an off-screen voice (`cast 3=director`): no body, so the lens holds on whoever spoke last and no jaw moves |

Everything except `beat` is **sticky** — a line inherits the last value set, so the file
shows only what changes, and a read of it is a read of the blocking. `beat` is deliberately
not sticky: a pause is an event, not a state.

`compile_scene.py` resolves the stickiness and writes a `stage` record beside every line
in the `.scn`; the engine reads it, cuts the lens and the clips on the line, and **eases**
position and facing toward the new mark over about 0.42s. That easing is the whole
difference between `pyaw=46` as a number and `pyaw=46` as a woman turning her back.

## What the direction is actually for

Four things carry the argument, and none of them are the words:

- **Facing.** She opens the scene at `pyaw=134` — talking to someone without turning
  round. When she finally turns to `180` on "I never asked you to stop", the scene
  changes gear without a single line saying so.
- **Distance.** `1.34 → 1.12 → 1.02 → 0.94 → 0.98` across the accusation, then back out
  to `1.06` when she has said the worst of it, and `1.78 → 2.20` as she leaves.
- **Gaze.** `plook=0` is her refusing to look at reva. It is switched on at the turn and
  off again at the exit, and the moment it goes on is the moment the fight starts.
- **The brow.** The morph LIFTS, so a NEGATIVE weight furrows it (the sampler already
  takes signed weights — it tests `fabsf`). Kaeli runs −0.35 down to −0.90 at the peak,
  then **comes back up to 0.00** for the exit: the anger leaving her face is what makes
  "next show, I'm standing in the back" final rather than heated. Because the key is per
  BODY and sticky, she holds the scowl through reva's lines, which is most of what being
  angry at someone actually looks like.

## The lenses

**A LENS PER SHOT.** jacob's first verdict on the finished cut was *"needs work on
composition, angles etc"*, and the root cause was one number: every shot was taken on
the club's 42-degree vertical, a wide angle chosen to hold a whole dancefloor. On a
conversation that is the wrong glass, and it does three bad things at once — the
speaker's face comes out small, anything near the lens (the listener's shoulder, in
every over-the-shoulder) comes out vast beside it, and the frame is so broad that two
people standing a metre apart float in a lake of black. No amount of moving the camera
fixes a lens choice.

    SHOT_FOV[] = { ots 30, tight 22, two 30, wide 40, solo 30 }

Every distance is then derived from the lens rather than picked: frame height is
`2*D*tan(fov/2)`, a head is ~0.45 club units, so `D = 0.45 / (fraction * that)`.

| | lens | where | head fills | for |
|---|---|---|---|---|
| `ots` | 30° | 2.95 out, 50° round | ~28% | the default; the near shoulder is a foreground edge |
| `tight` | 22° | 2.75 out, 48° round | ~42% | a face; the line that has to land |
| `two` | 30° | 3.20 from the midpoint | — | the room, the body language, who is facing whom |
| `wide` | 40° | 4.60 from the midpoint, aimed low | — | someone leaving |
| `solo` | 30° | 4.10 out, 54° round | ~20% | someone left on their own |

A long lens also **compresses**, which is what finally tamed the foreground: at 22° the
listener sits ~25° off the view axis against a 19° half-frustum, so the shoulder that
used to take a third of the picture is a strip at the edge, where an over-the-shoulder
wants it.

⭐ **FRAME ON THE EYES, NOT THE HEAD BONE**, and aim an eighth of a frame below them,
which is what puts the eyes on the upper third instead of dead centre. The head mark
sits inside the skull, and on two bodies 0.46 apart in height it centres the wrong one.

⭐ **SIZE-MATCHED COVERAGE.** A shot and its reverse are only "the same shot" if the head
fills the same part of the frame — and these two heads are not the same size. Measured
off the rig, in world units, **kaeli's interocular is 0.151 and reva's 0.131**: reva is
a stylised model with a small head, kaeli a realistic one, and the camera had no idea.
The engine now publishes the interocular every frame (both eye marks are already in
world space for the look-at, so the distance is free) and scales the shot distance by
it, so reva is framed 13% closer and the coverage matches on any pair of bodies without
a per-character table.

⛔ **AND THE NOMINAL MUST BE MEASURED, NOT GUESSED.** Hand-computing those interoculars
from the packs' rig units gave a 3x difference — the two packs carry wildly different
scales (reva's IOD reads 0.0719 in her rig, kaeli's 8.295 in hers) and the world scale
I divided by was an estimate. The real answer was 1.15x. The first `IOD_NOM` was a guess
at 0.26, which pinned every shot to the clamp floor and flew the lens halfway into both
of them.

⭐ **`hgt=` IS THE ANGLE.** Camera height off the speaker's eyeline, per line: below it
for the accusation (a low angle is what makes someone loom), above it for the confession
and the last line (a high angle is what makes someone small). It runs −0.26 through
kaeli's peak and +0.28 through reva's, and back to level for the exit.

⭐ **THE SIDE FOLLOWS THE SPEAKER, NOT THE LINE INDEX.** The original coverage alternated
the camera side with `held9&1`, and because `d` and `s` already flip when speaker and
listener swap, alternating on top of that **crosses the eyeline** every time two
consecutive lines share a speaker. Half of this scene is one woman talking twice. One
sign per speaker (`spk==0 ? +1 : -1`) puts every shot and every reverse on the same side
of the axis, which is the entire grammar.

⭐ **LOOKING ROOM.** The lens aims a little PAST the speaker toward the listener, which
pushes her off centre on the side she is looking away from. A face pinned to frame centre
talking to someone off-screen reads as wrong before anyone can say why.

## The stage

A scene **takes the set down**. The club's dressing — lava lamps, orbs, the flame snake,
the electric arcs — floats at head height and crosses both faces; against a conversation
it is debris. The lab's own instruments go with it: the measured floor grid, the footfall
breadcrumbs, the runway marker and the ANIM LAB label are for judging a gait.

What is left is black, and black needs light. The pair gets its own three-point rig
anchored to the two head marks rather than to the hero's feet:

    KEY     mid + s*2.60*side + d*1.10,  up 1.10   warm  (3.10 2.70 2.25)
    RIM     mid - s*3.10*side - d*0.90,  up 1.60   cool  (2.20 2.60 3.60)
    BOUNCE  mid,                         down 2.40 dim   (0.60 0.44 0.36)

The **rim is the one that matters**: with the dressing off, an unrimmed head is a dark
shape on a dark ground and every blink and jaw swing the face rig can do is invisible.
Key and rim sit relative to the camera and flip with `g_scnSide` — re-lighting for the
reverse, on the cut, where nobody can see it happen.

⛔ **THE KEY WAS BEHIND THE SPEAKER FOR A WHOLE PASS.** Written from the pair's axis with
`sg` pasted onto whichever terms looked symmetric, it put the warm light behind whoever
was talking and the cool rim across the camera side — so a face in its own close-up was
lit only by the rim on its own hair. That is exactly how reva's confession came out, and
it read as "her model is dark" rather than as a sign error. **Derive the vectors, do not
pattern-match them:** the camera sits at `+p*of` on *either* speaker (that is what
keeping one side of the eyeline means), and `d` — listener toward speaker — is `a*sg`.
So the key belongs at `+p` (camera side), `−a*sg` (in front of the speaker), and the rim
opposite on both.

⛔ **AND THEN IT BLEW THEM WHITE.** Once the key was actually pointing at the face, the
same intensity was far too much — and a near light blows the FOREGROUND worst, because
`1/(1+dd)` falls off hardest close in. Standing it off from 2.4 to 3.3 and taking a
third off the intensity gives the same exposure on the face with far less falloff across
the depth of the shot.

Subtitles sit on a four-band scrim; a flat `uCol` cannot make a gradient, and without one
the words landed on whoever was standing at the bottom of the frame.

## ⛔ The traps

- ⛔ **THE LENS NUMBERS ARE IN CLUB UNITS, NOT METRES.** `tight` and `solo` were first
  sized the way you would size a close-up on a human-scale set — 0.52 back, 0.46 across.
  The club is about **1.75× metric** (measured: heads sit at y 2.62 and 3.08), so the lens
  landed 0.70 from a head: inside the hair, and inside the **0.8 near plane**. Whole shots
  came back as an empty horizon with a shoulder at the bottom, and not one log line said
  anything was wrong. Derive from what works instead: the over-the-shoulder sits 2.4–3.0
  out and reads as head-and-shoulders, and at a 42° vertical fov the frame is `0.767 × D`
  tall, so a 0.45 head fills 40% of it at D = 1.55.
- ⛔ **THE NEAR PLANE IS THE REAL CONSTRAINT ON A CLOSE-UP.** Any lens closer than 0.8 to
  the listener's head slices her open. With the two of them standing ~1.24 apart there is
  very little room between them, which is why `tight` goes wide around the axis rather
  than close along it.
- ⛔ **REVA NEVER TURNS.** She stands on the lab's heading and only her head aims (clamped
  to 0.62 rad). Every unit of `pside` is therefore a degree that kaeli sits off reva's
  facing, and it comes straight off the close-up — at `pside=0.24` reva played her
  confession in near profile, because the lens angle, the placement angle and her own
  head turn all added up. Keep it near zero until somebody leaves.
- ⛔ **THE CAMERA RUNS ONE FRAME BEHIND THE SCENE CLOCK.** `camDirector` is called at the
  top of the frame loop and the clock advances near the bottom, so frame N draws the
  setup of the line that was active on frame N−1. It is 42ms at 24fps and invisible —
  but a contact sheet that steps the scene clock at 2fps makes it half a second, and
  three frames in a row got read as "the camera framed the wrong woman" before that was
  obvious. **Judge framing one frame per LINE, at its midpoint plus one**
  (`anim_bench`-style helper `perline.py` in the scratch bench does this); a fixed
  time grid keeps landing on the frame just after a cut.
- ⛔ **A SILENCE BELONGS TO WHOEVER JUST SPOKE.** `scnActive()` returns −1 in the gaps, so
  both the camera and the staging hold the last line rather than snapping to a default.
  Without that the 1.25s beat after the confession would have cut to nothing.
- ⛔ **`\n` DOES NOT SURVIVE A QUOTED HEREDOC HERE.** Patch scripts written with
  `<<'PYEOF'` came out with one level of backslash eaten, so a `f.write("...\n")` was
  silently written into the file as a real newline inside a string literal. Write patch
  scripts to a file instead of piping them, or build the escape with `chr(92)`.

## Where it goes next

- **Overlaps.** Real arguments interrupt. The mix schedules every line end-to-end, so
  "Don't." can only follow the line it is cutting off. A negative `beat` would do it.
- **A camera that moves.** Every shot here is locked. A slow push on the confession is
  one lerp away and would be the single biggest addition.
- **More bodies to play.** reva2 carries 12 clips and most are pair moves, so she stands
  through the whole scene. That is defensible for R.E.V.A. — the stillness is the
  character — but it is a constraint that got promoted to a choice.
- **The stop-motion variant** (`stop_motion.md`) is the same scene on a stepped clock.

---

## THE CONVERSATION LEAVES THE BLACK STAGE (2026-08-29, Z5)

`the_spot` was directed on a deliberate void, which was right for two people arguing in the
dark. SEVENTY-FOUR is fifteen minutes of people talking **in a street**, so the
scene system had to meet the set renderer.

⭐ **They already coexisted.** Proven on pixels before a line of code was written:
`scene=the_spot set=parsons` played its lines, drew its subtitle, and rendered all 500
props and 11 actors behind it. The only thing wrong was that the pair stood where the lab
parks them (0, −150) and the street was somewhere else.

⭐ **ONE MARK MOVES EVERYTHING**, so nothing in the two-body system had to be generalised:

    ctl  stage=x,z,yawDeg      stage=off

The hero's model matrix is built from `g_rvx / g_rvz / g_rvHead`; the partner is placed
RELATIVE to that matrix; and the scene camera frames the two HEAD WORLD POSITIONS that fall
out of it. Move the hero and the second body, the eyelines, the shot sizes and the camera
all follow. *That is why a stage mark beat refactoring hero/partner into an array — the
array would have rewritten the system that produced THE SPOT, for a film that needs it.*

⛔ **It must be applied AFTER the lab's floor clamps** (`g_rvx` to ±FLOOR_X, `g_rvz` to
2.0), which otherwise drag the pair straight back off the street they were staged on.

The driver carries it: `driver.py scene OUT.mp4 --name porch --set parsons --stage
"30,-20,0" --sun "46,204,1.25,1.0,0.96,0.90" --sky "0.46,0.52,0.64"`.

### Blocking a scene that is NOT an argument

`the_spot` is two people square to each other with the gap closing line by line. `porch`
had to read as its opposite, and the staging keys say so directly: `pyaw` stays near **96**
(a quarter turn, not a confrontation), `plook=0` for most of the scene, and `shot=two` or
`wide` so the STREET is the third thing in frame. They sit shoulder to shoulder pointing
the same way. The one time kaeli turns her head — `pyaw=142 plook=1 shot=tight` — is the
line where she stops humouring the count.

**Still owed:** there is no porch. They stand on the verge because the set has no steps
placed yet, and the film's `0:40` is explicitly *"REVA on kaeli's porch steps"*.

---

## ⛔ SHE HAS TO WALK TO HER MARK

*(jacob, on the first take of `denial`: "already i see characters clipping through each
othere, very unantural sliding movement from kaeli instead of taking steps... needs work
and you know it." Both were real and both were mine.)*

**THE SLIDE WAS BUILT IN.** Per-line staging eases `pgap` / `pyaw` / `pside` toward each
line's marks over 0.42s, and the partner's model matrix is built straight off those three
numbers. So a line that moves her from gap 1.85 to 1.60 TRANSLATES her half a metre in
four tenths of a second while she plays a standing idle. That is not a bug in the ease; it
is the ease with no locomotion attached. It never showed on `the_spot`'s black stage
because there is no ground to slide against.

**THE FIX:** measure her real world speed from the ease each frame, and above 0.22 units/s
play a WALK instead of the staged clip — her own (`w_walk` / `walking`), or borrowed off a
bank through the same canonical transfer the zombie clips use — with the phase running at
`gaitRate()`, the club's existing "how far does this clip's planted foot travel" measure.
Mixed in and out over 0.18s so the hand-off never pops. Measured working: speed peaks at
**0.621**, mix reaches **1.00** on kaeli's own `w_walk`, and it eases back to 0 when she
arrives.

⛔ **AND A CONTACT SHEET CANNOT JUDGE THIS.** A walk and a glide look identical in stills;
the repo's own law says animation is verified in motion. `REVA_STAGEDBG=1` prints speed,
mix and which clip she borrowed, and a strip of *consecutive* frames shows the feet
alternating. Both were needed — the sheet showed nothing either way.

### A measured separation floor

Nothing stopped the two bodies overlapping: a script could name any `pgap` it liked, and
`denial` asks for 0.95 in its tight. The floor is now measured from both bodies rather
than chosen — half a shoulder span each plus a hand's margin:

    [stage] separation floor 0.95 world units (shoulders 0.67 + 0.55, margin 0.34)

⛔ **It took two goes to measure that honestly.** The first read `0.37 + 0.11`: the hero
was measured through FK world matrices while the partner was taken from her clavicles'
LOCAL rest translations, which are offsets inside the chest's frame and not a span at all.
And **clavicles are the wrong bones** — 0.37 world units is 0.21 m, half a shoulder,
because a clavicle starts at the sternum. The canon map's upperarm bones (7 and 10) are
the shoulder joints. Both bodies now measure themselves from their own FK.

⛔ **THE CLAMP IS SCENE-ONLY, ON PURPOSE.** The lab path must keep no floor: the authored
contact moves need to get closer than any conversational minimum (`z_kiss` negotiates to
0.573). `lab_pair` stays bit-identical in the regression, which is what proves it.

**Also staged out:** two background neighbours were pacing the near verge — the same lane
the porch scenes are staged in — so a man in a denim jacket crossed frame and occluded the
pair mid-line. Background belongs across the road.

### ⭐ hmark= — the hero on furniture (09-11)

Only the partner slots could take a `mark`, so reva could not sit on a bed: the hero was placed by
`stage=` and took her Y off the ground. `hmark=<name>` (scene line, or ctl `hmark=`/`pmark=`/`p2mark=`
for the lab) puts slot 0 on a set mark exactly as `pmark=` does slot 1 — world x/z, the mark yaw, the
mark HEIGHT, the hip drop for a borrowed lay, and it beats the colliders. `off` hands her back.

A mark now carries a CONTACT: `mark <name> x y z yaw [hips]`. Default (feet) puts the rest mesh
lowest point at the height — right for lying and standing. `hips` pins the PELVIS bone there — a
seat: feet dangle below the edge. `sets/kaeli_house.set` `bedside` is reva on the edge of the bed.
A/B on the A camera: promoted binary = reva standing in the middle of the mattress; this build =
seated on the near edge, kaeli under the covers behind her. Log: `[hmark] hero on ... contact hips`.

⛔ The ctl `clip=` path only ever asked the moves pack; scenes resolved `hclip` through every bank.
It now falls through to `bankFind` (`[lab] clip -> bank ...`), so `clip=x_chairsat` works from the lab.

---

## ⭐ FOUR MORE KEYS, AND A SOLO THAT IS A SOLO (ROBOT NEKO ep 4, 09-11)

Every one of these is a per-line key on a `stage` record, written by `compile_scene.py` out of
`scenes/<name>.txt` and read by the engine's scene parser — the same road `snap=` and `tone=`
travel. All four are **sticky**.

### `shot=medium` — the hips-up two-shot

The ladder is now **wide → medium → two → ots → tight/solo** (`SIZE` in
`skate_src/film/shotgrammar.py` ranks it there too). *Default: the scene's last `shot=`.*

`two` and `wide` pick a distance off the lens and then CHECK the top of frame. A medium is
defined by **both edges at once**, so it is solved rather than chosen:

* the **bottom** edge sits at the lower of the two **measured** hips (`ATT_HIP`, the pelvis
  bone's own world matrix, already published every frame for the held props) minus a hand of
  thigh — which is what puts hands, and anything in them, large in frame;
* the **top** edge clears the higher head, or the higher **raised hand** if either body has one
  up (the same published hand marks the head-room guarantee uses);
* the distance falls out of that height on a 34° lens, and is then pushed back if the pair are
  too far apart to fit ACROSS the frame (their axis is square to the view, so their separation
  is frame *width* — which needs the aspect ratio, now published as `g_aspect`).

It publishes `g_scnWant` like `two`/`wide`, so the stop-motion tripod still lets go when the
frame has to grow. `[lens] line N medium: neither body published a hip …` if it has to guess.

### `reverse=1` — cross the line on purpose

*Default 0.* The conversation camera always sits on ONE side of the pair's axis; that is the
eyeline grammar above and it is correct. `reverse=1` mirrors it to the other side, so who is on
screen-left swaps and a different background comes up behind them. `reverse=0` returns.

It is one sign (`sd9`), which carries the camera position, the looking room and the
over-the-shoulder offset together — **and the key light crosses with it** (`g_scnRev` flips the
rig's `p` vector), or the reverse would be lit from behind. Log: `[lens] line N reverse=1`, once
per change. This is a deliberate jump, not a fault: nothing else about the eyeline rule moved.

### `shot=solo` — the listener's point of view

`solo` used to be a two-shot wearing a different lens: 4.10 out and 54° round, with the
listener's profile down half the frame. The comment in the code admitted it, and the reason it
could not be fixed by moving sideways is recorded there — the angle that has to clear the
half-frustum is measured from the view axis, and the view axis is already swung toward her.

So the lens **stands where she is**: at the listener's own eye mark, 0.10 behind it and 0.17 to
one side, looking at the speaker's eyes. She is behind the lens, which is the only place a body
is reliably out of frame.

⛔ **The near plane is what removes her, and it only works from close in.** A scene renders on a
0.30 near plane; her face runs about 0.25 behind her eye mark to 0.18 in front, so a lens 0.10
behind the mark has all of her inside 0.30 and the clip takes her head and chest whole. Standing
further back "to be safe" does the opposite — her skull leaves the clip and fills the frame
again. Nothing in this engine is back-face culled, so being *inside* her head is not an option
either. 0.10 is a measurement, not a taste. A hand she reaches toward the speaker DOES cross
0.30 and appears, which is what a point of view should do.

The solo lens is **38°** now (it was 30): at a pair-gap's distance a head does not fit on the 30
with head room, and the slight wide-angle is what "nearly into the lens" looks like. Head room
is still guaranteed, and a raised hand still grows the frame.

Logs: `[lens] line N solo: listener behind the lens` once per change; and when the geometry
runs out it says which number did — `… the speaker is inside the 0.30 near plane, so no clean
single exists; using ots` (they are standing on top of each other), or `… the frame is only
X tall on the 38 deg lens and the speaker's head will not fit — aiming at her eyeline`.

⚠ This changes every existing `shot=solo` in the film. `update` is the one shot scene that uses
it today.

### `jawgain=<f>` — the puppet jaw's opening

*Default 1.0* — what every scene shot so far used. `mouth hinge` drops the viseme track, which
is what leaves the jaw swinging on loudness alone, and on a loud line that hinge opens further
than a puppet's ever would. This scales it. Log: `[jaw] gain 0.50`.

It is also a **ctl key** (`jawgain=`), and a scene STARTS on the ctl's value the way `step=` and
`tone=` do. It does nothing on a scene that has a viseme track — there the shape is the
performance — and it says so rather than going quiet:
`[jaw] … but this scene has a VISEME track`.

### And the loud failures

* An unknown `shot=` name used to fall to `SHOT_OTS` without a word — a typo came back as "the
  default coverage". It now prints `[scene] line N: unknown shot 'x' -- using ots`, and the name
  match is anchored at both ends, so `twofold` is not `two`. `compile_scene.py` refuses the
  whole file before a take is ever paid for.
* `SHOT_FOV` was five entries long and every read of it is `SHOT_FOV[shot&7]`. It is eight now.

### ⭐ A HELD PROP THAT IS A LIGHT

    hprop=v1_town_lantern@0.42@0@lhand@FFD9A0@light        (power 5, the default)
    hprop=v1_town_lantern@0.42@0@lhand@FFD9A0@light:9      (power 9)

A sixth field on the held-prop spec makes the thing in her hand a **practical**: a point light
at the exact world position `drawHeld` puts the mesh, coloured by the prop's own `RRGGBB` tint,
lighting the bodies, the set, the actors and the faces like any other practical — and the prop
itself draws **emissive** (a new `uEmis` uniform on both body fragment shaders: full tint, no
sun, no sky, no practicals), so it reads as the source rather than as the darkest thing in the
frame. Shaded normally it would be exactly that: the light sits *inside* it, so every one of its
own normals faces away.

⛔ **The shader has six practicals and a set could claim all six.** A set now takes `NLIGHT-1`
of them and held lights fill DOWNWARD from the top, never below what the set claimed — so there
is always at least one, and one that will not fit says so. (`NLIGHT` has been 6 since Z6; the
`[set] light cap is 3` message had been stale ever since, and one of the five upload sites was
still sending the literal 3 of 6, so a set's fourth, fifth and sixth lamps reached the shader as
whatever was in those slots last.)

Logs: `[held] slot N 'stem' is a light (power P)` once per change, and
`… is a light but REFUSED: no attach point published` when there is no grip to hang it on.

The whole spec is parsed in ONE place now (`heldParse`), shared by the mesh and the light — two
parsers of one string is how a light ends up in the other hand. The prop fields grew from 32 to
64 characters with it: `v1_town_lantern@0.42@0@lhand@FFD9A0@light:6` is 42, and a 32-byte field
truncated the tail in silence.

And `hprop=` / `pprop=` / `p2prop=` are **ctl keys** now, and held props draw outside a scene, so
a prop — or a light — can be put in a hand and looked at from the lab in one line.

### ⛔ AND A LENS COULD ONLY EVER BE SAID BY A .scn

Every framing in this document is scene-only by construction — the solver is gated on two
published head marks — so a new shot size could not be LOOKED AT without authoring and voicing
a scene for it, which is against this repo's first law. Two ctl keys override a loaded scene's
own lens on every line:

    shotsize=wide|medium|two|ots|tight|solo|auto     `auto` hands it back to the script
    reverse=0|1|auto                                 same shape

⛔ **Not `shot=`** — that is the SCREENSHOT key and has been since 08-25. `shotsize=` is the
framing. Both print `[lens] ctl: …`, and an unknown name is refused by name rather than
falling to OTS.

## ⭐ THE FILMER, THE PHONE AND THE FALL (the skate promo, 09-19 → 09-20)

*(jacob: "i want reva to be filming ... the majority of the episode should be the camera feed
that reva is holding. the game we are working on, it is about capturing clips, filming your
friends skating." Four promo cuts came out of it -- `sk_promo`, `ep2_lines`, `ep3_session`,
`ep4_london` -- and four keys. Takes on `D:/reva902/robot_neko/promo/`. All of it runs on
`crowd_new.exe`; `crowd.exe` has not been promoted since 09-11.)*

### `camhold=filmer,subject[,fov[,aimh[,drop]]]` — the camera in a body's hand

The lens rides the filmer's WRIST (the contact pass publishes every body's forearm ends each
frame; the far end of the right forearm is the grip hand), sits a little in front of the fist
toward the subject and an arm's length clear of her chest, and aims at the subject's `aimh`.
`drop` is units below the hand: 0.55–0.62 is a phone by the knee as the skater comes past (the
LOW PASS). ⛔ **THE BOB IS THE SHOT.** The eye is raw and only the aim is sprung (9 rad/s --
quicker than a dolly, still late): springing the eye smooths away the very thing that makes it
read as footage (`feed_test`'s FAIL case, "a locked-off frame"). The filmer's own body is not
drawn. One lens mode per line; any other lens said on a line takes the camera out of her hand,
and `camhold=off` hands it back. Log: `[lens] line N camhold: slot F is filming slot S`.

⭐ **THE FILMER'S DISTANCE IS THE SHOT.** A foot behind kaeli on a fisheye reads as a knee, and
the first take came back as one; at ~3 u behind, a fisheye holds a whole body with the park
behind it. And the filmer RIDES: `z_rm_sk_film` (one arm out holding the camera, watching the
shot and not the road) on her own path beside or inside the subject's.

### `feed=0|1|2` and `fish=0..1` — whose camera this is, and the glass on the front of it

`feed=1` puts the shot inside her phone: portrait 9:16 in the delivery frame, a lens fall-off,
sensor noise, a blinking REC dot and a running timer. `feed=2` is the same phone turned
sideways (16:9). `feed=0` is a camera on a TRIPOD -- full frame, `camshake=0`, `fish=0`.
`fish=` is an equidistant fisheye as a projection (r = f·θ): 0.42–0.50 is the VX / Century
skate-video look. Both sticky, both land on the cut, both turn the post pass on (they ARE the
post pass). ⛔ A fisheye is a WIDE lens as well as a bent one: under `fish=` the 3D pass opens
its field toward 120° diagonal, so the corners have real geometry to bend; warping at the
authored fov moved the field six degrees and looked like nothing.

THE CAMERA RULE (ep 2 onward): every shot is a camera that EXISTS in the place -- a phone in
somebody's hand or a tripod left standing -- and the script names which.

### `hdoll= / pdoll= / p2doll= / p3doll= <sec>` — the fall is physics

That many seconds into the line the body goes LIMP: the skeleton becomes the ragdoll (18 linked
points, her own pose, the speed she actually had, the floor and walls she is on --
`engine_capabilities.md` §22), and `off` hands her back to the clip. NOT sticky. ⛔ **It needs a
body that is MOVING**: the cue waits up to 2 s for her to roll and is loud about a body at
rest ("this fall has no momentum in it"). ⛔ **`snap=1` lands EVERY body on its sticky mark** --
a snap that cuts the hero to the trick lane parks the partner back on her opening mark and ends
her lap, and twice the slam came out "carrying 0.00 u/s". One snap at the top, then everyone
rolls from shot to shot on paths. ⛔ **A path is ONE run while its value holds** -- re-stating
`ppath=@loopB` on the next line does not start a new lap; to send a body somewhere else, name
different marks.

### ⛔ ONE RUN, MANY CAMERAS

Ep 2 gave each shot its own short path, so she rode for four seconds and stood still for three
while the shot played out -- which is exactly what "static" looks like. A line is ONE continuous
path down the whole yard and the CAMERAS change over it (ep 3, ep 4): that is how a line is
filmed. A trick going AWAY from the camera is a speck: the lens waits at the far end of the lane
and she rides at it, slowly enough that the cut comes while she is still short of it (at 3.6 u/s
she ate the lane in two seconds and the kickflip happened inside the lens). And a missing
partner clip is a T-POSE for the whole line (ep 3: half the episode) -- the engine now boxes the
first miss and counts them at exit.

### ⭐ `hpath=@around:<slot>[,<radius>[,<points>]]` — a route round a body (ROBOT NEKO ep 13)

A closed circle of marks round where that slot IS right now — its hips, when it is a ragdoll on the floor —
entered at the point nearest this body, run the way she is already travelling, and rebuilt round the body's
current position every lap (`hloop=1`). Built for the filmer circling her fallen friend: a fixed route has to
know where a slam will end, and two takes of the same fall came to rest 8 u apart. Pair it with `campan=<slot>`
on the tripod that has to hold the body wherever it lands. The body keeps the mark it was placed by; a synthetic
mark name went through the hero's placement once and parked reva at z −150 with her phone filming the horizon.

⛔ **A PATH HANDED ON EARLY IS THAT LINE'S PATH.** When a path runs out before the next path's line, the next
one starts early — and it now takes ITS OWN line's `hspd=` and `hloop=`, pinned against the lines in between.
Before ep 13 it ran at the old speed without the loop, lapped once and stopped for good, because a stopped run
with the same value never restarts. The log says `starts early: rolling in at 6.60 u/s, to that line's 2.40 u/s,
looping`. So a filmer's line may simply END near the subject, and her orbit begins the moment it does.

⭐ **A TALKING FILMER, A BODY THAT TALKS FROM THE FLOOR.** `feed=1` + `camhold=` carry dialogue: her voice and
subtitle play over her own footage (subtitles land after the post pass, sharp across the phone's black bars) and
her body is never drawn; a body cued limp with `pdoll=` speaks through nine lines and stays down. `aimh` 1.1 for
a rider (a fisheye bends the top of the frame away from a body close by), 0.5 for a body on the ground.
