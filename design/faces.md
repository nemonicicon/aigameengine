# THE FACE — talking heads in the club

*(written 2026-08-28, out of the CLUB NIGHT conversation scene. jacob's verdicts drove
every rung: "mouths are not moving, eyes are not moving, heads are not moving" →
"revas eyebrows are moving when it should be her mouth opening instead" → "i suspect
you dont know where her eyes and mouth are" → "kaelis mouth opening looks like an
eldritch monster lol. it should be black inside right?" → "do we have any models with
working mouths, skeleton and all, so you can learn how it should work?")*

## What exists now

- **A conversation scene system.** `reva_native/scenes/`: a dialogue file is the script,
  `say_piper.py` speaks it with Piper (local neural TTS, a voice per character, per-line
  direction for pace / rhythm / intonation), `compile_scene.py` mixes ONE wav and emits
  a `.scn` timeline with a 30Hz mouth envelope per line. The engine plays it with
  `scene=<name>`: subtitles, camera coverage (over-the-shoulder, cutting on the line,
  two-shot every fifth), the jaw driven by the speaker's own loudness, blinks on
  independent clocks, and a head layer riding over whatever clip is playing.
- **Face landmarks in the rig.** `face_marks.py` appends zero-weight marker nodes
  (`face_mouth`, `face_eye_l`, `face_eye_r`, `face_chin`) parented to the head. They ride
  FK, so cameras and deformers ask where the mouth IS instead of guessing from the head
  bone. Calibrated against the render.
- **The face rig** (`ctl face=10`): eight angles around the head plus a mouth camera and
  an eye camera, aimed at the marks.
- **A shader mouth aperture**, ported from holly.
- **Tools:** `face_morphs.py` (jaw/smile/brow/blink), `mouth_dark.py`, `subdivide.py`
  (add geometry), `fingers.py` (add bones + bind weights), `face_calibrate.py`.

## THE REFERENCE: what the goblin could and could NOT teach

`v1_crowd_goblin.rpack` carries a full Character Creator face rig. Found by scanning all
133 packs for facial bones - the club also has `rabbit` (jaw_07), the toony robot
(eyelid controls) and six packs with teeth and eye bones.

    CC_Base_FacialBone
    |-- CC_Base_JawRoot        a BONE at the hinge (head +1.58 forward, -0.13 down)
    |   |-- CC_Base_Tongue01 -> 02 -> 03
    |   `-- CC_Base_Teeth02    LOWER teeth, ride the jaw
    |-- CC_Base_UpperJaw
    |   `-- CC_Base_Teeth01    UPPER teeth, stay put
    `-- CC_Base_L_Eye / R_Eye  eyes are BONES

**Two corrections to the first reading of this rig, both found by measuring it (08-28):**

1. **Its face bones carry ZERO skin weights, and its teeth and tongue meshes did not
   survive the bake.** "There is geometry inside the mouth" is true of the CC *source*
   and false of the *pack on disk*. Nothing could be copied out of it - every vertex of
   the interior had to be authored.
2. **Only its BONE-space ratios are usable.** Its mesh buffer and its bone worlds sit in
   different frames - head vertices read z~82 while the head bone reads z~2.9 - so any
   ratio taken off its geometry is nonsense.

What it *does* give, and what the tools now use:

    hinge        0.495 of the eye-to-lip span ABOVE the lip line, ~0.5 IOD behind it
    teeth        +/-0.186 span from the lip line (upper above, lower below)
    tongue       root 0.192 span below, tip 0.696 span below and further forward

**THE WEIGHT BAND COMES FROM THE RABBIT**, because the goblin has none.
`v1_crowd_rabbit.rpack` is the club's only body whose jaw bone is actually skinned
(`jaw_07`, 805 vertices). Measured: full weight out at the far chin, falling off toward
the hinge and above it, tapering laterally to about a quarter at the head's own
half-width, and **46% gradient**. A jaw is a soft band with a solid core - a first pass
without a plateau came out 92% gradient, which reads as mush.

The three lessons still stand, with the geometry one now owed to us rather than donated:

1. **The jaw is a BONE with the lower face skinned to it** - not a morph. It works at any
   mesh density and cannot be mis-banded.
2. **There must be GEOMETRY INSIDE the mouth.** Teeth and a tongue are why an open mouth
   reads. Neither club woman had anything in there, which is why every opening looked
   like a hole, a void, or flesh unfolding - and once the jaw really opened, like a hole
   straight through to the hair behind her skull.
3. **Eyes are bones**, so they can aim. But neither club woman *has* eyes to aim: reva has
   26 vertices within a third of an IOD of her eye mark (hers are painted) and kaeli's eye
   region is flat lid/lash shells, not spheres. On these two the head is what can aim, and
   that is what the look-at layer moves.

## THE RUNG THAT LANDED (08-28)

All six steps are in the club, each verified on the face rig and by looking:

1. **Measured mouth corners.** `face_marks.py` bakes `face_mouth_l` / `face_mouth_r`, and
   the engine sizes the aperture as the distance between them instead of `0.42 x IOD` (a
   guess resting on the eye marks, which are themselves a proportion). Both shader axes
   are now in mouth half-widths, so the opening is a mouth-shaped ellipse on any head
   instead of a face-sized circle.
2. **A jaw bone.** `jaw.py` grows `face_jaw` at the measured hinge with fingers.py's
   append-and-repoint, and skins the lower face to it.
3. **The band as WEIGHTS**, not deltas: zero at the lip line, full at the chin, released
   before the throat, tapering laterally - the rabbit's shape, with a plateau so the chin
   actually commits.
4. **An interior.** `mouth_interior.py` authors a dark cavity, an upper dental arch bound
   to the head, and a lower arch plus a tongue bound to `face_jaw` - the split the CC rig
   uses - all sized in mouth half-widths.
5. **Driven by the scene envelope**, the speaker's own loudness, not a morph weight. The
   aperture follows the same opening, so hole and face cannot drift apart.
6. **A look-at layer.** The listener holds the speaker's eye line; measured convergence
   28.0 deg -> 0.4 deg at a conversational angle, stopping at the neck clamp when the
   target is 51 deg off.

**NOTE on "keep the shader aperture only for bodies with no interior":** these two need
BOTH. Their lips are sealed geometry, so the aperture is what opens the skin; the
interior is what you then see through it. They are complementary, not alternatives.

### The instrument that made it all checkable

The face rig now **draws the marks** into every panel (`REVA_FACEDBG=1`, or automatically
under `ctl face=10`): cyan lip line, green corners with a corner-to-corner line, yellow
eyes, magenta chin, red jaw hinge. Every calibration before this had to *infer* a mark's
position from how the camera framed her, which cannot separate "the mark is wrong" from
"the feature is not where I thought" - and on reva both were true at once. It must be
drawn INSIDE the wall's panel loop; drawn outside it lands wherever the main lens points.

Calibration is now a loop: `face_marks.py <pack> --mouthdy D --eyedy D --mouthhw F`,
shoot, look, repeat. Marks that already exist are MOVED in place. The values that landed:

    reva2   --mouthdy -0.0571 --eyedy -0.0755 --mouthhw 0.254     (the OLD body -- superseded 2026-09-10)
    kaeli   --mouthdy  0.252  --eyedy -1.67   --mouthhw 0.205

⭐ **The rebuilt reva (2026-09-10)** — her body was re-ingested from the Meshy source
(`skate_src/reva2/rig_reva.py`), so every mark was re-derived, from a FILE rather than offsets:
`face_calibrate.py` reads her eyes off her paint (a symmetric dark pair) and the mouth's HEIGHT off
the reddest texels, now FENCED between the eyes (unfenced, it read a pink-violet lock of hair 5 cm
off her midline); the mouth's centre and width were read off the drawn marks on a face-rig render.

    face_marks.py packs/v1_r2moves.rpack --from packs/v1_r2moves.marks.txt --mouthhw 0.147
    jaw.py packs/v1_r2moves.rpack
    mouth_interior.py packs/v1_r2moves.rpack          # palette cells go in FREE atlas texels now
    # no mouth_dark.py: the interior covers the seam on this mesh

Two tool fixes came with it: `--from` marks now FEED the derivation (they used to override the
mouth after the corners were computed from the unmeasured guess — corners at her nose), and a
file depth that sits behind the face is re-projected, loudly.

`ctl jaw=0..1` parks the mouth at a chosen opening (`jaw=-1` hands it back to the scene);
`ctl look=0/1` toggles the gaze. `REVA_FACEDBG=1` also prints the gaze error every second.

## Still owed

- jacob's eyes on every one of the six steps - the verdicts are all mine so far.
- The teeth are authored primitives and read a little bright and a little large; that is
  a presentation call, not a mechanism one.
- Real eye bones for the six packs that carry them (rabbit, medic, miko, foxy, hina, the
  toony robot); the engine's look-at would drive them, but the two women cannot use it.

## THE EYES GATE, 08-28 — jacob's first real verdicts on the face

The face rig went in front of him one item at a time. What came back, in order, and what
each cost:

**"reva is looking great, except she needs lips not gums"** · **"teeth could use a little
more detail"** · **"kaeli ... her model is a little off kilter ... i would lower her mouth
a little bit and also tilt it kaelis mouth should be wider as well"** · **"too dark around
the teeth, shouldnt be dark around the teeth, rather it should be dark on the interior of
the mouth"** · **"the meshs ALREADY have lips. you dont need to make them"** · **"the black
around the lips/teeth is undesirable"** · **"this is what messed up looks like"** ·
**"regression you messed up revas mouth a few turns ago"**

### What landed

- **Individual teeth.** A continuous strip reads as a bright bar at conversation
  distance; each tooth is its own quad with a gap, front four slightly wider.
- **A mouth that can TILT.** The aperture assumed the lip line ran along mesh +X. It now
  takes the angle the CORNER MARKS make, shipped in the spare w of uMouthP and applied
  as a 2D rotation before the ellipse test, so `face_marks.py --mouthtilt` rotates the
  opening, the cavity, the teeth and the tongue together.
- ⭐ **kaeli is not a calibration reference.** jacob: "she is a bit of a janky model, not
  good for calibrating every model." She is the one body carrying an explicit tilt, and
  her numbers are not to be read as general. Landed: `--mouthdy -0.20 --mouthhw 0.25
  --mouthtilt 5`.

### ⛔ The mistake worth remembering: I built lips instead of asking why hers vanished

Her modelled, textured lips disappeared whenever the mouth opened. I authored a lip band
back over the hole — and gums under it — which is exactly the wrong way round. **The
aperture was eating them**: it was sized to the mouth CORNERS and half a mouth-width
tall, so the discard took her lips along with the sealed seam.

The right fix was to make the hole smaller. It only has to remove the thin line where the
lips MEET; her own lips then part by themselves, because the lower face is skinned to
`face_jaw`. Inset to 0.78 of the corner span, and a fifth of the height rather than half.

*When something disappears, find out what removed it before replacing it.*

The authored lips and gums are gone. So is the shadow ring — it multiplied the skin down
to a TENTH of its brightness in a wide band outside the hole to hide the stepped discard
edge, and that was the "black around the lips/teeth".

### ⛔ Two sources for one motion, twice

- **The bone and the morph.** The scene drives the speaker's mouth from a 30Hz loudness
  envelope; it was setting the "jaw" MORPH from that amplitude while `jawLayer` swung the
  jaw BONE from the same amplitude. Two deformations of the same vertices pulling
  different ways — her face came apart into a flat grey shard mid-sentence. On any body
  with a `face_jaw` bone the scene no longer drives `jaw` or `smile`.
  **The lesson had already been written down for the APERTURE last session and I fixed
  only that one driver.** When a stand-in is replaced by the real mechanism, every driver
  of the stand-in has to be found.
- ⛔ **And then the lip stopped moving.** Disabling the morph removed the only thing
  carrying her LOWER LIP: the bone's band is deliberately zero at the lip line so a
  sealed seam cannot tear. Her lips sat still while the chin swung away beneath them and
  the mouth appeared to open under her face. The band now starts at LIP_PART (0.45) at
  the lip line, tapering to zero at the corners — face_morphs.py's own solved shape,
  moved from deltas to weights where it belonged.

### ⛔ An instrument that lies is worse than no instrument

Hunting that regression, a botched edit left the scene telemetry's printf with its format
string intact and `g_scnT,li9` REMOVED from the arguments. It printed `t=0.0 line=0`
forever. I read it as a frozen scene clock and spent a long stretch chasing a bug that
did not exist — including a face-camera change that made things worse and was reverted.
With the printf repaired: t=2.0, 4.0, 6.1, lines advancing, jawopen 0.28-0.43 on the
speaker. **This bench has that rule written down twice already** (README: two of the first
three "defects" were bugs in the measuring tools). It applies to a printf as much as to a
metric.

A second self-inflicted one in the same stretch: `driver.py` writes its own ctl file, so
running a static comparison mid-investigation **overwrote the scene setup** and several
"verification" captures had no scene and no partner in them at all.

### Still owed on the face

- kaeli's EYE marks are visibly wrong — the crosses sit on her brow and temple. They were
  placed by proportion and never calibrated, unlike her mouth.
- The stop-motion look jacob liked is banked separately in `stop_motion.md`.

## Traps this cost (all measured, none guessed)

- `authorlib.face_points`' nose-bump heuristic picks the **fringe** on a body with bangs
  (reva's bangs out-protrude her nose by 0.005). It now takes the lowest strong peak.
- Its `chin` is the **throat** — the front-slice p02 lands below the neck joint.
- **Colour cannot separate a purple character from purple hair**: "the reddest texel" is a
  hair highlight, and the darkest are her bangs.
- The brow band ran into the **hairline**, so "raising an eyebrow" lifted her scalp.
- A jaw banded from the mouth **downward by face-heights** lands on the throat, hidden
  under a collar: the numbers said 5cm of travel while the lips never moved.
- Sealed lips **tear** if the lip surface itself is rotated — the inner paint stretches out
  of the face.
- The character shader sees the **raw mesh buffer** (bind is identity), which is not rig
  space on a UE export.
- ⛔ The pack morph blend was hero-only and capped at 80k verts; kaeli's mesh is 294k.
- ⛔ THE BIG ONE, unrelated but found here: the hero's model matrix was uploaded once
  above the two-target pass loop, so after `labDrawPartner` bound hers, the hero drew with
  the PARTNER's matrix and vanished from the main view. Every pair move since the high
  five was judged on the multi-cam wall, which rebinds per panel and hid it.


### Traps the 08-28 rung added

- **The depth trap.** `--mouthdy` moved the mark DOWN in Y while keeping the Z measured at
  the ORIGINAL height. A face recedes below the lips, so reva's mark ended up floating
  0.035 rig units clear of her face (~7cm at world scale); the aperture's depth gate then
  admitted only her NOSE and her "mouth opening" rendered as a vertical slit beside it.
  Every mark is now re-projected onto the surface AT ITS FINAL HEIGHT.
- **A band percentile is not a surface.** On reva's low-poly shell two sampling windows 2%
  apart in width disagreed by 0.02 rig units. The surface is found by RAY CAST against the
  face triangles now - nine rays and a median, because her face has real coverage gaps (a
  lateral scan at her lip line hits, misses twice, then hits again).
- **The lower face is NOT head-weighted.** On reva the chin, jaw and lower lip are dominant
  to the NECK; a head-family-only triangle filter threw away exactly the geometry the lip
  ray needed and every cast missed.
- **The skull bone is not the face's midline.** reva's sits 0.032 rig to one side of her
  face's own frontmost ridge - the mouth camera had been framing her cheek.
- **"Walk down until the mesh ends" only finds a chin on a head that is a separate shell.**
  kaeli's face runs into her neck without a break and the walk slid 9 rig units down her
  throat. The chin is where the face stops reaching FORWARD.
- **Weight quantisation overflow.** "Round each weight, give the last slot the remainder"
  does not sum to 255: when rounding-up pushes the earlier slots to 256 between them the
  remainder clamps to zero and the vertex carries 256/255 of itself - not the identity at
  rest. It moved 7 of kaeli's 293,760 vertices and the rest-pose gate refused the write.
  Largest-remainder instead - floor everything, hand the leftover units to the biggest
  fractions - and it now lives ONCE, as `authorlib.quantise4`, which jaw.py and fingers.py
  both call and which `authorlib.py --selftest` sweeps (the sweep catches the old pattern
  73 times; on uniform random 4-slot weights it is about 1 vertex in 9,000).
  It needs a FOURTH influence to bite: with three or fewer the remainder pattern is exact,
  because two round-ups can gain at most 1.0 and the raw parts already total 255. That is
  why jaw.py (which keeps a vertex's existing influences AND adds the jaw) hit it and
  fingers.py (hand + two finger joints, never four) had it only latent.
- **The aperture must never be armed for the interior mesh** - it discards by rest position
  and the interior sits exactly where the hole is. Depth cannot separate them.
- **Which way V runs is not knowable from the pack.** The same UV gave reva a dark cavity
  and kaeli a TAN one; neither was landing on the palette patch at all and reva's texture
  corner merely happened to be dark. Paint both ends of the sheet.
- **A cavity rim at constant depth bursts out through the CHEEKS**, because a face recedes
  from the midline. The rim ray-casts the real surface at every ring point.
- **The pack scan matched ".rpack" as a SUBSTRING**, so every `.rpack.marks.bak` and
  `.tex.bak` the authoring tools leave behind was loaded as a crowd member - the club
  carried stale twins of every edited body, kaeli's 294k-vertex mesh twice over.
- **Two runs at the same frame number are not the same instant** when a scene is playing:
  the scene clock runs on wall time. A look=0/look=1 comparison shot that way showed a
  head-angle difference that was pure drift. Verify a gaze by the ANGLE between the face's
  forward and the target, not by comparing two pictures.
- **`partner=<name>` alone loads her and draws nothing.** She needs a `pclip=` before she
  is drawn or her face marks published.

---

## THE THIRD FACE — SUSAN, and what a body that WASN'T reva2 taught the tools

*(2026-08-29, Z5 of SEVENTY-FOUR. The film needs a third speaking neighbour, and
`v1_crowd_susan` is the first body the face pipeline has met that it was not written for.
Four of the six things below were the tools being too narrow, not the body being wrong.)*

**The build, for the record:**

    face_marks.py <pack> --eyedy 0.0078 --mouthdy -0.0118 --mouthdx -0.0030 --chindy 0.006
    jaw.py <pack>
    # and NOTHING else -- see below

⭐ **`face_calibrate.py` IS THE CALIBRATION, not the eye.** The doc's loop was "shoot, look,
adjust `--mouthdy`, repeat". Susan's offsets were instead READ OFF HER PAINT — eyes are the
darkest texels, lips the reddest — and the numbers it gave (eyes y 1.5520, mouth y 1.4923)
matched the render's error exactly: the marks sat 0.0078 BELOW her eyes and 0.0066 ABOVE
her lips, which is precisely what the mark gizmo showed. Two independent sources agreeing
is worth more than either. One pass landed every mark within 0.0023.

⛔ **`jaw.py` REFUSED HER AT THE DOOR, and its diagnosis was wrong.** `IBM formula check vs
stored: worst 4.33e+01` — "this pack's inverse binds do not follow the formula". Measured:

- ALL 52 of her bones break `IBM = inv(boneWorld) . meshNodeWorld`; head and hips by 0.67,
  the fingers by 40.
- Her rest pose nonetheless RECONSTRUCTS through her own inverse binds, and across the
  whole face the drift is a near-CONSTANT 6.689e-3 (worst == median) — a uniform offset,
  not a deformation. Which is why she renders perfectly in the street.
- Recovering the frame each bone was really bound against (`M = boneWorld . IBM`) spreads
  0.46 over the whole skeleton but **1.3e-6 across the head chain**, on a frame translated
  (0.0034, 0.0023, −0.0067) from identity. Her head was bound ~7mm off; her HANDS were
  bound somewhere else entirely.

**`W[meshNode]` is one way a baker might produce inverse binds, not the only way.** The jaw
takes its weight from the HEAD, so the head's frame is the only one it must agree with:
jaw.py now RECOVERS that frame and requires the head's own chain to concur. The absolute
formula check is gone; the **differential** rest-pose gate — re-skin after the write, refuse
if any vertex moved — is what actually protects the body, and it reported 5.4e-08.

⛔ **SOME BODIES ARRIVE WITH A MOUTH.** Her teeth showed through her closed lips and I spent
three rounds on the aperture and the dental arch's depth before taking the control that
settles it: **marks only, no jaw bone, no interior, no aperture — the teeth were still
there.** They are her own geometry. Her texture atlas carries teeth, an eye sphere and a
tongue; she is a Character-Creator-style head like the goblin, and her lips are MODELLED
SLIGHTLY PARTED. Authoring `mouth_interior.py` onto her put two rows of teeth in one skull.
**She gets marks + jaw and no interior**, and `SEETHRU` measures 0.0000% without one — her
own geometry closes the back of her mouth. Her lower teeth ride `face_jaw` for free, because
the band is geometric and they are below the lip line.

⭐ **So `facecheck` learned the difference.** INTERIOR used to test for a mesh called
`mouth_interior`. What an interior has to DELIVER is "you cannot see through her head", and
SEETHRU measures exactly that — so a body with no authored mesh now defers to SEETHRU
instead of failing for a missing name. *An interior is a job, not a mesh name.*

⛔ **The chin ray fell into her throat.** At `--chindy 0` the mark projected to z 0.0298
(the neck); at 0.006 it found the chin at z 0.0879 and SURFACE went from 3.24 half-widths
off to 0.13. A face recedes fast below the lips — the documented depth trap, one rung down.

**Where she stands:** all eight facecheck rows pass. The only failure across the three
faces is reva2's own long-standing `face_chin: no face surface anywhere near it`.

### ⛔ jacob's verdict on Susan: "this is not good work here. reva looks good though."

**Rejected at the eyes gate (2026-08-29), and the instrument had passed her 8/8.** That is
the finding worth keeping: `facecheck` measures the rig AGAINST ITSELF — is the hinge the
right fraction behind the lips, does the chin travel far enough, is a mark on the surface —
and every one of those can be true of a mouth that reads badly. SEETHRU proved you cannot
see through her skull; it cannot tell you the hole looks like a hole.

What is actually wrong, from his frames: at `jaw=1.0` her mouth is a dark void with an arc
of upper teeth floating over it and **nothing below** — no lower teeth, no tongue, no
inside of a lower lip. Her own geometry supplied the upper arch; whatever it has for a
lower one does not ride the jaw the way the upper one rides the head, so opening her leaves
a hollow. And at rest her modelled parted lips put a full row of teeth on display, which on
a suburban neighbour reads as gormless rather than natural.

The lesson is not "add an interior" — that was tried and produced two rows of teeth in one
skull. It is that **a body that arrives with its own mouth needs its own recipe**, and
Susan's has not been written. Parked here rather than iterated: two wrong declarations in a
row is the bench's limit and this was the second.

reva2's face is the one that reads, and is the reference to measure Susan against when this
is picked back up.
