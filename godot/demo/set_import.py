"""set_import.py -- the project's import settings for every character and move file, written into the .import files.

    python godot/demo/set_import.py [--optimizer on|off] [--immutable keep|remove]

Every .glb under godot/animations/ and godot/characters/ is found by itself and its animation names are read out
of the file -- a new character needs no list edited here (ship_character.py relies on that).
Per animation: the loop mode (LOOPS below; ANIMATION_IMPORT.md's table). Per AnimationPlayer node: Godot's lossy key
optimizer (on by default in Godot 4; it lives on the node, `"PATH:AnimationPlayer"`). Per file:
`animation/remove_immutable_tracks` -- ON by default, it strips every track that never changes, i.e. the rest-pose
tracks rpack2gltf.py writes so a move never inherits the last move's pose. Measured with check_pose.gd /
check_pose_compare.py before these defaults were changed.
"""
import argparse, glob, json, os, re, struct

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
LOOPS = {"z_rm_sk_ride", "z_rm_sk_film", "z_rm_sk_carry", "z_board_carry", "z_board_wobble", "z_board_steady",
         "z_board_push", "z_upright", "z_rm_skiphand", "z_rm_bed_back", "z_rm_bed_alone"}


def animations_of(glb):
    b = open(glb, "rb").read()
    if b[:4] != b"glTF":
        return []
    j = json.loads(b[20:20 + struct.unpack_from("<I", b, 12)[0]])
    return [a.get("name", "") for a in j.get("animations", []) if a.get("name")]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--optimizer", default="off", choices=["on", "off"])
    ap.add_argument("--immutable", default="keep", choices=["keep", "remove"])
    a = ap.parse_args()
    files = sorted(glob.glob(os.path.join(ROOT, "animations", "*.glb")) +
                   glob.glob(os.path.join(ROOT, "characters", "**", "*.glb"), recursive=True))
    for glb in files:
        rel = os.path.relpath(glb, ROOT).replace("\\", "/")
        p = glb + ".import"
        if not os.path.exists(p):
            print("%s: no .import yet -- open the project in Godot once (or godot --headless --path godot --import)" % rel)
            continue
        anims = animations_of(glb)
        s = open(p, encoding="utf-8").read()
        s = re.sub(r"animation/remove_immutable_tracks=\w+", "animation/remove_immutable_tracks=%s"
                   % ("true" if a.immutable == "remove" else "false"), s)
        ents = ['"%s": {\n"settings/loop_mode": %d\n}' % (n, 1 if n in LOOPS else 0) for n in anims]
        # ⛔ THE OPTIMIZER IS AN ANIMATIONPLAYER-NODE OPTION, NOT A PER-ANIMATION ONE. The first version wrote
        # "optimizer/enabled" into each animation's entry; Godot carries an unknown key along and never reads
        # it, so the optimizer stayed ON: Clanker's ride kept 40 of its 62 Hips keys (frame 23 gone) and
        # every joint sat 1.1 mm off at frame 15. The 2.7 mm residue reva's check left "not isolated" was
        # this -- and toggling the dead key could not move it, which is why it was ruled out.
        node = '"PATH:AnimationPlayer": {\n"optimizer/enabled": %s\n}' % ("true" if a.optimizer == "on" else "false")
        sub = '_subresources={\n"animations": {\n' + ",\n".join(ents) + '\n},\n"nodes": {\n' + node + "\n}\n}"
        s = re.sub(r"_subresources=\{.*?\}(?=\ngltf/|\Z)", lambda m: sub, s, flags=re.S)
        open(p, "w", encoding="utf-8", newline="\n").write(s)
        print("%s: %d animations, optimizer %s, immutable tracks %s, %d loops" % (
            rel, len(anims), a.optimizer, a.immutable, sum(1 for n in anims if n in LOOPS)))


if __name__ == "__main__":
    main()
