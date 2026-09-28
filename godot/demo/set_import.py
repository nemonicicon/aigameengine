"""set_import.py -- the demo project's import settings for our moves (and Clanker's), written into the .import files.

    python godot/demo/set_import.py [--optimizer on|off] [--immutable keep|remove]

Per animation: the loop mode from ANIMATION_IMPORT.md's table. Per AnimationPlayer node: Godot's lossy key
optimizer (on by default in Godot 4; it lives on the node, `"PATH:AnimationPlayer"`). Per file: `animation/remove_immutable_tracks` -- ON by default, it strips every track
that never changes, i.e. the rest-pose tracks rpack2gltf.py writes so a move never inherits the last
move's pose. Measured with check_pose.gd / check_pose_compare.py before these defaults were changed.
"""
import argparse, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
LOOPS = {"z_rm_sk_ride", "z_rm_sk_film", "z_rm_sk_carry", "z_board_carry", "z_board_wobble", "z_board_steady",
         "z_board_push", "z_upright", "z_rm_skiphand", "z_rm_bed_back", "z_rm_bed_alone"}
SKATE = ["z_rm_sk_bail", "z_rm_sk_carry", "z_rm_sk_catchup", "z_rm_sk_dropin", "z_rm_sk_film", "z_rm_sk_grab",
         "z_rm_sk_kf", "z_rm_sk_manual", "z_rm_sk_method", "z_rm_sk_pop", "z_rm_sk_push", "z_rm_sk_ride",
         "z_rm_sk_shove", "z_rm_sk_stepoff"]
# paths from godot/ -- Clanker carries the same fourteen skate moves, baked onto his own skeleton
FILES = {"animations/reva_skate_moves.glb": SKATE,
         "characters/clanker/clanker.glb": SKATE,
         "animations/reva_moves.glb": ["z_armsup", "z_board_bail", "z_board_carry", "z_board_lift", "z_board_lower", "z_board_push",
                            "z_board_reach", "z_board_rise", "z_board_steady", "z_board_wobble", "z_rm_bed_alone",
                            "z_rm_bed_back", "z_rm_board_stepon", "z_rm_crawlcat", "z_rm_cuddleseat", "z_rm_curlsleep",
                            "z_rm_skiphand", "z_upright"]}

ap = argparse.ArgumentParser()
ap.add_argument("--optimizer", default="off", choices=["on", "off"])
ap.add_argument("--immutable", default="keep", choices=["keep", "remove"])
a = ap.parse_args()

for fn, anims in FILES.items():
    p = os.path.join(HERE, "..", fn + ".import")
    if not os.path.exists(p):
        print("%s: no .import yet -- open the project in Godot once (or godot --headless --path godot --import)" % fn)
        continue
    s = open(p, encoding="utf-8").read()
    s = re.sub(r"animation/remove_immutable_tracks=\w+", "animation/remove_immutable_tracks=%s"
               % ("true" if a.immutable == "remove" else "false"), s)
    ents = []
    for n in anims:
        ents.append('"%s": {\n"settings/loop_mode": %d\n}' % (n, 1 if n in LOOPS else 0))
    # ⛔ THE OPTIMIZER IS AN ANIMATIONPLAYER-NODE OPTION, NOT A PER-ANIMATION ONE. The first version wrote
    # "optimizer/enabled" into each animation's entry; Godot carries an unknown key along and never reads
    # it, so the optimizer stayed ON: Clanker's ride kept 40 of its 62 Hips keys (frame 23 gone) and
    # every joint sat 1.1 mm off at frame 15. The 2.7 mm residue reva's check left "not isolated" was
    # this -- and toggling the dead key could not move it, which is why it was ruled out.
    node = '"PATH:AnimationPlayer": {\n"optimizer/enabled": %s\n}' % ("true" if a.optimizer == "on" else "false")
    sub = '_subresources={\n"animations": {\n' + ",\n".join(ents) + '\n},\n"nodes": {\n' + node + "\n}\n}"
    s = re.sub(r"_subresources=\{.*?\}(?=\ngltf/|\Z)", lambda m: sub, s, flags=re.S)
    open(p, "w", encoding="utf-8", newline="\n").write(s)
    print("%s: optimizer %s, immutable tracks %s, %d loops" % (fn, a.optimizer, a.immutable,
                                                                sum(1 for n in anims if n in LOOPS)))
