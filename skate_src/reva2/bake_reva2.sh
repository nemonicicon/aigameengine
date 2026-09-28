#!/usr/bin/env bash
# bake_reva2.sh -- the REVA re-ingest, source statue to club body. Run from repo root.
#
#   bash skate_src/reva2/bake_reva2.sh
#
# Chain: rig_reva.py (Blender headless: 100k tris, the 4K base colour with emission
# screen-blended, a MEASURED landmark armature, heat weights solved on a voxel proxy and
# transferred) -> reva2.glb -> glb2rpack_anim -> packs/v1_reva2.rpack -> idle_reva2.py
# (her authored stand, mined as her idle role). Then bake_moves.sh's REVA2 section copies
# it to v1_r2moves and the face chain runs on that (face_marks/calib2marks, mouth_dark,
# jaw, mouth_interior -- see design/faces.md).
#
# The source is jacob's Meshy zip, Meshy_AI_REVA_Holographic_Neon_0629075151_image-to-3d-
# texture_fbx.zip, unzipped to D:\reva902\reva_meshy (the default); override with
# REVA_SRC=<dir holding the Meshy_AI_REVA_..._fbx folder>. REVA_TRIS / REVA_TEX /
# REVA_VOXEL / REVA_OUT / REVA_PREVIEW=<dir> (rig overlay renders) are the knobs.
#
# Rebuilt 2026-09-10: the first recipe hung her arm joints under her arms (shoulders that
# hunch into balls), dragged her head joint back with her ponytail (every borrowed clip put
# her chin on her chest), and cut the texture to 1024. rig_reva.py's docstring has the
# measurements.
set -e -o pipefail
PY="${CLUB_PYTHON:-python}"
BL="/c/Program Files/Blender Foundation/Blender 5.1/blender.exe"

# ⛔ --python-exit-code 1: without it Blender exits 0 on a Python traceback, and the grep
# below prints nothing alarming.
"$BL" --background --factory-startup --python-exit-code 1 --python skate_src/reva2/rig_reva.py 2>&1 \
    | grep -E "midline|facing|arm:|neck column|Neck |proxy|DECIMATED|emission|material|armature:|orphan|density|bound|EXPORTED|Error|Traceback|assert"

"$PY" skate_src/glb2rpack_anim.py --keepnames skate_src/reva2/reva2.glb \
    reva_native/packs/v1_reva2.rpack 2>&1 | tail -4

"$PY" skate_src/reva2/idle_reva2.py
"$PY" skate_src/author/rpack.py info reva_native/packs/v1_reva2.rpack
