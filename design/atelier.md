# THE ATELIER — REVA's wardrobe, and the club's tailor

> jacob, 09-29, with the street sheet: *"ok, now make this version of reva. id like the clothing to be modular, reva is
> also a seamstress, she loves making new clothes. thats something i want to implement but I dont know how... some way
> for reva to actually be making and designing clothes stich by stich that work natively in these game and animnation
> engines. i want cloth physics, and boob jiggle physic... please?"* — and, of the CLOTHING FABRICATION MODE sheet (a
> holographic dress form, a pattern on the cutting table, fabric layers, embellishments, a live fit preview, a mood
> wardrobe, *"DEPLOY OUTFIT? [Y] Yes [N] Absolutely."*): *"thats what this pic is about, reva making different outfits"*.

The canon agrees (`reva_canon_archive.md`, Character Bible v0.3): her first avatar was self-generated, she stays close
to it because she picked it, and her fashion *"looks chosen, not assigned"*. The Atelier is how she chooses it.

## What a garment IS

A garment is a **design** — data, not a mesh:

| part of a design | what it is | where it lives today |
|---|---|---|
| panels | shapes cut on the **dress form** (the body's own parametric surfaces: torso θ/z, legs φ/z, arms angle/t) — `g(u,v) < 0` inside | `reva_wardrobe.py`: `g_bra`, `TANK_TOP`, the shorts' waist and hem |
| ease | how far the fabric stands off the skin, everywhere (a bra 4 mm, a tank 3.4 mm, cargo shorts up to 2 cm at the hem) | the `lift(u, v)` of each panel |
| drape | how it hangs off a bulge (the tank's front falls straight from her bust) | the `hang(p, n, u, v)` of a panel |
| trims | straps, belts, buckles, rings, studs, pockets, fringe | `Form.strap`, `box`, `torus`, `fray`, `cargo_pocket` |
| fabric | a material (cotton, latex, leather, metal, glow…) and a colour | `M_*` + a swatch |
| prints | text in any font (the kanji too), flame-skulls, stripes — painted in the garment's OWN coordinates | the atlas's boxes: `paint_tank`, `paint_socks`, … |
| free panels | what hangs and swings (a jacket's body, a bandana) | CLOTH CHAINS: bones `Cloth_*` |
| slot | what it replaces when put on (top, bottom, outer, legs, shoes, hands, neck, ears, hair, …) | `Piece.slot` |
| outfits | which looks it belongs to (`base`, `street`, …) | `Piece.outfits` |

The **tailor** (`skate_src/reva902/atelier.py`, any character's) sews a design onto a body: each panel is cut on the
form, its outline snapped onto the curve (Newton, in metres along the surface), lifted by its ease, given a LIP where it
ends (a wall down to the skin — no pose shows a gap under an edge), weighted with the skin's own bone weights (so a
garment over her bust rides her breast bones). Every piece is its own mesh on her one skeleton.

## Modular, natively, in both engines

A built body's meshes carry the wardrobe in their names:

```
wear~<slot>~<piece>~<outfit>+<outfit>   a garment or a hairstyle, and the outfits it is in
skin~h=a,b                              skin that pieces a and b cover: hidden while either is worn
skin / head                             always drawn
```

Which skin a garment covers is MEASURED (`atelier.hide_sets`): a ray from every skin vertex along its normal; a skin
face is hidden by a garment only when all its corners are under it. So a worn garment can never have skin poke through
it, and taking it off never leaves a hole. REVA's skin splits into 16 such groups (her calves are `skin~h=socks,
stockings_latex`, her chest `skin~h=bra_latex,sportsbra,tank`, …).

- **Club engine** (`crowd_kms.c`, THE WARDROBE): reads the tags at load; `REVA_OUTFIT=<name>` or the first outfit;
  ctl `outfit=` / `poutfit=` dresses the hero / partner, `wear=+piece,-piece` / `pwear=` one piece at a time. One outfit
  per pack (v1). A `wear~` mesh is clothing — never measured as her body (`meshIsGarment`).
- **Godot** (`godot/characters/club_wardrobe.gd`): `ClubWardrobe.attach(reva)`, `wear_outfit("street")`,
  `put_on("jacket")` (it takes off whatever filled that slot), `take_off(...)`, `outfits()`, `pieces()`.

## Physics: cloth and soft mass, the same in both engines

- **Cloth chains.** A garment's free panel is rigged with bone chains named `Cloth_*` (her jacket's body on five, her
  bandana on three), weighted down the panel. The club's chain solver (H1, the hair solver: verlet, a spring toward the
  animated pose, world-space inertia and gravity) mines them like a ponytail — the family `cloth` is new — and Godot's
  `ClubHair.attach()` puts them on `SpringBoneSimulator3D` with cloth settings (looser, heavier than hair). The chain
  budget went 8 → 32 per body (a silent cap; loud now).
- **Soft mass.** Bones named `breast` (+ a `_tip` child) are the engine's JIGGLE (`jiggleTick`: a damped spring, 3.2 Hz,
  overshoots and settles like flesh); set actors jiggle now too, not only the hero and the cast. In Godot, ClubHair
  springs them stiff, with almost no gravity.
- **Per-vertex cloth** (the engine's §16 particle solver: bending, self-collision, body-mesh collision) stays PARKED; the
  chains are what runs natively in both engines today.

## The road to Fabrication Mode (the sheet)

1. ✅ **Designs as data, a tailor, modular pieces, hide sets, cloth chains, jiggle** — REVA's `base` and `street` outfits.
2. **Flat patterns, sewn.** A second backend for loose garments: panels cut FLAT (the cutting table), placed round the
   form, and SEWN by a cloth simulation — seam springs pulling edge to edge, stitch by stitch — then settled onto her
   under gravity. The result is a piece like any other (skinned, tagged, chained). The engine's parked particle solver
   (bending, self-collision, body collision) plus seam constraints is the in-engine version of the same step.
   ✅ (09-29) **Built**: `skate_src/atelier/sewing.py` (the sewing machine) + `drafting.py` (patterns drafted off a
   body) + `character.py`; practised on the suit man, then REVA's `sleepy` (sweatshirt + sleep shorts) and `coat`.

   **What real patterns taught it** (jacob: *"have you checked out any sewing patterns?"* — FreeSewing's source, MIT,
   `github.com/freesewing/freesewing` `designs/*/src`: the Brian block, the Sven sweatshirt, the Teagan tee, the Titan
   trousers, the Breanna women's block):
   - **Every girth is a tape's.** A tape across a cleavage does not go into it, and neither does cloth with ease:
     `drafting.Bridged` replaces each horizontal ring of the body with its convex hull. ⛔ Laid into the hollow with a
     3.5 cm standoff, the sweatshirt's centre-front column was cut 3-5 cm long (a tongue that sewed into a slit at the
     neck) and the spine's groove put a ridge down the back.
   - **Knits are finished with BANDS cut SHORTER than the opening** (`drafting.band`): Sven's hem band is the hips
     ×1.08 ×0.85, each cuff the wrist ×1.2 ×0.85; a neckband 85-90% of the neckline. The opening gathers into it.
     The sweatshirt's body tapers to its band (`hem_ease`), its sleeves and body end where their bands begin.
   - **The neckline is FITTED to the neck** (Brian: the front's depth moved until the opening is the neck + its
     collar ease; Sven 10%) — `top(neck_fit=)`, measured at the garment's own standoff (against the bare neck it asked
     for the impossible and settled on its bound: now it says so).
   - **Pull-on shorts have an elastic waistband** (Titan: 3% of waist-to-floor) — `trousers(waistband=)`, the band's
     free edge pinned (`grip`): a PBD ring on the slope of her hips crept 5-8 cm down.
   - **Cloth buckles; it does not push back.** A fabric's `compress` (how hard it resists shortening) is separate from
     its stretch, as in Vellum and Marvelous Designer — without it nothing can be gathered.

   **Bugs the patterns exposed in the machine**: each panel's `fabric` was ignored (only `sew(fabric=)` was read — the
   coat's wool and the tie's silk were all the default jersey); a seam chained across panels counted the jump from one
   panel's pattern to the next as seam (the neckband gave 12 of its 40 cm to the two neck points; every sleeve cap put
   ~2 cm on the shoulder point); a stiffness in PBD compounds over the iterations (0.2 applied 44 times is 0.99994 —
   `stiff`/`compress` are now per sew, Müller et al. 2007 §3.3); and the flattening's columns were shares of the
   width, so the column that reached the neck point had climbed the side seam and the armhole (the neckline was cut
   36% long) — they are fixed pattern x now.

   **The fit model** (jacob: *"why dont you use the characters as the models for fitting the clothes?"*): the cloth
   always met her body, but bare — sewn on skin and shown as a grey mannequin. A garment is now sewn OVER what she
   wears under it (`fab_reva.worn_under`), and every fitting look is her: head, hair and the rest of that outfit.
3. **The dress form on screen.** A holographic mannequin (the holo render mode — the canon's glitch, scanlines,
   magenta/purple glow) showing a design's panels going on and its seams closing stitch by stitch — the sheet's LIVE FIT
   PREVIEW, with FABRIC STRESS read off the sewing sim's stretch.
4. **Designing.** A design is a small file (panels as curves on the form, ease, fabric, trims, prints, chains) — so REVA
   (or the player) can make one: pick a panel, draw its edge, choose a fabric and a print, sew, deploy. The mood
   wardrobe (bug-fix, lo-fi, stream, overheat, night, combat, kombat, maniac, sleepy) is designs chosen by mood.

## Where things are

`skate_src/reva902/atelier.py` (the tailor) · `reva_body.py` (her body, face, hair, breast bones, rig) ·
`reva_wardrobe.py` (her designs) · `build_reva.py` (builds everything, splits the skin, bakes one painted, shadowed
texture, rigs, exports, renders each outfit). Sheets: `D:/reva902/reva_sheet/`.
