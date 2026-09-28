// club_camera_post.gdshader -- the club engine's post pass, the fisheye and the phone's frame, for Godot 4.
//
// ⛔ A REFERENCE TRANSLATION, NOT YET RUN IN GODOT. The ground truth is the fragment shader in
// reva_native/crowd_kms.c postInit (lines 2592-2680). Bloom and FXAA are left to Godot (Environment glow,
// Project Settings > Rendering > Anti Aliasing > FXAA); the club's tone knee is kept as an option.
//
// Use: a CanvasLayer over the 3D view, with a ColorRect (anchors: full rect) whose material is a
// ShaderMaterial of this shader. ClubCamera (club_camera.gd) sets fish / tht / feed every frame.
shader_type canvas_item;

uniform sampler2D screen_tex : hint_screen_texture, filter_linear_mipmap;
uniform float fish = 0.0;   // 0 = the rectilinear lens, 1 = a full equidistant fisheye (~0.4-0.6 is the skate-video look)
uniform float tht = 0.8;    // the DIAGONAL half-angle the frame covers, radians (ClubCamera.fish_fov)
uniform float feed = 0.0;   // 0 off, 1 phone portrait (9:16), 2 phone sideways (16:9)
uniform float tone = 0.0;   // the club's luminance knee start (0.66 in its episodes); 0 = leave tone to the Environment

float fhash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// THE FISHEYE, as a projection: radius on the frame = the angle off the axis (r = f*theta), so the source is
// sampled at tan(theta) -- where a rectilinear render put that ray. `fish` blends back to the identity.
vec2 warpA(vec2 t, float aa) {
	if (fish <= 0.0) { return t; }
	vec2 c = t * 2.0 - 1.0;
	c.x *= aa;
	float rmax = sqrt(1.0 + aa * aa);
	float rn = length(c) / rmax;
	if (rn < 1e-5) { return t; }
	float src = tan(rn * tht) / tan(tht);
	c *= mix(rn, src, fish) / rn;
	c.x /= aa;
	return c * 0.5 + 0.5;
}

// seven-segment digits: the phone's timer, without a font
float seg(vec2 p, vec2 a, vec2 b, float w) {
	vec2 pa = p - a;
	vec2 ba = b - a;
	float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
	return step(length(pa - ba * h), w);
}
float digit(vec2 p, int d) {
	if (p.x < -0.1 || p.x > 0.6 || p.y < -0.1 || p.y > 1.1) { return 0.0; }
	float on = 0.0;
	float w = 0.055;
	bool A = (d != 1 && d != 4);
	bool B = (d != 5 && d != 6);
	bool C = (d != 2);
	bool D = (d != 1 && d != 4 && d != 7);
	bool E = (d == 0 || d == 2 || d == 6 || d == 8);
	bool F = (d != 1 && d != 2 && d != 3 && d != 7);
	bool G = (d != 0 && d != 1 && d != 7);
	if (A) { on = max(on, seg(p, vec2(0.05, 1.0), vec2(0.45, 1.0), w)); }
	if (B) { on = max(on, seg(p, vec2(0.5, 0.95), vec2(0.5, 0.55), w)); }
	if (C) { on = max(on, seg(p, vec2(0.5, 0.45), vec2(0.5, 0.05), w)); }
	if (D) { on = max(on, seg(p, vec2(0.05, 0.0), vec2(0.45, 0.0), w)); }
	if (E) { on = max(on, seg(p, vec2(0.0, 0.45), vec2(0.0, 0.05), w)); }
	if (F) { on = max(on, seg(p, vec2(0.0, 0.95), vec2(0.0, 0.55), w)); }
	if (G) { on = max(on, seg(p, vec2(0.05, 0.5), vec2(0.45, 0.5), w)); }
	return on;
}

void fragment() {
	float asp = SCREEN_PIXEL_SIZE.y / SCREEN_PIXEL_SIZE.x;     // width / height
	vec2 uv = vec2(SCREEN_UV.x, 1.0 - SCREEN_UV.y);             // the club's GL convention: origin bottom-left
	// THE PHONE'S FRAME FIRST: everything after this is inside her screen
	vec2 fu = uv;
	float fasp = asp;
	float wf = 1.0;
	bool outside = false;
	if (feed > 0.5) {
		fasp = (feed > 1.5) ? (16.0 / 9.0) : (9.0 / 16.0);
		wf = clamp(fasp / asp, 0.05, 1.0);
		float dx = (uv.x - 0.5) / wf;
		outside = abs(dx) > 0.5;
		fu = vec2(dx + 0.5, uv.y);
	}
	vec2 fw = warpA(fu, fasp);
	vec2 uvw = vec2(0.5 + (fw.x - 0.5) * wf, fw.y);
	vec3 col = texture(screen_tex, vec2(uvw.x, 1.0 - uvw.y)).rgb;
	// the club's knee: on LUMINANCE, toward a 0.96 white point (per channel greys a saturated colour)
	if (tone > 0.0) {
		float l = luma(col);
		float ov = max(l - tone, 0.0);
		float wp = 0.96;
		float l2 = min(l, tone) + (wp - tone) * (1.0 - exp(-ov / (wp - tone)));
		col *= l2 / max(l, 1e-4);
	}
	// the phone's own frame goes on AFTER the tone: a camera's chrome is not part of the scene's exposure
	if (feed > 0.5) {
		float feedT = TIME;
		float r = length((fu - 0.5) * vec2(1.10, 1.0));
		col *= mix(1.0, 0.84, smoothstep(0.42, 0.80, r));                      // a phone lens falls off a little
		float g = fhash(fu * vec2(720.0, 1280.0) + vec2(feedT * 37.0, feedT * 29.0));
		col += (g - 0.5) * 0.022;                                                // sensor noise, not tape grain
		float sc = (feed > 1.5) ? 1.0 : (9.0 / 16.0);                            // keep the chrome square either way
		vec2 q = vec2((fu.x - 0.055) / sc, (fu.y - 0.945));
		float blink = step(0.35, fract(feedT));
		if (blink > 0.5 && length(q) < 0.013) { col = mix(col, vec3(0.95, 0.12, 0.10), 0.95); }
		float tsec = floor(mod(feedT, 60.0));
		float tmin = floor(mod(feedT / 60.0, 10.0));
		vec2 tp = vec2((fu.x - 0.085) / (0.030 * sc), (fu.y - 0.935) / 0.024);
		float ink = digit(tp, int(tmin));
		ink = max(ink, step(length(vec2(tp.x - 0.85, tp.y - 0.30)), 0.07));   // the colon
		ink = max(ink, step(length(vec2(tp.x - 0.85, tp.y - 0.72)), 0.07));
		ink = max(ink, digit(tp - vec2(1.15, 0.0), int(floor(tsec / 10.0))));
		ink = max(ink, digit(tp - vec2(1.85, 0.0), int(mod(tsec, 10.0))));
		col = mix(col, vec3(1.0), clamp(ink, 0.0, 1.0) * 0.92);
	}
	COLOR = outside ? vec4(0.0, 0.0, 0.0, 1.0) : vec4(col, 1.0);
}
