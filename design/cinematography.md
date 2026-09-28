# The camera has a grammar, and we were not speaking it

jacob dropped three books the way he dropped Richard Williams — no message, just the file.
Williams became `wmsApply` and a measurable animation layer. This is the same treatment for
the camera and the cut.

- **Mascelli**, *Five C's of Cinematography: Motion Picture Filming Techniques* (Silman-James)
- **Murch**, *In the Blink of an Eye: A Perspective on Film Editing*, 2nd ed. (Silman-James)
- **Katz**, *Film Directing: Shot by Shot* (Michael Wiese)

⛔ The books are copyrighted and are **not** in the repo. This file is the distilled rules
and where each one lands in the code.

---

## 1. Mascelli: establish before you close in

> *"Players are first shown in long shot, in relation to the setting; then in closer shots;
> and finally in close-ups, as they relate with each other, exchange dialogue."*

⭐ **This names the defect `design/406_qc_pass.md` had already found and could not explain** —
"six of seven takes were the conversation camera, so the whole film was two women in medium
shots." The QC pass had the symptom. Mascelli has the principle.

**Landed:** `skate_src/film/shotgrammar.py` audits every scene's `shot=` sequence — every
scene declares its framing per line, so the grammar is *checkable*. On first run it
convicted **8 of 12 scenes**, worst of all the cold open, which opened tight on a face and
never left, so the audience never once saw the bedroom.

⛔ **And the first version of the checker convicted GOOD grammar.** It counted every step
outward as wandering, so `the_spot` — `ots tight ots tight`, which is textbook
shot/reverse-shot coverage of an argument — came back with five faults. Alternating between
adjacent sizes is *how a dialogue is cut*. Only a jump of two ranks or a return to the
master counts now. An instrument that flags correct work is worse than no instrument.

⭐ **The cold open fix is the model for the rest.** It opens tight *on purpose* — a
nightmare is claustrophobic and the withhold is the scene. But the very next line is reva
saying *"You're in the house. It's Saturday. Nothing is wrong."* That line **is** the
establishing beat, so the camera now widens on the line that does the establishing, then
comes back in: `tight → wide → two → ots → tight`. A `# grammar:` note in the scene file
records the deliberate choice, so the checker excuses it instead of nagging.

### Also from Mascelli, not yet landed

- **Look/lead room.** *"A moving player should be given slightly more space in the direction
  in which he travels. Statically-positioned, he should be given slightly more space in the
  direction in which he looks."* Singles already do this (the `LOOKING ROOM` block in the
  conversation camera). **Moving actors do not** — walkers are framed dead-centre.
- **Head room**: too much reads bottom-heavy, too little reads crowded; and a player should
  never touch a frame edge. ⭐ **Landed for the TOP edge (2026-09-10).** The conversation
  lens's `two` and `wide` now MEASURE the frame they made: the top frame line at the pair's
  distance (eye height, pitch, the lens's own half-fov) must clear the higher eye mark plus
  `SCN_HEADTOP` (0.36). A two re-aims (closed form); a wide pulls back, keeping its feet.
  Found by `scenes/twelve`: reva2 beside anima lost her head in every two and both lost
  theirs in every wide. Logged as `[headroom] line N two|wide: ...` once per cut. Bottom
  and side edges are still unmeasured.
- **Cut on action**: the movement at the end of a shot is repeated at the head of the next,
  so the cut can land mid-move. Our A/B EDL cuts on **time**, which is why cuts feel
  arbitrary.

---

## 2. Murch: what a cut is actually for

The six criteria for a good cut, **in priority order**, and the instruction that makes them
usable — *sacrifice your way up, item by item, from the bottom*:

| # | criterion | do we honour it? |
|---|---|---|
| 1 | **Emotion** — how you want the audience to feel | not modelled at all |
| 2 | **Story** — does the cut advance it | the EDL is hand-authored, so: sometimes |
| 3 | **Rhythm** — is this moment "right" | no; cuts land on fixed times |
| 4 | **Eye-trace** — where the audience is looking in frame | no |
| 5 | **Planarity** — stage-line / screen direction | ⭐ **yes** — the eyeline rule |
| 6 | **3D continuity** — where people really are | ⭐ **yes** — staging drives the camera |

⛔ **We honour only the bottom two — the two Murch says to sacrifice FIRST.** The engine is
rigorous about exactly the things that matter least and silent on the four above them. That
is a fair description of why the film is technically clean and emotionally flat, and it is
the most useful sentence to come out of these three books.

The cheapest real win is **#3, rhythm**: `scenecheck.py` already measures motion energy per
take. Cutting on energy inflection instead of on a fixed timecode is Murch's rhythm
criterion, approximated, and it is a day of work rather than a rewrite.

---

## 3. Katz: coverage as a plan, not a reflex

*Shot by Shot* is the storyboard/coverage book — the one that says what shots you need
before you shoot, rather than which lens to point. Extracted but **not yet applied**; the
relevant part for us is master-scene coverage and the standard patterns for staging two and
three people, which is exactly what `design/scene_direction.md` improvises today.

---

## The order to attack this

1. ~~Shot grammar audit~~ ✅ `shotgrammar.py`, 8 of 12 convicted, cold open fixed.
2. Fix the six remaining scenes (`boarding`, `it_starts`, `three_lawns`, `the_spot`,
   `club_night`, `mouthtest`) — each either re-blocked or given a `# grammar:` reason.
3. Look/lead room for **moving** actors (Mascelli) — the b-camera work in act III.
4. Cut on rhythm rather than timecode (Murch #3) using `scenecheck.py`'s energy curve.
5. Cut on action (Mascelli) — needs the EDL to know where movements begin and end.

⛔ **And the honest framing.** The physics side of this project is measured to three
decimals; the camera side has been vibes. These three books are the first time the film
half has had a standard to fail against, and it failed on the first run in eight places.
That is the point of having one.
