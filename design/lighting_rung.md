# The lighting rung, and an outside review of the engine (2026-11-02)

*The engine order below is replaced by Phase 7 of [engine_plan.md](engine_plan.md). On 11-03 the lighting was added to the plan, for more capability and functionality. The game-feel rung is game work and can continue in reva.*

Two notes of outside advice came in, possibly bad advice, to be checked, with anything missing flagged. Each claim below
was checked against `crowd_kms.c` and the bake tools that night. This is a
proposal, not a decision: nothing here is built, and a decision is still owed on the order.

## What held up, and what did not

| The note said | The code says | |
|---|---|---|
| `crowd.exe` has not been promoted since 09-11; `dance.bat` runs it | True: 09-11, 1.19 MB, against 1.74 MB. `dance.bat` and `skate.bat` both run it | **Right, and first** |
| Every target is 8-bit, light clips at 1.0 | True: the post target is `GL_RGBA`/`UNSIGNED_BYTE`. `EXT_color_buffer_half_float=YES` here, so 16F can be built on the PC | Right |
| No specular, roughness or metal, except water | True: 0 lines match any of those words | Right |
| No tonemapping; raw values go to the screen | Half: no ACES or filmic, but doors have a soft luminance knee at 0.70, bloom and FXAA; `--film` adds grade and vignette | Overstated |
| Flat ambient, no AO | True: the realistic path is `base*(uSky + sun)`. 0 lines of SSAO. A toon room has `uHemi` | Right |
| Sun map is 28 u at 2048, no cascades | Nearly: it is +-28 (56 u across), 2048 at `high`, 4096 at `ultra`, with four-tap PCF. No cascades. The stairs are visible on the street at dusk, where a low sun stretches each texel | Right in effect |
| Three point lights | Stale: `NLIGHT` is 6 | Minor |
| glTF sources carry roughness and metal, the bake drops them | Half: `glb2rpack_anim.py` reads only the base colour. But UE exports lose their materials (`bake_town.sh`), and code-built packs never had any | Overstated |
| Reva looked too plasticky because there is no specular | Wrong: that remark was about the BLENDER renders of her sheet, and the answer was the bake (`build_reva.py`: unique UVs, AO, SSS, AgX Punchy) | **Wrong** |
| Hit-stop, shake, knockback and impact audio are what make ZAMNC feel better | Wrong: ZAMNC's Blueprints have none of them (`skate_src/zamnc/zamnc_logic.txt`). What it has is instant movement and a 4:1 speed ratio. The list is still good advice for this game | **Wrong about why** |
| Zombies run on dance-floor logic | True until that night: `street_step` fixed it | Right, done |
| RTS variants all boot the same skirmish | Stale: one engine, four parameter sets (`RTSVAR`) | Minor |
| Skate bench names packs that do not exist | True: `v1_reva_skate`, `v1_kickflip` are gone | Right |
| Leave the editor and the file split until after M1 | Agreed | Right |

## What it missed

1. **Bugs before shaders.** That same night two bugs were found by looking: every door's crowd on a set was body-shaped holes
   (the sky painted over it), and every `frnd()` on Windows returned 0..65536. Neither is a shading model, and the first
   was a bigger "looks bad" than anything on the list. Look at what you are about to light before lighting it, because
   there may be more. Not by sweeping the doors: most of them are half made (11-02), so checking them all makes no sense.
2. **There is no linear colour.** 0 lines mention sRGB or gamma, and nothing linearises a texture, so all lighting runs on
   gamma-encoded values. This sits UNDER the note's step 1: ACES on gamma-space light is wrong, and adding lights goes wrong
   the same way. It is the real 2005 tell. It re-lights every set and every show, so it needs its own A/B.
3. **"Lifts every scene at once" is not free, and not true.** WATEOT's toon palette is hand-tuned against a light capped at 1
   (the `⛔ THE LIGHT'S BRIGHTNESS IS CAPPED` note); a tonemap has to stand off toon bodies or it repaints the show. The post
   pass is off in the lab and in plain takes, so step 1 only reaches doors and `--film`.
4. **A rebake is not cheap here.** `CLAUDE.md`: a bake drops the face, the morphs and every authored clip. Per-material roughness
   has to go in by appending to finished packs (`rpack.py`), or as a table by material name, never by rebaking.
5. **Cheap wins it did not list.** Anisotropic filtering: the extension is present and it is never set, and the film's street wides
   look along the road. A smaller sun window in this game (the lens sees 33 u, the window is 56). The toon shadow path hard-codes
   `2048`, so it mis-samples at `ultra`.
6. **The game has no sound bed.** The mix dump is silence between effects: no music or ambience in the door, and the film's mix
   never reaches a game.

## Proposed order

1. ~~Play `reva_native/rvz.bat`~~ done 11-02: it works decently and feels a bit like ZAMNC. Playing it decides the next rung.
2. ~~Promote~~ done 11-02: `crowd.exe` is from 87f5b6c, the old build is `crowd_0911.exe`.
3. The game-feel rung on Reva vs Zombies: hit-stop, shake on a hit (`camshake` exists), the dead telegraphing a bite, music.
   It is the game in build, and lighting will not make it fun.
4. The lighting rung, each step with an off-switch and a look on a realistic body AND a toon one:
   anisotropy and the sun window (an hour) → linear colour → 16F + ACES on `high`/`ultra` → hemisphere ambient → SSAO → cascades →
   GGX last, because it needs material data no pack has.
5. The inspector and the file split after M1.
