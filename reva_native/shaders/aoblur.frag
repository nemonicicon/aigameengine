#version 450
// The occlusion's grain smoothed, without crossing an edge: four by four, the block that holds each of ao.frag's sixteen turns once, each neighbour counted by how close its depth is to where this pixel's surface would be there.
layout(set = 0, binding = 0) uniform sampler2D uA;
layout(set = 0, binding = 1) uniform sampler2D uD;
layout(push_constant) uniform PC { vec4 a; } pc;  // xy: one texel
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
void main() {
    float z0 = texture(uD, vUV).r, far = 1.0 / max(1.0 - z0, 1e-5);
    // ⛔ A neighbour on the same sloping surface has another depth, and counting it by its depth alone threw it out: on a close-up, where every surface slopes
    // steeply in depth, nothing was smoothed and the grain showed as a grid. So it's measured against the plane through this pixel, from the depth's own slope
    // here, taken across the pixel on each side and the gentler of the two kept, so an edge beside this pixel doesn't pass for a slope
    float zl = texture(uD, vUV - vec2(pc.a.x, 0.0)).r, zr = texture(uD, vUV + vec2(pc.a.x, 0.0)).r, zd = texture(uD, vUV - vec2(0.0, pc.a.y)).r, zu = texture(uD, vUV + vec2(0.0, pc.a.y)).r;
    vec2 g = vec2(abs(zr - z0) < abs(z0 - zl) ? zr - z0 : z0 - zl, abs(zu - z0) < abs(z0 - zd) ? zu - z0 : z0 - zd);
    vec2 sum = vec2(0.0); float w = 0.0;
    for (int y = -2; y <= 1; y++) for (int x = -2; x <= 1; x++) {
        vec2 uv = vUV + vec2(float(x), float(y)) * pc.a.xy;
        float k = 1.0 / (1.0 + 4000.0 * abs(texture(uD, uv).r - (z0 + float(x) * g.x + float(y) * g.y)) * far);
        sum += texture(uA, uv).rg * k; w += k;
    }
    o = vec4(sum / w, 0.0, 1.0);
}
