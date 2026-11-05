#version 450
// Flat colour, or SOFT: fading from the middle by its uv. A pixel here costs next to nothing, which is the point: a floor's pattern is worked out per tile on the CPU, not per pixel.
// SOFT 2 (queue step 24) also fades it out where it meets what's behind it, so a puff of smoke has no hard line where it cuts the floor: that reads the scene's depth, taken mid-frame.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
precision mediump float;
layout(constant_id = 1) const int SOFT = 0;
layout(constant_id = 3) const int LINEAR = 0;
layout(set = 0, binding = 9) uniform highp sampler2D uSceneZ;   // the scene's depth as it stood when everything solid was down
layout(location = 0) in vec4 vC;
layout(location = 1) in vec2 vU;
layout(location = 2) in highp vec3 vW;
layout(location = 0) out vec4 o;
void main() {
    float a = vC.a;
    if (SOFT >= 1) a *= exp(-dot(vU, vU) * 3.2);
    if (SOFT == 2) {
        highp float z = texelFetch(uSceneZ, ivec2(gl_FragCoord.xy), 0).r;
        highp vec2 sc = (gl_FragCoord.xy - V.cl2.zw) / V.cl3.xy;
        highp vec4 q = V.inv * vec4(sc.x * 2.0 - 1.0, 1.0 - sc.y * 2.0, z, 1.0);
        highp float gap = dot(q.xyz / q.w - vW, V.fwd.xyz);   // how far behind this pixel of it the scene is, along the lens
        a *= clamp(gap / 0.45, 0.0, 1.0);
    }
    o = vec4(LINEAR == 2 ? pow(max(vC.rgb, vec3(0.0)), vec3(2.2)) : vC.rgb, a);
}
