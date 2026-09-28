"""ue_texmap.py -- ask UE which texture each town mesh actually uses.

    UnrealEditor-Cmd.exe GunExport.uproject -run=pythonscript -script=ue_texmap.py
        -stdout -FullStdOutLogOutput -unattended -nosplash -nullrhi

WHY. bake_town.sh assigns textures by FILENAME PATTERN -- SM_road_* gets the road sheet,
SM_wall_* gets siding, everything else gets the shared colour palette. That is a guess,
and the guesses are visibly wrong: the mailbox Mrs. Parsons stands at came out PURPLE,
the pickups rainbow, house windows striped with swatch grids. UE knows the answer, so
ask it once and write the map to a file the baker reads.

⛔ HOW TO ASK. StaticMesh.materials is PROTECTED and cannot be read from Python (it
returns "Property 'Materials' ... is protected"), and the first version of this script
therefore reported 487 meshes with zero textures while exiting 0 -- a green run that
knew nothing. The asset registry's DEPENDENCY GRAPH answers it instead and works for
StaticMesh and SkeletalMesh alike: mesh package -> material package -> texture package,
two hops, filtered to Texture2D.

Output: skate_src/town/texmap.txt   -- "<meshname> <texturename>" per line
        skate_src/town/export/<texture>.png for every sheet referenced
"""
import unreal, os

OUT = r"C:\MyVeryOwnVideoGames\reva\skate_src\town\export"
MAP = r"C:\MyVeryOwnVideoGames\reva\skate_src\town\texmap.txt"
os.makedirs(OUT, exist_ok=True)
ROOT = "/Game/AssetsvilleTown/Meshes/"

AR = unreal.AssetRegistryHelpers.get_asset_registry()
OPT = unreal.AssetRegistryDependencyOptions(
    include_soft_package_references=True, include_hard_package_references=True,
    include_searchable_names=False, include_soft_management_references=False,
    include_hard_management_references=False)

_pkg_cache = {}


def deps(pkg):
    if pkg not in _pkg_cache:
        try:
            _pkg_cache[pkg] = [str(d) for d in (AR.get_dependencies(pkg, OPT) or [])]
        except Exception:
            _pkg_cache[pkg] = []
    return _pkg_cache[pkg]


def textures_under(pkg, depth=2):
    """Every Texture2D reachable from this package within `depth` hops."""
    found, seen, frontier = [], {pkg}, [pkg]
    for _ in range(depth):
        nxt = []
        for p in frontier:
            for d in deps(p):
                if d in seen:
                    continue
                seen.add(d)
                a = AR.get_assets_by_package_name(d)
                cls = ""
                if a:
                    try:
                        cls = str(a[0].asset_class_path.asset_name)
                    except Exception:
                        cls = str(getattr(a[0], "asset_class", ""))
                if cls == "Texture2D":
                    nm = d.rsplit("/", 1)[-1]
                    if nm not in found:
                        found.append(nm)
                        WANT[nm] = d
                else:
                    nxt.append(d)
        frontier = nxt
    return found


WANT = {}
rows = []
paths = unreal.EditorAssetLibrary.list_assets(ROOT, recursive=True, include_folder=False)
for p in sorted(paths):
    pkg = p.split(".")[0]
    a = AR.get_assets_by_package_name(pkg)
    if not a:
        continue
    try:
        cls = str(a[0].asset_class_path.asset_name)
    except Exception:
        cls = str(getattr(a[0], "asset_class", ""))
    if cls not in ("StaticMesh", "SkeletalMesh"):
        continue
    name = pkg.rsplit("/", 1)[-1]
    texs = textures_under(pkg)
    rows.append((name, texs))
    unreal.log("TEXMAP: %s -> %s" % (name, ",".join(texs) if texs else "(none)"))

with open(MAP, "w") as f:
    for name, texs in rows:
        f.write("%s %s\n" % (name, texs[0] if texs else "-"))
hit = sum(1 for _, t in rows if t)
unreal.log("TEXMAP: %d meshes, %d with a texture, %d distinct sheets"
           % (len(rows), hit, len(WANT)))

n = 0
for tn, pkg in sorted(WANT.items()):
    t = unreal.EditorAssetLibrary.load_asset(pkg + "." + tn)
    if t is None:
        continue
    task = unreal.AssetExportTask()
    task.set_editor_property("automated", True)
    task.set_editor_property("object", t)
    task.set_editor_property("exporter", unreal.TextureExporterPNG())
    task.set_editor_property("filename", os.path.join(OUT, tn + ".png"))
    task.set_editor_property("prompt", False)
    task.set_editor_property("replace_identical", True)
    if unreal.Exporter.run_asset_export_task(task):
        n += 1
unreal.log("TEXMAP DONE: %d textures exported" % n)
