/* ======================== WATER =====================================================
 * jacob: "i never see a swimming pool, i think we need water physics too" and "absolutley
 * we need real water physics, these things are why we are making our engine, by reducing
 * uneeded overhead we gain access to previously too heavy systems."
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
    GLuint vb, nb, ib, jb, wb, ib32; unsigned nIdx; int gl;
} Water;
static Water g_wat[WAT_MAX]; static int g_nWat=0;
/* ⛔ THE WATER HAS ITS OWN PROGRAM, so it needs its own copy of the view-projection --
 * drawSet is handed uniform LOCATIONS, not the matrix, and a location belongs to the
 * program it came from. Published by whoever sets up the scene pass. */
static float g_watVP[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
static int g_cullOn=1;   /* ⭐ the set's frustum cull (09-11); ctl cull=0 for the A/B */
static int g_cullEnvDone=0;   /* REVA_NOCULL=1: the same A/B in a door, which never reads the lab's ctl file (11-01) */


/* ---- THE SKY, AS A FUNCTION ---------------------------------------------------------
 * jacob: "we need a real skybox". It becomes a FUNCTION before it becomes a box, because
 * water has to reflect it and a reflection samples a direction, not a mesh -- so this one
 * string is shared by the water shader and (later) the sky pass, and there is exactly one
 * description of what the sky looks like. Driven by the sun rig the engine already has,
 * which `tod=` animates across a whole day. */
static const char*SKY_GLSL=
  "uniform vec3 uSunDir; uniform vec3 uSunCol; uniform float uSunI;\n"
  "uniform vec3 uSkyZen; uniform vec3 uSkyHz; uniform vec3 uGround; uniform float uSkyT;\n"
  "float skyH21(vec2 p){ return fract(sin(dot(p,vec2(41.3,289.1)))*43758.5453); }\n"
  "float skyVN(vec2 p){ vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);\n"
  "  float a=skyH21(i), b=skyH21(i+vec2(1.0,0.0)), c2=skyH21(i+vec2(0.0,1.0)), d2=skyH21(i+vec2(1.0,1.0));\n"
  "  return mix(mix(a,b,f.x),mix(c2,d2,f.x),f.y); }\n"
  "float skyFbm(vec2 p){ float v=0.0, a=0.5;\n"
  "  for(int i=0;i<4;i++){ v+=a*skyVN(p); p*=2.03; a*=0.5; } return v; }\n"
  "vec3 skyColor(vec3 d){\n"
  "  d=normalize(d);\n"
  "  float t=clamp(d.y,0.0,1.0);\n"
  "  vec3 c=mix(uSkyHz,uSkyZen,pow(t,0.55));\n"
  /* the sun: a hard disc inside a soft glow, both tinted by the sun's own colour so a low
   * orange sun gives an orange horizon for free */
  "  float sd=max(dot(d,normalize(uSunDir)),0.0);\n"
  /* \u2b50 the cloud deck: fbm on the ray's projection onto a flat ceiling, drifting on uSkyT */
  "  if(d.y>0.01){\n"
  "    vec2 q=d.xz/max(d.y,0.08);\n"
  "    float c1=skyFbm(q*0.42+vec2(uSkyT*0.0075,uSkyT*0.0030));\n"
  "    float c2=skyFbm(q*1.05+vec2(-uSkyT*0.0130,uSkyT*0.0068));\n"
  "    float cl=clamp((c1*0.70+c2*0.40-0.48)*2.1,0.0,1.0)*smoothstep(0.015,0.34,d.y);\n"
  "    vec3 cc=mix(vec3(0.95,0.95,0.97),uSunCol*1.06,0.40*pow(sd,3.0))*(0.72+0.42*uSunI);\n"
  "    c=mix(c,cc,cl*0.74); }\n"
  "  c+=uSunCol*uSunI*(pow(sd,1200.0)*9.0+pow(sd,26.0)*0.42+pow(sd,4.0)*0.10);\n"
  /* and below the horizon it is ground, not more sky -- a reflection that looks DOWN must
   * not come back with blue in it */
  "  c=mix(uGround,c,smoothstep(-0.10,0.015,d.y));\n"
  "  return c; }\n";

static GLuint g_watProg=0;
static GLint  g_wVP=-1,g_wEye=-1,g_wTint=-1,g_wSunDir=-1,g_wSunCol=-1,g_wSunI=-1,
              g_wZen=-1,g_wHz=-1,g_wGnd=-1,g_wT=-1;

static void watProgInit(void){
    if(g_watProg) return;
    const char*vs=
      "attribute vec3 p; attribute vec3 n;\n"
      "uniform mat4 vp;\n"
      "varying vec3 vn; varying vec3 vwp;\n"
      "void main(){ vn=n; vwp=p; gl_Position=vp*vec4(p,1.0); }\n";
    static char fs[4096];
    snprintf(fs,sizeof fs,
      "precision mediump float;\n"
      "varying vec3 vn; varying vec3 vwp;\n"
      "uniform vec3 uEye; uniform vec3 uTint;\n"
      "%s"
      "void main(){\n"
      "  vec3 N=normalize(vn);\n"
      "  vec3 V=normalize(uEye-vwp);\n"
      "  if(dot(N,V)<0.0) N=-N;\n"
      /* ⭐ FRESNEL IS WHY A RIPPLE IS VISIBLE. Looking straight down you see through the
       * water; at a glancing angle you see the sky. A two-centimetre wave swings the
       * reflected ray far enough to change the colour completely, which is exactly what a
       * diffuse blue plane cannot do however well it is simulated. */
      "  float F=0.02+0.98*pow(1.0-clamp(dot(N,V),0.0,1.0),5.0);\n"
      "  vec3 R=reflect(-V,N);\n"
      "  vec3 refl=skyColor(R);\n"
      "  vec3 deep=uTint*(0.42+0.58*max(dot(N,normalize(uSunDir)),0.0));\n"
      "  vec3 c=mix(deep,refl,F);\n"
      /* a glint where the reflected ray finds the sun itself */
      "  c+=uSunCol*uSunI*pow(max(dot(R,normalize(uSunDir)),0.0),300.0)*1.8;\n"
      "  gl_FragColor=vec4(c,mix(0.58,0.97,F)); }\n", SKY_GLSL);
    GLuint pr=glCreateProgram();
    glAttachShader(pr,sh(GL_VERTEX_SHADER,vs));
    glAttachShader(pr,sh(GL_FRAGMENT_SHADER,fs));
    glBindAttribLocation(pr,0,"p"); glBindAttribLocation(pr,1,"n");
    glLinkProgram(pr);
    GLint ok=0; glGetProgramiv(pr,GL_LINK_STATUS,&ok);
    if(!ok){ char log[1024]; glGetProgramInfoLog(pr,1024,NULL,log);
             printf("[water] SHADER LINK FAILED: %s%c",log,10); fflush(stdout); return; }
    g_watProg=pr;
    g_wVP=glGetUniformLocation(pr,"vp");        g_wEye=glGetUniformLocation(pr,"uEye");
    g_wTint=glGetUniformLocation(pr,"uTint");   g_wSunDir=glGetUniformLocation(pr,"uSunDir");
    g_wSunCol=glGetUniformLocation(pr,"uSunCol");g_wSunI=glGetUniformLocation(pr,"uSunI");
    g_wZen=glGetUniformLocation(pr,"uSkyZen");  g_wHz=glGetUniformLocation(pr,"uSkyHz");
    g_wGnd=glGetUniformLocation(pr,"uGround");
    printf("[water] reflection shader ready%c",10); fflush(stdout); }

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
static float g_starDir[8][4], g_starCol[8][4]; static int g_nStar=0;
static float g_starFl[8][3];   /* (10-22) a named star's flare: delay, rise, peak (peak 0 = none) */
static float g_milky[4]={0.f,1.f,0.f,0.f};
static GLuint g_spaceProg=0, g_spaceVB=0;
static GLint  g_spVP=-1,g_spEye=-1,g_spStar=-1,g_spStarC=-1,g_spNStar=-1,g_spMilky=-1,g_spPix=-1;
/* what deep space needs from the set this frame: 8 stars with their flares worked in, 8 colours, the milky way, how many and how big a pixel is */
__attribute__((unused)) static void spaceFill(float*o,int H){ float sd9[8][4],sc9[8][4]; memcpy(sd9,g_starDir,sizeof sd9); memcpy(sc9,g_starCol,sizeof sc9); static const float BW9[3]={0.86f,0.93f,1.0f};
    for(int i=0;i<g_nStar&&i<8;i++) if(g_starFl[i][2]>0.f){ float e=(g_setT-g_starFl[i][0])/fmaxf(g_starFl[i][1],0.01f); e=e<0.f?0.f:(e>1.f?1.f:e); e=e*e*(3.f-2.f*e); const float pk=1.f+(g_starFl[i][2]-1.f)*e; for(int k=0;k<3;k++) sc9[i][k]=(g_starCol[i][k]*(1.f-e)+BW9[k]*e)*pk; sc9[i][3]=g_starCol[i][3]*(1.f+2.f*e); sd9[i][3]=g_starDir[i][3]*(1.f+1.5f*e); }
    memcpy(o,sd9,sizeof sd9); memcpy(o+32,sc9,sizeof sc9); memcpy(o+64,g_milky,16); o[68]=(float)g_nStar; const float fov9=(g_camOn&&g_camFov>0.f)?g_camFov:42.f; o[69]=fov9*0.0174533f/(float)(H>0?H:720); o[70]=o[71]=0.f; }
static void spaceSkyDraw(void){
    if(!g_spaceProg){
        const char*vs=
          "attribute vec3 p;\n"
          "uniform mat4 vp; uniform vec3 uEye;\n"
          "varying vec3 vdir;\n"
          "void main(){ vec3 wp=uEye+p*90.0; vdir=p; gl_Position=vp*vec4(wp,1.0); }\n";
        const char*fs=
          "precision highp float;\n"
          "varying vec3 vdir;\n"
          "uniform vec4 uStar[8]; uniform vec4 uStarC[8]; uniform float uNStar; uniform vec4 uMilky; uniform float uPix;\n"
          "float h1(vec3 p){ return fract(sin(dot(p,vec3(127.1,311.7,74.7)))*43758.5453); }\n"
          "float vn(vec3 p){ vec3 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);\n"
          "  return mix(mix(mix(h1(i),h1(i+vec3(1,0,0)),f.x),mix(h1(i+vec3(0,1,0)),h1(i+vec3(1,1,0)),f.x),f.y),\n"
          "             mix(mix(h1(i+vec3(0,0,1)),h1(i+vec3(1,0,1)),f.x),mix(h1(i+vec3(0,1,1)),h1(i+vec3(1,1,1)),f.x),f.y),f.z); }\n"
          "vec3 h3(vec3 p){ return fract(sin(vec3(dot(p,vec3(127.1,311.7,74.7)),dot(p,vec3(269.5,183.3,246.1)),dot(p,vec3(113.5,271.9,124.6))))*43758.5453); }\n"
          "vec3 scol(float t){ vec3 r=vec3(1.0,0.42,0.22), w=vec3(1.0,0.96,0.90), b=vec3(0.40,0.56,1.0);\n"
          "  return t<0.5 ? mix(r,w,t*2.0) : mix(w,b,(t-0.5)*2.0); }\n"
          /* a star field on the cube's six faces (no pole pinch): one chance of a star per cell, kept off the cell's
           * edges so it never needs its neighbours; most faint, a few bright, every colour from red to blue */
          "vec3 field(vec3 d, float N, float dens, float seed){\n"
          "  vec3 a=abs(d); vec2 uv; float face;\n"
          "  if(a.x>=a.y&&a.x>=a.z){ uv=d.yz/a.x; face=d.x>0.0?0.0:1.0; }\n"
          "  else if(a.y>=a.z){ uv=d.xz/a.y; face=d.y>0.0?2.0:3.0; }\n"
          "  else { uv=d.xy/a.z; face=d.z>0.0?4.0:5.0; }\n"
          "  vec2 g=(uv*0.5+0.5)*N; vec2 cell=floor(g); vec2 f=g-cell;\n"
          "  vec3 r=h3(vec3(cell,face*13.0+seed));\n"
          "  if(r.x>dens) return vec3(0.0);\n"
          "  vec2 sp=0.2+0.6*r.yz;\n"
          "  float dist=length(f-sp)*2.0/N/(1.0+dot(uv,uv));\n"
          "  float b=pow(h1(vec3(cell.yx,face+seed*1.7)),5.0);\n"
          "  float rad=uPix*(0.6+1.7*b);\n"
          "  float k=1.0-smoothstep(rad*0.35,rad,dist);\n"
          "  return scol(h1(vec3(cell,face+seed+7.7)))*k*(0.32+1.9*b); }\n"
          "void main(){\n"
          "  vec3 d=normalize(vdir);\n"
          "  vec3 c=field(d,240.0,0.075,1.0)+field(d,96.0,0.05,5.0);\n"
          "  float mm=dot(d,uMilky.xyz); float band=exp(-mm*mm/0.03)*uMilky.w;\n"
          "  float gr=(0.45+0.55*vn(d*7.0))*(0.55+0.45*vn(d*19.0+3.1));\n"
          "  c+=vec3(0.60,0.64,0.78)*band*gr*0.30;\n"
          "  c+=field(d,420.0,0.22,9.0)*band*gr*0.55;\n"
          "  for(int i=0;i<8;i++){\n"
          "    if(float(i)>=uNStar) break;\n"
          "    float ang=acos(clamp(dot(d,uStar[i].xyz),-1.0,1.0));\n"
          "    float core=1.0-smoothstep(uStar[i].w*0.55,uStar[i].w,ang);\n"
          "    float halo=uStarC[i].a*exp(-ang/(uStar[i].w*3.5));\n"
          "    c+=uStarC[i].rgb*(core*1.6+halo*0.8); }\n"
          "  gl_FragColor=vec4(c,1.0); }\n";
        GLuint pr=glCreateProgram();
        glAttachShader(pr,sh(GL_VERTEX_SHADER,vs));
        glAttachShader(pr,sh(GL_FRAGMENT_SHADER,fs));
        glBindAttribLocation(pr,0,"p");
        glLinkProgram(pr);
        GLint ok=0; glGetProgramiv(pr,GL_LINK_STATUS,&ok);
        if(!ok){ char log[512]; glGetProgramInfoLog(pr,512,NULL,log);
                 printf("[space] LINK FAILED: %s%c",log,10); fflush(stdout); g_spaceProg=(GLuint)-1; return; }
        g_spaceProg=pr;
        g_spVP=glGetUniformLocation(pr,"vp");       g_spEye=glGetUniformLocation(pr,"uEye");
        g_spStar=glGetUniformLocation(pr,"uStar");  g_spStarC=glGetUniformLocation(pr,"uStarC");
        g_spNStar=glGetUniformLocation(pr,"uNStar"); g_spMilky=glGetUniformLocation(pr,"uMilky");
        g_spPix=glGetUniformLocation(pr,"uPix");
        static const float C[]={
            -1,-1,-1,  1,-1,-1,  1, 1,-1, -1,-1,-1,  1, 1,-1, -1, 1,-1,
            -1,-1, 1, -1, 1, 1,   1, 1, 1, -1,-1, 1,  1, 1, 1,  1,-1, 1,
            -1,-1,-1, -1, 1,-1,  -1, 1, 1, -1,-1,-1, -1, 1, 1, -1,-1, 1,
             1,-1,-1,  1,-1, 1,   1, 1, 1,  1,-1,-1,  1, 1, 1,  1, 1,-1,
            -1, 1,-1,  1, 1,-1,   1, 1, 1, -1, 1,-1,  1, 1, 1, -1, 1, 1,
            -1,-1,-1, -1,-1, 1,   1,-1, 1, -1,-1,-1,  1,-1, 1,  1,-1,-1 };
        glGenBuffers(1,&g_spaceVB);
        glBindBuffer(GL_ARRAY_BUFFER,g_spaceVB);
        glBufferData(GL_ARRAY_BUFFER,sizeof C,C,GL_STATIC_DRAW);
        printf("[space] star sky ready%c",10); fflush(stdout); }
    if(g_spaceProg==(GLuint)-1) return;
    GLint prev=0; glGetIntegerv(GL_CURRENT_PROGRAM,&prev);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND);   /* infinitely far, as the day sky is */
    glUseProgram(g_spaceProg);
    glUniformMatrix4fv(g_spVP,1,GL_FALSE,g_watVP);
    glUniform3fv(g_spEye,1,g_camPos);
    /* (10-22) A FLARE: from its delay after the set loaded, brighter by its peak over its rise, swollen, its colour
     * going to the flare star's blue-white -- a K-5's orange flashing white-blue */
    { float sd9[8][4], sc9[8][4]; memcpy(sd9,g_starDir,sizeof sd9); memcpy(sc9,g_starCol,sizeof sc9);
      static const float BW9[3]={0.86f,0.93f,1.0f};
      for(int i=0;i<g_nStar&&i<8;i++) if(g_starFl[i][2]>0.f){
          float e=(g_setT-g_starFl[i][0])/fmaxf(g_starFl[i][1],0.01f); e=e<0.f?0.f:(e>1.f?1.f:e); e=e*e*(3.f-2.f*e);
          float pk=1.f+(g_starFl[i][2]-1.f)*e;
          for(int k=0;k<3;k++) sc9[i][k]=(g_starCol[i][k]*(1.f-e)+BW9[k]*e)*pk;
          sc9[i][3]=g_starCol[i][3]*(1.f+2.f*e); sd9[i][3]=g_starDir[i][3]*(1.f+1.5f*e); }
      glUniform4fv(g_spStar,8,&sd9[0][0]); glUniform4fv(g_spStarC,8,&sc9[0][0]); }
    glUniform1f(g_spNStar,(float)g_nStar); glUniform4fv(g_spMilky,1,g_milky);
    { GLint vp9[4]; glGetIntegerv(GL_VIEWPORT,vp9); float fov9=(g_camOn&&g_camFov>0.f)?g_camFov:42.f;
      glUniform1f(g_spPix,fov9*0.0174533f/(float)(vp9[3]>0?vp9[3]:720)); }   /* a star is a pixel or two at any lens */
    glBindBuffer(GL_ARRAY_BUFFER,g_spaceVB);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,0);
    glDrawArrays(GL_TRIANGLES,0,36);
    glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
    glUseProgram((GLuint)prev); }

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
 * ⛔ A solid thing INSIDE the box hides ALL of the beam along its rays, the part in front of it too: this ANGLE
 * context has no OES_depth_texture, so the march cannot stop at the surface. Up to 8. */
#define BEAM_MAX 8
static float g_beamA[BEAM_MAX][3], g_beamU[BEAM_MAX][3], g_beamL[BEAM_MAX], g_beamR[BEAM_MAX][2], g_beamC[BEAM_MAX][3],
             g_beamK[BEAM_MAX], g_beamF[BEAM_MAX], g_beamT[BEAM_MAX];
static int g_nBeam=0;
static GLuint g_beamProg=0, g_beamVB=0;
static GLint g_bmVP=-1,g_bmEye=-1,g_bmA=-1,g_bmU=-1,g_bmV=-1,g_bmW=-1,g_bmL=-1,g_bmR=-1,g_bmC=-1,g_bmK=-1,g_bmF=-1,g_bmT=-1;
static void beamDraw(const float*vp){
    if(g_nBeam<=0) return;
#ifdef REVA_PACKET
    if(!g_noDraw) for(int i=0;i<g_nBeam;i++){ const float*U=g_beamU[i]; float rf[3]={0.f,1.f,0.f}; if(fabsf(U[1])>0.9f){ rf[0]=1.f; rf[1]=0.f; } float Vb[3]={U[1]*rf[2]-U[2]*rf[1],U[2]*rf[0]-U[0]*rf[2],U[0]*rf[1]-U[1]*rf[0]}; const float lv=sqrtf(Vb[0]*Vb[0]+Vb[1]*Vb[1]+Vb[2]*Vb[2]); Vb[0]/=lv; Vb[1]/=lv; Vb[2]/=lv; const float Wb[3]={U[1]*Vb[2]-U[2]*Vb[1],U[2]*Vb[0]-U[0]*Vb[2],U[0]*Vb[1]-U[1]*Vb[0]};
        const float d9[24]={g_beamA[i][0],g_beamA[i][1],g_beamA[i][2],g_beamL[i], U[0],U[1],U[2],g_beamK[i], Vb[0],Vb[1],Vb[2],g_beamF[i], Wb[0],Wb[1],Wb[2],g_beamT[i], g_beamR[i][0],g_beamR[i][1],2.5f*fmaxf(g_beamR[i][0],g_beamR[i][1]),0.f, g_beamC[i][0],g_beamC[i][1],g_beamC[i][2],0.f}; gfxBeam(d9); }
#endif
    if(!g_beamProg){
        const char*vs=
          "attribute vec3 p;\n"
          "uniform mat4 vp; uniform vec3 uA,uU,uV,uW,uR; uniform float uL;\n"
          "varying vec3 vW;\n"
          "void main(){ vW=uA+uU*((p.z*0.5+0.5)*uL)+(uV*p.x+uW*p.y)*uR.z; gl_Position=vp*vec4(vW,1.0); }\n";
        const char*fs=
          "precision highp float;\n"
          "varying vec3 vW;\n"
          "uniform vec3 uEye,uA,uU,uC,uR; uniform float uL,uK,uF,uT;\n"
          "void main(){\n"
          "  vec3 D=vW-uEye; float tM=length(D); D/=tM;\n"
          "  vec3 o=uEye-uA; float du=dot(D,uU), ou=dot(o,uU);\n"
          "  vec3 dp=D-du*uU, op=o-ou*uU;\n"
          "  float a=dot(dp,dp), b=2.0*dot(dp,op), c=dot(op,op)-uR.z*uR.z;\n"
          "  float t0=0.0, t1=tM;\n"
          "  if(a>1e-9){ float q=b*b-4.0*a*c; if(q<=0.0) discard; q=sqrt(q);\n"
          "    t0=max(t0,(-b-q)/(2.0*a)); t1=min(t1,(-b+q)/(2.0*a)); }\n"
          "  else if(c>0.0) discard;\n"
          "  if(abs(du)>1e-6){ float ta=-ou/du, tb=(uL-ou)/du; t0=max(t0,min(ta,tb)); t1=min(t1,max(ta,tb)); }\n"
          "  else if(ou<0.0||ou>uL) discard;\n"
          "  if(t1<=t0) discard;\n"
          /* ⛔ 40 EVEN STEPS across the bound (sized for the plume's FAR end, 2.5 x r1) came back as a string of
           * beads at the nozzle, where the plume is 3.5 u across and a step was 70. Each step is now 0.4 of the
           * plume's radius where the sample is, and outside it a stride toward its 2.6 r skin (sphere-traced, 0.75
           * of the way: the cone is wider ahead): a crossing near the nozzle takes ~30 steps, the length of the
           * plume seen down its axis ~50. Density at each step's middle. */
          "  float t=t0, acc=0.0;\n"
          "  for(int i=0;i<128;i++){ if(t>=t1) break;\n"
          "    vec3 q=o+D*t; float s=dot(q,uU); float r=mix(uR.x,uR.y,clamp(s/uL,0.0,1.0)); float d=length(q-s*uU);\n"
          "    float h=min(max(0.4*r,0.75*(d-2.6*r)),t1-t);\n"
          "    q=o+D*(t+0.5*h); s=dot(q,uU); vec3 rr=q-s*uU; float x=clamp(s/uL,0.0,1.0); r=mix(uR.x,uR.y,x);\n"
          "    acc+=exp(-dot(rr,rr)/(r*r))*pow(uR.x/r,uT)*pow(1.0-x,uF)*smoothstep(0.0,0.004,x)*h;\n"
          "    t+=h; }\n"
          "  gl_FragColor=vec4(uC*(1.0-exp(-uK*acc)),1.0); }\n";
        GLuint pr=glCreateProgram();
        glAttachShader(pr,sh(GL_VERTEX_SHADER,vs));
        glAttachShader(pr,sh(GL_FRAGMENT_SHADER,fs));
        glBindAttribLocation(pr,0,"p");
        glLinkProgram(pr);
        GLint ok=0; glGetProgramiv(pr,GL_LINK_STATUS,&ok);
        if(!ok){ char log[512]; glGetProgramInfoLog(pr,512,NULL,log);
                 printf("[beam] LINK FAILED: %s%c",log,10); fflush(stdout); g_beamProg=(GLuint)-1; return; }
        g_beamProg=pr;
        g_bmVP=glGetUniformLocation(pr,"vp"); g_bmEye=glGetUniformLocation(pr,"uEye");
        g_bmA=glGetUniformLocation(pr,"uA");  g_bmU=glGetUniformLocation(pr,"uU");
        g_bmV=glGetUniformLocation(pr,"uV");  g_bmW=glGetUniformLocation(pr,"uW");
        g_bmL=glGetUniformLocation(pr,"uL");  g_bmR=glGetUniformLocation(pr,"uR");
        g_bmC=glGetUniformLocation(pr,"uC");  g_bmK=glGetUniformLocation(pr,"uK");
        g_bmF=glGetUniformLocation(pr,"uF");  g_bmT=glGetUniformLocation(pr,"uT");
        /* the bound: a unit box, every face wound CCW seen from OUTSIDE, so culling GL_FRONT draws the faces behind */
        float C[36*3]; int n=0;
        static const float UV[4][2]={{-1.f,-1.f},{1.f,-1.f},{1.f,1.f},{-1.f,1.f}};
        static const int T[6]={0,1,2, 0,2,3};
        for(int ax=0;ax<3;ax++) for(int sg=-1;sg<=1;sg+=2){
            int b=(ax+1)%3, c=(ax+2)%3; float q[4][3];
            for(int k=0;k<4;k++){ q[k][ax]=(float)sg; q[k][b]=UV[k][0]; q[k][c]=UV[k][1]; }
            for(int j=0;j<6;j++){ int k=T[(sg>0)?j:5-j]; memcpy(C+n*3,q[k],12); n++; } }
        glGenBuffers(1,&g_beamVB);
        glBindBuffer(GL_ARRAY_BUFFER,g_beamVB);
        glBufferData(GL_ARRAY_BUFFER,sizeof C,C,GL_STATIC_DRAW);
        printf("[beam] volume pass ready%c",10); fflush(stdout); }
    if(g_beamProg==(GLuint)-1) return;
    GLint prev=0; glGetIntegerv(GL_CURRENT_PROGRAM,&prev);
    GLboolean cull=glIsEnabled(GL_CULL_FACE); GLint cf=GL_BACK; glGetIntegerv(GL_CULL_FACE_MODE,&cf);
    glUseProgram(g_beamProg);
    glUniformMatrix4fv(g_bmVP,1,GL_FALSE,vp);
    glUniform3fv(g_bmEye,1,g_camEye);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_ONE);
    glEnable(GL_CULL_FACE); glCullFace(GL_FRONT);
    glBindBuffer(GL_ARRAY_BUFFER,g_beamVB);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,0);
    for(int k=1;k<=4;k++) glDisableVertexAttribArray(k);
    for(int i=0;i<g_nBeam;i++){
        const float*U=g_beamU[i];
        float rf[3]={0.f,1.f,0.f}; if(fabsf(U[1])>0.9f){ rf[0]=1.f; rf[1]=0.f; }
        float V[3]={U[1]*rf[2]-U[2]*rf[1], U[2]*rf[0]-U[0]*rf[2], U[0]*rf[1]-U[1]*rf[0]};
        float lv=sqrtf(V[0]*V[0]+V[1]*V[1]+V[2]*V[2]); V[0]/=lv; V[1]/=lv; V[2]/=lv;
        float W[3]={U[1]*V[2]-U[2]*V[1], U[2]*V[0]-U[0]*V[2], U[0]*V[1]-U[1]*V[0]};
        glUniform3fv(g_bmA,1,g_beamA[i]); glUniform3fv(g_bmU,1,U);
        glUniform3fv(g_bmV,1,V); glUniform3fv(g_bmW,1,W);
        glUniform1f(g_bmL,g_beamL[i]);
        glUniform3f(g_bmR,g_beamR[i][0],g_beamR[i][1],2.5f*fmaxf(g_beamR[i][0],g_beamR[i][1]));
        glUniform3fv(g_bmC,1,g_beamC[i]); glUniform1f(g_bmK,g_beamK[i]); glUniform1f(g_bmF,g_beamF[i]); glUniform1f(g_bmT,g_beamT[i]);
        glDrawArrays(GL_TRIANGLES,0,36); }
    /* ⛔ HAND EVERYTHING BACK: the passes after this one assume opaque, depth-writing, their own program */
    glDisable(GL_BLEND); glDepthMask(GL_TRUE);
    glCullFace((GLenum)cf); if(!cull) glDisable(GL_CULL_FACE);
    glUseProgram((GLuint)prev); }

/* ---- THE STAR (10-22, THE WORLD AT THE END OF TIME, chapter 1: WAN-TO'S STAR) ------------------------------------------
 * jacob: "chapter 1 might be the most important in the entire book, to set the scene". Wan-To lives in "the interior of a
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
static GLuint g_stbProgS=0,g_stbProgF=0,g_stbProgC=0,g_stbProgL=0,g_stbProgW=0;
static GLuint g_stbSphVB=0,g_stbSphIB=0,g_stbFaceVB=0,g_stbQuadVB=0,g_stbLoopVB=0,g_stbWebVB=0;
static int g_stbSphN=0,g_stbFaceN=0,g_stbLoopOff[STB_LOOPS+1];
static float g_stbWebP[STB_WEB][3]; static int g_stbWebL[STB_WEB*3][2]; static int g_stbWebNL=0;
/* ⛔ sh() DIES on a compile error; a new effect should fail loudly and draw nothing, not take the take down */
static GLuint stbSh(GLenum ty,const char*src){ GLuint s=glCreateShader(ty); glShaderSource(s,1,&src,NULL); glCompileShader(s);
    GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){ char lg[1024]; glGetShaderInfoLog(s,1024,NULL,lg); printf("[star] SHADER FAILED: %s%c",lg,10); fflush(stdout); return 0; }
    return s; }
static GLuint stbProg(const char*vs,const char*fs,const char*a0,const char*a1,const char*a2){
    GLuint v=stbSh(GL_VERTEX_SHADER,vs), f=stbSh(GL_FRAGMENT_SHADER,fs);
    if(!v||!f) return (GLuint)-1;
    GLuint p=glCreateProgram(); glAttachShader(p,v); glAttachShader(p,f);
    if(a0) glBindAttribLocation(p,0,a0);
    if(a1) glBindAttribLocation(p,1,a1);
    if(a2) glBindAttribLocation(p,2,a2);
    glLinkProgram(p); GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){ char lg[1024]; glGetProgramInfoLog(p,1024,NULL,lg); printf("[star] LINK FAILED: %s%c",lg,10); fflush(stdout); return (GLuint)-1; }
    return p; }
static const char*STB_COMMON=
  "float h1(vec3 p){ return fract(sin(dot(p,vec3(127.1,311.7,74.7)))*43758.5453); }\n"
  "vec3 h3(vec3 p){ return fract(sin(vec3(dot(p,vec3(127.1,311.7,74.7)),dot(p,vec3(269.5,183.3,246.1)),dot(p,vec3(113.5,271.9,124.6))))*43758.5453); }\n"
  "float vn(vec3 p){ vec3 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);\n"
  "  return mix(mix(mix(h1(i),h1(i+vec3(1.0,0.0,0.0)),f.x),mix(h1(i+vec3(0.0,1.0,0.0)),h1(i+vec3(1.0,1.0,0.0)),f.x),f.y),\n"
  "             mix(mix(h1(i+vec3(0.0,0.0,1.0)),h1(i+vec3(1.0,0.0,1.0)),f.x),mix(h1(i+vec3(0.0,1.0,1.0)),h1(i+vec3(1.0,1.0,1.0)),f.x),f.y),f.z); }\n"
  /* the GRANULES: cells of hot gas (a cellular noise whose feature points wander, so the cells boil), F1 and F2 */
  "vec2 worley(vec3 p,float t){ vec3 i=floor(p), f=fract(p); float d1=8.0, d2=8.0;\n"
  "  for(int z=-1;z<=1;z++) for(int y=-1;y<=1;y++) for(int x=-1;x<=1;x++){\n"
  "    vec3 g=vec3(float(x),float(y),float(z)); vec3 o=h3(i+g); o=0.5+0.42*sin(t*0.55+6.2831*o);\n"
  "    float d=length(g+o-f); if(d<d1){ d2=d1; d1=d; } else if(d<d2) d2=d; }\n"
  "  return vec2(d1,d2); }\n"
  /* a black body's colour (Helland's fit), for a temperature in K */
  "vec3 bb(float T){ float t=clamp(T,1000.0,40000.0)/100.0;\n"
  "  float r=t<=66.0?1.0:clamp(1.292936*pow(t-60.0,-0.1332048),0.0,1.0);\n"
  "  float g=t<=66.0?clamp(0.3900816*log(t)-0.6318414,0.0,1.0):clamp(1.1298909*pow(t-60.0,-0.0755148),0.0,1.0);\n"
  "  float b=t>=66.0?1.0:(t<=19.0?0.0:clamp(0.5432068*log(t-10.0)-1.1962541,0.0,1.0));\n"
  "  return vec3(r,g,b); }\n";
static void stbBuildMeshes(void){
    /* the sphere: a latitude-longitude grid, 128 x 256, unit radius (32-bit indices) */
    const int NL=128, NO=256; int nv=(NL+1)*(NO+1);
    float*V=(float*)malloc((size_t)nv*12); unsigned*I=(unsigned*)malloc((size_t)NL*NO*6*4); int k=0, m=0;
    for(int a=0;a<=NL;a++){ float th=3.14159265f*(float)a/NL; for(int b=0;b<=NO;b++){ float ph=6.2831853f*(float)b/NO;
        V[k++]=sinf(th)*sinf(ph); V[k++]=cosf(th); V[k++]=sinf(th)*cosf(ph); } }
    for(int a=0;a<NL;a++) for(int b=0;b<NO;b++){ unsigned i0=(unsigned)(a*(NO+1)+b), i1=i0+1, i2=i0+(unsigned)(NO+1), i3=i2+1;
        I[m++]=i0; I[m++]=i2; I[m++]=i1; I[m++]=i1; I[m++]=i2; I[m++]=i3; }
    glGenBuffers(1,&g_stbSphVB); glBindBuffer(GL_ARRAY_BUFFER,g_stbSphVB); glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)nv*12,V,GL_STATIC_DRAW);
    glGenBuffers(1,&g_stbSphIB); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,g_stbSphIB); glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)m*4,I,GL_STATIC_DRAW);
    g_stbSphN=m; free(V); free(I);
    /* a quad for the corona, two triangles of corners */
    static const float Q[]={-1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1};
    glGenBuffers(1,&g_stbQuadVB); glBindBuffer(GL_ARRAY_BUFFER,g_stbQuadVB); glBufferData(GL_ARRAY_BUFFER,sizeof Q,Q,GL_STATIC_DRAW);
    glGenBuffers(1,&g_stbFaceVB); glGenBuffers(1,&g_stbLoopVB); glGenBuffers(1,&g_stbWebVB); }
/* the two CUT FACES of the wedge: half-discs in the planes at azimuth a0 and a0+w (fans from the core) */
static void stbBuildFaces(void){
    const int NS=96; float V[2*NS*3*3]; int k=0;
    for(int f=0;f<2;f++){ float az=g_stb.cutA0+(f?g_stb.cutW:0.f), sx=sinf(az), sz=cosf(az);
        for(int i=0;i<NS;i++){ float b0=-1.5707963f+3.1415926f*(float)i/NS, b1=-1.5707963f+3.1415926f*(float)(i+1)/NS;
            V[k++]=0.f; V[k++]=0.f; V[k++]=0.f;
            V[k++]=sx*cosf(b0); V[k++]=sinf(b0); V[k++]=sz*cosf(b0);
            V[k++]=sx*cosf(b1); V[k++]=sinf(b1); V[k++]=sz*cosf(b1); } }
    glBindBuffer(GL_ARRAY_BUFFER,g_stbFaceVB); glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)k*4,V,GL_STATIC_DRAW); g_stbFaceN=k/3; }
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
    glBindBuffer(GL_ARRAY_BUFFER,g_stbLoopVB); glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(k>0?k:8)*4,V,GL_STATIC_DRAW); free(V); }
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
static void stbInitGL(void){
    if(g_stbProgS) return;
    static char fsS[9000], fsF[9000], fsC[6000];
    const char*vsS=
      "attribute vec3 p; uniform mat4 vp; uniform vec3 uC, uS; uniform float uR;\n"
      "varying vec3 vQ; varying vec3 vW;\n"
      "void main(){ vQ=p; vW=uC+p*uR*uS; gl_Position=vp*vec4(vW,1.0); }\n";   /* uS: the stretch (10-22) */
    snprintf(fsS,sizeof fsS,"precision highp float;\n%s%s",STB_COMMON,
      "varying vec3 vQ; varying vec3 vW;\n"
      "uniform vec3 uEye; uniform float uTime, uTemp, uGrade, uExpo, uRot, uNSpot; uniform vec2 uCut; uniform vec4 uSpot[16];\n"
      "float fil(vec3 p,vec3 S){ vec3 up=abs(S.y)<0.95?vec3(0.0,1.0,0.0):vec3(1.0,0.0,0.0); vec3 e1=normalize(cross(S,up)), e2=cross(S,e1);\n"
      "  vec3 t=p-S*dot(p,S); return 0.5+0.5*sin(44.0*atan(dot(t,e2),dot(t,e1)+1e-6)); }\n"
      "void main(){\n"
      "  vec3 q=normalize(vQ);\n"
      "  if(uCut.y>0.0){ float d=mod(atan(q.x,q.z)-uCut.x+12.5663706,6.2831853); if(d<uCut.y) discard; }\n"
      /* the poles turn slower than the equator (p5) */
      "  float ang=uTime*uRot*(1.0-0.3*q.y*q.y); float ca=cos(ang), sa=sin(ang);\n"
      "  vec3 p=vec3(ca*q.x-sa*q.z,q.y,sa*q.x+ca*q.z);\n"
      "  vec2 w=worley(p*40.0,uTime); vec2 w2=worley(p*115.0+vec3(3.1),uTime*1.4);\n"
      "  float lane=smoothstep(0.015,0.13,w.y-w.x), lane2=smoothstep(0.02,0.12,w2.y-w2.x), body=pow(1.0-smoothstep(0.0,0.8,w.x),1.5);\n"
      "  float sg=vn(p*6.0+vec3(0.0,uTime*0.03,0.0));\n"
      "  float T=uTemp*(0.91+0.17*body+0.06*(sg-0.5))-1000.0*(1.0-lane)-170.0*(1.0-lane2);\n"
      /* a sunspot: 'a couple of thousand Kelvin' cooler (p49) -- the umbra, and the penumbra's filaments */
      "  for(int i=0;i<16;i++){ if(float(i)>=uNSpot) break;\n"
      "    float d=acos(clamp(dot(p,uSpot[i].xyz),-1.0,1.0)), z=uSpot[i].w;\n"
      "    float um=1.0-smoothstep(0.30*z,0.42*z,d), pen=1.0-smoothstep(0.78*z,1.0*z,d);\n"
      "    T-=2000.0*um+(650.0+550.0*fil(p,uSpot[i].xyz))*(pen-um); }\n"
      /* the limb: darker, and cooler so redder */
      "  float mu=clamp(dot(normalize(uEye-vW),q),0.0,1.0);\n"
      "  float I=0.30+0.70*pow(mu,0.5);\n"
      "  vec3 col=bb(T*(0.86+0.14*mu)*uGrade)*I*uExpo;\n"
      "  gl_FragColor=vec4(col,1.0); }\n");
    snprintf(fsF,sizeof fsF,"precision highp float;\n%s%s",STB_COMMON,
      "varying vec3 vQ; varying vec3 vW;\n"
      "uniform float uTime, uExpo;\n"
      "void main(){\n"
      "  float r=length(vQ); float hs=length(vQ.xz); float th=atan(vQ.y,hs+1e-6);\n"
      "  vec3 col;\n"
      "  if(r<0.25){\n"
      /* the CORE (our fill: a quarter of the radius): fifteen million degrees (p5), seething */
      "    float n=vn(vQ*14.0+vec3(0.0,0.0,uTime*0.9))*0.6+vn(vQ*31.0-vec3(uTime*1.3))*0.4;\n"
      "    col=mix(vec3(1.0,0.94,0.78),vec3(1.0,1.0,0.97),n)*(1.05-0.25*r/0.25);\n"
      "  } else if(r<0.8){\n"
      /* the RADIATIVE ZONE, four-fifths of the way out (p4): a photon's pinball walk from particle to particle */
      "    float u=(r-0.25)/0.55; col=mix(vec3(1.0,0.86,0.52),vec3(1.0,0.62,0.22),u)*(0.92+0.08*vn(vQ*9.0+vec3(uTime*0.2)));\n"
      "    vec2 g=vec2(hs,vQ.y)*70.0; vec2 c=floor(g); float tick=floor(uTime*7.0);\n"
      "    vec3 hp=h3(vec3(c,tick)); vec2 sp=c+0.15+0.7*hp.xy;\n"
      "    float dot1=1.0-smoothstep(0.10,0.22,length(g-sp));\n"
      "    col=mix(col,vec3(1.0,1.0,0.95),dot1*step(0.55,hp.z));\n"
      "  } else {\n"
      /* the CONVECTION ZONE, the outer fifth (p4): rolling cells -- hot gas rising, cooled gas falling back */
      "    float v=(r-0.8)/0.2; float u=(th+1.5707963)/3.1415926*15.0+0.45*sin(th*5.0+1.3)+0.25*sin(th*11.0+uTime*0.05); float ci=floor(u), fu=fract(u);\n"
      "    float sgn=mod(ci,2.0)<1.0?1.0:-1.0;\n"
      "    float psi=sin(3.1415926*fu)*sin(3.1415926*v);\n"
      "    float band=0.5+0.5*sin(6.2831853*(psi*3.0-sgn*uTime*0.35+0.15*vn(vec3(fu*4.0,v*4.0,ci+uTime*0.1))));\n"
      "    float heat=0.5+0.5*sgn*cos(3.1415926*fu);\n"
      "    col=mix(vec3(0.86,0.30,0.08),vec3(1.0,0.78,0.32),heat)*(0.84+0.16*band);\n"
      "  }\n"
      /* ink between the layers, the diagram's lines */
      "  float ink=min(min(abs(r-0.25),abs(r-0.8)),abs(r-1.0));\n"
      "  col*=mix(0.25,1.0,smoothstep(0.003,0.008,ink));\n"
      "  gl_FragColor=vec4(col*uExpo,1.0); }\n");
    g_stbProgS=stbProg(vsS,fsS,"p",NULL,NULL);
    g_stbProgF=stbProg(vsS,fsF,"p",NULL,NULL);
    const char*vsC=
      "attribute vec2 c; uniform mat4 vp; uniform vec3 uC, uRt, uUp; uniform float uR;\n"
      "varying vec2 vC;\n"
      "void main(){ vC=c*2.6; vec3 w=uC+(uRt*c.x+uUp*c.y)*uR*2.6; gl_Position=vp*vec4(w,1.0); }\n";
    snprintf(fsC,sizeof fsC,"precision highp float;\n%s%s",STB_COMMON,
      "varying vec2 vC; uniform float uTime, uTemp, uGrade;\n"
      "void main(){\n"
      "  float rho=length(vC); if(rho<0.97) discard;\n"
      "  float a=atan(vC.y,vC.x);\n"
      "  float x=max(rho-1.0,0.0);\n"
      /* the corona -- thrillingly diffuse (p4) -- in streamers */
      "  float str=0.55+0.45*vn(vec3(a*3.0,x*2.5-uTime*0.04,1.7));\n"
      "  float cor=exp(-x*4.2)*0.55*str*smoothstep(0.97,1.0,rho);\n"
      /* the chromosphere's ring and its spicules -- the 'speculae' (p4) -- a hair's height above the limb */
      "  float ring=exp(-x*90.0)*0.9;\n"
      "  float spi=step(0.62,h1(vec3(floor(a*260.0),3.0,0.0)))*(1.0-smoothstep(0.0,0.035,x))*0.8;\n"
      "  vec3 col=bb(uTemp*uGrade*1.15)*cor+vec3(1.0,0.38,0.42)*(ring+spi*0.7);\n"
      "  gl_FragColor=vec4(col,1.0); }\n");
    g_stbProgC=stbProg(vsC,fsC,"c",NULL,NULL);
    const char*vsL=
      "attribute vec3 p; attribute vec3 n; attribute vec2 s;\n"
      "uniform mat4 vp; uniform vec3 uC; uniform float uR, uAng;\n"
      "varying vec3 vN; varying vec3 vW; varying vec2 vS;\n"
      "void main(){ float ca=cos(uAng), sa=sin(uAng);\n"
      "  vec3 q=vec3(ca*p.x-sa*p.z,p.y,sa*p.x+ca*p.z); vec3 nn=vec3(ca*n.x-sa*n.z,n.y,sa*n.x+ca*n.z);\n"
      "  vW=uC+q*uR; vN=nn; vS=s; gl_Position=vp*vec4(vW,1.0); }\n";
    const char*fsL=
      "precision highp float;\n"
      "varying vec3 vN; varying vec3 vW; varying vec2 vS; uniform vec3 uEye; uniform float uTime, uPh;\n"
      "void main(){ float f=abs(dot(normalize(vN),normalize(uEye-vW))); f=pow(f,1.4);\n"
      "  float pulse=0.55+0.45*sin(vS.x*26.0-uTime*2.4+uPh+vS.y*2.1);\n"
      "  vec3 col=mix(vec3(1.0,0.36,0.42),vec3(1.0,0.72,0.36),0.5+0.5*sin(vS.x*3.14159))*pulse*f*0.85;\n"
      "  gl_FragColor=vec4(col,1.0); }\n";
    g_stbProgL=stbProg(vsL,fsL,"p","n","s");
    const char*vsW=
      "attribute vec3 p; attribute vec2 uv; attribute vec4 col; uniform mat4 vp;\n"
      "varying vec2 vUV; varying vec4 vCol;\n"
      "void main(){ vUV=uv; vCol=col; gl_Position=vp*vec4(p,1.0); }\n";
    const char*fsW=
      "precision mediump float;\n"
      "varying vec2 vUV; varying vec4 vCol;\n"
      "void main(){ float d=length(vUV); float a=exp(-d*d*3.5)*(1.0-smoothstep(0.85,1.0,d));\n"
      "  gl_FragColor=vec4(mix(vCol.rgb,vec3(1.0),exp(-d*d*20.0)*0.7),vCol.a*a); }\n";
    g_stbProgW=stbProg(vsW,fsW,"p","uv","col");
    stbBuildMeshes();
    printf("[star] passes %s%s%s%s%s%c",g_stbProgS==(GLuint)-1?"SURFACE-FAILED ":"surface ",g_stbProgF==(GLuint)-1?"FACES-FAILED ":"faces ",
           g_stbProgC==(GLuint)-1?"CORONA-FAILED ":"corona ",g_stbProgL==(GLuint)-1?"LOOPS-FAILED ":"loops ",g_stbProgW==(GLuint)-1?"WEB-FAILED":"web",10);
    fflush(stdout); }
static void stbUse(GLuint pr,const float*vp){ glUseProgram(pr);
    GLint l;
    if((l=glGetUniformLocation(pr,"vp"))>=0) glUniformMatrix4fv(l,1,GL_FALSE,vp);
    if((l=glGetUniformLocation(pr,"uC"))>=0) glUniform3fv(l,1,g_stb.c);
    if((l=glGetUniformLocation(pr,"uR"))>=0) glUniform1f(l,g_stb.R);
    /* ⭐ (10-22) the PARTICLE RACK (3.3-13): drawn out along x over its rise, thinned the other ways, heaving */
    if((l=glGetUniformLocation(pr,"uS"))>=0){ float sx=1.f;
        if(g_stb.st[0]!=1.f){ float e=(g_setT-g_stb.st[2])/fmaxf(g_stb.st[1],0.01f); e=e<0.f?0.f:(e>1.f?1.f:e); e=e*e*(3.f-2.f*e);
            sx=1.f+(g_stb.st[0]-1.f)*e; sx*=1.f+0.035f*e*sinf(g_stbT*2.3f)+0.02f*e*sinf(g_stbT*5.1f+1.3f); }
        float sy=1.f/sqrtf(fmaxf(sx,0.05f)); glUniform3f(l,sx,sy*(1.f+0.02f*(sx-1.f)*sinf(g_stbT*3.7f)),sy); }
    if((l=glGetUniformLocation(pr,"uEye"))>=0) glUniform3fv(l,1,g_camEye);
    if((l=glGetUniformLocation(pr,"uTime"))>=0) glUniform1f(l,g_stbT);
    if((l=glGetUniformLocation(pr,"uTemp"))>=0) glUniform1f(l,g_stb.T);
    if((l=glGetUniformLocation(pr,"uGrade"))>=0) glUniform1f(l,0.70f);   /* OUR grade: a warm white balance on the true temperatures */
    /* "just a touch variable" (p47): a slow breath in its light */
    if((l=glGetUniformLocation(pr,"uExpo"))>=0) glUniform1f(l,1.12f*(1.f+0.025f*sinf(g_stbT*0.37f)));
    /* ⛔ 0.006 rad/s carried a spot a third of the way round in a few minutes of screen time; a star turns in weeks */
    if((l=glGetUniformLocation(pr,"uRot"))>=0) glUniform1f(l,0.0006f); }
#ifdef REVA_PACKET
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
#endif
static void starDraw(const float*vp){
    if(!g_stb.on) return;
    stbInitGL();
    float dt=(float)g_animDtS; if(dt>0.1f) dt=0.1f; if(dt<0.f) dt=0.f; g_stbT+=dt;
    if(g_stb.dirty){ g_stb.dirty=0; stbBuildFaces(); if(g_stb.nLoop>0) stbBuildLoops(); if(g_stb.webN>0) stbBuildWeb(); }
#ifdef REVA_PACKET
    if(!g_noDraw) starPacket();
#endif
    GLint prev=0; glGetIntegerv(GL_CURRENT_PROGRAM,&prev);
    GLboolean cull=glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE); glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    for(int k=1;k<=4;k++) glDisableVertexAttribArray(k);
    /* 1. the surface (the wedge discarded), 2. the cut faces -- opaque, depth-writing */
    if(g_stbProgS!=(GLuint)-1){ stbUse(g_stbProgS,vp);
        GLint l=glGetUniformLocation(g_stbProgS,"uCut"); if(l>=0) glUniform2f(l,g_stb.cutA0,g_stb.cutW);
        if((l=glGetUniformLocation(g_stbProgS,"uNSpot"))>=0) glUniform1f(l,(float)g_stb.nSpot);
        if(g_stb.nSpot>0&&(l=glGetUniformLocation(g_stbProgS,"uSpot"))>=0){ float sp[STB_SPOTS*4];
            for(int i=0;i<g_stb.nSpot;i++){ stbLatLon(g_stb.spot[i][0],g_stb.spot[i][1],sp+i*4); sp[i*4+3]=g_stb.spot[i][2]*0.0174533f; }
            glUniform4fv(l,g_stb.nSpot,sp); }
        glBindBuffer(GL_ARRAY_BUFFER,g_stbSphVB); glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,g_stbSphIB); glDrawElements(GL_TRIANGLES,g_stbSphN,GL_UNSIGNED_INT,0); }
    if(g_stb.cutW>0.f&&g_stbProgF!=(GLuint)-1){ stbUse(g_stbProgF,vp);
        glBindBuffer(GL_ARRAY_BUFFER,g_stbFaceVB); glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,0);
        glDrawArrays(GL_TRIANGLES,0,g_stbFaceN); }
    /* 3. the corona, 4. the loops -- additive, depth-tested, not written */
    glDepthMask(GL_FALSE); glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_ONE);
    if(g_stbProgC!=(GLuint)-1){ stbUse(g_stbProgC,vp);
        float f[3]={g_stb.c[0]-g_camEye[0],g_stb.c[1]-g_camEye[1],g_stb.c[2]-g_camEye[2]}; float fl=sqrtf(f[0]*f[0]+f[1]*f[1]+f[2]*f[2])+1e-6f;
        for(int q=0;q<3;q++) f[q]/=fl;
        float up0[3]={0.f,1.f,0.f}; if(fabsf(f[1])>0.95f){ up0[0]=1.f; up0[1]=0.f; }
        float rt[3]={f[1]*up0[2]-f[2]*up0[1],f[2]*up0[0]-f[0]*up0[2],f[0]*up0[1]-f[1]*up0[0]}; float rl=sqrtf(rt[0]*rt[0]+rt[1]*rt[1]+rt[2]*rt[2]);
        for(int q=0;q<3;q++) rt[q]/=rl;
        float up[3]={rt[1]*f[2]-rt[2]*f[1],rt[2]*f[0]-rt[0]*f[2],rt[0]*f[1]-rt[1]*f[0]};
        GLint l; if((l=glGetUniformLocation(g_stbProgC,"uRt"))>=0) glUniform3fv(l,1,rt);
        if((l=glGetUniformLocation(g_stbProgC,"uUp"))>=0) glUniform3fv(l,1,up);
        glBindBuffer(GL_ARRAY_BUFFER,g_stbQuadVB); glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0);
        glDrawArrays(GL_TRIANGLES,0,6); }
    if(g_stb.nLoop>0&&g_stbProgL!=(GLuint)-1){ stbUse(g_stbProgL,vp);
        glBindBuffer(GL_ARRAY_BUFFER,g_stbLoopVB);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,32,(void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,32,(void*)12);
        glEnableVertexAttribArray(2); glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,32,(void*)24);
        GLint la=glGetUniformLocation(g_stbProgL,"uAng"), lp=glGetUniformLocation(g_stbProgL,"uPh");
        for(int i=0;i<g_stb.nLoop;i++){ float mlat=0.5f*(g_stb.loop[i][0]+g_stb.loop[i][2])*0.0174533f, sl=sinf(mlat);
            if(la>=0) glUniform1f(la,g_stbT*0.0006f*(1.f-0.3f*sl*sl));   /* the loop turns with the surface under it */
            if(lp>=0) glUniform1f(lp,(float)i*1.7f);
            glDrawArrays(GL_TRIANGLES,g_stbLoopOff[i],g_stbLoopOff[i+1]-g_stbLoopOff[i]); }
        glDisableVertexAttribArray(1); glDisableVertexAttribArray(2); }
    /* 5. WAN-TO: his pieces and the threads between them -- blended over, through the star when xray (neutrinos
     *    pass through everything), a pulse running down every thread */
    if(g_stb.webN>0&&g_stbWebNL>0&&g_stbProgW!=(GLuint)-1){
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
#ifdef REVA_PACKET
        if(!g_noDraw){ static float w9[(STB_WEB+STB_WEB*3*8)*6*9]; for(int i=0;i<n;i++){ const float*o=vb+i*9; float*w=w9+i*9; w[0]=o[0]; w[1]=o[1]; w[2]=o[2]; w[3]=o[5]; w[4]=o[6]; w[5]=o[7]; w[6]=o[8]; w[7]=o[3]; w[8]=o[4]; } gfxFxTris(w9,n,g_stb.webX?GFX_FX_WEBX:GFX_FX_WEB,0); }
#endif
        stbUse(g_stbProgW,vp);
        if(g_stb.webX) glDisable(GL_DEPTH_TEST);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glBindBuffer(GL_ARRAY_BUFFER,g_stbWebVB); glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)n*36,vb,GL_STREAM_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,36,(void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,36,(void*)12);
        glEnableVertexAttribArray(2); glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,36,(void*)20);
        glDrawArrays(GL_TRIANGLES,0,n);
        glDisableVertexAttribArray(1); glDisableVertexAttribArray(2); glEnable(GL_DEPTH_TEST); }
    glDisable(GL_BLEND); glDepthMask(GL_TRUE);
    if(cull) glEnable(GL_CULL_FACE);
    glUseProgram((GLuint)prev); }

/* ---- THE SKYBOX PASS ---------------------------------------------------------------
 * jacob: "we need a real skybox" and "we can see the end of the envirmonet".
 * ⭐ ONE PASS, BOTH PROBLEMS. The ground runs out (28x28 tiles plus one of run-off), and
 * the water was reflecting a sky nobody could see. Drawing the SAME skyColor() the water
 * samples means the horizon exists and the reflection matches what is above it.
 * ⭐ A BOX AROUND THE EYE, not a fullscreen quad: a fullscreen pass needs the INVERSE
 * view-projection to recover a ray, and drawSet is handed uniform locations, not matrices.
 * On a cube centred on the camera the interpolated world position minus the eye IS the
 * ray, and it rides the view-projection the frame already has. */
static GLuint g_skyProg=0, g_skyVB=0;
static GLint  g_skVP=-1,g_skEye=-1,g_skSunDir=-1,g_skSunCol=-1,g_skSunI=-1,
              g_skZen=-1,g_skHz=-1,g_skGnd=-1,g_skT=-1;

static void skyDraw(void){
    if(g_setSpace){ spaceSkyDraw(); return; }   /* ⭐ 10-22: deep space has no clouds */
    if(!g_skyProg){
        const char*vs=
          "attribute vec3 p;\n"
          "uniform mat4 vp; uniform vec3 uEye;\n"
          "varying vec3 vdir;\n"
          "void main(){ vec3 wp=uEye+p*90.0; vdir=p; gl_Position=vp*vec4(wp,1.0); }\n";
        static char fs[3072];
        snprintf(fs,sizeof fs,
          "precision mediump float;\n varying vec3 vdir;\n"
          "%s"
          "void main(){ gl_FragColor=vec4(skyColor(vdir),1.0); }\n", SKY_GLSL);
        GLuint pr=glCreateProgram();
        glAttachShader(pr,sh(GL_VERTEX_SHADER,vs));
        glAttachShader(pr,sh(GL_FRAGMENT_SHADER,fs));
        glBindAttribLocation(pr,0,"p");
        glLinkProgram(pr);
        GLint ok=0; glGetProgramiv(pr,GL_LINK_STATUS,&ok);
        if(!ok){ char log[512]; glGetProgramInfoLog(pr,512,NULL,log);
                 printf("[sky] LINK FAILED: %s%c",log,10); fflush(stdout); g_skyProg=(GLuint)-1; return; }
        g_skyProg=pr;
        g_skVP=glGetUniformLocation(pr,"vp");        g_skEye=glGetUniformLocation(pr,"uEye");
        g_skSunDir=glGetUniformLocation(pr,"uSunDir");g_skSunCol=glGetUniformLocation(pr,"uSunCol");
        g_skSunI=glGetUniformLocation(pr,"uSunI");   g_skZen=glGetUniformLocation(pr,"uSkyZen");
        g_skT=glGetUniformLocation(pr,"uSkyT");
        g_skHz=glGetUniformLocation(pr,"uSkyHz");    g_skGnd=glGetUniformLocation(pr,"uGround");
        static const float C[]={
            -1,-1,-1,  1,-1,-1,  1, 1,-1, -1,-1,-1,  1, 1,-1, -1, 1,-1,
            -1,-1, 1, -1, 1, 1,   1, 1, 1, -1,-1, 1,  1, 1, 1,  1,-1, 1,
            -1,-1,-1, -1, 1,-1,  -1, 1, 1, -1,-1,-1, -1, 1, 1, -1,-1, 1,
             1,-1,-1,  1,-1, 1,   1, 1, 1,  1,-1,-1,  1, 1, 1,  1, 1,-1,
            -1, 1,-1,  1, 1,-1,   1, 1, 1, -1, 1,-1,  1, 1, 1, -1, 1, 1,
            -1,-1,-1, -1,-1, 1,   1,-1, 1, -1,-1,-1,  1,-1, 1,  1,-1,-1 };
        glGenBuffers(1,&g_skyVB);
        glBindBuffer(GL_ARRAY_BUFFER,g_skyVB);
        glBufferData(GL_ARRAY_BUFFER,sizeof C,C,GL_STATIC_DRAW);
        printf("[sky] skybox ready%c",10); fflush(stdout); }
    if(g_skyProg==(GLuint)-1) return;
    GLint prev=0; glGetIntegerv(GL_CURRENT_PROGRAM,&prev);
    /* ⛔ NO DEPTH AT ALL. The sky is infinitely far: writing depth would occlude the world,
     * testing it would fight the clear. Drawn first, painted over by everything. */
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND);
    glUseProgram(g_skyProg);
    glUniformMatrix4fv(g_skVP,1,GL_FALSE,g_watVP);
    glUniform3fv(g_skEye,1,g_camPos);
    glUniform3fv(g_skSunDir,1,g_sun);
    glUniform3fv(g_skSunCol,1,g_sunC);
    glUniform1f(g_skSunI,g_sun[3]>0.f?g_sun[3]:0.15f);
    glUniform1f(g_skT,(float)(now_raw_ms()*0.001));   /* the cloud deck drifts on the wall clock */
    glUniform3f(g_skZen,g_sky[0]*1.35f,g_sky[1]*1.45f,g_sky[2]*1.75f);
    { float hz[3]; skyHorizonC(hz); glUniform3f(g_skHz,hz[0],hz[1],hz[2]); }
    glUniform3f(g_skGnd,g_sky[0]*0.55f,g_sky[1]*0.58f,g_sky[2]*0.50f);
    glBindBuffer(GL_ARRAY_BUFFER,g_skyVB);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,0);
    glDrawArrays(GL_TRIANGLES,0,36);
    glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
    glUseProgram((GLuint)prev); }

static void watDraw(GLint uModel,GLint uPal,GLint uBind,GLint uUse,GLint uTint,GLint uAlpha){
    (void)uModel;(void)uPal;(void)uBind;(void)uUse;(void)uTint;(void)uAlpha;
    if(!g_nWat) return;
    static int stamp=-1;
    int step=((int)g_frameStamp!=stamp); stamp=(int)g_frameStamp;
    watProgInit();
    if(!g_watProg) return;
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    /* ⛔ THE WATER OWNS THE PROGRAM FOR THIS PASS, so the caller's program has to come
     * back afterwards or every mesh drawn after the water renders with a water shader. */
    GLint g_watPrev=0; glGetIntegerv(GL_CURRENT_PROGRAM,&g_watPrev);
    glUseProgram(g_watProg);
    glUniformMatrix4fv(g_wVP,1,GL_FALSE,g_watVP);
    glUniform3fv(g_wEye,1,g_camPos);
    glUniform3fv(g_wSunDir,1,g_sun);
    glUniform3fv(g_wSunCol,1,g_sunC);
    glUniform1f(g_wSunI,g_sun[3]>0.f?g_sun[3]:0.15f);
    glUniform1f(g_wT,(float)(now_raw_ms()*0.001));
    /* the zenith/horizon pair is derived from the ambient the sun rig already publishes,
     * so a sunset sky and a sunset reflection cannot disagree */
    glUniform3f(g_wZen,g_sky[0]*1.35f,g_sky[1]*1.45f,g_sky[2]*1.75f);
    { float hz[3]; skyHorizonC(hz); glUniform3f(g_wHz,hz[0],hz[1],hz[2]); }
    glUniform3f(g_wGnd,g_sky[0]*0.55f,g_sky[1]*0.58f,g_sky[2]*0.50f);
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
            glGenBuffers(1,&W->vb); glGenBuffers(1,&W->nb); glGenBuffers(1,&W->ib);
            glGenBuffers(1,&W->jb); glGenBuffers(1,&W->wb);
            glBindBuffer(GL_ARRAY_BUFFER,W->vb); glBufferData(GL_ARRAY_BUFFER,sizeof W->pos,W->pos,GL_DYNAMIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER,W->nb); glBufferData(GL_ARRAY_BUFFER,sizeof W->nrm,W->nrm,GL_DYNAMIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER,W->jb); glBufferData(GL_ARRAY_BUFFER,sizeof jz,jz,GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER,W->wb); glBufferData(GL_ARRAY_BUFFER,sizeof wz,wz,GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,W->ib); glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)(n*2),idx,GL_STATIC_DRAW);
#ifdef REVA_PACKET
            { static unsigned ix9[(WAT_G-1)*(WAT_G-1)*6]; for(unsigned k9=0;k9<n;k9++) ix9[k9]=idx[k9]; glGenBuffers(1,&W->ib32); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,W->ib32); glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)(n*4),ix9,GL_STATIC_DRAW); }   /* the packet's indices are 32 bits */
#endif
            W->gl=1; }
        glBindBuffer(GL_ARRAY_BUFFER,W->vb); glBufferSubData(GL_ARRAY_BUFFER,0,sizeof W->pos,W->pos);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,0);
        glBindBuffer(GL_ARRAY_BUFFER,W->nb); glBufferSubData(GL_ARRAY_BUFFER,0,sizeof W->nrm,W->nrm);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,0,0);
        glBindBuffer(GL_ARRAY_BUFFER,W->jb); glEnableVertexAttribArray(2); glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_FALSE,0,0);
        glBindBuffer(GL_ARRAY_BUFFER,W->wb); glEnableVertexAttribArray(3); glVertexAttribPointer(3,4,GL_UNSIGNED_BYTE,GL_TRUE,0,0);
        glDisableVertexAttribArray(4);
        glUniform3f(g_wTint,W->tint[0],W->tint[1],W->tint[2]);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,W->ib);
        glDrawElements(GL_TRIANGLES,(GLsizei)W->nIdx,GL_UNSIGNED_SHORT,0);
#ifdef REVA_PACKET
        if(!g_noDraw){ static const float I9[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; GfxMesh m9; memset(&m9,0,sizeof m9); m9.id=((unsigned)GFX_KIND_PROP<<28)|((0xf100u+(unsigned)q)<<12); m9.vb[0]=W->vb; m9.vb[1]=W->nb; m9.vb[4]=W->ib32; m9.nIndex=W->nIdx; m9.nVerts=WAT_N; m9.model=I9; memcpy(m9.tint,W->tint,12); m9.flags=GFX_WATER|GFX_BLEND; gfxMesh(&m9); }
#endif
        }
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    glUseProgram(g_watPrev); }

