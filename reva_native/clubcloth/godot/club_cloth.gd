class_name ClubCloth
extends SkeletonModifier3D

## A rider's clothes as whole cloth, on the kit's solver (source in reva_native/clubcloth; a game keeps its own copy of the solver and this script beside it). Goes last on the skeleton, so it sees where the body really ends up.
## A garment is cloth when its mesh carries the tailor's numbers in its second UV: how far each point may go from the skinned pose, and how near the skin it may come. It's taken off the skeleton's skinning, the solver
## writes its vertices each frame in the skeleton's own space, and its node stays where the wardrobe expects it. Only what he's wearing is solved.
## Where the native library isn't built the class isn't there: available() says so, and the rider has the hem springs instead.

const SOLVER := &"ClubClothSim"
## Far enough for any garment in any pose, so nothing asks the mesh for its bounds: it never has any of its own.
const BOUNDS := AABB(Vector3(-2.5, -1.5, -2.5), Vector3(5.0, 5.0, 5.0))
const GRAVITY := Vector3(0.0, -9.8, 0.0)
const LOOK := preload("club_cloth.gdshader")   # beside this script, wherever a game keeps it
## How far a garment keeps outside the one it's worn over, and a garment's parts outside each other (metres). Big facets go through each other with their points further apart than this: at 6 mm a pair of baggy legs passed through each other in half the frames of a walk, and at 3 cm in a fifth, and they move no more for it.
const LAYER_GAP := 0.008
const PART_GAP := 0.006
const LEGS_PART_GAP := 0.03
## The most a board may move against him in a frame and still be something cloth keeps out of (metres).
const BOARD_TOO_FAST := 0.08
## How far out cloth looks up its shadows (metres), between a sun this dim and this bright. Moonlit, with a torch in his hand, it has to stay small: it moves the torch's light too.
const SUN_OUT_DIM := 0.03
const SUN_OUT_BRIGHT := 0.15
const SUN_DIM := 0.4
const SUN_BRIGHT := 1.0
const SUN_SEARCH_EVERY := 2.0
## How he's turning through the world: the most it counts for (radians a second, and a second squared), how fast it's followed, and how far in a frame is a teleport and not a turn (radians).
const TURN_MOST := 20.0
const TURN_ACCEL_MOST := 120.0
const TURN_EASE := 20.0
const TURN_JUMP := 1.0

## What the world is doing to him, in world space, set by the rider: his acceleration, and the air's speed past him.
var accel := Vector3.ZERO
var air := Vector3.ZERO

var _sim: Variant = null
var _wardrobe: ClubWardrobe
## The rig's cloth tables, copied at setup from whatever profile the game hands over: what each piece is made of, what's worn over what, which bones make a part, and the capsules under a slot. A profile without them means no cloth.
var _rig_fabrics := {}
var _rig_over := {}
var _rig_parts: Array = []
var _rig_capsules := {}
var _skeleton: Skeleton3D
var _palette := {}
## piece -> {"index": solver's garment, "node": its mesh node, "slot": the wardrobe's}
var _garments := {}
var _ready_to_step := false
var _boards := []
var _sun: DirectionalLight3D
var _sun_search_in := 0.0
var _sun_out := -1.0
var _sun_garments := 0
var _basis_was := Basis.IDENTITY
var _turn := Vector3.ZERO
var _turn_accel := Vector3.ZERO


static func available() -> bool:
	return ClassDB.class_exists(SOLVER)


## Does this character have anything to solve? A garment is cloth by its second UV.
static func wears_cloth(root: Node) -> bool:
	for m in root.find_children("wear~*", "MeshInstance3D", true, false):
		var mesh := (m as MeshInstance3D).mesh
		if mesh != null and mesh.get_surface_count() > 0 and mesh.surface_get_format(0) & Mesh.ARRAY_FORMAT_TEX_UV2:
			return true
	return false


## The same look on a garment that isn't on the solver: the dots are the smooth shading's, not the solver's. The browser build keeps what it has.
static func look_the_part(root: Node) -> void:
	if OS.has_feature("web"):
		return
	for m in root.find_children("wear~*", "MeshInstance3D", true, false):
		var node := m as MeshInstance3D
		var mesh := node.mesh
		if mesh != null and mesh.get_surface_count() > 0 and mesh.surface_get_format(0) & Mesh.ARRAY_FORMAT_TEX_UV2:
			node.set_surface_override_material(0, look_of(mesh.surface_get_material(0)))


## Solve this rider's clothes from now on, for whatever the wardrobe has him in. Call it once the wardrobe is on and everything else that works on the skeleton has been added.
func setup(skeleton: Skeleton3D, root: Node, wardrobe: ClubWardrobe, profile: Resource) -> void:
	_skeleton = skeleton
	_wardrobe = wardrobe
	if profile != null:
		var fab: Variant = profile.get("cloth_fabrics")
		_rig_fabrics = fab if fab is Dictionary else {}
		var over: Variant = profile.get("cloth_over")
		_rig_over = over if over is Dictionary else {}
		var parts: Variant = profile.get("cloth_parts")
		_rig_parts = parts if parts is Array else []
		var caps: Variant = profile.get("cloth_capsules")
		_rig_capsules = caps if caps is Dictionary else {}
	name = "ClubCloth"
	_sim = ClassDB.instantiate(SOLVER)
	skeleton.add_child(self)
	_basis_was = skeleton.global_basis.orthonormalized()
	_set_palette(root)
	for m in root.find_children("*", "MeshInstance3D", true, false):
		if not String(m.name).begins_with("wear~"):
			_add_body(m as MeshInstance3D)
	_fit()
	wardrobe.changed.connect(_fit)
	skeleton.child_order_changed.connect(_go_last)
	_ready_to_step = true


## Whatever's added to the skeleton later, it goes before this. The cloth only knows the pose as it stands when its turn comes: with the maze's raised torch after it, his arm went up and came out of a sleeve still hanging at his side.
func _go_last() -> void:
	if get_parent() == _skeleton and get_index() != _skeleton.get_child_count() - 1:
		_skeleton.move_child.call_deferred(self, -1)


## The skeleton's own space turned under the cloth all at once, and the pose with it, so that in the world nothing moved. rel takes a point of the old space to the new one.
func carry(rel: Transform3D) -> void:
	var b := rel.basis
	_sim.carry(PackedFloat32Array([b.x.x, b.y.x, b.z.x, rel.origin.x, b.x.y, b.y.y, b.z.y, rel.origin.y, b.x.z, b.y.z, b.z.z, rel.origin.z]))
	_basis_was = _skeleton.global_basis.orthonormalized()


func _process_modification_with_delta(delta: float) -> void:
	if not _ready_to_step or delta <= 0.0:
		return
	var into_skeleton := _skeleton.global_basis.inverse()
	_set_boxes()
	_follow_sun(delta)
	_follow_turning(delta)
	_sim.step(delta, into_skeleton * GRAVITY, into_skeleton * accel, into_skeleton * air, _turn, _turn_accel)


## How the skeleton itself is turning through the world, in its own space: a spin flares a hem. The cloth's simulated in the skeleton's own space, so turning reaches it only this way.
func _follow_turning(delta: float) -> void:
	var now := _skeleton.global_basis.orthonormalized()
	var step := (_basis_was.inverse() * now).get_rotation_quaternion()
	_basis_was = now
	if step.w < 0.0:
		step = -step
	# From the quaternion's own numbers: get_axis() stops normalising below about half a degree.
	var angle := step.get_angle()
	var along := Vector3(step.x, step.y, step.z)
	var want := Vector3.ZERO
	if along.length_squared() > 1e-16 and angle < TURN_JUMP:
		want = (along.normalized() * angle / delta).limit_length(TURN_MOST)
	var was := _turn
	_turn = _turn.lerp(want, 1.0 - exp(-TURN_EASE * delta))
	_turn_accel = ((_turn - was) / delta).limit_length(TURN_ACCEL_MOST)


## The box a board's kept out of it by, in the board's own space: for the checks.
func box_of(board: Node3D) -> AABB:
	for held: Dictionary in _boards:
		if held["node"] == board:
			return held["box"]
	return AABB()


## How long the last frame's solve took, on this thread.
func last_microseconds() -> float:
	return _sim.last_microseconds()


## The solver's garment for a piece, -1 if it has none: for the checks.
func garment_of(piece: String) -> int:
	return _garments[piece]["index"] if _garments.has(piece) else -1


func sim() -> Variant:
	return _sim


## The palette is every bone the character's skins bind, in the first skin's order.
func _set_palette(root: Node) -> void:
	var bones := PackedInt32Array()
	var binds := PackedFloat32Array()
	for m in root.find_children("*", "MeshInstance3D", true, false):
		var skin := (m as MeshInstance3D).skin
		if skin == null:
			continue
		for i in skin.get_bind_count():
			var bone := _bone_of(skin, i)
			if bone < 0 or _palette.has(bone):
				continue
			_palette[bone] = bones.size()
			bones.append(bone)
			var t := skin.get_bind_pose(i)
			var b := t.basis
			binds.append_array([b.x.x, b.y.x, b.z.x, t.origin.x, b.x.y, b.y.y, b.z.y, t.origin.y, b.x.z, b.y.z, b.z.z, t.origin.z])
	# A bone's parent, in the palette. The chain bones of the fallback's springs count as the bone they hang from: they stay at rest here.
	var parents := PackedInt32Array()
	for bone: int in bones:
		var up := _skeleton.get_bone_parent(bone)
		while up >= 0 and _skeleton.get_bone_name(up).begins_with("Cloth_"):
			up = _skeleton.get_bone_parent(up)
		parents.append(int(_palette.get(up, -1)))
	_sim.set_skeleton(_skeleton, bones, binds, parents)


func _bone_of(skin: Skin, bind: int) -> int:
	var bone := skin.get_bind_bone(bind)
	return bone if bone >= 0 else _skeleton.find_bone(skin.get_bind_name(bind))


## A surface's bone indices in the palette, four to a vertex.
func _palette_bones(skin: Skin, bones: PackedInt32Array) -> PackedInt32Array:
	var to_palette := PackedInt32Array()
	to_palette.resize(skin.get_bind_count())
	for i in skin.get_bind_count():
		to_palette[i] = int(_palette.get(_bone_of(skin, i), 0))
	var out := PackedInt32Array()
	out.resize(bones.size())
	for i in bones.size():
		out[i] = to_palette[bones[i]]
	return out


func _add_body(node: MeshInstance3D) -> void:
	if node.mesh == null or node.skin == null:
		return
	for s in node.mesh.get_surface_count():
		var arrays: Array = node.mesh.surface_get_arrays(s)
		_sim.add_body(arrays[Mesh.ARRAY_VERTEX], _four(arrays[Mesh.ARRAY_WEIGHTS]), _palette_bones(node.skin, arrays[Mesh.ARRAY_BONES]), arrays[Mesh.ARRAY_INDEX])


## A surface's weights as 32-bit floats, as the solver reads them.
func _four(weights: Variant) -> PackedFloat32Array:
	return PackedFloat32Array(weights)


## Whatever he's wearing is on the solver, whatever he isn't is off it. A garment joins the first time it's worn.
func _fit() -> void:
	for m in _wardrobe.root.find_children("wear~*", "MeshInstance3D", true, false):
		var node := m as MeshInstance3D
		var fields := String(node.name).split("~")
		var piece := fields[2]
		var on := _wardrobe.is_worn(piece)
		if _garments.has(piece):
			_sim.set_active(_garments[piece]["index"], on)
		elif on:
			_add_garment(piece, fields[1], node)
	_fit_layers()


## What's worn over what, of what he has on (the profile's `cloth_over`). The skin says nothing about what's between it and the cloth: his legs came through his top's belly.
func _fit_layers() -> void:
	for piece: String in _garments:
		var over := -1
		var wants: String = _rig_over.get(_garments[piece]["slot"], "")
		for other: String in _garments:
			if _garments[other]["slot"] == wants and _wardrobe.is_worn(other):
				over = _garments[other]["index"]
		_sim.set_under(_garments[piece]["index"], over, LAYER_GAP)


func _add_garment(piece: String, slot: String, node: MeshInstance3D) -> void:
	var mesh := node.mesh
	if mesh == null or mesh.get_surface_count() != 1 or not (mesh.surface_get_format(0) & Mesh.ARRAY_FORMAT_TEX_UV2) or node.skin == null:
		return
	var arrays: Array = mesh.surface_get_arrays(0)
	var pos: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var numbers: PackedVector2Array = arrays[Mesh.ARRAY_TEX_UV2]
	var limits := PackedFloat32Array()
	var nears := PackedFloat32Array()
	limits.resize(pos.size())
	nears.resize(pos.size())
	for i in pos.size():
		limits[i] = numbers[i].x
		nears[i] = numbers[i].y
	var index: int = _sim.add_garment(pos, _four(arrays[Mesh.ARRAY_WEIGHTS]), _palette_bones(node.skin, arrays[Mesh.ARRAY_BONES]), arrays[Mesh.ARRAY_INDEX], limits, nears, _fabric(piece, slot))
	_capsules(index, slot)
	_parts(index, LEGS_PART_GAP if slot == "legs" else PART_GAP)
	# Its own mesh from here: not skinned, its vertices rewritten each frame, finer than what's solved. Same material.
	var points: PackedVector3Array = _sim.drawn_points(index)
	var drawn := []
	drawn.resize(Mesh.ARRAY_MAX)
	drawn[Mesh.ARRAY_VERTEX] = points
	drawn[Mesh.ARRAY_NORMAL] = _sim.drawn_normals(index)
	drawn[Mesh.ARRAY_INDEX] = _sim.drawn_indices(index)
	if arrays[Mesh.ARRAY_TEX_UV] != null:
		drawn[Mesh.ARRAY_TEX_UV] = _drawn_uvs(arrays[Mesh.ARRAY_TEX_UV], _sim.drawn_parents(index))
	var cloth := ArrayMesh.new()
	cloth.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, drawn, [], {}, Mesh.ARRAY_FLAG_USE_DYNAMIC_UPDATE)
	cloth.surface_set_material(0, look_of(mesh.surface_get_material(0)))
	cloth.custom_aabb = BOUNDS
	node.mesh = cloth
	node.skin = null
	node.skeleton = NodePath()
	var format := cloth.surface_get_format(0)
	var rid := cloth.get_rid()
	var count := points.size()
	var pos_stride: int = RenderingServer.mesh_surface_get_format_vertex_stride(format, count)
	var nrm_stride: int = RenderingServer.mesh_surface_get_format_normal_tangent_stride(format, count)
	var nrm_offset: int = RenderingServer.mesh_surface_get_format_offset(format, count, Mesh.ARRAY_NORMAL)
	_sim.set_output(index, rid, 0, 0, pos_stride, nrm_offset, nrm_stride, pos_stride * count + nrm_stride * count)
	_garments[piece] = {"index": index, "node": node, "slot": slot}


## The garment's own colour and print on the cloth's look (club_cloth.gdshader).
static func look_of(source: Material) -> ShaderMaterial:
	var look := ShaderMaterial.new()
	look.shader = LOOK
	var from := source as StandardMaterial3D
	if from != null:
		look.resource_name = from.resource_name
		look.set_shader_parameter("albedo", from.albedo_color)
		look.set_shader_parameter("albedo_texture", from.albedo_texture)
	return look


func _fabric(piece: String, slot: String) -> PackedFloat32Array:
	var f: Array = _rig_fabrics.get(piece, _rig_fabrics.get(slot, []))
	# kg/m^2, bending length in cm, shear in N/m; then drive, damp, flex_damp, hem, substep, slack, friction, lag
	var out := PackedFloat32Array([0.2, 2.5, 4.0, 0.5, 0.3, 2e-6, 0.08, 1.0 / 480.0, 0.012, 0.25, 0.03])
	for i in mini(f.size(), out.size()):
		out[i] = f[i]
	return out


## A new vertex's UV is halfway between the two it's between.
func _drawn_uvs(uvs: PackedVector2Array, parents: PackedInt32Array) -> PackedVector2Array:
	var out := PackedVector2Array()
	out.resize(parents.size() / 2)
	for i in out.size():
		var a := parents[i * 2]
		out[i] = uvs[i] if a < 0 else (uvs[a] + uvs[parents[i * 2 + 1]]) * 0.5
	return out


## Which part of a garment each bone is on: the nearest bone above it the profile lists. A garment's parts keep out of each other.
func _parts(index: int, gap: float) -> void:
	var listed := {}
	for i in _rig_parts.size():
		for bone_name: String in _rig_parts[i]:
			listed[_skeleton.find_bone(bone_name)] = i + 1
	var parts := PackedInt32Array()
	parts.resize(_palette.size())
	for bone: int in _palette:
		var up := bone
		while up >= 0 and not listed.has(up):
			up = _skeleton.get_bone_parent(up)
		parts[_palette[bone]] = int(listed.get(up, 0))
	_sim.set_parts(index, parts, gap)


## What he's carrying or standing on, as a box round its deck, the biggest mesh it has: everything the rider has that's a board. A box round all of it is a box round the air between the wheels too, and that's carried right through his belly.
func use_boards(boards: Array) -> void:
	_boards.clear()
	for board: Node3D in boards:
		if board == null:
			continue
		var deck := AABB()
		var deck_node: MeshInstance3D = null
		for m in board.find_children("*", "MeshInstance3D", true, false):
			var mesh_node := m as MeshInstance3D
			if mesh_node.mesh == null:
				continue
			var box: AABB = (board.global_transform.affine_inverse() * mesh_node.global_transform) * mesh_node.get_aabb()
			if box.get_volume() > deck.get_volume():
				deck = box
				deck_node = mesh_node
		if deck.has_volume():
			_boards.append({"node": board, "box": _flat_of(deck, deck_node, board.global_transform.affine_inverse() * deck_node.global_transform)})


## A deck curves up at both ends, so a box round all of it has its middle above the wood, and a sleeve on the grip side, being below that middle, went out through the wood. Only as thick as the flat of its middle third.
func _flat_of(whole: AABB, node: MeshInstance3D, to_board: Transform3D) -> AABB:
	var long_way := whole.get_longest_axis_index()
	var thin_way := whole.get_shortest_axis_index()
	var middle := whole.get_center()[long_way]
	var lowest := INF
	var highest := -INF
	for s in node.mesh.get_surface_count():
		for v: Vector3 in node.mesh.surface_get_arrays(s)[Mesh.ARRAY_VERTEX]:
			var p := to_board * v
			if absf(p[long_way] - middle) < whole.size[long_way] / 6.0:
				lowest = minf(lowest, p[thin_way])
				highest = maxf(highest, p[thin_way])
	if lowest >= highest:
		return whole
	var flat := whole
	flat.position[thin_way] = lowest
	flat.size[thin_way] = highest - lowest
	return flat


func _set_boxes() -> void:
	var numbers := PackedFloat32Array()
	var into_skeleton := _skeleton.global_transform.affine_inverse()
	for held: Dictionary in _boards:
		var board: Node3D = held["node"]
		if not is_instance_valid(board) or not board.is_visible_in_tree():
			continue
		var box: AABB = held["box"]
		var t: Transform3D = into_skeleton * board.global_transform
		var scale_of := t.basis.get_scale()
		var b := t.basis.orthonormalized()
		var o := t * box.get_center()
		# Thrown clear in a bail or flicked in a flip it goes through the cloth in a frame or two: no use pushing anything out of the way of that.
		var moved := o.distance_to(held.get("was", o))
		held["was"] = o
		if moved > BOARD_TOO_FAST:
			continue
		var half := box.size * 0.5 * scale_of
		numbers.append_array([b.x.x, b.y.x, b.z.x, o.x, b.x.y, b.y.y, b.z.y, o.y, b.x.z, b.y.z, b.z.z, o.z, half.x, half.y, half.z])
	_sim.set_boxes(numbers)


## How bright the sun is, for the cloth's look: the brighter it is the further out its shadows are looked up. A scene's sun is found once.
func _follow_sun(delta: float) -> void:
	if not is_instance_valid(_sun):
		_sun_search_in -= delta
		if _sun_search_in > 0.0:
			return
		_sun_search_in = SUN_SEARCH_EVERY
		var scene := get_tree().current_scene if is_inside_tree() else null
		if scene == null:
			return
		for light in scene.find_children("*", "DirectionalLight3D", true, false):
			if (light as DirectionalLight3D).shadow_enabled:
				_sun = light
				break
		if not is_instance_valid(_sun):
			return
	var out: float = lerpf(SUN_OUT_DIM, SUN_OUT_BRIGHT, smoothstep(SUN_DIM, SUN_BRIGHT, _sun.light_energy))
	if is_equal_approx(out, _sun_out) and _garments.size() == _sun_garments:
		return
	_sun_out = out
	_sun_garments = _garments.size()
	for piece: String in _garments:
		var look := (_garments[piece]["node"] as MeshInstance3D).mesh.surface_get_material(0) as ShaderMaterial
		look.set_shader_parameter("sun_out", out)


## The limbs that can come up against this slot's clothes: [bone, bone at its other end, press], off the profile. Its radius is measured off his skin.
func _capsules(index: int, slot: String) -> void:
	var triples := PackedInt32Array()
	for c: Array in _rig_capsules.get(slot, []):
		var a := _skeleton.find_bone(c[0])
		var b := _skeleton.find_bone(c[1])
		if a < 0 or b < 0 or not _palette.has(a) or not _palette.has(b):
			push_warning("ClubCloth: no capsule %s to %s" % [c[0], c[1]])
			continue
		triples.append_array([_palette[a], _palette[b], int(c[2])])
	_sim.set_capsules(index, triples, PackedFloat32Array())
