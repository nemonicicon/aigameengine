# THE CHOREOGRAPHY — making the dancing worth filming

*(written 2026-08-27. jacob, watching Miku at Coachella against our wall: "our dancing
leaves alot to be desired." Then, on NicoNico Music Party 2015: "i think these are
real holograms." Both observations are queue.)*

## The diagnosis, with the code receipt

The club's dancers CANNOT be on the beat except by luck. The dance path samples clips
at `tsec * clip->fps + phase` (crowd_kms.c, slot dance sampling) — wall-clock at the
clip's native rate — while `beatPLL()` runs two hundred lines away feeding the FLOOR
shader and the camera's bar-cuts. The floor pulses on the beat, the camera cuts on the
beat, the dancers ignore it structurally. On top: random clip cycling every 14–36s,
random per-slot phase (right for a milling crowd, wrong for a show), and mocap that
was captured as mild social dancing, not staged choreography.

What a Vocaloid stage number has that we lack, in layers: BEAT-LOCKED playback ·
CHOREOGRAPHY (8-count phrases with HITS — poses that snap onto a beat, hold, release —
builds, choruses with variation) vs. our shuffled playlist · UNISON (ensemble locked
together or in deliberate canon) vs. our deliberate de-phasing · AMPLITUDE
(silhouette-first, oversized, reads from the back row).

Everything the homecoming built serves this: chains sell hair whips, DQS holds arms
overhead, morphs breathe between phrases, the bar-aware camera is already cutting,
waiting for dancers worth cutting to.

## The hologram truth (and why it flatters us)

Miku/NicoNico "holograms" are FLAT RENDERS on transparent rear-projection screens
(DILAD panels; some Western shows use Pepper's-ghost foil). No volume. The read comes
from stagecraft: darkness makes the screen vanish; one privileged viewpoint plus
distance hides the parallax; the stage lights are timed to look like they light her;
her rim is authored hot so she reads as a light source; the choreography is
silhouette-first because only silhouettes survive the trick.

reva in our engine is MORE of a hologram than Miku has ever been — volumetric,
correct from every angle, with the fresnel/scanline shader simulating the aesthetic
while owning the geometry the projection fakes. The show, not the hologram, was
always the product.

## The campaign

- **C1 — THE BEAT CONTRACT.** Dance clips play beat-locked: measure each clip's
  native tempo offline (pose-energy autocorrelation), play at `live_bpm / clip_bpm`
  with phase pinned to the bar (the gait-sync trick applied to dance). Gate:
  pose-velocity minima land within ±1 frame of beats, measured from the pose dump
  against the beat log. The old vocabulary reads twice as good the day this lands.
- **C2 — THE FIRST AUTHORED PHRASE.** An 8-count with two hits, authored oversized,
  silhouette-gated and beat-gated; reva performs it on the wall. Hits are the new
  primitive: snap onto the beat, hold, release.
- **C3 — VOCABULARY + STRUCTURE.** hit/hold/step as reusable score primitives; phrase
  chaining; verse/chorus with variation; UNISON mode for the showcase (the
  crowd-clocks-out law inverted: the show puts everyone ON the clock).
- **C4 — THE SHOW (the Vocaloid stagecraft, stolen honestly).** Dark-stage discipline
  during numbers (she is the light source) · beams and floor washes firing on the
  choreography's HITS (the light rig already lives on the beat clock) · a cheap
  planar floor reflection under the performer (half the concert-hologram look by
  itself) · **THE PENLIGHT SEA**: the crowd silhouetted, glowsticks in hand, swaying
  in unison color waves on the beat — the single most Vocaloid image there is, and
  our crowd already breathes on the clock · camera bar-cuts synced to phrase
  boundaries · facing and travel marks so she performs TO the audience.

## Asset repairs surfaced by looking at dancers

- miko's feet: a constant ~100° ankle-axis error in ALL 18 dance clips (mocap
  transplant era), fixed by `fixankles.py` — clip-space corrective conjugated
  per-frame through the parent. THE LAW IT COST: measure the pose that RENDERS, not
  the pose that rests (the first fix operated on her never-rendered rest pose).
- suspects awaiting a DANCE-TIME census (the rest-pose metric convicts wrong tissue):
  the chibi, the ninjatō fighter, possibly the rabbit's ears-vs-feet anatomy.
