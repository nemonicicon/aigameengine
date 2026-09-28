# AUTHORED ANIMATION — build plan

> jacob, 2026-08-26: *"if you are calibrated and working according to my intents, then you
> should be able to author these animations yourself without using premade animations...
> we should be able to author fully custom animations eventually."*

The target is a sentence like **"have reva do a cartwheel"** or **"have kaeli crawl around
cute like a cat, then curl up around reva and fall asleep, purring"** turning into motion
that did not exist before — not a clip found in a donor pack and renamed.

This plan is written to be argued with. Every phase ends in a gate that can be failed, and
the audit notes at the end record what was considered and thrown away, so the same ground
is not re-litigated later.

## 0. The standing principle: the engine is ours

> jacob, 2026-08-26: *"this our engine and we can edit modify it whatever we want to do —
> that is one of the major benefits. we should have total control in our own engine here.
> it is a work in progress."*

The first draft of this plan repeatedly described engine behaviour as a constraint to be
worked around — root motion "cancelled, so that's a precondition", frame snapping as a
choice between baking higher or living with it, a 16-bone canon listed under *what I
cannot do*. All three were wrong in the same way: they treated `crowd_kms.c` as somebody
else's library rather than the thing being built.

**Correct framing:** every one of those is a design decision made earlier, for a narrower
job, that is now due. The engine grows to fit the work. The only real constraints are
physics, frame budget, and my own eye.

What survives as genuinely fixed:
- **Frame budget** — 400 skinned bodies at ~95–102fps, on ANGLE here and bare KMS on a Pi.
  Earned, and not to be spent carelessly.
- **Don't break the eleven doors** — every engine change is regression-gated by
  `certify.sh` and `standsweep.sh`. Freedom to change is not freedom to break.
- **My aesthetic judgment.** No amount of engine control fixes that.

Consequence: engine work is a **first-class track in this plan**, not a tax on the
authoring track. Several of the rungs below are worth building whether or not a single
animation is ever authored.

---

## 1. The substrate that already exists

Worth stating, because it decides how much of this is new:

| piece | state | where |
|---|---|---|
| rpack format (RPK1 v2) | fully understood, writable | `skate_src/glb2rpack_anim.py` writes it; `crowd_kms.c:196` reads it |
| 16-bone canon + resolver | works across 133 packs | `canonOf()`, `Pack.pb[]`, `pbOK` |
| cross-rig transfer | works, with known failures | `donorPose()` rest-delta rotation transfer |
| transfer audits | legs/torso only | `donorOK()`, `standArmsOK()`, `clipUpright()` |
| donor distribution | a clip in a donor pack reaches all 133 | `dnElect()` (S4b) |
| the lab | door 12, one body, clean plate, live control file | `MODE_ANIM` |
| multi-cam wall | N viewports, one live frame | `grid=N`, up to 16 |
| numeric bench | stand + gait + continuity, all self-tested | `anim_bench/` |
| asset artery | UE 5.4–5.8 + Blender 5.1, headless, unattended | `skate_src/kaeli/`, `skate_src/guns/` |

**What does not exist: any path from a pose I decide to a body holding it.** Every clip in
the club arrived as baked data from someone else's capture. That is the hole.

---

## 2. What the two example sentences actually require

Decomposed, they are seven capabilities, and only some are hard:

| # | capability | difficulty | needed by |
|---|---|---|---|
| A | write motion data into the engine | **low** — format is known | everything |
| B | an authoring language expressive enough to be worth using | medium | everything |
| C | rig-agnostic authoring (author once, plays on any body) | **high** | "any character" |
| D | verification that authored motion is physically coherent | medium | everything |
| E | engine plays authored root motion without fighting it | medium, wide blast radius | anything airborne |
| F | multi-actor choreography with spatial relations | medium | the kaeli/reva scene |
| G | audio (purring) | separate discipline, nothing exists | the kaeli/reva scene |

A cartwheel needs A, B, D, E. The kaeli/reva scene needs all seven — which is why it is the
more useful of the two sentences to design against, and why the cartwheel is treated
throughout as one probe among several rather than as the target.

---

## 3. Design decisions, with the rejected alternatives

### 3.1 Author by CONTACTS and TRAJECTORY, not by joint angles
Raw keyframes on sixteen joints is how animators work, and I do not have an animator's
eye — that is the honest constraint this whole plan is shaped around. What I *can* do
reliably is satisfy constraints and check them afterwards.

So the authoring primitive is not *"rotate the upper arm to X"*. It is:

- **contacts** — "the left hand is planted at world point P from t=0.20 to t=0.50"
- **root trajectory** — where the hips go, and when they are ballistic
- **pose targets** — shaping the parts that are not pinned (spine curve, free limbs)
- **timing** — anticipation, ease, overshoot, settle

Contacts are what make motion read as real, *and* they are checkable — which means the
same declaration that produces the motion also verifies it. That property is why this
approach wins over keyframing: the score is the test.

*To be explicit about the interface:* there is no natural-language-to-animation machinery
in this plan and none is proposed. **I am that step.** You say "cartwheel", I write the
score, the tools bake and check it, and you rule on the result. The plan is about making
that loop fast enough and verified enough to be worth running.

### 3.2 Analytic 2-bone IK in Python. Not Blender — for now
A humanoid limb is two bones; solving elbow/knee for a wrist/ankle target is closed-form
(law of cosines plus a swivel angle). About forty lines, exact, no dependency, iterates in
milliseconds.

*Rejected:* driving Blender headless via `bpy`. It is a real solver and the artery already
runs it — but it is the wrong tool for a closed-form problem, and the round trip
(rpack → armature → keyframe → GLB → rpack) adds fidelity risk and seconds per iteration
to a loop I will run hundreds of times. **Kept as the named escape hatch** if full-body IK
with balance is ever needed (P6+); the rpack↔armature bridge is the only new part.

### 3.3 Append-only rpack writing
All rpack offsets are absolute from file start. New clip data, a new string table and a new
clip table can be appended at the end, and only four header fields repointed
(`nClips`, `offClips`, `offStr`, `total`). Existing bytes — nodes, meshes, textures,
old clips — are never touched, so a bug in the writer cannot corrupt a working pack.

*Rejected:* full parse and re-emit. More flexible, far more surface area to get wrong.

### 3.4 Authored clips live in their own pack, with a reserved name prefix
Authored clips go into `reva_native/packs/v1_moves.rpack` — and that pack **carries the
reference skeleton**: `bake_moves.sh` copies `v1_manny.rpack` and appends the `z_` clips
to it. This is not a convenience, it is what keeps P0–P4 honest. If the moves pack were
skeleton-less and donor-only, then *every* authored playback would go through `donorPose`
from day one — and P0.5's "arm at 90° ± 2°" would secretly be measuring the retarget,
whose correctness is not due until P5. Until P5, the lab subject **is** the moves pack:
direct playback, no transfer, the measurements test the authoring and nothing else. From
P5 onward it is also registered as a donor, which is when authored moves reach other
bodies.

This matters more than it looks. `dnElect()` elects donor roles by **exact name**
(`idle`, `x_fidget*`, `w_walk`, `w_run`, `x_down`, `x_gunfire`, `x_flee`, `x_convo*`,
`v_walk*`) and the pack-side miner elects by **substring** (`run`, `walk`, `idle`, `stand`,
`attack`, `death`…). An authored clip that accidentally matches would silently change the
behaviour of all 133 packs.

**Reserved prefix `z_`** — unused by every miner. And every authored name is linted against
the miners at bake time, because the substring rules bite: a clip called `z_crunch`
contains `run`.

### 3.5 The score is the artifact of record
Repo convention is recipes in, artifacts out — rpacks are gitignored. So each authored
motion is a committed Python score in `skate_src/author/moves/`, and
`bash skate_src/author/bake_moves.sh` regenerates the pack. Same shape as `bake_guns.sh`.

### 3.6 Canonical bone basis — the correctness upgrade that also pays an old debt
Rest-delta transfer assumes a delta expressed in the donor's local bone frame means the
same thing in the target's. When rest orientations differ by a rotation R, the delta needs
conjugating: `delta_target = R · delta_donor · R⁻¹`. Not doing that is why reva's run
mangles, and why arms fly out on borrowed stands.

Computing R needs a per-bone frame derived from the rest pose:
- **primary axis** — direction from this joint to its canonical child
- **secondary axis** — for a limb, the bend-plane normal of the three-joint chain
  (shoulder→elbow→wrist), which is anatomically meaningful and stable
- third by cross product

**The same machinery does both jobs**: author in canonical space and conjugate into each
rig's frame, *and* fix `donorPose`. One build, two payoffs — which is why P5's gate is tied
to reva's run planting its feet.

*Known degeneracy:* a perfectly straight limb has no bend plane (the cross product
vanishes). Needs a fallback axis, and rigs that bind fully extended must be detected.

### 3.7 External motion generators — MotionBricks (assessed 2026-08-26)
jacob surfaced NVIDIA's MotionBricks (SIGGRAPH 2026): a latent generative model, 350k
skills, 2ms latency, driven by "smart primitives" — velocity/heading/style commands plus
**proxy keyframes**. That is this plan's §4 interface shape, arrived at independently
with a trained model where we use a closed-form solver. Treat that as validation of the
abstraction, not competition for the spine. The ruling, in this repo's terms:

- **Runtime: never.** PyTorch/CUDA inference inside `crowd_kms.c` breaks the one-file /
  ANGLE-here / KMS-on-a-Pi thesis. Same ruling as UE: asset source, not runtime.
- **As an asset source: parked behind P5.** The released model (Apache 2.0 code,
  commercially usable weights) emits **Unitree G1 robot joints into MuJoCo** — robot
  topology, no spine chain, no clip export. Robot→canon retargeting is the rest-basis
  minefield P5 clears; before P5 this pipeline manufactures crumpled tangles at scale.
- **The sleeper is BONES-SEED** (their training data, released separately): 142k tagged
  real mocap sequences, sibling set in BVH/FBX → the existing artery, zero new
  machinery. Gated: free for academics and "qualifying startups" — jacob's application
  to make. Logged in ROADMAP's artery queue.
- **What it cannot do for the stated goal:** out-of-distribution motion (a cat-crawl, a
  curl around another body), two-character contact, "cute" as an input, or determinism
  (the ledger requires same score → same bytes). And anything it generates still faces
  `movecheck` and jacob's eyes — the bench is the moat either way.
- **Possible later instrument:** a comparison lane in P4 — the same brief authored vs
  generated, scored by the same corpus envelope. One lane, only if it earns its keep.


---

## 4. The authoring language

The centre of the system. A **score** is a Python file describing one move as declarative
constraints; a solver turns it into frames. The cartwheel was only ever a probe — the
design below is checked against five deliberately different kinds of motion, because a
language that only expresses acrobatics is the wrong language.

### Primitives

| primitive | meaning |
|---|---|
| `contact(part, at, t, frame=)` | a body part is pinned to a point over a time span |
| `root.*` | where the hips go — explicit keys, or *derived* (`ballistic`, `follow_contacts`, `roll`, `sway`) |
| `pose(t, **targets)` | shape for whatever is not pinned — spine curl, leg split, head aim |
| `ease(...)` | timing curve per segment — `anticipate`, `settle`, `snap`, `overshoot` |
| `layer(...)` | procedural secondary motion — drag, overlap, follow-through |
| `mask(...)` | which bone groups this score owns; the rest come from underneath |
| `tail(...)` | how the move ends — return to stand, or hand off to a loop |

*The table is provisional until P4 locks it.* The worked examples below already lean on
sugar the table does not define (`osc`, `ease.settle`, `gait`) — each must either become a
primitive or desugar into the seven above. The proving set is where the real surface gets
decided; pretending the language is final before four different moves have pushed on it
would be designing in the dark.

**Contacts carry a reference frame.** `frame=WORLD` for a floor plant, `frame=SELF` for a
hand on your own knee, `frame=actor("reva")` for curling against somebody. Deciding this
now — rather than making contacts world-absolute and retrofitting later — is what makes
the director phase a widening instead of a rewrite. It is the same lesson as authoring in
canonical space from day one.

### Five worked shapes, and what each one stresses

**1. Ballistic / acrobatic** — a cartwheel. Stresses root physics and contact hand-off.
```python
Move("z_cartwheel", dur=1.25, fps=60)
  contact("foot.L", floor_at(0.00), t=(0.00, 0.22), ease=anticipate)
  contact("hand.L", floor_at(0.62), t=(0.20, 0.55))
  contact("hand.R", floor_at(1.10), t=(0.45, 0.80))
  contact("foot.R", floor_at(1.72), t=(0.75, 1.05))
  root.roll(axis=TRAVEL, turns=1.0, t=(0.10, 1.00))
  root.follow_contacts()            # solve hip height so plants land on the floor
  pose(t=0.50, legs=split(140), spine=extend(15))
  tail.stand(t=1.05)
```

**2. Cyclic locomotion** — a cat-like crawl. Stresses phase scheduling and loop closure.
```python
Cycle("z_crawl_cat", period=1.10, fps=60)
  gait(order=["hand.L","foot.R","hand.R","foot.L"], phase=[0,.25,.5,.75], duty=0.62)
  root.height(0.38 * LEG); root.sway(amp=0.05, freq=2)   # weight shifts side to side
  pose.spine(arch=-15); pose.head(aim=FORWARD_DOWN)
  layer.drag("forearm", lag=0.05)   # paws trail the shoulder slightly
```
*Stress it reveals:* a cycle must **close** — frame 0 and frame N−1 have to match within ε
or the loop pops once per period. That becomes a `movecheck` test.

**3. Settling into a sustained state** — curl up and sleep. Stresses easing and endings.
```python
Move("z_curl_sleep", dur=3.0, fps=60)
  pose(t=0.0, from_=STAND)
  pose(t=1.2, spine=curl(60), knees=tuck(110), hips=low)
  contact("hip.R", floor, t=(1.35, END)); contact("shoulder.R", floor, t=(1.6, END))
  ease.settle(t=(1.6, 2.6), damping=0.7)          # weight coming to rest, not snapping
  tail.loop("z_sleep_breathe")                    # hand off to a breathing loop
```
*Stress it reveals:* a move must be able to **end in a loop**, not only return to a stand.
The first draft assumed every authored move was a one-shot bracketed by stands.

**4. Gesture / expression** — a wave, a beckon. Stresses layering.
```python
Move("z_wave", dur=1.4, fps=60, mask=UPPER_BODY)
  pose(t=0.15, arm.R=raise(120), ease=anticipate)
  osc("arm.R", axis=Z, amp=18, cycles=2.5, t=(0.25, 1.1))
  layer.drag("forearm.R", lag=0.07)               # the hand trails the wrist
  tail.blend_out(0.2)
```
*Stress it reveals:* **masking**, so a wave rides on top of a walk instead of replacing it.
The engine already has the machinery — the S7 arm mask restores an arm chain to rest while
the body plays a donor stand. Same idea, generalised to named bone groups.

**5. Two bodies in contact** — curling against reva. Stresses frames and interpenetration.
```python
Scene("s_kaeli_sleeps_on_reva")
  actor("reva",  at=(0,0,0), facing=0, play="z_sit_relaxed")
  actor("kaeli", play=["z_crawl_cat" * 3, "z_curl_sleep"],
        path=approach("reva", stop_at=0.55, facing=TOWARD))
  contact("kaeli.hip.R", frame=actor("reva").local(0.30, 0.10, -0.15), t=(FROM_CURL, END))
```
*Stress it reveals:* everything the director phase needs — relative placement, per-actor
sequencing, and contacts resolved in another body's frame.

### What the solver does with a score
1. Resolve contact frames to world points at each time
2. Solve the root trajectory (explicit → ballistic → `follow_contacts`, in that order)
3. 2-bone analytic IK for every limb with an active contact
4. Blend pose targets into whatever is unpinned, with the declared easing
5. Apply procedural layers (drag, overlap, sway)
6. Sample to `fps`, lint the name against the clip miners, append to `v1_moves.rpack`
7. Run `movecheck` — the score's own declarations are the test fixture

Step 7 is the property that makes this design worth having. **The score is simultaneously
the instruction and the test**, so every authored move arrives with its own verification
already written.

---

## 5. Composition: how moves combine

Three mechanisms, all extensions of machinery already in the engine rather than new
systems:

- **Sequence** — move A then move B across a blend window. The crossfade weight `rvXf`
  already exists and already resets whenever the clip key changes.
- **Masked layer** — a score declares which bone groups it owns (`UPPER_BODY`, `ARMS`,
  `SPINE`, `LEGS`); everything else comes from the layer beneath. This is exactly the S7
  arm mask generalised from one hardcoded chain to named groups.
- **Additive** — a score marked additive contributes *offsets* from rest rather than
  absolute rotations, so a limp or a shiver rides on top of any gait.

Priority when a move is requested mid-state: a move interrupts an idle, queues behind an
attack, and never interrupts a death. That ordering wants to live beside the existing
`BHV_` state tree, not in a parallel system.

---

## 6. Verification design

Given that two of the three "defects" found on this bench turned out to be bugs in the
measuring tool, the instrument gets designed as carefully as the thing it measures.

### `anim_bench/movecheck.py`
| test | how | source of truth |
|---|---|---|
| contact fidelity | declared contact met within **2cm** | the score |
| floor penetration | no joint below y=0 beyond tolerance | physics |
| ballistic truth | during declared flight, root Y is parabolic — and its *g* matches the club's | physics |
| bone length | invariant (already built, upleg→knee, one joint) | rig geometry |
| joint limits | no hyperextension or inversion; knees, elbows, spine | anatomy |
| self-intersection | limb segments vs a torso capsule, approximate — **tolerance comes from the score**, see below | geometry |
| continuity | no pops, measured on the collapsed step sequence | already built |
| **loop closure** | for cycles, frame 0 ≈ frame N−1 within ε | the score |
| terminal state | ends `posturecheck`-clean, or in its declared loop | the score |
| **corpus envelope** | inside the distribution of real captured motion | see below |

*A tension the proving set deliberately contains:* `z_curl_sleep` is the intersection
checker's hardest case — a curl is *made of* near-contacts, and one global ε loose enough
to pass it would pass garbage everywhere else. So the checker's tolerance is per-move,
taken from the score itself: parts the score declares close (a `contact` in another part's
frame, a tuck) are exempt down to skin distance; everything undeclared gets the strict ε.
Declared closeness is intent; undeclared closeness is a defect.

### `anim_bench/corpus.py` — real motion as the reference
The strongest available answer to *"is this any good"* short of jacob's eyes, and it costs
almost nothing because the reference already sits on disk: **every clip in the club is real
mocap**, plus 777 animation assets in `CivicDefenseV2`.

Extract, per motion class, the statistics that separate captured human movement from
hand-keyed approximations:
- joint **angular velocity** — peak and RMS, per joint
- **jerk** (third derivative) — real motion has a characteristic smoothness; hand-keyed
  motion is usually too clean between keys and too abrupt at them
- **contact durations** and duty factors
- **centre-of-mass** vertical trajectory and its oscillation
- **limb phase coupling** — arm swing against leg swing
- **settle times** — how long real motion takes to come to rest

Then `movecheck` reports where an authored move falls outside the envelope of real motion.
This does not measure charm. It measures *plausibility as human movement*, which is a
different thing and is the part I can actually get better at with a number in front of me.

### Testing the tools themselves
Two of the first three "defects" this bench ever found were bugs in the measuring tool, and
a third surfaced this session (a foot-skate ratio with no contacts scored 0.00 — a perfect
PASS for a body whose feet never touched the ground). The authoring tools get the same
discipline `posturecheck --selftest` already has:

- **IK selftest** — place a target at a known point, solve, assert the end effector lands
  within ε. Sweep targets from fully-folded to fully-extended, including the two
  degenerate cases: target beyond limb reach, and target at the joint itself.
- **Solver selftest** — author a score whose answer is known analytically (a limb rotating
  at a constant rate), assert the baked frames match closed form.
- **Round-trip test** — read an rpack, write it back unchanged, assert byte equality.
- **Determinism** — the same score must bake to the same bytes. Without this the
  calibration ledger measures noise, and no diff between two attempts means anything.

### Coverage, stated honestly
- **Rig resolution varies** — `pbOK` is 13, 14 or 15 across the humanoid roster. A score
  must degrade gracefully when a canonical bone is missing (skip that target, report it)
  rather than producing a silently wrong pose. `movecheck` reports which targets were
  dropped per body.
- **The 24 non-canon packs cannot receive humanoid authored motion at all** — a lobster
  has no thigh. They are out of scope by construction, not by oversight, and authoring for
  them would be a separate vocabulary.

---

## 7. The iteration loop

The loop's wall-clock decides whether this capability is usable at all, so it is a design
target rather than an afterthought:

```
edit  skate_src/author/moves/z_thing.py
bash  skate_src/author/bake_moves.sh          # score -> v1_moves.rpack     ~1s
bash  anim_bench/live.sh reload               # RUNNING lab reloads it      ~0.2s
bash  anim_bench/live.sh clip z_thing grid 9  # plays it, nine angles
bash  anim_bench/live.sh measure              # movecheck on the pose dump
```

**Seconds per iteration, no reboot.** That is why `clip=` and `reload=` are in P0 rather
than filed as tooling — an 11-second boot per attempt is the difference between authoring
and guessing. jacob has raised this twice: *"you should be able to leave this running and
fine tune it live, not open it screenshot it and close it."*

---

## 8. Phases

Each phase is finished-or-parked. Nothing proceeds on a failed gate.

### P0 — Write a clip at all
**Build:** `skate_src/author/rpack.py` — reader (nodes, meshes, clips, strings, textures)
and append-writer. Port `canonOf()` to Python. A lab control-file command `clip=<name>` so
an arbitrary clip can be played on demand, and `reload=1` so a re-baked pack loads into the
**running** lab without a reboot.

**Gate — falsifiable:** append a byte-copy of one of manny's own clips into the moves pack
(which carries manny's rig, §3.4) under a new name; played **as the subject, direct, no
transfer**, the copy must produce **frame-for-frame identical joint positions** to the
original under `REVA_POSEDUMP`. Not "it loads" — identical numbers. Same-rig direct
playback is what makes "identical" a fair demand; through a retarget it never would be.

**Also in P0 — exempt authored clips from the gait audits.** `clipUpright()` requires hips
above the lowest foot across the whole cycle, and `donorOK()` requires the same of a
transfer. A cartwheel fails both *by design*, and so does anything on the floor. The audits
must apply to clips being elected into a **locomotion or stand role**, not to a clip played
because it was asked for by name. Without this the cartwheel is refused before it renders.

*Why the control commands are in P0, not later:* without `clip=` I cannot see what I
authored, and without `reload=` every iteration costs an 11-second boot. jacob has already
called this out twice — *"you should be able to leave this running and fine tune it live."*

### P0.5 — The hello pose
**Build:** author a clip that holds ONE static non-rest pose: right upper arm raised 90°.

**Gate:** `posturecheck` measures ARM R at 90° ± 2°.

Trivially small, and deliberately its own gate. It isolates *can I control a joint at all*
from every timing and IK concern, and catches quaternion order, basis and node-mapping
errors immediately. This session alone produced three convention bugs that a check this
cheap would have caught in seconds.

### P1 — Keyframes, interpolation, timing
**Build:** the keyframe layer — pose targets on canonical joints, slerp, easing curves.
Author a **bow**: torso pitches forward 45°, holds, returns.

**Gate:** measured torso pitch hits 45° ± 2° at the declared time; continuity clean
(no pops on the step sequence); eyes on a GIF, not a still.

**Also in P1 — one-shot playback and the transitions either side.** Every authored move
that is not a loop needs to play **once** and hand back. The engine already has the
mechanism in skate mode (`onceClip` / `onceT` running a clip's own duration exactly once)
and a crossfade weight (`rvXf`, reset whenever the clip key changes) — both need wiring to
authored moves. A cartwheel entered with a pop and exited with a snap is not a cartwheel,
and P4's "returns to a clean stand" gate cannot be met without this.

### P1.5 — The interpolating sampler (engine rung E1)
Not a decision point. A fix, and the highest-value one in this document.

**What is there now:** every clip in the club is sampled **nearest-frame with no
interpolation** — `((unsigned)phase) % frames`, then a `memcpy` of that frame's raw
quaternion. Measured this session: at an 18fps bake against a 97fps render, **every pose
is held for about five render frames.** That is a systemic stair-step under all 133 packs,
every dancer, every walk, every borrowed stand — not just anything authored.

It is also copy-pasted across **eight sampling sites** (`crowd_kms.c` lines 424, 1374,
2205, 2245, 3993, 4006, 4019, 4048), which is why nobody noticed it as one thing.

**Build:** consolidate into one `clipPose(P, clip, phase, T, Q, S)` that takes a
*fractional* phase and slerps rotations / lerps translations between frame N and N+1.
Replace all eight call sites with it.

**Gate:** the club-wide step rate rises from ~18/s to the render rate, measured by
`posturecheck`'s `clip stepping` readout; `certify.sh` and `standsweep.sh` show no
regression; frame time holds at ~95–102fps with 400 bodies armed.

⚠ **Frame budget is the one real constraint here.** 400 slots × ~20 tracks × 60fps is
roughly half a million slerps a second. Almost certainly fine, but measure — and if it is
not, interpolate the hero always and the crowd by distance LOD. That is a genuine
engineering trade; "bake at 60fps and live with it" was me ducking it.

*This rung pays every door and is worth building even if no animation is ever authored.*

### P2 — Contacts and IK
**Build:** analytic 2-bone IK for all four limbs. Contact declarations. Author-time
grounding — compute the root trajectory so declared contacts sit at floor level.

**Also in P2 — `corpus.py` v1, over the club's own packs.** They are all real mocap, so
the envelope costs one measurement pass. The CivicDefense 777 widen it later via an artery
batch — an upgrade, not a prerequisite. Scheduled here because P4 *depends* on the corpus
existing and, until this pass, no phase actually built it.

**Gate:** `movecheck.py` confirms every declared contact is met within **2cm**, no joint
penetrates the floor beyond tolerance — and `corpus.py` reproduces known numbers from the
bench (manny's walk cadence and contact duty are already certified; the corpus must agree
with the certificate before it is trusted as a reference).

### P3 — Root ownership (engine rung E2)
**What is there now:** `model[13] = 0.14 − (lowest ankle y)·scale`, computed from the FK
result — which already contains any hip translation the clip authored. Raise the hips by
Δ, the ankles rise by Δ, the model drops by Δ. Net zero. With feet overhead the lowest
ankle is *high*, so the body is driven underground.

That is not a bug and not an obstacle. It is auto-grounding, written when every clip in
the club was an in-place cycle with no root motion — correct for the job it had. The
engine simply only implements **one** of the three ways a root can be owned.

**Build the whole model, explicitly:**

| owner | when | who sets Y |
|---|---|---|
| **gameplay** | skate physics, platformer jump/fall, knockdown | already does, additively via `rvAlt` |
| **clip root motion** | the clip carries a hips translation track | *missing — this is the new one* |
| **auto-ground** | in-place cycle, no root track | current behaviour, kept as the default |

Resolved in that priority order, decided per clip, stated in one place instead of implied
by an arithmetic accident. While in there: the hardcoded `0.02 + 0.12` sole allowance is a
constant tuned for reva's boots — derive it, or fold it into the S7 floor clamp that now
does that job properly.

**Gate:** `certify.sh` shows no gait regression (manny WALK 0.15 / RUN 0.19 hold), the
ten-human `standsweep` stays clean, and an authored root-motion clip plays with contacts on
the floor.

⚠ Widest blast radius in the plan — the hero's model matrix in all eleven doors. Not a
reason to sneak it in behind a flag; a reason to regression-gate it and land it properly.

### P4 — The proving set (not one move)
The cartwheel was jacob's probe for what I should be capable of, not the deliverable.
Building the language against a single move is how you get a language that authors exactly
one move. So P4 is **four scores, chosen because they fail differently**:

| move | proves | fails if |
|---|---|---|
| `z_cartwheel` | ballistic root, contact hand-off, inversion | root physics or contact solving is wrong |
| `z_crawl_cat` | phase-scheduled cycles, loop closure | cycles pop once per period |
| `z_curl_sleep` | easing, settling, ending in a loop | endings only work as return-to-stand |
| `z_wave` | masking, layering onto an existing gait | scores can only replace, never ride on top |

Timing and proportion come from published biomechanics and from `corpus.py`'s measured
envelope, not from my taste.

**Gate:** all four pass `movecheck` — contacts met within 2cm, no floor penetration,
parabolic flight with the right *g*, bone lengths invariant, cycles closed, each ending as
declared. **Then jacob's eyes, on GIFs and nine-angle walls.** The numbers prove these are
physically coherent motions; only jacob can say whether they are *good* ones — and whether
the cat reads as cute, which is the whole point and is not a number.

*If three of four pass and one does not, that is a language finding, not a move finding —
the failing one says which primitive is underspecified.*

### P5 — Canonical basis: author once, plays on any body
**Build:** per-bone rest bases; author in canonical space; conjugate into each rig's local
frame. Apply the same conjugation inside `donorPose`.

**Gate — two, and the second is the interesting one:**
1. The cartwheel plays on **≥8 of the 10 sweep humans** — `movecheck` clean, and the
   **terminal stand** `posturecheck`-clean. (Not posturecheck on the move itself: a body
   mid-inversion fails a standing audit *by design* — the same argument that moved the
   gait audits to role election. An earlier draft of this gate made exactly the mistake
   this plan warns against.)
2. **reva's run plants its feet.** It currently reads `THE FEET NEVER PLANT` — 0 contacts
   in 8 seconds. A measurable skate ratio would mean the basis fix landed.

⚠ **Performance.** `donorPose()` already runs a rest-delta transfer every frame for up to
400 slots. A per-bone basis conjugation on top of that, per slot, per frame, is not free.
The conjugation R is **static per (donor bone, target bone) pair** — it must be computed
once at election time and cached beside the existing `donorOK` verdict cache, never in the
inner loop. Measure frame time before and after; the floor is ~95–102fps fully armed.

*If gate 2 fails,* the fallback is the conclusion already reached earlier: offline
per-pack retargeting through Blender, baked once, deleting the runtime transfer entirely.
Named here so it is a planned branch, not a surprise.

### P6 — The director: multiple actors, spatial relations
⚠ **Hidden engine rung first (E3): a second hero-grade body.** The engine has exactly
**one** hero animation path — donor transfer with masks, one-shot playback, root
ownership, the floor clamp all live on the hero; the 400 slots run the simpler crowd path
(no root motion, no masks, no one-shots). A scene needs at least two bodies at hero grade.
So P6 *starts* with engine work — promote a slot to hero-grade animation or instance the
hero path — and only then is "a scene score" a scripting problem. Costing this as
scene-scripting alone would have been the plan's biggest under-estimate.

**Build:** the E3 rung above, then a scene score — actors, clips, placements **relative to
each other** ("curl up *around* reva" = a position derived from reva's transform and
facing), sequencing, blends between clips. A mode or lab state that plays a scene.

**Gate:** the kaeli-and-reva scene runs end to end — crawl, approach, curl, settle — with
kaeli's placement derived from reva's, and no interpenetration.

⛔ **If the director gets its own camera, gate it against the F310 chase cam in
`camDirector` FIRST.** That path fires before the shot switch whenever `g_manual=1` and
has eaten three game lenses.

### P7 — Audio (named, separate, not in this plan)
Purring needs an SFX layer that does not exist — it is a Phase 2 shared rung in
`ROADMAP.md`. Sound bank, clip-synced triggers, SDL audio. Its own build.

---

## 9. Risk register

| risk | severity | mitigation |
|---|---|---|
| Convention errors (quaternion order, basis, node mapping) | high, silent | P0.5 hello pose isolates all three |
| Interpolating sampler costs frame time on 400 slots | medium | measure; hero always, crowd by distance LOD if needed |
| Root-ownership change touches all 11 doors | high | explicit 3-way model, `certify.sh` + `standsweep.sh` gated |
| **Treating our own engine as fixed** | **high — already happened** | §0; engine rungs are a first-class track |
| Language shaped around one example move | high | P4 is four scores that fail differently, chosen for that |
| Cycles pop once per period (loop not closed) | high, easy to miss | loop-closure test in `movecheck` |
| Corpus envelope pulls everything toward bland realism | medium | open question 4 — gate or reference, jacob's call |
| My hit rate never improves | **the quiet one** | calibration ledger (§11); if it does not trend, change approach |
| P3/P5 blast radius across 11 doors | high | env flags, A/B, `certify.sh` + `standsweep.sh` |
| Canonical basis degenerate on straight-limb rigs | medium | fallback axis; detect and report |
| Accidental donor election changes 133 packs silently | high, silent | `z_` prefix + bake-time lint against the miners |
| Gait audits refuse the cartwheel before it renders | **certain** if unhandled | audits gated to role election, not to play-by-name (P0) |
| Basis conjugation costs frame time on 400 slots | medium | R is static per bone pair — cache at election, never in the loop |
| Authored one-shot pops on entry/exit | high | reuse `onceClip`/`rvXf`; P4's stand gate cannot pass without it |
| **My aesthetic judgment** | **high, unfixable by me** | physical invariants + published reference proportions + jacob's eyes at every gate + GIFs, never stills |
| Scope explosion | medium | hard gates, finished-or-parked |

---

## 10. What I cannot do, stated plainly

Two different things were mixed together in the first draft, and separating them matters:

### Genuinely mine, and not fixable by any amount of engine control
- **Judge whether it looks good.** I misread a crumpled pose as a run this session. Every
  gate ends with jacob's eyes for a reason; the numbers prove coherence, not quality.
- **Make "cute" a measurable target.** I can hit contacts, timing and proportion. Charm is
  a verdict, and it is yours.

### Merely unbuilt — engine rungs, costed, not limits
- **Fingers, toes, faces.** The canon is 16 bones because that was enough for dancers.
  `canonOf()` is a name table and `pb[]` is an array — extending to hands (2–3 bones per
  finger) and toes is mechanical. The real cost is upstream: most packs' *rigs* may not
  carry those bones, so it is an artery question (what the source assets have) far more
  than an engine one. Worth doing when a move needs it — a cat kneading its paws does.
- **Spine resolution.** Five chain bones (spine, spine1, spine2, neck, head) is enough for
  a curl and thin for a spine-led arch. Extendable the same way.
- **One-shot playback, root ownership, interpolation.** All listed as constraints in the
  first draft. All ours. All in the phases above.

---

## 11. What "done" looks like

The capability is finished when jacob names a motion in a sentence and, inside one working
session, it exists in the club: authored, passing `movecheck`, playing on any humanoid
body, committed as a regenerable score — and his verdict is favourable more often than not.

That last clause is the real bar, and it is the one this plan is least able to guarantee by
construction. So it gets measured.

### The calibration ledger — `design/authored_moves.md`
One row per authored move, kept from the first one:

| move | attempts to first yes | what jacob corrected | what it taught |
|---|---|---|---|

Not bookkeeping. jacob's framing was *"if you are calibrated and working according to my
intents"* — and calibration is a feedback loop or it is nothing. The ledger is the only
mechanism in this plan that operates on **my judgment** rather than on the engine or the
tools. Physical invariants and the corpus envelope raise the floor; only accumulated
corrections raise the ceiling.

Two things to watch in it:
- **Attempts-to-first-yes should trend down.** If it does not after a dozen moves, the
  approach is wrong and reference-matching should replace taste-guessing outright.
- **Recurring corrections become primitives.** If jacob says "too fast to settle" three
  times, that is not three notes, it is a missing default in `ease.settle`.

---

## 12. Open questions for jacob

1. **Order.** Proving set first (P0→P4, single body, provable), or rig-agnostic first
   (P5 early, so everything authored after works on the whole roster)? Proving-set-first is
   recommended — it exercises the language end to end and P5 becomes a widening rather than
   a rewrite, because the language is canonical and frame-relative from day one either way.
   **A case for going earlier on P1.5 (the interpolating sampler):** it is independent of
   all of this, improves every animation already in the club, and is the smallest rung here.
2. **How far before you want to see something?** The plan stops cleanly after P4 with a
   real authoring tool and four moves. P5/P6 are the difference between "reva can do it"
   and "anyone can do it, together."
3. **House style.** Timing and proportion would come from published biomechanics and the
   measured corpus. That gives *plausible*, not *yours* — the club has a look, and a
   cartwheel in a dance club is not a gymnastics-floor cartwheel. If there is a reference
   you have in mind (a game, a show, a specific weight and snap), that is the single most
   useful thing you could hand me, and it is exactly the intent I cannot infer.
4. **How much stylisation is wanted at all?** The corpus envelope pulls authored motion
   toward *realistic*. Cartoon timing — big anticipation, snappy extremes, held poses —
   deliberately violates it. If the club's look is stylised, the envelope becomes a
   reference rather than a gate, and I should know that before I start tuning against it.

---

## Audit log

Three passes over this plan before it was written down. What changed:

- **Added P0.5.** The first draft went straight from the writer to keyframed motion. Given
  three convention bugs in this session alone, a static single-joint pose with a numeric
  gate is worth its own step.
- **Moved `clip=` and `reload=` into P0.** They were implicit "tooling" — but without them
  there is no iteration loop, and jacob has twice asked for live tuning over
  boot-shoot-close.
- **Made the authoring language canonical from day one**, resolved against a single rig
  first. The first draft authored in manny's local space, which would have made P5 a
  rewrite of every score rather than a widening.
- **Promoted the grounding conflict from a P4 detail to its own phase.** It was verified
  during planning that authored vertical root motion is cancelled *exactly*, not merely
  perturbed — so it is a precondition for anything airborne.
- **Added the donor-collision risk.** Nearly missed: an authored clip named to match the
  exact-name or substring miners would silently retarget all 133 packs.
- **Tied P5's gate to reva's run.** The basis work was originally justified only as
  "author once, play anywhere". It is also the diagnosed root cause of the oldest open
  animation defect, so the gate should test that claim.
- **Made every gate falsifiable with a number.** "Looks right" appears nowhere except
  alongside jacob's eyes, which is the one place it belongs.

**Fourth pass, over the written document rather than the intention.** Four gaps that the
first three missed, all of them integration rather than design:

- **The gait audits would refuse the cartwheel.** `clipUpright()` and `donorOK()` demand
  hips above feet across the whole cycle. Every interesting authored move — cartwheel,
  crawl, curl, sleep — fails that by design. The audits belong on *role election*, not on
  playing a clip by name. This would have been discovered as a baffling silent refusal
  somewhere in P4.
- **One-shot playback and its transitions were never assigned to a phase.** They were
  assumed. The mechanism exists in skate mode and needs wiring; without it P4's
  "returns to a clean stand" gate is unreachable.
- **The basis conjugation had no performance note** despite running inside a function that
  already executes for 400 slots every frame. R is static per bone pair; it must be cached
  at election time.
- **The interface was left implicit.** Worth stating outright that no natural-language
  layer is proposed and I am that step, so nobody later expects a parser that was never
  planned.

**Fifth pass — jacob, on reading the draft:** *"this our engine and we can edit modify it
whatever we want... it is a work in progress."*

He was right, and the fault ran deeper than wording. Three separate places described
`crowd_kms.c` as something to design *around*:

- Root motion cancellation called **"a precondition, not polish"** — as though the engine
  had ruled. It is auto-grounding written for in-place cycles, and the fix is to state the
  root-ownership model properly (§P3). One of three cases was implemented; build the
  other two.
- Frame snapping offered as **"bake at 60fps, or add interpolation"** — hedging a fix as a
  choice. It is a stair-step under all 133 packs at eight copy-pasted call sites, and it
  is now its own rung (§P1.5), worth building even if nothing is ever authored.
- The 16-bone canon filed under **"what I cannot do"**, next to my aesthetic judgment.
  Those are not the same kind of thing at all. §10 now separates what is actually mine from
  what is merely unbuilt.

The tell in all three: I reached for a workaround before asking whether the engine should
change. Added §0 as the standing correction, and named it in the risk register, because
this is a habit rather than a one-off — and the habit is worth more attention than any of
the three instances.

**Sixth pass — jacob:** *"the cartwheel was just an example of me seeing what you should be
capable of."*

Taken as a design instruction, not a clarification. The plan had quietly become a plan to
build a cartwheel:

- **The language was one paragraph** (§3.1, now §4) and the phases were pages. Backwards — the
  language is the whole system. §4 now specifies it, and specifies it against **five
  deliberately different motions**, which immediately surfaced two requirements the
  cartwheel alone would never have shown: a move must be able to **end in a loop** rather
  than always returning to a stand (curl-up-and-sleep), and contacts must carry a
  **reference frame** rather than being world-absolute (curling against another body).
  Both are cheap now and structural later — the same lesson as authoring canonically from
  day one.
- **P4 became a proving set of four** chosen because they fail differently. One move
  proves one code path; four that stress root physics, cycle closure, endings and masking
  prove a language. If three pass and one fails, the failure names an underspecified
  primitive.
- **Added `corpus.py`.** Every clip in the club is real mocap, and 777 more sit in
  `CivicDefenseV2` — so the statistics of real human movement (angular velocity, jerk,
  contact duration, COM oscillation, settle time) are free reference data already on disk.
  Authored motion can be checked against that envelope. It does not measure charm; it
  measures plausibility as human movement, which is the part of my weakness that a number
  can actually address.
- **Added §11 and the calibration ledger.** jacob's original framing was about *me* being
  calibrated to his intent. Every other mechanism in this plan improves the engine or the
  tools; the ledger is the only one that operates on my judgment. If attempts-to-first-yes
  does not trend downward, the approach is wrong and should change.
- **Added two open questions** about house style and stylisation. The corpus envelope
  pulls toward realistic, and cartoon timing deliberately violates it — that is jacob's
  call to make before I tune anything against it, not after.

**Seventh pass — over the tools rather than the design.** Three gaps, all of the kind that
this bench has been bitten by before:

- **The authoring tools had no self-tests**, while the measuring tools do. Given that three
  separate instrument bugs have been found here — two early, and a foot-skate ratio that
  scored a perfect PASS for a body whose feet never touched the ground — the solver and the
  IK need `--selftest` from the start, including their degenerate cases (target beyond
  reach, target at the joint).
- **Determinism was never required.** If the same score does not bake to the same bytes,
  the calibration ledger is measuring noise and no diff between two attempts means
  anything. Now a stated requirement.
- **Graceful degradation was assumed.** `pbOK` is 13, 14 or 15 across the humanoid roster,
  so some bodies are missing canonical bones a score may target. Dropping a target must be
  reported, not silent — a silently wrong pose is exactly the failure mode this whole bench
  exists to prevent. And the 24 non-canon packs are out of scope by construction: a lobster
  has no thigh.

**Eighth pass — a cold read of the finished document, end to end.** Six findings; the
first would have quietly invalidated three phases:

- **Every authored playback would have gone through `donorPose` from day one.** The moves
  pack was specified as donor-only, so playing `z_` anything on a lab subject meant a
  cross-rig transfer — and P0.5's "arm at 90° ± 2°" would secretly have measured the
  retarget, which is not due until P5. A wrong number there would have been undiagnosable:
  authoring bug or transfer bug, no way to tell. Fix in §3.4: the moves pack carries the
  reference skeleton (a copy of manny's pack), the lab subject *is* the moves pack until
  P5, playback is direct. P0's gate is re-worded to demand same-rig directness explicitly.
- **`corpus.py` was P4's dependency and no phase built it.** It appeared in §6 as a tool
  and in P4 as a reference, and was never scheduled. Now built in P2, club packs first
  (the 777 CivicDefense assets are a later widening), gated on reproducing numbers the
  bench has already certified.
- **P5's gate demanded `posturecheck` on the cartwheel** — a standing audit on a body
  mid-inversion, the precise mistake the plan itself warns about when it moves the gait
  audits to role election. Now: `movecheck` on the move, `posturecheck` on the terminal
  stand only.
- **`z_curl_sleep` is the intersection checker's adversarial case** — a curl is made of
  near-contacts, and a global ε loose enough to pass it passes garbage. The checker now
  takes tolerance from the score: declared closeness is intent, undeclared closeness is a
  defect. Nobody had noticed the proving set and the instrument fighting each other.
- **The primitive table and the worked examples had drifted** (`osc`, `ease.settle`,
  `gait` appear in examples, not in the table). Marked provisional-until-P4 instead of
  pretending the surface is final before four moves have pushed on it.
- **P6 hid an engine rung.** The engine has one hero-grade animation path; a scene needs
  two bodies at that grade. Promoting a slot (or instancing the hero path) is E3, named
  first in P6, so the phase is costed as engine work plus scripting rather than scripting
  alone.

The pattern across all six: each is a place where two parts of the plan were individually
right and jointly wrong. That is what a cold read is for, and it is also the argument for
stopping here — the seams a ninth pass would find are cheaper to find in P0's code than in
prose.
