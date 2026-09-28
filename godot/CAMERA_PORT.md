# PORTING THE CLUB'S CAMERAS TO GODOT 4 — a brief for a person or an AI

The club engine's camera rigs, specified exactly from `reva_native/crowd_kms.c` (line numbers below), with
the Godot 4 equivalent of each, an implementation (`club_camera.gd`, `club_camera_post.gdshader`) and a
definition of done.

**Verified in Godot 4.7.2** (the `demo/` project in this folder — open `godot/` in Godot and press F5): every
rig recorded with Movie Maker at a fixed 60 fps and measured by `demo/score_rigs.py` — the tripod never
moved (0 m); with shake its motion was smooth (frame-to-frame change 0.16 of its speed, 0.15 on the re-run
with Clanker riding); track, pan and the hand-held phone kept the rider in frame 360 of 360 frames (reva's
stick figure first, then Clanker, the demo's rider now -- `rider=reva` brings her back); the orbit reached its end heading exactly at t =
4.0 s (0.00 deg); the dolly move landed on its end pose (0.0000 m); the fisheye opened a 44 deg lens to
60-64 deg; the hand-held eye's frame-to-frame motion changed 3.6x as much as the tracking lens's (the bob).
Two bugs the Godot run found and fixed: `E` is a Godot shader built-in (the seven-segment flags collided),
and the club's own phone chrome drew its timer digits 2.5x too narrow (now square in both orientations).

**Units.** The club's world is ~1.75x metric ("club units", heads at y 2.6–3.1). Every distance below is
given as `club u (≈ metres)`; the metre figure is club ÷ 1.75. Angles in degrees, rates in rad/s.
**The rule behind all of it:** every shot is a camera that EXISTS in the place — a phone in someone's hand
or a tripod someone left standing. The lens moves on its own smooth clock even when the bodies step.

## 1 · The shared parts

**The spring** (`camSpring`, crowd_kms.c 8281) — the exact step of a critically damped spring
`x'' = w²(xt − x) − 2w·x'`, not an approximation, so it is frame-rate independent:
```
y = x - xt;  e = exp(-w*dt);  j = v + w*y
x = xt + (y + j*dt) * e
v = (v - w*j*dt) * e
```
Rates: **6 rad/s** for a dolly/track lens (`CAMTRK_W`), **9 rad/s** for a hand-held camera's AIM
(`CAMHOLD_W`: quicker than a dolly, still late), **20 rad/s** for the height pre-filter (`CAMTRK_WY`,
measured: at 6 the aim trailed a rolling rider by 1.83 u, at 20 by 0.79). Clamp dt to [0, 0.25].

**The lead.** A spring trails a steady speed by `2v/w`; the lens aims AHEAD by exactly that
(`ff = speed * 2 / w` along the body's travel, and the same for its vertical speed), so a critically
damped lens lands ON a moving subject instead of behind it.

**The ease** (`lensEase`, 8298): smoothstep `t*t*(3-2t)`, t clamped to [0,1].

## 2 · The rigs

| rig | club key | behaviour (exact) | Godot 4 |
|---|---|---|---|
| **tripod** | `cam=ex,ey,ez,ax,ay,az[,fov]` | eye and aim pinned until `cam=off` | a `Camera3D` placed and `look_at`; nothing moves |
| **move** (dolly / crane / push-in) | `cammove=…` + `camsecs=` | eye, aim and fov eased from the line's `cam=` to the target over `camsecs` with `lensEase`; NO spring — the ease is the smoothing (8308) | tween the three with an ease-in-out (or `lensEase` by hand) |
| **track** | `camtrack=slot,dist,height,azim,fov[,aimh]` | body at (x,y,z) heading `tr` (radians; forward = (sin tr, 0, cos tr)), speed `vel`. `ff = vel*2/6`. Height `y` through the 20 rad/s spring → `yF`, `fy = yV*2/6`. **aim** = (x + sin(tr)·ff, y + fy + aimh, z + cos(tr)·ff). **eye**: a = tr + azim(°→rad); eye = (x − sin(a)·dist + sin(tr)·ff, y + fy + height (or `height` absolute when given as `=h`), z − cos(a)·dist + cos(tr)·ff). Eye and aim each through the 6 rad/s spring. azim 0 = behind, 90 = the body's front (8327) | a script on `Camera3D` reading the target's global transform and velocity each `_process` |
| **pan** | `campan=slot[,aimh]` | the eye stays pinned (the tripod); only the aim follows (sprung) — the tripod that has to hold a body wherever it lands (8355) | same script, eye fixed |
| **orbit** | `camorbit=slot,r,h,deg0,deg1[,fov[,aimh]]` | the CENTRE is the sprung aim (as track); the circle is exact: heading `hd = lerp(deg0, deg1, lensEase(t/T))`; eye = (C.x − sin(hd)·r, C.y − aimh + h (or `=h` absolute), C.z − cos(hd)·r). Heading is where the lens LOOKS: 0 = on the body's −z side looking +z. deg0 = deg1 is a world-fixed follow (the half-pipe lens: a pipe rider reverses at every wall, so a travel-relative lens would whip round) (8356) | same script |
| **hand-held (the phone)** | `camhold=filmer,subject[,fov[,aimh[,drop]]]` | **hp** = the filmer's grip WRIST (fallback: body pos + 0.25 u along heading, +1.45 u up). **to** = unit(aim − hp). eye = hp + to·0.20 u (the phone sits in front of the fist, toward the subject) + 0.22 u OUTWARD horizontally, away from the filmer's own chest (unit((hp − filmer body).xz)) + (0.04 − drop) u up. ⛔ **THE BOB IS THE SHOT: the EYE is raw, never sprung** — only the AIM goes through the 9 rad/s spring. The filmer's own body is not drawn (8329–8353) | a `BoneAttachment3D` on the filmer's hand bone; the camera script reads its global position as `hp`; hide the filmer's `MeshInstance3D` for this camera (a render layer the camera's `cull_mask` excludes) |
| **shake** | `camshake=amp[,hz]` | on top of ANY placed lens: t = time·hz (default 1.5 Hz); per channel k (0–2 eye, 3–5 aim): `n = (noise1(t,k) + 0.5·noise1(2.13t + 17, k+6)) / 1.5`; eye += n·amp, aim += n·amp/3. `noise1` is value noise with a QUINTIC fade: `u = u³(u(6u − 15) + 10)`, lerp of hashed lattice values in [−1,1]. White noise reads as a vibrating tripod, not a hand (8366–8385). Never written back into the pinned lens | `FastNoiseLite` (TYPE_VALUE, or TYPE_SIMPLEX_SMOOTH), two octaves, one seed per channel — or port `lensHash` / `lensNoise1` exactly (in `club_camera.gd`) |

**The conversation lens** (`shot=wide/medium/two/ots/tight/solo`, `camDirector` ~12815) is solved each
frame from the bodies' published eye, hand and hip marks — a larger port. Its two rules worth taking:
- **Head room is a guarantee**: the frame's top edge at the subjects must clear the highest EYE by
  0.36 u (`SCN_HEADTOP`, ≈ 0.21 m) and any RAISED HAND by 0.30 u (`SCN_HANDTOP`, ≈ 0.17 m); if it does not,
  a two-shot re-aims and every other shot backs the lens off ×1.06 per step until it does (13163–13200).
  Field-of-view per shot (vertical, degrees): ots 30, tight 22, two 30, wide 40, solo 38, medium 34.
- **The stop-motion tripod** (13355–13375): when the bodies are stepped (a stop-motion exposure clock) the
  lens settles for 0.45 s after a cut (eye and aim chase their targets with `k = 1 − exp(−dt/0.20)`), then
  LOCKS while nobody travels (both body speeds < 0.15 u/s). It lets go and eases out only when the frame the
  shot needs has GROWN by more than 0.08 u (a hand went up), and never tightens back in on its own.

## 3 · The lens and the phone's frame (the post pass)

`club_camera_post.gdshader` is the club's post fragment shader (crowd_kms.c 2592–2680) for a
`ColorRect` covering the viewport:

- **The fisheye is a projection, not a polynomial**: radius on the frame = angle off the axis (`r = f·θ`),
  so the source is sampled at `tan(θ)`. ⛔ **A fisheye is a WIDE lens as well as a bent one**: warping at
  the authored field moved it six degrees and looked like nothing. `fishFov` (2551) opens the 3D camera's
  field: authored diagonal half-angle `th = atan(d·tan(fov/2))` with `d = sqrt(1 + aspect²)`; target
  `tgt = th + (max(1.05, th) − th)·fish` (1.05 rad = 60° off axis = 120° diagonal), capped at 1.45 rad;
  the camera renders at `fov' = 2·atan(tan(tgt)/d)` (≤ 170°) and the shader gets `tht = tgt`. In Godot:
  set `Camera3D.fov = fov'` (Godot's fov is vertical with the default `keep_aspect`, as here).
- **The phone's frame** (`feed` 1 = portrait 9:16, 2 = sideways 16:9): the picture is pillarboxed to the
  phone's aspect, the corners fall off to 0.84 (smoothstep 0.42→0.80 on radius), sensor noise ±0.011
  that moves every frame, a red REC dot blinking (on 65% of each second) and a seven-segment `M:SS` timer
  drawn in the shader, top-left of HER screen — all AFTER the tone curve, because a camera's own frame is
  not part of the scene's exposure.
- The club's **tone knee** (a luminance shoulder toward a 0.96 white point, `tone` = where it starts),
  bloom and FXAA are Godot's `Environment` (tonemap, glow) and `Project Settings > Rendering > Anti
  Aliasing > FXAA`. The shader keeps the knee as an option (`tone` uniform, 0 = off) for a like-for-like
  comparison.

## 4 · What NOT to lose (each of these cost a take)

- ⛔ Spring the phone's AIM, never its EYE. A sprung eye came back as "a locked-off frame".
- ⛔ The filmer trails ~3 u (≈ 1.7 m) and stays on ONE side of the subject's line; at a foot behind, a
  fisheye reads as a knee, and a line that crosses the run rides the two bodies through each other.
- ⛔ Aim height: a rider ~1.1 u (≈ 0.63 m) above the body origin, a body lying on the ground 0.55 u; at
  0.95 through a fisheye the skater's head was cut off on the kickflip.
- ⛔ No lens here knows what is between it and its subject: a tracking lens went into a pillar. Godot has
  what the club lacks — a `RayCast3D` / `SpringArm3D` from the aim to the eye — use it.
- ⛔ Fast action under a moving lens must not be stepped: on a 12 fps exposure the pose jumps every 5th
  frame while the lens moves every frame, which reads as judder and double images.

## 5 · Definition of done — test it the way the club does

Record with Godot's **Movie Maker** mode (fixed frame rate, deterministic: `--write-movie out.avi
--fixed-fps 60`, or Project Settings > Editor > Movie Writer), convert to mp4 (`ffmpeg -i out.avi out.mp4`),
then check with the kit's instruments:

1. **The phone bobs, the aim lags.** One scene, two takes, same subject path: the phone rig vs a track rig
   at the same distance. `skate_src/robot_neko/mp4steps.py` on both: the phone's background changes more
   frame to frame (the bob) while the subject stays in frame; the track rig's is smooth. If they read the
   same, the eye is being sprung.
2. **A tripod holds.** A pinned lens over a still scene: `mp4steps.py` reads ~0.00 between frames.
   Add `camshake=0.015`: small, SMOOTH changes, never a flicker between neighbours.
3. **The orbit arrives on time.** deg0→deg1 over T seconds: at t = T the lens is at deg1 exactly (the circle
   is exact; only the centre is sprung).
4. **The fisheye is wide.** At fish = 0.6 the frame sees visibly more than at 0, straight lines bend toward
   the corners, and the centre of frame keeps its scale.
5. **Look at full frames, not numbers alone** — a contact sheet and a full frame can disagree; the full frame
   wins (`skate_src/robot_neko/scenesheet.py` makes sheets from a timeline).
