"""ue_dump_materials.py -- print Anima's material -> texture bindings, authoritatively.

⭐ WHY: her FBX carries 38 material slots and the recovered texture set has 37 PNGs, and
MOST line up by name (Body -> T_Body_Diffuse). About ten do NOT: Eyelets, Sole, BucklesGlossy,
PearlL/PearlR, SilverL/SilverR, CorneaL, Scalp, Straps, EyebrowsSecondary. Guessing those is
exactly how a body ends up wearing the wrong map -- the same class of mistake as the SSS map
landing on Character Creator skin, which took a rebuild to notice.

The UE project holds the MATERIAL INSTANCES themselves (MI_Body, MI_CorneaL, MI_Pearl...),
named after those very slots, and an MI records the texture it samples. So the mapping is a
fact we can READ rather than a similarity we infer.

Also dumps each skeletal mesh's slot order, because the slot NAME is what Blender matches on.
"""
import unreal

OUT = r"C:\MyVeryOwnVideoGames\reva\models\_out\anima_tex\MATERIALS.txt"
ROOT = "/Game/Anima/Characters"

lines = []


def emit(s):
    lines.append(s)
    unreal.log("ANIMAMAT: " + s)


def tex_params(mi):
    """Texture bindings on a material instance: its own overrides first, then whatever the
    parent material exposes and it inherits."""
    got = {}
    try:
        for p in mi.texture_parameter_values:
            n = str(p.parameter_info.name)
            v = p.parameter_value
            if v:
                got[n] = str(v.get_name())
    except Exception as e:
        emit("   (override read failed: %s)" % e)
    try:
        names = unreal.MaterialEditingLibrary.get_texture_parameter_names(mi.get_base_material())
        for n in names:
            n = str(n)
            if n in got:
                continue
            v = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mi, n)
            if v:
                got[n] = str(v.get_name())
    except Exception:
        pass
    return got


def main():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()

    emit("=== MATERIAL INSTANCES ===")
    for a in sorted(ar.get_assets_by_path(ROOT + "/Materials", recursive=True),
                    key=lambda x: str(x.asset_name)):
        name = str(a.asset_name)
        obj = unreal.EditorAssetLibrary.load_asset("%s.%s" % (str(a.package_name), name))
        if not isinstance(obj, unreal.MaterialInstance):
            continue
        par = obj.get_base_material()
        emit("%-24s parent=%s" % (name, par.get_name() if par else "?"))
        for k, v in sorted(tex_params(obj).items()):
            emit("      %-22s = %s" % (k, v))

    emit("")
    emit("=== SKELETAL MESH SLOTS ===")
    for sub in ("Meshes_HiPoly", "Meshes_LowPoly"):
        for a in sorted(ar.get_assets_by_path(ROOT + "/" + sub, recursive=True),
                        key=lambda x: str(x.asset_name)):
            obj = unreal.EditorAssetLibrary.load_asset(
                "%s.%s" % (str(a.package_name), str(a.asset_name)))
            if not isinstance(obj, unreal.SkeletalMesh):
                continue
            slots = []
            for m in obj.materials:
                mat = m.material_interface
                slots.append("%s->%s" % (str(m.material_slot_name),
                                         mat.get_name() if mat else "None"))
            emit("%-16s %-28s %s" % (sub, str(a.asset_name), ", ".join(slots)))

    with open(OUT, "w") as f:
        f.write("\n".join(lines))
    emit("wrote " + OUT)


main()
