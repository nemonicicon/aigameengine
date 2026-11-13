extends SceneTree
## engine_export.gd -- a Godot scene as our engine can take it (11-12, the Godot proof of concept).
##
## A map made in Godot can come over to the engine: this runs the scene in Godot,
## headless, for a few frames, so whatever its scripts build at the start (a park loader's model, a garden's plants, a lobby's
## dressing) is there, and then writes what's drawn as ONE glTF, and everything that isn't geometry as a manifest:
##   <out>/<name>.glb        every visible mesh (MeshInstance3D, each instance of a MultiMeshInstance3D, a CSG root's mesh),
##                           placed where it is in the world, named by its path in the scene so the other side can tell
##                           what it is; skinned meshes and the player's own nodes are left out
##   <out>/<name>.json       the spawn (the Player node), lights, markers, areas (a skate game's gaps), static shapes (a
##                           safety net), the WorldEnvironment, the park loader's sidecar (rails, gaps, walls), which meshes
##                           are ridden (the skate game's own rule, by name), and what can't come over as it is: shader
##                           materials, particles, decals, scripts that ran
##   <out>/shaders/           (11-12, step 12) each shader material's textures as pictures, for godot_shader.py: the manifest's "shaders" has
##                           each one's code and what its uniforms are set to, and the surfaces it's on are named for it in the glb
## usage: godot --headless --path <project> -s <this file> -- <res://scene.tscn> <out folder> [name] [frames]
##    or: ... -- --pieces <out folder> [name]   (11-12, step 14) the skate game's Create a Map library: every piece its ParkLibrary has, built
##        by its own ParkObjectFactory, at the origin under one node each (the glb), and each one's settings, collision and rails (the manifest)
## skate_src/godot/godot2set.py turns the two files into a set and a pack for the engine.

const SKIP_TYPES := ["CanvasLayer", "Control", "AudioStreamPlayer", "AudioStreamPlayer3D", "Camera3D"]
var _out := ""
var _name := ""   # the export's name: its pictures are named for it, so parks can share a folder
var _lvl: Node = null
var _shd_of := {}   # a shader material's instance -> its key
var _tex_of := {}   # a texture's instance -> its picture's file name
var _pieces_mode := false
var _stand_of := {}   # a shader material's instance -> the one stand-in its surfaces share in the glb


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 2:
		printerr("usage: -- <res://scene.tscn> <out folder> [name] [frames]")
		quit(2)
		return
	var scene_path: String = args[0]
	var out: String = args[1]
	var name: String = args[2] if args.size() > 2 else scene_path.get_file().get_basename()
	_name = name
	var frames: int = int(args[3]) if args.size() > 3 else 30
	DirAccess.make_dir_recursive_absolute(out)
	DirAccess.make_dir_recursive_absolute(out.path_join("shaders"))
	_out = out
	var lvl: Node
	if scene_path == "--pieces":
		_pieces_mode = true
		lvl = Node3D.new()
		lvl.name = "pieces"
	else:
		var packed := load(scene_path) as PackedScene
		if packed == null:
			printerr("can't load ", scene_path)
			quit(1)
			return
		lvl = packed.instantiate()
	_lvl = lvl
	root.add_child(lvl)
	for i in frames:
		await process_frame
	var m := {"format": "engine_export", "version": 1, "scene": scene_path, "name": name, "units": "metres, Godot's axes (x right, y up, -z forward)",
		"spawn": null, "lights": [], "markers": [], "areas": [], "statics": [], "environment": {}, "sidecar": null,
		"meshes": [], "unsupported": {"shader_materials": {}, "particles": [], "decals": [], "labels": [], "instances_expanded": 0, "skinned_skipped": []},
		"scripts": {}, "shaders": [], "pieces": []}
	if _pieces_mode and not _add_pieces(lvl as Node3D, m):
		printerr("no ParkLibrary / ParkObjectFactory in this project")
		quit(1)
		return
	var player := lvl.get_node_or_null("Player")
	if player is Node3D:
		m["spawn"] = _xf((player as Node3D).global_transform)
		for p in ["void_y", "respawn_y"]:
			if p in player:
				m["spawn"][p] = player.get(p)
		if "play_area" in player:
			var r: Rect2 = player.get("play_area")
			m["spawn"]["play_area"] = [r.position.x, r.position.y, r.size.x, r.size.y]
	m["rail_graph"] = _rail_graph()
	var out_root := Node3D.new()
	out_root.name = name
	root.add_child(out_root)
	_walk(lvl, player, out_root, m)
	var doc := GLTFDocument.new()
	var state := GLTFState.new()
	var err := doc.append_from_scene(out_root, state)
	if err == OK:
		err = doc.write_to_filesystem(state, out.path_join(name + ".glb"))
	m["glb_error"] = err
	var f := FileAccess.open(out.path_join(name + ".json"), FileAccess.WRITE)
	f.store_string(JSON.stringify(m, "\t"))
	f.close()
	print("ENGINE_EXPORT %s: %d meshes, %d lights, %d markers, %d areas, glb %s" % [name, m["meshes"].size(), m["lights"].size(), m["markers"].size(), m["areas"].size(), "ok" if err == OK else "error %d" % err])
	quit(0)


## (11-12) The level's grind rails as their own GrindRailGraph built them while the level loaded (skatepark_loader.gd's _build_rail_graph): the
## named rails, Start/End rails, the park file's polylines and the edges their builder finds in meshes named coping, ledge, rail, box or bench.
## Every piece: its ends in the world, the piece linked at each end and which of its ends, its chain, its length, and the body it rides on when
## that body moves (the port moves those pieces with it). Their builder runs here, in Godot, as their shaders' compiler does: content, kept.
func _rail_graph() -> Dictionary:
	var out := {"graphs": 0, "pieces": []}
	for node in get_nodes_in_group("grind_rail_graph"):
		out["graphs"] = int(out["graphs"]) + 1
		var moving: Array = node.get("_moving") if "_moving" in node else []
		var base: int = (out["pieces"] as Array).size()
		var segs: Array = node.get("segs")
		for i in segs.size():
			var p = segs[i]
			var wa: Vector3 = p.world_a()
			var wb: Vector3 = p.world_b()
			var body_path := ""
			if is_instance_valid(p.body) and moving.has(i):
				body_path = _path_name(p.body)
			(out["pieces"] as Array).append({"a": [wa.x, wa.y, wa.z], "b": [wb.x, wb.y, wb.z],
				"link_a": p.link_a + base if p.link_a >= 0 else -1, "link_a_end": p.link_a_end,
				"link_b": p.link_b + base if p.link_b >= 0 else -1, "link_b_end": p.link_b_end,
				"chain": p.chain, "graph": int(out["graphs"]) - 1, "length": p.length, "body": body_path})
	print("ENGINE_EXPORT rail graph: %d graph(s), %d pieces" % [out["graphs"], (out["pieces"] as Array).size()])
	return out


func _walk(n: Node, player: Node, out_root: Node3D, m: Dictionary) -> void:
	if n == player:
		return
	for t in SKIP_TYPES:
		if n.is_class(t):
			return
	var s: Script = n.get_script()
	if s != null and s.resource_path != "":
		m["scripts"][s.resource_path] = int(m["scripts"].get(s.resource_path, 0)) + 1
	if n is MeshInstance3D:
		_mesh(n as MeshInstance3D, out_root, m)
	elif n is MultiMeshInstance3D:
		var mmi := n as MultiMeshInstance3D
		if mmi.is_visible_in_tree() and mmi.multimesh != null and mmi.multimesh.mesh != null:
			var mm := mmi.multimesh
			for i in mm.visible_instance_count if mm.visible_instance_count >= 0 else mm.instance_count:
				var c := MeshInstance3D.new()
				c.mesh = mm.mesh
				c.material_override = mmi.material_override
				c.name = "%s_%d" % [_path_name(mmi), i]
				c.transform = mmi.global_transform * mm.get_instance_transform(i)
				out_root.add_child(c)
				m["meshes"].append({"name": String(c.name), "ride": _ride(mmi), "instance_of": _path_name(mmi)})
			m["unsupported"]["instances_expanded"] += mm.instance_count
	elif n is CSGShape3D and (n as CSGShape3D).is_root_shape() and (n as CSGShape3D).is_visible_in_tree():
		var got: Array = (n as CSGShape3D).get_meshes()
		if got.size() >= 2 and got[1] is Mesh:
			var c := MeshInstance3D.new()
			c.mesh = got[1]
			c.name = _path_name(n)
			c.transform = (n as Node3D).global_transform * (got[0] as Transform3D)
			out_root.add_child(c)
			m["meshes"].append({"name": String(c.name), "ride": _ride(n)})
	elif n is DirectionalLight3D or n is OmniLight3D or n is SpotLight3D:
		var l := n as Light3D
		if l.is_visible_in_tree():
			var d := {"name": String(l.name), "type": l.get_class(), "color": [l.light_color.r, l.light_color.g, l.light_color.b], "energy": l.light_energy,
				"shadow": l.shadow_enabled, "xf": _xf(l.global_transform)}
			if l is OmniLight3D:
				d["range"] = (l as OmniLight3D).omni_range
				d["attenuation"] = (l as OmniLight3D).omni_attenuation
			elif l is SpotLight3D:
				d["range"] = (l as SpotLight3D).spot_range
				d["angle"] = (l as SpotLight3D).spot_angle
			m["lights"].append(d)
	elif n is Marker3D:
		m["markers"].append({"name": _path_name(n), "xf": _xf((n as Node3D).global_transform)})
	elif n is Area3D:
		var a := {"name": _path_name(n), "xf": _xf((n as Node3D).global_transform), "shapes": _shapes(n)}
		for p in ["gap_name", "points"]:
			if p in n:
				a[p] = n.get(p)
		m["areas"].append(a)
	elif n is StaticBody3D and not (n.get_parent() is MeshInstance3D) and not _pieces_mode:
		var sh := _shapes(n)
		if not sh.is_empty():
			m["statics"].append({"name": _path_name(n), "xf": _xf((n as Node3D).global_transform), "shapes": sh})
	elif n is WorldEnvironment and (n as WorldEnvironment).environment != null:
		m["environment"] = _env((n as WorldEnvironment).environment, m)
	elif n is GPUParticles3D or n is CPUParticles3D:
		m["unsupported"]["particles"].append({"name": _path_name(n), "type": n.get_class(), "xf": _xf((n as Node3D).global_transform)})
	elif n is Decal:
		m["unsupported"]["decals"].append({"name": _path_name(n), "xf": _xf((n as Node3D).global_transform), "size": [(n as Decal).size.x, (n as Decal).size.y, (n as Decal).size.z]})
	elif n is Label3D:
		m["unsupported"]["labels"].append({"name": _path_name(n), "text": (n as Label3D).text, "xf": _xf((n as Node3D).global_transform)})
	if "town_path" in n and "sidecar_path" in n:
		var sp: String = n.get("sidecar_path")
		m["park"] = {"model": n.get("town_path"), "sidecar": sp}
		if sp != "" and FileAccess.file_exists(sp):
			m["sidecar"] = JSON.parse_string(FileAccess.get_file_as_string(sp))
	for c in n.get_children():
		_walk(c, player, out_root, m)


func _mesh(mi: MeshInstance3D, out_root: Node3D, m: Dictionary) -> void:
	if not mi.is_visible_in_tree() or mi.mesh == null:
		return
	if mi.skin != null or String(mi.skeleton) != "" and mi.get_node_or_null(mi.skeleton) is Skeleton3D:
		m["unsupported"]["skinned_skipped"].append(_path_name(mi))
		return
	var c := MeshInstance3D.new()
	c.mesh = mi.mesh
	c.material_override = mi.material_override
	for i in mi.get_surface_override_material_count():
		c.set_surface_override_material(i, mi.get_surface_override_material(i))
	c.name = _path_name(mi)
	c.transform = mi.global_transform
	out_root.add_child(c)
	m["meshes"].append({"name": String(c.name), "ride": _ride(mi)})
	for i in mi.mesh.get_surface_count():
		var mat: Material = mi.get_active_material(i)
		if mat is ShaderMaterial:
			var sm := mat as ShaderMaterial
			var sp := (sm.shader.resource_path) if sm.shader != null else "(no shader)"
			m["unsupported"]["shader_materials"][sp] = int(m["unsupported"]["shader_materials"].get(sp, 0)) + 1
			# (11-12, step 12) the shader goes over to be run in the engine (godot_shader.py translates it): its code, what its uniforms are set
			# to, its textures as pictures. The surface's stand-in material is NAMED for it, so the glb says which surfaces it's on. What the
			# material still holds of the model's own look (a texture it wraps, a colour) is the stand-in's too, for wherever it's drawn plainly
			# ⛔ ONE stand-in for each shader material, shared by its surfaces: the glTF writer makes a material of each instance and renames a name
			# it has seen (gshd_1, gshd_12, gshd_13 ...), and the cook knew one surface in each shader's
			if _stand_of.has(sm.get_instance_id()):
				c.set_surface_override_material(i, _stand_of[sm.get_instance_id()])
				if (_stand_of[sm.get_instance_id()] as StandardMaterial3D).albedo_texture != null:
					m["unsupported"]["shader_materials_kept"] = int(m["unsupported"].get("shader_materials_kept", 0)) + 1
				continue
			var stand := StandardMaterial3D.new()
			_stand_of[sm.get_instance_id()] = stand
			var got := false
			if sm.shader != null:
				stand.resource_name = _shader_rec(sm, m)
				for u in sm.shader.get_shader_uniform_list():
					var v = sm.get_shader_parameter(u["name"])
					if not got and v is Texture2D and String(u["name"]) in ["original", "albedo", "albedo_texture", "texture_albedo", "base_texture", "main_texture", "tex"]:
						stand.albedo_texture = v
						got = true
					elif v is Color and String(u["name"]) in ["albedo", "albedo_color", "color", "base_color", "tint"]:
						stand.albedo_color = v
						got = true
			c.set_surface_override_material(i, stand)
			if got:
				m["unsupported"]["shader_materials_kept"] = int(m["unsupported"].get("shader_materials_kept", 0)) + 1


## (11-12, step 14) a script class of the project's, by name, without naming it in this file (which runs in any project)
func _class_script(cname: String) -> Script:
	for c in ProjectSettings.get_global_class_list():
		if String(c["class"]) == cname:
			return load(String(c["path"])) as Script
	return null


## every piece of the Create a Map library, built by its own factory, under the root at the origin; and its record
func _add_pieces(lvl: Node3D, m: Dictionary) -> bool:
	var lib := _class_script("ParkLibrary")
	var fac := _class_script("ParkObjectFactory")
	if lib == null or fac == null:
		return false
	lib.ensure_loaded()
	m["categories"] = []
	for c in lib.get("CATEGORIES"):
		var col: Color = c["color"]
		m["categories"].append({"id": c["id"], "name": c["name"], "color": [col.r, col.g, col.b], "subs": Array(c["subs"])})
	for def in lib.all():
		var piece: Node3D = fac.create_piece(def, {"uid": 0})
		piece.name = String(def.id)
		lvl.add_child(piece)
		m["pieces"].append(_piece_rec(def, piece.get("asset")))
	return true


func _floats(a: PackedVector3Array) -> Array:
	var o := []
	for v in a:
		o.append(snappedf(v.x, 0.0001))
		o.append(snappedf(v.y, 0.0001))
		o.append(snappedf(v.z, 0.0001))
	return o


func _aabb(b: AABB) -> Array:
	return [b.position.x, b.position.y, b.position.z, b.end.x, b.end.y, b.end.z]


func _piece_rec(def, asset) -> Dictionary:
	var pal := []
	for c in def.tint_palette:
		pal.append([c.r, c.g, c.b])
	var t: Color = def.default_tint
	var g: Vector3 = def.family_grid
	var d := {"id": String(def.id), "name": String(def.display_name), "category": String(def.category), "sub": String(def.subcategory),
		"params": def.params, "edit_params": def.edit_params, "placement": int(def.placement), "ground_fit": int(def.ground_fit), "snap_mode": int(def.snap_mode),
		"snap_family": String(def.snap_family), "family_grid": [g.x, g.y, g.z], "rotation_step": def.rotation_step, "allow_tilt": def.allow_tilt,
		"max_slope_deg": def.max_slope_deg, "sink": def.sink, "max_per_map": def.max_per_map, "scale_mode": int(def.scale_mode), "scale_min": def.scale_min,
		"scale_max": def.scale_max, "rideable": def.rideable, "grindable": def.grindable, "tintable": def.tintable, "tint_palette": pal,
		"default_tint": [t.r, t.g, t.b, t.a], "material_options": Array(def.material_options), "default_material": String(def.default_material),
		"react": String(def.react), "gameplay_role": String(def.gameplay_role), "cost": def.cost, "tags": Array(def.tags),
		"icon": _tex_out(def.icon) if def.icon != null else ""}
	if asset != null:
		d["aabb"] = _aabb(asset.aabb)
		d["snap_aabb"] = _aabb(asset.snap_aabb)
		d["tri_count"] = asset.tri_count
		d["ride"] = _floats(asset.ride_faces)
		d["block"] = _floats(asset.block_faces)
		var boxes := []
		for b in asset.block_boxes:
			var s: Vector3 = b["size"]
			boxes.append({"xf": _xf(b["xf"]), "size": [s.x, s.y, s.z]})
		d["boxes"] = boxes
		var rails := []
		for r in asset.rails:
			rails.append(_floats(r))
		d["rails"] = rails
		var an := {}
		for k in asset.anchors:
			var v = asset.anchors[k]
			an[String(k)] = _xf(v) if v is Transform3D else str(v)
		d["anchors"] = an
	return d


## (11-12, step 12) a shader material, once: its code, its uniforms' values, its textures saved as pictures in <out>/shaders
func _shader_rec(sm: ShaderMaterial, m: Dictionary) -> String:
	var id := sm.get_instance_id()
	if _shd_of.has(id):
		return _shd_of[id]
	var key := "gshd_%d" % m["shaders"].size()
	_shd_of[id] = key
	var rec := {"key": key, "shader": sm.shader.resource_path, "code": sm.shader.code, "params": {}, "notes": [], "globals": {}}
	# a `global uniform` is the project's: its value as the scene stands (a script may move it as it runs; the cook holds this one)
	var rx := RegEx.new()
	rx.compile("global\\s+uniform\\s+\\w+\\s+(\\w+)")
	for mt in rx.search_all(sm.shader.code):
		var gn := mt.get_string(1)
		var gv = RenderingServer.global_shader_parameter_get(gn)
		if typeof(gv) == TYPE_BOOL or typeof(gv) == TYPE_INT or typeof(gv) == TYPE_FLOAT:
			rec["globals"][gn] = {"num": gv}
		elif gv is Color:
			rec["globals"][gn] = {"color": [gv.r, gv.g, gv.b, gv.a]}
	for u in sm.shader.get_shader_uniform_list():
		var nm := String(u["name"])
		var v = sm.get_shader_parameter(nm)
		if v == null:
			continue
		if v is TextureLayered:
			rec["params"][nm] = _layers_out(v as TextureLayered, rec)
		elif v is Texture2D:
			var fn := _tex_out(v as Texture2D)
			rec["params"][nm] = {"tex": fn} if fn != "" else {"tex_unread": (v as Texture2D).resource_path}
		elif v is Color:
			rec["params"][nm] = {"color": [v.r, v.g, v.b, v.a]}
		elif typeof(v) == TYPE_BOOL or typeof(v) == TYPE_INT or typeof(v) == TYPE_FLOAT:
			rec["params"][nm] = {"num": v}
		elif typeof(v) in [TYPE_VECTOR2, TYPE_VECTOR3, TYPE_VECTOR4, TYPE_VECTOR2I, TYPE_VECTOR3I, TYPE_VECTOR4I]:
			var a := []
			var n := 2 if typeof(v) in [TYPE_VECTOR2, TYPE_VECTOR2I] else (3 if typeof(v) in [TYPE_VECTOR3, TYPE_VECTOR3I] else 4)
			for k in n:
				a.append(v[k])
			rec["params"][nm] = {"vec": a}
		else:
			rec["notes"].append("uniform %s: a %s isn't carried" % [nm, type_string(typeof(v))])
	m["shaders"].append(rec)
	return key


func _tex_out(t: Texture2D) -> String:
	var id := t.get_instance_id()
	if _tex_of.has(id):
		return _tex_of[id]
	var img := t.get_image()
	if img == null:
		return ""
	if img.is_compressed():
		img.decompress()
	var fn := "%s_tex_%d.png" % [_name, _tex_of.size()]
	img.save_png(_out.path_join("shaders").path_join(fn))
	_tex_of[id] = fn
	return fn


## an array's layers. ⛔ The renderer --headless runs on (the dummy one) keeps no array's layers: get_layer_data is null for one built while
## the scene ran. Then it's made again from what it was cut from: a script in the scene that holds it in a variable, with a picture among
## its constants that shares a word with that variable's name and is a whole grid of the array's cells, cut row by row (the Hedge Maze's
## night_garden.gd: _foliage from FOLIAGE_ATLAS, 2 x 2). The manifest says when it was, so the guess can be checked
func _layers_out(t: TextureLayered, rec: Dictionary) -> Dictionary:
	var files := []
	for L in t.get_layers():
		var img := t.get_layer_data(L)
		if img == null:
			files = []
			break
		if img.is_compressed():
			img.decompress()
		var fn := "%s_arr_%d_%d.png" % [_name, t.get_instance_id() % 1000003, L]
		img.save_png(_out.path_join("shaders").path_join(fn))
		files.append(fn)
	if files.is_empty():
		files = _layers_from_atlas(t, rec)
	if files.is_empty():
		return {"layers_unread": [t.get_width(), t.get_height(), t.get_layers()]}
	return {"layers": files}


func _layers_from_atlas(t: TextureLayered, rec: Dictionary) -> Array:
	var nodes: Array = [_lvl] + _lvl.find_children("*", "", true, false)
	for node in nodes:
		var s: Script = node.get_script()
		if s == null:
			continue
		for p in node.get_property_list():
			var pv = node.get(p["name"])
			if not (int(p["usage"]) & PROPERTY_USAGE_SCRIPT_VARIABLE) or typeof(pv) != TYPE_OBJECT or pv != t:
				continue
			var vw := String(p["name"]).to_lower().split("_", false)
			var consts := s.get_script_constant_map()
			for cn in consts:
				var cv = consts[cn]
				if not (cv is Texture2D):
					continue
				var share := false
				for word in String(cn).to_lower().split("_", false):
					if word in vw:
						share = true
				if not share:
					continue
				var img := (cv as Texture2D).get_image()
				if img == null:
					continue
				if img.is_compressed():
					img.decompress()
				var w := t.get_width()
				var h := t.get_height()
				var cols := img.get_width() / w
				var rows := img.get_height() / h
				if cols < 1 or rows < 1 or cols * w != img.get_width() or rows * h != img.get_height() or cols * rows < t.get_layers():
					continue
				var files := []
				for L in t.get_layers():
					var cell := img.get_region(Rect2i(Vector2i(L % cols, L / cols) * Vector2i(w, h), Vector2i(w, h)))
					var fn := "%s_arr_%d_%d.png" % [_name, t.get_instance_id() % 1000003, L]
					cell.save_png(_out.path_join("shaders").path_join(fn))
					files.append(fn)
				rec["notes"].append("%s.%s: the dummy renderer keeps no array's layers, so it's cut again from %s (%s), a grid %d x %d, row by row" % [node.name, p["name"], cn, (cv as Texture2D).resource_path, cols, rows])
				return files
	return []


## the skate game's own rule (addons/skate_system/skatepark_loader.gd, _is_rideable_body): by the names on the way up
func _ride(n: Node) -> bool:
	var p: Node = n
	while p != null:
		var nm := String(p.name)
		if nm.contains("Catch") or nm.contains("Post") or nm.contains("Scaffold") or nm.contains("Guard"):
			return false
		if nm.contains("Terrain") or nm.contains("_Roads") or nm.contains("_Skate"):
			return true
		p = p.get_parent()
	return false


func _path_name(n: Node) -> String:
	var parts := PackedStringArray()
	var p: Node = n
	while p != null and p != root:
		parts.insert(0, String(p.name))
		p = p.get_parent()
	return "/".join(parts).replace(" ", "_")


func _shapes(n: Node) -> Array:
	var out := []
	for c in n.get_children():
		if c is CollisionShape3D and (c as CollisionShape3D).shape != null:
			var sh := (c as CollisionShape3D).shape
			var d := {"type": sh.get_class(), "xf": _xf((c as Node3D).global_transform)}
			if sh is BoxShape3D:
				d["size"] = [(sh as BoxShape3D).size.x, (sh as BoxShape3D).size.y, (sh as BoxShape3D).size.z]
			elif sh is SphereShape3D:
				d["radius"] = (sh as SphereShape3D).radius
			elif sh is CapsuleShape3D or sh is CylinderShape3D:
				d["radius"] = sh.get("radius")
				d["height"] = sh.get("height")
			out.append(d)
	return out


func _env(e: Environment, m: Dictionary) -> Dictionary:
	var d := {"background_mode": e.background_mode, "ambient_source": e.ambient_light_source,
		"ambient_color": [e.ambient_light_color.r, e.ambient_light_color.g, e.ambient_light_color.b], "ambient_energy": e.ambient_light_energy,
		"tonemap_mode": e.tonemap_mode, "exposure": e.tonemap_exposure, "glow": e.glow_enabled, "ssao": e.ssao_enabled,
		"fog": e.fog_enabled, "fog_color": [e.fog_light_color.r, e.fog_light_color.g, e.fog_light_color.b], "fog_density": e.fog_density,
		"background_color": [e.background_color.r, e.background_color.g, e.background_color.b]}
	if e.sky != null and e.sky.sky_material != null:
		var sm := e.sky.sky_material
		d["sky"] = sm.get_class()
		if sm is ProceduralSkyMaterial:
			var p := sm as ProceduralSkyMaterial
			d["sky_top"] = [p.sky_top_color.r, p.sky_top_color.g, p.sky_top_color.b]
			d["sky_horizon"] = [p.sky_horizon_color.r, p.sky_horizon_color.g, p.sky_horizon_color.b]
			d["ground_horizon"] = [p.ground_horizon_color.r, p.ground_horizon_color.g, p.ground_horizon_color.b]
			d["ground_bottom"] = [p.ground_bottom_color.r, p.ground_bottom_color.g, p.ground_bottom_color.b]
		elif sm is ShaderMaterial and (sm as ShaderMaterial).shader != null:
			d["sky_shader"] = (sm as ShaderMaterial).shader.resource_path
			d["sky_key"] = _shader_rec(sm as ShaderMaterial, m)   # (step 12) the sky's own shader, carried as a surface's is
			for pn in ["panorama"]:
				var v = (sm as ShaderMaterial).get_shader_parameter(pn)
				if v is Texture2D:
					d["sky_panorama"] = (v as Texture2D).resource_path
		elif sm is PanoramaSkyMaterial and (sm as PanoramaSkyMaterial).panorama != null:
			d["sky_panorama"] = (sm as PanoramaSkyMaterial).panorama.resource_path
	return d


func _xf(t: Transform3D) -> Dictionary:
	var q := t.basis.get_rotation_quaternion()
	var s := t.basis.get_scale()
	var fwd := -t.basis.z
	return {"pos": [t.origin.x, t.origin.y, t.origin.z], "quat": [q.x, q.y, q.z, q.w], "scale": [s.x, s.y, s.z],
		"yaw_deg": rad_to_deg(atan2(fwd.x, fwd.z)), "fwd": [fwd.x, fwd.y, fwd.z]}
