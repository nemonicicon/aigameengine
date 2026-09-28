# THE HOMECOMING — pulling the body-side features into the house

**STATUS (2026-08-27, one sitting, jacob: "follow it all the way through without
stopping"): ALL SEVEN RUNGS LANDED.** H1 chains (24 chains/6 packs, tip lag
telemetered) · H2 DQS (twist-gate localized the fix at the joint) · H3 QEM decimation
(24k→8k in 1.5s, 0.3% pixel diff) · H4 visibility-gated binding (56k refusals, body
stands) · H5 measured capsules (zero hand-tuned proxies in z_hug) · H6 rpack v3
morphs (the chest breathes) · H7 the GLB-native artery (`bake_statue.sh`: statue to
standing authored idle, zero Blender invocations). Per-rung details in the campaign
log; verdicts owed to jacob's eyes as always.

*(written 2026-08-27, from jacob's directive: "if you can pass it through blender, then
ok.. but why cant you just implement those features here where you have more control?
... write up a plan that includes all of these implementing them. i agree with leaving
fbx out of it, though think that you should have the ability to decimate since you have
the other tools, not that you need to tool specifically for that.")*

## §0 The principle

Every wound this campaign has taken happened **crossing between tools**, never inside
one: kaeli's unit chimera (artery bake), the dropped materials (FBX import), the
backwards statue (export axis mapping), the 63-char name truncations, MAX_PATH, the
exporter that re-reads disk instead of memory. The export boundary is where truth dies.
Therefore: **anything in the daily loop lives in the house.** External tools are
tolerated only for once-per-asset format problems, and each of those should be on a
path to elimination, not preservation. FBX is out of the pipeline by decision — GLB
comes straight from the source (Meshy exports it natively; UE exports it headless).
Blender remains only as a temporary courtesy for assets that arrive wrongly packaged,
and every rung below removes one of its remaining jobs.

The plan grows the three layers we already own, in their own languages:
- **the engine** (`crowd_kms.c`) — runtime features: simulation, skinning, blending
- **the baker** (`skate_src/glb2rpack_anim.py` + `rpack.py`) — asset features:
  weights, decimation, morph carriage, format v3
- **the author toolkit** (`authorlib.py` + scores) — measured proxies, gates

Laws carried in: verify on pixels from the wall; gates before eyes; percentiles not
maxima; joint centres live inside flesh; read bake logs unfiltered; the tiptoe law;
never derive what you can measure; never trust a name you didn't audit.

## The rungs, in order of value over cost

### H1 — VERLET CHAINS: tails, ponytails, ears (engine)
Secondary motion without authoring: each discovered chain (tail/hair/ear bones, mined
by the existing find_tail/find_face name families + childless-leaf walks) runs a
per-frame verlet integration — gravity, damping, parent-distance constraint, a small
stiffness toward the animated rest direction. ~100–150 lines in the engine + a ctl
(`chains=0/1`, `chainstiff=`) for the bench.
- **Yields to authorship**: a clip that animates a chain bone wins — the sim blends in
  only where the clip holds rest (the crowd's dancing tails must not be overwritten).
- **Pays**: all 133 packs, every mode, forever. Kaeli's twin tails, reva's ponytail,
  the cow, the dragon — and the purr scene's ear twitch becomes sim + one authored nudge.
- **Gate**: the wall, subject spinning (lab state RUN then idle): the ponytail LAGS the
  turn and SETTLES within ~1.5s, never explodes (max chain-link speed bounded); GIF at
  grid=13; numeric: chain tip overshoot angle measured from the pose dump.
- **Risk**: instability at low fps → clamp dt substeps; hair intersecting the body →
  phase 1 ships without collision (H5 adds capsules to push chains out).

### H2 — DUAL-QUATERNION SKINNING: the candy-wrapper cure (engine shader)
The elbow/wrist collapse is linear blend skinning, and we own the vertex shader.
DQS replaces the palette's matrix lerp with dual-quaternion blending (antipodality
handled by hemisphere-flipping against the first joint). GLES2 fits it: 2×vec4 per
bone uniform instead of a mat4 — the palette upload shrinks.
- **Pays**: every character, every pose, especially the new close-up column where
  wrists and shoulders fill the frame.
- **Gate**: a twist-pose test clip (forearm 120° twist, shoulder raise) shot on the
  close-up wall, LBS vs DQS side by side; numeric: silhouette width at the mid-forearm
  in the twist pose (LBS pinches, DQS holds); club-wide soak: no pack visually breaks
  (the nine-angle wall on 6 spot-check bodies).
- **Risk**: scale in rigs (DQS dislikes non-uniform scale — our rest scales are
  uniform, assert it at load); performance on the Pi lineage (keep an LBS fallback
  behind a define).

### H3 — DECIMATION IN THE BAKER (ability, not a bespoke tool)
Per jacob: the ability matters, not a dedicated tool. Two paths, try in order:
1. **Vendor meshoptimizer** (single-file C, MIT): a 30-line shim compiled with the
   mingw toolchain we already require; the baker calls it when `--tris N` is passed.
2. **Fallback: pure-python QEM** (quadric error metrics, ~200 lines) in the baker —
   slower (minutes on 400k tris) but once-per-asset and dependency-free.
- **Pays**: the statue-to-body recipe stops needing Blender for its heaviest job;
  any GLB source (Meshy direct) can be brought to club budget in-house.
- **Gate**: reva2 re-ingested end-to-end with NO Blender in the chain (GLB source →
  baker decimate → rpack): tri count ≤ 24k, silhouette diff vs the Blender-decimated
  body ≤ noise on the wall, UVs intact (texture reads clean on the close-up column).
- **Risk**: UV-seam preservation in QEM — meshoptimizer handles attributes; the
  python fallback must weight UV discontinuities in the quadrics or seams smear.

### H4 — AUTO-WEIGHTS IN THE BAKER (bind without Blender)
Distance-with-visibility weighting (Pinocchio-style): per vertex, candidate bones
scored by distance along bone capsule with a line-of-sight test through the mesh
(no binding across empty space — the flaw Blender's auto-weights silently commit),
top-4 normalized, smoothed one Laplacian pass.
- **Pays**: closes the last Blender job in statue-to-body; and our weights become
  GATED — the binder refuses a vertex whose best bone is occluded, instead of
  silently gluing a hand to a hip.
- **Gate**: reva2 re-bound in-house; posturecheck stand + the deformation test clip
  from H2 on the close-up wall vs the Blender-bound body; numeric: max vertex
  displacement disagreement in the test poses ≤ 0.03u except where the gate
  deliberately differed.
- **Risk**: visibility tests on 24k tris need a BVH (~80 lines) or they take hours.

### H5 — CAPSULE BODIES: measured collision for authoring and runtime
`body_dims` graduates from one torso ellipse to a **capsule per canon bone**, radii
measured from the skinned mesh (percentile distance of nearby flesh per bone). Author
side first: the hug/curl gates swap their hand-tuned cylinders for the measured
capsule set — one collision model for every future two-body score. Runtime second:
the verlet chains (H1) collide against the capsules, and a cheap capsule-overlap
check becomes available to game modes (melee reach, crowd shoving).
- **Gate**: re-run the hug and curl bakes with capsule gates — penetration numbers
  equal or better with ZERO hand-tuned constants left in the scores; a chain (ponytail)
  visibly rests ON a shoulder instead of through it.
- **Risk**: capsule radii from clothed meshes over-fatten limbs — reuse the
  percentile law and the arm-bone exclusion from body_dims.

### H6 — MORPH TARGETS: rpack v3 and faces (format + baker + engine)
The format is ours. v3 adds a morph block per mesh (sparse vertex deltas + names) and
a weights track type (3) in clips; the baker carries GLB shape keys through; the
engine blends up to 4 active morphs on the CPU into a scratch position buffer before
upload (GLES2-safe, no extra attributes needed; 24k verts × 4 morphs is cheap), or as
attributes later if profiling demands.
- **Pays**: faces — blinks, jaw, smiles — plus authored breathing that actually
  swells the chest, and the purr as a real surface tremor. Scores gain
  `morph("blink", profile)` as a primitive.
- **Gate**: a source with shape keys through the artery end-to-end; the close-up wall
  shows a blink cycle; append-only invariant holds (v2 packs load unchanged; v3
  writer proves nodes/meshes byte-identical on a no-morph pack).
- **Risk**: format creep — v3 is versioned, the loader gates on it, and the writer
  stays append-only; sparse deltas keep packs small.

### H7 — THE GLB-NATIVE ARTERY (retire the courtesy)
With H3+H4 landed, statue-to-body = GLB in → measure → decimate → landmark → bind →
rpack, one python chain, no Blender boot. The recipe keeps its landmark laws (facing
asserted from anatomy, ribs-band measures, arm-bone exclusion). FBX sources are
refused at the door with a message naming the source's own GLB export path.
- **Gate**: a NEW Meshy character (not reva2) from GLB download to standing on the
  wall with her authored idle, zero Blender invocations, one command.

## Sequencing and the session law

One rung per sitting, finished-or-parked, every gate on pixels: H1 → H2 → H3 → H4 →
H5 → H6 → H7. H1/H2 are engine sittings; H3/H4 baker sittings; H5 authorlib; H6 all
three layers (the longest); H7 is an integration day that mostly deletes code.
jacob's eyes gate every rung's wall GIF as always.

## Audit (one pass, the known traps checked against each rung)

- Heredoc/backslash law: H3's C shim and H6's format writer are Write-tool work.
- "ld returned 1 with no errors" = the exe is running — kill before link (H1/H2).
- Percentiles + arm exclusion carry into H5's capsule radii (measured, not maxima).
- H1 must not fight the crowd-clocks-out law: sim chains are idle-motion, allowed
  everywhere (they are physics, not dancing); jacob names exceptions.
- H2 keeps an LBS fallback: the Pi ancestor and any weak GPU stay bootable.
- H6's CPU morph blend must not hitch the club: blend only the HERO + partner unless
  profiling clears the crowd.
- Every gate above is falsifiable on pixels or a number — none reads "looks better."
