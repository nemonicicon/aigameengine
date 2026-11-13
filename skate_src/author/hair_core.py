#!/usr/bin/env python3
"""hair_core.py -- HAIR THAT READS OPAQUE: a blended hair mesh gets an opaque core, drawn first.

    hair_core.py <pack.rpack> [--report] [--only name,name]

(11-05) In a take of two characters, both heads of hair read as see-through where they should be opaque. Every hair mesh of both packs is flagged
BLEND (mesh flag 8), and the engine draws a blended mesh after everything opaque, depth-tested but never writing depth (gfx_vk.c, P_SKIN_BLEND:
PF_DEPTH | PF_BLEND), its alpha its texture's: REVA's strands are 0.61 alpha at their median texel and only a fifth of them past 0.9, so she
was 40% see-through wherever she had hair, and no lock hid the one behind it.

The cure is the two-pass hair real-time renderers use: the same locks twice. A copy that is NOT blended is drawn with the opaque bodies: its
texture has holes, so the engine draws it as a CUTOUT (mesh.frag discards below alpha 0.5) and it writes depth -- solid hair, each lock hiding
what's behind it. The blended original then lays the soft ends over it: where the core is, it is depth-tested against it (LESS_OR_EQUAL, so it
blends the same colour over itself and changes nothing), and past the core's edge, in its thin, sub-half-alpha ends, it is the soft fringe
it always was. The copy is the same vertices, weights and texture, named <name>_core (before any ~tag, so a ~worn piece stays one), so it rides
every bone, hair chain and outfit its original does.
"""
import argparse, os, sys

HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from packbody import PackBody                                          # noqa: E402


def core_name(name):
    if name.startswith("wear~"):          # a wardrobe piece (REVA's hair, 11-06): the same piece and outfits, since the engine's tag stops at a '.'
        return name.partition(".")[0] + ".core"
    base, _, tag = name.partition("~")
    return base + "_core" + ("~" + tag if tag else "")


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("pack"); ap.add_argument("--report", action="store_true"); ap.add_argument("--only", default="")
    a = ap.parse_args(); only = [w for w in a.only.split(",") if w]
    B = PackBody(a.pack); names = [m["name"] for m in B.meshes]
    hair = [m for m in B.meshes if (m["rec"][5] & 8) and any(w in m["name"].lower() for w in ("hair", "braid")) and "_core" not in m["name"]
            and (not only or m["name"] in only)]
    todo = [m for m in hair if core_name(m["name"]) not in names]
    for m in hair: print("  %-28s %6d verts, blended%s" % (m["name"], m["verts"], "" if m in todo else " (its core is there)"))
    if a.report or not todo: return 0
    for m in todo: B.add_mesh(core_name(m["name"]), m, *B.arrays(m), tex=None, blend=False)
    B.write(); print("  %s: %s, opaque under their blended selves" % (os.path.basename(a.pack), ", ".join(core_name(m["name"]) for m in todo)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
