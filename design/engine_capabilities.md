# THE CLUB — capability gaps, found by making a film in it

*(2026-08-30. jacob: "identify other places our engines capabilty needs to be expanded
upon, then engage.")*

Making SEVENTY-FOUR has been the best engine test the club has had, because a film
asks for things a demo never does. This is the running list: what the engine could not do,
what was done about it, and what is still true.

---

## DONE, 08-30

### 1 · A body can be put on a prop ✅
**The gap.** Every body stood on the ground plane. The scene pair is placed by `stage=` at
y=0 and a partner from the hero's matrix, also at y=0. So a chair was a thing you stand
next to and a bed was a thing you lie **through** — the cold open had to *sink a bed frame*
to y=−0.34 until its mattress met the woman lying on the floorboards.

**The fix.** Named marks, which is what a level format wants anyway:

    mark <name> x y z yaw       # in a .set, beside the prop it belongs to
    pmark= / p2mark=            # in a scene line

A mark carries a **height**, which is the whole point, and a **yaw**, because a chair faces
a direction and a body in it has no choice. It **overrides the pair relation**:
furniture-relative instead of person-relative, and the two cannot both be true.

⛔ **Slots 1–3 only.** The hero carries paths, the stage walk, foot planting and the state
tree; a mark would fight all of it. Both cases the film needed are partners.

### 2 · The grip is a frame, not a point ✅
**The gap.** Held props hung *at* the hand with the **body's** yaw. A torch could not point,
a hammer could not swing, and a prop stayed level while the arm holding it moved through a
whole clip.

**The fix.** The grip node's full world matrix falls out of the same FK that already
published the position — the rotation was being thrown away one line later.

- ⛔ **The basis has to be normalised.** `model * W[bone]` carries the **rig's** scale
  (0.018 on kmoves); multiplying the held size on top left a torch the size of a grain of
  rice.
- ⛔ **`stem@scale@pitch`**, because a UE forearm runs down the bone and a torch points out
  of the fist. A prop is turned into the hand *once, in the scene file*, rather than every
  asset being re-baked to a convention nobody wrote down.

### 3 · A scene can carry sound ✅
**The gap.** The film had no sound but voices — and Act V is *entirely* sound design, with
the last beat of the whole picture being **a knock**, which is a sound and not a line.

⭐ **And the engine needed no change at all.** `compile_scene.py` already mixes N line-wavs
into one track at offsets, and the engine already plays that track and muxes it against the
picture. A sound effect is a source at an offset like every line is.

    sfx <t> <name> [gain]       # ABSOLUTE scene seconds

Absolute, so re-voicing a line cannot drag the knock somewhere else. `sfxgen.py`
synthesises the library — knock, hammer, sprinkler, car_alarm, glass, door_close, footstep.
A knock is a damped impulse through a wooden panel; none of it needs a recording.

⛔ **Screams and moans are deliberately not faked.** They are performances, not waveforms,
and Act V lives or dies on them. **Real source needed.**

### 4 · The scene system takes more than two people ✅ *(earlier today)*
The partner globals became slot 1 of a cast array addressed through `#define`, so the
two-body path is bit-identical **by construction**. Camera marks are an index lookup, the
listener is the previous speaker, and there are per-slot staging keys.

---

## STILL TRUE — the honest list

### A · No runtime collision
`clear_spot` in `street.py` is **authoring-time separation between props**. Nothing stops a
body walking through a house, a fence or another body. jacob has already said this world
becomes a playable 3D space, and that needs a collider set built from the props plus a
swept test in the mover. **This is the biggest one.**

### B · Nobody can aim an eye
Reva's eyes are painted; Kaeli's are flat lid shells. The head aims; the eyes never do.
jacob, on watching a cut: *"the eyes. holy shit the eyes are so bad on most models."*
Untouched.

### C · Reva's mouth does not read in profile
Her face is nearly flat with a largely painted mouth, and the aperture + jaw bone were both
built against a frontal read. Currently unresolved — it may be a staging rule (do not shoot
her in pure profile) rather than a shader fix, and jacob's eyes are owed on which.

### D · A prop cannot be attached to a bone
Held props are drawn at the grip each frame, which is not the same thing. A hat, a bag, a
board carried between two people, a lead clipped to a collar — none of those have a
mechanism. The leash is a special case, hand-built.

### E · The b-roll path has no actors of its own
`broll.py` renders a set and the set's actors tick, but a b-roll shot cannot direct
anybody. Every background performance has to be authored into the `.set` and runs on a
loop for the whole take.

### F · Set actors have no behaviour
An actor plays one clip along a path with optional cues. It cannot react, look at anything,
avoid anything, or change what it is doing because something happened.

### G · Three lights, and no shadows from them
`lp[3]/lc[3]` is the shader's cap. Practicals do not cast shadows, so a lamp lights a room
without the furniture in it occluding anything.

### H · No coffee, no hammer, no boards
143 props and not one is a mug. Two coffees, a hammer and the boards Dale never finished
are all in the film's dialogue and none of them exists. **Artery job, not authoring.**

### 9 · A post pass, and fog ✅ (09-11)
**The gap.** No bloom, no anti-aliasing, no fog; the tone knee lived in two mesh shaders and not
the skate, hologram, sky or neon programs; the film graded in ffmpeg. And nothing softened distance,
so the end of the world was visible from any wide.

**The fix.** One full-screen FBO carries the whole 3D frame (`postBegin` at the main clear,
`postEnd` before the HUD, `g_mainFB` is what every mid-frame "back to the screen" binds now):
bloom (threshold → quarter-res → separable blur → add), FXAA, and the highlight knee ONCE on
everything -- on luminance, so a bright lawn stays green. Fog is exponential in eye distance in
the mesh shaders, toward the sky's own horizon colour unless a scene names one (`skyHorizonC`,
one formula where the skybox and the water carried two literals).

- ctl `post=0|1`, `bloom=<add>[,<threshold>]`, `knee=`, `fxaa=0|1`, `fog=<density>[,r,g,b]` / `off`;
  env `REVA_POST` `REVA_BLOOM` `REVA_FXAA` `REVA_FOG`.
- ⛔ **Default ON in the game doors, OFF in the lab and the film** (mode 11), so every strict bench
  shot and every scene stays byte-identical until it asks. The lab keeps its in-shader knee.
- ⛔ **Measured, twice**: bloom 0.30 over 0.72 turned a noon street into glare (siding and lawn are
  both above 0.72); 0.16 over 0.86 still veiled it (a 0.9 sky and a 1.0 tracer are the same pixel
  in 8 bits). 0.12 over 0.95: only saturated pixels bloom. An HDR buffer is what separates bright
  from white -- `EXT_color_buffer_half_float` is present for that day.
- The boot prints the context: `[gl] extensions: OES_depth_texture=no ...` -- **no depth texture on
  ANGLE/D3D11**, so the shadow pass packs depth into colour. `OES_depth24`, half-float colour,
  `ANGLE_instanced_arrays`, `EXT_frag_depth`, anisotropic filtering: all YES.

### 10 · The sun casts shadows ✅ (09-11)
**The gap.** The whole shadow budget was one 256² silhouette of reva projected on the dance floor
and blob ellipses under the crowd (off in sets). Every street frame had bodies with no contact.

**The fix.** A 2048² orthographic pass from the sun direction `tod=` already produces, DEPTH PACKED
INTO RGBA8 because this ANGLE/D3D11 context has no `OES_depth_texture` (measured at boot: `[gl]`).
The window (`shadowext=`, default ±28) sits a little past the lens aim and is snapped to its own
texel grid. Casters: the set props inside the window, and every skinned body the frame stashes —
hero, cast, actors, up to 32 crowd slots — one frame late like the floor mask always was; cut-outs
(leaves, hair) alpha-test in the pass. Receivers: all four mesh programs, four taps, slope bias,
sun term only. ctl `shadow=0|1` (default on whenever a sun is up), `REVA_SHADOW=0`.

A/B on parsons at 09:30 from the road: without, the pair stands on a flat beige pavement; with,
reva, kaeli, the bench, the crate and the bin throw long shadows toward the lens and the tree
dapples the right of frame. ⛔ A single window: a wide whose aim is 40 units from a house does
not shadow that house — widen `shadowext=` for a wide (2048² over ±90 is still 11 texels a unit),
or build cascades when a door needs both at once.

### 11 · Actors have minds, and the world has a nav grid ✅ (09-11)
**The gap (F above).** An actor played one clip on an authored path; it could not react, look,
avoid, or change what it was doing. jacob (09-03): "they should respond to events and eventually
turn into zombies."

**The fix.** At set load a walkable grid (one cell per unit over the colliders, `[nav] grid …`),
A* over it. A `mind wander|react|hunt <r>` line after an actor: wanderers pick walkable spots near
home; reacters run (4.2 u/s) for the walkable spot that puts the most distance on a zombie within
r, looking at it, and settle after five seconds; hunters shamble (1.1 u/s) at the nearest
non-zombie or the hero, re-planning every 0.7 s, and on contact the victim TURNS (`[mind] …
TURNED`) — or the hero is bitten (`wpnHurtHero`). A timed `path` still wins while it runs.
Bodies keep 0.9 apart and still respect the colliders. `REVA_NAVDBG=1` prints every decision;
`anim_bench/mindcheck.py` shoots `sets/_mind.set` from overhead N frames apart with the log.

Receipt: parsons' block builds a 198×140 grid (12% blocked); a 40 s run made 48 decisions —
18 wanders, 19 flees, 10 hunts, 1 TURNED — and the sheet shows the neighbours spread along the
pavement while the zombie works the road. ⛔ A shambler never catches a runner: contact needs
someone who is not looking (a wanderer), which is the film's own rule.

### 12 · Layered animation: a second clip on the upper body ✅ (09-11)
**The gap.** "No additive layering, no masked upper/lower body blend." Carrying a crate while
walking, waving while running, talking while boarding a window: a body played ONE clip.

**The fix.** `poseOverlay()` samples a second clip (own pack, then any bank through the canonical
transfer) on its own clock and blends it onto the first spine bone and everything hanging off it
(chest, neck, head, clavicles, arms, hands — 20 of reva's 31 bones); legs and hips keep the first
clip. One function, called from all three body paths. Hero: ctl `over=<clip>[@w]`, scene `hover=`;
cast: `pover=`/`p2over=`; actors: `over <clip> [w]` after the actor line. `REVA_OVERDBG=1` prints
the resolution and the mask size.

A/B on the lab floor at fixed step, `clip=w_walk` (mob1, through the bank) with and without
`over=z_armsup`: same stride at the same frame, arms overhead against arms swinging.
⛔ With a set loaded the lab PARKS the hero at z −150 — the first four takes shot `_bench.set`'s
leftover actor (cast with z_armsup at ep 3) and read as "identical"; shoot the hero on the lab floor.

### 13 · The set culls what the lens cannot see ✅ (09-11)
**The gap.** "No culling, no batching, no LOD" — all 559 of parsons' props drawn every frame.

**Measured first.** 10.4 ms a frame in a door on the street, 13.8 on a lab wide (with the sun pass
and the post pass). A sphere per instance (the pack's XZ radius and rendered height at the
instance scale, centred on the bone cloud) against the six planes of the pass's view-projection.
From a street-level lens: 7.06 → 5.25 ms of work; a wide keeps 518 of 559. ctl `cull=0` for the A/B,
`REVA_CULLDBG=1` counts. ⛔ Centre the sphere on the cloud, not the origin: a ground tile is
anchored at its max corner, and the first build culled the near tile out of a wide (14% of the frame
gone, found by pixel-diffing on against off — the diff must be 0.000%). Batching and instancing
(`ANGLE_instanced_arrays` is present) wait for a measurement that asks for them.

### 14 · Rigid bodies ✅ (09-11)
**The gap.** "Rigid bodies, projectiles, triggers: none. Nothing flies with physics."

**The fix.** A loose object is a sphere with an orientation under club gravity (the hair solver's
derivation), with restitution and friction on the ground (which sees the box's half HEIGHT, not the
sphere), the set's circle colliders as walls, pairwise contact, spin from the slip, sleep when still;
drawn as its prop about its own centre, casting a shadow, and a `hit` on every real impact.
Set `rigid <stem> x y z [scale] [vx vy vz]`; ctl `throw=<stem>@x,y,z@vx,vy,vz[@scale]`; in a door on
any set, Y (R) throws a crate from her hands. `REVA_RIGIDDBG=1`; `mindcheck.py --set _rigid --tag rigid`
shoots the strip. ⛔ The physics clock waits for the camera like the set clock does, or a bench
shoots six crates already at rest after the boot. ⛔ `effH` is a 3.15 FLOOR meant for bodies: the
first crates rested a unit in the air on it; `propY0/propY1` now measure the mesh.

### 15 · Eyes that aim on a morph rig, and the head layer on borrowed clips ✅ (09-11)
**The gap (B above).** jacob: "the eyes. holy shit the eyes are so bad on most models." Only bone-eyed
rigs could aim; Anima's ARKit eye-look shapes were in the pack and unwired.

**The fix.** `eyeMorphAim()` drives eyeLookIn/Out/Up/Down Left/Right from the same residual gaze
`eyeAim()` gives a bone rig (yaw>0 is her right: right eye Out, left eye In). Reva's painted eyes and
kaeli's lid shells stay as they are and the log says so once. ctl `gaze=yaw,pitch` forces the hero's
eyes for the bench. A/B on Anima's eye panel at fixed step: right / left / off move the irises
(3,073 / 3,062 px in the eye band; 0 before the fix below).

⛔ **Found on the way: the head layer never ran on a borrowed clip.** Only the own-clip branch called
`scnHeadLayer`; a hero on any bank clip (every `x_convo` in the film, Anima on `z_upright`) had no head
aim, no gaze and no jaw override. It runs on both branches now.

### 16 · Cloth: folds, self-collision, the body mesh, premade garments — PARKED (09-11)
jacob: "dont waste time on this pajama we made… something premade… clothing cant clip into the body, it must
respect the mesh" — and then, after two hours: "lets move on from the clothing its taking way too long for bad
results." Parked at his word. What landed, for the next time it is opened:
- the garment solver has BENDING constraints (one per interior edge, 50x softer than the edges) and
  SELF-COLLISION (a grid, half an edge's radius); `REVA_CLOTHBEND=0` for the A/B;
- it collides against the SKINNED BODY MESH (the largest non-cloth mesh, CPU-skinned each frame, ~23k samples),
  pushing every particle — pinned waistband included — out along the nearest vertex normal; `REVA_CLOTHMESH=0`;
  `REVA_CLOTHDBG=1` prints "N particles were INSIDE the skin before the push";
- ⛔ CLOTH_MAXV was a SILENT cap of 2048: a 4,271-vertex modelled garment skinned straight through her and two
  A/B sheets came back identical. 8192 now, and the refusal prints;
- `skate_src/author/dressglb.py` fits a PREMADE GLB garment to a body (landmarks off the skeleton, mean-radius
  width, a build-time push-out against the body normals, two-nearest-segment weights) and appends it as
  `<name>~cloth`; `packs/v1_anima_cargo.rpack` = Anima in the Sketchfab cargo pants (`models/Female_cargo_outfit`).
Last measured, mesh collision on, walking: 5–32 particles inside the skin before the push per frame, all pushed
clear; fitcheck pelvis −0.007 (capsule metric), calves +0.12 (baggy cargo, real). NOT looked at on a sheet after
the cap fix. The MetaHuman sweatpants (`models/Basic_Sweatpants…/metahuman/*.mhpkg`, a zip of uassets) want the
UE 5.7/5.8 MetaHuman plugin export — untouched.

### 17 · Cut on action ✅ (09-11)
**The gap (film item 1).** The EDL cut on seconds; a cut at a second lands wherever the bodies happen to be.

**The fix.** The engine publishes each body's MOTION ENERGY — summed joint angular speed over the bones,
rad/s — at 10 Hz during a scene (`[motion] t=.. hero .. p1 .. p2 ..`, hero from the pose it just built, cast
slots one frame behind). `shoot.py` reads the A take's log and slides every EDL boundary to the strongest
movement within 0.8 s, landing 0.12 s before its peak — mid-action, so both sides of the cut carry it. A
boundary with no movement near it stays where it was written; the quiet level is the 20th percentile (the
median of a talking body is already a gesture). `--no-action-cut` keeps the seconds; `--snaptest LOG --edl
"a-b:B,…"` shows what it would do.

Receipt, on a 10 s take of `armsup_test`: a boundary at 5.40 → 5.63 (the arms-up onset, peak 30.7);
one at 4.30, with only conversational gesture around it (5.4 against a 6.6 threshold), unchanged.

### 18 · Fingers transfer ✅ (09-11)
**The gap.** "The canon is 16 bones. Fingers never transfer." Every held mug sat in a flat hand and a zombie's
claw arrived open.

**The fix.** Thirty more canonical slots (five fingers × three joints × two sides, `16+side*15+finger*3+joint`),
parsed before the digit strip in `canonOf` for four rig families (UE `thumb_01_l`, Mixamo `LeftHandThumb1`,
CC `cc_base_l_thumb1`, Daz G9 `l_thumb1`; metacarpals and tips are not joints; Anima's `earring_01_l` is
not a ring finger). A finger joint's basis aims at the next joint, the last at its child; the same absolute
canonical transfer carries them. `[canon] <pack>: 16 body + N finger canon bones carry a basis` at load;
`REVA_NOFINGERS=1` puts the canon back to 16.

Census: the zombie bank animates fingers in all 32 clips (38 tracks each), `moves` in 3; Anima has 36
finger bones (20 with a basis: her fingertips have no child), the Mixamo citizens 40, reva none (a 27-node
Meshy statue). A/B, Anima on `z_atk_a` at fixed step: fingers on, her hands close into the claw; off, they
stay splayed at her hips (0.10% of the sheet, all of it at the hands).

### 19 · Data-driven doors ✅ (09-11)
**The gap.** "No course, quest or dialogue file format." The platformer's course was a C table and the
narrative RPG's errand a switch statement.

**The fix.** `data/plat_course.txt` (`ledge x0 x1 topY`, `orb x y`, `goal x`) and `data/nrpg_quest.txt` (`npc`,
`say <npc> <stage|N+|*>`, `goto <npc> <stage> <new> [score]`, `marker <stage> x z <new> <banner>`, `hud <stage>`,
`start`) are read at boot; both files reproduce the built-ins exactly, the built-ins stay as a loud fallback,
and `REVA_PLAT_COURSE` / `REVA_NRPG_QUEST` point a door at another file. Edit, relaunch: no rebuild.

Receipt: the narrative door with a variant file reads "QUEST: FIND THE MAYOR BY THE BAR" where the shipped
file reads "QUEST: TALK TO THE ELDER"; the platformer's variant loads "12 ledges, 8 orbs, goal at x 40.0"
(the neon ledges are too thin against the crowd to prove that half on a door lens — the same array feeds
physics and draw). The ARPG's trail and the arcade cabs are the next files.

### 20 · Normal maps ✅ (09-11)
**The gap.** "Base colour and tint only. No normal maps, roughness/metal, PBR."

**The fix.** The baker carries a material's `normalTexture` into the pack's texture table and writes its
slot (+1, so an old pack reads none) into the material record's spare bytes; the loader reads it per mesh
(`[normal] <pack>: N of M meshes carry a normal map`) and the array rides the load-time mesh permutation
(the fifth such array). Every draw path that binds a base colour binds the normal map on unit 1 through
`nrmBind()`, and the two mesh shaders perturb the shading normal through a tangent frame built from screen-
space derivatives (`OES_standard_derivatives`; no tangents in the pack, and a flipped V is handled by the
derivatives). `dressglb.py` carries a garment's normal map too. `REVA_NONORMAL=1` for the A/B.

Receipt: `v1_anima_cargo` (the Sketchfab pants carry one) at a hip close-up, fixed step: 3.08% of the frame
differs, all of it on the pants — pocket flaps, seams and wrinkles shade; without, the fabric is flat.
⛔ Anima's own UE export carries NO normal maps (0 of 35 materials): the artery loses UE materials, so her
skin gets one only when `ue_export_textures.py` is taught to fetch it. Roughness/metal and PBR are next.

### 21 · One body type — NOT DONE (09-11), the plan
The audit's first item and the one this session did not close. What changed: every capability built today
was written ONCE as a function every path calls — `poseOverlay`, `eyeMorphAim`, `sunStash`, `nrmBind`,
`clothPrep`, `motion energy`, `colResolve` — so the three paths (crowd slot, scene cast, set actor) now
share their new behaviour instead of re-implementing it, which is the unification done bottom-up. Still
separate: the clip election (three), the pose-to-palette loop (three), the draw loop (three), the head
layer's call sites (two + one missing on the crowd). The first real step: one `bodyPose(Pack*,clipSpec,
phase,Q,T,S)` that owns sampling + overlay + head layer + energy, called from all three; then one
`bodyDraw(Pack*,pal,model,tint)` that owns the mesh loop with cloth, tint and the normal map; the crowd
slot, cast and actor then become three OWNERS of state, not three renderers.

## THE SKATE PROMO CAMPAIGN (09-19 → 09-20)

Four promo cuts for jacob's skating game -- `scenes/sk_promo`, `ep2_lines`, `ep3_session`,
`ep4_london`; takes on `D:/reva902/robot_neko/promo/` -- each one asking for something the
engine could not do. All of it is in `crowd_new.exe`; `crowd.exe` has not been promoted since
09-11.

### 22 · The ragdoll — DONE (09-19 → 09-20)
**The gap.** A fall was a clip (`z_rm_sk_bail`): the same fall every time, from wherever the
body happened to be, at no speed. jacob: "we will need rag doll phisics for when they fall".

**The fix.** Any body, any time, goes limp: 18 linked points (verlet -- the hair chains'
solver), bone lengths, reach limits, the torso stiffened with its MEASURED cross-section (at
capsule radii she lay buried to her waist), each knee's and elbow's bend direction read off the
bail pose, hip and shoulder cones, self-collision at the contact pass's radii. Seeded from what
the body was actually doing (every joint's world velocity plus a 24-frame torso ring; a roll
about the axis across her travel puts her over the front instead of sledging on her chest).
Collides through the walking distance field and the audit grid's floor (she can land ON
furniture); Coulomb friction per Jakobsen 2003 (the tangential cut scales with penetration
depth and never reverses the slide); heavy drag once down, asleep after half a second still.
Half a link per step, six Gauss-Seidel passes, 6 ms substeps. Written back root first, the
chain solver's way, so fingers and twist bones follow. Cue: `hdoll=` / `pdoll=` / `p2doll=` /
`p3doll=` `<sec>` or `off`; gameplay: `ragdollGo(slot)`. The root freezes where she bailed, the
lenses track her hips, the walk and the contact pass stand aside.

Receipt: `scenes/rd_slam` -- cued at 4 u/s on real floor tiles: hips from ~0.9 u to under
0.45 u inside ~1.2 s, speed to 0, the lowest joint never below the floor by more than its
radius, and a body lying on the floor in the frames, not a pose. `rd_freeze` (REVA_RDFREEZE=1):
the write-back alone moves the root 0.000 u and turns no bone. Switches: REVA_NORAGDOLL,
REVA_RDDBG (`[rd]` hips / lowest / floor / speed and `[rd:fast]`), REVA_RDFREEZE, REVA_RDHINGE
(the hinge-plane rule, OFF: it fought the bone lengths), REVA_RDNOCONE, REVA_RDNOSELF.
⛔ The joint limits go FIRST and the lengths have the last word; the other way round `[rd:fast]`
showed a knee at 13 u/s under a body lying still. ⛔ A flat friction multiplier was glue (97%
of her speed in one frame) and a speed-scaled one let her crawl for ever. ⛔ A missing toe is
not a missing body: the four extremities are optional (anima has no toe bones; the first cue
refused her). `g_frameNo` counts rendered frames so a simulation inside the draw steps once
per frame -- the doll, and the jiggle springs, which had been integrating twice on a two-pass
draw.

### 23 · The filmer's camera — DONE (09-20)
**The gap.** Every lens was a dolly, a tripod or an orbit; the game is about filming your
friends.

**The fix.** `camhold=filmer,subject[,fov[,aimh[,drop]]]`: the eye rides the filmer's wrist
(the contact pass's published forearm), raw -- THE BOB IS THE SHOT -- with only the aim sprung
(9 rad/s); `drop` is the low pass; the filmer's body is not drawn. `feed=1|2|0` is her phone
(portrait / sideways) or a tripod: the post pass squeezes the frame and adds fall-off, sensor
noise, a REC dot and a seven-segment timer drawn in the shader, all after the tone knee.
`fish=0..1` is an equidistant fisheye as a projection (r = f·θ over a rectilinear render whose
field the 3D pass opens toward 120° diagonal -- a fisheye is a WIDE lens as well as a bent one).
`z_rm_sk_film` is the ride with the camera arm out. ⛔ Springing the eye smooths away the
footage. ⛔ Warping at the authored fov moved the field six degrees and looked like nothing.

Receipt: `scenes/feed_test` (the frame bobs with her body and the subject stays in it; CONTROL:
a camtrack at the same distance), `feed_park`, then the four cuts.

### 24 · Lines obey the world — DONE (09-20)
**The gap.** A path was a spline nothing checked; ep 2's filming line had ten samples up to
1.13 u inside a bench. And a stance that crossed the legs drew one thigh through the other.

**The fix.** Every interior path sample is pushed out of the walking distance field at 0.42 u
(`[path] slot N: M of K samples were inside the set`); the `room` scan, already in the audit grid
off its million triangles, pushes a line the way it pushes a walk. The contact pass keeps left
leg against right, capsule to capsule at 0.88 of the measured radii, so legs may touch.

### 25 · The cloud deck, and the T-pose alarm — DONE (09-20)
A drifting fbm cloud deck in the skybox and the water (jacob: "add a skybox etc"). And a
missing partner clip -- a T-pose for the whole line, half of promo ep 3 -- is a boxed notice at
the first miss and a count at exit, where `[stage] … NOT FOUND` had said it once and quietly.

## ROBOT NEKO ep 13 — GOT IT (09-25)

### 26 · A route around a body, and a path handed on early — DONE (09-25)
**The gap.** A path was a list of set marks: a filmer circling a fallen skater needed to know where the slam would
end, and two takes of the same fall came to rest 8 u apart. And a path handed on early (the last one ran out before
the next line) started at the old run's speed under the old line's loop flag, lapped once and stopped for good.
**The fix.** `hpath=@around:<slot>[,<radius>[,<points>]]` in `locoPathBuild`: a closed circle of marks round where
that slot IS (its hips when it is a ragdoll), entered at the point nearest this body, run the way she is already
travelling, rebuilt round the body's current position every lap; the body keeps the mark it was placed by (a
synthetic mark name went through the hero's placement and parked reva at z −150). The early hand-off adopts the
next line's `hspd=`/`hloop=` and pins them (`g_locoEarly`) against the lines in between, whose carried values are the
run that just ran out. Receipt: `[path] slot 0: … starts early: rolling in at 6.60 u/s, to that line's 2.40 u/s,
looping` then `a 8-point circle of radius 3.2 round slot 1 at (0.29,32.98)` re-centred at `(-0.50,30.93)` and
`(-1.04,28.32)` on laps 2 and 3, on the 8 fps take.

### 27 · The doll rests — DONE (09-25)
**The gap.** Lying face down with a leg folded, the ragdoll crept BACKWARDS 0.7 u/s for seven seconds with the
phone on her and a velocity term of nothing: a hip cone violated for good turned the limb a couple of degrees a pass,
the bone lengths pulled the hips after it and the floor pushed back — a ratchet the fastest-joint rest test never
saw (a shoulder jittering at ~1 u/s kept it from passing).
**The fix.** The cone rule stands down once she is down and slow (`settled9`); the rest test reads the hips'
VELOCITY (`hsp`), not the fastest joint and not the displacement. Receipt: the `[rd]` trace and `settled after` on
the take. Residue: the audit still reads her hand 0.39 u into the floor on the impact frame at 8 fps.

### The skate foley — DONE (09-25)
`roll` / `pop` / `land` / `slam` in `skate_src/robot_neko/foley.py`: band-limited wheel noise with the four
wheels ticking a seam, a tail pop, two trucks landing, a body hitting concrete with a slide and a board getting away.

## ROBOT NEKO ep 14 — WARDROBE (09-26)

### 28 · Premade clothes on a finished pack — DONE (09-26)
**The gap.** jacob: "if you could possibly get some clothes on kaeli". The only garment path (`dressglb.py`,
§16) dressed the body's GLB, and a GLB must be BAKED into a pack -- which drops everything authored on the pack
since (her face and morph vocabulary, every skate clip and board channel). The cloth solver is PARKED.
**The fix.** `skate_src/author/dresspack.py <pack> <garment.glb> --wear Mesh:kind[:MaterialMesh] ... --out <pack>`:
append-only on the PACK (header field 14, the morph table, kept). A garment is fitted by measured landmarks
(pants: waist and ankle heights, width off the hips, each leg onto its bone line; tops: strap line onto her
shoulders, width off the chest, hung off her BACK), pushed out of the skin (tops RADIALLY below the armpits,
then DRAPED off bulges at a 0.5 slope; the waistband CINCHED; every push dilated so a lining moves with its
outside), tucked into a boot mesh (`--tuck`), weighted with the SKIN's own weights (nearest skin verts of the
bones that garment may ride; below the crotch a leg rides only its own leg, sided by its PANEL), and the skin
it covers REMOVED from the body's index buffers (a ray along the skin normal, or behind the nearest cloth when
that cloth is not an opening -- a boundary edge, or where the garment ends in that sector round her). Thumbnail
CAPS (fan discs closing a garment's openings, valence >= 16) are dropped; a garment turned in its Sketchfab scene
is refused. Engine: a `~worn` mesh is drawn and shadowed like the body and never MEASURED as it
(`meshIsGarment`: the limb radii, markPick and the cloth collider skip it, as they skip `~cloth`).
Receipt: `packs/v1_kaeli_street.rpack` (v1_anima_outfit + the Sketchfab camo cargos and green tank):
43,007 skin verts hidden, bra and panties hidden, `packcheck` clean; four-angle benches in the T-pose and on
`z_rm_sk_ride`, a front and a rear close-up at full resolution (`D:/reva902/robot_neko/ep14/`), then ep 14.
⛔ Six fitting rounds, each found on pixels: the scene's 45-degree turn, a bib (centroid centring), the caps,
a fold under the bust (four A/Bs -- skin, normal map, shadow, caps -- before the radial push), holes at a rolled
hem, cloth between her knees in motion. Open: two small marks at the tank's lower bust, the waistband's lining
edge reads as a light band.

## CLANKER (09-27)

### 29 · A skate library onto another body, board and all — DONE (09-27)
**The gap.** jacob: "His name is Clanker. let me see him in engine, in the skate park, skating". Clanker
(`skate_src/robot/build_robot.py`, 1.72 m to the antenna tip, hips 0.61 m against reva's 1.05) arrived with
no clips, and a scene's cast plays clips from its OWN pack (a borrowed partner clip is a T-pose). The skate
library and its board channels were authored on reva.
**The fix.** `skate_src/author/retarget_skate.py <donor> <target>`: each frame through the canonical transfer
(retarget.py, the engine's donorPoseF in Python) and baked into the target pack. Limbs keep ABSOLUTE semantics
(their rests really differ: her arms out, his hanging); the TRUNK takes the donor's movement from rest --
⛔ absolute semantics laid reva's posture (a neck resting ~25 degrees forward) on a pivot under a cylinder and
cocked his head 40 degrees on every frame; the head keeps its whole TURN and a quarter of its TILT (a control
bench showed reva's authored head really rolls 37.5 degrees down the board -- on a cylinder, a broken neck).
Feet and hands (not in the canon) by deviation from rest; the hips' travel scaled by the leg ratio (0.605);
each `<clip>_bd` re-derived from HIS toe-joint midpoint with the donor's board rotation.
Receipt: 14 moves + 14 board channels in `packs/v1_clanker.rpack`; `scenes/clanker_skate.txt` on skate10
(`D:/reva902/clanker/clanker_skates.mp4`, 60 fps); the clip audit reads 40 of 1,798 frames with a wheel up to
0.093 u into the floor -- the carve dip already open for every rider.
**Handed on.** `rpack2gltf.py --mesh` writes a pack's own skinned, textured body into the glTF (materials
from the source file matched by geometry, not order); Blender and Godot 4.7.2 re-read him within 3 um of
the engine's skinning. He is in the aigameengine kit at `godot/characters/clanker/`.

## X3D (09-27)

### 30 · Hair that feels the world -- DONE (09-27)
**The gap.** jacob, of x3d: "could you give her hair physics". The H1 chain solver (verlet, a spring toward the
animated pose, measured gravity) mined any chain named for hair -- but it (1) ran for the scene cast only, never a
set ACTOR, so a body in a crowd or on the bench's sheet had rigid hair; and (2) lived in the body's own frame, so
hair only answered POSE changes: a skater rolling, carving, popping and landing, and her hair could not know.
Measured on x3d as the scene hero: tip lag mean 1 cm, max 7-8 cm over a run of tricks -- rigid.
**The fix.** Actors take chain slots after the cast's (`CH_ACTORS` 16; past that it says so, once). Every body
PUBLISHES the rig->world matrix it is drawn with (`chainPublish`), and the solver carries its state through the
body's motion between the last two published frames (one frame late: invisible, and no draw path was reordered).
Inertia comes for free -- the hair swings on a carve, lifts on a pop, settles on a landing -- the damping, now on
WORLD velocity, is air drag, and gravity is the world's down in her frame (a rider pitched on a pipe hangs toward
the ground). One step per frame per slot (`g_frameStamp`): an actor drawn once per panel stepped four times a frame
on a four-angle sheet. A jump past 0.35 u or 30 degrees in a frame is a cut, not motion.
Receipt: x3d (`skate_src/x3d/build_x3d.py`: three four-bone back chains rooted in Spine2 + a front-lock chain in the
head, `chains: x3d x4 lens=[4,4,4,4]`), the skate10 run as hero, REVA_CHDBG: at 30 fps rig-only mean 1.0 cm / max
8.5 cm vs world 2.0-2.5 cm / 17 cm; the 60 fps take mean 1.6 cm, worst 12 cm; frames of the kickflip side by side.
Controls: `REVA_CHWORLD=0` (the old frame), `REVA_ACTORCHAINS=0` (no actor chains).
Open: the solver's spring is first-order, so its steady trailing scales with the step (twice at 30 fps what it is at
60); an actor's own `sc` enters the gravity only through its published matrix (right) -- the pre-publish frames use
the pack scale; no hair-vs-body collision (the spring keeps it near its pose).

## R.E.V.A. 902 (09-29)

### 31 · A wardrobe: clothes as separate pieces, skin that hides under them -- DONE (09-29)
**The gap.** jacob, of REVA's street sheet: "id like the clothing to be modular, reva is also a seamstress, she loves
making new clothes". Every body in the engine was one fixed set of meshes: a garment was either baked in (a region of
the skin) or dresspack'd on with the skin under it REMOVED (§28) -- never taken off again.
**The fix.** The atelier (`skate_src/reva902/atelier.py`, design: `design/atelier.md`) builds each garment as its own
mesh on the one skeleton and MEASURES which skin it covers (a ray from every skin vertex along its normal; a face is
covered only when all its corners are). The skin splits into groups by the set of garments over them, and the mesh
names carry it all: `wear~<slot>~<piece>~<outfit>+<outfit>` and `skin~h=<pieces>`. The engine reads the tags AFTER the
load-time mesh reorder (the fifth per-mesh array that would otherwise point at the wrong meshes) and keeps `meshOff[]`:
pieces not worn, and skin under pieces that are. Every draw and measuring loop (13 of them) skips an off mesh; a
`wear~` mesh is clothing, never measured as the body (`meshIsGarment`). `REVA_OUTFIT=<name>` or the pack's first
outfit; ctl `outfit=` / `poutfit=`, `wear=+piece,-piece` / `pwear=`. One outfit per pack in v1.
Receipt: `packs/v1_reva902.rpack` -- 24 pieces, 2 outfits (`base`, `street`), 16 skin groups; the bench of both
outfits riding (`D:/reva902/reva_sheet/bench_street_ride.png`), the lab log `wears 'base' (10 pieces)` -> ctl
`outfit=street` -> `wears 'street' (17 pieces)`. Godot's `ClubWardrobe` reads the same tags.

### 32 · Cloth that swings natively, soft mass on actors, and a chain budget that says when it is full -- DONE (09-29)
**The gap.** jacob: "i want cloth physics, and boob jiggle physic". The particle cloth (§16) is parked, and runs in
one engine only. The chain solver swung only names that meant hair; its budget was a SILENT 8 per body (REVA carries
16); the jiggle (bones named `breast`, `jiggleTick`) ran for the hero and the cast, never a set actor.
**The fix.** A garment's free panel is rigged with `Cloth_*` chains (her jacket's hanging body on five, her bandana on
three) -- the miner's new family `cloth` -- and swings on the same solver as her hair (world inertia, drag, gravity);
Godot's `ClubHair` mines the same names onto SpringBoneSimulator3D with cloth settings, and `breast` bones with stiff,
nearly weightless springs: the same physics in both engines, natively. The chain budget is 32 (`CHMAXC`) and prints
when a body fills it. Set actors carry jiggle on their chain slots (`g_jgP[CH_SLOTS]`).
Receipt: the lab log `chains: reva902 x16 lens=[3,...]`, `[jiggle] reva902: 2 jiggle bone(s) -- BreastL BreastR`,
`1920 bone-frames, largest swing 0.050 u`; in Godot `ClubHair: 18 chain(s)` (16 + the two breasts).
Open: cloth chains do not collide with the body (the spring keeps them near their pose); the per-vertex cloth (§16)
and a SEWING simulation (panels cut flat and sewn onto the form, stitch by stitch) are the atelier's next backend.

## THE WORLD AT THE END OF TIME (10-18)

### 33 · The toon pass: cel shading and ink, per pack -- DONE (10-18)
jacob's look for the Pohl series (design/wateot_series.md): Atlantis-leaning design, Futurama rendering. A pack
carrying a node named `ToonStyle` is drawn in it; nothing else in the club changes. In the body fragment shaders
(fsO/fsC): `uCel` cuts the same key light and shadow-map term into TWO tones -- lit, and a cool shadow tint that is
gentler on dark colours (Wanda's face must read in shadow) -- with the light's brightness capped at 1 (a street's
sky+sun blew a white robe out; hue kept, so dusk still darkens), and a BILINEAR shadow-map test so a cast-shadow
edge is a contour, not texels. The INK is an inverted hull: each toon mesh drawn again, front faces culled, pushed
out along its normal by `uInk` -- a share of the viewport's height, in clip space, so the line holds its width at
any distance (ANGLE here has no depth texture, so edge detection was never an option; Godot toon shaders draw the
same hull). Hooked at every body draw site (actors, partner, crowd, lab hero, wall, select screen, props), never
the shadow passes; the face's drawn shapes (`~face~` meshes) get no line. Knobs: `REVA_NOTOON=1` (the control),
`REVA_TOONCEL=` (where light turns to shadow, 0.30), `REVA_TOONINK=` (0.0020). Receipt: `[toon] cel 0.30 ink
0.0020`; Amelia's face close-up: one smooth S-curve terminator. ⛔ The staircase down that face was NOT the shadow
map (shadow=0 kept it): the head's profile table was read with lerp_table, a crease at every row, and a hard cel
cut turns creases into ledges -- people/person.py now reads it with a monotone cubic (`smooth_table`).

### 34 · True size -- DONE (10-18)
Every pack was normalised at load to a 3.15-unit bone span, which suits a crowd of mismatched downloads and made the
series' five-year-olds as tall as the twelve-year-old ("kids are too big lmao"). A pack carrying a node named
`TrueScale` was built in metres at real size and takes the club's 1.75 units a metre instead. Receipt: `[size]
<pack>: TRUE SIZE ...` per pack; the cast's group shot.

### 35 · Cloth meets the legs -- DONE (10-18)
A chain whose root is named `Cloth*` is pushed out of a capsule round each thigh and shin, as thick as the body
measured itself (`measureBoneR`), then its lengths are re-solved -- the collision §32 left open and the parked
REVA 902 coat was waiting for. Hair does not collide. `REVA_CHCOLLIDE=0` is the control, `REVA_CHCOLR=` scales the
capsules, `[chdbg]` counts pushes. Receipt: Amelia's robe in a run, ~6 pushes a second on, 0 off. ⛔ Collision moves
chain NODES only: skin blended between a raised thigh and a hanging chain lands INSIDE the thigh whatever the
nodes do, so an OPEN robe's front carries no thigh share at all (people/costume.py `thigh=0.0`). Control on REVA
902's street outfit walking: 40 px of 4.2M differ on a 48-frame sheet -- in scope, and nothing else moved.

### 36 · Cloth drapes over the legs, hair never sinks into the head -- DONE (10-18, second pass)
jacob, on Amelia's run: "leg is going through the coat, why is the coat not moving aside for the legs? i think the
hair is clipping through the head too" -- and after my first pass, which I called fixed off a DOWNSCALED contact
sheet: "you need to actually look at your work before telling me it is better. the leg is still going through the
coat and the hair is still going through the head". He was right on both. The judge for clipping is now FULL-
RESOLUTION crops of a burst (8 frames x 3 cameras for cloth, 10 x 4 for hair), the control (`REVA_VCOLLIDE=0`) in
the row above.

A pack carrying a node named `VertexCollide` is drawn with two rules in the body vertex shaders (vs and vsDQ, in RIG
space before the model matrix, so the ink pass follows):
- THE DRAPE (skirts, `~skirt~`/`~cloth`). Cloth HANGS over a leg: along each vertex's own horizontal way out of the
  body (from the hips), it must lie past every part of each leg capsule at or ABOVE its height -- nine spheres a
  capsule; beside a sphere, and above one below it, the reach falls off at a SLOPE (1.2 in per 1 out), the tent's
  sides; no push at the waistband, full push from a third of the way down to the hips; the thigh capsule starts a
  fifth of the way down (its hip-joint sphere reached out of the hip). Both cloth walls move by the same amount (the
  outer, told by its REST normal, keeps its 4 mm). At rest it changes NOTHING: 0 pixels of 921,600 differ on/off,
  front and side, T-pose.
- HAIR NEVER INWARD (`wear~hair~`). Out of the neck capsule, and never nearer the head's centre (in the measured head
  ellipsoid's normalised frame) than where the vertex sits at REST -- at rest the hair covers the head, ears and all,
  so swinging cannot uncover it. The chain joints get the same rule against the animated pose. Chains whose root is
  named *Stiff* spring 3x harder (Amelia's bob: `Hair_StiffBob_*`).
What the first pass got wrong, each found on full-resolution frames:
⛔ PUSHING ONLY WHAT IS INSIDE THE LEG. A knee that had gone fully THROUGH a panel left its vertices behind it,
untouched; triangles between pushed and unpushed vertices stretched and flipped and drew INK.
⛔ A 4 cm FADE UNDER A 30 cm LIFT. The forward stride sheared the panel into fins (crumples, a crack across it).
⛔ THE CHAINS WERE PUSHED TOO. A cloth joint pushed out of the leg on the CPU swung the chain's fixed length up like a
flap hinged at the hip: the hem rose to the knee and left a flat underside -- a line across the leg from the front, a
box from the side -- that the shader could never pull down. §35's joint push is now OFF for VertexCollide packs.
⛔ THE LINING'S OUTLINE. At a grazing angle the inner wall's ink, nudged 1-2 px on screen, landed IN FRONT of the
outer wall 4 mm away: black bands (gone with REVA_TOONINK=0, which proved it). The lining draws no outline
expansion now; the ink pass has a slope-scaled depth bias (2.5, 4).
⛔ THE HEAD ELLIPSOID SITS INSIDE THE SKULL AND LEAVES OUT THE EARS: the swinging bob slid over the ear (a sliver of
skin and the ear's tip through the curtain, mid-run, side view). Hence the rest-relative rule above.
Kept from the first pass: hidden SKIN is measured (measureBoneR skipped skin under a worn garment: Amelia's thighs
measured nothing); skirts 72 x 22 (no knee between vertices); a bob swings only its lower half.
Knobs: `REVA_VCOLLIDE=0` (control), `REVA_VCOLR=` (scale the capsules: x4 balloons the robe -- the test that the push
runs at all), `REVA_VCOLDBG=1`. Open: a back-kicking heel fans the coat's back out to the heel; below a raised knee the
cloth hangs straight down to its own hem (stretched, not shortened); the open front's edges flap around a knee coming
through the gap; Amelia's side-swept lock flicks up like a horn in a run (inertia, not clipping).

### 37 · A skirt is cloth: the hanging-cloth solver -- DONE (10-18, third pass)
jacob, after the second pass: "hair is better but this labcoat needs to decide if it is a physical object or not". The
drape (§36) moved a SKINNED skirt in the vertex shader and could only push sideways, so the coat took shapes no cloth
can take: a flat flap at the hem over a raised knee, the cloth below the knee stretched to its own hem, the back fanned
out to a kicked heel. Now every `~skirt~` mesh on a VertexCollide pack is SIMULATED in world space (crowd_kms.c, THE
HANGING CLOTH; hooked at the top of clothPrep, so the hero, the scene cast and every set actor get it):
- particles on the OUTER wall's points (welded across UV seams). The lining is not simulated: it is drawn a wall's
  thickness behind its outer point along the outer surface's normal, so the walls never cross or part.
- the WAISTBAND is every point with no Cloth_* chain weight, riding the skin; the mesh's edges do not stretch; XPBD
  bends across every edge (compliance 3e-7: twill that swings in panels -- 2e-5 crumpled like a shirt); a TETHER from
  each point to its nearest waistband point at its rest distance along the cloth (a hem can be LIFTED, never dragged
  below its own length); gravity at the body's own scale; quadratic air drag across the cloth's face; 8+ substeps a
  frame with the waistband, the hips and the limbs interpolated through it.
- the body: THE HIPS HOLD IT OUT (in the hips' rest frame, above where the thighs begin, no point nearer their axis
  than it was cut); THE SKIN UNDER IT, the back-facing half (buttock, backs of the thighs, calves -- skin hidden under
  clothes included), off its tangent plane; capsules on the thighs, shins and feet (heel to toe, from the ANKLE).
  Contacts are inelastic, with friction (0.25 static / 0.15 kinetic).
- AT REST THE COLLISION MOVES NOTHING: each point's rest penetration of any margin is its own allowance, so a tight
  garment is never shoved off the body it was cut on.
- a new coat is hung in its cut and the body swung from rest into its pose over half a second (the PRE-ROLL), so a
  take that opens with a knee raised does not open with the knee through the cloth; a jump of half a metre re-hangs it.
- drawn from its own buffers (two bodies in one pack are two coats) with an identity model / palette / bind; colSet
  marks it uColOn 3 (no drape, and its lining -- facing the body -- draws no outline); it casts (sunStashW).
What broke on the way, each found on full-resolution frames:
⛔ ARM CAPSULES AT THE WAIST. The forearms swing across the waistband: pushed out the shortest way they crumpled the coat
up over the hands; pushed inward they dragged the whole skirt into her (stretch x3). Off by default; the hands alone
are `REVA_SKARMS=1` (2: the whole arm).
⛔ A PUSH THAT THROWS. A 30 mm correction in a 2 ms substep is 15 m/s, and Verlet keeps it: the coat flew over her
shoulders. The correction never becomes speed off the surface now.
⛔ footL IS THE TOE on these rigs (the lowest bone under the knee): the shin was drawn to it and the foot had no capsule,
so the heel went through the back of the coat. The ankle is the knee's own child on the way down.
⛔ THE SKIN LAYER OVER THE WHOLE BODY balled the front panel up in the hip crease (the underside of the belly and the top
of the raised thigh as planes) and balled Viktor's tight shift up standing STILL (the inner thighs across the crotch:
6 cm "inside" one, then the other). Back-facing skin only, and a contact deeper than three clearances is a wrong plane.
⛔ THE ARMS' SKIN: the rest pose hangs the arms at the hips, so "skin from the hem to the waist" took in the hands.
⛔ FRICTION 0.6: a rising thigh carried the front panel up into a wad at the hip.
Verified: the run, 8 frames x 3 cameras, the new coat above the control (`REVA_SKIRTSIM=0`) at 1:1
(`D:/worldattheendoftime/production/cast/coat_NEW_*`, `coat_NEW_vs_OLD_*`: every TILE labelled NEW/OLD at a size readable at 25% -- jacob twice cropped the control row of a sheet whose row labels were 5 px on his screen); the rest pose against the cut (1.9% of pixels differ -- the lit
band is wavier, the cloth settles into faint folds); all five of the cast walking (Viktor's and the boys' freezer
shifts, Wanda's tunic skirt). crowd_new.exe only.

**Second round, the same day** -- jacob sent three crops (her hand hanging at her hip, its lower half INSIDE the coat:
located by template match in the new back-quarter sheet, frames 520 and 536), a screenshot of my own "still not right"
list, and, mid-way: "the kids, what they are wearing, please don't make it come up like this, that's not how physics
work, something is off here". All of it was real:
- THE HANDS press the coat IN toward the body: her hand's own skin (~250 sampled points) against the cloth's points and
  its outer TRIANGLES (a finger is thinner than the 2-3 cm between cloth points -- point-only pushing let fingertips
  through as islands), floored at the skin under a pelvis point (`floorR`: the body's radius there + ease + wall);
  hips before hands, so a hand on the hip wins over the cut. At jacob's crops the whole hand now lies on the coat.
- THE AIR OF HER OWN RUN: the drag is on the cloth's speed through the air, and the planted foot measures the ground
  under her (3.5 m/s on the bench's sprint; ~0 on a path, where the body really moves). The coat now streams behind
  her instead of the knees flinging the panels wide. Drag 1.3 (twill DOUBLE-walled, ~450 g/m^2): 3.6 blew it level.
- 16 SUBSTEPS (1/960 s) and bend compliance 1e-7: the front panel swings as a broad sheet instead of balling up at the
  hip when the knee is highest.
- THE LEGS AS THEY ARE: capsules at the measured radius (x1.0, not x1.06), off it a garment's ease where the leg's skin
  is hidden in this outfit (Amelia, Wanda: 10 mm) and a hair where it is bare (Viktor, the boys: 6 mm).
⛔ THE BOYS' SHIFTS RODE UP TO THEIR HIPS. Two causes, both mine: the leg capsules were ~60% fatter than a child's leg
(measured radius, already 15% outside the skin, x1.06, + 1.2 cm), so a tight shift could not go round both legs in a
stride and was pushed up; and the back-skin layer tested the shift's FRONT -- pulled back between the legs, its nearest
skin was the backs of the thighs, facing away, and it was shoved 35 mm back through the leg every substep (stretch
x8). At 8 substeps the cloth stretched instead and hid both. Now: legs as they are; only cloth cut on the back half
meets the back skin; a contact deeper than 1.5 clearances is a wrong plane.
Verified: Amelia's run 8 frames x 3 cameras (`coat_NEW_*`, `coat_NEW_vs_OLD_*`, `coat_hands_your_crops_before_vs_now`),
Freddy and Billy walking a full stride x 3 cameras (`freddy_shift_NEW_*`, `billy_shift_NEW_*`: hem at the knee every
frame), the cast walking, the rest pose (1.4% of pixels differ). Knobs: `REVA_SKIRTSIM=0`, `REVA_SKIRTDBG=1|2|3`,
`REVA_SKBEND`, `REVA_SKSUB`, `REVA_SKDRAG`, `REVA_SKAIR=0`, `REVA_SKHANDS=0`, `REVA_SKLEGM`, `REVA_SKLEGR`,
`REVA_SKMU=s[,k]`, `REVA_SKARMS`, `REVA_SKSKIN=0`, `REVA_SKCONE=0`, `REVA_VCOLR`. Bench `sets/_wateot_kid.set` (Freddy).
Open: a sprint still folds the front panel at the hip for a few frames when the knee is highest and the air presses it
back; the coat's worst single edge reads ~1.3-1.5x where a hand presses it in beside the pinned waistband; on the boys
~1.4x (the hem round both legs in a stride); the cost is 16 substeps a frame per garment (fine on this machine, not
measured on the Pi); Godot still swings the chains.

### 38 · The ship's sets: a set prop in the show's look -- DONE (10-18)
jacob: "Start with the sick bay set for scene 2.1: Star Trek style, the warming pans, the bath, the boys' pan of milky
fluid." The set is built by code (`skate_src/ship/build_sickbay.py`, numpy + PIL, no Blender) and drawn by four
MARKER nodes a set prop can now carry (`skate_src/glb2rpack.py --marker NAME`, read in loadPack):
- `ToonSet`: the toon pass (§33) lit as a ROOM is lit -- the cel cut taken on the surface's facing UP (0.5 + 0.5 N.y)
  times the sun's shadow, not on the sun's direction. Every wall in the light, every underside (the ceiling, a
  saucer's belly) in the shadow tone, cast shadows from the sun's map. The bodies keep the key (`sun=`), so an
  interior's key is a cheat from the lens's side and the room does not care where it comes from. (`uHemi`, set by
  `toonSetLook` in drawProp.)
- `Glow`: drawn unlit at its own colour (`uEmis` 1), no ink -- the light panels, the strip and the cove over it, the
  LCARS screens, the emitter's face, the cubicle's lit inside, steam.
- `NoCast`: never in the sun's shadow map -- the shell (a ceiling over everyone blacks out the room), the hoods (they
  ARE the light over the pan), the milk.
- `Open`: never a collider. propSolid decides by NAME, and a run of fittings built in room coordinates measures as ONE
  circle as wide as the room, at its centre.
Ink needs one normal per vertex position (the inverted hull cracks open at every hard edge), so the builder makes
everything from rounded boxes (quarter-round edges), lathes with support loops at the profile's corners, and swept
tubes; winding is made CCW about the normals (the ink culls front faces).
⛔ OMNI PRACTICALS UNDER THE HOODS turned the ceiling and the milk PINK (the engine's lamps have no direction, and an
emitter a metre under the ceiling lights it; infrared is invisible anyway). The set has no practicals: every lamp in
the room is a Glow surface.
⛔ A grey ceiling under the shadow tint (x 0.50, 0.47, 0.66) came out saturated PURPLE: colour a surface that lives in
the shadow tone FOR it (the ceiling's base is yellowish, so it lands on a neutral warm grey).
⛔ A pan of milk read as a TABLE TOP until the bowl's wall was darker than its floor and the milk was cream
(so a glint can be brighter than it); two glasses with a pale band at the brim read as PINTS (cocoa to a finger under
the rim, and steam).
Scope: the committed engine and this build shot the same frames of `_wateot_cast` (the toon cast walking, simulated
cloth) and `parsons` (the town) at REVA_FIXEDSTEP=60, each twice: 0.000% of pixels differ run to run AND build to
build. Only a pack carrying a marker changes. crowd_new.exe only.

### 39 · Bodies that lie down: a set actor asleep in a warming pan -- DONE (10-18)
jacob: "The sick bay looks right. Commit it, then make the boys' huddle in the milk and Viktor waking in the pan." Two
moves, authored by code against the set they lie in (`skate_src/people/moves/`: `lying.py` -- the body, the bowl's
profile, the milk, mesh-against-mesh depth; `wake.py` -> `z_rm_wake` on v1_viktor, 9 s, played once; `huddle.py` ->
`z_rm_huddle` on v1_billy and v1_freddy, a 6 s loop; `bake.py` restores each pack from its CLIPLESS base before the
clips go on). They are played by SET ACTORS, which until now had only stood, walked, ridden and fallen:
- A `z_rm_` CLIP OWNS ITS ROOT ON A SET ACTOR, as it already did on the hero and the scene cast: no push out of the
  set's colliders (a boy in a pan is INSIDE the pan's collider), no foot plant, no Williams layer.
- A `z_rm_` TRANSITION PLAYS ONCE AND HOLDS its last frame (clipHoldsEnd): Viktor wakes once, he does not fall asleep and
  wake again every nine seconds. A `z_rm_` loop (the huddle) still loops.
- A `z_rm_` clip STARTS AT ITS FIRST FRAME, and under a capture it WAITS for the shot as the set clock does -- a burst
  opens on a boy asleep, not one already awake, and two bodies posed TOGETHER (an arm across the other's chest) play in
  step.
- SET ACTORS BLEND THEIR OWN CLIP'S MORPH TRACKS (type 3, per-actor weights through morphBlend). The hero and the scene
  partner always did; nobody on a set could shut his eyes. `REVA_MORPHFORCE` takes a list (`blink_L=0,blink_R=0`).
- THE HANGING CLOTH (§37) ON A BODY LYING DOWN: a FLOOR at the plane the body stands or lies on (its model's ground),
  checked FIRST so the body's passes have the last word (cloth squeezed between a body and the floor goes out of the
  body); a LYING body's hands (its hips' up more than 60 degrees off the world's) are SOLID -- cloth leaves a hand the
  shortest way, its POINTS and its TRIANGLES (every hand point kept rh off any outer triangle, the triangle moved away
  by its corners' barycentric shares: over a hand lying under the hem, under a hand resting on top; `REVA_SKLYTRI=0` is
  the control), no cone floors; a point that goes NON-FINITE is put back on its waistband anchor and SAID
  (`[skirt] ... NON-FINITE`). A standing body's arithmetic is unchanged.
And the eyes: `skate_src/people/eyelids.py` (Blender) gives every cast member `blink_L`/`blink_R`, built in FACE
COORDINATES (person.face_xyz inverted): the eye's interior squashes onto the lower lid, the upper lid's ink slides down
by the opening. `--redo 1` rebuilds them.
What broke, each on full-resolution frames:
⛔ VIKTOR'S SHIFT BALLED UP AT HIS WAIST and threw a spike to the edge of the frame: his body sank into the pan floor
while the cloth's floor was checked last, so the two fought every substep (9,200 contacts) and a point went to
infinity. The skin under the shift now rests a cloth's thickness (12 mm) over the floor, the floor goes first, and a
non-finite point is reported.
⛔ THE PRESSING HANDS (§37) dragged a lying boy's shift up toward his waist -- the world-horizontal "in toward the body"
runs along him when he lies down -- and his own radial stretched it x8 against the leg capsules. Lying hands are solid.
⛔ HIS FINGERS WENT THROUGH HIS SHIFT (jacob: "more collision issues, this should be simple, shouldnt be hapeneing, if
collision was working right"). Two faults, both real. The ENGINE: lying hands pushed only cloth POINTS, and a finger is
thinner than the 2-3 cm between two of them -- now the triangles too. The POSE: his hands lay as the rest pose hangs
them, palm to the thigh and thumb up, 3.5 cm out -- the thumb stood in the shift where it hangs off the thigh, where
there is room for neither, and no collision pass can make room. They now lie PALM UP (the forearm turned 40 degrees,
the hand 30), 7 cm out, 5 cm clear of the thigh; the rest solve and the timeline lay the arm the SAME way (one pole,
from rest -- they disagreed about the elbow, and turned PALM DOWN first, the relaxed curl drove his fingertips 3.8 cm
into the pan: measured, refused).
⛔ "RUBBING HIS STINGING EYES" WAS FISTS AT HIS CHEEKS: it is the KNUCKLES on the eyes (the wrist aimed so the middle
knuckle lands on the eye), and the hands travel the straight line from the eyes to the chest (an IK blend between the
two poses swung the arms out through his sides).
⛔ HIS FEET WERE BACKWARDS (jacob): the relax (toes away from the head, 38 degrees) was added to the foot's OWN rotation,
and the rest pose lays the legs five times while it is solved -- the IK sets only the hip and the knee, so the foot
turned 190 degrees. Every re-laid limb now starts its foot from rest.
⛔ THE BOYS LAY ON THE MILK. A level lens on the boys alone (`sets/_huddle_bare.set`, `_huddle_milk.set`) proved the
engine had them exactly where the score put them: the milk (11.5 cm) was shallower than they were (knees bent up to
20 cm). The legs now lie nearly straight, laid down from the hip until a calf or a heel touches, and the milk is 13 cm
(build_sickbay `FLUID_Y` 0.67): faces, the tops of the chests, the arms and the toes break its surface. 15 cm was
measured first and refused: lying back, a boy's eyes reach 14.2 cm at most before his head goes into the bowl's wall.
⛔ BILLY'S SHUT EYES READ OPEN from across the room: the lid ink was picked by its distance from the lid, and his low,
heavy brow is within half an opening of it -- its lower edge slid down onto the shut lid as a dark wedge. Lid inks and
brows are told apart by OFFSET now (0.0015 vs 0.0016 out); all five rebuilt (60 lid-ink points an eye on every face).
⛔ BILLY'S HAND "ON HIS TUMMY" LAY IN THE MILK BESIDE HIM: a bone's +y runs ALONG a lying body, so "2 cm up spine_01" was
a point by his back. The target is the top of his tummy straight over the spine.
⛔ A "TINY RANDOM TWITCH" OF A QUARTER OF A FIST stood Freddy's fingers straight up at Billy's chin (two at once: 0.96).
Finger twitches are a flicker (0.08-0.16) and never stack on one hand.
Verified on the engine's own frames (REVA_FIXEDSTEP=30, every frame): Viktor 9.5 s from three lenses (close on his
near hand, three-quarter, overhead) and his face; the boys 6.5 s from the room, a mid lens over both faces and hands,
and overhead; the near hand with the triangle pass on and off; his feet old vs new; Billy's lids with the blink forced
off. `D:/worldattheendoftime/production/moves/` (`viktor_wake_*.mp4`, `boys_huddle_*.mp4`, the strips, and the OLD/NEW
sheets, every tile bannered). Measured in the score: every vertex of both moves over the bowl within 6 mm (Viktor's
heel; the boys' backs 4 mm; Freddy's inner upper arm, holding Billy's hand under the milk, 10 mm), both boys' lower eyes
12 mm over the milk, their bodies 0 mm into each other.
Scope: the committed engine (3b18059) and this build shot the same frames of `_wateot_cast` (the toon cast walking,
simulated cloth) and `parsons` (the town) at REVA_FIXEDSTEP=60, each twice: 0.000% of pixels differ run to run AND
build to build. Only a set actor playing a `z_rm_` clip, a set actor with morph tracks, and lying cloth change.
crowd_new.exe only.
Open: the sun's shadow map is 28 units across at 2048 -- a fold's shadow on a gown is a staircase in a close-up
(`shadowext=` smaller for the shot); Freddy's inner upper arm sinks 1 cm into the pan's floor under the milk, where
nobody sees it; the doctors still have no move. The huddle is finished as it stands (jacob: "much less focus on the
huddle please, possibly the least sci fi element here"). The lying hand-vs-triangle pass
moves the boys' skirts where their held hands lie under the milk (0.9% of a mid lens: a sliver of thigh above the
milk at Billy's hem) -- left as it is: "move. on. from. the . kids".

### 40 · The cast talks: toon mouths, three faces in one scene, and a clipless body in a scene -- DONE (10-19)
jacob: "Start with the mouths and a rough cut of scene 2.1 on temporary voices" (and, on the voices, "i like piper").
The rough cut is `scenes/wateot_2_1.txt` on `sets/_scene21.set` -- all 34 slots of the adaptation's scene 2.1 word for
word, Viktor (hero), Wanda (slot 1), Amelia (slot 2) and the narrator (the voice slot), 208.5 s
(`D:/worldattheendoftime/production/scene21/wateot_2_1_rough.mp4`). What it needed:
- MOUTHS (`skate_src/people/mouths.py`, Blender): the scene opens a speaker's mouth through the morph NAMED `jaw` (a
  body with a jaw bone takes the bone). A toon face draws its mouth as one ink line a hair off the skin, so `jaw` opens
  that line into a dark lens -- its lower edge drops by the opening (deepest mid-mouth, nothing at the corners, the
  widest 0.085 head heights for a 17-degree mouth), its upper edge lifts a fifth; the lower lip's shadow stroke and
  Wanda's filled lips ride with it. Found by OFFSET and face coordinates, as the lids are. It also adds `blink` (both
  lids under the scene's own blink clock's name). Bench: `sets/_wateot_faces.set` (the five faces in a row) with
  `REVA_MORPHFORCE=jaw=1` / `blink=1` against `jaw=0`.
- EVERY BODY IN A SCENE HAS ITS OWN FACE: one morph-weight array had served every slot after the hero, read through
  each slot's own morph table, so a third speaker's jaw went to whichever slot the draw loop touched last and slots 2-3
  never blinked. `g_castMorphW[SCN_MAXCAST][MORPH_MAX]` (g_partMorphW is the current slot's row); the speaker's OWN slot
  opens; every body blinks on its own clock.
- ⛔ A PACK WITH NO CLIPS OF ITS OWN CRASHED THE SCENE. labDrawPartner sampled its own clip 0 as a placeholder under a
  borrowed clip -- and Wanda and Amelia own no clip at all: clips[0] of an empty table, a segfault the first time either
  stood in a scene (gdb on a -O1 -g build named the line). An empty clip stands in.
- `z_rm_sleep` (a 3.6 s breathing loop whose frame 0 is the wake's) and the wake in TWO HALVES (`z_rm_wake_up`,
  `z_rm_wake_rub`, cut at 4.5 s), so a line can hold him asleep under the narration and put the rub on the line that
  says it instead of on "Mom?" (bake.py; the 9 s `z_rm_wake` stays for the set bench).
- TEMPORARY VOICES, Piper (`say_piper.py`): a multi-speaker model's ONE voice is `speaker=` in PEOPLE. The only installed
  model with men in it is LibriTTS (904 speakers); the four were picked by measured pitch and pace off one line said by
  113 of them (`D:/worldattheendoftime/production/voices/audition/`: men 99-152 Hz, women 170-255).
⛔ THE SHOT LADDER FRAMES A PAIR FACING EACH OTHER. Three people in a cluster by a dispenser got one identical frame for
fifteen lines, the backs of two heads in it; every lens of the conversation is placed (`cam=`) -- over-the-shoulder pairs
for Viktor/Amelia, then Viktor/Wanda once he turns to her.
⛔ A REVIEW SHEET'S CAPTION ON THE FRAME HID THE FACES: a 50 px bar over a 288 px tile covers a close-up's eyes, and the
first sheet read as "his face is cut off". Captions go above the frame.
Scope: the committed engine (f0d6f59) and this build recorded the same 6 s of a TWO-person scene (`porch`, reva and
kaeli on parsons) twice each: 0.000% of pixels differ run to run and build to build -- a two-hander's faces are slots
0 and 1 exactly as before. crowd_new.exe only.

### 41 · Deep space: a star sky, a ship at her true size, a glowing volume, and a scene that cuts to another set -- DONE (10-22)
jacob, on the New Mayflower: "where we can be exact to the book, it is important that we do so, our style is what fills in
what isnt specified". The ship is built by code (`skate_src/ship/build_mayflower.py`, every book fact in its docstring with
its page) and stands in `sets/space_mayflower.set` 2000 u out along +z (nothing parked at the lab's origin reaches a lens
framed on her). What the engine had to learn:
- SPACE. A set that says `space` draws a BLACK sky with its stars at infinity (`spaceSkyDraw`): procedural, on the cube's six
  faces (no pole pinch), most faint, "hues from firebox red to sapphire and white" (p23); `milkyway nx ny nz strength` is a
  pale haze with a dense field of fainter stars along the great circle round that pole; `star dx dy dz r g b size_deg [halo]`
  is one of up to 8 NAMED stars, a core and a glow (the flare star, blue-white, 30 degrees off her course; the yellower
  destination "right under that bright one", p24). ⛔ NOT the day sky with its colours turned down: that sky has a CLOUD
  DECK, and the first blockout floated in grey clouds.
- REACH. `far <u>`: a ship at her true size outruns a placed lens's 900 u (the scene's 140), so a set may push every far
  plane out (`fmaxf(..., g_setFar)`); `sun elev azim I [r g b]` and `sky r g b` in a set are its OWN light, applied as it
  loads -- the ctl line's own `sun=`/`sky=` still win (re-applied after the load).
- BEAMS: `beam x y z dx dy dz length r0 r1 r g b k [fade [thin]]` (u), up to 8 -- a glowing VOLUME along a line: the drive's
  ion haze that "fuzzed everything. Including the star they were heading for" (p24), a light shaft, a projector's throw. A
  gaussian tube whose radius runs r0 -> r1, thinned as it spreads ((r0/r)^thin; 2 = the same flux through a wider tube) and
  faded along its length ((1-s/L)^fade); drawn ADDITIVE after every opaque thing (it lights what is behind it, never darkens
  it). Each pixel INTEGRATES the density along its own ray, so it reads as a volume from any side -- and a lens looking DOWN
  the plume sees it gather into its vanishing point, which is the book's shot. The bound is a box drawn by its back faces,
  so a lens inside the plume still sees it. ⛔ 40 EVEN STEPS across a bound sized for the plume's far end came back as a
  string of beads at the nozzle (3.5 u across, a step of 70): each step is now 0.4 of the plume's radius where it is, and a
  sphere-traced stride outside it. ⛔ A solid thing INSIDE the box hides all of the beam along its rays, the part in front
  of it too (no OES_depth_texture here to stop the march at the surface).
- A LINE ON ANOTHER SET. `set=<name>` on a scene line plays THAT line on another set -- a cutaway, the ship outside the sick
  bay -- and the next line without one is HOME again: the set the scene was staged on, its light put back exactly as the
  scene left it (a cutaway set brings its own). Not sticky (compile_scene.py checks the set exists). The cast keeps its
  places while it is away -- a set with no marks says nothing and moves nobody -- and its marks resolve again on the way
  home; the home set's actors and props reload (packs are cached: "from 16 packs" the second time).
- AN ORBIT ROUND A POINT. `camat=x,y,z` makes a line's `camorbit=` circle that WORLD point instead of a body -- a ship has no
  spot to follow; the aim is the point itself unless the orbit says aimh. It rides with the orbit in the compiler (carried
  while the orbit is, dropped with it).
Scene 2.1's rough cut uses all of it: the narration's ship is the New Mayflower outside (2.1-10's middle sentence, 2.1-33's
crane onto the screen's lens, 2.1-34's tilt up to the flare star). Bench: `scenes/_mf_orbit.txt` (14 s round her).
⛔ The sail's two faces (silver toward the light, gold behind) 4 cm apart FOUGHT in the depth buffer past ~300 m -- gold
speckled through the silver; 25 cm each side holds at any distance she is shot from.
Scope: the committed engine (ecedda6) and this build recorded the same 6 s of a scene that uses none of it (`porch` on
parsons): frames 3-143 bit-identical; frames 0-2 differ by at most 0.013% of pixels -- and this build differs from
ITSELF at exactly those three frames on a second take (start-up noise, not the change). crowd_new.exe only.

### 42 · Wan-To's star: a star you can cut open, and a set that is only space -- DONE (10-22)
jacob: "chapter 1 might be the most important in the entire book, to set the scene". Chapter 1 (scenes 1.1-1.3) is the
narrator alone over the inside of a star, and Wan-To has no body, so the whole cost was the star. Set lines (u):
`starbody x y z radius [tempK] [cutdeg] [cutaz] [seed]`, `starspot lat lon size_deg` (16), `starloop lat1 lon1 lat2 lon2
height [thick]` (24), `starweb nodes [seed] [xray]` (`starDraw`, drawn in the lab frame after the opaque). Every fact the
book gives is built: a medium-sized G-3 star just under a million miles across, 6,000-7,000 K and "just a touch variable"
(a slow breath in its light), its core at fifteen million degrees, RADIATION for the first four-fifths of a photon's way out
(the radiative zone's specks jump cell to cell -- "a ball in a pinball machine") and CONVECTION in the outer fifth (rolling
cells, hot gas rising and cooled gas falling back), its poles turning slower than its equator, magnetic field lines that
twist (three strands wound round every loop) and loop above the surface over sunspots, a sunspot only "a couple of thousand
Kelvin" cooler, the corona "thrillingly diffuse" and the spicules at the limb (p2-p7, p47-p49). Wan-To is "the network of
neutrinos that served him for neurons" (p6): pieces of him through the star, cyan, a pulse running down every thread, drawn
THROUGH the star (xray) because neutrinos pass through everything. OUR fill: the core's size (a quarter of the radius), the
warm grade on the true temperatures, the granules' scale, the loops' pink, his cyan. The surface: two scales of cellular
noise whose feature points wander (the cells boil), black-body colour from the temperature, limb darkening; the cutaway
(cutdeg) discards a wedge pole to pole and draws its two faces as the layers, with ink between them (the diagram's lines).
Sets `wanto_star` (closed), `wanto_him` (+ his web), `wanto_cut` (open, + web); the inserts `wanto_martini` (1.1-08: "a very
dry Martini indeed") and `wanto_tv` (1.2-04: "a wall of sets, each tuned to a different channel") by
`skate_src/wanto/build_inserts.py`; the rough cut `scenes/wateot_1.txt` (all 21 slots, 629 s).
- ⛔ A SET THAT IS ONLY SPACE WAS NO SET: drawSet (and its sky) ran only for a set with props, so the star hung in the lab's
  grey with the lab's HUD and stage lights over it. `g_setSpace` now counts as a world everywhere the lab asks "is a set up".
- ⛔ sh() DIES on a compile error: the star's passes compile through their own path that logs and draws nothing instead.
- ⛔ 0.006 rad/s carried a sunspot a third of the way round in the minutes of one take; a star turns in weeks (0.0006).
- SHEER SET PROPS: a marker node `AlphaNN` draws a prop NN% opaque, blended, not depth-writing (the martini's glass and its
  gin) -- props were opaque only; the blend existed for a body's meshes. List a sheer prop AFTER what it should show.

### 43 · A body carried round a drum, and a scene's room that changes -- DONE (10-22)
Scene 2.2 is the treadmill drum: "the huge rotating barrel that was the ship's treadmill, spun at nine revolutions a minute"
(p11), a mile "a little more than twenty-five times around" it (p17) -- a running floor 64 m round, radius 10.18 m (and at
nine turns a minute, 0.92 g: "enough of normal Earth gravity"); "Wanda was hanging at the hub of the wheel, talking to him as
he ran". `skate_src/ship/build_drum.py` builds it (unlit: in a drum every surface is a floor facing the hub, so a room's
light from world-up would shadow its whole top half). What the engine had to learn:
- THE ROLL: `hroll= proll= p2roll= p3roll=ax,ay,az,dx,dy,dz,degps[,deg0]` turns a body's whole placement -- after its mark,
  walk, lean and half-pipe -- about an axis at a rate (sticky; `off` hands it back; a new spec starts it on the cut, the same
  one carried on keeps it running). Viktor runs in place on the drum floor's mark and the roll carries him round its axis,
  head always to the hub; the lens rides in the DRUM'S frame (the drum holds still) and sees him go up the wall and over the
  top. His spot for the tracking lenses (locoWhere) is rolled with him, so a `campan` follows him round.
- `home=<set>` (sticky): the scene's home set changes from that line on, its light the old home's. 2.1 starts drained with
  no boys in any pan and changes home to `_scene21b` at 2.1-12 -- the boys and the milk in HIS pan, "the warming pan he had
  awakened in. Two little kids ... were in it now" (p30). Shared marks: `_scene21_marks.set`.
- `houtfit= poutfit= p2outfit= p3outfit=` (sticky): a body dressed on the cut, from its own pack's outfits -- Viktor into his
  tunic and shorts at 2.1-14 (with the sick bay's own glass in his hand, `hprop=sb_glass@0.34@0@hand`), and runs the
  wheel in them (jacob: "running in the wheel he needs pants") -- an outfit the pack already had, one key.
Scope (sections 41-43 together): the committed engine (ecedda6) and this build recorded the same 6 s of `porch` on parsons:
frames 3-143 bit-identical, frames 0-2 the known start-up noise. crowd_new.exe only.

### 44 · MIRRORS -- DONE (10-22)
"Half the walls on the ship were mirrored to make the rooms seem more spacious" (p18-19), and scene 2.9 turns on one:
Marie-Claude "catching a glimpse of herself in the wall mirror". The engine had no mirror of any kind.
- A set line `mirror x y z yaw w h` (u; yaw the way the glass FACES, into the room; 4 at most). The room is built with an
  OPENING there (skate_src/ship/build_rooms.py: a frame, a sheer glass `Alpha18`, and behind it a silvered backing marked
  `MirrorOnly`), and every body and prop in front of the glass is drawn a SECOND time through the glass's plane, right
  after its own draw, from the pose already solved (`mirInst`: the model matrix read back from the program, reflected,
  the winding flipped so the ink hull stays outside). The old game-engine mirror: the reflected room stands behind the
  wall and the depth test masks it -- the real wall round the opening hides it everywhere else.
- WHY NOT A SECOND CAMERA: the frame is one loop that SIMULATES AS IT DRAWS (cloth, chains, the doll step inside the body
  draws), so rendering the world again from a reflected eye would have stepped every sim twice. The kit's own note says the
  same: render interpolation waits on the sim/draw split.
- ⛔ The first mirror came back BLACK. A mirror's own backing and glass, reflected through their OWN mirror, land just
  inside the room facing away: the panels cull, but an inverted INK hull draws back faces, so the backing's ink blacked the
  opening out. Found by putting the lens BEHIND the wall, inside the reflected room -- which was there, whole. A reflection
  never inks a `MirrorOnly` backing or a sheer prop (mirInst flags: 1 cull back faces, 2 no ink).
- One bounce: a mirror seen in a mirror shows its silvered backing (drawn only in reflections), never an opening onto
  nothing. A set's room must be CLOSED: whatever stands behind a mirror wall shows through it.
- Hooked at four draws: drawProp (props, culling back faces so the mirror wall's own reflection, coplanar with it and facing
  away, drops out), actorDraw, labDrawPartner and the hero; not the club crowd, the floor or the lab's multi-cam wall.
  `REVA_NOMIRROR=1` is the control; `REVA_MIRDBG=1` prints the first 40 instances (program, model, distance in front).
- ⛔ AND A ROOM BUILT BY CODE IS NOT FURNITURE. The contact pass (limbs pushed out of bodies, furniture and the floor)
  decides "solid" by a prop's NAME, and `rm_cabin_shell` -- floor, walls and ceiling as one prop -- is no name it knows to
  leave open: 4054 of the cabin's cells were "furniture", and the scene's main body was thrown up toward the ceiling,
  legs first, on EVERY clip (a bank's x_convo1, his own z_rm_wake). Mirrors off changed nothing; REVA_NOCONTACT=1 stood
  him up. A pack marked NoCast AND Open (a code-built room's shell, glow, glass) is never furniture now: 1297 cells,
  the bunks and table and stools only. Set actors never ran the contact pass, so a bench of the same clips looked fine.

### 45 · THE KIT CAME HOME (10-22)
The aigameengine kit (github nemonicicon/aigameengine, local club-engine) had twelve commits on two branches that reva never
saw (09-30 .. 10-05, made in Claude sessions on the kit): the TICK (the sim moves by whole ticks, REVA_TICK), the RECEIPT
(REVA_SIMHASH), the DICE (rand() is the club's own LCG; REVA_RAND=libc for the library's), the LADDER (REVA_TIER), the
TAPE (REVA_RECORD / REVA_REPLAY), the HUD on a 1280-wide canvas, the SPLIT (club_math.c, club_audio.c, club_save.c behind
club.h), the Linux and Pi 4 builds, .dae/.brres import and T-posing any body (skate_src/tpose.py, design/tpose_import.md),
and ep 16's gestures and walk for code-built bodies (author/moves/z_gest.py, z_walk.py). Merged 3-way, file by file, with
the kit's 09-27 cut as the base: 4 conflicts in 48 files (the engine's one: reva's door 11 reached into the mixer, which
the split had moved behind club.h -- it now asks the mixer through four calls, sfxBankPCM / sfxVoiceGain / sfxSetMaster /
sfxReady). Scope: under REVA_RAND=libc the merged build recorded porch, 2.1, 2.2 and chapter 1 identical to the build
before it from frame 1 on (frame 0 is the known start-up noise). The kit's gestures and walk start from the body's REST;
our people rest in an A-pose, so both take `--hang DEG` (arms hanging, elbows soft) -- Bu's first walk held them out.

### 46 · The particle rack: a star pulled out of shape -- BUILT, verify on pixels (10-22)

Chapter 3 (3.3-13): "If you flooded a target star with the right particles you could pull it right out of shape ... When
stretched on the particle rack, the core bubbled and fountained like a geyser." A `starbody` line may end
`stretch k rise [delay]`: from `delay` s after the set loads the star is drawn out along x to k times its radius over
`rise` s (smoothstep), thinned the other two ways by 1/sqrt so it keeps its volume, and it HEAVES as it goes (two slow
sines on the long axis, one on y) -- the bubbling. The vertex shader scales the unit sphere by `uS` before the radius,
and the surface keeps its own coordinates, so its granules and spots stretch with it. `sets/ch3_zap.set` racks a star
x2.6 over 9 s from +1.5 s between two particle beams.

A note on the same night: **a child's gait.** The boys' walk was refused at boot ("REJECT walk clip 'w_walk' on billy:
not an upright gait") and they glided to every mark. `clipUpright` asks the hips to stand 30% of `effH` above the lowest
foot, and `effH` is a 3.15-unit FLOOR -- an adult crowd body's height. A TrueScale pack now keeps its true height
(`trueH`) and the test is a share of THAT: a five-year-old is 1.61-1.84 units tall with his hips ~0.9 over his toe.

And **a sitter is not standing.** The contact pass slides a STANDING body whose legs are inside furniture along the floor
to the nearest free side (a woman "standing inside of a bench"), and "standing" was both feet near the floor -- which a
SEATED body has too, with its thighs on the seat. Chapter 2's logs said `standing body out of obstacle by 0.17-0.67 u`
in 19 scenes of 27: every sitter was shoved off his chair, and 2.12's singles of Viktor framed the empty end of the
table. Standing now also asks the thigh's top to stand above its knee by half a thigh; a sitter's limbs still come out of
the seat one by one. Proof: Viktor at 2.12's table with the pass on lands where `REVA_NOCONTACT=1` puts him.

And **a walk that cannot get there is placed, loudly.** When the router finds no way round the furniture (`the furniture
closes every way to the mark -- walking straight`) or the start is off its grid (a body outside a room's shut doors), the
walk goes straight -- and colResolve, which collides against the set's real triangles, held the walker against the first
thing in the way for the rest of the scene: Viktor sat on the floor at the corner of the breakfast table for all of 2.12,
Wanda stood 0.26 m outside the freezatorium's shut thermal doors and was never seen calling him (2.24), and walks in 2.3
and 2.14 never arrived. A walker that moves less than a third of its step for 0.6 s of walking is STUCK: it is placed on
its mark, facing the mark's way, and the log says `[walk] slot N STUCK x.xxu short of '<mark>' ... PLACED on the mark`.
The scene generator places the walks it knows cannot be walked (through a shut door, round a crowded cabin) with
`snap=1` on the cut instead.

And **an extra wears what its line says** (10-23). jacob asked for the refectory's seated extras to be fixed because
I had reported them standing; profile shots at eye level in 2.29's room showed all seven sitting on their stools,
thighs level and shins down -- I had misread two high, cramped frames. What those shots DID show: an `actor` line had
no outfit, so every extra wore its pack's first one -- Werner and Marie-Claude barefoot in freezer shifts at the
farewell dinner, Amelia in her robe, Billy in his shift beside Freddy in day clothes. `outfit=<name>` on an actor line
(design/the_set.md) is now what that actor wears: the token is taken out before the positional read (an 11th token is
the board's name), each actor keeps its own worn-pieces mask, and the mask goes on the pack as the actor is drawn.
Proof: 2.29's table before/after with the same lens; `[set] actor N (<pack>) wears '<outfit>'` in the log.

