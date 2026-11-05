#version 450
// THE FLAT-PANEL LOOK (queue step 25): the picture as a tube shows it, on a panel that isn't one. The engine draws the tube's own picture, 640 x 480 or 640 x 240 as the
// Pi does, and this puts it on the glass the way a tube would, in the order a set does it:
//   the signal: composite video carries brightness to about 4.2 MHz and colour to far less, so each line is smoothed along itself, colour much more than brightness;
//   the beam: each line is a stripe of light with a soft profile, wider where it's bright. At 480 lines the stripes overlap and the picture is solid; at 240 the same
//     beam leaves dark between the lines, which is what 240p looks like;
//   the fields: a 480-line picture is drawn half its lines at a time. A panel can't flash a line for a millisecond, so the field not being drawn is shown at a share;
//   the mask: the glass lights in red, green and blue stripes, in slots or unbroken;
//   the tube's own curve from signal to light (about 2.4, a little deeper than a panel's), and a slight bulge.
// The numbers are the NTSC standard's and a typical set's, in data/tube.txt: nothing here has been measured off a tube yet, and a measured set's numbers go in that file.
layout(constant_id = 1) const int LINES = 1;   // 1: 480 lines, two fields. 2: 240 lines
layout(set = 0, binding = 0) uniform sampler2D uP;
// a: the picture's width and height in its own pixels, and in the glass's. b: how fine brightness is across a line, how fine colour is (in samples across the picture), the
// beam's width in 480ths of the picture's height, and how much wider it is at full brightness. c: the mask (0 none, 1 slots, 2 stripes), its pitch in glass pixels, how dark
// it is between, and the idle field's share. d: the tube's gamma, which field this frame draws, the bulge, the brightness made up
layout(push_constant) uniform PC { vec4 a; vec4 b; vec4 c; vec4 d; } pc;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
const mat3 TO_YIQ = mat3(0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312);
const mat3 TO_RGB = mat3(1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703);
// one line of the signal, read at x: its brightness smoothed a little, its colour a lot
vec3 signal(float x, float y) {
    float sl = 0.5 / pc.b.x, sc = 0.5 / pc.b.y, step = sc * 0.55;
    vec3 acc = vec3(0.0); vec2 w = vec2(0.0);
    for (int i = -4; i <= 4; i++) {
        float dx = float(i) * step, wl = exp(-0.5 * dx * dx / (sl * sl)), wc = exp(-0.5 * dx * dx / (sc * sc));
        vec3 q = TO_YIQ * texture(uP, vec2(x + dx, y)).rgb;
        acc += vec3(q.x * wl, q.yz * wc); w += vec2(wl, wc);
    }
    return clamp(TO_RGB * vec3(acc.x / w.x, acc.yz / w.y), 0.0, 1.0);
}
void main() {
    vec2 q = vUV * 2.0 - 1.0;
    q *= vec2(1.0 + pc.d.z * q.y * q.y, 1.0 + pc.d.z * q.x * q.x);     // the glass bulges: its corners are pulled in
    q = q * 0.5 + 0.5;
    if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0) { o = vec4(0.0, 0.0, 0.0, 1.0); return; }
    const float N = pc.a.y, L = q.y * N - 0.5, i0 = floor(L);
    vec3 light = vec3(0.0);
    for (int j = -1; j <= 2; j++) {
        float li = i0 + float(j); if (li < 0.0 || li > N - 1.0) continue;
        vec3 s = signal(q.x, (li + 0.5) / N);
        vec3 l = pow(s, vec3(pc.d.x));                               // the signal to light, by the tube's own curve
        float lum = dot(l, vec3(0.299, 0.587, 0.114));
        float d = (q.y - (li + 0.5) / N) * 480.0, sg = pc.b.z * (1.0 + pc.b.w * lum);
        float w = exp(-0.5 * d * d / (sg * sg));
        if (LINES == 1 && mod(li, 2.0) != pc.d.y) w *= pc.c.w;        // the field that isn't being drawn this frame
        light += l * w;
    }
    vec3 m = vec3(1.0);
    if (pc.c.x > 0.5) {
        float P = max(pc.c.y, 3.0), x = gl_FragCoord.x, y = gl_FragCoord.y;
        int k = int(mod(floor(x), P) * 3.0 / P);
        m = vec3(1.0 - pc.c.z); m[k] = 1.0;
        if (pc.c.x < 1.5) { if (mod(floor(x / P), 2.0) > 0.5) y += P; if (mod(floor(y), 2.0 * P) < 1.0) m *= 1.0 - pc.c.z; }   // slots: a dark bar across each, staggered from column to column
    }
    vec3 c = light * m * pc.d.w;
    o = vec4(pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
}
