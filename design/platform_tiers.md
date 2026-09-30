# THE LADDER — one build from a Raspberry Pi 4 to a desktop

*(2026-09-30. jacob: "a cross platform ai first game engine that can be run on a raspberry pi 4 as
the dev console and the target console, taking great inspiration from n64 recomp techniques, to
produce games that run flawlessly and scale elegantly from the weakest hardware to the most
powerful gaming computer.")*

## What the N64 recomp projects actually teach

Zelda 64: Recompiled gets widescreen, high frame rates and any resolution out of a 1998 game
without touching its logic, because the game never talks to the GPU: it runs its own fixed tick
and emits display lists, and a renderer that is free to run at any rate interprets them. Two
separations, and the club now has the first:

1. **The sim runs on whole ticks** (`REVA_TICK`, default 60). Wall time only decides how many
   ticks a frame gets (1..4, snapped against vsync jitter, capped so a stall is not a debt). A
   display faster than the tick waits for the boundary. `REVA_FIXEDSTEP` is one tick per frame
   with the tick equal to the step, byte for byte what it was.
2. **The picture is not the sim.** Not yet: sim and draw are one loop today. Interpolating a
   pose between ticks for a 144 Hz panel needs that loop split (item 5 of the same session);
   at a 60 Hz tick on a 60 Hz panel it never shows.

And the receipt that makes an AI-first engine possible: `REVA_SIMHASH=<every N frames>` prints
two FNV-1a hashes of what the sim decided (the hero's solved pose, her position and heading,
every crowd slot). **Exact** is the float bits, the regression receipt on one machine; **coarse**
rounds each float to a thousandth first, what two machines with different libm last bits can
still agree on. Verified 09-30: two runs, and a run of an unsigned-`char` build (ARM's `char`),
print identical hashes at every checkpoint. The dice are the club's own LCG now (`REVA_RAND=libc`
puts the library's back), because glibc and msvcrt roll different sequences from the same seed.

**What the receipt caught on its first day.** A `pi` run and a `high` run of the same show hashed
differently at the same tick, once, and not again in three sequential re-runs. The logs differed in
nothing but two extra stat lines: the odd run had been slower. glibc's `MALLOC_PERTURB_=<byte>`
fills every fresh allocation with that byte, so two runs with different bytes must hash the same
unless something reads memory it never wrote. They differed from the first checkpoint. The crowd
slots were `malloc`'d and spawn never set `pgT`, `pgD` or `blk`, all three read in the roam tree:
the receipt had been hashing heap history. The slots and the pose scratch are `calloc`'d now, and
the A/B is the standing test for this class: **two `MALLOC_PERTURB_` runs, identical hashes, or
there is an uninitialised read.** The build matrix runs it on every push.

## The Raspberry Pi 4, by the numbers

| | Pi 4 (VideoCore VI) | where from |
|---|---|---|
| GPU clock | 500 MHz | [Wikipedia: Raspberry Pi 4](https://en.wikipedia.org/wiki/Raspberry_Pi_4) |
| Compute | ~32 GFLOPS (8 FLOPS/Hz per QPU, four QPUs a slice) | [element14 benchmark](https://community.element14.com/products/raspberry-pi/w/documents/4319/benchmarking-the-raspberry-pi-4-model-b) |
| Fill rate, theoretical | 2.4 GPix/s | element14, same page |
| Memory bandwidth, measured | ~4.6 GiB/s (shared with the CPU) | element14, same page |
| GL | OpenGL ES 3.1, Mesa `v3d`, renderer "V3D 4.2", 4 render targets max | [mmozeiko's es2_info](https://gist.github.com/mmozeiko/8eb6728197099f8f6722b4808964ef43), [Mesa v3d docs](https://docs.mesa3d.org/drivers/v3d.html) |
| Extensions the club asks for | depth_texture, texture_float, half_float, packed_depth_stencil, element_index_uint, vertex_array_object, standard_derivatives, frag_depth, shader_texture_lod: **all present** | mmozeiko's es2_info |
| glmark2 (64-bit Bookworm, 2023) | ~425 | [Raspberry Pi forums](https://forums.raspberrypi.com/viewtopic.php?t=356102) |

What is NOT known until measured on the board: `MAX_VERTEX_UNIFORM_VECTORS` (the 256-bone palette
needs 1024 vec4; the engine warns at boot if the hardware cannot carry it), the real shaded fill
rate for the club's mesh shader (theoretical fill assumes a trivial fragment), and the frame time.
`reva_native/build_pi.sh` then `./crowd_linux 400 packs select 1280x720 bench` prints `work` ms.

## The budget, and where the rungs come from

A 720p frame is 0.92 MPix. The club draws it three times: the sun's shadow pass (2048² = 4.2 MPix
on its own!), the 3D frame, and the post quad. At a practical ~1 GPix/s of shaded fill that is
already past 30 fps before a single body's skinning is paid for. So the rungs cut the pixels first:

| rung | 3D render scale | sun map | bloom | FXAA | cloth substeps | pixels shaded per 720p frame |
|---|---|---|---|---|---|---|
| `pi` | 50% (640×360 in the post target) | 512² | off | off | 1 | 0.23 + 0.26 + 0.92 ≈ **1.4 MPix** |
| `low` | 67% | 1024² | off | on | 2 | 0.41 + 1.0 + 0.92 ≈ 2.4 MPix |
| `mid` | 85% | 2048² | 0.12 | on | 4 | 0.66 + 4.2 + 0.92 ≈ 5.8 MPix |
| `high` (default) | 100% | 2048² | 0.12 | on | 4 | 0.92 + 4.2 + 0.92 ≈ 6.1 MPix (today's club) |
| `ultra` | 100% | 4096² | 0.12 | on | 8 | 0.92 + 16.8 + 0.92 ≈ 18.6 MPix |

`REVA_TIER=<rung>` picks one; `auto` starts at `high` and measures: a frame over the tick by a
third for ~45 frames steps down a rung, a frame inside half the tick for 30 s steps up (with a
30 s cool-down after any step down, so it cannot oscillate). Under `REVA_FIXEDSTEP` auto never
moves. The sun map is set at boot only (the map is allocated once); the rest changes any frame.

**How the render scale works.** The 3D frame is drawn into a sub-rect of the post target, and
the post quad, which already runs once over every screen pixel, upsamples it: no second target,
no reallocation. Every fetch in the two post shaders goes through `fetch()` = `uv*uvs`, clamped
to the rect. The anim lab always draws 1:1: it is an instrument, and its multi-cam grid is laid
out in screen pixels.

## The split

A 20,000-line file builds in about two minutes on a Pi 4, every change, and a sim/draw split (the
second N64 separation, above) is impossible while sim and draw are one loop in it. So the file is
being taken apart, one section at a time, each stage with the same receipt: the `REVA_SIMHASH`
lines and the lab frame **byte-identical** to the run before the move.

| stage | unit | what left | outside face |
|---|---|---|---|
| 1 | `club_math.c` | the 4x4s | `M4`, four functions |
| 2 | `club_audio.c` | THE AUDIO MIXER (09-11) | six calls |
| 3 | `club_save.c` | THE SAVE FILE's key/value store | six calls |

`club.h` is the whole shared face. The recipe, for the next section: list the section's `static`
functions and `g_` globals, count each one's uses outside the section (a shell loop over `grep -cw`),
move the section when that list is short, un-`static` exactly the names on it, put their prototypes
in `club.h`, add the unit to every build line (`build_linux.sh`, `build_pi.sh`, `driver.py`'s `SRC`,
`CLAUDE.md`, `README.md`), then run the receipt. Sections that share a dozen globals with the loop
(the game modes, the cast, the skater) are not next; the post pass, water, the sun's shadow map and
the set cloth are.

## Still to measure, on the board

- The `pi` rung's frame time at 720p with 4, 40 and 400 slots. That number decides whether the
  Pi is a 30 fps or a 60 fps floor, and whether the shadow pass stays on at all.
- Whether v3d carries the 256-bone palette (boot prints the limit).
- Whether `auto` settles on `pi` or `low` on a Pi 4, and where it settles on the desktop.
