class_name ClubHair
## club_hair.gd -- hair physics for ANY character from the club, on Godot's SpringBoneSimulator3D (Godot 4.4+).
##
##   var who := preload("res://characters/x3d/x3d.glb").instantiate()
##   add_child(who)
##   ClubHair.attach(who)                  # every hair chain it has, swinging
##
## The chains are found the way the club engine's own solver finds them (crowd_kms.c, H1): a bone whose name
## says tail / pony / braid / hair / bang / tress / ribbon, whose parent's does not, starts a chain, and the chain
## walks its matching children (each matching child of the root is its own chain -- braids come in pairs).
## The simulator works in world space (center_from = world origin), so hair swings when the character turns,
## lifts on a jump and trails while it moves -- as the club engine does since 09-27. No collision: like the
## club's solver, the stiffness keeps it near its pose. Measured on x3d: standing, it hangs where it is posed
## (on vs off 0.1-0.7% of pixels); riding a 4 m circle at 2 m/s, it swings out and trails.

const NAMES := ["tail", "pony", "braid", "hair", "bang", "tress", "ribbon"]


static func is_chain_name(bone_name: String) -> bool:
	var lo := bone_name.to_lower()
	for n in NAMES:
		if lo.contains(n):
			return true
	return false


## [[root, end], ...] for every chain in the skeleton, as the club's miner would find them
static func find_chains(sk: Skeleton3D) -> Array:
	var out := []
	for b in sk.get_bone_count():
		if not is_chain_name(sk.get_bone_name(b)):
			continue
		var par := sk.get_bone_parent(b)
		if par >= 0 and is_chain_name(sk.get_bone_name(par)):
			continue                              # not a root
		var starts := []
		for c in sk.get_bone_children(b):
			if is_chain_name(sk.get_bone_name(c)):
				starts.append(c)
		if starts.is_empty():
			continue                              # a lone bone cannot swing
		for s in starts:
			var cur: int = s
			while true:
				var nxt := -1
				for c in sk.get_bone_children(cur):
					if is_chain_name(sk.get_bone_name(c)):
						nxt = c
						break
				if nxt < 0:
					break
				cur = nxt
			out.append([sk.get_bone_name(b), sk.get_bone_name(cur)])
	return out


static func attach(root: Node, stiffness: float = 3.5, drag: float = 0.5, gravity: float = 1.0) -> SpringBoneSimulator3D:
	var found := root.find_children("*", "Skeleton3D", true, false)
	if found.is_empty():
		push_error("ClubHair: no Skeleton3D under %s" % root.name)
		return null
	var sk: Skeleton3D = found[0]
	var chains := find_chains(sk)
	if chains.is_empty():
		push_warning("ClubHair: %s has no hair chains (bone names with %s)" % [root.name, ", ".join(NAMES)])
		return null
	var sim := SpringBoneSimulator3D.new()
	sim.name = "HairSprings"
	sk.add_child(sim)
	sim.set_setting_count(chains.size())
	for i in chains.size():
		sim.set_root_bone_name(i, chains[i][0])
		sim.set_end_bone_name(i, chains[i][1])
		sim.set_extend_end_bone(i, true)          # the last bone swings too
		sim.set_end_bone_length(i, 0.05)
		sim.set_stiffness(i, stiffness)
		sim.set_drag(i, drag)
		sim.set_gravity(i, gravity)
		sim.set_radius(i, 0.02)
	print("ClubHair: %s -- %d chain(s): %s" % [root.name, chains.size(), str(chains)])
	return sim
