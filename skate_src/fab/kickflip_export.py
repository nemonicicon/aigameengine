"""KICKFLIP (Fab: "Kickflip", Dude.fbx) -> GLB with his textures and fifteen clean clips.

ROBOT NEKO ep 9. jacob: "kicflip is a character. add him to the episode" -- and, just as firmly,
"dont use his animation for reva or kaeli". He plays his OWN bank and nobody else's.

MEASURED FIRST (see README): the FBX carries 35 armature actions that collapse to 20 distinct
motions; this keeps the 15 that are real body motion, under neutral names a scene script can carry
(the compiler splits a line on its FIRST '|', so 'KLIENT.ARMATURE.001|Kickflip' cannot survive a
script) that avoid every role-miner substring (walk/run/idle/stand).
Every one of them is ON THE BOARD, and the board (Plane.001) is skinned to his skeleton -- there is
no animation of him on the ground at all. That is the episode.

The FBX resolves neither texture, so they are wired in here from OPAQUE copies: the engine discards
texels with alpha under 0.4 (Luzer.UV.png had 20% of them, Skate.png 25%).

usage:
  ffmpeg -i <fab>/textures/Luzer.UV.png -pix_fmt rgb24 <out>/Luzer_rgb.png
  ffmpeg -i <fab>/textures/Skate.png    -pix_fmt rgb24 <out>/Skate_rgb.png
  blender --background --factory-startup --python-exit-code 1 --python kickflip_export.py -- <out>
  python skate_src/glb2rpack_anim.py --keepnames <out>/kickflip_tex.glb reva_native/packs/v1_dude.rpack
  python skate_src/author/sliceclip.py dude kf_seg8 kf_idle --from 32 --to 43 --pingpong --force
"""
import bpy, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(ROOT, "models", "VaultCache", "FabLibrary", "Kickflip-4347bca6", "fbx",
                   "kickflip_extracted", "source", "Dude.fbx")
OUT = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else os.path.join(HERE, "_out")
TEX_BODY = os.path.join(OUT, "Luzer_rgb.png")
TEX_BOARD = os.path.join(OUT, "Skate_rgb.png")

KEEP = {
    # in-place push loops on the board (a kick-push, NOT a walk), three speeds
    "KLIENT.ARMATURE.001|WalkCycle.002": "kf_push37",
    "KLIENT.ARMATURE.001|WalkCycle.001": "kf_push48",
    "KLIENT.ARMATURE.001|WalkCycle": "kf_push60",
    "KLIENT.ARMATURE.001|WalkCycle.004": "kf_push178",
    # the one real kickflip loop (its nine identical copies are dropped)
    "KLIENT.ARMATURE.001|KLIENT.ARMATURE.001|Kickflip": "kf_trick",
    # the 668-frame showreel -- the twin whose hip MOVES
    "KLIENT.ARMATURE.001|KLIENT.ARMATURE.001|Action": "kf_reel",
    # the trick cut into segments; kf_seg3 and kf_seg8 end standing upright on the deck
    "KLIENT.ARMATURE.001|Kickflip": "kf_seg0",
    "KLIENT.ARMATURE.001|Kickflip.001": "kf_seg1",
    "KLIENT.ARMATURE.001|Kickflip.002": "kf_seg2",
    "KLIENT.ARMATURE.001|Kickflip.003": "kf_seg3",
    "KLIENT.ARMATURE.001|Kickflip.004": "kf_seg4",
    "KLIENT.ARMATURE.001|Kickflip.005": "kf_seg5",
    "KLIENT.ARMATURE.001|Kickflip.006": "kf_seg6",
    "KLIENT.ARMATURE.001|Kickflip.007": "kf_seg7",
    "KLIENT.ARMATURE.001|Kickflip.008": "kf_seg8",
}

for p in (SRC, TEX_BODY, TEX_BOARD):
    assert os.path.exists(p), "missing: %s" % p

bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
bpy.ops.import_scene.fbx(filepath=SRC)
arm = next(o for o in sc.objects if o.type == 'ARMATURE')
body = sc.objects.get('KLIENT.001')
board = sc.objects.get('Plane.001')
ground = sc.objects.get('Plane')                 # a 4-vert ground square, not part of him
if ground:
    bpy.data.objects.remove(ground, do_unlink=True)

missing = [k for k in KEEP if k not in bpy.data.actions]
assert not missing, "the Fab file changed -- actions not found: %s" % missing
for a in list(bpy.data.actions):
    if a.name not in KEEP:
        bpy.data.actions.remove(a)
for a in list(bpy.data.actions):
    a.name = KEEP[a.name]


def wire_texture(mat, path):
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = next((n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'), None)
    if bsdf is None:
        bsdf = nt.nodes.new('ShaderNodeBsdfPrincipled')
        out = next((n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL'), None) or nt.nodes.new('ShaderNodeOutputMaterial')
        nt.links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])
    tex = nt.nodes.new('ShaderNodeTexImage')
    tex.image = bpy.data.images.load(path)
    nt.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    print("wired %s -> %s (%dx%d)" % (os.path.basename(path), mat.name, tex.image.size[0], tex.image.size[1]))


if body.data.materials and body.data.materials[0]:
    wire_texture(body.data.materials[0], TEX_BODY)
else:
    m = bpy.data.materials.new("Luzer"); body.data.materials.append(m); wire_texture(m, TEX_BODY)
mb = bpy.data.materials.new("Skate")
board.data.materials.clear(); board.data.materials.append(mb)
wire_texture(mb, TEX_BOARD)

bpy.ops.object.select_all(action='DESELECT')
for o in (arm, body, board):
    o.select_set(True)
bpy.context.view_layer.objects.active = arm
glb = os.path.join(OUT, "kickflip_tex.glb")
bpy.ops.export_scene.gltf(filepath=glb, export_format='GLB', use_selection=True,
                          export_animations=True, export_animation_mode='ACTIONS',
                          export_force_sampling=True, export_skins=True, export_apply=False,
                          export_image_format='AUTO')
print("exported %s  %d bytes, %d clips" % (glb, os.path.getsize(glb), len(bpy.data.actions)))
