#!/usr/bin/env python3
"""citymap.py -- THE CITY COOK: OpenStreetMap and USGS one-metre elevation into the engine's streaming-world cells (engine/stream.inc).

usage: citymap.py CONFIG --raw DIR --out DIR [--godot DIR [--name Seattle]]

CONFIG   a game's map, as JSON: which files, the origin, the box, the style, the marks and the views. Nothing here knows which city.
--raw    where the config's data files are (the config names them relative to it). Or CITYMAP_RAW.
--out    the game's folder: it writes props/<prefix>_II_JJ.rpack (a cell each), props/<prefix>_far.rpack (the horizon),
         sets/<prefix>.set (the cells, the marks), <prefix>_cells.txt (each cell's triangles and bytes) and <prefix>_report.txt.

The data are downloads: run it with python -I from somewhere that isn't their folder. README.md beside this says what it does and doesn't."""
import argparse
import json
import os
import site
import sys

site.addsitedir(site.getusersitepackages())   # python -I leaves the user's numpy and Pillow out
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding="utf-8")

import cook   # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("config")
    ap.add_argument("--raw", default=os.environ.get("CITYMAP_RAW"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--godot", help="(11-11) also write the city for a Godot game into this folder: SM_<name>.glb and SM_<name>.park.json (godot.py). Or CITYMAP_GODOT")
    ap.add_argument("--name", default="Seattle", help="the Godot files' name (SM_<name>.glb). Or CITYMAP_GODOT_NAME")
    a = ap.parse_args()
    if not a.raw:
        ap.error("say where the raw data are: --raw DIR or CITYMAP_RAW")
    if a.godot:
        os.environ["CITYMAP_GODOT"] = os.path.abspath(a.godot)
        os.environ.setdefault("CITYMAP_GODOT_NAME", a.name)
    cfg = json.load(open(a.config, encoding="utf-8"))
    os.makedirs(a.out, exist_ok=True)
    cook.Cook(cfg, a.raw, a.out).run()
    return 0


if __name__ == "__main__":
    sys.exit(main())
