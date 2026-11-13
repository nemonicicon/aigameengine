/* ================= THE SKATEPARK ==================================================
 * 08-21: the reality scan of the park is a rough first scan, good for inspiration and for textures. So the
 * scan supplies the LOOK and the ride surface is generated here.
 *
 * The whole park is a heightfield, and parkH() is BOTH the geometry source and the collision
 * query. That is the point of doing it this way: what she rolls on is, by construction, exactly
 * what is drawn -- there is no second surface to drift out of sync with the first.
 * Scale: reva is 3.45 units tall, so 1 unit is about half a metre. */
#define PARK_EXT  26.0f      /* the park is 52x52 units -- about 26m across */
#define PARK_STEP 0.40f
static unsigned parkVbo[3]={0,0,0}; static int parkVerts=0; static unsigned parkTex=0;

static float parkH(float x,float z){
    float y=0.f;
    /* ---- THE BIG BOWL: carved DOWN out of the slab. Flat floor out to 3.5, then the
     * transition up to the 9.0 rim, steepening as it rises the way a real bowl does. */
    { float dx=x+12.f, dz=z+10.f, r=sqrtf(dx*dx+dz*dz);
      if(r<9.f){ float u=(r-3.5f)/5.5f; if(u<0.f)u=0.f;
                 float b=-4.0f*(1.f-u*u); if(b<y) y=b; } }
    /* ---- THE POCKET: a small steep bowl, for quick transfers */
    { float dx=x-16.f, dz=z+14.f, r=sqrtf(dx*dx+dz*dz);
      if(r<5.5f){ float u=(r-1.8f)/3.7f; if(u<0.f)u=0.f;
                  float b=-2.6f*(1.f-u*u); if(b<y) y=b; } }
    /* ---- QUARTER PIPES on all four walls: a true quarter-circle profile, tangent to the
     * flat at the bottom and vertical at the coping, so the whole room is rideable. */
    { const float R=5.5f;
      float t;
      t=(-(PARK_EXT-R)-z)/R; if(t>0.f){ if(t>1.f)t=1.f; float h=R-R*sqrtf(1.f-t*t); if(h>y) y=h; }
      t=(z-(PARK_EXT-R))/R;  if(t>0.f){ if(t>1.f)t=1.f; float h=R-R*sqrtf(1.f-t*t); if(h>y) y=h; }
      t=(-(PARK_EXT-R)-x)/R; if(t>0.f){ if(t>1.f)t=1.f; float h=R-R*sqrtf(1.f-t*t); if(h>y) y=h; }
      t=(x-(PARK_EXT-R))/R;  if(t>0.f){ if(t>1.f)t=1.f; float h=R-R*sqrtf(1.f-t*t); if(h>y) y=h; } }
    /* ---- FUNBOX: flat top with banked sides */
    { float dx=fabsf(x-12.f), dz=fabsf(z-6.f), d=dx>dz?dx:dz;
      if(d<5.f){ float h=(d<3.f)?1.5f:1.5f*(5.f-d)*0.5f; if(h>y) y=h; } }
    /* ---- PYRAMID: four banks meeting at a peak */
    { float dx=fabsf(x+16.f), dz=fabsf(z-12.f), d=dx>dz?dx:dz;
      if(d<4.5f){ float h=2.0f*(4.5f-d)/4.5f; if(h>y) y=h; } }
    /* ---- THE LEDGE / manual pad: a low block with two grindable edges */
    { if(x>-8.f&&x<0.f&&z>15.2f&&z<16.8f){ if(0.65f>y) y=0.65f; } }
    /* ---- THE STAIR SET: a raised plaza dropping six steps to the flat. Stairs are just
     * height, so the same one function that draws them is the one she rolls down. */
    { if(x>0.f&&x<8.f){
        if(z>-10.f&&z<2.f){ if(2.4f>y) y=2.4f; }
        else if(z<=-10.f&&z>-16.f){
            int st=(int)((-10.f-z)/1.0f);          /* 1.0 deep, 0.4 tall: a real stair */
            float h=2.4f-0.4f*(float)(st+1);
            if(h<0.f) h=0.f;
            if(h>y) y=h; } } }
    return y;
}

/* Surface normal by central difference on parkH -- analytic enough to shade smoothly even
 * where the grid is coarse, and it is the same slope the board pitches to. */
static void parkN(float x,float z,float*n){
    const float e=0.15f;
    float dx=parkH(x+e,z)-parkH(x-e,z), dz=parkH(x,z+e)-parkH(x,z-e);
    float nx=-dx/(2.f*e), nz=-dz/(2.f*e), ny=1.f;
    float L2=sqrtf(nx*nx+ny*ny+nz*nz); if(L2<1e-6f) L2=1.f;
    n[0]=nx/L2; n[1]=ny/L2; n[2]=nz/L2;
}

/* ---- RAILS. A rail is a grind LINE plus the box drawn around it, so what she locks onto
 * and what you can see are one declaration. Ledge edges are rails too -- that is what makes
 * a ledge a ledge rather than a step. */
typedef struct { float a[3],b[3]; float r; const char*name; } Rail;
static Rail g_rail[6];
static int  g_nRail=0;
static unsigned railVbo[2]={0,0}; static int railVerts=0;

static void railAdd(float ax,float ay,float az,float bx,float by,float bz,float r,const char*nm){
    if(g_nRail>=(int)(sizeof g_rail/sizeof g_rail[0])) return;
    Rail*R=&g_rail[g_nRail++];
    R->a[0]=ax; R->a[1]=ay; R->a[2]=az;
    R->b[0]=bx; R->b[1]=by; R->b[2]=bz;
    R->r=r; R->name=nm;
}
/* one box per rail, as 12 triangles */
static void railBuild(void){
    railAdd(-9.f,0.95f, 19.f,  9.f,0.95f, 19.f, 0.09f,"RAIL");
    railAdd( 8.6f,3.30f,-10.f, 8.6f,0.90f,-16.f,0.09f,"HANDRAIL");   /* beside the stairs */
    railAdd(-8.f,0.68f, 16.8f, 0.f,0.68f, 16.8f,0.10f,"LEDGE");
    railAdd(-8.f,0.68f, 15.2f, 0.f,0.68f, 15.2f,0.10f,"LEDGE");
    railVerts=g_nRail*36;
    float*P=(float*)malloc((size_t)railVerts*3*sizeof(float));
    float*N=(float*)malloc((size_t)railVerts*3*sizeof(float));
    int v=0;
    for(int i=0;i<g_nRail;i++){
        Rail*R=&g_rail[i];
        float d[3]={R->b[0]-R->a[0],R->b[1]-R->a[1],R->b[2]-R->a[2]};
        float dl=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(dl<1e-5f) dl=1.f;
        d[0]/=dl; d[1]/=dl; d[2]/=dl;
        float up[3]={0.f,1.f,0.f};
        float rt[3]={up[1]*d[2]-up[2]*d[1], up[2]*d[0]-up[0]*d[2], up[0]*d[1]-up[1]*d[0]};
        float rl=sqrtf(rt[0]*rt[0]+rt[1]*rt[1]+rt[2]*rt[2]); if(rl<1e-5f) rl=1.f;
        rt[0]/=rl; rt[1]/=rl; rt[2]/=rl;
        float u2[3]={d[1]*rt[2]-d[2]*rt[1], d[2]*rt[0]-d[0]*rt[2], d[0]*rt[1]-d[1]*rt[0]};
        float rr=R->r;
        /* eight corners of the box */
        float c[8][3];
        for(int k=0;k<8;k++){
            const float*base=(k&4)?R->b:R->a;
            float sx=(k&1)?rr:-rr, sy=(k&2)?rr:-rr;
            for(int m=0;m<3;m++) c[k][m]=base[m]+rt[m]*sx+u2[m]*sy;
        }
        static const int F[6][4]={{0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3}};
        for(int f=0;f<6;f++){
            const int*q=F[f];
            float e1[3],e2[3],nn[3];
            for(int m=0;m<3;m++){ e1[m]=c[q[1]][m]-c[q[0]][m]; e2[m]=c[q[2]][m]-c[q[0]][m]; }
            nn[0]=e1[1]*e2[2]-e1[2]*e2[1]; nn[1]=e1[2]*e2[0]-e1[0]*e2[2]; nn[2]=e1[0]*e2[1]-e1[1]*e2[0];
            float nl=sqrtf(nn[0]*nn[0]+nn[1]*nn[1]+nn[2]*nn[2]); if(nl<1e-5f) nl=1.f;
            nn[0]/=nl; nn[1]/=nl; nn[2]/=nl;
            int tri[6]={q[0],q[1],q[2], q[0],q[2],q[3]};
            for(int k=0;k<6;k++){
                P[v*3]=c[tri[k]][0]; P[v*3+1]=c[tri[k]][1]; P[v*3+2]=c[tri[k]][2];
                N[v*3]=nn[0]; N[v*3+1]=nn[1]; N[v*3+2]=nn[2];
                v++; }
        }
    }
    gfxBuffersNew(2,railVbo);
     gfxBufferData(railVbo[0], (size_t)railVerts*12, P, 0);
     gfxBufferData(railVbo[1], (size_t)railVerts*12, N, 0);
    free(P); free(N);
    printf("rails: %d, %d tris\n",g_nRail,railVerts/3);
}

/* Closest point on a rail to (x,y,z); returns the parameter along it and the distances. */
static int railNear(float x,float y,float z,int*outIdx,float*outT,float*outLat,float*outDy){
    int best=-1; float bT=0.f,bLat=1e9f,bDy=0.f;
    for(int i=0;i<g_nRail;i++){
        Rail*R=&g_rail[i];
        float d[3]={R->b[0]-R->a[0],R->b[1]-R->a[1],R->b[2]-R->a[2]};
        float dd=d[0]*d[0]+d[1]*d[1]+d[2]*d[2]; if(dd<1e-6f) continue;
        float t=((x-R->a[0])*d[0]+(y-R->a[1])*d[1]+(z-R->a[2])*d[2])/dd;
        if(t<0.f||t>1.f) continue;
        float px=R->a[0]+d[0]*t, py=R->a[1]+d[1]*t, pz=R->a[2]+d[2]*t;
        float lat=hypotf(x-px,z-pz), dy=y-py;
        if(lat<bLat){ bLat=lat; best=i; bT=t; bDy=dy; }
    }
    if(best<0) return 0;
    *outIdx=best; *outT=bT; *outLat=bLat; *outDy=bDy;
    return 1;
}

static void parkBuild(void){
    int n=(int)((PARK_EXT*2.f)/PARK_STEP);
    parkVerts=n*n*6;
    float*P=(float*)malloc((size_t)parkVerts*3*sizeof(float));
    float*N=(float*)malloc((size_t)parkVerts*3*sizeof(float));
    float*T=(float*)malloc((size_t)parkVerts*2*sizeof(float));
    int v=0;
    for(int iz=0;iz<n;iz++) for(int ix=0;ix<n;ix++){
        float x0=-PARK_EXT+ix*PARK_STEP, z0=-PARK_EXT+iz*PARK_STEP;
        float x1=x0+PARK_STEP, z1=z0+PARK_STEP;
        float qx[6]={x0,x1,x1, x0,x1,x0}, qz[6]={z0,z0,z1, z0,z1,z1};
        for(int k=0;k<6;k++){
            float xx=qx[k], zz=qz[k];
            P[v*3]=xx; P[v*3+1]=parkH(xx,zz); P[v*3+2]=zz;
            parkN(xx,zz,N+v*3);
            T[v*2]=xx*0.55f; T[v*2+1]=zz*0.55f;   /* tight tile: the scan reads as grit, not as its atlas */
            v++; } }
    gfxBuffersNew(3,parkVbo);
     gfxBufferData(parkVbo[0], (size_t)parkVerts*12, P, 0);
     gfxBufferData(parkVbo[1], (size_t)parkVerts*12, N, 0);
     gfxBufferData(parkVbo[2], (size_t)parkVerts*8, T, 0);
    free(P); free(N); free(T);
    railBuild();
    { int tw,th,comp; unsigned char*img=stbi_load("props/park_diffuse.jpg",&tw,&th,&comp,4);
      if(img){ parkTex = gfxTextureNew();
               gfxTexture(parkTex, tw, th, img);
               stbi_image_free(img);
               printf("park: %d tris, scan texture %dx%d\n",parkVerts/3,tw,th); }
      else printf("park: %d tris, NO texture (props/park_diffuse.jpg missing)\n",parkVerts/3); }
}

/* The park has no rig at all, so joints/weights come in as CONSTANT vertex attributes
 * (bone 0, full weight) rather than as buffers nobody would ever vary. */
static void parkDraw(void){
    static const M4 ident={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    if(!parkVerts) return;
    if(!g_noDraw){ GfxMesh m9; memset(&m9,0,sizeof m9); m9.id=((unsigned)GFX_KIND_PROP<<28)|(0xf300u<<12); m9.vb[0]=parkVbo[0]; m9.vb[1]=parkVbo[1]; m9.vb[5]=parkVbo[2]; m9.nVerts=(unsigned)parkVerts; m9.model=ident; m9.tex=parkTex; m9.tint[0]=m9.tint[1]=m9.tint[2]=1.f; m9.useTex=parkTex?1.f:0.f; gfxMesh(&m9); }
}

/* Rails are painted metal, not concrete: no texture, flat tint. */
static void railDraw(void){
    static const M4 ident={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    if(!railVerts) return;
    if(!g_noDraw){ GfxMesh m9; memset(&m9,0,sizeof m9); m9.id=((unsigned)GFX_KIND_PROP<<28)|(0xf301u<<12); m9.vb[0]=railVbo[0]; m9.vb[1]=railVbo[1]; m9.nVerts=(unsigned)railVerts; m9.model=ident; m9.tint[0]=0.72f; m9.tint[1]=0.74f; m9.tint[2]=0.80f; gfxMesh(&m9); }
}

/* ================= A SMALL ANIMATION SYSTEM =========================================
 * Built rather than downloaded, because the point is the craft: the same three ideas below
 * are what any animation layer needs, in this engine or in Godot or anywhere else.
 *
 * 1. SPRINGS, NOT EASES. An ease (x += (target-x)*k) can only creep toward its target. It
 *    never winds up, never overshoots, never settles -- which is exactly why pose-to-pose
 *    motion driven by eases reads as SLIDING rather than moving. A second-order spring
 *    carries velocity, so it arrives with momentum, overshoots a little, and settles. That
 *    overshoot is most of what reads to the eye as weight.
 *
 * 2. LEAD AND LAG. Real bodies do not move all at once: the hips go first, the chest
 *    follows, the head and arms trail. The cheap and correct way to get that is to give
 *    each level of the chain its own stiffness -- heavy and quick at the root, looser
 *    further out -- and let the physics produce the delay instead of scripting it.
 *
 * 3. POSES ON A TIMELINE. A pose is a named set of joint angles; an action is a list of
 *    poses with durations. That IS keyframe animation, with the keys living in code. It
 *    replaces a pile of independent scalars that can only ever average into mush.
 */
typedef struct { float x,v; } Spring;
static void springTo(Spring*s,float target,float stiff,float damp,float dt){
    if(dt>0.05f) dt=0.05f;                     /* a stall must not explode the integrator */
    float a=(target-s->x)*stiff - s->v*damp;
    s->v += a*dt;
    s->x += s->v*dt;
}

/* One authored pose. Angles are radians ADDED to the rest pose; the two foot fields are in
 * world units along the deck, and backOff is 0 on the board, 1 planted on the ground. */
typedef struct {
    float hipDrop;                 /* how far the hips sink -- the source of knee bend */
    float thigh, knee;             /* leg shape on top of what the IK solves */
    float spineFold, spineTwist;
    float armSpread, armFwd, elbow;
    float headTurn;
    float footFront, footBack;     /* along the deck from its centre */
    float backOff;
} SkPose;

/* ---- THE POSE LIBRARY. These are the keys. Chosen to read as skating: a rider is always
 * a little crouched, arms out and low, weight over the front foot. ---- */
static const SkPose PZ_STAND  ={0.13f, 0.05f,0.20f, 0.06f,-0.10f, 0.50f,-0.20f,0.30f, -0.30f,  0.30f,-0.26f, 1.00f};
static const SkPose PZ_ROLL   ={0.21f, 0.10f,0.34f, 0.14f,-0.13f, 0.62f,-0.24f,0.36f, -0.32f,  0.30f,-0.26f, 0.00f};
static const SkPose PZ_CARVE  ={0.30f, 0.14f,0.48f, 0.22f,-0.26f, 0.92f,-0.30f,0.52f, -0.44f,  0.30f,-0.26f, 0.00f};
static const SkPose PZ_PLANT ={0.34f, 0.18f,0.56f, 0.30f,-0.08f, 0.70f,-0.34f,0.44f, -0.26f,  0.32f,-0.62f, 1.00f};
static const SkPose PZ_RECOV ={0.22f, 0.10f,0.36f, 0.16f,-0.12f, 0.64f,-0.26f,0.38f, -0.30f,  0.30f,-0.16f, 0.15f};
static const SkPose PZ_LOAD   ={0.44f, 0.30f,0.92f, 0.40f,-0.16f, 0.78f, 0.10f,0.72f, -0.28f,  0.28f,-0.24f, 0.00f};
static const SkPose PZ_POP    ={0.00f,-0.05f,0.06f,-0.06f,-0.10f, 0.46f,-0.50f,0.18f, -0.30f,  0.26f,-0.20f, 0.00f};
static const SkPose PZ_TUCK   ={0.30f, 0.62f,1.05f, 0.30f,-0.14f, 0.86f,-0.10f,0.66f, -0.34f,  0.24f,-0.22f, 0.00f};
static const SkPose PZ_LAND   ={0.48f, 0.26f,0.96f, 0.34f,-0.12f, 0.96f,-0.16f,0.60f, -0.30f,  0.30f,-0.26f, 0.00f};
static const SkPose PZ_GRIND  ={0.26f, 0.12f,0.44f, 0.16f,-0.30f, 1.05f,-0.28f,0.56f, -0.46f,  0.34f,-0.30f, 0.00f};
static const SkPose PZ_MANUAL ={0.30f, 0.16f,0.50f,-0.14f,-0.12f, 0.88f,-0.34f,0.50f, -0.28f,  0.34f,-0.34f, 0.00f};

static void poseLerp(SkPose*o,const SkPose*a,const SkPose*b,float t){
    const float*pa=(const float*)a; const float*pb=(const float*)b; float*po=(float*)o;
    for(unsigned k=0;k<sizeof(SkPose)/sizeof(float);k++) po[k]=pa[k]+(pb[k]-pa[k])*t;
}

/* ---- ACTIONS: a pose list with durations. The ollie is not one number, it is a shape that
 * happens over time -- load, snap straight, tuck, level out -- and that sequence is the
 * difference between a jump you feel and a body that teleports upward. ---- */
typedef struct { const SkPose*p; float dur; } PoseKey;
static const PoseKey ACT_OLLIE[]={ {&PZ_POP,0.09f}, {&PZ_TUCK,0.26f}, {&PZ_ROLL,0.22f} };
static const PoseKey ACT_LAND []={ {&PZ_LAND,0.11f}, {&PZ_ROLL,0.26f} };
static const PoseKey*g_act=NULL; static int g_actN=0, g_actI=0; static float g_actT=0.f;
static SkPose g_pose;   /* this frame's resolved pose: the IK reads its foot channels */
static void actPlay(const PoseKey*a,int n){ g_act=a; g_actN=n; g_actI=0; g_actT=0.f; }


/* ================= THE SKATE STATE TREE ===========================================
 * 08-21: at rest she stands relaxed on a still board, or with one foot off it.
 * Skating is a whole tree of states.
 *
 *      stopped            kicks              rolls              leans hard
 *   IDLE  <----->  PUSH  <----->  ROLL  <----->  CARVE
 *    one foot        one foot        both feet       both feet, banked
 *    on the ground   kicking         on the deck
 *      ^                                |
 *      |  (speed bleeds off)            |  (leaves a lip)
 *      +--------------------------------+----> AIR
 *
 * States are decided from MEASURED ground travel, not from intent: whatever actually moved
 * her decides what she is doing, so the pose can never disagree with the motion. Thresholds
 * carry hysteresis so she does not flicker on the boundary.
 * AIR is wired but unreachable until launch physics exist -- there is nothing yet that can
 * take her off the surface. */
/* ================= THE SKATE STATE TREE =============================================
 *
 *                       ON THE BOARD                                ON FOOT
 *
 *      IDLE <---> PUSH <---> ROLL <---> CARVE            FOOT_IDLE <---> WALK <---> RUN
 *        |          |         |   \                            ^                    |
 *        |          |         |    +---> GRIND                 |                    |
 *        |          |         |    +---> MANUAL                |   remount (A near  |
 *        |          |         |                                |    the board)      |
 *        +----------+---------+---> AIR ---> TRICK             |                    |
 *                                     |                        |                    |
 *                                   land clean -> ROLL         |                    |
 *                                   land dirty -> BAIL --------+--------------------+
 *
 * Every state resolves to (clip, rate, hold) through a FALLBACK CHAIN, so a character who
 * did not ship a given animation degrades to the nearest thing they do have, ending at their
 * held neutral stance. That is what lets an arbitrary downloaded character work at all: no
 * asset has every clip, and a missing clip must never be a missing state. */
enum { SK_IDLE=0, SK_PUSH, SK_ROLL, SK_CARVE, SK_AIR, SK_GRIND, SK_MANUAL,
       SK_FOOT_IDLE, SK_FOOT_WALK, SK_FOOT_RUN, SK_BAIL };
static int   g_skState=SK_IDLE;
static float g_skSpd=0.f;    /* measured ground speed, units/sec, smoothed */
static float g_skTurn=0.f;   /* |heading rate|, rad/sec, smoothed */
static float g_skAirH=0.f;   /* height above the park surface */

static void skUpdate(double dt,float x,float z,float head,float airH){
    static float px=0.f,pz=0.f,ph=0.f; static int init=0;
    if(!init){ init=1; px=x; pz=z; ph=head; }
    if(dt>1e-4){
        float inst=hypotf(x-px,z-pz)/(float)dt;
        g_skSpd += (inst-g_skSpd)*fminf(1.f,(float)dt*6.f);
        float dh=head-ph; while(dh>3.14159265f)dh-=6.28318531f; while(dh<-3.14159265f)dh+=6.28318531f;
        float it=fabsf(dh)/(float)dt;
        g_skTurn += (it-g_skTurn)*fminf(1.f,(float)dt*5.f); }
    px=x; pz=z; ph=head; g_skAirH=airH;
    /* hysteresis: the bar to LEAVE a state is set wider than the bar to enter it */
    int s=g_skState;
    float rollIn =(s==SK_ROLL||s==SK_CARVE)?2.20f:2.90f;
    float pushIn =(s==SK_IDLE)?0.60f:0.35f;
    float carveIn=(s==SK_CARVE)?0.60f:0.95f;
    if(airH>0.35f)            s=SK_AIR;
    else if(g_skSpd<pushIn)   s=SK_IDLE;
    else if(g_skSpd<rollIn)   s=SK_PUSH;
    else if(g_skTurn>carveIn) s=SK_CARVE;
    else                      s=SK_ROLL;
    g_skState=s;
}


/* ---------- CAMERA DIRECTOR (ported from the page's 07-22 recut: wide/overhead/crane/threeq/
 * orbit/hero/reva3rd/revapov/dolly, hard cuts on BARS of the live bpm from the band's state) ---- */
/* THE FLOOR (07-22): 40x20, x +-20, z -19..-0.5 */
#define FLOOR_X 200.0f
#define FLOOR_ZN -190.0f   /* just for fun: x10. four hundred units of disco. */
#define FLOOR_ZF -0.5f
static float g_rvx=0.0f, g_rvz=1.6f;   /* reva's LIVE position -- she roams now (was fixed RVX/RVZ) */
static int g_rvTravel=0;   /* 1 while the host is crossing to the next npc (director follows HER) */
static float g_rvHead=0;   /* her live heading, for the travel follow-cam */
static float g_rvAlt=0;    /* flight altitude (fastest tier) -- the follow-cam climbs with her */
/* SKATE MODE (08-21: a mode of its own, which leaves
 * the dance floor as it was). One switch, REVA_SKATE=1. Off, every line below is inert and the
 * dancefloor build behaves exactly as it did. On: park + board load, the floor quad is not
 * drawn, and she rides -- no flight, no dancing. */
static int g_skate=0;
static float g_revaFootY=0.f;
/* HER SKELETON IS NOT AXIS-ALIGNED. Measured from the rest pose: the left/right separation of
 * her hip bones has near-equal X and Z components, i.e. the whole rig is yawed ~50 degrees in
 * model space. Everything about the stance depends on knowing that -- the board runs along her
 * LATERAL axis (she stands across it), so this angle is what maps her body onto the deck, and
 * getting it wrong puts the foot IK targets on the wrong axis entirely. Measured at load. */
static float g_stanceRad=1.30f;   /* replaced with the measured value once she is loaded */
static int   g_goofy=0;
/* PROCEDURAL vs BAKED. The whole pose layer -- stance yaw, authored poses, foot IK -- assumes
 * a rig we can identify: canonical bones resolved and named feet to solve onto. A character
 * that arrives with its own baked skate animation needs none of it, and applying it anyway
 * would fight the animation it came with. So the rig is checked once, and a rig we cannot map
 * is simply left alone to play its clips. */
static int g_procRig=0;
/* A BAKED rig does its tricks with its own animation, not by us spinning a prop -- the board
 * is part of that rig. So a trick press has to fire the character's trick CLIP instead. */
static int   g_rvTrick=-1;      /* the short, single-trick clip */
static int   g_rvTrick2=-1; static float g_rvTrick2Dur=0.f;   /* a second, different trick */
static int   g_rvFootIdle=-1;   /* a standing-on-the-ground clip, if there is one */
static int   g_rvRide=-1;      /* a FLAT loop: rolling along, no airs */
/* THE BOARD IS ITS OWN BONE CHAIN. A downloaded character brings a skateboard skinned to a
 * handful of bones of its own, which means the deck can be rotated independently of the rider
 * -- so a kickflip, a heelflip and a shove-it can be genuinely different moves even when the
 * character only shipped with one trick animation. The flip is layered ON TOP of whatever the
 * baked clip is doing to the board. */
static int g_boardRoot=-1;
/* WHERE IN A CLIP IS HE STANDING STILL? Some characters ship nothing but performance -- every
 * clip is a run, so there is no idle to use and any window cut from one still has him mid-trick.
 * The frame where he is LOWEST is the frame where his feet are on the ground, and holding that
 * frame is a truthful resting pose. Measured from the joint cloud, so it needs no named bones
 * and works on a rig nobody has mapped. */
static float g_idleHold=-1.f;
static int   g_idleOverlay=0;   /* this frame is a synthesised idle, not a played clip */
/* AUTHORED WHERE IT EXISTS, PROCEDURAL IN BETWEEN. When a real clip is driving a state
 * the procedural layer has to GET OUT OF THE WAY rather than fight it: resetting the arms
 * to rest and re-solving the legs to gait targets wipes exactly the performance that was
 * imported. Procedural fill is for the states a character has no animation for. */
static int   g_authored=0;
static float g_rvTrickDur=0.f;
static int   g_skTrickFired=0;
static int   g_skTrickWhich=0;   /* 0 = the flip, 1 = the second trick */
static double g_skTime=0.0;   /* seconds inside skStep, for the auto-trick test harness */
static float g_rvTrickCur=0.f;   /* duration of whichever trick is playing */
static float g_trickSpd=0.f;   /* REVA_TRICKSPD: a reel is performed slowly for the camera;
                                * a trick in a game is about half a second. */
static int   g_skPushing=0;    /* the trigger is held: she is kicking, whatever the speed says */
static float g_skOllieChg=0.f; /* 0..1 crouch charge -- hold A to load the ollie, release to pop */
static float g_skPopT=0.f;     /* decays after the pop, for the extension */
static float g_rvCarve=0;  /* signed slalom phase (ski/fly weave) -- the chase cam swings on it */
static int g_rvArrived=0;  /* set on FIRST arrival at the current target: micro re-chases after
                            * that must NOT reset the dwell clock (the show was stuck in a loop --
                            * a walking partner kept re-arming travel, the cut never fired) */
static int g_rvDance[64], g_rvNDance=0, g_rvWalk=-1, g_rvFly=-1, g_rvSki=-1, g_rvRun=-1;   /* reva's locomotion clip map (by name at load) */
static int g_rvHipTrans=0;   /* S4.5: does ANY hero clip translate the hips? if not, the procedural bounce returns */
static int g_lockTgt=-1; static float g_rt=0; static int g_btnX=0, g_btnBPrev=0;
static float g_boltT=0, g_boltFrom[3], g_boltTo[3]; static int g_boltVictim=-1;   /* SITH PROTOCOL: RT lock-on, B = eviction lightning */

