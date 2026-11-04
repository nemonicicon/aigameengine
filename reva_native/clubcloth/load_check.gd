extends SceneTree

## Does the library load in Godot on this machine, and does it really solve here? Two tubes of cloth round a swinging bone, so it runs on its threads. Run in a small project with only the library in it (a game's .github/workflows): godot --headless --path <project> -s res://load_check.gd. Exits 1 if not.

const RINGS := 12
const SEGS := 12
const SOLVER := &"ClubClothSim"


func _init() -> void:
	call_deferred("_run")


func _fail(why: String) -> void:
	print("load check failed: ", why)
	quit(1)


## A tube down from y = 0.4 at this radius, all on bone 0, as a mesh's arrays: positions, 4 weights and 4 bones a vertex, triangles.
func _tube(radius: float) -> Dictionary:
	var pos := PackedVector3Array()
	var wgt := PackedFloat32Array()
	var bon := PackedInt32Array()
	var idx := PackedInt32Array()
	for r in RINGS + 1:
		for s in SEGS:
			var a := TAU * s / SEGS
			pos.append(Vector3(cos(a) * radius, 0.4 - 0.4 * r / RINGS, sin(a) * radius))
			wgt.append_array([1.0, 0.0, 0.0, 0.0])
			bon.append_array([0, 0, 0, 0])
	for r in RINGS:
		for s in SEGS:
			var a := r * SEGS + s
			var b := r * SEGS + (s + 1) % SEGS
			var c := a + SEGS
			var d := b + SEGS
			idx.append_array([a, c, b, b, c, d])
	return {"pos": pos, "wgt": wgt, "bon": bon, "idx": idx}


## Cloth at this radius over what's under it: held at the top ring, free to go 10 cm below it.
func _add_garment(sim: Object, radius: float) -> int:
	var t := _tube(radius)
	var limits := PackedFloat32Array()
	var nears := PackedFloat32Array()
	limits.resize(t.pos.size())
	nears.resize(t.pos.size())
	for i in t.pos.size():
		limits[i] = 0.0 if i < SEGS else 0.1
		nears[i] = 0.005
	return sim.add_garment(t.pos, t.wgt, t.bon, t.idx, limits, nears, PackedFloat32Array([0.2, 2.5, 4.0, 0.5, 0.3, 2e-6, 0.08, 1.0 / 480.0, 0.012, 0.25, 0.03]))


func _run() -> void:
	if not ClassDB.class_exists(SOLVER):
		_fail("no %s: the library didn't load" % SOLVER)
		return
	print("the library loaded on ", OS.get_name(), ", ", Engine.get_architecture_name())
	var skeleton := Skeleton3D.new()
	root.add_child(skeleton)
	skeleton.add_bone("limb")
	var sim: Object = ClassDB.instantiate(SOLVER)
	sim.set_skeleton(skeleton, PackedInt32Array([0]), PackedFloat32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0]), PackedInt32Array([-1]))
	var body := _tube(0.05)
	sim.add_body(body.pos, body.wgt, body.bon, body.idx)
	var inner := _add_garment(sim, 0.07)
	var outer := _add_garment(sim, 0.09)
	sim.set_under(outer, inner, 0.005)
	var at_rest: PackedVector3Array = sim.targets(inner)
	var moved := 0.0
	var slowest := 0.0
	for f in 300:
		var t := f / 60.0
		skeleton.set_bone_pose_rotation(0, Quaternion(Vector3.RIGHT, 0.8 * sin(t * 6.0)))
		sim.step(1.0 / 60.0, Vector3(0, -9.8, 0), Vector3.ZERO, Vector3(0, 0, 2.0 * sin(t * 2.0)))
		slowest = maxf(slowest, sim.last_microseconds() / 1000.0)
		for g in [inner, outer]:
			var x: PackedVector3Array = sim.points(g)
			var y: PackedVector3Array = sim.targets(g)
			for k in x.size():
				if not x[k].is_finite() or x[k].length() > 10.0:
					_fail("a point blew up at frame %d" % f)
					return
				moved = maxf(moved, x[k].distance_to(y[k]))
	if at_rest.is_empty() or moved < 0.005:
		_fail("the cloth never moved off its body (%.4f m)" % moved)
		return
	print("two garments solved for 300 frames: the most any point went from its body %.3f m, the slowest frame %.2f ms" % [moved, slowest])
	sim = null
	skeleton.queue_free()
	print("load check passed")
	quit(0)
