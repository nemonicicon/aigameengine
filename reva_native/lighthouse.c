/* ================= R.E.V.A. -- THE LAST LIGHTHOUSE (10-12) ===========================
 * jacob 10-12: "i want to start with the start menu". design/reva_start_menu.md.
 * The last engineered system in a dead universe, flung near light speed so time dilation
 * carries the archive past every other star: a red dwarf inside a Dyson LATTICE (panels and
 * gaps -- a solid shell is unstable), a black hole that drives it, three pulsars frozen at
 * the hole's edge that it navigates by. Absolute black around it.
 *
 * Door 11 is its own program from the first frame: no packs load, no crowd, no club HUD.
 * Pass 1 rasterises the sphere and the star into an FBO with VIEW DISTANCE in alpha (this
 * ANGLE context has no depth texture). Pass 2 traces every pixel's ray past the hole --
 * bent by a Schwarzschild-like pull, crossing the accretion disk, the pulsar cores and their
 * beams analytically -- and looks the bent ray up in pass 1. Then bloom, tone, the menu.
 *   REVA_LHT=<s>      start the clock at s seconds (pick an orbit phase for a shot)
 *   REVA_LHSTART=<f>  press A at frame f on the selected row (headless; START NEW GAME by default)
 *   REVA_LHSEL=<i>    menu row selected at boot (2 + LHSTART = the options page)
 *   REVA_LHNOLENS=1   THE CONTROL RUN: pass 2 off, the raster scene straight to bloom
 *   REVA_SHOT / REVA_SHOT_PATH / REVA_SHOTN / REVA_SHOTEVERY  as in the club, counted here */
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

#define LH_R     42.0f     /* the swarm (true scale: star radii) */
#define LH_STAR   1.0f    /* a 0.3 R_sun red dwarf = the unit */
#define LH_NRING  24       /* the swarm's orbital bands */
#define LH_RS     0.42f    /* 30,000 M_sun: rs = 88,500 km = 0.42 R_star */
static const float LH_H[3]={-300.f,60.f,-258.f};   /* 400 R_star: outside the tidal radius (46) */
static const float LH_PIV[3]={0.f,0.f,0.f};

/* ---- small vector helpers ---- */
static void lhNorm(float*v){ float l=sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); if(l>1e-9f){ v[0]/=l; v[1]/=l; v[2]/=l; } }
static void lhCross(float*r,const float*a,const float*b){ float t[3]={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; r[0]=t[0]; r[1]=t[1]; r[2]=t[2]; }
static float lhDot(const float*a,const float*b){ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static float lhHash(int i){ unsigned x=(unsigned)i*2654435761u; x^=x>>13; x*=0x5bd1e995u; x^=x>>15; return (x&0xffffff)/16777216.f; }
static float lhSmooth(float a,float b,float x){ float t=(x-a)/(b-a); if(t<0)t=0; if(t>1)t=1; return t*t*(3-2*t); }

static GLuint lhProg(const char*vs,const char*fs,const char*what){
    GLuint p=glCreateProgram(); glAttachShader(p,sh(GL_VERTEX_SHADER,vs)); glAttachShader(p,sh(GL_FRAGMENT_SHADER,fs));
    glBindAttribLocation(p,0,"p"); glBindAttribLocation(p,1,"b"); glBindAttribLocation(p,2,"n"); glBindAttribLocation(p,3,"s"); glBindAttribLocation(p,4,"r");
    glLinkProgram(p); GLint ok; glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){ char lg[1024]; glGetProgramInfoLog(p,1024,NULL,lg); fprintf(stderr,"[lighthouse] %s link: %s\n",what,lg); die("lighthouse link"); }
    return p; }

/* ---- shaders ---- */
static const char*LH_VS_MESH=
 "attribute vec3 p; attribute vec3 b; attribute vec3 n; attribute float s;\n"
 "uniform mat4 vp; varying vec3 wp; varying vec3 vb; varying vec3 vn; varying float vs;\n"
 "void main(){ wp=p; vb=b; vn=n; vs=s; gl_Position=vp*vec4(p,1.0); }\n";
/* THE RINGS (jacob 10-12, a picture from his dream: "looked more like this"). The swarm is
 * orbital bands, nested at their own radii so no two ever touch, each turning on its orbit.
 * Geometry is a band on a sphere; its ROTATION is an arc offset (the band is symmetric), so a
 * missing arc and the truss pattern are cut here, by position along the ring after the turn.
 *   b = (arc position, across -1..1, surface 0 outer / 1 inner / 2 edge wall)
 *   s = style (0 panelled, 1 open ladder, 2 track with open bays) + a per-ring seed in the fraction
 *   r = (angular speed, ring radius, gap start, gap length)  -- arcs in world units */
static const char*LH_VS_RING=
 "attribute vec3 p; attribute vec3 b; attribute vec3 n; attribute float s; attribute vec4 r;\n"
 "uniform mat4 vp; uniform float t;\n"
 "varying vec3 wp; varying vec3 vb; varying vec3 vn; varying float vs; varying vec4 vr;\n"
 "void main(){ wp=p; vb=vec3(b.x+r.x*r.y*t,b.y,b.z); vn=n; vs=s; vr=r; gl_Position=vp*vec4(p,1.0); }\n";
static const char*LH_FS_RING=
 "precision highp float;\n"
 "uniform vec3 eye; uniform vec3 hole; uniform vec3 mov; uniform float t;\n"
 "uniform vec3 pP[3]; uniform vec3 pA[3];\n"
 "varying vec3 wp; varying vec3 vb; varying vec3 vn; varying float vs; varying vec4 vr;\n"
 "float h1(vec3 q){ return fract(sin(dot(q,vec3(12.9898,78.233,37.719)))*43758.5453); }\n"
 "void main(){\n"
 "  float circ=6.2831853*vr.y; float u=mod(vb.x,circ); float v=vb.y, av=abs(v); float kind=vb.z;\n"
 "  if(vr.w>0.0&&mod(u-vr.z,circ)<vr.w) discard;\n"
 "  float sty=floor(vs+0.001), seed=fract(vs);\n"
 "  float L=3.4; float ru=fract(u/L);\n"
 "  float rail=step(0.80,av), rib=step(av,0.07), rung=step(ru,0.09);\n"
 "  float bay=mod(floor(u/(L*5.0)),2.0);\n"
 "  if(kind<1.5){\n"
 "    if(sty>0.5&&sty<1.5&&rail<0.5&&rung<0.5) discard;\n"
 "    if(sty>1.5&&rail<0.5&&rib<0.5&&rung<0.5&&bay<0.5) discard; }\n"
 "  float pan=0.75+0.5*h1(vec3(floor(u/L),floor(v*2.0),seed*91.0));\n"
 "  vec3 alb=vec3(0.30,0.30,0.32)*(rail>0.5?1.25:(rib>0.5||rung>0.5)?1.1:0.6*pan);\n"
 "  vec3 N=normalize(vn); vec3 V=normalize(eye-wp); vec3 col;\n"
 "  vec3 star=vec3(1.0,0.72,0.48);   /* a 3200 K blackbody */\n"
 "  if(kind>0.5&&kind<1.5){\n"
 "    float d=length(wp); float irr=1.0/(d*d);\n"
 "    col=alb*star*irr*2600.0;\n"
 "    float sp=pow(max(dot(N,normalize(normalize(-wp)+V)),0.0),24.0);\n"
 "    col+=star*sp*irr*420.0*(rail>0.5?1.0:0.4);\n"
 "  } else {\n"
 "    vec3 Lh=hole-wp; float dl=length(Lh); Lh/=dl; float ndl=max(dot(N,Lh),0.0);\n"
 "    vec3 dk=vec3(0.80,0.88,1.0)*2600.0/(dl*dl);\n"
 "    float sp=pow(max(dot(N,normalize(Lh+V)),0.0),60.0);\n"
 "    col=alb*ndl*dk+sp*dk*0.30*(rail>0.5?1.2:0.6);\n"
 "    for(int k=0;k<3;k++){ vec3 w=wp-pP[k]; float dw=length(w); float c=abs(dot(w/dw,pA[k]));\n"
 "      float spot=0.0;\n"
 "      col+=vec3(0.55,0.72,1.0)*spot*(alb*6.0*max(dot(N,-w/dw),0.0)+0.35*pow(max(dot(N,normalize(-w/dw+V)),0.0),40.0)); }\n"
 "    if(kind>1.5){ float d=length(wp); col+=alb*star*40.0/(d*d)*40.0; }\n"
 "    float bw=max(dot(N,mov),0.0); float fr=pow(1.0-max(dot(N,V),0.0),2.0);\n"
 "    bw*=fr*0.0;\n"
 "  }\n"
 "  gl_FragColor=vec4(col*0.25, clamp(length(wp-eye)/1500.0,0.0,0.99));\n"
 "}\n";
/* the red dwarf: limb-darkened, boiling, spotted */
static const char*LH_FS_STAR=
 "precision highp float;\n"
 "uniform vec3 eye; uniform float t; varying vec3 wp; varying vec3 vb; varying vec3 vn; varying float vs;\n"
 "float h3(vec3 p){ return fract(sin(dot(p,vec3(127.1,311.7,74.7)))*43758.5453); }\n"
 "float n3(vec3 p){ vec3 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);\n"
 "  return mix(mix(mix(h3(i),h3(i+vec3(1,0,0)),f.x),mix(h3(i+vec3(0,1,0)),h3(i+vec3(1,1,0)),f.x),f.y),\n"
 "             mix(mix(h3(i+vec3(0,0,1)),h3(i+vec3(1,0,1)),f.x),mix(h3(i+vec3(0,1,1)),h3(i+vec3(1,1,1)),f.x),f.y),f.z); }\n"
 "void main(){ vec3 N=normalize(wp); float mu=max(dot(N,normalize(eye-wp)),0.0);\n"
 "  float g=n3(N*9.0+vec3(t*0.15,0.0,t*0.11))*0.6+n3(N*23.0-vec3(0.0,t*0.3,0.0))*0.4;\n"
 "  float spot=smoothstep(0.62,0.7,n3(N*3.0+vec3(7.1)));\n"
 "  vec3 c=mix(vec3(1.0,0.60,0.36),vec3(1.0,0.80,0.58),g)*(0.35+0.65*pow(mu,0.55))*(1.0-0.7*spot)*5.0;\n"
 "  gl_FragColor=vec4(c*0.25, clamp(length(wp-eye)/1500.0,0.0,0.99)); }\n";
static const char*LH_VS_QUAD=
 "attribute vec2 p; varying vec2 uv; void main(){ uv=p*0.5+0.5; gl_Position=vec4(p,0.0,1.0); }\n";
/* THE LENS: every pixel's ray, bent past the hole */
static const char*LH_FS_LENS=
 "precision highp float;\n"
 "varying vec2 uv; uniform sampler2D scn; uniform float lensOn;\n"
 "uniform vec3 eye; uniform vec3 cR; uniform vec3 cU; uniform vec3 cF; uniform vec2 tanH; uniform mat4 vp;\n"
 "uniform vec3 H; uniform float rs; uniform float rin; uniform float rout; uniform vec3 dN; uniform vec3 dU; uniform vec3 dV;\n"
 "uniform float t; uniform vec3 pP[3]; uniform vec3 pA[3]; uniform float pF[3];\n"
 "const float RM=60.0; const float LR=44.3;\n"
 "float hs(vec2 p){ return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }\n"
 "float vn2(vec2 p){ vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);\n"
 "  return mix(mix(hs(i),hs(i+vec2(1,0)),f.x),mix(hs(i+vec2(0,1)),hs(i+vec2(1,1)),f.x),f.y); }\n"
 "float fbm(vec2 p){ return vn2(p)*0.55+vn2(p*2.1+3.7)*0.3+vn2(p*4.3-1.3)*0.15; }\n"
 "vec3 lookup(vec3 p, vec3 v){\n"
 "  float tt=max(length(H-eye)-length(p-eye)+15.0,1.0); vec4 s=vec4(0.0,0.0,0.0,1.0);\n"
 "  for(int k=0;k<3;k++){ vec3 q=p+v*tt; vec4 c=vp*vec4(q,1.0); if(c.w<=0.01) return vec3(0.0);\n"
 "    vec2 u=c.xy/c.w*0.5+0.5; if(u.x<0.0||u.y<0.0||u.x>1.0||u.y>1.0) return vec3(0.0);\n"
 "    s=texture2D(scn,u); if(s.a>0.985) break; tt=max(tt+s.a*1500.0-length(q-eye),0.5); }\n"
 "  return s.a>0.985? vec3(0.0) : s.rgb*4.0; }\n"
 "uniform float Tin;\n"
 "vec3 bb(float T){ T=clamp(T,1000.0,40000.0)/100.0; vec3 c;\n"
 "  c.r=T<=66.0?1.0:clamp(1.292936*pow(T-60.0,-0.1332047),0.0,1.0);\n"
 "  c.g=T<=66.0?clamp(0.3900816*log(T)-0.6318414,0.0,1.0):clamp(1.129891*pow(T-60.0,-0.0755148),0.0,1.0);\n"
 "  c.b=T>=66.0?1.0:(T<=19.0?0.0:clamp(0.5432068*log(T-10.0)-1.1962541,0.0,1.0));\n"
 "  return c; }\n"
 "vec4 disk(vec3 xr, float rr, vec3 v){\n"
 "  float x=(rr-rin)/(rout-rin);\n"
 "  float om=1.3*pow(rin/rr,1.5)*t; float cu=dot(xr,dU), cv=dot(xr,dV);\n"
 "  vec2 pr=vec2(cu*cos(om)-cv*sin(om), cu*sin(om)+cv*cos(om));\n"
 "  float n=fbm(pr*0.9); float ring=0.8+0.2*sin(rr*5.0+n*6.0);\n"
 "  float Tm=pow(rin/rr,0.75)*pow(max(1.0-sqrt(rin/rr),0.0),0.25)*1.5;\n"
 "  vec3 u=normalize(cross(dN,xr)); float be=min(sqrt(0.5*rs/max(rr-rs,0.05)),0.9); float ga=1.0/sqrt(1.0-be*be);\n"
 "  float g=sqrt(max(1.0-rs/rr,0.0))/(ga*(1.0-be*dot(u,-v)));\n"
 "  float T=Tin*Tm*g;\n"
 "  vec3 c=bb(T)*pow(Tm*g,4.0)*6.0;\n"
 "  float a=smoothstep(0.0,0.07,x)*(1.0-smoothstep(0.55,1.0,x))*(0.45+0.55*n)*ring;\n"
 "  return vec4(c,clamp(a,0.0,0.95)); }\n"
 "void main(){\n"
 "  vec2 nd=uv*2.0-1.0; vec3 d=normalize(cF+nd.x*tanH.x*cR+nd.y*tanH.y*cU);\n"
 "  vec4 s0=texture2D(scn,uv); vec3 base=s0.rgb*4.0; float sd=s0.a>0.985?1e9:s0.a*1500.0;\n"
 "  if(lensOn<0.5){ gl_FragColor=vec4(base*0.25,1.0); return; }\n"
 "  vec3 oc=H-eye; float tc=dot(oc,d); float b2=dot(oc,oc)-tc*tc;\n"
 "  if(b2>RM*RM||tc<-RM){ gl_FragColor=vec4(base*0.25,1.0); return; }\n"
 "  float t0=max(tc-sqrt(RM*RM-b2),0.0);\n"
 "  if(sd<t0){ gl_FragColor=vec4(base*0.25,1.0); return; }\n"
 "  vec3 p=eye+d*t0; vec3 v=d; float path=t0; vec3 acc=vec3(0.0); float T=1.0; vec3 bg=vec3(0.0); bool fin=false;\n"
 "  float jit=hs(gl_FragCoord.xy+fract(t*7.13)*31.0);\n"
 "  for(int i=0;i<220;i++){\n"
 "    vec3 r=p-H; float rl=length(r);\n"
 "    if(rl<rs){ fin=true; break; }\n"
 "    if(i>0&&rl>RM&&dot(r,v)>0.0){ bg=lookup(p,v); fin=true; break; }\n"
 "    if(dot(p,p)<LR*LR){ vec4 c=vp*vec4(p,1.0); vec2 u=c.xy/c.w*0.5+0.5; bg=texture2D(scn,u).rgb*4.0; fin=true; break; }\n"
 "    float h=clamp(rl*0.04,0.004,2.5); if(i==0) h*=0.5+jit;\n"
 "    vec3 cr=cross(r,v); float h2=dot(cr,cr);\n"
 "    vec3 v2=normalize(v-1.5*rs*h2*r/pow(rl,5.0)*h);\n"
 "    vec3 pn=p+v2*h;\n"
 "    float a0=dot(r,dN), a1=dot(pn-H,dN);\n"
 "    if(a0*a1<0.0){ vec3 x=p+(pn-p)*(a0/(a0-a1)); vec3 xr=x-H; float rr=length(xr);\n"
 "      if(rr>rin&&rr<rout){ vec4 dc=disk(xr,rr,v2); acc+=T*dc.rgb*dc.a; T*=1.0-dc.a; } }\n"
 "    for(int k=0;k<3;k++){\n"
 "      vec3 w0=p-pP[k]; float sp=-dot(w0,v2);\n"
 "      if(sp>0.0&&sp<=h){ vec3 cl=w0+v2*sp; float dd=dot(cl,cl);\n"
 "        acc+=T*vec3(0.80,0.88,1.0)*(0.004+pF[k]*14.0)*(0.0004/(dd+0.0004)*3.0+0.02/(1.0+dd*40.0)); }\n"
 "      float bb=dot(v2,pA[k]); float dn=max(1.0-bb*bb,1e-4); float dq=dot(v2,w0), eq=dot(pA[k],w0);\n"
 "      float sc=(bb*eq-dq)/dn;\n"
 "      if(sc>0.0&&sc<=h){ float tb=eq+bb*sc; vec3 cp=w0+v2*sc-pA[k]*tb; float pd=dot(cp,cp);\n"
 "        float wd=0.012*abs(tb)+0.05;\n"
 "        float ib=exp(-pd/(wd*wd))*exp(-abs(tb)/22.0)*smoothstep(0.15,1.2,abs(tb))*(1.0-smoothstep(24.0,36.0,abs(tb)))*1.7725/max(sqrt(dn),0.06);\n"
 "        ib*=0.0; } }\n"
 "    p=pn; v=v2; path+=h;\n"
 "    if(T<0.01){ fin=true; break; }\n"
 "  }\n"
 "  if(!fin) bg=lookup(p,v);\n"
 "  gl_FragColor=vec4((acc+T*bg)*0.25,1.0);\n"
 "}\n";
static const char*LH_FS_BRIGHT=
 "precision mediump float; varying vec2 uv; uniform sampler2D tx; uniform vec2 px;\n"
 "void main(){ vec3 c=vec3(0.0);\n"
 "  c+=texture2D(tx,uv+px*vec2(-1.0,-1.0)).rgb; c+=texture2D(tx,uv+px*vec2(1.0,-1.0)).rgb;\n"
 "  c+=texture2D(tx,uv+px*vec2(-1.0,1.0)).rgb; c+=texture2D(tx,uv+px*vec2(1.0,1.0)).rgb;\n"
 "  c=c*0.25*4.0; c=max(c-vec3(0.55),0.0); gl_FragColor=vec4(c*0.25,1.0); }\n";
static const char*LH_FS_BLUR=
 "precision mediump float; varying vec2 uv; uniform sampler2D tx; uniform vec2 dir;\n"
 "void main(){ vec3 c=texture2D(tx,uv).rgb*0.227;\n"
 "  c+=texture2D(tx,uv+dir*1.385).rgb*0.316; c+=texture2D(tx,uv-dir*1.385).rgb*0.316;\n"
 "  c+=texture2D(tx,uv+dir*3.231).rgb*0.070; c+=texture2D(tx,uv-dir*3.231).rgb*0.070;\n"
 "  gl_FragColor=vec4(c,1.0); }\n";
static const char*LH_FS_FINAL=
 "precision highp float; varying vec2 uv; uniform sampler2D tb; uniform sampler2D bl; uniform sampler2D bl2;\n"
 "uniform float fade; uniform float t; uniform vec2 res;\n"
 "float hs(vec2 p){ return fract(sin(dot(p,vec2(12.9898,78.233)))*43758.5453); }\n"
 "void main(){ vec3 c=texture2D(tb,uv).rgb*4.0+texture2D(bl,uv).rgb*4.0*0.7+texture2D(bl2,uv).rgb*4.0*0.5;\n"
 "  c*=fade*0.9;\n"
 "  c=(c*(2.51*c+0.03))/(c*(2.43*c+0.59)+0.14);\n"
 "  vec2 q=uv-0.5; c*=1.0-0.45*pow(length(q)*1.25,2.4);\n"
 "  c=pow(clamp(c,0.0,1.0),vec3(1.0/2.2));\n"
 "  c+=(hs(uv*res+fract(t*3.1)*91.0)-0.5)*(1.6/255.0);\n"
 "  gl_FragColor=vec4(c,1.0); }\n";
static const char*LH_VS_TXT=
 "attribute vec2 p; attribute vec3 b; uniform vec2 scr; varying vec2 tc;\n"
 "void main(){ tc=b.xy; gl_Position=vec4(p.x/scr.x*2.0-1.0,1.0-p.y/scr.y*2.0,0.0,1.0); }\n";
static const char*LH_FS_TXT=
 "precision mediump float; varying vec2 tc; uniform sampler2D ft; uniform vec4 col; uniform float useTex;\n"
 "void main(){ float a=useTex>0.5?texture2D(ft,tc).r:1.0; gl_FragColor=vec4(col.rgb,col.a*a); }\n";

/* ---- state ---- */
typedef struct { GLuint fbo,tex,w,h; } LhTarget;
static struct {
    int ok, W, H;
    GLuint pHull,pStar,pLens,pBright,pBlur,pFinal,pTxt;
    GLuint vHull,vStar,vQuad,vTxt; int nHull,nStar;
    LhTarget scn,lens,b1,b2,b3;
    GLuint depthRb;
    /* fonts: 0 = the menu (light sans), 1 = her terminal line */
    GLuint ftex[2]; stbtt_bakedchar fc[2][96]; float fpx[2]; int fok[2];
    int sTick[3], sHum, sGroan, sKey, sMove, sOk, vHum, vGroan;
} LH;

static LhTarget lhTarget(int w,int h,int depth){
    LhTarget T; T.w=w; T.h=h;
    glGenTextures(1,&T.tex); glBindTexture(GL_TEXTURE_2D,T.tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1,&T.fbo); glBindFramebuffer(GL_FRAMEBUFFER,T.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,T.tex,0);
    if(depth){ glGenRenderbuffers(1,&LH.depthRb); glBindRenderbuffer(GL_RENDERBUFFER,LH.depthRb);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT16,w,h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,LH.depthRb); }
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) printf("[lighthouse] FBO %dx%d INCOMPLETE\n",w,h);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    return T; }

/* an icosphere, subdivided n times, as a flat triangle list of unit vectors */
static int lhIco(int sub,float**out){
    static const float X=0.525731112119133606f, Z=0.850650808352039932f;
    float vv[12][3]={{-X,0,Z},{X,0,Z},{-X,0,-Z},{X,0,-Z},{0,Z,X},{0,Z,-X},{0,-Z,X},{0,-Z,-X},{Z,X,0},{-Z,X,0},{Z,-X,0},{-Z,-X,0}};
    static const int tt[20][3]={{0,4,1},{0,9,4},{9,5,4},{4,5,8},{4,8,1},{8,10,1},{8,3,10},{5,3,8},{5,2,3},{2,7,3},
        {7,10,3},{7,6,10},{7,11,6},{11,0,6},{0,1,6},{6,1,10},{9,0,11},{9,11,2},{9,2,5},{7,2,11}};
    int n=20; float*a=(float*)malloc(sizeof(float)*9*20);
    for(int i=0;i<20;i++) for(int k=0;k<3;k++) memcpy(a+i*9+k*3,vv[tt[i][k]],12);
    for(int s=0;s<sub;s++){ float*b=(float*)malloc(sizeof(float)*9*n*4);
        for(int i=0;i<n;i++){ float*A=a+i*9,*B=A+3,*C=A+6, ab[3],bc[3],ca[3];
            for(int k=0;k<3;k++){ ab[k]=A[k]+B[k]; bc[k]=B[k]+C[k]; ca[k]=C[k]+A[k]; }
            lhNorm(ab); lhNorm(bc); lhNorm(ca);
            float*o=b+i*36;
            memcpy(o,A,12); memcpy(o+3,ab,12); memcpy(o+6,ca,12);
            memcpy(o+9,ab,12); memcpy(o+12,B,12); memcpy(o+15,bc,12);
            memcpy(o+18,ca,12); memcpy(o+21,bc,12); memcpy(o+24,C,12);
            memcpy(o+27,ab,12); memcpy(o+30,bc,12); memcpy(o+33,ca,12); }
        free(a); a=b; n*=4; }
    *out=a; return n; }

/* vertex: p3 b3 n3 s1 */
static GLuint lhMeshVbo(const float*v,int nv){ GLuint b; glGenBuffers(1,&b); glBindBuffer(GL_ARRAY_BUFFER,b);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)nv*40,v,GL_STATIC_DRAW); return b; }
static void lhMeshAttribs(void){
    glDisableVertexAttribArray(4); glVertexAttrib4f(4,0.f,1.f,0.f,0.f);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,40,(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,40,(void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,40,(void*)24);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3,1,GL_FLOAT,GL_FALSE,40,(void*)36);
    for(int k=4;k<8;k++) glDisableVertexAttribArray(k); }

/* ring vertex: p3 b3 n3 s1 r4 = 14 floats */
static void lhRingAttribs(void){
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,56,(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,56,(void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,56,(void*)24);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3,1,GL_FLOAT,GL_FALSE,56,(void*)36);
    glEnableVertexAttribArray(4); glVertexAttribPointer(4,4,GL_FLOAT,GL_FALSE,56,(void*)40);
    for(int k=5;k<8;k++) glDisableVertexAttribArray(k); }

/* THE SWARM: LH_NRING bands, each a zone of a sphere around a great circle. Normals spread on a
 * golden spiral over the hemisphere (a ring and its flip are the same ring); radii shuffled
 * across 8.4..10.4 so neighbours in tilt are never neighbours in depth; thickness 0.05 inside a
 * 0.087 spacing, so no two can touch. */
static void lhBuildGeometry(void){
    enum { SEG=320, ROWS=4 };
    int per=SEG*(ROWS*2+2)*6; float*v=(float*)malloc(sizeof(float)*14*per*LH_NRING); int nv=0;
    int order[LH_NRING]; for(int i=0;i<LH_NRING;i++) order[i]=i;
    for(int i=LH_NRING-1;i>0;i--){ int j=(int)(lhHash(i*31+7)*(i+1)); if(j>i)j=i; int t=order[i]; order[i]=order[j]; order[j]=t; }
    for(int i=0;i<LH_NRING;i++){
        float z=(i+0.5f)/LH_NRING, rr2=sqrtf(1.f-z*z), g=2.3999632f*i+0.4f*lhHash(i*5+2);
        float N[3]={rr2*cosf(g),z,rr2*sinf(g)}; lhNorm(N);
        float E1[3], ref[3]={0.f,1.f,0.f}; if(fabsf(N[1])>0.9f){ ref[0]=1.f; ref[1]=0.f; }
        lhCross(E1,N,ref); lhNorm(E1); float E2[3]; lhCross(E2,N,E1);
        float R=40.f+order[i]*(4.0f/(LH_NRING-1)), th=0.10f;
        float hw=(1.35f+1.25f*lhHash(i*11+3))/R;                      /* angular half-width */
        float hs=lhHash(i*17+5); int sty=hs<0.30f?0:hs<0.70f?1:2;
        float om=4.85e-5f*powf(42.f/R,1.5f)*(lhHash(i*29+1)<0.5f?-1.f:1.f);   /* Kepler: one lap per ~36 h */
        float circ=6.2831853f*R, gap0=0.f, gapL=0.f;
        if(i%5==2){ gap0=lhHash(i*41+3)*circ; gapL=(0.12f+0.22f*lhHash(i*43+1))*circ; }
        float sd=sty+0.99f*lhHash(i*47+13);
        /* a point on the band: theta round the ring, phi across it, at radius rad */
        #define RP(o,th_,ph_,rad) do{ float ct=cosf(th_),st=sinf(th_),cp=cosf(ph_),sp=sinf(ph_); \
            for(int j=0;j<3;j++) (o)[j]=(rad)*(cp*(ct*E1[j]+st*E2[j])+sp*N[j]); }while(0)
        #define EMIT(P,NR,U,VV,K) do{ float*o=v+nv*14; memcpy(o,P,12); o[3]=(U); o[4]=(VV); o[5]=(K); \
            memcpy(o+6,NR,12); o[9]=sd; o[10]=om; o[11]=R; o[12]=gap0; o[13]=gapL; nv++; }while(0)
        for(int s=0;s<SEG;s++){ float t0=6.2831853f*s/SEG, t1=6.2831853f*(s+1)/SEG, u0=t0*R, u1=t1*R;
            for(int surf=0;surf<2;surf++){ float rad=surf?R-th:R, sg=surf?-1.f:1.f;
                for(int rw=0;rw<ROWS;rw++){ float v0=-1.f+2.f*rw/ROWS, v1=-1.f+2.f*(rw+1)/ROWS;
                    float P00[3],P10[3],P01[3],P11[3],N00[3],N10[3],N01[3],N11[3];
                    RP(P00,t0,v0*hw,rad); RP(P10,t1,v0*hw,rad); RP(P01,t0,v1*hw,rad); RP(P11,t1,v1*hw,rad);
                    RP(N00,t0,v0*hw,sg); RP(N10,t1,v0*hw,sg); RP(N01,t0,v1*hw,sg); RP(N11,t1,v1*hw,sg);
                    EMIT(P00,N00,u0,v0,(float)surf); EMIT(P10,N10,u1,v0,(float)surf); EMIT(P11,N11,u1,v1,(float)surf);
                    EMIT(P00,N00,u0,v0,(float)surf); EMIT(P11,N11,u1,v1,(float)surf); EMIT(P01,N01,u0,v1,(float)surf); } }
            for(int side=0;side<2;side++){ float ph=side?hw:-hw, sg=side?1.f:-1.f;
                float A0[3],A1[3],B0[3],B1[3],W0[3],W1[3];
                RP(A0,t0,ph,R); RP(A1,t1,ph,R); RP(B0,t0,ph,R-th); RP(B1,t1,ph,R-th);
                for(int j=0;j<3;j++){ W0[j]=sg*(-sinf(ph)*(cosf(t0)*E1[j]+sinf(t0)*E2[j])+cosf(ph)*N[j]);
                                      W1[j]=sg*(-sinf(ph)*(cosf(t1)*E1[j]+sinf(t1)*E2[j])+cosf(ph)*N[j]); }
                float vv=side?1.f:-1.f;
                EMIT(A0,W0,u0,vv,2.f); EMIT(A1,W1,u1,vv,2.f); EMIT(B1,W1,u1,vv,2.f);
                EMIT(A0,W0,u0,vv,2.f); EMIT(B1,W1,u1,vv,2.f); EMIT(B0,W0,u0,vv,2.f); } }
        #undef RP
        #undef EMIT
    }
    { GLuint b; glGenBuffers(1,&b); glBindBuffer(GL_ARRAY_BUFFER,b); glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)nv*56,v,GL_STATIC_DRAW);
      LH.vHull=b; LH.nHull=nv; }
    free(v);
    float*ico; int n=lhIco(4,&ico); v=(float*)malloc(sizeof(float)*10*3*n); nv=0;
    for(int i=0;i<n*3;i++){ float*o=v+nv*10; for(int j=0;j<3;j++){ o[j]=ico[i*3+j]*LH_STAR; o[6+j]=ico[i*3+j]; }
        o[3]=o[4]=o[5]=0; o[9]=0; nv++; }
    LH.vStar=lhMeshVbo(v,nv); LH.nStar=nv; free(v); free(ico);
    printf("[lighthouse] swarm %d rings (%d tris), star %d tris\n",LH_NRING,LH.nHull/3,LH.nStar/3); }

static void lhFont(int f,const char*const*paths,float px){
    for(int i=0;paths[i];i++){ FILE*fp=fopen(paths[i],"rb"); if(!fp) continue;
        fseek(fp,0,SEEK_END); long sz=ftell(fp); fseek(fp,0,SEEK_SET);
        unsigned char*ttf=(unsigned char*)malloc((size_t)sz); fread(ttf,1,(size_t)sz,fp); fclose(fp);
        unsigned char*bmp=(unsigned char*)calloc(1024*1024,1);
        int r=stbtt_BakeFontBitmap(ttf,0,px,bmp,1024,1024,32,96,LH.fc[f]); free(ttf);
        if(r==0){ free(bmp); continue; }
        glGenTextures(1,&LH.ftex[f]); glBindTexture(GL_TEXTURE_2D,LH.ftex[f]);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE,1024,1024,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,bmp);
        glPixelStorei(GL_UNPACK_ALIGNMENT,4);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        free(bmp); LH.fpx[f]=px; LH.fok[f]=1;
        printf("[lighthouse] font %d: %s\n",f,paths[i]); return; }
    printf("[lighthouse] ⛔ font %d: NONE of the paths opened -- the blocky debug font stands in\n",f); }

/* text width in pixels at size px with tracking (em fraction) */
static float lhTextW(int f,float px,float trk,const char*s){
    if(!LH.fok[f]) return stb_easy_font_width((char*)s)*px/12.f;
    float sc=px/LH.fpx[f], w=0; for(;*s;s++){ int c=(unsigned char)*s; if(c<32||c>127) continue;
        w+=LH.fc[f][c-32].xadvance*sc+trk*px; } return w; }
static void lhText(int f,float x,float y,float px,float trk,const char*s,float r,float g,float b,float a){
    static float buf[4096*4]; int nv=0;
    glUseProgram(LH.pTxt);
    glUniform2f(glGetUniformLocation(LH.pTxt,"scr"),(float)LH.W,(float)LH.H);
    glUniform4f(glGetUniformLocation(LH.pTxt,"col"),r,g,b,a);
    if(LH.fok[f]){
        float sc=px/LH.fpx[f], cx=0;
        for(;*s&&nv<3900;s++){ int c=(unsigned char)*s; if(c<32||c>127) continue;
            stbtt_aligned_quad q; float qx=0,qy=0;
            stbtt_GetBakedQuad(LH.fc[f],1024,1024,c-32,&qx,&qy,&q,1);
            float x0=x+cx+q.x0*sc, x1=x+cx+q.x1*sc, y0=y+q.y0*sc, y1=y+q.y1*sc;
            float qd[6][4]={{x0,y0,q.s0,q.t0},{x1,y0,q.s1,q.t0},{x1,y1,q.s1,q.t1},{x0,y0,q.s0,q.t0},{x1,y1,q.s1,q.t1},{x0,y1,q.s0,q.t1}};
            for(int k=0;k<6;k++){ buf[nv*4]=qd[k][0]; buf[nv*4+1]=qd[k][1]; buf[nv*4+2]=qd[k][2]; buf[nv*4+3]=qd[k][3]; nv++; }
            cx+=qx*sc+trk*px; }
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,LH.ftex[f]);
        glUniform1i(glGetUniformLocation(LH.pTxt,"ft"),0);
        glUniform1f(glGetUniformLocation(LH.pTxt,"useTex"),1.f);
    } else {
        static char eb[60000]; float sc=px/12.f;
        int nq=stb_easy_font_print(0,0,(char*)s,NULL,eb,sizeof eb); static const int iq[6]={0,1,2,0,2,3};
        for(int i=0;i<nq&&nv<3900;i++) for(int k=0;k<6;k++){ float*vv=(float*)(eb+i*64+iq[k]*16);
            buf[nv*4]=x+vv[0]*sc; buf[nv*4+1]=y-px*0.8f+vv[1]*sc; buf[nv*4+2]=0; buf[nv*4+3]=0; nv++; }
        glUniform1f(glGetUniformLocation(LH.pTxt,"useTex"),0.f); }
    glBindBuffer(GL_ARRAY_BUFFER,LH.vTxt); glBufferData(GL_ARRAY_BUFFER,nv*16,buf,GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,16,(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,16,(void*)8);
    for(int k=2;k<8;k++) glDisableVertexAttribArray(k);
    glDrawArrays(GL_TRIANGLES,0,nv); }

/* a flat rectangle in screen pixels (the text program, untextured) */
static void lhRect(float x0,float y0,float x1,float y1,float r,float g,float b,float a){
    float q[24]={x0,y0,0,0, x1,y0,0,0, x1,y1,0,0, x0,y0,0,0, x1,y1,0,0, x0,y1,0,0};
    glUseProgram(LH.pTxt);
    glUniform2f(glGetUniformLocation(LH.pTxt,"scr"),(float)LH.W,(float)LH.H);
    glUniform4f(glGetUniformLocation(LH.pTxt,"col"),r,g,b,a);
    glUniform1f(glGetUniformLocation(LH.pTxt,"useTex"),0.f);
    glBindBuffer(GL_ARRAY_BUFFER,LH.vTxt); glBufferData(GL_ARRAY_BUFFER,sizeof q,q,GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,16,(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,16,(void*)8);
    for(int k=2;k<8;k++) glDisableVertexAttribArray(k);
    glDrawArrays(GL_TRIANGLES,0,6); }

/* ---- sound: synthesised once, into the mixer's bank ---- */
static int lhBankPCM(const char*name,float*pcm,int n){ return sfxBankPCM(name,pcm,n); }   /* club.h (10-22) */
static void lhVoiceGain(int id,float g){ sfxVoiceGain(id,g); }
static void lhSynth(void){
    const float R=(float)SFX_RATE, TAU=6.2831853f;
    /* a pulsar tick: a soft click, a little ring, a long faint tail -- three voices */
    static const float tf[3]={1180.f,890.f,620.f};
    for(int k=0;k<3;k++){ int n=(int)(R*0.9f); float*a=(float*)calloc((size_t)n,4); unsigned rs=1234u+k;
        for(int i=0;i<n;i++){ float tt=i/R; rs=rs*1664525u+1013904223u; float nz=((rs>>9)/8388608.f)-1.f;
            a[i]=nz*expf(-tt*900.f)*0.5f + sinf(TAU*tf[k]*tt)*expf(-tt*38.f)*0.35f + sinf(TAU*tf[k]*0.5f*tt)*expf(-tt*6.f)*0.07f
                + sinf(TAU*58.f*tt)*expf(-tt*20.f)*0.25f; }
        /* a cheap room: two feedback combs */
        for(int i=1500;i<n;i++) a[i]+=a[i-1500]*0.32f; for(int i=2333;i<n;i++) a[i]+=a[i-2333]*0.22f;
        char nm[16]; snprintf(nm,sizeof nm,"lh_tick%d",k); LH.sTick[k]=lhBankPCM(nm,a,n); }
    /* the sphere's hum: 8 s, every partial a whole number of cycles so the loop is seamless */
    { int n=(int)(R*8); float*a=(float*)malloc((size_t)n*4);
      for(int i=0;i<n;i++){ float tt=i/R;
          float am=0.75f+0.25f*sinf(TAU*0.125f*tt), am2=0.6f+0.4f*sinf(TAU*0.25f*tt+1.f);
          a[i]=(sinf(TAU*41.25f*tt)*0.5f+sinf(TAU*55.f*tt)*0.3f*am2+sinf(TAU*82.5f*tt)*0.12f+sinf(TAU*123.75f*tt)*0.05f*am)*am; }
      LH.sHum=lhBankPCM("lh_hum",a,n); }
    /* the gravitational groan: very low, slowly bending (phase-modulated, still seamless) */
    { int n=(int)(R*8); float*a=(float*)malloc((size_t)n*4);
      for(int i=0;i<n;i++){ float tt=i/R; float pm=1.8f*sinf(TAU*0.125f*tt)+0.9f*sinf(TAU*0.375f*tt);
          float env=0.55f+0.45f*sinf(TAU*0.125f*tt-1.2f);
          a[i]=(sinf(TAU*27.5f*tt+pm)*0.6f+sinf(TAU*28.875f*tt+pm*1.3f)*0.4f+sinf(TAU*55.f*tt+pm*2.f)*0.12f)*env; }
      LH.sGroan=lhBankPCM("lh_groan",a,n); }
    /* her keystroke; the menu's move and confirm (a bit-crushed blip: her only sound here) */
    { int n=(int)(R*0.05f); float*a=(float*)malloc((size_t)n*4); unsigned rs=99u;
      for(int i=0;i<n;i++){ float tt=i/R; rs=rs*1664525u+1013904223u; a[i]=(((rs>>9)/8388608.f)-1.f)*expf(-tt*320.f)*0.5f+sinf(TAU*2400.f*tt)*expf(-tt*200.f)*0.15f; }
      LH.sKey=lhBankPCM("lh_key",a,n); }
    { int n=(int)(R*0.09f); float*a=(float*)malloc((size_t)n*4);
      for(int i=0;i<n;i++){ float tt=i/R; float s=sinf(TAU*1560.f*tt)*expf(-tt*55.f); a[i]=floorf(s*6.f)/6.f*0.22f; }
      LH.sMove=lhBankPCM("lh_move",a,n); }
    { int n=(int)(R*1.4f); float*a=(float*)malloc((size_t)n*4);
      for(int i=0;i<n;i++){ float tt=i/R; a[i]=(sinf(TAU*392.f*tt)*0.5f+sinf(TAU*587.33f*tt)*0.3f+sinf(TAU*783.99f*tt)*0.15f)*expf(-tt*2.6f)*(1.f-expf(-tt*200.f))*0.28f; }
      LH.sOk=lhBankPCM("lh_ok",a,n); }
}

static void lhInit(int W,int H){
    LH.W=W; LH.H=H;
    LH.pHull=lhProg(LH_VS_RING,LH_FS_RING,"rings"); LH.pStar=lhProg(LH_VS_MESH,LH_FS_STAR,"star");
    LH.pLens=lhProg(LH_VS_QUAD,LH_FS_LENS,"lens"); LH.pBright=lhProg(LH_VS_QUAD,LH_FS_BRIGHT,"bright");
    LH.pBlur=lhProg(LH_VS_QUAD,LH_FS_BLUR,"blur"); LH.pFinal=lhProg(LH_VS_QUAD,LH_FS_FINAL,"final");
    LH.pTxt=lhProg(LH_VS_TXT,LH_FS_TXT,"text");
    lhBuildGeometry();
    { float q[12]={-1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1}; glGenBuffers(1,&LH.vQuad); glBindBuffer(GL_ARRAY_BUFFER,LH.vQuad);
      glBufferData(GL_ARRAY_BUFFER,sizeof q,q,GL_STATIC_DRAW); }
    glGenBuffers(1,&LH.vTxt);
    LH.scn=lhTarget(W,H,1); LH.lens=lhTarget(W,H,0);
    LH.b1=lhTarget(W/4,H/4,0); LH.b2=lhTarget(W/4,H/4,0); LH.b3=lhTarget(W/4,H/4,0);
    { static const char*const menuF[]={"C:/Windows/Fonts/segoeuil.ttf","C:/Windows/Fonts/segoeui.ttf","C:/Windows/Fonts/bahnschrift.ttf",NULL};
      static const char*const termF[]={"C:/Windows/Fonts/consola.ttf","C:/Windows/Fonts/cour.ttf",NULL};
      lhFont(0,menuF,64.f); lhFont(1,termF,48.f); }
    if(!sfxReady()) sfxInit();
    lhSynth();
    LH.vHum=sfxPlaySlot(LH.sHum,0.f,NULL,1); LH.vGroan=sfxPlaySlot(LH.sGroan,0.f,NULL,1);
    LH.ok=1; }

static void lhQuad(GLuint prog){ glBindBuffer(GL_ARRAY_BUFFER,LH.vQuad);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0);
    for(int k=1;k<8;k++) glDisableVertexAttribArray(k);
    (void)prog; glDrawArrays(GL_TRIANGLES,0,6); }
static void lhBind(LhTarget*T){ glBindFramebuffer(GL_FRAMEBUFFER,T?T->fbo:0); glViewport(0,0,T?T->w:LH.W,T?T->h:LH.H); }
static void lhTex(GLuint prog,const char*nm,int unit,GLuint tex){ glActiveTexture(GL_TEXTURE0+unit); glBindTexture(GL_TEXTURE_2D,tex);
    glUniform1i(glGetUniformLocation(prog,nm),unit); }

/* the three pulsars: where they sit at the hole's edge and where each beam points at time t */
static void lhPulsars(float t,float P[3][3],float A[3][3]){
    static const float rr[3]={12.f,18.f,26.f}, inc[3]={0.35f,-0.9f,1.25f}, ph[3]={0.4f,2.6f,4.4f};
    static const float orb[3]={109.f,200.f,350.f};   /* Kepler round 30,000 M_sun, seconds */
    static const float per[3]={2.4f,3.6f,6.0f};   /* 2:3:5 -- one cycle every 36 s */
    static const float ax[3][3]={{0.3f,1.f,0.2f},{-0.8f,0.4f,0.5f},{0.2f,-0.3f,1.f}};
    for(int k=0;k<3;k++){ float a=ph[k]+t*6.2831853f/orb[k];
        float x=cosf(a)*rr[k]*LH_RS, y=sinf(a)*rr[k]*LH_RS;
        P[k][0]=LH_H[0]+x; P[k][1]=LH_H[1]+y*sinf(inc[k]); P[k][2]=LH_H[2]+y*cosf(inc[k]);
        float K[3]={ax[k][0],ax[k][1],ax[k][2]}; lhNorm(K);
        float E1[3], up[3]={0.f,0.f,1.f}; lhCross(E1,K,up); lhNorm(E1); float E2[3]; lhCross(E2,K,E1);
        float sw=6.2831853f*t/per[k], tl=0.87f;       /* magnetic axis 50 deg off the spin axis */
        for(int j=0;j<3;j++) A[k][j]=cosf(tl)*K[j]+sinf(tl)*(cosf(sw)*E1[j]+sinf(sw)*E2[j]);
        lhNorm(A[k]); } }

static void lhDrawScene(const float*eye,const M4 vp,float t,float P[3][3],float A[3][3]){
    lhBind(&LH.scn);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    float mov[3]={0.55f,0.25f,0.8f}; lhNorm(mov);
    glUseProgram(LH.pHull);
    glUniformMatrix4fv(glGetUniformLocation(LH.pHull,"vp"),1,GL_FALSE,vp);
    glUniform3fv(glGetUniformLocation(LH.pHull,"eye"),1,eye);
    glUniform3fv(glGetUniformLocation(LH.pHull,"hole"),1,LH_H);
    glUniform3fv(glGetUniformLocation(LH.pHull,"mov"),1,mov);
    glUniform1f(glGetUniformLocation(LH.pHull,"t"),t);
    glUniform3fv(glGetUniformLocation(LH.pHull,"pP"),3,&P[0][0]);
    glUniform3fv(glGetUniformLocation(LH.pHull,"pA"),3,&A[0][0]);
    glBindBuffer(GL_ARRAY_BUFFER,LH.vHull); lhRingAttribs(); glDrawArrays(GL_TRIANGLES,0,LH.nHull);
    glUseProgram(LH.pStar);
    glUniformMatrix4fv(glGetUniformLocation(LH.pStar,"vp"),1,GL_FALSE,vp);
    glUniform3fv(glGetUniformLocation(LH.pStar,"eye"),1,eye);
    glUniform1f(glGetUniformLocation(LH.pStar,"t"),t);
    glBindBuffer(GL_ARRAY_BUFFER,LH.vStar); lhMeshAttribs(); glDrawArrays(GL_TRIANGLES,0,LH.nStar);
    glDisable(GL_DEPTH_TEST); }

static void lhPost(const float*eye,const float*ctr,float fovy,const M4 vp,float t,float fade,float P[3][3],float A[3][3],float*pf,int lensOn){
    float F[3]={ctr[0]-eye[0],ctr[1]-eye[1],ctr[2]-eye[2]}; lhNorm(F);
    float up[3]={0,1,0}, Rr[3], U[3]; lhCross(Rr,F,up); lhNorm(Rr); lhCross(U,Rr,F);
    float ty=tanf(fovy*(float)M_PI/360.f), tx=ty*(float)LH.W/(float)LH.H;
    float dN[3]={0.12f,1.f,0.28f}; lhNorm(dN); float dU[3], dV[3], ref[3]={1,0,0}; lhCross(dU,dN,ref); lhNorm(dU); lhCross(dV,dN,dU);
    lhBind(&LH.lens); glUseProgram(LH.pLens);
    lhTex(LH.pLens,"scn",0,LH.scn.tex);
    #define U3(n,v) glUniform3fv(glGetUniformLocation(LH.pLens,n),1,v)
    U3("eye",eye); U3("cR",Rr); U3("cU",U); U3("cF",F); U3("H",LH_H); U3("dN",dN); U3("dU",dU); U3("dV",dV);
    #undef U3
    glUniform2f(glGetUniformLocation(LH.pLens,"tanH"),tx,ty);
    glUniformMatrix4fv(glGetUniformLocation(LH.pLens,"vp"),1,GL_FALSE,vp);
    glUniform1f(glGetUniformLocation(LH.pLens,"rs"),LH_RS);
    glUniform1f(glGetUniformLocation(LH.pLens,"rin"),LH_RS*1.16f);
    glUniform1f(glGetUniformLocation(LH.pLens,"Tin"),13500.f);   /* a quiescent disk: 1e-8 Eddington */
    glUniform1f(glGetUniformLocation(LH.pLens,"rout"),LH_RS*25.0f);
    glUniform1f(glGetUniformLocation(LH.pLens,"t"),t);
    glUniform1f(glGetUniformLocation(LH.pLens,"lensOn"),(float)lensOn);
    glUniform3fv(glGetUniformLocation(LH.pLens,"pP"),3,&P[0][0]);
    glUniform3fv(glGetUniformLocation(LH.pLens,"pA"),3,&A[0][0]);
    glUniform1fv(glGetUniformLocation(LH.pLens,"pF"),3,pf);
    lhQuad(LH.pLens);
    lhBind(&LH.b1); glUseProgram(LH.pBright); lhTex(LH.pBright,"tx",0,LH.lens.tex);
    glUniform2f(glGetUniformLocation(LH.pBright,"px"),1.f/LH.W,1.f/LH.H); lhQuad(LH.pBright);
    /* pass 0 blurs the bright pass back into b1 (the tight bloom); pass 1 blurs b1 wider
     * into b3 (the wide bloom) and leaves b1 alone -- the final pass adds both */
    glUseProgram(LH.pBlur);
    for(int pass=0;pass<2;pass++){ float s=(pass?2.5f:1.f);
        lhBind(&LH.b2); lhTex(LH.pBlur,"tx",0,LH.b1.tex); glUniform2f(glGetUniformLocation(LH.pBlur,"dir"),s/LH.b1.w,0); lhQuad(LH.pBlur);
        lhBind(pass?&LH.b3:&LH.b1); lhTex(LH.pBlur,"tx",0,LH.b2.tex); glUniform2f(glGetUniformLocation(LH.pBlur,"dir"),0,s/LH.b1.h); lhQuad(LH.pBlur); }
    lhBind(NULL); glUseProgram(LH.pFinal);
    lhTex(LH.pFinal,"tb",0,LH.lens.tex); lhTex(LH.pFinal,"bl",1,LH.b1.tex); lhTex(LH.pFinal,"bl2",2,LH.b3.tex);
    glUniform1f(glGetUniformLocation(LH.pFinal,"fade"),fade); glUniform1f(glGetUniformLocation(LH.pFinal,"t"),t);
    glUniform2f(glGetUniformLocation(LH.pFinal,"res"),(float)LH.W,(float)LH.H);
    lhQuad(LH.pFinal); glActiveTexture(GL_TEXTURE0); }

static void lhShot(void){
    static long shotN=-2, fno=0; static const char*path=0; static int burst=1, every=1, taken=0;
    if(shotN==-2){ const char*e=getenv("REVA_SHOT"); shotN=e?atol(e):-1; path=getenv("REVA_SHOT_PATH"); if(!path) path="lighthouse.ppm";
        e=getenv("REVA_SHOTN"); burst=e?atoi(e):1; e=getenv("REVA_SHOTEVERY"); every=e?atoi(e):1; if(burst<1)burst=1; if(every<1)every=1; }
    ++fno;
    if(shotN<0||fno<shotN||((fno-shotN)%every)||taken>=burst) return;
    int W=LH.W,H=LH.H; unsigned char*px=(unsigned char*)malloc((size_t)W*H*4);
    while(glGetError()!=GL_NO_ERROR){}
    glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,px);
    char pth[512];
    if(burst>1) snprintf(pth,sizeof pth,"%.*s_%05d.ppm",(int)(strrchr(path,46)?strrchr(path,46)-path:(long)strlen(path)),path,taken);
    else snprintf(pth,sizeof pth,"%s",path);
    FILE*sf=fopen(pth,"wb");
    if(sf){ fprintf(sf,"P6\n%d %d\n255\n",W,H); unsigned char*rw=(unsigned char*)malloc((size_t)W*3);
        for(int y=H-1;y>=0;y--){ for(int x=0;x<W;x++){ rw[x*3]=px[((size_t)y*W+x)*4]; rw[x*3+1]=px[((size_t)y*W+x)*4+1]; rw[x*3+2]=px[((size_t)y*W+x)*4+2]; }
            fwrite(rw,1,(size_t)W*3,sf); }
        free(rw); fclose(sf); printf("shot: %s %dx%d frame %ld\n",pth,W,H,fno); }
    else printf("shot FAILED to open %s (a native exe needs a Windows path)\n",pth);
    free(px); fflush(stdout); if(++taken>=burst) exit(0); }

/* ---- THE MENU ---------------------------------------------------------------------- */
static void revaLighthouse(SDL_Window*win,int W,int H){
    if(!LH.ok) lhInit(W,H);
    printf("R.E.V.A.: the last lighthouse\n"); fflush(stdout);
    static const char*ITEM[4]={"RESUME GAME","START NEW GAME","OPTIONS","EXIT GAME"};
    int canResume=0;                         /* nothing to resume until the game can save */
    int sel=1; { const char*e=getenv("REVA_LHSEL"); if(e) sel=atoi(e)&3; }
    long autoStart=-1; { const char*e=getenv("REVA_LHSTART"); if(e) autoStart=atol(e); }
    int lensOn=!(getenv("REVA_LHNOLENS")&&getenv("REVA_LHNOLENS")[0]=='1');
    float tOff=0.f; { const char*e=getenv("REVA_LHT"); if(e) tOff=(float)atof(e); }
    int vol=(int)saveGetI("reva.volume",8); if(vol<0)vol=0; if(vol>10)vol=10; sfxSetMaster(vol/10.f);
    int state=0;            /* 0 menu, 1 options, 2 starting, 3 leaving */
    int oSel=0; float stT=-1.f, lvT=-1.f, menuA=0.f;
    int dyP=0, dxP=0, aP=1, bP=1, stP=1; long fno=0;
    float lastC[3]={0,0,0}, lastC2[3]={0,0,0};
    double t0=now_raw_ms(), tPrev=t0; float camE[3]={0,0,0}, camC[3]={0,0,0}, pushE[3]={0}, pushFrom[3]={0};
    while(g_run){
        clockTick(); fno++;
        double tn=now_raw_ms(); float dt=(float)((tn-tPrev)/1000.0); tPrev=tn; if(dt>0.1f)dt=0.1f;
        { static double fpsT=-1; static long fpsN=0; if(fpsT<0) fpsT=tn; fpsN++;
          if(tn-fpsT>=5000.0){ printf("[lighthouse] %.1f fps\n",fpsN*1000.0/(tn-fpsT)); fflush(stdout); fpsT=tn; fpsN=0; } }
        float tl=(float)((tn-t0)/1000.0), t=tl+tOff;
        jsPoll(tl); kbPoll(tl); SDL_SetRelativeMouseMode(SDL_FALSE);
        /* ---- input ---- */
        int up=0,dn=0,lf=0,rt=0,ok=0,back=0;
        { int dy=(g_ly>0.55f)?1:(g_ly<-0.55f)?-1:0, dx=(g_lx>0.55f)?-1:(g_lx<-0.55f)?1:0;
          if(dy&&!dyP){ if(dy<0)up=1; else dn=1; } if(dx&&!dxP){ if(dx<0)lf=1; else rt=1; } dyP=dy; dxP=dx;
          if(g_btnA&&!aP) ok=1; if(g_btnStart&&!stP) ok=1; if(g_btnB&&!bP) back=1; aP=g_btnA; stP=g_btnStart; bP=g_btnB; }
        { SDL_Event ev; while(SDL_PollEvent(&ev)){
            if(ev.type==SDL_QUIT) exit(0);
            if(ev.type==SDL_KEYDOWN){ SDL_Keycode k=ev.key.keysym.sym;
                if(k==SDLK_UP) up=1; if(k==SDLK_DOWN) dn=1; if(k==SDLK_LEFT) lf=1; if(k==SDLK_RIGHT) rt=1;
                if(k==SDLK_RETURN||k==SDLK_KP_ENTER) ok=1; if(k==SDLK_ESCAPE||k==SDLK_BACKSPACE) back=1; } } }
        g_btnStart=0; g_startEdge=0;
        if(autoStart>=0&&fno==autoStart&&state==0){ ok=1; menuA=1.f; }   /* presses A on whatever REVA_LHSEL chose */
        if(state==0&&menuA>0.5f){
            if(up||dn){ int s=sel; do{ s=(s+(dn?1:3))&3; }while(s==0&&!canResume);
                if(s!=sel){ sel=s; sfxPlaySlot(LH.sMove,0.5f,NULL,0); } }
            if(ok){ if(sel==1){ state=2; stT=t; memcpy(pushFrom,camE,12); sfxPlaySlot(LH.sOk,0.6f,NULL,0); }
                else if(sel==2){ state=1; oSel=0; sfxPlaySlot(LH.sMove,0.5f,NULL,0); }
                else if(sel==3){ state=3; lvT=t; } }
            if(back){ state=3; lvT=t; } }
        else if(state==1){
            if(up||dn){ oSel^=1; sfxPlaySlot(LH.sMove,0.5f,NULL,0); }
            if(oSel==0&&(lf||rt)){ vol+=rt?1:-1; if(vol<0)vol=0; if(vol>10)vol=10; sfxSetMaster(vol/10.f);
                saveSetI("reva.volume",vol); sfxPlaySlot(LH.sMove,0.6f,NULL,0); }
            if(back||(ok&&oSel==1)){ state=0; sfxPlaySlot(LH.sMove,0.5f,NULL,0); } }
        else if(state==2){ if(back&&t-stT>1.f){ state=0; menuA=0.f; } }
        if(state==3&&t-lvT>1.6f) break;
        /* ---- the lens: a slow revolution round the system; START pushes in to the sphere ---- */
        { /* true scale: the hole is 400 R_star away, so the lens sweeps an arc on the swarm's far
           * side from it -- both in frame, the hole passing behind the swarm at the arc's middle */
          float hd=atan2f(LH_H[2],LH_H[0]), th=hd+3.14159265f+0.70f*sinf(t*6.2831853f/300.f+0.9f), R=260.f, hgt=34.f+14.f*sinf(t*0.021f);
          camE[0]=cosf(th)*R; camE[1]=hgt; camE[2]=sinf(th)*R;
          for(int j=0;j<3;j++) camC[j]=LH_H[j]*0.22f;
          if(state==2){ float k=lhSmooth(0.f,7.f,t-stT);
              float dir[3]={pushFrom[0],pushFrom[1],pushFrom[2]}; lhNorm(dir);
              for(int j=0;j<3;j++){ pushE[j]=dir[j]*LH_R*1.7f; camE[j]=pushFrom[j]+(pushE[j]-pushFrom[j])*k; camC[j]*=1.f-k; } } }
        float fovy=40.f; M4 proj,view,vp; m4_persp(proj,fovy,(float)W/H,1.0f,3000.f);
        m4_lookat(view,camE[0],camE[1],camE[2],camC[0],camC[1],camC[2]); m4_mul(vp,proj,view);
        float P[3][3],A[3][3],pf[3];
        lhPulsars(t,P,A);
        for(int k=0;k<3;k++){ float to[3]={camE[0]-P[k][0],camE[1]-P[k][1],camE[2]-P[k][2]}; lhNorm(to);
            float c=fabsf(lhDot(to,A[k])); pf[k]=powf(c,60.f);
            /* a pulse is the beam sweeping past us: tick at the peak of its approach */
            if(lastC[k]>lastC2[k]&&lastC[k]>=c&&fno>3){ float g=0.05f+0.30f*powf(lastC[k],10.f);
                sfxPlaySlot(LH.sTick[k],g,NULL,0); }
            lastC2[k]=lastC[k]; lastC[k]=c; }
        /* ---- sound: near-silence; the groan swells as the lens passes the hole ---- */
        { float dh=sqrtf((camE[0]-LH_H[0])*(camE[0]-LH_H[0])+(camE[1]-LH_H[1])*(camE[1]-LH_H[1])+(camE[2]-LH_H[2])*(camE[2]-LH_H[2]));
          float fi=lhSmooth(0.f,6.f,tl); lhVoiceGain(LH.vHum,0.13f*fi); lhVoiceGain(LH.vGroan,(0.04f+0.30f*lhSmooth(560.f,420.f,dh))*fi); }
        /* ---- draw ---- */
        float fade=lhSmooth(0.f,5.f,tl); if(state==3) fade*=1.f-lhSmooth(0.f,1.5f,t-lvT);
        if(state==2) fade*=1.f-0.55f*lhSmooth(5.5f,7.5f,t-stT);   /* the system dims as she boots */
        lhDrawScene(camE,vp,t,P,A);
        lhPost(camE,camC,fovy,vp,t,fade,P,A,pf,lensOn);
        /* ---- text ---- */
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        float target=(state==0||state==1)?lhSmooth(5.f,8.f,tl):0.f;
        menuA+=(target-menuA)*fminf(dt*3.f,1.f); if(state==0&&tl>8.f&&menuA>0.97f) menuA=1.f;
        float px=H*0.026f, x0=W*0.075f, y0=H*0.60f, gap=px*2.1f;
        if(menuA>0.01f){
            if(state==0) for(int i=0;i<4;i++){ float y=y0+i*gap; const char*s=ITEM[i];
                if(i==0&&!canResume){ lhText(0,x0,y,px,0.22f,s,0.8f,0.8f,0.85f,0.16f*menuA); continue; }
                if(i==sel){
                    /* her trace: a purple edge, and once in a while one glitch flicker */
                    float gl=fmodf(t,4.7f)<0.09f?1.f:0.f; float jx=gl*((fmodf(t*91.f,1.f)-0.5f)*10.f);
                    lhText(0,x0-px*0.9f,y,px,0.f,"|",0.72f,0.30f,1.0f,0.85f*menuA);
                    lhText(0,x0-1.6f+jx,y,px,0.22f,s,0.65f,0.25f,1.0f,0.55f*menuA);
                    lhText(0,x0+1.6f-jx,y,px,0.22f,s,0.65f,0.25f,1.0f,0.35f*menuA);
                    if(gl>0.f) lhText(0,x0+4.f,y,px,0.22f,s,0.2f,0.9f,1.0f,0.35f*menuA);
                    lhText(0,x0+jx*0.5f,y,px,0.22f,s,0.96f,0.95f,1.0f,0.95f*menuA); }
                else lhText(0,x0,y,px,0.22f,s,0.85f,0.86f,0.92f,0.42f*menuA); }
            else { char ln[64]; snprintf(ln,sizeof ln,"MASTER VOLUME   < %d >",vol);
                const char*o[2]={ln,"BACK"};
                for(int i=0;i<2;i++){ float y=y0+i*gap;
                    if(i==oSel){ lhText(0,x0-px*0.9f,y,px,0.f,"|",0.72f,0.30f,1.0f,0.85f*menuA);
                        lhText(0,x0-1.6f,y,px,0.22f,o[i],0.65f,0.25f,1.0f,0.5f*menuA);
                        lhText(0,x0,y,px,0.22f,o[i],0.96f,0.95f,1.0f,0.95f*menuA); }
                    else lhText(0,x0,y,px,0.22f,o[i],0.85f,0.86f,0.92f,0.42f*menuA); } } }
        if(state==2){
            /* "Initializing R.E.V.A." types itself once the lens has arrived */
            static const char*L="Initializing R.E.V.A."; int nL=(int)strlen(L);
            float ts=t-stT-6.5f; int nc=ts<0?0:(int)(ts*11.f); if(nc>nL) nc=nL;
            static int lastNc=0; if(nc!=lastNc){ if(nc>lastNc&&nc<=nL&&L[nc-1]!=' ') sfxPlaySlot(LH.sKey,0.35f,NULL,0); lastNc=nc; }
            if(ts>-0.5f){ char buf[64]; snprintf(buf,sizeof buf,"%.*s",nc,L);
                float tp=H*0.024f, w=lhTextW(1,tp,0.02f,L);
                float xx=(W-w)*0.5f, yy=H*0.80f;
                /* a soft dark band behind her line: the cage behind it is the brightest thing on screen */
                { float ba=0.72f*lhSmooth(-0.5f,0.5f,ts), y0b=yy-tp*1.9f, y1b=yy+tp*1.1f;
                  for(int k=0;k<6;k++){ float e=k*tp*0.35f; lhRect(0.f,y0b-e,(float)W,y1b+e,0.f,0.f,0.f,ba*0.28f); } }
                lhText(1,xx,yy,tp,0.02f,buf,0.92f,0.90f,1.0f,0.92f);
                if(fmodf(t,1.0f)<0.55f){ float cw=lhTextW(1,tp,0.02f,buf);
                    lhText(1,xx+cw+tp*0.1f,yy,tp,0.f,"_",0.72f,0.30f,1.0f,0.9f); } } }
        glDisable(GL_BLEND);
        lhShot();
        SDL_GL_SwapWindow(win);
    }
    glBindFramebuffer(GL_FRAMEBUFFER,0); glViewport(0,0,W,H); glEnable(GL_DEPTH_TEST);
    lhVoiceGain(LH.vHum,0.f); lhVoiceGain(LH.vGroan,0.f);
    printf("R.E.V.A.: back to the doors\n"); fflush(stdout); }
