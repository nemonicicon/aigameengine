#version 450
// 2D sprites on the raster: position, where in the image, and a colour to multiply by.
layout(location = 0) in vec2 aP;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aC;
layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vC;
layout(push_constant) uniform PC { vec4 scr; } pc;
void main() { vUV = aUV; vC = aC; gl_Position = vec4(aP.x / pc.scr.x * 2.0 - 1.0, 1.0 - aP.y / pc.scr.y * 2.0, 0.0, 1.0); }
