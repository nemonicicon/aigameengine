#!/usr/bin/env bash
# standsweep.sh -- put N subjects in the lab, one at a time, and measure the stand.
# Run from reva_native/.
#
#   bash anim_bench/standsweep.sh                      # the ten-human roster, IDLE
#   SUBJECTS="manny reva" STATE=3 bash anim_bench/standsweep.sh
#
# Each subject boots the lab alone, holds the state for ~700 frames while the 11-joint
# pose dump runs, and exits on a 9-pane multi-cam frame so there is a picture next to
# every number. posturecheck.py turns the dump into one row of a table.
#
# A subject named in SUBJECTS is resolved as packs/v1_<name>.rpack; anything containing
# a dot is taken as a path (that is how the root-level packs -- reva, kaeli -- get in).
set -e
PY="${CLUB_PYTHON:-python}"
OUT="${STANDOUT:-$PWD/anim_bench/_sweep}"; mkdir -p "$OUT"
STATE="${STATE:-0}"
FRAMES="${FRAMES:-700}"
GRID="${GRID:-9}"

if tasklist 2>/dev/null | grep -qi crowd_new.exe; then
  echo "standsweep: another crowd_new.exe is running -- stop it first (one instance at a time)"
  exit 1
fi

# THE TEN. Six carry an idle clip of their own; four have none and must be supplied by
# the donor bank or a fallback -- which is the half of the roster worth measuring.
SUBJECTS="${SUBJECTS:-manny crowd_bgirl crowd_susan crowd_spy crowd_doctor crowd_thailand Character_01 Character_Female_05 Character_17_Police v1_reva.rpack}"

TSV="$OUT/table.tsv"; : > "$TSV"
for S in $SUBJECTS; do
  case "$S" in *.rpack) ARG="$S"; SUB="$S";; *) ARG="$S"; SUB="$S";; esac
  TAG=$(echo "$S" | sed 's/\.rpack$//; s/^v1_//')
  # a PRIVATE control file per subject: a lab left running elsewhere would otherwise
  # drive these runs (it has, and the result was read as a fleet-wide regression)
  CTL="$OUT/ctl_$TAG.txt"
  printf 'seq=1 state=%s grid=%s\n' "$STATE" "$GRID" > "$CTL"
  REVA_LABCTL="$(cygpath -w "$CTL")" \
  REVA_ANIMSUBJ="$SUB" REVA_ANIMSTATE="$STATE" REVA_WINDOWED=1 REVA_AUTOMODE=11 \
  REVA_POSEDUMP=-1 REVA_SHOT="$FRAMES" REVA_SHOT_PATH="$(cygpath -w "$OUT/$TAG.ppm")" \
    ./crowd_new.exe 400 packs "$ARG" > "$OUT/$TAG.log" 2>&1 || true
  "$PY" anim_bench/posturecheck.py "$OUT/$TAG.log" --label "$TAG" --tsv >> "$TSV" || true
  printf '  %-22s %s\n' "$TAG" "$(tail -1 "$TSV" | cut -f19,20)"
done

echo
"$PY" - "$TSV" <<'PYEOF'
import sys
HDR = ["GROUND","DOUBLE","PELVIS","SHLDR","LEAN","BAL","STANCE","KNEE-L","KNEE-R",
       "K-ASYM","ARM-L","ARM-R","TORS","BONE","LIVE","POPS","DRIFT"]
rows = [ln.rstrip("\n").split("\t") for ln in open(sys.argv[1]) if ln.strip()]
w = max((len(r[0]) for r in rows), default=8)
print("STAND TABLE".ljust(w), " ".join(h.rjust(7) for h in HDR), "  defects")
for r in rows:
    print(r[0].ljust(w), " ".join(("%7.3f" % float(x)) for x in r[1:18]),
          "  " + (r[19] if len(r) > 19 and r[19] else "-"))
PYEOF
echo
echo "logs + 9-pane frames in $OUT"
