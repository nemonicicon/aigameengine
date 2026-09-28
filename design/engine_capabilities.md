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
