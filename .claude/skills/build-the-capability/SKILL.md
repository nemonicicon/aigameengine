---
name: build-the-capability
description: Use when the club can't do something you need — a camera won't move, a texture won't blend, a limit looks too low, a tool refuses a body, a shot can't be expressed. Turns "work around it" into "measure it, then build it". Invoke for any "the engine doesn't support X", "X isn't possible", "we'd have to fake X", missing engine feature, hardcoded cap, or a tool that only handles one kind of asset.
---

# BUILD THE CAPABILITY

jacob's standing rule, 2026-09-01, verbatim:

> *"if we ever come across anything our engine cant do, we need to build and implement it,
> unless it is ridiculos"*

and the corollary he added when this kept happening:

> *"now that moving camera is another capability to implement, and maybe we should set this
> up as a skill, catching unimplemented capabilities and adding them"*

So: **do not design around a missing capability. Do not downgrade an asset to fit an old
limit. Build the thing.** The only exit is "ridiculous", and that bar is high — it means
weeks of work or a rewrite of a system that already works, not "this is awkward".

## Why this skill exists: the limits were all GUESSES

Every one of these was found by an asset needing it, and every one was a number or a
branch nobody had ever measured:

| It "couldn't" | The truth | Found by |
|---|---|---|
| 64 bones max (`MAXB`) | hardware reports **1023** mat4 | a 96-bone body |
| morphs capped at 8, clamped **silently** | raised to 128 | a 107-shape ARKit face |
| alpha BLENDING did not exist, only alpha *testing* | one flag + a draw-order pass | hair cards |
| no mipmaps, ever generated | three lines at upload | every texture aliasing |
| hair solver: 2 constraint passes, gravity **8.4x real** | passes scale with length; gravity measured | one braid |
| **the lens could not move** | `camto=` / `camsecs=` | an aerial push into a window |
| partner clips couldn't be posed by hand | the retarget was already there, 370 lines up | a body with 1 clip |

Note the last row. **Check whether it already exists somewhere else in the file first** —
the scene path could borrow a clip while the live ctl path could not, and the fix was to
call what was already written.

## The procedure

1. **NAME the capability in one sentence**, as a thing the engine cannot express — not as
   the symptom. "The lens cannot move" beats "the cold open looks static".

2. **MEASURE the limit before you believe it.** Print what the hardware/format/file
   actually reports. A cap in the source is a claim, not a fact. `MAXB` was 64 against
   1023; the chain solver's "stylized" gravity was 8.4x physical and nobody had done the
   division.

3. **SEARCH for it already existing.** `grep` the neighbouring systems. Half of these were
   present on one code path and absent on another.

4. **BUILD IT PROPERLY** — as a general mechanism, not a special case for today's asset:
   - one interpolator that covers dolly + re-frame + zoom, not three
   - a *name* addressing every mesh that carries it, not an index
   - a texture map read from the source's own materials, not guessed by similarity
   - expose it as a ctl / flag so a scene can choose, with the measured value as default

5. **MAKE THE FAILURE LOUD.** A silent clamp is worse than the limit. The morph loader
   dropped everything past 8 without a word; it now prints what it dropped. If the new
   mechanism can be misused, say why it refused — "no bank" and "rig refused" are
   different fixes.

6. **VERIFY ON PIXELS, and A/B it.** Build the pre-fix binary from `git show HEAD:...` and
   compare under identical conditions. A number that moves (1.55% -> 6.51% of a face
   panel) is the proof; "it looks better" is not.

7. **RECORD it**: a comment at the site saying what the old limit was and what exposed it,
   a line in the commit, and a memory entry if it changes how future work is done.

## Traps this project has already paid for

- ⛔ **A fixed crop + pixel diff cannot measure anything while the camera moves.** It
  returns 70-85% "movement" regardless. The club's director pans constantly.
- ⛔ **Feature-finding in a render does not work.** Counting connected blobs of hair colour
  reports 12-24 "detached strands" on *healthy* alpha-cut hair. `face_aim.py` failed this
  way twice. Measure the rig against itself, not the picture against an idea.
- ⛔ **Never run two GPU benches at once.** A background A/B calling `driver.py kill`
  destroyed a 10-minute take mid-capture.
- ⛔ **An instrument that lies is worse than none.** If a tool you wrote to prove the fix
  turns out to measure the wrong thing, say so and delete it — do not quote its numbers.
- ⛔ **`primitives[0]` / mesh-record-zero.** Four separate tools have read one primitive of
  one mesh and silently ignored the rest. Any tool touching geometry must walk them all.
- ⛔ **Every per-mesh array must ride the load-time mesh permutation.** The comment listed
  three; `morphMesh` was a fourth, and a jaw morph landed on a pair of boots.
- ⛔ **A quoted heredoc here eats one level of backslash.** Write patch scripts to a FILE.

## When it really is ridiculous

Say so plainly, say what it would cost, and offer the nearest thing that is not. That is a
different sentence from "the engine doesn't support it", and it is the only acceptable
version of that sentence.
