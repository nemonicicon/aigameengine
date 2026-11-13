class_name ClubWardrobe
extends Node
## club_wardrobe.gd -- a character's WARDROBE in Godot 4: every piece of clothing (and each hairstyle) is its
## own mesh on the character's one skeleton, tagged in its name exactly as the engine reads it:
##
##   wear~<slot>~<piece>~<outfit>+<outfit>   a piece, and the outfits it belongs to
##   wear~...~<outfits>~h=a,b                the part of a piece that pieces a and b cover (a shirt under a
##                                           jacket): drawn while its piece is worn and none of a, b is
##   skin~h=a,b                              skin that pieces a and b cover: hidden while either is worn
##                                           (nothing pokes through a worn garment, nothing is missing without it)
##   anything else                           always drawn (her head, the skin no garment covers)
##
##   var reva := preload("res://characters/reva902/reva902.glb").instantiate()
##   add_child(reva)
##   var w := ClubWardrobe.attach(reva)       # dresses her in her first outfit
##   w.wear_outfit("street")                  # every piece of an outfit, nothing else
##   w.take_off("jacket"); w.put_on("jacket") # one piece at a time
##   print(w.outfits(), w.pieces(), w.worn_pieces())
##
## Physics rides the skeleton, not the meshes: ClubHair.attach(reva) swings every hair and cloth chain and springs
## the soft mass, whatever she is wearing. Built for REVA (by the tailor that sewed her clothes): any
## character whose builder tags its meshes this way gets the same wardrobe.

signal changed             # what he's wearing changed

var root: Node
var _meshes := []          # [{node, kind, piece, slot, outfits:Array, hiders:Array}]
var _worn := {}            # piece -> true
var _outfits := []         # in the order they first appear


static func attach(r: Node, outfit: String = "") -> ClubWardrobe:
	var w := ClubWardrobe.new()
	w.name = "Wardrobe"
	w.root = r
	r.add_child(w)
	w._scan()
	if outfit != "":
		w.wear_outfit(outfit)
	elif not w._outfits.is_empty():
		w.wear_outfit(w._outfits[0])
	return w


static func _tag_of(n: Node) -> String:
	# the glTF node's name, or its mesh's (Godot may have tidied one of them)
	var s := String(n.name)
	if not (s.begins_with("wear~") or s.begins_with("skin")) and n is MeshInstance3D and n.mesh:
		s = n.mesh.resource_name
	var dot := s.find(".")
	return s.substr(0, dot) if dot >= 0 else s


func _scan() -> void:
	_meshes.clear()
	for n in root.find_children("*", "MeshInstance3D", true, false):
		var tag := _tag_of(n)
		var m := {"node": n, "kind": "plain", "piece": "", "slot": "", "outfits": [], "hiders": []}
		if tag.begins_with("wear~"):
			var f := tag.split("~")
			if f.size() >= 3:
				m.kind = "wear"; m.slot = f[1]; m.piece = f[2]
				m.outfits = Array(f[3].split("+")) if f.size() > 3 else []
				if f.size() > 4 and f[4].begins_with("h="):
					m.hiders = Array(f[4].substr(2).split(","))
				for o in m.outfits:
					if not _outfits.has(o):
						_outfits.append(o)
		elif tag.begins_with("skin~h="):
			m.kind = "skin"
			m.hiders = Array(tag.substr(7).split(","))
		_meshes.append(m)


func outfits() -> Array:
	return _outfits.duplicate()


func pieces() -> Array:
	var out := []
	for m in _meshes:
		if m.kind == "wear" and not out.has(m.piece):
			out.append(m.piece)
	return out


func worn_pieces() -> Array:
	return _worn.keys()


func slot_of(piece: String) -> String:
	for m in _meshes:
		if m.piece == piece:
			return m.slot
	return ""


func wear_outfit(outfit: String) -> void:
	if not _outfits.has(outfit):
		push_warning("ClubWardrobe: %s has no outfit '%s' (%s)" % [root.name, outfit, ", ".join(_outfits)])
		return
	_worn.clear()
	for m in _meshes:
		if m.kind == "wear" and m.outfits.has(outfit):
			_worn[m.piece] = true
	_apply()
	print("ClubWardrobe: %s wears '%s' -- %s" % [root.name, outfit, ", ".join(_worn.keys())])


func put_on(piece: String, replace_slot: bool = true) -> void:
	# putting on a piece takes off whatever else fills its slot (a second top goes over nothing)
	if replace_slot:
		var slot := slot_of(piece)
		for p in _worn.keys():
			if slot_of(p) == slot:
				_worn.erase(p)
	_worn[piece] = true
	_apply()


func take_off(piece: String) -> void:
	_worn.erase(piece)
	_apply()


func is_worn(piece: String) -> bool:
	return _worn.has(piece)


func _apply() -> void:
	for m in _meshes:
		match m.kind:
			"wear":
				var covered := false
				for h in m.hiders:
					if _worn.has(h):
						covered = true
						break
				m.node.visible = _worn.has(m.piece) and not covered
			"skin":
				var hidden := false
				for h in m.hiders:
					if _worn.has(h):
						hidden = true
						break
				m.node.visible = not hidden
			_:
				m.node.visible = true
	changed.emit()
