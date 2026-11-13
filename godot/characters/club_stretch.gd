class_name ClubStretch
extends SkeletonModifier3D
## club_stretch.gd -- a garment HELD BETWEEN TWO MOVING PARTS: the engine's stretch bones, in Godot (4.4+).
##
##   ClubStretch.attach(skeleton)     # ClubHair.attach() calls it first, so every character that swings gets it
##
## A bone named Stretch_<x> keeps its head where its parent carries it (REVA's elbow), turns to point at the head of
## Anchor_<x> (a point on her back, a child of her spine), and STRETCHES along that line by the distance over its rest
## distance -- the engine's stretchTick (reva_native/engine/pack.inc), Blender's Stretch To, the rig the industry uses for a
## drape between two limbs. Its children (the cloth chains hung from it) turn with it and are never scaled. It runs
## as the FIRST modifier under the skeleton, so ClubHair's springs swing the chains from where it put them.
##
## Why it exists (09-29): REVA's off-shoulder jacket hangs at her elbows, round her back. Weighted by blending the
## arms into the spine, linear-blend skinning put the band half way between where each would carry it and a flap
## stood out of her back: the rig's fault, not the model's.
## Godot's AimModifier3D can aim a bone but not stretch it -- hence this.

var pairs := []          # [stretch bone, anchor bone, rest line in the stretch bone's frame]


static func attach(sk: Skeleton3D) -> ClubStretch:
	var m := ClubStretch.new()
	m.name = "ClubStretch"
	for b in sk.get_bone_count():
		var nm := sk.get_bone_name(b)
		if not nm.begins_with("Stretch_"):
			continue
		var a := sk.find_bone("Anchor_" + nm.substr(8))
		if a < 0:
			push_warning("ClubStretch: %s has no Anchor_%s -- it stays a plain bone" % [nm, nm.substr(8)])
			continue
		var rb := sk.get_bone_global_rest(b)
		var ra := sk.get_bone_global_rest(a)
		var line := rb.basis.inverse() * (ra.origin - rb.origin)
		if line.length() < 1e-6:
			continue
		m.pairs.append([b, a, line])
	if m.pairs.is_empty():
		m.free()
		return null
	sk.add_child(m)
	sk.move_child(m, 0)                   # before the springs
	print("ClubStretch: %d stretch bone(s) on %s" % [m.pairs.size(), sk.name])
	return m


func _process_modification_with_delta(_delta: float) -> void:
	var sk := get_skeleton()
	if sk == null:
		return
	for p in pairs:
		var b: int = p[0]
		var a: int = p[1]
		var g := sk.get_bone_global_pose(b)
		var cur: Vector3 = g.basis * (p[2] as Vector3)          # its rest line, as its parent carries it now
		var to := sk.get_bone_global_pose(a).origin - g.origin
		if cur.length() < 1e-6 or to.length() < 1e-6:
			continue
		var turned := Basis(Quaternion(cur.normalized(), to.normalized())) * g.basis
		var k := clampf(to.length() / cur.length(), 0.2, 5.0)
		var u := to.normalized()
		# (I + (k-1) u u^T): the stretch, along the line only
		var stretch := Basis(Vector3(1, 0, 0) + (k - 1.0) * u.x * u, Vector3(0, 1, 0) + (k - 1.0) * u.y * u,
				Vector3(0, 0, 1) + (k - 1.0) * u.z * u)
		# the children turn with it, unscaled: their global poses from the TURNED frame, set after the stretch
		var kids := []
		for c in sk.get_bone_children(b):
			kids.append([c, Transform3D(turned, g.origin) * sk.get_bone_pose(c)])
		sk.set_bone_global_pose(b, Transform3D(stretch * turned, g.origin))
		for kc in kids:
			sk.set_bone_global_pose(kc[0], kc[1])
