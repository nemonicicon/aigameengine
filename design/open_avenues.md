# Open avenues

jacob: *"we have accumulated alot of avenues here for things we need to do, could you check
through our conversation and list all of these?"*

Everything raised and not finished, as of 2026-08-30. ⭐ = his words, ⛔ = a known trap or a
limit I have already measured. Ordered by area, not priority — priority is his call.

---

## A. THE STORY

1. **⭐ THE COUNT AND THE TITLE DO NOT ADD UP.** *Confirmed by jacob: "the count and the
   title need to be changed."* The film is SEVENTY-FOUR. The cold open (4am Sat)
   says **411**. The treatment's Act I (7:15am Sat) says *"there are 406 on this street...
   there were 411 on Thursday"* — so 411 is **Thursday's** number and the opening line is
   two days stale. Act III then says *"406 on Thursday, 401 now. Ten people"* — which moves
   406 back to Thursday **and** miscounts (406−401 = 5). Act V says 391 of 406. Four
   different numbers, one of them the title, and the arithmetic is wrong in two places.
   **Decide the canonical ledger first, then fix every line that quotes it.**
2. **The title number is never spoken.** Whatever the ledger becomes, "four hundred and six"
   should probably land in the film at least once.

## B. THE FIRST SCENE

3. **⭐ THE NEW OPENING IMAGE.** *"a still room, reva just chilling, thinking, as she does,
   kicked back one leg off the bed the other on it stretched out. kaeli under the covers on
   her side turned away from reva."* Plants "have you been sitting there the whole time"
   before the line asks it. Needs: a reclining/seated pose for reva against the bed, kaeli
   under the covers **facing away**, and a held beat before the nightmare.
4. **The covers have to cover her.** We have simulated bedding now, but it is *made* — a
   sheet lying flat on a mattress. A body **under** it is a different problem: the sheet
   must collide with a lying body, not a box.
5. **They are dressed for a nightclub at 4am.** `v1_kaeli_pj` exists and works; the shot
   list still names `kmoves`. One line in `skate_src/film/shoot.py`.
6. **Reva has no night clothes at all.** `makepajamas.py` is general — it measures whatever
   body it is given.

## C. THE WORLD

7. **⭐ YOU CAN SEE THE END OF THE ENVIRONMENT.** The ground is 28×28 tiles with one tile of
   run-off either end (`street.py`), so a wide shot runs out of world. Needs either a much
   larger ground plane, a horizon/backdrop, or fog that hides the seam. Related to (9).
8. **⭐ PEOPLE CLIP IN AND OUT OF EXISTENCE.** ⛔ Not the set renderer — that is explicitly
   "no culling, no batching, no LOD". Suspects, in order: the crowd's per-slot frustum cull
   (`FPS LEVERS`, tuned on the CRT), the sky flyers, and actor spawn/despawn. **Diagnose
   before fixing** — three different systems, three different fixes.
9. **⭐ A REAL SKYBOX.** *"we need a real skybox."* `skyColor(dir)` is written and shared,
   driven by the sun rig `tod=` already animates — but **nothing draws it**. The water
   reflects a sky the audience cannot see. This also fixes (7) at the horizon.
10. **Refraction through water.** Currently reflection + Fresnel only; you cannot see the
    bottom of the pool.

## D. SIMULATION

11. **⛔ THE GARMENT SOLVER NEVER GOT THE SUBSTEPPING.** Only the set cloth did. `clothStep`
    still runs `for(pass=0;pass<3;pass++)` — the shape *Small Steps* says is wrong. Same
    fix, already proven on curtains (3.463% → 2.559%).
12. **Armholes.** ⛔ A tube has no armholes, so across the shoulders the pyjama top bridges
    straight over and reads as bulk. Topology, not a number — the shell needs holes cut for
    arms and neck.
13. **⭐ TORN CLOTHING.** *"torn clothing exposing wounds underneath"* — not started. XPBD's
    λ **is** the constraint force, so a tear threshold is now cheap: break edges whose λ
    exceeds a limit.
14. **⭐ DRESSES THAT SHOW EVERY CURVE.** Same generator, tighter compliance and a skirt
    topology (open at the bottom, no leg split).
15. **⛔ Cloth collides with SPHERES AND BOXES, not geometry.** Will not drape over a chair
    arm or catch on a fence. A real limit, written down rather than discovered later.
16. **⛔ Water is a HEIGHT FIELD.** Cannot splash above itself, break into droplets, or
    pour. Fine for a pool; wrong for anything else.
17. **Stam's fluid solver is on the shelf, deliberately unapplied** — it is a *velocity*
    field, right for smoke/dust/fog, which the zombie material will plausibly want.

## E. CHARACTERS

18. **Reva's mouth in profile is still wrong.** Raised well before this session's work and
    never resolved. My own staging notes say how to frame it: lens on ±x at her z.
19. **⛔ reva2's DQS shoulder balloon** — confirmed, unfixed, `dqs=0` is the stopgap. Her
    weights are 4× asymmetric (2560 left-arm verts against 633 right).
20. **⛔ Kaeli's clips are rotation-only** — hips pinned, no bounce.

## F. INSTRUMENTS

21. **THE DRAW-PATH REGRESSION.** The gap `packcheck.py` does **not** close. Twice this
    session a fix landed on one draw path and not the others (partner tint, actor tint).
    The test: render the same body as hero, as partner, and as set actor from one lens and
    require the three to agree. Would have caught both automatically.
22. **⛔ `shotsweep` measures sameness, not correctness** — and it cannot see the scene
    camera at all, which is most of what gets changed.
23. **Re-shoot the whole film.** Nothing has been shot since Piper restoration, business
    clips, props, collision, marks, SFX, six lights, the cast array, cloth, water, the
    grammar re-block and the head-room fix. ~15 min at 24fps is 21,600 frames.

## G. THE CRAFT (from the books)

24. **Murch #3, rhythm** — cut on energy inflection, not fixed timecode. `scenecheck.py`
    already measures motion energy per take. **Cheapest real win on the list.**
25. **Mascelli, look/lead room for MOVING actors** — singles have it; walkers are framed
    dead centre.
26. **Mascelli, cut on action** — the EDL needs to know where movements begin and end.
27. **Katz, master-scene coverage** — extracted, unapplied; it is what
    `design/scene_direction.md` improvises.

## H. FREE MATERIAL NOT YET PULLED

28. **Ten Minute Physics** (Müller — code, not prose), **GPU Gems 1–3** (water and cloth
    chapters), **PBRT** in full, **Íñigo Quílez's articles** — directly on point for the
    procedural sky.

## I. PRE-EXISTING, STILL OPEN

29. ⛔ `crowd.exe 0 packs select` **segfaults** — a zero-dancer crowd is unguarded.
30. ⛔ **The skate PARK is broken** for every body on disk; only the actor path works, and
    `skate.bat` names packs that do not exist.
31. ⛔ **Park textures render as static noise** on Windows.
32. ⛔ **The crowd is nondeterministic** run-to-run even with clock, seed, lens and beat
    pinned — ~20% of pixels at the first capturable frame. Unisolated. It is why the doors
    cannot be pixel-diffed.
33. **PHOSPHOR grade B** — transplant its sim onto club slots.
34. ⛔ **The Pi copy is a forked ancestor** — none of this reaches it.
