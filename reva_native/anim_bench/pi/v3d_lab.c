/* v3d_lab.c -- what one frame can afford on the Pi 4's GPU, by itself, with nothing of the engine in the way.
 * Vertices, draws, bone uploads, instancing and pixels, each measured as GL work + glFinish per frame, on real scanout.
 * Build: gcc -O2 -o v3d_lab v3d_lab.c -I/usr/include/libdrm -ldrm -lgbm -lEGL -lGLESv2 -lm
 * Run:   sudo ./v3d_lab 480i | 240p      (console stopped; both modes are in spec for an NTSC set; picture stays dim) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

static int fd; static uint32_t crtc,connId; static drmModeModeInfo mode; static struct gbm_surface*gs; static EGLDisplay dpy; static EGLSurface surf;
static int W,H,first=1,flipPending; static struct gbm_bo*prevBo,*prev2Bo; static uint32_t prevFb,prev2Fb;
static double nowMs(clockid_t c){ struct timespec t; clock_gettime(c,&t); return t.tv_sec*1000.0+t.tv_nsec/1e6; }
static void onFlip(int f,unsigned a,unsigned b,unsigned c,void*d){ (void)f;(void)a;(void)b;(void)c;(void)d; flipPending=0; }
static drmEventContext ev;
static void die(const char*m){ fprintf(stderr,"FATAL %s\n",m); exit(1); }

static void present(void){
    eglSwapBuffers(dpy,surf);
    struct gbm_bo*bo=gbm_surface_lock_front_buffer(gs); uint32_t fb;
    drmModeAddFB(fd,W,H,24,32,gbm_bo_get_stride(bo),gbm_bo_get_handle(bo).u32,&fb);
    if(first){ if(drmModeSetCrtc(fd,crtc,fb,0,0,&connId,1,&mode)) die("setcrtc"); first=0; }
    else { while(flipPending){ fd_set s; FD_ZERO(&s); FD_SET(fd,&s); struct timeval tv={1,0}; if(select(fd+1,&s,NULL,NULL,&tv)>0) drmHandleEvent(fd,&ev); else break; }
        if(prev2Bo){ drmModeRmFB(fd,prev2Fb); gbm_surface_release_buffer(gs,prev2Bo); prev2Bo=NULL; }
        flipPending=1; if(drmModePageFlip(fd,crtc,fb,DRM_MODE_PAGE_FLIP_EVENT,NULL)) flipPending=0; }
    prev2Bo=prevBo; prev2Fb=prevFb; prevBo=bo; prevFb=fb;
}
static void initDisplay(int want240){
    fd=open("/dev/dri/card1",O_RDWR|O_CLOEXEC); if(fd<0) die("open card1");
    drmModeRes*res=drmModeGetResources(fd); if(!res) die("res"); drmModeConnector*c=NULL;
    for(int i=0;i<res->count_connectors;i++){ drmModeConnector*k=drmModeGetConnector(fd,res->connectors[i]); if(k&&k->connector_type==DRM_MODE_CONNECTOR_Composite){ c=k; break; } if(k) drmModeFreeConnector(k); }
    if(!c) die("no composite");
    int got=0; for(int i=0;i<c->count_modes;i++){ drmModeModeInfo*m=&c->modes[i]; int il=(m->flags&DRM_MODE_FLAG_INTERLACE)!=0;
        if(m->hdisplay==720&&((want240&&m->vdisplay==240&&!il)||(!want240&&m->vdisplay==480&&il))){ mode=*m; got=1; } }
    if(!got) die("mode"); W=mode.hdisplay; H=mode.vdisplay; connId=c->connector_id;
    if(c->encoder_id){ drmModeEncoder*e=drmModeGetEncoder(fd,c->encoder_id); if(e){ crtc=e->crtc_id; drmModeFreeEncoder(e);} }
    if(!crtc){ for(int i=0;i<c->count_encoders&&!crtc;i++){ drmModeEncoder*e=drmModeGetEncoder(fd,c->encoders[i]); if(!e) continue; for(int j=0;j<res->count_crtcs;j++) if(e->possible_crtcs&(1u<<j)){ crtc=res->crtcs[j]; break; } drmModeFreeEncoder(e); } }
    if(!crtc) die("crtc");
    memset(&ev,0,sizeof ev); ev.version=2; ev.page_flip_handler=onFlip;
    struct gbm_device*g=gbm_create_device(fd); gs=gbm_surface_create(g,W,H,GBM_FORMAT_XRGB8888,GBM_BO_USE_SCANOUT|GBM_BO_USE_RENDERING); if(!gs) die("gbm surface");
    dpy=eglGetDisplay((EGLNativeDisplayType)g); eglInitialize(dpy,NULL,NULL); eglBindAPI(EGL_OPENGL_ES_API);
    EGLint at[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,0,EGL_DEPTH_SIZE,24,EGL_RENDERABLE_TYPE,0x40,EGL_NONE};
    EGLConfig cf[64],pick=0; EGLint n=0; eglChooseConfig(dpy,at,cf,64,&n);
    for(int i=0;i<n;i++){ EGLint v=0; eglGetConfigAttrib(dpy,cf[i],EGL_NATIVE_VISUAL_ID,&v); if((uint32_t)v==GBM_FORMAT_XRGB8888){ pick=cf[i]; break; } }
    if(!pick) die("config");
    EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE}; EGLContext ctx=eglCreateContext(dpy,pick,EGL_NO_CONTEXT,ca); if(!ctx) die("ctx");
    surf=eglCreateWindowSurface(dpy,pick,(EGLNativeWindowType)gs,NULL); eglMakeCurrent(dpy,surf,surf,ctx); eglSwapInterval(dpy,0);
    EGLint db=0; eglGetConfigAttrib(dpy,pick,EGL_DEPTH_SIZE,&db);
    printf("[lab] %s %dx%d, %s, depth bits %d, %s\n",want240?"240p":"480i",W,H,glGetString(GL_RENDERER),db,glGetString(GL_VERSION));
}

/* ---- shaders ---- */
static GLuint prog(const char*vs,const char*fs){
    GLuint p=glCreateProgram(); const char*src[2]={vs,fs}; GLenum ty[2]={GL_VERTEX_SHADER,GL_FRAGMENT_SHADER};
    for(int i=0;i<2;i++){ GLuint s=glCreateShader(ty[i]); glShaderSource(s,1,&src[i],NULL); glCompileShader(s); GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
        if(!ok){ char log[1024]; glGetShaderInfoLog(s,1024,NULL,log); fprintf(stderr,"%s\n",log); die("compile"); } glAttachShader(p,s); }
    glBindAttribLocation(p,0,"aP"); glBindAttribLocation(p,1,"aN"); glBindAttribLocation(p,2,"aI"); glBindAttribLocation(p,3,"aW");
    glLinkProgram(p); GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok); if(!ok){ char log[1024]; glGetProgramInfoLog(p,1024,NULL,log); fprintf(stderr,"%s\n",log); die("link"); }
    return p;
}
#define FS_LIT "#version 300 es\nprecision mediump float; in vec3 vN; out vec4 o; uniform vec3 uC; void main(){ o=vec4(uC*(0.4+0.6*max(dot(normalize(vN),vec3(0.3,0.8,0.5)),0.0)),1.0); }\n"
static const char*VS_STATIC="#version 300 es\nlayout(location=0) in vec3 aP; layout(location=1) in vec3 aN; uniform vec4 uXf; out vec3 vN; void main(){ vN=aN; gl_Position=vec4(aP.xy*uXf.z+uXf.xy,aP.z*0.1+0.5,1.0); }\n";
/* four weights from a palette of 3-vec4 bones, the common form */
static const char*VS_SKIN="#version 300 es\nlayout(location=0) in vec3 aP; layout(location=1) in vec3 aN; layout(location=2) in vec4 aI; layout(location=3) in vec4 aW;\n"
    "uniform vec4 uXf; uniform vec4 uB[96]; out vec3 vN;\n"
    "void main(){ ivec4 i=ivec4(aI)*3; vec4 a=uB[i.x]*aW.x+uB[i.y]*aW.y+uB[i.z]*aW.z+uB[i.w]*aW.w; vec4 b=uB[i.x+1]*aW.x+uB[i.y+1]*aW.y+uB[i.z+1]*aW.z+uB[i.w+1]*aW.w; vec4 c=uB[i.x+2]*aW.x+uB[i.y+2]*aW.y+uB[i.z+2]*aW.z+uB[i.w+2]*aW.w;\n"
    " vec4 p=vec4(aP,1.0); vec3 q=vec3(dot(a,p),dot(b,p),dot(c,p)); vN=vec3(dot(a.xyz,aN),dot(b.xyz,aN),dot(c.xyz,aN)); gl_Position=vec4(q.xy*uXf.z+uXf.xy,q.z*0.1+0.5,1.0); }\n";
/* many bodies in one draw: eight shared poses of 32 bones, each instance picks one */
static const char*VS_INST="#version 300 es\nlayout(location=0) in vec3 aP; layout(location=1) in vec3 aN; layout(location=2) in vec4 aI; layout(location=3) in vec4 aW;\n"
    "uniform vec4 uB[768]; uniform vec4 uInst[400]; out vec3 vN;\n"
    "void main(){ vec4 X=uInst[gl_InstanceID]; int base=int(X.w)*96; ivec4 i=ivec4(aI)*3+base; vec4 a=uB[i.x]*aW.x+uB[i.y]*aW.y+uB[i.z]*aW.z+uB[i.w]*aW.w; vec4 b=uB[i.x+1]*aW.x+uB[i.y+1]*aW.y+uB[i.z+1]*aW.z+uB[i.w+1]*aW.w; vec4 c=uB[i.x+2]*aW.x+uB[i.y+2]*aW.y+uB[i.z+2]*aW.z+uB[i.w+2]*aW.w;\n"
    " vec4 p=vec4(aP,1.0); vec3 q=vec3(dot(a,p),dot(b,p),dot(c,p)); vN=vec3(dot(a.xyz,aN),dot(b.xyz,aN),dot(c.xyz,aN)); gl_Position=vec4(q.xy*X.z+X.xy,q.z*0.1+0.5,1.0); }\n";
static const char*VS_QUAD="#version 300 es\nout vec2 vT; void main(){ vec2 p=vec2(float((gl_VertexID&1)*2-1),float((gl_VertexID&2)-1)); vT=p*0.5+0.5; gl_Position=vec4(p,0.0,1.0); }\n";
static const char*FS_TEX="#version 300 es\nprecision mediump float; in vec2 vT; out vec4 o; uniform sampler2D uT; uniform float uA; void main(){ o=vec4(texture(uT,vT).rgb*0.25,uA); }\n";

/* ---- meshes: an indexed grid, a soft bump so normals vary, bones along y ---- */
typedef struct { GLuint vao; int nIdx; } Mesh;
typedef struct { float p[3],n[3]; unsigned char i[4],w[4]; } V;
static Mesh grid(int G){
    int nv=(G+1)*(G+1), ni=G*G*6; V*v=malloc(sizeof(V)*nv); unsigned*ix=malloc(4*ni);
    for(int y=0;y<=G;y++) for(int x=0;x<=G;x++){ V*q=&v[y*(G+1)+x]; float u=(float)x/G*2-1,t=(float)y/G*2-1; float z=0.5f*(1-u*u)*(1-t*t);
        q->p[0]=u; q->p[1]=t; q->p[2]=z; float len=sqrtf(u*u+t*t+1); q->n[0]=u/len; q->n[1]=t/len; q->n[2]=1/len;
        int b=(int)((t*0.5f+0.5f)*30.99f); q->i[0]=b; q->i[1]=b+1; q->i[2]=(b+2)%32; q->i[3]=(b+3)%32; q->w[0]=128; q->w[1]=64; q->w[2]=40; q->w[3]=23; }
    int k=0; for(int y=0;y<G;y++) for(int x=0;x<G;x++){ unsigned a=y*(G+1)+x,b=a+1,c=a+G+1,d=c+1; ix[k++]=a; ix[k++]=b; ix[k++]=c; ix[k++]=b; ix[k++]=d; ix[k++]=c; }
    Mesh m; m.nIdx=ni; glGenVertexArrays(1,&m.vao); glBindVertexArray(m.vao); GLuint vb,ib; glGenBuffers(1,&vb); glGenBuffers(1,&ib);
    glBindBuffer(GL_ARRAY_BUFFER,vb); glBufferData(GL_ARRAY_BUFFER,sizeof(V)*nv,v,GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ib); glBufferData(GL_ELEMENT_ARRAY_BUFFER,4*ni,ix,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,0,sizeof(V),(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,0,sizeof(V),(void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,0,sizeof(V),(void*)24);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3,4,GL_UNSIGNED_BYTE,1,sizeof(V),(void*)28);
    free(v); free(ix); return m;
}
static GLuint pStatic,pSkin,pInst,pTex; static GLint uXfS,uCS,uXfK,uCK,uBK,uBI,uInstI,uCI,uTT,uAT;
static float inst[1600];
static float boneV[768*4];
static void makeBones(float t){ for(int p=0;p<8;p++) for(int b=0;b<32;b++){ float a=0.08f*sinf(t+b*0.3f+p),s=sinf(a),co=cosf(a); float*r=&boneV[(p*96+b*3)*4];
        r[0]=co; r[1]=-s; r[2]=0; r[3]=0; r[4]=s; r[5]=co; r[6]=0; r[7]=0; r[8]=0; r[9]=0; r[10]=1; r[11]=0; } }

/* ---- the frame loop ---- */
typedef void(*Fn)(int,Mesh*,float);
static int cmpd(const void*a,const void*b){ double x=*(double*)a,y=*(double*)b; return x<y?-1:x>y; }
static void run(const char*name,int param,Mesh*m,Fn fn,int frames){
    static double w[400],c[400]; int n=0; float t=0;
    for(int i=0;i<frames+8&&i<400;i++){ double t0=nowMs(CLOCK_MONOTONIC),c0=nowMs(CLOCK_THREAD_CPUTIME_ID);
        glViewport(0,0,W,H); glClearColor(0.02f,0.02f,0.04f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        t+=1.0f/60; fn(param,m,t); glFinish();
        double dw=nowMs(CLOCK_MONOTONIC)-t0,dc=nowMs(CLOCK_THREAD_CPUTIME_ID)-c0; present();
        if(i>=8){ w[n]=dw; c[n]=dc; n++; }
        if(dw>250.0&&i>=12) break; }
    qsort(w,n,sizeof(double),cmpd); qsort(c,n,sizeof(double),cmpd);
    printf("%-34s %6d : work %7.2f ms median, %7.2f p95, %7.2f max | cpu %6.2f ms | over 16.6 ms in %d of %d\n",name,param,w[n/2],w[n*95/100],w[n-1],c[n/2],({int o=0; for(int k=0;k<n;k++) if(w[k]>16.6) o++; o;}),n); fflush(stdout);
}
static void dSingleStatic(int p,Mesh*m,float t){ (void)p;(void)t; glUseProgram(pStatic); glEnable(GL_DEPTH_TEST); glUniform3f(uCS,0.2f,0.25f,0.3f); glUniform4f(uXfS,0,0,0.5f,0); glBindVertexArray(m->vao); glDrawElements(GL_TRIANGLES,m->nIdx,GL_UNSIGNED_INT,0); }
static void dSingleSkin(int p,Mesh*m,float t){ (void)p; makeBones(t); glUseProgram(pSkin); glEnable(GL_DEPTH_TEST); glUniform3f(uCK,0.2f,0.25f,0.3f); glUniform4f(uXfK,0,0,0.5f,0); glUniform4fv(uBK,96,boneV); glBindVertexArray(m->vao); glDrawElements(GL_TRIANGLES,m->nIdx,GL_UNSIGNED_INT,0); }
static void place(int i,int N,float*x,float*y,float*s){ int side=(int)ceilf(sqrtf((float)N)); float cell=2.0f/side; *x=-1+cell*(i%side+0.5f); *y=-1+cell*(i/side+0.5f); *s=cell*0.45f; }
static void dManyStatic(int N,Mesh*m,float t){ (void)t; glUseProgram(pStatic); glEnable(GL_DEPTH_TEST); glUniform3f(uCS,0.2f,0.25f,0.3f); glBindVertexArray(m->vao);
    for(int i=0;i<N;i++){ float x,y,s; place(i,N,&x,&y,&s); glUniform4f(uXfS,x,y,s,0); glDrawElements(GL_TRIANGLES,m->nIdx,GL_UNSIGNED_INT,0); } }
static void dManySkin(int N,Mesh*m,float t){ makeBones(t); glUseProgram(pSkin); glEnable(GL_DEPTH_TEST); glUniform3f(uCK,0.2f,0.25f,0.3f); glBindVertexArray(m->vao);
    for(int i=0;i<N;i++){ float x,y,s; place(i,N,&x,&y,&s); glUniform4fv(uBK,96,boneV+(i%8)*96*4); glUniform4f(uXfK,x,y,s,0); glDrawElements(GL_TRIANGLES,m->nIdx,GL_UNSIGNED_INT,0); } }
static void dInst(int N,Mesh*m,float t){ makeBones(t); glUseProgram(pInst); glEnable(GL_DEPTH_TEST); glUniform3f(uCI,0.2f,0.25f,0.3f); glUniform4fv(uBI,768,boneV);
    for(int i=0;i<N;i++){ float x,y,s; place(i,N,&x,&y,&s); inst[i*4]=x; inst[i*4+1]=y; inst[i*4+2]=s; inst[i*4+3]=(float)(i%8); }
    glUniform4fv(uInstI,N,inst); glBindVertexArray(m->vao); glDrawElementsInstanced(GL_TRIANGLES,m->nIdx,GL_UNSIGNED_INT,0,N); }
static GLuint tex;
static void dFill(int L,Mesh*m,float t){ (void)m;(void)t; glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glUseProgram(pTex); glUniform1f(uAT,1.0f); glBindTexture(GL_TEXTURE_2D,tex); static GLuint va; if(!va) glGenVertexArrays(1,&va); glBindVertexArray(va); for(int i=0;i<L;i++) glDrawArrays(GL_TRIANGLE_STRIP,0,4); }
static void dBlend(int L,Mesh*m,float t){ (void)m;(void)t; glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); glUseProgram(pTex); glUniform1f(uAT,0.3f); glBindTexture(GL_TEXTURE_2D,tex); static GLuint va; if(!va) glGenVertexArrays(1,&va); glBindVertexArray(va); for(int i=0;i<L;i++) glDrawArrays(GL_TRIANGLE_STRIP,0,4); glDisable(GL_BLEND); }
/* the engine's pi rung in miniature: 3D into a half-size target, then the target upsampled to the screen */
static GLuint rt,rtTex,rtDepth; static int rtW,rtH;
static void dPost(int mesh,Mesh*m,float t){ (void)mesh; if(!rt){ rtW=W/2; rtH=H/2; glGenTextures(1,&rtTex); glBindTexture(GL_TEXTURE_2D,rtTex); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,rtW,rtH,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glGenRenderbuffers(1,&rtDepth); glBindRenderbuffer(GL_RENDERBUFFER,rtDepth); glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT16,rtW,rtH); glGenFramebuffers(1,&rt); glBindFramebuffer(GL_FRAMEBUFFER,rt); glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,rtTex,0); glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,rtDepth); }
    glBindFramebuffer(GL_FRAMEBUFFER,rt); glViewport(0,0,rtW,rtH); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT); dSingleStatic(0,m,t);
    glBindFramebuffer(GL_FRAMEBUFFER,0); glViewport(0,0,W,H); glDisable(GL_DEPTH_TEST); dFill(0,m,t); glBindTexture(GL_TEXTURE_2D,rtTex); glDrawArrays(GL_TRIANGLE_STRIP,0,4); }

int main(int argc,char**argv){
    int want240=(argc>1&&!strcmp(argv[1],"240p")); initDisplay(want240);
    pStatic=prog(VS_STATIC,FS_LIT); uXfS=glGetUniformLocation(pStatic,"uXf"); uCS=glGetUniformLocation(pStatic,"uC");
    pSkin=prog(VS_SKIN,FS_LIT); uXfK=glGetUniformLocation(pSkin,"uXf"); uCK=glGetUniformLocation(pSkin,"uC"); uBK=glGetUniformLocation(pSkin,"uB");
    pInst=prog(VS_INST,FS_LIT); uBI=glGetUniformLocation(pInst,"uB"); uInstI=glGetUniformLocation(pInst,"uInst"); uCI=glGetUniformLocation(pInst,"uC");
    pTex=prog(VS_QUAD,FS_TEX); uTT=glGetUniformLocation(pTex,"uT"); uAT=glGetUniformLocation(pTex,"uA");
    glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex); static unsigned char px[256*256*4]; for(int i=0;i<256*256;i++){ px[i*4]=(i*7)&255; px[i*4+1]=(i*13)&255; px[i*4+2]=(i*29)&255; px[i*4+3]=255; }
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,256,256,0,GL_RGBA,GL_UNSIGNED_BYTE,px); glGenerateMipmap(GL_TEXTURE_2D); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    Mesh body288=grid(12), body800=grid(20), m100k=grid(129), m200k=grid(183), m400k=grid(258), m800k=grid(365);
    int F=60;
    printf("[lab] 'indices' = indices drawn per frame, 3 per triangle\n");
    run("empty frame",0,&body288,dFill,F);
    run("one mesh, no skin, indices",100000,&m100k,dSingleStatic,F); run("one mesh, no skin, indices",200000,&m200k,dSingleStatic,F); run("one mesh, no skin, indices",400000,&m400k,dSingleStatic,F); run("one mesh, no skin, indices",800000,&m800k,dSingleStatic,F);
    run("one mesh, 4-bone skin, indices",100000,&m100k,dSingleSkin,F); run("one mesh, 4-bone skin, indices",200000,&m200k,dSingleSkin,F); run("one mesh, 4-bone skin, indices",400000,&m400k,dSingleSkin,F); run("one mesh, 4-bone skin, indices",800000,&m800k,dSingleSkin,F);
    int D[]={50,100,200,400,800}; for(int i=0;i<5;i++) run("draws, no skin, 288 tris each",D[i],&body288,dManyStatic,F);
    int B[]={50,100,200,400}; for(int i=0;i<4;i++) run("bodies, own 32 bones, 288 tris",B[i],&body288,dManySkin,F);
    for(int i=0;i<4;i++) run("bodies, own 32 bones, 800 tris",B[i],&body800,dManySkin,F);
    for(int i=0;i<4;i++) run("instanced, 8 poses, 288 tris",B[i],&body288,dInst,F);
    for(int i=0;i<4;i++) run("instanced, 8 poses, 800 tris",B[i],&body800,dInst,F);
    int L[]={1,2,4,8}; for(int i=0;i<4;i++) run("full-screen textured layers",L[i],&body288,dFill,F);
    for(int i=0;i<3;i++) run("full-screen blended layers",L[i],&body288,dBlend,F);
    run("half-size target + upsample, 100k",0,&m100k,dPost,F);
    printf("[done]\n");
    return 0;
}
