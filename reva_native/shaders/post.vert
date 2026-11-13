#version 450
// One triangle over the whole target, no buffers. The viewport has a negative height like every other pass, so y up in clip space is the top row of the image.
layout(location = 0) out vec2 vUV;
void main() {
    vec2 p = vec2(float(gl_VertexIndex & 1) * 4.0 - 1.0, float(gl_VertexIndex & 2) * 2.0 - 1.0);
    vUV = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    gl_Position = vec4(p, 0.0, 1.0);
}
