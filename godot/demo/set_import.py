"""set_import.py -- the demo project's import settings for our moves, written into the .import files.

    python godot/demo/set_import.py [--optimizer on|off] [--immutable keep|remove]

Per animation: the loop mode from ANIMATION_IMPORT.md's table, and Godot's lossy key optimizer (on by
default in Godot 4). Per file: `animation/remove_immutable_tracks` -- ON by default, it strips every track
that never changes, i.e. the rest-pose tracks rpack2gltf.py writes so a move never inherits the last
move's pose. Measured with check_pose.gd / check_pose_compare.py before these defaults were changed.
"""
import argparse, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
LOOPS = {"z_rm_sk_ride", "z_rm_sk_film", "z_rm_sk_carry", "z_board_carry", "z_board_wobble", "z_board_steady",
         "z_board_push", "z_upright", "z_rm_skiphand", "z_rm_bed_back", "z_rm_bed_alone"}
FILES = {"reva_skate_moves.glb": ["z_rm_sk_bail", "z_rm_sk_carry", "z_rm_sk_catchup", "z_rm_sk_dropin", "z_rm_sk_film",
                                  "z_rm_sk_grab", "z_rm_sk_kf", "z_rm_sk_manual", "z_rm_sk_method", "z_rm_sk_pop",
                                  "z_rm_sk_push", "z_rm_sk_ride", "z_rm_sk_shove", "z_rm_sk_stepoff"],
         "reva_moves.glb": ["z_armsup", "z_board_bail", "z_board_carry", "z_board_lift", "z_board_lower", "z_board_push",
                            "z_board_reach", "z_board_rise", "z_board_steady", "z_board_wobble", "z_rm_bed_alone",
                            "z_rm_bed_back", "z_rm_board_stepon", "z_rm_crawlcat", "z_rm_cuddleseat", "z_rm_curlsleep",
                            "z_rm_skiphand", "z_upright"]}

ap = argparse.ArgumentParser()
ap.add_argument("--optimizer", default="off", choices=["on", "off"])
ap.add_argument("--immutable", default="keep", choices=["keep", "remove"])
a = ap.parse_args()

for fn, anims in FILES.items():
    p = os.path.join(HERE, "..", "animations", fn + ".import")
    s = open(p, encoding="utf-8").read()
    s = re.sub(r"animation/remove_immutable_tracks=\w+", "animation/remove_immutable_tracks=%s"
               % ("true" if a.immutable == "remove" else "false"), s)
    ents = []
    for n in anims:
        ents.append('"%s": {\n"optimizer/enabled": %s,\n"settings/loop_mode": %d\n}' % (
            n, "true" if a.optimizer == "on" else "false", 1 if n in LOOPS else 0))
    sub = '_subresources={\n"animations": {\n' + ",\n".join(ents) + "\n}\n}"
    s = re.sub(r"_subresources=\{.*?\}(?=\ngltf/|\Z)", lambda m: sub, s, flags=re.S)
    open(p, "w", encoding="utf-8", newline="\n").write(s)
    print("%s: optimizer %s, immutable tracks %s, %d loops" % (fn, a.optimizer, a.immutable,
                                                                sum(1 for n in anims if n in LOOPS)))
