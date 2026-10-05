# Importing .brres and .dae, and putting a body into a T-pose (2026-10-05)

jacob: ".brres and .dae" — then "what we are trying to do here is to get t posed meshes" — then
"we are trying to take the higher quality brawl trophies and apply them to the ds models skeletons.
a t pose is needed as proof." Two Metroid Prime Hunters hunters (Noxus, Kanden) as Super Smash Bros.
Brawl trophies (.brres, high-poly statues) and the same hunters as DS exports (.dae).

## What was built

| tool | does |
|---|---|
| `skate_src/rpackwrite.py` | the ONE pack writer for every importer written after this date (scene shape: nodes, meshes pre-baked to rig space, clips, PNG textures); `scale_scene`, `rotate_scene`, `fit_height`, `bounds_report` |
| `skate_src/brres.py` | the BRRES reader: index groups, MDL0 (bones, node mixes, vertex / normal / colour / uv arrays, materials + samplers, objects = GX display lists), TEX0 (I4 I8 IA4 IA8 RGB565 RGB5A3 RGBA8 C4 C8 C14X2 CMPR) + PLT0, CHR0 (keyed 32/48/96-bit Hermite and baked 8/16/32-bit tracks); Yaz0 + U8 containers (.szs / .arc). `info`, `tex`, `scene` |
| `skate_src/brres2rpack.py`, `brres2dae.py` | a Wii model → pack / COLLADA, T-posed where it has limbs (`--anim` pulls CHR0s from a second archive, `--height`, `--yaw`, `--glb`) |
| `skate_src/daelib.py` | COLLADA in (1.4 / 1.5, any up axis and unit, polylist / polygons / strips, skins, animations on sids or whole transforms, clips) and out (joints, skins, PNGs beside, baked-matrix animations) |
| `skate_src/dae2rpack.py` | a COLLADA body → pack, T-posed (`--up`, `--unit` overrule a lying asset block) |
| `skate_src/glbwrite.py` | the scene shape → binary glTF (the statue path reads this; Blender / Godot read it too) |
| `skate_src/tpose.py` | `canon_map` (the club's canon + a Nintendo / generic bone-name matcher + `--map`), `solve_tpose` (aim each limb along its T-pose axis with the authoring kit's shortest-arc primitives, parent-first, the side read from where the hand IS), `bake_tpose` (skin re-baked in the pose, inverse binds rebuilt, hold tracks for clips that drove the old rest), `measure` (the angle table), `cloud_sheet` (front / side / top with the skeleton). Also a tool on a finished pack, with `--dae` / `--glb` |
| `skate_src/statue_marks.py` | gridded per-part sheets, a slab census, a marks overlay: the instrument for rigging a statue by MEASURED joints |
| `skate_src/statue2body.py --marks file --facing +z` | the landmark rigger takes measured joints and an operator's facing assertion |
| `skate_src/statue2tpose.py` | the whole statue chain in one command: import → strip → rig (marks) → autoweights → bake → T-pose → .rpack / .dae / .glb + sheet; `--hybrid` (round 2) |
| `skate_src/tpose_receipt.py` | the numbers behind a proof sheet: each limb's mesh against its own bone line, left vs right, and the welded pieces against the bones — PASS / FAIL (round 2) |

## The receipts (each one a test that could have failed)

- **The BRRES polygon decode is exact.** RiiStudio's public `human_walk.brres` decodes to 4876
  triangles and 8042 vertex corners — the counts its MDL0 header stores. Noxus to 35993, its header's
  number. Every vertex array's decoded bounds equal the bounds stored in its own header.
- **Two independent encodings agree.** Noxus and Kanden exist as .brres (my display-list reader) and
  as BrawlCrate's .dae of the same model (my COLLADA reader). Nearest-neighbour between the two
  clouds: worst 0.0000 on every one of Kanden's three parts and Noxus's six (mean 0.00000). A bug in
  either reader would have shown here; that it is zero on the vertex count AND the positions is what
  lets the rest of this page stand.
- **The COLLADA writer round-trips.** A shipped body (X3D) written to .dae and read back: bind cloud
  within 8e-6 rig units per vertex, an animated frame's joints within 1e-3.
- **The T-pose bake changes nothing it should not.** X3D (an A-pose rest, arms 45°) re-rested into a
  T-pose and rendered in the lab playing z_idle, A/B against the original pack: the frames differ by
  edge pixels only (the engine's auto-scale moved from 2.2448 to 2.2449 because the rest bounds
  changed shape). The angle table reads 0.0 on every limb after the solve.
- **CHR0 reads.** The Mario Kart driver's 29 animations read with their frame counts and bone counts
  (`wait` 90 frames, 20 bones, loop); the human's walk (71 frames @ 60) resamples to 37 @ 30 and
  drives all 53 bones.

## Lessons (paid for on these two bodies)

- ⛔ **A trophy is a statue.** Both Brawl trophies hang every object on a `TopN / rotN / pos00 /
  transN / <Name>N` dummy chain with no limb bone. `canon_map` resolves nothing, and the importers say
  so and write the body AS IS instead of pretending. The T-pose needs the statue path.
- ⛔ **The landmark laws are for upright humans.** `statue2body`'s arm band is the widest slab between
  60–90 % of height; a hunter mid-stance with curled blade-arms put the band on his chest, and the
  T-pose straightened his legs under curled arms. The fix is not a cleverer law: it is MEASURING. The
  slab census and the gridded sheets give the joints; `--marks` places them. Noxus and Kanden each
  took 27 measured joints and came out T-posed, on the sheet and in the lab.
- ⛔ **The chest test reads a hunch backwards.** `facing: chest −0.023 foot +0.280` on Noxus — his
  toes point +Z, his back crest fools the chest slab. `--facing +z` is an OPERATOR assertion, printed
  loudly; a statue that really faces −Z is turned first (`--yaw 180`, Kanden), never rigged backwards.
- ⛔ **Hold tracks replay the old pose.** `bake_tpose` adds a rotation track holding the OLD rest to
  every clip that lacks one, so an imported animation plays as authored. The statue rigger's two-frame
  `tpose` stub is a clip too, so the lab played the crouch and the receipt looked like a failed bake.
  The stub is now rewritten as a hold of the NEW rest. The receipt for a T-pose is the lab with that
  clip, or the lab with no clip at all (the rest pose).
- ⛔ **The lab is no proof sheet.** jacob, on the first lab frames: "doesent look like a t pose. and
  its dark." Night lighting, perspective cameras, 640-pixel tiles. `proofrender.py` is the proof:
  front, BACK and side, orthographic, lit, the body's own texture lifted, a ground line and a scale
  bar. The back view exists because the next thing he said was "thats the back of the model not the
  front": on Kanden BOTH slab tests (chest −0.056, foot −0.200) pointed the wrong way, and my 180°
  turn put his back to the lens. The operator decides facing from the lit render; the heuristics are
  reported, never trusted, on a non-human body.
- ⛔ **Smooth weights smear a body of plates.** With distance weights Noxus's hood took a share of
  each arm bone and the T-pose stretched it into a cape between the claws. `autoweights.py
  --rigid-pieces` (driver `--rigid`): every connected piece of mesh goes whole to the bone its
  vertices favour, weight 1 — what the Wii's own single-bind objects do. Gaps at joints, never smears.
- ⛔ **Measure the pieces, not the cloud.** The marks that finally bound Noxus came from the
  connected pieces of his parts (union-find over the triangles, centre, box, long axis): the arms
  object splits into a right upper arm pulled BACK behind the torso and a left arm crossing in front
  of the chest, which no sheet of dots had shown. The nearest-segment support census (which bone each
  vertex would take by distance) is the check: a bone with 0 vertices is a joint placed in air.
- **Digitigrade legs.** Noxus's ankle is raised and his foot bone is as long as his shin. A foot bone
  longer than half the shin is aimed straight down with the rest of the leg (he stands tall on his
  toes) and the toe bone carries the forward direction; a human foot keeps its own drop.
- **Symmetric limbs.** Hand-measured joints put the two sides centimetres apart and the T-pose read
  as a stance; `--symmetric` sets each limb bone's length to the mean of the two sides (clavicle,
  upper arm, forearm, hand, thigh, shin, metatarsus) before the bake.
- ⛔ **A terminated xvfb-run leaves the engine running.** Three `crowd_linux` and three `Xvfb` from
  finished labs were alive at once, and a queue waiting for "no engine" waited for ever.
  `linux_lab.py` now starts the engine in its own session and kills the group.
- ⛔ **A DS export that lies about its axis.** `Kanden_lod0_pal_01.dae` says `Y_UP` over Z-up data;
  `--up z`. And ⛔ **none of the DS exports carries a skeleton** — nodes with geometry, no joints, no
  controllers, no animations. The DS skeletons the trophies are meant to wear have to come from an
  export that keeps them, or from the raw DS files and a reader for them (next build).

## Round 2: "Kanden is still not in a T-pose" (10-05)

jacob, on the first Kanden proof sheet. The solver's table read 0.0° off axis on every limb and the
BONES were a perfect T — the arm MESH hung off them: the forearm angled down from the elbow, the fist
curled up, a lobe of upper arm under the shoulder. Every cause was measured before it was changed
(the instruments are in this repo; Kanden's marks instrument lives with his deliverable):

- ⛔ **An angle table measures what the solver placed.** `tpose_receipt.py` measures where the MESH
  went: per limb bone, the centroid of the vertices it dominates against the bone's own line, the
  left/right mirror of those centroids, and (binding-independent) the welded pieces of each limb
  against its bone line. The shipped Kanden failed 11 rows (perp up to 0.158, mirror dz up to 0.156);
  round 2 passes all of them (worst perp 0.040, worst mirror 0.034).
- ⛔ **Marks in the air.** The first marks put the elbows 9 cm OUTSIDE the arms (LeftForeArm beside
  the upper arm) and the shoulders on top of the pads; mirroring the marks into the file's frame
  (x,z → −x,−z) also put the TOES BEHIND THE HEELS — the feet were aimed backwards. Distance binding
  then gave the real arm to whatever bone was near. Round 2 MEASURES every joint from the pieces:
  connected components of the welded mesh (16 on Kanden: torso+head, two arm tubes, two fists, two
  thighs, shins, feet, five cannon rings); a joint is where two pieces meet (the contact ring of
  thigh and shin is the knee) or the corner of two tube axes (the elbow).
- ⛔ **A tube's axis is not its PCA.** Kanden's forearm is a short, wide bracer: its PCA axis swung
  15° between iterations and the elbow→wrist line ran 25° off the bracer — straightened, the
  forearm crossed its bone like a diagonal (seen from above, +0.06 in front at the elbow, −0.04
  behind at the wrist). The axis from the SURFACE NORMALS (the direction they are all perpendicular
  to) is stable to a few degrees; the elbow goes to the corner of the upper-arm and forearm axes.
- **Mirror copies share joints.** Thigh, shin and foot are exact mirror copies (ICP with a
  reflection: 3 mm mean), the upper-arm and forearm parts too: each side's joints are mapped into
  the other's piece and averaged. The torso is bilaterally symmetric about its OWN plane (mirror +
  ICP: turned 15.2°, tilted 2.6°): spine joints on it, hips and shoulders symmetric through it.
- ⛔ **A leaf bone had no direction.** The 22 joints carry no tails: autoweights gave each hand a
  2 cm stub pointing world-UP (the fist split between forearm and hand by a coin toss) and the solver
  never aimed a hand. `statue2body` now writes the tails as plain END NODES (HeadEnd, *HandEnd,
  *ToeEnd: not skin joints, the skin stays 22); autoweights uses one as its bone's tail, tpose aims
  the hand at it.
- ⛔ **The index graph splits plates at UV seams.** 414 "pieces" on Kanden and 690 on Noxus where
  the bodies have 16 and 19: `--rigid-pieces` rode fragments of ONE plate on different bones, and
  plain smooth weights (visibility gate and smoothing over the index graph) gave seam copies
  different weights — the shipped Kanden had 257 seams torn open by > 1 cm, Noxus 681.
  `--hybrid-pieces` (driver `--hybrid`) welds coincident vertices: a piece that spans one bone rides
  it rigidly (fist, ring, thigh, shin); a piece that spans a joint — owns ≥ 10 % and covers ≥ half
  of two or more bones (Kanden's arm tube, his torso along the spine) — is smooth over exactly
  those bones (inverse 4th power, two Laplacian passes inside the piece). Round 2: 0 torn seams.
- **A turned statue is squared first.** The pelvis turns so the hip line lies along x (only a turn
  < 45°; a body facing the other way is still the importers' `--yaw`). Kanden: 15.3°.
- **A bent limb straightens with its roll decided** (`aim_pole`): elbow points back, kneecaps
  forward, the same on both sides. Shortest arcs left his two arms rolled 45° apart.
- ⛔ **The foot kept the wrong pitch.** The old aim took the foot's drop AFTER the shin was
  straightened and dropped the sideways part of a splayed foot: X3D's feet came out 9° toes-up.
  The foot now keeps its statue orientation in the world (sole flat) and turns about y to face
  forward (Kanden's right foot: 54°).
- ⛔ **Straight legs are longer.** The T-pose put Kanden's soles 0.24 under his floor: the tool
  lifts the pelvis so the lowest vertex stands where the statue's stood.
- **A head turned on its body** stays turned when the body is squared (Kanden's faced +z on a torso
  facing −15°: squared, 18° to his left). Optional mark `HeadFront` (a point in front of the face,
  measured from the head cone's elongated slices) → a plain node; the solver turns the head to face
  forward. Its HeadEnd sits at the CROWN, so the back of the head binds to the Head bone rather
  than blending with the spine (the straight spine runs along the front of his head).

`statue2tpose.py Kanden.brres kanden --height 1.9 --marks kanden_marks.txt --facing +z --hybrid`

## What the T-pose is, exactly

Facing +Z, Y up. The pelvis squared to the hip line, then the spine chain and neck along +Y and the
face (if a HeadFront says where it looks) forward. Upper arm and forearm along ±X (the side from
the hand's own position, not its name) with the elbow point back, the hand along the same axis (at
its End node), the clavicle levelled but keeping its reach; thigh and shin along −Y with the
kneecap forward; the foot keeping its own statue pitch, turned to face forward; the pelvis lifted so
the lowest vertex stands where it stood. Solved with `authorlib.aim_bone` (and `aim_pole` where a
joint is bent), parent-first. Baked: a vertex goes through its old palette into rig space and back
out through the new one, so the body does not move — its rest moves to meet it; inverse binds
become inv(W'), the bind matrix identity.
