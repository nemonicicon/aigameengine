# THE SET — aigameengine grows a world

*(written 2026-08-29, out of the film. The conversation stage is deliberately
a void, which was right for a two-person scene in the dark and useless for a street.)*

⭐ **This is the level renderer.** The film is what forces it to be real; the door it
opens is a game that runs on the same files.

---

## THE ARTERY, again

`skate_src/town/{ue_export_town.py, bake_town.sh}` pull AssetsvilleTown out of
the zombie prototype's Unreal project through UE 5.8 headless — the same shim-project pattern that
took 16 weapons through in ~40s. **112 meshes and 15 textures in about two minutes, 0
failures.** Ground tiles, the wall/roof/door/window kit, 34 yard and street props, trees,
six vehicles, and enough interior for the night the windows get boarded. 124 prop rpacks
(the wall modules bake once per siding colour).

**Scale: the meshes come out in real-world METRES.** Measured off the GLBs — a mailbox is
1.42 tall, a wall module 3.00 × 4.00, a sedan 5.40 long, a road tile 16 × 16 — and the
engine runs 3.15 units per 1.8m body, so `--scale 1.75` lands the whole town at true size.

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
outfit -- which dressed two actors on one pack alike when the scene had them in different clothes. Each actor keeps its own
worn pieces, so two actors on one pack can dress differently.

⭐ (11-11) `prop=<spec>` and `prop2=<spec>` anywhere on an actor line are what that actor HOLDS: a scene prop's spec
(`stem@size@pitch@where@RRGGBB`, the size the longest side in world units, so a small actor's prop is sized for her). Only a scene's
four bodies could hold anything; now an actor publishes her grip and attach points into the voice slot (the cast slot with no body)
as she is drawn, and the held-prop draw takes her spec there. One actor at a time (the first that asks; a second is said once). First
used in the music video: a small copy of her carrying a prop.

A stem resolves as `props/v1_town_<stem>` → `props/<stem>` → `packs/<stem>`, so a BODY is
placeable the same way a mailbox is. An optional r,g,b replaces the texture with a flat
tint, which is how a kit of grey primitives becomes a painted lawn.

Deliberately dumb — no culling, no batching, no LOD. A street is a few hundred draws of a
few hundred triangles, which this engine does not notice, and **the shape of the FILE is
what has to be right**: it is the level format.

⛔ **A set instance must honour the pack's own scale and `mny`.** A prop rpack is baked at
engine scale and reports 1.0; a CHARACTER reports the factor from its rig units to the
world. Placing one through the prop path put a fifty-unit woman on the lawn.

### street.py

`skate_src/town/street.py` GENERATES the street, because the kit ships four houses and a
hundred modules to make the rest from. The grid, measured off the exported GLBs at engine
scale:

    ground tiles   28 x 28, origin at a corner: x 0..28, z -28..0
    sidewalk        7 x 7
    wall modules    7 wide along its own -Z, 5.25 tall, thin in X
    roof tiles      7 x 7, CENTRED
    fence 7.3 long · mailbox 2.5 tall · utility pole 14 · tree_02 21.8

Yaw follows the engine's convention (forward is +z), so a wall at yaw=90 runs along −X from
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

**No arrangement of POINT lights can stand in for a sun** — the engine's falloff is
`1/(1+dd*0.055)`, so a lamp far enough away to read as sunlight delivers 2% of its
brightness. `uSun.w == 0` keeps the engine's old dark-room constants exactly, so every
existing shot is bit-identical and the regression stays green.

A sun also means a **sky**: `clearOverride` paints the backdrop from the sky ambient,
lifted. Black above the horizon is right for a dance floor and wrong for a street, and night is
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

*(2026-08-29. The film runs from a wet 7am to after dark; the sun going
down is not dressing, it is carried by the light.)*

### The day is one curve

    ctl  tod=<hours>            0..24
         tod=off                back to whatever sun=/sky= last set
         todrate=<hoursPerSec>  let the clock run

Fourteen keyframes drive sun elevation, azimuth, intensity, colour **and** the sky ambient
together, because they are not independent quantities: a low sun is a warm sun, and a warm
sun sits under a sky that has gone violet. Keyframed rather than computed from an
ephemeris — this is a film, and the golden hour lasts as long as the cutting needs it to.

⭐ **NIGHT IS NOT `sun=off`.** `uSun.w == 0` is the engine's dark-room constant path, which
has no sky and no direction and is useless outdoors. Night here is a **weak blue
directional over a near-black sky**, which is what moonlight is, so one shader branch
carries noon and midnight and the practicals read against both. `sun=off` still means the
dark room; the two do not collide. The sun is also never allowed below y=0.06 as a
*light* — a directional from underneath lights faces from the chin up.

### Practicals: `light x y z r g b [power]`

Every mesh program already carried `lp[3]`/`lc[3]` with falloff `1/(1+dd*0.055)` — the
dance floor's band lights. A set now claims them. **Three is the cap and the cap is the shader's.**
A set with no light lines binds ZEROS, which is right: a daylit street is lit by the sun
and nothing else, and a night one is dark until somebody turns something on.

⛔ **THEY BLOW OUT FAST.** At two units the falloff is 0.82, so a power of 6 delivers 4.9 —
five times white, and the first interior had a wall like a photographer's softbox. A lamp
you are standing next to is about **1.5**, not 6.

*(The cap has since moved: `NLIGHT` is 6, a set takes 5 and the top slot is kept for a held
light.)*

### ⭐ A lamp has a REACH: `light x y z r g b [power] [reach]`

A bedroom set at night was too bright. Every lamp in the engine fell
off the same way — half brightness at **4.26 u**, streetlight and night light alike — so a
night light strong enough to find two faces on a pillow lit the far walls cream, and the
wallpaper was the brightest thing in every frame. **REACH** is the distance at which that
lamp is at half brightness (`k = 1/reach²`). It goes to the shader as EXTRA falloff over
the engine's 0.055 (`uniform float lk[6]`), so a set or a program that never names one is
byte-for-byte what it was. A held light takes one too: `…@light:<power>:<reach>`
(a cigarette's ember is `light:0.5:0.5` — her fingers and her face, not the room).

⛔ **REACH ALONE CANNOT MAKE A POOL.** However short the reach, a wall `d_w` from the lamp
is at most `(d_w/d_f)²` darker than a face `d_f` from it. The first relight gave the night
light reach 2.4 where it stood — 2.7 u from the faces, 5.5 u from the walls — and the walls
only went from cream to grey (4x is the ceiling). **Put the lamp next to what it lights**:
moved to the pillows (faces ~2 u, walls 11 u), the same room went to night. And a light
placed **OUTSIDE a wall** lights only what faces the opening — the inner face of the wall
turns away from it — which is what a streetlight through a window and the night in an open
door actually do (a house set).

⛔ The boot's `[set] N practical(s), sky ambient X` report is printed when the SET loads,
which is before a scene's first line applies its `tod=`: in a scene it states the default
sky (0.353), not the hour the shot is lit at. Read the ratio against the `[held] … sky ambient`
line, which is printed after.

### ⛔ A set owns the screen — including the dance floor's own decoration

Two soft glowing orbs hung over the road in every frame of the first day sheet. They are
**not** the sky-layer flyers: `REVA_NOSKY=1` left them exactly where they were. They are
the dance floor's lamp sprites, light orbs and flame snake, which were already switched off while
a *scene* plays because a conversation wants a dark stage. A street wants it for the same
reason, so the gate now asks about a set too.

### `propr` — a prop with all three angles

    propr <stem> x y z yaw pitch roll scale [r g b]

`prop` carries yaw only, which is right for a kit that snaps to a grid. A scene with
planks nailed over a window needs them leaning, and there is no yaw that leans a
plank. A separate keyword rather than more optional fields, because `prop`'s tint is
already positional and appending angles would have made them depend on being tinted.

⛔ **AND MOST THINGS DO NOT NEED IT.** `fence_101` measures 0.52 × 2.89 × 7.26 — already
long in Z — so `yaw 90` alone lays it across a wall as a run of pickets. Adding roll 90 on
top tipped the boards into the room at a diagonal. Measure the prop (`skate_src/town/propdims.py`)
before reaching for an angle.

### `room` — a scan that is a PLACE

    room <stem> x y z yaw pitch roll scale

The same fields as `propr`, for an environment you stand INSIDE — the Southbank Undercroft,
a Fab photogrammetry scan, 1.82M triangles at full resolution (`sets/southbank.set`).
Two things a `propr` does are wrong for a place:

- ⛔ **IT BECOMES A COLLIDER BY NAME.** `propSolid()` reads the stem, and a scan is named for
  where it is, so the Undercroft came back solid — its circle, sized off the footprint, would
  have been **r 50.2**, ejecting everyone in the park. A room is never a circle.
- ⛔ **IT IS LIT TWICE.** A photogrammetry texture already IS the light the place had; the sun's
  N·L across a ceiling the sun never reaches, plus the sky ambient, turned it into night. A room
  draws self-lit through the shader's `uEmis` gain — ctl `roomlit=` (REVA_ROOMLIT), 1.0 default.
  Bodies keep the sun and sky.

Measure a scan before placing it (the recipe, `skate_src/fab/README.md`): which local axis is
up (stand two copies at pitch ±90 and look), the floor as a **plane fit** over a per-cell height
map — never "the densest band", because an undercroft's ceiling is as big as its floor — and a
top-down map of clear 8×8 stages before any mark goes down.

⛔ **A PLACED SCREEN DRAWS AFTER THE SET.** `drawSet()` paints its sky first with no depth test;
anything drawn earlier in the frame is wiped, and its depth keeps the room out — a sky-coloured
hole the shape of the object. It is why a placed screen came out blank (`scrDrawPlaced`).

### `sets/<room>.set` — a boarded room

A 14 × 14 room on the kit's own 7-unit module: floor tiles, four walls, **a ceiling**
(without it the night sky shows through the top of the room), furniture, the boards, three
bodies and three practicals. ⛔ Roof tiles are CENTRED on their 7×7; wall modules are
placed at the FAR EDGE of the span they cover. Mixing those two conventions is how a room
ends up with a gap in one corner.

**Still owed:** the two bodies working are playing dance clips as stand-ins — nobody is
actually hammering; one body's `laying_idle` puts her on the floor beside the armchair rather
than in it; and the lamp still burns the wall behind it a little hot.

---

## Z6.5 — the quality pass

⭐ **`skate_src/town/texaudit.py`** replays `bake_town.sh`'s case statement against what
`ue_texmap.py` says UE actually uses, and prints every disagreement. It found **28**, of
which six were real and are now fixed:

| mesh | was given | UE says | what it looked like |
|---|---|---|---|
| `SM_floor_01` | the colour palette | **`T_planks_01` only** | the boarded room's floor was a flat tint instead of floorboards |
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
with the engine's dark-room constants. Nothing in the town happens to select it today, so
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

Two scenes were blocked on the same missing thing twice over: a set actor held ONE clip at
ONE place for a whole take. One needs a bystander to *become* a zombie mid-shot; the other,
three bodies crossing three lawns. Both are now one feature.

    actor v1_Character_29_Female  z_idle_a  30.2 0 21.2  200 1.0
    cue   12.40  zombie            # at t=12.4 she turns. one word.
    cue   16.50  z_atk_c
    path   0.00  30.20 0.00 21.20  # be here at t=0 ...
    path  16.40  29.90 0.00 20.15  # ... and there by 16.4

⭐ **A CUE IS THE TURN.** `cue <t> zombie` re-runs the same casting note the actor line
takes, so an actor who has stood at a mailbox for twelve seconds becomes a
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

- **`sets/<turn>.set`** — a walker follows a path, stops at
  a mailbox where another actor stands, and does not walk away. Verified as a sequence: she stands, he arrives, at 12.4
  she turns, at 16.6 he goes down and stays down — in an ordinary frame with a picket
  fence, a parked pickup and a bin.
- **`sets/<lawns>.set`** — laid out to match the scene's dialogue: two
  behind the hedge who never move, four on the road walking away, five at the truck, and
  one of the five turning at t=30.6.

⛔ **CLOSE-UPS IN A CROSSING SCENE ARE STILL NOT SHOOTABLE.** `stage=` is a static mark, so the
conversation pair cannot cross with the actors — such a scene is a WIDE, with the scene supplying
voice and subtitles over the top. That is defensible (she is calling positions, not
delivering a close-up) but it is a constraint, not a choice. A `stagepath` is the next rung.

⛔ **And that scene's framing is not directed yet** — the truck eats half the frame and the
three walkers leave it early. The blocking is right; the coverage is not.

## THE SKATE PROMO (09-19 → 09-20) — a line obeys the world

⭐ **A PATH IS PUSHED OUT OF THE SET.** A path was a Catmull-Rom spline through clear marks, and
nothing checked the curve BETWEEN them -- one filming line put ten samples up to 1.13 u
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
past ledges to the roll-out, and NO `route` line on purpose (going in circles is
passé). `sets/sb_session.set` -- a session laid over `skate10`'s Southbank scan: a bank at the
open south end, two ledges, marks that WEAVE the 11 × 18 u island between the pillars. ⛔ NO
HALF-PIPE IN THE UNDERCROFT: one was added and came back as a twelve-metre grey slab across the
hall. What the place gives you is tight.

⛔ **A MISSING CLIP IS A T-POSE, AND IT IS LOUD NOW.** A partner asked for a clip that lived only
on the other body's pack stood in `z_rest` for half of a promo take, and `[stage]` said NOT FOUND
once, quietly. The first miss prints a boxed MISSING CLIP notice, later ones count, and the exit
report says how many lines that body stood in a T-pose for. Every clip a script names is baked
on the body that plays it.

### Marker nodes on a prop: `ToonSet`, `Glow`, `NoCast`, `Open` (10-18)

A baked prop can carry named marker nodes (`skate_src/glb2rpack.py <glb> <rpack> --marker ToonSet --marker Open ...`)
that change how the set draws it -- engine_capabilities.md section 38, first used by the space series'
sick bay (a set built by code):

- `ToonSet` -- the show's toon look (cel + ink), lit as a room: walls lit, undersides in the shadow tone, sun shadows.
- `Glow` -- unlit at its own colour, no ink: lamps, screens, an emitter, a lit cubicle.
- `NoCast` -- out of the sun's shadow map: a ceiling, a room's walls, anything that IS a light.
- `Open` -- never a collider, whatever its name. ⛔ A shell or a run of fittings built in ROOM coordinates is one pack
  whose footprint is the whole room: without `Open`, propSolid made it one circle that wide, at the room's centre.

A prop's instance stands on its ROOT NODE (the baker's origin), not on its lowest vertex, so pieces built in room
coordinates -- a ceiling at 2.9 m, a hood hanging from it -- all go in at `0 0 0` and stay where they were built.

### `gloss y [F0] [max] [fade]` and the marker `Floor` -- a polished floor (10-23)

A set's `gloss` line makes its floor reflect: everything above the plane `y` is drawn again through it, fading over
`fade` units below it, and the floor itself -- a prop pack baked with `--marker Floor` -- is drawn last over that
reflection with a Fresnel term (`F0` looking straight down, toward `max` at a grazing angle). The ship's rooms and the sick
bay say `gloss 0 0.16 0.50 5.0`; their floors are their own packs (`rm_<room>_floor`, `sb_floor`, from
`setkit.walls(floor=)`). REVA_NOGLOSS=1 is matte; engine_capabilities.md section 47.

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

## THE SHIP AND THE SKY (the space series, 10-22 → 10-23)

The lines the space series added (engine_capabilities.md sections 41-46):

    mirror   x y z yaw w h                                   a mirror (4 at most), facing `yaw` into the room
    star     dx dy dz r g b size_deg [halo] [flare delay rise peak]
    starbody x y z R T cutdeg cutwidth seed [stretch k rise [delay]]
    actor    ... outfit=<name>                               what this actor wears (above)
    actor    ... prop=<spec> [prop2=<spec>]                  what this actor holds (above)
    place    t x y z [yaw]                                   (11-12) after an actor line: she is put there at t, no walk between

- `place` (11-12, the music video's copies hopping one column on every beat): the last place whose time has passed is where the actor
  stands, her line's own place before the first; up to 96 an actor. A `path` WALKS her (it turns her the way she goes and paces her clip
  to the speed, so a jump of a path's waypoints turned her sideways for good); a `place` puts her there and changes nothing else. Her
  time is the scene's when her clip is on it (`sceneclock`), the set's otherwise. `[place]` says her first place; `REVA_PLACESAY=1` says
  every one. Her hair hangs again by itself (the strands' cut rule: further than half her size in a frame).
- `mirror`: bodies and props are drawn again through the plane -- a toon body's ink too, a MirrorOnly backing never.
  A room built by code carries the opening, a frame, the glass (`Alpha18`) and the backing (marker
  `MirrorOnly`: drawn only in a reflection, so the mirror is silver from the room and never shows its own back).
  REVA_NOMIRROR is the control, REVA_MIRDBG the instrument. ⛔ The first mirror came back BLACK: it reflected its own
  backing and glass, and a body's ink hull seen from behind is a black shell -- neither is drawn in a reflection now.
- `flare` on a `star`: from `delay` s after the set loads it brightens to `peak` over `rise` s, swells, and turns the
  flare star's blue-white (five stars flaring in turn).
- `stretch` on a `starbody`: the star is drawn out along x to `k` times its radius over `rise` s, thinned the other
  two ways to keep its volume, heaving. The rest of the space lines (`space`, `far`,
  `milkyway`, `beam`, `starspot`, `starloop`, `starweb`) are sections 41-42.
- ⭐ **THE SET'S TIMELINE (11-03, `engine/settrack.inc`):** `track <file> [seconds]` plays a file of sky, lights, beams and props against the scene's clock (the clock plus `seconds` is the file's clock, so a take can start mid-song); `mover <stem> [r g b]` is a prop it places, the Nth line being the file's Nth mover row, hidden at scale 0 and never a collider. The file: one line `TRK2 <fps> <frames> <lights> <beams> <movers>`, then little-endian floats per frame: sky r g b; lights x y z r g b; beams x y z ux uy uz length r0 r1 r g b k; movers x y z yaw pitch roll sx sy sz. Written by code (a light-show script). A set with no `track` line never reaches it. A set may have 5 lights: NLIGHT is 6 and the top slot is reserved for a held light.
- ⛔ **A room's SHELL is not furniture.** A pack marked `NoCast` + `Open` is never solid to the contact pass -- built
  in room coordinates, it threw the hero toward the ceiling on every clip until it was excluded.
- ⭐ **A room tells the scene generator where its walls are**: `# BOUNDS x0 x1 z0 z1 ceiling` (metres) on its set file,
  and its geometry as glbs on D: (`production/sets/rooms/rm_<room>_{shell,fix,glow,glass}.glb`), which the generator
  tests every lens against.
- ⭐ **A body that LIES needs a mark AT the bed** (`bunk_lie` on the mattress, another in the pan): a mark at a
  bunk's edge or beside a pan is for sitting or standing, and a lying clip on it lies on the floor.

**TRK2 (11-03).** A mover now takes three scales and its (x y z) is its own origin, so a piece can squash and stretch and turn about its middle. A TRK1 file still reads: one scale, stood on its lowest point. Budgets: `SET_MAXP` 384 stems, `SET_MAXI` 4096 instances; a row costs 36 bytes a frame, so 1423 movers across the song is 244 MB of track.

**TRK3 (11-04, queue step 21).** The header gains a count of looks: `TRK3 <fps> <frames> <lights> <beams> <movers> <looks>`. A mover's row is 15 floats: `x y z yaw pitch roll sx sy sz`, then `r g b wash`, `alpha`, `picture`.
- `r g b wash`: a colour, and how much of it goes over the mover's own texture or colour, 0 none to 1 all of it. One stem can change colour now.
- `alpha`: 1 solid, down to 0 not there. Smoke can thin out and not only shrink. Under half, it stops casting a shadow.
- `picture`: which cell of its sheet it shows, counted along the rows from the top left, from 0; -1 is its own texture. The set line says the sheet: `mover <stem> [r g b] grid=CxR` cuts the stem's first texture into C x R pictures (256 at most, 16 sheets a set). The stem's UVs should span the whole texture, as for one picture. ⛔ A stem already in memory as a plain prop, or a cooked (compressed) one, has no pixels to cut: it says so and its pictures are blank.
- After the movers, each look is `x y z on`: a place for scene slot N's head and eyes to go, while `on` is over a half. It beats who's talking and a body told not to look, and a look up or down reaches a little further than a look at a face.
A row is 60 bytes a frame now. TRK2 and TRK1 files still read.

**Where a clip starts (11-04, queue step 21).** `clipstart <seconds>` in a set: every `z_rm_` clip, a set actor's or a scene body's, starts that far in, so a take that starts mid-song needs no re-bake of its performances. On one body: an actor's clip as `name>12.5s` (seconds in) or `name>0.33` (that far through), and the same on a scene line, `hclip=name>12.5s`, `pclip=name>0.33`. It starts there and plays on. While a capture's waiting, a `z_rm_` actor stands on that frame. `@` still holds a pose. A start past the end is the last frame.
`scenes/trk3_demo.py` makes a small set that shows all of it (`_trk3_demo`).

**A scene's body washed with a mover's colour (11-11, the music video's chorus).** `slotwash <slot> <mover> <amount>` in a set: scene slot `<slot>` (0 the hero, 1 the partner, 2 and 3 the others) is drawn mixed `<amount>` (0 to 1) of the way toward the colour the timeline gives mover number `<mover>` that frame, skin and clothes alike; her hair keeps its own colour and her eyes their glow. A body standing before a card that changes colour on the beat changes with it, and a body with no line is in full colour. `engine/set.inc` `slotWashFor`, `engine/body.inc` `g_bodyWash`. The chorus's take script writes three (at 0.5).

**Cloth on a mover, and eyes that glow (11-04, queue step 23).** `curtain x y z w h yaw [r g b]`, `sheet` and `solid` take `mover=N`: the cloth's pins, or the box, ride the Nth mover of the set's timeline, and their numbers are in that mover's own space (`_trk3_demo` hangs a curtain under its third card). An actor's line takes `eyes=r,g,b[,amount]` anywhere on it: her eyes glow that colour, if the engine can find them (meshes of their own, or a built person's).

**Animals, and fur.** A set actor needn't be a person: an animal baked at its true size (`glb2rpack_anim.py --height`) plays its own clips by name. `sets/_fur_demo.set` is four of them (`scenes/fur_demo.py`). Fur is in the pack's material chunk, and a set's `wind` leans it.

**What a set's surfaces reflect (11-04, queue step 22).** `reflect x y z` says where the bake looks out from. The same run that bakes a set's probes (`REVA_BAKE=sets/<name>.probes` in the no-display build, with `REVA_SET=<name>`) also writes `sets/<name>.env`: a small cube of how open the sky is in each direction and whether the sun reaches what's in the way. A set picks it up at load, and with the `reflect` setting at 2 a shiny surface stops reflecting sky it can't see. Without a line it looks from the middle of the set at head height. A prop's own material is in its pack (`design/pack_format.md`, `MATL`). `scenes/material_demo.py` makes a set that shows materials (`_mat_demo`).
⛔ A prop made by code needs nothing special now, but until 11-04 one with no bone node was stood a billion units down by its `prop` line.

## EFFECTS AND THE WORLD (11-05, queue step 24)

Six kinds of line, and one scene that shows them all: `python reva_native/scenes/everything.py --shoot OUT.png` (it writes `sets/everything.set` itself, a station a capability, and shoots a camera a station; `--list` says what each is there to show). The props its new stations stand on are made by code: `skate_src/author/foliage.py` (grass, a bush, a tree) and `skate_src/author/groundwork.py` (a pond's bed, a rock, steps against a wall).

**`emitter <kind> x y z [dx dy dz] [rate= size= life= speed= spread= rgb=r,g,b on=N]`** is particles from a place, thrown a way (up, if it isn't said). Kinds: `smoke fire sparks steam dust embers fountain snow exhaust pyro fuse rain`. `on=N` rides the set's Nth `mover`, so what the timeline moves smokes where it goes. Rain is thrown down from a box of air 10 across: put it overhead, pointing down. A soft kind (smoke, fire, steam, dust, exhaust) fades out where it meets a surface on a machine that reads the scene's depth, and ends in a line across it elsewhere.

**`beamlight <scale>`**: a set's beams light what they point at, by this many times their strength (6 if it isn't said, 0 for beams that are only a glow). On a machine whose `beamlights` setting is 2 they cast shadows too. A beam the timeline sweeps is a light that sweeps.

**`grass x0 z0 x1 z1 [per-unit]`** covers a patch of ground with grass (the prop `fol_grass`), and **`scatter <prop> x0 z0 x1 z1 count [y= seed= scale=a,b sway= rgb=r,g,b[,amount]]`** puts count copies of any prop about a patch, each turned its own way and its own size, the same every time for a seed. `sway` is how far its top leans in the set's `wind`, as a share of its height (grass 0.25, a tree 0.03); the wind's waves cross a field. `rgb` washes each copy a little more or less with a colour, so a field isn't one green. The machine's `foliage` setting draws that many in a hundred.

**Level of detail needs no line.** A prop with `props/<stem>~lod1.rpack`, `~lod2.rpack` and `~far.rpack` beside it is drawn from whichever suits how big it is in the picture, and so is a set actor's body with `~lod1` and `~lod2` (always in `props/`, whatever folder the body is in). `python skate_src/author/lod.py props/<stem>.rpack` makes a prop's three; `--body` makes a body's two. `REVA_LODDBG=1` washes a prop's lod1 green, its lod2 yellow and its far picture red, a body's copy green, and says once which copy each actor is drawn from. The machine's `lod` setting (0 to 4) says how soon detail lets go.
⛔ A far picture is cut out by its texture's alpha, and a wash over a half stops the cutting: `scatter`'s `rgb` is held under that.
⛔ A copy of a body made before the body was rebaked has other bones, and isn't drawn (the engine compares the count): make the copies again after a bake.

**`decal <kind> x y z size [dx dy dz] [along= deep= turn= rgb=r,g,b a= pic=<prop>]`** throws a mark at the set: whatever lies inside its box takes it, a floor, a step, a wall, a prop. `dx dy dz` is the way it's thrown, into the surface (down at a floor, if it isn't said). Kinds: `mark splat scorch wet crack` stain what's under them, so they're lit and shadowed as it is, and their colour is what a white floor would look like under them (0.6 is wet stone, 0.3 nearly black); `stripe` and `picture` are paint, laid over and lit by the sun and the sky. A stripe is the whole box: `size` across, `along=` the other way. A picture shows the first texture of the prop `pic=` names, its top up a wall, and away from someone at +z looking at a floor. `deep=` is how far through the surface it reaches each way: a splat on a stair wants enough to reach the step below. On a machine that doesn't read the scene's depth (`decals` off) each is a flat dark mark across its box's middle, and a picture isn't drawn.

**`water x y z width depth [r g b]`** is as it was, and on a machine whose `refract` setting is on it shows what's under it, bent by its ripples and darker with depth, with a broken line of foam where it meets ground, and it thins to nothing at its edge. So water wants a bed with sides: laid over a flat floor it's a tinted sheet. `gw_pond` is one.

## THE LENS, FILM'S LOOK, AND THE TIMELINE'S FOURTH FORMAT (11-05, queue step 25)

**`lens [roll=DEG] [focus=<distance>|aim[,soft]] [shutter=0..1]`** is a set's own lens: rolled about its own line, focused that far away (or on whatever it's aimed at) with the rest going soft by `soft` (1 is about a fast portrait lens), and its shutter open that much of a frame, so a fast move smears. **`film [sepia=] [grain=] [burn=] [flash=] [weave=x,y]`** is its look in the post pass. Both stand until something says otherwise: a ctl line's `camroll= focus= shutter= sepia= grain= flash= burn= weave=`, a scene line's `camroll= focus= shutter= sepia= grain=`, a game's `E->lensLook`, or the timeline. `cam=ex,ey,ez,ax,ay,az,fov,roll` rolls a placed lens with an eighth number, and a lens placed without one is level. None of it does anything where the machine has no post pass, and depth of field and motion blur need their settings (`dof`, `mblur`).

**TRK4.** A track's header is `TRK4 <fps> <frames> <lights> <beams> <movers> <looks> <cams> <posts>`, and after a frame's looks come `cam ex ey ez ax ay az fov roll on` (if cams is 1) and `post exposure sepia flash burn weaveX weaveY grain focus dof shutter haze grade` (if posts is 1). While `on` is over a half the track's lens is the picture's, over a scene's own. It's read between the file's frames, so a move at 30 a second is smooth at 60, and a jump of more than 2.5 units in one frame is a cut. In the post row, exposure, haze and grade under 0 leave the set's own; focus 0 leaves the lens's and under 0 focuses on the aim. TRK3, TRK2 and TRK1 still read. `scenes/everything.py --film` writes one and shoots five moments of it.

**TRK5** (11-11, the music video's panel light). The header has a ninth number, `<spots>`, and each frame ends with `r g b` for each of the set's first that-many `spot` lines, in their order: what the spot's colour is multiplied by that frame (1 1 1 as the line says, 0 0 0 dark). The spot keeps its place, its aim and its shadow. It's read from the nearest frame and never eased, so a swap on a beat is a cut. The take's script writes one. ⛔ A track with lights replaces the set's own `light` lines: carry them into the track, or the set goes dark. ⛔ The take's frame n shows the scene's time (n-1)/30: a cut written for frame 15 lands on 16, so lay the sound a frame early.

**`trackt=<seconds>`** on a ctl line holds a set's timeline at a moment with no scene playing, `trackt=play` runs it round on the lab's clock, and `trackt=off` gives it back to the scene's.

## THE SET'S DRAWINGS (11-11, `engine/setart.inc`, for the music video)

**`art <file> [seconds]`** plays a file of DRAWINGS against the scene's clock, as `track` plays lights and props: pen strokes, flat shapes and pictures, placed anew in every frame. The scene's clock plus `[seconds]` is the file's (a take that starts mid-song), and `trackt=` holds or runs it with the timeline. A set stood still, then it could move props (`track`), and it still couldn't draw: no stroke with a width, nothing revealed as a pen moves, no picture laid over the frame or behind the bodies. `skate_src/band/artfile.py` writes the file; the engine lays nothing out and eases nothing, it draws what each frame says.

- **What a file holds, made once:** pictures (PNG and what stb_image reads, by path beside the file: a sheet of letters is a picture), strokes (a pen's path), groups (strokes in the order a hand draws them, each with its own width and colour: a creature, a written line), meshes (flat triangles), and up to 32 planes (a place in the world: where the canvas's corner is, and the world's step for one canvas unit across and one down. A whiteboard is a plane).
- **A frame's items, in order:** a quad (a flat colour or a picture, with a soft edge if asked), a stroke or a group drawn from any share of its length to any other (so it's DRAWN in time, pen at one speed, round ends, mitred turns, a soft edge), a mesh. Each is placed with six numbers (an affine), and a group's stroke widths scale with it.
- **The canvas** is 1920 x 1080, top left 0,0, y down, fitted to the picture's height and centred, whatever the picture's size.
- **Layers:** 0 OVER the finished picture, under the captions, exactly the colours given. 1 UNDER the world: behind everything solid and in front of the sky, on the frame: a backdrop for bodies to stand against. 2 + k ON PLANE k, in the world: bodies in front of it cover it.
- ⛔ Under the world and on a plane, a picture gives its ALPHA only, coloured by the item's colour (they're the world's own effect triangles: `gfxFxTris`, `gfxFxTex`), and those layers go through the scene's exposure and post. ⛔ A mesh's edge is hard: outline it with a stroke. ⛔ The world's effects are 64 batches a frame for everything (particles too), and a change between flat colour and a picture on the world's layers starts a new one: keep a plane's lettering together.
- It needs no renderer work: both renderers already drew canvas and world triangles, so the engine's own rasterizer draws the same drawings. `anim_bench/fuzzcheck.py` has a kind for the file (`art`), and `scenes/everything.py` a station (`drawings`).
- **The tools beside it** (`skate_src/band/`): `doodles.py` (a marker's hand: lines that bow and wander, circles that run past their start, a drawing `drawn()` part-way and `boil()`ed), `strokefont.py` (single-stroke letters from `fonts/*.svg`, so a word is written and can be timed to a voice), `showmath.py` (the song's grid, a hit that dies away, eases, a spring, dice that depend only on what they're for), `artfile.py` (the writer).
