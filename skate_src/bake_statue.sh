#!/usr/bin/env bash
# bake_statue.sh -- a GLB statue to a standing club body, NO BLENDER (homecoming H7).
#
#   bash skate_src/bake_statue.sh <statue.glb> <name> [tris]
#
# Chain: statue2body rig (anatomy-asserted facing, measured landmarks, 22 mixamo
# joints) -> autoweights (visibility-gated binding) -> decimate_glb (welded QEM) ->
# glb2rpack -> z_upright + authored idle. FBX is refused at the door: every source
# this artery accepts exports GLB itself.
set -e -o pipefail
PY="${CLUB_PYTHON:-python}"
SRC="$1"; NAME="$2"; TRIS="${3:-16000}"
case "$SRC" in *.fbx|*.FBX) echo "FBX refused: export GLB from the source instead"; exit 1;; esac
T="${TMPDIR:-/tmp}/statue_$NAME"
mkdir -p "$T"
"$PY" skate_src/statue2body.py rig "$SRC" "$T/rigged.glb"
"$PY" skate_src/author/autoweights.py "$T/rigged.glb" "$T/bound.glb"
"$PY" skate_src/author/decimate_glb.py "$T/bound.glb" "$T/body.glb" --tris "$TRIS"
"$PY" skate_src/glb2rpack_anim.py --keepnames "$T/body.glb" "reva_native/packs/v1_$NAME.rpack"
"$PY" skate_src/author/moves/z_upright.py "reva_native/packs/v1_$NAME.rpack"
"$PY" skate_src/reva2/idle_reva2.py "reva_native/packs/v1_$NAME.rpack"
"$PY" skate_src/author/rpack.py info "reva_native/packs/v1_$NAME.rpack" | tail -4
