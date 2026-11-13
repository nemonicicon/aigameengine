#!/bin/bash
# vk_lab_build.sh -- writes the shaders, compiles them to SPIR-V and builds vk_lab. Run on the Pi from this folder.
# The shading matches v3d_lab2.c, strength for strength.
set -e
mkdir -p spv glsl
VATT='layout(location=0) in vec3 aP; layout(location=1) in vec3 aN; layout(location=2) in vec2 aT; layout(location=3) in uvec4 aI; layout(location=4) in vec4 aW;
layout(push_constant) uniform PC { vec4 xf; } pc;
layout(location=0) out vec3 vN; layout(location=1) out vec2 vT; layout(location=2) out vec3 vP;'
SKIN='ivec4 i=ivec4(aI)*3; vec4 a=B.b[i.x]*aW.x+B.b[i.y]*aW.y+B.b[i.z]*aW.z+B.b[i.w]*aW.w; vec4 b=B.b[i.x+1]*aW.x+B.b[i.y+1]*aW.y+B.b[i.z+1]*aW.z+B.b[i.w+1]*aW.w; vec4 c=B.b[i.x+2]*aW.x+B.b[i.y+2]*aW.y+B.b[i.z+2]*aW.z+B.b[i.w+2]*aW.w; vec4 p4=vec4(aP,1.0); vec3 q=vec3(dot(a,p4),dot(b,p4),dot(c,p4)); vec3 nn=vec3(dot(a.xyz,aN),dot(b.xyz,aN),dot(c.xyz,aN));'
cat > glsl/static_v.vert <<EOF
#version 450
$VATT
void main(){ vN=aN; vT=aT; vec2 xy=aP.xy*pc.xf.z+pc.xf.xy; vP=vec3(xy,0.0); gl_Position=vec4(xy,aP.z*0.1+0.5+pc.xf.w,1.0); }
EOF
cat > glsl/skin_v.vert <<EOF
#version 450
$VATT
layout(set=0,binding=0) uniform BB { vec4 b[96]; } B;
void main(){ $SKIN vN=nn; vT=aT; vec2 xy=q.xy*pc.xf.z+pc.xf.xy; vP=vec3(xy,0.0); gl_Position=vec4(xy,q.z*0.1+0.5+pc.xf.w,1.0); }
EOF
cat > glsl/inst_v.vert <<EOF
#version 450
$VATT
layout(set=0,binding=0) uniform BB { vec4 b[768]; vec4 inst[400]; } B;
void main(){ vec4 X=B.inst[gl_InstanceIndex]; int base=int(X.w)*96; ivec4 i=ivec4(aI)*3+base; vec4 a=B.b[i.x]*aW.x+B.b[i.y]*aW.y+B.b[i.z]*aW.z+B.b[i.w]*aW.w; vec4 b=B.b[i.x+1]*aW.x+B.b[i.y+1]*aW.y+B.b[i.z+1]*aW.z+B.b[i.w+1]*aW.w; vec4 c=B.b[i.x+2]*aW.x+B.b[i.y+2]*aW.y+B.b[i.z+2]*aW.z+B.b[i.w+2]*aW.w; vec4 p4=vec4(aP,1.0); vec3 q=vec3(dot(a,p4),dot(b,p4),dot(c,p4)); vec3 nn=vec3(dot(a.xyz,aN),dot(b.xyz,aN),dot(c.xyz,aN)); vN=nn; vT=aT; vec2 xy=q.xy*X.z+X.xy; vP=vec3(xy,0.0); gl_Position=vec4(xy,q.z*0.1+0.5,1.0); }
EOF
cat > glsl/quad_v.vert <<'EOF'
#version 450
layout(location=0) out vec3 vN; layout(location=1) out vec2 vT; layout(location=2) out vec3 vP;
void main(){ vec2 p=vec2(float((gl_VertexIndex&1)*2-1),float((gl_VertexIndex&2)-1)); vN=vec3(0.0,0.0,1.0); vT=p*2.0; vP=vec3(p,0.0); gl_Position=vec4(p,0.95,1.0); }
EOF
FH='layout(location=0) in vec3 vN; layout(location=1) in vec2 vT; layout(location=2) in vec3 vP; layout(location=0) out vec4 o;
layout(set=0,binding=1) uniform sampler2D uT;'
FSH='layout(set=0,binding=2) uniform sampler2DShadow uSh;
float shade(vec3 P){ vec3 c=vec3(P.xy*0.5+0.5,0.6); float e=1.0/1024.0; return 0.25*(texture(uSh,c+vec3(-e,-e,0.0))+texture(uSh,c+vec3(e,-e,0.0))+texture(uSh,c+vec3(-e,e,0.0))+texture(uSh,c+vec3(e,e,0.0))); }'
cat > glsl/lit_f.frag <<EOF
#version 450
precision mediump float;
$FH
void main(){ vec3 N=normalize(vN); o=vec4(vec3(0.25)*(0.4+0.6*max(dot(N,vec3(0.3,0.8,0.5)),0.0)),1.0); }
EOF
cat > glsl/a_f.frag <<EOF
#version 450
precision mediump float;
$FH
void main(){ vec3 N=normalize(vN); vec3 tx=texture(uT,vT).rgb*0.5; float s=max(dot(N,vec3(0.3,0.8,0.5)),0.0); o=vec4(tx*(0.35+0.65*s),1.0); }
EOF
cat > glsl/b_f.frag <<EOF
#version 450
precision mediump float;
$FH
$FSH
void main(){ vec3 N=normalize(vN); vec3 tx=texture(uT,vT).rgb*0.5; float s=max(dot(N,vec3(0.3,0.8,0.5)),0.0); o=vec4(tx*(0.35+0.65*s*shade(vP)),1.0); }
EOF
cat > glsl/c_f.frag <<EOF
#version 450
precision mediump float;
$FH
$FSH
// six lights written out: a local array walked in a loop goes to scratch memory on V3D (59 ms a full-screen pass, measured)
vec3 lamp(vec3 N,vec3 pos,vec3 col){ vec3 D=pos-vP; float dd=dot(D,D); return col*max(0.0,dot(N,D*inversesqrt(dd+0.001)))/(1.0+dd*0.2); }
void main(){ vec4 t4=texture(uT,vT); if(t4.a<0.02) discard; vec3 N=normalize(vN); vec3 tx=t4.rgb*0.5; float s=max(dot(N,vec3(0.3,0.8,0.5)),0.0); vec3 li=lamp(N,vec3(-0.5,0.5,0.4),vec3(0.1,0.05,0.02))+lamp(N,vec3(0.5,0.5,0.4),vec3(0.02,0.05,0.1))+lamp(N,vec3(-0.5,-0.5,0.4),vec3(0.05,0.1,0.02))+lamp(N,vec3(0.5,-0.5,0.4),vec3(0.1,0.02,0.05))+lamp(N,vec3(0.0,0.0,0.5),vec3(0.05))+lamp(N,vec3(0.0,0.8,0.3),vec3(0.02,0.02,0.1)); o=vec4(tx*(0.35+0.65*s*shade(vP)+li),1.0); }
EOF
for f in glsl/*.vert glsl/*.frag; do n=$(basename "$f"); n=${n%.*}; glslangValidator -V -o "spv/$n.spv" "$f" > /tmp/glslang.out || { cat /tmp/glslang.out; exit 1; }; done
gcc -O2 -std=gnu17 -o vk_lab vk_lab.c -I/usr/include/libdrm -ldrm -lvulkan -lm
ls -la vk_lab spv | head -16
