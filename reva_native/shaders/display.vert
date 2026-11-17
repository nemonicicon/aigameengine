#version 450
// (11-16) A GAME'S DISPLAY ON A PANEL IN THE WORLD (gfx.h gfxHolo, display.frag): two triangles between its four corners, its picture across them, its top-left
// corner the picture's top-left
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(push_constant) uniform Holo { vec4 c[4]; vec4 l0; vec4 l1; vec4 l2; } P;   // c: top-left, top-right, bottom-right, bottom-left; l0..l2: holo.frag's
layout(location = 0) out vec2 vUV;
const int I[6] = int[6](0, 1, 2, 0, 2, 3);
const vec2 UV[4] = vec2[4](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));
void main() {
    int k = I[gl_VertexIndex];
    vUV = UV[k];
    gl_Position = V.vp * vec4(P.c[k].xyz, 1.0);
}
