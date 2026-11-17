#version 450
// (11-16) A GAME'S DISPLAY AS A HOLOGRAM (gfx.h gfxHolo, display.vert): its picture given off, times its brightness and its scan, and as see-through
// as it's dark, so black is clear: what a cockpit's projected instruments look like. The picture holds what the 2D drew, sRGB as the screen's does,
// so it's decoded first; how much hides what's behind is its brightest channel, times a gain. The scan: lines across its height, a band rolling
// down it, and a flicker on the whole (the game's own, for this frame)
layout(constant_id = 3) const int LINEAR = 0;
layout(set = 1, binding = 0) uniform sampler2D uT;
layout(push_constant) uniform Holo { vec4 c[4]; vec4 l0; vec4 l1; vec4 l2; } P;
// l0: brightness, the opacity's gain, the lines' frequency (radians a picture's height), their depth. l1: the band's gain, its sharpness, its
// speed (pictures a second), the flicker. l2.x: the clock, seconds
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
vec3 decode(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c)); }
void main() {
    vec3 lin = decode(clamp(texture(uT, vUV).rgb, 0.0, 1.0));
    float v = vUV.y, T = P.l2.x;
    float lines = (1.0 - P.l0.w) + P.l0.w * sin(v * P.l0.z);
    float band = 1.0 + P.l1.x * exp(-fract(v - T * P.l1.z) * P.l1.y);
    vec3 e = lin * P.l0.x * lines * band * P.l1.w;
    float a = clamp(max(max(lin.r, lin.g), lin.b) * P.l0.y, 0.0, 1.0);
    o = vec4(LINEAR == 2 ? e : pow(max(e, vec3(0.0)), vec3(1.0 / 2.2)), a);
}
