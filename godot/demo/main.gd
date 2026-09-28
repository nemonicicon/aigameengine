extends Node3D
## demo/main.gd -- the club's camera rigs and skate moves in a Godot 4 scene, built in code.
##
## Reva rides a ring among pillars on z_rm_sk_ride (the board animated by the move itself); a filmer
## rides inside her on z_rm_sk_film with a phone on her RIGHT hand bone. One ClubCamera shows it.
## F5 runs the tracking lens. A test chooses a rig and records it with Movie Maker:
##   godot --path godot --write-movie out/track/f.png --fixed-fps 60 --quit-after 360 -- mode=track log=1
## mode: tripod pan track orbit hold move      extras: shake=<m> fish=<0..1> feed=<0|1|2> log=1

const SKATE := preload("res://animations/reva_skate_moves.glb")
const RING := 5.0        # reva's circle, metres
const SPEED := 2.5       # metres per second

var args := {}
var cam: ClubCamera
var rider: Node3D
var filmer: Node3D
var hips_bone := -1
var rider_sk: Skeleton3D
var hand: BoneAttachment3D
var t := 0.0
var frame := 0


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=")
		if kv.size() == 2:
			args[kv[0]] = kv[1]
	_world()
	rider = _skater("z_rm_sk_ride", 1)
	filmer = _skater("z_rm_sk_film", 2)          # render layer 2: the phone's own camera does not see her
	rider_sk = rider.find_children("*", "Skeleton3D", true, false)[0]
	hips_bone = rider_sk.find_bone("Hips")
	var fsk: Skeleton3D = filmer.find_children("*", "Skeleton3D", true, false)[0]
	hand = BoneAttachment3D.new()
	hand.bone_name = "RightHand"                  # the camera hand in z_rm_sk_film (held out at shoulder height)
	fsk.add_child(hand)
	_place(0.0)
	_camera(args.get("mode", "track"))


func _world() -> void:
	var we := WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	var sky := Sky.new()
	sky.sky_material = ProceduralSkyMaterial.new()
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	we.environment = env
	add_child(we)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50.0, 30.0, 0.0)
	sun.shadow_enabled = true
	add_child(sun)
	# a ground with a grid on it, so a moving lens reads as moving
	var img := Image.create(256, 256, false, Image.FORMAT_RGB8)
	img.fill(Color(0.58, 0.58, 0.56))
	for k in range(0, 256, 32):
		img.fill_rect(Rect2i(k, 0, 3, 256), Color(0.38, 0.38, 0.40))
		img.fill_rect(Rect2i(0, k, 256, 3), Color(0.38, 0.38, 0.40))
	var gm := StandardMaterial3D.new()
	gm.albedo_texture = ImageTexture.create_from_image(img)
	gm.uv1_scale = Vector3(20.0, 20.0, 1.0)
	var plane := PlaneMesh.new()
	plane.size = Vector2(80.0, 80.0)
	plane.material = gm
	var ground := MeshInstance3D.new()
	ground.mesh = plane
	add_child(ground)
	# pillars round the ring: something for every lens to move past
	for i in 16:
		var ang := TAU * i / 16.0
		var bm := BoxMesh.new()
		bm.size = Vector3(0.5, 3.0, 0.5)
		var pm := StandardMaterial3D.new()
		pm.albedo_color = Color.from_hsv(float(i) / 16.0, 0.45, 0.85)
		bm.material = pm
		var p := MeshInstance3D.new()
		p.mesh = bm
		p.position = Vector3(cos(ang) * 9.5, 1.5, sin(ang) * 9.5)
		add_child(p)


func _skater(anim_name: String, layer: int) -> Node3D:
	var s: Node3D = SKATE.instantiate()
	add_child(s)
	var ap: AnimationPlayer = s.find_children("*", "AnimationPlayer", true, false)[0]
	ap.get_animation(anim_name).loop_mode = Animation.LOOP_LINEAR
	ap.play(anim_name)
	for mi in s.find_children("*", "MeshInstance3D", true, false):
		(mi as MeshInstance3D).layers = layer
	return s


## Both skaters on their circles at time t. A rider rolls ALONG her board: the move's board nose is the
## model's +X, so +X goes along the travel and she stands sideways to it, as a skater does.
func _place(time: float) -> void:
	var ang := time * SPEED / RING
	_put(rider, ang, RING)
	_put(filmer, ang - 1.7 / RING, RING - 0.9)    # ~1.7 m behind her, on the INSIDE of her line (one side, always)


func _put(s: Node3D, ang: float, radius: float) -> void:
	var p := Vector3(cos(ang) * radius, 0.0, sin(ang) * radius)
	var along := Vector3(-sin(ang), 0.0, cos(ang))
	s.global_transform = Transform3D(Basis(along, Vector3.UP, along.cross(Vector3.UP)), p)


func _camera(mode: String) -> void:
	cam = ClubCamera.new()
	cam.subject = rider
	cam.aim_height = 0.63
	cam.lens_fov = 44.0
	cam.tripod_eye = Vector3(7.2, 2.0, 2.4)          # inside the pillar ring, outside hers
	cam.tripod_aim = Vector3(0.0, 0.6, 0.0)
	cam.track_distance = 2.6
	cam.track_height = 1.0
	cam.orbit_radius = 2.6
	cam.orbit_height = 0.8
	cam.orbit_deg0 = 0.0
	cam.orbit_deg1 = 180.0
	cam.orbit_seconds = 4.0
	match mode:
		"tripod":
			cam.mode = ClubCamera.Mode.TRIPOD
		"pan":
			cam.mode = ClubCamera.Mode.PAN
		"orbit":
			cam.mode = ClubCamera.Mode.ORBIT
		"hold":
			cam.mode = ClubCamera.Mode.HOLD
			cam.filmer_hand = hand
			cam.filmer_body = filmer
			cam.cull_mask = 0xFFFFF & ~2          # the filmer is behind the lens: never drawn in her own footage
		"move":
			cam.mode = ClubCamera.Mode.MOVE
			cam.move_to_eye = Vector3(2.2, 1.3, 2.2)
			cam.move_to_aim = Vector3(0.0, 0.8, 0.0)
			cam.move_seconds = 3.0
		_:
			cam.mode = ClubCamera.Mode.TRACK
	cam.shake_amplitude = float(args.get("shake", "0"))
	cam.fish = float(args.get("fish", "0"))
	cam.feed = int(args.get("feed", "0"))
	var start_eye := Vector3(6.0, 3.0, 6.0) if mode == "move" else cam.tripod_eye
	var start_aim := Vector3(0.0, 0.6, 0.0)
	var layer := CanvasLayer.new()
	var rect := ColorRect.new()
	rect.set_anchors_preset(Control.PRESET_FULL_RECT)
	rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var mat := ShaderMaterial.new()
	mat.shader = load("res://club_camera_post.gdshader")
	rect.material = mat
	layer.add_child(rect)
	add_child(layer)
	cam.post_material = mat
	cam.position = start_eye
	add_child(cam)
	cam.look_at(start_aim, Vector3.UP)
	cam.cut()
	cam.current = true
	print("DEMO mode %s shake %.3f fish %.2f feed %d" % [mode, cam.shake_amplitude, cam.fish, cam.feed])


func _process(delta: float) -> void:
	t += delta
	frame += 1
	_place(t)
	if args.get("log", "0") == "1":
		call_deferred("_log")


## after everyone has moved: where the lens is, where it looks, and where reva is in its frame
func _log() -> void:
	var eye := cam.global_position
	var fwd := -cam.global_transform.basis.z
	var hips := rider_sk.global_transform * rider_sk.get_bone_global_pose(hips_bone).origin
	var vp := get_viewport().get_visible_rect().size
	var sp := cam.unproject_position(hips)
	var inview := (not cam.is_position_behind(hips)) and sp.x >= 0.0 and sp.y >= 0.0 and sp.x <= vp.x and sp.y <= vp.y
	print("CAM %d %.4f  %.5f %.5f %.5f  %.5f %.5f %.5f  %.1f %.1f %d  %.3f" % [frame, t, eye.x, eye.y, eye.z,
		fwd.x, fwd.y, fwd.z, sp.x, sp.y, 1 if inview else 0, cam.fov])
