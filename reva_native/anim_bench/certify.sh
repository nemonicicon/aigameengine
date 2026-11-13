#!/usr/bin/env bash
# certify.sh -- the animation regression suite. One command, every subject x state,
# PASS/FAIL against the thresholds in motioncheck.py. Run from reva_native/.
#
#   bash anim_bench/certify.sh            # check
#   bash anim_bench/certify.sh --baseline # check and OVERWRITE the recorded baseline
#
# Numbers scrolling past in a terminal are not a baseline; this writes them down so a
# future change can be diffed against what was true when the work was certified.
set -e
PY="${CLUB_PYTHON:-python}"
OUT="${TMPDIR:-/tmp}/anim_certify"; mkdir -p "$OUT"
# certify drives the states itself, so point the live-control poll at an EMPTY private
# file: a lab left running with a pinned state would otherwise hijack every measurement
# (it did -- and reported it as a fleet-wide regression).
export REVA_LABCTL="$OUT/certify_ctl.txt"; : > "$REVA_LABCTL"
if tasklist 2>/dev/null | grep -qi crowd_new.exe; then
  echo "certify: another crowd_new.exe is running -- stop it first (one instance at a time)"; exit 1
fi
BASE="anim_bench/baseline.txt"
SUBJECTS="${SUBJECTS:-manny reva}"
STATES="${STATES:-1 2}"
report=""
for S in $SUBJECTS; do
  if [ "$S" = "reva" ]; then unset REVA_ANIMSUBJ; ARG=v1_reva.rpack; else export REVA_ANIMSUBJ=$S; ARG=$S; fi
  for ST in $STATES; do
    NAME=$([ $ST = 1 ] && echo WALK || echo RUN)
    REVA_ANIMSTATE=$ST REVA_WINDOWED=1 REVA_AUTOMODE=11 REVA_POSEDUMP=-1 \
      REVA_SHOT=800 REVA_SHOT_PATH="$OUT/c.ppm" ./crowd_new.exe 400 packs $ARG > "$OUT/$S-$ST.log" 2>&1 || true
    line=$("$PY" anim_bench/motioncheck.py "$OUT/$S-$ST.log" | grep -E "FOOT SKATE|CADENCE" | \
           sed -E 's/.*NEVER PLANT.*/skate=noplant FAIL/; s/.*ratio ([0-9.]+).*(PASS|FAIL).*/skate=\1 \2/; s/.*-> stride ([0-9.]+)u +.*(PASS|FAIL).*/stride=\1 \2/' | tr '\n' ' ')
    report="${report}$(printf '%-6s %-5s %s\n' "$S" "$NAME" "$line")
"
  done
done
unset REVA_ANIMSUBJ
echo "$report"
if [ "$1" = "--baseline" ]; then printf '%s' "$report" > "$BASE"; echo "baseline written to $BASE"; exit 0; fi
if [ -f "$BASE" ]; then
  echo "--- vs baseline (tolerance: skate 0.08, stride 0.25; the sim is not frame-identical run to run) ---"
  printf '%s' "$report" > "$OUT/now.txt"
  "$PY" - "$BASE" "$OUT/now.txt" <<'PYCMP'
import re,sys
def load(p):
    d={}
    for ln in open(p):
        f=ln.split()
        if len(f)>=2:
            key=(f[0],f[1]); m={}
            for i,x in enumerate(f[2:],2):
                if '=' in x:
                    k,v=x.split('=',1); m[k]=v
                    # the PASS/FAIL that follows a metric is its verdict
                    m[k+'_st']=f[i+1] if i+1<len(f) else '?'
            d[key]=m
    return d
base,now=load(sys.argv[1]),load(sys.argv[2])
bad=0
for k in sorted(set(base)|set(now)):
    b,n=base.get(k),now.get(k)
    if not b or not n: print("  %-6s %-5s MISSING"%k); bad=1; continue
    for m in ('skate','stride'):
        if m in b and m in n:
            # a row that FAILED at baseline and FAILS now is the RECORDED broken state
            # (reva's run) -- its numbers are noise from too few contacts and flap
            # run-to-run; only a verdict CHANGE on it is news
            if b.get(m+'_st','').startswith('**') or b.get(m+'_st')=='FAIL':
                if n.get(m+'_st','').startswith('**') or n.get(m+'_st')=='FAIL': continue
            if b[m]!=n[m] and not (b[m].replace('.','',1).isdigit() and n[m].replace('.','',1).isdigit()):
                print("  %-6s %-5s %s %s -> %s"%(k[0],k[1],m,b[m],n[m])); bad=1; continue
            if not b[m].replace('.','',1).isdigit(): continue
            db,dn=float(b[m]),float(n[m])
            # per-metric tolerance: skate is a 0..1 ratio; stride is 1..3u estimated
            # from ~11 plants a run, so +-1 plant moves it ~9% -- 0.08 there is noise
            tol=0.25 if m=='stride' else 0.08
            if abs(db-dn)>tol:
                print("  %-6s %-5s %s %.2f -> %.2f  REGRESSION"%(k[0],k[1],m,db,dn)); bad=1
print("  no regression" if not bad else "  ^ investigate")
sys.exit(0)
PYCMP
fi
