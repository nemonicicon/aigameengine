"""texaudit.py -- what the baker GAVE each prop vs what UE says it USES.

bake_town.sh assigns sheets by filename pattern. ue_texmap.py asked UE. Where they
disagree, a prop is wearing the wrong half of somebody else's texture -- which is how
the whole town came out magenta once already.
"""
import os, re, subprocess, sys

T = "skate_src/town"
want = {}
for ln in open(os.path.join(T, "texmap.txt")):
    p = ln.split()
    if len(p) == 2:
        want[p[0]] = p[1]

# replay bake_town.sh's case statement
# NOTE: keep this in step with bake_town.sh's case statement, specific rules first.
DELIBERATE = {
    # UE lists these meshes' textures in dependency order, not material order, and a wall
    # module references brick AND two wallpapers -- none of which is the SIDING the kit
    # actually dresses a house with. bake_town.sh bakes the siding variants on purpose,
    # so these disagreements are a choice, not a bug.
    ("T_panels_01_white", "T_brickSmall_01_red"),
    ("T_PoliceCar_01", "T_colorPalette2048"),      # the cruiser DOES carry the police skin
}


def assigned(b):
    if b == "SM_floor_01": return "T_planks_01"
    if re.match(r"SM_road_sign_", b) or b == "SM_road_cone": return "T_colorPalette2048"
    if b == "SM_rock_01": return "T_roadSet"
    if b.startswith("SM_chimney_"): return "T_brickSmall_01_white"
    if b == "SM_tree_01": return "T_leaf_01a"
    if b in ("SM_tree_03", "SM_tree_04"): return "T_leaf_02"
    if b == "SM_treeLowPoly_01": return "T_colorPalette2048"
    if re.match(r"SM_(road|sidewalk|parcel|dirt)_", b) or b == "SM_grass_00": return "T_roadSet"
    if re.match(r"SM_(wall|wallDoor|wallGarageDoor|wallAttic|window)", b): return "T_panels_01_white"
    if re.match(r"SM_(tree|treeLowPoly|bush)_", b) or b == "SM_cornPlant": return "T_leaf_01"
    if b == "SM_grassVerticalSingle": return "T_grassAlpha"
    if b.startswith("SK_veh_PoliceCar"): return "T_PoliceCar_01"
    return "T_colorPalette2048"

glbs = sorted(x[:-4] for x in os.listdir(os.path.join(T, "export")) if x.endswith(".glb"))
bad = []
for b in glbs:
    a = assigned(b)
    w = want.get(b)
    if w and w != "-" and w != a and (a, w) not in DELIBERATE:
        bad.append((b, a, w))
print("%d baked props, %d disagree with UE (deliberate choices excluded)" % (len(glbs), len(bad)))
by = {}
for b, a, w in bad:
    by.setdefault((a, w), []).append(b)
for (a, w), names in sorted(by.items(), key=lambda kv: -len(kv[1])):
    print("\n  given %-22s but UE says %-26s  (%d)" % (a, w, len(names)))
    print("    " + ", ".join(names[:10]) + (" ..." if len(names) > 10 else ""))
