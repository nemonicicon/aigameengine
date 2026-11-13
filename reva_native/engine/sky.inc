/* ======================== WATER =====================================================
 * Real water physics: an engine without overhead it doesn't need has room
 * for systems that used to be too heavy.
 *
 * ⭐ A POOL DOES NOT WANT PARTICLES. SPH or FLIP would be slower, far harder, and would
 * look WORSE at this scale, because a pool is a SURFACE -- you never see inside the volume.
 * What a surface wants is a HEIGHT FIELD, and that is a real simulation and not a sine
 * wave: every cell carries a height and a vertical velocity and each step solves the 2D
 * wave equation against its neighbours.
 *
 *     a  = (mean of my four neighbours - me) * stiffness
 *     v += a*dt ;   v *= damping ;   h += v*dt
 *
 * Ripples PROPAGATE at a real speed, REFLECT off the walls, INTERFERE, and die away.
 * Nothing is keyframed and nothing loops: drop something in one corner and the far corner
 * finds out a moment later. That is the whole difference between water and a wobbling
 * texture.
 *
 * ⛔ AND IT IS A HEIGHT FIELD, so it cannot splash above itself, break into droplets or
 * pour. Written down so the limit is known rather than discovered. */
#define WAT_MAX 4
#define WAT_G   48
#define WAT_N   (WAT_G*WAT_G)
typedef struct {
    int used;
    float x, z, w, d, y;               /* centre, footprint, rest height */
    float h[WAT_N], v[WAT_N];
    float pos[WAT_N][3], nrm[WAT_N][3];
    float tint[3];
    unsigned vb, nb, ib, jb, wb, ib32; unsigned nIdx; int gl;
} Water;
static Water g_wat[WAT_MAX]; static int g_nWat=0;
/* the scene pass's view-projection, published for the frustum cull (objects.inc, shadow.inc) and the water */
static float g_watVP[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
static int g_cullOn=1;   /* ⭐ the set's frustum cull (09-11); ctl cull=0 for the A/B */
static int g_cullEnvDone=0;   /* REVA_NOCULL=1: the same A/B in a door, which never reads the lab's ctl file (11-01) */





static void watAdd(float x,float y,float z,float w,float d,const float*rgb){
    if(g_nWat>=WAT_MAX) return;
    Water*W=&g_wat[g_nWat];
    memset(W,0,sizeof *W);
    W->used=1; W->x=x; W->y=y; W->z=z; W->w=w; W->d=d;
    for(int k=0;k<3;k++) W->tint[k]=rgb[k];
    g_nWat++;
    printf("[water] pool %d: %gx%g at %.2f,%.2f,%.2f  %dx%d cells%c",
           g_nWat-1,w,d,x,y,z,WAT_G,WAT_G,10);
    fflush(stdout); }

/* ⭐ SOMETHING IS IN THE WATER. Any body drawn inside a pool's footprint pushes the
 * surface down; the sim does not care what moved, so wading, falling in and swimming all
 * make their own wake with no special case for any of them. */
static void waterDisturb(float wx,float wy,float wz,float rad,float amt){
    for(int q=0;q<g_nWat;q++){
        Water*W=&g_wat[q];
        if(!W->used) continue;
        if(wy>W->y+2.0f||wy<W->y-3.0f) continue;              /* not at the surface */
        float hw=W->w*0.5f, hd=W->d*0.5f;
        if(wx<W->x-hw-rad||wx>W->x+hw+rad) continue;
        if(wz<W->z-hd-rad||wz>W->z+hd+rad) continue;
        for(int r=0;r<WAT_G;r++) for(int c=0;c<WAT_G;c++){
            float px=W->x-hw+W->w*(float)c/(float)(WAT_G-1);
            float pz=W->z-hd+W->d*(float)r/(float)(WAT_G-1);
            float dx=px-wx, dz=pz-wz, dd=sqrtf(dx*dx+dz*dz);
            if(dd>=rad) continue;
            float f=1.f-dd/rad;
            /* ⛔ A BODY DISPLACES WATER TO A DEPTH -- IT DOES NOT ACCELERATE IT DOWNWARD.
             * Adding a fixed downward velocity every frame does not displace, it DIGS: the
             * first pool came back with a crater that followed the wader around, deep
             * enough to show the DECK through the water, and capping the velocity did not
             * help because the HEIGHT still ran away to the clamp.
             * ⭐ So push toward a shallow TARGET instead: the surface settles a little low
             * under her, springs back when she leaves, and the transient on the way in and
             * out is what launches the ripples. Which is the whole point -- what reads as
             * water is the rings LEAVING, not the dent staying. */
            int i9=r*WAT_G+c;
            float target=-amt*16.f*f*f;
            W->v[i9]+=(target-W->h[i9])*amt*46.f; } } }

static float g_watT=0.f;
static void watStep(Water*W,float dt){
    if(dt>0.033f) dt=0.033f;
    if(dt<1e-4f) dt=1e-4f;
    const float STIFF=42.f, DAMP=0.992f;
    static float acc[WAT_N];
    /* ⭐ WIND MAKES WAVES, and that is not decoration -- a pool is never mirror-flat, and a
     * mirror-flat pool is exactly what read as blue plastic. This is FORCING, not a
     * painted-on sine: the pressure goes into the same velocity field, so the waves it
     * raises then propagate, reflect off the walls and interfere with the rings a body
     * makes, like everything else in here. No wind line in the set = a still pool. */
    float wsp=sqrtf(g_windV[0]*g_windV[0]+g_windV[2]*g_windV[2]);
    if(wsp>1e-3f){
        float wx=g_windV[0]/wsp, wz=g_windV[2]/wsp;
        /* ⛔ 0.055 HERE WAS AN OCEAN IN A STORM -- metre-high swell in a swimming pool,
         * slamming into the height clamp so the surface came back as a mountain range.
         * A pool ripples in millimetres. */
        float amp=0.0022f*wsp*dt*60.f, hw=W->w*0.5f, hd=W->d*0.5f;
        for(int r=0;r<WAT_G;r++) for(int c=0;c<WAT_G;c++){
            float px=W->x-hw+W->w*(float)c/(float)(WAT_G-1);
            float pz=W->z-hd+W->d*(float)r/(float)(WAT_G-1);
            float ph=(px*wx+pz*wz)*3.1f-g_watT*4.4f;
            W->v[r*WAT_G+c]+=amp*(sinf(ph)+0.55f*sinf(ph*2.17f+1.3f)); } }
    for(int r=0;r<WAT_G;r++) for(int c=0;c<WAT_G;c++){
        int i=r*WAT_G+c;
        /* ⛔ CLAMPED NEIGHBOURS ARE A WALL, and that is exactly right for a pool: a wave
         * that runs out of grid reflects instead of vanishing, which is what the tiled
         * side of a pool does to it. */
        int l=(c>0)?i-1:i, rr=(c<WAT_G-1)?i+1:i;
        int u=(r>0)?i-WAT_G:i, dn=(r<WAT_G-1)?i+WAT_G:i;
        float avg=(W->h[l]+W->h[rr]+W->h[u]+W->h[dn])*0.25f;
        acc[i]=(avg-W->h[i])*STIFF; }
    for(int i=0;i<WAT_N;i++){
        W->v[i]=(W->v[i]+acc[i]*dt)*DAMP;
        W->h[i]+=W->v[i]*dt;
        /* a pool is shallow: a wave taller than this is a wave that has left the pool */
        if(W->h[i]> 0.22f){ W->h[i]= 0.22f; W->v[i]*=-0.4f; }
        if(W->h[i]<-0.22f){ W->h[i]=-0.22f; W->v[i]*=-0.4f; } }
    float hw=W->w*0.5f, hd=W->d*0.5f;
    for(int r=0;r<WAT_G;r++) for(int c=0;c<WAT_G;c++){
        int i=r*WAT_G+c;
        W->pos[i][0]=W->x-hw+W->w*(float)c/(float)(WAT_G-1);
        W->pos[i][1]=W->y+W->h[i];
        W->pos[i][2]=W->z-hd+W->d*(float)r/(float)(WAT_G-1); }
    float sx=W->w/(float)(WAT_G-1), sz=W->d/(float)(WAT_G-1);
    for(int r=0;r<WAT_G;r++) for(int c=0;c<WAT_G;c++){
        int i=r*WAT_G+c;
        int l=(c>0)?i-1:i, rr=(c<WAT_G-1)?i+1:i;
        int u=(r>0)?i-WAT_G:i, dn=(r<WAT_G-1)?i+WAT_G:i;
        /* the gradient IS the normal -- this is what makes light travel across a ripple */
        float gx=(W->h[rr]-W->h[l])/(2.f*sx), gz=(W->h[dn]-W->h[u])/(2.f*sz);
        float n[3]={-gx,1.f,-gz};
        float L=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        for(int k=0;k<3;k++) W->nrm[i][k]=n[k]/L; } }


/* ---- SPACE (10-22, THE WORLD AT THE END OF TIME: the New Mayflower) --------------------------------------------------
 * A set that is a place in deep space says `space`, and its sky is BLACK with the stars drawn at infinity: procedural,
 * tens of thousands, most faint, "from firebox red to sapphire and white" (the book, p23), dense in a Milky Way haze
 * along a great circle (`milkyway nx ny nz strength`, the band's pole), and up to eight NAMED stars (`star dx dy dz r g b
 * size_deg [halo]` -- the flare star, the destination), each a core and a glow. A ship at its true size outruns a lens's
 * 900 u, so `far <u>` lets a space set be seen whole. A set may carry its own light too: `sun elev azim I [r g b]` and
 * `sky r g b`, applied as it loads (the ctl line's own sun=/sky= still win).
 * ⛔ NOT the day sky with its colours turned down: that sky is a gradient with a CLOUD DECK, and it drew grey clouds
 * behind the ship in deep space. */
static int g_setSpace=0; static float g_setFar=0.f;
static float g_skyYaw=0.f, g_skyPitch=0.f;   /* (11-10) the space sky turned, for a ship that turns under its crew (E->skyTurn): radians, and both 0 is the sky as the set has it */
static float g_starDir[8][4], g_starCol[8][4]; static int g_nStar=0;
static float g_starFl[8][3];   /* (10-22) a named star's flare: delay, rise, peak (peak 0 = none) */
static float g_milky[4]={0.f,1.f,0.f,0.f};
/* what deep space needs from the set this frame: 8 stars with their flares worked in, 8 colours, the milky way, how many and how big a pixel is */
static void spaceFill(float*o,int H){ float sd9[8][4],sc9[8][4]; memcpy(sd9,g_starDir,sizeof sd9); memcpy(sc9,g_starCol,sizeof sc9); static const float BW9[3]={0.86f,0.93f,1.0f};
    for(int i=0;i<g_nStar&&i<8;i++) if(g_starFl[i][2]>0.f){ float e=(g_setT-g_starFl[i][0])/fmaxf(g_starFl[i][1],0.01f); e=e<0.f?0.f:(e>1.f?1.f:e); e=e*e*(3.f-2.f*e); const float pk=1.f+(g_starFl[i][2]-1.f)*e; for(int k=0;k<3;k++) sc9[i][k]=(g_starCol[i][k]*(1.f-e)+BW9[k]*e)*pk; sc9[i][3]=g_starCol[i][3]*(1.f+2.f*e); sd9[i][3]=g_starDir[i][3]*(1.f+1.5f*e); }
    memcpy(o,sd9,sizeof sd9); memcpy(o+32,sc9,sizeof sc9); memcpy(o+64,g_milky,16); o[68]=(float)g_nStar; const float fov9=(g_camOn&&g_camFov>0.f)?g_camFov:42.f; o[69]=fov9*0.0174533f/(float)(H>0?H:720); o[70]=g_skyYaw; o[71]=g_skyPitch; }   /* (11-10) the last two were spare: the sky's turn, which shaders/space.frag undoes on each ray */

/* ---- BEAMS (10-22, THE WORLD AT THE END OF TIME: "the haze of ions streaming from the drive jets fuzzed everything.
 * Including the star they were heading for", p24) ------------------------------------------------------------------------
 * `beam x y z dx dy dz length r0 r1 r g b k [fade [thin]]` (u) in a set: a glowing VOLUME along a line -- a drive's ion plume, a
 * light shaft, a projector's throw. Its density is a gaussian tube whose radius runs r0 -> r1 along its length, thinned
 * as it spreads ((r0/r)^thin: 2, the default, is the same flux through a wider tube; less keeps a far plume visible)
 * and faded by (1-s/L)^fade (default 1); drawn ADDITIVE
 * after every opaque thing, so it lights what is behind it and never darkens it. Each pixel INTEGRATES the density
 * along its own ray (between the ray's entry and exit of the tube's bound, in steps sized to the plume where they
 * are; k per u of it, saturating as 1-exp), so it reads as a volume from any side -- and a lens looking DOWN the plume sees it gather into its vanishing
 * point, which is the book's shot. The bound is a box drawn by its BACK faces, so a lens inside the plume still sees
 * it, and a solid thing between the lens and the box's far side hides the beam along that ray.
 * ⛔ A solid thing INSIDE the box hides ALL of the beam along its rays, the part in front of it too: the march
 * doesn't read the depth of what's in front, so it cannot stop at the surface. Up to 8. */
#define BEAM_MAX 8
static float g_beamA[BEAM_MAX][3], g_beamU[BEAM_MAX][3], g_beamL[BEAM_MAX], g_beamR[BEAM_MAX][2], g_beamC[BEAM_MAX][3],
             g_beamK[BEAM_MAX], g_beamF[BEAM_MAX], g_beamT[BEAM_MAX];
static int g_nBeam=0;
/* ⭐ A BEAM IS A LIGHT TOO (queue step 24, 11-04: beams as real lights that cast and move per frame). A beam was a glow in the air and nothing
 * more: a shaft could cross a body and leave it dark. Where the machine has lights (gfxset `beamlights`: 1 they light, 2 they cast), every beam is also a spot from
 * its near end, along it, as wide as it opens, in its colour, by its strength times the set's `beamlight <scale>` (6 to start with, 0 for a set whose beams are
 * only glow). They're made each frame from the beams as they stand, so a beam the timeline sweeps is a light that sweeps. */
static float g_beamLight=6.f;
static void beamDraw(void){
    if(g_nBeam<=0) return;
    if(!g_noDraw) for(int i=0;i<g_nBeam;i++){ const float*U=g_beamU[i]; float rf[3]={0.f,1.f,0.f}; if(fabsf(U[1])>0.9f){ rf[0]=1.f; rf[1]=0.f; } float Vb[3]={U[1]*rf[2]-U[2]*rf[1],U[2]*rf[0]-U[0]*rf[2],U[0]*rf[1]-U[1]*rf[0]}; const float lv=sqrtf(Vb[0]*Vb[0]+Vb[1]*Vb[1]+Vb[2]*Vb[2]); Vb[0]/=lv; Vb[1]/=lv; Vb[2]/=lv; const float Wb[3]={U[1]*Vb[2]-U[2]*Vb[1],U[2]*Vb[0]-U[0]*Vb[2],U[0]*Vb[1]-U[1]*Vb[0]};
        const float d9[24]={g_beamA[i][0],g_beamA[i][1],g_beamA[i][2],g_beamL[i], U[0],U[1],U[2],g_beamK[i], Vb[0],Vb[1],Vb[2],g_beamF[i], Wb[0],Wb[1],Wb[2],g_beamT[i], g_beamR[i][0],g_beamR[i][1],2.5f*fmaxf(g_beamR[i][0],g_beamR[i][1]),0.f, g_beamC[i][0],g_beamC[i][1],g_beamC[i][2],0.f}; gfxBeam(d9); }
}

/* ---- THE STAR (10-22, THE WORLD AT THE END OF TIME, chapter 1: WAN-TO'S STAR) ------------------------------------------
 * Chapter 1 sets the scene for the whole book. Wan-To lives in "the interior of a
 * medium-sized G-3 star" (p2): just under a million miles across, 6,000-7,000 K at its surface and "just a touch variable"
 * (p47), its core at fifteen million degrees (p5); a photon escapes by radiation for the first FOUR-FIFTHS of the way out and
 * by CONVECTION -- cells of hot gas rising, cooling ones falling back -- in the outer fifth (p4); its poles turn slower than
 * its equator (p5); its magnetic field lines twist, loop below the surface and here and there rise above it to make sunspots
 * (p5), and a sunspot is a patch only "a couple of thousand Kelvin" cooler, dark only by comparison (p49). Wan-To is plasma,
 * his pieces thousands of miles apart, held together by "the network of neutrinos that served him for neurons" (p6).
 * Set lines (u):
 *   starbody x y z radius [tempK] [cutdeg] [cutaz] [seed]   the star; cutdeg > 0 opens a wedge (centred on azimuth cutaz,
 *                                                          about +y, 0 = toward +z) down to the core, to show its layers
 *   starspot lat lon size_deg                              a sunspot on the turning surface (16)
 *   starloop lat1 lon1 lat2 lon2 height [thick]            a magnetic loop between two footpoints; height, thick in radii (24)
 *   starweb nodes [seed] [xray]                            WAN-TO: his pieces and the neutrino threads between them (32);
 *                                                          xray 1 (default) draws them through the star, as neutrinos go
 * OUR FILL: the core's size (a quarter of the radius), the colours (a warm grade of the true temperatures), the granules'
 * scale, the loops' pink, Wan-To's cyan. The clock is the exposure clock, so a held exposure holds the star. */
#define STB_SPOTS 16
#define STB_LOOPS 24
#define STB_WEB   32
typedef struct { int on; float c[3], R, T, cutA0, cutW, seed; float st[3]; int nSpot; float spot[STB_SPOTS][4];
                 int nLoop; float loop[STB_LOOPS][6]; int webN, webX; float webSeed; int dirty; } StarBody;
static StarBody g_stb;
static float g_stbT=0.f;
static unsigned g_stbSphVB=0,g_stbSphIB=0,g_stbFaceVB=0,g_stbQuadVB=0,g_stbLoopVB=0;
static int g_stbSphN=0,g_stbFaceN=0,g_stbLoopOff[STB_LOOPS+1];
static float g_stbWebP[STB_WEB][3]; static int g_stbWebL[STB_WEB*3][2]; static int g_stbWebNL=0;
static void stbBuildMeshes(void){
    /* the sphere: a latitude-longitude grid, 128 x 256, unit radius (32-bit indices) */
    const int NL=128, NO=256; int nv=(NL+1)*(NO+1);
    float*V=(float*)malloc((size_t)nv*12); unsigned*I=(unsigned*)malloc((size_t)NL*NO*6*4); int k=0, m=0;
    for(int a=0;a<=NL;a++){ float th=3.14159265f*(float)a/NL; for(int b=0;b<=NO;b++){ float ph=6.2831853f*(float)b/NO;
        V[k++]=sinf(th)*sinf(ph); V[k++]=cosf(th); V[k++]=sinf(th)*cosf(ph); } }
    for(int a=0;a<NL;a++) for(int b=0;b<NO;b++){ unsigned i0=(unsigned)(a*(NO+1)+b), i1=i0+1, i2=i0+(unsigned)(NO+1), i3=i2+1;
        I[m++]=i0; I[m++]=i2; I[m++]=i1; I[m++]=i1; I[m++]=i2; I[m++]=i3; }
    g_stbSphVB = gfxBufferNew();  gfxBufferData(g_stbSphVB, (long)nv*12, V, 0);
    g_stbSphIB = gfxBufferNew();  gfxBufferData(g_stbSphIB, (long)m*4, I, 0);
    g_stbSphN=m; free(V); free(I);
    /* a quad for the corona, two triangles of corners */
    static const float Q[]={-1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1};
    g_stbQuadVB = gfxBufferNew();  gfxBufferData(g_stbQuadVB, sizeof Q, Q, 0);
    g_stbFaceVB = gfxBufferNew(); g_stbLoopVB = gfxBufferNew(); }
/* the two CUT FACES of the wedge: half-discs in the planes at azimuth a0 and a0+w (fans from the core) */
static void stbBuildFaces(void){
    const int NS=96; float V[2*NS*3*3]; int k=0;
    for(int f=0;f<2;f++){ float az=g_stb.cutA0+(f?g_stb.cutW:0.f), sx=sinf(az), sz=cosf(az);
        for(int i=0;i<NS;i++){ float b0=-1.5707963f+3.1415926f*(float)i/NS, b1=-1.5707963f+3.1415926f*(float)(i+1)/NS;
            V[k++]=0.f; V[k++]=0.f; V[k++]=0.f;
            V[k++]=sx*cosf(b0); V[k++]=sinf(b0); V[k++]=sz*cosf(b0);
            V[k++]=sx*cosf(b1); V[k++]=sinf(b1); V[k++]=sz*cosf(b1); } }
     gfxBufferData(g_stbFaceVB, (long)k*4, V, 0); g_stbFaceN=k/3; }
static void stbLatLon(float lat,float lon,float*o){ float a=lat*0.0174533f, b=lon*0.0174533f;
    o[0]=cosf(a)*sinf(b); o[1]=sinf(a); o[2]=cosf(a)*cosf(b); }
/* the LOOPS: each an arch between its footpoints, three strands wound round it (the field lines TWIST, p5) */
static void stbBuildLoops(void){
    const int NA=40, NR=6, NSTR=3; int per=NA*NR*6*NSTR; float*V=(float*)malloc((size_t)g_stb.nLoop*per*8*4); int k=0;
    for(int l=0;l<g_stb.nLoop;l++){ g_stbLoopOff[l]=k/8;
        float A[3],B[3]; stbLatLon(g_stb.loop[l][0],g_stb.loop[l][1],A); stbLatLon(g_stb.loop[l][2],g_stb.loop[l][3],B);
        float H=g_stb.loop[l][4], th=g_stb.loop[l][5]>0.f?g_stb.loop[l][5]:0.006f;
        float om=acosf(fmaxf(-1.f,fminf(1.f,A[0]*B[0]+A[1]*B[1]+A[2]*B[2]))); if(om<1e-4f) om=1e-4f;
        float C[NA+1][3];
        for(int i=0;i<=NA;i++){ float t=(float)i/NA, s0=sinf((1.f-t)*om)/sinf(om), s1=sinf(t*om)/sinf(om), lift=1.f+H*sinf(3.1415926f*t);
            for(int q=0;q<3;q++) C[i][q]=(A[q]*s0+B[q]*s1)*lift; }
        for(int st=0;st<NSTR;st++) for(int i=0;i<NA;i++) for(int j=0;j<NR;j++){
            int ii[2]={i,i+1}, jj[2]={j,j+1}; float P4[2][2][3], N4[2][2][3];
            for(int a=0;a<2;a++){ int c=ii[a]; float T[3]; int c0=c>0?c-1:c, c1=c<NA?c+1:c;
                for(int q=0;q<3;q++) T[q]=C[c1][q]-C[c0][q];
                float tl=sqrtf(T[0]*T[0]+T[1]*T[1]+T[2]*T[2])+1e-9f; for(int q=0;q<3;q++) T[q]/=tl;
                float Rv[3]={C[c][0],C[c][1],C[c][2]}; float rl=sqrtf(Rv[0]*Rv[0]+Rv[1]*Rv[1]+Rv[2]*Rv[2])+1e-9f; for(int q=0;q<3;q++) Rv[q]/=rl;
                float E1[3]={Rv[1]*T[2]-Rv[2]*T[1],Rv[2]*T[0]-Rv[0]*T[2],Rv[0]*T[1]-Rv[1]*T[0]};
                float el=sqrtf(E1[0]*E1[0]+E1[1]*E1[1]+E1[2]*E1[2])+1e-9f; for(int q=0;q<3;q++) E1[q]/=el;
                float E2[3]={T[1]*E1[2]-T[2]*E1[1],T[2]*E1[0]-T[0]*E1[2],T[0]*E1[1]-T[1]*E1[0]};
                float tw=6.2831853f*(1.5f*(float)c/NA+(float)st/NSTR);   /* the strand's place round the arch, winding */
                float off[3]; for(int q=0;q<3;q++) off[q]=(E1[q]*cosf(tw)+E2[q]*sinf(tw))*th*1.6f;
                for(int b=0;b<2;b++){ float an=6.2831853f*(float)jj[b]/NR;
                    for(int q=0;q<3;q++){ N4[a][b][q]=E1[q]*cosf(an)+E2[q]*sinf(an); P4[a][b][q]=C[c][q]+off[q]+N4[a][b][q]*th; } } }
            static const int TR[6][2]={{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};
            for(int v=0;v<6;v++){ int a=TR[v][0], b=TR[v][1];
                V[k++]=P4[a][b][0]; V[k++]=P4[a][b][1]; V[k++]=P4[a][b][2];
                V[k++]=N4[a][b][0]; V[k++]=N4[a][b][1]; V[k++]=N4[a][b][2];
                V[k++]=(float)ii[a]/NA; V[k++]=(float)st; } } }
    g_stbLoopOff[g_stb.nLoop]=k/8;
     gfxBufferData(g_stbLoopVB, (long)(k>0?k:8)*4, V, 0); free(V); }
/* WAN-TO: his pieces across the star (a quarter of the way out to nine-tenths), each held to its nearest by a thread */
static void stbBuildWeb(void){
    unsigned r=(unsigned)(g_stb.webSeed*7919.f)+12345u; g_stbWebNL=0;
    for(int i=0;i<g_stb.webN;i++){ float p[3], l2;
        do { for(int q=0;q<3;q++){ r=r*1664525u+1013904223u; p[q]=((r>>8)&0xffff)/32767.5f-1.f; } l2=p[0]*p[0]+p[1]*p[1]+p[2]*p[2]; } while(l2>0.81f||l2<0.0625f);
        memcpy(g_stbWebP[i],p,12); }
    for(int i=0;i<g_stb.webN;i++){ int b1=-1,b2=-1; float d1=9.f,d2=9.f;
        for(int j=0;j<g_stb.webN;j++){ if(j==i) continue; float d=0.f; for(int q=0;q<3;q++){ float e=g_stbWebP[i][q]-g_stbWebP[j][q]; d+=e*e; }
            if(d<d1){ d2=d1; b2=b1; d1=d; b1=j; } else if(d<d2){ d2=d; b2=j; } }
        int nb[2]={b1,b2};
        for(int e=0;e<2;e++){ int j=nb[e]; if(j<0) continue; int dup=0;
            for(int z=0;z<g_stbWebNL;z++) if((g_stbWebL[z][0]==j&&g_stbWebL[z][1]==i)||(g_stbWebL[z][0]==i&&g_stbWebL[z][1]==j)) dup=1;
            if(!dup&&g_stbWebNL<STB_WEB*3){ g_stbWebL[g_stbWebNL][0]=i; g_stbWebL[g_stbWebNL][1]=j; g_stbWebNL++; } } } }
static void stbInit(void){
    static int done=0; if(done) return; done=1;
    stbBuildMeshes();
    printf("[star] ready%c",10); fflush(stdout); }
/* the star, as data: the same numbers stbUse gives its programs */
static void starPacket(void){
    GfxStar s; memset(&s,0,sizeof s); s.on=1; s.sphVB=g_stbSphVB; s.sphIB=g_stbSphIB; s.sphN=(unsigned)g_stbSphN; if(g_stb.cutW>0.f){ s.faceVB=g_stbFaceVB; s.faceN=(unsigned)g_stbFaceN; } s.quadVB=g_stbQuadVB; s.loopVB=g_stbLoopVB; s.nLoop=g_stb.nLoop;
    memcpy(s.c,g_stb.c,12); s.c[3]=g_stb.R;
    { float sx=1.f; if(g_stb.st[0]!=1.f){ float e=(g_setT-g_stb.st[2])/fmaxf(g_stb.st[1],0.01f); e=e<0.f?0.f:(e>1.f?1.f:e); e=e*e*(3.f-2.f*e); sx=1.f+(g_stb.st[0]-1.f)*e; sx*=1.f+0.035f*e*sinf(g_stbT*2.3f)+0.02f*e*sinf(g_stbT*5.1f+1.3f); }
      float sy=1.f/sqrtf(fmaxf(sx,0.05f)); s.s[0]=sx; s.s[1]=sy*(1.f+0.02f*(sx-1.f)*sinf(g_stbT*3.7f)); s.s[2]=sy; s.s[3]=g_stbT; }
    s.t[0]=g_stb.T; s.t[1]=0.70f; s.t[2]=1.12f*(1.f+0.025f*sinf(g_stbT*0.37f)); s.t[3]=0.0006f;
    s.cut[0]=g_stb.cutA0; s.cut[1]=g_stb.cutW; s.cut[2]=(float)g_stb.nSpot;
    for(int i=0;i<g_stb.nSpot;i++){ stbLatLon(g_stb.spot[i][0],g_stb.spot[i][1],s.spot[i]); s.spot[i][3]=g_stb.spot[i][2]*0.0174533f; }
    { float f[3]={g_stb.c[0]-g_camEye[0],g_stb.c[1]-g_camEye[1],g_stb.c[2]-g_camEye[2]}; float fl=sqrtf(f[0]*f[0]+f[1]*f[1]+f[2]*f[2])+1e-6f; for(int q=0;q<3;q++) f[q]/=fl;
      float up0[3]={0.f,1.f,0.f}; if(fabsf(f[1])>0.95f){ up0[0]=1.f; up0[1]=0.f; }
      float rt[3]={f[1]*up0[2]-f[2]*up0[1],f[2]*up0[0]-f[0]*up0[2],f[0]*up0[1]-f[1]*up0[0]}; float rl=sqrtf(rt[0]*rt[0]+rt[1]*rt[1]+rt[2]*rt[2]); for(int q=0;q<3;q++) rt[q]/=rl;
      s.rt[0]=rt[0]; s.rt[1]=rt[1]; s.rt[2]=rt[2]; s.up[0]=rt[1]*f[2]-rt[2]*f[1]; s.up[1]=rt[2]*f[0]-rt[0]*f[2]; s.up[2]=rt[0]*f[1]-rt[1]*f[0]; }
    for(int i=0;i<=g_stb.nLoop;i++) s.loopOff[i]=g_stbLoopOff[i];
    for(int i=0;i<g_stb.nLoop;i++){ float mlat=0.5f*(g_stb.loop[i][0]+g_stb.loop[i][2])*0.0174533f, sl=sinf(mlat); s.loopAng[i]=g_stbT*0.0006f*(1.f-0.3f*sl*sl); s.loopPh[i]=(float)i*1.7f; }
    gfxStar(&s); }
static void starDraw(void){
    if(!g_stb.on) return;
    stbInit();
    float dt=(float)g_animDtS; if(dt>0.1f) dt=0.1f; if(dt<0.f) dt=0.f; g_stbT+=dt;
    if(g_stb.dirty){ g_stb.dirty=0; stbBuildFaces(); if(g_stb.nLoop>0) stbBuildLoops(); if(g_stb.webN>0) stbBuildWeb(); }
    if(!g_noDraw) starPacket();
    /* 5. WAN-TO: his pieces and the threads between them -- blended over, through the star when xray (neutrinos
     *    pass through everything), a pulse running down every thread */
    if(g_stb.webN>0&&g_stbWebNL>0){
        static float vb[(STB_WEB+STB_WEB*3*8)*6*9]; int n=0;
        float f[3]={g_stb.c[0]-g_camEye[0],g_stb.c[1]-g_camEye[1],g_stb.c[2]-g_camEye[2]}; float fl=sqrtf(f[0]*f[0]+f[1]*f[1]+f[2]*f[2])+1e-6f;
        for(int q=0;q<3;q++) f[q]/=fl;
        float up0[3]={0.f,1.f,0.f}; if(fabsf(f[1])>0.95f){ up0[0]=1.f; up0[1]=0.f; }
        float rt[3]={f[1]*up0[2]-f[2]*up0[1],f[2]*up0[0]-f[0]*up0[2],f[0]*up0[1]-f[1]*up0[0]}; float rl=sqrtf(rt[0]*rt[0]+rt[1]*rt[1]+rt[2]*rt[2]);
        for(int q=0;q<3;q++) rt[q]/=rl;
        float up[3]={rt[1]*f[2]-rt[2]*f[1],rt[2]*f[0]-rt[0]*f[2],rt[0]*f[1]-rt[1]*f[0]};
        const float CY[3]={0.32f,0.90f,1.0f};
        #define STB_V(P,U,V,A) do{ float*o_=vb+n*9; o_[0]=(P)[0]; o_[1]=(P)[1]; o_[2]=(P)[2]; o_[3]=(U); o_[4]=(V); o_[5]=CY[0]; o_[6]=CY[1]; o_[7]=CY[2]; o_[8]=(A); n++; }while(0)
        for(int e=0;e<g_stbWebNL;e++){ float A[3],B[3]; for(int q=0;q<3;q++){ A[q]=g_stb.c[q]+g_stbWebP[g_stbWebL[e][0]][q]*g_stb.R; B[q]=g_stb.c[q]+g_stbWebP[g_stbWebL[e][1]][q]*g_stb.R; }
            float d[3]={B[0]-A[0],B[1]-A[1],B[2]-A[2]};
            float s[3]={d[1]*f[2]-d[2]*f[1],d[2]*f[0]-d[0]*f[2],d[0]*f[1]-d[1]*f[0]}; float sl=sqrtf(s[0]*s[0]+s[1]*s[1]+s[2]*s[2])+1e-6f;
            float w=g_stb.R*0.010f; for(int q=0;q<3;q++) s[q]=s[q]/sl*w;
            float ph=fmodf(g_stbT*0.55f+(float)e*0.37f,1.f);
            for(int k=0;k<8;k++){ float t0=(float)k/8.f, t1=(float)(k+1)/8.f;
                float a0=0.30f+0.70f*expf(-(t0-ph)*(t0-ph)*60.f), a1=0.30f+0.70f*expf(-(t1-ph)*(t1-ph)*60.f);
                float P0[3],P1[3],Q0[3],Q1[3]; for(int q=0;q<3;q++){ float x0=A[q]+d[q]*t0, x1=A[q]+d[q]*t1; P0[q]=x0-s[q]; Q0[q]=x0+s[q]; P1[q]=x1-s[q]; Q1[q]=x1+s[q]; }
                STB_V(P0,0.f,-1.f,a0*0.75f); STB_V(P1,0.f,-1.f,a1*0.75f); STB_V(Q1,0.f,1.f,a1*0.75f);
                STB_V(P0,0.f,-1.f,a0*0.75f); STB_V(Q1,0.f,1.f,a1*0.75f); STB_V(Q0,0.f,1.f,a0*0.75f); } }
        for(int i=0;i<g_stb.webN;i++){ float C[3]; for(int q=0;q<3;q++) C[q]=g_stb.c[q]+g_stbWebP[i][q]*g_stb.R;
            float sz=g_stb.R*(0.045f+0.012f*sinf(g_stbT*2.1f+(float)i)); float c4[4][3];
            static const float CS[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}};
            for(int k=0;k<4;k++) for(int q=0;q<3;q++) c4[k][q]=C[q]+(rt[q]*CS[k][0]+up[q]*CS[k][1])*sz;
            STB_V(c4[0],-1.f,-1.f,0.95f); STB_V(c4[1],1.f,-1.f,0.95f); STB_V(c4[2],1.f,1.f,0.95f);
            STB_V(c4[0],-1.f,-1.f,0.95f); STB_V(c4[2],1.f,1.f,0.95f); STB_V(c4[3],-1.f,1.f,0.95f); }
        #undef STB_V
        if(!g_noDraw){ static float w9[(STB_WEB+STB_WEB*3*8)*6*9]; for(int i=0;i<n;i++){ const float*o=vb+i*9; float*w=w9+i*9; w[0]=o[0]; w[1]=o[1]; w[2]=o[2]; w[3]=o[5]; w[4]=o[6]; w[5]=o[7]; w[6]=o[8]; w[7]=o[3]; w[8]=o[4]; } gfxFxTris(w9,n,g_stb.webX?GFX_FX_WEBX:GFX_FX_WEB,0); }
           }
     }



static void watDraw(void){
    if(!g_nWat) return;
    static int stamp=-1;
    int step=((int)g_frameStamp!=stamp); stamp=(int)g_frameStamp;
    for(int q=0;q<g_nWat;q++){
        Water*W=&g_wat[q];
        if(!W->used) continue;
        if(step&&q==0) g_watT+=(float)g_chDt;
        if(step) watStep(W,(float)g_chDt);
        if(!W->gl){
            static unsigned short idx[(WAT_G-1)*(WAT_G-1)*6]; unsigned n=0;
            for(int r=0;r<WAT_G-1;r++) for(int c=0;c<WAT_G-1;c++){
                unsigned short i=(unsigned short)(r*WAT_G+c);
                idx[n++]=i; idx[n++]=(unsigned short)(i+1); idx[n++]=(unsigned short)(i+WAT_G);
                idx[n++]=(unsigned short)(i+1); idx[n++]=(unsigned short)(i+WAT_G+1); idx[n++]=(unsigned short)(i+WAT_G); }
            W->nIdx=n;
            static unsigned char jz[WAT_N*4], wz[WAT_N*4];
            for(int i=0;i<WAT_N;i++){ jz[i*4]=jz[i*4+1]=jz[i*4+2]=jz[i*4+3]=0;
                wz[i*4]=255; wz[i*4+1]=wz[i*4+2]=wz[i*4+3]=0; }
            W->vb = gfxBufferNew(); W->nb = gfxBufferNew(); W->ib = gfxBufferNew();
            W->jb = gfxBufferNew(); W->wb = gfxBufferNew();
             gfxBufferData(W->vb, sizeof W->pos, W->pos, 1);
             gfxBufferData(W->nb, sizeof W->nrm, W->nrm, 1);
             gfxBufferData(W->jb, sizeof jz, jz, 0);
             gfxBufferData(W->wb, sizeof wz, wz, 0);
             gfxBufferData(W->ib, (long)(n*2), idx, 0);
            { static unsigned ix9[(WAT_G-1)*(WAT_G-1)*6]; for(unsigned k9=0;k9<n;k9++) ix9[k9]=idx[k9]; W->ib32 = gfxBufferNew();  gfxBufferData(W->ib32, (long)(n*4), ix9, 0); }   /* the packet's indices are 32 bits */
            W->gl=1; }
         gfxBufferSub(W->vb, 0, sizeof W->pos, W->pos);
         gfxBufferSub(W->nb, 0, sizeof W->nrm, W->nrm);
        if(!g_noDraw){ static const float I9[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; GfxMesh m9; memset(&m9,0,sizeof m9); m9.id=((unsigned)GFX_KIND_PROP<<28)|((0xf100u+(unsigned)q)<<12); m9.vb[0]=W->vb; m9.vb[1]=W->nb; m9.vb[4]=W->ib32; m9.nIndex=W->nIdx; m9.nVerts=WAT_N; m9.model=I9; memcpy(m9.tint,W->tint,12); m9.flags=GFX_WATER|GFX_BLEND; gfxMesh(&m9); }
        }
     }

