# THE WILLIAMS LAYER — weight, drag and counteraction on every body in the club

**Source:** Richard Williams, *The Animator's Survival Kit* — the WALKS chapter and the
FLEXIBILITY chapter. jacob handed the book over with no other words, which is a note, not
a reading list.

This is the design home for the club's procedural animation principles. The code is
`wmsApply()` in `reva_native/crowd_kms.c`; the instrument is
`reva_native/anim_bench/weightcheck.py`.

---

## What was wrong

Williams is blunt in the walks chapter: **a traced live-action walk floats.** Rotoscope it
accurately and it still reads weightless, and the cure is to *exaggerate* the ups and the
downs, because it is the up-and-down of the masses that reads as weight.

Our case was worse than tracing. `donorPoseF` is **rotation-only** — a retargeted clip
carries no pelvis translation at all. So every borrowed walk in the club (which is most
of them: 105 citizens, 32 zombie clips, the whole street) arrived with its vertical
component *deleted*. Not damped. Deleted.

Measured, on pixels, before the layer: **the top of a walking body's head moved 2 pixels
in 72 frames.** She was on a rail.

## What the layer does

An overlay applied on top of whatever clip is playing, so it reaches authored clips,
borrowed clips, retargeted shambles and the skaters without re-authoring a frame.

| # | Williams | in the engine |
|---|---|---|
| 1 | The body goes **down just after the contact** and **up just after the passing position**; the up-and-down of the masses is the feeling of weight | the pelvis rises and falls 9% of leg length peak-to-peak, lagged ~55 ms |
| 2 | **The belt line** tilts back and forth favouring the leg that is lowest | the pelvis rolls toward the low foot, about the direction of travel |
| 3 | The **shoulders oppose the hips** | the top spine bone counter-yaws against the leading leg |
| 4 | **Overlapping action** — the parts do not start and stop together; the head arrives late | a one-pole lag on the head |
| 5 | **Breaking joints** — when an arm swings, the hand keeps on going and the elbow breaks | a one-pole lag on both forearms |

⭐ **4 and 5 are NOT gated on walking.** His bulldog drags its jowls when it *turns its
head*. That is why our idles read dead, and it is the half of this that reaches a woman
standing still on a porch.

## ⭐ The phase is measured off the feet, not the clock

Williams defines a walk by its **contact** and **passing** positions, and both of those
are *foot positions*. So reading the feet after FK gives the phase for any clip, on any
rig, borrowed or authored, at any playback rate:

    feet apart     = contact  ->  the body goes DOWN
    feet together  = passing  ->  the body goes UP

Nothing to keep in sync, no per-clip metadata to author, and it is correct for the great
majority of the club's clips, which nobody ever labelled.

The same measurement gives the **amplitude for free**: it follows how fast the feet are
actually swapping, so an idle does not bob and a walk does. One term, no state machine.

## The traps, all of them paid for

- ⛔ **RIG SPACE IS NOT BONE SPACE.** A correction post-multiplied onto a bone's local
  quat turns about *that bone's* axes, and on a UE rig those run down the bone. Every
  axis here is converted into the bone's own frame first (`wmsLocalAxis`), or the belt
  line rolls about whatever direction the pelvis happened to be drawn along.
- ⛔ **THE BOUNCE MUST BE ZERO-MEAN.** Against a fixed 0.5 midpoint the term keeps a net
  sign for any clip whose stride is not symmetric, and the side-by-side caught it at
  once — she walked the whole strip standing *taller*. It now subtracts a slow running
  mean of its own phase. A body that hovers because of its own animation layer is this
  project's oldest bug.
- ⛔ **THE `WmsState` TYPEDEF MUST PRECEDE `SetActor`,** which contains one. Declared
  after it, gcc treats the member as `int` and the call site then fails 700 lines away
  with a pointer-type error that names neither.
- ⛔ **`gcc ... | head` REPORTS `head`'s EXIT CODE.** A build that had failed read as
  `EXIT=0`, and the harness then measured a stale binary — the "instrument that lies"
  failure, again. Check `${PIPESTATUS[0]}`.

## The instrument

    python reva_native/anim_bench/weightcheck.py           # the layer on
    python reva_native/anim_bench/weightcheck.py --off     # REVA_NOWMS=1, for comparison
    python reva_native/anim_bench/weightcheck.py --selftest

Williams' own test, on pixels: one body walks a cycle **on the spot** past a locked side
lens, and for every frame the tool finds the top of her head and reports how far it
travelled.

| | head travel | direction changes |
|---|---|---|
| layer OFF | **2 px** (3 distinct rows) | noise |
| layer ON | **14 px** (13 distinct rows) | 14 — rises and falls twice per stride |

- ⛔ **SHE MUST NOT TRAVEL.** A head at a fixed world height projects to a different
  screen row as it crosses the frame; over ten units of street that put **211 px** of
  perspective on top of a 14 px bob. Walking on the spot removes the trend, and the
  layer still engages because its activity term reads the feet, not the ground.
  (Williams says never to *animate* a cycle in place. This is measuring one.)
- ⛔ **THE FINDER MUST TRACK HER.** The lab draws its HUD in the top-left, and a finder
  that takes the topmost non-sky pixel measures the *text* — which never moves, so it
  reports a beautifully steady walk and looks like a result. It finds her columns first,
  in the lower two thirds, and only then looks up. The selftest plants a decoy HUD.
- ⛔ **ONE BAD FRAME MUST NOT SET THE RANGE.** A single mis-read frame turned a 15 px bob
  into 62 px. The range is robust now and the outliers are printed out loud.

## ⛔ And on why the pixel regression is not this

jacob, twice: *"your regression gate is flawed too"*, and *"why do i find the pixel
regression to be pointless"*. He is right, and this doc is where the answer lives.

`shotsweep.py` measures **sameness**, not quality. It fails in both directions:

- **Green on bad work.** It reported 15/15 while every neighbour in the film walked with
  their head bolted to a rail. It had no opinion, because nothing *changed*.
- **Red on good work.** The run that landed this layer flagged `lab_pair` at 3.59% — and
  the reference frame reads `STATE 1/7 IDLE` while the run reads `STATE 2/7 WALK`. The
  harness photographed a different *moment*. A pixel gate cannot tell that apart from a
  broken render, so it screams either way.

Its one honest job is catching an **accident** — the day the hero went invisible whenever
a partner was on stage, a strict shot would have screamed. That is the whole of it.
Quoting "15/15" as a verdict is the same failure as reading log lines and calling them
receipts, with a percentage sign on it.

**The pattern to copy is `weightcheck`:** pick the property that actually matters, find
the number that goes *up* when the work gets better, and measure it on pixels.

---

## THE ARMS STOP TWINNING (the skaters)

jacob on the crew: *"terrible lol, keep reiterating"*, *"the skaters' arms read posed"*.

⛔ **They were a literal mirror pair.** Both arms took the same `armSpread`, the same
`armFwd` and the *same* `elbow` — the only difference between them was a sign flip on the
spread axis. Every extreme landed on both arms on the same frame. Williams names this
("the arms *twin* unnaturally"), and no amount of tuning the pose library could have
cured it, because the library only carries **one** set of arm numbers to give.

Three things, in the order they matter:

1. **The stance split.** A rider's arms are not a mirror pair to begin with — the arm over
   the nose of the board rides higher and further forward, the trailing arm hangs back
   and lower. `SK_STANCE` already says which way the deck points, so which arm leads is a
   fact the engine *holds*, not a guess.
2. **A signature per body**, deterministic from the body, so a crew of six reads as six
   people and it is the same six every take. Noise would flicker; this does not.
3. **Independent slow drift, out of phase between the two arms.** Williams' actual point
   is that the extremes landing on the same frame is what reads mechanical — the symmetry
   is only the symptom.

⛔ **AND THE ARM CODE EXISTED TWICE** — once in `skateApply` for the set's riders and once
inlined in the hero's `g_skate && g_procRig` block. De-twinning one copy would have left
half the cast twinned, exactly the way the mouth aperture survived two fixes because its
shader source was duplicated. There is one `skateArms()` now, and both call it.

`REVA_TWIN=1` restores the old mirror so the comparison can be shot; `anim_bench/ridecheck.py`
is the instrument (a **front** view is what reads twinning — a side view cannot).

## The hero and the partner have it now

The blocker was a frame clock at their FK sites, and there already was one: **`g_chDt`**,
published by the main loop for H1's chains. The hero's layer runs before her FK and
*before* `chainSim`, so her hair swings with the bounce instead of against it; the
partner's runs after `scnHeadLayer`, so a look-at is what drags.

⭐ **Measured, and the honest result is that the hero barely needed the weight:** her walk
already travels **23 px** at the head (she has authored hip translation, plus the S4.5
procedural bounce for rotation-only heroes), and the layer took it to 24. The gain there
is the **drag**, not the bounce — and `lab_reva` (her, standing) came back **bit-identical**,
which is the activity gate proving it does not touch a body that is not moving.

The 2 px → 14 px win is the **actors**: the citizens, the crowd, the zombie bank, the
street. They are the ones whose clips arrive rotation-only.


## The dials

| | |
|---|---|
| ctl `wms=<0..2>` | the amount. Default **0.85** — Ken Harris' note, via Williams, was to understate it |
| `REVA_NOWMS=1` | off entirely |
| `REVA_WMSDBG=1` | prints leg length, foot separation, phase and gain |

## Owed

- **The board separates from the feet at the apex of an ollie** — visible in column 4 of
  every row of the ride sheet, and still jacob's open note. Untouched here.
- **The arm swing is widest on the DOWN position, not the contact** (his words, and he
  notes it is the opposite of what an animator would prefer). That is a property of the
  authored clip, not something an overlay can move.
- **Twinning elsewhere.** Fixed on the riders; the walk cycles' arms are whatever the
  authored clip does, and nothing checks them.
- **`shotsweep`'s `lab_pair` shot is not pinned to a lab state**, which is why it can
  photograph IDLE one run and WALK the next. Pin it or drop it.
- jacob's eyes on all of it.
