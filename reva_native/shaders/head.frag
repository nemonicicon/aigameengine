#version 450
// Holly's head: the old road's look. A hole cut where the lips part, and the floating head: surfaces turning from the camera fall into the black, because the texture is baked emissive and lamps can't make him sit on it.
layout(constant_id = 3) const int LINEAR = 0;   // 3: the sampler decoded the texture, and this look is all in gamma
layout(set = 1, binding = 0) uniform sampler2D uTex;
layout(std140, set = 0, binding = 0) uniform HeadBlock { mat4 mvp, model; vec4 w0, w1; vec4 a, b, c, d, e, f; } H;
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vFace;
layout(location = 3) in float vMouth;
layout(location = 0) out vec4 o;
void main() {
    float uOpen = H.a.x, uSeal = H.a.z, uLipY = H.a.w, uMouthW = H.c.z, uMouthDark = H.d.z, uAperture = H.d.w, uEdge = H.e.x, uEdgeSharp = H.e.y, uLift = H.e.z, uDeform = H.f.w;
    vec3 col = texture(uTex, vUv).rgb;
    if (LINEAR == 3) col = pow(col, vec3(1.0 / 2.2));
    float aH = uAperture * uOpen * uDeform;
    if (aH > 0.0005) {
        float ax = vFace.y / max(1e-4, uMouthW * 0.92);
        float ay = (vFace.x - (uLipY - aH * 0.75)) / max(1e-4, aH * 1.75);
        float r = sqrt(ax * ax + ay * ay);
        if (r < 1.0) discard;
        col *= mix(0.12, 1.0, smoothstep(1.0, 1.5, r));
    }
    col *= 1.0 - vMouth * max(uOpen, uSeal * 0.55) * uMouthDark;
    float facing = clamp(normalize(vNormal).z, 0.0, 1.0);
    col *= mix(uEdge, 1.0, pow(facing, uEdgeSharp)) * uLift;
    o = vec4(col, 1.0);
}
