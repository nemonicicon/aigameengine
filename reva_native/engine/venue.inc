/* ---- THE DANCE FLOOR AS DATA (11-02, engine plan Phase 2) --------------------------------------------------------------------------------
 * The old road drew the floor as one quad under a large per-pixel shader. In the packet it is what that shader worked out, per tile, on the CPU: the same iterated sin-warp, flashes and families, and the
 * light pools sampled at each tile's middle. A pixel of it then costs next to nothing, which is the lever on the tube. Only tiles the lens can see, near enough to read. */
#define VENUE_TILES 6000
static void venueFloor(const float*vp,const float*eye,float beat,const float*flp,const float*flc){ if(WORLD_OFF) return;
    static float v[VENUE_TILES*6*9]; int n=0;
    float pl[6][4];
    for(int r=0;r<4;r++){ pl[0][r]=vp[r*4+3]+vp[r*4+0]; pl[1][r]=vp[r*4+3]-vp[r*4+0]; pl[2][r]=vp[r*4+3]+vp[r*4+1];
                          pl[3][r]=vp[r*4+3]-vp[r*4+1]; pl[4][r]=vp[r*4+3]+vp[r*4+2]; pl[5][r]=vp[r*4+3]-vp[r*4+2]; }
    int tx0=(int)floorf(-FLOOR_X/2.f),tx1=(int)floorf(FLOOR_X/2.f),tz0=(int)floorf(FLOOR_ZN/2.f),tz1=(int)floorf(0.5f/2.f);
    float t2=beat*0.11f;
    for(int tz=tz0;tz<=tz1;tz++) for(int tx=tx0;tx<=tx1&&n<VENUE_TILES*6;tx++){
        float cx=((float)tx+0.5f)*2.f,cz=((float)tz+0.5f)*2.f,dx=cx-eye[0],dz=cz-eye[2];
        if(dx*dx+dz*dz>19600.f) continue;
        int in=1;
        for(int k=0;k<6&&in;k++){ float l=sqrtf(pl[k][0]*pl[k][0]+pl[k][1]*pl[k][1]+pl[k][2]*pl[k][2]); if(pl[k][0]*cx+pl[k][2]*cz+pl[k][3]<-1.5f*l) in=0; }
        if(!in) continue;
        float s9=sinf((float)tx*12.9898f+(float)tz*78.233f)*43758.5453f, h=s9-floorf(s9);
        float qx=(float)tx*0.23f,qz=(float)tz*0.23f;
        for(int i=0;i<3;i++){ float ax=(qx*0.8f-qz*0.6f)*1.25f+sinf(qz*0.9f+t2), az=(qx*0.6f+qz*0.8f)*1.25f+sinf(qx*0.9f+t2); qx=ax; qz=az; }
        float field=0.5f+0.5f*sinf(qx+qz);
        float ph2=beat*0.125f+h, fl9=ph2-floorf(ph2); float fi9=1.f-fl9; fi9*=fi9; float flash=fi9*fi9*fi9;   /* (1-fract)^6 */
        float fam=h*3.7f; fam-=floorf(fam);
        float br,bg,bb; if(fam<0.6f){ br=0.52f; bg=0.20f; bb=0.92f; } else if(fam<0.9f){ br=0.16f; bg=0.80f; bb=0.34f; } else { br=1.f; bg=0.22f; bb=0.72f; }
        float s7=h*7.31f+floorf(ph2)*0.618f; float shade=0.55f+0.65f*(s7-floorf(s7));
        float lum=0.13f+0.11f*field+0.80f*flash;
        float f9=flash*1.35f; if(f9>1.f) f9=1.f; float aa=0.30f+0.66f*f9;
        float pr=0.f,pg=0.f,pb=0.f;
        for(int i=0;i<4;i++){ float ex=cx-flp[i*3],ez=cz-flp[i*3+2],e9=expf(-(ex*ex+ez*ez)*0.012f)*0.5f; pr+=flc[i*3]*e9; pg+=flc[i*3+1]*e9; pb+=flc[i*3+2]*e9; }
        float pa=(pr+pg+pb)*0.6f; if(pa>1.f) pa=1.f; if(pa>aa) aa=pa;
        float r=br*shade*lum+pr,g=bg*shade*lum+pg,b=bb*shade*lum+pb;
        float x0=cx-1.f,x1=cx+1.f,z0=cz-1.f,z1=cz+1.f;
        if(z1>0.5f) z1=0.5f;
        if(z0<FLOOR_ZN) z0=FLOOR_ZN;
        const float qd[6][2]={{x0,z0},{x1,z0},{x1,z1},{x0,z0},{x1,z1},{x0,z1}};
        for(int k=0;k<6;k++){ float*o=v+n*9; n++; o[0]=qd[k][0]; o[1]=0.004f; o[2]=qd[k][1]; o[3]=r; o[4]=g; o[5]=b; o[6]=aa; o[7]=0.f; o[8]=0.f; } }
    gfxFxTris(v,n,GFX_FX_ALPHA,1); }
/* the blob shadows' list as the GL block builds it: x y z, u v, how dark */
static void venueBlobs(const float*shv,int quads){ if(WORLD_OFF) return;
    { static int no9=-1; if(no9<0){ const char*e9=getenv("REVA_NOBLOBS"); no9=(e9&&e9[0]=='1')?1:0; } if(no9) return; }   /* (11-07) REVA_NOBLOBS=1: none, for a test */
    static float v[(1024+1)*6*9];
    for(int i=0;i<quads*6;i++){ const float*s=shv+i*6; float*o=v+i*9; o[0]=s[0]; o[1]=s[1]; o[2]=s[2]; o[3]=o[4]=o[5]=0.f; o[6]=s[5]; o[7]=s[3]; o[8]=s[4]; }
    gfxFxTris(v,quads*6,GFX_FX_SOFT,1); }
/* a ribbon given as a triangle strip of x y z r g b, into added triangles */
static void venueStrip(const float*s,int verts,float alpha){ if(WORLD_OFF) return;
    static float v[1024*6*9]; int n=0;
    for(int i=0;i+2<verts&&n<1024*6-3;i++){ for(int k=0;k<3;k++){ const float*q=s+(i+k)*6; float*o=v+n*9; n++; memcpy(o,q,24); o[6]=alpha; o[7]=0.f; o[8]=0.f; } }
    gfxFxTris(v,n,GFX_FX_ADD,1); }
/* a line strip of x y z r g b, into the packet's line pairs */
static void venueLineStrip(const float*s,int verts){ if(WORLD_OFF) return;
    static float v[4096*6]; int n=0;
    for(int i=0;i+1<verts&&n<4094;i++){ memcpy(v+n*6,s+i*6,24); n++; memcpy(v+n*6,s+(i+1)*6,24); n++; }
    gfxLines3D(v,n); }
/* the lasers: a beam's quad through its matrix, brightest at the rig, gone at the far end */
static float g_lasV[14*6*9]; static int g_lasN=0;
static void venueLaser(const float*m,float r,float g,float b,float amp){ if(WORLD_OFF) return;
    static const float q[6][3]={{-0.05f,0.f,0.f},{0.05f,0.f,0.f},{0.16f,-30.f,0.f},{-0.05f,0.f,0.f},{0.16f,-30.f,0.f},{-0.16f,-30.f,0.f}};
    for(int i=0;i<6&&g_lasN<14*6;i++){ float*o=g_lasV+g_lasN*9; g_lasN++; float x=q[i][0],y=q[i][1],vy=-y/30.f,k=amp*(1.f-vy*0.75f);
        o[0]=m[0]*x+m[4]*y+m[12]; o[1]=m[1]*x+m[5]*y+m[13]; o[2]=m[2]*x+m[6]*y+m[14]; o[3]=r*k; o[4]=g*k; o[5]=b*k; o[6]=0.5f*(1.f-vy); o[7]=0.f; o[8]=0.f; } }
static void venueLasersEnd(void){ if(WORLD_OFF) return; gfxFxTris(g_lasV,g_lasN,GFX_FX_ADD,0); g_lasN=0; }
