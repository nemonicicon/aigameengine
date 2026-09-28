# SKATING — the pose, generalized off the skate bench

*(written 2026-08-29. jacob: "add skaters to this scene... a punk influence, the gritty
neighborhood kind... we need skate animations generalized, and zombies vs neighbors is
punk in its character." Then, on a screenshot of reva contorted in the park: "great for a
terminator 2 t1000 post getting blasted... but reva should not look like this in the skate
park.")*

---

## THE STATE OF THE SKATE BENCH

⛔ **It is broken for every body currently on disk, and the packs it was tuned against are
gone.** `skate.bat` still names `v1_reva_skate.rpack` and `v1_kickflip.rpack`; `chars.txt`
still points REVA at `v1_reva.rpack`. **None of those three files exist.** Tested:
`v1_r2moves` comes out a fan of stretched triangles, `v1_Character_30_Female` a mass
floating above the board, `v1_moves` takes the BAKED path and was not checked closely.

⛔ Skate mode also wants a different argv than the lab: `crowd.exe 1 packs
packs/v1_<name>.rpack` — a PATH, not a subject name.

**What was right and is kept:** the pose library (`PZ_STAND / ROLL / CARVE / PLANT /
RECOV / LOAD / POP / TUCK / LAND / GRIND / MANUAL`), the `PoseKey` trick chains, and the
spring table. Crouch is `PZ_LOAD`; a trick is a chain. That is authored data and it is
good.

---

## THE RIDER, ON THE ACTOR SYSTEM

`actor <pack> skate x y z yaw scale ... <board>` — clip `"skate"` is not a clip, it is the
procedural rider. Any body with the canonical bone map can ride.

⭐ **THE BOARD FOLLOWS THE FEET, not the reverse.** Posing legs onto a fixed deck is an IK
problem with a hundred quiet ways to be wrong; measuring where the feet actually landed
after FK and putting the deck under their midpoint is right by construction. Reported by
`REVA_SKDBG=1`: feet y 0.160–0.223, deck 0.000–0.031 — soles on a 0.16 deck, wheels on the
road. `SK_DECK_H` is used twice in opposite directions so the two cannot drift.

⭐ **A CROUCH IS THE LEGS, NOT THE ROOT.** `hipDrop` as a root translation sinks the feet
with the body (measured 0.13–0.22 below ground). The knees bend; the body is then
re-planted by the measured difference.

⛔ **AND THE PLANT IS A GROUND CONTRACT.** Holding the soles at rest height while airborne
cancels the tuck exactly — `PZ_TUCK` pulls the knees to the chest and the plant puts them
straight back. It releases as she leaves.

⭐ **A SKATER RIDES SIDEWAYS.** The body takes a quarter turn off the direction of travel
(`SK_STANCE`, +1 regular / −1 goofy); the deck keeps its own heading because it still
points the way it rolls. *This was invisible for six rounds because every check was a side
view.*

⭐ **THE SPRINGS ARE THE ANIMATION.** All twelve channels run through critically-damped
springs before they reach the rig, with the reference's stiffness table: hips and feet
heavy and quick, spine looser, head and arms loosest, **so motion reaches the extremities
late without a line of code saying so**. Without them every channel arrives at the same
instant on a straight line and every key boundary is a corner — jacob: *"way too stiff.
jerky."*

⛔ **A KICKFLIP SPINS ABOUT THE DECK'S LONG AXIS.** Measured off the prop's own vertices:
1.372 in Z, 0.375 in X, 0.211 in Y. Rolling about X sends it end over end and it hangs
vertical. (A shove-it is the yaw term and was already right.)

---

## ⛔ THE LESSONS THAT COST THE MOST

**PORT THE REFERENCE, DO NOT RE-DERIVE IT.** I rewrote the pose applier from the `SkPose`
comments and reintroduced bugs the original — thirty lines further down the same file —
had already found and fixed: inverted knees, a T-pose, an elbow twisting in its socket
instead of bending. It also **resets every touched bone to REST first**, which stops the
pose carrying the underlying clip's asymmetry (its own note: no rotation mode could fix a
right hand reaching 3–4× further than the left, because the difference was never in the
rotation).

**THE THIGH AND THE KNEE DO NOT SHARE A SIGN.** Settled by A/B, not reasoning: thigh −1
stacks the rider over the board (+1 reclines them, feet ahead of the hips); knee +1 folds
(−1 hyperextends). Both overridable: `REVA_KNEESIGN`, `REVA_THIGHSIGN`, and `REVA_ARMMODE`
for the arm spread axis.

**A REST-POSE PRE-BEND PROBE WAS BUILT AND THROWN AWAY.** It detects knee direction from
the few degrees riggers author into a leg (`n = (knee−hip) × (ankle−knee)`), works on a
rig that has one (citizens: 4051 → 5810, dot positive) and **decides on noise for one that
does not** (reva2: 0.062 → 0.045, dot negative) — so it flipped the single body that was
already correct. Two rounds went into tuning against a metric that could not be trusted,
and the metric itself ("ankle behind knee along +Z") assumed a forward direction no
arbitrary rig owes you. **The bench's circular-verification law, learned again.**

---

## THE INSTRUMENT

`reva_native/anim_bench/ridecheck.py` — **four angles × N frames on one sheet.**

    python reva_native/anim_bench/ridecheck.py [--set rider] [--body r2moves] [--secs 4]

A skate pose lerps between rolling and carving and fires tricks on a timer, so any single
frame is one sample of something that is mostly not that; and a side view cannot show a
stance square to travel while a front view cannot show a knee bending backwards. Every
bug above was invisible from at least one of the two. `REVA_FIXEDSTEP` is what makes the
grid legible: same frame index = same instant across four separate runs, so a column reads
as one moment from four sides.

⛔ **`REVA_SHOT` is what ARMS a capture.** `SHOTN`/`SHOTEVERY` only say how many and how
often; without a start frame nothing is written and the run hangs forever. And the engine
**never exits once its quota is met**, so a harness must poll for its frames and stop it.

---

## STILL OWED

- The arms read posed rather than loose; the board separates from the feet at the apex.
- `v1_skateref` — a real Mixamo skateboarding capture found at
  `D:\free_3dmodels\objaverse_sketchfab_cc\gorilla\Skateboarding1__fca51c36.glb` (42
  joints, 65 frames @ 30fps, 2.17s) — bakes and loads but renders as fragments (96 meshes).
  **Retargeting real capture onto these bodies is probably a better answer than more
  hand-tuning**, and it is what jacob meant by "maybe you need to reference real skaters."
- The skate PARK itself is still broken; this pose lives on the actor path only. The same
  corrected application is what should drive it.

---

## 09-20 — THE STANCE FROM JACOB'S ANGLES, THE FILMER'S RIDE, THE BAIL THAT IS PHYSICS

*(The rider lives in AUTHORED clips now -- `skate_src/author/moves/z_sk_ride.py` (ride, push,
pop, kickflip, shove-it, manual, indy, method, and the filmer's ride) and `z_sk_board.py`
(carry, pickup, catch-up, drop-in, step-off, bail) -- baked onto each body with the board as a
channel of the clip (`@board`, ROBOT NEKO ep 10). The procedural rider above is the 08-29
actor-path ancestor. The skate promo -- four cuts, 09-19 → 09-20, `D:/reva902/robot_neko/promo/`
-- is the game's own look test: jacob's group is building a game about FILMING your friends
skate, so the camera is in a rider's hand more often than not.)*

⭐ **THE STANCE IS JUDGED FROM BEHIND.** jacob: "the knees look off" / "how they are standng on
the skateboard doesent seem to natural" / on the first fix, from behind: "knees are very very
bad" -- the legs bowed out. The cause, read off the script and not guessed: `RIDE_KNEEOUT` pushed
each knee's pole vector outward, and a foot's toe-out yaw ROTATES that leg's pole by the same
angle, so a 42° front foot plus a kneeout splayed both knees. Now: `RIDE_SINK` 0.26 (bent knees,
not soft ones), lean 24°, front foot 26° toe-out, back foot −2°, feet 0.34 / 0.36 u either side of
the board's centre, no kneeout. `scenes/rd_stance.txt` shoots it from behind, front, side and a
low three-quarter -- a side view cannot show splay, a rear view cannot show the bend.

⭐ **THE FILMER RIDES.** `z_rm_sk_film` is the ride with the right arm out and forward holding the
camera (breathing a little -- a held camera is never still -- far less than a balancing arm), the
balance arm working harder, and her look on the shot, not the road. `camhold=` puts the lens on
that hand (`design/scene_direction.md`). Every loop but the ride is gated first-frame-vs-last.

⭐ **A BAIL IS PHYSICS, NOT A CLIP.** `z_rm_sk_bail` plays the same way every time; `pdoll=<sec>`
hands the body to the ragdoll -- her own pose, the speed she was actually carrying (7 u/s
mid-run in ep 3), the real floor -- and she slides and settles instead of skating on her face
(`engine_capabilities.md` §22; the fixture is `scenes/rd_slam.txt`, and it is written the way a
test should be: THE ONE QUESTION, PASS, FAIL, CONTROL, PRECONDITIONS, then the take).

⛔ **A PATH WAS A SPLINE NOTHING CHECKED.** Every sample of a line is now pushed out of the set's
real geometry before the line is measured (`[path] slot N: M of K samples were inside the set`),
and the Undercroft's `room` scan -- sampled into the audit grid off a million triangles -- pushes
a line the same way it pushes a walk. Lines, not laps: `sets/theyard.set` is a 56 u run with a
bank, a gap and ledges and NO route; `sets/sb_session.set` weaves the London park's 11 × 18 u
island. jacob: "going in circles is kinda passe."
