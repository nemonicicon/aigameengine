/* skin_one.c — first SKINNED dancer in the native engine. 2026-07-22.
 * Loads an .rpack (RPK1 v1 — layout in ../bake/rpack_write.mjs), samples one clip frame,
 * FK-poses the node tree, GPU-skins with a mat4 palette, renders offscreen 720x480 -> PPM.
 *   ./skin_one <pack.rpack> [clipName] [frame] [out.ppm]
 * Bind matrix note: packs don't ship mesh.bindMatrix; for three's 'attached' bindMode the
 * effective bind is the mesh node's REST world — we reconstruct exactly that from pack TRS.
 * Build: gcc -O2 -o skin_one skin_one.c -lgbm -lEGL -lGLESv2 -lm
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

#define W 720
#define H 480
#define MAXB 64   /* bones per mesh palette (V3D uniform budget is comfortable here) */

/* ---------- tiny mat/quat ---------- */
typedef float M4[16]; /* column-major, GL style */
static void m4_ident(M4 m){ memset(m,0,64); m[0]=m[5]=m[10]=m[15]=1; }
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

/* ---------- rpack ---------- */
typedef struct { unsigned name; int parent; unsigned kind; float t[3],q[4],s[3]; } __attribute__((packed)) PNode;
typedef struct { unsigned name,node,verts,idxCount,boneCount,flags,offPos,offNorm,offUV,offJoints,offWeights,offIdx,offBoneMap,offIBM; } PMesh;
typedef struct { unsigned name; float fps; unsigned frames,nTracks,offTracks; } __attribute__((packed)) PClip;

static unsigned char *g_pk; static size_t g_pklen;
static const char* pstr(unsigned idx){
    unsigned offStrings=((unsigned*)(g_pk+4))[11];
    unsigned n=*(unsigned*)(g_pk+offStrings);
    static char buf[256];
    if(idx>=n) return "?";
    unsigned so=*(unsigned*)(g_pk+offStrings+4+idx*8), sl=*(unsigned*)(g_pk+offStrings+4+idx*8+4);
    const char*blob=(char*)g_pk+offStrings+4+n*8;
    if(sl>255) sl=255; memcpy(buf,blob+so,sl); buf[sl]=0; return buf;
}
static void die(const char*m){ fprintf(stderr,"FATAL: %s (egl 0x%x gl 0x%x)\n",m,eglGetError(),glGetError()); exit(1); }
static double now_ms(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec*1000.0+ts.tv_nsec/1e6; }
static GLuint sh(GLenum ty,const char*src){ GLuint s=glCreateShader(ty); glShaderSource(s,1,&src,NULL); glCompileShader(s);
    GLint ok; glGetShaderiv(s,GL_COMPILE_STATUS,&ok); if(!ok){ char lg[512]; glGetShaderInfoLog(s,512,NULL,lg); fprintf(stderr,"%s\n",lg); die("shader"); } return s; }

int main(int argc, char**argv){
    if(argc<2){ fprintf(stderr,"usage: %s pack.rpack [clip] [frame] [out.ppm]\n",argv[0]); return 1; }
    const char*clipWant=argc>2?argv[2]:NULL; int frameWant=argc>3?atoi(argv[3]):0;
    const char*outPath=argc>4?argv[4]:"/tmp/native_skin.ppm";

    /* load pack */
    FILE*f=fopen(argv[1],"rb"); if(!f) die("open pack");
    struct stat st; fstat(fileno(f),&st); g_pklen=st.st_size;
    g_pk=malloc(g_pklen); if(fread(g_pk,1,g_pklen,f)!=g_pklen) die("read pack"); fclose(f);
    if(memcmp(g_pk,"RPK1",4)) die("magic");
    unsigned *Hd=(unsigned*)(g_pk+4);
    unsigned nNodes=Hd[1],nMeshes=Hd[2],nClips=Hd[4];
    unsigned offNodes=Hd[6],offMeshes=Hd[7],offClips=Hd[9];
    PNode *nodes=(PNode*)(g_pk+offNodes);
    PMesh *meshes=(PMesh*)(g_pk+offMeshes);
    PClip *clips=(PClip*)(g_pk+offClips);
    printf("pack: %u nodes, %u meshes, %u clips\n",nNodes,nMeshes,nClips);

    /* pose: locals from rest, then clip frame overrides */
    float (*T)[3]=malloc(nNodes*12), (*Q)[4]=malloc(nNodes*16), (*S)[3]=malloc(nNodes*12);
    for(unsigned i=0;i<nNodes;i++){ memcpy(T[i],nodes[i].t,12); memcpy(Q[i],nodes[i].q,16); memcpy(S[i],nodes[i].s,12); }
    int clipIdx=-1;
    if(clipWant){ for(unsigned c=0;c<nClips;c++) if(!strcmp(pstr(clips[c].name),clipWant)){ clipIdx=(int)c; break; } }
    if(clipIdx>=0){ PClip*c=&clips[clipIdx]; unsigned fr=frameWant<0?0:((unsigned)frameWant % c->frames);
        unsigned char*tr=g_pk+c->offTracks;
        for(unsigned t=0;t<c->nTracks;t++){ unsigned node=*(unsigned*)(tr+t*12), type=*(unsigned*)(tr+t*12+4), offD=*(unsigned*)(tr+t*12+8);
            float*d=(float*)(g_pk+offD); int comp=type==0?4:3;
            if(type==0) memcpy(Q[node],d+fr*comp,16);
            else if(type==1) memcpy(T[node],d+fr*comp,12);
            else memcpy(S[node],d+fr*comp,12); }
        printf("clip: %s frame %u/%u\n",pstr(c->name),fr,c->frames);
    } else if(clipWant) printf("clip '%s' NOT FOUND -> rest pose\n",clipWant);

    /* FK (parents precede children in pack order) */
    M4 *world=malloc(nNodes*sizeof(M4));
    for(unsigned i=0;i<nNodes;i++){ M4 loc; m4_trs(loc,T[i],Q[i],S[i]);
        if(nodes[i].parent<0) memcpy(world[i],loc,64); else m4_mul(world[i],world[nodes[i].parent],loc); }

    /* auto-frame from posed bone cloud (bones only) */
    float mn[3]={1e9f,1e9f,1e9f},mx[3]={-1e9f,-1e9f,-1e9f}; unsigned nb=0;
    for(unsigned i=0;i<nNodes;i++) if(nodes[i].kind==1){ nb++;
        for(int k=0;k<3;k++){ float v=world[i][12+k]; if(v<mn[k])mn[k]=v; if(v>mx[k])mx[k]=v; } }
    float cx=(mn[0]+mx[0])/2, cy=(mn[1]+mx[1])/2, cz=(mn[2]+mx[2])/2;
    float span=mx[1]-mn[1]; if(span<=0) span=1;
    printf("bones %u, span y=%.3f center(%.2f %.2f %.2f)\n",nb,span,cx,cy,cz);

    /* GL up (render node, surfaceless) */
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

    const char*vs=
      "attribute vec3 p; attribute vec3 n; attribute vec4 j; attribute vec4 w;\n"
      "uniform mat4 mvp; uniform mat4 bind; uniform mat4 pal[64];\n"
      "varying vec3 vn;\n"
      "void main(){ vec4 bp=bind*vec4(p,1.0); vec4 bn4=bind*vec4(n,0.0);\n"
      "  mat4 skin=w.x*pal[int(j.x)]+w.y*pal[int(j.y)]+w.z*pal[int(j.z)]+w.w*pal[int(j.w)];\n"
      "  vec4 wp=skin*bp; vn=normalize((skin*bn4).xyz); gl_Position=mvp*wp; }\n";
    const char*fs=
      "precision mediump float; varying vec3 vn;\n"
      "void main(){ float l=0.25+0.75*max(0.0,dot(normalize(vn),normalize(vec3(0.4,0.8,0.6))));\n"
      "  gl_FragColor=vec4(0.75*l,0.72*l,0.80*l,1.0); }\n";
    GLuint prog=glCreateProgram(); glAttachShader(prog,sh(GL_VERTEX_SHADER,vs)); glAttachShader(prog,sh(GL_FRAGMENT_SHADER,fs));
    glBindAttribLocation(prog,0,"p"); glBindAttribLocation(prog,1,"n"); glBindAttribLocation(prog,2,"j"); glBindAttribLocation(prog,3,"w");
    glLinkProgram(prog); GLint ok; glGetProgramiv(prog,GL_LINK_STATUS,&ok); if(!ok) die("link"); glUseProgram(prog);

    /* camera: frame the posed character */
    M4 proj,view,mvp; m4_persp(proj,42.0f,(float)W/H,span*0.01f,span*20.0f);
    m4_lookat(view, cx, cy+span*0.12f, cz+span*1.55f, cx, cy, cz);
    m4_mul(mvp,proj,view);
    glUniformMatrix4fv(glGetUniformLocation(prog,"mvp"),1,GL_FALSE,mvp);

    glClearColor(0.02f,0.01f,0.04f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    double t0=now_ms();
    for(unsigned mi=0;mi<nMeshes;mi++){ PMesh*m=&meshes[mi];
        if(!(m->flags&4)) continue;                    /* skinned only */
        if(m->boneCount>MAXB){ printf("mesh %u skipped (%u bones > %d)\n",mi,m->boneCount,MAXB); continue; }
        /* palette: world[boneMap[j]] * ibm[j]; bind = rest world of the mesh node ('attached') */
        static float pal[MAXB*16];
        unsigned short*bm=(unsigned short*)(g_pk+m->offBoneMap);
        float*ibm=(float*)(g_pk+m->offIBM);
        for(unsigned b=0;b<m->boneCount;b++){ M4 pm; m4_mul(pm,world[bm[b]],*(M4*)(ibm+b*16)); memcpy(pal+b*16,pm,64); }
        glUniformMatrix4fv(glGetUniformLocation(prog,"pal"),m->boneCount,GL_FALSE,pal);
        /* bind matrix: mesh node REST world (recompute: rest locals up the chain) */
        M4 bind; { unsigned ni=m->node; M4 acc; m4_ident(acc); int chain[256]; int cn=0;
            for(int c2=(int)ni;c2>=0;c2=nodes[c2].parent) chain[cn++]=c2;
            for(int k=cn-1;k>=0;k--){ M4 loc; m4_trs(loc,nodes[chain[k]].t,nodes[chain[k]].q,nodes[chain[k]].s); m4_mul(acc,acc,loc); }
            memcpy(bind,acc,64); }
        glUniformMatrix4fv(glGetUniformLocation(prog,"bind"),1,GL_FALSE,bind);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,g_pk+m->offPos);
        if(m->flags&1){ glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,0,g_pk+m->offNorm); }
        glEnableVertexAttribArray(2); glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_FALSE,0,g_pk+m->offJoints);
        glEnableVertexAttribArray(3); glVertexAttribPointer(3,4,GL_UNSIGNED_BYTE,GL_TRUE,0,g_pk+m->offWeights);
        if(m->idxCount) glDrawElements(GL_TRIANGLES,m->idxCount,GL_UNSIGNED_INT,g_pk+m->offIdx);
        else glDrawArrays(GL_TRIANGLES,0,m->verts);
    }
    glFinish(); double t1=now_ms();
    unsigned char*px=malloc(W*H*4); glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,px);
    FILE*o=fopen(outPath,"wb"); fprintf(o,"P6\n%d %d\n255\n",W,H);
    for(int y=H-1;y>=0;y--) for(int x=0;x<W;x++) fwrite(px+(y*W+x)*4,1,3,o); fclose(o);
    printf("draw+finish %.2f ms -> %s\n",t1-t0,outPath);
    return 0;
}
