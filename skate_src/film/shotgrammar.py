#!/usr/bin/env python3
"""shotgrammar.py -- does each scene obey the grammar, or just point a lens at people?

    python skate_src/film/shotgrammar.py [--fix-hints]

WHY THIS EXISTS. jacob dropped Mascelli's *The Five C's of Cinematography* the same way he
dropped Richard Williams, and Williams became a measurable animation layer. This is the
equivalent for the camera. The QC pass (design/406_qc_pass.md) had already found the
symptom -- "six of seven takes were the conversation camera, so the whole film was two
women in medium shots" -- but had no principle to name it. Mascelli does, in one sentence:

    "Players are first shown in long shot, in relation to the setting; then in closer
     shots; and finally in close-ups, as they relate with each other, exchange dialogue."
                                                    -- Mascelli, THE FIVE C's, Continuity

That is the grammar of a scene, and it is checkable, because every scene in
reva_native/scenes/ declares its framing per line with `shot=`.

THE RULES CHECKED, all of them from the book:

  ESTABLISH   A scene opens on its widest, not its tightest. The audience is told WHERE
              before it is told WHO. Opening tight is the single worst offender because the
              viewer spends the whole scene reconstructing the room instead of watching it.

  PROGRESS    Sizes move INWARD as the scene tightens: wide -> medium -> two -> ots -> tight.
              (`medium` -- ROBOT NEKO ep 4 -- is the hips-up two-shot: both bodies, the frame's
              bottom edge at hip/thigh height, so hands and whatever is in them are large.) Going
              outward mid-scene is legal (a reveal, a character leaving) but going outward
              because the scene never established is the same bug wearing a different hat.

  VARIETY     A scene with one shot size is a locked-off camera pointed at a conversation.
              Mascelli's whole Cutting chapter is about the alternative.

  ⛔ NOT A LINTER TO BE OBEYED BLINDLY. A cold open CAN legitimately start tight -- on an
  eye, on a hand -- as a deliberate withhold. The checker's job is to make the choice
  conscious: every violation must be either fixed or answered. Which is what
  `# grammar: <reason>` in the scene file is for.
"""
import argparse
import glob
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SCENES = os.path.join(ROOT, "reva_native", "scenes")

# widest to tightest. This ordering IS the grammar.
SIZE = {"wide": 0, "medium": 1, "two": 2, "ots": 3, "tight": 4, "solo": 3}
NAME = {0: "wide", 1: "medium", 2: "two", 3: "ots/solo", 4: "tight"}


def audit(path):
    txt = open(path, encoding="utf-8", errors="replace").read()
    shots = re.findall(r"shot=([a-z]+)", txt)
    excused = "# grammar:" in txt
    reason = ""
    if excused:
        reason = txt.split("# grammar:", 1)[1].splitlines()[0].strip()
    faults = []
    if not shots:
        return shots, ["no shot= direction at all -- every line is whatever the last one was"], reason
    ranks = [SIZE.get(s, 1) for s in shots]

    # ESTABLISH: the opening shot should be the widest the scene ever uses
    if ranks[0] > min(ranks):
        faults.append("opens on %s but goes as wide as %s later -- establish FIRST"
                      % (shots[0], NAME[min(ranks)]))
    if ranks[0] >= 4:
        faults.append("opens TIGHT: the audience is told who before it is told where")

    # VARIETY
    if len(set(ranks)) == 1:
        faults.append("one size for the whole scene (%s x%d) -- a locked-off camera"
                      % (shots[0], len(shots)))

    # PROGRESS. ⛔ AND THE FIRST VERSION OF THIS CONVICTED GOOD GRAMMAR. It counted every
    # step outward, so the_spot -- ots/tight/ots/tight, which is textbook SHOT/REVERSE-SHOT
    # coverage of an argument -- came back as "wanders: 5 moves". Alternating between
    # adjacent sizes is how a dialogue is cut. Only a JUMP back out (two ranks or more, or
    # all the way to a master) is the scene losing its place.
    # (the ladder grew a rung in ep 4, so "all the way back to a master" is now x >= 3 --
    #  ots or tighter -- which is the same sentence it always was.)
    out = sum(1 for x, y in zip(ranks, ranks[1:]) if y < x - 1 or (y == 0 and x >= 3))
    if out > 1:
        faults.append("wanders: %d big jumps back out across %d shots" % (out, len(shots)))
    return shots, faults, reason


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenes", default=SCENES)
    a = ap.parse_args()
    files = sorted(glob.glob(os.path.join(a.scenes, "*.txt")))
    bad = 0
    print("%-14s %-34s %s" % ("scene", "shot sizes, in order", "verdict"))
    print("-" * 96)
    for f in files:
        nm = os.path.splitext(os.path.basename(f))[0]
        shots, faults, reason = audit(f)
        seq = " ".join(shots)[:33]
        if not faults:
            print("%-14s %-34s ok" % (nm, seq))
            continue
        if reason:
            print("%-14s %-34s EXCUSED: %s" % (nm, seq, reason))
            continue
        bad += 1
        print("%-14s %-34s %s" % (nm, seq, faults[0]))
        for extra in faults[1:]:
            print("%-14s %-34s %s" % ("", "", extra))
    print("-" * 96)
    print("%d scene(s), %d breaking the grammar" % (len(files), bad))
    print("Mascelli: long shot in relation to the setting, THEN closer, THEN close-ups.")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
