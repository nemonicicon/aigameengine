#version 450
// Holly's head, for the jumbotrons: the same deformation the old road's holly.c has, in its own off-screen picture. Up to eight morph targets as plain attributes. On a rigged head uDeform is 0, and everything after the morphs contributes nothing.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec3 aM0;
layout(location = 4) in vec3 aM1;
layout(location = 5) in vec3 aM2;
layout(location = 6) in vec3 aM3;
layout(location = 7) in vec3 aM4;
layout(location = 8) in vec3 aM5;
layout(location = 9) in vec3 aM6;
layout(location = 10) in vec3 aM7;
layout(std140, set = 0, binding = 0) uniform HeadBlock { mat4 mvp, model; vec4 w0, w1; vec4 a, b, c, d, e, f; } H;  // a: open wide seal lipY. b: falloff lipHalf drop pivotY. c: pivotZ angle mouthW round. d: spread press mouthDark aperture. e: edge edgeSharp lift minY. f: height centerX halfWidth deform
layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vFace;
layout(location = 3) out float vMouth;
void main() {
    float uOpen = H.a.x, uWide = H.a.y, uSeal = H.a.z, uLipY = H.a.w, uFalloff = H.b.x, uLipHalf = H.b.y, uDrop = H.b.z, uPivotY = H.b.w, uPivotZ = H.c.x, uAngle = H.c.y, uMouthW = H.c.z, uRound = H.c.w, uSpread = H.d.x, uPress = H.d.y, uMinY = H.e.w, uHeight = H.f.x, uCenterX = H.f.y, uHalfWidth = H.f.z, uDeform = H.f.w;
    vec3 p = aPos + aM0 * H.w0.x + aM1 * H.w0.y + aM2 * H.w0.z + aM3 * H.w0.w + aM4 * H.w1.x + aM5 * H.w1.y + aM6 * H.w1.z + aM7 * H.w1.w;
    float h = (aPos.y - uMinY) / uHeight;
    float w = 1.0 - smoothstep(uLipY - uFalloff, uLipY, h);
    float amt = w * uOpen * (1.0 - uSeal) * uDeform;
    vec3 pivot = vec3(uCenterX, uMinY + uPivotY * uHeight, uPivotZ * uHeight);
    float ang = amt * uAngle;
    vec3 rel = p - pivot;
    float ca = cos(ang), sa = sin(ang);
    p = pivot + vec3(rel.x, ca * rel.y - sa * rel.z, sa * rel.y + ca * rel.z);
    p.y -= amt * uDrop * uHeight;
    float lip = 1.0 - smoothstep(0.0, uLipHalf, abs(h - uLipY));
    float dx = (aPos.x - uCenterX) / uHalfWidth;
    float mouth = lip * (1.0 - smoothstep(uMouthW, uMouthW + 0.25, abs(dx))) * uDeform;
    float purse = max(0.0, -uWide), spread = max(0.0, uWide);
    p.x -= dx * mouth * purse * uRound * uHeight;
    p.z += mouth * (1.0 - abs(dx)) * purse * uRound * uHeight;
    p.x += dx * mouth * spread * uSpread * uHeight;
    p.z -= mouth * spread * uSpread * uHeight * 0.3;
    p.y += (w - 0.5) * 2.0 * mouth * uSeal * uPress * uHeight;
    vUv = aUv;
    vNormal = mat3(H.model) * aNormal;
    vFace = vec2(h, dx);
    vMouth = mouth;
    gl_Position = H.mvp * vec4(p, 1.0);
}
