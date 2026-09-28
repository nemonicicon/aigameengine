# THE ZOMBIES ARE THE NEIGHBOURS — Z4 of SEVENTY-FOUR

*(written 2026-08-29. The film's own line: "⭐ **the zombies are the neighbours.** Same
bodies, retargeted onto the ZAMN zombie clip bank. That is the trope stated correctly, it
is what the asset situation wants, and it means one ingest serves both crowds.")*

Z4's rung was **zombie ingest + retarget onto the citizen skeleton**, ending in *Mrs.
Parsons at her mailbox*. It landed, and on the way it cashed out a four-day-old defect
that had been quietly wrecking the whole town's colour.

---

## THE INGEST

`skate_src/zombies/` — the same artery pattern as the guns and the town, run unattended:

    ue_export_zombies.py   UE 5.8 headless -> 32 GLBs   (32/32 OK, ~90s)
    merge_zombies.py       32 GLBs -> zombies.glb       (one rig, 32 named clips)
    glb2rpack_anim.py --keepnames -> packs/v1_zmoves.rpack   (32 clips, 7.4 MB)

Source: `ZAMNRemake/Content/ZombieAnimationPack/Animations/Mannequin_UE5` — **on C:, not
D:** (`C:\MyVeryOwnVideoGames\Unreal Projects\ZAMNRemake`, engine 5.6; the 5.8 shim reads
its assets fine). Attack ×4, Walk ×3, Run, Idle ×2, Belly_Crawling ×2, Laying_Idle ×2,
Impaled ×2, Taking_Damage ×6, Burst ×10.

⭐ **THEY ARRIVE IN THE CLUB'S OWN BONE LANGUAGE.** The canonical map in `crowd_kms.c:259`
is written against UE names — `pelvis`, `spine_`, `clavicle_l`, `upperarm_l`, `thigh_l`,
`calf_l` — because that is the convention the whole retarget was authored to. The UE5
mannequin these clips ride is not "a rig to be mapped"; it *is* the reference rig. Loaded:
**pbOK=16, cbOK=1** — every canonical bone resolved, basis valid, no translation table.

⭐ **NO BLENDER.** `merge_mob1.py` needed it because its source was FBX. These 32 came out
of one exporter against one skeleton and measured **identical** — 90 nodes, same names,
same order, 30fps, T/R/S on all 89 joints in every file — so the merge is JSON surgery:
copy the base's mesh/skin/nodes once, append each animation with its node indices used
verbatim. It also dodges the FBX traps (the UE scale-compensation convention that crushed
kaeli's hips to 1e-4).

**What the merge keeps, decided by measurement not habit:**

| | | why |
|---|---|---|
| rotation, all joints | **keep** | this is what the transfer replays |
| pelvis translation | **keep** | THE HEIGHT SIGNAL — pelvis y is 0.84 standing, 0.82–0.92 walking, **0.11 on the crawls and lays** |
| root translation | **zero** | the actor system drives travel with `spd`; Walk/Run/Idle are already in place (root moves 0.03/cycle) but the four attacks lunge (Attack_A: z −0.15→1.91) |
| scale | **drop** | the documented kaeli trap |
| translation, other joints | **drop** | bones do not stretch; rest translations already live in the hierarchy |

⛔ **CLIP NAMES ARE AN ENGINE CONTRACT.** Everything is `z_`-prefixed so it dodges every
matcher: `dnElect()` elects on EXACT names (`w_walk`, `v_walk*`, `w_run`, `idle`,
`x_fidget*`, `x_convo*`, `x_gunfire`, `x_down`, `x_flee*`) and the pack-local walk finder
at line 595 takes **any name containing "alk"**. A zombie shamble must never be elected as
the crowd's walk — the neighbours have somewhere to be.

---

## THE ENGINE RUNG: a clip is addressable across packs

Until now a clip name resolved **only inside the actor's own pack**, which is why a
900-vert town citizen could do nothing but the walk and idle it shipped with. `bankFind()`
closes that: name a clip that lives in any loaded bank and the actor plays it through the
canonical transfer.

    actor v1_Character_29_Female  z_idle_a  30.2 0 21.2  200  1.0  0.85
    actor v1_Character_04         zombie    ...          # a casting note, not a clip

⭐ `zombie` picks a variant off the bank **by actor index** — a moving one shambles
(`z_walk_a/b/c`), a standing one sways (`z_idle_a/b`) — because forty zombies playing one
loop reads as forty copies of one zombie.

**Proof it is a real retarget:** the smoke test runs on `v1_Character_01`, a pack with
**30 dance clips and no walk, no idle, nothing like a zombie**. It shambles.

`actorDraw` also stopped stair-stepping its own clips on the way past — it was written
after the P1.5 interpolating sampler landed and had gone back to the nearest-frame memcpy.

### ⭐ THE HIP DROP — and the limit that made it necessary

`donorPoseF` is **rotation-only**. Right for a body on its feet; wrong for every clip that
puts one on the ground. The first `zombiecheck` sheet showed it exactly: `z_lay_a`,
`z_imp_a`, `z_burst_a` and the two ground attacks all arrived correctly *oriented* and
**suspended flat in mid-air at standing hip height**, and the belly crawls stood up.

`donorHipDrop()` reads the donor's pelvis translation at the current phase, takes its
offset below the donor's own rest, and normalises by the two rigs' **rest hip heights** —
measured by walking the rest hierarchy, so nothing is tuned:

    zombie bank pelvis rest world Y   0.959   (metres)
    v1_Character_01 pelvis rest world Y   269.736   (a different unit system entirely)

The ratio of those two is the conversion, and it costs nothing on an upright clip (the
function returns 0 when the pelvis is at or above its rest).

### `include` in the set format

A fifteen-minute film shot on one street needs many sets over the *same* 514 lines of
suburbia. `include block` pulls a set in wholesale, depth-limited to 2. `parsons.set` is
a street plus five cast lines.

---

## ⛔ AND THE THING THIS RUNG ACTUALLY FOUND: THE TOWN WAS FLIPPED

Mrs. Parsons stood at a **magenta** mailbox beside a **purple-and-orange** pickup, in
front of houses whose windows were dark chequerboards of colour swatches.

**The V flip was backwards, and had been since the town was baked.** `bake_town.sh`
transposed every sheet vertically on the reasoning that glTF's UV origin is top-left and
GL's is bottom-left. This engine uploads the image top-row-first and samples it the glTF
way, so the flip put every palette-UV'd mesh on the wrong swatch.

**Proven on pixels, one at a time**, the same mailbox baked both ways: flipped it is
magenta; unflipped it is a navy USPS box with white bolts and a white envelope decal.

⛔ **The measurement that originally argued for the flip was real and the inference was
wrong.** "Mean luminance 8.6 as stored, 124.8 flipped" was measured with a probe whose own
sampling convention already differed from the engine's — so it agreed with itself. The
thing that actually rescued the town in that same change was the **alpha flatten**; the
flip rode along as cargo. *Circular verification, again, and this time it survived four
days because the sheets that would have exposed it (brick, siding, road) are tiling and
symmetric — only the palette ATLAS, where position means colour, could show it.*

Three more findings from the same pass:

- ⭐ **`ue_texmap.py`** asks UE what texture each mesh *actually* uses, via the asset
  registry's dependency graph — 487 meshes, 481 with a texture, 16 sheets, **7 of which
  the old export never knew about**. ⛔ `StaticMesh.materials` is PROTECTED in Python and
  the first version of this script reported 487 meshes with zero textures *while exiting
  0* — a green run that knew nothing.
- ⛔ **`T_colorPalette_METAL` is a MASK, not a palette** — sampled under the mailbox's own
  UVs it is pure black and pure white and nothing else. Never bake it as a base colour.
- ⛔ **A mesh whose UVs span 0..1 is not a palette prop.** Measured: `SM_doorHouse_01`
  sits at u 0.155..0.790 (one swatch); `SM_wall_01` and every `SM_window_*` run the full
  0..1 — they TILE a dedicated sheet. Windows had been getting the palette, which is the
  chequered rectangle that was on every house.
- ⛔ **`bush_01` is 10.74 × 8.48 × 11.22** — taller than a 5.25 wall module and 2.7× a
  person. Placed at scale 1.0, every front garden grew a shrub the size of the house and
  ate the shot it was dressing. Now 0.24–0.33, against the mailbox's measured 2.49.

---

## THE INSTRUMENT

`reva_native/anim_bench/zombiecheck.py` — **rows are clips, columns are time**, the
`ridecheck.py` shape, on a citizen that has never met the donor rig. N actors stand in a
line each playing a different clip, so one launch yields N rows.

⛔ **It lied twice before it told the truth**, and both times it reported clips as broken
that a full frame showed were perfect: once by assuming the actors filled the frame's
width, once by squashing a 213×648 strip into a square cell. It now projects each actor's
feet and head through the same look-at the engine used and crops what that says. **When a
sheet and a full frame disagree, the full frame wins** — an instrument that lies is worse
than none.

    python reva_native/anim_bench/zombiecheck.py [--body v1_Character_01] [--clips a,b,c]
    bash skate_src/zombies/shoot_set.sh <set> "<ex,ey,ez,ax,ay,az,fov>"   # one set, 3 frames, stdout kept

---

## WHERE Z4 STANDS

**Done:** the ingest, the bank, cross-pack clip resolution, the hip drop, `include`, and
the Parsons beat rendered. The whole town's colour is fixed as a side effect.
Regression **15/15, all four strict shots bit-identical**.

**Owed:**
- jacob's eyes on all of it.
- The distant citizens in a street wide read as dark silhouettes — unexplained.
- The road does not extend past the generated lots, so a low camera at the street's end
  sees the void under it.
- Nothing yet turns a neighbour INTO a zombie at a moment in time — Z4 gives the film a
  cast that is already turned. Act III needs the transition (a `z_hit_*` into a `z_burst_*`
  into a shamble), which is a behaviour question, not an asset one.
