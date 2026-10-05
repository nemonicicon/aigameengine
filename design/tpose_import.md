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
| `skate_src/statue2tpose.py` | the whole statue chain in one command: import → strip → rig (marks) → autoweights → bake → T-pose → .rpack / .dae / .glb + sheet |

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

## What the T-pose is, exactly

Facing +Z, Y up. Upper arm and forearm along ±X (the side from the hand's own position, not its
name), the hand along the same axis, the clavicle levelled but keeping its reach; thigh and shin
along −Y; the foot aimed forward along the facing, keeping its own drop; the spine chain and neck
along +Y; the pelvis and root untouched. Solved with `authorlib.aim_bone`, parent-first, so the roll
of every bone changes as little as the shortest arc allows. Baked: a vertex goes through its old
palette into rig space and back out through the new one, so the body does not move — its rest
moves to meet it; inverse binds become inv(W'), the bind matrix identity.
