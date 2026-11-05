/* ---- A PERSON, BUILT WHILE THE GAME RUNS (queue step 15, 11-04) ----------------------------------------------------------------------------------------------------------
 * jacob, on players building their own character in a game: "thats a per game question / we need the capability and the feature." The people builder was a Python tool (skate_src/people/person.py), so a game couldn't call it. This is one in C, from the same measurements and tables: a body is a stature, how many heads tall it is, how it's built and what it's wearing.
 * It writes an ordinary pack into memory and the ordinary loader reads it, so a built body is the same as any other: it walks, it's lit, it talks (engine/face.inc), it can be worn and let go. E->bodyBuild gives a number like E->bodyPack does. The building's done on the stream's thread, so it can't reach a frame, and anything measured or compared does it on the spot. Nothing here is random: the same spec is the same pack, byte for byte, on any machine.
 * Built on y up, +z forward, +x the body's left, in metres, in a T pose. The skin, face and hair are lofted rings on Mixamo-named bones, so clips borrowed from other rigs work on it. It's a person at a game's budget, not film's: about 2,500 triangles, one 128 x 64 texture, and the face painted on. */
#define PB_MAXV 14000
#define PB_MAXI 60000
#define PB_MAXMORPH 8
#define PB_SEG 12
enum { PN_ROOT, PN_HIPS, PN_SPINE, PN_SPINE1, PN_NECK, PN_HEAD, PN_TOP, PN_LSH, PN_LARM, PN_LFORE, PN_LHAND, PN_LUP, PN_LLEG, PN_LFOOT, PN_LTOE, PN_RSH, PN_RARM, PN_RFORE, PN_RHAND, PN_RUP, PN_RLEG, PN_RFOOT, PN_RTOE, PN_JAW, PN_EYEL, PN_EYER, PN_MOUTH, PN_MOUTHL, PN_MOUTHR, PN_CHIN, PN_PONY1, PN_PONY2, PN_PONY3, PN_TRUE, PN_BODY, PN_INNER, PN_N };
static const char* const PN_NAME[PN_N]={"Armature","mixamorig:Hips","mixamorig:Spine","mixamorig:Spine1","mixamorig:Neck","mixamorig:Head","mixamorig:HeadTop_End","mixamorig:LeftShoulder","mixamorig:LeftArm","mixamorig:LeftForeArm","mixamorig:LeftHand","mixamorig:LeftUpLeg","mixamorig:LeftLeg","mixamorig:LeftFoot","mixamorig:LeftToeBase","mixamorig:RightShoulder","mixamorig:RightArm","mixamorig:RightForeArm","mixamorig:RightHand","mixamorig:RightUpLeg","mixamorig:RightLeg","mixamorig:RightFoot","mixamorig:RightToeBase","face_jaw","face_eye_l","face_eye_r","face_mouth","face_mouth_l","face_mouth_r","face_chin","hair_pony_1","hair_pony_2","hair_pony_3","TrueScale","person_body","mouth_interior"};
static const signed char PN_PARENT[PN_N]={-1,PN_ROOT,PN_HIPS,PN_SPINE,PN_SPINE1,PN_NECK,PN_HEAD,PN_SPINE1,PN_LSH,PN_LARM,PN_LFORE,PN_HIPS,PN_LUP,PN_LLEG,PN_LFOOT,PN_SPINE1,PN_RSH,PN_RARM,PN_RFORE,PN_HIPS,PN_RUP,PN_RLEG,PN_RFOOT,PN_HEAD,PN_HEAD,PN_HEAD,PN_HEAD,PN_HEAD,PN_HEAD,PN_HEAD,PN_HEAD,PN_PONY1,PN_PONY2,PN_ROOT,PN_ROOT,PN_ROOT};
#define PN_BONES (PN_PONY3)   /* nodes 1 to this are bones, in order, so a bone's place in the palette is its node less one */
enum { PC_SKIN, PC_HAIR, PC_TOP, PC_BOTTOM, PC_SHOES, PC_WHITE, PC_DARK, PC_TEETH, PC_EYE };
typedef struct { float*p,*n,*t; unsigned char*j,*w; unsigned*ix; int nv,ni; } PMeshB;
typedef struct { const char*name; int nD; unsigned*vi; float*d; } PMorphB;
typedef struct { float c[3],a,bf,bb; int cell,j0,j1,j1R; float w1; } PRing;   /* a half-width along U and depths along +V and -V. j1R: where the far bone's different on the side where U is negative */
static float pbClamp(float v,float a,float b){ return v<a?a:(v>b?b:v); }
static float pbSmooth(float a,float b,float x){ const float t=pbClamp((x-a)/(b-a),0.f,1.f); return t*t*(3.f-2.f*t); }
static float pbLerp(float a,float b,float t){ return a+(b-a)*t; }
static float pbG(float x,float s){ const float q=x/s; return expf(-q*q); }
static void pbCellUV(int cell,float*u,float*v){ const int cx=cell%4,cy=cell/4; *u=(64.f+16.f*(float)cx+8.f)/128.f; *v=(16.f*(float)cy+8.f)/64.f; }
static int pbVert(PMeshB*M,float x,float y,float z,float nx,float ny,float nz,float u,float v,int j0,int j1,float w1){ if(M->nv>=PB_MAXV) return M->nv-1; const int i=M->nv++; const float l=sqrtf(nx*nx+ny*ny+nz*nz); const float k=l>1e-6f?1.f/l:0.f;
    M->p[i*3]=x; M->p[i*3+1]=y; M->p[i*3+2]=z; M->n[i*3]=nx*k; M->n[i*3+1]=ny*k; M->n[i*3+2]=nz*k; M->t[i*2]=u; M->t[i*2+1]=v;
    /* a joint is a place in the palette; a weight's out of 255 and they add up to it */
    int wa=(int)((1.f-w1)*255.f+0.5f); if(wa<0) wa=0; if(wa>255) wa=255; unsigned char*J=M->j+i*4,*W=M->w+i*4; J[0]=(unsigned char)(j0-1); J[1]=(unsigned char)((j1>0?j1:j0)-1); J[2]=J[3]=0; W[0]=(unsigned char)wa; W[1]=(unsigned char)(255-wa); W[2]=W[3]=0; return i; }
static void pbTri(PMeshB*M,int a,int b,int c){ if(M->ni+3>PB_MAXI) return; M->ix[M->ni++]=(unsigned)a; M->ix[M->ni++]=(unsigned)b; M->ix[M->ni++]=(unsigned)c; }
static void pbQuad(PMeshB*M,int a,int b,int c,int d){ pbTri(M,a,b,c); pbTri(M,a,c,d); }
/* a skin of rings along a limb. Each ring keeps its own colour for the strip above it, and where the colour changes the ring's said twice */
static void pbRingAt(const PRing*R,const float*U,const float*V,float th,float*o){ const float s=sinf(th),c=cosf(th),dv=c>=0.f?R->bf:R->bb; for(int k=0;k<3;k++) o[k]=R->c[k]+U[k]*R->a*s+V[k]*dv*c; }
static void pbLoft(PMeshB*M,const PRing*R,int n,const float*U,const float*V,int seg,int capA,int capB){ int prev[64]; int last=-1;
    for(int i=0;i<n;i++){ const PRing*Q=&R[i]; const int strip=(i+1<n)?Q->cell:R[i-1].cell;
        int cur[64]; float cu,cv; pbCellUV(strip,&cu,&cv);
        for(int k=0;k<seg;k++){ const float th=6.28318f*(float)k/(float)seg,d=6.28318f/(float)seg; float p[3],a[3],b[3],c[3],e[3]; pbRingAt(Q,U,V,th,p); pbRingAt(Q,U,V,th-d,a); pbRingAt(Q,U,V,th+d,b);
            pbRingAt(&R[i>0?i-1:i],U,V,th,c); pbRingAt(&R[i+1<n?i+1:i],U,V,th,e);
            const float tx=b[0]-a[0],ty=b[1]-a[1],tz=b[2]-a[2],lx=e[0]-c[0],ly=e[1]-c[1],lz=e[2]-c[2]; float nx=ty*lz-tz*ly,ny=tz*lx-tx*lz,nz=tx*ly-ty*lx; const float rx=p[0]-Q->c[0],ry=p[1]-Q->c[1],rz=p[2]-Q->c[2]; if(nx*rx+ny*ry+nz*rz<0.f){ nx=-nx; ny=-ny; nz=-nz; }
            const int j1=(Q->j1R>0&&sinf(th)<0.f)?Q->j1R:Q->j1; cur[k]=pbVert(M,p[0],p[1],p[2],nx,ny,nz,cu,cv,Q->j0,j1,Q->w1); }
        if(i>0){ if(last!=strip){ /* the strip below ends on this ring in its own colour */ float u0,v0; pbCellUV(last,&u0,&v0); int low[64]; for(int k=0;k<seg;k++){ const int v=cur[k]; low[k]=pbVert(M,M->p[v*3],M->p[v*3+1],M->p[v*3+2],M->n[v*3],M->n[v*3+1],M->n[v*3+2],u0,v0,Q->j0,(Q->j1R>0&&sinf(6.28318f*(float)k/(float)seg)<0.f)?Q->j1R:Q->j1,Q->w1); }
                for(int k=0;k<seg;k++){ const int k2=(k+1)%seg; pbQuad(M,prev[k],prev[k2],low[k2],low[k]); } }
            else for(int k=0;k<seg;k++){ const int k2=(k+1)%seg; pbQuad(M,prev[k],prev[k2],cur[k2],cur[k]); } }
        memcpy(prev,cur,sizeof(int)*(size_t)seg); last=strip;
        if((i==0&&capA)||(i==n-1&&capB)){ const float nx=i==0?R[0].c[0]-R[1].c[0]:R[n-1].c[0]-R[n-2].c[0],ny=i==0?R[0].c[1]-R[1].c[1]:R[n-1].c[1]-R[n-2].c[1],nz=i==0?R[0].c[2]-R[1].c[2]:R[n-1].c[2]-R[n-2].c[2]; const int mid=pbVert(M,Q->c[0],Q->c[1],Q->c[2],nx,ny,nz,cu,cv,Q->j0,Q->j1,Q->w1); for(int k=0;k<seg;k++) pbTri(M,mid,cur[k],cur[(k+1)%seg]); } } }
/* a ball on one bone, its colours cut from the eye's cell or a plain one */
static void pbBall(PMeshB*M,const float*c,float rx,float ry,float rz,int bone,int cell,int nLat,int nLon,int eye){ int base=M->nv; float cu,cv; pbCellUV(cell,&cu,&cv);
    for(int a=0;a<=nLat;a++){ const float ph=1.5708f-3.14159f*(float)a/(float)nLat,cp=cosf(ph),sp=sinf(ph); for(int b=0;b<nLon;b++){ const float lm=6.28318f*(float)b/(float)nLon; const float dx=sinf(lm)*cp,dy=sp,dz=cosf(lm)*cp; float u=cu,v=cv;
            if(eye){ const float k=dz>0.f?1.f:0.f; u=cu+dx*k*7.5f/128.f; v=cv+dy*k*7.5f/64.f; }
            pbVert(M,c[0]+dx*rx,c[1]+dy*ry,c[2]+dz*rz,dx,dy,dz,u,v,bone,bone,0.f); } }
    for(int a=0;a<nLat;a++) for(int b=0;b<nLon;b++){ const int b2=(b+1)%nLon; pbQuad(M,base+a*nLon+b,base+a*nLon+b2,base+(a+1)*nLon+b2,base+(a+1)*nLon+b); } }
/* ---- the head: the people builder's table, chin (u 0) to crown (u 1), in units of the head's half-breadth, and half-depth forward and behind ---- */
static const float PH_T[15][4]={{0.000f,0.16f,0.70f,-0.52f},{0.035f,0.36f,0.80f,-0.20f},{0.090f,0.55f,0.86f,0.12f},{0.160f,0.70f,0.90f,0.40f},{0.230f,0.80f,0.92f,0.60f},{0.310f,0.86f,0.92f,0.78f},{0.400f,0.93f,0.91f,0.90f},{0.490f,0.97f,0.90f,0.97f},{0.600f,1.00f,0.94f,1.01f},{0.690f,1.00f,0.92f,1.03f},{0.780f,0.96f,0.82f,1.00f},{0.860f,0.86f,0.66f,0.90f},{0.930f,0.66f,0.44f,0.66f},{0.975f,0.38f,0.20f,0.36f},{1.000f,0.06f,0.02f,0.04f}};
typedef struct { float chinY,hh,bw,hd,zAxis,uLip,uEye,uNose,eyeX,nose,jaw; } PHead;
static void phRow(float u,float*hw,float*f,float*b){ u=pbClamp(u,0.f,1.f); int i=0; while(i<13&&PH_T[i+1][0]<u) i++; const float t=(u-PH_T[i][0])/(PH_T[i+1][0]-PH_T[i][0]),s=t*t*(3.f-2.f*t); const float m=0.5f*(t+s);
    *hw=pbLerp(PH_T[i][1],PH_T[i+1][1],m); *f=pbLerp(PH_T[i][2],PH_T[i+1][2],m); *b=pbLerp(PH_T[i][3],PH_T[i+1][3],m); }
static void phPoint(const PHead*H,float u,float th,float*o){ float hw,f,b; phRow(u,&hw,&f,&b); const float jw=u<0.35f?pbLerp(H->jaw,1.f,pbSmooth(0.05f,0.35f,u)):1.f; const float x=hw*jw*0.5f*H->bw*sinf(th),zf=f*H->hd,zb=-b*H->hd,zc=0.5f*(zf+zb),zr=0.5f*(zf-zb),cs=cosf(th); float z=zc+zr*cs;
    if(cs>0.f){ const float hb=H->hh;   /* what a face has that a ball hasn't */
        z+=cs*(H->nose*pbG(x,0.10f*H->bw)*pbG(u-H->uNose,0.045f)+0.45f*H->nose*pbG(x,0.07f*H->bw)*pbG(u-0.45f,0.075f)   /* the nose and its bridge */
            +0.045f*hb*pbG(x,0.17f*H->bw)*pbG(u-H->uLip,0.028f)                                                             /* the lips */
            +0.035f*hb*pbG(x,0.24f*H->bw)*pbG(u-0.07f,0.055f)                                                               /* the chin */
            +0.022f*hb*pbG(fabsf(x)-H->eyeX,0.28f*H->bw)*pbG(u-0.60f,0.030f)                                                /* the brow */
            -0.040f*hb*pbG(fabsf(x)-H->eyeX,0.16f*H->bw)*pbG(u-H->uEye,0.040f)); }                                          /* the sockets */
    o[0]=x; o[1]=H->chinY+u*H->hh; o[2]=H->zAxis+z; }
static void phNormal(const PHead*H,float u,float th,float*n){ float a[3],b[3],c[3],d[3]; const float du=0.006f,dt=0.03f; phPoint(H,u-du,th,a); phPoint(H,u+du,th,b); phPoint(H,u,th-dt,c); phPoint(H,u,th+dt,d); const float ax=b[0]-a[0],ay=b[1]-a[1],az=b[2]-a[2],bx=d[0]-c[0],by=d[1]-c[1],bz=d[2]-c[2]; n[0]=by*az-bz*ay; n[1]=bz*ax-bx*az; n[2]=bx*ay-by*ax; float p[3]; phPoint(H,u,th,p); if(n[0]*p[0]+n[2]*(p[2]-H->zAxis)+n[1]*(p[1]-H->chinY-0.5f*H->hh)<0.f){ n[0]=-n[0]; n[1]=-n[1]; n[2]=-n[2]; } }
/* ---- the texture: the face on the left, front on, and flat colours on the right ---- */
/* (step 23) its alpha says what kind of surface each pixel is, because the whole person is one mesh: 230 skin, 204 hair, 179 an eye, 153 cloth, 255 nothing in particular.
 * A mesh whose name has person_body in it is read that way (engine/load.inc, MAT_KIND). Nothing's see-through: every one of them is well over the half that cuts a hole */
static void pbPaint(unsigned char*px,const EngineBodySpec*S,const PHead*H,const unsigned char col[9][3]){ (void)S; const int W=128,HT=64;
    for(int y=0;y<HT;y++) for(int x=0;x<W;x++){ unsigned char*o=px+(size_t)(y*W+x)*4; float c[3]; const unsigned char*sk=col[PC_SKIN]; c[0]=sk[0]; c[1]=sk[1]; c[2]=sk[2]; unsigned char ka=230;
        if(x<64){ const float X=((float)x+0.5f)/32.f-1.f,U=((float)y+0.5f)/64.f*0.86f,ax=fabsf(X); const float mw=0.34f;
            /* a blush on the cheeks */
            { const float bl=0.10f*pbG(ax-0.52f,0.2f)*pbG(U-0.32f,0.06f); c[0]+=(205.f-c[0])*bl; c[1]+=(80.f-c[1])*bl; c[2]+=(80.f-c[2])*bl; }
            /* shade under the nose, round the eyes and under the bottom lip */
            { const float sh=0.20f*pbG(ax,0.10f)*pbG(U-(H->uNose-0.035f),0.012f)+0.08f*pbG(ax-0.45f,0.30f)*pbG(U-H->uEye,0.05f)+0.12f*pbG(ax,0.20f)*pbG(U-(H->uLip-0.05f),0.012f); for(int k=0;k<3;k++) c[k]*=1.f-sh; }
            /* the lips: a bow on top, and fuller below */
            { const float top=H->uLip+0.020f*(1.f-(ax/mw)*(ax/mw))+0.006f*pbG(ax,0.05f)*(-1.f),bot=H->uLip-0.030f*(1.f-(ax/mw)*(ax/mw)); if(ax<mw&&U<top&&U>bot){ const float k=pbSmooth(0.f,0.04f,mw-ax); c[0]+=(sk[0]*0.70f+50.f-c[0])*k; c[1]+=(sk[1]*0.34f-c[1])*k; c[2]+=(sk[2]*0.40f-c[2])*k; }
              if(ax<mw&&fabsf(U-H->uLip)<0.0035f){ c[0]*=0.45f; c[1]*=0.35f; c[2]*=0.35f; } }
            /* the brows, in the hair's colour */
            { const float m=fabsf(ax-0.47f)/0.27f; const float yb=0.605f+0.016f*(1.f-m*m); if(m<1.f&&fabsf(U-yb)<0.012f*(1.f-0.45f*m)){ const unsigned char*h=col[PC_HAIR]; c[0]=h[0]*0.8f; c[1]=h[1]*0.8f; c[2]=h[2]*0.8f; } } }
        else { const int cx=(x-64)/16,cy=y/16,cell=cy*4+cx; const unsigned char*f=cell<9?col[cell]:col[PC_SKIN]; c[0]=f[0]; c[1]=f[1]; c[2]=f[2];
            ka=(unsigned char)(cell==PC_HAIR?204:(cell==PC_TOP||cell==PC_BOTTOM)?153:(cell==PC_EYE||cell==PC_WHITE)?179:(cell==PC_SKIN||cell>=9)?230:255);
            if(cell==PC_EYE){ const float ex=((float)(x-64-cx*16)-7.5f)/7.5f,ey=((float)(y-cy*16)-7.5f)/7.5f,r=sqrtf(ex*ex+ey*ey); const unsigned char*wh=col[PC_WHITE]; if(r>0.62f){ c[0]=wh[0]; c[1]=wh[1]; c[2]=wh[2]; } else if(r<0.26f){ c[0]=c[1]=c[2]=12.f; } else { const float k=0.65f+0.5f*r; c[0]*=k; c[1]*=k; c[2]*=k; } } }
        o[0]=(unsigned char)pbClamp(c[0],0.f,255.f); o[1]=(unsigned char)pbClamp(c[1],0.f,255.f); o[2]=(unsigned char)pbClamp(c[2],0.f,255.f); o[3]=ka; } }
/* a PNG with nothing compressed: it's small, and stb_image reads it like any other */
static unsigned pbCrc(unsigned c,const unsigned char*b,size_t n){ c=~c; for(size_t i=0;i<n;i++){ c^=b[i]; for(int k=0;k<8;k++) c=(c>>1)^(0xEDB88320u&(0u-(c&1u))); } return ~c; }
static void pbBE(unsigned char*o,unsigned v){ o[0]=(unsigned char)(v>>24); o[1]=(unsigned char)(v>>16); o[2]=(unsigned char)(v>>8); o[3]=(unsigned char)v; }
static size_t pbPngChunk(unsigned char*o,const char*tag,const unsigned char*d,unsigned n){ pbBE(o,n); memcpy(o+4,tag,4); if(n) memcpy(o+8,d,n); pbBE(o+8+n,pbCrc(0u,o+4,(size_t)n+4u)); return (size_t)n+12u; }
static size_t pbPng(unsigned char*o,const unsigned char*rgba,int w,int h){ static const unsigned char sig[8]={137,80,78,71,13,10,26,10}; memcpy(o,sig,8); size_t at=8; unsigned char ih[13]; pbBE(ih,(unsigned)w); pbBE(ih+4,(unsigned)h); ih[8]=8; ih[9]=6; ih[10]=ih[11]=ih[12]=0; at+=pbPngChunk(o+at,"IHDR",ih,13);
    const size_t raw=(size_t)h*((size_t)w*4u+1u); unsigned char*r=(unsigned char*)malloc(raw); for(int y=0;y<h;y++){ r[(size_t)y*((size_t)w*4u+1u)]=0; memcpy(r+(size_t)y*((size_t)w*4u+1u)+1,rgba+(size_t)y*(size_t)w*4u,(size_t)w*4u); }
    const unsigned blocks=(unsigned)((raw+65534u)/65535u); unsigned char*z=(unsigned char*)malloc(raw+(size_t)blocks*5u+6u); size_t zn=0; z[zn++]=0x78; z[zn++]=0x01; unsigned a=1,bb=0; for(size_t i=0;i<raw;i++){ a=(a+r[i])%65521u; bb=(bb+a)%65521u; }
    for(size_t off=0;off<raw;){ const unsigned n=raw-off>65535u?65535u:(unsigned)(raw-off); z[zn++]=off+n>=raw?1:0; z[zn++]=(unsigned char)n; z[zn++]=(unsigned char)(n>>8); z[zn++]=(unsigned char)~n; z[zn++]=(unsigned char)(~n>>8); memcpy(z+zn,r+off,n); zn+=n; off+=n; }
    pbBE(z+zn,(bb<<16)|a); zn+=4; at+=pbPngChunk(o+at,"IDAT",z,(unsigned)zn); at+=pbPngChunk(o+at,"IEND",NULL,0); free(r); free(z); return at; }
/* ---- its clips, from the sample's: an idle, a walk and a run, all made of sines ---- */
typedef struct { const char*name; int frames; float thigh,knee,arm,elbow,lean,bob; int idle; } PClipDef;
static const PClipDef PB_CLIPS[3]={{"idle",60,0,0,0.03f,0.15f,0,0,1},{"w_walk",30,0.45f,0.9f,0.35f,0.25f,0.03f,0.012f,0},{"w_run",20,0.8f,1.5f,0.6f,1.1f,0.14f,0.03f,0}};
#define PB_TRACKS 10
static const int PB_TRACKN[PB_TRACKS]={PN_LARM,PN_RARM,PN_LFORE,PN_RFORE,PN_SPINE,PN_LUP,PN_RUP,PN_LLEG,PN_RLEG,PN_HIPS};
static void pbClipFrame(const PClipDef*C,float u,float hipsY,float k,float Q[9][4],float*t){ const float s=sinf(6.28318f*u),TAU=6.28318f; float a[4],b[4];
    const float swing=C->arm*s; qaxis(a,1,0,0,swing); qaxis(b,0,0,1,-1.25f); qmul(Q[0],a,b); qaxis(a,1,0,0,-swing); qaxis(b,0,0,1,1.25f); qmul(Q[1],a,b); qaxis(Q[2],0,1,0,C->elbow); qaxis(Q[3],0,1,0,-C->elbow);
    if(C->idle){ qaxis(Q[4],1,0,0,0.015f*s); for(int i=5;i<9;i++){ Q[i][0]=Q[i][1]=Q[i][2]=0.f; Q[i][3]=1.f; } t[0]=0.f; t[1]=hipsY+0.004f*s*k; t[2]=0.f; return; }
    qaxis(a,1,0,0,C->lean); qaxis(b,0,1,0,0.08f*s); qmul(Q[4],a,b); const float c0=cosf(TAU*u),c1=cosf(TAU*(u+0.5f)); const float kl=c0>0.f?powf(c0,1.5f):0.f,kr=c1>0.f?powf(c1,1.5f):0.f;
    qaxis(Q[5],1,0,0,-C->thigh*s); qaxis(Q[6],1,0,0,C->thigh*s); qaxis(Q[7],1,0,0,0.05f+C->knee*kl); qaxis(Q[8],1,0,0,0.05f+C->knee*kr); t[0]=0.f; t[1]=hipsY+C->bob*k*(cosf(2.f*TAU*u)-1.f); t[2]=0.f; }
/* ---- the file ---- */
typedef struct { unsigned char*b; size_t n,cap; } PBuf;
static size_t pbPut(PBuf*B,const void*d,size_t n){ B->n=(B->n+15u)&~(size_t)15u; if(B->n+n>B->cap){ B->cap=(B->n+n)*2u; B->b=(unsigned char*)realloc(B->b,B->cap); } const size_t o=B->n; if(d) memcpy(B->b+o,d,n); else memset(B->b+o,0,n); B->n+=n; return o; }
static void pbPutU(unsigned char*o,unsigned v){ memcpy(o,&v,4); }
/* a whole pack from a spec. Any thread: it only reads the spec, and it's the same bytes each time. Gives malloc'd bytes */
static unsigned char* peopleBytes(const EngineBodySpec*Sp,size_t*len){ EngineBodySpec S=*Sp; const float H0=S.stature>0.2f?pbClamp(S.stature,0.3f,3.f):1.70f,heads=S.heads>1.f?pbClamp(S.heads,3.5f,9.f):7.5f,bd=S.build>0.f?pbClamp(S.build,0.5f,2.f):1.f; const float K=H0/1.65f,kid=pbClamp((7.5f-heads)/2.f,0.f,1.f);
    const float msh=S.shoulders>0.f?S.shoulders:1.f,mwa=S.waist>0.f?S.waist:1.f,mhi=S.hips>0.f?S.hips:1.f,mli=(S.limbs>0.f?S.limbs:1.f)*sqrtf(bd); const float bust=pbClamp(S.bust,0.f,1.f),belly=pbClamp(S.belly,0.f,1.f);
#define PLM(a,b) (pbLerp((a),(b),kid)*H0)
    float yAnkle=PLM(.042f,.048f),yKnee=PLM(.283f,.262f),yCrotch=PLM(.472f,.440f),yHipJ=PLM(.512f,.476f),yWaist=PLM(.625f,.598f),yBust=PLM(.722f,.688f),yArmpit=PLM(.748f,.703f),yShoulder=PLM(.815f,.770f),yNeck=PLM(.830f,.790f); const float hh=H0/heads,yChin=H0-hh;
#undef PLM
    if(S.legs>0.f){ const float want=pbClamp(S.legs,0.3f,0.65f)*H0,k1=want/yCrotch; yAnkle*=k1; yKnee*=k1; yHipJ=want+(yHipJ-yCrotch)*0.8f; const float d=want-yCrotch; yWaist+=d*0.6f; yBust+=d*0.3f; yArmpit+=d*0.2f; yCrotch=want; }
    if(yNeck>yChin-0.03f*hh){ const float d=yNeck-(yChin-0.03f*hh); yNeck-=d; yShoulder-=d; yArmpit-=d; yBust-=d; }
    const float hipX=.176f*K*bd*mhi,hipF=.090f*K*bd,hipB=.108f*K*bd,waX=.128f*K*bd*mwa+.02f*K*belly,waF=.078f*K*bd*mwa+.055f*K*belly,waB=.080f*K*bd*mwa,chX=.140f*K*bd*(0.5f+0.5f*msh),chF=.084f*K*bd+.040f*K*bust,chB=.090f*K*bd,shX=.188f*K*(0.6f+0.4f*bd)*msh,neckR=.049f*K*(0.7f+0.3f*bd);
    const float armU=.300f*K,armF=.238f*K,handL=.180f*K,rDel=.046f*K*mli,rUp=.041f*K*mli,rElb=.034f*K*mli,rFore=.037f*K*mli,rWr=.025f*K*(0.6f+0.4f*mli),rTh=.080f*K*mli,rKn=.049f*K*mli,rCalf=.053f*K*mli,rAnk=.032f*K*(0.6f+0.4f*mli);
    const float hipSX=fmaxf(.095f*K,hipX*0.52f),shJX=shX-.045f*K,shJY=yShoulder-.028f*K;
    PHead Hd; Hd.chinY=yChin; Hd.hh=hh; Hd.bw=0.658f*hh*(1.f+0.08f*kid); Hd.hd=0.856f*hh*0.5f*(1.f+0.04f*kid); Hd.zAxis=0.012f*hh; Hd.uLip=pbLerp(0.215f,0.17f,kid); Hd.uEye=pbLerp(0.50f,0.42f,kid); Hd.uNose=pbLerp(0.335f,0.27f,kid); Hd.eyeX=0.45f*Hd.bw*0.5f; Hd.nose=(S.nose>0.f?S.nose:1.f)*0.052f*hh*(1.f-0.45f*kid); Hd.jaw=S.jaw>0.f?pbClamp(S.jaw,0.7f,1.25f):1.f;
    const float er=0.075f*hh*(1.f+0.25f*kid);   /* an eye's a bit bigger than life, so a face reads on a TV */
    float eL[3]; phPoint(&Hd,Hd.uEye,asinf(pbClamp(Hd.eyeX/(0.97f*Hd.bw*0.5f),-1.f,1.f)),eL); eL[0]=Hd.eyeX; eL[2]-=0.45f*er; float lip[3],lipC[3]; phPoint(&Hd,Hd.uLip,0.f,lip); const float mouthW=0.112f*hh; phPoint(&Hd,Hd.uLip,asinf(pbClamp(mouthW/(0.82f*Hd.bw*0.5f),-1.f,1.f)),lipC);
    /* the bones, where they stand */
    float W[PN_N][3]; memset(W,0,sizeof W);
#define PW(i,x,y,z) do{ W[i][0]=(x); W[i][1]=(y); W[i][2]=(z); }while(0)
    PW(PN_HIPS,0,yHipJ+.02f*H0,0); PW(PN_SPINE,0,0.5f*(yHipJ+yWaist)+.02f*H0,0); PW(PN_SPINE1,0,yBust-.03f*H0,0); PW(PN_NECK,0,yNeck,-.004f*H0); PW(PN_HEAD,0,yChin+.10f*hh,-.002f*H0); PW(PN_TOP,0,H0,0);
    for(int s=0;s<2;s++){ const float sx=s?-1.f:1.f; const int o=s?PN_RSH-PN_LSH:0; PW(PN_LSH+o,sx*.05f*K,shJY,0); PW(PN_LARM+o,sx*shJX,shJY,0); PW(PN_LFORE+o,sx*(shJX+armU),shJY,0); PW(PN_LHAND+o,sx*(shJX+armU+armF),shJY,0);
        PW(PN_LUP+o,sx*hipSX,yHipJ,0); PW(PN_LLEG+o,sx*hipSX,yKnee,0); PW(PN_LFOOT+o,sx*hipSX,yAnkle,-.01f*K); PW(PN_LTOE+o,sx*hipSX,.03f*K,.12f*K); }
    PW(PN_JAW,0,yChin+.40f*hh,-.05f*hh); PW(PN_EYEL,eL[0],eL[1],eL[2]); PW(PN_EYER,-eL[0],eL[1],eL[2]); PW(PN_MOUTH,0,lip[1],lip[2]); PW(PN_MOUTHL,mouthW,lipC[1],lipC[2]); PW(PN_MOUTHR,-mouthW,lipC[1],lipC[2]); PW(PN_CHIN,0,yChin,0.8f*Hd.hd);
    { float bk[3]; phPoint(&Hd,0.66f,3.14159f,bk); const float L=0.42f*hh*(1.f+pbClamp(S.hairLength,0.f,3.f)); PW(PN_PONY1,0,bk[1],bk[2]-.02f*K); PW(PN_PONY2,0,bk[1]-L*0.45f,bk[2]-.045f*K); PW(PN_PONY3,0,bk[1]-L,bk[2]-.045f*K); }
    unsigned char col[9][3]; const unsigned char dSk[3]={214,170,140},dHa[3]={55,38,28},dTo[3]={70,110,170},dBo[3]={50,55,70},dSh[3]={35,30,30},dEy[3]={80,110,70};
#define PCOL(c,f,d) do{ if((f)[0]||(f)[1]||(f)[2]) memcpy(col[c],(f),3); else memcpy(col[c],(d),3); }while(0)
    PCOL(PC_SKIN,S.skin,dSk); PCOL(PC_HAIR,S.hair,dHa); PCOL(PC_TOP,S.top,dTo); PCOL(PC_BOTTOM,S.bottom,dBo); PCOL(PC_SHOES,S.shoes,dSh); PCOL(PC_EYE,S.eyes,dEy); col[PC_WHITE][0]=232; col[PC_WHITE][1]=232; col[PC_WHITE][2]=226; col[PC_DARK][0]=28; col[PC_DARK][1]=10; col[PC_DARK][2]=12; col[PC_TEETH][0]=225; col[PC_TEETH][1]=222; col[PC_TEETH][2]=210;
#undef PCOL
    PMeshB M,MI; memset(&M,0,sizeof M); memset(&MI,0,sizeof MI); PMeshB*Ms[2]={&M,&MI}; for(int i=0;i<2;i++){ Ms[i]->p=(float*)malloc((size_t)PB_MAXV*12u); Ms[i]->n=(float*)malloc((size_t)PB_MAXV*12u); Ms[i]->t=(float*)malloc((size_t)PB_MAXV*8u); Ms[i]->j=(unsigned char*)malloc((size_t)PB_MAXV*4u); Ms[i]->w=(unsigned char*)malloc((size_t)PB_MAXV*4u); Ms[i]->ix=(unsigned*)malloc((size_t)PB_MAXI*4u); }
    static const float UX[3]={1,0,0},UY[3]={0,1,0},VZ[3]={0,0,1}; PRing R[32]; int n;
#define PRG(cx,cy,cz,A,F,B,C,J0,J1,JR,WW) do{ PRing*r_=&R[n++]; r_->c[0]=(cx); r_->c[1]=(cy); r_->c[2]=(cz); r_->a=(A); r_->bf=(F); r_->bb=(B); r_->cell=(C); r_->j0=(J0); r_->j1=(J1); r_->j1R=(JR); r_->w1=(WW); }while(0)
    /* the torso, crotch to neck. What it's in changes at the waist, and it's skin from the collar up */
    { const float y0=yCrotch,z0=.006f*K; n=0; const int skirt=S.bottomKind==2; (void)skirt;
      PRG(0,y0,z0,hipX*.70f,hipF*.70f,hipB*.72f,PC_BOTTOM,PN_HIPS,PN_LUP,PN_RUP,0.5f);
      PRG(0,y0+.018f*H0,z0,hipX*.92f,hipF*.92f,hipB*.94f,PC_BOTTOM,PN_HIPS,PN_LUP,PN_RUP,0.25f);
      PRG(0,y0+.045f*H0,z0,hipX,hipF,hipB,PC_BOTTOM,PN_HIPS,PN_SPINE,0,0.f);
      PRG(0,pbLerp(y0,yWaist,.62f),z0,.5f*(hipX+waX)+.05f*(hipX-waX),.5f*(hipF+waF),.5f*(hipB+waB),PC_BOTTOM,PN_HIPS,PN_SPINE,0,0.35f);
      PRG(0,yWaist,z0,waX,waF,waB,PC_TOP,PN_SPINE,PN_HIPS,0,0.10f);
      PRG(0,pbLerp(yWaist,yBust,.62f),z0,.5f*(waX+chX)+.2f*(chX-waX),.5f*(waF+chF),.5f*(waB+chB),PC_TOP,PN_SPINE,PN_SPINE1,0,0.6f);
      PRG(0,yBust,z0,chX,chF,chB,PC_TOP,PN_SPINE1,PN_SPINE,0,0.1f);
      PRG(0,yArmpit,z0+.002f,chX*1.03f,chF*.92f,chB,PC_TOP,PN_SPINE1,PN_SPINE1,0,0.f);
      PRG(0,pbLerp(yArmpit,yShoulder,.5f),z0,fmaxf(chX,shJX+.25f*rDel),chF*.82f,chB*.94f,PC_TOP,PN_SPINE1,PN_SPINE1,0,0.f);
      PRG(0,yShoulder-.004f*H0,z0-.003f*K,shJX+.06f*rDel,chF*.60f,chB*.74f,PC_TOP,PN_SPINE1,PN_SPINE1,0,0.f);
      PRG(0,yNeck-.006f*H0,z0-.006f*K,neckR*1.9f,neckR*1.15f,neckR*1.45f,PC_TOP,PN_SPINE1,PN_NECK,0,0.2f);
      PRG(0,yNeck+.010f*H0,z0-.008f*K,neckR*1.32f,neckR*1.08f,neckR*1.25f,PC_SKIN,PN_NECK,PN_SPINE1,0,0.2f);
      PRG(0,pbLerp(yNeck,yChin,.5f),z0-.006f*K,neckR,neckR*.98f,neckR*1.02f,PC_SKIN,PN_NECK,PN_HEAD,0,0.2f);
      PRG(0,yChin+.22f*hh,z0-.004f*K,neckR*.9f,neckR*.9f,neckR*.9f,PC_SKIN,PN_HEAD,PN_NECK,0,0.1f);
      pbLoft(&M,R,n,UX,VZ,PB_SEG+4,1,1); }
    /* a skirt, over the legs, and it goes with them */
    if(S.bottomKind==2){ const float hem=pbLerp(yKnee,yCrotch,0.35f); n=0; PRG(0,yWaist-.01f*H0,.006f*K,waX*1.04f,waF*1.04f,waB*1.04f,PC_BOTTOM,PN_HIPS,PN_SPINE,0,0.2f); PRG(0,pbLerp(yWaist,yCrotch,.5f),.006f*K,hipX*1.06f,hipF*1.1f,hipB*1.1f,PC_BOTTOM,PN_HIPS,PN_LUP,PN_RUP,0.1f); PRG(0,yCrotch,.006f*K,hipX*1.18f,hipF*1.3f,hipB*1.3f,PC_BOTTOM,PN_HIPS,PN_LUP,PN_RUP,0.5f); PRG(0,hem,.006f*K,hipX*1.34f,hipF*1.55f,hipB*1.55f,PC_BOTTOM,PN_LUP,PN_HIPS,0,0.15f); for(int i=0;i<n;i++) if(i==n-1){ R[i].j0=PN_HIPS; R[i].j1=PN_LUP; R[i].j1R=PN_RUP; R[i].w1=0.85f; } pbLoft(&M,R,n,UX,VZ,PB_SEG+4,0,0); }
    /* the legs: what's on them stops at the knee or goes on to the ankle */
    for(int s=0;s<2;s++){ const float sx=s?-1.f:1.f,x=sx*hipSX; const int o=s?PN_RSH-PN_LSH:0,J0=PN_LUP+o,J1=PN_LLEG+o,J2=PN_LFOOT+o,J3=PN_LTOE+o; const int bare=S.bottomKind==2; const float yHem=bare?1e6f:(S.bottomKind==0?pbLerp(yKnee,yCrotch,0.45f):yAnkle+.03f*K); const int cA=PC_BOTTOM,cB=PC_SKIN; n=0; const float t1=yHipJ+.02f*H0,c0=.005f*K;
#define PLEGC(y) (((y)>=yHem)?cA:cB)
      PRG(x-sx*.012f*K,t1,c0,rTh*.95f,rTh*.95f,rTh*.95f,PLEGC(t1),PN_HIPS,J0,0,0.5f); PRG(x,yCrotch,c0,rTh,rTh,rTh*1.04f,PLEGC(yCrotch),J0,PN_HIPS,0,0.05f);
      for(int i=1;i<=3;i++){ const float u=(float)i/4.f,y=pbLerp(yCrotch,yKnee,u),r=pbLerp(rTh,rKn,u*u*0.6f+u*0.4f); PRG(x,y,c0,r,r,r*1.02f,PLEGC(y),J0,J1,0,pbSmooth(0.6f,1.f,u)*0.35f); }
      PRG(x,yKnee+.012f*H0,c0,rKn,rKn*1.04f,rKn,PLEGC(yKnee+.012f*H0),J0,J1,0,0.35f); PRG(x,yKnee,c0,rKn,rKn*1.04f,rKn*.98f,PLEGC(yKnee),J0,J1,0,0.5f); PRG(x,yKnee-.014f*H0,c0,rKn*.98f,rKn,rKn,PLEGC(yKnee-.014f*H0),J1,J0,0,0.3f);
      { const float L=yKnee-yAnkle; for(int i=1;i<=4;i++){ const float u=(float)i/5.f,y=yKnee-L*u,r=u<.32f?pbLerp(rKn,rCalf,u/.32f):pbLerp(rCalf,rAnk,(u-.32f)/.68f); PRG(x,y,c0,r,r*.95f,r*1.1f*(u<.7f?1.f:.95f),PLEGC(y),J1,J2,0,pbSmooth(0.85f,1.f,u)*0.4f); } }
      PRG(x,yAnkle+.008f*H0,c0,rAnk,rAnk,rAnk,PC_SHOES,J1,J2,0,0.6f); PRG(x,yAnkle,-.002f*K,rAnk*1.04f,rAnk*1.1f,rAnk*1.05f,PC_SHOES,J2,J1,0,0.1f);
#undef PLEGC
      pbLoft(&M,R,n,UX,VZ,PB_SEG,1,1);
      /* a shoe, heel to toe */
      n=0; const float fl=.238f*K/1.05f,fh=yAnkle+.012f*K,sw=.044f*K*(0.8f+0.2f*mli); PRG(x,.45f*fh,-.27f*fl,sw*.60f,.42f*fh,.42f*fh,PC_SHOES,J2,J2,0,0.f); PRG(x,.50f*fh,-.20f*fl,sw*.80f,.50f*fh,.50f*fh,PC_SHOES,J2,J2,0,0.f); PRG(x,.62f*fh,0.f,sw*.9f,.62f*fh,.62f*fh,PC_SHOES,J2,J2,0,0.f); PRG(x,.50f*fh,.26f*fl,sw,.50f*fh,.50f*fh,PC_SHOES,J2,J3,0,0.5f); PRG(x,.34f*fh,.50f*fl,sw*1.02f,.34f*fh,.34f*fh,PC_SHOES,J3,J2,0,0.f); PRG(x,.26f*fh,.68f*fl,sw*.80f,.26f*fh,.26f*fh,PC_SHOES,J3,J3,0,0.f); PRG(x,.20f*fh,.74f*fl,sw*.45f,.17f*fh,.17f*fh,PC_SHOES,J3,J3,0,0.f); pbLoft(&M,R,n,UX,UY,PB_SEG-2,1,1); }
    /* the arms, along x in the T pose: sleeves to the elbow, to the wrist, or none */
    for(int s=0;s<2;s++){ const float sx=s?-1.f:1.f; const int o=s?PN_RSH-PN_LSH:0,J0=PN_LARM+o,J1=PN_LFORE+o,J2=PN_LHAND+o; const int sl=S.sleeves; const float yy=shJY; n=0; const float sleeveEnd=sl==0?0.f:(sl==1?.55f:1.f);
#define PACOL(u) (((u)<sleeveEnd)?PC_TOP:PC_SKIN)
      PRG(sx*(shJX-.05f*K),yy,0,rDel*.8f,rDel*.85f,rDel*.85f,sl?PC_TOP:PC_SKIN,J0,PN_SPINE1,0,0.5f); PRG(sx*shJX,yy+.005f*K,0,rDel,rDel,rDel,sl?PC_TOP:PC_SKIN,J0,PN_SPINE1,0,0.15f);
      for(int i=1;i<=3;i++){ const float u=(float)i/4.f,r=u<.5f?pbLerp(rDel,rUp,u*2.f):pbLerp(rUp,rElb,(u-.5f)*2.f); PRG(sx*(shJX+armU*u),yy,0,r,r,r,PACOL(u),J0,J1,0,pbSmooth(0.7f,1.f,u)*0.35f); }
      PRG(sx*(shJX+armU),yy,0,rElb,rElb,rElb,PACOL(1.f),J0,J1,0,0.5f); for(int i=1;i<=3;i++){ const float u=(float)i/4.f,r=u<.3f?pbLerp(rElb,rFore,u/.3f):pbLerp(rFore,rWr,(u-.3f)/.7f); PRG(sx*(shJX+armU+armF*u),yy,0,r,r,r,PACOL(1.f+u),J1,u<.5f?J0:J2,0,u<.5f?pbSmooth(0.5f,0.f,u)*0.4f:pbSmooth(.8f,1.f,u)*0.3f); }
#undef PACOL
      PRG(sx*(shJX+armU+armF),yy,0,rWr,rWr,rWr,PC_SKIN,J1,J2,0,0.5f); const float hx=shJX+armU+armF; PRG(sx*(hx+.22f*handL),yy,.004f*K,rWr*.72f,rWr*1.55f,rWr*1.55f,PC_SKIN,J2,J2,0,0.f); PRG(sx*(hx+.52f*handL),yy,.004f*K,rWr*.62f,rWr*1.72f,rWr*1.72f,PC_SKIN,J2,J2,0,0.f); PRG(sx*(hx+.82f*handL),yy,.002f*K,rWr*.46f,rWr*1.5f,rWr*1.5f,PC_SKIN,J2,J2,0,0.f); PRG(sx*(hx+1.f*handL),yy,0,rWr*.28f,rWr*.95f,rWr*.95f,PC_SKIN,J2,J2,0,0.f); pbLoft(&M,R,n,UY,VZ,PB_SEG-2,1,1);
      /* a thumb */
      n=0; PRG(sx*(hx+.14f*handL),yy-.002f*K,rWr*1.2f,rWr*.5f,rWr*.5f,rWr*.5f,PC_SKIN,J2,J2,0,0.f); PRG(sx*(hx+.30f*handL),yy-.006f*K,rWr*1.9f,rWr*.42f,rWr*.42f,rWr*.42f,PC_SKIN,J2,J2,0,0.f); PRG(sx*(hx+.46f*handL),yy-.008f*K,rWr*2.3f,rWr*.28f,rWr*.28f,rWr*.28f,PC_SKIN,J2,J2,0,0.f); pbLoft(&M,R,n,UY,VZ,6,1,1); }
    /* the head: a grid of its own, with the jaw's weight on what's below the lips */
    { static const float US[]={0.f,.02f,.05f,.09f,.125f,.15f,.17f,.185f,.2f,.215f,.23f,.25f,.275f,.3f,.33f,.36f,.4f,.44f,.48f,.52f,.56f,.6f,.66f,.72f,.78f,.84f,.9f,.95f,.98f,1.f}; const int nu=(int)(sizeof US/sizeof US[0]),nc=24; const int base=M.nv; const float ul=Hd.uLip;
      for(int i=0;i<nu;i++){ const float u0=US[i],u=u0<=.215f?u0*ul/.215f:ul+(u0-.215f)/(1.f-.215f)*(1.f-ul); for(int k=0;k<nc;k++){ const float th=6.28318f*(float)k/(float)nc; float p[3],nr[3]; phPoint(&Hd,u,th,p); phNormal(&Hd,u,th,nr); const float cs=cosf(th);
              const float tu=(p[0]/(Hd.bw*0.5f)*0.5f+0.5f); const float fu=pbClamp(tu,0.012f,0.988f),fv=cs>0.f?pbClamp(u/0.86f,0.006f,0.994f):0.994f; const float U=(fu*62.f+1.f)/128.f,V=(fv*62.f+1.f)/64.f;
              const float jw=(1.f-pbSmooth(ul-.006f,ul+.006f,u))*pbSmooth(-0.45f,0.15f,cs); pbVert(&M,p[0],p[1],p[2],nr[0],nr[1],nr[2],U,V,PN_HEAD,PN_JAW,jw); } }
      for(int i=0;i+1<nu;i++) for(int k=0;k<nc;k++){ const int k2=(k+1)%nc; pbQuad(&M,base+i*nc+k,base+i*nc+k2,base+(i+1)*nc+k2,base+(i+1)*nc+k); }
      /* ears */
      for(int s=0;s<2;s++){ const float sx=s?-1.f:1.f; float e[3]; phPoint(&Hd,Hd.uEye-.05f,sx*1.5708f,e); const float c[3]={e[0]+sx*.004f*K,e[1],e[2]-.01f*K}; pbBall(&M,c,.010f*K,.040f*hh*1.6f,.024f*hh*1.4f,PN_HEAD,PC_SKIN,4,6,0); } }
    /* the eyes, on their own bones, and the lids that come down over them */
    PMorphB mor[PB_MAXMORPH]; int nMor=0;
    unsigned*mv[4]; float*md[4]; for(int i=0;i<4;i++){ mv[i]=(unsigned*)malloc(sizeof(unsigned)*4000); md[i]=(float*)malloc(sizeof(float)*12000); }
    int nBl=0; for(int s=0;s<2;s++){ const float sx=s?-1.f:1.f; const float c[3]={sx*eL[0],eL[1],eL[2]}; pbBall(&M,c,er,er,er,s?PN_EYER:PN_EYEL,PC_EYE,8,12,1);
        float su,sv; pbCellUV(PC_SKIN,&su,&sv); const int nr=4,ncl=11,base=M.nv; const float Rl=er*1.07f,top=1.45f,restE=0.42f,shutE=-0.72f,hw=1.25f;
        for(int i=0;i<=nr;i++){ const float t=(float)i/(float)nr,ph=pbLerp(top,restE,t),ph2=pbLerp(top,shutE,t); for(int k=0;k<ncl;k++){ const float l=pbLerp(-hw,hw,(float)k/(float)(ncl-1)); const float dx=sinf(l)*cosf(ph),dy=sinf(ph),dz=cosf(l)*cosf(ph),dx2=sinf(l)*cosf(ph2),dy2=sinf(ph2),dz2=cosf(l)*cosf(ph2); const int v=pbVert(&M,c[0]+dx*Rl,c[1]+dy*Rl,c[2]+dz*Rl,dx,dy,dz,su,sv,PN_HEAD,PN_HEAD,0.f);
                if(nBl<4000){ mv[0][nBl]=(unsigned)v; md[0][nBl*3]=(dx2-dx)*Rl; md[0][nBl*3+1]=(dy2-dy)*Rl; md[0][nBl*3+2]=(dz2-dz)*Rl; nBl++; } } }
        for(int i=0;i<nr;i++) for(int k=0;k+1<ncl;k++) pbQuad(&M,base+i*ncl+k,base+i*ncl+k+1,base+(i+1)*ncl+k+1,base+(i+1)*ncl+k); }
    mor[nMor].name="blink"; mor[nMor].nD=nBl; mor[nMor].vi=mv[0]; mor[nMor].d=md[0]; nMor++;
    /* a smile and a brow: the head's own vertices, by how near they are to the corners and the brows. They've been written, so this reads them back */
    { int nS=0,nB=0; const float cx=mouthW,cy=lipC[1],cz=lipC[2],bx=Hd.eyeX,byy=yChin+0.61f*hh; for(int v=0;v<M.nv&&(nS<4000&&nB<4000);v++){ const float x=M.p[v*3],y=M.p[v*3+1],z=M.p[v*3+2]; if(z<Hd.zAxis) continue; if(y<yChin-.01f||y>H0) continue; const float ax=fabsf(x);
            { const float g=pbG(ax-cx,0.75f*mouthW)*pbG(y-cy,0.13f*hh)*pbG(z-cz,0.2f*hh); if(g>0.03f){ mv[1][nS]=(unsigned)v; md[1][nS*3]=(x>0.f?1.f:-1.f)*g*.022f*hh; md[1][nS*3+1]=g*.055f*hh; md[1][nS*3+2]=-g*.012f*hh; nS++; } }
            { const float g=pbG(ax-bx,0.9f*Hd.bw*0.5f)*pbG(y-byy,0.085f*hh); if(g>0.03f&&y>eL[1]+er*0.9f){ mv[2][nB]=(unsigned)v; md[2][nB*3]=0.f; md[2][nB*3+1]=g*.05f*hh; md[2][nB*3+2]=0.f; nB++; } } }
      mor[nMor].name="smile"; mor[nMor].nD=nS; mor[nMor].vi=mv[1]; mor[nMor].d=md[1]; nMor++; mor[nMor].name="brow"; mor[nMor].nD=nB; mor[nMor].vi=mv[2]; mor[nMor].d=md[2]; nMor++; }
    /* hair: a shell a little off the head, a line round the face and the nape, and whatever hangs from it */
    { const int style=S.hairStyle; const float thick=(.022f+.02f*pbClamp(S.hairLength,0.f,1.f))*hh; if(style>0){ const int nc=24,nr=8,base=M.nv; float cu,cv; pbCellUV(PC_HAIR,&cu,&cv); const float nape=(style==1||style==4)?0.34f:(style==2?.28f:.42f);
          for(int r=0;r<=nr;r++){ const float v=(float)r/(float)nr; for(int k=0;k<nc;k++){ const float th=6.28318f*(float)k/(float)nc,cs=cosf(th),sn=fabsf(sinf(th)); const float hl=(cs>0.f?pbLerp(0.68f,0.84f,cs*cs):pbLerp(0.68f,nape,cs*cs))-0.02f*sn; const float u=pbLerp(hl,1.f,v); float p[3],nr2[3]; phPoint(&Hd,u,th,p); phNormal(&Hd,u,th,nr2); const float l=sqrtf(nr2[0]*nr2[0]+nr2[1]*nr2[1]+nr2[2]*nr2[2]); const float t=thick*(0.25f+0.75f*pbSmooth(0.f,0.28f,v)); if(l>1e-6f) for(int q=0;q<3;q++) p[q]+=nr2[q]/l*t; pbVert(&M,p[0],p[1],p[2],nr2[0],nr2[1],nr2[2],cu,cv,PN_HEAD,PN_HEAD,0.f); } }
          for(int r=0;r<nr;r++) for(int k=0;k<nc;k++){ const int k2=(k+1)%nc; pbQuad(&M,base+r*nc+k,base+r*nc+k2,base+(r+1)*nc+k2,base+(r+1)*nc+k); }
          /* a curtain of it down the back and sides: to the jaw or the shoulders */
          if(style==2||style==4){ const float len=(style==4?0.55f*hh:(0.95f+1.0f*pbClamp(S.hairLength,0.f,2.f))*hh); const int nC=13,nR=6,b2=M.nv; for(int r=0;r<=nR;r++){ const float t=(float)r/(float)nR; for(int k=0;k<nC;k++){ const float th=pbLerp(1.15f,5.13f,(float)k/(float)(nC-1)); float p[3],nn[3]; phPoint(&Hd,0.42f,th,p); phNormal(&Hd,0.42f,th,nn); const float l=sqrtf(nn[0]*nn[0]+nn[1]*nn[1]+nn[2]*nn[2]); if(l>1e-6f) for(int q=0;q<3;q++){ nn[q]/=l; p[q]+=nn[q]*thick*(1.1f+0.4f*t); } const float w1=pbSmooth(0.35f,1.f,t); pbVert(&M,p[0]*(1.f+.10f*t),p[1]-len*t*(1.f-(float)0),p[2]-.012f*K*t,nn[0],nn[1],nn[2],cu,cv,PN_HEAD,PN_NECK,w1*.5f); } }
              for(int r=0;r<nR;r++) for(int k=0;k+1<nC;k++) pbQuad(&M,b2+r*nC+k,b2+r*nC+k+1,b2+(r+1)*nC+k+1,b2+(r+1)*nC+k); }
          /* a ponytail: three bones whose names make them swing */
          if(style==3){ const float rp=.030f*K; n=0; PRG(W[PN_PONY1][0],W[PN_PONY1][1],W[PN_PONY1][2]+.012f*K,rp*.8f,rp*.8f,rp*.8f,PC_HAIR,PN_PONY1,PN_HEAD,0,0.3f); PRG(W[PN_PONY1][0],W[PN_PONY1][1]-.02f*K,W[PN_PONY1][2],rp,rp,rp,PC_HAIR,PN_PONY1,PN_PONY1,0,0.f); PRG(W[PN_PONY2][0],W[PN_PONY2][1]+.03f*K,W[PN_PONY2][2],rp*.95f,rp*.95f,rp*.95f,PC_HAIR,PN_PONY1,PN_PONY2,0,0.5f); PRG(W[PN_PONY2][0],W[PN_PONY2][1]-.04f*K,W[PN_PONY2][2],rp*.82f,rp*.82f,rp*.82f,PC_HAIR,PN_PONY2,PN_PONY3,0,0.2f); PRG(W[PN_PONY3][0],W[PN_PONY3][1]+.02f*K,W[PN_PONY3][2],rp*.5f,rp*.5f,rp*.5f,PC_HAIR,PN_PONY3,PN_PONY3,0,0.f); PRG(W[PN_PONY3][0],W[PN_PONY3][1]-.01f*K,W[PN_PONY3][2],rp*.2f,rp*.2f,rp*.2f,PC_HAIR,PN_PONY3,PN_PONY3,0,0.f); pbLoft(&M,R,n,UX,VZ,8,1,1); } } }
#undef PRG
    /* the inside of the mouth: dark, with two rows of teeth, in a mesh of its own so the hole between the lips doesn't cut it */
    { const float cz=lip[2]-.34f*hh,yl=lip[1],rx=mouthW*1.05f,ry=.12f*hh,rz=.22f*hh; float du,dv; pbCellUV(PC_DARK,&du,&dv); const int nA=8,nB=12,base=MI.nv; for(int a=0;a<=nA;a++){ const float ph=1.5708f-3.14159f*(float)a/(float)nA; for(int b=0;b<nB;b++){ const float lm=6.28318f*(float)b/(float)nB; const float dx=sinf(lm)*cosf(ph),dy=sinf(ph),dz=cosf(lm)*cosf(ph); const float py=yl+dy*ry; const int jaw=py<yl-.0015f; pbVert(&MI,dx*rx,py,cz+dz*rz,-dx,-dy,-dz,du,dv,jaw?PN_JAW:PN_HEAD,jaw?PN_JAW:PN_HEAD,0.f); } }
      for(int a=0;a<nA;a++) for(int b=0;b<nB;b++){ const int b2=(b+1)%nB; pbQuad(&MI,base+a*nB+b,base+a*nB+b2,base+(a+1)*nB+b2,base+(a+1)*nB+b); }
      float tu,tv; pbCellUV(PC_TEETH,&tu,&tv); for(int row=0;row<2;row++){ const int bone=row?PN_JAW:PN_HEAD,nC=9,b0=MI.nv; const float y0=row?yl-.075f*hh:yl+.012f*hh,y1=row?yl-.012f*hh:yl+.075f*hh; for(int r=0;r<2;r++) for(int k=0;k<nC;k++){ const float l=pbLerp(-1.15f,1.15f,(float)k/(float)(nC-1)); pbVert(&MI,sinf(l)*mouthW*.92f,r?y1:y0,lip[2]-.095f*hh-(1.f-cosf(l))*.14f*hh,sinf(l),0.f,cosf(l),tu,tv,bone,bone,0.f); } for(int k=0;k+1<nC;k++) pbQuad(&MI,b0+k,b0+k+1,b0+nC+k+1,b0+nC+k); } }
    /* ---- now the file ---- */
    PBuf B; memset(&B,0,sizeof B); B.cap=(size_t)M.nv*48u+(size_t)M.ni*4u+65536u; B.b=(unsigned char*)malloc(B.cap); B.n=68;
    const size_t offNodes=pbPut(&B,NULL,(size_t)PN_N*52u),offMeshes=pbPut(&B,NULL,120u),offMats=pbPut(&B,NULL,72u),offClips=pbPut(&B,NULL,60u),offTex=pbPut(&B,NULL,16u);
    /* the strings: nodes, then the clips */
    { unsigned char*bt=B.b; (void)bt; for(int i=0;i<PN_N;i++){ unsigned char*o=B.b+offNodes+(size_t)i*52u; pbPutU(o,(unsigned)i); int par=PN_PARENT[i]; memcpy(o+4,&par,4); const unsigned kind=(i>=1&&i<=PN_BONES)?1u:((i==PN_BODY||i==PN_INNER)?2u:0u); pbPutU(o+8,kind);
            float tq[10]={0,0,0,0,0,0,1,1,1,1}; if(par>=0){ tq[0]=W[i][0]-W[par][0]; tq[1]=W[i][1]-W[par][1]; tq[2]=W[i][2]-W[par][2]; } memcpy(o+12,tq,40); } }
    float ibm[PN_BONES*16]; unsigned short bm[PN_BONES]; for(int b=0;b<PN_BONES;b++){ const float*w=W[b+1]; bm[b]=(unsigned short)(b+1); static const float I[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; memcpy(ibm+b*16,I,64); ibm[b*16+12]=-w[0]; ibm[b*16+13]=-w[1]; ibm[b*16+14]=-w[2]; }
    { static const float I[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; for(int mi=0;mi<2;mi++){ const PMeshB*Q=Ms[mi]; size_t oP=pbPut(&B,Q->p,(size_t)Q->nv*12u),oN=pbPut(&B,Q->n,(size_t)Q->nv*12u),oT=pbPut(&B,Q->t,(size_t)Q->nv*8u),oJ=pbPut(&B,Q->j,(size_t)Q->nv*4u),oW=pbPut(&B,Q->w,(size_t)Q->nv*4u),oI=pbPut(&B,Q->ix,(size_t)Q->ni*4u),oM=pbPut(&B,bm,sizeof bm),oB=pbPut(&B,ibm,sizeof ibm),oX=pbPut(&B,I,64u); unsigned rec[15]={(unsigned)(mi?PN_INNER:PN_BODY),(unsigned)(mi?PN_INNER:PN_BODY),(unsigned)Q->nv,(unsigned)Q->ni,(unsigned)PN_BONES,7u,(unsigned)oP,(unsigned)oN,(unsigned)oT,(unsigned)oJ,(unsigned)oW,(unsigned)oI,(unsigned)oM,(unsigned)oB,(unsigned)oX}; memcpy(B.b+offMeshes+(size_t)mi*60u,rec,60); unsigned char*m=B.b+offMats+(size_t)mi*36u; memset(m,0,36); pbPutU(m,(unsigned)mi); int tex=0,nt=0; memcpy(m+24,&tex,4); memcpy(m+28,&nt,4); } }
    /* the texture */
    { unsigned char*px=(unsigned char*)malloc(128u*64u*4u),*png=(unsigned char*)malloc(128u*64u*4u+70000u); pbPaint(px,&S,&Hd,col); const size_t n2=pbPng(png,px,128,64); const size_t oT=pbPut(&B,png,n2); unsigned char*t=B.b+offTex; memset(t,0,16); pbPutU(t+8,(unsigned)oT); pbPutU(t+12,(unsigned)n2); free(px); free(png); }
    /* the clips */
    for(int c=0;c<3;c++){ const PClipDef*C=&PB_CLIPS[c]; const int nf=C->frames+1; float*dq=(float*)malloc(sizeof(float)*(size_t)nf*(9u*4u+3u)); const float hipsY=W[PN_HIPS][1];
        for(int f=0;f<nf;f++){ const float u=(float)(f%C->frames)/(float)C->frames; float Q[9][4],t[3]; pbClipFrame(C,u,hipsY,K,Q,t); for(int k=0;k<9;k++) memcpy(dq+(size_t)k*(size_t)nf*4u+(size_t)f*4u,Q[k],16); memcpy(dq+(size_t)9*(size_t)nf*4u+(size_t)f*3u,t,12); }
        unsigned tr[PB_TRACKS*3]; for(int k=0;k<9;k++){ const size_t o=pbPut(&B,dq+(size_t)k*(size_t)nf*4u,(size_t)nf*16u); tr[k*3]=(unsigned)PB_TRACKN[k]; tr[k*3+1]=0u; tr[k*3+2]=(unsigned)o; } { const size_t o=pbPut(&B,dq+(size_t)9*(size_t)nf*4u,(size_t)nf*12u); tr[27]=(unsigned)PN_HIPS; tr[28]=1u; tr[29]=(unsigned)o; } free(dq);
        const size_t oTr=pbPut(&B,tr,sizeof tr); unsigned char*q=B.b+offClips+(size_t)c*20u; pbPutU(q,(unsigned)(PN_N+c)); const float fps=30.f; memcpy(q+4,&fps,4); pbPutU(q+8,(unsigned)nf); pbPutU(q+12,(unsigned)PB_TRACKS); pbPutU(q+16,(unsigned)oTr); }
    /* the shapes */
    size_t offMorph=0; { unsigned tab[1+PB_MAXMORPH*4]; tab[0]=(unsigned)nMor; for(int i=0;i<nMor;i++){ unsigned char*sd=(unsigned char*)malloc((size_t)mor[i].nD*16u); for(int k=0;k<mor[i].nD;k++){ memcpy(sd+(size_t)k*16u,&mor[i].vi[k],4); memcpy(sd+(size_t)k*16u+4,&mor[i].d[k*3],12); } const size_t o=pbPut(&B,sd,(size_t)mor[i].nD*16u); free(sd); tab[1+i*4]=0u; tab[2+i*4]=(unsigned)(PN_N+3+i); tab[3+i*4]=(unsigned)mor[i].nD; tab[4+i*4]=(unsigned)o; } offMorph=pbPut(&B,tab,(size_t)(1+nMor*4)*4u); }
    /* the size block, and the string table */
    float szb[12]; memset(szb,0,sizeof szb); szb[9]=H0; szb[10]=1.f; szb[7]=0.f; const size_t offSize=pbPut(&B,szb,sizeof szb);
    { int nStr=PN_N+3+nMor; size_t textN=0; const char*str[PN_N+3+PB_MAXMORPH]; for(int i=0;i<PN_N;i++) str[i]=PN_NAME[i]; for(int i=0;i<3;i++) str[PN_N+i]=PB_CLIPS[i].name; for(int i=0;i<nMor;i++) str[PN_N+3+i]=mor[i].name; for(int i=0;i<nStr;i++) textN+=strlen(str[i]);
      unsigned char*tb=(unsigned char*)malloc(4u+(size_t)nStr*8u+textN); pbPutU(tb,(unsigned)nStr); size_t off=0; for(int i=0;i<nStr;i++){ const size_t l=strlen(str[i]); pbPutU(tb+4u+(size_t)i*8u,(unsigned)off); pbPutU(tb+8u+(size_t)i*8u,(unsigned)l); memcpy(tb+4u+(size_t)nStr*8u+off,str[i],l); off+=l; } const size_t offStr=pbPut(&B,tb,4u+(size_t)nStr*8u+textN); free(tb);
      unsigned h[15]={2u,(unsigned)PN_N,2u,2u,3u,1u,(unsigned)offNodes,(unsigned)offMeshes,(unsigned)offMats,(unsigned)offClips,(unsigned)offTex,(unsigned)offStr,(unsigned)offSize,(unsigned)B.n,(unsigned)offMorph}; memcpy(B.b,"RPK1",4); memcpy(B.b+4,h,60); }
    for(int i=0;i<2;i++){ free(Ms[i]->p); free(Ms[i]->n); free(Ms[i]->t); free(Ms[i]->j); free(Ms[i]->w); free(Ms[i]->ix); } for(int i=0;i<4;i++){ free(mv[i]); free(md[i]); }
    *len=B.n; return B.b; }
#undef PW
