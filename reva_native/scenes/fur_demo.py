"""fur_demo.py -- fur on animals, four coats side by side (queue step 23, 11-04: fur's physics matter for clothing and for animals,
and it takes several animals to show the different furs).

What it makes, from four of Quaternius's animated animals (CC0 1.0, poly.pizza/u/Quaternius; no credit is owed and one is given here):
  props/animal_shiba.rpack    a short plush coat        Shiba_Inu__y4wdQpg767.glb
  props/animal_husky.rpack    a thick double coat       Husky__wcWiuEqwzq.glb
  props/animal_fox.rpack      long and fluffy           Fox__Bc97C66HKi.glb
  props/animal_alpaca.rpack   wool                      Alpaca__bCVFD48i2l.glb
  sets/_fur_demo.set          the four standing in a wind that gusts, and two more running laps so the coat trails and settles

Each is baked at its real height and marked true size (glb2rpack_anim.py --height), so a dog isn't stood up to a person's. The files colour their parts by material and carry no
texture; the baker keeps each colour in the mesh's name. Then each coat is said in the pack's material chunk (skate_src/author/material.py): fur, how long in metres, how fine, how curly.

  python fur_demo.py <the folder with the four .glb files>
  python .claude/skills/run-reva/driver.py looks out.png --new --subject reva902 --set _fur_demo --env REVA_TIER=high --cams "row=2.8,1.5,7.2,2.8,0.9,0,40"
REVA_GFX=fur=0 is the control: the same animals with no coat drawn.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.path.join(HERE, "..")
SK = os.path.join(HERE, "..", "..", "skate_src")
sys.path.insert(0, os.path.join(SK, "author"))
import material

# stem, source file, metres tall at rest, and its coat: rough, how curly, how fine, how long in metres
ANIMALS = [
    ("shiba", "Shiba_Inu__y4wdQpg767.glb", 0.50, ["ShibaInu_Main*"], (0.60, 0.00, 0.85, 0.016)),
    ("husky", "Husky__wcWiuEqwzq.glb", 0.72, ["Cube_Material#*", "Cube_Material_001#*"], (0.60, 0.05, 0.70, 0.045)),
    ("fox", "Fox__Bc97C66HKi.glb", 0.48, ["Fox_Main*", "Fox_Grey*"], (0.55, 0.10, 0.80, 0.050)),
    ("alpaca", "Alpaca__bCVFD48i2l.glb", 1.45, ["Alpaca_Main*"], (0.75, 0.90, 0.35, 0.080)),
]
# an eye nobody named as one: the husky's parts are Material, Material.001 and so on
EYES = {"husky": ["Cube_Material_002#*", "Cube_Material_003#*"]}

SET = """# _fur_demo -- made by scenes/fur_demo.py: fur on animals, four coats side by side, each on its own clips
sun 40 60 1.3
sky 0.40 0.46 0.58
wind 1.6 0.0 0.5 0.6
prop floor_01    0.00  0.00   0.00   0  1.6   0.62 0.66 0.60
prop floor_01    7.00  0.00   0.00   0  1.6   0.62 0.66 0.60
prop floor_01    0.00  0.00   7.00   0  1.6   0.62 0.66 0.60
prop floor_01    7.00  0.00   7.00   0  1.6   0.62 0.66 0.60
# standing, in the wind
actor animal_shiba   Idle   0.0 0 0.0   20 1.0
actor animal_husky   Idle   1.6 0 0.0   20 1.0
actor animal_fox     Idle   3.4 0 0.0   20 1.0
actor animal_alpaca  Idle   5.6 0 0.0   20 1.0
# running laps, at the speed each clip's own paws go back at (1.83 and 1.31 units a second): the coat trails, and at the lap's end it's a cut
actor animal_husky   Gallop  -1.0 0 2.6   90 1.0 1.0 1.83 9.0
actor animal_shiba   Gallop  -1.0 0 3.8   90 1.0 1.0 1.31 9.0
"""


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    src = sys.argv[1]
    for stem, glb, height, coat, fur in ANIMALS:
        dst = os.path.join(NATIVE, "props", "animal_%s.rpack" % stem)
        r = subprocess.run([sys.executable, os.path.join(SK, "glb2rpack_anim.py"), os.path.join(src, glb), dst, "--keepnames", "--height", str(height), "--fresh"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
        if r.returncode != 0:
            sys.exit("%s: the bake failed\n%s" % (glb, r.stdout[-1500:]))
        sets = {pat: (fur[0], fur[1], fur[2], fur[3], material.KINDS["fur"]) for pat in coat}
        for pat in EYES.get(stem, []):
            sets[pat] = (0.20, 0.0, 0.0, 0.0, material.KINDS["eye"])
        n = material.set_materials(dst, sets)
        print("props/animal_%s.rpack: %.2f m tall, %d parts said (its coat %.1f cm long, fineness %.2f, curl %.2f)" % (stem, height, n, fur[3] * 100.0, fur[2], fur[1]))
    with open(os.path.join(NATIVE, "sets", "_fur_demo.set"), "w", newline="\n") as o:
        o.write(SET)
    print("wrote sets/_fur_demo.set")


if __name__ == "__main__":
    main()
