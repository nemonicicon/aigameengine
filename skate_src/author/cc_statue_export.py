"""cc_statue_export.py -- export a Character Creator .blend as a club-ready STATUE glb.

Run INSIDE Blender:

    blender -b "<file>.blend" --python skate_src/author/cc_statue_export.py -- \\
        --out models/_out/camilia_statue.glb --keep Body,Bra,Underwear_Bottoms,Teeth,Eye,Tongue

⭐ WHY THIS EXISTS: HER SKIN CAME OUT GREY. A plain glTF export of a Character Creator body
writes the SUBSURFACE MAP as base colour --

    Skin_Head -> Std_Skin_Head_SSSMap        Skin_Arm -> Std_Skin_Arm_SSSMap
    Skin_Body -> Std_Skin_Body_SSSMap        Skin_Leg -> Std_Skin_Leg_SSSMap

-- while the bra and underwear come out right (`Bra_Diffuse`, `Underwear_Bottoms_Diffuse`).
An SSS map is a desaturated greyish thing, so she renders like a mannequin, and the exported
GLB contains NO skin diffuse at all: the images are MNAOMask, MicroNMask, SSSMap, roughness.

⛔ AND THE DIFFUSE WAS NEVER MISSING -- it is packed in the .blend the whole time, at
4096x4096 (`Std_Skin_Body_Diffuse`, `_Head_`, `_Arm_`, `_Leg_`). CC's skin shader is a NODE
GROUP, and the exporter walks the tree looking for something plugged into Base Color; on that
group it finds the SSS map. Nothing is broken and nothing is absent -- the export just needs
telling which image is the colour.

⭐ So each material is rebuilt as a plain Principled BSDF with the matching `*_Diffuse` wired
to Base Color (and `*_Opacity`/alpha kept where one exists). The exporter then has exactly one
obvious answer. Every material it cannot match is PRINTED, because a silent fallback here is
how a body ends up grey without anyone noticing.
"""
import os
import sys

import bpy


def argv_after_dashes():
    return sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


MATMAP = {}


def slot_key(mat_name):
    """`Scalp.001` and `Scalp` are one slot. Blender suffixes a duplicate material name on
    import, and a body with two hair meshes gets one of each."""
    n = mat_name
    if "." in n:
        head, _, tail = n.rpartition(".")
        if head and tail.isdigit():
            n = head
    return n.lower()


def load_matmap(path):
    """Read `slot  colour  alpha` produced by skate_src/anima/ue_dump_materials.py.

    WHY AN EXPLICIT MAP AND NOT NAME MATCHING. pick_diffuse below serves a Character
    Creator export, where the texture is named after the material. Anima is not that: her
    HiPoly skin slots are Std_Skin_Body / Std_Skin_Head against maps called T_Body_Diffuse
    and T_Head_Diffuse, her panties slot is `Material__1`, her Eyelets and Buckles both
    sample T_Metal_Diffuse, and her boot Sole and Straps both sample T_Boot_Diffuse.
    Ten of her slots share a map with a differently-named sibling and four more do not
    contain their map's name at all -- so similarity cannot reach them, and the LOW-poly
    body would have matched while the HIGH-poly one came out grey.
    Her UE material instances record the binding as fact, so it is read, not inferred."""
    n = 0
    for raw in open(path, encoding="utf-8"):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        col = parts[1] if parts[1] != "-" else None
        alp = parts[2] if len(parts) > 2 and parts[2] != "-" else None
        MATMAP[parts[0].lower()] = (col, alp)
        n += 1
    print("matmap: %d slot binding(s) from %s" % (n, path))


def img_named(stem):
    if not stem:
        return None
    want = stem.lower()
    for im in bpy.data.images:
        if im.name.lower() == want and im.size[0] and im.size[1]:
            return im
    return None


def pick_diffuse(mat_name):
    """The colour image for a material, by name. CC names textures after the material's
    texture set, so `Skin_Body` is served by `Std_Skin_Body_Diffuse`. Prefer an exact
    `<name>_Diffuse`, then any image carrying both the material name and 'Diffuse'."""
    want = mat_name.lower()
    exact, loose = [], []
    for im in bpy.data.images:
        # ⛔ AN IMAGE DATABLOCK IS NOT AN IMAGE. Selina's .blend names her textures but
        # the files were never shipped, so the datablocks exist with size 0x0 and the exporter
        # dies trying to read them off disk. Matching on NAME alone found one and re-armed the
        # same crash. Require actual pixels.
        if im.size[0] == 0 or im.size[1] == 0:
            continue
        n = im.name.lower()
        # ⛔ NOT EVERY PIPELINE SAYS "Diffuse". Character Creator does; Unreal exports say
        # BaseColor, and Selina's are Cloth_BaseColor.1001.png. Accept both.
        if "diffuse" not in n and "basecolor" not in n and "base_color" not in n:
            continue
        if any(n.startswith(want + k) or n.endswith(want + k)
               for k in ("_diffuse", "_basecolor", "_base_color")):
            exact.append(im)
        elif want in n:
            loose.append(im)
    for pool in (exact, loose):
        if pool:
            # the biggest one: CC ships a 64x64 occlusion stub beside a 4096 skin map
            return max(pool, key=lambda i: i.size[0] * i.size[1])
    return None


def pick_named(sub, mat_name):
    want = mat_name.lower()
    hits = [im for im in bpy.data.images
            if sub in im.name.lower() and want in im.name.lower()]
    return max(hits, key=lambda i: i.size[0] * i.size[1]) if hits else None


def rewire(mat):
    """Replace the material with a plain Principled BSDF whose Base Color is the diffuse.
    A material with no resolvable colour image still gets a clean BSDF, so a model whose
    textures were not shipped comes in untextured rather than failing to export at all."""
    mapped = MATMAP.get(slot_key(mat.name))
    img = img_named(mapped[0]) if mapped else None
    forced_alpha = img_named(mapped[1]) if mapped else None
    if img is None and not mapped:
        img = pick_diffuse(mat.name)
    if img is None:
        # A mapped slot whose colour is a flat utility map (Anima's Scalp samples plain
        # white) has no texture to wire and keeps its mesh tint -- report it, do not
        # silently pretend it was matched.
        return False, None
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    out.location = (400, 0)
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.location = (100, 0)
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    tex.location = (-300, 0)
    nt.links.new(bsdf.inputs["Base Color"], tex.outputs["Color"])
    # ⛔ eyelashes and some garments carry their cut-out in a SEPARATE opacity image; without
    # it they export as opaque rectangles across the face.
    # A MAPPED SLOT'S ALPHA IS FINAL, INCLUDING WHEN IT IS "NONE".
    # pick_named is a substring search, and `EyeL` is a substring of `T_EyeLash_Opacity`:
    # falling back to it gave Anima's LEFT eye the eyelash cut-out and left the right eye
    # alone, so her face was asymmetrically transparent and nothing symmetric would catch
    # it. The map is read off her UE materials and says EyeL has no opacity -- so it has
    # none. Only an UNMAPPED slot may guess.
    op = forced_alpha if mapped else pick_named("opacity", mat.name)
    if op is not None:
        otex = nt.nodes.new("ShaderNodeTexImage")
        otex.image = op
        otex.location = (-300, -320)
        if "Alpha" in bsdf.inputs:
            nt.links.new(bsdf.inputs["Alpha"], otex.outputs["Color"])
        mat.blend_method = 'BLEND' if hasattr(mat, "blend_method") else mat.blend_method
    elif "_Opacity" in img.name or "-" in img.name:
        if "Alpha" in bsdf.inputs:
            nt.links.new(bsdf.inputs["Alpha"], tex.outputs["Alpha"])
    nt.links.new(out.inputs["Surface"], bsdf.outputs["BSDF"])
    return True, img


def main():
    a = argv_after_dashes()
    out = None
    keep = None
    rigged = "--keep-rig" in a
    morphs = "--morphs" in a
    morphkeep = None
    for i, t in enumerate(a):
        if t == "--morphkeep" and i + 1 < len(a):
            morphkeep = {s2.strip().lower() for s2 in a[i + 1].split(",") if s2.strip()}
            morphs = True
    src_fbx = None
    texdir = None
    matmap = None
    tints = []
    for i, t in enumerate(a):
        if t == "--out" and i + 1 < len(a):
            out = a[i + 1]
        if t == "--keep" and i + 1 < len(a):
            keep = {s.strip() for s in a[i + 1].split(",") if s.strip()}
        if t == "--fbx" and i + 1 < len(a):
            src_fbx = a[i + 1]
        if t == "--texdir" and i + 1 < len(a):
            texdir = a[i + 1]
        if t == "--matmap" and i + 1 < len(a):
            matmap = a[i + 1]
        if t == "--tint" and i + 1 < len(a):
            for pair in a[i + 1].split(","):
                k, _, v = pair.partition("=")
                if k.strip() and v.strip():
                    tints.append((k.strip().lower(), v.strip()))
    if src_fbx:
        # ⭐ FBX COMES IN THROUGH BLENDER, AND THE AXIS IS SETTLED AT THE DOOR.
        # ⛔ The default import adds rot=(90,0,0) scale=0.01 to make the file Z-up for
        # Blender -- and then asking the exporter for export_yup converts a SECOND time, which
        # is why she came out flat on her back. Six attempts went into correcting that
        # downstream (transform_apply, zeroing, -90, a quarter-turn search, export_yup=False,
        # a data-level rotation that broke the skinning) and every one was fighting a rotation
        # that need never have existed.
        # ⭐ A CHARACTER CREATOR FBX IS ALREADY Y-UP, exactly like glTF. Importing with
        # up=Z forward=Y gives an IDENTITY armature and leaves the data alone; exporting with
        # export_yup off passes it straight through. Measured: rot=[0,0,0], height on Y.
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.fbx(filepath=src_fbx, use_manual_orientation=True,
                                 axis_up='Z', axis_forward='Y')
        print("imported %s (identity orientation, data left Y-up)" % src_fbx)
    if not out:
        print("!! --out is required")
        return
    os.makedirs(os.path.dirname(out), exist_ok=True)

    # ⛔ PURGE IMAGE DATABLOCKS WITH NO PIXELS BEFORE ANYTHING ELSE. Replacing every
    # material with a clean BSDF was not enough -- the exporter still gathers images from the
    # file and dies on the missing ones ("Cannot read ...Cloth_BaseColor.1001.png"). Selina
    # shipped as a .blend and a thumbnail with no Textures folder at all, so every one of her
    # image datablocks is a 0x0 promise. Remove them and she exports untextured, which is a
    # usable base body; leave them and she does not export at all.
    dead = [im for im in bpy.data.images if im.size[0] == 0 or im.size[1] == 0]
    for im in dead:
        try:
            bpy.data.images.remove(im)
        except Exception:
            pass
    if dead:
        print("purged %d image datablock(s) with no pixels (textures not shipped)" % len(dead))

    if texdir:
        # RECOVERED MAPS COME IN AS FRESH DATABLOCKS, NAMED WITHOUT THE EXTENSION.
        # Anima shipped from Fab as an FBX and a .max and NO texture folder: all 66 of her
        # image datablocks are 0x0 promises, purged just above, so there is nothing to
        # relink to -- the images have to be loaded anew and matched by name.
        # The extension MATTERS. pick_diffuse tests `name.endswith(material + "_diffuse")`,
        # and a datablock called "T_Body_Diffuse.png" fails that and falls through to the
        # loose `material in name` branch, where the material `Eye` matches T_EyeL,
        # T_EyeR AND T_Eyelash and the biggest one wins. Stripping the extension keeps the
        # exact branch working, so `Eye` stays unmatched instead of quietly wearing a lash.
        loaded = 0
        for fn in sorted(os.listdir(texdir)):
            if not fn.lower().endswith((".png", ".tga", ".jpg", ".jpeg")):
                continue
            try:
                im = bpy.data.images.load(os.path.join(texdir, fn), check_existing=True)
            except Exception as e:
                print("   !! could not load %s: %s" % (fn, e))
                continue
            im.name = os.path.splitext(fn)[0]
            loaded += 1
        print("loaded %d recovered texture(s) from %s" % (loaded, texdir))

    # WHICH MESH WEARS WHICH SLOT, BEFORE ANYTHING IS REWIRED. A slot that finds no
    # colour is reported by NAME further down, and a name on its own ("Material") does not
    # say what will render untextured -- Anima's HiPoly body carries a slot called exactly
    # that. Printing the mesh alongside it turns an unmatched material into a known part.
    for o in bpy.data.objects:
        if o.type == 'MESH':
            print("   mesh %-24s mats=%s" % (
                o.name, ",".join(m.name for m in o.data.materials if m)))

    if matmap:
        load_matmap(matmap)

    if morphkeep:
        # KEEP THE SHAPES WE WILL ACTUALLY DRIVE, DROP THE REST.
        # Anima carries 107 shape keys and glTF repeats every target on every PRIMITIVE --
        # her body is 15 primitives by material, so exporting the lot wrote 1,590 targets
        # and 712 MB into an 805 MB file. A face that acts needs the visemes, the brows,
        # the blinks, the eye-look directions and a jaw; it does not need Cheek_Blow_R.
        # Deleting the others in Blender is what makes the export tractable, and it is
        # reversible -- the source FBX is untouched.
        for o in bpy.data.objects:
            if o.type != 'MESH' or not o.data.shape_keys:
                continue
            kb = o.data.shape_keys.key_blocks
            drop = [k.name for k in kb
                    if k.name.lower() not in morphkeep and k != kb[0]]
            for nm in drop:
                o.shape_key_remove(o.data.shape_keys.key_blocks[nm])
            left = [k.name for k in o.data.shape_keys.key_blocks][1:]
            print("   morphs on %-22s kept %d of %d: %s"
                  % (o.name, len(left), len(left) + len(drop), ",".join(left[:6])
                     + ("..." if len(left) > 6 else "")))
            missing = morphkeep - {k.lower() for k in left}
            if missing:
                # NAMED, NOT SWALLOWED -- a misspelled shape is a silently dead expression.
                print("   !! asked for but not on this mesh: %s" % ", ".join(sorted(missing)))

    print("=== rewiring materials to their diffuse ===")
    ok, miss = 0, []
    for m in bpy.data.materials:
        if not m.users:
            continue
        done, img = rewire(m)
        if done:
            ok += 1
            print("   %-28s <- %s (%dx%d)" % (m.name[:28], img.name, img.size[0], img.size[1]))
        else:
            miss.append(m.name)
    if miss:
        # ⛔ NAMED, NOT SWALLOWED. A material with no diffuse renders as flat grey, which is
        # exactly the failure this file exists to stop, and it is invisible in a thumbnail.
        print("   !! no diffuse found for: %s" % ", ".join(miss))
    print("   %d material(s) rewired, %d without a diffuse" % (ok, len(miss)))

    for o in bpy.data.objects:
        try:
            o.select_set(False)
        except Exception:
            pass
    kept = []
    for o in bpy.data.objects:
        if o.type == 'MESH' and (keep is None or o.name in keep):
            o.select_set(True)
            kept.append(o.name)
            bpy.context.view_layer.objects.active = o
            # ⛔ THE EXPORTER WRITES THE MESH *DATA* NAME, NOT THE OBJECT NAME, and an FBX
            # import usually leaves the data called `Mesh.001`. Anima came through as
            # Mesh..Mesh.013 and Selina as Cube/Plane/Amelia_FaceMesh -- which throws away the
            # whole point of a modular character, because REVA_HIDEMESH matches on that name.
            # Copying the object's name onto its data is what keeps `anima_bra` addressable.
            try:
                # ⭐ THE ENGINE READS `#rrggbb` OFF AN UNTEXTURED MESH NAME (meshTintOf),
                # which is how a garment with no material gets a colour. A model whose maps
                # were never shipped can be coloured the same way -- no texture, no UVs, and
                # nothing new in the pack format. --tint substring=rrggbb[,...]
                nm = o.name
                low = nm.lower()
                for k, v in tints:
                    if k in low:
                        nm = "%s#%s" % (nm, v)
                        break
                o.data.name = nm
            except Exception:
                pass
    arm = None
    if rigged:
        for o in bpy.data.objects:
            if o.type == 'ARMATURE':
                o.select_set(True)
                bpy.context.view_layer.objects.active = o
                arm = o
    # ⛔ A MESH PARENTED TO A BONE CRASHES THE ANIMATION EXPORTER. Daz Genesis rigs do
    # this, and the glTF sampler dies with KeyError: None resolving `parent_bone_uuid` --
    # in the animation path, so it only bites once there is a clip to sample. Re-parent to
    # the armature OBJECT, preserving the world transform.
    for o in bpy.data.objects:
        if o.type == 'MESH' and o.parent is not None and o.parent_type == 'BONE':
            bone = o.parent_bone
            mw = o.matrix_world.copy()
            o.parent_type = 'OBJECT'
            o.matrix_world = mw
            # ⛔ A BONE-PARENTED MESH IS RIGID, NOT SKINNED -- it has no weights at all, so
            # it exports with no JOINTS_0 and glb2rpack_anim SILENTLY DISCARDS it along with
            # the ground planes it means to skip. That is how the vampire lost her dress, her
            # hair and her eyes while her body came through: Daz attaches those to bones.
            # ⭐ Bind them for real instead -- one vertex group for the parent bone at
            # weight 1, plus an armature modifier. Rigid attachment expressed as skinning,
            # which is the only kind of attachment the pack format has.
            if bone and o.parent.type == 'ARMATURE':
                # ⛔ CHECK FOR EXISTING WEIGHTS FIRST -- I DESTROYED HERS BY NOT DOING IT.
                # A bone-parented Daz mesh usually ALSO carries real vertex groups; it simply
                # has no armature modifier, which is why it exports with no JOINTS_0. Her hair
                # had 16 weighted groups and her dress 28. Adding a weight-1 group with
                # 'REPLACE' overwrote every one of them and collapsed both to a single rigid
                # bone -- so the hair could not move (no chain to simulate) and the gown could
                # not deform with her legs. jacob saw it immediately: "cant say ive seen any
                # hair move at all".
                # ⭐ The modifier is all that was ever missing. Only INVENT a weight-1
                # group when the mesh genuinely has none.
                weighted = set()
                for v in o.data.vertices:
                    for g in v.groups:
                        if g.weight > 0.01:
                            weighted.add(g.group)
                if not weighted:
                    vg = o.vertex_groups.get(bone) or o.vertex_groups.new(name=bone)
                    vg.add([v.index for v in o.data.vertices], 1.0, 'REPLACE')
                    print("   %s had NO weights -> rigid to bone '%s'" % (o.name[:26], bone))
                else:
                    print("   %s keeps its own %d-bone skinning" % (o.name[:26], len(weighted)))
                if not any(m.type == 'ARMATURE' for m in o.modifiers):
                    md = o.modifiers.new(name="Armature", type='ARMATURE')
                    md.object = o.parent
            else:
                print("   re-parented %s from bone to object" % o.name[:34])

    # ⛔ NO BLENDER-SIDE ANIMATION STUB. It crashed the exporter on the Daz vampire's
    # 405-bone rig (KeyError: None in the glTF animation sampler) and it is fragile in
    # general. glb_stubclip.py writes the clip straight into the finished GLB instead, which
    # is what statue2body has always done.
    print("%s: %s" % ("RIGGED" if rigged else "STATUE", sorted(kept)))
    # ⛔ NO ARMATURE AND NO SKIN: bake_statue.sh re-rigs from anatomy to 22 canonical joints.
    # Rigify's 724 bones are mostly controls that deform nothing and whose names mean nothing
    # to the engine's canonOf().
    # ⛔ export_apply applies MODIFIERS -- fine for a statue, fatal for a rigged body
    # (it collapses the armature modifier and destroys the skinning). And export_yup must be
    # OFF on the rigged path: the data is already Y-up, so converting would rotate it away.
    # ⛔ export_yup BELONGS TO THE SOURCE, NOT TO `rigged`. Tying it to rigged was wrong
    # and Camilia proved it immediately: an FBX arrives Y-UP (imported at identity above, so
    # converting again would rotate it away) while a .blend is Z-UP and does need the
    # conversion. Same flag, opposite answers, decided by where the data came from.
    # ⛔ export_apply applies MODIFIERS -- fine for a statue, fatal for a rigged body,
    # where it collapses the armature modifier and destroys the skinning.
    bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', use_selection=True,
                              export_apply=not rigged, export_yup=(src_fbx is None),
                              export_skins=rigged, export_animations=False,
                              # MORPH TARGETS ARE OPT-IN, AND THEY DOMINATE THE FILE.
                              # Anima's HiPoly body carries 106 facial blendshapes, and
                              # glTF repeats every target on every primitive -- across her
                              # 15 material primitives that is 1,590 targets and 712 MB of
                              # a 805 MB export, 454 MB of it morph NORMALS alone. The club
                              # drives a face from a jaw BONE and authored marks, so the
                              # film needs none of it. Pass --morphs when the expressions
                              # are actually wanted (rpack v3 carries morphs; see morphs.py)
                              # and expect the file to grow by an order of magnitude.
                              export_morph=morphs,
                              export_morph_normal=False,
                              export_materials='EXPORT')
    print("WROTE %s (%.1f MB)" % (out, os.path.getsize(out) / 1e6))


main()
