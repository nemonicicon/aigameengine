# clubcloth: worn cloth, on any skinned character

The club's solver for a whole garment. Plain C, no engine under it: `clubcloth.h` says what it is and why, `clubcloth.c` is all of it. `godot/club_cloth_gde.c` binds it to Godot 4.7 as a GDExtension, on the interface header Godot itself writes (`godot --dump-gdextension-interface`), so there's nothing to fetch and nothing else to build.

Built 11-02 for the alien's clothes, with the collision and clipping work they needed.

## Build

```
build_windows.bat [folder for the dll]
```

Builds and runs `test_clubcloth.c` (does it stay on him) and `bench_clubcloth.c` (does it move like cloth), then the extension: a solver that fails either isn't copied anywhere. About 18 seconds. Static C runtime: the DLL needs nothing installed. Visual Studio's compiler, found by its path at the top of the script.

```
sh build_unix.sh [folder for the library]
```

The same on a Mac or on Linux, with the machine's own compiler: a universal `.dylib` for both kinds of Mac, signed ad hoc, or a `.so`. Nobody needs one of those machines for it: the game's `.github/workflows/cloth-library.yml` runs this on GitHub's, loads the result in Godot there with `load_check.gd`, and commits what passes. Built on the oldest Linux GitHub offers, as a library only runs on a C library at least as new as the one it was built on.

## What a garment needs

Its mesh, skinned as usual (that skinned pose, kept outside the skin, is the target), and two numbers for each point, from the tailor:

- **How far it may go from the skinned pose.** 0 is the skin's pose itself: collars, shoulders, waistbands. 10 cm at a hem.
- **How near the skin it may come.** A top over legs asks for more, so it stays out of them.

In a glb they go in `TEXCOORD_1` (`skate_src/author/glb_redress.py` writes it, every material ignores it). `DontGetDropped/tools/anim/alien_dress.py` is the worked example, and its `FREE_BY_HEIGHT` table is all the painting there is.

And what it's made of, `cc_fabric`: what a square metre weighs, its bending length (the textile lab's number, ASTM D1388), and how hard it is to pull on the bias. Typical values, from general textile knowledge and not from one lab's table: a tee's jersey 0.16 kg/m^2, 2 cm, 3 N/m; a shorts' twill 0.23, 3, 5; a sweatshirt's fleece 0.32, 3, 3; jeans' denim 0.40, 4.5, 8.

## The bench

`bench_clubcloth.c` is the solver in scenes with an answer known from physics or from the lab, every line a measurement and the range allowed. The solver is checked by this bench, not by filming it (11-03). The solver as it was failed 40 of its lines and swung at 6.1 Hz. Now:

| | measured | known answer |
|---|---|---|
| a 40 cm sheet's period | 1.04 to 1.05 s | 1.036 s, a compound pendulum |
| its motion above 3 Hz | 0.0% | |
| at 30, 99, 144 fps | period the same to 0.1% | the same |
| frame times wobbling +-40% | 0.1% off the even swing | the same |
| falling flat, tee to denim | 1.45 to 2.26 m/s | 1.48 to 2.33 m/s, sqrt(2 m g / rho Cd A) |
| bending length read back, tee, twill, fleece, denim | 2.0, 3.0, 3.0, 4.6 cm | 2, 3, 3, 4.5 cm, against the exact heavy elastica |
| on a 10 and a 25 degree slope, friction 0.3 | stays, slides | stays below 16.7 degrees |
| a panel on a body at 6 rad/s, how far out its end flies | 43.8 to 46 cm | 46.3 cm, a heavy chain integrated down its length |
| a skirt on a body that starts and stops | 1.2 to 1.4 Hz, 11 to 14 cm, 0.2% above 3 Hz | a pendulum's about 1 Hz; it was 12 Hz and 0.6 mm |

It only holds on meshes as coarse as garments are: one pass a substep can't carry a stiff fabric's bending down a mesh much finer than 1 cm. Denim at 1 cm reads a third short at 480 substeps a second; below 5 mm everything reads far too soft.

Then on a body, with its own contacts: read what's drawn by frequency, against the pose's own. A pose has its own jumps (see the game's clips), and what cloth does at a jump isn't the cloth's fault. On the alien at a run, 1% of the tee's own motion is above 8 Hz, and it was a buzz at 13 Hz before.

## Measured

On a Ryzen 5 5500, the alien's tee and shorts (908 points), 60 Hz, main thread, 480 substeps a second: 0.81 ms a frame, 99th percentile 1.2, worst 1.4. Sweatshirt and trousers 1.3, 1.7, 2.0. Two garments solve on two threads, so the caller waits for the slower. GDScript was 27 ms for the same loops and Godot's SoftBody3D about 2 ms a tick with a worst of 15 ms, at 60 Hz only.

## In Godot

`godot/club_cloth.gd` is the rider's side: a `SkeletonModifier3D` that has to go after everything else that moves the skeleton, a ragdoll included. A ragdoll's simulator moves itself last when it starts, so whatever starts one has to put this back after it (`skater_ragdoll.gd` in the game). It moves itself back behind anything added to the skeleton later, like a map's raised torch. When the skeleton's own space jumps with nothing moving in the world, a rider put back after turning round on his deck, `carry(rel)` takes the cloth through it. `godot/club_cloth.gdshader` is the garment's look: keep its `LIGHT_VERTEX` line or smooth cloth breaks up in dots at a glancing sun.

## What it is not yet

Under an arm there's still a small crease in most poses. A garment's parts lean off each other (`cc_set_parts`) but a fold within one part can pass through itself. A garment goes over one other (`cc_set_under`), and what's under it never moves for it, only tucks under it as drawn. The kit's engine still has its own `skirtStep` and `clothStep` (`engine/cloth.inc`): this one takes their recipe and doesn't replace them yet. `design/engine_capabilities.md` has the section.
