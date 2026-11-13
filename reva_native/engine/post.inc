/* ================= THE POST PASS (09-11): ITS SETTINGS ==========================
 * The audit ranked "a post pass" fifth for the game and the film alike: bloom, anti-aliasing, fog, one tone knee for
 * everything, the film's grade and vignette, the fisheye and the tape frame. Since the renderer moved to Vulkan the
 * pass itself is gfx_vk.c's (shaders/post.frag), and what's here decides for the frame and holds the numbers, which
 * reach it through GfxView (main.inc, where the frame is handed over).
 *   post=0|1   bloom=<add strength>   fxaa=0|1   fog=<density>[,r,g,b]   (ctl; REVA_POST /
 *   REVA_BLOOM / REVA_FXAA / REVA_FOG env). Default: ON in the game doors, OFF in the lab
 *   and the film (mode 11), so every strict bench shot and every scene stays byte-identical
 *   until it asks. A frame with the pass on says so once. */
static int   g_post=-1;                     /* -1 = by mode; 0/1 forced (ctl/env) */
static int   g_postOn=0;                    /* the decision for THIS frame */
/* ⛔ MEASURED, not guessed (09-11): bloom 0.30 over a 0.72 threshold turned a noon street
 * into glare -- white siding and a sunlit lawn are both above 0.72, so the whole frame
 * bloomed. 0.16 over 0.86 leaves the neon and the sun's own highlights as the only
 * sources. And with the knee unset (tone=0) the post pass rolls at 0.70, the value the
 * film measured (23.1% -> 0.0% clipped), because a pass that can stop clipping should. */
/* ⛔ AND 0.16 OVER 0.86 STILL VEILED THE STREET: in an 8-bit buffer a 0.9 sky and a 1.0 neon
 * tracer are the same kind of pixel, so anything below ~0.95 blooms the whole sky. Only
 * saturated pixels bloom: 0.12 over 0.95. (An HDR buffer is what separates "bright" from
 * "white"; EXT_color_buffer_half_float is present when that day comes.) */
static float g_bloom=0.12f; static int g_fxaa=1; static float g_bloomThr=0.95f, g_postKnee=0.70f;
/* ⭐ (10-23) THE FILM'S LOOK, in the post pass: grade= a gentle S, the
 * shadows a little cool and the highlights a little warm, a touch more colour (0 off, 1 the show's); vig= the corners
 * fall away (0..1). ctl grade= vig=, REVA_GRADE / REVA_VIG. REVA_SSAA=2: the 3D frame drawn at twice the size into
 * the post target and resolved by the post quad's own bilinear tap (a 2x2 box): a take's ink lines and edges
 * antialiased, the HUD still drawn sharp at the window's size. Boot only; never on the multi-cam wall (its panels are
 * laid out in screen pixels). */
static float g_grade=0.f, g_vig=0.f; static int g_ssaa=1;
/* ⭐ THE LENS, AND FILM'S OWN LOOK, FRAME BY FRAME (queue step 25, 11-04: a camera track with roll, and post per frame).
 * What stands until it's said again: a ctl line (camroll= focus= shutter= sepia= grain= flash= burn= weave=), a set's `lens` and `film` lines, a scene line's
 * camroll= focus= shutter= sepia= grain=, a game's E->lensLook. What the set's timeline says for this frame goes over all of them (engine/settrack.inc, TRK4).
 *   roll     the lens turned about its own line, degrees: the frame tilts. Over 0 the horizon drops on the right
 *   focus    how far away the lens is focused, in world units; -1 is whatever it's aimed at; 0 is everything sharp. dof: how soft the rest goes (1 a portrait lens wide open)
 *   shutter  how much of a frame's time the shutter is open, 0 none to 1: what moves across the lens smears that far (the lens's own move; a body's isn't followed)
 *   sepia    0 to 1; flash: white added, 0 to 1 and past; burn: a film burn eating in from the frame's edge, 0 to 1; grain: 0 to 1
 *   weave    the gate's: the whole picture off its place, as a share of its width and its height
 * All of it is picture. None of it is in the rules, and none of it does anything where the machine has no post pass (the pi and low rungs). */
static float g_camRoll=0.f, g_lensFocus=0.f, g_lensDof=1.f, g_lensShutter=0.f;
static float g_filmSepia=0.f, g_filmFlash=0.f, g_filmBurn=0.f, g_filmGrain=0.f, g_filmWeave[2]={0.f,0.f};
static struct { float roll,focus,dof,shutter,sepia,flash,burn,grain,weave[2],exposure,haze,grade,fov; int camOn; float eye[3],aim[3]; } g_lensNow;   /* this frame's, worked out by lensNow */
static void lensClear(void){ g_camRoll=0.f; g_lensFocus=0.f; g_lensDof=1.f; g_lensShutter=0.f; g_filmSepia=g_filmFlash=g_filmBurn=g_filmGrain=0.f; g_filmWeave[0]=g_filmWeave[1]=0.f; }
/* `focus=` as a ctl line, a scene line and a set's line all say it: a distance, `aim`, or `off`, then how soft */
static void lensFocusSay(const char*v){ float d=0.f,s=-1.f;
    if(!strncmp(v,"off",3)){ g_lensFocus=0.f; return; }
    if(!strncmp(v,"aim",3)){ g_lensFocus=-1.f; const char*c=strchr(v,','); if(c&&c-v<=4) s=(float)atof(c+1); }
    else { const int n=sscanf(v,"%f,%f",&d,&s); if(n<1) return; g_lensFocus=d<0.f?0.f:d; if(n<2) s=-1.f; }
    if(s>=0.f) g_lensDof=s; }
static int g_labGrid;   /* a tentative definition: the lab's own is below */
/* ---- RENDER SCALE (2026-09-30): the ladder's biggest rung ------------------------------------
 * A Pi 4 is fill-bound: its GPU shades about a gigapixel a second in practice, and 720p with a
 * shadow pass and a post pass is three screens a frame. So the 3D frame is drawn into a SUB-RECT
 * of the post target (g_rW x g_rH = g_rscale of the screen) and the post quad, which already
 * runs once over every pixel, upsamples it: no second target, no reallocation, changeable
 * every frame. Every fetch in the two post shaders goes through fetch(): uv*uvs, clamped to
 * the rect. The anim lab (mode ANIM) is an instrument and always draws at 1:1; its multi-cam
 * grid is laid out in screen pixels and would land off the rect. */
static float g_rscale=1.f;
static void tierApply(int t,int boot){
    const Tier*T=&TIERS[t]; g_tier=t;
    g_rscale=T->rscale; g_bloom=T->bloom; g_fxaa=T->fxaa; g_clothSub=T->clothSub;
    if(boot) g_sunMap=T->sunMap;
    printf("[tier] %s: render %.0f%%, sun map %d%s, bloom %.2f, fxaa %d, cloth x%d%s\n",T->name,T->rscale*100.f,
           boot?T->sunMap:g_sunMap,boot?"":" (boot only)",T->bloom,T->fxaa,T->clothSub,""); fflush(stdout); }
static float g_fish=0.f;      /* ⭐ 0 = the lens we had, 1 = a full equidistant fisheye; ~0.6 is the VX look */
static float g_tape=0.f;      /* ⭐ the tape frame: 4:3 pillarbox, vignette, grain, a REC dot */
static int   g_camHoldSlot=-1;   /* ⭐ the slot whose camera this shot is coming from: never drawn */
static float g_tapeT=0.f;     /* its own clock, for the grain and the blink */
static float g_fishTh=0.f;    /* the DIAGONAL half-angle the final frame covers, radians (for the shader) */
static float g_fishAsp=1.f;
/* The 3D pass must cover the same total field RECTILINEARLY, so the warp has real geometry to bend at the edge
 * instead of stretched border pixels. Centre scale is preserved by construction: at r->0 the fisheye's
 * d(theta)/dr is th, and matching it to the rectilinear tan(th_authored) is what sets the target field. */
static float fishFov(float fov,float asp){
    if(g_fish<=0.f){ g_fishTh=0.f; return fov; }
    g_fishAsp=asp;
    float d=sqrtf(1.f+asp*asp);
    float th=atanf(d*tanf(fov*0.5f*0.0174532925f));       /* the authored diagonal half-angle */
    /* A FISHEYE IS A WIDE LENS AS WELL AS A BENT ONE. Morphing the projection at the authored fov moved the
     * field by six degrees and looked like nothing: a Century on a VX sees ~180, and the filmer stands a
     * foot away because of it. `fish` opens the field toward 120 degrees diagonal AND bends it. */
    const float FISH_MAX=1.05f;                           /* 60 deg off axis = 120 diagonal */
    float tgt=th+((FISH_MAX>th?FISH_MAX:th)-th)*g_fish;
    if(tgt>1.45f) tgt=1.45f;                              /* 83 degrees off axis: past that a rectilinear render dies */
    g_fishTh=tgt;
    float f=2.f*atanf(tanf(tgt)/d)*57.2957795f;
    return (f>170.f)?170.f:f; }
static float g_fog[4]={0.f,0.f,0.f,0.f};    /* rgb, density (0 = off) */
static int   g_fogAuto=1;                   /* colour from the sky horizon unless a scene names one */
/* the sky's horizon colour on the C side -- ONE formula; the skybox and the water used to
 * carry it as two identical literals, and the fog wants the same answer */
static void skyHorizonC(float*hz){ hz[0]=g_sky[0]*1.85f+0.10f; hz[1]=g_sky[1]*1.80f+0.11f; hz[2]=g_sky[2]*1.70f+0.12f; }
static void postInit(int W,int H){
    { const char*e=getenv("REVA_SSAA"); if(e&&e[0]){ int s=atoi(e); g_ssaa=(s>=2)?2:1; } }
    if(g_ssaa>1){ W*=g_ssaa; H*=g_ssaa; printf("[post] SSAA x%d: the 3D frame at %dx%d\n",g_ssaa,W,H); }
    { const char*e; if((e=getenv("REVA_FEED"))&&e[0]){ g_tape=(float)atoi(e); if(g_tape<0.f)g_tape=0.f; if(g_tape>2.f)g_tape=2.f; if(g_tape>0.f) g_post=1; printf("[feed] %s (REVA_FEED)\n",(g_tape>0.f)?"ON":"off"); }
      if((e=getenv("REVA_FISH"))&&e[0]){ g_fish=(float)atof(e); if(g_fish<0.f)g_fish=0.f; if(g_fish>1.f)g_fish=1.f;
          if(g_fish>0.f) g_post=1;
          printf("[fish] %.2f (REVA_FISH)\n",g_fish); }
      if((e=getenv("REVA_POST"))&&e[0]) g_post=atoi(e)?1:0;
      if((e=getenv("REVA_BLOOM"))&&e[0]) g_bloom=(float)atof(e);
      if((e=getenv("REVA_FXAA"))&&e[0]) g_fxaa=atoi(e)?1:0;
      if((e=getenv("REVA_GRADE"))&&e[0]) g_grade=(float)atof(e);
      if((e=getenv("REVA_VIG"))&&e[0]) g_vig=(float)atof(e);
      if((e=getenv("REVA_FOG"))&&e[0]){ float d=0,r=0,g=0,bb=0; int n=sscanf(e,"%f,%f,%f,%f",&d,&r,&g,&bb);
          g_fog[3]=d; if(n==4){ g_fog[0]=r; g_fog[1]=g; g_fog[2]=bb; g_fogAuto=0; } } }
    printf("[post] settings: post=%d bloom=%.2f fxaa=%d fog=%.3f (the pass is the renderer's)\n",g_post,g_bloom,g_fxaa,g_fog[3]); fflush(stdout); }
/* the decision for this frame, made once at the top of it so everything that reads it agrees */
static void postDecide(int mode,int modeAnim){
    int want=(g_post>=0)?g_post:(mode!=modeAnim);
    if(g_ssaa>1) want=(mode==modeAnim&&g_labGrid>1)?0:1;   /* ⭐ (10-23) the post quad IS the resolve */
    if(g_rscale<0.999f&&mode!=modeAnim) want=1;   /* a rung below full scale always ran the pass (its quad was the upsampler): the decision stays */
    g_postOn=want?1:0;
    if(g_postOn){ g_tapeT+=(float)g_animDtS; if(g_tapeT>1e6f) g_tapeT=0.f; }   /* the tape frame's own clock, for the grain and the blink */
    static int said=-1; if(said!=g_postOn){ said=g_postOn; printf("[post] %s\n",g_postOn?"ON: bloom + fxaa + tone in one pass":"off"); fflush(stdout); } }
