extends SceneTree
## check_pose.gd -- does GODOT pose our moves where the club engine does?
##   godot --headless --path godot --script res://demo/check_pose.gd -- clip=z_rm_sk_kf frames=0,9,15,21
##   godot ... -- file=moves clip=z_upright frames=0,4 before=z_rm_skiphand@1.5
##   godot ... -- file=clanker clip=z_rm_sk_kf frames=0,9,15,21 all=1     (or file=res://<any .glb>)
## Prints, for each frame, the world position of a few joints (and the skateboard's origin and nose axis)
## as `POSE <frame> <name> x y z`, for check_pose_compare.py to hold against the pack's own forward
## kinematics -- a second, independent path to the same numbers.
## `before=clip@sec` plays another move first: a bone the move does not key must NOT keep the last
## move's pose (the club engine puts it at rest; Godot keeps it unless a track holds it).

var scene: Node3D
var ap: AnimationPlayer
var sk: Skeleton3D
var board: Node3D
var frames: Array = []
var clip := "z_rm_sk_kf"
var before := ""
var before_t := 0.0
var step := 0


func _init() -> void:
	var file := "res://animations/reva_skate_moves.glb"
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=")
		if kv.size() != 2:
			continue
		match kv[0]:
			"clip":
				clip = kv[1]
			"frames":
				for f in kv[1].split(","):
					frames.append(int(f))
			"file":
				if kv[1].begins_with("res://"):
					file = kv[1]
				elif kv[1] == "clanker":
					file = "res://characters/clanker/clanker.glb"
				else:
					file = "res://animations/reva_moves.glb" if kv[1] == "moves" else "res://animations/reva_skate_moves.glb"
			"before":
				before = kv[1].split("@")[0]
				before_t = float(kv[1].split("@")[1])
	if frames.is_empty():
		frames = [0, 9, 15, 21]
	var ps: PackedScene = load(file)
	scene = ps.instantiate()
	get_root().add_child(scene)
	ap = scene.find_children("*", "AnimationPlayer", true, false)[0]
	sk = scene.find_children("*", "Skeleton3D", true, false)[0]
	board = scene.get_node_or_null("skateboard")


func _process(_delta: float) -> bool:
	if step >= frames.size():
		quit()
		return true
	var f: int = frames[step]
	if before != "":
		ap.play(before)
		ap.seek(before_t, true)
		ap.advance(0.0)
	ap.play(clip)
	ap.seek(f / 30.0, true)
	ap.advance(0.0)
	sk.force_update_all_bone_transforms()
	var bones := ["Hips", "Head", "LeftFoot", "RightFoot", "LeftHand", "RightHand"]
	if OS.get_cmdline_user_args().has("all=1"):
		bones = []
		for bi in sk.get_bone_count():
			bones.append(sk.get_bone_name(bi))
	for bn in bones:
		var i := sk.find_bone(bn)
		var p: Vector3 = (sk.global_transform * sk.get_bone_global_pose(i)).origin
		print("POSE %d %s %.6f %.6f %.6f" % [f, bn, p.x, p.y, p.z])
	if board:
		var o := board.global_transform.origin
		var x := board.global_transform.basis.x
		print("POSE %d board %.6f %.6f %.6f" % [f, o.x, o.y, o.z])
		print("POSE %d boardX %.6f %.6f %.6f" % [f, x.x, x.y, x.z])
	step += 1
	return false
