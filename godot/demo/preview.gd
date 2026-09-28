extends SceneTree
## preview.gd -- one character on a plain stage from four sides, one Movie Maker frame per side, for its README.
##
##   godot --path godot --write-movie C:/out/p/f.png --fixed-fps 30 --quit-after 6 --resolution 480x720 \
##         --script res://demo/preview.gd -- file=res://characters/x3d/x3d.glb clip=z_rm_sk_ride t=0.5
##
## Frame 0 warms up (textures, shaders); frames 1-4 are front, three-quarter, side, back (the character faces
## +Z, glTF's forward). ship_character.py tiles 1-4 into preview_godot.png. The pose is the clip held at `t`
## seconds; hair=1 hangs ClubHair on it first (the frames then show its settled rest, not a swing).

var who: Node3D
var cam: Camera3D
var frame := 0
var center := Vector3.ZERO
var dist := 3.0
const ANGLES := [0.0, 40.0, 90.0, 180.0]


func _init() -> void:
	var args := {}
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=")
		if kv.size() == 2:
			args[kv[0]] = kv[1]
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.86, 0.86, 0.87)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(1, 1, 1)
	env.ambient_light_energy = 0.55
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	var we := WorldEnvironment.new()
	we.environment = env
	get_root().add_child(we)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-40.0, -35.0, 0.0)
	sun.light_energy = 1.1
	sun.shadow_enabled = true
	get_root().add_child(sun)
	var ground := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(12, 12)
	var gm := StandardMaterial3D.new()
	gm.albedo_color = Color(0.72, 0.72, 0.73)
	pm.material = gm
	ground.mesh = pm
	get_root().add_child(ground)
	var ps: PackedScene = load(args.get("file", "res://characters/x3d/x3d.glb"))
	who = ps.instantiate()
	get_root().add_child(who)
	var aps := who.find_children("*", "AnimationPlayer", true, false)
	var clip: String = args.get("clip", "z_rm_sk_ride")
	if not aps.is_empty() and (aps[0] as AnimationPlayer).has_animation(clip):
		var ap: AnimationPlayer = aps[0]
		ap.play(clip)
		ap.seek(float(args.get("t", "0.5")), true)
		ap.pause()
	if args.get("hair", "0") == "1":
		ClubHair.attach(who)
	cam = Camera3D.new()
	cam.keep_aspect = Camera3D.KEEP_HEIGHT     # the fov is VERTICAL: Movie Maker writes this project 1280x720
	cam.fov = 32.0
	get_root().add_child(cam)
	cam.current = true


## ⛔ NOT IN _init: a SceneTree script's nodes are not inside the tree until the first frame, so their global
## transforms (and look_at) fail there. Frame it on the first frame instead.
func _frame_it() -> void:
	var box := AABB()
	var first := true
	for mi in who.find_children("*", "MeshInstance3D", true, false):
		var b: AABB = (mi as MeshInstance3D).global_transform * (mi as MeshInstance3D).get_aabb()
		box = b if first else box.merge(b)
		first = false
	center = box.get_center()
	var h: float = max(box.size.y, 0.5)
	dist = h * 0.62 / tan(deg_to_rad(cam.fov * 0.5))


func _place(i: int) -> void:
	var a := deg_to_rad(ANGLES[clamp(i, 0, ANGLES.size() - 1)])
	cam.look_at_from_position(center + Vector3(sin(a) * dist, dist * 0.10, cos(a) * dist), center, Vector3.UP)


func _process(_delta: float) -> bool:
	if frame == 0:
		_frame_it()
	_place(max(frame - 1, 0))
	frame += 1
	return false
