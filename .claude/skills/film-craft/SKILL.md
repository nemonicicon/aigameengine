---
name: film-craft
description: The reference shelf and its rules — shot grammar, coverage, cutting, framing, and animation weight for SEVENTY-FOUR and any Firestrider scene. Use when blocking or re-blocking a scene, choosing shot sizes or camera angles, writing or editing an EDL, deciding where to cut, judging whether a take reads, staging two or three people, adding b-roll coverage, or when animation looks floaty, weightless or posed.
---

# The reference shelf

jacob drops books the way other people send links — no message, just the file. Richard
Williams became `wmsApply` and a measurable animation layer, so every book here gets the
same treatment: **distilled into rules, wired to an instrument, and applied to the film.**

⛔ **The books are copyrighted and are NOT in this repo.** They live in jacob's Downloads.
This skill is the distillation. Do not commit the PDFs.
⭐ **The physics papers ARE in the repo** — `design/papers/`, all free from the authors' own
pages, with `.txt` extractions next to them. `design/papers/README.md` says what each one
bought and, for one of them, why it is deliberately *not* applied.

Full write-up: **`design/cinematography.md`** (camera/cut) and **`design/animation_principles.md`**
(weight/timing). This file is the operational summary.

---

## Before touching a scene: run the audit

```bash
python skate_src/film/shotgrammar.py
```

Audits every `reva_native/scenes/*.txt` against Mascelli's grammar. It convicted **8 of 12**
on its first run. A scene may answer a fault instead of fixing it — put `# grammar: <reason>`
in the file and the checker records the choice as deliberate rather than nagging.

---

## Mascelli — *Five C's of Cinematography*

> *"Players are first shown in long shot, in relation to the setting; then in closer shots;
> and finally in close-ups, as they relate with each other, exchange dialogue."*

| rule | in practice here |
|---|---|
| **ESTABLISH** | A scene opens on its widest. Tell the audience WHERE before WHO. |
| **PROGRESS** | Sizes move inward: `wide → two → ots → tight`. |
| **VARIETY** | One size for a whole scene is a locked-off camera pointed at a conversation. |
| **LOOK/LEAD ROOM** | *"A moving player should be given slightly more space in the direction in which he travels. Statically-positioned, more space in the direction in which he looks."* |
| **HEAD ROOM** | Too much reads bottom-heavy; too little reads crowded. Never let a body touch a frame edge. |
| **CUT ON ACTION** | The movement at the end of a shot repeats at the head of the next, so the cut can land mid-move. |
| **ACTION AXIS** | Cross it and the audience loses which way people face. |

⭐ **The cold-open pattern — reach for this when a scene must open tight.** `coldopen.txt`
opens tight on a nightmare *on purpose*: the withhold is the scene. But reva's next line is
*"You're in the house. It's Saturday. Nothing is wrong."* — **that line IS the establishing
beat**, so the camera widens on the line that does the establishing, then comes back in.
`tight → wide → two → ots → tight`. Let the dialogue tell you where the establish goes.

## Murch — *In the Blink of an Eye*

Six criteria for a good cut, **in priority order**, plus the instruction that makes them
usable: *sacrifice your way up, item by item, from the bottom.*

| # | criterion | us |
|---|---|---|
| 1 | Emotion | ✗ not modelled |
| 2 | Story | ~ the EDL is hand-authored |
| 3 | Rhythm | ✗ cuts land on fixed times |
| 4 | Eye-trace | ✗ |
| 5 | Planarity (stage-line) | ✓ the eyeline rule |
| 6 | 3D continuity | ✓ staging drives the camera |

⛔ **We honour exactly the bottom two — the two he says to sacrifice FIRST.** The engine is
rigorous about what matters least and silent on everything above. Keep this table honest;
it is the best short description of why the film is clean and flat.

## Katz — *Film Directing: Shot by Shot*

Coverage as a plan rather than a reflex: master-scene method and the standard patterns for
staging two and three people — which `design/scene_direction.md` currently improvises.
Extracted, not yet applied.

## Williams — *The Animator's Survival Kit*

Already landed as the Williams layer (`wmsApply`): weight comes from the feet, and the body
drops as the legs pass. Verified by `anim_bench/weightcheck.py` — head-top travel 2px → 14px.

---

## Applying it

1. `python skate_src/film/shotgrammar.py` — see what is convicted.
2. Re-block the scene, or write `# grammar: <reason>`. **Both are valid**; an unexamined
   choice is not.
3. Shoot it: `python .claude/skills/run-reva/driver.py scene out.mp4 --name <scene> --set <set> --stage x,z,yaw`
4. Check coverage: `framecheck.py` (is the camera pointed at them, are they big enough),
   `scenecheck.py` (motion energy per take, dead stretches).
5. Judge a BODY in isolation, never in a wide two-shot:
   `python .claude/skills/run-reva/driver.py bench out.png --subject <body>`

## Gotchas

- ⛔ **An instrument that flags correct work is worse than none.** `shotgrammar.py`'s first
  version convicted `the_spot` for `ots/tight/ots/tight` — textbook shot/reverse-shot.
  Alternating between *adjacent* sizes is how a dialogue is cut; only a two-rank jump or a
  return to the master is a scene losing its place.
- ⛔ **`shot=` is per LINE and sticky** — a line without one inherits the last. That is why
  a scene can have twenty lines and two shot sizes.
- ⛔ **Judging a character in a two-shot is how six rounds of garment bugs survived.** Wide
  coverage is for judging *staging*, never a body, a face or a garment.
- ⛔ **Test fixtures are not scenes.** `mouthtest` exists to open a mouth; it should carry a
  `# grammar:` note rather than be re-blocked into a film scene.
- ⛔ **Do not add the books to the repo.** Distil them here.
