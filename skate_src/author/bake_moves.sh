#!/usr/bin/env bash
# bake_moves.sh -- score -> v1_moves.rpack, the authored-motion pack. Run from repo root.
#
#   bash skate_src/author/bake_moves.sh
#
# v1_moves.rpack is a COPY OF MANNY'S PACK with the z_ clips appended: it carries the
# reference skeleton, so until P5 the lab subject IS this pack and playback is direct --
# no donor transfer between the authoring and the measurement (the P0 audit finding: a
# donor-only moves pack would have made every authored number secretly measure the
# retarget, which is not due until P5).
#
# rpacks are gitignored (recipes in, artifacts out); this script and the scores in
# moves/ are the record. Append-only: nodes/meshes/textures stay byte-identical, which
# is what makes the lab's reload=1 hot swap valid.
set -e
PY="${CLUB_PYTHON:-python}"
SRC="reva_native/packs/v1_manny.rpack"
DST="reva_native/packs/v1_moves.rpack"

cp -f "$SRC" "$DST"
echo "== moves pack: fresh copy of manny ($(stat -c%s "$DST")B) =="

# every score in moves/ appends its clip(s); each score lints its own names
shopt -s nullglob
for S in skate_src/author/moves/z_*.py; do
    "$PY" "$S" "$DST" && echo "  $(basename "$S" .py)"
done

"$PY" skate_src/author/rpack.py info "$DST" | tail -5

# P5b: KAELI'S OWN MOVES -- the same scores re-solved on her rig, contacts exact by
# construction. Scores that cannot resolve on her (missing bones) report and skip.
KDST="reva_native/packs/v1_kmoves.rpack"
cp -f reva_native/v1_kaeli.rpack "$KDST"
echo "== kaeli moves pack: fresh copy of kaeli =="
for S in skate_src/author/moves/z_*.py; do
    if "$PY" "$S" "$KDST" >/dev/null 2>&1; then echo "  $(basename "$S" .py) (kaeli)"
    else echo "  $(basename "$S" .py) SKIPPED on kaeli"; fi
done
"$PY" skate_src/author/rpack.py info "$KDST" | tail -3

# REVA2: the re-ingested body (skate_src/reva2/bake_reva2.sh). Clean metres end to
# end, so unlike kaeli's unit-chimera pack her copy can carry AUTHORED ROOT MOTION.
RDST="reva_native/packs/v1_r2moves.rpack"
if [ -f reva_native/packs/v1_reva2.rpack ]; then
    cp -f reva_native/packs/v1_reva2.rpack "$RDST"
    echo "== reva2 moves pack: fresh copy of reva2 =="
    for S in skate_src/author/moves/z_*.py; do
        if "$PY" "$S" "$RDST" >/dev/null 2>&1; then echo "  $(basename "$S" .py) (reva2)"
        else echo "  $(basename "$S" .py) SKIPPED on reva2"; fi
    done
    "$PY" skate_src/author/rpack.py info "$RDST" | tail -3
fi
