/* crowd_bench.c — the phase-2 gate: N skinned dancers per frame, steady-state frame time. 2026-07-22.
 * Loads one or more .rpacks, deals SLOTS round-robin across them in a grid, every slot playing a
 * random clip at its own phase; animates + FK-poses + draws ALL of them for F frames offscreen and
 * reports steady-state ms/frame (CPU sample+FK and GPU draw+finish split).
 *   ./crowd_bench <slots> <frames> <pack1.rpack> [pack2.rpack ...]
 * Build: gcc -O2 -o crowd_bench crowd_bench.c -lgbm -lEGL -lGLESv2 -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb/stb_image.h>   /* Debian libstb-dev */

#define W 720
#define H 480
#define MAXB 64

typedef float M4[16];
static void m4_mul(M4 r, const M4 a, const M4 b){ M4 t; for(int c=0;c<4;c++)for(int rw=0;rw<4;rw++){ t[c*4+rw]=0; for(int k=0;k<4;k++) t[c*4+rw]+=a[k*4+rw]*b[c*4+k]; } memcpy(r,t,64); }
static void m4_trs(M4 m, const float *t, const float *q, const float *s){
    float x=q[0],y=q[1],z=q[2],w=q[3];
    float xx=x*x,yy=y*y,zz=z*z,xy=x*y,xz=x*z,yz=y*z,wx=w*x,wy=w*y,wz=w*z;
    m[0]=(1-2*(yy+zz))*s[0]; m[1]=(2*(xy+wz))*s[0]; m[2]=(2*(xz-wy))*s[0]; m[3]=0;
    m[4]=(2*(xy-wz))*s[1]; m[5]=(1-2*(xx+zz))*s[1]; m[6]=(2*(yz+wx))*s[1]; m[7]=0;
    m[8]=(2*(xz+wy))*s[2]; m[9]=(2*(yz-wx))*s[2]; m[10]=(1-2*(xx+yy))*s[2]; m[11]=0;
    m[12]=t[0]; m[13]=t[1]; m[14]=t[2]; m[15]=1;
}
static void m4_persp(M4 m, float fovyDeg, float aspect, float zn, float zf){
    float f=1.0f/tanf(fovyDeg*(float)M_PI/360.0f); memset(m,0,64);
    m[0]=f/aspect; m[5]=f; m[10]=(zf+zn)/(zn-zf); m[11]=-1; m[14]=2*zf*zn/(zn-zf);
}
static void m4_lookat(M4 m, float ex,float ey,float ez, float cx,float cy,float cz){
    float fz[3]={ex-cx,ey-cy,ez-cz}; float l=sqrtf(fz[0]*fz[0]+fz[1]*fz[1]+fz[2]*fz[2]); for(int i=0;i<3;i++)fz[i]/=l;
    float up[3]={0,1,0};
    float sx[3]={ up[1]*fz[2]-up[2]*fz[1], up[2]*fz[0]-up[0]*fz[2], up[0]*fz[1]-up[1]*fz[0] };
    l=sqrtf(sx[0]*sx[0]+sx[1]*sx[1]+sx[2]*sx[2]); for(int i=0;i<3;i++)sx[i]/=l;
    float uy[3]={ fz[1]*sx[2]-fz[2]*sx[1], fz[2]*sx[0]-fz[0]*sx[2], fz[0]*sx[1]-fz[1]*sx[0] };
    m[0]=sx[0];m[1]=uy[0];m[2]=fz[0];m[3]=0; m[4]=sx[1];m[5]=uy[1];m[6]=fz[1];m[7]=0;
    m[8]=sx[2];m[9]=uy[2];m[10]=fz[2];m[11]=0;
    m[12]=-(sx[0]*ex+sx[1]*ey+sx[2]*ez); m[13]=-(uy[0]*ex+uy[1]*ey+uy[2]*ez); m[14]=-(fz[0]*ex+fz[1]*ey+fz[2]*ez); m[15]=1;
}

typedef struct { unsigned name; int parent; unsigned kind; float t[3],q[4],s[3]; } __attribute__((packed)) PNode;
typedef struct { unsigned name,node,verts,idxCount,boneCount,flags,offPos,offNorm,offUV,offJoints,offWeights,offIdx,offBoneMap,offIBM,offBind; } PMesh;  /* RPK1 v2: 60B, offBind = the page's real mesh.bindMatrix */
typedef struct { unsigned name; float fps; unsigned frames,nTracks,offTracks; } __attribute__((packed)) PClip;

typedef struct {
    unsigned char *pk; size_t len;
    unsigned nNodes,nMeshes,nClips,nMats,nTex;
    PNode *nodes; PMesh *meshes; PClip *clips;
    unsigned char *mats;      /* 36B records */
    float scale;              /* normalize to ~3 units from posed rest span */
    float gnd;                /* audition grounding: min(bone cloud, ibmY0) */
    GLuint *vbo;              /* per mesh: pos/norm/joints/weights/idx/uv = 6 slots */
    GLuint *tex;              /* GL textures (decoded PNG blobs) */
    int   *meshTex;           /* per mesh: texture id or -1 */
    unsigned *meshMatFlags;   /* per mesh: material flags (1 nearest,2 flipY,4 transparent,8 emissiveIsMap) */
} Pack;

typedef struct { int pack; int clip; unsigned phase; float x,z,rotY; } Slot;

static void die(const char*m){ fprintf(stderr,"FATAL: %s (egl 0x%x gl 0x%x)\n",m,eglGetError(),glGetError()); exit(1); }
static double now_ms(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec*1000.0+ts.tv_nsec/1e6; }
static GLuint sh(GLenum ty,const char*src){ GLuint s=glCreateShader(ty); glShaderSource(s,1,&src,NULL); glCompileShader(s);
    GLint ok; glGetShaderiv(s,GL_COMPILE_STATUS,&ok); if(!ok){ char lg[512]; glGetShaderInfoLog(s,512,NULL,lg); fprintf(stderr,"%s\n",lg); die("shader"); } return s; }

static Pack loadPack(const char*path){
    Pack P; memset(&P,0,sizeof P);
    FILE*f=fopen(path,"rb"); if(!f) die("open pack");
    struct stat st; fstat(fileno(f),&st); P.len=st.st_size;
    P.pk=malloc(P.len); if(fread(P.pk,1,P.len,f)!=P.len) die("read"); fclose(f);
    if(memcmp(P.pk,"RPK1",4)) die("magic");
    unsigned*Hd=(unsigned*)(P.pk+4);
    P.nNodes=Hd[1]; P.nMeshes=Hd[2]; P.nMats=Hd[3]; P.nClips=Hd[4]; P.nTex=Hd[5];
    P.nodes=(PNode*)(P.pk+Hd[6]); P.meshes=(PMesh*)(P.pk+Hd[7]); P.clips=(PClip*)(P.pk+Hd[9]);
    P.mats=P.pk+Hd[8];
    /* materials FIRST (they carry per-texture flipY/nearest), then decode textures accordingly.
     * three.js uploads with flipY (bottom-up); PNG rows are top-down -- honor the baked flag or
     * every V coordinate samples the wrong half of the garment (seen: garbled black patches). */
    P.meshTex=malloc(P.nMeshes*sizeof(int)); P.meshMatFlags=calloc(P.nMeshes,sizeof(unsigned));
    int *texFlip=calloc(P.nTex?P.nTex:1,sizeof(int)), *texNear=calloc(P.nTex?P.nTex:1,sizeof(int));
    for(unsigned m=0;m<P.nMeshes;m++) P.meshTex[m]=-1;
    for(unsigned r=0;r<P.nMats;r++){ unsigned mesh=*(unsigned*)(P.mats+r*36);
        int tex=*(int*)(P.mats+r*36+24); unsigned fl=*(unsigned*)(P.mats+r*36+16);
        if(tex>=0&&(unsigned)tex<P.nTex){ if(fl&2) texFlip[tex]=1; if(fl&1) texNear[tex]=1; }
        if(mesh<P.nMeshes&&P.meshTex[mesh]==-1){ P.meshTex[mesh]=tex; P.meshMatFlags[mesh]=fl; } }
    P.tex=calloc(P.nTex?P.nTex:1,sizeof(GLuint));
    unsigned char*texTab=P.pk+Hd[10];
    for(unsigned t=0;t<P.nTex;t++){ unsigned off=*(unsigned*)(texTab+t*16+8), len=*(unsigned*)(texTab+t*16+12);
        stbi_set_flip_vertically_on_load(texFlip[t]);
        int tw,th,comp; unsigned char*img=stbi_load_from_memory(P.pk+off,(int)len,&tw,&th,&comp,4);
        if(!img){ fprintf(stderr,"tex %u decode failed\n",t); continue; }
        glGenTextures(1,&P.tex[t]); glBindTexture(GL_TEXTURE_2D,P.tex[t]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,tw,th,0,GL_RGBA,GL_UNSIGNED_BYTE,img);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,texNear[t]?GL_NEAREST:GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,texNear[t]?GL_NEAREST:GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        stbi_image_free(img); }
    free(texFlip); free(texNear);
    /* rest-pose bone span -> normalization scale (the page's want=3.05..3.4; use 3.15) */
    M4 *world=malloc(P.nNodes*sizeof(M4));
    float mny=1e9f,mxy=-1e9f;
    for(unsigned i=0;i<P.nNodes;i++){ M4 loc; float t[3],q[4],s[3];
        memcpy(t,P.nodes[i].t,12); memcpy(q,P.nodes[i].q,16); memcpy(s,P.nodes[i].s,12);
        m4_trs(loc,t,q,s);
        if(P.nodes[i].parent<0) memcpy(world[i],loc,64); else m4_mul(world[i],world[P.nodes[i].parent],loc);
        if(P.nodes[i].kind==1){ float y=world[i][13]; if(y<mny)mny=y; if(y>mxy)mxy=y; } }
    free(world);
    float span=(mxy-mny); P.scale=(span>1e-4f)?(3.15f/(span*1.13f)):1.0f;
    /* AUDITION-GRADE sizing (cast expansion): mesh-aware scale fallback (armature-scaled rigs
     * have ~zero bone span) + ibmY grounding — same lessons as the show's loadPack */
    { unsigned*Hd2=(unsigned*)(P.pk+4); float*szb=(float*)(P.pk+Hd2[12]);
      float meshY=szb[9], iy0=szb[7];
      /* trust meshY ONLY when bench-measured (szb[10]) -- legacy page meshY semantics vary
       * per family (the jacob-caught giants lesson); legacy packs keep bone-span sizing */
      if(szb[10]==1.0f&&meshY==meshY&&meshY>1e-4f) P.scale=3.15f/(meshY*1.05f);
      P.gnd=mny; if(szb[10]==1.0f&&iy0==iy0&&iy0<P.gnd) P.gnd=iy0; }
    /* VBOs per mesh (pos, norm, joints, weights, index, uv) */
    P.vbo=calloc(P.nMeshes*6,sizeof(GLuint));
    for(unsigned m=0;m<P.nMeshes;m++){ PMesh*M=&P.meshes[m]; if(!(M->flags&4)||M->boneCount>MAXB) continue;
        GLuint b[6]; glGenBuffers(6,b); memcpy(P.vbo+m*6,b,sizeof b);
        glBindBuffer(GL_ARRAY_BUFFER,b[0]); glBufferData(GL_ARRAY_BUFFER,M->verts*12,P.pk+M->offPos,GL_STATIC_DRAW);
        if(M->flags&1){ glBindBuffer(GL_ARRAY_BUFFER,b[1]); glBufferData(GL_ARRAY_BUFFER,M->verts*12,P.pk+M->offNorm,GL_STATIC_DRAW); }
        glBindBuffer(GL_ARRAY_BUFFER,b[2]); glBufferData(GL_ARRAY_BUFFER,M->verts*4,P.pk+M->offJoints,GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER,b[3]); glBufferData(GL_ARRAY_BUFFER,M->verts*4,P.pk+M->offWeights,GL_STATIC_DRAW);
        if(M->idxCount){ glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,b[4]); glBufferData(GL_ELEMENT_ARRAY_BUFFER,M->idxCount*4,P.pk+M->offIdx,GL_STATIC_DRAW); }
        if(M->flags&2){ glBindBuffer(GL_ARRAY_BUFFER,b[5]); glBufferData(GL_ARRAY_BUFFER,M->verts*8,P.pk+M->offUV,GL_STATIC_DRAW); } }
    return P;
}

int main(int argc,char**argv){
    if(argc<4){ fprintf(stderr,"usage: %s <slots> <frames> <pack...>\n",argv[0]); return 1; }
    int NS=atoi(argv[1]), NF=atoi(argv[2]); int NP=argc-3;

    /* GL first (VBO upload happens in loadPack) */
    int fd=open("/dev/dri/renderD128",O_RDWR); if(fd<0) die("renderD128");
    struct gbm_device*gbm=gbm_create_device(fd);
    PFNEGLGETPLATFORMDISPLAYEXTPROC gpd=(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy=gpd(EGL_PLATFORM_GBM_KHR,gbm,NULL); eglInitialize(dpy,NULL,NULL); eglBindAPI(EGL_OPENGL_ES_API);
    static const EGLint ca[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_NONE}; EGLConfig cfg; EGLint nc=0; eglChooseConfig(dpy,ca,&cfg,1,&nc);
    static const EGLint cx2[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLContext ctx=eglCreateContext(dpy,nc?cfg:NULL,EGL_NO_CONTEXT,cx2);
    if(!eglMakeCurrent(dpy,EGL_NO_SURFACE,EGL_NO_SURFACE,ctx)) die("makeCurrent");
    GLuint fbo,rb,drb; glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glGenRenderbuffers(1,&rb); glBindRenderbuffer(GL_RENDERBUFFER,rb); glRenderbufferStorage(GL_RENDERBUFFER,0x8058,W,H);
    glGenRenderbuffers(1,&drb); glBindRenderbuffer(GL_RENDERBUFFER,drb); glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT16,W,H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,drb);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) die("FBO");
    glViewport(0,0,W,H); glEnable(GL_DEPTH_TEST);

    Pack *packs=malloc(NP*sizeof(Pack));
    unsigned long vtot=0;
    for(int i=0;i<NP;i++){ packs[i]=loadPack(argv[3+i]);
        for(unsigned m=0;m<packs[i].nMeshes;m++) if(packs[i].meshes[m].flags&4) vtot+=packs[i].meshes[m].verts;
        printf("pack %d: %s  nodes %u meshes %u clips %u scale %.4f\n",i,argv[3+i],packs[i].nNodes,packs[i].nMeshes,packs[i].nClips,packs[i].scale); }

    const char*vs=
      "attribute vec3 p; attribute vec3 n; attribute vec4 j; attribute vec4 w; attribute vec2 t;\n"
      "uniform mat4 vp; uniform mat4 model; uniform mat4 bind; uniform mat4 pal[64];\n"
      "varying vec3 vn; varying vec2 vt;\n"
      "void main(){ vec4 bp=bind*vec4(p,1.0); vt=t;\n"
      "  mat4 skin=w.x*pal[int(j.x)]+w.y*pal[int(j.y)]+w.z*pal[int(j.z)]+w.w*pal[int(j.w)];\n"
      "  vec4 wp=model*skin*bp; vn=normalize((model*skin*(bind*vec4(n,0.0))).xyz);\n"
      "  gl_Position=vp*wp; }\n";
    /* TWO programs: opaque keeps early-Z (a shader containing `discard` loses early-Z for EVERY
     * draw on V3D -- measured: it cost more than the merge saved); cutout only for transparent mats */
    const char*fsO=
      "precision mediump float; varying vec3 vn; varying vec2 vt;\n"
      "uniform vec3 tint; uniform float useTex; uniform sampler2D smp;\n"
      "void main(){ float l=0.3+0.7*max(0.0,dot(normalize(vn),normalize(vec3(0.4,0.8,0.6))));\n"
      "  vec4 tx=texture2D(smp,vt);\n"
      "  vec3 base=mix(tint,tx.rgb,useTex);\n"
      "  gl_FragColor=vec4(base*(l*0.80+0.34),1.0); }\n";
    const char*fsC=
      "precision mediump float; varying vec3 vn; varying vec2 vt;\n"
      "uniform vec3 tint; uniform float useTex; uniform sampler2D smp;\n"
      "void main(){ float l=0.3+0.7*max(0.0,dot(normalize(vn),normalize(vec3(0.4,0.8,0.6))));\n"
      "  vec4 tx=texture2D(smp,vt);\n"
      "  if(useTex>0.5&&tx.a<0.4) discard;\n"
      "  vec3 base=mix(tint,tx.rgb,useTex);\n"
      "  gl_FragColor=vec4(base*(l*0.80+0.34),1.0); }\n";
    typedef struct { GLuint prog; GLint uVP,uModel,uBind,uPal,uTint,uUse; } Prog;
    Prog PR[2];
    for(int pi=0;pi<2;pi++){ GLuint prog=glCreateProgram();
        glAttachShader(prog,sh(GL_VERTEX_SHADER,vs)); glAttachShader(prog,sh(GL_FRAGMENT_SHADER,pi?fsC:fsO));
        glBindAttribLocation(prog,0,"p"); glBindAttribLocation(prog,1,"n"); glBindAttribLocation(prog,2,"j"); glBindAttribLocation(prog,3,"w"); glBindAttribLocation(prog,4,"t");
        glLinkProgram(prog); GLint ok; glGetProgramiv(prog,GL_LINK_STATUS,&ok); if(!ok) die("link");
        PR[pi].prog=prog;
        PR[pi].uVP=glGetUniformLocation(prog,"vp"); PR[pi].uModel=glGetUniformLocation(prog,"model");
        PR[pi].uBind=glGetUniformLocation(prog,"bind"); PR[pi].uPal=glGetUniformLocation(prog,"pal");
        PR[pi].uTint=glGetUniformLocation(prog,"tint"); PR[pi].uUse=glGetUniformLocation(prog,"useTex");
        glUseProgram(prog); glUniform1i(glGetUniformLocation(prog,"smp"),0); }
    glActiveTexture(GL_TEXTURE0);
    int curProg=-1;
    GLint uVP,uModel,uBind,uPal,uTint,uUse;   /* aliases of the ACTIVE program's locations */
    #define USEPROG(i) do{ if(curProg!=(i)){ curProg=(i); glUseProgram(PR[i].prog); \
        uVP=PR[i].uVP; uModel=PR[i].uModel; uBind=PR[i].uBind; uPal=PR[i].uPal; uTint=PR[i].uTint; uUse=PR[i].uUse; } }while(0)
    USEPROG(0);

    /* deal slots in a grid (like the floor: 26 x 12) */
    Slot *slots=malloc(NS*sizeof(Slot)); unsigned seed=777;
    int cols=(int)ceilf(sqrtf(NS*26.f/12.f));
    for(int i=0;i<NS;i++){ seed=seed*1103515245+12345;
        Pack*P=&packs[i%NP];
        slots[i].pack=i%NP;
        slots[i].clip=(int)((seed>>8)%(P->nClips?P->nClips:1));
        seed=seed*1103515245+12345; slots[i].phase=(seed>>8)%1000;
        slots[i].x=(i%cols)*(26.f/cols)-13.f+1.3f; slots[i].z=-(i/cols)*(12.f/(NS/cols+1))-1.0f;
        if(NS==1){ slots[i].x=0.f; slots[i].z=-4.5f; slots[i].rotY=0.25f; }   /* AUDITION: solo model dead-center in the lens */
        seed=seed*1103515245+12345; slots[i].rotY=((seed>>8)%628)/100.f; }

    M4 proj,view,vp; m4_persp(proj,42.0f,(float)W/H,0.1f,100.0f);
    m4_lookat(view, 0.f,4.5f,10.5f, 0.f,1.2f,-4.5f);
    m4_mul(vp,proj,view);
    for(int pi=0;pi<2;pi++){ glUseProgram(PR[pi].prog); glUniformMatrix4fv(PR[pi].uVP,1,GL_FALSE,vp); }
    curProg=-1; USEPROG(0);

    /* scratch */
    unsigned maxNodes=0; for(int i=0;i<NP;i++) if(packs[i].nNodes>maxNodes) maxNodes=packs[i].nNodes;
    float (*T)[3]=malloc(maxNodes*12); float (*Q)[4]=malloc(maxNodes*16); float (*S)[3]=malloc(maxNodes*12);
    M4 *world=malloc(maxNodes*sizeof(M4));
    static float pal[MAXB*16];

    double cpuTot=0, gpuTot=0; int measured=0;
    for(int fr=0;fr<NF;fr++){
        double c0=now_ms();
        glClearColor(0.02f,0.01f,0.04f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        for(int si=0;si<NS;si++){ Slot*sl=&slots[si]; Pack*P=&packs[sl->pack];
            /* sample clip frame */
            for(unsigned i=0;i<P->nNodes;i++){ memcpy(T[i],P->nodes[i].t,12); memcpy(Q[i],P->nodes[i].q,16); memcpy(S[i],P->nodes[i].s,12); }
            if(P->nClips){ PClip*c=&P->clips[sl->clip]; unsigned f2=(fr+sl->phase)%c->frames;
                unsigned char*tr=P->pk+c->offTracks;
                for(unsigned t=0;t<c->nTracks;t++){ unsigned node=*(unsigned*)(tr+t*12), type=*(unsigned*)(tr+t*12+4), offD=*(unsigned*)(tr+t*12+8);
                    float*d=(float*)(P->pk+offD); int comp=type==0?4:3;
                    if(type==0) memcpy(Q[node],d+f2*comp,16);
                    else if(type==1) memcpy(T[node],d+f2*comp,12);
                    else memcpy(S[node],d+f2*comp,12); } }
            for(unsigned i=0;i<P->nNodes;i++){ M4 loc; m4_trs(loc,T[i],Q[i],S[i]);
                if(P->nodes[i].parent<0) memcpy(world[i],loc,64); else m4_mul(world[i],world[P->nodes[i].parent],loc); }
            /* model: place on floor grid at normalized scale */
            M4 model; float ca2=cosf(sl->rotY), sa=sinf(sl->rotY), sc=P->scale;
            memset(model,0,64);
            model[0]=ca2*sc; model[2]=-sa*sc; model[5]=sc; model[8]=sa*sc; model[10]=ca2*sc; model[15]=1;
            model[12]=sl->x; model[13]=-P->gnd*sc; model[14]=sl->z;
            float tint[3]={0.55f+0.4f*((si*37%100)/100.f), 0.5f+0.3f*((si*61%100)/100.f), 0.6f+0.4f*((si*13%100)/100.f)};
            for(unsigned mi=0;mi<P->nMeshes;mi++){ PMesh*M=&P->meshes[mi];
                if(!(M->flags&4)||M->boneCount>MAXB) continue;
                USEPROG((P->meshMatFlags[mi]&4)?1:0);   /* transparent mats -> cutout program; everyone else keeps early-Z */
                glUniformMatrix4fv(uModel,1,GL_FALSE,model);
                glUniform3fv(uTint,1,tint);
                unsigned short*bm=(unsigned short*)(P->pk+M->offBoneMap);
                float*ibm=(float*)(P->pk+M->offIBM);
                for(unsigned b2=0;b2<M->boneCount;b2++){ M4 pm; m4_mul(pm,world[bm[b2]],*(M4*)(ibm+b2*16)); memcpy(pal+b2*16,pm,64); }
                /* MEASURED TRUTH (cast expansion): CPU-skin a vertex sample through the exact GPU
                 * math on the first posed frame -> the model's REAL rendered height/floor. Every
                 * static sizing guess (bone span, raw pos, rest*IBM) lies for some rig family. */
                { static int measured=0; static float mmn=1e9f,mmx=-1e9f; static int msDone=0;
                  static double cxs=0,czs=0; static long cn=0;
                  if(!msDone){
                    float*pp=(float*)(P->pk+M->offPos); float*bnd=(float*)(P->pk+M->offBind);
                    unsigned char*jj=P->pk+M->offJoints; unsigned char*ww=P->pk+M->offWeights;
                    for(unsigned v=0;v<M->verts;v+=7){
                        float bp[3]; for(int r=0;r<3;r++) bp[r]=bnd[r]*pp[v*3]+bnd[4+r]*pp[v*3+1]+bnd[8+r]*pp[v*3+2]+bnd[12+r];
                        float sy=0,sx=0,sz=0,swt=0;
                        for(int k=0;k<4;k++){ float wgt=ww[v*4+k]/255.f; if(wgt<=0) continue; float*pm2=pal+jj[v*4+k]*16;
                            sx+=wgt*(pm2[0]*bp[0]+pm2[4]*bp[1]+pm2[8]*bp[2]+pm2[12]);
                            sy+=wgt*(pm2[1]*bp[0]+pm2[5]*bp[1]+pm2[9]*bp[2]+pm2[13]);
                            sz+=wgt*(pm2[2]*bp[0]+pm2[6]*bp[1]+pm2[10]*bp[2]+pm2[14]); swt+=wgt; }
                        if(swt>0.01f){ sy/=swt; if(sy<mmn)mmn=sy; if(sy>mmx)mmx=sy;
                            cxs+=sx/swt; czs+=sz/swt; cn++; } }
                    measured++;
                    if(mi==P->nMeshes-1||measured>=(int)P->nMeshes){
                        printf("MEASURED name=%s heightRaw=%.4f floorRaw=%.4f cx=%.4f cz=%.4f (pre-model-scale units)\n",
                            argv[3+sl->pack],mmx-mmn,mmn,(float)(cxs/(cn?cn:1)),(float)(czs/(cn?cn:1))); fflush(stdout); msDone=1; } } }
                glUniformMatrix4fv(uPal,M->boneCount,GL_FALSE,pal);
                glUniformMatrix4fv(uBind,1,GL_FALSE,(float*)(P->pk+M->offBind));   /* v2: the page's ACTUAL bindMatrix, baked -- no reconstruction guessing */
                GLuint*b=&P->vbo[mi*6];
                int tex=P->meshTex[mi];
                if(tex>=0&&(M->flags&2)){ glBindTexture(GL_TEXTURE_2D,P->tex[tex]); glUniform1f(uUse,1.0f); }
                else glUniform1f(uUse,0.0f);
                glBindBuffer(GL_ARRAY_BUFFER,b[0]); glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,0);
                if(M->flags&1){ glBindBuffer(GL_ARRAY_BUFFER,b[1]); glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,0,0); }
                glBindBuffer(GL_ARRAY_BUFFER,b[2]); glEnableVertexAttribArray(2); glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_FALSE,0,0);
                glBindBuffer(GL_ARRAY_BUFFER,b[3]); glEnableVertexAttribArray(3); glVertexAttribPointer(3,4,GL_UNSIGNED_BYTE,GL_TRUE,0,0);
                if(M->flags&2){ glBindBuffer(GL_ARRAY_BUFFER,b[5]); glEnableVertexAttribArray(4); glVertexAttribPointer(4,2,GL_FLOAT,GL_FALSE,0,0); }
                else glDisableVertexAttribArray(4);
                if(M->idxCount){ glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,b[4]); glDrawElements(GL_TRIANGLES,M->idxCount,GL_UNSIGNED_INT,0); }
                else glDrawArrays(GL_TRIANGLES,0,M->verts);
            }
        }
        double c1=now_ms();
        glFinish();
        double c2=now_ms();
        if(fr>=10){ cpuTot+=c1-c0; gpuTot+=c2-c1; measured++; }   /* skip warmup */
    }
    printf("slots %d, packs %d, ~%lu verts/char-set\n",NS,NP,vtot);
    printf("steady state over %d frames: submit(CPU) %.2f ms + finish(GPU) %.2f ms = %.2f ms/frame  (%.1f fps)\n",
        measured, cpuTot/measured, gpuTot/measured, (cpuTot+gpuTot)/measured, 1000.0/((cpuTot+gpuTot)/measured));
    /* dump last frame for eyes */
    unsigned char*px=malloc(W*H*4); glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,px);
    FILE*o=fopen("/tmp/native_crowd.ppm","wb"); fprintf(o,"P6\n%d %d\n255\n",W,H);
    for(int y=H-1;y>=0;y--) for(int x=0;x<W;x++) fwrite(px+(y*W+x)*4,1,3,o); fclose(o);
    printf("last frame -> /tmp/native_crowd.ppm\n");
    return 0;
}
