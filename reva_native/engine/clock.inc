/* ---- THE CLOCK, AND HOW TO STOP IT LYING -------------------------------------------
 * REVA_FIXEDSTEP=<fps> replaces wall time with a VIRTUAL clock that advances exactly
 * 1000/fps per rendered frame. Everything downstream -- tsec, dt, the beat PLL, the
 * scene timeline, the menus -- reads now_ms(), so one substitution makes the whole
 * engine reproducible instead of each subsystem needing its own capture mode.
 *
 * ⛔ WHY: without it two runs stopped at the same frame NUMBER are different INSTANTS,
 * because frame N lands wherever the machine happened to be. That is not a theoretical
 * hazard -- a look-at change was "verified" this way by comparing two captures at frame
 * 700, and the difference was the head layer's own drift, not the change. Any pixel
 * regression, any A/B of two builds, and any "did my edit do that?" needs this on.
 * REVA_SCENEFPS already did it for the scene timeline alone; this generalises it. */
static double g_fixedStepMs=-1.0, g_virtMs=0.0;
/* ---- THE TICK (2026-09-30): the sim clock only ever moves by whole ticks ------------------
 * The N64 lesson, as the recomp projects retell it: the game runs at its own fixed tick and
 * the picture is whatever rate the glass can manage. Until now that was true only under
 * REVA_FIXEDSTEP (a bench switch); the show ran on wall time, and the four per-frame physics
 * steps used a constant 1/60 whatever the frame took -- so a Pi at 20 fps ran the actors at a
 * third speed while their clips played at full. Now g_virtMs is THE clock: every frame it
 * advances by k whole ticks (REVA_TICK=<hz>, default 60), k from the wall time owed, capped
 * at TICK_MAX so a stall is not a debt, snapped to whole ticks against vsync jitter, and a
 * display faster than the tick waits for the boundary rather than draw the same instant
 * twice. REVA_FIXEDSTEP is k=1 with the tick equal to the step, byte for byte what it was.
 * ⭐ (11-02) ONE TICK AT A TIME. k ticks used to be one long step, so the rules changed with the
 * frame rate: a body at 30 fps took one 33 ms step where a body at 60 took two of 16. Now a frame
 * that owes k passes through the whole loop k times, one tick each, and only the last pass draws
 * (clockFrame, g_noDraw). simStepS() is always one tick. REVA_CADENCE=1,1,1,1,2 forces the pattern
 * of ticks per frame under REVA_FIXEDSTEP: any cadence must print the same [simhash] lines.
 * ⛔ Render interpolation between ticks needs the sim/draw split (they are one loop today); a
 * 60 Hz tick on a 60 Hz panel never shows it.
 * The tick count since boot is the sim hash's frame number (REVA_SIMHASH). */
static double g_tickMs=1000.0/60.0;
static int    g_tickN=1;           /* ticks this frame, 1..TICK_MAX */
static double g_tickAcc=0.0;       /* wall ms owed to the sim */
static long   g_tickTotal=0;       /* ticks since boot */
static double g_wallDt=1.0/60.0;   /* the frame's REAL seconds: the HUD fps and the ladder read this, never the sim */
#define TICK_MAX 4
static double wall_ms(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec*1000.0+ts.tv_nsec/1e6; }
/* ---- STOP MOTION: THE EXPOSURE CLOCK ------------------------------------------------
 * jacob, 08-28, on the talking faces: "it kinda looks like robot chicken... i kinda dig
 * it". design/stop_motion.md banked the recipe and named what it was missing: a step rate
 * applied to the ANIMATION clock only, so the lens keeps moving smoothly.
 *
 * REVA_FIXEDSTEP could not do it. It steps the WHOLE engine, the scene clock included, so
 * a 24fps render with bodies at 12 put the picture at double speed against the wav.
 *
 * So there are two clocks. now_ms() is the EXPOSURE clock: under ctl `step=<fps>` (or a
 * scene line's `step=`) it advances only in whole steps, and every frame in between
 * re-exposes the same instant -- dt is zero, the phases hold, the eases hold, the actors
 * hold. now_raw_ms() is the lens's clock, and camDirector alone is handed it. The scene
 * clock keeps its own fixed step, which is what keeps the wav in sync.
 * step=0 is "on ones": now_ms() IS now_raw_ms() and nothing else in the engine changes. */
static float  g_stepFps=0.f;      /* exposures per second; 0 = off */
static float  g_stepCtl=0.f;      /* what the ctl asked for -- a scene starts and ends on it */
static double g_animMs=-1.0;      /* the exposure clock */
static double g_animDtS=0.0;      /* seconds the exposure clock moved this frame */
static unsigned long g_frameNo=0; /* ⛔ rendered frames, so a SIMULATION inside the draw steps once per frame */
static double g_camDt=1.0/60.0;   /* the lens's own frame dt */
static long   g_stepIdx=0;        /* which exposure this is -- the boil's seed */
static float  g_boil=1.f;         /* ctl boil=: the animator's hand; 0 = a perfect robot */
/* ---- TONE: a highlight shoulder --------------------------------------------------------
 * Daylight is lit as base*(sky + sun*cos) -- about 1.8x a texture's own brightness at noon --
 * and nothing ever mapped it back into range, so white siding at 0.95 landed at 1.3 and
 * clipped. MEASURED on ROBOT NEKO ep 1 (2026-09-10): 25-41% of every frame was pure white.
 * `tone=<knee>` leaves everything below the knee exactly alone and rolls everything above it
 * off exponentially toward a WHITE POINT of 0.96, per channel -- toward 1.0 it still left 9.3%
 * of a sunlit frame at 252/255. 0 = off, which is byte-for-byte the old shader
 * output -- so the film and every door keep their look until a scene or the ctl asks. */
static float  g_tone=0.f, g_toneCtl=0.f;
/* ⛔ MUST BE CALLED BEFORE ANYTHING READS THE MODE, not just lazily from now_ms().
 * It was lazy, and srand() runs BEFORE the first clock read -- so the "fixed-step
 * implies the bench seed" rule never fired and the crowd was still seeded from
 * time(NULL). Two captures of the same frame then differed 23% at the very first
 * capturable frame, which reads exactly like a deep engine nondeterminism and is
 * really just an init-order bug. */
static void clockInit(void){
    static int init=0; if(init) return; init=1;
    const char*e=getenv("REVA_FIXEDSTEP");
    if(e&&atof(e)>0.0) g_fixedStepMs=1000.0/atof(e);
    { const char*t=getenv("REVA_TICK"); if(t&&atof(t)>0.0) g_tickMs=1000.0/atof(t); }
    if(g_fixedStepMs>0.0) g_tickMs=g_fixedStepMs;   /* a measured run: the tick IS the step */ }
static double now_raw_ms(void){ clockInit(); return g_virtMs; }   /* whole ticks only; wall_ms() is the wall */
static double now_ms(void){
    return (g_stepFps>0.f&&g_animMs>=0.0)?g_animMs:now_raw_ms(); }
/* REVA_CLEAR=r,g,b -- override the stage backdrop. It exists for one test:
 * facecheck.py's SEE-THROUGH check has to tell "backdrop AROUND her head" from
 * "backdrop THROUGH her head", and it cannot do that against the lab's near-black
 * studio because the inside of a mouth is near-black too, on purpose. Shot against a
 * colour the club never renders, any such pixel inside her mouth arrived through a
 * hole in her skull. Returns 1 if it overrode. */
static float g_sun[4], g_sunC[3], g_sky[3];   /* the sun rig; defined once, used by the sky too */
static int clearOverride(void){
    static int on=-1; static float c[3];
    if(on<0){ on=0; const char*e=getenv("REVA_CLEAR");
        if(e&&sscanf(e,"%f,%f,%f",&c[0],&c[1],&c[2])==3) on=1; }
    if(on){ glClearColor(c[0],c[1],c[2],1.f); return 1; }
    /* A SUN MEANS A SKY. Black above the horizon is right for a club and wrong for a
     * street -- and the sky ambient is already the colour the world is being lit by, so
     * clearing to a lifted version of it costs nothing and makes the set sit in daylight
     * instead of in space. Night is sun=off, which puts the black straight back. */
    if(g_sun[3]>0.f){
        glClearColor(g_sky[0]*1.55f+0.10f, g_sky[1]*1.55f+0.13f, g_sky[2]*1.55f+0.18f, 1.f);
        return 1; }
    return 0; }
/* one tick per RENDERED frame; a no-op unless REVA_FIXEDSTEP is set */
static void sunInit(void){ g_sun[0]=0.f; g_sun[1]=1.f; g_sun[2]=0.f; g_sun[3]=0.f;
    g_sunC[0]=1.f; g_sunC[1]=0.97f; g_sunC[2]=0.90f;
    g_sky[0]=0.30f; g_sky[1]=0.34f; g_sky[2]=0.42f; }
/* ================= Z6 THE DAY ======================================================
 * hour, elevation, azimuth, intensity, sun rgb, sky rgb. One table, because these are
 * not independent quantities: a low sun is a warm sun and a warm sun sits under a sky
 * that has gone violet. Keyframed rather than computed from an ephemeris -- this is a
 * film, and the golden hour lasts as long as the cutting needs it to.
 * ⭐ NIGHT IS A WEAK BLUE DIRECTIONAL over a near-black sky, not sun=off: w==0 is the
 * club's dark-room path, which has no sky and no direction and is useless outdoors. */
typedef struct { float h, el, az, it, r,g,b, sr,sg,sb; } TodKey;
static const TodKey TOD[]={
 /*  h    el    az     I     sun r,g,b            sky r,g,b            */
 { 0.0f,  42.f, 320.f, 0.10f, 0.55f,0.62f,0.88f,  0.030f,0.036f,0.062f },  /* deep night */
 { 4.6f,  26.f, 350.f, 0.10f, 0.55f,0.62f,0.88f,  0.036f,0.042f,0.072f },
 { 5.6f,  -3.f,  76.f, 0.06f, 0.80f,0.60f,0.62f,  0.105f,0.098f,0.150f },  /* first light */
 { 6.6f,   6.f,  84.f, 0.55f, 1.00f,0.63f,0.40f,  0.215f,0.200f,0.245f },  /* dawn */
 { 7.4f,  14.f,  93.f, 0.95f, 1.00f,0.80f,0.60f,  0.315f,0.350f,0.450f },  /* the film opens */
 { 9.0f,  30.f, 112.f, 1.20f, 1.00f,0.90f,0.76f,  0.400f,0.455f,0.570f },
 {12.0f,  66.f, 178.f, 1.38f, 1.00f,0.98f,0.94f,  0.460f,0.520f,0.640f },  /* noon */
 {15.0f,  40.f, 238.f, 1.24f, 1.00f,0.94f,0.84f,  0.440f,0.495f,0.605f },
 {17.4f,  16.f, 262.f, 0.92f, 1.00f,0.80f,0.56f,  0.380f,0.390f,0.470f },
 {18.6f,   6.f, 270.f, 0.62f, 1.00f,0.62f,0.34f,  0.310f,0.270f,0.300f },  /* golden */
 {19.4f,   0.5f,276.f, 0.28f, 1.00f,0.44f,0.24f,  0.190f,0.155f,0.200f },  /* sunset */
 {20.2f,  -6.f, 282.f, 0.08f, 0.72f,0.46f,0.52f,  0.088f,0.086f,0.135f },  /* twilight */
 {21.2f,  18.f, 298.f, 0.10f, 0.55f,0.62f,0.88f,  0.038f,0.044f,0.075f },  /* moon up */
 {24.0f,  42.f, 320.f, 0.10f, 0.55f,0.62f,0.88f,  0.030f,0.036f,0.062f }};
static float g_tod=-1.f;        /* <0: the clock is off and sun=/sky= own the light */
static float g_exposure=0.f;    /* a set's own, on the HDR road: `exposure 1.2`. 0 is the default */
static float g_todRate=0.f;     /* hours per real second, for a timelapse */
static void todApply(float h){
    int n=(int)(sizeof TOD/sizeof TOD[0]);
    h=fmodf(h,24.f); if(h<0.f) h+=24.f;
    int i=0; while(i<n-2&&TOD[i+1].h<=h) i++;
    const TodKey*A=&TOD[i],*B=&TOD[i+1];
    float span=B->h-A->h, u=(span>1e-4f)?(h-A->h)/span:0.f;
    if(u<0.f) u=0.f;
    if(u>1.f) u=1.f;
    u=u*u*(3.f-2.f*u);                                  /* ease, so a cut never snaps */
    #define L(f) (A->f+(B->f-A->f)*u)
    float e=L(el)*0.0174532925f, a2=L(az)*0.0174532925f;
    g_sun[0]=cosf(e)*sinf(a2); g_sun[1]=sinf(e); g_sun[2]=cosf(e)*cosf(a2);
    g_sun[3]=L(it);
    g_sunC[0]=L(r); g_sunC[1]=L(g); g_sunC[2]=L(b);
    g_sky[0]=L(sr); g_sky[1]=L(sg); g_sky[2]=L(sb);
    #undef L
    /* the sun never goes below the horizon as a LIGHT -- a directional from underneath
     * lights faces from the chin up. Below the horizon it is the moon, already blue and
     * already weak, so lift the direction and leave the colour alone. */
    if(g_sun[1]<0.06f){ g_sun[1]=0.06f;
        float l9=sqrtf(g_sun[0]*g_sun[0]+g_sun[1]*g_sun[1]+g_sun[2]*g_sun[2]);
        if(l9>1e-5f){ g_sun[0]/=l9; g_sun[1]/=l9; g_sun[2]/=l9; } } }
/* how many ticks the wall clock owes this frame */
static double g_ledFieldMs;
static int clockOwed(void){
    static double last=-1.0; double w=wall_ms(); if(last<0.0) last=w;
    double d=w-last; last=w; g_wallDt=d*0.001;
    if(g_fixedStepMs>0.0) return 1;
    /* vsync jitter is not time. ⛔ (11-02) IT WAS +-20%, and a frame on a 100 Hz panel paced to 60 is 20 ms: within 20% of one tick, so every
     * door on jacob's monitor ran at 5/6 speed, real time in, one tick out. Jitter is a fraction of a millisecond; a 20 ms frame is 20 ms. */
    for(int n=1;n<=TICK_MAX;n++) if(fabs(d-n*g_tickMs)<0.05*g_tickMs){ d=n*g_tickMs; break; }
    g_tickAcc+=d; if(g_tickAcc>TICK_MAX*g_tickMs) g_tickAcc=TICK_MAX*g_tickMs;
    int k=(int)floor(g_tickAcc/g_tickMs);
#ifndef REVA_SDL
    /* ⛔ (11-02) THE FLIP IS THE ONLY PACER ON A DIRECT ROAD. The sleep below is for a window whose vsync can't be trusted. Here, one jittered frame (jacob: "its probably from the pi throttling itself cuz it thinks
     * its not working and should conserve power": the CPU governor's clock changes) left the clock short of a tick, it slept, and each sleep measured the next frame from its own end. That locked the loop to its own
     * 60.000 Hz against a 60.055 Hz field, and a field went missing every 1 / (60.055 - 60) = 18.2 s, for as long as the game ran. So: short of a tick is one tick with the debt forgiven. A real gap still pays
     * (d carries, so a 50 Hz field gets 6 ticks in 5 frames), and the sim stays at a field a tick, which runs 0.09% fast at 240p and nothing can feel. */
    if(k<1){ g_tickAcc=0.0; return 1; }
    /* ⛔ (11-03) A LATE FRAME ISN'T A DEBT. It owed the next frame two ticks, and every tick costs a whole pass, so a game a little over budget took twice as long, then owed three, then four: 18 ms of work became 73, and 13 fps. The schedule is one tick a flip, and what a field needs is all it pays:
     * one at 60 Hz, up to two at 50. Over budget, the game slows by a hair and each frame still costs what it costs, which is what a console does. */
    { int most=g_ledFieldMs>0.0?(int)ceil(g_ledFieldMs/g_tickMs-0.01):TICK_MAX; if(k>most){ k=most; g_tickAcc=0.0; return k; } }
    g_tickAcc-=k*g_tickMs; return k;
#endif
    if(k<1){ double rem=g_tickMs-g_tickAcc; struct timespec ts={0,(long)(rem*1e6)};
             /* ⛔ (11-02) A sliver short of a tick is not worth a sleep: on Windows a sleep can run many times long, and it broke a locked 100 fps down to 60.
              * vsync is pacing that frame already. The debt is carried below, and only a real gap sleeps. */
             if(rem>g_tickMs*0.25) nanosleep(&ts,NULL);
             /* ⛔ (11-02) This reset the clock after the sleep, and a sleep on Windows runs long: what it overslept was never paid for. Now it is owed. */
             double w2=wall_ms(); g_tickAcc+=w2-last; last=w2; k=(int)floor(g_tickAcc/g_tickMs);
             if(k<1) k=1; else if(k>TICK_MAX) k=TICK_MAX;   /* woke early: the tick is still taken, and the debt carried */
             g_tickAcc-=k*g_tickMs; }
    else g_tickAcc-=k*g_tickMs;
    return k; }
/* the menus' clock: their loops have no sim to step, so what's owed is paid at once */
__attribute__((unused)) static void clockTick(void){
    clockInit(); int k=clockOwed(); double s=(g_fixedStepMs>0.0)?g_fixedStepMs:g_tickMs;
    g_tickN=k; g_virtMs+=k*s; g_tickTotal+=k; }
/* the main loop's clock: one call a pass, one tick a pass. g_tickFirst says this pass starts a frame */
static int g_tickOwed=0,g_tickFirst=1;
static void clockFrame(void){
    clockInit(); g_tickFirst=(g_tickOwed<=0);
    if(g_tickFirst){ int k=clockOwed();
        if(g_fixedStepMs>0.0){ static int cad[32],nc=-1,ci=0;
            if(nc<0){ nc=0; const char*e=getenv("REVA_CADENCE"); while(e&&*e&&nc<32){ cad[nc++]=atoi(e); e=strchr(e,','); if(e) e++; } }
            if(nc>0){ k=cad[ci++%nc]; if(k<1) k=1; } }
        g_tickOwed=k; }
    g_tickOwed--; g_noDraw=(g_tickOwed>0); g_tickN=1;
    g_virtMs+=(g_fixedStepMs>0.0)?g_fixedStepMs:g_tickMs; g_tickTotal++; }
static float simStepS(void){ return (float)(((g_fixedStepMs>0.0)?g_fixedStepMs:g_tickMs)*0.001); }   /* one tick, always */
/* ---- THE LADDER (2026-09-30): one build, from a Pi 4 to a desktop -----------------------------
 * REVA_TIER=pi|low|mid|high|ultra|auto (default high: byte for byte what the club drew before).
 * A tier is the render scale (the 3D rect inside the post target), the sun map size (boot only:
 * the map is allocated once), bloom, FXAA and the cloth substeps. ⛔ There is no `auto` rung any
 * more, jacob 11-02: "I've never liked automatic cuts that measure performance, normally leads to
 * a seesaw effect." Budgets hold the frame. design/platform_tiers.md has the Pi 4 numbers the
 * rungs came from and what each rung asks of the fill rate. */
typedef struct { const char*name; float rscale; int sunMap; float bloom; int fxaa; int clothSub; } Tier;
static const Tier TIERS[]={
    {"pi",   0.50f, 512, 0.00f,0,1},
    {"low",  0.67f,1024, 0.00f,1,2},
    {"mid",  0.85f,2048, 0.12f,1,4},
    {"high", 1.00f,2048, 0.12f,1,4},
    {"ultra",1.00f,4096, 0.12f,1,8},
    {"film", 1.00f,4096, 0.12f,1,8}};   /* film's own: every sample and light the old look had, on the old look's colours. An episode's palette was made on it */
#define TIER_N 6
static int g_tier=3, g_clothSub=4, g_sunMap=2048;
static void tierApply(int t,int boot);
static void tierInit(void){
    const char*e=getenv("REVA_TIER"); int t=3;
    { const char*am=getenv("REVA_AUTOMODE"); if(am&&atoi(am)==11) t=5; }   /* the lab and a scene are film's */
#if defined(REVA_VK)&&!defined(REVA_SDL)
    t=0;   /* the direct road is the Pi, and its picture is the budget's */
#endif
    if(e&&e[0]){ if(e[0]>='0'&&e[0]<='9') t=atoi(e);
        else { t=-1; for(int i=0;i<TIER_N;i++) if(!strcmp(e,TIERS[i].name)) t=i;
               if(t<0){ printf("[tier] REVA_TIER=%s is not a rung (pi low mid high ultra film) -- high\n",e); t=3; } } }
    if(t<0) t=0;
    if(t>=TIER_N) t=TIER_N-1;
    tierApply(t,1); }
/* ---- THE SIM HASH (2026-09-30): the cross-platform receipt --------------------------------
 * Pixels differ between llvmpipe, v3d and ANGLE by rasterizer; simulation state need not. Each
 * frame mixes what the sim decided -- the hero's world matrices after FK, her position and
 * heading, every crowd slot's place and state -- into two FNV-1a hashes: EXACT (the float bits;
 * a regression receipt on one machine) and COARSE (each float rounded to 1e-3 first; what two
 * machines with different libm last bits can still agree on). REVA_SIMHASH=<every N frames>
 * prints them; a REVA_SHOT frame always does. ⛔ FMA contraction changes the last bit between
 * x86 and ARM: the build scripts pass -ffp-contract=off so the exact hash has a chance. */
static unsigned long long g_shEx=0, g_shCo=0; static int g_shEvery=-1;
/* REVA_SIMDUMP=<tick>:<file> (11-02): every value the hash mixes at that tick, by name, in hex and decimal. When two machines' hashes differ, this says which value did. */
static long g_shDumpTick=-1; static FILE*g_shDumpF=NULL; static const char*g_shDumpTag="";
static void simHashBegin(void){ g_shEx=g_shCo=1469598103934665603ull;
    if(g_shEvery<0){ const char*e=getenv("REVA_SIMHASH"); g_shEvery=e?atoi(e):0;
        const char*d=getenv("REVA_SIMDUMP"); if(d&&strchr(d,':')){ g_shDumpTick=atol(d); g_shDumpF=fopen(strchr(d,':')+1,"w"); } } }
static void simHashMix(const float*f,unsigned n){
    if(g_shDumpF&&g_tickTotal==g_shDumpTick) for(unsigned i=0;i<n;i++){ unsigned u; memcpy(&u,&f[i],4); fprintf(g_shDumpF,"%s %u %08x %.9g\n",g_shDumpTag,i,u,f[i]); }
    for(unsigned i=0;i<n;i++){ unsigned u; memcpy(&u,&f[i],4);
        for(int b=0;b<4;b++){ g_shEx^=(u>>(8*b))&255u; g_shEx*=1099511628211ull; }
        long q=lround((double)f[i]*1000.0); unsigned long long uq=(unsigned long long)q;
        for(int b=0;b<8;b++){ g_shCo^=(uq>>(8*b))&255u; g_shCo*=1099511628211ull; } } }
/* a game's own state, as bytes: it's a game's to lay out, and some of it isn't floats. Exact only: the coarse hash is for rounding floats */
static void simHashBytes(const void*p,size_t n){ const unsigned char*b=(const unsigned char*)p;
    if(g_shDumpF&&g_tickTotal==g_shDumpTick) for(size_t i=0;i+4<=n;i+=4){ unsigned u; memcpy(&u,b+i,4); fprintf(g_shDumpF,"%s %u %08x\n",g_shDumpTag,(unsigned)i,u); }
    for(size_t i=0;i<n;i++){ g_shEx^=b[i]; g_shEx*=1099511628211ull; } }
static void simHashPrint(const char*why){
    printf("[simhash] tick %ld exact %016llx coarse %016llx (%s)\n",g_tickTotal,g_shEx,g_shCo,why); fflush(stdout); }
/* THE EXPOSURE. Once per rendered frame, straight after clockTick(). Off, it tracks the raw
 * clock exactly, so switching stop motion on mid-take starts from NOW rather than from
 * wherever it was last left. On, it advances only by WHOLE steps -- floor, not round, so a
 * 24fps capture at step=12 is hold, expose, hold, expose and never two exposures running. */
static void animTick(void){
    double r=now_raw_ms(), was=g_animMs;
    g_frameNo++;
    if(g_stepFps<=0.f||g_animMs<0.0){
        g_animMs=r; g_stepHeld=0; g_animDtS=(was<0.0)?0.0:(r-was)*0.001; return; }
    double st=1000.0/(double)g_stepFps;
    double n=floor((r-g_animMs)/st+1e-6);
    if(n>=1.0){ g_animMs+=n*st; g_stepHeld=0; g_stepIdx++; }
    else g_stepHeld=1;
    g_animDtS=(g_animMs-was)*0.001; }
/* A per-frame step that used to be a constant (the actors, the day, the partner's walk mix)
 * is the exposure's step while stepping, and exactly what it always was otherwise. */
static float animStepS(float legacy){ return (g_stepFps>0.f)?(float)g_animDtS:legacy; }
/* THE ANIMATOR'S HAND. A puppet is never put back exactly where it was: every exposure it
 * lands a fraction of a degree and a hair's breadth off (design/stop_motion.md: "a per-hold
 * sub-degree wobble on the root would sell it more than any amount of polish"). Seeded by
 * the exposure index, so a held frame holds its error too, and a take is the same twice. */
static float stepBoil(int who,int axis){
    if(g_stepFps<=0.f||g_boil<=0.f) return 0.f;
    unsigned h=(unsigned)g_stepIdx*2654435761u ^ ((unsigned)who*40503u+(unsigned)axis*9973u+0x9e37u);
    h^=h>>13; h*=0x5bd1e995u; h^=h>>15;
    return ((float)(h&0xffffu)/65535.f*2.f-1.f)*g_boil; }
static GLuint sh(GLenum ty,const char*src){ GLuint s=glCreateShader(ty); glShaderSource(s,1,&src,NULL); glCompileShader(s);
    GLint ok; glGetShaderiv(s,GL_COMPILE_STATUS,&ok); if(!ok){ char lg[512]; glGetShaderInfoLog(s,512,NULL,lg); fprintf(stderr,"%s\n",lg); die("shader"); } return s; }

static int clipUpright(Pack*P,int clip);   /* S6e: defined below, used by the role audit */
/* S7: a clip whose name says it happens on the FLOOR can never be a stand, however much
 * of the word "idle" it contains. Takes an already-lowercased name. */
static int floorClipName(const char*lo){
    static const char*F[]={"lay","lying","sleep","sit","kneel","crouch","prone","floor",
                           "ground","dead","death","dying","fall","down",NULL};
    for(int i=0;F[i];i++) if(strstr(lo,F[i])) return 1;
    return 0; }
/* 09-01 AN IDLE MUST LOOP. kaeli's `idle` (v1_kmoves) is a 184-frame GET-UP -- crouch,
 * rise, stand -- whose last frame sits 111 deg from its first; every miner that read the
 * word "idle" elected it and played it on a loop, so she stood up and snapped back to the
 * floor for the whole of every scene (jacob: "do you know kaeli's is in a crouch to
 * standing animation as her base idle animation?"). The NAME said idle; the DATA said
 * transition. Mean first-to-last-frame rotation gap, degrees, across the clip's rotation
 * tracks; 0 for a static. Above IDLE_LOOP_DEG the clip cannot be an idle, whatever it is
 * called, and the refusal is printed so the next mislabelled pack says so at boot. */
#define IDLE_LOOP_DEG 20.f
static float clipLoopGapDeg(Pack*P,PClip*c){
    if(!c||c->frames<2||!c->nTracks) return 0.f;
    unsigned char*tr=P->pk+c->offTracks; double acc=0; int n=0;
    for(unsigned t=0;t<c->nTracks;t++){ unsigned type=*(unsigned*)(tr+t*12+4), offD=*(unsigned*)(tr+t*12+8);
        if(type!=0) continue;
        float*a=(float*)(P->pk+offD),*b=a+(size_t)(c->frames-1)*4;
        float d=fabsf(a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+a[3]*b[3]); if(d>1.f) d=1.f;
        acc+=2.0*acos(d)*57.2957795; n++; }
    return n?(float)(acc/n):0.f; }
static int clipLoopsOK(Pack*P,PClip*c,const char*who,const char*nm){
    float g=clipLoopGapDeg(P,c);
    if(g<=IDLE_LOOP_DEG) return 1;
    printf("[idle] %s: '%s' REFUSED as an idle -- first-to-last frame gap %.0f deg, that is a transition\n",who,nm,g);
    return 0; }
