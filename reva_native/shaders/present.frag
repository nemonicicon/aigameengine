#version 450
// The picture onto the glass (queue step 25), when they aren't the same size or shape: a picture held to 4:3 on a wide panel, or drawn at a resolution of its own.
// The viewport is the picture's place; what's outside it was cleared to black.
// HDR 2: the glass is an HDR10 display. The picture is the ordinary one, so it goes on at paper white, and the light that was past white (shaders/hdrex.frag) is added back,
// in the display's own primaries and its own curve (BT.2020, SMPTE ST 2084).
layout(constant_id = 1) const int HDR = 0;
layout(set = 0, binding = 0) uniform sampler2D uP;
layout(set = 0, binding = 1) uniform sampler2D uX;
layout(push_constant) uniform PC { vec4 a; } pc;   // x: paper white in nits. y: whether this frame has light past white to add
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
void main() {
    vec3 c = texture(uP, vUV).rgb;
    if (HDR == 2) {
        vec3 lin = pow(c, vec3(2.2)) + texture(uX, vUV).rgb * pc.a.y;   // light, with 1 at paper white
        const mat3 TO2020 = mat3(0.6274, 0.0691, 0.0164, 0.3293, 0.9195, 0.0880, 0.0433, 0.0114, 0.8956);
        vec3 n = clamp(TO2020 * lin * (pc.a.x / 10000.0), 0.0, 1.0);
        const float m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
        vec3 p = pow(n, vec3(m1));
        c = pow((c1 + c2 * p) / (1.0 + c3 * p), vec3(m2));
    }
    o = vec4(c, 1.0);
}
