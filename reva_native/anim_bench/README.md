# anim_bench — proving animation, not just looking at it

The lab is **door 12, ANIM LAB**. One body, a neutral studio, a measured floor.

## Leave it running and tune it live
Boot once (loading 134 packs is the slow part), then drive it through a control file —
no relaunch, no reboot between experiments:

```
REVA_ANIMSUBJ=manny REVA_WINDOWED=1 REVA_AUTOMODE=11 ./crowd_new.exe 400 packs manny &
printf 'seq=1 state=2 cam=2 shot=/path/run.ppm\n' > anim_bench/lab_ctl.txt
```

`seq=` must increase or the command is ignored (so the same file never re-fires).
Fields: `state=0..6` (idle/walk/run/air/trick/melee/gunfire) · `cam=0..3`
(side/front/3q/top) · `gait=<f>` · `speed=<f>` u/s · `reset=1` · `shot=<path>`.

## The instruments
| tool | answers |
|---|---|
| `motioncheck.py <log>` | is the motion physically correct? foot skate, contact, bone length, cadence, knee range |
| `posecheck.py <log>`   | is it continuous? pops, transitions, the die/hide ledger |
| `filmstrip.py`         | contact sheet + ghost overlay — poses side by side |
| `makegif.py`           | a real loop — the only way to judge timing and weight |
| `certify.sh`           | the regression suite: every subject × state, diffed against `baseline.txt` |

Telemetry comes from `REVA_POSEDUMP=<slot|-1 hero|-5 nearest>`.

## Two rules learned the hard way
1. **Validate a metric against a known-good AND a known-bad before believing it.** Two
   of the first three "defects" found here were bugs in the measuring tools: summing
   per-frame slip punished fast clips (reported a good walk as 2.06 FAIL), and "thigh"
   was measured across two joints so it changed honestly with leg swing.
2. **Judge on a clean plate.** A translucent body under coloured club movers reads
   magenta from the front and blue from the side. The lab renders solid, on its own
   fixed three-point white rig, framed identically from every angle.

`baseline.txt` is the certified record. Beat it or explain it.

## Are we reinventing the wheel? (2026-08-26)

Partly. Worth writing down so it is not re-litigated:

**The gait math is not reinvention, but it was 16% wrong.** The club plays IN-PLACE
cycles, so playback rate must be derived: `rate = desired speed / the speed the cycle was
authored at`. `gaitRate()` estimated that with forward kinematics. MoCap Online ships a
ROOT-MOTION sibling of every in-place clip, and its root displacement is that speed as a
fact — so the estimate could finally be checked:

| clip | FK estimate | ground truth | error |
|---|---|---|---|
| Walk_02_Cheerful | 0.0568 u/frame | 0.0676 | **−16% (slow)** |

Slow means the body outruns its own feet, which is precisely the residual drift the lab
keeps measuring. So: right quantity, wrong value.

**But feeding the exact number in made it worse** (manny run skate 0.20 → 0.33). Authored
speed belongs to the rig it was captured on; these clips are TRANSFERRED onto other
bodies, and stride scales with the target's limb lengths. The sidecar is therefore
opt-in (`REVA_GAITSIDECAR=1`) until it carries a source/target leg-length ratio. The
measurement itself is kept (`v1_mob1.gait`, `skate_src/mob1/make_gait_sidecar.py`)
because the number is correct — it is the *application* that is unfinished.

**Genuine reinvention we are living with:** runtime retargeting (`donorPose` does
rest-delta transfer every frame for 400 slots, where Blender/UE could bake retargeted
clips per pack offline and delete both the transfer and the "24 uncovered packs"
problem), and a hand-rolled state machine every commercial engine ships. Both are
deliberate: the engine runs 400 skinned bodies at ~100fps in one C file, on ANGLE here
and bare KMS on a Pi, which is not a thing Unreal does. UE is already used correctly —
as the asset source through the artery, not as the runtime.

## Standing (2026-08-26)

A gait and a stand fail in different ways, so `motioncheck.py` mostly degenerates on an
idle -- zero contacts, zero stride -- while everything that actually goes wrong in a
stand goes unwatched. `posturecheck.py` measures the stand's own invariants: ankle at
floor level, both feet weighted, pelvis and shoulders level, spine vertical, head over
the feet, hip-width stance, knees extended and agreeing, arms hanging, torso not wrung,
bone lengths fixed, alive but not thrashing, no corrupt baked frames, no drift. All
scale-free, so ten models of ten different sizes go in one table.

```
bash anim_bench/standsweep.sh          # ten humans x IDLE, table + a 9-pane wall each
python anim_bench/posturecheck.py --selftest
```

`--selftest` injects faults of known magnitude (pelvis roll, torsion, splay, arm-out) and
demands the numbers back. Run it after touching the tool. `stand_baseline.txt` is the
record: ten of ten stand, one asset defect outstanding.

### What the sweep found, and what it cost to believe it
Four defects, each caught by a number and confirmed on the wall before being touched:

1. **crowd_susan slept through IDLE, face down.** She ships `idle`, `laying_idle` and
   `sleeping_idle`; the hero's role scan had no first-wins guard, so the LAST substring
   match won. The pack-side mining had the guard. Fixed both ways -- `floorClipName()`
   refuses names that describe the floor, and the stand now gets the same `clipUpright()`
   audit the gaits have had since S6e. A name filter is a guess; the audit is a measurement.
2. **Most of the roster has no stand at all.** The whole AssetsvilleTown cast is dance
   loops and nothing else, and reva has no idle either. They fell through to "hold the
   walk cycle 27% in" -- a statue caught mid-step, stance 5.7x hip width. The crowd has
   drawn its stand from the donor bank since S4g; the hero never did. Now it does, in the
   lab and in all eleven doors.
3. **The borrowed stand flung their arms out** (40-68 deg from hanging, against 8-21 for a
   real stand). `donorOK` only ever asked whether the body stays UP -- in the gaits it was
   written for, arms swing. So the arms are audited separately for the stand, and where
   they will not transfer the arm chain is masked back to the body's own rest rotations:
   donor legs and spine, own arms. Stiff arms beat arms out sideways.
4. **reva stood 9cm through the floor.** Grounding places the ANKLE at a fixed sole
   allowance, which quietly assumes the ankle-to-toe angle of her rest pose. Clamped on
   the bone that actually reaches lowest, one-sided, and gated to a body standing still --
   letting it run during locomotion moved her measured stride 4.95u -> 2.40u, and a metric
   that shifts under an unrelated fix is a fix that reached too far.

### Two more bugs in the instruments (rule one, again)
- **"Biggest single-frame joint move" measured the render rate, not the animation.** Clips
  are sampled nearest-frame with no interpolation, so at 97fps against an 18fps bake every
  pose is held ~5 frames. It called manny a popping mess. Pops are now found on the
  collapsed *step* sequence, which is render-rate independent -- and the real defect it
  then found is genuine: one corrupt baked frame in manny's `a_idle` snaps his left foot
  8cm and back, once per loop.
- **A foot-skate ratio with no contacts is 0.00, which prints as a perfect PASS.** reva's
  broken run scored that way and the old baseline inherited the fiction. Too few contacts
  is not a good score, it is no measurement -- and for a moving body it is the defect
  itself. Her run now reads honestly: THE FEET NEVER PLANT.

Thresholds here were first reasoned from anatomy and were wrong in the expensive
direction -- BALANCE at 0.10 failed nine of ten subjects the wall showed standing fine. A
test that fails almost everything is usually a bad threshold, not a bad fleet. Each band
is now set from the population confirmed upright on the wall, widened past the worst good
one, and re-checked to still catch susan and reva. That is calibration, not grading on a
curve: both known-bads are still caught.

### Open
- manny `a_idle` carries one corrupt baked frame (POPS 1). Asset defect, not code -- it
  wants a re-bake or a frame patch.
- reva's RUN still fails, now precisely: her feet never plant. Rest-orientation mismatch
  between her rig and the donors; the real cure is offline retargeting per pack, not
  another runtime constant.
- reva's own rest pose holds the left arm ~48 deg out, so her masked stand inherits it.

## GIF captures use the WALL (jacob, 2026-08-27)

Every move delivered for the eyes gate is captured on the nine-angle wall (`grid=9`),
whole body, no cropping. A single-camera capture with a crop is the exact habit the
wall was built to end -- one angle hides what eight others show, and the first
single-cam greet capture cut the raised hand off at the frame top. `grid=9` in the ctl
line of the capture run; full-frame GIF; done.

## grid=13: the wall grows a close-up column (jacob, 2026-08-27)

"you need close ups of the body from 4 more angels. add them to the grid." grid=13
keeps the nine-angle wall on the left 3/4 and adds a right-hand column of FOUR
CLOSE-UPS: a third the orbit distance, on the diagonals between the wall's cardinals,
aimed at chest height -- where hands meet bodies. With a partner loaded the close-ups
centre between the pair. Two-body deliveries should ship on grid=13; the close column
is where "how the hands lay across the bodies" is actually visible.
