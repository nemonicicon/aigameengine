#version 450
// Bloom from what's really over white, on the HDR target. MODE 0 is the first step down and cuts out what isn't bright, 1 halves again, 2 is a tent on the way back up, added by the blend.
layout(constant_id = 1) const int MODE = 0;
layout(set = 0, binding = 0) uniform sampler2D uT;
layout(push_constant) uniform PC { vec4 a; } pc;  // xy: one texel of the source. z: on an 8-bit target nothing's over white, so what's nearly saturated is what blooms, from this
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
void main() {
    vec2 e = pc.a.xy;
    vec3 c;
    if (MODE == 2) {
        c = texture(uT, vUV).rgb * 4.0;
        c += (texture(uT, vUV + vec2(e.x, 0.0)).rgb + texture(uT, vUV - vec2(e.x, 0.0)).rgb + texture(uT, vUV + vec2(0.0, e.y)).rgb + texture(uT, vUV - vec2(0.0, e.y)).rgb) * 2.0;
        c += texture(uT, vUV + e).rgb + texture(uT, vUV - e).rgb + texture(uT, vUV + vec2(e.x, -e.y)).rgb + texture(uT, vUV + vec2(-e.x, e.y)).rgb;
        o = vec4(c / 16.0, 1.0);
        return;
    }
    c = texture(uT, vUV).rgb * 4.0 + texture(uT, vUV + e).rgb + texture(uT, vUV - e).rgb + texture(uT, vUV + vec2(e.x, -e.y)).rgb + texture(uT, vUV + vec2(-e.x, e.y)).rgb;
    c /= 8.0;
    if (MODE == 0) { if (pc.a.z > 0.0) c = max(c - vec3(pc.a.z), vec3(0.0)) / (1.0 - pc.a.z); else { float b = max(c.r, max(c.g, c.b)); c = min(c, vec3(16.0)) * smoothstep(0.9, 1.6, b); } }
    o = vec4(c, 1.0);
}
