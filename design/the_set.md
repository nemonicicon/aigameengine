# THE SET — the club grows a world

*(written 2026-08-29, out of SEVENTY-FOUR. The conversation stage is deliberately
a void, which was right for two women arguing in the dark and useless for a street.)*

⭐ **This is the level renderer.** The film is what forces it to be real; the door it
opens is a game that runs on the same files.

---

## THE ARTERY, again

`skate_src/town/{ue_export_town.py, bake_town.sh}` pull AssetsvilleTown out of
`D:\Unreal projects\ZAMNC` through UE 5.8 headless — the same shim-project pattern that
took 16 weapons through in ~40s. **112 meshes and 15 textures in about two minutes, 0
failures.** Ground tiles, the wall/roof/door/window kit, 34 yard and street props, trees,
six vehicles, and enough interior for the night the windows get boarded. 124 prop rpacks
(the wall modules bake once per siding colour).

**Scale: the meshes come out in real-world METRES.** Measured off the GLBs — a mailbox is
1.42 tall, a wall module 3.00 × 4.00, a sedan 5.40 long, a road tile 16 × 16 — and the
club runs 3.15 units per 1.8m body, so `--scale 1.75` lands the whole town at true size.

### ⛔ Three texture traps, and the first two returned an EMPTY FRAME

Every diagnostic said success: the set loaded, `drawSet` ran with valid uniforms and
correct matrices, every pack reported drawable. Nothing rendered.

1. **THE ALPHA IS NOT OPACITY.** The mesh shader alpha-tests
   (`if(useTex>0.5 && tx.a<0.4) discard`), and this kit uses the palette's alpha channel
   as a mask for something else — **59% of its 2048×2048 pixels sit below that
   threshold**. The palette did not tint the town, it *deleted* it.
2. **AND THE V IS UPSIDE DOWN.** glTF puts the UV origin top-left, GL puts it
   bottom-left, and `glb2rpack.py` writes no flip flag. On a normal texture that is a
   visible mistake; on a palette ATLAS it is an invisible one — the meshes simply sample
   a different swatch. MEASURED on `SM_mailbox_1`'s own UVs: **mean luminance 8.6 as
   stored, 124.8 flipped.** The town was rendering black on a black stage, which looks
   exactly like not rendering at all.
3. **ONE PALETTE IS NOT ENOUGH.** Props are palette-UV'd, but walls UV into tiled siding
   sheets and foliage into leaf sheets. Give a wall the palette and the house wears the
   swatch grid; give a tree the palette and it wears confetti.

---

## THE SET FILE

`reva_native/sets/<name>.set`, loaded with ctl `set=<name>`:

    prop  <stem> x y z yaw scale [r g b]
    actor <pack> <clip> x y z yaw scale [rate] [speed] [runlen] [board]

⭐ (10-23) `outfit=<name>` anywhere on an actor line is what that actor WEARS (one of its pack's wardrobe
outfits; a name it lacks is refused and the outfits it has are listed). Without it an actor wears its pack's FIRST
outfit -- which put Billy in his freezer shift beside Freddy in day clothes (WATEOT 2.22). Each actor keeps its own
worn pieces, so two actors on one pack can dress differently.

A stem resolves as `props/v1_town_<stem>` → `props/<stem>` → `packs/<stem>`, so a BODY is
placeable the same way a mailbox is. An optional r,g,b replaces the texture with a flat
tint, which is how a kit of grey primitives becomes a painted lawn.

Deliberately dumb — no culling, no batching, no LOD. A street is a few hundred draws of a
few hundred triangles, which this engine does not notice, and **the shape of the FILE is
what has to be right**: it is the level format.

⛔ **A set instance must honour the pack's own scale and `mny`.** A prop rpack is baked at
club scale and reports 1.0; a CHARACTER reports the factor from its rig units to the
world. Placing one through the prop path put a fifty-unit woman on the lawn.

### street.py

`skate_src/town/street.py` GENERATES the street, because the kit ships four houses and a
hundred modules to make the rest from. The grid, measured off the exported GLBs at club
scale:

    ground tiles   28 x 28, origin at a corner: x 0..28, z -28..0
    sidewalk        7 x 7
    wall modules    7 wide along its own -Z, 5.25 tall, thin in X
    roof tiles      7 x 7, CENTRED
    fence 7.3 long · mailbox 2.5 tall · utility pole 14 · tree_02 21.8

Yaw follows the club's convention (forward is +z), so a wall at yaw=90 runs along −X from
its origin. That one fact is most of the house builder. 5 lots = ~510 instances.

---

## ACTORS

`actorDraw()` is `labDrawPartner`'s chain — rest pose, clip tracks over it, FK, skin, draw
— with the hero-relative placement and the face/head layers taken OUT. Background actors
do not talk, and keeping them off that path means **the two-body conversation system the
film's dialogue depends on is not touched at all**.

`speed`/`runlen` give a straight run along the actor's own facing that wraps, which is
what a skater going down a block is and the cheapest useful path there is. Phases and
travel are staggered per actor or a row of them moves in lockstep like a chorus.

⛔ **TWO PALETTE BUGS, same shape — a mesh drawn with a palette not built for it:**
- `drawProp` uploaded ONE identity matrix. Correct for a katana, which is rigid and
  weighted entirely to bone 0; hand it a 52-bone character and every vertex on any other
  bone reads an unset uniform and folds to the origin. **The mesh comes back as a star of
  triangles radiating from a point**, which is the unmistakable look of exactly this.
- And the palette is `world × INVERSE BIND`, not the world matrix. Skip the inverse bind
  and every vertex sits at its bone's origin.

---

## LIGHT

    ctl  sun=elevDeg,azimDeg,intensity[,r,g,b]   sun=off
         sky=r,g,b
         cam=ex,ey,ez,ax,ay,az[,fov]             cam=off

**No arrangement of POINT lights can stand in for a sun** — the club's falloff is
`1/(1+dd*0.055)`, so a lamp far enough away to read as sunlight delivers 2% of its
brightness. `uSun.w == 0` keeps the club's old dark-room constants exactly, so every
existing shot is bit-identical and the regression stays green.

A sun also means a **sky**: `clearOverride` paints the backdrop from the sky ambient,
lifted. Black above the horizon is right for a club and wrong for a street, and night is
just `sun=off`. Elevation and colour on the scene clock is the film's whole day-to-night
arc.

A set owns the screen the way a scene does — the lab HUD steps off.

---

## STILL OWED

- The skeletal vehicles (`SK_veh_*`) sample the wrong sheet; that rainbow pickup is UVs
  that do not match the palette.
- Roofs and chimneys are tinted because their sheets were never exported.
- No windows-lit-at-night, no interiors placed, no crowd of neighbours yet.
- `sets/casting*.set` are the casting instruments: the roster in bind pose, placed
  through the same set system, close enough to actually judge.

---

## Z6 — THE DAY, THE PRACTICALS, AND THE ROOM AT THE END OF IT

*(2026-08-29. The film runs from a wet 7am to a boarded window after dark; the sun going
down is not dressing, it is the plot.)*

### The day is one curve

    ctl  tod=<hours>            0..24
         tod=off                back to whatever sun=/sky= last set
         todrate=<hoursPerSec>  let the clock run

Fourteen keyframes drive sun elevation, azimuth, intensity, colour **and** the sky ambient
together, because they are not independent quantities: a low sun is a warm sun, and a warm
sun sits under a sky that has gone violet. Keyframed rather than computed from an
ephemeris — this is a film, and the golden hour lasts as long as the cutting needs it to.

⭐ **NIGHT IS NOT `sun=off`.** `uSun.w == 0` is the club's dark-room constant path, which
has no sky and no direction and is useless outdoors. Night here is a **weak blue
directional over a near-black sky**, which is what moonlight is, so one shader branch
carries noon and midnight and the practicals read against both. `sun=off` still means the
dark room; the two do not collide. The sun is also never allowed below y=0.06 as a
*light* — a directional from underneath lights faces from the chin up.

### Practicals: `light x y z r g b [power]`

Every mesh program already carried `lp[3]`/`lc[3]` with falloff `1/(1+dd*0.055)` — the
club's band lights. A set now claims them. **Three is the cap and the cap is the shader's.**
A set with no light lines binds ZEROS, which is right: a daylit street is lit by the sun
and nothing else, and a night one is dark until somebody turns something on.

⛔ **THEY BLOW OUT FAST.** At two units the falloff is 0.82, so a power of 6 delivers 4.9 —
five times white, and the first interior had a wall like a photographer's softbox. A lamp
you are standing next to is about **1.5**, not 6.

*(The cap has since moved: `NLIGHT` is 6, a set takes 5 and the top slot is kept for a held
light — ROBOT NEKO ep 4.)*

### ⭐ A lamp has a REACH: `light x y z r g b [power] [reach]` (ROBOT NEKO ep 12)

jacob, on the ONE AM bedroom: *"its too bright in the room too"*. Every lamp in the club fell
off the same way — half brightness at **4.26 u**, streetlight and night light alike — so a
night light strong enough to find two faces on a pillow lit the far walls cream, and the
wallpaper was the brightest thing in every frame. **REACH** is the distance at which that
lamp is at half brightness (`k = 1/reach²`). It goes to the shader as EXTRA falloff over
the club's 0.055 (`uniform float lk[6]`), so a set or a program that never names one is
byte-for-byte what it was. A held light takes one too: `…@light:<power>:<reach>`
(a cigarette's ember is `light:0.5:0.5` — her fingers and her face, not the room).

⛔ **REACH ALONE CANNOT MAKE A POOL.** However short the reach, a wall `d_w` from the lamp
is at most `(d_w/d_f)²` darker than a face `d_f` from it. The first relight gave the night
light reach 2.4 where it stood — 2.7 u from the faces, 5.5 u from the walls — and the walls
only went from cream to grey (4x is the ceiling). **Put the lamp next to what it lights**:
moved to the pillows (faces ~2 u, walls 11 u), the same room went to night. And a light
placed **OUTSIDE a wall** lights only what faces the opening — the inner face of the wall
turns away from it — which is what a streetlight through a window and the night in an open
door actually do (`kaeli_house.set`).

⛔ The boot's `[set] N practical(s), sky ambient X` report is printed when the SET loads,
which is before a scene's first line applies its `tod=`: in a scene it states the default
sky (0.353), not the hour the shot is lit at. Read the ratio against the `[held] … sky ambient`
line, which is printed after.

### ⛔ A set owns the screen — including the club's own decoration

Two soft glowing orbs hung over the road in every frame of the first day sheet. They are
**not** the sky-layer flyers: `REVA_NOSKY=1` left them exactly where they were. They are
the club's lamp sprites, light orbs and flame snake, which were already switched off while
a *scene* plays because a conversation wants a dark stage. A street wants it for the same
reason, so the gate now asks about a set too.

### `propr` — a prop with all three angles

    propr <stem> x y z yaw pitch roll scale [r g b]

`prop` carries yaw only, which is right for a kit that snaps to a grid. Act V is three
women nailing Dale's unfinished fence over a window, and there is no yaw that leans a
plank. A separate keyword rather than more optional fields, because `prop`'s tint is
already positional and appending angles would have made them depend on being tinted.

⛔ **AND MOST THINGS DO NOT NEED IT.** `fence_101` measures 0.52 × 2.89 × 7.26 — already
long in Z — so `yaw 90` alone lays it across a wall as a run of pickets. Adding roll 90 on
top tipped the boards into the room at a diagonal. Measure the prop (`skate_src/town/propdims.py`)
before reaching for an angle.

### `room` — a scan that is a PLACE (ROBOT NEKO ep 9)

    room <stem> x y z yaw pitch roll scale

The same fields as `propr`, for an environment you stand INSIDE — the Southbank Undercroft,
jacob's Fab photogrammetry scan, 1.82M triangles at full resolution (`sets/southbank.set`).
Two things a `propr` does are wrong for a place:

- ⛔ **IT BECOMES A COLLIDER BY NAME.** `propSolid()` reads the stem, and a scan is named for
  where it is, so the Undercroft came back solid — its circle, sized off the footprint, would
  have been **r 50.2**, ejecting everyone in the park. A room is never a circle.
- ⛔ **IT IS LIT TWICE.** A photogrammetry texture already IS the light the place had; the sun's
  N·L across a ceiling the sun never reaches, plus the sky ambient, turned it into night. A room
  draws self-lit through the shader's `uEmis` gain — ctl `roomlit=` (REVA_ROOMLIT), 1.0 default.
  Bodies keep the sun and sky.

Measure a scan before placing it (the ep 9 recipe, `skate_src/fab/README.md`): which local axis is
up (stand two copies at pitch ±90 and look), the floor as a **plane fit** over a per-cell height
map — never "the densest band", because an undercroft's ceiling is as big as its floor — and a
top-down map of clear 8×8 stages before any mark goes down.

⛔ **A PLACED SCREEN DRAWS AFTER THE SET.** `drawSet()` paints its sky first with no depth test;
anything drawn earlier in the frame is wiped, and its depth keeps the room out — a sky-coloured
hole the shape of the object. It is why Holly was blank in ep 8 (`scrDrawPlaced`, ep 9).

### `sets/boarded.set` — Act V

A 14 × 14 room on the kit's own 7-unit module: floor tiles, four walls, **a ceiling**
(without it the night sky shows through the top of the room), furniture, the boards, three
bodies and three practicals. ⛔ Roof tiles are CENTRED on their 7×7; wall modules are
placed at the FAR EDGE of the span they cover. Mixing those two conventions is how a room
ends up with a gap in one corner.

**Still owed:** the two women working are playing dance clips as stand-ins — nobody is
actually hammering; susan's `laying_idle` puts her on the floor beside the armchair rather
than in it; and the lamp still burns the wall behind it a little hot.

---

## Z6.5 — the quality pass

⭐ **`skate_src/town/texaudit.py`** replays `bake_town.sh`'s case statement against what
`ue_texmap.py` says UE actually uses, and prints every disagreement. It found **28**, of
which six were real and are now fixed:

| mesh | was given | UE says | what it looked like |
|---|---|---|---|
| `SM_floor_01` | the colour palette | **`T_planks_01` only** | the Act V floor was a flat tint instead of floorboards |
| `SM_road_sign_*`, `SM_road_cone` | the road sheet | the palette | signs wearing tarmac |
| `SM_chimney_*` | the palette | `T_brickSmall_01_white` | |
| `SM_tree_01` / `_03` / `_04` | `T_leaf_01` | `T_leaf_01a` / `T_leaf_02` | every tree the same foliage |
| `SM_rock_01` | the palette | the road sheet | |
| `SM_treeLowPoly_01` | `T_leaf_01` | the palette | |

The remaining 22 are **deliberate and now marked as such in the audit**: a wall module's
material references brick and two wallpapers, none of which is the SIDING the kit dresses
a house with, and the police sedan does carry its own skin. UE lists textures in dependency
order, not material order, so "the first one" is only authoritative when there is *one*.

⛔ **A TINT REPLACES THE TEXTURE.** `prop ... r g b` sets `uUse=0` — the mesh stops sampling
altogether. So tinting `floor_01` threw away the floorboards it had just been given, and
tinting `carpet_01` threw away the carpet. Tint the things that have no texture worth
keeping; do not tint the things that do.

⛔ **AND SOMETIMES THE TINT IS RIGHT.** The shingle tint on `roof_01` was documented as a
workaround for "a sheet we do not have" — wrong, it UVs into the palette like everything
else. But removing it after the flip fix made every roof WHITE on white siding, i.e. a
house with no roof. Reason corrected, tint kept: it is an art choice, not a workaround.

⛔ **ONE TILE OF GROUND RUN-OFF EITHER END.** `street.py` started the world at x=0, so a
camera two units before the first lot — exactly where you stand to shoot down a street —
saw the void underneath it.

### ⛔ The "dark silhouettes" were not a bug

Four skaters read as black cut-outs down every street wide while the near one was fully
lit. I diagnosed a lighting fault twice — first distance, then a per-program uniform gap —
and then put the camera on them: **up close every one is correctly lit and simply wearing
black.** They are the Killer/Monster end of the roster, cast as the punk skate crew on
purpose. It is a STAGING note (dark clothes on grey asphalt at distance), not a defect.

The second hypothesis did surface a real latent fault, which is kept: `uSun` was uploaded
to a single program, while `USEPROG` picks index 0 or 1 off each mesh's material flags and
a uniform is per-program state — so a mesh selecting the other program would have drawn
with the club's dark-room constants. Nothing in the town happens to select it today, so
the change is inert on pixels; it is here because the next pack ingested might not be.

### Frame rate

**60fps is a parameter, not a limit** — `driver.py scene --fps 60`. Verified on a real
capture: 600 frames, `r_frame_rate=60/1`, video 10.000s against audio 9.985s, and adjacent
frames differ by 4–9% of pixels, so it is genuine 60 and not 30 doubled. The clips are
baked at 30 and the P1.5 sampler interpolates between them.

⛔ **THE CEILING IS DISK, NOT THE ENGINE.** Frames land as 1280×720 PPM = 2.76 MB each, and
the driver keeps every one until the mux. Ten seconds at 60fps is 1.6 GB; fifteen minutes
would be **149 GB**. Shoot per SCENE — which is how a film is cut anyway — and no take
needs more than a few GB.

---

## Z7a — AN ACTOR GETS A TIMELINE

Two acts were blocked on the same missing thing twice over: a set actor held ONE clip at
ONE place for a whole take. Act III turns on a neighbour *becoming* one of them; Act IV is
three women crossing three lawns. Both are now one feature.

    actor v1_Character_29_Female  z_idle_a  30.2 0 21.2  200 1.0
    cue   12.40  zombie            # at t=12.4 she turns. one word.
    cue   16.50  z_atk_c
    path   0.00  30.20 0.00 21.20  # be here at t=0 ...
    path  16.40  29.90 0.00 20.15  # ... and there by 16.4

⭐ **A CUE IS THE TURN.** `cue <t> zombie` re-runs the same casting note the actor line
takes, so a neighbour who has stood at her own mailbox for twelve seconds becomes a
shambler on the frame the script says — same body, same rig, through the canonical
transfer. Measured firing exactly on time:

    [cue] t=12.40 actor 6 -> zombie      [cue] t=16.50 actor 6 -> z_atk_c
    [cue] t=14.80 actor 11 -> zombie     [cue] t=16.60 actor 7 -> z_hit_d

⭐ **A PATH IS BLOCKING, IN TIME.** Waypoints carry a TIME, not a speed — "be at the gate
by 6.0" is what a director says, and it means the crossing cuts to the dialogue rather
than the dialogue waiting for the crossing. Speed falls out of distance over time, the
actor faces the way she is going, and the clip phase runs at `gaitRate()` for that speed,
so nobody slides. (The same lesson as the partner's staging walk, one layer down.)

⭐ **ONE PLACE RESOLVES A CLIP NAME.** `actorClip()` is used by the actor line at load AND
by every cue, so a name means the same thing whenever it is said: the body's own clip,
else borrowed off a bank, else the `zombie` casting note.

⛔ **A SHOT CLOCK STARTS WHEN THE CAMERA ROLLS.** `g_setT` ran from the moment the set
loaded, but a capture does not begin until `REVA_SHOT`'s frame — at 30fps that is
**fourteen seconds of the shot gone before the first frame**, so a `cue 12.4` had already
fired off-camera. The clock now waits for the first captured frame. (The scene recorder
arms on `REVA_SCENEFPS` from the top, so it was never affected.)

### The two sets

- **`sets/the_turn.set`** — Act III. The mail carrier walks his round on a path, stops at
  her box, and does not walk away. Verified as a sequence: she stands, he arrives, at 12.4
  she turns, at 16.6 he goes down and stays down — in an ordinary frame with a picket
  fence, a parked pickup and a bin.
- **`sets/three_lawns.set`** — Act IV, laid out to match what reva actually *says*: two
  behind the hedge who never move, four on the road walking away, five at the truck, and
  one of the five turning at t=30.6 when the alarm dies.

⛔ **CLOSE-UPS IN ACT IV ARE STILL NOT SHOOTABLE.** `stage=` is a static mark, so the
conversation pair cannot cross with the actors — Act IV is a WIDE, with the scene supplying
voice and subtitles over the top. That is defensible (she is calling positions, not
delivering a close-up) but it is a constraint, not a choice. A `stagepath` is the next rung.

⛔ **And the Act IV framing is not directed yet** — the truck eats half the frame and the
three women leave it early. The blocking is right; the coverage is not.

## THE SKATE PROMO (09-19 → 09-20) — a line obeys the world

⭐ **A PATH IS PUSHED OUT OF THE SET.** A path was a Catmull-Rom spline through clear marks, and
nothing checked the curve BETWEEN them -- ep 2's filming line put ten samples up to 1.13 u
inside a bench, rider and board with it. `locoPathBuild` now pushes every interior sample out
of the walking distance field at 0.42 u (a rider is ~0.34 u across at the hips; a board sticks
out further) and says so: `[path] slot N: M of K samples were inside the set -- the line is
pushed out (deepest D u)`. The ENDS are left alone: a mark inside a feature is a staging
decision (a body on a bank, a board set down on a ledge), and moving a mark out from under
someone is worse than the clip.

⭐ **A `room` PUSHES A LINE TOO.** The scan is never a circle collider, but it IS sampled into the
audit grid off its own triangles (the Undercroft: 1,013,816 of them, 66,919 blocked cells), and
the collision field is built from that grid -- so pillars and walls were already solid to a walk
and a rider, and now to a path. No new collider; the one that was there, finally applied.

⭐ **THE SKY HAS A CLOUD DECK.** fbm on the ray's projection onto a flat ceiling, two layers
drifting on the wall clock, lit toward the sun; the water reflects it because both use the one
`skyColor`. Under `tod=` it takes the sun's colour with the rest of the sky.

**Two sets for lines.** `sets/theyard.set` -- a park built for LINES, not laps: a 56 u straight
run (about 32 m, ten seconds at park speed) from a bank at the top, over a gap that is ollied,
past ledges to the roll-out, and NO `route` line on purpose (jacob: "going in circles is kinda
passe"). `sets/sb_session.set` -- a session laid over `skate10`'s Southbank scan: a bank at the
open south end, two ledges, marks that WEAVE the 11 × 18 u island between the pillars. ⛔ NO
HALF-PIPE IN THE UNDERCROFT: one was added and came back as a twelve-metre grey slab across the
hall. What the place gives you is tight.

⛔ **A MISSING CLIP IS A T-POSE, AND IT IS LOUD NOW.** A partner asked for a clip that lived only
on the other body's pack stood in `z_rest` for half of promo ep 3, and `[stage]` said NOT FOUND
once, quietly. The first miss prints a boxed MISSING CLIP notice, later ones count, and the exit
report says how many lines that body stood in a T-pose for. Every clip a script names is baked
on the body that plays it.

### Marker nodes on a prop: `ToonSet`, `Glow`, `NoCast`, `Open` (10-18)

A baked prop can carry named marker nodes (`skate_src/glb2rpack.py <glb> <rpack> --marker ToonSet --marker Open ...`)
that change how the set draws it -- engine_capabilities.md section 38, first used by THE WORLD AT THE END OF TIME's
sick bay (`sets/sickbay.set`, written by `skate_src/ship/build_sickbay.py`):

- `ToonSet` -- the show's toon look (cel + ink), lit as a room: walls lit, undersides in the shadow tone, sun shadows.
- `Glow` -- unlit at its own colour, no ink: lamps, screens, an emitter, a lit cubicle.
- `NoCast` -- out of the sun's shadow map: a ceiling, a room's walls, anything that IS a light.
- `Open` -- never a collider, whatever its name. ⛔ A shell or a run of fittings built in ROOM coordinates is one pack
  whose footprint is the whole room: without `Open`, propSolid made it one circle that wide, at the room's centre.

A prop's instance stands on its ROOT NODE (the baker's origin), not on its lowest vertex, so pieces built in room
coordinates -- a ceiling at 2.9 m, a hood hanging from it -- all go in at `0 0 0` and stay where they were built.

### SETS BUILT BY CODE: `skate_src/setkit.py` (10-23)

The kit every set builder draws with, split out of build_sickbay.py, which re-exports it: the six builders that borrow
it through `import build_sickbay as sb` run unchanged, and a shim writes `sb.A = sb.Atlas()` through to the kit. One
ATLAS per set (swatches from `PAL`, boxes painted with PIL); primitives that keep ONE NORMAL PER POSITION, because the
ink is an inverted hull and it cracks wherever a position carries two (`rbox`, `lathe` with support loops at its
corners, `rod`, `tube`; `quad` and `disc_tex` for what the ink never sees); a Trek room's `walls()` (`ceil=` its
height), `door_frame()`, `screen()` and the LCARS painters; `write_glb`. `python skate_src/setkit.py --selftest` checks
each promise beside a control that breaks it and bakes its test room through glb2rpack. The split was proven by
building all seven set builders before and after: 120 glbs and atlases and 135 set files, byte for byte.
⛔ Start a new atlas by REBINDING the kit's (`setkit.A = setkit.Atlas()`, or `sb.A = ...`): a local `A = Atlas()` is an
atlas nobody draws into.

## THE SHIP AND THE SKY (THE WORLD AT THE END OF TIME, 10-22 → 10-23)

The lines chapters 1-3 added (engine_capabilities.md sections 41-46):

    mirror   x y z yaw w h                                   a mirror (4 at most), facing `yaw` into the room
    star     dx dy dz r g b size_deg [halo] [flare delay rise peak]
    starbody x y z R T cutdeg cutwidth seed [stretch k rise [delay]]
    actor    ... outfit=<name>                               what this actor wears (above)

- `mirror`: bodies and props are drawn again through the plane -- a toon body's ink too, a MirrorOnly backing never.
  A room from `skate_src/ship/build_rooms.py` carries the opening, a frame, the glass (`Alpha18`) and the backing (marker
  `MirrorOnly`: drawn only in a reflection, so the mirror is silver from the room and never shows its own back).
  REVA_NOMIRROR is the control, REVA_MIRDBG the instrument. ⛔ The first mirror came back BLACK: it reflected its own
  backing and glass, and a body's ink hull seen from behind is a black shell -- neither is drawn in a reflection now.
- `flare` on a `star`: from `delay` s after the set loads it brightens to `peak` over `rise` s, swells, and turns the
  flare star's blue-white (Wan-To's anomalous K-5; chapter 3's probing fire, five stars flaring in turn).
- `stretch` on a `starbody`: the star is drawn out along x to `k` times its radius over `rise` s, thinned the other
  two ways to keep its volume, heaving (3.3-13's particle rack). The rest of the space lines (`space`, `far`,
  `milkyway`, `beam`, `starspot`, `starloop`, `starweb`) are sections 41-42.
- ⛔ **A room's SHELL is not furniture.** A pack marked `NoCast` + `Open` is never solid to the contact pass -- built
  in room coordinates, it threw the hero toward the ceiling on every clip until it was excluded.
- ⭐ **A room tells the scene generator where its walls are**: `# BOUNDS x0 x1 z0 z1 ceiling` (metres) on its set file,
  and its geometry as glbs on D: (`production/sets/rooms/rm_<room>_{shell,fix,glow,glass}.glb`), which the generator
  tests every lens against.
- ⭐ **A body that LIES needs a mark AT the bed** (`bunk_lie` on the mattress, 2.1's `vik_pan` in the pan): a mark at a
  bunk's edge or beside a pan is for sitting or standing, and a lying clip on it lies on the floor.
