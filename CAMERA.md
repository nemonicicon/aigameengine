# THE CAMERA — every lens the club has, and the rule each one cost

All of it lives in `reva_native/crowd_kms.c`. A scene script line (`reva_native/scenes/<name>.txt`,
manual: `design/scene_direction.md`) or a live control line (`anim_bench/lab_ctl.txt`, written by
`driver.py lab`) sets these keys; `camDirector()` (crowd_kms.c ~12815) decides each frame which lens
wins. The skate look-test rule behind most of it: **every shot is a camera that exists in the place** —
a phone in somebody's hand, or a tripod somebody left standing.

## The lenses

| key | what it is | code |
|---|---|---|
| `shot=wide/medium/two/ots/tight/solo` | the CONVERSATION LENS: a shot ladder solved from the bodies' published eye, hand and hip marks. `medium` = mid-thigh (a cowboy), `solo` = the listener's point of view, `ots` over the shoulder. `focus=` picks whose shot, `hgt=` the angle, `reverse=1` crosses the line on purpose (and crosses the key light with it) | `SHOT_NAME`/`SHOT_FOV` ~6800, the ladder in `camDirector` |
| head room | a guarantee, not a hope: the frame grows to clear the eyes AND anything a body raises (a hand over the head) — `[headroom]` in the log says what it solved from | ~13189 |
| `step=12` + the tripod | stop motion: bodies are exposed on twelves (a clock), the lens stays smooth. Under a stepped exposure the lens is on a TRIPOD — it settles 0.45 s after a cut and then holds, and eases out (`[tripod] … easing out`) only when the frame a shot needs has grown | ~13363 |
| `cam=ex,ey,ez,ax,ay,az[,fov]` | a pinned lens: a tripod. Held until `cam=off` | `camDirector` |
| `cammove=…` + `camsecs=` | the pinned lens eases to a second pose over the line: a crane, a dolly, a push-in | `g_camMove*` ~4228 |
| `camorbit=<slot>,r,h,deg0,deg1[,fov[,aimh]]` | circle a body from heading deg0 to deg1 (the heading the lens LOOKS); deg0 = deg1 is a fixed-angle follow (the half-pipe lens) | `camTrackTick` ~8301 |
| `camtrack=<slot>,dist,height,azim,fov[,aimh]` | a lens that FOLLOWS a body (azim 0 behind, 90 its front) | `camTrackTick` ~8301 |
| `campan=<slot>[,aimh]` | a pinned eye whose AIM follows a body — the tripod that has to hold a body wherever it lands | `camTrackTick` |
| `camshake=<amp>[,hz]` | hand-held on top of any placed lens: smooth two-octave noise per channel ("white noise would be a vibrating tripod, not a person holding it") | `camShakeApply` ~8379 |
| `camhold=filmer,subject[,fov[,aimh[,drop]]]` | THE PHONE: the eye rides the filmer's wrist (the contact pass's published forearm), the filmer's body is not drawn, only the AIM is sprung (9 rad/s); `drop` is the low pass | `camHandAt` ~10543, `camSpring` ~8281, `g_camHoldSlot` ~2544 |
| `feed=1\|2\|0` | the phone's frame (portrait / sideways) or a tripod: the post pass squeezes the frame, adds fall-off, sensor noise, a REC dot and a seven-segment timer drawn in the shader, after the tone knee | post shader ~2570-2610 |
| `fish=0..1` | an equidistant fisheye as a PROJECTION (r = f·θ) over a rectilinear render whose field opens toward 120° diagonal under it | `fishFov` ~2551, `g_fish` ~2542 |
| `V` / `fpv=` | first person | |
| the F310 chase cam | gameplay follow cam (the pad-driven modes) | `camDirector` — ⛔ it fires before the shot switch whenever the pad is driving, and has eaten three game lenses; gate a new mode camera there first |
| `post= bloom= knee= fxaa= fog= tone=` | the post pass: `tone=0.66` is a highlight shoulder toward a 0.96 white point (clipped pixels 23.1% → 0.0% on a noon street) | `postInit/postBegin/postEnd` ~2581-2725 |

## The rules they cost

- ⛔ **THE BOB IS THE SHOT.** Springing a hand-held eye smooths away the very thing that reads as footage
  (the first test came back as "a locked-off frame"). Spring the aim, never the eye.
- ⛔ **A fisheye is a WIDE lens as well as a bent one.** Warping at the authored field of view moved the
  frame six degrees and looked like nothing; open the 3D pass's field so the corners have geometry to bend.
- ⛔ **A filmer trails by ~3 units and stays on ONE side of the run**; at a foot behind, a fisheye reads as
  a knee, and a line that crosses the run rides the two bodies through each other.
- ⛔ **A tracking lens knows nothing about what is between it and its subject** — occlusion awareness is
  NOT built. On a loop, keep it inside, under the radius.
- ⛔ **`cam=` beats the scene lens** (the director returns early on it): a framing test under a pinned lens
  tests the pin. `cam=off` on every framing test.
- ⛔ **Test a lens change on a short fixture before paying for a take** (`scenes/cam_test`, `feed_test`,
  `feed_park`, `pipe_test`), and judge a new lens against its neighbours on the ladder — the first `medium`
  landed on the frame the `two` already held.
- ⛔ **Under stop motion, fast action under a moving lens is shot on ONES** (`step=60`): on twelves the pose
  jumps every fifth frame while the lens moves every frame, which reads as judder and double images.
- **Sun and lens are chosen together**: a reverse lit from behind comes back flat and hot; `reverse=1`
  crosses the key light with the lens.

Fixtures that exercise the lenses: `reva_native/scenes/cam_test` (the moving lenses), `feed_test` (the
phone against a tracking control at the same distance), `feed_park`, `pipe_test` (orbits round a
half-pipe), `rd_slam` (lenses holding a ragdoll). They need characters and a set of your own to run.
