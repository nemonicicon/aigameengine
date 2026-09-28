## club_camera.gd -- the club engine's camera rigs for Godot 4.
##
## Translated from reva_native/crowd_kms.c and VERIFIED in Godot 4.7.2 (demo/, demo/score_rigs.py):
## camSpring (8281), camTrackTick (8301), camShakeApply (8379), fishFov (2551). CAMERA_PORT.md says
## what each rig must do and how to prove it (section 5). Units here are METRES (the club's / 1.75).
##
## Attach to a Camera3D. For the fisheye / phone frame, add a CanvasLayer with a full-rect ColorRect
## using club_camera_post.gdshader and assign its ShaderMaterial to `post_material`.
extends Camera3D
class_name ClubCamera

enum Mode { TRIPOD, MOVE, TRACK, PAN, ORBIT, HOLD }

@export var mode: Mode = Mode.TRIPOD
@export_group("Subject")
@export var subject: Node3D                   ## the body the lens follows (TRACK / PAN / ORBIT / HOLD aim)
@export var subject_faces_plus_z := true      ## glTF characters face +Z; set false for a -Z-forward rig
@export var aim_height := 0.63                ## above the subject's origin: a rider 0.63 m (1.1 u), a body on the ground 0.31 m
@export_group("Tripod / pan")
@export var tripod_eye := Vector3(0, 1.6, 4)
@export var tripod_aim := Vector3(0, 1.0, 0)
@export_group("Track")
@export var track_distance := 1.7
@export var track_height := 0.9
@export var track_height_absolute := false    ## the club's `=h`: the eye stays at world y = track_height
@export var track_azimuth_deg := 0.0          ## 0 = behind the subject, 90 = its front
@export_group("Orbit")
@export var orbit_radius := 2.4
@export var orbit_height := 0.6
@export var orbit_deg0 := 0.0                 ## world heading the lens LOOKS along: 0 = on the body's -z side looking +z
@export var orbit_deg1 := 90.0
@export var orbit_seconds := 4.0
@export_group("Move (dolly / crane / push-in)")
@export var move_to_eye := Vector3(0, 1.2, 2)
@export var move_to_aim := Vector3(0, 1.0, 0)
@export var move_seconds := 3.0
@export_group("Hand-held (the phone)")
@export var filmer_hand: Node3D               ## a BoneAttachment3D on the filmer's GRIP hand
@export var filmer_body: Node3D               ## the filmer (the phone is held out, away from her chest)
@export var hold_drop := 0.0                  ## lower the phone toward the ground (the low pass)
@export_group("Shake and lens")
@export var shake_amplitude := 0.0            ## metres on the eye, a third of it on the aim (0.015 u = 0.009 m is the club's light shake)
@export var shake_hz := 1.5
@export var lens_fov := 44.0                  ## the AUTHORED vertical fov; the fisheye widens the render past it
@export_range(0.0, 1.0) var fish := 0.0
@export_enum("off", "phone portrait", "phone sideways") var feed := 0
@export var post_material: ShaderMaterial

const W_LENS := 6.0      ## rad/s: a dolly / track lens (CAMTRK_W)
const W_HOLD := 9.0      ## rad/s: a hand-held camera's AIM (CAMHOLD_W)
const W_Y := 20.0        ## rad/s: the height pre-filter (CAMTRK_WY)
const PHONE_FWD := 0.20 / 1.75   ## the phone sits in front of the fist, toward the subject
const PHONE_OUT := 0.22 / 1.75   ## ...and out, away from the filmer's own chest
const PHONE_UP := 0.04 / 1.75

var _eS := Vector3.ZERO
var _eV := Vector3.ZERO
var _aS := Vector3.ZERO
var _aV := Vector3.ZERO
var _yF := 0.0
var _yV := 0.0
var _t := 0.0
var _shake_t := 0.0
var _snap := true
var _prev := Vector3.ZERO
var _from_eye := Vector3.ZERO
var _from_aim := Vector3.ZERO


func _ready() -> void:
	cut()


## A cut: the springs land on their targets on the next frame and the move/orbit clocks restart.
func cut() -> void:
	_snap = true
	_t = 0.0
	_from_eye = global_position
	_from_aim = global_position - global_transform.basis.z


## The exact step of x'' = w^2 (xt - x) - 2 w x' (critically damped, frame-rate independent). Returns (x, v).
static func spring(x: float, v: float, xt: float, w: float, dt: float) -> Vector2:
	var y := x - xt
	var e := exp(-w * dt)
	var j := v + w * y
	return Vector2(xt + (y + j * dt) * e, (v - w * j * dt) * e)


static func spring3(x: Vector3, v: Vector3, xt: Vector3, w: float, dt: float) -> Array:
	var rx := spring(x.x, v.x, xt.x, w, dt)
	var ry := spring(x.y, v.y, xt.y, w, dt)
	var rz := spring(x.z, v.z, xt.z, w, dt)
	return [Vector3(rx.x, ry.x, rz.x), Vector3(rx.y, ry.y, rz.y)]


static func lens_ease(t: float) -> float:
	t = clampf(t, 0.0, 1.0)
	return t * t * (3.0 - 2.0 * t)


## camshake's noise: the club's lensHash / lensNoise1 (value noise, quintic fade), 32-bit wrap reproduced.
static func _hash(i: int, ch: int) -> float:
	var h := (i * 374761393 + ch * 668265263) & 0xFFFFFFFF
	h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
	h = h ^ (h >> 16)
	return float(h & 0xFFFF) / 32767.5 - 1.0


static func _noise1(t: float, ch: int) -> float:
	var f := floorf(t)
	var u := t - f
	var i := int(f)
	u = u * u * u * (u * (u * 6.0 - 15.0) + 10.0)
	var a := _hash(i, ch)
	var b := _hash(i + 1, ch)
	return a + (b - a) * u


## The fisheye is a WIDE lens as well as a bent one: returns (render fov degrees, tht radians for the shader).
static func fish_fov(fov_deg: float, aspect: float, amount: float) -> Vector2:
	if amount <= 0.0:
		return Vector2(fov_deg, 0.0)
	var d := sqrt(1.0 + aspect * aspect)
	var th := atan(d * tan(deg_to_rad(fov_deg) * 0.5))    # the authored DIAGONAL half-angle
	var tgt := th + (maxf(1.05, th) - th) * amount           # toward 60 degrees off axis = 120 diagonal
	tgt = minf(tgt, 1.45)                                    # past ~83 degrees a rectilinear render dies
	var f := rad_to_deg(2.0 * atan(tan(tgt) / d))
	return Vector2(minf(f, 170.0), tgt)


func _heading(v: Vector3) -> float:
	var h := Vector2(v.x, v.z)
	if h.length() > 0.05:
		return atan2(h.x, h.y)                               # forward = (sin tr, 0, cos tr), as in the club
	var fwd := subject.global_transform.basis.z if subject_faces_plus_z else -subject.global_transform.basis.z
	return atan2(fwd.x, fwd.z)


func _process(delta: float) -> void:
	var dt := clampf(delta, 0.0, 0.25)
	_t += dt
	var eye := tripod_eye
	var aim := tripod_aim
	if mode == Mode.MOVE:
		var e := lens_ease(_t / maxf(move_seconds, 1e-4))    # no spring: the ease IS the smoothing
		eye = _from_eye.lerp(move_to_eye, e)
		aim = _from_aim.lerp(move_to_aim, e)
	elif mode != Mode.TRIPOD and subject != null:
		var p := subject.global_position
		var vel := Vector3.ZERO if _snap else (p - _prev) / maxf(dt, 1e-4)
		_prev = p
		var tr := _heading(vel)
		var speed := Vector2(vel.x, vel.z).length()
		var dx := sin(tr)
		var dz := cos(tr)
		var ff := speed * 2.0 / W_LENS                       # the lead: a spring trails a steady speed by 2v/w
		if _snap:
			_yF = p.y
			_yV = 0.0
		else:
			var ry := spring(_yF, _yV, p.y, W_Y, dt)
			_yF = ry.x
			_yV = ry.y
		var fy := _yV * 2.0 / W_LENS
		var aim_t := Vector3(p.x + dx * ff, p.y + fy + aim_height, p.z + dz * ff)
		var eye_t := tripod_eye
		if mode == Mode.TRACK:
			var a := tr + deg_to_rad(track_azimuth_deg)
			var ey := track_height if track_height_absolute else p.y + fy + track_height
			eye_t = Vector3(p.x - sin(a) * track_distance + dx * ff, ey, p.z - cos(a) * track_distance + dz * ff)
		elif mode == Mode.HOLD:
			var hp := filmer_hand.global_position if filmer_hand != null else p + Vector3(0, 0.83, 0)
			var to := (aim_t - hp).normalized()
			eye_t = hp + to * PHONE_FWD
			if filmer_body != null:
				var o := Vector2(hp.x - filmer_body.global_position.x, hp.z - filmer_body.global_position.z)
				if o.length() > 1e-3:
					o = o.normalized() * PHONE_OUT
					eye_t += Vector3(o.x, 0.0, o.y)
			eye_t.y += PHONE_UP - hold_drop
		if _snap:
			_eS = eye_t
			_aS = aim_t
			_eV = Vector3.ZERO
			_aV = Vector3.ZERO
			_snap = false
		elif mode == Mode.HOLD:
			# THE BOB IS THE SHOT: the eye is raw, only the aim is sprung (an operator behind the trick)
			_eS = eye_t
			_eV = Vector3.ZERO
			var ra := spring3(_aS, _aV, aim_t, W_HOLD, dt)
			_aS = ra[0]
			_aV = ra[1]
		else:
			var re := spring3(_eS, _eV, eye_t, W_LENS, dt)
			_eS = re[0]
			_eV = re[1]
			var ra2 := spring3(_aS, _aV, aim_t, W_LENS, dt)
			_aS = ra2[0]
			_aV = ra2[1]
		if mode == Mode.PAN:
			_eS = tripod_eye                                 # a pan never moves its eye
			_eV = Vector3.ZERO
		elif mode == Mode.ORBIT:
			# the circle is exact, round the SPRUNG centre: a sprung circle would cut its corner and arrive late
			var hd := deg_to_rad(lerpf(orbit_deg0, orbit_deg1, lens_ease(_t / maxf(orbit_seconds, 1e-4))))
			_eS = Vector3(_aS.x - sin(hd) * orbit_radius, _aS.y - aim_height + orbit_height, _aS.z - cos(hd) * orbit_radius)
			_eV = Vector3.ZERO
		eye = _eS
		aim = _aS
	# the shake goes on the lens being drawn, never back into the springs (it would walk off)
	if shake_amplitude > 0.0:
		_shake_t += dt
		var t := _shake_t * (shake_hz if shake_hz > 0.0 else 1.5)
		var n := []
		for k in 6:
			n.append((_noise1(t, k) + 0.5 * _noise1(t * 2.13 + 17.0, k + 6)) / 1.5)
		eye += Vector3(n[0], n[1], n[2]) * shake_amplitude
		aim += Vector3(n[3], n[4], n[5]) * (shake_amplitude / 3.0)
	global_position = eye
	if not eye.is_equal_approx(aim):
		look_at(aim, Vector3.UP)
	# the lens: widened for the fisheye, and the post shader told what it is
	var size := get_viewport().get_visible_rect().size
	var ff2 := fish_fov(lens_fov, size.x / maxf(size.y, 1.0), fish)
	fov = ff2.x
	if post_material != null:
		post_material.set_shader_parameter("fish", fish if ff2.y > 0.0 else 0.0)
		post_material.set_shader_parameter("tht", ff2.y if ff2.y > 0.0 else 0.8)
		post_material.set_shader_parameter("feed", float(feed))
