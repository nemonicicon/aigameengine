/* v3d_lab2.c -- what a frame can afford on the Pi 4's GPU through OpenGL ES: the first lab's synthetic tests, a real street citizen with
 * its real texture, shading in three strengths, a shadow pass, the whole "proof frame" of the engine plan, and four-way split screen.
 * vk_lab.c draws the same work through Vulkan.
 *   A: texture * (sky + sun)                    the budget shader
 *   B: A + a four-tap shadow lookup
 *   C: B + six point lights per pixel + normalize + alpha discard   what today's engine shader does
 * Build: gcc -O2 -std=gnu17 -o v3d_lab2 v3d_lab2.c -I/usr/include/libdrm -ldrm -lgbm -lEGL -lGLESv2 -lm
 * Run:   sudo ./v3d_lab2 480i|240p citizen.lab     (console stopped; in-spec modes; the picture stays dim and changes every second) */
#define _GNU_SOURCE
#include "lab_common.h"
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
static void onFlip(int f,unsigned a,unsigned b,unsigned c,void*d){ (void)f;(void)a;(void)d; labFlipMark(b,c); flipPending=0; }
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
    if(!got) die("mode"); labFieldMs=(double)mode.htotal*mode.vtotal/mode.clock/((mode.flags&DRM_MODE_FLAG_INTERLACE)?2.0:1.0); W=mode.hdisplay; H=mode.vdisplay; connId=c->connector_id;
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
    printf("[lab] GL %s %dx%d, %s, %s\n",want240?"240p":"480i",W,H,glGetString(GL_RENDERER),glGetString(GL_VERSION));
}

/* ---- shaders ---- */
static GLuint prog(const char*vs,const char*fs){
    GLuint p=glCreateProgram(); const char*src[2]={vs,fs}; GLenum ty[2]={GL_VERTEX_SHADER,GL_FRAGMENT_SHADER};
    for(int i=0;i<2;i++){ GLuint s=glCreateShader(ty[i]); glShaderSource(s,1,&src[i],NULL); glCompileShader(s); GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
        if(!ok){ char log[2048]; glGetShaderInfoLog(s,2048,NULL,log); fprintf(stderr,"%s\n%s\n",src[i],log); die("compile"); } glAttachShader(p,s); }
    glLinkProgram(p); GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok); if(!ok){ char log[2048]; glGetProgramInfoLog(p,2048,NULL,log); fprintf(stderr,"%s\n",log); die("link"); }
    return p;
}
#define VATT "layout(location=0) in vec3 aP; layout(location=1) in vec3 aN; layout(location=2) in vec2 aT; layout(location=3) in vec4 aI; layout(location=4) in vec4 aW;\n"
#define SKIN "ivec4 i=ivec4(aI)*3; vec4 a=uB[i.x]*aW.x+uB[i.y]*aW.y+uB[i.z]*aW.z+uB[i.w]*aW.w; vec4 b=uB[i.x+1]*aW.x+uB[i.y+1]*aW.y+uB[i.z+1]*aW.z+uB[i.w+1]*aW.w; vec4 c=uB[i.x+2]*aW.x+uB[i.y+2]*aW.y+uB[i.z+2]*aW.z+uB[i.w+2]*aW.w; vec4 p4=vec4(aP,1.0); vec3 q=vec3(dot(a,p4),dot(b,p4),dot(c,p4)); vec3 nn=vec3(dot(a.xyz,aN),dot(b.xyz,aN),dot(c.xyz,aN));\n"
static const char*VS_STATIC="#version 300 es\n" VATT "uniform vec4 uXf; out vec3 vN; out vec2 vT; out vec3 vP;\nvoid main(){ vN=aN; vT=aT; vec2 xy=aP.xy*uXf.z+uXf.xy; vP=vec3(xy,0.0); gl_Position=vec4(xy,aP.z*0.1+0.5+uXf.w,1.0); }\n";
static const char*VS_SKIN="#version 300 es\n" VATT "uniform vec4 uXf; uniform vec4 uB[96]; out vec3 vN; out vec2 vT; out vec3 vP;\nvoid main(){ " SKIN " vN=nn; vT=aT; vec2 xy=q.xy*uXf.z+uXf.xy; vP=vec3(xy,0.0); gl_Position=vec4(xy,q.z*0.1+0.5+uXf.w,1.0); }\n";
static const char*VS_INST="#version 300 es\n" VATT "uniform vec4 uB[768]; uniform vec4 uInst[400]; out vec3 vN; out vec2 vT; out vec3 vP;\nvoid main(){ vec4 X=uInst[gl_InstanceID]; int base=int(X.w)*96; ivec4 i=ivec4(aI)*3+base; vec4 a=uB[i.x]*aW.x+uB[i.y]*aW.y+uB[i.z]*aW.z+uB[i.w]*aW.w; vec4 b=uB[i.x+1]*aW.x+uB[i.y+1]*aW.y+uB[i.z+1]*aW.z+uB[i.w+1]*aW.w; vec4 c=uB[i.x+2]*aW.x+uB[i.y+2]*aW.y+uB[i.z+2]*aW.z+uB[i.w+2]*aW.w; vec4 p4=vec4(aP,1.0); vec3 q=vec3(dot(a,p4),dot(b,p4),dot(c,p4)); vec3 nn=vec3(dot(a.xyz,aN),dot(b.xyz,aN),dot(c.xyz,aN)); vN=nn; vT=aT; vec2 xy=q.xy*X.z+X.xy; vP=vec3(xy,0.0); gl_Position=vec4(xy,q.z*0.1+0.5,1.0); }\n";
static const char*VS_QUAD="#version 300 es\nout vec3 vN; out vec2 vT; out vec3 vP;\nvoid main(){ vec2 p=vec2(float((gl_VertexID&1)*2-1),float((gl_VertexID&2)-1)); vN=vec3(0.0,0.0,1.0); vT=p*2.0; vP=vec3(p,0.0); gl_Position=vec4(p,0.95,1.0); }\n";
#define FS_HEAD "#version 300 es\nprecision mediump float; in vec3 vN; in vec2 vT; in vec3 vP; out vec4 o; uniform sampler2D uT;\n"
#define FS_SUN "vec3 N=normalize(vN); vec3 tx=texture(uT,vT).rgb*0.5; float s=max(dot(N,vec3(0.3,0.8,0.5)),0.0);\n"
static const char*FS_LIT=FS_HEAD "void main(){ vec3 N=normalize(vN); o=vec4(vec3(0.25)*(0.4+0.6*max(dot(N,vec3(0.3,0.8,0.5)),0.0)),1.0); }\n";
static const char*FS_A=FS_HEAD "void main(){ " FS_SUN " o=vec4(tx*(0.35+0.65*s),1.0); }\n";
#define FS_SHADOW "uniform mediump sampler2DShadow uSh;\nfloat shade(vec3 P){ vec3 c=vec3(P.xy*0.5+0.5,0.6); float e=1.0/1024.0; return 0.25*(texture(uSh,c+vec3(-e,-e,0.0))+texture(uSh,c+vec3(e,-e,0.0))+texture(uSh,c+vec3(-e,e,0.0))+texture(uSh,c+vec3(e,e,0.0))); }\n"
static const char*FS_B=FS_HEAD FS_SHADOW "void main(){ " FS_SUN " o=vec4(tx*(0.35+0.65*s*shade(vP)),1.0); }\n";
static const char*FS_C=FS_HEAD FS_SHADOW "uniform vec3 uLp[6]; uniform vec3 uLc[6];\nvoid main(){ vec4 t4=texture(uT,vT); if(t4.a<0.02) discard; vec3 N=normalize(vN); vec3 tx=t4.rgb*0.5; float s=max(dot(N,vec3(0.3,0.8,0.5)),0.0); vec3 li=vec3(0.0); for(int i=0;i<6;i++){ vec3 D=uLp[i]-vP; float dd=dot(D,D); li+=uLc[i]*max(0.0,dot(N,D*inversesqrt(dd+0.001)))/(1.0+dd*0.2); } o=vec4(tx*(0.35+0.65*s*shade(vP)+li),1.0); }\n";

typedef struct { GLuint p; GLint xf,b,inst,t,sh,lp,lc; } Prog;
static Prog mk(const char*vs,const char*fs){ Prog r; r.p=prog(vs,fs); r.xf=glGetUniformLocation(r.p,"uXf"); r.b=glGetUniformLocation(r.p,"uB"); r.inst=glGetUniformLocation(r.p,"uInst"); r.t=glGetUniformLocation(r.p,"uT"); r.sh=glGetUniformLocation(r.p,"uSh"); r.lp=glGetUniformLocation(r.p,"uLp"); r.lc=glGetUniformLocation(r.p,"uLc");
    glUseProgram(r.p); if(r.t>=0) glUniform1i(r.t,0); if(r.sh>=0) glUniform1i(r.sh,1);
    static const float lp[18]={-0.5f,0.5f,0.4f, 0.5f,0.5f,0.4f, -0.5f,-0.5f,0.4f, 0.5f,-0.5f,0.4f, 0.f,0.f,0.5f, 0.f,0.8f,0.3f}; static const float lc[18]={0.1f,0.05f,0.02f, 0.02f,0.05f,0.1f, 0.05f,0.1f,0.02f, 0.1f,0.02f,0.05f, 0.05f,0.05f,0.05f, 0.02f,0.02f,0.1f};
    if(r.lp>=0) glUniform3fv(r.lp,6,lp); if(r.lc>=0) glUniform3fv(r.lc,6,lc); return r; }

typedef struct { GLuint vao; int ni; } GMesh;
static GMesh up(LMesh m){ GMesh g; g.ni=m.ni; glGenVertexArrays(1,&g.vao); glBindVertexArray(g.vao); GLuint vb,ib; glGenBuffers(1,&vb); glGenBuffers(1,&ib);
    glBindBuffer(GL_ARRAY_BUFFER,vb); glBufferData(GL_ARRAY_BUFFER,sizeof(LV)*m.nv,m.v,GL_STATIC_DRAW); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ib); glBufferData(GL_ELEMENT_ARRAY_BUFFER,4*m.ni,m.ix,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,0,sizeof(LV),(void*)0); glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,0,sizeof(LV),(void*)12); glEnableVertexAttribArray(2); glVertexAttribPointer(2,2,GL_FLOAT,0,sizeof(LV),(void*)24);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3,4,GL_UNSIGNED_BYTE,0,sizeof(LV),(void*)32); glEnableVertexAttribArray(4); glVertexAttribPointer(4,4,GL_UNSIGNED_BYTE,1,sizeof(LV),(void*)36); return g; }
static GLuint upTex(LTex t,float bright){ GLuint id; glGenTextures(1,&id); glBindTexture(GL_TEXTURE_2D,id); unsigned char*d=malloc((size_t)t.w*t.h*4); for(size_t k=0;k<(size_t)t.w*t.h*4;k++) d[k]=(k%4==3)?t.px[k]:(unsigned char)(t.px[k]*bright);
    int w=t.w,h=t.h,l=0; unsigned char*cur=d; for(;;){ glTexImage2D(GL_TEXTURE_2D,l,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,cur); int nw,nh; unsigned char*nx=labMipDown(cur,w,h,&nw,&nh); free(cur); if(!nx) break; cur=nx; w=nw; h=nh; l++; }
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT); return id; }

static Prog pStatic,pSkin,pInst,pA,pAs,pB,pBs,pC,pCs,qA,qB,qC,pDepthSkin,pDepthStatic; static GMesh body800,m100k,m800k,cit,hero,street; static GLuint tex[4],shTex,shFbo,qVao; static float gt;

static void skinDraw(Prog*p,GMesh*m,int pose,float x,float y,float s,float z){ glUniform4fv(p->b,96,labBoneV+pose*96*4); glUniform4f(p->xf,x,y,s,z); glBindVertexArray(m->vao); glDrawElements(GL_TRIANGLES,m->ni,GL_UNSIGNED_INT,0); }
static void statDraw(Prog*p,GMesh*m,float x,float y,float s,float z){ glUniform4f(p->xf,x,y,s,z); glBindVertexArray(m->vao); glDrawElements(GL_TRIANGLES,m->ni,GL_UNSIGNED_INT,0); }

/* the frame the plan budgets: 100 citizens, the hero, a street of 100 meshes with textures changing, the ground, two HUD layers */
static void citizens(Prog*ps,int views,int v){ glUseProgram(ps->p); glBindTexture(GL_TEXTURE_2D,tex[0]); (void)views;(void)v; for(int i=0;i<100;i++){ float x,y,s; labPlace(i,100,&x,&y,&s); skinDraw(ps,&cit,i%8,x,y,0.16f,0.f); } skinDraw(ps,&hero,0,0.f,0.f,0.16f,-0.1f); }
static void streetDraw(Prog*pst){ glUseProgram(pst->p); for(int i=0;i<100;i++){ float x,y,s; labPlace(i,100,&x,&y,&s); glBindTexture(GL_TEXTURE_2D,tex[i%4]); statDraw(pst,&street,x+0.05f,y+0.04f,s,0.15f); } }
static void shadowPass(void){ glBindFramebuffer(GL_FRAMEBUFFER,shFbo); glViewport(0,0,1024,1024); glClear(GL_DEPTH_BUFFER_BIT); glUseProgram(pDepthSkin.p); glEnable(GL_DEPTH_TEST);
    for(int i=0;i<100;i++){ float x,y,s; labPlace(i,100,&x,&y,&s); skinDraw(&pDepthSkin,&cit,i%8,x+0.03f,y-0.04f,0.16f,0.f); } skinDraw(&pDepthSkin,&hero,0,0.03f,-0.04f,0.16f,-0.1f);
    static const GLenum disc[1]={GL_DEPTH_ATTACHMENT}; glInvalidateFramebuffer(GL_FRAMEBUFFER,0,disc); glBindFramebuffer(GL_FRAMEBUFFER,0); glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,shTex); glActiveTexture(GL_TEXTURE0); }
static void groundAndHud(Prog*q){ glUseProgram(q->p); glBindTexture(GL_TEXTURE_2D,tex[1]); glBindVertexArray(qVao); glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    glDisable(GL_DEPTH_TEST); glUseProgram(pStatic.p); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); glUseProgram(qA.p); glBindTexture(GL_TEXTURE_2D,tex[2]); glDrawArrays(GL_TRIANGLE_STRIP,0,4); glDrawArrays(GL_TRIANGLE_STRIP,0,4); glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST); }

static void tEmpty(int a){ (void)a; }
static void tGrid100(int a){ (void)a; labBones(gt); glUseProgram(pSkin.p); glEnable(GL_DEPTH_TEST); skinDraw(&pSkin,&m100k,0,0,0,0.5f,0); }
static void tGrid800(int a){ (void)a; labBones(gt); glUseProgram(pSkin.p); glEnable(GL_DEPTH_TEST); skinDraw(&pSkin,&m800k,0,0,0,0.5f,0); }
static void tDraws(int N){ glUseProgram(pStatic.p); glEnable(GL_DEPTH_TEST); for(int i=0;i<N;i++){ float x,y,s; labPlace(i,N,&x,&y,&s); statDraw(&pStatic,&body800,x,y,s,0); } }
static void tBodies(int N){ labBones(gt); glUseProgram(pSkin.p); glEnable(GL_DEPTH_TEST); for(int i=0;i<N;i++){ float x,y,s; labPlace(i,N,&x,&y,&s); skinDraw(&pSkin,&body800,i%8,x,y,s,0); } }
static void tInst(int N){ labBones(gt); static float inst[1600]; glUseProgram(pInst.p); glEnable(GL_DEPTH_TEST); glUniform4fv(pInst.b,768,labBoneV); for(int i=0;i<N;i++){ float x,y,s; labPlace(i,N,&x,&y,&s); inst[i*4]=x; inst[i*4+1]=y; inst[i*4+2]=s; inst[i*4+3]=(float)(i%8); } glUniform4fv(pInst.inst,N,inst); glBindVertexArray(body800.vao); glDrawElementsInstanced(GL_TRIANGLES,body800.ni,GL_UNSIGNED_INT,0,N); }
static void tLayer(int k){ Prog*q=k==0?&qA:k==1?&qB:&qC; glDisable(GL_DEPTH_TEST); glUseProgram(q->p); glBindTexture(GL_TEXTURE_2D,tex[1]); glBindVertexArray(qVao); glDrawArrays(GL_TRIANGLE_STRIP,0,4); glEnable(GL_DEPTH_TEST); }
static void tCitizens(int k){ labBones(gt); glEnable(GL_DEPTH_TEST); citizens(k==0?&pA:k==1?&pB:&pC,1,0); }
static void tProof(int k){ labBones(gt); if(k>0) shadowPass(); glViewport(0,0,W,H); glEnable(GL_DEPTH_TEST); citizens(k==0?&pA:k==1?&pB:&pC,1,0); streetDraw(k==0?&pAs:k==1?&pBs:&pCs); groundAndHud(k==0?&qA:k==1?&qB:&qC); }
static void tSplit(int k){ labBones(gt); if(k>0) shadowPass(); glEnable(GL_DEPTH_TEST); for(int v=0;v<4;v++){ glViewport((v&1)*W/2,(v>>1)*H/2,W/2,H/2); citizens(k==0?&pA:&pB,4,v); streetDraw(k==0?&pAs:&pBs); } glViewport(0,0,W,H); groundAndHud(k==0?&qA:&qB); }

static void run(const char*name,void(*fn)(int),int arg){
    static double w[120],c[120]; int n=0;
    for(int i=0;i<68;i++){ if(i==8) labGpuSnap(0); double t0=labMs(CLOCK_MONOTONIC),c0=labMs(CLOCK_THREAD_CPUTIME_ID);
        glBindFramebuffer(GL_FRAMEBUFFER,0); glViewport(0,0,W,H); glClearColor(0.02f,0.02f,0.04f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT); gt+=1.0f/60;
        fn(arg); static const GLenum disc[1]={GL_DEPTH}; glInvalidateFramebuffer(GL_FRAMEBUFFER,1,disc); glFinish();
        double dw=labMs(CLOCK_MONOTONIC)-t0,dc=labMs(CLOCK_THREAD_CPUTIME_ID)-c0; if(i>=8){ w[n]=dw; c[n]=dc; n++; labGpuSnap(1); } present();
        if(dw>300.0&&i>=12) break; }
    labReport(name,w,c,n);
}
/* LIVE: as a game runs. No finish, one pipelined flip after another, and the kernel's timestamps say whether any frame was late */
static void live(const char*name,void(*fn)(int),int arg){
    for(int i=0;i<30;i++){ gt+=1.0f/60; glBindFramebuffer(GL_FRAMEBUFFER,0); glViewport(0,0,W,H); glClearColor(0.02f,0.02f,0.04f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT); fn(arg); present(); }
    labLiveStart(); double t0=labMs(CLOCK_MONOTONIC),cpu=0.0; int N=getenv("LAB_LIVEONLY")?atoi(getenv("LAB_LIVEONLY")):300;
    for(int i=0;i<N;i++){ double c0=labMs(CLOCK_THREAD_CPUTIME_ID); gt+=1.0f/60; glBindFramebuffer(GL_FRAMEBUFFER,0); glViewport(0,0,W,H); glClearColor(0.02f,0.02f,0.04f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT); fn(arg); static const GLenum disc[1]={GL_DEPTH}; glInvalidateFramebuffer(GL_FRAMEBUFFER,1,disc); present(); cpu+=labMs(CLOCK_THREAD_CPUTIME_ID)-c0; }
    labLiveReport(name,(labMs(CLOCK_MONOTONIC)-t0)/1000.0,cpu/N);
}
int main(int argc,char**argv){
    int want240=(argc>1&&!strcmp(argv[1],"240p")); initDisplay(want240);
    LMesh cm; LTex ct; if(argc<3||!labLoad(argv[2],&cm,&ct)) die("need citizen.lab");
    pStatic=mk(VS_STATIC,FS_LIT); pSkin=mk(VS_SKIN,FS_LIT); pInst=mk(VS_INST,FS_LIT);
    pA=mk(VS_SKIN,FS_A); pAs=mk(VS_STATIC,FS_A); pB=mk(VS_SKIN,FS_B); pBs=mk(VS_STATIC,FS_B); pC=mk(VS_SKIN,FS_C); pCs=mk(VS_STATIC,FS_C); qA=mk(VS_QUAD,FS_A); qB=mk(VS_QUAD,FS_B); qC=mk(VS_QUAD,FS_C);
    static const char*FS_NONE="#version 300 es\nprecision mediump float; in vec3 vN; in vec2 vT; in vec3 vP; void main(){ }\n"; pDepthSkin=mk(VS_SKIN,FS_NONE); pDepthStatic=mk(VS_STATIC,FS_NONE);
    body800=up(labGrid(20)); m100k=up(labGrid(129)); m800k=up(labGrid(365)); cit=up(cm); hero=up(labGrid(100)); street=up(labGrid(18));
    for(int i=0;i<4;i++) tex[i]=upTex(ct,0.5f+0.1f*i);
    glGenVertexArrays(1,&qVao);
    glGenTextures(1,&shTex); glBindTexture(GL_TEXTURE_2D,shTex); glTexStorage2D(GL_TEXTURE_2D,1,GL_DEPTH_COMPONENT24,1024,1024); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_COMPARE_REF_TO_TEXTURE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_FUNC,GL_LEQUAL); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1,&shFbo); glBindFramebuffer(GL_FRAMEBUFFER,shFbo); glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,shTex,0); { GLenum none=GL_NONE; glDrawBuffers(1,&none); glReadBuffer(GL_NONE); } if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) die("shadow fbo"); glBindFramebuffer(GL_FRAMEBUFFER,0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,shTex); glActiveTexture(GL_TEXTURE0);
    if(!getenv("LAB_LIVEONLY")){
    run("empty",tEmpty,0); run("grid skin 100k",tGrid100,0); run("grid skin 800k",tGrid800,0); run("draws 800 static",tDraws,800);
    run("bodies 100 own bones",tBodies,100); run("bodies 400 own bones",tBodies,400); run("instanced 100",tInst,100); run("instanced 400",tInst,400);
    run("full-screen layer A",tLayer,0); run("full-screen layer B",tLayer,1); run("full-screen layer C",tLayer,2);
    run("citizens+hero A",tCitizens,0); run("citizens+hero B",tCitizens,1); run("citizens+hero C",tCitizens,2);
    run("PROOF FRAME A",tProof,0); run("PROOF FRAME B + shadow",tProof,1); run("PROOF FRAME C + shadow",tProof,2);
    run("SPLIT x4 A",tSplit,0); run("SPLIT x4 B + shadow",tSplit,1); }
    live("PROOF A",tProof,0); live("PROOF B + shadow",tProof,1); live("PROOF C + shadow",tProof,2); live("SPLIT x4 A",tSplit,0); live("SPLIT x4 B + shadow",tSplit,1);
    printf("[done]\n"); return 0;
}
