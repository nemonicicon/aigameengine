# CLOTH — garments, and the instruments that stopped me guessing about them

> jacob: *"i would like for our cloth physics to be very well done.. blowing curtains, torn
> clothing exposing wounds underneath dainty dresses showing every curve... and kailis loose
> pajamas she wears because they are very comfortable."*

Two cloth systems share one engine. **Set cloth** (`sclAdd/sclStep/sclDraw`) is curtains,
sheets and bedding — hung in the world, pinned to a rail. **Garment cloth** (`clothInit/
clothStep`) is worn: it is skinned to a body first and simulated second, and everything below
is about that one. Design home for the film that needs them:
[seventy_four.md](seventy_four.md).

---

## The shape of a garment

`skate_src/author/makepajamas.py` builds one from the body it will be worn on — it measures
her, then emits a mesh skinned to her bones, named `<name>~cloth#rrggbb` so the engine knows
both that it simulates and what colour it is.

    python skate_src/author/makepajamas.py reva2 reva_pj 5A6E9E
    #                                      ^src   ^dst    ^colour

### ⭐ A SHIRT IS A FRONT AND A BACK, SEWN — NOT A TUBE WITH HOLES IN IT

> jacob: *"would patterns help you, like sewing stuff"*

It is the whole answer, and the failure that preceded it proved it. Cutting armholes out of a
closed torso tube fragmented the shirt into **fourteen** pieces, and capping the hole until it
stopped severing meant removing **zero** faces. An anatomically honest armhole really is most
of a torso's width, so on a 16-segment tube the two holes meet and cut the shoulder yoke off.
That is not a tuning failure — the tube is the wrong object.

So `shell()` takes an `arc`, and the shirt is built as two panels:

* **front** over `+Z`, **back** over `−Z`, with a gap at each side
* `sew()` joins them down the ribs — **stopping at the armpit**
* `sew_row()` joins them across the top, leaving the middle columns open — **that is the neck**
* what is left open between the two seams **is the armhole**

Nothing is ever cut. That is why a real shirt survives having armholes and the tube did not.

### ⛔ THE COMPASS — made twice in one session

`shell()` places at `cos(a)→X, sin(a)→Z`, so angle 0 is **+X**. **Club forward is +Z.**
Centring the "front" panel on angle 0 put it on her *right side*, the back panel on her left,
and both armhole gaps on her chest and her spine. She rendered bare-chested with blue at the
flanks — while the piece dump insisted the shirt was present, correctly sized and centred at
`x −0.010, z +0.009`. It was. It was rotated 90°.

`fitcheck.py`'s first table made the identical mistake, labelling +X "front". If a direction
has a name in this repo, check it against +Z before believing the label.

---

## Fit: measure it, do not admire it

> jacob: *"from what i can see the fabric is wayyy ofar away from her skin"*

That is a distance, and both meshes are in one pack. **`skate_src/author/fitcheck.py`** reports
it per bone:

    python skate_src/author/fitcheck.py reva_pj

    bone                 body r  cloth r       gap       x   verdict
    Spine                0.1372   0.1641   +0.0269    1.20   ok
    RightShoulder        0.1229   0.1210   -0.0019    0.98   CLIPPING -- fabric inside her

Median standoff went **+0.080 → +0.018** rig units on his note — 4.4× closer to her skin.

**⭐ Per bone, not per height band**, for reasons the band version got wrong on its first run:

* ⛔ a T-pose puts the arms at chest height, so a chest band measured a "body radius" of
  **0.934** — her *arm span* — and duly reported the shirt as 0.11 *inside* her body;
* ⛔ the band compass was wrong (see above);
* ⭐ a bone capsule is exactly what the engine collides against, so the number **predicts what
  the solver will do** rather than merely describing the mesh.

**⛔ And the garment goes to its NEAREST bone, not the one it is skinned to.** The garment binds
to 8 bones with no forearm, so every sleeve vertex over the wrist belonged to `LeftArm` and
measured its distance to the **elbow** — a whole forearm of counterfeit looseness, reported as
`x2.56`. Nearest-bone removes the bias and needs nothing from the binding.

### ⛔ The pad was the culprit, not the slack

Six rounds were spent raising `slack` to escape a catsuit. `fitcheck` convicted the **pad** in
one run: at `tspan*0.055` the pad *alone* was 0.054 rig units when the whole target clearance
is 0.035, so the garment could not be anything but a tent whatever the slack said. Small pad,
modest slack.

---

## The solver

XPBD, 3 substeps × 1 iteration (Macklin et al., SCA 2019 — `design/papers/`). Compliance is a
physical constant: `4.0e-7` is canvas, `5.0e-6` is cheesecloth that billows, **`1.6e-6` is worn
cotton**.

### ⛔ THE FOLLOW MUST NOT BECOME MOMENTUM

> jacob: *"its z fighting or vibrating from some other reason sooo bad"*

He was right, and it was the vibrating one. The skinned-pose pull was added to the new position
and then `prv=cur` stored — but **in Verlet the velocity IS `cur−prv`**, so the 55% pull toward
her skin was remembered as *speed*. Every step the particle was thrown most of the way to the
skin, sailed past, and was shoved back by the constraints. It never settled.

Measured on a T-pose with a fixed lens, where nothing should move at all:

| | changed pixels per frame |
|---|---|
| solver **off** | **0.00 %** |
| before the fix | **1.79 %** (and 5.07 % with ease on) |
| after the fix | **0.00 %** |

**⭐ A positional attachment moves `cur` AND `prv` together** — same steady pull, zero velocity
injected. ⛔ And the 0.00 % control is also what proves it was *not* z-fighting: static geometry
z-fights identically every frame, it does not shimmer.

⛔ **A fix that stops the vibration by stopping the simulation is not a fix.** On a moving body
the solver still differs from pure skinning on **5.4 %** of pixels — that difference is the
drape. Check it, every time damping or follow changes.

### ⛔ EDGE DEDUP — a silent cap that read like a report

The edge list stored every shared edge **twice** (one per triangle), so 2404 triangles asked for
7212 edges against `CLOTH_MAXE 6144`. It filled up and **stopped**, dropping the last ~1068 —
the sleeves and seams, which are last in the index buffer. The log said:

    [cloth] pyjamas~cloth: 1284 particles, 6141 edges, 182 pieces, 106 pinned

The mesh has **5** components. The counter said 182, and a fragmented graph gets its *top 8 %
pinned per fragment* — 106 particles nailed to the skinned pose, and a cloth that cannot move
keeps the shape it was built in. **That was the "balloon".** The duplicates also solved every
edge twice per iteration, so the fabric was stiffer than anything ever asked it to be.

Fixed with an open-addressed hash. **The cap now shouts** — a silent truncation that prints a
plausible number is the "instrument that lies" failure, and it hid this for a whole session.

### ⛔ EASE — a good lever that does not pay yet

Fabric folds because it has *surplus*: more circumference than the body inside it. So rest
lengths were scaled by `(1+ease)` while particles start where they were built — close to her
skin (his second note) while behaving as though there were more fabric (his first). ⛔ **Ease is
a rest-length property, not a position one**; building the mesh wider is the same tent further
out, and he objected to both in one breath.

**The sweep said no.** At `0.10` holes open in the garment; at `0.22` it tears itself apart.
Surplus only becomes *folds* if something resists buckling, and this solver has neither bending
constraints nor self-collision — so slack quads simply invert and pass through each other and
through her. **Default 0.** The knob stays (`REVA_CLOTHEASE`, ctl `clothease=`) because it is
the right lever; it pays once bend + self-collision land.

⭐ This is also why the garment still reads a little thick: the silhouette is a smooth swept
tube, and smooth swept tubes read as foam. **Folds are the open work, not clearance.**

---

## Collision

⛔ **A BONE IS A LINE, AND ITS ORIGIN IS NOT ITS AXIS.** The capsule radii were taken as each
vertex's distance from the bone's *origin*, which returns the limb's **length**:

    LeftForeArm r 24.1   ·   LeftUpLeg r 30.0   ·   Hips r 19.4

on a body 140 units tall whose leg measures **6.7** across. Three to five times too big, all of
them — the garment was being shoved out by balloons, and that is most of why every loose version
came back a blob. Radius is measured to the **segment**: this bone to its first child.

⛔ **This mistake has now been made four times in this repo** — the generator's per-limb centroid,
the armhole spread, these capsules, and `fitcheck`'s first draft. It is written at the top of
every function that could make it a fifth.

---

## The bench

    python .claude/skills/run-reva/driver.py bench OUT.png --subject reva_pj --clip tpose

> jacob: *"why are you doing one angle at a time. you should be able to do like 8 at once"*

⭐ **`--orbit 8`** (the default) walks eight evenly spaced yaws at one height, radius and fov, so
a difference between two panels is a difference in the *subject* and not in how it was shot.
⛔ Before that, `ANGLES` was a hard-coded four, and when four sides were not enough I started
typing `cam=` by hand — which is exactly the loop the bench exists to replace.
⛔ And the tile was pinned at 210px, so `--cols 2` gave *fewer* pictures at the *same* size
instead of bigger ones. It now sizes from the column count.

⭐ **`--clip tpose` is the default and must stay that way.** Judging a garment on a body
mid-crouch is unreadable; one look at a T-pose showed the thing was a **catsuit**, which six
animated benches had not. Same mistake as judging eye marks on a tilted head.

> jacob: *"you need a stable test bench, that is reva."*

⭐ **`--env REVA_NOCLOTH=1` is the control run**, and it separates "the sim is wrong" from "the
mesh is wrong" in one shot.

⛔ **AND THE FULL FRAME BEATS THE CONTACT SHEET.** From the 8-up ring I read "sleeves missing,
one continuous onesie". Enlarged, the sleeves were *correct, full-length, with cuffs* and the
**shirt** was the missing thing. Same law `zombiecheck.py` already carries.

---

## Static checks

* **`skate_src/author/packcheck.py`** — rpack invariants; `makepajamas.py` runs it on what it
  writes. ⛔ Its first real catch: **the bone map is NODE indices, not slot indices** — with
  `range(len(bones))` the rest palette was off by 111 units, which is what caused four failed
  static fits and an entire solver detour.
* **`reva_native/anim_bench/pathcheck.py`** — the same body down all three draw paths (hero,
  scene partner, set actor), which have each drifted independently.

---

## ⛔ THE T-POSE BENCH CANNOT SEE A BENT JOINT

The garment resolved **eight** bones -- `Hips, Spine, LeftUpLeg, LeftLeg, RightUpLeg,
RightLeg, LeftArm, RightArm`. **No forearm. No shoulder.** And `WANT`'s chest entry
(`spine_03`/`spine02`/`chest`) matched nothing on this rig, which calls them `Spine1`/`Spine2`
-- so the whole chest rode on `Spine`.

A sleeve runs shoulder to wrist, so every vertex of it was bound to the **upper arm**. The
sleeve stays rigid along the upper-arm axis; the instant an elbow bends, its far half points
off into space and gravity slides it down. In jacob's live window that read as **a sleeve
detached and floating**, with the arm under it bare.

⛔ **And a T-pose cannot show it.** With the arm straight, an upper-arm-only sleeve sits
perfectly. The stable fixture that made the fit calibration possible is structurally blind to
every bug that only appears when a joint bends -- and the garment was shot at four angles,
then eight, and never once at a bent elbow.

⭐ **So the bench must be run on a MOVING clip as well as `tpose`.** `--clip tpose` for fit
and shape; a real clip for anything that depends on the rig.

⛔ **Four wrong diagnoses came first**, each killed by a picture, none by reasoning:

| I blamed | What killed it |
|---|---|
| the armhole | grafting it shut changed nothing |
| reva2's own mini-skirt | matched its *position* in a bare shot, never its *colour* |
| a draw-path difference | the actor path was fine |
| two overlapping bodies | a set with no actor rendered an empty road |

What found it was shooting **the same body on a different clip**. When something is wrong in
one view and right in another, change one variable at a time and shoot each.

## Open

1. **Bending constraints + self-collision** — the gate on folds, and therefore on the garment
   reading as fabric rather than foam. Everything else about ease is already built.
2. **A visible hem.** Shirt and trousers are one continuous blue: the hem needs to hang proud of
   the waistband before it reads as two garments.
3. **reva2's asymmetry leaks into her clothes** — 2995 arm vertices classified on the left
   against **276** on the right, so the right sleeve is built from a tenth of the cloud and is
   the only place `fitcheck` finds negative gaps. Her weights want re-solving
   ([homecoming.md](homecoming.md) H4).
4. **Torn clothing, and dresses that show a curve** — jacob's original ask, still unstarted.
