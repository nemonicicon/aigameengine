/* ⭐ THE FRAME LEDGER (11-02, engine plan Phase 0). The hard rule is that no frame ever drops, and a mean fps cannot see one. On the
 * KMS road each flip's own kernel timestamp goes in here, and a gap over 1.5 fields is a late frame; on the SDL road the swap's
 * return stands in for the flip, which is a proxy. [frames] prints with the stat line. */
static long g_ledN=0,g_ledLate=0,g_ledLate2=0,g_ledTotN=0,g_ledTotLate=0; static double g_ledLast=0.0,g_ledMax=0.0,g_ledWorkMax=0.0,g_ledFieldMs=16.68;
static void ledgerMark(double t){
    if(g_ledLast>0.0){ double g=t-g_ledLast; g_ledN++; g_ledTotN++; if(g>g_ledMax) g_ledMax=g;
        if(g>g_ledFieldMs*1.5){ g_ledLate++; g_ledTotLate++; } if(g>g_ledFieldMs*2.5) g_ledLate2++; }
    g_ledLast=t; }
static long g_visAcc=0,g_visN=0; static int g_visMax=0;   /* bodies in view: the proof's number */
static long g_farAcc=0; static int g_farOn=0;   /* (11-07) and those too small to draw (E->tune->farPx) */
static long g_liteAcc=0; static int g_liteOn=0;   /* (11-07) and those drawn from a light copy (E->tune->bodyLite) */
/* what the frame just drawn asked for, and what that cost the renderer */
static void frameCostPrint(void){
    { GfxStats s9; gfxStats(&s9); printf("[gfx] last frame: %d draws %ld indices, sun %d draws %ld indices\n",s9.draws,s9.indices,s9.sunDraws,s9.sunIndices); }
#ifdef REVA_VK
    { int c9,b9,p9,s9; gfxBackStats(&c9,&b9,&p9,&s9); printf("[gfx] the renderer made that %d calls and %d texture binds, with %d texture pages and %d stretched meshes\n",c9,b9,p9,s9); }
#endif
    posePrint(); }
static void ledgerPrint(void){
    if(g_visN){ printf("[bodies] %.1f in view on average, %d at most",(double)g_visAcc/(double)g_visN,g_visMax);
        if(g_farOn) printf("; %.1f too small to draw",(double)g_farAcc/(double)g_visN);
        if(g_liteOn) printf("; %.1f drawn light",(double)g_liteAcc/(double)g_visN);
        printf("\n"); }
    g_visAcc=g_visN=0; g_visMax=0; g_farAcc=0; g_liteAcc=0;
    frameCostPrint();
    printf("[frames] %ld flips, %ld late (%ld by 2+ fields), worst gap %.1f ms, worst work %.1f ms | total %ld late of %ld\n",
        g_ledN,g_ledLate,g_ledLate2,g_ledMax,g_ledWorkMax,g_ledTotLate,g_ledTotN);
    g_ledN=g_ledLate=g_ledLate2=0; g_ledMax=g_ledWorkMax=0.0; }
/* ⭐ THE PASS CLOCK (11-02). REVA_PASSTIME=1 finishes the GPU at each mark and charges the time since the last mark to that name,
 * so a slot's number is its CPU plus its GPU. A mark is the CPU's clock (the old road finished the GPU at each one; the renderer's own
 * cost is gfxWaited): the numbers say which part costs what, not how fast the whole frame runs. REVA_PASSTIME=2 marks only the frame's
 * big stations. On the Pi each slot also gets the GPU's own books: bin is the vertex side, render the pixel side, and the
 * two run in parallel. Off, a mark is one branch. */
#define PASS_MAX 24
static int g_passOn=-1,g_passN=0; static const char*g_passName[PASS_MAX]; static double g_passMs[PASS_MAX],g_passBin[PASS_MAX],g_passRen[PASS_MAX],g_passT=0.0,g_passB0=0.0,g_passR0=0.0;
static void passGpu(double*bin,double*ren){ *bin=*ren=0.0; FILE*f=fopen("/sys/devices/platform/v3dbus/fec00000.v3d/gpu_stats","r"); if(!f) return; char l[160],q[32]; unsigned long long ts,j,rt;
    while(fgets(l,sizeof l,f)) if(sscanf(l,"%31s %llu %llu %llu",q,&ts,&j,&rt)==4){ if(!strcmp(q,"bin")) *bin=rt/1e6; else if(!strcmp(q,"render")) *ren=rt/1e6; }
    fclose(f); }
static void passStart(void){ if(g_passOn<0){ const char*e=getenv("REVA_PASSTIME"); g_passOn=(e&&e[0]>='1')?e[0]-'0':0; }
    if(g_passOn){  passGpu(&g_passB0,&g_passR0); g_passT=wall_ms(); } }
static void passMark(const char*name){ if(g_passOn<=0) return;
    if(g_passOn==2&&strcmp(name,"sim0")&&strcmp(name,"holly")&&strcmp(name,"blobmap")&&strcmp(name,"sun")&&strcmp(name,"game")&&strcmp(name,"post")&&strcmp(name,"hud")) return;
     double t=wall_ms(),bb,rr; int i; passGpu(&bb,&rr);
    for(i=0;i<g_passN;i++) if(!strcmp(g_passName[i],name)) break;
    if(i==g_passN&&g_passN<PASS_MAX) g_passName[g_passN++]=name;
    if(i<PASS_MAX){ g_passMs[i]+=t-g_passT; g_passBin[i]+=bb-g_passB0; g_passRen[i]+=rr-g_passR0; }
    g_passB0=bb; g_passR0=rr; g_passT=wall_ms(); }
static void passPrint(long frames){ if(g_passOn<=0||frames<=0) return; printf("[pass]");
    for(int i=0;i<g_passN;i++){ printf(" %s %.2f (%.2f,%.2f)",g_passName[i],g_passMs[i]/frames,g_passBin[i]/frames,g_passRen[i]/frames); g_passMs[i]=g_passBin[i]=g_passRen[i]=0.0; }
    printf(" (ms a frame: wall (bin,render), at each %s)\n",g_passOn==2?"station":"mark"); }
/* The HUD's text. It lived inside main() as the engine's one GNU nested function, which clang can't compile and AddressSanitizer can't follow (11-02). */
static float g_hudW=1280.f,g_hudH=720.f; static char g_fontBuf[60000];
static void drawText(float x,float y,float sc,const char*txt,float r4,float g4,float b4,float a4){
    static float tv2[24000];
    int nq=stb_easy_font_print(0,0,(char*)txt,NULL,g_fontBuf,sizeof g_fontBuf);
    int nv=0; static const int idx[6]={0,1,2,0,2,3};
    for(int i2=0;i2<nq&&nv<23000;i2++)
        for(int k2=0;k2<6;k2++){ float*vv=(float*)(g_fontBuf+i2*64+idx[k2]*16);
            tv2[nv++]=x+vv[0]*sc; tv2[nv++]=y+vv[1]*sc; }
    hudTris(tv2,nv/2,g_hudW,g_hudH,r4,g4,b4,a4); }
#include "bridge.inc"    /* the bridge: a line of JSON each way, for an AI driving the running engine */
#include "display.inc"   /* the screen: what setup learned, changing the mode, setup itself */
#include "net.inc"       /* online play: the players gather, every machine starts from the same nothing, and each tick's pads come in where a tape's would */
#define PAUSE_N 6
#define PAUSE_SETUP "SCREEN SETUP","TEXT SIZE","CONTROLS",
static int g_pauseDark=0;
#ifdef REVA_VK
/* REVA_BTWDBG=<folder> (step 25): once a second, how many refreshes had a tick in them and how many lay between two, and how many of the mixed picture's draws found their
 * pair; and from the tenth second, six refreshes in a row written there as pictures, named for whether each had a tick. It reads the picture the refresh BEFORE this one drew */
static void btwDbg(int between,int W,int H){
    static int on=-1; static const char*dir; if(on<0){ dir=getenv("REVA_BTWDBG"); on=(dir&&dir[0])?1:0; } if(!on) return;
    if(!between&&g_noDraw) return;   /* a pass that only ticks shows nothing */
    static double t0=0.0; static int nt=0,nb=0,shot=0,wasBetween=0,wasT=0,kind[8],frac[8],sw=0,sh=0; static unsigned char*keep[8]; static long seq=0; double w=wall_ms(); if(t0<=0.0) t0=w;
    if(seq>1300&&shot<8){ if(!sw){ sw=W; sh=H; gfxShotSize(&sw,&sh); } keep[shot]=(unsigned char*)malloc((size_t)sw*sh*4);
        if(keep[shot]&&gfxRead(keep[shot],sw,sh)){ kind[shot]=wasBetween; frac[shot]=wasT; shot++; }
        if(shot==8) for(int k=0;k<8;k++){ char q[400]; snprintf(q,sizeof q,"%s/btw_%d_%s_%02d.ppm",dir,k,kind[k]?"between":"tick",frac[k]); FILE*f=fopen(q,"wb");
            if(f){ fprintf(f,"P6\n%d %d\n255\n",sw,sh); for(int y=sh-1;y>=0;y--) for(int x=0;x<sw;x++) fwrite(keep[k]+((size_t)y*sw+x)*4,1,3,f); fclose(f); } free(keep[k]); } }
    seq++; wasBetween=between; wasT=(int)(g_tickAcc/g_tickMs*100.0); if(between) nb++; else nt++;
    if(w-t0>=1000.0){ int pr=0,dr=0; gfxBetweenSay(&pr,&dr); printf("[between] a second: %d refreshes with a tick, %d between; %d of %d draws paired\n",nt,nb,pr,dr); fflush(stdout); nt=nb=0; t0=w; } }
#endif
static void aspectInit(int composite){ const char*e=getenv("REVA_ASPECT");
    if(e&&e[0]) g_dispAspect=(float)atof(e); else if(composite) g_dispAspect=15.f/11.f*g_fit.stretch;
    if(g_dispAspect>0.f) printf("[aspect] the picture is %.3f wide, whatever the framebuffer's shape\n",g_dispAspect); }
static int packNameCmp(const void*a,const void*b){ const unsigned char*x=a,*y=b;
    for(;;x++,y++){ int cx=toupper(*x),cy=toupper(*y); if(cx!=cy) return cx-cy; if(!cx) return 0; } }
#ifdef REVA_VK
__attribute__((unused)) static void vkFlip(unsigned sec,unsigned usec){ ledgerMark(sec*1000.0+usec/1000.0); }
#endif

#ifdef REVA_HEADLESS
static void g_win_headless_note(void){ printf("[headless] no display, no GPU, no vsync: the sim only\n"); fflush(stdout); }
#endif
/* ⭐ (11-07) THE HERO'S LIGHTER COPY, for the Pi's performance in a bigger game. The street game's co-star is 1.06 million triangles,
 * drawn twice a frame with her shadow: on the Pi that held the game to 7 frames a second (the tower game's body as the rider: 34). A body can have lighter copies beside
 * its pack, <pack>~lod1.rpack and ~lod2 (skate_src/author/lod.py --body, as a set actor's: the same skeleton, the same meshes by name, fewer triangles), and where the
 * machine's `lod` setting is its lowest (0, the Pi's rung) the hero is drawn from the lightest one, on her own pose, her shadow too. What a copy hasn't got: a face that
 * moves, cloth on chains of its own, hair kept off the head vertex by vertex. REVA_HEROLOD=0 draws the pack itself anywhere (the control), 1 or 2 that copy anywhere */
/* ⭐ (11-07) THE STATUS LINE: fps, vi and ms/vi in the top left corner of the display. Counted over the last whole
 * second and redrawn once a second, so it's that second exactly, not a running average: FPS is the new pictures the display showed, VI the fields the display itself
 * counted (its vblank sequence on the direct road; where there's none, the screen's rate), MS/VI the time each new picture took to make, everything but sitting idle for
 * the next field, against a field's 16.7 ms (under it, every field could have a new picture), and MAX the worst picture of the second, which an average hides.
 * Above the text grid, top left, where the tube still shows. On by itself on the direct road (the Pi on the tube), REVA_STATS=1 anywhere, 0 never */
static float g_statFps, g_statVi, g_statMs, g_statMax; static int g_statHave, g_statLines;
static int statsOn(void){ static int on=-1; if(on<0){ const char*e=getenv("REVA_STATS");
#ifdef REVA_SDL
    on=(e&&e[0]=='1')?1:0;
#else
    on=(e&&e[0]=='0')?0:1;
#endif
    } return on; }
static void statsFrame(void){
    if(!statsOn()) return;
    static double tPrev=-1.0, win0=-1.0, busy=0.0, worst=0.0, vt0=0.0; static int frames=0; static unsigned seq0=0u;
    const double now=wall_ms(), idle=gfxIdleWaited();
    if(tPrev<0.0){ tPrev=win0=now; seq0=gfxVblankSeq(); vt0=gfxVblankAt(); return; }
    double made=(now-tPrev)-idle; if(made<0.0) made=0.0; tPrev=now;
    busy+=made; if(made>worst) worst=made; frames++;
    if(now-win0>=1000.0){ const double secs=(now-win0)/1000.0; const unsigned seq=gfxVblankSeq(); const double vt=gfxVblankAt(), fr=gfxFieldRate();
        g_statFps=(float)(frames/secs); g_statMs=frames?(float)(busy/frames):0.f; g_statMax=(float)worst;
        if(seq&&seq0&&vt>vt0){   /* the display's own count over its own clock; a driver whose count goes up more than once a field is divided down to fields */
            const double steps=(double)(seq-seq0)/(vt-vt0); double per=fr>1.0?floor(steps/fr+0.5):1.0; if(per<1.0) per=1.0;
            g_statVi=(float)(steps/per); }
        else { SDL_DisplayMode dm9; g_statVi=(!SDL_GetCurrentDisplayMode(0,&dm9)&&dm9.refresh_rate>0)?(float)dm9.refresh_rate:0.f; }
        g_statLines=gfxLines();   /* (11-07) the world's lines as the second ends, so the label goes with its numbers */
        g_statHave=1; frames=0; busy=0.0; worst=0.0; win0=now; seq0=seq; vt0=vt; } }
static void statsDraw(void){
    if(!statsOn()||!g_statHave||g_noDraw) return;
    char t9[96]; const int ln9=g_statLines;
    if(ln9>0) snprintf(t9,sizeof t9,"FPS %.1f VI %.2f MS/VI %.1f 3D %d",g_statFps,g_statVi,g_statMs,ln9);   /* (11-07) the world's lines (the setting `lines`), in MAX's place */
    else snprintf(t9,sizeof t9,"FPS %.1f VI %.2f MS/VI %.1f MAX %.0f",g_statFps,g_statVi,g_statMs,g_statMax);   /* 35 columns at most: the grid's width */
    const float ok9=g_statMs<=16.7f?1.f:0.f;
    tvDraw(0.f,-1.3f,1,t9,ok9?0.55f:1.f,ok9?1.f:0.85f,ok9?0.55f:0.35f); }   /* green when a picture fits a field, amber when it doesn't */

/* (11-07) and ~lod3, the low-poly copy (skate_src/author/lowpoly.py: her model at a low polygon count, her hair baked as one merged mesh): her body thinned to about
 * 9,000 triangles and her hair's cards baked into one solid shell. REVA_HEROLOD=1|2|3 draws that copy, or the heaviest there is under it; the Pi's rung asks for 3 */
static Pack g_heroLodPk[3]; static int g_heroLodHave[3], g_heroLodTried; static const void*g_heroLodFor; static char g_heroLodPath[512];
static Pack* heroLod(Pack*P){
    static int want=-2;
    if(want==-2){ const char*e=getenv("REVA_HEROLOD"); want=(e&&e[0])?atoi(e):(g_gfxLod<=0?3:0); }
    if(want<=0||!g_heroLodPath[0]) return NULL;
    if(!g_heroLodTried){ g_heroLodTried=1; g_heroLodFor=P->pk;
        char stem[512]; snprintf(stem,sizeof stem,"%s",g_heroLodPath); char*dot=strstr(stem,".rpack"); if(dot) *dot=0;
        for(int l=0;l<3;l++){ char pp[600]; snprintf(pp,sizeof pp,"%s~lod%d.rpack",stem,l+1); FILE*t=fopen(pp,"rb");
            if(!t){ const char*b=strrchr(stem,'/'); snprintf(pp,sizeof pp,"props/%s~lod%d.rpack",b?b+1:stem,l+1); t=fopen(pp,"rb"); }   /* or in props/, where an actor's are */
            if(!t) continue;
            fclose(t); g_heroLodPk[l]=loadPack(pp); g_heroLodHave[l]=(g_heroLodPk[l].nNodes==P->nNodes);
            printf("[lod] the hero has a lod%d: %s%s\n",l+1,pp,g_heroLodHave[l]?"":" -- ⛔ other bones than the hero's: not used"); }
        fflush(stdout); }
    if(P->pk!=g_heroLodFor) return NULL;   /* the hero has put on another body since: its copies aren't this one's */
    int lv=-1; for(int l=(want>3?3:want)-1;l>=0&&lv<0;l--) if(g_heroLodHave[l]) lv=l;   /* the copy asked for, or the heaviest there is under it */
    for(int l=0;l<3&&lv<0;l++) if(g_heroLodHave[l]) lv=l;
    return lv<0?NULL:&g_heroLodPk[lv]; }

/* (11-12) a captured frame to its file (REVA_SHOTLAG's road): the PPM, its rows turned over as the picture comes back from gfxRead/gfxReadLag */
static void shotWritePPM(const char*shotPath,int burst,int idx,int SW9,int SH9,const unsigned char*px,long fno){
    char pth[512];
    if(burst>1) snprintf(pth,sizeof pth,"%.*s_%05d.ppm",(int)(strrchr(shotPath,46)?(size_t)(strrchr(shotPath,46)-shotPath):strlen(shotPath)),shotPath,idx);
    else snprintf(pth,sizeof pth,"%s",shotPath);
    FILE*sf=fopen(pth,"wb");
    if(!sf){ printf("shot FAILED to open %s (a native exe needs a Windows path)\n",pth); return; }
    fprintf(sf,"P6\n%d %d\n255\n",SW9,SH9);
    unsigned char*rw=(unsigned char*)malloc((size_t)SW9*3);
    for(int y=SH9-1;y>=0;y--){ for(int x=0;x<SW9;x++){ rw[x*3]=px[((size_t)y*SW9+x)*4]; rw[x*3+1]=px[((size_t)y*SW9+x)*4+1]; rw[x*3+2]=px[((size_t)y*SW9+x)*4+2]; } fwrite(rw,1,(size_t)SW9*3,sf); }
    free(rw); fclose(sf); printf("shot: %s %dx%d frame %ld\n",pth,SW9,SH9,fno); simHashPrint("shot"); }
int main(int argc,char**argv){
#ifdef __ANDROID__
    androidBoot(&argc,&argv);   /* (11-06) the log, the data out of the APK, the working folder, play.bat's arguments */
#endif
    if(argc<3){ fprintf(stderr,"usage: %s <slots> <packdir>\n",argv[0]); return 1; }
    g_argv=argv;   /* the start menu's MAIN MENU relaunches this exact invocation */
    int NS=atoi(argv[1]); const char*dir=argv[2];
    /* BENCH (2026-08-01 resolution sweep): argv[4]="WxH" forces a mode, argv[5]="bench"
     * fixes the seed so every resolution runs the IDENTICAL show. */
    int g_benchW=0,g_benchH=0,g_bench=0;
    if(argc>4&&strchr(argv[4],'x')) sscanf(argv[4],"%dx%d",&g_benchW,&g_benchH);
    if(argc>5&&!strcmp(argv[5],"bench")) g_bench=1;
    /* A REPRODUCIBLE CLOCK WITH A RANDOM SEED IS NOT REPRODUCIBLE. REVA_FIXEDSTEP
     * exists so two runs can be compared frame for frame, and the crowd draws its
     * clips, frames and dance switches from rand() -- so fixed-step implies the bench
     * seed. Verified: without this, two fixed-step captures of the same frame still
     * hash differently. (The same models came up over and over: rand was UNSEEDED and
     * every boot replayed the identical show, which is why the seed moved to time() --
     * that stays the default for the SHOW; only a measured run pins it.) */
    bridgeInit();                /* before the clock: a bridged run is on a fixed step */
    clockInit();                 /* before srand: the seed rule depends on it */
    scrLoad();                   /* what setup learned about this screen, before the first mode is picked */
    tierInit();                  /* ⭐ the ladder: before the sun map is allocated */
    sunInit();                   /* the sun starts OFF: the dance floor is a dark room */
    if(g_fixedStepMs>0.0) g_bench=1;
    /* REVA_SEED (11-02): the same dice under the real clock, with no finish in the frame. A replay takes the seed its tape was recorded with. */
    { unsigned seed9=getenv("REVA_SEED")?(unsigned)atol(getenv("REVA_SEED")):g_bench?12345u:(unsigned)time(NULL);
      const char*q9=getenv("REVA_REPLAY"); if(q9&&q9[0]) tapeSeedOf(q9,&seed9);
      g_tapeSeedUsed=seed9; srand(seed9); }
    signal(SIGINT,onsig); signal(SIGTERM,onsig);

#ifdef REVA_SDL
    /* [win32] SDL2 window, the renderer's surface on it. No mode enumeration -- the window IS the mode.
     * argv[4]=WxH still forces a size, matching the bench path. */
    int W = g_benchW>0 ? g_benchW : 1280;
    int H = g_benchH>0 ? g_benchH : 720;
#ifdef REVA_HEADLESS
    SDL_setenv("SDL_VIDEODRIVER","dummy",1); SDL_setenv("SDL_AUDIODRIVER","dummy",1); g_win_headless_note();
#endif
    /* (step 27) the engine's own rasterizer needs no display to draw. REVA_NOWINDOW=1 is the whole engine with its picture in memory: a shot or a receipt with nothing on anyone's screen.
     * (11-10) On Vulkan it's a window that's made and never shown: the renderer still has its surface, a shot is the same picture, and nothing comes up over whatever
     * is on the screen or takes the keys from it (a person can be playing something else on this machine while a game is photographed). Sound stays off for it too */
    const int g_noWindow=getenv("REVA_NOWINDOW")&&getenv("REVA_NOWINDOW")[0]=='1'; (void)g_noWindow;
#ifdef REVA_SOFT
    if(g_noWindow){ SDL_setenv("SDL_VIDEODRIVER","dummy",1); SDL_setenv("SDL_AUDIODRIVER","dummy",1); }
#elif !defined(REVA_HEADLESS)
    if(g_noWindow) SDL_setenv("SDL_AUDIODRIVER","dummy",1);
#endif
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_AUDIO)!=0){ fprintf(stderr,"SDL_Init: %s\n",SDL_GetError()); return 1; }
    /* [win32] Fullscreen at the panel's NATIVE resolution on a chosen display.
     * REVA_DISPLAY=<n> picks the monitor (SDL indexes in Windows enumeration order);
     * REVA_WINDOWED=1 falls back to a plain window for debugging. */
    int g_disp = 0; { const char* e=getenv("REVA_DISPLAY"); if(e) g_disp=atoi(e); }
    int g_windowed = 0; { const char* e=getenv("REVA_WINDOWED"); if(e&&e[0]=='1') g_windowed=1; }
    (void)g_windowed;
#ifdef REVA_VK
    displayPrefs(&g_windowed,&W,&H);   /* (step 25) save/graphics.txt: a window or the whole panel, and a window's size */
#endif
    { const char* e=getenv("REVA_SKATE"); g_skate=(e&&e[0]=='1'); if(g_skate) printf("SKATE MODE\n"); }
    if(g_disp<0||g_disp>=SDL_GetNumVideoDisplays()) g_disp=0;
#ifdef REVA_HEADLESS
    Uint32 g_wflags = SDL_WINDOW_HIDDEN;
#elif defined(REVA_SOFT)
    Uint32 g_wflags = g_noWindow?SDL_WINDOW_HIDDEN:(SDL_WINDOW_SHOWN|(g_windowed?SDL_WINDOW_RESIZABLE:SDL_WINDOW_FULLSCREEN_DESKTOP));   /* a plain window: no driver is asked for a surface. (11-12) A window can be resized and maximized */
#else
    Uint32 g_wflags = SDL_WINDOW_VULKAN|(g_noWindow?SDL_WINDOW_HIDDEN:SDL_WINDOW_SHOWN|(g_windowed?SDL_WINDOW_RESIZABLE:SDL_WINDOW_FULLSCREEN_DESKTOP));   /* (11-12) a window can be resized, maximized and made fullscreen (Alt+Enter, F11) */
#endif
    SDL_Window* g_win = SDL_CreateWindow("reva",
                                          SDL_WINDOWPOS_CENTERED_DISPLAY(g_disp),
                                          SDL_WINDOWPOS_CENTERED_DISPLAY(g_disp),
                                          W,H,g_wflags);
    if(!g_win){ fprintf(stderr,"SDL_CreateWindow: %s\n",SDL_GetError()); return 1; }
#ifdef REVA_VK
    gfxConfigBoot(); gfxInit(g_win,&W,&H);
#endif
#ifdef __ANDROID__
    touchScreen(W,H);
#endif
    /* the drawable is the truth, not what we asked for: the projection's aspect reads W/H below, and fullscreen gives us the panel's real size. */
    { SDL_DisplayMode dm; if(!SDL_GetCurrentDisplayMode(g_disp,&dm))
        printf("display %d: %dx%d@%dHz -> drawable %dx%d\n",g_disp,dm.w,dm.h,dm.refresh_rate,W,H); }
    { SDL_DisplayMode dm9; const char*dv9=SDL_GetCurrentVideoDriver();
      if(!SDL_GetCurrentDisplayMode(g_disp,&dm9)&&dm9.refresh_rate>0) g_ledFieldMs=1000.0/dm9.refresh_rate;
      /* (step 25) the pictures between ticks, on a display that's faster than them. REVA_INTERP=0 never, 1 everywhere it can; unsaid, the `interp` setting, and not in the anim lab, where a frame is an exposure */
#ifdef REVA_VK
      { const char*ei9=getenv("REVA_INTERP"); const int want9=ei9?atoi(ei9):g_gfxInterp;
        g_interpOn=(want9>0&&dm9.refresh_rate>62&&!getenv("REVA_FIXEDSTEP")&&!getenv("REVA_REPLAY")&&!getenv("REVA_RECORD")); g_interpForce=(ei9&&atoi(ei9)>0);
        if(g_interpOn) printf("[display] %d Hz: the pictures between the rules' ticks are worked out from the two packets, one tick behind%s\n",dm9.refresh_rate,g_interpForce?"":" (not in the anim lab)"); }
#endif
      if(g_ledFieldMs<g_tickMs&&!g_interpOn) g_ledFieldMs=g_tickMs;   /* a display faster than the tick shows a new frame each tick, so that's when one is due */
      aspectInit(dv9&&!strcmp(dv9,"KMSDRM")&&W==720&&(H==480||H==240||H==576||H==288));
      { const float sa9=gfxShownAspect(); if(sa9>0.f&&g_dispAspect<=0.f){ g_dispAspect=sa9; printf("[aspect] the picture's pixels aren't square: it's shown %.3f wide\n",sa9); } } }   /* (step 25) a tube's 240-line picture */
#else
    /* KMS: the card WITH A CONNECTOR. On a Pi 4 (vc4-kms-v3d) /dev/dri holds two cards -- v3d
     * (render only, no connectors) and vc4 (the display) -- and which one is card0 depends on
     * probe order, so "card1" was a guess that held on one board. REVA_DRM=/dev/dri/cardN pins it;
     * otherwise every card is opened and the first one that reports a connector is the glass. */
    int fd=-1;
    { const char*ed=getenv("REVA_DRM"); char cp[64]; const char*tried[8]; int nt=0;
      for(int ci=-1;ci<8&&fd<0;ci++){
          if(ci<0){ if(!ed||!ed[0]) continue; snprintf(cp,sizeof cp,"%s",ed); }
          else snprintf(cp,sizeof cp,"/dev/dri/card%d",ci);
          int f=open(cp,O_RDWR|O_CLOEXEC); if(f<0) continue;
          drmModeRes*r=drmModeGetResources(f);
          if(r&&r->count_connectors>0){ fd=f; printf("drm: %s (%d connectors)\n",cp,r->count_connectors); }
          else { if(nt<8) tried[nt++]=strdup(cp); close(f); }
          if(r) drmModeFreeResources(r); }
      if(fd<0){ fprintf(stderr,"drm: no card with a connector"); for(int i=0;i<nt;i++) fprintf(stderr,"%s %s",i?",":" (tried",tried[i]); fprintf(stderr,"%s\n",nt?")":""); die("open drm card"); } }
    drmSetMaster(fd);   /* labwc must be gone; if this fails we still try (first client may auto-master) */
    drmModeRes*res=drmModeGetResources(fd); if(!res) die("resources");
    drmModeConnector*conn=NULL;
    for(int i=0;i<res->count_connectors;i++){ drmModeConnector*c=drmModeGetConnector(fd,res->connectors[i]);
        if(c&&c->connection==DRM_MODE_CONNECTED){ conn=c; break; } if(c) drmModeFreeConnector(c); }
    /* 09-02 COMPOSITE HAS NO HOTPLUG DETECT. On kernel 6.18 the CRT's Composite-1 reports
     * UNKNOWN (not CONNECTED) even after a forced detect, and this died with "no connected
     * connector" on a display that was plainly showing the console. A connector that
     * carries modes and is the only thing there IS the display: fall back to it. */
    if(!conn){ for(int i=0;i<res->count_connectors;i++){ drmModeConnector*c=drmModeGetConnector(fd,res->connectors[i]);
        if(c&&c->connection==DRM_MODE_UNKNOWNCONNECTION&&c->count_modes>0){ conn=c;
            printf("connector %u: status UNKNOWN but %d modes -- taking it (composite has no hotplug detect)\n",c->connector_id,c->count_modes); break; }
        if(c) drmModeFreeConnector(c); } }
    if(!conn) die("no connected connector");
    /* Take the display's preferred RESOLUTION but its FASTEST progressive refresh
     * at that resolution. 08-01: a 2K panel lists 1440p60 as preferred with
     * 1440p100 further down the list, and at 60 the floor sat vsync-capped with
     * headroom to spare. Composite 480i is unaffected -- it offers one mode. */
    drmModeModeInfo mode=conn->modes[0];
    if(conn->connector_type==DRM_MODE_CONNECTOR_Composite){
        /* ⭐ THE WHITELIST (11-02, engine plan). On composite the engine only ever emits the four standard modes, so it cannot produce an out-of-range scan rate, the one kind
         * of signal damage on record. REVA_VIDEO=480i|240p|576i|288p picks one by name, default 480i (a whole-mode argv like 720x240 still works). ⛔ The 50 Hz modes
         * are out of spec on a 60 Hz set: they only ever run by name, and setup's voice prompt in front of them is Phase 4's. */
        static const struct { const char*n; int h,il; } V9[4]={{"480i",480,1},{"240p",240,0},{"576i",576,1},{"288p",288,0}};
        const char*want=getenv("REVA_VIDEO"); int vi=g_fit.mode,got=0;
        if(VM[vi].hz==50&&!g_fit.allow50) vi=0;
        if(want&&want[0]){ vi=-1; for(int i=0;i<4;i++) if(!strcmp(want,V9[i].n)) vi=i; if(vi<0){ printf("[video] REVA_VIDEO=%s is not a standard composite mode (480i 240p 576i 288p) -- 480i\n",want); vi=0; } }
        else for(int i=0;i<4;i++) if(g_benchW==720&&g_benchH==V9[i].h) vi=i;
        for(int mi=0;mi<conn->count_modes&&!got;mi++){ drmModeModeInfo*m2=&conn->modes[mi];
            if(m2->hdisplay==720&&m2->vdisplay==V9[vi].h&&!!(m2->flags&DRM_MODE_FLAG_INTERLACE)==V9[vi].il){ mode=*m2; got=1; } }
        if(!got) die("composite: the driver does not list the standard mode asked for"); }
    else {
    if(g_benchW>0){ for(int mi=0;mi<conn->count_modes;mi++)
        if(conn->modes[mi].hdisplay==g_benchW&&conn->modes[mi].vdisplay==g_benchH
           &&!(conn->modes[mi].flags&DRM_MODE_FLAG_INTERLACE)){ mode=conn->modes[mi]; break; } }
    for(int mi=0;mi<conn->count_modes;mi++){ drmModeModeInfo*m2=&conn->modes[mi];
        if(m2->hdisplay!=mode.hdisplay||m2->vdisplay!=mode.vdisplay) continue;
        if((m2->flags&DRM_MODE_FLAG_INTERLACE)&&!(mode.flags&DRM_MODE_FLAG_INTERLACE)) continue;
        if(m2->vrefresh>mode.vrefresh) mode=*m2; } }
    printf("connector %u: %s  mode %ux%u@%u%s\n",conn->connector_id,
        conn->connector_type==DRM_MODE_CONNECTOR_Composite?"Composite":
        conn->connector_type==DRM_MODE_CONNECTOR_HDMIA?"HDMI-A":"?",
        mode.hdisplay,mode.vdisplay,mode.vrefresh,(mode.flags&DRM_MODE_FLAG_INTERLACE)?"i":"p");
    g_ledFieldMs=(double)mode.htotal*(double)mode.vtotal/(double)mode.clock/((mode.flags&DRM_MODE_FLAG_INTERLACE)?2.0:1.0);
    aspectInit(conn->connector_type==DRM_MODE_CONNECTOR_Composite);
    drmModeEncoder*enc=drmModeGetEncoder(fd,conn->encoder_id);
    unsigned crtcId=enc?enc->crtc_id:res->crtcs[0];
    int W=mode.hdisplay,H=mode.vdisplay;
    gfxConfigBoot(); gfxInitKms(fd,crtcId,conn->connector_id,&mode,W,H,vkFlip);
    g_vConn=conn; g_vFd=fd;
#endif
    g_vNow=vmOfSize(W,H); scrApply(W,H);
    setupAtBoot(&W,&H,g_dispAspect>0.f&&g_vNow>=0);   /* a first boot asks about the screen, before anything is loaded */

    /* WHAT ARE WE PLAYING, AND AS WHOM: mode select first, then the character select
     * (argv[3] names a pack outright; absent or "select", the screen decides). Windows
     * only; on the Pi the argv behavior is unchanged and REVA_MODE/REVA_SKATE pick modes. */
#ifdef REVA_SDL
    g_mode=modeSelect(W,H);
    saveLoad(); atexit(saveFlush);   /* the engine remembers: see THE SAVE FILE above */
    saveSetI("last.mode",g_mode); saveSetI("last.variant",g_modeVariant); saveSetI("nights",saveGetI("nights",0)+1);
    /* ⭐ 10-12: door 11 is its own program from the first frame -- no packs, no crowd.
     * EXIT GAME comes back to the doors; under REVA_MODE it ends the run. */
#else
    { const char*e=getenv("REVA_MODE");
      if(e&&e[0]){ for(int m=0;m<MODE_N;m++) if(MODE_NAME[m]&&((e[0]>='0'&&e[0]<='9'&&atoi(e)==m)||(e[0]|32)==(MODE_NAME[m][0]|32))) g_mode=m; } }
    { const char*e=getenv("REVA_AUTOVAR"); if(e&&e[0]) g_modeVariant=atoi(e); }   /* ⭐ (11-02) the Pi's direct road reaches a variant too */
#endif
    /* ⭐ REVA_SET=<name>: a game door plays IN A SET. Until now only the ANIM LAB could load
     * one (the live-control file is the lab's), so no door had ever stood on the street the
     * film is shot on, and "walk the player into a house" could not be tested in the one
     * mode that has a player. The world becoming a playable space starts here. */
    { const char*es=getenv("REVA_SET");
      /* ⭐ THE ZOMBIE GAME: the door itself plays on the street (rules' `street`; `none` is v0's yard) */
      if(g_mode==MODE_GAME){ apiInit(); netBoot(); if(g_game->video) g_videoWant=vmOf(g_game->video); }   /* (step 29) a session's players gather here: the game's known, and nothing of it is loaded */
      else if(getenv("REVA_NET")&&getenv("REVA_NET")[0]){ printf("[net] online play is a game's, and this door isn't one: REVA_NET is left alone\n"); fflush(stdout); }
      int game9=GAME_ON;
      if(game9){ g_game->boot(); actionsLoad();
        const char*st9=g_game->street();
        if((!es||!es[0])&&strcmp(st9,"none")){ char p9[96]; snprintf(p9,sizeof p9,"sets/%s.set",st9); FILE*f9=fopen(p9,"rb");
            if(f9){ fclose(f9); es=st9; } else printf("[rvz] no %s here: the door plays in the yard\n",p9); } }
      if(es&&es[0]){ setLoad(es);
        /* ⛔ A SET WITHOUT A DAY IS LIT BY THE DANCE FLOOR. Nothing outside the lab applies sun=/sky=,
         * so the first door on the street stood under the dark-room rig with the dance floor sky's
         * magenta ground band painted under the road. A set brings its hour: REVA_TOD=<h>,
         * default noon, through the same fourteen-keyframe curve tod= uses. */
        const char*et=getenv("REVA_TOD"); g_tod=(et&&et[0])?(float)atof(et):(game9?g_game->tod():12.0f); if(g_tod>=0.f) todApply(g_tod);   /* (11-12) a game's hour under 0: the set's own sun and sky light it */
        printf("[set] REVA_SET=%s loaded for %s at %05.2f h\n",es,MODE_NAME[g_mode],g_tod); } }
    if(g_mode==MODE_SKATE||DOOR_ON_WHEELS){ g_skate=1; printf("SKATE PHYSICS ON (%s)\n",MODE_NAME[g_mode]); }
    char heroPick[512]={0};
    if(WORLD_OFF) printf("[game] flat: no bodies\n");   /* a flat game has no body to choose */
    else if(argc>3&&strcmp(argv[3],"select")) snprintf(heroPick,sizeof heroPick,"%s",argv[3]);
#ifdef REVA_SDL
    else { const char*pk=charSelect(W,H); if(pk) snprintf(heroPick,sizeof heroPick,"%s",pk); }
#endif

    /* packs */
    #define MAXPACKS 1024
    static Pack packs[MAXPACKS]; int NP=0;   /* static: 512 Pack structs would blow the stack */
    DIR*dd=opendir(dir); struct dirent*de;
    /* ⛔ ".rpack" as a SUBSTRING also matched every backup the authoring tools leave
     * beside a pack -- v1_kmoves.rpack.marks.bak and .tex.bak were both being loaded
     * as crowd members, so the engine carried stale twins of any body I had edited (two
     * extra copies of the co-star's 294k-vertex mesh among them) and a fix could appear
     * unapplied because the ghost was standing next to her. It must be the SUFFIX. */
    int movesI9=-1;
    /* ⭐ THE PACKS IN A SET ORDER (11-02). readdir gives the filesystem's order: alphabetical on NTFS, hashed on the Pi's ext4. So
     * the same crowd drew different bodies on each machine, and no hash could ever match across them. Names sorted case-blind by
     * upper case, NTFS's own order, so Windows keeps the cast it has always had. */
    { static char nm9[MAXPACKS][256]; int nn9=0;
      while(dd&&(de=readdir(dd))&&nn9<MAXPACKS){ size_t ln9=strlen(de->d_name);
          if(ln9>6&&ln9<256&&!strcmp(de->d_name+ln9-6,".rpack")) strcpy(nm9[nn9++],de->d_name); }
      qsort(nm9,(size_t)nn9,256,packNameCmp);
      /* ⭐ (11-12) REVA_PACKS=<name,...>: only these from the pack folder (a film take used 5 bodies from props/ and loaded all 162 here: 17 s a take) */
      const char*only9=getenv("REVA_PACKS"); int skipped9=0; const double lt0=wall_ms();
      for(int i9=0;i9<nn9;i9++){
          if(only9&&only9[0]){ int want9=0; const char*a9=only9;
              while(*a9&&!want9){ const char*c9=strchr(a9,','); size_t n9=c9?(size_t)(c9-a9):strlen(a9);
                  char w9[256]; if(n9>0&&n9<200){ memcpy(w9,a9,n9); w9[n9]=0; char f9[300];
                      const int hasExt9=n9>6&&!strcmp(w9+n9-6,".rpack"); const int hasV19=!strncmp(w9,"v1_",3);
                      snprintf(f9,sizeof f9,"%s%s%s",hasV19?"":"v1_",w9,hasExt9?"":".rpack");
                      if(!strcmp(f9,nm9[i9])||!strcmp(w9,nm9[i9])) want9=1; }
                  if(!c9) break; a9=c9+1; }
              if(!want9){ skipped9++; continue; } }
          if(!strcmp(nm9[i9],"v1_moves.rpack")) movesI9=NP;
          char p[512]; snprintf(p,sizeof p,"%.255s/%.255s",dir,nm9[i9]); packs[NP++]=loadPack(p); }
      printf("[load] %d packs from %s in %.1f s%s\n",NP,dir,(wall_ms()-lt0)/1000.0,skipped9?" (REVA_PACKS: the rest left out)":""); if(skipped9) printf("[load] REVA_PACKS left out %d packs\n",skipped9); fflush(stdout); }
    if(dd) closedir(dd);
    if(!NP&&!WORLD_OFF) die("no packs");
    /* ⛔ THE MOVES PACK IS FOUND BY ITS FILE NAME. The exact-LABEL match ("v1 moves") had stopped matching --
     * the boot said "123 packs loaded" with no "(moves pack found)" -- so P5's donor was gone for every body and a
     * set crowd's w_cheer came back NOT FOUND x52, with nothing louder than that absence. */
    if(movesI9>=0) g_movesPk=&packs[movesI9];
    for(int i9=0;!g_movesPk&&i9<NP;i9++) if(!strcmp(packs[i9].label,"v1 moves")){ g_movesPk=&packs[i9]; break; }   /* P5: EXACT label -- v1_kmoves must not shadow it */
    printf("%d packs loaded%s\n",NP,g_movesPk?" (moves pack found)":" -- ⛔ MOVES PACK NOT FOUND: no body can borrow w_cheer, a_convo, z_greet ... (packs/v1_moves.rpack)");
    if(g_movesPk&&movesI9>=0&&strcmp(g_movesPk->label,"v1 moves")) printf("[load] the moves pack's label is '%s' -- which is why the old label match missed it\n",g_movesPk->label);
    { int tc=0,tp=0; for(int i9=0;i9<NP;i9++) if(packs[i9].nCh){ tp++; tc+=packs[i9].nCh; }
      printf("H1 chains: %d chains on %d packs\n",tc,tp);
      if(tp<=24) for(int i9=0;i9<NP;i9++) if(packs[i9].nCh)
          { char lens[96]; int lp=0;
            for(int c9=0;c9<packs[i9].nCh&&lp<80;c9++)
                lp+=snprintf(lens+lp,(size_t)(96-lp),"%s%d",c9?",":"",packs[i9].chN[c9]);
            /* ⛔ EVERY LENGTH, NOT THE FIRST. "first len 2" said nothing about
             * whether the bought character's seven-bone braids were mined -- and before the branching
             * fix the first was 2 as well, so the summary never moved while the chains
             * that matter were missing entirely. */
            printf("  chains: %s x%d lens=[%s]\n",packs[i9].label,packs[i9].nCh,lens); } }
    /* REVA (07-22) — her own pack, drawn LAST as the hologram */
    Pack revaP; int hasReva=0; float revaGround=0;
    /* THE BOARD (08-21): a skateboard to ride on. ONE prop on ONE character,
     * so it is baked at final world size and carries no decimation: it rides her ground
     * position and heading but NOT her model scale. A missing board is never fatal. */
    Pack boardP; int hasBoard=0; int hasPark=0;
    { const char*sub=getenv("REVA_ANIMSUBJ");   /* the lab's test body: a mannequin reads better than a hologram */
      if(sub&&*sub){ FILE*sf=fopen(sub,"rb");
          if(!sf){ char alt[512]; snprintf(alt,sizeof alt,"packs/v1_%s.rpack",sub); sf=fopen(alt,"rb");
                   /* (step 23) a lead who isn't one of the crowd lives in props/: a new file in packs/ changes who the crowd is, on every machine and in every receipt */
                   if(!sf){ snprintf(alt,sizeof alt,"props/v1_%s.rpack",sub); sf=fopen(alt,"rb"); }
                   if(sf){ fclose(sf); snprintf(heroPick,sizeof heroPick,"%s",alt); } }
          else { fclose(sf); snprintf(heroPick,sizeof heroPick,"%s",sub); }
          printf("ANIM SUBJECT: %s\n",heroPick); } }
    if(heroPick[0]){ revaP=loadPack(heroPick); hasReva=1; g_heroPk=&revaP; snprintf(g_heroLodPath,sizeof g_heroLodPath,"%s",heroPick);
        /* 07-22 (she stood huge and stationary): her pack was exported from the PAGE-SCALED model --
         * mesh 3.4 model-units vs bone cloud 0.298 (the 11.4x lives in her baked bind). Bone-span
         * sizing made her ~32 units tall, and glide speeds tuned for 3 units read as frozen.
         * Cure: size + ground her by the MESH (page kept it normalized + floored at 0). */
        { unsigned*Hd2=(unsigned*)(revaP.pk+4); float*szb=(float*)(revaP.pk+Hd2[12]); float meshY=szb[9];
          if(meshY==meshY&&meshY>0.5f&&meshY*revaP.scale>3.15f*1.5f){
              revaP.scale=3.45f/meshY; revaP.mny=0.f; revaP.cx=0.f; revaP.cz=0.f; revaP.effH=-1.f;
              printf("reva MESH-sized: meshY %.2f -> scale %.4f\n",meshY,revaP.scale); }
          else revaP.scale*=3.45f/3.15f; }   /* clean rig: bone-sized like the crowd, then 6ft (reva is tall) */
        /* ground her: rest-pose min bone y in model units */
        M4*w2=malloc(revaP.nNodes*sizeof(M4)); float mny=1e9f;
        for(unsigned i=0;i<revaP.nNodes;i++){ M4 loc; m4_trs(loc,revaP.nodes[i].t,revaP.nodes[i].q,revaP.nodes[i].s);
            if(revaP.nodes[i].parent<0) memcpy(w2[i],loc,64); else m4_mul(w2[i],w2[revaP.nodes[i].parent],loc);
            if(revaP.nodes[i].kind==1){ float y=w2[i][13]; if(y<mny)mny=y; } }
        { int lu=revaP.pb[12], ru=revaP.pb[14];      /* left and right upper leg */
          if(lu>=0&&ru>=0){
              float dx=w2[lu][12]-w2[ru][12], dz=w2[lu][14]-w2[ru][14];
              if(dx*dx+dz*dz>1e-6f){
                  float lat=atan2f(dx,dz);        /* direction of her lateral axis */
                  { const char*e=getenv("REVA_GOOFY"); g_goofy=(e&&e[0]=='1'); }
                  g_stanceRad = -lat + (g_goofy?3.14159265f:0.f);
                  printf("stance MEASURED: lateral axis %.1f deg -> stance %.1f deg%s\n",
                         lat*57.2958f, g_stanceRad*57.2958f, g_goofy?" (goofy)":" (regular)"); } } }
        free(w2); revaGround=-mny*revaP.scale+0.02f;
        g_revaFootY=mny;   /* rest-pose sole, in model units -- the height the IK targets sit at */
        if(revaP.effH<0){ revaGround=0.02f; revaP.effH=3.15f; }   /* mesh-sized: her mesh floor IS 0 (page grounded it) */
        /* A downloaded rig is sized from its bone cloud, and a rig full of control bones
         * spread far from the body measures much taller than the character. REVA_SKSCALE
         * lets that be corrected without a rebuild. */
        { const char*e=getenv("REVA_SKSCALE"); if(e){ float m=(float)atof(e); if(m>0.01f&&m<20.f){ revaP.scale*=m; revaGround*=m; } } }
        printf("reva loaded: %u nodes %u clips, scale %.4f ground %.2f\n",revaP.nNodes,revaP.nClips,revaP.scale,revaGround);
        /* locomotion clip map (full walk/run states): w_* by name; dances = p_live* + m_* + c_* */
        for(unsigned c=0;c<revaP.nClips;c++){ const char*nm=packStr(&revaP,revaP.clips[c].name);
            /* Names from a downloaded pack will not follow our w_* convention, so match on
             * what the clip obviously IS. Longest/most specific first. */
            { char lo[64]; int li=0;
              for(const char*q=nm; *q && li<63; q++){ char ch=*q; lo[li++]=(ch>='A'&&ch<='Z')?ch+32:ch; }
              lo[li]=0;
              float clen=(revaP.clips[c].fps>0.f)?((float)revaP.clips[c].frames/revaP.clips[c].fps):0.f;
              if(strstr(lo,"kickflip")||strstr(lo,"flip")||strstr(lo,"trick")||strstr(lo,"ollie")){
                  /* the shortest one is a single trick; a long one is a whole routine */
                  if(strstr(lo,"trick2")){ g_rvTrick2=(int)c; g_rvTrick2Dur=clen; }
                  else if(g_rvTrick<0||clen<g_rvTrickDur){ g_rvTrick=(int)c; g_rvTrickDur=clen; }
                  if(g_rvSki<0) g_rvSki=(int)c; }
              else if(strstr(lo,"walk")&&g_rvWalk<0) g_rvWalk=(int)c;
              else if(strstr(lo,"run")||strstr(lo,"jog")||strstr(lo,"sprint")) g_rvRun=(int)c;
              /* S7: FIRST match wins and floor clips are refused. Without the g_rvFootIdle<0
               * guard the LAST "idle" won, so crowd_susan (idle, laying_idle, sleeping_idle)
               * spent the whole IDLE state asleep on her face. The pack-side mining had this
               * guard; the hero's did not. */
              else if(g_rvFootIdle<0&&!floorClipName(lo)&&(strstr(lo,"idle")||strstr(lo,"stand"))
                      &&clipLoopsOK(&revaP,&revaP.clips[c],"hero",nm))
                  g_rvFootIdle=(int)c; }
            if(!strcmp(nm,"w_walk")) g_rvWalk=(int)c;
            else if(!strcmp(nm,"w_run")) g_rvRun=(int)c;
            else if(!strcmp(nm,"w_fly")) g_rvFly=(int)c;
            else if(!strcmp(nm,"w_ski")) g_rvSki=(int)c;
            else if(((nm[0]=='p'||nm[0]=='m'||nm[0]=='c')&&nm[1]=='_')&&g_rvNDance<64) g_rvDance[g_rvNDance++]=(int)c; }
        /* only a loop the baker MEASURED as flat is given the w_ski role, so this is
         * a ride loop and not a trick being repeated */
        if(revaP.footL>=0&&revaP.footR>=0&&revaP.pb[0]>=0){   /* S6e: the hero's roles get the same audit */
            if(g_rvRun>=0&&!clipUpright(&revaP,g_rvRun)){
                printf("REJECT hero run clip %d: not an upright gait -- the donor bank will cover it\n",g_rvRun);
                g_rvRun=-1; }
            if(g_rvWalk>=0&&!clipUpright(&revaP,g_rvWalk)){
                printf("REJECT hero walk clip %d: not an upright gait\n",g_rvWalk);
                g_rvWalk=-1; }
            /* S7: the STAND gets the same audit as the gaits. A name filter is a guess;
             * this is a measurement -- hips above the lowest foot across the whole cycle.
             * A mis-named floor clip cannot get through both. */
            if(g_rvFootIdle>=0&&!clipUpright(&revaP,g_rvFootIdle)){
                printf("REJECT hero idle clip %d: does not stand up\n",g_rvFootIdle);
                g_rvFootIdle=-1; } }
        g_rvRide=g_rvSki;
        printf("reva loco: walk %d fly %d ski %d trick %d (%.2fs), %d dances\n",
               g_rvWalk,g_rvFly,g_rvSki,g_rvTrick,g_rvTrickDur,g_rvNDance);
        /* S4.5: the co-star's hip-bounce debt. Her retarget is rotation-only (hips pinned by the
         * 1e-4 scale collapse), so no clip ever moves her root. Detect that honestly: scan
         * every hero clip for a translation track on the hips bone; none found -> the model
         * matrix gets a beat-locked bounce back. */
        g_rvHipTrans=0;
        if(revaP.pb[0]>=0) for(unsigned c=0;c<revaP.nClips&&!g_rvHipTrans;c++){ PClip*cc=&revaP.clips[c];
            unsigned char*tr=revaP.pk+cc->offTracks;
            for(unsigned t=0;t<cc->nTracks;t++){ unsigned node=*(unsigned*)(tr+t*12), type=*(unsigned*)(tr+t*12+4);
                if(type==1&&node==(unsigned)revaP.pb[0]){ g_rvHipTrans=1; break; } }
        }
        printf("hero hip translation: %s\n",g_rvHipTrans?"authored":"none -> procedural bounce on"); }
    if(g_skate){ const char*bpz=getenv("REVA_BOARD"); if(!bpz) bpz="props/v1_board.rpack";
      FILE*bf=fopen(bpz,"rb");
      if(bf){ fclose(bf); boardP=loadPack(bpz); hasBoard=1;
              printf("board loaded: %u meshes scale %.4f\n",boardP.nMeshes,boardP.scale); }
      else printf("no board at %s -- she skates without one\n",bpz); }
    /* S5a: the weapon rack. Missing files are fine -- the layer falls back to neon. */
    for(int wi=0;wi<WPNDEF_N;wi++){
        FILE*wf=fopen(WPNDEF[wi].file,"rb"); if(!wf) continue; fclose(wf);
        g_wpnPack[wi]=loadPack(WPNDEF[wi].file);
        if(!g_wpnPack[wi].drawable) continue;
        g_wpnPackOK[wi]=1; g_wpnAnyProp=1;
        int wk=WPNDEF[wi].kind;
        if(wk>0&&wk<WPROP_N&&g_wpnNByKind[wk]<8) g_wpnByKind[wk][g_wpnNByKind[wk]++]=wi; }
    printf("THE ARSENAL: melee=%d pistol=%d spread=%d beam=%d (%d meshes)\n",
           g_wpnNByKind[1],g_wpnNByKind[2],g_wpnNByKind[3],g_wpnNByKind[4],
           g_wpnNByKind[1]+g_wpnNByKind[2]+g_wpnNByKind[3]+g_wpnNByKind[4]);
    /* THE PARK: static world geometry at the origin, baked at final size. */
    if(g_skate){ parkBuild(); hasPark=1; }   /* generated, not scanned */

    /* programs (opaque + cutout) */
    /* DARKER, we are in space -- key light dimmer, ambient floor way down; the color now
     * comes from the DANCE FLOOR: 3 nearest colored point lights (orbs / lamps / snake / reva) per dancer */

    unsigned floorVbo; { float fq[12]={-FLOOR_X,FLOOR_ZN, FLOOR_X,FLOOR_ZN, FLOOR_X,0.5f, -FLOOR_X,FLOOR_ZN, FLOOR_X,0.5f, -FLOOR_X,0.5f};
        floorVbo = gfxBufferNew();  gfxBufferData(floorVbo, sizeof fq, fq, 0); }
    #define NSTAR 2400
    #define NNEB 56
    unsigned starVbo; { static float sd[(NSTAR+NNEB)*5]; unsigned sr=4242;   /* a dancefloor IN SPACE -- full sphere of stars + nebulae, far shell, never clipping */
        for(int i=0;i<NSTAR+NNEB;i++){ float dx3,dy3,dz3,ln3;
            do{ sr=sr*1103515245+12345; dx3=((sr>>8)%2000)/1000.f-1.f;
                sr=sr*1103515245+12345; dy3=((sr>>8)%2000)/1000.f-1.f;
                sr=sr*1103515245+12345; dz3=((sr>>8)%2000)/1000.f-1.f;
                ln3=sqrtf(dx3*dx3+dy3*dy3+dz3*dz3); }while(ln3<0.15f||ln3>1.f);
            sr=sr*1103515245+12345; float rr=300.f+((sr>>8)%1600)/10.f;
            sd[i*5]=dx3/ln3*rr; sd[i*5+1]=dy3/ln3*rr; sd[i*5+2]=dz3/ln3*rr;
            if(i>=NSTAR){ sr=sr*1103515245+12345; sd[i*5+3]=((sr>>8)%628)/100.f;
                sr=sr*1103515245+12345; sd[i*5+4]=5.f+((sr>>8)%300)/100.f; continue; }   /* nebula: tw.y>4 flags BIG soft blob */
            sr=sr*1103515245+12345; sd[i*5+3]=((sr>>8)%628)/100.f;
            sr=sr*1103515245+12345; sd[i*5+4]=1.f+((sr>>8)%300)/100.f; }
        starVbo = gfxBufferNew();  gfxBufferData(starVbo, sizeof sd, sd, 0); }
    /* lasers: unit quad 0..-30 in Y, widening; per-beam model matrix + color */
    unsigned laserVbo; { float lq[18]={ -0.05f,0,0, 0.05f,0,0, 0.16f,-30,0,  -0.05f,0,0, 0.16f,-30,0, -0.16f,-30,0 };
        laserVbo = gfxBufferNew();  gfxBufferData(laserVbo, sizeof lq, lq, 0); }

    /* ---- VENUE JUMBOS: Holly's head on jumbotrons (arena-broadcast style) ---------
     * One textured quad per screen, all sampling the single off-screen head. Emissive: a screen
     * emits, it is not lit, so there is no normal and no light term -- which also means the
     * shadow pass can ignore them entirely. */
    /* x, y, z, yaw(rad), half-width, half-height. 4:3 to match the head's own target. */
    static const float JUMBOS[][6]={
        {-188.f, 26.f,  -45.f,  1.5708f, 30.f, 22.5f},   /* left wall, facing in */
        {-188.f, 26.f, -105.f,  1.5708f, 30.f, 22.5f},
        {-188.f, 26.f, -165.f,  1.5708f, 30.f, 22.5f},
        { 188.f, 26.f,  -45.f, -1.5708f, 30.f, 22.5f},   /* right wall */
        { 188.f, 26.f, -105.f, -1.5708f, 30.f, 22.5f},
        { 188.f, 26.f, -165.f, -1.5708f, 30.f, 22.5f},
        { -70.f, 30.f, -186.f,  0.0f,    36.f, 27.f },   /* back wall, the big pair */
        {  70.f, 30.f, -186.f,  0.0f,    36.f, 27.f },
    };
    const int NJUMBOS=(int)(sizeof JUMBOS/sizeof JUMBOS[0]);
    /* tracers: 4 ribbons x 48 points, streamed; pos+rgb per vertex */
    #define TRN 48
    static float trail[4][TRN][3]; static int trailN[4]={0,0,0,0};
    int trBone[4]={-1,-1,-1,-1};
    unsigned trailVbo; trailVbo = gfxBufferNew();
    /* round-4 FX buffers: blob shadows (one draw, all dancers), lava-lamp blobs, flame snake */
    unsigned shadowVbo,lampVbo,snakeVbo; shadowVbo = gfxBufferNew(); lampVbo = gfxBufferNew(); snakeVbo = gfxBufferNew();
    /* REAL SHADOW -- reva's silhouette from the key light, a 256^2 mask the floor
     * receives (gfxFloorShadow: the renderer draws the casters through g_shvp). One frame of lag. */
    #define SHMAP 256
    unsigned shTex=gfxTextureNew(); gfxTargetMake(shTex,SHMAP,SHMAP,0);
    /* THE BROADCAST FEED (08-01): a camera feed up on every other display.
     * Ruby Rhod's show cuts to the talent, so half the screens carry a live close-up of
     * reva while she performs underneath them. Same economics as Holly: one small target,
     * sampled by as many quads as we like. ONLY her mesh is redrawn -- re-rendering the
     * crowd from a second camera would draw 115 dancers twice and gut the frame rate. */
    #define FEEDW 256
    #define FEEDH 192
    unsigned feedTex=gfxTextureNew(); gfxTargetMake(feedTex,FEEDW,FEEDH,0);
    /* Holly's head into its own small target. 160x120 is his native crush -- the jaw is a vertex
     * stretch on a mesh with sealed lips, and it only reads as a mouth at that resolution. One
     * draw feeds every screen in the venue, so more jumbotrons cost nothing. */
    { const char*hm=getenv("HOLLY_MODEL");
      if(!holly_fbo_init(160,120,hm?hm:"holly.hlm")) fprintf(stderr,"[holly] init failed\n"); }
    static M4 g_shvp;   /* light view-proj used for BOTH the mask pass and the floor sampling */
    /* MORE SHADOWS: everyone inside the mask window casts -- reva + the showcased npc +
     * neighbors within 6u. Up to 8 mesh entries stashed per frame (each remembers its pack+model). */
    /* ELECTRIC ORBS -- 6 plasma balls wandering
     * the whole floor in 3D; they are the dance floor's COLORED LIGHT SOURCES: every dancer is lit by its
     * 3 nearest lights, the floor pools color under the nearest 4 */
    #define NORB 6
    static float orbP[NORB][3], orbC[NORB][3];
    #define NLITE (NORB+3)
    static float liteP[NLITE][3], liteC[NLITE][3]; static int nLite=0;
    if(hasReva){ /* find her hand/foot nodes by name suffix via the pack string table (header slot 11 = offStrings; 12 is offSizing — that off-by-one segfaulted on first boot) */
        unsigned offStr=((unsigned*)(revaP.pk+4))[11];
        unsigned nStr=*(unsigned*)(revaP.pk+offStr);
        const char*names[4]={"LeftHand","RightHand","LeftFoot","RightFoot"};
        for(unsigned i=0;i<revaP.nNodes;i++){ unsigned si2=revaP.nodes[i].name;
            if(si2>=nStr) continue;
            unsigned so=*(unsigned*)(revaP.pk+offStr+4+si2*8), sl=*(unsigned*)(revaP.pk+offStr+4+si2*8+4);
            const char*blob=(const char*)revaP.pk+offStr+4+nStr*8;
            char nm[128]; if(sl>127) sl=127; memcpy(nm,blob+so,sl); nm[sl]=0;
            for(int k=0;k<4;k++){ size_t ln=strlen(names[k]), nl=strlen(nm);
                if(nl>=ln&&!strcmp(nm+nl-ln,names[k])&&(nl==ln||nm[nl-ln-1]<'A'||1)) if(trBone[k]<0) trBone[k]=(int)i; } }
        /* the board's root bone: the shallowest bone of the second mesh's own skin */
        if(revaP.nMeshes>1){
            PMesh*BM=&revaP.meshes[1];
            unsigned short*bmv=(unsigned short*)(revaP.pk+BM->offBoneMap);
            int bestD=1<<30;
            for(unsigned bi=0;bi<BM->boneCount;bi++){
                int nd=bmv[bi], dpt=0, guard=0;
                while(nd>=0 && guard<128){ nd=revaP.nodes[nd].parent; dpt++; guard++; }
                if(dpt<bestD){ bestD=dpt; g_boardRoot=bmv[bi]; } }
            printf("board root bone: node %d (depth %d)\n",g_boardRoot,bestD); }
        if(g_rvSki>=0 && revaP.nClips>(unsigned)g_rvSki){
            PClip*IC=&revaP.clips[g_rvSki];
            float bestY=-1e30f; unsigned bestF=0;
            float (*QT)[4]=malloc(revaP.nNodes*sizeof(*QT));
            float (*TT)[3]=malloc(revaP.nNodes*sizeof(*TT));
            float (*SS)[3]=malloc(revaP.nNodes*sizeof(*SS));
            M4*WW=malloc(revaP.nNodes*sizeof(M4));
            if(QT&&TT&&SS&&WW){
                unsigned char*tr=revaP.pk+IC->offTracks;
                for(unsigned f=0;f<IC->frames;f++){
                    for(unsigned n=0;n<revaP.nNodes;n++){
                        memcpy(TT[n],revaP.nodes[n].t,12);
                        memcpy(QT[n],revaP.nodes[n].q,16);
                        memcpy(SS[n],revaP.nodes[n].s,12); }
                    for(unsigned t=0;t<IC->nTracks;t++){
                        unsigned node=*(unsigned*)(tr+t*12), typ=*(unsigned*)(tr+t*12+4), offD=*(unsigned*)(tr+t*12+8);
                        float*dd=(float*)(revaP.pk+offD);
                        if(typ==0) memcpy(QT[node],dd+f*4,16);
                        else if(typ>=3) ; else if(typ==1) memcpy(TT[node],dd+f*3,12);
                        else memcpy(SS[node],dd+f*3,12); }
                    float loY=1e30f, hiY=-1e30f;
                    for(unsigned n=0;n<revaP.nNodes;n++){
                        M4 loc; m4_trs(loc,TT[n],QT[n],SS[n]);
                        if(revaP.nodes[n].parent<0) memcpy(WW[n],loc,64);
                        else m4_mul(WW[n],WW[revaP.nodes[n].parent],loc);
                        if(revaP.nodes[n].kind==1){ float yy=WW[n][13]; if(yy<loY)loY=yy; if(yy>hiY)hiY=yy; } }
                    /* STANDING = feet down AND body extended. Lowest average height finds the
                     * deepest crouch instead, which is how a folded-double pose became the
                     * idle. Score the upright silhouette: tall span, low feet. */
                    if(hiY>loY){ float score=(hiY-loY)-loY*0.65f;
                                 if(score>bestY||bestY>1e29f){ bestY=score; bestF=f; } } }
                g_idleHold=(IC->frames>1)?((float)bestF/(float)IC->frames):0.f;
                printf("idle hold: frame %u/%u of the ride clip (most upright stance)\n",bestF,IC->frames); }
            free(QT); free(TT); free(SS); free(WW); }
        castInit();   /* ⛔ before anything can load a partner: a zero struct is a body inside a body */
        g_markEdit = getenv("REVA_MARKEDIT")?1:0;
        if(g_markEdit) printf("[mark] EDITOR ON. 1/2/3/4 = eye_l eye_r mouth chin, "
                              "LEFT CLICK to place. Written on every click.%c",10);
        g_clothOff = getenv("REVA_NOCLOTH")?1:0;
        { const char*cf=getenv("REVA_CLOTHFOLLOW"); if(cf) g_clothFollow=(float)atof(cf); }
        { const char*ce=getenv("REVA_CLOTHEASE"); if(ce) g_clothEase=(float)atof(ce); }
        if(g_clothOff) printf("[cloth] OFF (REVA_NOCLOTH) -- garments use the skinned pose%c",10);
        g_wmsOff = getenv("REVA_NOWMS")?1:0;
    if(g_wmsOff) printf("[wms] the Williams layer is OFF (REVA_NOWMS)\n");
    g_procRig = (trBone[2]>=0 && trBone[3]>=0 && revaP.pbOK>=10);
        printf("tracer bones: %d %d %d %d   canon %d/%d   rig=%s\n",
               trBone[0],trBone[1],trBone[2],trBone[3],revaP.pbOK,NCANON,
               g_procRig?"PROCEDURAL":"BAKED (plays its own clips)"); }
    postInit(W,H);   /* ⭐ the post pass's targets and programs (09-11) */
    sfxInit();       /* ⭐ the audio mixer (09-11) */

    /* slots on a 26x12-ish floor */
    Slot*slots=calloc((size_t)NS,sizeof(Slot)); unsigned seed=777;   /* ⛔ ZEROED (09-30): pgT/pgD/blk were never set at spawn and pgT is read in the roam tree -- heap garbage, and with it the receipt, varied with the heap's history */
    g_slots=slots; g_NS=NS; g_packs=packs; g_packN=NP; g_packBoot=NP;   /* the weapon layer's file-scope view of the floor */
    g_api.bodies=slots; g_api.nBodies=NS;   /* and a game's */
    /* S4f: two standing donors -- the MOB1 mocap bank (CivicDefenseV2 Source/FBX, baked
     * via skate_src/mob1/merge_mob1.py) first, the co-star's zombie-prototype set second. Authored clips
     * reach the whole crowd by joining either pack. REVA_NODONOR=1 disables. */
    if(!getenv("REVA_NODONOR")){
        static Pack dn1,dn2; FILE*df;
        if((df=fopen("v1_mob1.rpack","rb"))){ fclose(df); dn1=loadPack("v1_mob1.rpack"); dnElect(&dn1); }
        if((df=fopen("v1_kaeli.rpack","rb"))){ fclose(df); dn2=loadPack("v1_kaeli.rpack"); dnElect(&dn2); }
        bankAdd(&dn1); bankAdd(&dn2);
        /* ⛔ AND THE MOVES PACK WAS NEVER IN THE BANK. v1_moves carries 37 clips --
         * a_convo, a_convo2, w_look, w_text, w_cheer, w_music, z_greet, z_bow, z_hello --
         * and bankFind only ever walked mob1, the co-star and the zombies. No scene has ever
         * been able to ask for any of them by name, which is most of the reason the film
         * has no business in it. */
        if(g_movesPk) bankAdd(g_movesPk);
        /* ⛔ AND NEITHER WAS THE TWO-BODY ROOM REPERTOIRE. The z_rm_* clips -- the
         * curl-up under the covers, and everything authored beside it -- live ONLY in
         * v1_kmoves and v1_<body>_pj, and the bank loads v1_<body>. So a scene could ask
         * for z_rm_curlsleep and get it if and only if the co-star's own pack was playing the
         * part. Cast anybody else and the clip came back NOT FOUND, the partner fell
         * back to z_rest, and a woman who is supposed to be asleep under the covers
         * stood on the bed IN A T-POSE.
         * Exactly the fault the comment above describes for v1_moves, one pack later.
         * Added LAST so it shadows nothing: bankFind returns the first pack that has
         * the name, so mob1, the co-star and moves keep their claims. */
        { static Pack dn3; FILE*rf;
          if((rf=fopen("packs/v1_kmoves.rpack","rb"))){ fclose(rf);
              dn3=loadPack("packs/v1_kmoves.rpack"); bankAdd(&dn3); } }
        printf("DONOR BANK v2: walks=%d idles=%d convo=%d run=%s gun=%s down=%s flee=%s%s\n",
            g_dnNWalks,g_dnNIdles,g_dnNConvo,g_dnRunP?"y":"-",g_dnGunP?"y":"-",g_dnDownP?"y":"-",
            g_dnFleeP?"y":"-",g_dnFlee2>=0?"+2":""); }
    /* Z4 THE ZOMBIE BANK, loaded outside the REVA_NODONOR guard: it is the film's asset,
     * not one of the crowd's donor roles, and killing the crowd's donors to get a clean
     * floor should not empty the street. */
    { static Pack zp; FILE*zf;
      if((zf=fopen("packs/v1_zmoves.rpack","rb"))){ fclose(zf);
          zp=loadPack("packs/v1_zmoves.rpack"); g_zombP=&zp; bankAdd(&zp);
          printf("ZOMBIE BANK: %u clips, %u nodes, pbOK=%d cbOK=%d\n",
                 zp.nClips,zp.nNodes,zp.pbOK,zp.cbOK); } }
    g_slotStride=(int)sizeof(Slot);   /* the director indexes slots through this -- NEVER let its mirror struct go stale again */
    int cols=(int)ceilf(sqrtf(NS*2.f));
    for(int i=0;i<NS;i++){ seed=seed*1103515245+12345;
        Pack*P=&packs[i%NP];
        slots[i].pack=i%NP; slots[i].clip=(int)((seed>>8)%(P->nClips?P->nClips:1));
        seed=seed*1103515245+12345; slots[i].phase=(seed>>8)%1000;
        slots[i].x=(i%cols)*(2*FLOOR_X/cols)-FLOOR_X+1.5f; slots[i].z=-(i/cols)*((-FLOOR_ZN-2.f)/(NS/cols+1))-1.0f;
        seed=seed*1103515245+12345; slots[i].rotY=((seed>>8)%628)/100.f;
        slots[i].state=0; slots[i].mate=-1; slots[i].moving=0; slots[i].tT=frnd()*3.f;
        slots[i].stateT=0; slots[i].tx=slots[i].x; slots[i].tz=slots[i].z;
        slots[i].h=slots[i].rotY; slots[i].spd=0;
        { Pack*Pi=&packs[slots[i].pack];
          slots[i].baseClip=Pi->nDance?Pi->danceIdx[rand()%Pi->nDance]:0;
          slots[i].baseFrame=Pi->nClips?(unsigned)rand()%Pi->clips[slots[i].baseClip].frames:0;
          slots[i].nextClip=slots[i].baseClip; slots[i].nextFrame=slots[i].baseFrame;
          slots[i].blend=0; slots[i].wph=0;
          slots[i].mode=(frnd()<0.30f&&Pi->nDance>0)?1:0; slots[i].clipT=14.f+frnd()*22.f;
          rollGenome(&slots[i],Pi->nClips,NULL,1);
          /* HAND-BAN LIFTED (07-23): the 'invisibles' were the stale-SlotL indexing bug,
           * not the models. Only MEASURED bans remain: auto-quarantine (displaced/microscopic)
           * plus the parade's drawable check. */
          g_slotBan[i]=0; g_slotDrawable[i]=Pi->drawable;
          g_slotNoShow[i]=strstr(Pi->label,"Character")?1:0;
          /* REVA_NOSKY=1 grounds the sky layer -- the flyers cross every mode's sky and
           * have been a standing complaint about the platformer lens.
           * (It was TRIED as a determinism fix and is NOT one: two fixed-step runs of
           * the same frame still differ in the same thin horizon band with the sky
           * grounded, so that residue is distant venue lighting, not the flyers.) */
          { static int nosky=-1; if(nosky<0){ const char*e=getenv("REVA_NOSKY"); nosky=(e&&e[0]!='0'); }
            g_slotAlt[i]=(Pi->fly&&!nosky)?(8.f+((i*73)%220)/10.f):0.f; }   /* 8-30u: THROUGH the space */
          if(Pi->fly) printf("SKY: %s (fly=%d alt=%.1f scale=%.2f)\n",Pi->label,Pi->fly,g_slotAlt[i],Pi->scale);
          slots[i].altT=g_slotAlt[i]; } }

    /* ================= GAME INIT (08-25: the ten doors) =========================
     * Whatever the mode needs staged before the first frame: hero spawn, armies placed,
     * the cast pinned to their marks. Slots stay the population everywhere -- a mode
     * borrows dancers, it never invents new people. */
    memset(&GM,0,sizeof GM);
    GM.hpMax=100.f; GM.hp=100.f;
    wpnReset();
    objReady();   /* what a game asked for in boot gets read before it starts */
    switch(g_mode){
    case MODE_GAME:
        g_game->start(g_setNI>0);
        break;
    case MODE_ANIM:
        /* THE ROOM IS EMPTIED: every other body is hidden, so nothing in frame moves
         * except the one under test. */
        for(int i9=0;i9<NS;i9++){ g_slotHide[i9]=1; g_slotBan[i9]=1; }
        g_rvx=0.f; g_rvz=-150.f; GM.stage=LAB_IDLE; GM.tB=0.f; g_bcN=0;
        GM.wKind=0; GM.wVar=-1; g_labArm=0;
        { const char*e9=getenv("REVA_ANIMSTATE"); if(e9){ GM.stage=atoi(e9)%LAB_N; GM.flag=1; } }
        gmBanner("ANIM LAB - A NEXT STATE, X BACK, B RESET",5.f);
        break;
    default: break;
    }
    if(g_mode!=MODE_DANCE) printf("GAME: %s%s%s\n",MODE_NAME[g_mode],
        MODE_VAR[g_mode][0]?" / ":"",MODE_VAR[g_mode][0]?MODE_VAR[g_mode][g_modeVariant]:"");

    unsigned maxNodes=0; for(int i=0;i<NP;i++) if(packs[i].nNodes>maxNodes) maxNodes=packs[i].nNodes;
    if(hasReva&&revaP.nNodes>maxNodes) maxNodes=revaP.nNodes;
    if(maxNodes<512) maxNodes=512;   /* room for a body that's read in later */
    g_poseNodes=(int)maxNodes;
    float (*T)[3]=calloc(maxNodes,12); float (*Q)[4]=calloc(maxNodes,16); float (*S)[3]=calloc(maxNodes,12);
    float (*T2)[3]=calloc(maxNodes,12); float (*Q2)[4]=calloc(maxNodes,16); float (*S2)[3]=calloc(maxNodes,12);
    M4 *world=calloc(maxNodes,sizeof(M4));
    static float pal[MAXB*16];

    netStart(NS);   /* (step 29) a session: everything's loaded, and every machine's clock starts from nothing together */
    double t0=now_ms(), lastStat=wall_ms(); long frames=0; __attribute__((unused)) int first=1; long tickAcc9=0;
#ifndef REVA_SDL
#endif
    int *order=calloc((size_t)NS,sizeof(int)); float *depth=calloc((size_t)NS,sizeof(float));

    langBoot();   /* (step 26) the language asked for, or the one chosen last time */
    double lastT=now_ms();
    double workAcc=0; long workN=0;   /* bench: sim+GPU work per frame, flip wait EXCLUDED */
    while(g_run){
        if(gfxXrQuit()){ printf("[xr] the headset's runtime asked the game to end\n"); fflush(stdout); g_run=0; break; }
        bridgePoll();
        netPoll();   /* (step 29) a session: this machine waits here until it has every pad for the next tick */
        gameLibPoll();
#ifdef REVA_VK
        g_interpLive=g_interpOn&&(g_interpForce||g_mode!=MODE_ANIM)&&!gfxXrOn();   /* (11-11) with a headset on, the headset sets the beat */
        /* a window's vsync can't be trusted to pace this (measured 11-05: 600 refreshes a second in a window on a 100 Hz panel), so the loop keeps the display's own beat itself: a refresh is begun no sooner than one field after the last */
        if(g_interpLive){ static double next9=0.0; const double per9=g_ledFieldMs>0.5?g_ledFieldMs:10.0; double w9=wall_ms(); if(next9<=0.0||w9-next9>per9*4.0) next9=w9;
            while(next9-w9>1.2){ SDL_Delay(1); w9=wall_ms(); } next9+=per9; }
#endif
        clockFrame();
        /* (step 25) THE FAIR VIEW. A game played against other people says the shape its picture is played in (E->tune->fairAspect), and on a screen wider than that the world is
         * drawn in a view of that shape in the middle, through the same lens, with black either side: a wider monitor shows no more of the level. The HUD keeps the whole screen.
         * REVA_FAIR=<w/h> says it for a game that doesn't, to look at */
        { static float env9=-1.f; if(env9<0.f){ const char*e9=getenv("REVA_FAIR"); env9=(e9&&e9[0])?(float)atof(e9):0.f; }
          const float want9=GAME_ON?(env9>0.f?env9:g_tune.fairAspect):0.f;
          g_fairNow=(want9>0.2f&&g_dispAspect<=0.f&&(float)W/(float)H>want9*1.01f)?want9:0.f; }
#ifdef REVA_VK
        gfxBetweenSet(g_interpLive,(float)(g_tickAcc/g_tickMs));
        btwDbg(g_tickBetween,W,H);
        if(g_tickBetween){ gfxBetween(); ledgerMark(wall_ms()); SDL_PumpEvents(); continue; }   /* (step 25) no tick this refresh: the picture between the last two, and the rules don't run */
#endif
        videoPoll(&W,&H);
        { static int ok9=0; if(!ok9&&g_tickTotal>1800){ ok9=1; scrBootOk(); } }   /* this run lasted: it wasn't a power cycle asking for setup */
#ifdef REVA_VK
        if(!g_noDraw) gfxPagesBuild();
#endif
        if(!g_noDraw) gfxFrameBegin();
        simHashBegin();
        stStep(g_rvx,g_rvz,g_tickTotal);
        bodyPackStep();
        if(GAME_ON) levelStep();
        trackApply();   /* the set's timeline: lights, beams and movers at this moment of the scene, before any pass reads them */
        artApply();     /* (11-11) and which frame of its drawings this moment is (setart.inc) */
        animTick();
        /* the bodies' view of the scene clock: latched on an exposure, held between */
        if(!g_stepHeld||g_scnTA<0.f||g_scnT<g_scnTA) g_scnTA=g_scnT;
        { float wS9=animStepS(simStepS())*(GAME_ON?g_timeScale:1.f);   /* a game's slow motion takes the world with it */
          actorsTick(wS9);
          rbStep(wS9);   /* ⭐ the loose objects (09-11) */
          objStep(wS9);   /* and a game's */
          if(GAME_ON){ rideStep(wS9); liqStep(wS9); } }   /* (11-05) and what a game's bodies ride: engine/ride.inc */
        if(g_tod>=0.f){   /* Z6: the day runs, and the light is recomputed every frame */
            if(g_todRate!=0.f) g_tod+=g_todRate*animStepS(simStepS());
            todApply(g_tod); }
        postDecide(g_mode,MODE_ANIM);   /* ⭐ decided here, once a frame, so the knee goes to one place */
        static double frameT0; if(g_tickFirst){ dutyRest(frameT0); frameT0=wall_ms(); } passStart();   /* (11-09) a capture's GPU held under its duty */
        double tsec=(now_ms()-t0)/1000.0;
        double dt=(now_ms()-lastT)/1000.0; lastT=now_ms(); if(dt>0.25)dt=0.25;
        if(GAME_ON) dt*=(double)g_timeScale;
        g_chDt=dt;   /* H1: the chains read the frame clock */
        g_frameStamp++;
        /* ⭐ THE LENS IS NOT A PUPPET. Under stop motion tsec/dt above are the EXPOSURE clock
         * -- zero on a held frame -- and a camera fed that would step with the bodies, which
         * reads as a dropped frame rather than as stop motion. It gets the raw clock. */
        { static double camLast=-1.0; double cr=now_raw_ms();
          g_camDt=(camLast<0.0)?dt:(cr-camLast)/1000.0; camLast=cr;
          if(g_camDt>0.25) g_camDt=0.25; }
        /* Ruby Rhod runs the broadcast -- the ported director cuts on bars of the live bpm */
        float eye[3],ctr[3];
        if(g_stepFps>0.f) camDirector((now_raw_ms()-t0)/1000.0,g_camDt,NS,slots,eye,ctr);
        else camDirector(tsec,dt,NS,slots,eye,ctr);
        if(g_gameLens.shakeT>0.f){ float a9=g_gameLens.shake*g_gameLens.shakeT/g_gameLens.shakeLen, t9=(float)g_tickTotal;   /* from the tick, so it's the same every run and no dice are spent */
            eye[0]+=a9*sinf(t9*1.7f); eye[1]+=a9*sinf(t9*2.3f+1.f)*0.6f; ctr[0]+=a9*sinf(t9*1.1f+2.f)*0.5f; g_gameLens.shakeT-=simStepS(); }
        /* REVA_CAMLOCK=1 -- A MEASUREMENT LENS. The dance floor runs a self-directing show, so
         * two runs of the same frame come back from different points in the cut cycle
         * and 83% of the pixels differ even with the clock and the seed pinned. That
         * makes a door impossible to regression-test on its picture: it is not the
         * WORLD that moved, it is the CAMERA. This pins the lens to a fixed offset from
         * the hero -- deterministic, always framing her -- so anim_bench/shotsweep.py
         * can diff a door frame for frame. It deliberately hides camera regressions
         * (those want their own test); what it buys is every OTHER rendering
         * regression, on all eleven doors, which is the class that has actually bitten.
         * Off by default: this is not a lens anyone should watch the show through. */
        { static int lock=-1; if(lock<0){ const char*e=getenv("REVA_CAMLOCK"); lock=(e&&e[0]!='0'); }
          if(lock){ eye[0]=g_rvx+4.5f; eye[1]=3.2f; eye[2]=g_rvz+6.5f;
                    ctr[0]=g_rvx;      ctr[1]=1.5f; ctr[2]=g_rvz; } }
        lensNow(eye,ctr);   /* (step 25) this frame's lens and look; the set's timeline may have the lens */
        M4 proj,view,vp;
        g_aspect=VIEW_ASPECT(W,H);   /* ⭐ the medium's lateral guarantee needs the frame's SHAPE, and W/H are locals in here */
        if(g_lensNow.camOn&&g_lensNow.fov>0.f){ float fv9=fishFov(g_lensNow.fov,VIEW_ASPECT(W,H)); m4_persp(proj,fv9,VIEW_ASPECT(W,H),0.30f,fmaxf(900.0f,g_setFar)); }   /* the timeline's lens */
        else if(g_fpvNow){ float fv9=fishFov(68.0f,VIEW_ASPECT(W,H)); m4_persp(proj,fv9,VIEW_ASPECT(W,H),0.12f,fmaxf(900.0f,g_setFar)); }   /* first person: a wide eye and a near plane inside her own head */
        else if(GAME_ON&&g_tune.firstPerson){ m4_persp(proj,g_tune.lensFov>0.f?g_tune.lensFov:68.0f,VIEW_ASPECT(W,H),0.12f,fmaxf(520.0f,g_setFar)); }   /* (11-06) a game's first person: its lens at her eyes, the near plane in close */
        else if(g_camOn&&g_camFov>0.f){ float fv9=fishFov(g_camFov,VIEW_ASPECT(W,H)); m4_persp(proj,fv9,VIEW_ASPECT(W,H),0.30f,fmaxf(900.0f,g_setFar)); }   /* a street runs to the horizon */
        else if(g_scnT>=0.f){ float fv9=fishFov(SHOT_FOV[g_scnShot&7],VIEW_ASPECT(W,H)); m4_persp(proj,fv9,VIEW_ASPECT(W,H),0.30f,fmaxf(140.0f,g_setFar)); }   /* a scene is two bodies on a black stage */
        else          { m4_persp(proj,(GAME_ON&&g_tune.lensFov>0.f)?g_tune.lensFov:42.0f,VIEW_ASPECT(W,H),0.8f,fmaxf(520.0f,g_setFar)); }   /* the far plane is far enough that there is no view distance */
        m4_lookat(view, eye[0],eye[1],eye[2], ctr[0],ctr[1],ctr[2]);
        if(g_lensNow.roll!=0.f){ const float a9=g_lensNow.roll*0.0174532925f, c9=cosf(a9), s9=sinf(a9); M4 r9,t9; memset(r9,0,64); r9[0]=c9; r9[1]=s9; r9[4]=-s9; r9[5]=c9; r9[10]=1.f; r9[15]=1.f; m4_mul(t9,r9,view); memcpy(view,t9,64); }   /* (step 25) the lens rolled about its own line */
        /* the lens's own position, for anything later in the frame that has to face it
         * (a billboarded screen). Stamped here because this is where it is decided. */
        g_camEye[0]=eye[0]; g_camEye[1]=eye[1]; g_camEye[2]=eye[2];
        m4_mul(vp,proj,view);
        /* (11-11) A HEADSET (engine/xr.inc): the lens is where its space is anchored, and each eye is drawn where it is. vp becomes a view round both, for the cull */
        g_xrDraw=0; if(gfxXrOn()&&!g_noDraw){ g_xrLive=1; g_xrDraw=gfxXrFrame(g_xrEye,g_xrH); if(g_xrDraw){ xrViews(eye,ctr,vp,fmaxf(520.0f,g_setFar)); g_camEye[0]=eye[0]; g_camEye[1]=eye[1]; g_camEye[2]=eye[2]; } }
        sfxListener(eye,ctr);   /* ⭐ the mixer hears from the lens (09-11) */
        /* ⭐ the sun's window follows what the lens is looking at (a little past the aim, since
         * most of a frame lies beyond it), for next frame's pass */
        { float dx=ctr[0]-eye[0], dz=ctr[2]-eye[2], dl=sqrtf(dx*dx+dz*dz); if(dl<1e-3f) dl=1.f;
          g_sunFocus[0]=ctr[0]+dx/dl*g_shadowExt*0.35f; g_sunFocus[1]=0.f; g_sunFocus[2]=ctr[2]+dz/dl*g_shadowExt*0.35f; }
        /* ⭐ FOG needs the eye and, unless a scene named one, the SKY'S OWN HORIZON as its colour,
         * so the far end of a street dissolves into the sky it stands under instead of a grey. */
        /* ⭐ (10-23) the toon key and the room gradient, read once (REVA_TOONKEY, REVA_ROOMGRAD): toonTell hands them to the renderer */
        { if(g_fog[3]>0.f&&g_fogAuto){ float hz[3]; skyHorizonC(hz); g_fog[0]=hz[0]; g_fog[1]=hz[1]; g_fog[2]=hz[2]; }
          { static int kinit=0; if(!kinit){ kinit=1; const char*e9=getenv("REVA_TOONKEY"); if(e9&&*e9) g_toonKeyCam=atoi(e9)!=0;
                          e9=getenv("REVA_ROOMGRAD"); if(e9&&*e9) g_roomGrad=(float)atof(e9);
                          printf("[toon] key %s, room gradient %.2f\n",g_toonKeyCam?"from the lens":"the set's sun (REVA_TOONKEY=0)",g_roomGrad); fflush(stdout); } } }


        netPreInput();
        jsPoll(tsec);   /* F310: any input takes reva manual */
        kbPoll(tsec);   /* WASD + mouse ride the same axes as the pad */
        { /* mouse look grabs the cursor only once a key or the mouse has actually been
           * used, so a pad-only session never loses its pointer; the start menu lets go. */
          static int grabOK=-1; if(grabOK<0) grabOK=getenv("REVA_NOMOUSEGRAB")?0:1;
#ifdef REVA_SDL
          const int gp9=GAME_ON?g_tune.pointer:0;   /* (11-12) a game's say: 1 the pointer shown and free (its menus), 2 grabbed for looking round */
          SDL_SetRelativeMouseMode((gp9==2||(gp9==0&&grabOK&&g_kbActive&&!g_pause))?SDL_TRUE:SDL_FALSE);
#else
          (void)grabOK;
#endif
        }
        { static int aper=-2; static long aF=0;   /* REVA_AUTOA=<frames>: pulse the A button for the bench
           * (headless jump/swing/fire verification -- menus run before this loop, untouched) */
          if(aper==-2){ const char*e=getenv("REVA_AUTOA"); aper=e?atoi(e):0; }
          if(aper>0&&MODE_PLAYS&&g_gameT>2.f&&!g_pause){ aF++;
              g_btnA=((aF%aper)<3)?1:0; } }   /* WRITE both edges: padless jsPoll never clears, the latch ate the pulses */
        inputTape(tsec);   /* ⭐ THE TAPE: record what the polls decided, or replace it with a recorded session */
        bridgeInput();
        netInput();   /* (step 29) a session: this machine's pads go out, and every machine's for this tick come in, where the tape's would */
        padsUpdate();
        double btF0=beatPLL(tsec);   /* one beat clock for floor + dancers this frame */
        g_btF=(float)btF0;
        /* GAME-LAYER INPUT POLICY, before anything reads g_manual this frame: action modes
         * are pad-driven ALWAYS (no 75s idle handback mid-match), the RTS and the rhythm
         * game must NOT let a button press drag the host around, and the grounded modes
         * pin the flight axis the A button would otherwise feed. */
        if(DOOR_ON_THE_PAD||g_mode==MODE_ANIM||GAME_ON){   /* a game is a run-around game */
            g_manual=1; g_manAlt=0.f; }
        if(g_mode==MODE_ANIM){   /* synthetic stick: WALK and RUN are exact, repeatable speeds */
            g_lx=0.f; g_ly=(GM.stage==LAB_WALK)?-0.26f:(GM.stage==LAB_RUN)?-1.f:0.f;
            if(g_labSpeed>=0.f) g_ly=-(g_labSpeed/8.6f);   /* live speed sweep */
            if(g_labFreeze){ g_lx=g_ly=0.f; }              /* frozen: every lens sees one instant */
            if(GM.stage==LAB_AIR) g_manAlt=2.2f; }
        else if(DOOR_OFF_THE_PAD){ g_manual=0; }
        /* ⛔ M0 (11-01): that line took the zombie cab too. The policy above set g_manual=1 for it ("a run-around game") and this
         * line undid it every frame, so no stick or key ever drove her in the cab -- the DANCE host walked her about. The
         * beat lane and another old door still keep the host; the zombie cab is the pad's. */
        { static double lastIn=-1e9;   /* M0: the zombie cab's attract host gives way to ANY live input -- pad, keys or a tape -- for 30 s */
          if(fabsf(g_lx)>0.3f||fabsf(g_ly)>0.3f||g_btnA||g_btnB||g_btnX||padsOthersTouched()) lastIn=tsec;
          g_cabAttract=GAME_ON&&(tsec-lastIn>30.0); }
        /* ================= THE START MENU =============================================
         * Start toggles it anywhere; while it is up the pad belongs to the menu (the
         * game sees a neutral controller) and the game-layer clock stops -- the dance floor
         * itself keeps dancing behind it, because the dance floor never stops.
         * REVA_AUTOPAUSE=<frame> opens it headlessly for the shot instruments. */
        { static long apF=-2, fcnt=0;
          if(apF==-2){ const char*e=getenv("REVA_AUTOPAUSE"); apF=e?atol(e):-1; }
          if(apF>=0&&++fcnt==apF) g_startEdge=1; }
        if(g_startEdge&&GAME_ON&&g_tune.pauseOwn) g_startEdge=0;   /* (11-12) the game's own: it reads Start (and Escape, if it bound it) from its pad */
        if(g_startEdge){ g_startEdge=0; g_pause=!g_pause; g_pauseSel=0;
            printf(g_pause?"START MENU open\n":"START MENU closed\n"); }
        if(!g_pause) g_ctlOn=0;
        if(g_pause&&g_ctlOn){ ctlTick(); g_lx=g_ly=g_rx=g_ry=g_rt=g_lt=0.f; g_btnA=g_btnB=g_btnX=g_btnY=0; g_btnMore=0u; }
        else if(g_pause){
            static int puP=0,pdP=0,paP=0,pbP=0;
            int up=(g_ly<-0.55f), dn=(g_ly>0.55f);
            if(up&&!puP) g_pauseSel=(g_pauseSel+PAUSE_N-1)%PAUSE_N;
            if(dn&&!pdP) g_pauseSel=(g_pauseSel+1)%PAUSE_N;
            puP=up; pdP=dn;
            if(g_btnB&&!pbP) g_pause=0;
            if(g_btnA&&!paP){
                if(g_pauseSel==0) g_pause=0;
                else if(g_pauseSel==1){
                    printf("START MENU: main menu -- clean relaunch\n"); fflush(stdout);
#ifdef _WIN32
                    _putenv("REVA_MODE="); _putenv("REVA_SKATE=");
                    _putenv("REVA_AUTOMODE="); _putenv("REVA_AUTOPICK=");
                    _putenv("REVA_SHOT="); _putenv("REVA_SELSHOT=");
                    _putenv("REVA_MODESHOT="); _putenv("REVA_AUTOPAUSE=");
                    _execv(g_argv[0],(const char*const*)g_argv);
#else
                    unsetenv("REVA_MODE"); unsetenv("REVA_SKATE");
                    execv(g_argv[0],g_argv);
#endif
                    printf("relaunch failed -- resuming\n"); g_pause=0; }
                else if(g_pauseSel==2){ g_pause=0; setupRun(&W,&H,0); }
                else if(g_pauseSel==3){ g_pause=0; setupRun(&W,&H,2); }
                else if(g_pauseSel==4){ g_ctlOn=1; g_ctlSel=0; g_ctlWait=0; }
                else g_run=0; }
            paP=g_btnA; pbP=g_btnB;
            g_lx=g_ly=g_rx=g_ry=g_rt=g_lt=0.f; g_btnA=g_btnB=g_btnX=g_btnY=0; g_btnMore=0u; }
        /* THE FLOOR'S SHADOW: a key light from high overhead, an ortho window centred on reva, her silhouette on the floor. The lens
         * is worked out here, first in the frame, and the renderer draws last frame's casters through it (gfxFloorShadow, g_shvp) */
        { float L[3]={0.30f,-1.0f,0.18f}; float ll=sqrtf(L[0]*L[0]+L[1]*L[1]+L[2]*L[2]);
          L[0]/=ll; L[1]/=ll; L[2]/=ll;
          float R2[3]={L[2],0,-L[0]}; float rl=sqrtf(R2[0]*R2[0]+R2[2]*R2[2]); R2[0]/=rl; R2[2]/=rl;
          float U2[3]={ L[1]*R2[2]-L[2]*R2[1], L[2]*R2[0]-L[0]*R2[2], L[0]*R2[1]-L[1]*R2[0] };
          float C[3]={g_rvx,1.7f+g_rvAlt*0.5f,g_rvz}; float ext=7.0f+g_rvAlt*0.8f;
          M4 svp; memset(svp,0,64);
          svp[0]=R2[0]/ext; svp[4]=R2[1]/ext; svp[8]=R2[2]/ext; svp[12]=-(R2[0]*C[0]+R2[1]*C[1]+R2[2]*C[2])/ext;
          svp[1]=U2[0]/ext; svp[5]=U2[1]/ext; svp[9]=U2[2]/ext; svp[13]=-(U2[0]*C[0]+U2[1]*C[1]+U2[2]*C[2])/ext;
          svp[15]=1;
          memcpy(g_shvp,svp,64);
          /* the head, off-screen, before the venue's own passes; it restores fbo+viewport */
          { static double ht=0; ht+=1.0/60.0;
            /* ⭐ AND HE FINALLY GETS A MOUTH. `g_holly_open` has been written
             * exactly nowhere since he was built -- a head with a working jaw, rendering
             * on screens, frozen at 0.45 for the life of the project. The scene has been
             * producing a 30 Hz envelope the whole time; when the speaker is the VOICE
             * slot (the director -- no body, so nothing else was using it) that envelope
             * drives his jaw. Same hinge as `mouth hinge` on a body: loudness, no visemes,
             * which is both the show's puppet rule and how Holly has always looked. */
            { int li8=scnActive();
              if(li8>=0&&g_scnL[li8].spk==SCN_VOICE){
                  float a8=scnMouth(&g_scnL[li8],scnBodyT());
                  if(a8<0.f)a8=0.f;
                  if(a8>1.f)a8=1.f;
                  g_holly_open=0.06f+0.80f*a8; }
              else g_holly_open=0.06f; }
            passMark("sim0"); { static int nh9=-1; if(nh9<0) nh9=getenv("REVA_NOHOLLY")?1:0; if(!nh9&&!g_noDraw) holly_fbo_render(g_holly_open,g_holly_wide,g_holly_seal,ht); } passMark("holly");
            }
}
        passMark("blobmap"); sunPass();passMark("sun");   /* ⭐ THE SUN'S SHADOW MAP (09-11): props + last frame's stashed bodies */
        nCast=0;   /* the shadow-caster stash refills as the frame draws */
        /* ORBS + THE LIGHT LIST: plasma balls mill about the floor in slow 3D lissajous wanders;
         * list = orbs + snake head + reva's own glow + the nearest lava lamp */
        { nLite=0;
          for(int i2=0;i2<NORB;i2++){ float ph4=(float)tsec*0.11f+i2*2.39996f;
              orbP[i2][0]=sinf(ph4*0.83f+sinf(ph4*0.31f)*1.4f)*(FLOOR_X*0.72f);
              orbP[i2][2]=FLOOR_ZN*0.5f+sinf(ph4*0.67f+1.1f+sinf(ph4*0.23f+i2)*1.2f)*(-FLOOR_ZN*0.42f);
              orbP[i2][1]=1.3f+1.7f*(0.5f+0.5f*sinf(ph4*1.19f+i2*1.7f));
              float hue=(float)tsec*0.04f+i2*0.61f;
              float rr,gg,bb; hsv(hue,&rr,&gg,&bb);
              /* electric bias: pull toward cyan/violet/white */
              orbC[i2][0]=0.35f+rr*0.75f; orbC[i2][1]=0.35f+gg*0.75f; orbC[i2][2]=0.55f+bb*0.65f;
              memcpy(liteP[nLite],orbP[i2],12);
              liteC[nLite][0]=orbC[i2][0]*1.5f; liteC[nLite][1]=orbC[i2][1]*1.5f; liteC[nLite][2]=orbC[i2][2]*1.5f; nLite++; }
          /* snake head light (flame) */
          { float t4=(float)tsec*0.50f;
            liteP[nLite][0]=sinf(t4*0.61f+1.7f*sinf(t4*0.213f))*(FLOOR_X*0.55f);
            liteP[nLite][2]=FLOOR_ZN*0.5f+sinf(t4*0.83f+1.3f+1.1f*sinf(t4*0.171f))*(-FLOOR_ZN*0.40f);
            liteP[nLite][1]=0.35f+1.9f*(0.5f+0.5f*sinf(t4*1.31f+sinf(t4*0.37f)));
            liteC[nLite][0]=1.5f; liteC[nLite][1]=0.75f; liteC[nLite][2]=0.18f; nLite++; }
          if(g_boltT>0){ memcpy(liteP[nLite],g_boltTo,12);
              liteC[nLite][0]=3.2f; liteC[nLite][1]=0.5f; liteC[nLite][2]=3.6f; nLite++; }   /* the bolt SEARS */
          /* reva glows purple on her neighbors */
          liteP[nLite][0]=g_rvx; liteP[nLite][1]=1.9f+g_rvAlt; liteP[nLite][2]=g_rvz;
          liteC[nLite][0]=0.95f; liteC[nLite][1]=0.40f; liteC[nLite][2]=1.35f; nLite++; }
        /* SOCIAL ROAM (page constants ported verbatim): mill/wander/meet/chat; walkers walk the
         * 26x12 floor, pairs seek each other, stop short, face, disband; stage line z<-0.5 held. */
        if(GAME_ON) nearBuild();
        if(GAME_ON&&g_agNG) agPoll();
        for(int si=0;si<NS;si++){ Slot*s=&slots[si];
            s->tT-=(float)dt;
            if(g_agBodyEver) agBodyTick(si,s->pack,s->spd,(float)dt);   /* a body with a graph of its own: here, seen or not, so its events fall on the same tick at any frame rate */
            /* ⭐ THE STREET'S OWN FEET (11-02). On the street the cab's bodies still walked by the DANCE FLOOR's manners, and
             * those are wrong out here: everyone was clamped to the floor's z (-190..-0.5), so half the street did not exist;
             * everyone was pushed out of a 2.4 u circle round her and carved around her, so the dead steered AWAY from the body
             * they wanted; they kept personal space from the neighbour they came for; and nothing met the world. The dead and the
             * neighbours walk here instead. `street_step 0` in the rules is the old feet, the control. */
            if(GAME_ON&&g_game->bodyStep(si,(float)dt)) continue;
            if(g_slotBhv[si]==BHV_DOWN){ s->moving=0; s->spd=0.f; continue; }   /* the falling fall in place */
            if(g_slotAlt[si]>0.001f){   /* SKY LAYER v2 (much higher, various directions,
                 * changing direction, picking paths): free 3D wander over the whole space */
                if(s->tT<=0){ s->tT=6.f+frnd()*10.f;
                    s->tx=(frnd()*2-1)*(FLOOR_X-8.f);
                    s->tz=FLOOR_ZN+5.f+frnd()*(-FLOOR_ZN-10.f);
                    s->altT=8.f+frnd()*22.f;   /* 8-30u: high enough to clear ground shots */
                    s->spd=1.4f+frnd()*2.6f; }
                float dxF=s->tx-s->x, dzF=s->tz-s->z;
                float tgF=atan2f(dxF,dzF);
                float dhF=fmodf(tgF-s->h+9.42477f,6.28318f)-3.14159f;
                s->h+=dhF*fminf(1,(float)dt*0.7f);
                s->x+=sinf(s->h)*s->spd*(float)dt; s->z+=cosf(s->h)*s->spd*(float)dt;
                /* CAMERA AVOIDANCE (creatures flashing/filling the screen = lens flybys):
                 * inside 16u of the lens they veer off and climb */
                { float cdx=s->x-g_lastE[0], cdz=s->z-g_lastE[2];
                  float cd=hypotf(cdx,cdz);
                  if(cd<16.f){ float away=atan2f(cdx,cdz);
                      float dhA=fmodf(away-s->h+9.42477f,6.28318f)-3.14159f;
                      s->h+=dhA*fminf(1,(float)dt*1.6f);
                      if(s->altT<g_lastE[1]+9.f) s->altT=g_lastE[1]+9.f+frnd()*6.f; } }
                g_slotAlt[si]+=(s->altT-g_slotAlt[si])*fminf(1,(float)dt*0.25f);
                s->rotY=s->h; s->moving=1; s->state=1;
                continue; }
            if(s->state==2){ Slot*o=(s->mate>=0)?&slots[s->mate]:NULL;
                if(!o||o->state==3){ s->state=0; s->tT=1+frnd()*2; s->moving=0; }
                else { float md=hypotf(o->x-s->x,o->z-s->z);
                    float nx=md>0.01f?(s->x-o->x)/md:1, nz=md>0.01f?(s->z-o->z)/md:0;
                    s->tx=o->x+nx*1.6f; s->tz=o->z+nz*1.6f; s->moving=md>2.3f;
                    if(md<2.3f||s->tT<=0){ float Tc=8+frnd()*12;
                        s->state=3; s->stateT=Tc; s->moving=0; s->spd*=0.3f;
                        if(o->state!=3){ o->state=3; o->stateT=Tc; o->mate=si; o->moving=0; o->tT=Tc; } s->tT=Tc; } } }
            else if(s->state==3){ s->stateT-=(float)dt; s->moving=0; s->spd*=fmaxf(0,1-(float)dt*5);
                if(s->stateT<=0){ Slot*o=(s->mate>=0)?&slots[s->mate]:NULL;
                    if(o&&o->mate==si&&o->state==3){ o->state=0; o->tT=1+frnd()*3; o->mate=-1; }
                    s->state=0; s->mate=-1; s->tT=1+frnd()*3; } }
            else if(s->tT<=0){ float roll=frnd();
                if(roll<0.42f&&NS>1){ int best=-1; float bd=1e9f;   /* seek someone NEARBY to dance with (random-anywhere = forever treks on the vast floor) */
                    for(int c4=0;c4<6;c4++){ int c2=rand()%NS;
                        if(c2==si||slots[c2].state>=2) continue;
                        float dd=hypotf(slots[c2].x-s->x,slots[c2].z-s->z);
                        if(dd<bd){ bd=dd; best=c2; } }
                    if(best>=0){ s->mate=best; s->state=2; s->tT=12+bd/1.1f; s->moving=1; }
                    else { s->state=0; s->moving=0; s->tT=3+frnd()*5; } }
                else if(roll<0.86f){ s->state=1; s->moving=1; s->tT=25+frnd()*25;
                    /* PATHFINDING: sample 4 destinations, walk to the least crowded one */
                    float bestX=0,bestZ=0,bestScore=1e9f;
                    for(int c3=0;c3<4;c3++){ float cx3=(frnd()*2-1)*(FLOOR_X-1.f), cz3=-(0.5f+frnd()*(-FLOOR_ZN-2.f));
                        float sc3=0;
                        for(int j2=0;j2<NS;j2++){ if(j2==si) continue;
                            float dd=hypotf(slots[j2].x-cx3,slots[j2].z-cz3); if(dd<3.0f) sc3+=(3.0f-dd); }
                        { float dd=hypotf(g_rvx-cx3,g_rvz-cz3); if(dd<3.0f) sc3+=(3.0f-dd); }
                        if(sc3<bestScore){ bestScore=sc3; bestX=cx3; bestZ=cz3; } }
                    s->tx=bestX; s->tz=bestZ; s->pgT=0; s->pgD=1e9f; }
                else { s->state=0; s->moving=0; s->tT=1.5f+frnd()*3; } }
            else if(s->state==1&&s->moving&&hypotf(s->tx-s->x,s->tz-s->z)<0.7f){ s->state=0; s->moving=0; s->tT=1.5f+frnd()*3; }
            /* STUCK DETECTION: no real progress toward the target for 2.5s -> abandon the plan
             * gracefully (re-roll next frame) instead of grinding into a clump forever */
            if(s->moving&&(s->state==1||s->state==2)){
                float dcur=hypotf(s->tx-s->x,s->tz-s->z);
                if(s->pgD-dcur>0.25f){ s->pgD=dcur; s->pgT=0; }
                else { s->pgT+=(float)dt;
                    if(s->pgT>2.5f){ s->state=0; s->moving=0; s->tT=0.3f+frnd()*0.8f; s->pgT=0; s->pgD=1e9f;
                        if(s->mate>=0){ Slot*o2=&slots[s->mate]; if(o2->mate==si) o2->mate=-1; s->mate=-1; } } } }
            Slot*mo=(s->state==3&&s->mate>=0)?&slots[s->mate]:NULL;
            float tgH=mo?atan2f(mo->x-s->x,mo->z-s->z):atan2f(s->tx-s->x,s->tz-s->z);
            float dh=fmodf(tgH-s->h+9.42477f,6.28318f)-3.14159f;
            s->h+=dh*fminf(1,(float)dt*(mo?2.5f:1.5f));
            /* GROOVE DRIFT: millers never truly stand still -- they slow-orbit
             * their spot, so the whole floor breathes even between walks */
            float want=s->moving?1.15f*(1.f-0.55f*fminf(1,s->blk)):(s->state==0?0.18f:0.f);   /* DENSITY THROTTLE: ease off through tight spots */
            if(g_slotSpdMul[si]>0.f) want*=g_slotSpdMul[si];   /* game layer: hunters run, squads march */
            s->blk*=fmaxf(0,1.f-(float)dt*3.f);
            if(s->state==0&&!s->moving) s->h+=(float)dt*0.35f*(si&1?1.f:-1.f);
            s->spd+=(want-s->spd)*fminf(1,(float)dt*1.1f);
            s->x+=sinf(s->h)*s->spd*(float)dt; s->z+=cosf(s->h)*s->spd*(float)dt;
            { float dx=s->x-RVX,dz=s->z-RVZ,d2=hypotf(dx,dz); if(d2<2.4f&&d2>0.01f){ s->x+=dx/d2*(2.4f-d2)*fminf(1,(float)dt*3); s->z+=dz/d2*(2.4f-d2)*fminf(1,(float)dt*3); } }
            for(int j=0;j<NS;j++){ if(j==si) continue; Slot*o=&slots[j];
                int pair=(s->mate==j||o->mate==si);
                float rad=pair?1.3f:(packs[s->pack].avoidR+packs[o->pack].avoidR)*0.5f;   /* size-aware personal space (nobody clips into anybody) */
                float dx=s->x-o->x,dz=s->z-o->z,d2=hypotf(dx,dz);
                if(d2<rad&&d2>0.01f){
                    /* yield physics: standers absorb the shove, walkers keep their stride */
                    float wS=(s->moving&&!o->moving)?0.15f:((!s->moving&&o->moving)?1.7f:1.0f);
                    s->x+=dx/d2*(rad-d2)*wS*fminf(1,(float)dt*5); s->z+=dz/d2*(rad-d2)*wS*fminf(1,(float)dt*5); }
                /* ANTICIPATORY STEERING (smooth redirection, as a person does it): a walker seeing
                 * someone in the lane ahead carves around them BEFORE contact */
                { float lane=1.2f+packs[o->pack].avoidR;   /* bigger bodies get carved around earlier */
                  if(s->moving&&d2<lane&&d2>0.01f){
                    float fx=sinf(s->h),fz=cosf(s->h);
                    float ahead=(-dx*fx-dz*fz)/d2;               /* how squarely they're in front */
                    if(ahead>0.5f){ float side=(-dx*fz+dz*fx)>0?-1.f:1.f;
                        s->h+=side*ahead*(1.f-d2/lane)*(float)dt*3.2f;
                        if(s->blk<ahead*(1.f-d2/lane)) s->blk=ahead*(1.f-d2/lane); } } } }
            /* she's on the floor with them now -- carve around reva too */
            { float dx=s->x-g_rvx,dz=s->z-g_rvz,d2=hypotf(dx,dz);
              if(s->moving&&d2<3.2f&&d2>0.01f){ float fx=sinf(s->h),fz=cosf(s->h);
                  float ahead=(-dx*fx-dz*fz)/d2;
                  if(ahead>0.5f){ float side=(-dx*fz+dz*fx)>0?-1.f:1.f;
                      s->h+=side*ahead*(1.f-d2/3.2f)*(float)dt*3.6f; } } }
            if(s->x<-FLOOR_X)s->x=-FLOOR_X;
            if(s->x>FLOOR_X)s->x=FLOOR_X;
            if(s->z<FLOOR_ZN)s->z=FLOOR_ZN;
            if(s->z>FLOOR_ZF)s->z=FLOOR_ZF;
            s->rotY=s->h;
        }
        passMark("steer");
        /* ⛔ THE CROWD WAS HOLES (11-02). drawSet paints the sky with depth OFF over the whole frame, and the crowd draws BEFORE
         * drawSet: every body's colour was wiped and its depth kept, so the ground behind it then failed the depth test and the
         * sky's own ground band showed through, in the shape of a person. No tint, texture, sun or shadow could touch it, and on
         * every door that has ever played on a set the crowd was flat dark cut-outs. In a door the sky now goes down first and
         * drawSet stands down for the frame. The lab shows no crowd and keeps its order. REVA_SKYLATE=1 is the control. */
        { static int late9=-1; if(late9<0){ const char*e9=getenv("REVA_SKYLATE"); late9=(e9&&e9[0]=='1'); }
          if(!late9&&g_mode!=MODE_ANIM&&(g_setNI||g_nPipe||g_setSpace)){ memcpy(g_watVP,vp,64); } }
        passMark("sky");
        /* STARFIELD (behind everything, twinkling on its own phases) */
        if(g_mode!=MODE_ANIM&&SET_NONE&&!g_noDraw&&!WORLD_OFF) gfxSkyPoints(starVbo,NULL,NSTAR+NNEB,1);
        /* the disco floor, under everyone's feet -- STANDARD blend (lit tiles must be
         * SOLID; the old additive func could never hide a star no matter the alpha) */
        { float fp4[12],fc4[12];   /* the floor pools under the 4 strongest lights near reva */
          int pk4[4]={-1,-1,-1,-1}; float pv4[4]={0,0,0,0};
          for(int li2=0;li2<nLite;li2++){ float dx5=liteP[li2][0]-g_rvx, dz5=liteP[li2][2]-g_rvz;
              float s5=(liteC[li2][0]+liteC[li2][1]+liteC[li2][2])/(1.f+(dx5*dx5+dz5*dz5)*0.004f);
              for(int k4=0;k4<4;k4++) if(s5>pv4[k4]){ for(int m4=3;m4>k4;m4--){ pv4[m4]=pv4[m4-1]; pk4[m4]=pk4[m4-1]; } pv4[k4]=s5; pk4[k4]=li2; break; } }
          for(int k4=0;k4<4;k4++){ int li2=pk4[k4];
              if(li2<0){ fp4[k4*3]=0; fp4[k4*3+1]=0; fp4[k4*3+2]=999; fc4[k4*3]=fc4[k4*3+1]=fc4[k4*3+2]=0; }
              else { memcpy(fp4+k4*3,liteP[li2],12); memcpy(fc4+k4*3,liteC[li2],12); } }
          if(!g_skate&&g_mode!=MODE_ANIM&&SET_NONE&&!g_noDraw) venueFloor(vp,g_camEye,(float)btF0,fp4,fc4);
#ifdef REVA_VK
          if(!g_skate&&g_mode!=MODE_ANIM&&SET_NONE&&!g_noDraw&&!WORLD_OFF){ const float r9[4]={-FLOOR_X,FLOOR_X,FLOOR_ZN,0.5f}; gfxFloorShadow(shTex,g_shvp,r9); }
#endif
        }
        /* BLOB SHADOWS (one streamed draw, every dancer grounded;
         * real shadow-maps = a second full-scene pass, parked -- see the reva-caster idea) */
        { static float shv[(1024+1)*36]; int nq=0;
          /* 09-01 THE FIELD HAD SMUDGES. The regression's "plain field" tiles carried grey
           * ellipses at the same spots in every frame: the crowd's blob shadows. The lab
           * and a loaded set never draw the crowd, but this loop ran in every mode and
           * grounded 400 invisible dancers. Same gate as the dance floor: no crowd on a
           * clean plate or a set. The hero's own blob (below) stays -- she is there. */
          for(int si=0;(g_mode!=MODE_ANIM&&SET_NONE)&&si<NS;si++){ Slot*sl2=&slots[si];
              Pack*P2=&packs[sl2->pack]; float r2=fminf(3.0f,fmaxf(0.9f,P2->avoidR*0.55f));
              if(nq>=1024) break;
              float shfa=0.42f/(1.f+g_slotAlt[si]*0.45f);   /* sky layer: shadow softens with altitude */
              float cx4=sl2->x, cz4=sl2->z; float*v=shv+nq*36; nq++;
              float qq[6][2]={{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
              for(int k3=0;k3<6;k3++){ v[k3*6]=cx4+qq[k3][0]*r2; v[k3*6+1]=0.015f; v[k3*6+2]=cz4+qq[k3][1]*r2;
                  v[k3*6+3]=qq[k3][0]; v[k3*6+4]=qq[k3][1]; v[k3*6+5]=shfa; } }
          if(hasReva){ float r2=1.35f, fa=0.42f/(1.f+g_rvAlt*0.8f);
              float*v=shv+nq*36; nq++;
              float qq[6][2]={{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
              for(int k3=0;k3<6;k3++){ v[k3*6]=g_rvx+qq[k3][0]*r2; v[k3*6+1]=0.015f+g_feet.y; v[k3*6+2]=g_rvz+qq[k3][1]*r2;
                  v[k3*6+3]=qq[k3][0]; v[k3*6+4]=qq[k3][1]; v[k3*6+5]=fa; } }
           gfxBufferData(shadowVbo, nq*36*4, shv, 1);
          if(!g_noDraw) venueBlobs(shv,nq);
           }
        /* LAVA LAMPS: warm blobs rising in glass columns at the floor
         * edges -- streamed points through the sky points' big-soft-nebula branch.
         * A SCENE TAKES THE SET DOWN. The dance floor's dressing -- lamps, orbs, the flame
         * snake, the arcs -- floats at head height and crosses both faces; against a
         * conversation it reads as debris. Two lit bodies in black is the stage this
         * wants, and it is the dark stage the choreography doc is already asking for. */
        /* ⭐ ...AND A SET DOES TOO. Two soft orbs hung over the road in every frame of
         * the first day sheet. Not the sky-layer flyers -- REVA_NOSKY left them exactly
         * where they were -- but the dance floor's own lamp sprites, light orbs and flame snake,
         * which a scene already switches off because a conversation wants a dark stage.
         * A street wants it for the same reason. */
        if(g_scnT<0.f&&SET_NONE&&!g_setSpace){
        { static float lv[(48+NORB*2)*5]; int np=0;
          for(int li=0;li<8&&np<48;li++){
              float lx=(li<4)?(-FLOOR_X*0.94f+li*(FLOOR_X*2*0.94f/3.f)):(FLOOR_X*0.94f-(li-4)*(FLOOR_X*2*0.94f/3.f));
              float lz=(li<4)?(FLOOR_ZN*0.97f):( -2.0f);
              for(int b3=0;b3<5&&np<48;b3++){ float ph3=fmodf((float)tsec*0.10f+li*0.37f+b3*0.23f,1.f);
                  float*v=lv+np*5; np++;
                  v[0]=lx+sinf((float)tsec*0.7f+b3*2.1f+li)*0.5f; v[1]=0.7f+ph3*4.6f;
                  v[2]=lz+cosf((float)tsec*0.6f+b3*1.7f+li)*0.5f;
                  v[3]=0.15f+0.9f*((li*5+b3)%7)/7.f;   /* warm hue walk */
                  v[4]=5.2f+((b3*13+li)%10)*0.14f; } }
          /* the ORBS themselves: soft halo + bright core, the same sky points */
          for(int i2=0;i2<NORB;i2++){ float*v=lv+np*5; np++;
              memcpy(v,orbP[i2],12); v[3]=(float)tsec*0.25f+i2*0.61f; v[4]=5.6f;
              v=lv+np*5; np++;
              memcpy(v,orbP[i2],12); v[3]=(float)tsec*0.25f+i2*0.61f; v[4]=3.0f; }
           gfxBufferData(lampVbo, np*20, lv, 1);
          if(!g_noDraw&&!WORLD_OFF) gfxSkyPoints(0,lv,np,1);
           }
        /* THE FLAME SNAKE:
         * a thin fire ribbon weaving a lissajous through the crowd, head white-hot, tail ember */
        {
          #define SNK 200
          static float sv[SNK*2*6];
          float ax=(float)tsec*0.50f; float px0=0,py0=0,pz0=0;
          for(int i2=0;i2<SNK;i2++){ float t4=ax-i2*0.052f;
              float x4=sinf(t4*0.61f+1.7f*sinf(t4*0.213f))*(FLOOR_X*0.55f);
              float z4=FLOOR_ZN*0.5f+sinf(t4*0.83f+1.3f+1.1f*sinf(t4*0.171f))*(-FLOOR_ZN*0.40f);
              float y4=0.35f+1.9f*(0.5f+0.5f*sinf(t4*1.31f+sinf(t4*0.37f)));
              float dx4=(i2==0)?0.f:(px0-x4), dy4=(i2==0)?0.f:(py0-y4), dz4=(i2==0)?0.f:(pz0-z4);
              float dl=sqrtf(dx4*dx4+dy4*dy4+dz4*dz4); if(dl<1e-5f){ dx4=1; dl=1; }
              float sxx=-dz4/dl, szz=dx4/dl;   /* side = up x dir */
              float u4=(float)i2/SNK; float w4=0.30f*(1.f-u4)+0.035f;
              float flick=0.72f+0.28f*sinf((float)tsec*12.f+i2*0.9f);
              float r4=1.0f*flick, gg4=(0.90f*powf(1.f-u4,1.25f)+0.06f)*flick, b4=0.30f*powf(1.f-u4,3.f)*flick;
              float*v=sv+i2*12;
              v[0]=x4+sxx*w4; v[1]=y4; v[2]=z4+szz*w4; v[3]=r4; v[4]=gg4; v[5]=b4;
              v[6]=x4-sxx*w4; v[7]=y4; v[8]=z4-szz*w4; v[9]=r4; v[10]=gg4; v[11]=b4;
              px0=x4; py0=y4; pz0=z4; }
           gfxBufferData(snakeVbo, sizeof sv, sv, 1);
          if(!g_noDraw) venueStrip(sv,SNK*2,0.95f);
          /* ELECTRIC ARCS: jittered crackle around each orb, new randoms every frame */
          { static float av[NORB*8*2*6]; int nv3=0;
            for(int i2=0;i2<NORB;i2++) for(int a3=0;a3<8;a3++){
                float*v=av+nv3*6;
                v[0]=orbP[i2][0]+(frnd()-0.5f)*0.9f; v[1]=orbP[i2][1]+(frnd()-0.5f)*0.9f; v[2]=orbP[i2][2]+(frnd()-0.5f)*0.9f;
                v[3]=orbC[i2][0]*1.3f; v[4]=orbC[i2][1]*1.3f; v[5]=orbC[i2][2]*1.5f; nv3++;
                v=av+nv3*6;
                v[0]=orbP[i2][0]+(frnd()-0.5f)*1.6f; v[1]=orbP[i2][1]+(frnd()-0.5f)*1.6f; v[2]=orbP[i2][2]+(frnd()-0.5f)*1.6f;
                v[3]=orbC[i2][0]*0.5f; v[4]=orbC[i2][1]*0.5f; v[5]=orbC[i2][2]*0.7f; nv3++; }
            gfxBufferData(snakeVbo, nv3*24, av, 1);
            if(!g_noDraw) gfxLines3D(av,nv3);
             } }
        }

        /* FPS LEVERS (07-22, tuned LIVE on the CRT): frustum-cull each slot (sphere vs vp planes),
         * then draw front-to-back so early-Z eats the occluded crowd. Culled slots skip anim too. */
        int nVis=0;
        { float pl[6][4];
          for(int r2=0;r2<4;r2++){ /* row extraction: left/right/bottom/top/near/far from column-major vp */
              pl[0][r2]=vp[r2*4+3]+vp[r2*4+0]; pl[1][r2]=vp[r2*4+3]-vp[r2*4+0];
              pl[2][r2]=vp[r2*4+3]+vp[r2*4+1]; pl[3][r2]=vp[r2*4+3]-vp[r2*4+1];
              pl[4][r2]=vp[r2*4+3]+vp[r2*4+2]; pl[5][r2]=vp[r2*4+3]-vp[r2*4+2]; }
          /* ⭐ (11-07) TOO SMALL TO SEE (E->tune->farPx, for the Pi's performance): a body shorter than that many of the picture's lines isn't drawn, posed or
           * shadowed. The street game's 84 people were drawn from half a kilometre off, each under a line tall, and they were 72% of the Pi's
           * main pass. Its height in lines is its height over its depth times the lens's own scale (the length of the projection's y row) and half the picture's lines.
           * One that was cut comes back a quarter bigger than the cut, so a body at the edge doesn't flicker. One picture only: split screen draws them all */
          static unsigned char*farOut=NULL; static int farOutN=0;
          if(farOutN!=NS){ free(farOut); farOut=(unsigned char*)calloc((size_t)(NS>0?NS:1),1); farOutN=NS; }
          const float farPx9=(GAME_ON&&g_tune.farPx>0.f&&g_gameViews.n<=1&&farOut)?g_tune.farPx:0.f;
          const float ky9=sqrtf(vp[1]*vp[1]+vp[5]*vp[5]+vp[9]*vp[9])*0.5f*(float)H;   /* a unit's lines at a depth of one */
          int far9=0;
          if(farPx9>0.f&&!g_farOn){ g_farOn=1; const Pack*P0=NS>0?&packs[slots[0].pack]:NULL;
              printf("[bodies] a body under %.1f of the picture's %d lines isn't drawn (E->tune->farPx); body 0 is %.2f units tall\n",farPx9,H,P0?P0->effH*P0->scale:0.f); fflush(stdout); }
          for(int si=0;si<NS;si++){ Slot*sl=&slots[si];
              float cx3=sl->x, cy3=1.6f+g_slotFeet[si], cz3=sl->z, rad=2.4f; int in=1;
              if(GAME_ON&&si<1024&&g_bodyPlaceOn[si]){ cx3=g_bodyPlace[si][0]; cy3=g_bodyPlace[si][1]; cz3=g_bodyPlace[si][2]; rad=3.4f; }   /* (11-12) a placed body (E->bodyPlace) is culled where it's drawn: a skater up on a plaza was culled at the floor's height */
              for(int p2=0;p2<6&&in&&g_gameViews.n<=1;p2++){ float l2=sqrtf(pl[p2][0]*pl[p2][0]+pl[p2][1]*pl[p2][1]+pl[p2][2]*pl[p2][2]);
                  if(pl[p2][0]*cx3+pl[p2][1]*cy3+pl[p2][2]*cz3+pl[p2][3]<-rad*l2) in=0; }
              if(!in) continue;
              /* camera-space depth ~ clip w = row3 . pos */
              const float w9=vp[3]*cx3+vp[7]*cy3+vp[11]*cz3+vp[15];
              if(farPx9>0.f){ const Pack*P9=&packs[sl->pack]; float h9=P9->effH*P9->scale; if(!(h9>0.3f)) h9=3.2f;
                  const float px9=w9>0.01f?h9*ky9/w9:1e9f;
                  if(px9<(farOut[si]?farPx9*1.25f:farPx9)){ farOut[si]=1; far9++; continue; }
                  farOut[si]=0; }
              depth[nVis]=w9;
              order[nVis++]=si; }
          if(!g_noDraw) g_farAcc+=far9;
          for(int a2=1;a2<nVis;a2++){ int o2=order[a2]; float d2=depth[a2]; int b2=a2-1;   /* insertion sort: nearly-sorted across frames */
              while(b2>=0&&depth[b2]>d2){ order[b2+1]=order[b2]; depth[b2+1]=depth[b2]; b2--; }
              order[b2+1]=o2; depth[b2+1]=d2; } }
        /* ⛔ THE CAP IS BY RANK, AND RANK IS UNSTABLE -- which is why people
         * popped in and out of existence. With 400 dancers forty are dropped each frame,
         * and WHICH forty comes from a front-to-back sort that shifts as the camera moves,
         * so a body sitting near rank 360 crosses the line back and forth between frames and
         * strobes. A hard cut applied to a jittering number.
         * ⭐ HYSTERESIS, FOR NOTHING. Drawn slots keep being drawn until they fall past the
         * upper band; undrawn slots stay out until they climb inside the lower one. A body
         * at the boundary picks a side and holds it. Same worst case, same FPS lever.
         * ⛔ The cap itself stays -- it is a real lever, tuned live on the CRT for the
         * 1000-road, and dropping the most distant bodies is the right trade. The bug was
         * never the cap. */
        /* ⛔ NS IS argv[1], not a constant -- the dancer count is chosen at launch. A
         * heap array sized once on the first frame, because this must persist ACROSS frames:
         * remembering what was drawn last frame is the entire mechanism. */
        { static unsigned char*shown=NULL; static int shownN=0;
          if(shownN!=NS){ free(shown); shown=(unsigned char*)calloc((size_t)(NS>0?NS:1),1); shownN=NS; }
          if(!shown) goto nohyst;
          const int HI=(GAME_ON&&g_tune.bodyCap>0)?g_tune.bodyCap:390, LO=HI-60;      /* enter inside LO, leave only past HI */
          int keep=0;
          for(int a3=0;a3<nVis;a3++){
              int si3=order[a3];
              int in3 = shown[si3] ? (a3<HI) : (a3<LO);
              shown[si3]=(unsigned char)in3;
              if(in3) order[keep++]=si3; }
          /* a slot the frustum rejected never reaches this loop, so it keeps its last
           * state and re-enters on the generous band -- which is what you want when the
           * camera swings back onto a crowd it was already drawing. */
          nVis=keep; }
        nohyst:
        if(nVis>NS) nVis=NS;
        if(!g_noDraw){ g_visAcc+=nVis; g_visN++; if(nVis>g_visMax) g_visMax=nVis; }
        double btF=btF0;
        passMark("venue");
        poseTick(); int poseR9=poseRate(),poseN9=poseNear();
        const int lite9On=(GAME_ON&&g_tune.bodyLite&&g_gameViews.n<=1)?1:0; int liteN9=0;   /* (11-07) light copies: a body's height in lines is its height over its depth times this */
        const float kyL9=sqrtf(vp[1]*vp[1]+vp[5]*vp[5]+vp[9]*vp[9])*0.5f*(float)H;
        static signed char liteWas9[1024]; static int liteInit9=0; if(!liteInit9){ memset(liteWas9,-1,sizeof liteWas9); liteInit9=1; }
        static int liteDbg9=-1; if(liteDbg9<0){ const char*e9=getenv("REVA_LODDBG"); liteDbg9=(e9&&e9[0]!='0')?1:0; }
        for(int oi=0;oi<nVis;oi++){ int si=order[oi]; Slot*sl=&slots[si]; Pack*P=&packs[sl->pack];
            g_vkOwner=((unsigned)GFX_KIND_CROWD<<28)|(((unsigned)si&0xffffu)<<12);
            bodyRest(P,T,Q,S);
            /* THE SPOTLIGHT PINS ITS MODEL (the stuck loop's root cause was the featured npc
             * walking away mid-showcase): wander target snaps home, speed damps -> they dance
             * in place with reva until the director moves on */
            if(si==CAMD.tgt&&CAMD.shot==9){ sl->tx=sl->x; sl->tz=sl->z;
                sl->spd*=1.f-fminf(1.f,(float)dt*3.f);
                /* FACE THE LENS (spotlights kept showing rears): turn to the camera */
                { float fy=atan2f(g_lastE[0]-sl->x,g_lastE[2]-sl->z);
                  float dhf=fmodf(fy-sl->rotY+9.42477f,6.28318f)-3.14159f;
                  sl->rotY+=dhf*fminf(1,(float)dt*2.2f); sl->h=sl->rotY; } }
            int canDonorLoco=(P->pbOK>=10);
            int zDead=(GAME_ON&&g_game->bodyLoco(si,sl->spd)&&g_zombP&&canDonorLoco);   /* M0: the zombie cab's dead */
            int walking=(sl->spd>0.45f&&(P->walkIdx>=0||P->runIdx>=0||(canDonorLoco&&g_dnNWalks>0)||zDead));
            int fastLoco=(g_slotBhv[si]==BHV_RUN||sl->spd>2.2f);   /* RUN is a speed, not just a behavior; sl->spd is low-passed so no flap */
            /* S4a THE BEHAVIOR CHANNEL, anim side: a fighting or dying slot plays its pack's
             * authored role clip once (mined at load); RUN swaps the gait-synced walk for the
             * pack's run cycle; packs without the clip get the procedural fills below. */
            int rides=0;   /* S4.5: a crowd body on a deck gets a rider's body */
            int bhvOnce=-1, bhvDnClip=-1, bhvDone=0; float bhvU=0.f; Pack*bhvDnP=NULL;
            int stKey=0;   /* S4g: which variant of a state is playing, so the crossfade sees the change */
            if(g_slotBhv[si]==BHV_ATK){ bhvU=g_slotBhvT[si]/0.65f;
                if(P->atkIdx>=0) bhvOnce=P->atkIdx;
                else if(g_dnGunP&&P->pbOK>=10&&g_slotWpn[si]>=WPN_PISTOL){ bhvDnP=g_dnGunP; bhvDnClip=g_dnGun; } }
            else if(g_slotBhv[si]==BHV_DOWN){ bhvU=g_slotBhvT[si]/0.95f;
                if(P->downIdx>=0) bhvOnce=P->downIdx;
                else if(g_dnDownP&&P->pbOK>=10){ bhvDnP=g_dnDownP; bhvDnClip=g_dnDown; } }
            if(bhvU>1.f) bhvU=1.f;
            int sh9=poseR9>0&&oi>=poseN9&&!rides&&!(g_slotBhv[si]==BHV_ATK&&bhvOnce<0&&bhvDnClip<0)&&!(g_rdGameOn&&si<1024&&g_bodyRd[si])&&!(g_faceN&&si<1024&&g_faceOf[si]); PoseRow*pr9=NULL;   /* a strike with no clip is drawn on its own pose below */
            Pack*agP9=NULL; int agC9=-1,agO9=0,agK9=-1,agL9=0; double agF9=0.0;
            if(g_agBodyEver&&agBodyPose(si,sl->pack,&agP9,&agC9,&agO9,&agF9,&agK9,&agL9)){ bhvDone=1; PClip*c=&agP9->clips[agC9]; POSE_CLIP(agP9,agC9,c,agF9,agO9); }   /* it has a graph of its own: that says what it plays */
            else if(bhvOnce>=0){ bhvDone=1; PClip*c=&P->clips[bhvOnce];   /* play ONCE, hold the last frame */
                /* P1.5: fractional phase; bhvU*(frames-1) never reaches the wrap, so a
                 * one-shot blends between real neighbours and holds its true last frame */
                POSE_CLIP(P,bhvOnce,c,(double)bhvU*(double)(c->frames-1),1); }
            else if(bhvDnClip>=0){ bhvDone=1;   /* S4b: a donor clip on this rig, rest-delta transfer */
                PClip*c=&bhvDnP->clips[bhvDnClip];
                POSE_CLIP(bhvDnP,bhvDnClip,c,(double)bhvU*(double)(c->frames-1),0); }
            else if(walking){
                /* S4e LOCOMOTION LADDER: idle-walk-run is a SPEED ladder for every canon rig.
                 * Own clips first; a pack with neither strides on the co-star's through the donor
                 * bank -- the 116 walkless packs used to SLIDE across the floor. */
                Pack*sp=P; int ci=-1;
                if(zDead&&(ci=zombLoco(si,sl->spd,P))>=0) sp=g_zombP;   /* M0: the dead walk the Z4 bank, not the crowd's gait */
                else if(g_slotFleeT[si]>0.f&&canDonorLoco&&g_dnFleeP){   /* S4f: the panic layer's AUTHORED body -- the scared walk */
                    sp=g_dnFleeP; ci=((si&1)&&g_dnFlee2>=0)?g_dnFlee2:(g_dnFlee>=0?g_dnFlee:g_dnFlee2); }
                else if(fastLoco&&P->runIdx>=0) ci=P->runIdx;
                else if(fastLoco&&canDonorLoco&&g_dnRunP&&donorOK(g_dnRunP,g_dnRun,P)){ sp=g_dnRunP; ci=g_dnRun; }
                else if(P->walkIdx>=0) ci=P->walkIdx;
                else if(P->runIdx>=0) ci=P->runIdx;   /* run-only packs stride with what they have */
                else if(canDonorLoco&&g_dnNWalks>0){ int wi9=si%g_dnNWalks;   /* variety: the crowd doesn't march in lockstep */
                    if(donorOK(g_dnWalksP[wi9],g_dnWalks[wi9],P)){ sp=g_dnWalksP[wi9]; ci=g_dnWalks[wi9]; } }
                if(ci>=0){ PClip*c=&sp->clips[ci];
                    /* GAIT SYNC (against moonwalking): clip playback rides the slot's actual speed --
                     * at cruise 1.15 u/s the cycle runs ~0.7x, feet plant instead of sliding */
                    sl->wph+=(float)dt*c->fps*fminf(2.5f,sl->spd*(zDead?g_game->bodyGait(si):0.62f));
                    double fp2=(double)sl->wph+(double)sl->phase;   /* P1.5: fractional */
                    POSE_CLIP(sp,ci,c,fp2,sp==P); } }
            else if(zDead&&zombIdle(si,P)>=0){   /* M0: a standing dead body stands in the bank's idle, never a dance */
                int zc=zombIdle(si,P); PClip*c=&g_zombP->clips[zc];
                POSE_CLIP(g_zombP,zc,c,tsec*(double)c->fps+(double)sl->phase,0); }
            else if(sl->mode==1&&P->nClips&&P->nDance&&g_mode==MODE_DANCE){   /* MOCAP DANCER (for more variation):
                 * ~30% of the floor performs the real retargeted clips full-out, cycling numbers */
                sl->clipT-=(float)dt;
                if(sl->clipT<=0){ sl->clip=rand()%P->nDance; sl->clipT=14.f+frnd()*22.f; }
                PClip*c=&P->clips[P->danceIdx[sl->clip%P->nDance]];
                POSE_CLIP(P,P->danceIdx[sl->clip%P->nDance],c,tsec*(double)c->fps+(double)sl->phase,1); }   /* P1.5: fractional */
            else if(g_mode!=MODE_DANCE&&!walking&&g_slotBhv[si]==BHV_NONE&&sl->state==3&&g_dnNConvo>0&&P->pbOK>=10){
                /* STATE TREE CHAT (S4g): the social sim already pairs people off and turns them
                 * to face each other -- give that a body. Conversation loops from the MOB1 bank;
                 * the pair takes different loops so nobody mirrors their partner. */
                int ci9=(si+(sl->mate>si?0:1))%g_dnNConvo; stKey=100+ci9;
                PClip*c=&g_dnConvoP[ci9]->clips[g_dnConvo[ci9]];
                POSE_CLIP(g_dnConvoP[ci9],g_dnConvo[ci9],c,tsec*(double)c->fps+(double)sl->phase,0); }   /* P1.5 */
            else if(g_mode!=MODE_DANCE&&!walking&&g_slotBhv[si]==BHV_NONE&&(P->idleIdx>=0||(g_dnNIdles>0&&P->pbOK>=10))){
                /* STATE TREE IDLE (S4d/g): a clocked-out slot plays an authored idle -- its own,
                 * or one of the bank's idle+fidget set -- never a frozen mid-move frame. The
                 * set rotates on a slow, per-slot-staggered clock so a waiting crowd drifts
                 * between weight shifts instead of breathing in unison. */
                if(P->idleIdx>=0){ PClip*c=&P->clips[P->idleIdx];
                    unsigned nf9=c->frames?c->frames:1;   /* PING-PONG: the loop seam popped every cycle (posecheck f99/f507) */
                    /* P1.5 fractional ping-pong: reflect, then hold the last frame across the
                     * reversal (the clamp) so the top of the bounce never wraps to frame 0 */
                    double tt9=fmod(tsec*(double)c->fps*0.9+(double)sl->phase,(double)(2*nf9));
                    double fp2=tt9<(double)nf9?tt9:(2.0*nf9-tt9);
                    if(fp2>(double)nf9-1.0) fp2=(double)nf9-1.0;
                    POSE_CLIP(P,P->idleIdx,c,fp2,1); }
                else { int ii9=(si+(int)(tsec/19.0))%g_dnNIdles; stKey=10+ii9;
                    PClip*c=&g_dnIdlesP[ii9]->clips[g_dnIdles[ii9]];
                    unsigned nf9=c->frames?c->frames:1;
                    double tt9=fmod(tsec*(double)c->fps*0.9+(double)sl->phase,(double)(2*nf9));
                    double fp2=tt9<(double)nf9?tt9:(2.0*nf9-tt9);
                    if(fp2>(double)nf9-1.0) fp2=(double)nf9-1.0;
                    POSE_CLIP(g_dnIdlesP[ii9],g_dnIdles[ii9],c,fp2,0); } }
            else if(P->pbOK>=10){   /* procedural (cast expansion: nClips no longer required -- clipless rigs dance from REST stance; stance-freeze blocks below guard on nClips) */
                /* PROCEDURAL DANCER (every one independent): frozen real-clip stance + this
                 * dancer's genome of beat-locked oscillators. Move re-rolls blend stances. */
                sl->moveT-=(float)dt*(g_bpm/60.f);
                if(P->nClips&&sl->moveT<=0&&sl->blend<=0){ sl->nextClip=P->nDance?P->danceIdx[rand()%P->nDance]:0;
                    sl->nextFrame=(unsigned)rand()%P->clips[sl->nextClip].frames;
                    sl->blend=0.0001f; rollGenome(sl,P->nClips,NULL,1); }
                if(P->nClips){ PClip*c=&P->clips[sl->baseClip];
                unsigned f2=sl->baseFrame%c->frames;
                unsigned char*tr=P->pk+c->offTracks;
                for(unsigned t=0;t<c->nTracks;t++){ unsigned node=*(unsigned*)(tr+t*12), type=*(unsigned*)(tr+t*12+4), offD=*(unsigned*)(tr+t*12+8);
                    float*d=(float*)(P->pk+offD); int comp=type==0?4:3;
                    if(type>=3) continue; else if(type==0) memcpy(Q[node],d+f2*comp,16);
                    else if(type==1) memcpy(T[node],d+f2*comp,12);
                    else memcpy(S[node],d+f2*comp,12); } }
                if(P->nClips&&sl->blend>0){   /* stance transition: nlerp toward the next frozen frame */
                    for(unsigned i=0;i<P->nNodes;i++){ memcpy(T2[i],P->nodes[i].t,12); memcpy(Q2[i],P->nodes[i].q,16); memcpy(S2[i],P->nodes[i].s,12); }
                    PClip*c2=&P->clips[sl->nextClip]; unsigned f3=sl->nextFrame%c2->frames;
                    unsigned char*tr2=P->pk+c2->offTracks;
                    for(unsigned t=0;t<c2->nTracks;t++){ unsigned node=*(unsigned*)(tr2+t*12), type=*(unsigned*)(tr2+t*12+4), offD=*(unsigned*)(tr2+t*12+8);
                        float*d=(float*)(P->pk+offD); int comp=type==0?4:3;
                        if(type==0) memcpy(Q2[node],d+f3*comp,16);
                        else if(type==1) memcpy(T2[node],d+f3*comp,12);
                        else memcpy(S2[node],d+f3*comp,12); }
                    float bl=sl->blend; float e2=bl*bl*(3-2*bl);
                    for(unsigned i=0;i<P->nNodes;i++){ qnlerp(Q[i],Q[i],Q2[i],e2);
                        for(int k2=0;k2<3;k2++) T[i][k2]=T[i][k2]*(1-e2)+T2[i][k2]*e2; }
                    sl->blend+=(float)dt*2.2f;
                    if(sl->blend>=1){ sl->baseClip=sl->nextClip; sl->baseFrame=sl->nextFrame; sl->blend=0; } }
                /* HEAD SANITY (heads were thrown back and to the left, at extreme angles):
                 * stance frames freeze mid-head-whip poses -- pull neck+head 55% back toward rest */
                for(int k2=4;k2<=5;k2++){ int b2=P->pb[k2]; if(b2>=0) qnlerp(Q[b2],Q[b2],P->nodes[b2].q,0.55f); }
                /* the genome: hips sway, spine counter-rotation (the calibration lesson), head bob,
                 * independent arm figures (arms do NOT mirror), knee pulse -- all on the LIVE beat */
                double bt2=btF*sl->gSub+sl->gPh; float en2=(sl->state==3)?0.45f:1.0f;   /* chatters groove gently */
                if(g_mode!=MODE_DANCE) en2*=0.12f;   /* 08-25: dancing is the SHOWCASE's job -- elsewhere the crowd just breathes */
                { float gg=fminf(1.f,3.6f/P->effH); en2*=gg*gg; }   /* giantess grace SQUARED (round 2: underwear still visible) -- a 2x body gets 1/4 the radians; skirts stay draped */
                float off[4],tmpq[4];
                float hipY=sinf((float)(bt2*3.14159))*sl->gHip*en2;
                if(P->pb[0]>=0){ qaxis(off,0,1,0,hipY); qmul(Q[P->pb[0]],Q[P->pb[0]],off); }
                float tap[3]={0.30f,0.35f,0.35f};
                for(int k2=0;k2<3;k2++){ int b2=P->pb[1+k2]; if(b2<0) continue;
                    qaxis(off,0,1,0,-hipY*sl->gSpine*tap[k2]);
                    qaxis(tmpq,1,0,0,sinf((float)(bt2*1.5708))*0.05f*en2);
                    qmul(off,off,tmpq); qmul(Q[b2],Q[b2],off); }
                if(P->pb[5]>=0){ qaxis(off,1,0,0,sinf((float)(bt2*6.2832+0.4))*sl->gHead*en2); qmul(Q[P->pb[5]],Q[P->pb[5]],off); }
                for(int side=0;side<2;side++){ int up=P->pb[side?10:7], fo=P->pb[side?11:8];
                    float ph2=sl->gArmPh[side]; float a0=sl->gArmA[side*2]*en2, a1=sl->gArmA[side*2+1]*en2;
                    if(up>=0){ qaxis(off,0,0,1,sinf((float)(btF*sl->gArmSub*3.14159)+ph2)*a0*(side?-1.f:1.f));
                        qaxis(tmpq,1,0,0,cosf((float)(btF*sl->gArmSub*1.5708)+ph2)*a1*0.7f);
                        qmul(off,off,tmpq); qmul(Q[up],Q[up],off); }
                    if(fo>=0){ qaxis(off,1,0,0,(0.5f+0.5f*sinf((float)(btF*6.2832)+ph2))*a1*0.8f); qmul(Q[fo],Q[fo],off); } }
                for(int side=0;side<2;side++){ int kn=P->pb[side?15:13]; if(kn<0) continue;
                    qaxis(off,1,0,0,(0.5f+0.5f*sinf((float)(btF*3.14159)+(side?3.14f:0.f)))*sl->gKnee*en2);
                    qmul(Q[kn],Q[kn],off); } }
            else if(P->nClips){ /* fallback rigs (a body with only a few clips still dances): DANCE clips while stationary, walk only when walking */
                PClip*c=&P->clips[(P->nDance&&!walking&&g_mode==MODE_DANCE)?P->danceIdx[sl->clip%P->nDance]:sl->clip];
                unsigned f2=(g_mode!=MODE_DANCE&&sl->spd<=0.45f)?((unsigned)sl->phase%c->frames):((unsigned)(tsec*c->fps)+sl->phase)%c->frames;   /* off the clock: STILL holds a stance; a MOVING creature keeps its own motion */
                unsigned char*tr=P->pk+c->offTracks;
                for(unsigned t=0;t<c->nTracks;t++){ unsigned node=*(unsigned*)(tr+t*12), type=*(unsigned*)(tr+t*12+4), offD=*(unsigned*)(tr+t*12+8);
                    float*d=(float*)(P->pk+offD); int comp=type==0?4:3;
                    if(type>=3) continue; else if(type==0) memcpy(Q[node],d+f2*comp,16);
                    else if(type==1) memcpy(T[node],d+f2*comp,12);
                    else memcpy(S[node],d+f2*comp,12); } }
            if(rides&&g_slotBhv[si]!=BHV_DOWN&&P->pbOK>0){   /* S4.5: riders RIDE -- loaded knees, arms out, carve lean */
                float off9[4]; float lean9=sinf((float)tsec*2.1f+si*1.7f)*0.12f;
                int kl9=P->pb[13], kr9=P->pb[15];
                if(kl9>=0){ qaxis(off9,1,0,0,0.55f); qmul(Q[kl9],Q[kl9],off9); }
                if(kr9>=0){ qaxis(off9,1,0,0,0.35f); qmul(Q[kr9],Q[kr9],off9); }
                for(int k9=1;k9<4;k9++){ int b9=P->pb[k9]; if(b9<0) continue;
                    qaxis(off9,1,0,0,0.20f); qmul(Q[b9],Q[b9],off9);
                    qaxis(off9,0,0,1,lean9); qmul(Q[b9],Q[b9],off9); }
                for(int sd9=0;sd9<2;sd9++){ int up9=P->pb[sd9?10:7];
                    if(up9>=0){ qaxis(off9,0,0,1,(sd9?-1.f:1.f)*0.5f); qmul(Q[up9],Q[up9],off9); } } }
            if(g_slotBhv[si]==BHV_ATK&&!bhvDone){   /* S4a fill: no clip, no donor -> synthesize the strike */
                float sw9=sinf(bhvU*3.14159f); float off9[4],tq9[4];
                if(P->pb[7]>=0){ qaxis(off9,1,0,0,-1.9f*sw9); qmul(Q[P->pb[7]],Q[P->pb[7]],off9); }
                if(P->pb[8]>=0){ qaxis(off9,1,0,0,-0.7f*(1.f-bhvU)); qmul(Q[P->pb[8]],Q[P->pb[8]],off9); }
                for(int k9=1;k9<4;k9++){ int b9=P->pb[k9]; if(b9<0) continue;
                    qaxis(off9,0,1,0,0.35f*sw9); qaxis(tq9,1,0,0,0.18f*sw9);
                    qmul(off9,off9,tq9); qmul(Q[b9],Q[b9],off9); } }
            if(GAME_ON) rideBodyPose(si,P,Q,T);   /* (11-05) standing on what it rides */
            if(GAME_ON) perfBodyPose(si,P,Q,T);   /* (11-08) playing an instrument: its limbs on their targets (engine/perform.inc) */
            { /* THE STATE TREE'S EDGES (S4d): every clip/state switch crossfades ~0.2s.
               * Slots used to HARD-CUT between walk/dance/attack/fall -- and each cut was a pop.
               * Same trick as the hero's rvXf, stored per slot for the first 128 nodes. */
              static unsigned char xInit[1024]; static int xKey[1024]; static float xF[1024]; static int xPack[1024];
              if(xPack[si]!=sl->pack+1){ xPack[si]=sl->pack+1; xInit[si]=0; }   /* someone else's bones are nothing to blend from */
              static float xQ[1024][128][4], xT[1024][128][3];
              int key9=(agK9>=0)?(6000+agK9):(bhvOnce>=0)?(1000+bhvOnce):(bhvDnClip>=0)?(2000+bhvDnClip)
                       :(fastLoco&&walking)?3500
                       :walking?3000:(sl->mode==1&&g_mode==MODE_DANCE)?4000:(5000+(rides?77:0)+stKey);
              unsigned nn9=P->nNodes; if(nn9>128) nn9=128;
              if(pr9) xInit[si]=0;   /* a shared pose has nothing to blend from, and nothing here may touch it */
              else {
              if(!xInit[si]||xKey[si]!=key9){ xF[si]=xInit[si]?0.f:1.f; xKey[si]=key9; }
              if(xF[si]<1.f){ float e9=xF[si]*xF[si]*(3.f-2.f*xF[si]);
                  for(unsigned i9=0;i9<nn9;i9++){ qnlerp(Q[i9],xQ[si][i9],Q[i9],e9);
                      for(int k9=0;k9<3;k9++) T[i9][k9]=xT[si][i9][k9]*(1.f-e9)+T[i9][k9]*e9; }
                  xF[si]+=(float)dt*5.5f; }
              for(unsigned i9=0;i9<nn9;i9++){ memcpy(xQ[si][i9],Q[i9],16); memcpy(xT[si][i9],T[i9],12); }
              xInit[si]=1; } }
            g_faceCur=(g_faceN&&si<1024&&g_faceOf[si])?&g_faces[g_faceOf[si]-1]:NULL; if(g_faceCur) faceLayer(g_faceCur,P,Q);
            if(g_eyeEver){ if(si<1024) memcpy(g_vkEye,g_slotEye[si],16); else g_vkEye[3]=0.f; }   /* (step 23) this body's eyes glow, if a game said so */
            if(!pr9||!pr9->world) bodyFK(P,T,Q,S,world);
            if(pr9&&!pr9->world&&!poseKeep(pr9,P,world)) pr9=NULL;
            M4*world9=pr9?pr9->world:world; if(pr9) g_poseStood++;
            if(GAME_ON&&si<1024&&g_bodyMirror[si]) world9=poseMirror(P,world9);   /* (11-12) left for right (E->bodyMirror) */
            if(g_faceCur) facePosed(g_faceCur,P,world9);   /* (step 20) where its eyes are now, for a seated body's look and shot */
            /* STATUE RESCUE (rooster/zoo-class: no clips AND unmapped rig): the whole body
             * grooves -- beat bob + yaw sway. The rig is there for future clips; v0 dances anyway. */
            int statue=(P->nClips==0&&P->pbOK<10);
            float ry2=sl->rotY+((statue&&g_mode==MODE_DANCE)?sinf((float)(btF*3.14159)+sl->gPh*20.f)*0.24f:0.f);
            M4 model; float ca2=cosf(ry2),sa=sinf(ry2),sc=P->scale;
            float tpl=(g_slotBhv[si]==BHV_DOWN&&!bhvDone)?fminf(1.f,(g_slotBhvT[si]/0.95f)*1.35f):0.f;   /* S4a fill: no clip, no donor -> the body TOPPLES */
            float cp9=cosf(tpl*1.5f), sp9=sinf(tpl*1.5f);
            memset(model,0,64);
            model[0]=ca2*sc; model[2]=-sa*sc;
            model[4]=sp9*sa*sc; model[5]=cp9*sc; model[6]=sp9*ca2*sc;
            model[8]=cp9*sa*sc; model[9]=-sp9*sc; model[10]=cp9*ca2*sc; model[15]=1;
            /* grounding only (the 10 invisibles: xz-centering DISPLACED off-center rigs from
             * their slots -- the camera flew to empty floor. Vertical fix stays, xz shift dies.) */
            model[12]=sl->x; model[14]=sl->z;
            float bnc=(g_mode!=MODE_DANCE)?0.f:(statue?fmaxf(sl->gBounce,0.07f):sl->gBounce);
            model[13]=-P->mny*sc+((!walking&&bnc>0)?powf(fabsf(sinf((float)(btF*3.14159))),1.5f)*bnc:0);
            if(tpl>0.f) model[13]=model[13]*(1.f-tpl)+0.5f*tpl;   /* the fallen come to rest at floor height */
            else if(g_slotBhv[si]==BHV_DOWN&&bhvDnClip>=0){   /* donor fall is rotation-only: lend it the sink */
                float ds9=fminf(1.f,g_slotBhvT[si]/0.95f); model[13]-=ds9*ds9*0.75f; }
            if(rides) model[13]+=parkH(sl->x,sl->z)+0.12f;   /* riders track the concrete, deck-height up */
            if(g_slotWpn[si]>0&&P->gripNode>=0&&P->pbOK>=10&&!g_slotHide[si])   /* S5a: what this one is carrying (humanoids only) */
                wpnGripPush(model,world9[P->gripNode],g_slotWvar[si],sl->x,sl->z);
            if(g_slotAlt[si]>0.f) model[13]+=g_slotAlt[si]+sinf((float)tsec*0.5f+si*1.7f)*0.3f;   /* the sky layer swims/soars */
            model[13]+=g_slotFeet[si];
            if(agL9&&agP9&&!agO9) model[13]+=donorHipDropW(agP9,agC9,agF9,P)*sc;   /* (11-06) a borrowed clip its graph says is low: let down by the donor's own hip drop, in the world's up (donorHipDropW) */
            if(GAME_ON) rideBodyModel(si,model);   /* (11-05) tipped and leaned with what it rides */
            if(GAME_ON&&si<1024&&g_bodyPlaceOn[si]) bodyPlaceModel(si,model);   /* (11-12) where and how the game said, this frame (E->bodyPlace) */
            if(g_rdGameOn) rdBody(si,P,world,(const float*)model);   /* limp, if a game said so */
            if(g_attN&&GAME_ON&&si<1024) attDraw(si,P,world9,model);   /* what's on its bones */
            /* AUTO-QUARANTINE (measured, not guessed): if this dancer's RENDERED hips diverge >4u
             * from its sim slot, it self-bans from the spotlight (ghosts kept turning up) */
            { static int pd9=-2; if(pd9==-2){ const char*e=getenv("REVA_POSEDUMP"); pd9=e?atoi(e):-9999; }
              if(pd9==si||(pd9==-5&&si==g_dumpNear)){   /* S4d INSTRUMENT: world-space joints per frame -> posecheck.py; -5 = whoever is nearest the hero */
                  float pj[8][3]; int pb4[8]={P->pb[0],P->pb[12],P->pb[13],P->pb[14],P->pb[15],P->footL,P->footR,P->pb[8]};
                  for(int j9=0;j9<8;j9++){ int b9=pb4[j9];
                      if(b9<0){ pj[j9][0]=pj[j9][1]=pj[j9][2]=-999.f; continue; }
                      float hx=world9[b9][12],hy=world9[b9][13],hz=world9[b9][14];
                      pj[j9][0]=model[0]*hx+model[4]*hy+model[8]*hz+model[12];
                      pj[j9][1]=model[1]*hx+model[5]*hy+model[9]*hz+model[13];
                      pj[j9][2]=model[2]*hx+model[6]*hy+model[10]*hz+model[14]; }
                  printf("[pose] t=%.3f si=%d bhv=%d bhvT=%.2f spd=%.2f xz=%.2f,%.2f hip=%.3f,%.3f,%.3f uL=%.3f,%.3f,%.3f kL=%.3f,%.3f,%.3f uR=%.3f,%.3f,%.3f kR=%.3f,%.3f,%.3f fL=%.3f,%.3f,%.3f fR=%.3f,%.3f,%.3f hnd=%.3f,%.3f,%.3f\n",
                      tsec,si,g_slotBhv[si],g_slotBhvT[si],sl->spd,sl->x,sl->z,
                      pj[0][0],pj[0][1],pj[0][2],pj[1][0],pj[1][1],pj[1][2],
                      pj[2][0],pj[2][1],pj[2][2],pj[3][0],pj[3][1],pj[3][2],
                      pj[4][0],pj[4][1],pj[4][2],pj[5][0],pj[5][1],pj[5][2],
                      pj[6][0],pj[6][1],pj[6][2],pj[7][0],pj[7][1],pj[7][2]); } }
            /* ⛔ 11-04: not a game's limp body. Its hips are where it fell, a long way from its place on the floor if it was thrown, and this banned it from the pool for good: the one way a limp body reached the rules */
            if(P->pb[0]>=0&&!g_slotBan[si]&&!(g_rdGameOn&&si<1024&&g_bodyRd[si])){ float hx=world9[P->pb[0]][12],hy=world9[P->pb[0]][13],hz=world9[P->pb[0]][14];
                float rx3=model[0]*hx+model[4]*hy+model[8]*hz+model[12];
                float rz3=model[2]*hx+model[6]*hy+model[10]*hz+model[14];
                if(hypotf(rx3-sl->x,rz3-sl->z)>4.f){ g_slotBan[si]=1;
                    printf("AUTO-QUARANTINE displaced: %s (render %.0f,%.0f vs slot %.0f,%.0f)\n",
                        P->label,rx3,rz3,sl->x,sl->z); fflush(stdout); }
                if(P->pb[5]>=0){ float dx4=world9[P->pb[5]][12]-hx, dy4=world9[P->pb[5]][13]-hy, dz4=world9[P->pb[5]][14]-hz;
                    float rh=sqrtf(dx4*dx4+dy4*dy4+dz4*dz4)*sc;   /* FULL 3D head-hip distance: pose-independent (Y-only falsely banned leaning dancers) */
                    if(rh<0.4f){ g_slotBan[si]=1;
                        printf("AUTO-QUARANTINE microscopic: %s (head-hip %.3f units)\n",P->label,rh); fflush(stdout); } } }
            float tint[3]={0.55f+0.4f*((si*37%100)/100.f),0.5f+0.3f*((si*61%100)/100.f),0.6f+0.4f*((si*13%100)/100.f)};
            if(g_slotTeam[si]==1){ tint[0]=0.25f; tint[1]=0.55f; tint[2]=1.45f; }        /* game layer: blue army */
            else if(g_slotTeam[si]==2){ tint[0]=1.45f; tint[1]=0.30f; tint[2]=0.25f; }   /* red army / hunters */
            else if(g_slotTeam[si]==3){ tint[0]=0.35f; tint[1]=1.25f; tint[2]=0.30f; }   /* the zombie cab: the zombies, sickly green */
            else if(g_slotTeam[si]==4){ tint[0]=1.35f; tint[1]=1.15f; tint[2]=0.40f; }   /* the zombie cab: the neighbors, warm yellow */
            if(g_slotSel[si]){ float pu2=0.8f+0.2f*sinf((float)tsec*9.f); tint[0]*=pu2*1.4f; tint[1]*=pu2*1.4f; tint[2]*=pu2*1.4f; }
            if(si==g_lockTgt){ float pu=0.65f+0.35f*sinf((float)tsec*12.f);   /* lock-on: the chosen one pulses magenta */
                tint[0]=1.2f*pu; tint[1]=0.25f; tint[2]=1.0f*pu; }
            if(g_boltT>0&&si==g_boltVictim){ float st=(((int)(tsec*24.f))&1)?2.6f:0.4f;
                tint[0]=st; tint[1]=st*0.5f; tint[2]=st; }
            /* this dancer's 3 nearest colored lights (orbs/lamps/snake/reva) */
            float lpv[NLIGHT*3]={0},lcv[NLIGHT*3]={0};
            int labLight=(g_mode==MODE_ANIM);   /* THE LAB'S OWN RIG: the dance floor's coloured movers
                 * lit the SAME pose magenta from the front and blue from the side -- a review
                 * plate has to be neutral, fixed, and identical from every angle. Three white
                 * lamps around the subject: key, fill, rim. */
            { int pick[3]={-1,-1,-1}; float ps[3]={0,0,0};
              for(int li2=0;li2<nLite;li2++){ float dx5=liteP[li2][0]-sl->x, dz5=liteP[li2][2]-sl->z;
                  float s5=(liteC[li2][0]+liteC[li2][1]+liteC[li2][2])/(1.f+(dx5*dx5+dz5*dz5)*0.055f);
                  for(int k4=0;k4<3;k4++) if(s5>ps[k4]){ for(int m4=2;m4>k4;m4--){ ps[m4]=ps[m4-1]; pick[m4]=pick[m4-1]; } ps[k4]=s5; pick[k4]=li2; break; } }
              for(int k4=0;k4<3;k4++){ int li2=pick[k4];
                  if(li2<0){ lpv[k4*3]=0; lpv[k4*3+1]=-99; lpv[k4*3+2]=0; lcv[k4*3]=lcv[k4*3+1]=lcv[k4*3+2]=0; }
                  else { memcpy(lpv+k4*3,liteP[li2],12); memcpy(lcv+k4*3,liteC[li2],12); } } }
            lightsStamp(lpv,lcv,3);   /* ⭐ a torch in someone's hand lights the crowd too */
            int nearRv=hypotf(sl->x-g_rvx,sl->z-g_rvz)<6.0f||si==CAMD.tgt;
            if(g_slotHide[si]) continue;   /* squid-convicted: invisible until relaunch */
            if(si<1024&&g_bodyUnseen[si]&&!g_shadowPass) continue;   /* (11-11) not on this machine's picture (E->bodyUnseen): its shadow still is */
            /* ⭐ (11-07) SMALL IN THE PICTURE, A LIGHT COPY (E->tune->bodyLite, body.inc litePrep): copy l while one of its cells, a LITE_DIV[l]th of the body, is
             * under two of the picture's lines. To go back to a heavier copy it has to be 15% past the line, so a body at the edge doesn't flicker between them */
            int lite9=-1;
            if(lite9On&&si<1024){ const float h9=fmaxf(P->effH*P->scale,0.3f), w9=vp[3]*model[12]+vp[7]*(model[13]+0.5f*h9)+vp[11]*model[14]+vp[15];
                const float ln9=w9>0.01f?h9*kyL9/w9:1e9f; const int was9=liteWas9[si];
                if(ln9<=2.f*LITE_DIV[1]*(was9>=1?1.15f:1.f)) lite9=1; else if(ln9<=2.f*LITE_DIV[0]*(was9>=0?1.15f:1.f)) lite9=0;
                liteWas9[si]=(signed char)lite9;
                if(lite9>=0){ litePrep(P); if(!P->liteN) lite9=-1; else liteN9++; } }
            for(unsigned mi=0;mi<P->nMeshes;mi++){ PMesh*M=&P->meshes[mi];
                if(!(M->flags&4)||M->boneCount>MAXB||MOFF(P,mi)) continue;
                if(labLight){
                    float kx=g_rvx,kz=g_rvz;
                    float llp[NLIGHT*3]={kx+2.6f,3.4f,kz+2.2f,  kx-3.0f,2.2f,kz+1.4f,  kx-0.6f,2.8f,kz-3.2f,
                                        0,-99,0, 0,-99,0, 0,-99,0};
                    float llc[NLIGHT*3]={2.6f,2.6f,2.6f,        1.15f,1.15f,1.25f,      1.5f,1.5f,1.45f,
                                        0,0,0, 0,0,0, 0,0,0};
                    lightsStamp(llp,llc,3);
                      }
                else {   }
                const float*palU=pal; int palS9=0;
                if(pr9&&pr9->pal[mi]){ palU=pr9->pal[mi]; palS9=1; }
                else { bodyPalette(P,mi,world9,pal);
                /* SQUID SENTINEL (whatever model causes the flashes is removed): any
                 * degenerate palette translation (NaN or absurd) convicts the slot BY NAME */
                { int guilty=0;
                  for(unsigned b3=0;b3<M->boneCount&&!guilty;b3++){
                      float tx2=pal[b3*16+12],ty2=pal[b3*16+13],tz2=pal[b3*16+14];
                      if(tx2!=tx2||ty2!=ty2||tz2!=tz2||fabsf(tx2)>8e3f||fabsf(ty2)>8e3f||fabsf(tz2)>8e3f) guilty=1; }
                  if(guilty){ g_slotHide[si]=1; g_slotBan[si]=1;
                      printf("SQUID: %s (slot %d, mesh %u) -- hidden\n",P->label,si,mi); fflush(stdout);
                      break; } }
                  if(pr9&&(palU=poseKeepPal(pr9,mi,M->boneCount,pal))!=pal) palS9=1; }
                if(nearRv&&nCast<NCASTMAX-2){   /* MORE SHADOWS: pair + neighbors cast for real (2 slots reserved for reva) */
                    memcpy(castPal[nCast],palU,M->boneCount*64); castP[nCast]=P; castMi[nCast]=(int)mi;
                    memcpy(castModelA[nCast],model,64); nCast++; }
                if(lite9>=0&&P->liteN[mi*2+lite9]){ g_vkIdxIb=P->liteIb[mi*2+lite9]; g_vkIdxN=P->liteN[mi*2+lite9]; }   /* (11-07) its light copy: the same vertices, fewer triangles */
                if(g_shadowOn) sunStash(P,(int)mi,palU,(int)M->boneCount,model,1);   /* ⭐ the crowd casts under a sun (09-11), capped */
                unsigned*b=&P->vbo[mi*6];
                int tex=P->meshTex[mi];
                if(tex>=0&&(M->flags&2)){
                    /* ⭐ a texture wins over a tint outright, so the cab's "sickly green" never showed on a textured body. Part of the way, it does:
                     * the renderer mixes tint and texture by useTex (under 0.5 it stops alpha-testing, so dead_tint stays below that). */
                      }
                else {  }
                g_vkPalShared=palS9?g_poseStamp:0u;
                { static const float lc9[2][3]={{0.2f,1.f,0.2f},{1.f,0.9f,0.1f}};   /* REVA_LODDBG: a body drawn from its first copy washed green, its second yellow */
                  const int d9=liteDbg9&&g_vkIdxIb; g_meshTintSlot=GAME_ON?si:-1; bodyDrawMesh(P,mi,model,palU,b,d9?lc9[lite9]:tint,d9?0.55f:(GAME_ON?g_game->bodyUse(si):1.0f),0,0); g_meshTintSlot=-1; }
                g_vkPalShared=0u; g_vkIdxIb=g_vkIdxN=0;
            }
        }
        if(!g_noDraw&&lite9On){ g_liteAcc+=liteN9; g_liteOn=1; }
        /* VENUE JUMBOS — the head, on the walls. Opaque and depth-tested: they are objects in
         * the room. Drawn before the lasers so the beams glow over them. */
        if(holly_fbo_texture()&&g_mode!=MODE_ANIM){   /* the lab is a clean plate: no screens */
          for(int si=0;si<NJUMBOS;si++){
              const float*S2=JUMBOS[si];
              /* 08-01: the house face on one screen, the live feed of the star on
               * the next. Alternating puts both on every wall. */
              float cy=cosf(S2[3]), sy2=sinf(S2[3]);
              M4 mm2; memset(mm2,0,64);
              /* column-major: T * Ry * S, with the quad's own half-extents as the scale */
              mm2[0]= cy*S2[4]; mm2[2]=-sy2*S2[4];
              mm2[5]= S2[5];
              mm2[8]= sy2;      mm2[10]= cy;
              mm2[12]=S2[0]; mm2[13]=S2[1]; mm2[14]=S2[2]; mm2[15]=1;
#ifdef REVA_VK
              if(!g_noDraw){ double bp9=beatPhase(); gfxScreen((si&1)?feedTex:holly_fbo_texture(),mm2,0.9f+0.25f*(float)(1.0-(bp9-floor(bp9)))); }
#endif
               }
           }

        passMark("crowd");
        /* ⭐ PLACED SCREENS -- and these DO draw in MODE_ANIM, which is the whole point:
         * the jumbotrons are venue dressing and the lab is a clean plate, but a screen a
         * SET asked for is a thing in the story. ⛔ T * Ry * Rx * S: the jumbotron matrix
         * above is yaw-only and a telly face-down in the dirt is the gag. Steady amplitude
         * too -- the dance floor's beat pulse has no business on a driveway. */
        /* PLACED SCREENS, drawn by scrDrawPlaced. HERE only when no set is loaded: a
         * set paints its sky first in drawSet with no depth test, which wiped this quad every frame
         * while its depth kept the room out -- Holly's blank screen. With a set, the world pass draws
         * them after the world. */
        if(!g_setNI&&!g_setSpace) scrDrawPlaced();

        /* THE DANCE FLOOR'S LASERS — 7-beam fan from above the back of the stage, hue-cycling, beat-pulsed */
        if(g_mode!=MODE_ANIM&&SET_NONE&&!g_setSpace){ double bp=beatPhase(); float pulse=0.8f+0.4f*powf(1.0f-(float)(bp-floor(bp)),2.0f);
          float sw=0.55f+0.25f*sinf((float)tsec*0.21f);
          for(int li=0;li<14;li++){ float ph=(li%7)/7.f; int rig=li/7;   /* TWO rigs -- it's a rave */
              float rz2=sinf((float)tsec*(0.55f+g_bpm/400.f)+ph*6.283f+rig*1.7f)*sw*(rig?-1.f:1.f);
              float rx2=0.95f+sinf((float)tsec*0.37f+ph*4.2f+rig*2.6f)*0.28f;
              float cz2=cosf(rz2),sz2=sinf(rz2),cx2=cosf(rx2),sx2=sinf(rx2);
              M4 mm; memset(mm,0,64);
              mm[0]=cz2; mm[1]=sz2; mm[4]=-sz2*cx2; mm[5]=cz2*cx2; mm[6]=sx2;
              mm[8]=sz2*sx2; mm[9]=-cz2*sx2; mm[10]=cx2; mm[15]=1;
              mm[12]=rig?10.5f:-10.5f; mm[13]=9.0f; mm[14]=-14.0f;
              float r3,g3,b3; hsv((float)tsec*0.07f+ph*0.45f+rig*0.5f,&r3,&g3,&b3);
              if(!g_noDraw) venueLaser(mm,r3,g3,b3,pulse);
               }
          if(!g_noDraw) venueLasersEnd();
}
        /* REVA — the hologram, drawn over her town */
        if(hasReva&&g_heroWear>=0){ revaP=g_packs[g_heroWear]; g_heroWorn=g_heroWear; g_heroWear=-1; g_heroPk=&revaP; revaGround=-revaP.mny*revaP.scale+0.02f; { Face*hf9=faceOf(-1); if(hf9) hf9->pk=NULL; } }   /* E->heroWear */
        if(hasReva){ Pack*P=&revaP;
            /* host statics HOISTED: the locomotion picker below reads last frame's state */
            static float rvH=0; static int rvState=0, rvTgt=-1; static double rvT=2;
            static float rvTx=0, rvTz=0;   /* travel DESTINATION: the composed spot beside the showcased model */
            static int rvMode=0; static float rvSpd=5.2f, rvAlt=0, rvPitch=0, rvAm=0;
            static float rvCph=0, rvHW=0, rvRoll=0;   /* carve phase / weave heading / bank */
            /* LOCOMOTION STATE MACHINE (full walk and run states; the movement was janky):
             * travel tier picks the clip -- walk AND run share w_walk with the cycle riding her
             * speed (gait sync), ski holds the pumping skate cycle, flight flies; the dwell
             * cycles the WHOLE 33-dance vocabulary -- the 18 old dances finally sit right
             * (alias conjugation retarget cured the reversed Spine02/01 crunch). */
            static int rvCM=-1; static double rvWph=0; static float rvXf=1;
            static float pQ[160][4]; static float pT[160][3]; static int pInit=0;   /* S4d: 64 missed the ankles -- the grounding bone snapped outside the blend */
            int want=-1; float rate=1.0f;
            Pack*hdP=NULL; int hdC=-1;   /* S6: the hero borrowing a donor clip (she has no run of her own) */
            int hdMaskArms=0;            /* S7: keep the borrowed legs, drop the borrowed arms */
            /* SKATE MODE owns the clip outright: she rides, and neither the flight branch
             * nor the idle dance picker below is ever reached. */
            float skHold=-1.f;   /* >=0 pins the clip to this fraction of its length */
            if(g_skate){
                skStep(dt);                       /* physics owns her position now */
                g_rvx=SK.x; g_rvz=SK.z; g_rvHead=SK.head;
                skUpdate(dt,SK.x,SK.z,SK.head,SK.air?(SK.y-parkH(SK.x,SK.z)):0.f);
                /* ONE-SHOT TRICK CLIP (baked rigs). skHold already pins a clip to a fraction of
                 * its length, so a trick is simply that fraction walked from 0 to 1 once, at the
                 * clip's own duration -- no blending machinery required. */
                static float onceT=0.f; static int onceClip=-1;
                if(g_skTrickFired){
                    g_skTrickFired=0;
                    int tw=(g_skTrickWhich&&g_rvTrick2>=0)?g_rvTrick2:g_rvTrick;
                    float tdur=(g_skTrickWhich&&g_rvTrick2>=0)?g_rvTrick2Dur:g_rvTrickDur;
                    if(g_trickSpd<=0.f){ const char*e=getenv("REVA_TRICKSPD");
                                         g_trickSpd=e?(float)atof(e):2.6f; }
                    if(!g_procRig && tw>=0 && tdur>0.01f){ onceClip=tw; onceT=0.f;
                        g_rvTrickCur=tdur/g_trickSpd;
                        printf("  -> one-shot clip %d for %.2fs\n",onceClip,g_rvTrickDur); fflush(stdout); } }
                if(onceClip>=0){
                    onceT+=(float)dt;
                    if(onceT>=g_rvTrickCur) onceClip=-1;
                }
                /* the tree cannot see SK from where it is defined, so the two rail/wheel states are applied here */
                /* on-foot overrides everything: he is not on the board at all */
                if(SK.onFoot) g_skState=(g_skSpd<0.45f)?SK_FOOT_IDLE:((g_skSpd<5.0f)?SK_FOOT_WALK:SK_FOOT_RUN);
                else if(SK.grind) g_skState=SK_GRIND;
                else if(SK.manual) g_skState=SK_MANUAL;
                /* measured speed decides the rest, but a held trigger is an INTENT the
                 * measurement cannot see: she should kick the moment you ask, not once
                 * she happens to be in the right speed band. */
                else if(g_skPushing && !SK.air) g_skState=SK_PUSH;
                g_idleOverlay=0; g_authored=0;
                /* ---- RESOLVE THE STATE TO A CLIP ------------------------------------
                 * Authored animation wherever the character has it; procedural fill only
                 * where they do not. Each state names its preferred clip and then falls back,
                 * ending at HOLD: the neutral stance held still with the idle oscillators
                 * layered on, which is a pose that always exists because it is measured from
                 * the character rather than shipped with them.
                 * Nothing here is specific to this asset -- a character with a full set of
                 * clips simply never reaches the fallbacks. */
                #define SK_HOLD() do{ g_authored=0; if(g_rvSki>=0&&g_idleHold>=0.f){ want=g_rvSki; rate=0.f; skHold=g_idleHold; } \
                                      else if(g_rvWalk>=0){ want=g_rvWalk; rate=0.f; skHold=0.27f; } \
                                      g_idleOverlay=1; }while(0)
                if(onceClip>=0){ g_authored=1; want=onceClip; rate=0.f; skHold=onceT/g_rvTrickCur; }
                else switch(g_skState){

                /* ---------------- on foot ---------------- */
                case SK_FOOT_IDLE:
                    if(g_rvFootIdle>=0){ g_authored=1; want=g_rvFootIdle; rate=1.f; }
                    else if(g_rvWalk>=0){ g_authored=1; want=g_rvWalk; rate=0.f; skHold=0.27f; g_idleOverlay=1; }
                    else SK_HOLD();
                    break;
                case SK_FOOT_WALK:
                    if(g_rvWalk>=0){ g_authored=1; want=g_rvWalk; rate=0.55f+g_skSpd*0.22f; }
                    else SK_HOLD();
                    break;
                case SK_FOOT_RUN:
                    if(g_rvRun>=0){ g_authored=1; want=g_rvRun; rate=0.75f+g_skSpd*0.10f; }
                    else if(g_rvWalk>=0){ g_authored=1; want=g_rvWalk; rate=0.90f+g_skSpd*0.20f; }   /* no run clip: drive the walk harder */
                    else SK_HOLD();
                    break;

                /* ---------------- on the board ---------------- */
                case SK_IDLE:
                    if(!g_procRig && g_rvNDance>0){ g_authored=1; want=g_rvDance[0]; rate=1.f; }
                    else if(g_procRig && g_rvWalk>=0){ g_authored=1; want=g_rvWalk; rate=0.f; skHold=0.27f; }
                    else SK_HOLD();
                    break;
                case SK_PUSH:
                    if(g_rvWalk>=0){ g_authored=1; want=g_rvWalk;
                        rate=g_procRig?(0.55f+g_skSpd*0.34f):(0.75f+g_skSpd*0.14f); }
                    else SK_HOLD();
                    break;
                case SK_ROLL:
                case SK_CARVE:
                case SK_GRIND:
                case SK_MANUAL:
                case SK_AIR:
                default:
                    /* a RIDE loop only counts if it is flat -- a trick loop wired here makes
                     * him flip continuously while simply rolling along */
                    if(g_rvRide>=0){ g_authored=1; want=g_rvRide; rate=0.55f+g_skSpd*0.05f; }
                    else SK_HOLD();
                    break; } }
            else
            if(g_manual||GAME_ON){   /* M0: the zombie cab's attract host moves like a thumb, so she animates like one */
                __attribute__((unused)) static float gT9=0.f;   /* landing hysteresis (S4d): 1-frame ground touches must not flicker the clip */
                if(g_mode==MODE_ANIM){   /* THE LAB: the state IS the clip, no guessing */
                    switch(GM.stage){
                    case LAB_RUN:   if(g_rvRun>=0){ want=g_rvRun; rate=gaitRate(P,want,g_manSpd); }
                                    else if(g_dnRunP&&P->pbOK>=10&&donorOK(g_dnRunP,g_dnRun,P)){ hdP=g_dnRunP; hdC=g_dnRun; rate=gaitRate(hdP,hdC,g_manSpd); }
                                    else if(g_rvWalk>=0){ want=g_rvWalk; rate=gaitRate(P,want,g_manSpd); }
                                    break;
                    case LAB_WALK:  if(g_rvWalk>=0){ want=g_rvWalk; rate=gaitRate(P,want,g_manSpd); }
                                    else if(g_dnNWalks>0&&P->pbOK>=10&&donorOK(g_dnWalksP[0],g_dnWalks[0],P)){ hdP=g_dnWalksP[0]; hdC=g_dnWalks[0];
                                        rate=gaitRate(hdP,hdC,g_manSpd); } break;
                    case LAB_AIR:   if(g_rvFly>=0){ want=g_rvFly; rate=1.0f; } break;
                    case LAB_TRICK: if(g_rvTrick>=0){ want=g_rvTrick; rate=1.0f; }
                                    else if(g_rvSki>=0){ want=g_rvSki; rate=1.0f; }
                                    break;
                    /* S7 THE STAND LADDER: own idle clip -> an audited borrowed stand ->
                     * the REST pose. The old last rung held the walk cycle 27% through,
                     * which measured stance 5.7x hip width and read as a statue caught
                     * mid-step; rest is at least a pose a body can be in. */
                    default:        if(g_rvFootIdle>=0){ want=g_rvFootIdle; rate=1.0f; }
                                    else if((hdMaskArms=dnStandPick(P,&hdP,&hdC))){ rate=1.0f; hdMaskArms=(hdMaskArms==2); }
                                    break; } }
                else if(g_manAlt>0.4f&&g_rvFly>=0){ want=g_rvFly; rate=1.0f; }
                else if(g_manSpd>4.0f&&g_rvRun<0&&g_dnRunP&&P->pbOK>=10&&donorOK(g_dnRunP,g_dnRun,P)){
                    hdP=g_dnRunP; hdC=g_dnRun; rate=g_manSpd*runMul(); }   /* S6: a real run, borrowed */
                else if(g_manSpd>4.0f&&g_rvRun>=0){ want=g_rvRun; rate=gaitRate(P,want,g_manSpd); }   /* she HAS a run cycle; auto-synced */
                else if(g_manSpd>4.0f&&g_rvWalk>=0){ want=g_rvWalk; rate=gaitRate(P,want,g_manSpd); }
                else if(g_manSpd>0.35f&&g_rvWalk>=0){ want=g_rvWalk; rate=gaitRate(P,want,g_manSpd); }
                else if(g_mode!=MODE_DANCE&&g_rvFootIdle>=0){ want=g_rvFootIdle; rate=1.0f; }   /* on the clock: no idle twerk mid-match */
                else if(g_mode!=MODE_DANCE&&(hdMaskArms=dnStandPick(P,&hdP,&hdC))){ rate=1.0f; hdMaskArms=(hdMaskArms==2); }   /* S7: same stand ladder, all eleven doors */
                else { static double pkT=-99; static int di3=0;
                    if(tsec-pkT>22.0){ pkT=tsec; di3=(di3+7)%(g_rvNDance?g_rvNDance:1); }
                    want=g_rvNDance?g_rvDance[di3]:0; } }
            else if(rvState==1&&rvMode==4&&g_rvFly>=0){ want=g_rvFly; rate=1.0f; }
            else if(rvState==1&&rvMode==3&&g_rvSki>=0){ want=g_rvSki; rate=0.45f; }
            else if(rvState==1&&rvMode==2&&g_rvWalk>=0){ want=g_rvWalk; rate=rvSpd*0.5f; }
            else if(rvState==1&&g_rvWalk>=0){ want=g_rvWalk; rate=rvSpd*0.40f; }
            else { static double pickT=-99; static int di=0; static int lastP=-2;
                if(rvTgt!=lastP){ lastP=rvTgt; pickT=tsec; di=(di+7)%(g_rvNDance?g_rvNDance:1); }   /* fresh dance for every guest (the spotlight's dances were repetitive) */
                if(tsec-pickT>22.0){ pickT=tsec; di=(di+7)%(g_rvNDance?g_rvNDance:1); }
                want=g_rvNDance?g_rvDance[di]:0; }
            /* ⭐ ONE-SHOT TRICKS fire and end here, on an exposure, before any body's clip is chosen or advanced */
            if(g_mode==MODE_ANIM) trickTick();
            /* P0: ctl clip=<name> plays ANY clip by name -- the authoring loop's playback
             * path. Overrides the state tree (the state still drives travel speed and the
             * camera); direct on the subject's own rig, rate 1, from frame 0 on switch.
             * No donor, no gait audit: play-by-name is exempt by design (a cartwheel
             * fails clipUpright on purpose). */
            if(g_mode==MODE_ANIM&&g_labClip>=0&&(unsigned)g_labClip<P->nClips){
                /* P1 once=1: at the clip's last frame, clear the override -- the state
                 * tree resumes and rvXf crossfades the join. A move authored to END at
                 * its base pose hands back invisibly; one that does not is caught by the
                 * continuity check, which is the point. */
                if(g_labOnce&&!g_labWphReset&&rvWph>=(double)P->clips[g_labClip].frames-1.0){
                    if(g_labRM){ printf("[lab] HANDBACK: origin (%.3f,%.3f) -> rmEndW (%.3f,%.3f)  wph %.2f/%d\n",
                                        g_rvx,g_rvz,g_rmEndW[0],g_rmEndW[1],rvWph,P->clips[g_labClip].frames); fflush(stdout);
                                 g_rvx=g_rmEndW[0]; g_rvz=g_rmEndW[1]; }   /* P4: the travel is KEPT -- no teleport home */
                    g_labClip=-1; printf("[lab] clip once: done, state tree resumes\n"); fflush(stdout); }
                else { want=g_labClip; hdP=NULL; hdC=-1; rate=1.0f; skHold=-1.f;
                       if(g_labWphReset){ rvWph=heroStartPh(P->clips[g_labClip].fps,P->clips[g_labClip].frames); g_labWphReset=0; } } }
            /* P5: the authored move found in the MOVES pack -- played on THIS body
             * through the canonical donor transfer. The co-star inherits every score. */
            else if(g_mode==MODE_ANIM&&g_labDnC>=0&&(g_labDnP?g_labDnP:g_movesPk)&&
                    (unsigned)g_labDnC<(g_labDnP?g_labDnP:g_movesPk)->nClips){
                Pack*dnp9=g_labDnP?g_labDnP:g_movesPk;
                if(g_labOnce&&!g_labWphReset&&rvWph>=(double)dnp9->clips[g_labDnC].frames-1.0){
                    if(g_labRM){ g_rvx=g_rmEndW[0]; g_rvz=g_rmEndW[1]; }
                    g_labDnC=-1; printf("[lab] clip once: done, state tree resumes (donor)"); printf("%c",10); fflush(stdout); }
                else { hdP=dnp9; hdC=g_labDnC; want=-1; rate=1.0f; skHold=-1.f;
                       if(g_labWphReset){ rvWph=heroStartPh(dnp9->clips[g_labDnC].fps,dnp9->clips[g_labDnC].frames); g_labWphReset=0; } } }
            /* ⭐ A GAIT UNDER THE STAGE MOVE. Same hook the hero already uses to borrow
             * a run she does not own: point hdP/hdC at the bank's walk and let gaitRate()
             * pick the stride for the speed the mark is actually carrying her at. */
            if((g_stageOn||g_heroMk)&&g_loco[0].spd>0.22f&&!P->nWheel&&!castRides(0)&&P->cbOK&&P->pbOK>=10){   /* ⭐ world speed; a rider rolls */
                int bc9=-1; Pack*bp9=bankFind("w_walk",&bc9);
                if(bp9){ hdP=bp9; hdC=bc9; want=-1;
                         rate=gaitRate(bp9,bc9,g_loco[0].spd); } }
            if(GAME_ON&&g_agHero.on){ agTick(P,(float)dt,&want,&rate,&skHold); if(g_agG[g_agHero.g].on){ hdP=NULL; hdC=-1; hdMaskArms=0; } }   /* a game with an animation graph: it picks the clip and says how far through */
            { int key=(hdC>=0)?(9000+hdC):want; if(key!=rvCM){ rvCM=key; rvXf=pInit?0.f:1.f; } }
            if(g_trkFresh[0]) rvXf=1.f;   /* ⭐ a trick is a CUT, going and coming back -- no crossfade */
            g_rvClipNow=want; g_rvRateNow=rate;   /* the lab HUD reads these */
            /* P7: the partner runs on the HERO'S clock. A two-body move is one
             * performance -- give her a clock of her own and the five drifts out of
             * time within a few seconds. When the hero restarts a one-shot, so does she. */
            /* ⭐ AND THE WHOLE CAST RUNS ON IT, not just the partner. A scene is one
             * performance; give any body its own clock and it drifts out of the take
             * within seconds. The loop lends each slot g_castI and the existing lines
             * resolve to it unchanged. */
            { int sv7=g_castI;
              for(g_castI=1;g_castI<SCN_MAXCAST;g_castI++)
                if(g_labPartner&&g_labPClip>=0){
                    if(g_labWphReset) g_labPPhase=0.0;
                    /* ⛔ A HELD POSE DOES NOT ADVANCE. Without this the clip walks off the
                     * frame that was chosen and the pose is only correct for one instant. */
                    else if(!g_labFreeze&&g_labPHold<0.f&&!g_trkFresh[g_castI]) g_labPPhase+=dt*g_labPartner->clips[g_labPClip].fps*rate; }   /* ⭐ a trick's cut exposure is its frame 0 */
              g_castI=sv7; }
            bodyRest(P,T,Q,S);
            if(hdC>=0&&hdP){   /* S6: donor clip on the hero's rig, rest-delta like the crowd */
                PClip*c=&hdP->clips[hdC];
                if(g_heroHold>=0.f) rvWph=(double)c->frames*g_heroHold;
                else if(!g_labFreeze&&!g_trkFresh[0]) rvWph+=dt*c->fps*rate;   /* ⭐ a trick's cut exposure is its frame 0 */
                if(g_trkOn[0]&&hdP==(g_labDnP?g_labDnP:g_movesPk)&&hdC==g_labDnC) trickClampEnd(0,&rvWph,c);
                donorPoseF(hdP,hdC,rvWph,P,Q);   /* P1.5: fractional */
                /* P5: authored ROOT MOTION crosses rigs too -- the donor's pelvis
                 * translation delta, taken world, facing-mapped, scaled by the leg
                 * ratio, and conjugated into the target pelvis's parent frame. */
                if(g_labRM&&hdP==g_movesPk&&hdP->pb[0]>=0&&P->pb[0]>=0&&hdP->cbOK&&P->cbOK&&hdP->cbLeg>1e-3f){
                    double fp5=fmod(rvWph,(double)c->frames); if(fp5<0) fp5+=c->frames;
                    unsigned f5=(unsigned)fp5, f6=(f5+1)%c->frames; float t5=(float)(fp5-(double)f5);
                    unsigned char*tr5=hdP->pk+c->offTracks;
                    for(unsigned t9=0;t9<c->nTracks;t9++){
                        unsigned nd5=*(unsigned*)(tr5+t9*12), ty5=*(unsigned*)(tr5+t9*12+4), of5=*(unsigned*)(tr5+t9*12+8);
                        if(ty5!=1||nd5!=(unsigned)hdP->pb[0]) continue;
                        float*d5=(float*)(hdP->pk+of5);
                        float v5[3]={ d5[(size_t)f5*3]+(d5[(size_t)f6*3]-d5[(size_t)f5*3])*t5,
                                      d5[(size_t)f5*3+1]+(d5[(size_t)f6*3+1]-d5[(size_t)f5*3+1])*t5,
                                      d5[(size_t)f5*3+2]+(d5[(size_t)f6*3+2]-d5[(size_t)f5*3+2])*t5 };
                        float dl[3]={ v5[0]-hdP->nodes[hdP->pb[0]].t[0],
                                      v5[1]-hdP->nodes[hdP->pb[0]].t[1],
                                      v5[2]-hdP->nodes[hdP->pb[0]].t[2] };
                        /* donor-local -> donor-world -> facing-map -> scale -> target-local */
                        float qv[4]={dl[0],dl[1],dl[2],0}, tq[4], wv[4], pc[4];
                        qmul(tq,hdP->cbWpar[0],qv); qconj(pc,hdP->cbWpar[0]); qmul(wv,tq,pc);
                        float GG5[4],gi5[4]; qconj(gi5,hdP->cbG); qmul(GG5,P->cbG,gi5);
                        float qv2[4]={wv[0],wv[1],wv[2],0}, t2q[4], wv2[4];
                        qmul(t2q,GG5,qv2); qconj(pc,GG5); qmul(wv2,t2q,pc);
                        float sc5=P->cbLeg/hdP->cbLeg;
                        float qv3[4]={wv2[0]*sc5,wv2[1]*sc5,wv2[2]*sc5,0}, t3q[4], lv[4], ic5[4];
                        qconj(ic5,P->cbWpar[0]); qmul(t3q,ic5,qv3); qmul(lv,t3q,P->cbWpar[0]);
                        T[P->pb[0]][0]=P->nodes[P->pb[0]].t[0]+lv[0];
                        T[P->pb[0]][1]=P->nodes[P->pb[0]].t[1]+lv[1];
                        T[P->pb[0]][2]=P->nodes[P->pb[0]].t[2]+lv[2];
                        break; } }
                /* S7 ARM MASK: put the arm chain back on the body's own rest rotations.
                 * Clavicle through forearm, both sides -- everything below the forearm is
                 * parented to it and follows for free. */
                if(hdMaskArms) for(int a9=6;a9<=11;a9++){ int b9=P->pb[a9];
                    if(b9>=0) memcpy(Q[b9],P->nodes[b9].q,16); }
                /* ⛔ THE HEAD LAYER NEVER RAN ON A BORROWED CLIP (09-11). Only the own-clip
                 * branch called scnHeadLayer, so a hero on a bank clip -- every x_convo in the
                 * film, the bought character on z_upright in the lab -- got no head aim, no gaze, no jaw
                 * bench override. Found by the eyes: gaze=0.6 changed 0 pixels. */
                scnHeadLayer(P,Q,0); }
            else if(P->nClips&&want>=0){ PClip*c=&P->clips[want];
                if(skHold>=0.f) rvWph=(double)c->frames*skHold;
                else if(!g_labFreeze&&!g_trkFresh[0]) rvWph+=dt*c->fps*rate;   /* ⭐ a trick's cut exposure is its frame 0 */
                if(g_trkOn[0]&&g_mode==MODE_ANIM&&want==g_labClip) trickClampEnd(0,&rvWph,c);
                /* H6: clip-driven morph weights. A SCENE owns the face while it runs --
                 * without this guard the pose sampler zeroed the weights the scene had
                 * just set, every frame, and the mouths would never have opened. */
                if(g_scnT<0.f){ for(int m9=0;m9<MORPH_MAX;m9++) g_heroMorphW[m9]=0.f;
                    g_morphSink=g_heroMorphW; g_morphSinkN=P->nMorph; }
                else if(P->nMorph){ memset(g_clipMorphW[0],0,sizeof g_clipMorphW[0]); g_morphSink=g_clipMorphW[0]; g_morphSinkN=P->nMorph; }   /* (11-11) in a scene, her clip's face beside the scene's */
                /* ⭐ a TRANSITION plays once in a scene and holds its last frame (clipHoldsEnd) */
                if(g_scnT>=0.f&&rvWph>(double)(c->frames-1)&&clipHoldsEnd(P,c,0,packStr(P,c->name))) rvWph=(double)(c->frames-1);
                clipSampleF(P,c,rvWph,T,Q,S);   /* P1.5: fractional */
                g_morphSink=0;
                scnHeadLayer(P,Q,0); }
            /* ⭐ THE UPPER-BODY LAYER (09-11): a second clip on the spine and up, on its own clock */
            if(g_overClip[0][0]) poseOverlay(P,Q,g_overClip[0],overPhase(P,g_overClip[0],tsec),g_overW[0]);
            /* CROSSFADE from last frame's pose (~0.35s): state/clip switches never pop */
            g_boardXfKeep[0]=0.f;   /* ⭐ the board channel crossfades with the body (boardPublish) */
            if(pInit&&rvXf<1.f){ float e5=rvXf*rvXf*(3-2*rvXf); g_boardXfKeep[0]=1.f-e5;
                for(unsigned i=0;i<P->nNodes&&i<160;i++){ qnlerp(Q[i],pQ[i],Q[i],e5);
                    for(int k5=0;k5<3;k5++) T[i][k5]=pT[i][k5]*(1-e5)+T[i][k5]*e5; }
                rvXf+=(float)dt*2.8f; }
            /* ⭐ MOTION ENERGY, from the pose itself (09-11): the summed joint angular speed over
             * the bones, rad/s. Published at 10 Hz during a scene as `[motion] t=.. hero .. p1 .. p2 ..`
             * so the film's cutter can cut ON AN ACTION instead of on a second (Mascelli). The
             * cast slots report through g_motE from their own draws, one frame behind. */
            if(pInit&&g_scnT>=0.f){ float e=0.f; for(unsigned i=0;i<P->nNodes&&i<160;i++){ float d=fabsf(Q[i][0]*pQ[i][0]+Q[i][1]*pQ[i][1]+Q[i][2]*pQ[i][2]+Q[i][3]*pQ[i][3]); if(d>1.f)d=1.f; e+=2.f*acosf(d); }
                g_motE[0]=(dt>1e-5)?e/(float)dt:0.f;
                static float lastT=-1.f; float st=scnBodyT(); if(st-lastT>=0.1f||st<lastT){ lastT=st;
                    printf("[motion] t=%.2f hero %.2f p1 %.2f p2 %.2f\n",st,g_motE[0],g_motE[1],g_motE[2]); } }
            for(unsigned i=0;i<P->nNodes&&i<160;i++){ memcpy(pQ[i],Q[i],16); memcpy(pT[i],T[i],12); }
            /* ---- A SYNTHESISED IDLE ----------------------------------------------
             * Some characters ship nothing but performance: every clip is a run, so there is
             * no idle to play and any window cut from one still has them mid-trick. Holding
             * the lowest-stance frame gets the POSE right but leaves a statue.
             * So the stance is held and life is layered on top: a slow weight shift through
             * the knees, the chest counter-rotating against it, and a breath. It costs four
             * oscillators and turns a frozen frame into someone waiting on a board -- and it
             * works on any rig, because it only needs the bones that did map. */
            if(g_skate && g_idleOverlay && P->pbOK>0){
                float w1=(float)sin(tsec*0.8), w2=(float)sin(tsec*0.53+1.1);
                float br=(float)sin(tsec*1.9);              /* breath */
                float off[4];
                int kl=P->pb[13], kr=P->pb[15];
                if(kl>=0){ qaxis(off,1,0,0, 0.045f*w1+0.012f*br); qmul(Q[kl],Q[kl],off); }
                if(kr>=0){ qaxis(off,1,0,0,-0.045f*w1+0.012f*br); qmul(Q[kr],Q[kr],off); }
                for(int k2=0;k2<3;k2++){ int b2=P->pb[1+k2]; if(b2<0) continue;
                    qaxis(off,0,0,1,-0.030f*w1*(k2==0?1.f:0.6f));
                    qmul(Q[b2],Q[b2],off);
                    qaxis(off,1,0,0, 0.014f*br);
                    qmul(Q[b2],Q[b2],off); }
                if(P->pb[5]>=0){ qaxis(off,0,1,0,0.05f*w2); qmul(Q[P->pb[5]],Q[P->pb[5]],off); }
                for(int side=0;side<2;side++){ int up=P->pb[side?10:7];
                    if(up>=0){ qaxis(off,0,0,1,(side?-1.f:1.f)*0.035f*w2); qmul(Q[up],Q[up],off); } }
            }
            /* ---- S4.5 HERO STRIKE + GUNFIRE: the swing was neon-only; now the body commits.
             * Melee: one-shot arm arc + torso twist over whatever clip is playing (BR + ARPG
             * share the GM.tA swing window). Firing: the co-star's two-hand stance via the donor
             * bank for the first beat of the gun's cooldown. */
            if((DOOR_SWINGS||g_mode==MODE_ANIM)&&GM.tA>0.f&&P->pbOK>0){
                float u9=1.f-fminf(1.f,GM.tA/0.25f); float sw9=sinf(u9*3.14159f);
                float off9[4],tq9[4];
                if(P->pb[7]>=0){ qaxis(off9,1,0,0,-2.0f*sw9); qmul(Q[P->pb[7]],Q[P->pb[7]],off9); }
                if(P->pb[8]>=0){ qaxis(off9,1,0,0,-0.8f*(1.f-u9)); qmul(Q[P->pb[8]],Q[P->pb[8]],off9); }
                for(int k9=1;k9<4;k9++){ int b9=P->pb[k9]; if(b9<0) continue;
                    qaxis(off9,0,1,0,0.42f*sw9); qaxis(tq9,1,0,0,0.16f*sw9);
                    qmul(off9,off9,tq9); qmul(Q[b9],Q[b9],off9); } }
            else if((DOOR_FIRES||g_mode==MODE_ANIM)&&GM.wKind>=WPN_PISTOL&&GM.wCd>0.f&&g_dnGunP&&P->pbOK>=10){
                float u9=1.f-GM.wCd/WPN_CD[GM.wKind]; if(u9<0.f) u9=0.f;
                if(u9<0.55f){ PClip*c9=&g_dnGunP->clips[g_dnGun];
                    donorPose(g_dnGunP,g_dnGun,(unsigned)(u9/0.55f*(float)(c9->frames-1)),P,Q); } }

            /* ---- BOARD FLIP, layered on the baked animation ----
             * flipDeg spins the deck about its long axis (kick/heel, opposite directions) and
             * shovDeg about its vertical (shove-it). The physics already tracks both, so the
             * three buttons produce three visibly different moves from one clip. */
            if(g_skate && !g_procRig && g_boardRoot>=0 && (SK.flipDeg!=0.f || SK.shovDeg!=0.f)){
                static int fa=-1;
                if(fa<0){ const char*e=getenv("REVA_FLIPAXIS"); fa=e?atoi(e):2; }
                float qf[4],qs[4],qc[4];
                float ax=(fa==0)?1.f:0.f, ay=(fa==1)?1.f:0.f, az=(fa==2)?1.f:0.f;
                qaxis(qf,ax,ay,az,SK.flipDeg*0.0174532925f);
                qaxis(qs,0.f,1.f,0.f,SK.shovDeg*0.0174532925f);
                qmul(qc,qf,qs);
                qmul(Q[g_boardRoot],Q[g_boardRoot],qc);
            }
            pInit=1;
            /* ================= PROCEDURAL SKATE POSE ================================
             * The clip supplies a base; this layer supplies the skating. Every rotation is
             * applied in the LOCAL space of its bone on top of the sampled pose -- the same
             * convention the crowd procedural dance layer uses -- so the result reads as her
             * own animation crouched, tucked and reaching, rather than as a replacement.
             *
             * The single biggest correction here is STANCE. A skater does not face the way
             * they are travelling: they stand across the deck and look over their shoulder.
             * Her body is yawed off the direction of travel (see SK_STANCE where the model
             * matrix is built) and the spine and neck twist back toward the nose, which is
             * what actually makes her read as riding rather than as being carried.
             */
            if(g_skate && g_procRig && !g_authored && P->pbOK>0){
                /* ---- 1. WHAT POSE SHOULD SHE BE IN? An action, if one is playing, otherwise
                 * whatever the state tree says. Actions win because they are deliberate. ---- */
                static float pushPh=0.f; static Spring ch[12];
                static int wasAir=0, chInit=0;
                SkPose tgt;
                if(g_act){
                    g_actT+=(float)dt;
                    while(g_act&&g_actT>=g_act[g_actI].dur){
                        g_actT-=g_act[g_actI].dur; g_actI++;
                        if(g_actI>=g_actN){ g_act=NULL; break; } } }
                if(g_act) tgt=*g_act[g_actI].p;
                else switch(g_skState){
                    case SK_IDLE:   tgt=PZ_STAND; pushPh=0.f; break;
                    case SK_PUSH: {
                        /* the kick is its own little loop: plant, sweep back, swing home */
                        pushPh+=(float)dt*1.75f;
                        float pc=pushPh-floorf(pushPh);
                        if(pc<0.55f) poseLerp(&tgt,&PZ_RECOV,&PZ_PLANT,pc/0.55f);
                        else         poseLerp(&tgt,&PZ_PLANT,&PZ_RECOV,(pc-0.55f)/0.45f);
                    } break;
                    case SK_CARVE:  tgt=PZ_CARVE;  break;
                    case SK_GRIND:  tgt=PZ_GRIND;  break;
                    case SK_MANUAL: tgt=PZ_MANUAL; break;
                    case SK_AIR:    tgt=PZ_TUCK;   break;
                    default:        tgt=PZ_ROLL;   break; }
                /* holding A loads the ollie: blend toward the deep crouch by how much is stored */
                if(g_skOllieChg>0.01f){ SkPose base=tgt; poseLerp(&tgt,&base,&PZ_LOAD,g_skOllieChg); }
                if(SK.grabbing){ tgt.armFwd+=0.85f; tgt.elbow+=0.75f; tgt.knee+=0.22f; }
                /* the frame she touches down, play the absorb */
                if(wasAir&&!SK.air) actPlay(ACT_LAND,(int)(sizeof ACT_LAND/sizeof ACT_LAND[0]));
                wasAir=SK.air;

                /* ---- 2. SPRING EVERY CHANNEL. The stiffness table IS the lead-and-lag: hips
                 * and feet are heavy and quick, the spine looser, the head and arms loosest, so
                 * motion reaches the extremities late without a line of code saying so. ---- */
                static const float STF[12]={220.f,190.f,190.f,130.f,130.f, 95.f, 95.f, 95.f, 75.f,300.f,300.f,170.f};
                static const float DMP[12]={ 23.f, 21.f, 21.f, 17.f, 17.f, 13.f, 13.f, 13.f, 12.f, 32.f, 32.f, 21.f};
                const float*tp=(const float*)&tgt;
                if(!chInit){ chInit=1; for(int k=0;k<12;k++){ ch[k].x=tp[k]; ch[k].v=0.f; } }
                for(int k=0;k<12;k++) springTo(&ch[k],tp[k],STF[k],DMP[k],(float)dt);
                { float*po=(float*)&g_pose; for(int k=0;k<12;k++) po[k]=ch[k].x; }
                SkPose pz=g_pose;

                /* ---- 3. APPLY IT TO THE RIG ---- */
                float off[4],tq[4];
                if(P->pb[0]>=0){
                    float isc0=(P->scale>1e-6f)?(1.f/P->scale):1.f;
                    T[P->pb[0]][1] -= pz.hipDrop*isc0; }   /* low hips are why knees bend */
                for(int side=0;side<2;side++){
                    int th=P->pb[side?14:12], kn=P->pb[side?15:13];
                    float sgn=side?-1.f:1.f;
                    if(th>=0){ memcpy(Q[th],P->nodes[th].q,16);
                        qaxis(off,0,0,1,sgn*0.42f);          /* stance width, along the deck */
                        qaxis(tq,1,0,0,pz.thigh);
                        qmul(off,off,tq); qmul(Q[th],Q[th],off); }
                    if(kn>=0){ memcpy(Q[kn],P->nodes[kn].q,16);
                        qaxis(off,1,0,0,pz.knee); qmul(Q[kn],Q[kn],off); } }
                for(int k2=0;k2<3;k2++){ int b2=P->pb[1+k2]; if(b2<0) continue;
                    qaxis(off,1,0,0,pz.spineFold*0.5f);
                    qaxis(tq,0,1,0,pz.spineTwist*0.42f - g_lx*0.10f);
                    qmul(off,off,tq); qmul(Q[b2],Q[b2],off); }
                if(P->pb[5]>=0){ qaxis(off,0,1,0,pz.headTurn);
                                 qaxis(tq,1,0,0,-pz.spineFold*0.30f);
                                 qmul(off,off,tq); qmul(Q[P->pb[5]],Q[P->pb[5]],off); }
                /* the hero rides the SAME de-twinned arms as the crew -- this block
                 * used to be a second copy of them, which is how a fix reaches only
                 * half a cast. sgn/am/the reset all live in skateArms now. */
                skateArms(P,&pz,Q,0.37f,(float)tsec);
            }
            if(GAME_ON) rideHeroPose(P,Q,T,-revaGround/(P->scale>1e-6f?P->scale:1.f));   /* (11-05) standing on what she rides; (11-06) her rest floor is revaGround over her feet's height */
            /* ⭐ THE WILLIAMS LAYER ON THE HERO. It needed a frame clock and there already
             * was one: g_chDt, which H1's chains read. FK once to MEASURE the stride
             * (contact vs passing is a distance between the feet), overlay, then let the
             * FK below run on the corrected pose -- and chainSim after that, so her hair
             * swings with the bounce instead of against it. */
            if(!g_wmsOff&&!(g_mode==MODE_ANIM&&g_scnT>=0.f&&g_labRM&&(g_labClip>=0||g_labDnC>=0))){ static WmsState hw;   /* ⭐ a scene's z_rm_ clip owns the root -- no bounce on it */
                bodyFK(P,T,Q,S,world);
                wmsApply(P,T,Q,world,&hw,(float)g_chDt,g_wms); }
            /* a face is picture, not rules, and her pose is in the receipt: so it goes on after the receipt's had the pose without it */
            { Face*hf9=(g_faceN&&GAME_ON)?faceOf(-1):NULL; if(hf9){ bodyFK(P,T,Q,S,world); g_shDumpTag="pose"; simHashMix((const float*)world,P->nNodes*16); faceLayer(hf9,P,Q); facePosed(hf9,P,world); } g_faceHeroHashed=hf9!=NULL; }
            bodyFK(P,T,Q,S,world);
            stretchTick(P,world);  /* ⭐ drapes held between two limbs (09-29) */
            chainSim(P,world,0);   /* H1: the hero's tails/hair swing after the pose is solved */
            if(!g_faceHeroHashed){ g_shDumpTag="pose"; simHashMix((const float*)world,P->nNodes*16); }   /* ⭐ the receipt: her solved pose */

            /* ================= FOOT IK: HER FEET BELONG TO THE BOARD ==================
             * Posing the legs got the stance right but left the feet only APPROXIMATELY on
             * the deck -- the moment the board tilts or she pushes, an authored pose drifts.
             * So the feet are solved onto real targets instead.
             *
             * Two things make this tractable. First, in skate mode her model matrix carries
             * the board's FULL tilt (not a fraction of it), so her model space IS the board's
             * frame and a deck-relative target is a CONSTANT rather than something that has
             * to be re-derived every frame. Second, her vertical placement is set directly
             * from the rest-pose sole height rather than from the posed foot, which breaks
             * the circular dependency -- otherwise her height depends on the IK, which
             * depends on the targets, which depend on her height.
             *
             * The solve is the standard analytic two-bone: set the knee to the angle that
             * makes the chain reach, then aim the whole limb at the target. Aiming is done in
             * the PARENT's frame (transpose, not inverse -- the basis is orthonormal), so no
             * matrix inversion is needed anywhere.
             */
            if(g_skate && g_procRig && !g_authored && P->pbOK>0 && trBone[2]>=0 && trBone[3]>=0){
                const float STANCE=g_stanceRad;
                /* the board's axes as seen from her model space: constant, because her body
                 * is yawed a fixed amount off the deck */
                float bd[3]={sinf(-STANCE),0.f,cosf(-STANCE)};
                float br[3]={sinf(-STANCE+1.5708f),0.f,cosf(-STANCE+1.5708f)};
                float isc=(P->scale>1e-6f)?(1.f/P->scale):1.f;   /* world units -> model units */
                /* REST-POSE MEASUREMENTS, taken once. The IK drives the ANKLE bone, and an
                 * ankle is not a sole -- targeting the ankle at deck height buries her feet in
                 * the board by the height of her own ankle. The right target height is simply
                 * where the ankle sits in the rest pose, because her model height is already
                 * set so that the rest SOLE lands on the deck. */
                static float restAnkY[2]={0.f,0.f}; static float restUp[2][3];
                static float restShoY=0.f;
                static int restInit=0;
                if(!restInit){
                    int lf=P->pb[12],rf=P->pb[14];
                    (void)lf; (void)rf;
                    M4*rw=(M4*)malloc((size_t)P->nNodes*sizeof(M4));
                    if(rw){
                        for(unsigned i9=0;i9<P->nNodes;i9++){
                            M4 lc9; m4_trs(lc9,P->nodes[i9].t,P->nodes[i9].q,P->nodes[i9].s);
                            if(P->nodes[i9].parent<0) memcpy(rw[i9],lc9,64);
                            else m4_mul(rw[i9],rw[P->nodes[i9].parent],lc9); }
                        int fts[2]={trBone[2],trBone[3]};
                        restShoY=(P->pb[7]>=0)?rw[P->pb[7]][13]:0.f;
                        for(int lg9=0;lg9<2;lg9++){
                            float*M9=rw[fts[lg9]];
                            restAnkY[lg9]=M9[13];
                            float v9[3]={M9[1],M9[5],M9[9]};      /* M^T * (0,1,0) */
                            float vl9=sqrtf(v9[0]*v9[0]+v9[1]*v9[1]+v9[2]*v9[2]);
                            if(vl9<1e-5f) vl9=1.f;
                            restUp[lg9][0]=v9[0]/vl9; restUp[lg9][1]=v9[1]/vl9; restUp[lg9][2]=v9[2]/vl9; }
                        free(rw); restInit=1; } }
                float bx=P->cx, bz=P->cz;

                /* The push cycle now lives in the pose layer, which is the single source of
                 * truth for where a foot should be; the IK just puts it there. */

                float ftF[3], tgtB[3];
                if(SK.onFoot){
                    /* ---- A PROCEDURAL WALK CYCLE -------------------------------------------
                     * The feet are not playing an animation; they are being placed. Each foot
                     * runs half a cycle out of phase with the other: the stance half stays put
                     * and slides backwards under her (which is what carries her forward), the
                     * swing half lifts and reaches ahead.
                     *
                     * The phase advances with GROUND COVERED rather than with time, so the
                     * planted foot is stationary in the world at any speed -- that is the whole
                     * trick to feet that do not skate, and it means walking and running are one
                     * continuous behaviour rather than two clips with a blend between them. */
                    float sf=sinf(-(g_stanceRad+1.5708f)), cf2=cosf(-(g_stanceRad+1.5708f));
                    float fw[3]={sf,0.f,cf2};
                    float rt[3]={cosf(-(g_stanceRad+1.5708f)+1.5708f),0.f,-sinf(-(g_stanceRad+1.5708f)+1.5708f)};
                    float stride=(0.34f+SK.spd*0.055f);          /* longer strides at speed */
                    float lift  =(0.10f+SK.spd*0.022f);
                    float sideO = 0.16f;
                    float legs[2][3];
                    for(int lg2=0;lg2<2;lg2++){
                        float ph=SK.gaitPh+(lg2?0.5f:0.f);
                        ph-=floorf(ph);
                        float along=cosf(ph*6.2831853f)*stride*0.5f;
                        float up2  =(ph>0.5f)?(sinf((ph-0.5f)*6.2831853f)*lift):0.f;
                        float so=(lg2?-sideO:sideO);
                        legs[lg2][0]=bx+(fw[0]*along+rt[0]*so)*isc;
                        legs[lg2][1]=restAnkY[lg2]+up2*isc;
                        legs[lg2][2]=bz+(fw[2]*along+rt[2]*so)*isc;
                    }
                    memcpy(ftF ,legs[0],12);
                    memcpy(tgtB,legs[1],12);
                } else {
                    float f0[3]={bx+bd[0]*g_pose.footFront*isc, restAnkY[0], bz+bd[2]*g_pose.footFront*isc};
                    float b0[3]={bx+bd[0]*g_pose.footBack*isc,  restAnkY[1], bz+bd[2]*g_pose.footBack*isc};
                    float gnd[3]={bx+br[0]*0.40f*isc+bd[0]*g_pose.footBack*isc,
                                  restAnkY[1]-0.16f*isc,
                                  bz+br[2]*0.40f*isc+bd[2]*g_pose.footBack*isc};
                    memcpy(ftF,f0,12);
                    for(int k7=0;k7<3;k7++) tgtB[k7]=b0[k7]*(1.f-g_pose.backOff)+gnd[k7]*g_pose.backOff;
                    tgtB[1]+= sinf(g_pose.backOff*3.14159f)*0.05f*isc*(g_skState==SK_PUSH?1.f:0.f);
                }

                /* ---- ARM TARGETS. Her rest pose is measurably asymmetric -- the right hand
                 * sits 19 units higher and 67 across from the left -- so posing the arms as
                 * offsets from rest can never come out even, whatever axis or sign is used.
                 * Solving them to targets that are symmetric BY CONSTRUCTION removes the
                 * question entirely. She rides across the deck, so her arms reach along it:
                 * one hand toward the nose, one toward the tail, both at the same height. */
                /* MEASURED: her shoulder-to-hand chain is ~44 model units. Targeting 26 out
                 * left the solver correctly folding her elbows up at her chest, which is why
                 * she looked like she had her arms crossed. Reach ~0.8 of the chain. */
                float armR=(0.56f+g_pose.armSpread*0.20f)*isc;
                float armS=(0.10f-g_pose.armFwd*0.18f)*isc;      /* out in front of her chest */
                float armY=restShoY-(0.30f+g_pose.elbow*0.16f)*isc;   /* hands at chest, not shoulder */
                float hL[3]={bx+bd[0]*armR+br[0]*armS, armY, bz+bd[2]*armR+br[2]*armS};
                float hR[3]={bx-bd[0]*armR+br[0]*armS, armY, bz-bd[2]*armR+br[2]*armS};

                /* ARM IK is OFF by default: it makes the hands symmetric by construction,
                 * which the numbers confirm, but the result looked worse on the glass
                 * and eyes decide. REVA_ARMIK=1 solves the arms; otherwise only the legs
                 * are solved and the arms keep the pose-layer rotation. */
                static int armIK=-1;
                if(armIK<0){ const char*e=getenv("REVA_ARMIK"); armIK=(e&&e[0]=='1')?1:0; }
                int nCh=armIK?4:2;
                int legTh[4]={P->pb[12],P->pb[14],P->pb[7], P->pb[10]};
                int legKn[4]={P->pb[13],P->pb[15],P->pb[8], P->pb[11]};
                int legFt[4]={trBone[2],trBone[3],trBone[0],trBone[1]};
                const float*legTgt[4]={ftF,tgtB,hL,hR};   /* regular stance: left foot leads */

                /* ---- pass 1: the knee angle that makes the chain reach ---- */
                for(int lg=0;lg<nCh;lg++){
                    int th=legTh[lg],kn=legKn[lg],ft=legFt[lg];
                    if(th<0||kn<0||ft<0) continue;
                    float*H=&world[th][12],*K=&world[kn][12],*F=&world[ft][12];
                    float L1=sqrtf((K[0]-H[0])*(K[0]-H[0])+(K[1]-H[1])*(K[1]-H[1])+(K[2]-H[2])*(K[2]-H[2]));
                    float L2=sqrtf((F[0]-K[0])*(F[0]-K[0])+(F[1]-K[1])*(F[1]-K[1])+(F[2]-K[2])*(F[2]-K[2]));
                    if(L1<1e-4f||L2<1e-4f) continue;
                    float cur=sqrtf((F[0]-H[0])*(F[0]-H[0])+(F[1]-H[1])*(F[1]-H[1])+(F[2]-H[2])*(F[2]-H[2]));
                    const float*Tg=legTgt[lg];
                    float d=sqrtf((Tg[0]-H[0])*(Tg[0]-H[0])+(Tg[1]-H[1])*(Tg[1]-H[1])+(Tg[2]-H[2])*(Tg[2]-H[2]));
                    float lo=fabsf(L1-L2)+1e-3f, hi=L1+L2-1e-3f;
                    if(d<lo)d=lo;
                    if(d>hi)d=hi;
                    if(cur<lo)cur=lo;
                    if(cur>hi)cur=hi;
                    float cA=(L1*L1+L2*L2-cur*cur)/(2.f*L1*L2);
                    float cB=(L1*L1+L2*L2-d*d)/(2.f*L1*L2);
                    if(cA>1.f)cA=1.f;
                    if(cA<-1.f)cA=-1.f;
                    if(cB>1.f)cB=1.f;
                    if(cB<-1.f)cB=-1.f;
                    float r7[4]; qaxis(r7,1,0,0,acosf(cA)-acosf(cB));
                    qmul(Q[kn],Q[kn],r7);
                }
                for(unsigned i7=0;i7<P->nNodes;i7++){ M4 lc; m4_trs(lc,T[i7],Q[i7],S[i7]);
                    if(P->nodes[i7].parent<0) memcpy(world[i7],lc,64);
                    else m4_mul(world[i7],world[P->nodes[i7].parent],lc); }

                /* ---- pass 2: aim the limb, in the parent's frame ---- */
                for(int lg=0;lg<nCh;lg++){
                    int th=legTh[lg],ft=legFt[lg];
                    if(th<0||ft<0) continue;
                    float*H=&world[th][12],*F=&world[ft][12];
                    const float*Tg=legTgt[lg];
                    float a7[3]={F[0]-H[0],F[1]-H[1],F[2]-H[2]};
                    float b7[3]={Tg[0]-H[0],Tg[1]-H[1],Tg[2]-H[2]};
                    float al=sqrtf(a7[0]*a7[0]+a7[1]*a7[1]+a7[2]*a7[2]);
                    float bl=sqrtf(b7[0]*b7[0]+b7[1]*b7[1]+b7[2]*b7[2]);
                    if(al<1e-5f||bl<1e-5f) continue;
                    for(int k7=0;k7<3;k7++){ a7[k7]/=al; b7[k7]/=bl; }
                    int par=P->nodes[th].parent;
                    if(par>=0){
                        float*M7=world[par];
                        float ap[3]={M7[0]*a7[0]+M7[1]*a7[1]+M7[2]*a7[2],
                                     M7[4]*a7[0]+M7[5]*a7[1]+M7[6]*a7[2],
                                     M7[8]*a7[0]+M7[9]*a7[1]+M7[10]*a7[2]};
                        float bp[3]={M7[0]*b7[0]+M7[1]*b7[1]+M7[2]*b7[2],
                                     M7[4]*b7[0]+M7[5]*b7[1]+M7[6]*b7[2],
                                     M7[8]*b7[0]+M7[9]*b7[1]+M7[10]*b7[2]};
                        float apl=sqrtf(ap[0]*ap[0]+ap[1]*ap[1]+ap[2]*ap[2]);
                        float bpl=sqrtf(bp[0]*bp[0]+bp[1]*bp[1]+bp[2]*bp[2]);
                        if(apl<1e-5f||bpl<1e-5f) continue;
                        for(int k7=0;k7<3;k7++){ ap[k7]/=apl; bp[k7]/=bpl; }
                        memcpy(a7,ap,12); memcpy(b7,bp,12);
                    }
                    float arc[4],nq[4];
                    qarc(arc,a7,b7);
                    qmul(nq,arc,Q[th]);           /* parent-frame rotation: PRE-multiply */
                    memcpy(Q[th],nq,16);
                }
                for(unsigned i7=0;i7<P->nNodes;i7++){ M4 lc; m4_trs(lc,T[i7],Q[i7],S[i7]);
                    if(P->nodes[i7].parent<0) memcpy(world[i7],lc,64);
                    else m4_mul(world[i7],world[P->nodes[i7].parent],lc); }

                /* ---- pass 3: LEVEL THE SOLES. Position alone is not contact -- a foot solved
                 * to the right point still points wherever the leg left it, which read as her
                 * toes hanging off the deck. In her model space the deck is level (that is the
                 * whole point of carrying the board's full tilt), so "flat on the board" is
                 * simply "the same orientation the sole has at rest". Take the local axis that
                 * pointed up in the rest pose, and rotate it back to up. */
                {
                  for(int lg=0;lg<2;lg++){   /* soles only: hands are not levelled */
                      int ft=legFt[lg]; if(ft<0) continue;
                      int par=P->nodes[ft].parent; if(par<0) continue;
                      float*M8=world[ft];
                      float u8[3]={M8[0]*restUp[lg][0]+M8[4]*restUp[lg][1]+M8[8]*restUp[lg][2],
                                   M8[1]*restUp[lg][0]+M8[5]*restUp[lg][1]+M8[9]*restUp[lg][2],
                                   M8[2]*restUp[lg][0]+M8[6]*restUp[lg][1]+M8[10]*restUp[lg][2]};
                      float ul=sqrtf(u8[0]*u8[0]+u8[1]*u8[1]+u8[2]*u8[2]); if(ul<1e-5f) continue;
                      u8[0]/=ul; u8[1]/=ul; u8[2]/=ul;
                      float w8[3]={0.f,1.f,0.f};
                      float*MP=world[par];
                      float up8[3]={MP[0]*u8[0]+MP[1]*u8[1]+MP[2]*u8[2],
                                    MP[4]*u8[0]+MP[5]*u8[1]+MP[6]*u8[2],
                                    MP[8]*u8[0]+MP[9]*u8[1]+MP[10]*u8[2]};
                      float wp8[3]={MP[0]*w8[0]+MP[1]*w8[1]+MP[2]*w8[2],
                                    MP[4]*w8[0]+MP[5]*w8[1]+MP[6]*w8[2],
                                    MP[8]*w8[0]+MP[9]*w8[1]+MP[10]*w8[2]};
                      float a8=sqrtf(up8[0]*up8[0]+up8[1]*up8[1]+up8[2]*up8[2]);
                      float b8=sqrtf(wp8[0]*wp8[0]+wp8[1]*wp8[1]+wp8[2]*wp8[2]);
                      if(a8<1e-5f||b8<1e-5f) continue;
                      for(int k8=0;k8<3;k8++){ up8[k8]/=a8; wp8[k8]/=b8; }
                      float arc8[4],nq8[4];
                      qarc(arc8,up8,wp8);
                      qmul(nq8,arc8,Q[ft]);
                      memcpy(Q[ft],nq8,16); } }
                for(unsigned i8=0;i8<P->nNodes;i8++){ M4 lc8; m4_trs(lc8,T[i8],Q[i8],S[i8]);
                    if(P->nodes[i8].parent<0) memcpy(world[i8],lc8,64);
                    else m4_mul(world[i8],world[P->nodes[i8].parent],lc8); }

                /* ---- POSE DIAGNOSTIC (REVA_POSEDUMP=1). Prints the solved skeleton in BOARD
                 * coordinates -- forward along the deck, right across it, up off it -- because
                 * "looks wrong" is not something I can act on and a signed number is. */
                { static int dumped=-1;
                  if(dumped<0){ const char*e=getenv("REVA_POSEDUMP"); dumped=(e&&e[0]=='1')?0:1; }
                  if(dumped==0){ dumped=1;
                    float upv[3]={0.f,1.f,0.f};
                    struct { const char*nm; int a,b,c; } CH[4]={
                        {"LEG L",P->pb[12],P->pb[13],trBone[2]},
                        {"LEG R",P->pb[14],P->pb[15],trBone[3]},
                        {"ARM L",P->pb[7], P->pb[8], trBone[0]},
                        {"ARM R",P->pb[10],P->pb[11],trBone[1]} };
                    printf("---- POSE DUMP (board frame: fwd=along deck, rgt=across, up) ----\n");
                    for(int c9=0;c9<4;c9++){
                        if(CH[c9].a<0||CH[c9].b<0||CH[c9].c<0){ printf("  %s  MISSING\n",CH[c9].nm); continue; }
                        float*A9=&world[CH[c9].a][12];
                        float*B9=&world[CH[c9].b][12];
                        float*C9=&world[CH[c9].c][12];
                        /* the mid joint's offset from the straight line root->tip */
                        float ac[3]={C9[0]-A9[0],C9[1]-A9[1],C9[2]-A9[2]};
                        float acl=sqrtf(ac[0]*ac[0]+ac[1]*ac[1]+ac[2]*ac[2]); if(acl<1e-5f) acl=1.f;
                        float u9[3]={ac[0]/acl,ac[1]/acl,ac[2]/acl};
                        float ab[3]={B9[0]-A9[0],B9[1]-A9[1],B9[2]-A9[2]};
                        float d9=ab[0]*u9[0]+ab[1]*u9[1]+ab[2]*u9[2];
                        float bend[3]={ab[0]-u9[0]*d9, ab[1]-u9[1]*d9, ab[2]-u9[2]*d9};
                        float bf=bend[0]*bd[0]+bend[1]*bd[1]+bend[2]*bd[2];
                        float brr=bend[0]*br[0]+bend[1]*br[1]+bend[2]*br[2];
                        float bu=bend[0]*upv[0]+bend[1]*upv[1]+bend[2]*upv[2];
                        /* and where the tip ended up, relative to the deck centre */
                        float tf=(C9[0]-bx)*bd[0]+(C9[2]-bz)*bd[2];
                        float tr=(C9[0]-bx)*br[0]+(C9[2]-bz)*br[2];
                        printf("  %s  bend fwd%+7.2f rgt%+7.2f up%+7.2f   tip fwd%+7.2f rgt%+7.2f y%+8.2f\n",
                               CH[c9].nm, bf,brr,bu, tf,tr, C9[1]);
                    }
                    printf("  targets: front fwd%+7.2f   back fwd%+7.2f   backOff %.2f\n",
                           g_pose.footFront*isc, g_pose.footBack*isc, g_pose.backOff);
                    printf("  ankle rest y: L%.2f R%.2f   hipDrop %.3f\n",restAnkY[0],restAnkY[1],g_pose.hipDrop);
                    fflush(stdout); } }
            }
            /* BOLT LIFECYCLE: track her hand, burn the timer, evict at zero */
            if(g_boltT>0){ g_boltT-=(float)dt;
                if(g_boltVictim>=0){ g_boltTo[0]=slots[g_boltVictim].x; g_boltTo[1]=g_slotAlt[g_boltVictim]+1.5f; g_boltTo[2]=slots[g_boltVictim].z; }
                if(g_boltT<=0&&g_boltVictim>=0){
                    g_slotHide[g_boltVictim]=1; g_slotBan[g_boltVictim]=1;
                    dngAdd(slots[g_boltVictim].x,slots[g_boltVictim].z);   /* the bolt scares the floor too */
                    Pack*VP=&packs[slots[g_boltVictim].pack];
                    FILE*ev=fopen("evicted.txt","a");
                    if(ev){ fprintf(ev,"%.0f  %s\n",unix_now(),VP->label); fclose(ev); }
                    printf("EVICTED: %s (slot %d) -- for not coming correct\n",VP->label,g_boltVictim); fflush(stdout);
                    if(g_lockTgt==g_boltVictim) g_lockTgt=-1;
                    g_boltVictim=-1; } }
            /* THE HOST goes round her own party, meeting and dancing with each
             * guest: dance with someone, then stride to the next (statics hoisted above) */
            if(g_manual&&!g_cabAttract){   /* F310 DRIVE: camera-relative run/fly, A up / B down */
                float lxn=fabsf(g_lx)>0.16f?g_lx:0.f, lyn=fabsf(g_ly)>0.16f?g_ly:0.f;
                float mag=hypotf(lxn,lyn); if(mag>1.f)mag=1.f;
                if(g_btnA&&!GAME_ON) g_manAlt=fminf(30.f,g_manAlt+(float)dt*7.5f);   /* in the zombie cab A is the squirt, never lift */
                if(g_btnX) g_manAlt=fmaxf(0.f,g_manAlt-(float)dt*9.f);
                float top=(g_manAlt>0.4f)?12.5f:8.6f;
                if(GAME_ON&&g_tune.heroSpd>0.f) top=g_tune.heroSpd;
                /* RT LOCK-ON: acquire ONCE on the PRESS (accidental bans came from the
                 * continuous re-scan); altitude-gated so sky judgments require flying up */
                { static float rtPrev=0;
                  if(g_rt>0.45f&&rtPrev<=0.45f){ int best=-1; float bs=1e9f;
                    for(int c8=0;c8<NS;c8++){ if(g_slotHide[c8]) continue;   /* banned/quarantined ARE evictable (the unbannable-hippo fix) */
                        float dx8=slots[c8].x-g_rvx, dz8=slots[c8].z-g_rvz;
                        float d8=hypotf(dx8,dz8); if(d8>28.f||d8<0.8f) continue;
                        if(fabsf(g_slotAlt[c8]-g_manAlt)>4.5f) continue;
                        float a8=fabsf(fmodf(atan2f(dx8,dz8)-rvH+9.42477f,6.28318f)-3.14159f);
                        if(a8>1.15f) continue;
                        float sc8=d8+a8*9.f;
                        if(sc8<bs){ bs=sc8; best=c8; } }
                    g_lockTgt=best; }
                  else if(g_rt<=0.35f) g_lockTgt=-1;
                  rtPrev=g_rt; }
                /* B: THE EVICTION BOLT (sith protocol). The narrative mode's cast is not
                 * evictable -- bolting your quest-giver is a softlock, not a feature. */
                if(g_btnB&&!g_btnBPrev&&g_lockTgt>=0&&g_boltT<=0&&!DOOR_NO_BOLT){
                    g_boltT=0.95f; g_boltVictim=g_lockTgt;
                    g_boltTo[0]=slots[g_boltVictim].x; g_boltTo[1]=g_slotAlt[g_boltVictim]+1.5f; g_boltTo[2]=slots[g_boltVictim].z; }
                g_btnBPrev=g_btnB;
                /* ⭐ SNAP (the zombie prototype, 11-02): no ease in the zombie cab -- she is at speed the frame the stick moves and stopped the frame it
                 * lets go, and she goes where the stick says NOW while her body catches up at hero_turn. Dead, she stands. */
                int snap9=GAME_ON&&g_tune.heroSnap>0.f;
                if(GAME_ON&&GM.stage==3) mag=0.f;
                if(GAME_ON&&g_tune.heroSpd<0.f) mag=0.f;   /* she doesn't take the stick: it's a menu's, or a conversation's */
                if(snap9) g_manSpd=mag*top;
                else if(GAME_ON&&g_tune.heroAccel>0.f){ const float d9=mag*top-g_manSpd, m9=g_tune.heroAccel*(float)dt; g_manSpd+=d9>m9?m9:(d9<-m9?-m9:d9); }   /* (11-06) at a steady rate: E->tune->heroAccel */
                else g_manSpd+=(mag*top-g_manSpd)*fminf(1,(float)dt*4.f);
                if(g_kbActive&&!GAME_ON){   /* (the zombie cab's keys go through the stick path below: its lens is fixed) */
                    /* KEYBOARD DRIVE (the WASD pass): the pad steers like a tank --
                     * a held left stick means "keep turning left", which on A/D would walk
                     * her in circles. So the MOUSE turns her and WASD moves in her frame,
                     * with A/D as real strafes. forward=(sin,cos), right=(cos,-sin). */
                    rvH+=g_mdx*0.0042f;
                    float fwd=-lyn, str=-lxn;   /* W: g_ly=-1 -> +fwd.  D: g_lx=-1 -> +right */
                    g_rvx+=(sinf(rvH)*fwd+cosf(rvH)*str)*g_manSpd*(float)dt;
                    g_rvz+=(cosf(rvH)*fwd-sinf(rvH)*str)*g_manSpd*(float)dt; }
                else {
                    /* M0: in the zombie cab the lens is FIXED (case 22 looks down -z), so the stick is the SCREEN -- up goes up the
                     * picture (-z), right goes right (+x) -- and she turns fast; everywhere else it steers from her heading. */
                    float mvH9=rvH;
                    if(mag>0.05f){ float des=GAME_ON?atan2f(-lxn,lyn)+g_tune.stickYaw:rvH+atan2f(lxn,-lyn);
                        float dh6=fmodf(des-rvH+9.42477f,6.28318f)-3.14159f;
                        if(snap9){ float mt9=g_tune.heroTurn*0.0174533f*(float)dt; rvH+=(dh6>mt9)?mt9:((dh6<-mt9)?-mt9:dh6);
                            if(rvH>3.14159f)rvH-=6.28318f;
                            if(rvH<-3.14159f)rvH+=6.28318f;
                            mvH9=des; }
                        else { rvH+=dh6*fminf(1,(float)dt*(GAME_ON?10.f:3.4f)); mvH9=rvH; } }
                    g_rvx+=sinf(mvH9)*g_manSpd*(float)dt; g_rvz+=cosf(mvH9)*g_manSpd*(float)dt; }
                rvAlt=g_skate?0.f:g_manAlt;   /* skate mode: the pad cannot lift her off the park */
                { float pT2=(g_manAlt>0.4f?0.5f:(g_manSpd>4.0f?0.20f:0.f))*fminf(1.f,g_manSpd/11.f+0.35f);
                  rvPitch+=(pT2-rvPitch)*fminf(1,(float)dt*(snap9?8.f:2.f)); }
                rvRoll+=(-(GAME_ON?0.f:lxn)*0.30f*(g_manAlt>0.4f?1.f:0.3f)-rvRoll)*fminf(1,(float)dt*3.f);
                if(g_heroFaceOn){ g_heroFaceOn=0; const float dh9=fmodf(g_heroFaceY-rvH+9.42477f,6.28318f)-3.14159f; rvH+=dh9*fminf(1,(float)dt*10.f); }   /* E->heroFace */
                rvHW=rvH; g_rvCarve=0; g_rvTravel=0; g_rvArrived=1; g_rvHead=rvH; g_rvAlt=rvAlt;
            } else if(GAME_ON){   /* ⭐ THE ZOMBIE GAME'S M0 (11-01): THE CAB'S ATTRACT HOST. Every door runs itself until the pad
                 * is taken, and this door's host was still the DANCE host -- she walked toward whoever the dance floor's director
                 * was filming and stood pinned in the yard's corner. Now she plays the cab: the dead inside 7 u are faced and
                 * squirted (g_cabFire, pulled by the cab's A), otherwise she walks -- runs past 18 u -- to the nearest
                 * neighbour still standing. Same speed and clip ladder as a thumb on the stick (g_manSpd). */
                g_game->host(&rvH,(float)dt);
                if(g_heroFaceOn){ g_heroFaceOn=0; const float dh9=fmodf(g_heroFaceY-rvH+9.42477f,6.28318f)-3.14159f; rvH+=dh9*fminf(1,(float)dt*10.f); }   /* E->heroFace */
                rvAlt=0.f; rvPitch*=1.f-fminf(1,(float)dt*4.f); rvRoll*=1.f-fminf(1,(float)dt*4.f);
                rvHW=rvH; g_rvCarve=0; g_rvTravel=0; g_rvArrived=1; g_rvHead=rvH; g_rvAlt=rvAlt;
            } else {
            if(CAMD.shot==9&&rvTgt!=CAMD.tgt){ rvTgt=CAMD.tgt; rvState=1; rvT=180; g_rvArrived=0;   /* NO teleport, no equalizing -- the journey is the show */
                Slot*ts=&slots[rvTgt];
                /* destination: the orbit's mid-azimuth spot beside the model, inside the lens */
                rvAm=CAMD.seed+0.45f+0.75f;
                rvTx=ts->x+sinf(rvAm)*2.0f; rvTz=ts->z+cosf(rvAm)*2.0f;
                /* SPEED TIERS by trip length (fly > ski > run > walk > dance-move) */
                float bd=hypotf(rvTx-g_rvx,rvTz-g_rvz);
                rvMode = (bd>140.f&&!g_skate)?4 : bd>70.f?3 : bd>28.f?2 : bd>10.f?1 : 0;
                rvSpd  = rvMode==4?13.f : rvMode==3?8.5f : rvMode==2?6.0f : rvMode==1?3.4f : 2.0f; }
            if(rvTgt>=0){   /* the npc WALKS while she crosses -- the destination must track it
                 * (telemetry: she flew to a stale spot and danced alone 20u from the partner) */
                Slot*ts=&slots[rvTgt];
                rvTx=ts->x+sinf(rvAm)*2.0f; rvTz=ts->z+cosf(rvAm)*2.0f;
                if(rvState==0&&hypotf(rvTx-g_rvx,rvTz-g_rvz)>3.5f){ rvState=1; rvSpd=fmaxf(rvSpd,3.4f); } }
            rvT-=dt;
            if(rvState==0){ /* the PARADE owns her itinerary now -- no random re-picks; she
                 * dances with this npc until the director cuts to the next */
                if(rvTgt>=0){ Slot*ts=&slots[rvTgt];   /* face her dance partner */
                    float tgH=atan2f(ts->x-g_rvx,ts->z-g_rvz);
                    float dh2=fmodf(tgH-rvH+9.42477f,6.28318f)-3.14159f; rvH+=dh2*fminf(1,(float)dt*1.5f); } }
            else { float dx=rvTx-g_rvx, dz=rvTz-g_rvz, d3=hypotf(dx,dz);
                float tgH=atan2f(dx,dz);
                float dh2=fmodf(tgH-rvH+9.42477f,6.28318f)-3.14159f; rvH+=dh2*fminf(1,(float)dt*2.4f);
                /* CARVING (as a skier does): the ski/fly tiers weave an S down the
                 * line -- heading oscillates at a distance-locked cadence, she BANKS into each
                 * turn, and the last-stretch approach damps the weave so she still arrives */
                /* r5: LONGER on each side -- long carving traverses, not flutter:
                 * cadence ~4-5s per full S at ski speed, wider swing */
                float wAmp = rvMode==3?0.62f : rvMode==4?0.30f : rvMode==2?0.14f : 0.f;
                wAmp*=fmaxf(0.f,fminf(1.f,(d3-4.f)*0.12f));   /* weave dies fully inside 4u */
                /* APPROACH BRAKING (she got stuck circling a character): at 13u/s her turn radius
                 * (v/omega ~5.4u) exceeded the 1u arrival circle = a permanent orbit. Speed now
                 * shrinks with distance so the turn radius always fits the approach. */
                float vEff=fminf(rvSpd, 0.7f+d3*1.1f);
                rvCph+=(float)dt*vEff*0.155f;
                float hW=rvH+sinf(rvCph)*wAmp;
                g_rvx+=sinf(hW)*vEff*(float)dt; g_rvz+=cosf(hW)*vEff*(float)dt;   /* tiered cruise */
                rvHW=hW; g_rvCarve=sinf(rvCph)*wAmp;
                if(d3<1.3f||rvT<=0){ rvState=0; rvT=7+frnd()*8; g_rvArrived=1; } }   /* first arrival arms the dwell clock */
            /* FLIGHT + LEAN (flying is the fastest tier): altitude only while airborne-tier
             * and still far out; pitch leans her into ski/run/flight, all eased */
            { float dxa=rvTx-g_rvx, dza=rvTz-g_rvz; float d4=hypotf(dxa,dza);
              float altT=(!g_skate&&rvState==1&&rvMode==4&&d4>16.f)?3.4f:0.f;
              float pitT=(rvState==1)?(rvMode==4?0.55f:rvMode==3?0.30f:rvMode==2?0.14f:0.f):0.f;
              float rolT=(rvState==1)?(-sinf(rvCph+0.35f)*(rvMode==3?0.30f:rvMode==4?0.34f:rvMode==2?0.08f:0.f)):0.f;
              rvAlt+=(altT-rvAlt)*fminf(1,(float)dt*1.6f);
              rvPitch+=(pitT-rvPitch)*fminf(1,(float)dt*2.0f);
              rvRoll+=(rolT-rvRoll)*fminf(1,(float)dt*3.0f); }
            if(rvState!=1){ g_rvCarve*=1.f-fminf(1,(float)dt*3.f); rvHW=rvH; }
            g_rvTravel=(rvState==1); g_rvHead=(rvState==1)?rvHW:rvH; g_rvAlt=rvAlt;   /* the director rides with her */
            }   /* end auto-host (manual drive above) */
            if(g_skate){ const float PB=PARK_EXT-2.f;
                if(g_rvx<-PB)g_rvx=-PB;
                if(g_rvx>PB)g_rvx=PB;
                if(g_rvz<-PB)g_rvz=-PB;
                if(g_rvz>PB)g_rvz=PB; }
            /* ⛔ THE DANCE FLOOR FENCED EVERY MODE. These clamps are the dance floor's own bounds, and
             * they ran in every door with a 300-unit street loaded -- the player walked to
             * z=2.0 and stopped at the edge of a floor that was not there. With a set loaded
             * the WORLD is the bound: its colliders (colResolve, below) and its ground apron. */
            if(!g_setNI){
                if(g_rvx<-(FLOOR_X-1.5f))g_rvx=-(FLOOR_X-1.5f);
                if(g_rvx>FLOOR_X-1.5f)g_rvx=FLOOR_X-1.5f;
                if(g_rvz<FLOOR_ZN+1.5f)g_rvz=FLOOR_ZN+1.5f;
                if(g_rvz>2.0f)g_rvz=2.0f; }
            /* ⛔ AFTER the floor clamps, or the lab's own dance-floor bounds drag the
             * pair straight back off the street they were staged on. */
            if(g_stageOn){ g_rvx=g_stage[0]; g_rvz=g_stage[1]; g_rvHead=g_stage[2]; }
            /* ⭐ THE HERO ON A MARK (09-11). `hmark=<name>` in a scene line, or ctl hmark=.
             * Furniture places her: world x/z, the mark's yaw, and -- below, in the model
             * build -- the mark's HEIGHT, which the ground plane could never give her. It
             * overrides the stage walk the way pmark overrides the pair relation, and it
             * beats the colliders (a bed is inside a house). `off` hands her back. Loud on
             * every change: a mark the set does not carry is a staging error, not a default. */
            { SetMark*HM=markFind(g_castMark[0]);
              static char hmLast[32]={0}; static int hmMiss=0;
              if(strcmp(hmLast,g_castMark[0])){ snprintf(hmLast,32,"%s",g_castMark[0]); hmMiss=0;
                  if(HM) printf("[hmark] hero on '%s' at (%.2f, %.2f, %.2f) yaw %.0f\n",HM->name,HM->x,HM->y,HM->z,HM->yaw);
                  else if(g_castMark[0][0]&&strcmp(g_castMark[0],"off")){ hmMiss=1;
                      printf("[hmark] '%s' NOT FOUND in the loaded set -- hero stays on the stage\n",g_castMark[0]); }
                  else if(g_castMark[0][0]) printf("[hmark] off\n");
                  fflush(stdout); }
              (void)hmMiss;
              g_heroMk=HM;
              locoPublish();
              if(HM){ if(g_loco[0].pon||g_loco[0].phold){ g_rvx=g_loco[0].px; g_rvz=g_loco[0].pz; g_rvHead=g_loco[0].pyaw; }   /* ⭐ walking there, on the exposure */
                      else { g_rvx=HM->x; g_rvz=HM->z; g_rvHead=HM->yaw*0.0174532925f; } }
              g_castFloorY[0]=HM?HM->y:0.f; }
            g_castAt[0][0]=g_rvx; g_castAt[0][1]=g_rvz; g_castAt[0][2]=g_rvHead; g_castAtOK[0]=1;
            /* ⭐ THE PLAYER COLLIDES. The colliders have existed since Z2 (`solid` props ->
             * circles, 640 of them on one street set) and only SET ACTORS ever called colResolve --
             * the hero walked through houses, fences and mailboxes. design/engine_capabilities.md
             * called this "the biggest one"; the 09-01 audit ranked it second only to unifying
             * the body systems. Same radius the actors use. NOT while staged: a scene places her
             * by authored marks, and a mark inside a collider must win over the collider. */
            static int   g_heroPushes=0; static float g_heroPushLast=0.f, g_heroPushMax=0.f;
            if(GAME_ON&&rideHero(&g_rvx,&g_rvz,&g_rvHead,&rvPitch,&rvRoll)){}   /* (11-05) riding: her ride places her on its deck, turned with it */
            else if(g_spOn&&!g_stageOn&&!g_heroMk) spHeroStep(&g_rvx,&g_rvz,(float)dt);   /* a set that's walked on: she follows its surfaces */
            else if(!g_stageOn&&!g_heroMk){ float pu=colResolve(&g_rvx,&g_rvz,0.42f);
                if(pu>1e-4f){ g_heroPushes++; g_heroPushLast=pu; if(pu>g_heroPushMax) g_heroPushMax=pu; } }
            /* REVA_HERODBG=1: where she is, once a second. The lab parks her at z -150 and
             * the camera debug prints zeros, so the first collision A/B measured nothing. */
            /* THE SAVE FILE's first client: a door's score becomes its record. Once a second,
             * every mode -- this block runs for the hero in all of them. */
            { static int hd=-1, hn=0; if(hd<0) hd=getenv("REVA_HERODBG")?1:0;
              if(hd&&(hn++%60)==0){ printf("[hero] %.2f,%.2f head %.2f manual %d  stick %.2f,%.2f spd %.2f dt %.4f kb %d  pushes %d last %.3f max %.3f\n",g_rvx,g_rvz,g_rvHead,g_manual,g_lx,g_ly,g_manSpd,(float)dt,g_kbActive,g_heroPushes,g_heroPushLast,g_heroPushMax); fflush(stdout); } }
            float sc=P->scale;
            /* T * RotY(weave) * RotX(lean) * RotZ(bank) * S -- composed by quaternion */
            static M4 curVP;   /* the vp of the pass currently being drawn */
            /* The dancefloor wander logic still runs above and still writes her position;
             * in skate mode the physics is the authority, so it is re-asserted here, after
             * that code and before anything is drawn from it. */
            if(g_skate){
                g_rvx=SK.x; g_rvz=SK.z; g_rvHead=SK.head;
                /* she leans WITH the deck but not as far -- a rider keeps their head up */
                float hb=SK.head+g_stanceRad;   /* her body frame, not the deck frame */
                float f0[3]={sinf(hb),0.f,cosf(hb)};
                float r0[3]={cosf(hb),0.f,-sinf(hb)};
                float dpf=SK.up[0]*f0[0]+SK.up[1]*f0[1]+SK.up[2]*f0[2];
                float dpr=SK.up[0]*r0[0]+SK.up[1]*r0[1]+SK.up[2]*r0[2];
                if(dpf> 1.f)dpf= 1.f;
                if(dpf<-1.f)dpf=-1.f;
                if(dpr> 1.f)dpr= 1.f;
                if(dpr<-1.f)dpr=-1.f;
                /* FULL tilt, not a fraction: this is what makes her model space the
                 * board frame, which is what lets the IK targets be constants. */
                rvPitch=g_procRig?asinf(dpf):0.f;
                rvRoll =g_procRig?(asinf(dpr) - g_lx*0.22f):0.f;
            }
            M4 model; { float qy[4],qx[4],qz2[4],qt[4],qt2[4];
              /* SK_STANCE: a skater stands ACROSS the deck. 1.30 rad (~75 deg) off the
               * direction of travel, slightly open rather than a full 90, with the spine
               * and neck twisting the rest of the way back in the pose layer above. */
              /* REVA_SKYAW turns a rig that was authored facing the other way. Which way a
               * downloaded character faces is a property of the file, not something the
               * engine can know, so it is a knob rather than a guess. */
              static float skYaw=999.f;
              if(skYaw>900.f){ const char*e=getenv("REVA_SKYAW"); skYaw=e?((float)atof(e)*0.0174532925f):0.f; }
              /* A rider stands ACROSS the deck; a person walks facing forward. The two are
               * ninety degrees apart, and both are measured from her own skeleton rather
               * than assumed. */
              float yawOff = (g_skate&&g_procRig) ? (SK.onFoot ? (g_stanceRad+1.5708f) : g_stanceRad) : skYaw;
              qaxis(qy,0,1,0,g_rvHead+yawOff+stepBoil(0,0)*0.006f); qaxis(qx,1,0,0,rvPitch); qaxis(qz2,0,0,1,rvRoll);
              qmul(qt,qy,qx); qmul(qt2,qt,qz2);
              float tp[3]={g_rvx+stepBoil(0,1)*0.005f,0,g_rvz+stepBoil(0,2)*0.005f}, ss[3]={sc,sc,sc};   /* + the animator's hand */
              m4_trs(model,tp,qt2,ss); }
            /* PER-FRAME FOOT GROUNDING (she was dancing on a lower plane than the floor): her lowest posed ankle
             * kisses the floor every frame, +0.12 platform-sole allowance; flight rides on top */
            { float fmn=1e9f;
              /* ================= P3 ROOT OWNERSHIP (engine rung E2) =====================
               * Three ways a root can own Y, resolved in priority order:
               *   gameplay (rvAlt, skate)  >  CLIP ROOT MOTION  >  auto-ground
               * Auto-ground was the only one implemented, and it cancels authored root
               * motion EXACTLY (raise the hips, the ankles rise, the model drops: net
               * zero -- and a body with feet overhead is driven underground). A clip
               * named z_rm_* declares root authority: the ground offset FREEZES at its
               * stand value (captured at clip start) and the clip's own pelvis
               * translation track does the rest. Bounce, floor clamp and the Y rate
               * clamp stand down -- an authored arc is exact, not eased. */
              int rmOwn=(g_mode==MODE_ANIM&&(g_labClip>=0||g_labDnC>=0)&&g_labRM);
              if(rmOwn&&g_rmY<-900.f){ float pf=1e9f;
                  for(int k2=2;k2<4;k2++) if(trBone[k2]>=0){ float y=world[trBone[k2]][13]; if(y<pf)pf=y; }
                  /* capture from THIS frame's stand pose, before the clip's first posed frame */
                  g_rmY=(pf<1e8f)?(0.02f+0.12f-pf*sc):revaGround;
                  /* ⭐ a rider on its @board channel stands the way a partner does -- its rest mesh's lowest point
                   * on the floor, which is the floor the skate clips were authored on. The ankle guess above put
                   * reva's board 0.048 u into the floor on the first board test (the co-star, placed by mny, was right). */
                  if(castOnBoard(0)) g_rmY=(g_heroMk?g_heroMk->y:0.f)-P->mny*sc; }
              for(int k2=2;k2<4;k2++) if(trBone[k2]>=0){ float y=world[trBone[k2]][13]; if(y<fmn)fmn=y; }
              if(rmOwn) model[13]=g_rmY+rvAlt+g_feet.y;
              g_rmActive=rmOwn;
              if(rmOwn&&P->pb[0]>=0){ float hx=world[P->pb[0]][12],hy=world[P->pb[0]][13],hz=world[P->pb[0]][14];
                  g_rmEndW[0]=model[0]*hx+model[4]*hy+model[8]*hz+model[12];
                  g_rmEndW[1]=model[2]*hx+model[6]*hy+model[10]*hz+model[14];
                  g_rmEndW[2]=model[1]*hx+model[5]*hy+model[9]*hz+model[13]; }
              else model[13]=((fmn<1e8f&&!(GAME_ON&&rideHeroReach()))?(0.02f+0.12f-fmn*sc):revaGround)+rvAlt+g_feet.y;   /* (11-06) a ride that puts her feet on it: her rest floor on the deck */
              /* ⭐ ON A MARK, THE FURNITURE OWNS Y (09-11). Same convention as the partner
               * on a mark: the rest mesh's lowest point sits at the mark's height, and a
               * borrowed lay pays the hip drop -- which the hero path had never paid, so a
               * lying clip on the hero floated at standing hip height. Bounce, the S7 sole
               * clamp and the Y rate clamp stand down: a body placed by a bed does not
               * bob, and "lift her out of the floor" is wrong when the floor is a mattress. */
              int onMk=(g_heroMk!=NULL);
              if(onMk){ float hd=(hdP&&hdC>=0)?donorHipDrop(hdP,hdC,rvWph,P):0.f;
                  model[13]=g_heroMk->y-P->mny*sc+hd*sc;
                  { float af9=castOnBoard(0)?boardAuthoredFloor(0,P,sc):NAN; if(af9==af9) model[13]=g_heroMk->y-af9*sc; }
                  if(g_heroMk->hips&&P->pb[0]>=0) model[13]=g_heroMk->y-world[P->pb[0]][13]*sc; }
              if(!onMk&&!rmOwn&&!g_rvHipTrans&&!g_skate&&rvAlt<0.15f&&!DOOR_PLAT)   /* S4.5: rotation-only heroes bounce again */
                  model[13]+=powf(fabsf(sinf((float)(btF*3.14159))),1.5f)*0.09f;
              /* S7 FLOOR CLAMP. The line above grounds the ANKLE at a fixed sole allowance,
               * which silently assumes the ankle-to-toe angle of her REST pose. A borrowed
               * stand flexes the ankle differently and put her 9cm through the floor
               * (measured: contact -0.144u). So clamp on the bone that actually reaches
               * lowest -- footL/footR are elected as the lowest descendant of each knee.
               * One-sided: it can only lift a body out of the floor, so a pose that already
               * sits right is untouched, and it stays out of skate (she rides a deck, and
               * the park floor is not y=0). */
              /* Gated to a body STANDING STILL on the ground. The sink was measured in the
               * stand and nowhere else; letting the clamp run during locomotion moved
               * reva's measured stride 4.95u -> 2.40u by changing which frames read as
               * contacts, and a metric that shifts under an unrelated fix is a fix that
               * reached too far. Widen it when a gait is measured sinking, not before. */
              /* ⭐ A RIDER IS PLACED BY ITS DECK, NOT ITS ANKLES -- the partner's rest-pose grounding,
               * so a pushing foot authored below the rig floor reaches the real one */
              if(castRides(0)&&!onMk){ float af9=castOnBoard(0)?boardAuthoredFloor(0,P,sc):NAN;
                  model[13]=(af9==af9)?-af9*sc:-P->mny*sc; }   /* ⭐ a @board rider on the floor its clips were authored on */
              if(!onMk&&!rmOwn&&!g_skate&&!castRides(0)&&g_manSpd<0.35f&&rvAlt<0.15f&&P->footL>=0&&P->footR>=0){
                  float tmn=1e9f;
                  for(int k2=0;k2<2;k2++){ int b2=k2?P->footR:P->footL;
                      float hx=world[b2][12],hy=world[b2][13],hz=world[b2][14];
                      float wy=model[1]*hx+model[5]*hy+model[9]*hz+model[13];
                      if(wy<tmn) tmn=wy; }
                  if(tmn<0.02f&&tmn>-3.f) model[13]+=0.02f-tmn; }
              model[13]+=castDeckLift(0);   /* ⭐ on a deck she stands ON it */
              { /* GROUNDED Y RATE-CLAMP (S4d, posecheck finding): the grounding offset (per-clip
                 * lowest-joint) SNAPS at clip switches while the pose crossfades -- a 1.9u root
                 * teleport. On the ground, ease vertical settles at <=0.28u/frame; in the air
                 * the physics arc stays exact. */
                static float yPrev9=0.f; static int yInit9=0;
                if(yInit9&&!g_skate&&!rmOwn&&!onMk){   /* physics never exceeds ~0.16u/frame; only snaps get eaten; an authored arc is exact */
                    float dy9=model[13]-yPrev9;
                    if(dy9>0.30f) model[13]=yPrev9+0.30f;
                    else if(dy9<-0.30f) model[13]=yPrev9-0.30f; }
                yPrev9=model[13]; yInit9=1; }
              { static int pd9=-2; if(pd9==-2){ const char*e=getenv("REVA_POSEDUMP"); pd9=e?atoi(e):-9999; }
                if(pd9==-1) printf("[hpose] want=%d wph=%.1f x=%.2f y=%.2f z=%.2f alt=%.2f onG=%d dj=%d\n",
                    rvCM,(float)rvWph,model[12],model[13],model[14],rvAlt,GM.onG,GM.dj); }
              if(MODE_PLAYS&&GM.wKind>0&&P->gripNode>=0)   /* S5a: the hero's carry, in her hand */
                  wpnGripPush(model,world[P->gripNode],GM.wVar,g_rvx,g_rvz);
              /* S6: her feet in world space -- the lab's contact marks and the slip
               * meter both read these, and so does REVA_POSEDUMP=-1. */
              /* the hero speaks the SAME telemetry line as a slot (si=-1), so
               * motioncheck.py analyses her without knowing she is the hero. */
              { static int pdh=-2; if(pdh==-2){ const char*e=getenv("REVA_POSEDUMP"); pdh=e?atoi(e):-9999; }
                if(pdh==-1&&P->pb[0]>=0){
                    /* S7: head and both clavicles ride along so posture -- balance over the
                     * feet, a level pelvis, level shoulders -- can be measured, not eyeballed.
                     * Appended AFTER hnd= so every existing reader's regex still matches. */
                    float hj[16][3]; int hb[16]={P->pb[0],P->pb[12],P->pb[13],P->pb[14],P->pb[15],P->footL,P->footR,P->pb[8],P->pb[5],P->pb[6],P->pb[9],
                                                 P->pb[7],P->pb[10],P->pb[11],
                                                 P->gripNode,P->gripL};   /* P2/P4: both REAL hands; 'hnd' is historically the LEFT forearm */
                    for(int j9=0;j9<16;j9++){ int b9=hb[j9];
                        if(b9<0){ hj[j9][0]=hj[j9][1]=hj[j9][2]=-999.f; continue; }
                        float hx=world[b9][12],hy=world[b9][13],hz=world[b9][14];
                        hj[j9][0]=model[0]*hx+model[4]*hy+model[8]*hz+model[12];
                        hj[j9][1]=model[1]*hx+model[5]*hy+model[9]*hz+model[13];
                        hj[j9][2]=model[2]*hx+model[6]*hy+model[10]*hz+model[14]; }
                    printf("[pose] t=%.3f si=-1 bhv=%d bhvT=0.00 spd=%.2f xz=%.2f,%.2f hip=%.3f,%.3f,%.3f uL=%.3f,%.3f,%.3f kL=%.3f,%.3f,%.3f uR=%.3f,%.3f,%.3f kR=%.3f,%.3f,%.3f fL=%.3f,%.3f,%.3f fR=%.3f,%.3f,%.3f hnd=%.3f,%.3f,%.3f head=%.3f,%.3f,%.3f shL=%.3f,%.3f,%.3f shR=%.3f,%.3f,%.3f uaL=%.3f,%.3f,%.3f uaR=%.3f,%.3f,%.3f foR=%.3f,%.3f,%.3f grp=%.3f,%.3f,%.3f grpL=%.3f,%.3f,%.3f\n",
                        tsec,GM.stage,g_manSpd,g_rvx,g_rvz,
                        hj[0][0],hj[0][1],hj[0][2],hj[1][0],hj[1][1],hj[1][2],
                        hj[2][0],hj[2][1],hj[2][2],hj[3][0],hj[3][1],hj[3][2],
                        hj[4][0],hj[4][1],hj[4][2],hj[5][0],hj[5][1],hj[5][2],
                        hj[6][0],hj[6][1],hj[6][2],hj[7][0],hj[7][1],hj[7][2],
                        hj[8][0],hj[8][1],hj[8][2],hj[9][0],hj[9][1],hj[9][2],
                        hj[10][0],hj[10][1],hj[10][2],hj[11][0],hj[11][1],hj[11][2],
                        hj[12][0],hj[12][1],hj[12][2],hj[13][0],hj[13][1],hj[13][2],
                        hj[14][0],hj[14][1],hj[14][2],hj[15][0],hj[15][1],hj[15][2]); } }
              /* ---- S6c: close the loop on foot slip ---- */
              if(P->footL>=0&&P->footR>=0&&getenv("REVA_SERVO")){   /* OFF by default: see the note above */
                  static float pfw[3]={0,0,0}, pbx=0,pbz=0; static int pSide=-1, pOK=0;
                  float fw[2][3];
                  for(int e=0;e<2;e++){ int b9=e?P->footR:P->footL;
                      float hx=world[b9][12],hy=world[b9][13],hz=world[b9][14];
                      fw[e][0]=model[0]*hx+model[4]*hy+model[8]*hz+model[12];
                      fw[e][1]=model[1]*hx+model[5]*hy+model[9]*hz+model[13];
                      fw[e][2]=model[2]*hx+model[6]*hy+model[10]*hz+model[14]; }
                  int side=(fw[0][1]<=fw[1][1])?0:1;
                  float bodyD=hypotf(g_rvx-pbx,g_rvz-pbz);
                  if(pOK&&pSide==side&&bodyD>1e-4f&&bodyD<1.f&&g_manSpd>0.5f){
                      float dirx=(g_rvx-pbx)/bodyD, dirz=(g_rvz-pbz)/bodyD;
                      float slip=(fw[side][0]-pfw[0])*dirx+(fw[side][2]-pfw[2])*dirz;
                      g_servoSlip=fabsf(slip);
                      /* slip>0: the planted foot is being dragged FORWARD, so the clip is
                       * running too slow for the speed -- and vice versa. */
                      float corr=1.f+0.9f*(slip/bodyD);
                      if(corr<0.90f) corr=0.90f;
                      if(corr>1.10f) corr=1.10f;   /* gentle: no oscillation */
                      g_servo*=corr;
                      if(g_servo<0.25f) g_servo=0.25f;
                      if(g_servo>4.0f) g_servo=4.0f; }
                  else if(g_manSpd<=0.5f){ g_servo+=(1.f-g_servo)*0.05f; g_servoSlip=0.f; }
                  memcpy(pfw,fw[side],12); pbx=g_rvx; pbz=g_rvz; pSide=side; pOK=1; }
              { int fb9[2]={P->footL,P->footR}; g_heroFOK=0;
                for(int k9=0;k9<2;k9++){ int b9=fb9[k9];
                    if(b9<0){ g_heroF[k9][0]=g_heroF[k9][1]=g_heroF[k9][2]=-999.f; continue; }
                    float hx=world[b9][12],hy=world[b9][13],hz=world[b9][14];
                    g_heroF[k9][0]=model[0]*hx+model[4]*hy+model[8]*hz+model[12];
                    g_heroF[k9][1]=model[1]*hx+model[5]*hy+model[9]*hz+model[13];
                    g_heroF[k9][2]=model[2]*hx+model[6]*hy+model[10]*hz+model[14];
                    g_heroFOK=1; } }
              /* Set outright, NOT accumulated off the posed foot: the IK targets are
               * expressed at the rest sole height, so deriving her height from the posed
               * foot would make height depend on IK and IK depend on height. */
              /* GROUND HER ON THE FOOT SHE IS ACTUALLY STANDING ON. Using the REST sole
               * height is right while the procedural layer owns the legs, because it puts
               * the feet where it wants them. An authored clip does not: it moves her feet
               * every frame, so grounding off the rest pose leaves her floating on one
               * stride and sinking on the next. MEASURED: the lowest joint wandered 4.7
               * units through a walk cycle that should hold it nearly constant.
               * fmn is the lowest posed ankle, so this pins whichever foot is down. */
              if(g_skate){
                  if(g_authored && fmn<1e8f) model[13]=SK.y+0.02f-fmn*sc;
                  else model[13]=SK.y+((g_procRig&&!SK.onFoot)?0.16f:0.02f)-g_revaFootY*sc; } }
            /* HER BODY has to sit on the deck, and her body is not at her pack origin: the
             * rest-pose bone cloud is centred (cx,cz) away from it -- MEASURED at 0.696 right
             * and 0.646 back once scaled -- which is exactly why she rendered beside the board
             * rather than on it. Nothing had ever had to line up with her before, so nothing
             * caught it. Cancel it THROUGH the model matrix, whose rotation columns already
             * carry her scale, so the correction stays right at any heading and lean. */
            if(g_skate){
                float ox=P->cx, oz=P->cz;
                model[12] -= model[0]*ox + model[8]*oz;
                model[14] -= model[2]*ox + model[10]*oz;
            }
            /* ⭐ THE CARVE -- a rider on a path leans into the curve, about its travel axis through the
             * ground contact. After every placement above; before the grips and the attach points. */
            if(!g_skate&&g_heroMk&&(g_loco[0].pon||g_loco[0].phold)) locoLeanModel(model,0,g_castFloorY[0]);
            /* ⭐ ON A HALF-PIPE the ride places her -- contact, the surface's up, the air's turn -- over all of the above */
            if(!g_skate&&g_mode==MODE_ANIM){ pipeModel(0,model,P,sc); rollModel(0,model); }   /* (10-22) and the roll */
            /* HER REAL TEXTURE. The hologram (GFX_HOLO) is a deliberate look for the venue --
             * fresnel, scanlines, additive -- but it throws her texture away: her atlas has skin
             * tones, grey panels and purple fabric, and none of it survives an additive purple
             * wash. In skate mode she is a person on a board, not an apparition, so she is drawn
             * solid. REVA_HOLO=1 puts the hologram back. */
            static int holoMode=-1;
            if(holoMode<0){ const char*e=getenv("REVA_HOLO"); holoMode=(e&&e[0]=='1')?1:0; }
            /* ⭐ M0 (11-01): in the zombie cab she is the zombie game's Reva -- an android with a body on a street, not the
             * venue's apparition -- so the cab draws her solid, as skate mode does. REVA_HOLO=1 still puts the hologram back. */
            int skSolid = (g_mode==MODE_ANIM) ? 1 : ((g_skate||GAME_ON) && !holoMode);   /* the lab is never a hologram */   /* the lab shows a SOLID body: a translucent hologram hides the very pops being hunted */
            if(skSolid){
                /* NOT a flat tint: a single-colour body merges its own limbs into one
                 * silhouette and a crumpled pose reads as a fine one. The lab keeps the
                 * pack's texture and lets the neutral rig shade it, so an arm can be told
                 * from a leg. */
                         /* SHE NEEDS HER OWN LIGHT. The venue's light is deliberately dark and
                          * comes from the three nearest of the dance floor's lights, which are aimed at the
                          * dancefloor -- a solid body there was lit by nothing but the 0.13
                          * ambient floor, so her texture showed up almost black. A key over her
                          * shoulder, a cool fill opposite, and a low warm bounce. */
                         /* A SCENE GETS LIT FOR THE SCENE. The rig above is aimed at
                          * one body standing on a runway; a two-hander needs the pair
                          * lit as a pair, and it needs a RIM -- once the dance floor's dressing
                          * is off, an unrimmed head is a dark shape on a dark ground and
                          * every jaw and blink the face rig can do is invisible. Key
                          * warm off the camera side, rim cool from behind-opposite,
                          * a dim warm bounce off the floor so chins are not black. */
                         if(g_scnT>=0.f&&g_heroHeadOK&&g_partHeadOK){
                           const float*HA=g_heroHeadW,*HB=g_partHeadW;
                           float mx=(HA[0]+HB[0])*0.5f, my=(HA[1]+HB[1])*0.5f, mz=(HA[2]+HB[2])*0.5f;
                           float ax=HA[0]-HB[0], az=HA[2]-HB[2];
                           float al=sqrtf(ax*ax+az*az); if(al<1e-3f){ ax=0.f; az=1.f; al=1.f; }
                           ax/=al; az/=al;
                           float px=-az, pz=ax, sg=g_scnSide;
                           /* ⭐ AND THE KEY CROSSES WITH IT. The camera always sits at +p
                            * (that is what one side of the eyeline means) and the key is written
                            * from the camera side, so a reversed lens needs the rig mirrored too
                            * -- otherwise the reverse is lit from behind. */
                           if(scnRevNow()){ px=-px; pz=-pz; }
                           /* ⭐ THE KEY BELONGS ON THE SPEAKER, NOT THE MIDPOINT. Keyed
                            * at the middle, the listener's shoulder -- which is the
                            * nearest thing to the lens in every over-the-shoulder -- came
                            * back as the BRIGHTEST object in the frame and pulled the eye
                            * off the face that was talking. Keyed on the speaker, the
                            * foreground body falls away into the dark edge it is supposed
                            * to be, and the person speaking is the person lit. The rim
                            * stays at the midpoint so the near shoulder keeps an edge and
                            * does not merge into the black. */
                           const float*SPK=(g_scnSide>0.f)?HA:HB;
                           /* FURTHER OUT AND DIMMER. Once the key was actually pointing
                            * at the face it blew both of them white -- and a near light
                            * blows the FOREGROUND worst, because 1/(1+dd) falls off
                            * hardest close in. Standing it off at 3.3 instead of 2.4 and
                            * taking a third off the intensity gives the same exposure on
                            * the face with far less falloff across the depth of a shot. */
                           float lp3[NLIGHT*3]={ SPK[0]+px*3.30f-ax*1.30f*sg, SPK[1]+1.45f, SPK[2]+pz*3.30f-az*1.30f*sg,
                                          mx  -px*3.60f+ax*1.00f*sg, my +1.80f,    mz  -pz*3.60f+az*1.00f*sg,
                                          mx,                        my -2.40f,    mz , 0,-99,0, 0,-99,0, 0,-99,0 };
                           float lc3[NLIGHT*3]={ 2.55f,2.28f,2.02f,   1.60f,1.90f,2.65f,   0.50f,0.38f,0.32f , 0,0,0, 0,0,0, 0,0,0 };
                           lightsStamp(lp3,lc3,3);   /* ⭐ the held light is the fourth lamp on the pair */
                         } else { float kx=sinf(SK.head), kz=cosf(SK.head);
                           float lp3[NLIGHT*3]={ SK.x-kx*2.2f+kz*1.8f, SK.y+3.4f, SK.z-kz*2.2f-kx*1.8f,
                                          SK.x+kx*3.0f-kz*2.2f, SK.y+2.6f, SK.z+kz*3.0f+kx*2.2f,
                                          SK.x,                 SK.y+0.4f, SK.z , 0,-99,0, 0,-99,0, 0,-99,0 };
                           float lc3[NLIGHT*3]={ 2.30f,2.15f,2.55f,   0.85f,0.95f,1.45f,   0.55f,0.42f,0.70f , 0,0,0, 0,0,0, 0,0,0 };
                           lightsStamp(lp3,lc3,3);
                              }
                         }
            /* TWO-PASS HOLOGRAM (her textures and mesh read wrong): single-pass alpha+depth-write
             * z-blocked her own far surfaces -> hollow patches showing the background through her.
             * Pass 1 lays her depth silently; pass 2 colors ONLY the nearest surface, any alpha. */
            /* TWO TARGETS. vcPass 1 = the broadcast feed (a small target, tight camera on
             * her); vcPass 0 = the real frame. ONLY the mesh draw repeats: her locomotion
             * state machine, the tracer ribbons and the lightning sit outside this loop and
             * must tick exactly once per frame. */
            /* ⭐ (11-07) A GAME DRAWS THE FEED ONLY WHEN THE WALLS SHOW IT. The feed is read by the venue's jumbos and nothing else, and in a game with no jumbos it
             * went nowhere: the street game drew its co-star twice a frame, her 37 meshes 74 draws, and on the Pi a second pass of her. The doors and the lab keep both
             * passes as they were: the lab steps its jiggle and its contact solve once a pass, and film's takes were made that way */
            const int feed9=!GAME_ON||(holly_fbo_texture()&&g_mode!=MODE_ANIM);
            /* ⭐ (11-12) IN THE LAB THE FEED IS NEVER SHOWN (the jumbos above aren't drawn in MODE_ANIM), yet the GPU rendered the whole frame into it, and
             * the pass's hero and cast went into the picture's own list as well: every scene body drawn twice where it's seen (a music video chorus: 14 M of 31.7 M
             * indices). Its pass still runs for its CPU steps, which film's takes were made with, but nothing of it is drawn (g_vkPassDark) and no feed is
             * rendered. REVA_LABFEED=1: as before */
            static int labFeed9=-1; if(labFeed9<0){ const char*e9=getenv("REVA_LABFEED"); labFeed9=(e9&&e9[0]=='1'); }
            const int feedDark9=(g_mode==MODE_ANIM)&&!labFeed9;
            for(int vcPass=feed9?1:0;vcPass>=0;vcPass--){
            g_vkPassDark=(vcPass==1&&feedDark9);

            if(vcPass){
                { /* the broadcast camera: chest height, close, drifting three-quarter orbit */
                  float fx7=model[12], fy7=model[13], fz7=model[14];
                  float ang7=(float)tsec*0.22f;
                  float ex7=fx7+sinf(ang7)*4.4f, ez7=fz7+cosf(ang7)*4.4f, ey7=fy7+2.7f;
                  M4 fproj,fview,fvp;
                  m4_persp(fproj,34.0f,(float)FEEDW/FEEDH,0.3f,80.0f);
                  m4_lookat(fview,ex7,ey7,ez7, fx7,fy7+1.85f,fz7);
                  m4_mul(fvp,fproj,fview);
#ifdef REVA_VK
                  if(!g_noDraw&&!feedDark9){ float e9[3]={ex7,ey7,ez7}; gfxFeed(feedTex,fvp,e9); }
#endif
                  memcpy(curVP,fvp,64);  }
            } else {
                memcpy(curVP,vp,64); memcpy(g_watVP,vp,64);
                /* THE SET goes down before the actors do -- same lights, and the depth
                 * buffer sorts the rest. On the real pass only: the broadcast feed is a
                 * tight orbit of the hero and wants no scenery. A set with no light lines
                 * is lit by the sun alone. */
                /* ⭐ A SET DRAWS IN EVERY MODE, not only the lab. These four gates carried
                 * `g_mode==MODE_ANIM&&`, so no game door had ever rendered the street the
                 * film is shot on: REVA_SET could load its colliders and the player would
                 * bump into houses that were not drawn. A loaded set IS the world now. */
                passMark("heroPose");
                if(g_setNI||g_nPipe||g_setSpace)   /* ⭐ 10-22: a set that is only space still paints its sky */
                    drawSet();
                else objDraw();
                passMark("set");
                if(g_setNA){
                    actorsDraw(pal);
                    leadsCollect(); }
                passMark("actors");
                /* ⭐ A HELD PROP OUTSIDE A SCENE. This was gated on a loaded .scn, so
                 * the lab could not put anything in a hand and look at it -- which is where a
                 * held LIGHT has to be judged before it is cast. */
                /* ⭐ (11-12) IN THE FILM'S LAB EVERY HELD PROP IS DRAWN AFTER THE CAST (drawBoards' pass, in the frame its holder was posed in).
                 * From here everything it hangs was placed from last frame's attach points: on the frame the music video's chest door swaps
                 * shut to open, the clip moves her in one frame, and the open door was drawn at last frame's chest, its leaf across the black
                 * card for that frame. Elsewhere (a game, the doors) as before: they have no pass after the cast. REVA_HELDLATE=0: the old pass */
                { static int hl9=-1; if(hl9<0){ const char*e9=getenv("REVA_HELDLATE"); hl9=!(e9&&e9[0]=='0'); } g_heldLate=hl9&&g_mode==MODE_ANIM&&skSolid; }
                if(g_scnN>0||heldAny())
                    drawHeld();
                /* ⭐ THE PLACED SCREENS, AFTER THE WORLD -- drawSet's sky would wipe them otherwise */
                if(g_setNI) scrDrawPlaced();
                artWorld(vp);   /* (11-11) the set's drawings that hang behind the world or lie on a plane in it (setart.inc) */
            }
            if(g_mode==MODE_ANIM){ ragdollTick(P,world,model,0); jiggleTick(P,world,model,0); ctSolve(P,world,model,0); }   /* ⭐ limp / soft mass / THE CONTACT PASS, before her skin */
            else if(g_rdGameOn) rdBody(-1,P,world,model);   /* limp, if a game said so */
            /* H6: MORPHS -- blend the active targets into the position buffer on the
             * CPU (sparse deltas over the bind positions) and re-upload. Restores the
             * base exactly once when every weight returns to zero. */
            if(g_scnT>=0.f) for(int m9=0;m9<P->nMorph&&m9<MORPH_MAX;m9++) if(g_clipMorphW[0][m9]>g_heroMorphW[m9]) g_heroMorphW[m9]=g_clipMorphW[0][m9];   /* (11-11) her clip's face shapes over the scene's */
            { static int wasAct=0; morphBlend(P,g_heroMorphW,&wasAct); }
            /* HER OWN MODEL MATRIX, EVERY PASS. It used to be uploaded once above the
             * vcPass loop -- but labDrawPartner binds HERS at the end of pass 1, so on
             * pass 0 (the frame you actually see) the hero was drawn with the PARTNER's
             * matrix: at the partner's scale, inside the partner's body. She simply
             * vanished from the main view whenever a second body was on stage, and
             * every pair move since the high five was judged on the multi-cam wall,
             * which rebinds per panel and hid the bug. */
            chainPublish(0,model);   /* her chains feel where she goes */
            g_vkOwner=(unsigned)GFX_KIND_HERO<<28;
            if(g_mode==MODE_ANIM) slotWashFor(0);   /* (11-11) a set's `slotwash 0 ...`: until the cast is drawn, below */
            /* the hologram needs two passes (depth, then colour); a solid character needs one */
            for(int hp2=((g_fpvNow||g_camHoldSlot==0||(GAME_ON&&g_tune.firstPerson==1))?2:(skSolid?1:0));hp2<2;hp2++){   /* first person, or her own camera: her body is not drawn */
                if(skSolid){    }
                else if(hp2==0){    }
                else {     }
                g_vkBodySkip=(!skSolid&&hp2==0); g_vkBodyFlags=skSolid?0:GFX_HOLO;   /* the renderer does a hologram's two passes itself */
                /* mesh 1 is his skateboard, skinned to his rig: once he has bailed it is lying on the ground, so it must not ride along with him. The lab reads the body, not a silhouette: real texture, white tint, neutral light. */
                { static const float labTint9[3]={0.80f,0.80f,0.82f}; int lab9=(g_mode==MODE_ANIM);
                  BodyGL g9={0,g_heroMorphW,1,vcPass,(g_skate&&SK.onFoot&&!g_procRig)?1u:0u,lab9?labTint9:NULL};
                  g_faceCur=(g_faceN&&GAME_ON)?faceOf(-1):NULL;
                  if(g_eyeEver) memcpy(g_vkEye,g_heroEye,16);
                  { Pack*Q9=skSolid?heroLod(P):NULL;   /* (11-07) the lightest copy on the Pi's rung */
                    if(Q9){ g_lodNoCast=vcPass; bodyMeshesLod(P,Q9,world,model,pal); g_lodNoCast=0; } else bodyMeshes(P,world,model,pal,&g9); }   /* the feed pass casts nothing, as the whole body's path */
                  g_faceCur=NULL; g_vkEye[3]=0.f; if(g_attN&&GAME_ON&&hp2==1) attDraw(-1,P,world,model); } }
            g_vkBodySkip=0; g_vkBodyFlags=0;
            /* ⭐ THE PICKER NEEDS THE HERO EXACTLY AS DRAWN -- her world matrices and her
             * model matrix, this frame, in this pose. Stashed here because this is the one
             * place that has both. */
            if(g_markEdit){ memcpy(g_markW,world,sizeof(M4)*(P->nNodes<1024?P->nNodes:1024));
                            memcpy(g_markM,model,64); g_markP=P; }
            if(P->pb[5]>=0){    /* the hero's head, for the scene camera -- and (09-11) for first person, in EVERY mode */
                int hd9=P->pb[5];
                float hx=world[hd9][12],hy=world[hd9][13],hz=world[hd9][14];
                if(g_mode==MODE_ANIM){ int gb0=(P->gripNode>=0)?P->gripNode:P->pb[11];
                  if(gb0>=0){ ptWorld(world[gb0],model,g_castGripW[0]); g_castGripOK[0]=1;
                              m4_mul(g_castGripM[0],(float*)model,world[gb0]); }
                  attPublish(P,world,model,0);
                  /* ⭐ her board off her clip's channel, at the phase she was sampled at (after any hold) */
                  if(hdC>=0&&hdP) boardPublish(0,hdP,hdC,rvWph,1,model);
                  else if(want>=0&&(unsigned)want<P->nClips) boardPublish(0,P,want,rvWph,1,model);
                  else boardPublish(0,NULL,-1,0.0,1,model); }
                g_heroHeadW[0]=model[0]*hx+model[4]*hy+model[8]*hz+model[12];
                g_heroHeadW[1]=model[1]*hx+model[5]*hy+model[9]*hz+model[13];
                g_heroHeadW[2]=model[2]*hx+model[6]*hy+model[10]*hz+model[14];
                g_heroHeadOK=1;
                clipAuditBody(P,world,model,0);   /* ⭐ the hero, exactly as drawn */
                if(P->fmMouth>=0&&P->fmEyeL>=0&&P->fmEyeR>=0){
                    ptWorld(world[P->fmMouth],model,g_heroMouthW);
                    float el[3],er[3];
                    ptWorld(world[P->fmEyeL],model,el); ptWorld(world[P->fmEyeR],model,er);
                    for(int k9=0;k9<3;k9++) g_heroEyeW[k9]=0.5f*(el[k9]+er[k9]);
                    g_heroIOD=sqrtf((el[0]-er[0])*(el[0]-er[0])+(el[1]-er[1])*(el[1]-er[1])+(el[2]-er[2])*(el[2]-er[2]));
                /* THE SEPARATION FLOOR, from the two bodies rather than from taste:
                 * half a shoulder each, plus a hand's margin so sleeves do not
                 * interpenetrate. Across the UPPERARM bones, which are the shoulder
                 * joints; the clavicles sit near the sternum and under-report by half. */
                if(g_heroShoulderW<=0.f&&P->pb[7]>=0&&P->pb[10]>=0){
                    float*a7=world[P->pb[7]],*b7=world[P->pb[10]];
                    float dx=a7[12]-b7[12], dy=a7[13]-b7[13], dz=a7[14]-b7[14];
                    g_heroShoulderW=sqrtf(dx*dx+dy*dy+dz*dz)*P->scale; }
                if(g_stageMinSep<=0.f&&g_heroShoulderW>0.f&&g_partShoulderW>0.f){
                    g_stageMinSep=(g_heroShoulderW+g_partShoulderW)*0.5f+0.34f;
                    printf("[stage] separation floor %.2f world units "
                           "(shoulders %.2f + %.2f, margin 0.34)\n",
                           g_stageMinSep,g_heroShoulderW,g_partShoulderW);
                    fflush(stdout); }
                    /* forward = (eye line) x (world up), flipped to agree with the body
                     * so a mirrored rig cannot point the lens at the back of her head */
                    { float lx=el[0]-er[0], ly=el[1]-er[1], lz=el[2]-er[2];
                      float ll=sqrtf(lx*lx+ly*ly+lz*lz);
                      if(ll>1e-6f){ lx/=ll; ly/=ll; lz/=ll;
                          float fx=-lz, fy=0.f, fz=lx;          /* (eye line) x world up */
                          float fl=sqrtf(fx*fx+fz*fz);
                          if(fl>1e-6f){ fx/=fl; fz/=fl;
                              if(fx*sinf(g_rvHead)+fz*cosf(g_rvHead)<0.f){ fx=-fx; fz=-fz; }
                              g_heroFaceFwd[0]=fx; g_heroFaceFwd[1]=fy; g_heroFaceFwd[2]=fz; } } }
                    g_heroFaceOK=1; } }
            /* P7: the hero's right hand in world, for the contact gate below.
             * hi5=3 (THE KISS): the HEAD bone instead -- lips have no bone, but the
             * score prints its predicted head-head distance, and the engine measuring
             * the same number through its own FK is the independent half of the check. */
            if(g_mode==MODE_ANIM&&g_labHi5&&P->pb[g_labHi5==3?5:11]>=0){
                int hb=P->pb[g_labHi5==3?5:11], tip=-1;
                if(g_labHi5!=3)
                    for(unsigned n9=0;n9<P->nNodes;n9++) if(P->nodes[n9].parent==hb){ tip=(int)n9; break; }
                if(tip<0) tip=hb;
                float hx=world[tip][12],hy=world[tip][13],hz=world[tip][14];
                g_labHeroHand[0]=model[0]*hx+model[4]*hy+model[8]*hz+model[12];
                g_labHeroHand[1]=model[1]*hx+model[5]*hy+model[9]*hz+model[13];
                g_labHeroHand[2]=model[2]*hx+model[6]*hy+model[10]*hz+model[14];
                g_labHeroHandOK=1;
                { static int said2=0; if(!said2){ said2=1;
                    int sh=P->pb[10];
                    if(sh>=0&&g_labHi5!=3){ float shw=model[1]*world[sh][12]+model[5]*world[sh][13]
                                        +model[9]*world[sh][14]+model[13];
                        float a1=0,a2=0;
                        { float dx=world[sh][12]-world[hb][12],dy=world[sh][13]-world[hb][13],dz=world[sh][14]-world[hb][14];
                          a1=sqrtf(dx*dx+dy*dy+dz*dz); }
                        { float dx=world[hb][12]-world[tip][12],dy=world[hb][13]-world[tip][13],dz=world[hb][14]-world[tip][14];
                          a2=sqrtf(dx*dx+dy*dy+dz*dz); }
                        printf("[hi5] HERO geometry: shoulder %.2fu above ground, arm %.2fu\n",
                               shw,(a1+a2)*P->scale); fflush(stdout); } } } }
            /* ⭐ THE CAST, DRAWN. Every line inside labDrawPartner resolves through
             * g_castI, so a third body is the same function called again -- which is the
             * whole point of the array and the reason nothing else had to change. */
            g_bodyWash[3]=0.f;   /* (11-11) the hero's wash ends here */
            if(g_mode==MODE_ANIM&&skSolid){
                int sv8=g_castI;
                for(g_castI=1;g_castI<SCN_MAXCAST;g_castI++)
                    if(g_labPartner&&(g_labPClip>=0||g_labPDnP)){
                        slotWashFor(g_castI); labDrawPartner(g_labPartner,g_labPClip,g_labPPhase,model,pal); g_bodyWash[3]=0.f; }
                g_castI=sv8;
                /* ⭐ THE BOARDS, in the frame their riders were just posed in (every rider has published) */
                if(g_scnN>0||heldAny()){ drawBoards();
                     } }
            if(g_mode==MODE_ANIM&&g_labGrid>1&&skSolid){
                /* ================= THE MULTI-CAM WALL =================================
                 * Not copies of the body scattered through the world -- that clutters the
                 * scene and is not what a review wall is. This is N VIEWPORTS, each with
                 * its own camera orbiting the subject, all drawn from the pose that was
                 * already solved this frame. Same instant by construction, live, nothing
                 * frozen. The panes are cleared and drawn last so they own the screen. */
                /* grid=13 (close-ups of the body from four more angles): the nine-angle wall keeps the left 3/4 of
                 * the screen; a right-hand column carries FOUR CLOSE-UPS orbiting the
                 * upper body on the diagonals -- the angles between the wall's
                 * cardinals, at a third the distance, aimed where hands meet bodies.
                 * With a partner, the close-ups centre between the two of them. */
                if(g_faceDbg<0){ const char*e9=getenv("REVA_FACEDBG"); g_faceDbg=(e9&&e9[0]!='0'); }
                int gn=g_labGrid; if(gn>16) gn=16;
                g_wallV.n=gn;
                int nClose=(gn==13&&!g_labFace)?4:0, nWall=gn-nClose;
                int cols, rows, pw, ph;
                if(nClose){ cols=3; rows=3; pw=(W*3/4)/3; ph=H/3; }
                else { cols=1; while(cols*cols<gn) cols++;
                       rows=(gn+cols-1)/cols; pw=W/cols; ph=H/rows; }
                for(int gi=0;gi<gn;gi++){
                    int pw2=pw, ph2=ph, vx, vy;
                    if(g_labFace){
                        /* eight small heads across the top half, then the mouth and
                         * the eyes big across the bottom -- the two panels the face
                         * work is actually judged on */
                        if(gi<8){ pw2=W/4; ph2=H/4; vx=(gi%4)*pw2; vy=H-(gi/4+1)*ph2; }
                        else { pw2=W/2; ph2=H/2; vx=(gi==9)?W/2:0; vy=0; }
                    } else if(gi<nWall||!nClose){ int cx=gi%cols, cy=gi/cols;
                        vx=cx*pw; vy=H-(cy+1)*ph; }
                    else { int k=gi-nWall; pw2=W-3*pw; ph2=H/4;
                        vx=3*pw; vy=H-(k+1)*ph2; }
                    /* P5: a root-motion clip displaces the MESH away from the body's
                     * origin -- the wall was orbiting empty air while the co-star crawled out
                     * of frame (caught on the grid). Aim at the actual pelvis. */
                    float bx=g_rmActive?g_rmEndW[0]:g_rvx, bz=g_rmActive?g_rmEndW[1]:g_rvz;
                    float by=g_rmActive?(g_rmEndW[2]*0.85f+0.35f):(1.55f+g_rvAlt*0.8f);
                    M4 pj,vw,pvp;
                    if(g_labFace){
                        /* ================= THE FACE RIG (face=10) ================
                         * Eight angles from a little further out, then one close on her mouth and one
                         * close on her eyes. A jaw that travels 2cm and a lid that
                         * travels 5mm cannot be judged on a body framed at seven
                         * metres -- nor on a lens buried in her hair, which is what
                         * the first attempt gave.
                         *   0-7  the whole head, every 45 degrees around it
                         *   8    the MOUTH   9  the EYES
                         * The head position is the measured one published by the
                         * draw. The mouth and eye offsets are world constants: every
                         * body in the engine is scaled to the same 3.45u height, so a face
                         * occupies the same handful of centimetres on all of them. */
                        const float*HD=g_heroHeadW; int ok=g_heroHeadOK;
                        float fw=g_rvHead;
                        float hx0=ok?HD[0]:bx, hy0=ok?HD[1]:(by+0.9f), hz0=ok?HD[2]:bz;
                        float ax,ay,az,dist,fov;
                        if(gi<8){                       /* the eight, pulled back */
                            ax=hx0+sinf(fw)*0.04f; ay=hy0+0.045f; az=hz0+cosf(fw)*0.04f;
                            dist=1.55f; fov=26.f;
                            float fa=fw+(float)gi*0.7854f;
                            float ex=ax+sinf(fa)*dist, ez=az+cosf(fa)*dist;
                            m4_persp(pj,fov,(float)pw2/(float)ph2,0.05f,40.f);
                            m4_lookat(vw,ex,ay+0.02f,ez, ax,ay,az);
                        } else {
                            /* AIMED AT THE MARKS. The panels didn't know
                             * where her eyes and mouth are: these
                             * were guessed offsets from the head bone, which is why
                             * the panels framed hair. face_marks.py put real marker
                             * nodes in the rig; they ride FK, so this is her mouth
                             * and her eye line in any pose, on any body. */
                            int eyes=(gi==9);
                            const float*MK=eyes?g_heroEyeW:g_heroMouthW;
                            if(g_heroFaceOK){ ax=MK[0]; ay=MK[1]; az=MK[2]; }
                            else { ay=hy0+(eyes?0.085f:0.020f);
                                   ax=hx0+sinf(fw)*0.04f; az=hz0+cosf(fw)*0.04f; }
                            dist=0.60f; fov=22.f;
                            /* ALONG THE FACE, not along the body. Her head yaws inside
                             * the clip, so a lens on the body heading framed her mouth
                             * off-centre and the lateral calibration drifted with
                             * whatever clip was loaded. */
                            float fdx=g_heroFaceOK?g_heroFaceFwd[0]:sinf(fw);
                            float fdz=g_heroFaceOK?g_heroFaceFwd[2]:cosf(fw);
                            float ex=ax+fdx*dist, ez=az+fdz*dist;
                            m4_persp(pj,fov,(float)pw2/(float)ph2,0.02f,40.f);
                            m4_lookat(vw,ex,ay+(eyes?0.010f:0.004f),ez, ax,ay,az); }
                    } else if(gi>=nWall&&nClose){
                        int k=gi-nWall;
                        if(g_labPartner&&g_labPClip>=0){   /* centre on the PAIR's contact */
                            bx+=sinf(g_rvHead)*g_labPGap*0.5f+cosf(g_rvHead)*g_labPSide*0.5f;
                            bz+=cosf(g_rvHead)*g_labPGap*0.5f-sinf(g_rvHead)*g_labPSide*0.5f; }
                        float cy2=by+g_labAimY;            /* ctl aimy=: chest for hands, higher for faces */
                        float ang=g_rvHead+0.7854f+(float)k*1.5708f;
                        float ex=bx+sinf(ang)*2.7f, ez=bz+cosf(ang)*2.7f;
                        m4_persp(pj,30.f,(float)pw2/(float)ph2,0.3f,120.f);
                        m4_lookat(vw,ex,cy2+0.15f,ez, bx,cy2,bz);
                    } else {
                        float ang=(float)gi*6.28318f/(float)nWall;
                        float ex=bx+sinf(g_rvHead+ang)*7.4f, ez=bz+cosf(g_rvHead+ang)*7.4f;
                        m4_persp(pj,40.f,(float)pw2/(float)ph2,0.3f,120.f);
                        m4_lookat(vw,ex,g_rmActive?(by+0.6f):1.95f,ez, bx,by,bz);
                    }
                    m4_mul(pvp,pj,vw);
                    memcpy(g_wallV.vp[gi],pvp,64); g_wallV.eye[gi][0]=-(vw[0]*vw[12]+vw[1]*vw[13]+vw[2]*vw[14]); g_wallV.eye[gi][1]=-(vw[4]*vw[12]+vw[5]*vw[13]+vw[6]*vw[14]); g_wallV.eye[gi][2]=-(vw[8]*vw[12]+vw[9]*vw[13]+vw[10]*vw[14]); g_wallV.x[gi]=vx; g_wallV.y[gi]=H-vy-ph2; g_wallV.w[gi]=pw2; g_wallV.h[gi]=ph2;
                    for(unsigned mi=0;mi<P->nMeshes;mi++){ PMesh*M=&P->meshes[mi];
                        if(!(M->flags&4)||M->boneCount>MAXB||MOFF(P,mi)) continue;
                        /* P7: the panels used to inherit whatever palette the main pass
                         * left bound. That held only while the hero was the last thing
                         * drawn -- the partner binds her own, so the hero's is rebuilt
                         * here for every panel. */
                        bodyPalette(P,mi,world,pal);
                         }
                    /* the partner appears on every angle of the wall too -- an interaction
                     * judged from one side is the single-camera habit the wall replaced */
                    if(g_labPartner&&g_labPClip>=0){
                        g_vkBodySkip=1;   /* the packet has her from the main pass: every panel shows it */
                        labDrawPartner(g_labPartner,g_labPClip,g_labPPhase,model,pal);
                        g_vkBodySkip=0;
                        }
                    /* THE FACE MARKS, per panel. They MUST be drawn in here: the wall
                     * re-renders the body with its own camera for every pane, so a
                     * gizmo drawn once outside the loop lands wherever the MAIN lens
                     * happens to point -- which is exactly what it did, a crosshair
                     * floating over the top of the grid while the mouth panel stayed
                     * bare. The crosshair size is tied to the face (a fraction of the
                     * measured mouth half-width), so it reads the same on a 1.8-unit
                     * rig and a 168-unit one. */
                    if((g_labFace||g_faceDbg)&&P->fmMouth>=0){
                        static float gz[64*6];
                        float s9=P->mouthBind[3]*P->scale*0.55f;
                        if(!(s9>1e-6f)) s9=0.010f;
                        int ng=faceGizmo(P,world,model,gz,64,s9);
                        if(ng>1){
                            gfxBufferData(trailVbo, ng*24, gz, 1);
                            if(gi==0&&!g_noDraw) gfxLines3DOver(gz,ng);
                             } } }
                /* ⭐ THE LEAD. From the dog's collar to the walker's hand -- the partner's
                 * head is published every frame, and a hand is a forearm below it and a
                 * little forward. Drawn on the same line batch the face marks use. */
                if(g_leadN>0){
                    static float lv[LEAD_MAX*12];
                    for(int li=0;li<g_leadN;li++){ float*v=lv+li*12;
                        v[0]=g_leadA[li][0]; v[1]=g_leadA[li][1]; v[2]=g_leadA[li][2];
                        v[3]=0.62f; v[4]=0.47f; v[5]=0.31f;
                        v[6]=g_leadB[li][0]; v[7]=g_leadB[li][1]; v[8]=g_leadB[li][2];
                        v[9]=0.62f; v[10]=0.47f; v[11]=0.31f; }
                    gfxBufferData(trailVbo, (long)(g_leadN*12*sizeof(float)), lv, 1);
                    if(!g_noDraw){ static float q9[LEAD_MAX*6*9]; int n9=0;   /* a lead is three pixels wide at any distance, so a thin strip facing the lens */
                        for(int li=0;li<g_leadN;li++){ const float*a9=g_leadA[li],*b9=g_leadB[li]; float d9[3]={b9[0]-a9[0],b9[1]-a9[1],b9[2]-a9[2]}, e9[3]={g_camEye[0]-a9[0],g_camEye[1]-a9[1],g_camEye[2]-a9[2]}; float s9[3]={d9[1]*e9[2]-d9[2]*e9[1],d9[2]*e9[0]-d9[0]*e9[2],d9[0]*e9[1]-d9[1]*e9[0]}; float sl9=sqrtf(s9[0]*s9[0]+s9[1]*s9[1]+s9[2]*s9[2]); if(sl9<1e-6f) continue; float w9=sqrtf(e9[0]*e9[0]+e9[1]*e9[1]+e9[2]*e9[2])*0.0016f/sl9;
                            const float P9[6][4]={{0,-1,0,0},{1,-1,0,0},{1,1,0,0},{0,-1,0,0},{1,1,0,0},{0,1,0,0}}; for(int k9=0;k9<6;k9++){ float*o9=q9+n9*9; const float t9=P9[k9][0],c9=P9[k9][1]*w9; for(int r9=0;r9<3;r9++) o9[r9]=a9[r9]+d9[r9]*t9+s9[r9]*c9; o9[3]=0.62f; o9[4]=0.47f; o9[5]=0.31f; o9[6]=1.f; o9[7]=0.f; o9[8]=0.f; n9++; } }
                        if(n9) gfxFxTris(q9,n9,GFX_FX_ALPHA,0); }
                     }
                 }
            /* THE PARK and THE BOARD are real objects, drawn solid whatever she is. The board
             * takes her ground point and heading plus her bank so it leans into the carve, but
             * not her pitch or scale. */
            if(g_skate&&(hasPark||hasBoard)){
                if(hasPark){ parkDraw();
                             railDraw(); }
                { static int rep=0; if(!rep++) printf("skate geom: her xz=(%.3f,%.3f) y=%.3f   board xz=(%.3f,%.3f) y=%.3f\n",
                    model[12],model[14],model[13],SK.x,SK.z,SK.y); }
                /* A character who arrived with her own skateboard rides THAT one -- it is
                 * skinned to the same rig, so the animation already knows where it is.
                 * Our prop is only for a rider we are driving ourselves. */
                /* Our prop board is drawn for a rider we drive ourselves, AND for a baked
                 * character who has bailed -- his own board is skinned to his rig, so once
                 * he is off it that mesh is hidden and this stands in where it fell. */
                if(hasBoard && (g_procRig || SK.onFoot)){
                    /* THE BOARD sits in a basis built from the surface itself: up is the
                     * (eased) surface normal, forward is her heading projected into that
                     * plane, and right closes the frame. That is what angles it with the
                     * slope instead of skating it through the concrete.
                     * Trick rotations are applied in the OWN space of the board -- a flip
                     * about its long axis, a shove-it about its up -- so they stay correct
                     * at any lean, and about its centre rather than its wheels. */
                    int drop=(!g_procRig && SK.onFoot);
                    float bu[3]={drop?0.f:SK.up[0], drop?1.f:SK.up[1], drop?0.f:SK.up[2]};
                    float bh=drop?SK.bHead:SK.head;
                    float f8[3]={sinf(bh),0.f,cosf(bh)};
                    float dpu=f8[0]*bu[0]+f8[1]*bu[1]+f8[2]*bu[2];
                    float bf[3]={f8[0]-bu[0]*dpu, f8[1]-bu[1]*dpu, f8[2]-bu[2]*dpu};
                    float bl=sqrtf(bf[0]*bf[0]+bf[1]*bf[1]+bf[2]*bf[2]); if(bl<1e-5f) bl=1.f;
                    bf[0]/=bl; bf[1]/=bl; bf[2]/=bl;
                    float br[3]={bu[1]*bf[2]-bu[2]*bf[1], bu[2]*bf[0]-bu[0]*bf[2],
                                 bu[0]*bf[1]-bu[1]*bf[0]};
                    M4 basis={ br[0],br[1],br[2],0.f,  bu[0],bu[1],bu[2],0.f,
                               bf[0],bf[1],bf[2],0.f,
                               drop?SK.bX:SK.x, drop?parkH(SK.bX,SK.bZ):SK.y, drop?SK.bZ:SK.z, 1.f };
                    float fr=drop?0.f:(SK.flipDeg*0.0174532925f), sv=drop?0.f:(SK.shovDeg*0.0174532925f);
                    float cf=cosf(fr), sf8=sinf(fr), cs=cosf(sv), ss8=sinf(sv);
                    M4 rz={ cf,sf8,0.f,0.f,  -sf8,cf,0.f,0.f,  0.f,0.f,1.f,0.f,  0.f,0.f,0.f,1.f };
                    M4 ry={ cs,0.f,-ss8,0.f, 0.f,1.f,0.f,0.f,  ss8,0.f,cs,0.f,   0.f,0.f,0.f,1.f };
                    M4 tup={1.f,0.f,0.f,0.f, 0.f,1.f,0.f,0.f, 0.f,0.f,1.f,0.f, 0.f, 0.12f,0.f,1.f};
                    M4 tdn={1.f,0.f,0.f,0.f, 0.f,1.f,0.f,0.f, 0.f,0.f,1.f,0.f, 0.f,-0.12f,0.f,1.f};
                    M4 r1,r2,r3,bmodel;
                    m4_mul(r1,rz,ry); m4_mul(r2,tup,r1); m4_mul(r3,r2,tdn);
                    m4_mul(bmodel,basis,r3);
                    drawProp(&boardP,bmodel); }

            }
            /* ⭐ (10-23) THE GLOSSY FLOOR, after every body that reflects in it (the real frame only) */
            if(!vcPass&&g_gloss.on){
                glossFloorPass();
}
            }   /* end vcPass: feed target, then the frame */
            g_vkPassDark=0;
            passMark("hero");
            /* ⭐ THE SMOKE -- after every opaque thing in the real frame (the set, the actors, the cast, the
             * boards, the park), into the frame, far to near */
            if(g_mode==MODE_ANIM){ starDraw(); beamDraw(); smokeTick(); smokeDraw();
                ptStepAll((float)g_animDtS); ptDrawAll(view); dcStepAll((float)g_animDtS); dcDrawAll(); }   /* (step 24) a set's emitters, on the exposure clock: a held exposure holds them */
            if(trBone[1]>=0){ float hx6=world[trBone[1]][12],hy6=world[trBone[1]][13],hz6=world[trBone[1]][14];
                g_boltFrom[0]=model[0]*hx6+model[4]*hy6+model[8]*hz6+model[12];
                g_boltFrom[1]=model[1]*hx6+model[5]*hy6+model[9]*hz6+model[13];
                g_boltFrom[2]=model[2]*hx6+model[6]*hy6+model[10]*hz6+model[14]; }
            /* TRACERS: push her hand/foot world positions into the ribbons */
            for(int k2=0;k2<4;k2++){ if(trBone[k2]<0) continue;
                float bx2=world[trBone[k2]][12], by2=world[trBone[k2]][13], bz2=world[trBone[k2]][14];
                float wx2=model[0]*bx2+model[4]*by2+model[8]*bz2+model[12];
                float wy2=model[1]*bx2+model[5]*by2+model[9]*bz2+model[13];
                float wz2=model[2]*bx2+model[6]*by2+model[10]*bz2+model[14];
                if(trailN[k2]<TRN) trailN[k2]++;
                memmove(trail[k2][0],trail[k2][1],(TRN-1)*12);
                trail[k2][TRN-1][0]=wx2; trail[k2][TRN-1][1]=wy2; trail[k2][TRN-1][2]=wz2; }
            /* draw ribbons: hue-cycling, fading to the tail -- the VENUE's look (her tracers and glowsticks), so never in the lab
             * and never in the zombie cab (M0, 11-01: a woman on a street at dusk does not leave rainbow trails) */
            if(g_mode!=MODE_ANIM&&!GAME_ON){ static float tvb[TRN*6];
              for(int k2=0;k2<4;k2++){ if(trBone[k2]<0||trailN[k2]<2) continue;
                  int n0=TRN-trailN[k2];
                  for(int i2=n0;i2<TRN;i2++){ float f2=(float)(i2-n0)/(float)(trailN[k2]-1);
                      float r3,g3,b3; hsv((float)tsec*0.9f+f2*0.85f,&r3,&g3,&b3);
                      float br=powf(f2,1.4f)*1.25f;   /* the color trail earns its keep */
                      int o2=(i2-n0)*6;
                      tvb[o2]=trail[k2][i2][0]; tvb[o2+1]=trail[k2][i2][1]; tvb[o2+2]=trail[k2][i2][2];
                      tvb[o2+3]=r3*br; tvb[o2+4]=g3*br; tvb[o2+5]=b3*br; }
                  if(!g_noDraw) venueLineStrip(tvb,trailN[k2]);
                  gfxBufferData(trailVbo, trailN[k2]*24, tvb, 1);
                   }
              /* GLOWSTICKS (IN her hands, not through her wrists -- and GLOWING):
               * rod rides the HAND BONE'S OWN AXIS (world col 1 = along the hand), triple-line core */
              for(int k2=0;k2<2;k2++){ if(trBone[k2]<0||trailN[k2]<1) continue;
                  float*hp=trail[k2][TRN-1];
                  float ax=world[trBone[k2]][4], ay=world[trBone[k2]][5], az=world[trBone[k2]][6];
                  float wx3=model[0]*ax+model[4]*ay+model[8]*az;
                  float wy3=model[1]*ax+model[5]*ay+model[9]*az;
                  float wz3=model[2]*ax+model[6]*ay+model[10]*az;
                  float ln=sqrtf(wx3*wx3+wy3*wy3+wz3*wz3); if(ln<1e-5f) continue;
                  wx3/=ln; wy3/=ln; wz3/=ln;
                  float r3,g3,b3; hsv(0.52f+k2*0.35f+(float)tsec*0.02f,&r3,&g3,&b3);
                  float sv[12][6]; int nv2=0;
                  for(int o3=0;o3<3;o3++){ float off=(o3-1)*0.018f; float bb=o3==1?2.6f:1.4f;
                      sv[nv2][0]=hp[0]+off-wx3*0.05f; sv[nv2][1]=hp[1]-wy3*0.05f; sv[nv2][2]=hp[2]+off-wz3*0.05f;
                      sv[nv2][3]=r3*bb; sv[nv2][4]=g3*bb; sv[nv2][5]=b3*bb; nv2++;
                      sv[nv2][0]=hp[0]+off+wx3*0.30f; sv[nv2][1]=hp[1]+wy3*0.30f; sv[nv2][2]=hp[2]+off+wz3*0.30f;
                      sv[nv2][3]=r3*bb; sv[nv2][4]=g3*bb; sv[nv2][5]=b3*bb; nv2++; }
                  if(!g_noDraw) gfxLines3D(&sv[0][0],nv2);
                  gfxBufferData(trailVbo, nv2*24, sv, 1);
                   }
              /* SITH LIGHTNING (purple and magenta): jagged forked bolt,
               * re-rolled every frame = crackle; thin lock-beam while RT holds a target */
              if(g_boltT>0||g_lockTgt>=0){ static float bl[256*6]; int nb=0;
                  float tx7,ty7,tz7;
                  if(g_boltT>0){ tx7=g_boltTo[0]; ty7=g_boltTo[1]; tz7=g_boltTo[2]; }
                  else { tx7=slots[g_lockTgt].x; ty7=g_slotAlt[g_lockTgt]+1.5f; tz7=slots[g_lockTgt].z; }
                  float fx=g_boltFrom[0], fy=g_boltFrom[1], fz=g_boltFrom[2];
                  if(g_boltT>0){
                      for(int st7=0;st7<3;st7++){
                        int SEG=12; float px7=fx,py7=fy,pz7=fz;
                        float wob=0.6f+st7*0.5f;
                        for(int s7=1;s7<=SEG&&nb<230;s7++){ float u7=(float)s7/SEG;
                          float nx7=fx+(tx7-fx)*u7+(frnd()-0.5f)*wob*(1.f-u7*0.6f);
                          float ny7=fy+(ty7-fy)*u7+(frnd()-0.5f)*wob*(1.f-u7*0.6f);
                          float nz7=fz+(tz7-fz)*u7+(frnd()-0.5f)*wob*(1.f-u7*0.6f);
                          float mg=(s7&1)?1.3f:0.7f;
                          bl[nb*6]=px7; bl[nb*6+1]=py7; bl[nb*6+2]=pz7; bl[nb*6+3]=0.8f*mg; bl[nb*6+4]=0.2f; bl[nb*6+5]=1.2f*mg; nb++;
                          bl[nb*6]=nx7; bl[nb*6+1]=ny7; bl[nb*6+2]=nz7; bl[nb*6+3]=1.2f*mg; bl[nb*6+4]=0.25f; bl[nb*6+5]=0.95f*mg; nb++;
                          px7=nx7; py7=ny7; pz7=nz7; } }
                      for(int k7=0;k7<10&&nb<250;k7++){
                          float a9=frnd()*6.28f, e9=frnd()*2.4f, r9=0.4f+frnd()*2.6f*(1.f-g_boltT*0.6f);
                          bl[nb*6]=tx7; bl[nb*6+1]=ty7; bl[nb*6+2]=tz7; bl[nb*6+3]=2.2f; bl[nb*6+4]=0.9f; bl[nb*6+5]=2.4f; nb++;
                          bl[nb*6]=tx7+sinf(a9)*r9; bl[nb*6+1]=ty7+sinf(e9)*r9*0.7f; bl[nb*6+2]=tz7+cosf(a9)*r9;
                          bl[nb*6+3]=1.0f; bl[nb*6+4]=0.2f; bl[nb*6+5]=1.0f; nb++; } }
                  else if(((int)(tsec*6.f))&1){   /* lock beam: dashed magenta thread */
                      bl[0]=fx; bl[1]=fy; bl[2]=fz; bl[3]=0.7f; bl[4]=0.12f; bl[5]=0.7f;
                      bl[6]=tx7; bl[7]=ty7; bl[8]=tz7; bl[9]=0.9f; bl[10]=0.15f; bl[11]=0.9f; nb=2; }
                  if(nb>1&&!g_noDraw) gfxLines3D(bl,nb);
                  }
}
        }

        passMark("ribbons");
        /* ================= GAME STEP (08-25: the ten doors) =====================
         * Runs after the hero and the crowd have moved, before the HUD reads its lines.
         * Each mode is a small loop over things that already exist: it borrows dancers,
         * points them somewhere, and keeps score. Kills are g_slotHide -- the same door
         * the squid sentinel and the eviction bolt already use. */
        if(MODE_PLAYS&&(hasReva||WORLD_OFF)&&!g_pause){ float dtf=(float)dt; if(dtf>0.1f)dtf=0.1f;
            g_gameT+=dtf;   /* the game clock: stops with the start menu, unlike tsec */
            if(GAME_ON){ fxStep(dtf); ptStepAll(dtf); dcStepAll(dtf); if(!g_noDraw&&!WORLD_OFF){ fxDrawAll(view); ptDrawAll(view); dcDrawAll(); } }   /* a game's particles and decals, and (step 24) its emitters and its set's */
            if(GM.msgT>0.f) GM.msgT-=dtf;
            if(GM.ifr>0.f) GM.ifr-=dtf;
            GM.l1[0]=GM.l2[0]=GM.l3[0]=0;
            wpnPickStep(); wpnProjStep(dtf);   /* the weapon layer's shared tick: pickups glow, tracers fly */
            /* ⭐ Y (R) THROWS A CRATE (09-11) -- the rigid layer's first game verb, on any set */
            { static int pY9=0; if(g_btnY&&!pY9&&g_setNI&&!g_skate&&GM.stage==0){
                  float p9[3]={g_rvx+sinf(g_rvHead)*0.7f,1.55f,g_rvz+cosf(g_rvHead)*0.7f};
                  float v9[3]={sinf(g_rvHead)*9.f,3.5f,cosf(g_rvHead)*9.f};
                  if(rbSpawn("crate_1",p9,v9,1.f)>=0) sfxPlay("swing",0.6f,NULL,0); }
              pY9=g_btnY; }
            pnicStep(dtf); crpStep(dtf);       /* the panic layer: the uninvolved scatter; the fallen leave marks */
            bhvStep(dtf);                      /* the behavior channel: strikes expire, the falling finish falling */
            g_dumpNear=wpnNearestFoe(g_rvx,g_rvz,80.f,-1,-1);   /* the instrument tracks whoever is closest */
            if(GM.wCd>0.f) GM.wCd-=dtf;
            switch(g_mode){

            case MODE_GAME: uiFrame(); musicClockStep(); g_game->tick(dtf); talkStep(); apiUiEnd(); faceStep(); unseenClear(); g_game->draw(g_game->state); break;

            case MODE_ANIM: {   /* -------- THE ANIM LAB: one body, measured -------- */
                /* state clock: auto-advance unless REVA_ANIMSTATE pinned one; A steps
                 * forward, X steps back, B resets her to the top of the runway. */
                static int aP=0,xP=0,bP=0;
                if(!GM.flag){ GM.tB+=dtf;
                    if(GM.tB>7.f){ GM.tB=0.f; GM.stage=(GM.stage+1)%LAB_N; g_bcN=0;
                        g_labSlip=g_labTravel=0.f; } }
                /* S8: the confirm that ENTERED the lab could still be held on the first
                 * game frames, and aP's static 0 read it as a fresh edge -- an IDLE gate
                 * run silently walked instead of standing (caught by posturecheck DRIFT;
                 * which binary suffered it was a frame-timing lottery). Arm the buttons
                 * only once every one of them has been seen up inside the mode. */
                if(!g_labArm){ if(!g_btnA&&!g_btnX&&!g_btnB) g_labArm=1; }
                else {
                    if(g_btnA&&!aP){ GM.stage=(GM.stage+1)%LAB_N; GM.tB=0.f; g_bcN=0; g_labSlip=g_labTravel=0.f; }
                    if(g_btnX&&!xP){ GM.stage=(GM.stage+LAB_N-1)%LAB_N; GM.tB=0.f; g_bcN=0; g_labSlip=g_labTravel=0.f; }
                    if(g_btnB&&!bP){ g_rvx=0.f; g_rvz=-150.f; g_bcN=0; g_labSlip=g_labTravel=0.f; } }
                aP=g_btnA; xP=g_btnX; bP=g_btnB;
                if(g_rvz>-6.f){ g_rvz=-150.f; g_bcN=0; }   /* end of the runway: back to the top */
                /* the weapon under test rides along only where it belongs */
                GM.wKind=(GM.stage==LAB_SWING)?WPN_MELEE:(GM.stage==LAB_GUN)?WPN_PISTOL:0;
                if(GM.wKind&&GM.wVar<0) GM.wVar=wpnPickDef(GM.wKind,0u);
                if(GM.stage!=GM.count){ GM.count=GM.stage; GM.wVar=wpnPickDef(GM.wKind,0u); }
                if(GM.stage==LAB_SWING){ GM.tA-=dtf; if(GM.tA<=0.f) GM.tA=0.25f; }   /* swing on a loop */
                else if(GM.stage==LAB_GUN){ GM.wAmmo=99; GM.wCd-=dtf;
                    if(GM.wCd<=0.f) GM.wCd=WPN_CD[WPN_PISTOL]; }
                else GM.tA=0.f;
                /* ---- THE SCENE CLOCK. One clock drives the mouths, the subtitle and
                 * the cuts, and it is the same clock the wav mix was built from. The
                 * jaw weight is the speaker's own loudness at 30Hz; the LISTENER gets a
                 * slow brow so she is a person waiting to answer, not a mannequin. */
                if(g_scnT>=0.f){
                    /* REVA_SCENEFPS=<n>: advance the scene by a FIXED step per rendered
                     * frame instead of by real time. A capture writes 2.7MB per frame
                     * and cannot hold 30fps, so on the wall clock the scene would skip
                     * -- and the recorded frames would not line up with the wav they are
                     * muxed against. With a fixed step, frame N is scene second N/n by
                     * construction, and picture and sound land together in the file. */
                    static float sfps=-1.f;
                    if(sfps<0.f){ const char*e9=getenv("REVA_SCENEFPS"); sfps=e9?(float)atof(e9):0.f; }
                    g_scnT+=(sfps>0.f)?(1.f/sfps):dtf;
                    /* ---- THE STAGING. The line that is speaking owns the stage, and a
                     * SILENCE belongs to whoever just spoke -- so during a beat the last
                     * line keeps the camera and the bodies rather than snapping to a
                     * default nobody asked for. */
                    { static int held8=-1; int la8=scnStageLine();
                      /* ⭐ WHO IS BEING ANSWERED. Updated only when the LINE changes, never
                       * per frame: a silence belongs to whoever just spoke, so the listener
                       * must not change during a beat either -- a held shot cannot swing
                       * mid-pause. */
                      if(la8>=0&&la8!=held8&&held8>=0&&g_scnL[held8].spk!=g_scnL[la8].spk)
                          g_scnPrevSpk=g_scnL[held8].spk;
                      if(la8>=0) held8=la8;
                      if(held8>=0){ ScnLine*S8=&g_scnL[held8];
                        g_scnLook[0]=S8->hlook; g_scnLook[1]=S8->plook;
                        g_scnShot=(g_shotForce>=0)?g_shotForce:S8->shot;   /* ⭐ the ctl may override the size */
                        /* the KEY and the RIM sit relative to the camera, so they flip
                         * with it -- which is exactly what re-lighting for the reverse
                         * is, and it happens on the cut where nobody can see it. */
                        if(S8->spk<SCN_VOICE) g_scnSide=(S8->spk==0)?1.f:-1.f;
                        g_scnBrow[0]=S8->hbrow; g_scnBrow[1]=S8->pbrow;
                        g_scnHgt=S8->hgt;
                        /* CLIPS switch on the cut; positions EASE toward the new mark.
                         * (0.42s time constant: fast enough to have arrived inside a
                         * three-second line, slow enough that a 134-degree turn is a
                         * turn and not a snap.) */
                        if(held8!=g_scnStaged){ g_scnStaged=held8;
                            /* ⭐ 10-22: A LINE ON ANOTHER SET. set=<name> plays this one line on another set -- a
                             * cutaway: the ship outside the sick bay -- and the next line without one is HOME again,
                             * the set the scene was staged on. Each set brings its own light (its sun/sky lines);
                             * home's is put back exactly as the scene left it. */
                            if(!g_scnHomeOK){ g_scnHomeOK=1; snprintf(g_scnHome,sizeof g_scnHome,"%s",g_setCur); g_scnAway[0]=0; }
                            /* ⛔ a set the CTL loaded mid-scene is neither home nor our cutaway: it is the NEW home, or
                             * the next cut would drag the scene back to a set nobody asked for */
                            else if(strcmp(g_setCur,g_scnHome)&&strcmp(g_setCur,g_scnAway)){
                                snprintf(g_scnHome,sizeof g_scnHome,"%s",g_setCur); g_scnAway[0]=0; }
                            /* ⭐ home=: the scene's home set changes from this line on (the boys put in his pan once he
                             * is out of it). Its light is the old home's, as the scene had it. */
                            if(S8->home[0]&&strcmp(S8->home,g_scnHome)){
                                if(!strcmp(g_setCur,g_scnHome)){ memcpy(g_scnHomeSun,g_sun,16); memcpy(g_scnHomeSunC,g_sunC,12); memcpy(g_scnHomeSky,g_sky,12); }
                                printf("[scene] line %d: home is now '%s' (was '%s')\n",held8,S8->home,g_scnHome); fflush(stdout);
                                snprintf(g_scnHome,sizeof g_scnHome,"%s",S8->home); }
                            { const char*want12=S8->set[0]?S8->set:g_scnHome;
                              if(want12[0]&&strcmp(want12,g_setCur)){
                                  if(!strcmp(g_setCur,g_scnHome)){ memcpy(g_scnHomeSun,g_sun,16); memcpy(g_scnHomeSunC,g_sunC,12); memcpy(g_scnHomeSky,g_sky,12); }
                                  setLoad(want12);
                                  snprintf(g_scnAway,sizeof g_scnAway,"%s",strcmp(want12,g_scnHome)?want12:"");
                                  if(!strcmp(want12,g_scnHome)){ memcpy(g_sun,g_scnHomeSun,16); memcpy(g_sunC,g_scnHomeSunC,12); memcpy(g_sky,g_scnHomeSky,12); }
                                  printf("[scene] line %d: set %s%s\n",held8,want12,strcmp(want12,g_scnHome)?" -- a cutaway":" -- home"); fflush(stdout); } }
                            /* ⭐ 10-22: DRESSED ON THE CUT -- houtfit= etc., each slot's own pack (a man into his tunic) */
                            { static char wore12[4][24];
                              for(int o12=0;o12<4;o12++){ const char*of=S8->outfit[o12]; if(!of[0]||!strcmp(of,wore12[o12])) continue;
                                  Pack*pk12=(o12==0)?g_heroPk:g_cast[o12].pk;
                                  if(pk12){ wardWear(pk12,of); snprintf(wore12[o12],24,"%s",of);
                                      printf("[scene] line %d: slot %d dressed in '%s'\n",held8,o12,of); fflush(stdout); } } }
                            /* (step 23) eyes that glow, on the cut: heyes= peyes= p2eyes= p3eyes= */
                            { static char had12[4][28];
                              for(int o12=0;o12<4;o12++){ const char*ey=S8->eyes[o12]; if(!ey[0]||!strcmp(ey,had12[o12])) continue;
                                  eyesSay(o12?g_castEyeG[o12]:g_heroEye,ey); snprintf(had12[o12],28,"%s",ey); printf("[scene] line %d: slot %d eyes %s\n",held8,o12,ey); fflush(stdout); } }
                            /* ⭐ 10-22: THE ROLL on the cut -- a new spec starts it, the same one carried on keeps it running */
                            for(int o12=0;o12<4&&o12<SCN_MAXCAST;o12++){ const char*rs=S8->roll[o12]; CastRoll*CR=&g_castRoll[o12];
                              if(!rs[0]){ if(CR->on){ CR->on=0; CR->spec[0]=0; printf("[roll] line %d: slot %d stands again\n",held8,o12); fflush(stdout); } continue; }
                              if(!strcmp(rs,CR->spec)) continue;
                              snprintf(CR->spec,sizeof CR->spec,"%s",rs);
                              if(!strcmp(rs,"off")){ CR->on=0; continue; }
                              float v12[8]={0,0,0,1,0,0,0,0}; int n12=sscanf(rs,"%f,%f,%f,%f,%f,%f,%f,%f",v12,v12+1,v12+2,v12+3,v12+4,v12+5,v12+6,v12+7);
                              float dl=sqrtf(v12[3]*v12[3]+v12[4]*v12[4]+v12[5]*v12[5]);
                              if(n12<7||dl<1e-6f){ printf("[roll] line %d: slot %d '%s' wants ax,ay,az,dx,dy,dz,degps[,deg0] -- IGNORED\n",held8,o12,rs); CR->on=0; continue; }
                              for(int k=0;k<3;k++){ CR->a[k]=v12[k]; CR->d[k]=v12[3+k]/dl; }
                              CR->rate=v12[6]*0.0174533f; CR->a0=v12[7]*0.0174533f; CR->t0=scnBodyT(); CR->on=1;
                              printf("[roll] line %d: slot %d turns round (%.1f,%.1f,%.1f)+(%.2f,%.2f,%.2f) at %.1f deg/s from %.0f\n",
                                     held8,o12,v12[0],v12[1],v12[2],CR->d[0],CR->d[1],CR->d[2],v12[6],v12[7]); fflush(stdout); }
                            trickArm(held8);   /* ⭐ last line's tricks hand their bodies back; this line's are armed */
                            puffArm(held8);    /* ⭐ this line's exhales */
                            dollArm(held8);    /* ⭐ this line's falls */
                            /* ⭐ THE EXPOSURE RATE CHANGES ON THE CUT, like the lens and the clips. */
                            if(S8->feed>=0.f&&S8->feed!=g_tape){ g_tape=S8->feed; if(g_tape>0.f&&g_post<=0) g_post=1;
                                printf("[feed] line %d: %s\n",held8,(g_tape>1.5f)?"HER PHONE, TURNED SIDEWAYS":((g_tape>0.f)?"HER PHONE, PORTRAIT":"out of the phone")); fflush(stdout); }
                            /* (step 25) the lens's roll, its focus and shutter, and film's sepia and grain: sticky, and they land on the cut */
                            if(S8->camRoll>-9000.f&&S8->camRoll!=g_camRoll){ g_camRoll=S8->camRoll; printf("[lens] line %d: rolled %.1f\n",held8,g_camRoll); fflush(stdout); }
                            if(S8->focusSaid[0]){ lensFocusSay(S8->focusSaid); }
                            if(S8->shut>-9000.f) g_lensShutter=S8->shut;
                            if(S8->sepia>-9000.f) g_filmSepia=S8->sepia;
                            if(S8->grain>-9000.f) g_filmGrain=S8->grain;
                            if(S8->fish>=0.f&&S8->fish!=g_fish){ g_fish=S8->fish; if(g_fish>0.f&&g_post<=0) g_post=1;
                                printf("[fish] line %d: %.2f\n",held8,g_fish); fflush(stdout); }
                            if(S8->step>=0.f&&S8->step!=g_stepFps){ g_stepFps=S8->step;
                                printf("[step] line %d: %.0f exposures/sec%s\n",held8,g_stepFps,g_stepFps>0.f?"":" (off -- on ones)");
                                fflush(stdout); }
                            /* ⭐ THE LIGHT CHANGES ON THE CUT TOO. A time of day is a property of the
                             * SHOT, so a scene can be shot "on Thursday" line by line -- which is a
                             * continuity error if nobody meant it and a joke if somebody did. */
                            if(S8->tod>=0.f&&(S8->tod!=g_tod||g_todRate!=0.f)){ g_tod=S8->tod; g_todRate=0.f; todApply(g_tod);
                                printf("[tod] line %d: %05.2f\n",held8,g_tod); fflush(stdout); }
                            if(S8->tone>=0.f&&((S8->tone>0.90f)?0.90f:S8->tone)!=g_tone){ g_tone=(S8->tone>0.90f)?0.90f:S8->tone;
                                printf("[tone] line %d: knee %.2f\n",held8,g_tone); fflush(stdout); }
                            if(S8->snap){ g_scnSnap=1; if(g_hsSnapCut) g_hsCutPending=2; printf("[snap] line %d: marks land on the cut\n",held8); fflush(stdout); }   /* (11-11) and every head's hair is hung again on it (strands.inc) */
                            /* ⭐ CROSSING THE LINE IS A CUT, like everything else here. */
                            if(S8->reverse!=g_scnRev){ g_scnRev=S8->reverse;
                                printf("[lens] line %d reverse=%d\n",held8,g_scnRev); fflush(stdout); }
                            /* ⭐ THE PUPPET JAW'S GAIN. It only does anything in `mouth hinge` mode
                             * (no viseme track), so a scene that has visemes is told the key did
                             * nothing rather than left to wonder. */
                            if(S8->jawgain>=0.f&&S8->jawgain!=g_jawGain){ g_jawGain=S8->jawgain;
                                printf("[jaw] gain %.2f\n",g_jawGain); fflush(stdout);
                                if(g_jawGain!=1.f&&g_scnL[held8].nVis>0)
                                    printf("[jaw] ... but this scene has a VISEME track -- jawgain only scales the `mouth hinge` jaw\n");
                                fflush(stdout); }
                            /* ⭐ CAST BY ROLE, NOT BY NAME. `auto` means: whoever is
                             * SPEAKING this line gestures (x_convo1) and whoever is
                             * listening listens (x_convo2), swapping every line. A script
                             * that names a clip still wins. Borrowed off the bank through
                             * the canonical transfer, so it works on any canon rig. */
                            const char*hcl=S8->hclip; const char*pcl=S8->pclip;
                            char hcb[40];   /* (step 28, found by the memory checker on an unbroken scene) the clip's name without its `@` or `>`: hcl points at it from here on, so it lives as long as hcl does. It was inside the block below, and what's said about the clip after that block read a buffer that was gone */
                            if(!strcmp(hcl,"auto")) hcl=(S8->spk==0)?"x_convo1":"x_convo2";
                            if(!strcmp(pcl,"auto")) pcl=(S8->spk==0)?"x_convo2":"x_convo1";
                            if(g_heroPk&&hcl[0]){
                                /* ⭐ `name@0.6` on the hero as well -- see the partner path. */
                                g_heroHold=-1.f; float hCueF=-1.f,hCueS=-1.f; const int wr0=g_labWphReset;
                                { int k7=0; const char*a7=hcl;
                                  while(*a7&&*a7!='@'&&*a7!='>'&&k7<39) hcb[k7++]=*a7++;
                                  hcb[k7]=0;
                                  if(*a7=='@') g_heroHold=(float)atof(a7+1);
                                  else if(*a7=='>'){ const char*v7=a7+1; const size_t L7=strlen(v7);   /* (step 21) `name>12.5s`, `name>0.33`: start there and play on, as the partner's `>` does */
                                      if(L7&&v7[L7-1]=='s') hCueS=(float)atof(v7); else { hCueF=(float)atof(v7); if(hCueF<0.f) hCueF=0.f; if(hCueF>1.f) hCueF=1.f; } } }
                                hcl=hcb;
                                int f8=-1; for(unsigned c8=0;c8<g_heroPk->nClips;c8++)
                                    if(!strcmp(packStr(g_heroPk,g_heroPk->clips[c8].name),hcl)){ f8=(int)c8; break; }
                                if(f8>=0&&f8!=g_labClip){ g_labClip=f8; g_labDnC=-1; g_labWphReset=1;
                                    g_labRM=(strncmp(hcl,"z_rm_",5)==0); g_rmY=-999.f; }   /* ⭐ a scene's z_rm_ clip owns Y too */
                                else if(f8<0){ int bc=-1; Pack*bp=bankFind(hcl,&bc);
                                    if(bp&&g_heroPk->cbOK&&g_heroPk->pbOK>=10){
                                        g_labDnP=bp; g_labDnC=bc; g_labClip=-1; g_labWphReset=1;
                                        g_labRM=(strncmp(hcl,"z_rm_",5)==0); g_rmY=-999.f; }
                                    else printf("[stage] hclip '%s' NOT FOUND\n",hcl); }
                                else { g_labDnP=NULL; }
                                if(hCueF>=0.f||hCueS>=0.f){ g_heroCueF=hCueF; g_heroCueS=hCueS; g_labWphReset=1; }   /* a cue starts it there even when it's the clip already playing */
                                else if(g_labWphReset&&!wr0) g_heroStartOff=1; }                                    /* a line's new clip: the set's `clipstart` applies */
                            /* ⭐ SLOT 2'S CLIP, resolved exactly as slot 1's is -- her own
                             * pack first, then the bank through the canonical transfer --
                             * by lending her g_castI for the length of the lookup. */
                            /* ⭐ WHAT EACH OF THEM IS CARRYING. Sticky, and `off` clears
                             * it, because a woman has to be able to put a coffee down. */
                            if(S8->hmark[0])  snprintf(g_castMark[0],32,"%s",S8->hmark);
                            /* ⭐ the layers, sticky; a name may carry @w */
                            { const char*ov[3]={S8->hover,S8->pover,S8->p2over};
                              for(int s9=0;s9<3;s9++) if(ov[s9][0]){ const char*at=strchr(ov[s9],'@');
                                  int k=0; while(ov[s9][k]&&ov[s9]+k!=at&&k<31){ g_overClip[s9][k]=ov[s9][k]; k++; } g_overClip[s9][k]=0;
                                  g_overW[s9]=at?(float)atof(at+1):1.f;
                                  printf("[over] line %d: slot %d layer '%s' w %.2f\n",held8,s9,g_overClip[s9],g_overW[s9]); } }
                            /* ⭐ THE SCREEN MOVES ON THE CUT. No ease -- a telly does not
                             * rotate itself upright over 1.4 seconds, an animator picks it up
                             * between exposures. Sticky, like every other staging key. */
                            /* ⭐ THE AUTHORED LENS. Placed on the line, cleared on the
                             * line -- never left armed for the next one. */
                            if(S8->camHas==1){
                                for(int k8=0;k8<3;k8++){ g_camPos[k8]=S8->cam[k8]; g_camAim[k8]=S8->cam[3+k8]; }
                                g_camFov=(S8->cam[6]>0.f)?S8->cam[6]:-1.f; g_camOn=1;
                                printf("[cam] line %d: eye %.1f,%.1f,%.1f -> aim %.1f,%.1f,%.1f fov %.0f\n",
                                       held8,g_camPos[0],g_camPos[1],g_camPos[2],
                                       g_camAim[0],g_camAim[1],g_camAim[2],g_camFov); fflush(stdout); }
                            else if(S8->camHas==2&&g_camOn){ g_camOn=0;
                                printf("[cam] line %d: off -- back to the shot ladder\n",held8); fflush(stdout); }
                            if(S8->scrHas>=3&&g_scrN>0){
                                g_scr[0][0]=S8->scr[0]; g_scr[0][1]=S8->scr[1]; g_scr[0][2]=S8->scr[2];
                                if(S8->scrHas>=4) g_scr[0][3]=S8->scr[3]*0.0174533f;
                                if(S8->scrHas>=5) g_scr[0][4]=S8->scr[4]*0.0174533f;
                                if(S8->scrHas>=6) g_scr[0][5]=S8->scr[5];
                                if(S8->scrHas>=7) g_scr[0][6]=S8->scr[6];
                                printf("[screen] line %d -> (%.2f %.2f %.2f) yaw %.0f pitch %.0f\n",
                                       held8,g_scr[0][0],g_scr[0][1],g_scr[0][2],
                                       g_scr[0][3]*57.2958f,g_scr[0][4]*57.2958f); fflush(stdout); }
                            if(S8->pmark[0])  snprintf(g_castMark[1],32,"%s",S8->pmark);
                            if(S8->p2mark[0]) snprintf(g_castMark[2],32,"%s",S8->p2mark);
                            if(S8->p3mark[0]) snprintf(g_castMark[3],32,"%s",S8->p3mark);
                            /* ⭐ PATHS. The compiler carries a path on every line while it is in force, so a
                             * record WITHOUT one means none is (a body mid-roll still finishes). A path STARTS when
                             * its run's first line is staged (pseg) -- or earlier, handed on by the path before it. */
                            for(int s10=0;s10<4;s10++){
                                /* ⭐ a run handed on EARLY keeps its own line's path, speed and loop until that
                                 * line (or a later one, or an `off`) is staged -- the lines between carry the run that
                                 * already ran out, and re-applying them braked the filmer's orbit after one lap */
                                if(g_locoEarly[s10]>=0){
                                    if(S8->pseg[s10]<g_locoEarly[s10]&&strcmp(S8->path[s10],"off")) continue;
                                    g_locoEarly[s10]=-1; }
                                snprintf(g_castPath[s10],sizeof g_castPath[s10],"%s",S8->path[s10]);
                                g_castLoop[s10]=(S8->loop[s10]>0); g_castPathSeg[s10]=S8->pseg[s10];
                                if(S8->spd[s10]>0.f) g_castSpd[s10]=S8->spd[s10]; }
                            /* ⭐ THE HALF-PIPE. Carried by the compiler while in force, like a path: a record
                             * without one means none is, and the ride (locoTick) ends. The same name rides on; a new
                             * one drops in again. */
                            for(int s11=0;s11<4;s11++){
                                snprintf(g_castPipe[s11],sizeof g_castPipe[s11],"%s",S8->pipe[s11]);
                                snprintf(g_castPipeAir[s11],sizeof g_castPipeAir[s11],"%s",S8->pipeAir[s11]); }
                            /* ⭐ THE TRACKING LENS. camtrack follows a body's CONTINUOUS spot from a distance,
                             * height and azimuth off its travel; campan keeps the pinned cam= eye and turns the aim
                             * onto the body. Sticky (the compiler carries them) until camtrack=off / campan=off or
                             * a line with cam=. A NEW framing is a cut; the same one carried onto a line is held. */
                            /* ⭐ AND THE MOVING LENSES, in the same hand-over. cammove eases the line's cam= to its
                             * own pose (an event: the compiler carries its END on as the next line's cam=, so the lens
                             * stays where the move put it); camorbit is sticky like camtrack; camshake rides on top of
                             * whichever placed lens is up. One lens mode per line -- the compiler refuses two. */
                            { CamTrk*T10=&g_camTrk; int was10=T10->mode;
                              static const char*const LNM11[5]={"lens","camtrack","campan","cammove","camorbit"};
                              /* a cam= that IS the move's end: carried on if the move is still running (camsecs= longer
                               * than its line), merely "done" if it has arrived */
                              int end11=(was10==3&&S8->camHas==1&&!S8->movHas);
                              if(end11) for(int k11=0;k11<6;k11++) if(fabsf(S8->cam[k11]-T10->mB[k11])>1e-3f) end11=0;
                              int carry11=(end11&&T10->mt<T10->mT);
                              if(S8->camHas&&!carry11) T10->mode=0;
                              if(S8->trkHas==2&&T10->mode==1) T10->mode=0;
                              if(S8->panHas==2&&T10->mode==2) T10->mode=0;
                              if(S8->orbHas==2&&T10->mode==4) T10->mode=0;
                              if(S8->hldHas==2&&T10->mode==5) T10->mode=0;
                              if(carry11){ printf("[lens] line %d: cammove carried on -- %.2f s of it left\n",held8,T10->mT-T10->mt); fflush(stdout); }
                              if(S8->hldHas==1){   /* ⭐ THE FILMER'S OWN CAMERA */
                                  int fl12=(int)S8->hld[0], sb12=(int)S8->hld[1];
                                  if(fl12<0) fl12=0;
                                  if(fl12>SCN_BODIES-1) fl12=SCN_BODIES-1;
                                  if(sb12<0) sb12=0;
                                  if(sb12>SCN_BODIES-1) sb12=SCN_BODIES-1;
                                  float fv12=(S8->hldN>=3)?S8->hld[2]:-1.f, ah12=(S8->hldN>=4)?S8->hld[3]:1.4f;
                                  float dp12=(S8->hldN>=5)?S8->hld[4]:0.f; if(dp12<0.f) dp12=0.f; if(dp12>1.4f) dp12=1.4f;
                                  int same12=(was10==5&&T10->holder==fl12&&T10->slot==sb12&&T10->fov==fv12&&T10->aimh==ah12&&T10->drop==dp12);
                                  T10->holder=fl12; T10->slot=sb12; T10->fov=fv12; T10->aimh=ah12; T10->drop=dp12; T10->mode=5; T10->line=held8;
                                  if(!same12) T10->snap=1;
                                  g_camOn=1; g_camMoveOn=0; if(fv12>0.f) g_camFov=fv12;
                                  printf("[lens] line %d camhold: slot %d is filming slot %d%s aimh %.2f%s %s\n",
                                         held8,fl12,sb12,(fv12>0.f)?" (fov set)":"",ah12,(dp12>0.01f)?" -- HELD LOW":"",same12?"(held)":"(cut)"); fflush(stdout); }
                              else if(S8->trkHas==1){
                                  int sl10=(int)S8->trk[0]; if(sl10<0) sl10=0; if(sl10>SCN_BODIES-1) sl10=SCN_BODIES-1;
                                  float d10=(S8->trk[1]<0.5f)?0.5f:S8->trk[1], ah10=(S8->trkN>=6)?S8->trk[5]:1.6f;
                                  int same10=(was10==1&&T10->slot==sl10&&T10->dist==d10&&T10->height==S8->trk[2]&&T10->habs==S8->trkAbs
                                              &&T10->azim==S8->trk[3]&&T10->fov==S8->trk[4]&&T10->aimh==ah10);
                                  T10->slot=sl10; T10->dist=d10; T10->height=S8->trk[2]; T10->habs=S8->trkAbs; T10->azim=S8->trk[3];
                                  T10->fov=S8->trk[4]; T10->aimh=ah10; T10->mode=1; T10->line=held8;
                                  if(!same10) T10->snap=1;
                                  g_camOn=1; g_camMoveOn=0; if(T10->fov>0.f) g_camFov=T10->fov;
                                  printf("[lens] line %d camtrack slot %d dist %.2f height %s%.2f%s azim %.0f fov %.0f aimh %.2f %s\n",
                                         held8,sl10,d10,T10->habs?"=":"",T10->height,T10->habs?" (absolute)":"",T10->azim,T10->fov,ah10,
                                         same10?"(held)":"(cut)"); fflush(stdout); }
                              else if(S8->panHas==1){
                                  int sl10=(int)S8->pan[0]; if(sl10<0) sl10=0; if(sl10>SCN_BODIES-1) sl10=SCN_BODIES-1;
                                  float ah10=(S8->panN>=2)?S8->pan[1]:1.6f, e10[3];
                                  if(S8->camHas==1) memcpy(e10,S8->cam,12);
                                  else if(was10==2) memcpy(e10,T10->eye,12);
                                  else { memcpy(e10,g_camOn?g_camPos:g_lastE,12);
                                         printf("[lens] line %d: campan with no cam= on its line -- it pans from the lens it has\n",held8); }
                                  int same10=(was10==2&&T10->slot==sl10&&T10->aimh==ah10&&!memcmp(e10,T10->eye,12));
                                  T10->slot=sl10; T10->aimh=ah10; memcpy(T10->eye,e10,12); T10->mode=2; T10->line=held8;
                                  if(!same10) T10->snap=1;
                                  g_camOn=1; g_camMoveOn=0;
                                  printf("[lens] line %d campan slot %d aimh %.2f eye %.2f,%.2f,%.2f %s\n",
                                         held8,sl10,ah10,e10[0],e10[1],e10[2],same10?"(held)":"(cut)"); fflush(stdout); }
                              else if(S8->orbHas==1){   /* ⭐ THE REPLAY ORBIT */
                                  int sl11=(int)S8->orb[0]; if(sl11<0) sl11=0; if(sl11>SCN_BODIES-1) sl11=SCN_BODIES-1;
                                  float r11=(S8->orb[1]<0.5f)?0.5f:S8->orb[1];
                                  /* ⭐ 10-22: round a WORLD point (camat=) the aim is the point itself unless aimh says */
                                  float fv11=(S8->orbN>=6)?S8->orb[5]:-1.f, ah11=(S8->orbN>=7)?S8->orb[6]:(S8->atHas?0.f:1.6f);
                                  int same11=(was10==4&&T10->slot==sl11&&T10->dist==r11&&T10->height==S8->orb[2]&&T10->habs==S8->orbAbs
                                              &&T10->d0==S8->orb[3]&&T10->d1==S8->orb[4]&&T10->fov==fv11&&T10->aimh==ah11
                                              &&T10->wc==S8->atHas&&(!S8->atHas||!memcmp(T10->wcP,S8->at,12)));
                                  /* a sweep that starts where the last one ended, round the same body, is not a cut */
                                  int cont11=(!same11&&was10==4&&T10->slot==sl11&&T10->aimh==ah11&&T10->d1==S8->orb[3]);
                                  if(!same11){ T10->mt=0.f; T10->mT=(S8->secs>0.f)?S8->secs:scnLineSpan(held8); }
                                  if(!same11&&!cont11) T10->snap=1;
                                  T10->slot=sl11; T10->dist=r11; T10->height=S8->orb[2]; T10->habs=S8->orbAbs;
                                  T10->d0=S8->orb[3]; T10->d1=S8->orb[4]; T10->fov=fv11; T10->aimh=ah11; T10->mode=4; T10->line=held8;
                                  T10->wc=S8->atHas; memcpy(T10->wcP,S8->at,12);
                                  if(T10->wc){ printf("[lens] line %d camorbit round the world point %.1f,%.1f,%.1f\n",held8,T10->wcP[0],T10->wcP[1],T10->wcP[2]); fflush(stdout); }
                                  g_camOn=1; g_camMoveOn=0; if(fv11>0.f) g_camFov=fv11;
                                  printf("[lens] line %d camorbit slot %d radius %.2f height %s%.2f heading %.0f -> %.0f over %.2f s fov %.0f aimh %.2f %s\n",
                                         held8,sl11,r11,T10->habs?"=":"",T10->height,T10->d0,T10->d1,T10->mT,fv11,ah11,
                                         same11?"(held)":(cont11?"(carried on)":"(cut)")); fflush(stdout); }
                              else if(S8->movHas==1){   /* ⭐ THE DOLLY / CRANE / PUSH-IN */
                                  float A11[7];
                                  if(S8->camHas==1) memcpy(A11,S8->cam,sizeof A11);
                                  else { memcpy(A11,g_camOn?g_camPos:g_lastE,12); memcpy(A11+3,g_camOn?g_camAim:g_lastC,12);
                                         A11[6]=g_camOn?g_camFov:-1.f;
                                         printf("[lens] line %d: cammove with no cam= on its line -- it moves from the lens it has\n",held8); }
                                  memcpy(T10->mA,A11,sizeof A11); memcpy(T10->mB,S8->mov,sizeof A11);
                                  /* a fov on one end only: no end fov HOLDS the start's; no start fov starts from the one
                                   * the projection was drawing with none */
                                  if(!(T10->mB[6]>0.f)) T10->mB[6]=T10->mA[6];
                                  else if(!(T10->mA[6]>0.f)) T10->mA[6]=lensFovNone();
                                  T10->mt=0.f; T10->mT=(S8->secs>0.f)?S8->secs:scnLineSpan(held8);
                                  T10->mode=3; T10->line=held8;
                                  memcpy(g_camPos,T10->mA,12); memcpy(g_camAim,T10->mA+3,12);
                                  g_camFov=(T10->mA[6]>0.f)?T10->mA[6]:-1.f; g_camOn=1; g_camMoveOn=0;
                                  printf("[lens] line %d cammove over %.2f s: eye %.2f,%.2f,%.2f -> %.2f,%.2f,%.2f aim %.2f,%.2f,%.2f -> %.2f,%.2f,%.2f fov %.0f -> %.0f\n",
                                         held8,T10->mT,T10->mA[0],T10->mA[1],T10->mA[2],T10->mB[0],T10->mB[1],T10->mB[2],
                                         T10->mA[3],T10->mA[4],T10->mA[5],T10->mB[3],T10->mB[4],T10->mB[5],T10->mA[6],T10->mB[6]); fflush(stdout); }
                              else if(was10&&!T10->mode){
                                  printf("[lens] line %d: %s %s -- %s\n",held8,LNM11[(was10>0&&was10<5)?was10:0],end11?"done":"off",
                                         end11?"the lens holds where the move put it":(S8->camHas==1?"the line's cam= takes the lens":
                                         (g_camOn?"the lens holds where it was":"back to the shot ladder"))); fflush(stdout); }
                              /* ⭐ camshake, sticky -- said again with the same numbers it is not news */
                              if(S8->shkHas){
                                  float a11=(S8->shk[0]>0.f)?S8->shk[0]:0.f, h11=(S8->shk[1]>0.f)?S8->shk[1]:1.5f;
                                  if(a11!=g_camShk[0]||h11!=g_camShk[1]){ g_camShk[0]=a11; g_camShk[1]=h11;
                                      if(a11>0.f) printf("[lens] line %d camshake amp %.2f u hz %.2f%s\n",held8,a11,h11,
                                                         g_camOn?"":" -- but no placed lens is up: it rides cam/cammove/camtrack/campan/camorbit only");
                                      else printf("[lens] line %d camshake off\n",held8);
                                      fflush(stdout); } } }
                            if(S8->hprop[0])  snprintf(g_castProp[0],PROP_SPEC,"%s",S8->hprop);
                            if(S8->pprop[0])  snprintf(g_castProp[1],PROP_SPEC,"%s",S8->pprop);
                            if(S8->p2prop[0]) snprintf(g_castProp[2],PROP_SPEC,"%s",S8->p2prop);
                            /* ⭐ and the other hand, sticky the same way, `off` clears it */
                            if(S8->hprop2[0])  snprintf(g_castProp2[0],PROP_SPEC,"%s",S8->hprop2);
                            if(S8->pprop2[0])  snprintf(g_castProp2[1],PROP_SPEC,"%s",S8->pprop2);
                            if(S8->p2prop2[0]) snprintf(g_castProp2[2],PROP_SPEC,"%s",S8->p2prop2);
                            /* ⭐ (11-11) slots 2 and 3 read a clip's `@` hold and `>` cue as the partner's does. They matched the WHOLE string against
                             * the clip names, so the music video run's riders (`p2clip=z_rm_fv_rider>5.58s`) found nothing, said nothing, and stood in
                             * z_rest across the pourer. ⛔ Cast EXPLICITLY or not at all (`auto` doesn't resolve past slot 1) */
                            for(int s9=2;s9<=3;s9++){ const char*sp9=(s9==2)?S8->p2clip:S8->p3clip;
                                if(!g_cast[s9].pk||!sp9[0]) continue;
                                const int sv9=g_castI; g_castI=s9;
                                char cb9[40]; float h9=-1.f, q9=-1.f, qs9=-1.f;
                                { int k9=0; const char*a9=sp9;
                                  while(*a9&&*a9!='@'&&*a9!='>'&&k9<39) cb9[k9++]=*a9++;
                                  cb9[k9]=0;
                                  if(*a9=='@') h9=(float)atof(a9+1);
                                  else if(*a9=='>'){ const char*v9=a9+1; const size_t L9=strlen(v9);
                                      if(L9&&v9[L9-1]=='s') qs9=(float)atof(v9); else { q9=(float)atof(v9); if(q9<0.f) q9=0.f; if(q9>1.f) q9=1.f; } } }
                                int f9=-1; for(unsigned c9=0;c9<g_labPartner->nClips;c9++)
                                    if(!strcmp(packStr(g_labPartner,g_labPartner->clips[c9].name),cb9)){ f9=(int)c9; break; }
                                if(f9>=0){ const PClip*k9=&g_labPartner->clips[f9];
                                    if(f9!=g_labPClip){ g_labPClip=f9; g_labPPhase=(g_clipStartS>0.f&&!strncmp(cb9,"z_rm_",5))?(double)g_clipStartS*(double)k9->fps:0.0; }
                                    if(qs9>=0.f){ g_labPPhase=(double)qs9*(double)k9->fps; if(k9->frames>1&&g_labPPhase>(double)k9->frames-1.0) g_labPPhase=(double)k9->frames-1.0; }
                                    g_labPHold=h9;
                                    if(h9>=0.f) g_labPPhase=(double)h9*(double)k9->frames;
                                    else if(q9>=0.f) g_labPPhase=(double)q9*(double)k9->frames;
                                    g_labPDnP=NULL; g_labPDnC=-1; }
                                else { int bc9=-1; Pack*bp9=bankFind(cb9,&bc9);
                                    if(bp9&&g_labPartner->cbOK&&g_labPartner->pbOK>=10){ g_labPDnP=bp9; g_labPDnC=bc9; g_labPHold=h9;
                                        if(h9>=0.f) g_labPPhase=(double)h9*(double)bp9->clips[bc9].frames;
                                        else if(q9>=0.f) g_labPPhase=(double)q9*(double)bp9->clips[bc9].frames;
                                        else if(qs9>=0.f) g_labPPhase=(double)qs9*(double)bp9->clips[bc9].fps; }
                                    else { g_missClip++; printf("[stage] line %d slot %d's clip '%s' NOT FOUND on her body or in a bank: z_rest, a T-pose\n",held8,s9,sp9); fflush(stdout); } }
                                g_castI=sv9; }
                            if(g_labPartner&&pcl[0]){
                                /* ⭐ `name@0.85` -- hold the clip at 85% through, as a POSE.
                                 * The fraction is of the clip's FRAMES, so the same number
                                 * means the same moment on a 40-frame clip and a 400-frame
                                 * one. */
                                /* ⭐ `name>0.33` -- CUE the loop: start it 33% through and PLAY ON, even when it
                                 * is the clip already playing. A loop only ever restarted when the clip CHANGED, so a
                                 * smoking loop ran on its own clock under the dialogue and put the cigarette in her
                                 * mouth in the middle of her line; the director has to be able to say where the drag
                                 * falls. (`@` still HOLDS a pose; `>` plays from one.) */
                                char pcb[40]; float pHold=-1.f, pCue=-1.f, pCueS=-1.f;
                                { int k8=0; const char*a8=pcl;
                                  while(*a8&&*a8!='@'&&*a8!='>'&&k8<39) pcb[k8++]=*a8++;
                                  pcb[k8]=0;
                                  if(*a8=='@') pHold=(float)atof(a8+1);
                                  else if(*a8=='>'){ const char*v8=a8+1; const size_t L8=strlen(v8);   /* (step 21) `>12.5s` is seconds in; a bare number is still how far through */
                                      if(L8&&v8[L8-1]=='s') pCueS=(float)atof(v8); else { pCue=(float)atof(v8); if(pCue<0.f) pCue=0.f; if(pCue>1.f) pCue=1.f; } } }
                                const char*pcn=pcb;
                                int f8=-1; for(unsigned c8=0;c8<g_labPartner->nClips;c8++)
                                    if(!strcmp(packStr(g_labPartner,g_labPartner->clips[c8].name),pcn)){ f8=(int)c8; break; }
                                if(f8>=0){ if(f8!=g_labPClip){ g_labPClip=f8; g_labPPhase=(g_clipStartS>0.f&&!strncmp(pcn,"z_rm_",5))?(double)g_clipStartS*(double)g_labPartner->clips[f8].fps:0.0; }
                                           if(pCueS>=0.f){ const PClip*c8=&g_labPartner->clips[f8]; g_labPPhase=(double)pCueS*(double)c8->fps; if(c8->frames>1&&g_labPPhase>(double)c8->frames-1.0) g_labPPhase=(double)c8->frames-1.0; }
                                           g_labPHold=pHold;
                                           if(pHold>=0.f)
                                               g_labPPhase=(double)pHold*(double)g_labPartner->clips[f8].frames;
                                           else if(pCue>=0.f){ g_labPPhase=(double)pCue*(double)g_labPartner->clips[f8].frames;
                                               printf("[stage] line %d pclip '%s' CUED at %.2f (frame %.0f of %u) -- plays on from there\n",
                                                      held8,pcn,pCue,g_labPPhase,g_labPartner->clips[f8].frames); }
                                           g_labPDnP=NULL; g_labPDnC=-1; }
                                else { int bc=-1; Pack*bp=bankFind(pcn,&bc);
                                    if(bp&&g_labPartner->cbOK&&g_labPartner->pbOK>=10){
                                        const int chg9=(g_labPDnP!=bp||g_labPDnC!=bc);
                                        g_labPDnP=bp; g_labPDnC=bc; g_labPHold=pHold;
                                        if(pHold>=0.f) g_labPPhase=(double)pHold*(double)bp->clips[bc].frames;
                                        else if(pCue>=0.f) g_labPPhase=(double)pCue*(double)bp->clips[bc].frames;
                                        else if(pCueS>=0.f) g_labPPhase=(double)pCueS*(double)bp->clips[bc].fps;
                                        else if(chg9&&g_clipStartS>0.f&&!strncmp(pcn,"z_rm_",5)) g_labPPhase=(double)g_clipStartS*(double)bp->clips[bc].fps; }
                                    else { g_missClip++; static int said9=0;
                                        if(!said9++) printf("\n========================================================================\n"
                                               "  \u26d4 MISSING CLIP: the script asks the PARTNER for '%s' and her\n"
                                               "     body does not have it. She falls back to z_rest, A T-POSE.\n"
                                               "     Bake it onto her pack, or name a clip she has.\n"
                                               "========================================================================\n\n",pcl);
                                        else printf("[stage] pclip '%s' NOT FOUND (a T-pose; %ld lines so far)\n",pcl,g_missClip);
                                        fflush(stdout); } }
                                /* ⛔ AND SHE MUST END UP WITH SOMETHING. labDrawPartner
                                 * returns on clip<0 and DRAWS NOTHING -- so a partner
                                 * whose clip never resolved is simply ABSENT, her head
                                 * mark is never published, and the conversation solver
                                 * (gated on BOTH marks) never runs: every scene falls
                                 * back to a static wide and no shot grammar fires. That
                                 * is what removing the ctl's `pclip=idle` did, and the
                                 * take still rendered at the right size with the right
                                 * subtitles and one of the two women missing.
                                 * Her own stand, else her first clip: a WRONG clip is a
                                 * bug you can see, an ABSENT body is one you cannot. */
                                if(g_labPClip<0&&!g_labPDnP&&g_labPartner->nClips){
                                    g_labPClip=(g_labPartner->idleIdx>=0)?g_labPartner->idleIdx:0;
                                    g_labPPhase=0.0;
                                    static int said8=0;
                                    if(!said8){ said8=1;
                                        printf("[stage] partner had NO clip -- falling back to %s so she is drawn at all\n",
                                               packStr(g_labPartner,g_labPartner->clips[g_labPClip].name));
                                        fflush(stdout); } } }
                            printf("[stage] line %d shot %d  gap %.2f yaw %.0f side %.2f  look h%d p%d  %s/%s\n",
                                   held8,S8->shot,S8->pgap,S8->pyaw,S8->pside,S8->hlook,S8->plook,
                                   hcl,pcl); fflush(stdout); }
                        { float st8=animStepS((sfps>0.f)?(1.f/sfps):dtf);
                          float k8=g_scnSnap?1.f:(1.f-expf(-st8/0.42f));
                          float g0=g_labPGap, s0=g_labPSide;
                          float sx0=g_stage[0], sz0=g_stage[1];
                          float c1g0=g_cast[1].gap, c1s0=g_cast[1].side, c2g0=g_cast[2].gap, c2s0=g_cast[2].side;
                          /* ⛔ A FLOOR ON THE MARKS, measured not chosen. Two bodies at the
                           * radius the script asks for can be inside one another -- and the
                           * LAB path must keep no floor at all, because the authored contact
                           * moves have to get closer than any conversational minimum. So the
                           * clamp lives here, on the scene staging path only. */
                          float tg=S8->pgap, ts=S8->pside;
                          if(g_stageMinSep>0.f){
                              float r=sqrtf(tg*tg+ts*ts);
                              if(r<g_stageMinSep&&r>1e-4f){ float f=g_stageMinSep/r; tg*=f; ts*=f; }
                              else if(r<=1e-4f) tg=g_stageMinSep; }
                          g_labPGap += (tg      -g_labPGap )*k8;
                          g_labPYaw += (S8->pyaw-g_labPYaw )*k8;
                          g_labPSide+= (ts      -g_labPSide)*k8;
                          /* ⭐ AND SLOT 2 EASES TO HERS, through the same one-pole -- a body
                           * that teleports between blocking marks reads as a cut when there
                           * is not one. Her keys are sticky, so a three-hander sets p2side
                           * once and she stays there.
                           * ⛔ AND THE SEPARATION CLAMP HAS TO INCLUDE HER: the two-body
                           * version kept the pair out of each other, and with three she can
                           * be dropped inside EITHER. She is pushed off both. */
                          if(g_cast[2].pk){
                              float t2g=S8->p2gap, t2s=S8->p2side;
                              if(g_stageMinSep>0.f){
                                  float r2=sqrtf(t2g*t2g+t2s*t2s);              /* off the hero */
                                  if(r2<g_stageMinSep&&r2>1e-4f){ float f=g_stageMinSep/r2; t2g*=f; t2s*=f; }
                                  else if(r2<=1e-4f) t2g=g_stageMinSep;
                                  { float dg=t2g-tg, ds=t2s-ts;                 /* off slot 1 */
                                    float r3=sqrtf(dg*dg+ds*ds);
                                    if(r3<g_stageMinSep&&r3>1e-4f){ float f=(g_stageMinSep-r3)/r3;
                                        t2g+=dg*f; t2s+=ds*f; }
                                    else if(r3<=1e-4f) t2s=ts+g_stageMinSep; } }
                              g_cast[2].gap += (t2g      -g_cast[2].gap )*k8;
                              g_cast[2].yaw += (S8->p2yaw-g_cast[2].yaw )*k8;
                              g_cast[2].side+= (t2s      -g_cast[2].side)*k8; }
                          /* ⭐ SLOT 3 EASES THE SAME WAY, pushed off all three bodies
                           * already placed -- the hero, slot 1 and slot 2. With four on a
                           * sidewalk the odds of landing inside somebody are three times what
                           * they were, and the clamp is the only thing standing between a
                           * four-hander and two women occupying the same volume. */
                          if(g_cast[3].pk){
                              float t3g=S8->p3gap, t3s=S8->p3side;
                              if(g_stageMinSep>0.f){
                                  float r4=sqrtf(t3g*t3g+t3s*t3s);              /* off the hero */
                                  if(r4<g_stageMinSep&&r4>1e-4f){ float f=g_stageMinSep/r4; t3g*=f; t3s*=f; }
                                  else if(r4<=1e-4f) t3g=g_stageMinSep;
                                  { const float og[2]={tg,g_cast[2].gap}, os[2]={ts,g_cast[2].side};
                                    for(int o9=0;o9<2;o9++){     /* off slot 1, then off slot 2 */
                                        float dg=t3g-og[o9], ds=t3s-os[o9];
                                        float r5=sqrtf(dg*dg+ds*ds);
                                        if(r5<g_stageMinSep&&r5>1e-4f){ float f=(g_stageMinSep-r5)/r5;
                                            t3g+=dg*f; t3s+=ds*f; }
                                        else if(r5<=1e-4f) t3s=os[o9]+g_stageMinSep; } } }
                              g_cast[3].gap += (t3g      -g_cast[3].gap )*k8;
                              g_cast[3].yaw += (S8->p3yaw-g_cast[3].yaw )*k8;
                              g_cast[3].side+= (t3s      -g_cast[3].side)*k8; }
                          /* ⭐ AND THE PAIR'S OWN MARK. A line may carry hx/hz; the stage
                           * eases toward it and the hero walks the distance. */
                          if(g_stageOn&&S8->hasMark){
                              float tx9=(S8->hasMark&1)?S8->hx:g_stage[0];
                              float tz9=(S8->hasMark&2)?S8->hz:g_stage[1];
                              float kk=g_scnSnap?1.f:0.f;   /* ⭐ the pair WALKS to hx/hz (locoSpeed); it no longer eases there */
                              float ox=g_stage[0], oz=g_stage[1];
                              if(kk>=1.f){ g_stage[0]=tx9; g_stage[1]=tz9; g_stageV9=0.f; }
                              else { float ddx9=tx9-g_stage[0], ddz9=tz9-g_stage[1], lf9=sqrtf(ddx9*ddx9+ddz9*ddz9);
                                  if(lf9>0.01f&&st8>1e-6f){ g_stageV9=locoSpeed(g_stageV9,lf9,st8,LOCO_CRUISE); if(g_stageV9<0.f) g_stageV9=0.f;
                                      float sp9=g_stageV9*st8; if(sp9>lf9) sp9=lf9;
                                      g_stage[0]+=ddx9/lf9*sp9; g_stage[1]+=ddz9/lf9*sp9; }
                                  else if(lf9<=0.01f){ g_stage[0]=tx9; g_stage[1]=tz9; g_stageV9=0.f; } }
                              /* (z walks with x, above) */
                              if(st8>1e-5f){ float dx9=g_stage[0]-ox, dz9=g_stage[1]-oz;
                                  float v9=sqrtf(dx9*dx9+dz9*dz9)/st8;
                                  g_heroStageSpd+=(v9-g_heroStageSpd)*(1.f-expf(-st8/0.12f)); } }
                          else g_heroStageSpd*=expf(-st8/0.20f);
                          /* HER SPEED, from how far the marks actually moved her. */
                          if(st8>1e-5f){
                              float dx=g_labPGap-g0, dz=g_labPSide-s0;
                              float v=sqrtf(dx*dx+dz*dz)/st8;
                              g_partSpd += (v-g_partSpd)*(1.f-expf(-st8/0.12f)); }
                          locoTick(st8,g_scnSnap);   /* ⭐ marks are walked to; speed is measured in the world */
                          /* ⭐ A JUMP IS NOT A WALK. The distance just covered in one frame reads as a
                           * sprint to the speed measures above, and a partner "sprinting" plays her
                           * walk -- so a snapped cut zeroes them before anything can listen. */
                          if(g_scnSnap){
                              /* ⭐ AND THE MARKS JUMP WITH THEM. The lens reads the head and eye marks
                               * the LAST frame's draw published, and held props hang off last
                               * frame's grips -- so for one frame after a snap the bodies stood in
                               * the new place while the camera framed the old one: a flash frame on
                               * every jump cut (an 8fps test, 33.7s). Shift the published marks
                               * by exactly each body's jump; the next draw replaces them with the
                               * measured truth. Placement is the partner's own formula. */
                              float sh9=sinf(g_rvHead), ch9=cosf(g_rvHead);
                              float dx0=g_stage[0]-sx0, dz0=g_stage[1]-sz0;
                              float dg1=g_cast[1].gap-c1g0, ds1=g_cast[1].side-c1s0;
                              float dg2=g_cast[2].gap-c2g0, ds2=g_cast[2].side-c2s0;
                              float jx[3]={dx0, dx0+sh9*dg1+ch9*ds1, dx0+sh9*dg2+ch9*ds2};
                              float jz[3]={dz0, dz0+ch9*dg1-sh9*ds1, dz0+ch9*dg2-sh9*ds2};
                              g_heroHeadW[0]+=jx[0]; g_heroHeadW[2]+=jz[0];
                              g_heroEyeW[0] +=jx[0]; g_heroEyeW[2] +=jz[0];
                              for(int c9=1;c9<SCN_BODIES&&c9<3;c9++){
                                  g_cast[c9].headW[0]+=jx[c9]; g_cast[c9].headW[2]+=jz[c9];
                                  g_cast[c9].eyeW[0] +=jx[c9]; g_cast[c9].eyeW[2] +=jz[c9]; }
                              for(int c9=0;c9<SCN_BODIES&&c9<3;c9++) for(int k9=0;k9<ATT_N;k9++){
                                  g_castAttM[c9][k9][12]+=jx[c9]; g_castAttM[c9][k9][14]+=jz[c9]; }
                              for(int s9=0;s9<SCN_MAXCAST;s9++){ g_loco[s9].spd=0.f; g_loco[s9].winit=0; g_loco[s9].on=0; g_loco[s9].pon=0; }
                              g_partSpd=0.f; g_heroStageSpd=0.f; g_partWalkMix=0.f; g_scnSnap=0; }
                        } } }
                    for(int m9=0;m9<MORPH_MAX;m9++) g_heroMorphW[m9]=0.f;
                    memset(g_castMorphW,0,sizeof g_castMorphW);
                    int li9=scnActive();
                    if(li9>=0){ ScnLine*L9=&g_scnL[li9];
                        int fi9=(int)((scnBodyT()-L9->t0)*30.f);
                        if(fi9<0) fi9=0;
                        if(fi9>=L9->nEnv) fi9=L9->nEnv?L9->nEnv-1:0;
                        float amp9=L9->nEnv?(float)L9->env[fi9]/100.f:0.f;
                        if(amp9>1.f) amp9=1.f;
                        /* speech RMS spends most of its time at 0.2-0.4; a linear map
                         * left the mouth barely parted through whole sentences. */
                        amp9=scnMouth(L9,scnBodyT());   /* same mouth as the bone gets */
                        int ss9=(L9->spk>=1&&L9->spk<SCN_BODIES)?L9->spk:1;   /* the speaking slot, its own face */
                        Pack*SP=(L9->spk==0)?g_heroPk:g_cast[ss9].pk;
                        Pack*LP=(L9->spk==0)?g_cast[1].pk:g_heroPk;
                        float*SW=(L9->spk==0)?g_heroMorphW:g_castMorphW[ss9];
                        float*LW=(L9->spk==0)?g_castMorphW[1]:g_heroMorphW;
                        /* ⛔ A BODY WITH A JAW BONE MUST NOT ALSO GET THE JAW MORPH.
                         * They are two different deformations of the SAME vertices,
                         * both driven from the same speech amplitude: the bone swings
                         * the mandible while the morph drags the lower face somewhere
                         * else, and the face TEARS -- reva came apart into a flat grey
                         * shard mid-sentence.
                         * The morph was the stand-in for a rig she did not
                         * have; she has one now. The same goes for `smile`, which
                         * pulls the mouth band the bone is already moving. Brow and
                         * blink are untouched -- they are elsewhere on the face and
                         * fight nothing.
                         * (The aperture had this same "two sources for one opening"
                         * hazard and was fixed for it; this path was missed.) */
                        /* by NAME, every mesh that carries it: a sculpted shape is split
                         * per material, so the bought character's `jaw` is three morphs (lips, face, teeth). */
                        if(L9->spk<SCN_VOICE&&!(SP&&SP->fmJaw>=0)){   /* the last slot is a voice with no mouth */
                            morphSetName(SP,SW,"jaw",amp9);
                            morphSetName(SP,SW,"smile",0.30f*amp9); }  /* widens as it opens */
                        morphSetName(LP,LW,"brow",0.28f+0.16f*sinf(scnBodyT()*1.6f)); }
                    /* THE STAGED BROW, over the top of that. It is set per BODY rather
                     * than per speaker so a listener can hold a scowl through the other
                     * one's lines -- which is most of what being angry at someone looks
                     * like. It is safe to layer here: brow is nowhere near the jaw band
                     * and fights nothing (unlike jaw/smile, which the bone owns). */
                    { for(int b7=0;b7<2;b7++){
                          if(g_scnBrow[b7]<-8.f) continue;
                          Pack*BP=b7?g_cast[1].pk:g_heroPk;
                          float*BW=b7?g_castMorphW[1]:g_heroMorphW;
                          /* both brows: Brow_Raise_Left and _Right inject under one name */
                          morphSetName(BP,BW,"brow",
                                       g_scnBrow[b7]+0.055f*sinf(scnBodyT()*1.3f+b7*2.f)); } }
                    /* BLINKS. Nobody holds their eyes open for a minute.
                     * Each body runs her own clock, so they never
                     * blink in lockstep. */
                    { static float bT9[SCN_BODIES]={1.1f,2.7f,1.9f,3.4f}, bP9[SCN_BODIES]={-1.f,-1.f,-1.f,-1.f};
                      float st9=animStepS((sfps>0.f)?(1.f/sfps):dtf);
                      /* ⛔ (10-23) NOT a count of the scene's bodies (one that was never set said 2): the third and fourth bodies of every scene
                       * never blinked. A slot with no body has no `blink` (morphIdx(NULL) is -1) and is skipped. */
                      for(int b9=0;b9<SCN_BODIES;b9++){
                          Pack*BP=b9?g_cast[b9].pk:g_heroPk;
                          float*BW=b9?g_castMorphW[b9]:g_heroMorphW;
                          if(morphIdx(BP,"blink")<0) continue;
                          /* eyeBlinkLeft and eyeBlinkRight both inject as `blink`, so one
                           * clock shuts both eyes -- a body does not wink at the street. */
                          if(bP9[b9]>=0.f){ bP9[b9]+=st9;
                              if(bP9[b9]>=0.16f){ bP9[b9]=-1.f; morphSetName(BP,BW,"blink",0.f);
                                  bT9[b9]=2.6f+2.9f*(float)(((int)(g_scnT*17.f)+b9*5)%7)/7.f; }
                              else morphSetName(BP,BW,"blink",sinf(3.14159f*(bP9[b9]/0.16f))); }
                          else { bT9[b9]-=st9; if(bT9[b9]<=0.f) bP9[b9]=0.f; } } }
                    /* (11-11) THE HELD FACE: a line's hface= pface= p2face= p3face= hold face shapes by name through it (a laugh's shut eyes
                     * and its smile), over the speech's mouth and the blink clock (the music video). A line
                     * that says none changes nothing. ⛔ The jaw is the BONE's on a body that has one: a held `jaw` here would fight it */
                    { int lf9=scnActive();
                      if(lf9>=0) for(int b9=0;b9<SCN_BODIES;b9++){ const char*s9=g_scnL[lf9].face[b9]; if(!s9[0]) continue;
                          Pack*BP=b9?g_cast[b9].pk:g_heroPk; float*BW=b9?g_castMorphW[b9]:g_heroMorphW; if(!BP) continue;
                          while(*s9){ char nm9[48]; size_t k9=0;
                              while(*s9&&*s9!=':'&&*s9!=','&&k9+1<sizeof nm9) nm9[k9++]=*s9++;
                              nm9[k9]=0; float w9=1.f;
                              if(*s9==':'){ s9++; w9=(float)atof(s9); while(*s9&&*s9!=',') s9++; }
                              if(*s9==',') s9++;
                              if(nm9[0]&&!(BP->fmJaw>=0&&!strcmp(nm9,"jaw"))) morphSetName(BP,BW,nm9,w9); } } }
                    { const char*mf9=getenv("REVA_MORPHFORCE");   /* bench: pin the jaw open */
                      if(mf9&&!strchr(mf9,'=')){ float v9=(float)atof(mf9);
                          morphSetName(g_heroPk,g_heroMorphW,"jaw",v9);
                          for(int c9=1;c9<SCN_BODIES;c9++) morphSetName(g_cast[c9].pk,g_castMorphW[c9],"jaw",v9); } }
                    { static float tel9=0.f; tel9+=dtf;
                      if(tel9>2.0f){ tel9=0.f;
                        printf("[scene] t=%.1f line=%d  jaw idx h=%d p=%d  weights h=[%.2f %.2f %.2f %.2f] p=[%.2f %.2f %.2f %.2f]\n",
                               g_scnT,li9,morphIdx(g_heroPk,"jaw"),morphIdx(g_labPartner,"jaw"),
                               g_heroMorphW[0],g_heroMorphW[1],g_heroMorphW[2],g_heroMorphW[3],
                               g_partMorphW[0],g_partMorphW[1],g_partMorphW[2],g_partMorphW[3]);
                        fflush(stdout); } }
                    if(g_scnT>g_scnLen+1.4f){ g_scnT=-1.f;
                        for(int m9=0;m9<MORPH_MAX;m9++) g_heroMorphW[m9]=0.f;
                        memset(g_castMorphW,0,sizeof g_castMorphW);
                        g_stepFps=g_stepCtl; g_tone=g_toneCtl;
                        printf("[scene] done\n"); fflush(stdout); } }
                /* ---- LIVE CONTROL (08-26): the lab is left running and tuned live, not opened, photographed and closed.
                 * The lab polls a tiny
                 * command file, exactly the way the band clock is polled, so state,
                 * camera, gait and captures can all be driven while it RUNS -- no reboot,
                 * no reloading 134 packs to see one change. Commands apply when `seq`
                 * changes, so the same file is never executed twice.
                 *   seq=<n>  state=<0-6>  cam=<0-3>  gait=<f>  speed=<f>  shot=<path> */
                { static float ctlT=0.f; static long lastSeq=-1;
                  ctlT+=dtf;
                  if(ctlT>0.12f){ ctlT=0.f;
                    static const char*ctlPath=NULL;
                    if(!ctlPath){ ctlPath=getenv("REVA_LABCTL"); if(!ctlPath) ctlPath="anim_bench/lab_ctl.txt"; }
                    FILE*cf=fopen(ctlPath,"r");
                    if(cf){ char buf[1024]; size_t n=fread(buf,1,sizeof buf-1,cf); buf[n]=0; fclose(cf);
                      char*sq=strstr(buf,"seq=");
                      long sv=sq?atol(sq+4):-1;
                      if(sv>=0&&sv!=lastSeq){ lastSeq=sv;
                        char*q;
                        if((q=strstr(buf,"state="))){ int v=atoi(q+6)%LAB_N; if(v!=GM.stage){ GM.stage=v; g_bcN=0; g_labSlip=g_labTravel=0.f; } GM.flag=1; }
                        if((q=strstr(buf,"cam="))) g_labCam=atoi(q+4)&3;
                        if((q=strstr(buf,"gait="))){ float v=(float)atof(q+5); if(v>0.f) g_gaitMul=v; }
                        if((q=strstr(buf,"trim="))){ float v=(float)atof(q+5); if(v>0.f) g_servo=v; }   /* live trim on the DERIVED rate */
                        if((q=strstr(buf,"freeze="))) g_labFreeze=atoi(q+7)?1:0;   /* MULTI-CAM: hold the pose */
                        if((q=strstr(buf,"grid="))){ g_labGrid=atoi(q+5); if(g_labGrid>16) g_labGrid=16; }
                        if((q=strstr(buf,"speed="))){ float v=(float)atof(q+6); if(v>=0.f) g_labSpeed=v; }
                        if((q=strstr(buf,"reset="))){ g_rvx=0.f; g_rvz=-150.f; g_bcN=0; g_labSlip=g_labTravel=0.f; }
                        if((q=strstr(buf,"shot="))){ char*e2=q+5; int k=0;
                            while(*e2&&*e2!='\n'&&*e2!='\r'&&*e2!=' '&&k<511) g_shotNow[k++]=*e2++;
                            g_shotNow[k]=0; }
                        /* P0 clip=<name>: play a clip by NAME on the subject; clip=off restores
                         * the state tree. This is the authoring loop's playback path. */
                        /* THE KEY IS "clip=", NOT "...clip=". strstr finds it INSIDE
                         * `pclip=` and `hclip=`, so the driver passing --pclip idle was
                         * silently setting the HERO's clip to idle -- which on the co-star is a
                         * get-up-off-the-floor loop. She has been crouching on all fours
                         * through entire scenes because of a substring match. */
                        q=strstr(buf,"clip=");
                        while(q&&q>buf&&(q[-1]=='p'||q[-1]=='h')) q=strstr(q+1,"clip=");
                        if(q){ char nm2[64]; int k2=0; q+=5;
                            while(*q&&*q!='\n'&&*q!='\r'&&*q!=' '&&k2<63) nm2[k2++]=*q++;
                            nm2[k2]=0;
                            if(g_scnN>0){ /* the script owns it -- see the note above */ }
                            else if(!k2||!strcmp(nm2,"off")){ g_labClip=-1; g_labDnC=-1; printf("[lab] clip override OFF\n"); }
                            else if(g_heroPk){ int found=-1;
                                for(unsigned c2=0;c2<g_heroPk->nClips;c2++)
                                    if(!strcmp(packStr(g_heroPk,g_heroPk->clips[c2].name),nm2)){ found=(int)c2; break; }
                                if(found<0&&g_movesPk&&g_movesPk!=g_heroPk)   /* P5: the moves pack donates to ANY body */
                                    for(unsigned c2=0;c2<g_movesPk->nClips;c2++)
                                        if(!strcmp(packStr(g_movesPk,g_movesPk->clips[c2].name),nm2)){ g_labDnC=(int)c2; break; }
                                if(found<0&&g_labDnC>=0&&g_movesPk){
                                    g_labClip=-1; g_labWphReset=1;
                                    g_labRM=(strncmp(nm2,"z_rm_",5)==0); g_rmY=-999.f;
                                    printf("[lab] clip -> moves donor %d '%s' (%uf @ %gfps)\n",g_labDnC,nm2,
                                           g_movesPk->clips[g_labDnC].frames,g_movesPk->clips[g_labDnC].fps); }
                                else if(found>=0){ g_labClip=found; g_labDnC=-1; g_labWphReset=1;
                                    g_labRM=(strncmp(nm2,"z_rm_",5)==0); g_rmY=-999.f;   /* P3 */
                                    printf("[lab] clip -> %d '%s' (%uf @ %gfps)\n",found,nm2,
                                           g_heroPk->clips[found].frames,g_heroPk->clips[found].fps); }
                                else { /* ⭐ THEN ANY BANK (09-11). The scene path resolved hclip through
                                        * bankFind and the ctl path only ever asked the moves pack --
                                        * `clip=x_chairsat` (v1_kmoves) was NOT FOUND from the lab while
                                        * every scene played it. Same neighbouring-path gap as always. */
                                    int bc=-1; Pack*bp=bankFind(nm2,&bc);
                                    if(bp&&g_heroPk->cbOK&&g_heroPk->pbOK>=10){
                                        g_labDnP=bp; g_labDnC=bc; g_labClip=-1; g_labWphReset=1;
                                        g_labRM=(strncmp(nm2,"z_rm_",5)==0); g_rmY=-999.f;
                                        printf("[lab] clip -> bank '%s' donor %d '%s' (%uf @ %gfps)\n",bp->label,bc,nm2,
                                               bp->clips[bc].frames,bp->clips[bc].fps); }
                                    else printf("[lab] clip '%s' NOT FOUND (%u clips, no bank carries it%s)\n",nm2,g_heroPk->nClips,
                                                (bp&&!(g_heroPk->cbOK&&g_heroPk->pbOK>=10))?"; a bank does but this rig cannot receive":""); } } }
                        if((q=strstr(buf,"once="))) g_labOnce=atoi(q+5)?1:0;   /* P1: one-shot playback */
                        /* ⛔ A SCENE OWNS ITS CLIPS. The per-line staging resolves the
                         * hero's and partner's clips ~160 lines above this, and this
                         * parse ran afterwards -- so a ctl `clip=idle` overwrote the
                         * staging's choice on EVERY poll, and the co-star spent an entire scene
                         * playing her get-up-off-the-floor loop on all fours. Two earlier
                         * attempts looked at the staging (which was correct, and said so
                         * in the log) and never at what came after it. */
                        if(g_scnN>0&&(strstr(buf,"clip=")||strstr(buf,"pclip="))){
                            static int said=0;
                            if(!said){ said=1;
                                printf("[scene] ctl clip=/pclip= IGNORED while a scene plays -- the script owns them");
                                printf("%c",10); fflush(stdout); } }
                        /* P7 partner=<pack>: load the SECOND body (accepts a bare name the
                         * same way REVA_ANIMSUBJ does). partner=off puts her away. */
                        /* ⭐ partner2= LOADS SLOT 2 THROUGH THE SAME CODE, by lending it
                         * g_castI -- including the hero-style mesh sizing below, which is
                         * what lets two bodies share a frame at the same scale. Slot 1 is
                         * restored at the end of the block. */
                        /* ⭐ THE CAST LOADS THROUGH ONE PIECE OF CODE. partner= is slot 1 and
                         * partner2= is slot 2; the loop lends each of them g_castI so the whole
                         * body below -- including the hero-style MESH sizing, which is what lets
                         * two bodies share a frame at the same scale -- is written once.
                         * ⛔ A single ctl line can carry BOTH, which is why this is a loop and
                         * not a ternary: the first draft took whichever key it found first and
                         * silently dropped the other. */
                        static const char*PKEY9[SCN_BODIES]={"","partner=","partner2=","partner3="};
                        for(int cs9=1;cs9<SCN_BODIES;cs9++){
                          const char*ky9=PKEY9[cs9];
                          q=strstr(buf,ky9); if(!q) continue;
                          q+=strlen(ky9); g_castI=cs9;
                          { char nm3[64]; int k3=0;
                            while(*q&&*q!='\n'&&*q!='\r'&&*q!=' '&&k3<63) nm3[k3++]=*q++;
                            nm3[k3]=0;
                            /* ⭐ partnerN=<pack>@<height> -- a cast member's HEIGHT in engine units
                             * The loader sizes every body to reva's 3.45, which is right for
                             * two women sharing a frame and wrong for a cartoon kid on a skateboard.
                             * A height, not a scale: a scale means nothing across rigs authored in
                             * different units. Stripped before the name is used to find the pack. */
                            float wantH9=-1.f;
                            { char*at9=strchr(nm3,'@');
                              if(at9){ wantH9=(float)atof(at9+1); *at9=0; k3=(int)strlen(nm3); } }
                            if(!k3||!strcmp(nm3,"off")){ g_labPartner=NULL; g_labPClip=-1;
                                printf("[lab] partner OFF\n"); }
                            else { char pp[512]; FILE*pf=fopen(nm3,"rb");
                                if(pf){ fclose(pf); snprintf(pp,sizeof pp,"%s",nm3); }
                                else { snprintf(pp,sizeof pp,"packs/v1_%s.rpack",nm3); FILE*t9=fopen(pp,"rb"); if(t9) fclose(t9); else snprintf(pp,sizeof pp,"props/v1_%s.rpack",nm3); }   /* a lead outside the crowd: props/ */
                                FILE*pf2=fopen(pp,"rb");
                                if(!pf2) printf("[lab] partner '%s' NOT FOUND\n",nm3);
                                else { fclose(pf2); g_labPartnerPk=loadPack(pp); g_labPartner=&g_labPartnerPk;
                                    g_labPClip=-1; g_labPPhase=0.0;
                                    /* ⛔ A SLOT WITH NO CLIP IS NEVER DRAWN. The per-line
                                     * staging only names clips for the hero and slot 1, so
                                     * anyone past that would load, size, and then not exist.
                                     * Give the later slots their own stand at load time --
                                     * a wrong clip is visible, an absent body is not. */
                                    if(cs9>=2&&g_labPartnerPk.nClips)
                                        g_labPClip=(g_labPartnerPk.idleIdx>=0)?g_labPartnerPk.idleIdx:0;
                                    /* SIZE HER LIKE A HERO, not like crowd furniture. The
                                     * pack loader sizes by BONE SPAN, and the co-star's cloud runs
                                     * ~57 rig-units below her own feet, which shrank her to a
                                     * third of reva's height standing next to her. The hero
                                     * path sizes by MESH for exactly this reason. Two
                                     * characters sharing a frame must be sized the same way,
                                     * or no authored contact between them can ever land. */
                                    { Pack*K=&g_labPartnerPk;
                                      unsigned*Hd3=(unsigned*)(K->pk+4);
                                      float*szb3=(float*)(K->pk+Hd3[12]); float meshY3=szb3[9];
                                      if(meshY3==meshY3&&meshY3>0.5f&&meshY3*K->scale>3.15f*1.5f){
                                          K->scale=3.45f/meshY3; K->mny=0.f; K->cx=0.f; K->cz=0.f; K->effH=-1.f; }
                                      else K->scale*=3.45f/3.15f;
                                      if(wantH9>0.f){
                                          float had9=(meshY3==meshY3&&meshY3>0.5f)?meshY3*K->scale:3.45f;
                                          K->scale*=wantH9/had9; K->effH=-1.f;
                                          printf("[lab] slot %d height override %.2fu (was %.2fu, scale %.4f)\n",
                                                 cs9,wantH9,had9,K->scale);
                                          fflush(stdout); }
                                      /* re-ground on the rest pose, as the hero is grounded */
                                      M4*w3=(M4*)malloc(K->nNodes*sizeof(M4)); float mny3=1e9f;
                                      for(unsigned i3=0;i3<K->nNodes;i3++){ M4 loc3;
                                          m4_trs(loc3,K->nodes[i3].t,K->nodes[i3].q,K->nodes[i3].s);
                                          if(K->nodes[i3].parent<0) memcpy(w3[i3],loc3,64);
                                          else m4_mul(w3[i3],w3[K->nodes[i3].parent],loc3);
                                          if(K->nodes[i3].kind==1){ float y3=w3[i3][13]; if(y3<mny3)mny3=y3; } }
                                      if(mny3<1e8f) K->mny=mny3;
                                      /* her geometry, from the REST pose -- the scores need
                                       * it before she has a clip to play, so it cannot wait
                                       * for the draw. */
                                      { int sh4=K->pb[10], el4=K->pb[11];
                                        if(sh4>=0&&el4>=0){
                                          int tp4=-1; for(unsigned n4=0;n4<K->nNodes;n4++)
                                              if(K->nodes[n4].parent==el4){ tp4=(int)n4; break; }
                                          float a4=0,b4=0;
                                          { float dx=w3[sh4][12]-w3[el4][12],dy=w3[sh4][13]-w3[el4][13],dz=w3[sh4][14]-w3[el4][14];
                                            a4=sqrtf(dx*dx+dy*dy+dz*dz); }
                                          if(tp4>=0){ float dx=w3[el4][12]-w3[tp4][12],dy=w3[el4][13]-w3[tp4][13],dz=w3[el4][14]-w3[tp4][14];
                                            b4=sqrtf(dx*dx+dy*dy+dz*dz); }
                                          printf("[hi5] PARTNER geometry: shoulder %.2fu above ground, "
                                                 "arm %.2fu, scale %.4f\n",
                                                 (w3[sh4][13]-K->mny)*K->scale,(a4+b4)*K->scale,K->scale);
                                          fflush(stdout); } }
                                      free(w3); }
                                    printf("[lab] partner -> %s (%u nodes, %u clips, scale %.4f)\n",pp,
                                           g_labPartnerPk.nNodes,g_labPartnerPk.nClips,g_labPartnerPk.scale);
                                    /* ⭐ WHICH BONES SHE HOLDS THINGS WITH -- a prop on the wrong bone reads as
                                     * a missing prop, and nothing else in the log could say which. */
                                    /* ⛔ packStr returns ONE static buffer. Called twice in one printf, both
                                     * arguments pointed at it and both hands printed the SAME name -- which
                                     * read as a grip-election bug and cost a take. Copy each
                                     * name out before the next call. */
                                    { char gr9[64], gl9[64];
                                      snprintf(gr9,64,"%s",g_labPartnerPk.gripNode>=0?packStr(&g_labPartnerPk,g_labPartnerPk.nodes[g_labPartnerPk.gripNode].name):"-");
                                      snprintf(gl9,64,"%s",g_labPartnerPk.gripL>=0?packStr(&g_labPartnerPk,g_labPartnerPk.nodes[g_labPartnerPk.gripL].name):"-");
                                      printf("[grip] partner %s: right '%s' left '%s'\n",pp,gr9,gl9); } } } } }
                        g_castI=1;
                        if((q=strstr(buf,"pgap="))) g_labPGap=(float)atof(q+5);
                        if((q=strstr(buf,"pside="))) g_labPSide=(float)atof(q+6);
                        if((q=strstr(buf,"chains="))) g_chainsOn=atoi(q+7);
                        if((q=strstr(buf,"chainstiff="))) g_chStiff=(float)atof(q+11);
                        if((q=strstr(buf,"chaindamp="))) g_chDamp=(float)atof(q+10);
                        /* multiples of REAL gravity -- 1.0 is physical at the engine's scale.
                         * A dancefloor may want hair thrown harder than a woman sitting
                         * on a bed at 4am; that is a scene's choice, not the solver's. */
                        if((q=strstr(buf,"actors="))) g_setActorsOn=atoi(q+7)?1:0;
                        if((q=strstr(buf,"chaingrav="))){ float v=(float)atof(q+10);
                            if(v>=0.f&&v<=20.f) g_chGrav=v; }
                        if((q=strstr(buf,"pyaw="))) g_labPYaw=(float)atof(q+5);
                        if((q=strstr(buf,"aimy="))) g_labAimY=(float)atof(q+5);   /* close-up aim height (faces vs hands) */
                        if((q=strstr(buf,"wms="))) g_wms=(float)atof(q+4);
                        if((q=strstr(buf,"face="))){ g_labFace=atoi(q+5); if(g_labFace>10) g_labFace=10;
                            if(g_labFace) g_labGrid=g_labFace; }      /* 0 = LBS (soft-bound shoulders balloon under DQS) */
                        /* jaw=0..1 holds the jaw open by hand; jaw=-1 hands it back to
                         * the scene envelope. The bench needs to park a mouth at a
                         * chosen opening to judge the interior against it. */
                        if((q=strstr(buf,"stage="))){ float sx,sz,sy;
                            if(!strncmp(q+6,"off",3)){ g_stageOn=0; printf("[stage] off\n"); }
                            else if(sscanf(q+6,"%f,%f,%f",&sx,&sz,&sy)==3){
                                g_stage[0]=sx; g_stage[1]=sz; g_stage[2]=sy*0.0174532925f;
                                g_stageOn=1;
                                printf("[stage] pair at (%.2f, %.2f) facing %.0f deg\n",sx,sz,sy); }
                            fflush(stdout); }
                        /* ⭐ hmark= / pmark= / p2mark= on the ctl line too (09-11), so the lab can
                         * put a body on furniture without compiling a scene. `off` clears. */
                        { char*rl9=strstr(buf,"roomlit=");
                          if(rl9){ g_roomLit=(float)atof(rl9+8); printf("[set] roomlit %.2f\n",g_roomLit); fflush(stdout); } }
                        { char*ca9=strstr(buf,"clipaudit=");   /* ⭐ the clipping audit, live */
                          if(ca9) caEnable(atoi(ca9+10)!=0,"ctl clipaudit="); }
                        { const char*mk9[4]={"hmark=","pmark=","p2mark=","p3mark="};
                          for(int s9=0;s9<4;s9++){ char*r=strstr(buf,mk9[s9]);
                              /* `pmark=` matches inside `p2mark=` -- the digit guard again */
                              while(r&&r>buf&&r[-1]>='0'&&r[-1]<='9') r=strstr(r+1,mk9[s9]);
                              if(r){ int k=0; r+=strlen(mk9[s9]);
                                  while(*r&&*r!=' '&&*r!='\n'&&*r!='\r'&&k<31) g_castMark[s9][k++]=*r++;
                                  g_castMark[s9][k]=0; } } }
                        if((q=strstr(buf,"jaw="))) g_jawForce=(float)atof(q+4);
                        /* ⭐ gaze=yaw,pitch (radians; off) forces the hero's eyes for the bench (09-11) */
                        if((q=strstr(buf,"gaze="))){ if(!strncmp(q+5,"off",3)){ g_gazeForce[0]=-9.f; } else { float gy=0,gp=0; if(sscanf(q+5,"%f,%f",&gy,&gp)>=1){ g_gazeForce[0]=gy; g_gazeForce[1]=gp; } }
                            printf("[eyes] gaze force %s\n",g_gazeForce[0]>-8.f?"on":"off"); }
                        /* (step 23) eyes= peyes= p2eyes= p3eyes=  r,g,b[,amount] or off: that body's eyes glow. `eyes=` is inside the others: the word has to start there */
                        { static const char*const EK9[4]={"eyes=","peyes=","p2eyes=","p3eyes="};
                          for(int s9=0;s9<4;s9++){ char*r9=strstr(buf,EK9[s9]); while(r9&&r9>buf&&r9[-1]!=' ') r9=strstr(r9+1,EK9[s9]);
                              if(r9) eyesSay(s9?g_castEyeG[s9]:g_heroEye,r9+strlen(EK9[s9])); } }
                        if((q=strstr(buf,"look="))) g_lookOn=atoi(q+5)?1:0;
                        if((q=strstr(buf,"fpv="))){ g_fpv=atoi(q+4)?1:0; printf("[fpv] %s (ctl)\n",g_fpv?"FIRST PERSON":"third person"); }
                        /* \u2b50 fish=<0..0.6>: the lens on the front of the camera. 0.18 is a wide, 0.35 is a
                         * Century fisheye on a VX1000 -- the skate-video lens. It needs the post pass (it IS the
                         * post pass), so it turns it on. */
                        if((q=strstr(buf,"feed="))){ g_tape=(float)atoi(q+5); if(g_tape<0.f)g_tape=0.f; if(g_tape>2.f)g_tape=2.f; if(g_tape>0.f&&g_post==0) g_post=1;
                            printf("[feed] %s (ctl)\n",(g_tape>1.5f)?"HER PHONE, SIDEWAYS (16:9)":((g_tape>0.f)?"HER PHONE (9:16 portrait)":"off")); fflush(stdout); }
                        if((q=strstr(buf,"fish="))){ g_fish=(float)atof(q+5); if(g_fish<0.f)g_fish=0.f; if(g_fish>1.f)g_fish=1.f;
                            if(g_fish>0.f&&g_post==0) g_post=1;
                            printf("[fish] %.2f%s (ctl)\n",g_fish,(g_fish>0.f)?" -- the 3D pass renders the whole field and the post pass projects it":" -- off"); fflush(stdout); }
                        if((q=strstr(buf,"shadow="))){ g_shadow=atoi(q+7)?1:0; }
                        if((q=strstr(buf,"cull="))){ g_cullOn=atoi(q+5)?1:0; printf("[cull] %s\n",g_cullOn?"on":"OFF"); }
                        /* ⭐ throw=<stem>@x,y,z@vx,vy,vz[@scale] (09-11): a loose object from the lab */
                        if((q=strstr(buf,"throw="))){ char st[64]; float p3[3],v3[3],sc=1.f; int k=0; q+=6;
                            while(*q&&*q!='@'&&*q!=' '&&*q!='\n'&&k<63) st[k++]=*q++;
                            st[k]=0;
                            if(*q=='@'&&sscanf(q+1,"%f,%f,%f@%f,%f,%f@%f",&p3[0],&p3[1],&p3[2],&v3[0],&v3[1],&v3[2],&sc)>=6) rbSpawn(st,p3,v3,sc);
                            else printf("[rigid] throw= wants <stem>@x,y,z@vx,vy,vz[@scale]\n"); }
                        /* ⭐ over= / pover= / p2over=  <clip>[@w]  (09-11): the upper-body layer per slot.
                         * `over=` also sits inside `pover=`/`p2over=`/`hover=`: the letter guard. */
                        { const char*ok9[3]={"over=","pover=","p2over="};
                          for(int s9=0;s9<3;s9++){ char*r=strstr(buf,ok9[s9]);
                              while(r&&r>buf&&((r[-1]>='a'&&r[-1]<='z')||(r[-1]>='0'&&r[-1]<='9'))) r=strstr(r+1,ok9[s9]);
                              if(r){ char nm[40]; int k=0; r+=strlen(ok9[s9]);
                                  while(*r&&*r!=' '&&*r!='\n'&&*r!='\r'&&*r!='@'&&k<31) nm[k++]=*r++;
                                  nm[k]=0;
                                  g_overW[s9]=(*r=='@')?(float)atof(r+1):1.f;
                                  snprintf(g_overClip[s9],32,"%.31s",nm);
                                  printf("[over] slot %d layer '%s' w %.2f (ctl)\n",s9,nm,g_overW[s9]); fflush(stdout); } } }
                        /* ⭐ sfx=<name>[,<name>...] test-fires the mixer from the lab (09-11); a
                         * name may carry @x,y,z for a spatial play: sfx=shot@30,1,-20 */
                        if((q=strstr(buf,"sfx="))){ char nm[200]; int k=0; q+=4;
                            while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<199) nm[k++]=*q++;
                            nm[k]=0;
                            char*tok=strtok(nm,",");
                            while(tok){ float p3[3]; int sp=0; char*at=strchr(tok,'@');
                                if(at){ *at=0; if(sscanf(at+1,"%f,%f,%f",&p3[0],&p3[1],&p3[2])==3) sp=1; }
                                int id9=sfxPlay(tok,0.9f,sp?p3:NULL,0);
                                printf("[sfx] ctl %s -> %s\n",tok,id9>=0?"playing":"NOT PLAYED"); fflush(stdout);
                                tok=strtok(NULL,","); } }
                        if((q=strstr(buf,"shadowext="))){ float v=(float)atof(q+10); if(v>2.f) g_shadowExt=v; printf("[shadow] window +-%.0f\n",g_shadowExt); }
                        /* ⭐ THE POST PASS's dials (09-11) */
                        if((q=strstr(buf,"post="))){ g_post=atoi(q+5)?1:0; }
                        if((q=strstr(buf,"bloom="))){ float s=0,t=0; int n=sscanf(q+6,"%f,%f",&s,&t); g_bloom=s; if(n==2) g_bloomThr=t; printf("[post] bloom %.2f over %.2f\n",g_bloom,g_bloomThr); }
                        if((q=strstr(buf,"knee="))){ g_postKnee=(float)atof(q+5); printf("[post] default knee %.2f\n",g_postKnee); }
                        if((q=strstr(buf,"fxaa="))){ g_fxaa=atoi(q+5)?1:0; printf("[post] fxaa %d\n",g_fxaa); }
                        if((q=strstr(buf,"grade="))){ g_grade=(float)atof(q+6); printf("[post] grade %.2f\n",g_grade); }
                        /* (step 25) the lens and film's look: engine/post.inc */
                        if((q=strstr(buf,"camroll="))){ g_camRoll=(float)atof(q+8); printf("[lens] rolled %.1f\n",g_camRoll); }
                        if((q=strstr(buf,"words="))){ g_tvWords=atoi(q+6)?1:0; }   /* (step 26) a page of every script the TV font has */
                        if((q=strstr(buf,"lang="))){ char c9[16]=""; sscanf(q+5,"%15[A-Za-z0-9_-]",c9); langSet(strcmp(c9,"off")?c9:"",0); }
                        if((q=strstr(buf,"trackt="))){ g_stkScrub=!strncmp(q+7,"off",3)?-1.f:!strncmp(q+7,"play",4)?-2.f:(float)atof(q+7); g_stkPlay0=now_ms()*0.001; g_stkLast=-1;
                            if(g_stkScrub>=0.f) printf("[track] held at %.2f s by hand\n",g_stkScrub); else printf(g_stkScrub<-1.5f?"[track] playing round on the lab's clock\n":"[track] back on the scene's clock\n"); }
                        if((q=strstr(buf,"focus="))){ lensFocusSay(q+6); printf("[lens] focus %s%.2f, soft %.2f\n",g_lensFocus<0.f?"on the aim ":"",g_lensFocus<0.f?0.f:g_lensFocus,g_lensDof); }
                        if((q=strstr(buf,"shutter="))){ g_lensShutter=(float)atof(q+8); printf("[lens] shutter %.2f\n",g_lensShutter); }
                        if((q=strstr(buf,"sepia="))){ g_filmSepia=(float)atof(q+6); printf("[film] sepia %.2f\n",g_filmSepia); }
                        if((q=strstr(buf,"grain="))){ g_filmGrain=(float)atof(q+6); printf("[film] grain %.2f\n",g_filmGrain); }
                        if((q=strstr(buf,"flash="))){ g_filmFlash=(float)atof(q+6); printf("[film] flash %.2f\n",g_filmFlash); }
                        if((q=strstr(buf,"burn="))){ g_filmBurn=(float)atof(q+5); printf("[film] burn %.2f\n",g_filmBurn); }
                        if((q=strstr(buf,"weave="))){ g_filmWeave[0]=g_filmWeave[1]=0.f; sscanf(q+6,"%f,%f",&g_filmWeave[0],&g_filmWeave[1]); printf("[film] weave %.4f,%.4f\n",g_filmWeave[0],g_filmWeave[1]); }
                        if((q=strstr(buf,"vig="))){ g_vig=(float)atof(q+4); printf("[post] vignette %.2f\n",g_vig); }
                        if((q=strstr(buf,"fog="))){ float d=0,r=0,g=0,b=0; int n=sscanf(q+4,"%f,%f,%f,%f",&d,&r,&g,&b);
                            if(!strncmp(q+4,"off",3)){ d=0; n=1; }
                            g_fog[3]=d; if(n==4){ g_fog[0]=r; g_fog[1]=g; g_fog[2]=b; g_fogAuto=0; } else g_fogAuto=1;
                            printf("[fog] density %.4f colour %s\n",d,n==4?"named":"from the sky horizon"); fflush(stdout); }
                        /* ⭐ step=<fps> -- STOP MOTION (design/stop_motion.md). The bodies expose at
                         * this rate and hold between; the lens stays smooth. 0 = on ones.
                         * boil=<k> scales the animator's hand (0 = none, 1 = default). */
                        if((q=strstr(buf,"step="))&&(q==buf||q[-1]==' ')){ float v=(float)atof(q+5);
                            g_stepCtl=g_stepFps=(v>0.f)?v:0.f;
                            printf("[step] ctl: %.0f exposures/sec%s\n",g_stepFps,g_stepFps>0.f?"":" (off)"); fflush(stdout); }
                        if((q=strstr(buf,"boil="))&&(q==buf||q[-1]==' ')) g_boil=(float)atof(q+5);
                        /* ⭐ smoke=0 clears and stops every wisp and exhale; smoke=1 lets them back */
                        if((q=strstr(buf,"smoke="))&&(q==buf||q[-1]==' ')){ g_smokeOn=atoi(q+6)?1:0;
                            if(!g_smokeOn){ g_nSmk=0; g_nJet=0; }
                            printf("[smoke] ctl: %s\n",g_smokeOn?"ON":"OFF (cleared)"); fflush(stdout); }
                        /* ⭐ shotsize=<name>|auto and reverse=0|1|auto -- override the
                         * loaded scene's own lens for EVERY line, so a framing can be shot in the
                         * lab off any scene instead of needing one authored for it.
                         * ⛔ NOT `shot=`: that is the SCREENSHOT key, and has been since 08-25. */
                        if((q=strstr(buf,"shotsize="))){ q+=9;
                            static const struct { const char*nm; int v; } SNC[]={
                                {"ots",SHOT_OTS},{"tight",SHOT_TIGHT},{"two",SHOT_TWO},
                                {"wide",SHOT_WIDE},{"solo",SHOT_SOLO},{"medium",SHOT_MEDIUM},{"auto",-1} };
                            int f9=-2;
                            for(int k9=0;k9<7;k9++){ size_t n9=strlen(SNC[k9].nm);
                                if(!strncmp(q,SNC[k9].nm,n9)&&(q[n9]==' '||q[n9]=='\n'||q[n9]=='\r'||q[n9]==0)){ f9=SNC[k9].v; break; } }
                            if(f9==-2){ char bad9[24]; int kb=0;
                                while(q[kb]&&q[kb]!=' '&&q[kb]!='\n'&&q[kb]!='\r'&&kb<23){ bad9[kb]=q[kb]; kb++; } bad9[kb]=0;
                                printf("[lens] ctl: unknown shot '%s' -- it is wide/medium/two/ots/tight/solo/auto\n",bad9); }
                            else { g_shotForce=f9;
                                printf("[lens] ctl: shot %s\n",(f9<0)?"auto (the script's own)":SHOT_NAME[f9&7]); }
                            fflush(stdout); }
                        if((q=strstr(buf,"reverse="))){
                            g_revForce=(!strncmp(q+8,"auto",4))?-1:(atoi(q+8)?1:0);
                            printf("[lens] ctl: reverse %s\n",(g_revForce<0)?"auto (the script's own)":(g_revForce?"1":"0")); fflush(stdout); }
                        /* ⭐ THE WARDROBE FROM THE CTL (09-29): outfit=<name> dresses the hero, poutfit= the partner;
                         * wear=+piece,-piece / pwear= put one piece on or take it off. ⛔ `outfit=` is inside
                         * `poutfit=` and `wear=` inside `pwear=`: the leading-space guard. */
                        if((q=strstr(buf,"outfit="))&&(q==buf||q[-1]==' ')){ char o9[24]; int k9=0; q+=7;
                            while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k9<23) o9[k9++]=*q++;
                            o9[k9]=0;
                            if(g_heroPk) wardWear(g_heroPk,o9); }
                        if((q=strstr(buf,"poutfit="))&&(q==buf||q[-1]==' ')){ char o9[24]; int k9=0; q+=8;
                            while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k9<23) o9[k9++]=*q++;
                            o9[k9]=0;
                            if(g_labPartner) wardWear(g_labPartner,o9); }
                        if((q=strstr(buf,"wear="))&&(q==buf||q[-1]==' ')){ char o9[256]; int k9=0; q+=5;
                            while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k9<255) o9[k9++]=*q++;
                            o9[k9]=0;
                            if(g_heroPk) wardToggle(g_heroPk,o9); }
                        if((q=strstr(buf,"pwear="))&&(q==buf||q[-1]==' ')){ char o9[256]; int k9=0; q+=6;
                            while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k9<255) o9[k9++]=*q++;
                            o9[k9]=0;
                            if(g_labPartner) wardToggle(g_labPartner,o9); }
                        /* ⭐ jawgain=<f> -- the puppet jaw's opening scale in `mouth hinge` mode
                         * A scene's own jawgain= beats it, and a scene STARTS on it. */
                        if((q=strstr(buf,"jawgain="))&&(q==buf||q[-1]==' ')){ float v=(float)atof(q+8);
                            if(v<0.f) v=0.f;
                            if(v>4.f) v=4.f;
                            g_jawGainCtl=g_jawGain=v; printf("[jaw] gain %.2f (ctl)\n",v); fflush(stdout); }
                        /* ⭐ hprop= / pprop= / p2prop= FROM THE CTL. The spec was only ever
                         * reachable from a .scn, so a held prop -- and now a held LIGHT -- could not
                         * be put in a hand from the lab and looked at. Same string, same parser.
                         * ⛔ `pprop=` matches inside `p2prop=`: the digit guard, as everywhere. */
                        if((q=strstr(buf,"hprop="))){ int k=0; q+=6;
                            while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<PROP_SPEC-1) g_castProp[0][k++]=*q++;
                            g_castProp[0][k]=0;
                            printf("[held] ctl slot 0 '%s'\n",g_castProp[0]); fflush(stdout); }
                        { char*q2=strstr(buf,"pprop="); while(q2&&q2>buf&&q2[-1]>='0'&&q2[-1]<='9') q2=strstr(q2+1,"pprop=");
                          if(q2){ int k=0; q2+=6;
                            while(*q2&&*q2!=' '&&*q2!='\n'&&*q2!='\r'&&k<PROP_SPEC-1) g_castProp[1][k++]=*q2++;
                            g_castProp[1][k]=0;
                            printf("[held] ctl slot 1 '%s'\n",g_castProp[1]); fflush(stdout); } }
                        if((q=strstr(buf,"p2prop="))){ int k=0; q+=7;
                            while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<PROP_SPEC-1) g_castProp[2][k++]=*q++;
                            g_castProp[2][k]=0;
                            printf("[held] ctl slot 2 '%s'\n",g_castProp[2]); fflush(stdout); }
                        /* ⭐ THE SECOND HAND FROM THE CTL. ⛔ The `=` keeps the long and
                         * short keys apart -- see the .scn stage parser -- and the digit guard
                         * is carried on the p-form exactly as above. */
                        if((q=strstr(buf,"hprop2="))){ int k=0; q+=7;
                            while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<PROP_SPEC-1) g_castProp2[0][k++]=*q++;
                            g_castProp2[0][k]=0;
                            printf("[held] ctl slot 0 prop2 '%s'\n",g_castProp2[0]); fflush(stdout); }
                        { char*q2=strstr(buf,"pprop2="); while(q2&&q2>buf&&q2[-1]>='0'&&q2[-1]<='9') q2=strstr(q2+1,"pprop2=");
                          if(q2){ int k=0; q2+=7;
                            while(*q2&&*q2!=' '&&*q2!='\n'&&*q2!='\r'&&k<PROP_SPEC-1) g_castProp2[1][k++]=*q2++;
                            g_castProp2[1][k]=0;
                            printf("[held] ctl slot 1 prop2 '%s'\n",g_castProp2[1]); fflush(stdout); } }
                        if((q=strstr(buf,"p2prop2="))){ int k=0; q+=8;
                            while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<PROP_SPEC-1) g_castProp2[2][k++]=*q++;
                            g_castProp2[2][k]=0;
                            printf("[held] ctl slot 2 prop2 '%s'\n",g_castProp2[2]); fflush(stdout); }
                        /* ⭐ tone=<knee> -- the highlight shoulder (0 = off). See g_tone. */
                        if((q=strstr(buf,"tone="))&&(q==buf||q[-1]==' ')){ float v=(float)atof(q+5);
                            if(v<0.f) v=0.f;
                            if(v>0.90f) v=0.90f;   /* the knee must sit under the 0.96 white point */
                            g_toneCtl=g_tone=v; printf("[tone] ctl: knee %.2f%s\n",v,v>0.f?"":" (off)"); fflush(stdout); }
                        /* scene=<name> plays scenes/<name>.scn (voices, subtitles,
                         * faces and camera coverage); scene=off stops it. */
                        if((q=strstr(buf,"scene="))){ char nm5[64]; int k5=0; q+=6;
                            while(*q&&*q!='\n'&&*q!='\r'&&*q!=' '&&k5<63) nm5[k5++]=*q++;
                            nm5[k5]=0;
                            if(!k5||!strcmp(nm5,"off")){ g_scnT=-1.f;
                                sfxStop(g_scnVoice); g_scnVoice=-1;
                                for(int m5=0;m5<MORPH_MAX;m5++) g_heroMorphW[m5]=0.f;
                                memset(g_castMorphW,0,sizeof g_castMorphW);
                                printf("[scene] off\n"); fflush(stdout); }
                            else scnLoad(nm5); }
                        /* set=<name> lays a world down; set=off clears it. */
                        if((q=strstr(buf,"sun="))){
                            if(!strncmp(q+4,"off",3)){ g_sun[3]=0.f; printf("[sun] off\n"); }
                            else { float el=45.f,az=30.f,it=1.f,r=1.f,g2=0.97f,b=0.90f;
                                int n8=sscanf(q+4,"%f,%f,%f,%f,%f,%f",&el,&az,&it,&r,&g2,&b);
                                if(n8>=3){ float e=el*0.0174532925f, a2=az*0.0174532925f;
                                    g_sun[0]=cosf(e)*sinf(a2); g_sun[1]=sinf(e); g_sun[2]=cosf(e)*cosf(a2);
                                    g_sun[3]=it;
                                    if(n8>=6){ g_sunC[0]=r; g_sunC[1]=g2; g_sunC[2]=b; }
                                    printf("[sun] elev %.0f azim %.0f  I %.2f  dir %.2f,%.2f,%.2f  rgb %.2f,%.2f,%.2f\n",
                                           el,az,it,g_sun[0],g_sun[1],g_sun[2],g_sunC[0],g_sunC[1],g_sunC[2]); } }
                            fflush(stdout); }
                        if((q=strstr(buf,"tod="))){
                            if(!strncmp(q+4,"off",3)){ g_tod=-1.f; g_todRate=0.f; printf("[tod] off\n"); }
                            else { g_tod=(float)atof(q+4); todApply(g_tod);
                                printf("[tod] %05.2f  sun dir %.2f,%.2f,%.2f I %.2f  rgb %.2f,%.2f,%.2f  sky %.3f,%.3f,%.3f\n",
                                       g_tod,g_sun[0],g_sun[1],g_sun[2],g_sun[3],
                                       g_sunC[0],g_sunC[1],g_sunC[2],g_sky[0],g_sky[1],g_sky[2]); }
                            fflush(stdout); }
                        if((q=strstr(buf,"todrate="))) g_todRate=(float)atof(q+8);
                        if((q=strstr(buf,"sky="))) sscanf(q+4,"%f,%f,%f",&g_sky[0],&g_sky[1],&g_sky[2]);
                        /* ⭐ lamps=<scale>: the shot's dimmer on the SET's own practicals.
                         * A day set shot at night needs its lamps taken down, and the alternative
                         * was forking the .set file per scene. 1.0 = untouched. */
                        if((q=strstr(buf,"lamps="))){ float s9=(float)atof(q+6);
                            if(s9<0.f) s9=0.f;
                            if(s9!=g_lampScale){ g_lampScale=s9;
                                printf("[lamps] set practicals x%.2f", g_lampScale);
                                for(int k9=0;k9<g_setNL;k9++)
                                    printf("  L%d %.2f->%.2f",k9,g_setLc[k9*3],g_setLc[k9*3]*g_lampScale);
                                printf("%c",10); fflush(stdout); } }
                        if((q=strstr(buf,"cam="))){
                            if(!strncmp(q+4,"off",3)){ g_camOn=0; printf("[cam] off\n"); }
                            else { float f=-1.f;
                                float rl7=0.f;
                                int n7=sscanf(q+4,"%f,%f,%f,%f,%f,%f,%f,%f",
                                              &g_camPos[0],&g_camPos[1],&g_camPos[2],
                                              &g_camAim[0],&g_camAim[1],&g_camAim[2],&f,&rl7);
                                if(n7>=6){ g_camOn=1; g_camFov=(n7>=7)?f:-1.f; g_camRoll=(n7>=8)?rl7:0.f;   /* (step 25) an eighth number rolls it; a lens placed without one is level */
                                    printf("[cam] eye %.1f,%.1f,%.1f -> aim %.1f,%.1f,%.1f fov %.0f\n",
                                           g_camPos[0],g_camPos[1],g_camPos[2],
                                           g_camAim[0],g_camAim[1],g_camAim[2],g_camFov); }
                                else printf("[cam] need ex,ey,ez,ax,ay,az[,fov]\n"); }
                            fflush(stdout); }
                        /* camto= the destination lens; camsecs= starts the move.
                         * \u26d4 A IS SNAPSHOTTED AT START, not read live each frame -- lerping
                         * from the live lens into B feeds the result back into its own input,
                         * which is an ease-out that never arrives and cannot be replayed. */
                        if((q=strstr(buf,"camto="))){ float f=-1.f;
                            int n=sscanf(q+6,"%f,%f,%f,%f,%f,%f,%f",
                                         &g_camB[0],&g_camB[1],&g_camB[2],
                                         &g_camAimB[0],&g_camAimB[1],&g_camAimB[2],&f);
                            if(n>=6){ g_camFovB=(n>=7)?f:-1.f;
                                printf("[cam] to %.1f,%.1f,%.1f -> %.1f,%.1f,%.1f fov %.0f\n",
                                       g_camB[0],g_camB[1],g_camB[2],
                                       g_camAimB[0],g_camAimB[1],g_camAimB[2],g_camFovB); }
                            else printf("[cam] camto needs ex,ey,ez,ax,ay,az[,fov]\n");
                            fflush(stdout); }
                        if((q=strstr(buf,"camease="))) g_camEase=atoi(q+8)?1:0;
                        if((q=strstr(buf,"camsecs="))){ float T=(float)atof(q+8);
                            if(T>1e-4f&&g_camOn){
                                memcpy(g_camA,g_camPos,12); memcpy(g_camAimA,g_camAim,12);
                                g_camFovA=g_camFov;
                                g_camMoveDur=T; g_camMoveT=0.f; g_camMoveOn=1;
                                printf("[cam] moving over %.2fs (%s)\n",
                                       T,g_camEase?"eased":"linear"); }
                            else { g_camMoveOn=0;
                                printf("[cam] camsecs ignored (%s)\n",
                                       g_camOn?"T<=0":"no placed lens: set cam= first"); }
                            fflush(stdout); }
                        /* ⭐ lean=0 stands a rider on a path bolt upright (the carve's A/B); lean=1 leans again */
                        if((q=strstr(buf,"lean="))&&(q==buf||q[-1]==' '||q[-1]=='\t'||q[-1]=='\n'||q[-1]=='\r')){
                            g_leanOff=(atoi(q+5)==0); printf("[lean] carve %s\n",g_leanOff?"OFF (lean=0)":"on"); fflush(stdout); }
                        if((q=strstr(buf,"set="))){ char nm6[64]; int k6=0; q+=4;
                            while(*q&&*q!='\n'&&*q!='\r'&&*q!=' '&&k6<63) nm6[k6++]=*q++;
                            nm6[k6]=0;
                            if(strcmp(nm6,g_setCur)){ setLoad(nm6);
                                /* ⭐ 10-22: a set may bring its own sun and sky -- the ctl line's own still win */
                                const char*q7;
                                if((q7=strstr(buf,"sky="))) sscanf(q7+4,"%f,%f,%f",&g_sky[0],&g_sky[1],&g_sky[2]);
                                if((q7=strstr(buf,"sun="))){
                                    if(!strncmp(q7+4,"off",3)) g_sun[3]=0.f;
                                    else { float el7=45.f,az7=30.f,it7=1.f,r7=1.f,g7=0.97f,b7=0.90f;
                                        int n7=sscanf(q7+4,"%f,%f,%f,%f,%f,%f",&el7,&az7,&it7,&r7,&g7,&b7);
                                        if(n7>=3){ float e7=el7*0.0174532925f, a7=az7*0.0174532925f;
                                            g_sun[0]=cosf(e7)*sinf(a7); g_sun[1]=sinf(e7); g_sun[2]=cosf(e7)*cosf(a7); g_sun[3]=it7;
                                            if(n7>=6){ g_sunC[0]=r7; g_sunC[1]=g7; g_sunC[2]=b7; } } } } } }
                        if((q=strstr(buf,"hi5="))) g_labHi5=atoi(q+4);   /* 1=hands, 2=held-hands, 3=kiss (head to head), 4=massage (hero hand to her shoulder) */
                        /* P7 pclip=<name>: what the partner plays -- HER OWN pack first,
                         * then ANY loaded bank, retargeted onto her.
                         * ⭐ THE SCENE PATH COULD ALREADY BORROW AND THIS ONE COULD NOT, so a
                         * body cast as the partner could be DIRECTED inside a compiled scene but
                         * not POSED BY HAND -- and hand-posing is how a scene gets blocked before
                         * it is ever shot.
                         * ⛔ The bought character arrives with ONE clip (a two-frame rest stub; her Fab
                         * download carries no animation at all), so pclip=w_look came back NOT
                         * FOUND and she could not be looked at in a pose -- while the retarget
                         * that serves it sat in bankFind, already gated, ~370 lines above. */
                        if((q=strstr(buf,"pclip="))&&g_labPartner){ char nm4[64]; int k4=0; q+=6;
                            while(*q&&*q!='\n'&&*q!='\r'&&*q!=' '&&k4<63) nm4[k4++]=*q++;
                            nm4[k4]=0;
                            int f4=-1;
                            for(unsigned c4=0;c4<g_labPartner->nClips;c4++)
                                if(!strcmp(packStr(g_labPartner,g_labPartner->clips[c4].name),nm4)){ f4=(int)c4; break; }
                            if(f4>=0){ g_labPClip=f4; g_labPPhase=0.0;
                                g_labPDnP=NULL; g_labPDnC=-1;
                                printf("[lab] pclip -> %d '%s' (%uf @ %gfps)\n",f4,nm4,
                                       g_labPartner->clips[f4].frames,g_labPartner->clips[f4].fps); }
                            else { int bc4=-1; Pack*bp4=bankFind(nm4,&bc4);
                                /* the SAME gate the scene path uses: a rotation transfer needs a
                                 * rig whose bones this engine can name. */
                                if(bp4&&g_labPartner->cbOK&&g_labPartner->pbOK>=10){
                                    g_labPDnP=bp4; g_labPDnC=bc4; g_labPPhase=0.0;
                                    /* ⛔ A BORROWED POSE STILL NEEDS AN OWN-CLIP INDEX.
                                     * labDrawPartner returns on clip<0 and draws NOTHING, so a
                                     * donor pointed at a bank with g_labPClip left at -1 is an
                                     * INVISIBLE partner -- the trap the scene path documents. */
                                    if(g_labPClip<0) g_labPClip=(g_labPartner->idleIdx>=0)?g_labPartner->idleIdx:0;
                                    printf("[lab] pclip '%s' BORROWED from %s (%uf)\n",
                                           nm4,bp4->label,bp4->clips[bc4].frames); }
                                else { g_labPClip=-1; g_labPDnP=NULL; g_labPDnC=-1;
                                    /* say WHY, not just no: own-clip miss, no bank, or a rig the
                                     * transfer refuses -- three different fixes. */
                                    printf("[lab] pclip '%s' NOT FOUND (%u own, bank=%s, cbOK=%d pbOK=%d)\n",
                                           nm4,g_labPartner->nClips,bp4?bp4->label:"-",
                                           g_labPartner->cbOK,g_labPartner->pbOK); } } }
                        /* P0 reload=1: re-read the subject's rpack from disk into the RUNNING
                         * lab -- the iteration loop must not cost an 11s boot per attempt.
                         * Valid under the append-only bake (bake_moves.sh): nodes, meshes and
                         * textures are byte-identical, so the GPU uploads stay right and only
                         * the pk/clip/string pointers move. A changed skeleton is refused. */
                        if((q=strstr(buf,"reload="))&&atoi(q+7)&&g_heroPk&&heroPick[0]){
                            FILE*rf=fopen(heroPick,"rb");
                            if(!rf) printf("[lab] RELOAD: cannot open %s\n",heroPick);
                            else { fseek(rf,0,SEEK_END); long rl=ftell(rf); fseek(rf,0,SEEK_SET);
                                unsigned char*npk=(unsigned char*)malloc((size_t)rl);
                                if(npk&&fread(npk,1,(size_t)rl,rf)==(size_t)rl&&rl>64&&!memcmp(npk,"RPK1",4)){
                                    { size_t rl9=(size_t)rl; packChunksMerge(&npk,&rl9,heroPick); rl=(long)rl9; }
                                    unsigned*NH=(unsigned*)(npk+4);
                                    if(NH[1]==g_heroPk->nNodes&&NH[2]==g_heroPk->nMeshes){
                                        unsigned oldN=g_heroPk->nClips;
                                        free(g_heroPk->pk);
                                        g_heroPk->pk=npk; g_heroPk->len=(size_t)rl;
                                        free(g_heroPk->nodes); g_heroPk->nodes=packNodes(npk,NH[6],NH[1]);
                                        g_heroPk->meshes=(PMesh*)(npk+NH[7]);
                                        g_heroPk->mats=npk+NH[8];
                                        g_heroPk->nClips=NH[4];
                                        g_heroPk->clips=(PClip*)(npk+NH[9]);
                                        if(g_heroPk->gaitUPS&&NH[4]>oldN){   /* sized per clip at load */
                                            float*ng=(float*)realloc(g_heroPk->gaitUPS,NH[4]*sizeof(float));
                                            if(ng){ memset(ng+oldN,0,(NH[4]-oldN)*sizeof(float)); g_heroPk->gaitUPS=ng; } }
                                        if(g_labClip>=(int)NH[4]) g_labClip=-1;
                                        printf("[lab] RELOAD %s: %u -> %u clips\n",heroPick,oldN,NH[4]); }
                                    else { printf("[lab] RELOAD refused: skeleton changed (%u/%u nodes, %u/%u meshes)\n",
                                                  NH[1],g_heroPk->nNodes,NH[2],g_heroPk->nMeshes); free(npk); } }
                                else { printf("[lab] RELOAD: bad read of %s\n",heroPick); free(npk); }
                                fclose(rf); } }
                        printf("[lab] seq %ld -> state %s cam %d gait %.3f trim %.2f speed %.2f%s\n",
                               sv,LAB_NAME[GM.stage],g_labCam,gaitMul(),g_servo,g_labSpeed,
                               g_shotNow[0]?"  (shot queued)":""); fflush(stdout); } } } }
                /* THE MEASURED FLOOR: a 1u grid, every 5th line bright, so a stride can be
                 * counted against real units instead of guessed at. */
                { float cx=floorf(g_rmActive?g_rmEndW[0]:g_rvx), cz=floorf(g_rmActive?g_rmEndW[1]:g_rvz);   /* the floor follows a displaced rm body */
                  for(int i9=-9;i9<=9;i9++){
                      float b9=((int)(cz+i9)%5==0)?0.55f:0.16f;
                      gmLine(cx-9.f,0.02f,cz+i9,cx+9.f,0.02f,cz+i9,b9*0.4f,b9,b9*0.8f);
                      float b8=((int)(cx+i9)%5==0)?0.55f:0.16f;
                      gmLine(cx+i9,0.02f,cz-9.f,cx+i9,0.02f,cz+9.f,b8*0.4f,b8,b8*0.8f); } }
                /* FOOT CONTACT BREADCRUMBS: a mark wherever a foot is down. Feet that
                 * plant leave clean pairs; feet that slide smear them into streaks --
                 * foot-skate you can SEE, next to the number that measures it. */
                if(g_heroFOK){
                    int low=(g_heroF[0][1]<=g_heroF[1][1])?0:1;
                    static float pfx=0,pfz=0; static int pLow=-1;
                    if(g_heroF[low][1]<0.30f){
                        if(pLow==low){ float d9=hypotf(g_heroF[low][0]-pfx,g_heroF[low][2]-pfz);
                                       if(d9<1.5f) g_labSlip+=d9; }
                        if(pLow!=low&&g_bcN<BC_MAX){   /* a new footfall */
                            g_bc[g_bcN].x=g_heroF[low][0]; g_bc[g_bcN].z=g_heroF[low][2];
                            g_bc[g_bcN].t=0.f; g_bc[g_bcN].side=low; g_bcN++; }
                        pfx=g_heroF[low][0]; pfz=g_heroF[low][2]; pLow=low; }
                    else pLow=-1; }
                { static float px9=0,pz9=0; static int pi9=0;
                  if(pi9) g_labTravel+=hypotf(g_rvx-px9,g_rvz-pz9);
                  px9=g_rvx; pz9=g_rvz; pi9=1; }
                for(int b9=0;b9<g_bcN;b9++){ g_bc[b9].t+=dtf;
                    float f9=1.f-g_bc[b9].t/9.f; if(f9<=0.f) continue;
                    float r9=g_bc[b9].side?1.3f*f9:0.3f*f9, gg9=0.5f*f9, bb9=g_bc[b9].side?0.3f*f9:1.3f*f9;
                    gmLine(g_bc[b9].x-0.22f,0.05f,g_bc[b9].z,g_bc[b9].x+0.22f,0.05f,g_bc[b9].z,r9,gg9,bb9);
                    gmLine(g_bc[b9].x,0.05f,g_bc[b9].z-0.22f,g_bc[b9].x,0.05f,g_bc[b9].z+0.22f,r9,gg9,bb9); }
                if(g_scnT>=0.f||g_setNI||g_setSpace){ GM.l1[0]=GM.l2[0]=GM.l3[0]=0; }   /* a scene -- or a SET (10-22: or space) -- owns the screen */
                else { float ratio=(g_labTravel>0.5f)?(g_labSlip/g_labTravel):0.f;
                  snprintf(GM.l1,sizeof GM.l1,"STATE %d/%d  %s%s",GM.stage+1,LAB_N,LAB_NAME[GM.stage],
                           GM.flag?"  [PINNED]":"");
                  snprintf(GM.l2,sizeof GM.l2,"clip %d  rate %.2f (trim %.2f)  spd %.2f u/s  feet %.2f / %.2f",
                           g_rvClipNow,g_rvRateNow,g_servo,g_manSpd,
                           g_heroFOK?g_heroF[0][1]:-1.f,g_heroFOK?g_heroF[1][1]:-1.f);
                  snprintf(GM.l3,sizeof GM.l3,"FOOT SKATE %.2f (0=plants)  servo x%.2f resid %.3fu  steps %d  A NEXT X BACK B RESET",
                           ratio,g_servo,g_servoSlip,g_bcN); }
                } break;
            default: break; }
        }

        /* S5a WEAPON PASS: every gun, bat and floor pickup, drawn solid. */
        if(g_wdrawN){
            for(int i9=0;i9<g_wdrawN;i9++){
                int d9=g_wdraw[i9].def; if(d9<0||!g_wpnPackOK[d9]) continue;
                float yw9=g_wdraw[i9].yaw-1.5708f;   /* the rack runs along +X; the engine faces +Z */
                float ca9=cosf(yw9), sa9=sinf(yw9);
                M4 wm; memset(wm,0,64);
                wm[0]=ca9; wm[2]=-sa9; wm[5]=1.f; wm[8]=sa9; wm[10]=ca9; wm[15]=1.f;
                wm[12]=g_wdraw[i9].x; wm[13]=g_wdraw[i9].y; wm[14]=g_wdraw[i9].z;
                drawProp(&g_wpnPack[d9],wm); }
            g_wdrawN=0;}
        /* GAME OVERLAY FLUSH: the mode's neon geometry, one additive line batch. */
        { static long gmDbg=0; if(getenv("REVA_GMDBG")&&(gmDbg++%120)==0)
            printf("[gm] mode=%d n=%d v0=(%.1f,%.1f,%.1f rgb %.1f,%.1f,%.1f)\n",
                   g_mode,gmN,gmV[0],gmV[1],gmV[2],gmV[3],gmV[4],gmV[5]); }
        if(g_scnT>=0.f) gmN=gmSN=0;   /* ...and the lab's instruments with it: the measured
                                  * floor grid, the footfall breadcrumbs and the runway
                                  * marker are for judging a gait, not for a scene. Zero
                                  * the batch rather than skipping the flush -- the flush
                                  * is what resets it. */
        if(gmN&&!(g_mode==MODE_ANIM&&g_labGrid>1)){   /* the multi-cam wall owns the screen */
            static unsigned gmVbo=0; if(!gmVbo) gmVbo = gfxBufferNew();
            gfxBufferData(gmVbo, gmN*24, gmV, 1);
            if(!g_noDraw) gfxLines3D(gmV,gmN);

            gmN=0; }
        if(gmSN&&!(g_mode==MODE_ANIM&&g_labGrid>1)&&!g_noDraw) gfxLines3DSolid(gmS,gmSN);   /* (11-12) and its solid lines, their own colour */
        gmSN=0;
        passMark("game");
        /* HUD — the terminal readout returns (green/pink CRT text, top-right) */
        int labClean=((g_mode==MODE_ANIM||GAME_ON)&&!getenv("REVA_ANIMHUD"));   /* REVA_ANIMHUD=1 puts the readouts back. The zombie game is not a venue: no clock, no temperature, no chord */
        /* ⭐ THE HUD CANVAS (2026-09-30). Every readout was laid out in pixels for a 1280-wide window and ran off a
         * 640-wide one (and would off a composite CRT). The overlay now draws on a 1280-wide virtual canvas whose
         * height follows the aspect: at 1280x720 it is pixel-identical to before, elsewhere it scales. */
        const float HW=1280.f, HH=(g_dispAspect>0.f)?1280.f/g_dispAspect:1280.f*(float)H/(float)W;
        g_hudW=HW; g_hudH=HH;
        if(g_tvW!=W||g_tvH!=H) tvInit(W,H,g_dispAspect>0.f);
        {
          /* SUBTITLES: the speaker's line across the bottom, in her own colour, with a
           * drop shadow so it survives a bright frame. Centred by the font's own metric
           * (stb_easy_font is ~6px per character at scale 1). */
          artOver();   /* (11-11) the set's drawings that go over the finished picture, under the captions (setart.inc) */
          if(g_scnT>=0.f&&g_capOK) capDraw(HW,HH);   /* ⭐ (10-23) the captions (see capDraw); REVA_OLDCAPS=1 is the control */
          else if(g_scnT>=0.f){ int li9=scnActive();
              if(li9>=0&&!g_scnL[li9].silent){ const char*tx=g_scnL[li9].text;   /* ⭐ an ACTION line has no subtitle */
                  /* A SCRIM. The words landed on whoever was standing at the bottom of
                   * the frame -- the co-star's midriff ate a whole line in the two-shot. Four
                   * stacked bands fake the gradient a single flat colour cannot. */
                  { static float bd[12]; float y0=HH-146.f, st=36.5f;
                    for(int b6=0;b6<4;b6++){
                        float ya=y0+b6*st, yb=ya+st+1.f;
                        bd[0]=0;bd[1]=ya; bd[2]=HW;bd[3]=ya; bd[4]=HW;bd[5]=yb;
                        bd[6]=0;bd[7]=ya; bd[8]=HW;bd[9]=yb; bd[10]=0;bd[11]=yb;
                        gfxHud(bd,6,HW,HH,0.f,0.f,0.f,0.10f+0.17f*(float)b6);
                        } }
                  float sc9=2.4f; float w9=6.0f*sc9*(float)strlen(tx);
                  float lim9=HW-110.f;
                  if(w9>lim9){ sc9*=lim9/w9; w9=lim9; }   /* long lines shrink to fit, never run off */
                  float x9=(HW-w9)*0.5f, y9=HH-62.f;
                  /* ⭐ BY SLOT, NOT BY "IS IT REVA". Three speakers need three colours --
                   * the audience tracks who is talking in a dark room by hue before they
                   * read a word of it. reva cyan (hers since the first show), the co-star warm,
                   * slot 2 green. ⛔ And the NAME is indexed too: slot 2's dialogue used to
                   * come up over the co-star's name, which is the exact bug that already
                   * happened once to slot 2 and would make a long scene unreadable. */
                  int sp9=g_scnL[li9].spk; if(sp9<0||sp9>=SCN_MAXCAST) sp9=0;
                  static const float SPKC[SCN_MAXCAST][3]={
                      {0.55f,0.95f,1.00f},   /* reva  -- cyan */
                      {1.00f,0.78f,0.42f},   /* the co-star -- warm */
                      {0.62f,0.94f,0.58f},   /* slot 2  -- green */
                      {0.78f,0.74f,0.82f},   /* slot 3  -- dead grey-violet */
                      {0.86f,0.86f,0.90f}};  /* anyone else */
                  const float*cc9=SPKC[sp9];
                  drawText(x9+2.f,y9+2.f,sc9,tx,0.f,0.f,0.f,0.75f);
                  drawText(x9,y9,sc9,tx,cc9[0],cc9[1],cc9[2],0.98f);
                  drawText(x9,y9-30.f,1.7f,g_scnWho[sp9],cc9[0]*0.62f,cc9[1]*0.72f,cc9[2]*0.70f,0.85f); } }
          char ln[128]; time_t tt=time(NULL); struct tm*tmv=localtime(&tt);
          static double fpsAvg=0; fpsAvg+=(1.0/(g_wallDt>0.001?g_wallDt:0.001)-fpsAvg)*0.05;
          /* under a fixed step this readout runs on the tick clock too: a check run's frame depends on nothing outside the run (the packet's hash found it, 11-02) */
          if(g_fixedStepMs>0.0){ static struct tm fx9; long s9=(long)(tsec); fx9.tm_hour=(int)(s9/3600); fx9.tm_min=(int)(s9/60%60); fx9.tm_sec=(int)(s9%60); tmv=&fx9; fpsAvg=1000.0/g_fixedStepMs; }
          /* THE OLD UI -- the page's scheme: GREEN sys block
           * top-right (#39ff8a), CYAN music info bottom-center (#7df9ff) with the CHORD in hot
           * pink (#ff7be0), credit line bottom-left. Layout mirrors reva_dance.html. */
          snprintf(ln,sizeof ln,"%02d:%02d:%02d",tmv->tm_hour,tmv->tm_min,tmv->tm_sec);
          const int tv9=g_dispAspect>0.f;   /* on composite the readouts go on the TV's grid: at 240p this font is two lines tall */
          if(!labClean&&tv9) tvTextC(0,1,ln,0.22f,1.0f,0.54f);
          if(!labClean&&!tv9) drawText(520,44,2.6f,ln,0.22f,1.0f,0.54f,0.95f);   /* pulled in: 480i overscan eats ~5% */
          /* 480i truth: loop=fields, so full frames = half.
           * On a PROGRESSIVE mode the loop already renders whole frames -- halving
           * there is the same lie inverted, so the divisor follows the mode. */
          snprintf(ln,sizeof ln,"SOC %.1fC  FLD %.0f  FPS %.0f",g_fixedStepMs>0.0?0.f:g_soc,fpsAvg,
#ifdef REVA_SDL
              fpsAvg);   /* [win32] a window is always progressive: no field halving */
#else
              fpsAvg*((mode.flags&DRM_MODE_FLAG_INTERLACE)?0.5f:1.0f));
#endif
          if(!labClean&&tv9) tvTextC(1,1,ln,0.22f,1.0f,0.54f);
          if(!labClean&&!tv9) drawText(452,76,1.5f,ln,0.22f,1.0f,0.54f,0.92f);
          snprintf(ln,sizeof ln,"V3D DIRECT  %d DANCERS",NS);
          if(!labClean&&tv9){ tvTextC(2,1,ln,0.22f,1.0f,0.54f); tvTextC(3,1,g_manual?"MANUAL - BACK RELEASES":"ENGINE UNDER CONSTRUCTION - LIVE",g_manual?0.3f:1.0f,g_manual?1.0f:0.82f,g_manual?0.5f:0.29f); }
          if(!labClean&&!tv9) drawText(452,93,1.5f,ln,0.22f,1.0f,0.54f,0.80f);
          if(!labClean&&!tv9) drawText(452,110,1.5f,g_manual?"F310 MANUAL - BACK RELEASES, START MENU":"ENGINE UNDER CONSTRUCTION - LIVE",g_manual?0.3f:1.0f,g_manual?1.0f:0.82f,g_manual?0.5f:0.29f,0.9f);
          if(g_skate){
              char sb[240];
              snprintf(sb,sizeof sb,"%ld",SK.score);
              drawText(40,40,3.0f,sb,1.00f,0.86f,0.20f,0.97f);
              snprintf(sb,sizeof sb,"%.1f  %s",SK.spd,SK.air?"AIR":"ROLL");
              drawText(40,74,1.3f,sb,0.55f,0.90f,1.00f,0.75f);
              if(SK.combo[0]){
                  snprintf(sb,sizeof sb,"%s   %ld x%d",SK.combo,SK.comboPts,SK.comboMult);
                  drawText(40,96,1.6f,sb,1.00f,0.55f,0.92f,0.95f); }
              if(SK.bannerT>0.f)
                  drawText(40,124,2.0f,SK.banner,1.00f,0.42f,0.28f,0.95f);
              drawText(40,660,1.1f,g_kbActive
                       ?"SHIFT PUSH   SPACE OLLIE   Q KICKFLIP   R HEELFLIP   E SHOVE-IT   Z/C GRAB   WASD STEER"
                       :"RT PUSH   A OLLIE   X KICKFLIP   Y HEELFLIP   B SHOVE-IT   LB/RB GRAB",
                       0.55f,0.75f,0.85f,0.60f); }
          /* ---- THE ZOMBIE GAME'S HUD, on the TV's grid (11-02): the numbers on two rows at the top, the banner big, one hint at the bottom. Reads at 480i and 240p. */
          if(GAME_ON){ tvWideSet(g_tune.hudWide); if(!g_pause){ g_ptrHitNext=0; if(!g_xrDraw) g_game->hud(g_game->state);   /* (11-12) what the pointer's over is what this hud says, afresh */   /* (11-11) a HUD laid over both eyes reads as nothing: in a headset a game writes in the world */
 g_tvView=-1; uiFlush(); faceHud(); sprFlush(); levelHud(); } }
          /* ---- GAME MODE HUD: the mode's three lines, its banner, and the beat lane ---- */
          else if(MODE_PLAYS){
              { char mh[80];
                if(MODE_VAR[g_mode][0]) snprintf(mh,sizeof mh,"%s / %s",MODE_NAME[g_mode],MODE_VAR[g_mode][g_modeVariant]);
                else snprintf(mh,sizeof mh,"%s",MODE_NAME[g_mode]);
                if(g_scnT<0.f&&!g_setNI&&!g_setSpace) drawText(40,150,1.4f,mh,0.49f,0.98f,1.0f,0.75f); }
              if(GM.l1[0]) drawText(40,174,1.9f,GM.l1,1.00f,0.86f,0.20f,0.95f);
              if(GM.l2[0]) drawText(40,200,1.5f,GM.l2,0.55f,0.90f,1.00f,0.90f);
              if(GM.l3[0]) drawText(40,222,1.3f,GM.l3,0.55f,0.90f,1.00f,0.75f);
              for(int f9=0;f9<3;f9++) if(g_feedT[f9]>0.f)   /* the kill feed under the HUD lines */
                  drawText(40,246+f9*17,1.2f,g_feed[f9],1.0f,0.95f,0.85f,fminf(1.f,g_feedT[f9])*0.9f);
              if(g_kbActive&&!g_skate&&!labClean)   /* the keyboard legend, only once a key has been touched */
                  drawText(40,HH-58.f,1.15f,
                      "WASD MOVE   MOUSE LOOK   LMB/SPACE ACT   RMB/E BOLT   Q SPECIAL   SHIFT LOCK   ESC MENU",
                      0.45f,0.85f,0.95f,0.62f);
              if(GM.msgT>0.f&&g_mode!=MODE_ANIM){ float pu=0.75f+0.25f*sinf((float)tsec*8.f);
                  float bs=2.9f; int bw=(int)(stb_easy_font_width(GM.msg)*bs);
                  drawText((HW-bw)*0.5f,HH*0.30f,bs,GM.msg,1.0f*pu,0.48f*pu,0.88f*pu,0.96f); }
              }
          /* ---- THE START MENU OVERLAY: dim the dance floor, three doors ---- */
          /* a menu left alone for five minutes goes dark, and any touch brings it back: the Wii's precedent for a static picture on a set that can burn */
          { static double pauseFrom=0.0; if(!g_pause||g_btnA||g_btnB||g_btnStart||fabsf(g_ly)>0.3f||fabsf(g_lx)>0.3f) pauseFrom=tsec; g_pauseDark=g_pause&&(tsec-pauseFrom>300.0); }
          if(g_pause){
              { static float dim[12];
                dim[0]=0;dim[1]=0; dim[2]=HW;dim[3]=0; dim[4]=HW;dim[5]=HH;
                dim[6]=0;dim[7]=0; dim[8]=HW;dim[9]=HH; dim[10]=0;dim[11]=HH;
                hudTris(dim,6,HW,HH,0.01f,0.005f,0.03f,g_pauseDark?0.97f:0.62f); }
              if(g_pauseDark){}
              else if(g_ctlOn) ctlDraw();
              else if(tv9||GAME_ON){ static const char*PT9[PAUSE_N]={"BACK TO THE GAME","MAIN MENU",PAUSE_SETUP "QUIT"};
                tvTextC(6,2,"PAUSED",0.49f,0.98f,1.0f);
                for(int m9=0;m9<PAUSE_N;m9++){ char pl[32]; snprintf(pl,sizeof pl,(m9==g_pauseSel)?"> %s <":"%s",PT9[m9]);
                    if(m9==g_pauseSel) tvTextC((PAUSE_N>5?10.f:11.f)+(float)m9*2.f,1,pl,1.f,0.92f,0.4f); else tvTextC((PAUSE_N>5?10.f:11.f)+(float)m9*2.f,1,pl,0.7f,0.75f,0.8f); }
                tvTextC(22,1,g_kbActive?"W/S PICK  SPACE OK  ESC CLOSE":"STICK PICK  A OK  B CLOSE",0.22f,1.0f,0.54f); }
              else { { const char*ttl="PAUSED";
                float sc9=4.4f; float tw=stb_easy_font_width((char*)ttl)*sc9;
                drawText((HW-tw)*0.5f,HH*0.26f,sc9,ttl,0.49f,0.98f,1.0f,0.95f); }
              { static const char*PM[PAUSE_N]={"BACK TO THE FLOOR","MAIN MENU",PAUSE_SETUP "QUIT"};
                for(int m9=0;m9<PAUSE_N;m9++){
                    float sc9=(m9==g_pauseSel)?2.8f:2.1f;
                    char pl[48]; snprintf(pl,sizeof pl,(m9==g_pauseSel)?"> %s <":"%s",PM[m9]);
                    float tw=stb_easy_font_width(pl)*sc9;
                    float pu=(m9==g_pauseSel)?(0.75f+0.25f*sinf((float)tsec*6.f)):1.f;
                    if(m9==g_pauseSel) drawText((HW-tw)*0.5f,HH*0.42f+m9*56.f,sc9,pl,1.0f*pu,0.48f*pu,0.88f*pu,0.97f);
                    else drawText((HW-tw)*0.5f,HH*0.42f+m9*56.f,sc9,pl,0.55f,0.60f,0.66f,0.85f); } }
              { const char*hint=g_kbActive?"W/S PICK   SPACE OR CLICK CONFIRM   ESC CLOSES"
                                          :"STICK PICK   A CONFIRM   START OR B CLOSE";
                float sc9=1.5f; float tw=stb_easy_font_width((char*)hint)*sc9;
                drawText((HW-tw)*0.5f,HH*(PAUSE_N>5?0.92f:0.74f),sc9,hint,0.22f,1.0f,0.54f,0.75f); } } }
          if(g_chord[0]&&tv9){ char c9[TV_COLS+1]; snprintf(c9,sizeof c9,"%s",g_chord); tvTextC(21,1,c9,1.0f,0.48f,0.88f); }
          else if(g_chord[0]){   /* bottom-center musinfo: pink chord over cyan band line */
              int cw=(int)(strlen(g_chord)*8*2.0f);
              drawText((720-cw)/2,406,2.0f,g_chord,1.0f,0.48f,0.88f,0.95f); }
          if(!labClean){ char mus[96]; snprintf(mus,sizeof mus,"TIME %d/4  %s %s  KEY %s  %s",
                g_bpb,g_genre,g_style,g_key,g_sect);
            int cw=(int)(strlen(mus)*8*1.4f);
            if(tv9){ mus[TV_COLS]=0; tvTextC(22,1,mus,0.49f,0.98f,1.0f); }
            else drawText((720-cw)/2,428,1.4f,mus,0.49f,0.98f,1.0f,0.9f); }
          if(g_manual&&g_lockTgt>=0){ char lk[96];
              snprintf(lk,sizeof lk,"LOCK-ON: %s",packs[slots[g_lockTgt].pack].label);
              drawText(60,340,1.8f,lk,1.0f,0.25f,0.95f,0.95f); }
          /* ⛔ THE DANCE FLOOR'S OWN CREDIT DOES NOT BELONG ON A STREET. The dancer spotlight
           * prints a pack label and artist over the frame, and it fired straight through
           * a whole act -- a pack's label sitting in the bottom-left of a suburban morning.
           * Exactly the lesson already learned for the lamp sprites, orbs and flame snake:
           * A SET OWNS THE SCREEN, including the dance floor's own decoration. Those were gated on
           * a scene and then on a set; this credit was missed because it is drawn with the
           * HUD text rather than with the dance floor's props. Same gate as the mode hint above. */
          if(CAMD.shot==9&&g_scnT<0.f&&!g_setNI&&!g_setSpace){ Pack*sp=&packs[slots[CAMD.tgt].pack];
              g_showH=sp->effH;
              if(tv9){ char a9[TV_COLS+1]; snprintf(a9,sizeof a9,"%s",sp->label); tvText(0,18,1,a9,1.0f,0.92f,1.0f);
                  if(sp->artist[0]){ snprintf(a9,sizeof a9,"by %s",sp->artist); tvText(0,19,1,a9,0.5f,0.95f,0.85f); } }
              else { drawText(60,362,2.4f,sp->label,1.0f,0.92f,1.0f,0.98f);   /* raised clear of the musinfo block */
              if(sp->artist[0]){ char a2[96]; snprintf(a2,sizeof a2,"by %s",sp->artist);
                  drawText(60,386,1.6f,a2,0.5f,0.95f,0.85f,0.95f); } } }
          if(g_tvWords&&g_mode==MODE_ANIM) tvWordsPage();   /* (step 26) the lab's `words=1`: a page of every script */
          netHud();
          tvFlush();   /* at a bigger reading size, the frame's text goes down now that it's all known */
}
        passMark("hud");
        { float sv9[4]={g_rvx,g_rvz,g_rvHead,g_rvAlt}; g_shDumpTag="hero"; simHashMix(sv9,4);
          if(g_spOn){ float f9[2]={g_feet.y,g_feet.vy}; g_shDumpTag="feet"; simHashMix(f9,2); }
          objHash();
          rideHash();
          liqHash();
          agHash(NS);
          for(int i9=0;i9<NS;i9++){ float sl9[6]={slots[i9].x,slots[i9].z,slots[i9].rotY,(float)slots[i9].state,(float)slots[i9].clip,(float)slots[i9].phase}; char t9[24]; snprintf(t9,sizeof t9,"slot%d",i9); g_shDumpTag=t9; simHashMix(sl9,6); }
          if(GAME_ON&&g_game->state&&g_game->stateSize){ g_shDumpTag="game"; simHashBytes(g_game->state,g_game->stateSize); }   /* what a game says is its state is part of the receipt */
          g_shDumpTag="";
          netTickDone();   /* (step 29) the tick's hash is whole: held against the other machines' */
          if(g_shDumpF&&g_tickTotal>=g_shDumpTick){ fclose(g_shDumpF); g_shDumpF=NULL; }
          if(g_shEvery>0&&(g_frameNo%(unsigned long)g_shEvery)==0) simHashPrint("every");
#ifdef REVA_ALLOCCOUNT
          { extern long g_allocN,g_allocBytes,g_allocTick,g_allocFrom; g_allocTick=g_tickTotal; if(g_allocFrom<0&&getenv("REVA_ALLOCWHO")) g_allocFrom=atol(getenv("REVA_ALLOCWHO"));
            if(g_shEvery>0&&(g_frameNo%(unsigned long)g_shEvery)==0) printf("[alloc] tick %ld calls %ld bytes %ld\n",g_tickTotal,g_allocN,g_allocBytes); }
#endif
          /* REVA_EXITTICK=<tick> (11-02): a check ends itself here, with no timer and no kill */
          { static long ex9=-2; if(ex9==-2){ const char*e=getenv("REVA_EXITTICK"); ex9=e?atol(e):-1; }
            if(ex9>0&&g_tickTotal>=ex9){ simHashPrint("end"); fflush(stdout); g_run=0; } } }
        tickAcc9+=g_tickN;
        if(g_noDraw) continue;   /* a pass that only ticks: no finish, no shot, no swap or flip. Window events and the stat line wait for the pass that draws */
        { GfxView v9; memset(&v9,0,sizeof v9); memcpy(v9.vp,vp,64); memcpy(v9.sunVP,g_sunVP,64); memcpy(v9.eye,g_camEye,12); memcpy(v9.sunDir,g_sun,12); v9.sunI=g_sun[3];
          memcpy(v9.sunCol,g_sunC,12); memcpy(v9.sky,g_sky,12); memcpy(v9.fog,g_fog,12); v9.fogD=g_fog[3]; skyHorizonC(v9.clear); v9.knee=g_tone>0.f?g_tone:g_postKnee; v9.shadowOn=g_shadowOn; v9.tsec=(float)tsec; v9.exposure=g_exposure; if(g_tod>=0.f) v9.dayExposure=1.f/(0.4f+0.6f*(g_sun[3]<1.f?g_sun[3]:1.f));   /* the day's: a night's weak sun is opened up by a fixed curve, never by measuring the picture */
          if(SET_NONE&&!g_setSpace){ if(g_mode==MODE_ANIM){ v9.clear[0]=0.055f; v9.clear[1]=0.058f; v9.clear[2]=0.070f; } else { v9.clear[0]=0.006f; v9.clear[1]=0.003f; v9.clear[2]=0.014f; } }   /* a dark room, or the lab's grey: no sky to match */
          if(WORLD_OFF) memcpy(v9.clear,g_flatClear,12);
          clearOverride(v9.clear);   /* REVA_CLEAR=r,g,b: the impossible backdrop, for one test */
          const int FW9=g_fairNow>0.f?(((int)((float)H*g_fairNow+0.5f))&~1):W, FX9=(W-FW9)/2;   /* (step 25) the fair view's place in the picture: all of it, when there's no rule */
          if(g_fairNow>0.f){ v9.bars=1; if(g_gameViews.n<=1){ v9.nViews=1; memcpy(v9.views[0].vp,vp,64); memcpy(v9.views[0].eye,g_camEye,12); v9.views[0].x=FX9; v9.views[0].y=0; v9.views[0].w=FW9; v9.views[0].h=H; } }
          if(g_gameViews.n>1){ int n9=g_gameViews.n,cols9=n9>2?2:1,w9=FW9/cols9,h9=H/2; v9.nViews=n9;
              for(int i9=0;i9<n9;i9++){ M4 pj9,vw9,vp9; const float*e9=g_gameViews.e[i9],*a9=g_gameViews.a[i9];
                  m4_persp(pj9,g_tune.lensFov>0.f?g_tune.lensFov:42.f,VIEW_ASPECT(W,H)*((float)w9/(float)FW9)/((float)h9/(float)H),0.8f,fmaxf(520.f,g_setFar));
                  m4_lookat(vw9,e9[0],e9[1],e9[2],a9[0],a9[1],a9[2]); m4_mul(vp9,pj9,vw9);
                  memcpy(v9.views[i9].vp,vp9,64); memcpy(v9.views[i9].eye,e9,12); v9.views[i9].x=FX9+(i9%cols9)*w9; v9.views[i9].y=(i9/cols9)*h9; v9.views[i9].w=w9; v9.views[i9].h=h9; } }
          /* the lab's wall: a lens to a panel, and the bodies alone in each */
          if(g_xrDraw){ v9.nViews=2; v9.bars=0; for(int i9=0;i9<2;i9++){ memcpy(v9.views[i9].vp,g_xrVP[i9],64); memcpy(v9.views[i9].eye,g_xrEyeAt[i9],12); v9.views[i9].x=i9*(W/2); v9.views[i9].y=0; v9.views[i9].w=W/2; v9.views[i9].h=H; } }   /* (11-11) the two eyes, side by side */
          if(g_wallV.n>1){ v9.nViews=g_wallV.n; v9.wall=1; for(int i9=0;i9<g_wallV.n;i9++){ memcpy(v9.views[i9].vp,g_wallV.vp[i9],64); memcpy(v9.views[i9].eye,g_wallV.eye[i9],12); v9.views[i9].x=g_wallV.x[i9]; v9.views[i9].y=g_wallV.y[i9]; v9.views[i9].w=g_wallV.w[i9]; v9.views[i9].h=g_wallV.h[i9]; } g_wallV.n=0; v9.clear[0]=0.055f; v9.clear[1]=0.058f; v9.clear[2]=0.070f; }
          /* the dance floor's coloured movers, which the old road lit the crowd with */
          if(!GAME_ON&&!WORLD_OFF&&g_mode!=MODE_ANIM&&!g_setNI) for(int k9=0;k9<nLite;k9++) frameLightAdd(liteP[k9][0],liteP[k9][1],liteP[k9][2],liteC[k9][0],liteC[k9][1],liteC[k9][2],0.055f,0.f,0.f,0.f,0.f);
          v9.skyDome=(g_mode!=MODE_ANIM&&(g_setNI||g_nPipe)&&!g_setSpace&&!WORLD_OFF);
          { static float sp9[72]; v9.space=NULL; if(g_setSpace&&!WORLD_OFF){ spaceFill(sp9,H); v9.space=sp9; } }
          v9.tunnel=g_xrDraw?g_xrTunnelAmt:0.f;   /* (11-11) a headset's comfort tunnel, as the game asks */
          v9.focus=g_lensNow.focus; v9.dof=g_lensNow.focus>0.f?g_lensNow.dof:0.f; v9.shutter=g_lensNow.shutter; v9.sepia=g_lensNow.sepia; v9.flash=g_lensNow.flash; v9.burn=g_lensNow.burn; v9.grain=g_lensNow.grain; v9.weave[0]=g_lensNow.weave[0]; v9.weave[1]=g_lensNow.weave[1];
          if(g_lensNow.exposure>0.f) v9.exposure=g_lensNow.exposure;   /* (step 25) the timeline's, for this frame */
          v9.filmBloom=g_bloom; v9.filmPostOn=g_postOn; v9.filmTone=g_tone; v9.fish=(g_fishTh>0.f)?g_fish:0.f; v9.fishTh=(g_fishTh>0.f)?g_fishTh:0.8f; v9.feed=g_tape; v9.feedT=g_tapeT; if(g_gloss.on){ v9.gloss[0]=g_gloss.y; v9.gloss[1]=1.f/g_gloss.fade; v9.gloss[2]=g_gloss.f0; v9.gloss[3]=g_gloss.fmax; }
          { static int rd9=0; static float gr9=0.f,vg9=0.f; if(!rd9){ rd9=1; const char*e9=getenv("REVA_GRADE"); if(e9&&e9[0]) gr9=(float)atof(e9); e9=getenv("REVA_VIG"); if(e9&&e9[0]) vg9=(float)atof(e9); } v9.filmGrade=g_grade>0.f?g_grade:gr9; v9.filmVig=g_vig>0.f?g_vig:vg9; if(g_lensNow.grade>=0.f) v9.filmGrade=g_lensNow.grade; }
          /* lights beyond the sun: the set's, its spots, and what the game asked for this frame */
          { static GfxLight L9[GFX_MAX_LIGHTS]; int n9=0;
            { float lp9[NLIGHT*3],lc9[NLIGHT*3]; memcpy(lp9,g_setLp,sizeof lp9); memcpy(lc9,g_setLc,sizeof lc9); for(int k9=g_setNL*3;k9<NLIGHT*3;k9++) lc9[k9]=0.f;
              if(g_lampScale!=1.f) for(int k9=0;k9<g_setNL*3&&k9<NLIGHT*3;k9++) lc9[k9]*=g_lampScale;   /* a scene's `lamps=` */
              lightsStamp(lp9,lc9,g_setNL); for(int k9=0;k9<g_setNL&&k9<NLIGHT;k9++) g_stampK[k9]=g_setLk[k9];   /* and a light in someone's hand, in the top slots */
              for(int k9=0;k9<NLIGHT&&n9<GFX_MAX_LIGHTS;k9++){ if(lc9[k9*3]+lc9[k9*3+1]+lc9[k9*3+2]<=0.f) continue; GfxLight*L=&L9[n9++]; memset(L,0,sizeof *L); memcpy(L->pos,&lp9[k9*3],12); memcpy(L->col,&lc9[k9*3],12); L->falloff=0.055f+g_stampK[k9]; if(k9<g_setNL) L->reach=g_setLr[k9]; }
              for(int k9=0;k9<g_setNLX&&n9<GFX_MAX_LIGHTS;k9++){ GfxLight*L=&L9[n9++]; memset(L,0,sizeof *L); memcpy(L->pos,&g_setLpX[k9*3],12);   /* (11-12) the set's lights past the practicals */
                  for(int c9=0;c9<3;c9++){ L->col[c9]=g_setLcX[k9*3+c9]*g_lampScale; } L->falloff=0.055f+g_setLkX[k9]; L->reach=g_setLrX[k9]; } }
            for(int k9=0;k9<g_setNSpot&&n9<GFX_MAX_LIGHTS;k9++){ L9[n9]=g_setSpot[k9];   /* (11-11) a TRK5 timeline colours the set's spots, frame by frame */
                if(g_stk&&k9<g_stkNS) for(int c9=0;c9<3;c9++) L9[n9].col[c9]*=g_stkSpot[k9][c9];
                n9++; }
            if(g_gfxBeamLights>0&&g_beamLight>0.f&&g_mode==MODE_ANIM) for(int k9=0;k9<g_nBeam&&n9<GFX_MAX_LIGHTS;k9++){   /* (step 24) a beam is a light too */
                const float len9=g_beamL[k9], wide9=fmaxf(g_beamR[k9][0],g_beamR[k9][1]), pw9=g_beamK[k9]*g_beamLight; if(len9<=1e-3f||pw9<=0.f) continue;
                float deg9=atan2f(wide9,len9)*57.29578f*1.2f; if(deg9<5.f) deg9=5.f; if(deg9>70.f) deg9=70.f;
                GfxLight*B9=&L9[n9++]; lightFill(B9,g_beamA[k9][0],g_beamA[k9][1],g_beamA[k9][2],g_beamC[k9][0]*pw9,g_beamC[k9][1]*pw9,g_beamC[k9][2]*pw9,0.055f,g_beamU[k9][0],g_beamU[k9][1],g_beamU[k9][2],deg9);
                B9->shadow=(g_gfxBeamLights>1); }
            for(int k9=0;k9<g_frameNL&&n9<GFX_MAX_LIGHTS;k9++) L9[n9++]=g_frameLight[k9];
            g_frameNL=0; v9.lights=L9; v9.nLights=n9; memcpy(v9.ground,g_ground,12); v9.haze=g_lensNow.haze>=0.f?g_lensNow.haze:g_haze; }
          /* the lens jumped: more in one tick than anything travels. The first frame carries nothing over. */
          { static int first9=1; static float le9[3]; static unsigned cuts9=0; float dx9=g_camEye[0]-le9[0],dy9=g_camEye[1]-le9[1],dz9=g_camEye[2]-le9[2];
            if(!first9&&dx9*dx9+dy9*dy9+dz9*dz9>36.f) cuts9++;
            memcpy(le9,g_camEye,12); v9.cuts=cuts9; v9.reset=first9; first9=0; v9.tick=g_tickTotal; }
          statsDraw(); ANDROID_TOUCHDRAW gfxFrameEnd(&v9); statsFrame(); }   /* make the GPU actually finish so the number means something */
        bridgeFrameDone(W,H);
#ifdef REVA_VK
        frameT0+=gfxWaited();   /* on the direct road the wait for the last flip happens in gfxFrameEnd: work is the CPU's own */
#endif
        { double w9=wall_ms()-frameT0; workAcc+=w9; workN++; if(w9>g_ledWorkMax) g_ledWorkMax=w9; }
#ifdef REVA_SDL
        /* HEADLESS FRAME DUMP: REVA_SHOT=<frame> writes that presented frame to
         * REVA_SHOT_PATH (default /tmp/reva-shot.ppm) and exits, so a change can be
         * judged on pixels without taking over the desktop. Off unless asked for. */
        { static long shotN=-2; static const char*shotPath=0; static long fno=0;
          if(shotN==-2){ const char*e=getenv("REVA_SHOT"); shotN=e?atol(e):-1;
                         shotPath=getenv("REVA_SHOT_PATH"); if(!shotPath) shotPath="/tmp/reva-shot.ppm"; }
          /* THE BURST INDEX IS FIVE DIGITS. It was three, which is fine for the dozen
           * frames a filmstrip wants and silently wrong for a scene: a 24fps take of a
           * 88-second conversation is 2112 files, f_1000 sorts BEFORE f_999, and an
           * ffmpeg %03d pattern stops dead at 999. Fixed width sorts and globs. */
          /* REVA_SHOTN captures a RUN of frames, not one. Animation cannot be judged from a
           * still -- timing, weight, foot-slide and pops only exist between frames. */
          static int burst=-1, taken=0, every=1;
          if(burst<0){ const char*e=getenv("REVA_SHOTN"); burst=e?atoi(e):1;
                       const char*e2=getenv("REVA_SHOTEVERY"); every=e2?atoi(e2):1;
                       if(burst<1) burst=1;
                       if(every<1) every=1; }
          int SW9=W,SH9=H; gfxShotSize(&SW9,&SH9);   /* (step 25) a shot is the glass's size, which is the picture's unless a presenter's between them */
          if(g_shotNow[0]){   /* LIVE SHOT: write it, keep running */
              unsigned char*px=(unsigned char*)malloc((size_t)SW9*SH9*4);
              if(!gfxRead(px,SW9,SH9)) memset(px,0,(size_t)SW9*(size_t)SH9*4);
              FILE*sf=fopen(g_shotNow,"wb");
              if(sf){ fprintf(sf,"P6\n%d %d\n255\n",SW9,SH9);
                  unsigned char*rw=(unsigned char*)malloc((size_t)SW9*3);
                  for(int y=SH9-1;y>=0;y--){ for(int x=0;x<SW9;x++){ rw[x*3]=px[((size_t)y*SW9+x)*4];
                      rw[x*3+1]=px[((size_t)y*SW9+x)*4+1]; rw[x*3+2]=px[((size_t)y*SW9+x)*4+2]; }
                      fwrite(rw,1,(size_t)SW9*3,sf); }
                  free(rw); fclose(sf);
                  printf("[lab] shot -> %s\n",g_shotNow); fflush(stdout); }
              else printf("[lab] shot FAILED to open %s (a native exe needs a Windows path)\n",g_shotNow);
              free(px); fflush(stdout); g_shotNow[0]=0; }
          /* A FIXED-STEP SCENE CAPTURE RECORDS FROM THE SCENE'S FIRST FRAME, not from a
           * frame number guessed against an 11-second boot. Frame N is then scene second
           * N/REVA_SCENEFPS exactly, which is what lets the wav be muxed in blind. */
          int capArm=(getenv("REVA_SCENEFPS")&&g_scnT>=0.f);
          ++fno;
          /* ⭐ (11-12) REVA_SHOTLAG=1: A SCENE CAPTURE A FRAME LATE. Every captured frame used to wait for the GPU to finish it before the CPU began
           * the next (gfxRead's idle wait: a music video chorus's draft, 166 ms of GPU and 35 of CPU a frame, one after the other). Here frame k is written at
           * frame k+1 (gfxReadLag waits only for it, which the GPU has drawn while the CPU built k+1), the last read whole at the end, and one left
           * over when the scene ends written then. Frame k's pixels still go to file k.
           * Measured losslessly (driver.py scene --lossless, 11-12): the same frames as the serialized capture but for a few single pixels at hair tips
           * (the GPU's hair lands a strand a pixel apart when frames overlap), the sim's hashes the same tick for tick. ⛔ A lossy A/B said "drifts from
           * frame 32": one frame's difference runs through every frame coded after it. a take script's drafts use it; a final is serialized */
          static int lag9=-1; static int lagK9=0; if(lag9<0){ const char*e9=getenv("REVA_SHOTLAG"); lag9=(e9&&e9[0]=='1'); }
          if(lag9&&shotPath&&burst>1&&capArm&&taken<burst){
              unsigned char*px=(unsigned char*)malloc((size_t)SW9*SH9*4);
              if(lagK9>taken){ if(!gfxReadLag(px,SW9,SH9)){ memset(px,0,(size_t)SW9*(size_t)SH9*4); } shotWritePPM(shotPath,burst,taken,SW9,SH9,px,fno-1); taken++; }
              lagK9++;
              if(taken==burst-1){ if(!gfxRead(px,SW9,SH9)){ memset(px,0,(size_t)SW9*(size_t)SH9*4); } shotWritePPM(shotPath,burst,taken,SW9,SH9,px,fno); taken++; }
              free(px); fflush(stdout); g_capBegun=1; if(taken>=burst) exit(0); }
          else if(lag9&&shotPath&&burst>1&&!capArm&&lagK9>taken){   /* the scene ended with its last captured frame not yet written */
              unsigned char*px=(unsigned char*)malloc((size_t)SW9*SH9*4);
              if(!gfxReadLag(px,SW9,SH9)){ memset(px,0,(size_t)SW9*(size_t)SH9*4); } shotWritePPM(shotPath,burst,taken,SW9,SH9,px,fno-1); taken++; free(px); fflush(stdout); }
          else if(((capArm&&shotPath) || (shotN>=0 && fno>=shotN && ((fno-shotN)%every)==0)) && taken<burst){
              if(!capArm) frameCostPrint();
              static int shotT9=-1; static double st9[6], lastT9=0.0; static int stN9=0; if(shotT9<0){ const char*e9=getenv("REVA_SHOTTIME"); shotT9=(e9&&e9[0]=='1'); }
              const double c0=wall_ms(); if(shotT9) gfxDrain(); const double c1=wall_ms();   /* (11-12) REVA_SHOTTIME: the GPU's own finish, timed apart */
              unsigned char*px=(unsigned char*)malloc((size_t)SW9*SH9*4);
              if(!gfxRead(px,SW9,SH9)) memset(px,0,(size_t)SW9*(size_t)SH9*4);
              const double c2=wall_ms();
              char pth[512];
              if(burst>1) snprintf(pth,sizeof pth,"%.*s_%05d.ppm",(int)(strrchr(shotPath,46)?(size_t)(strrchr(shotPath,46)-shotPath):strlen(shotPath)),shotPath,taken);
              else snprintf(pth,sizeof pth,"%s",shotPath);
              FILE*sf=fopen(pth,"wb");
              if(sf){ fprintf(sf,"P6\n%d %d\n255\n",SW9,SH9);
                      { unsigned char*rw=(unsigned char*)malloc((size_t)SW9*3); for(int y=SH9-1;y>=0;y--){ for(int x=0;x<SW9;x++){ rw[x*3]=px[((size_t)y*SW9+x)*4]; rw[x*3+1]=px[((size_t)y*SW9+x)*4+1]; rw[x*3+2]=px[((size_t)y*SW9+x)*4+2]; } fwrite(rw,1,(size_t)SW9*3,sf); } free(rw); }   /* GL origin is bottom-left */
                      fclose(sf); const double c3=wall_ms(); printf("shot: %s %dx%d frame %ld\n",pth,SW9,SH9,fno); simHashPrint("shot");
                      if(shotT9){ const double c4=wall_ms(); st9[0]+=workAcc>0.0&&workN>0?workAcc/workN:0.0; st9[1]+=c1-c0; st9[2]+=c2-c1; st9[3]+=c3-c2; st9[4]+=c4-c3; st9[5]+=lastT9>0.0?c0-lastT9:0.0; lastT9=c0;
                          if(++stN9==30){ printf("[shottime] 30 shots, a shot's ms: GPU finish %.1f, readback %.1f, PPM write %.1f, hash and log %.1f; shot to shot %.1f\n",
                                              st9[1]/30,st9[2]/30,st9[3]/30,st9[4]/30,st9[5]/30); fflush(stdout); stN9=0; memset(st9,0,sizeof st9); } } }
              free(px); fflush(stdout); g_capBegun=1; if(++taken>=burst) exit(0); } }
        clipAuditFrame();   /* ⭐ every body drawn this frame, tested together */
        ledgerMark(wall_ms());
        { static int rs9=-1, rw9, rh9, rf9;   /* (11-12) REVA_RESIZETEST=WxH@frame: the window made that size at that frame, for a check of the rebuild with no hands */
          if(rs9<0){ const char*e=getenv("REVA_RESIZETEST"); rs9=e&&sscanf(e,"%dx%d@%d",&rw9,&rh9,&rf9)==3; }
          if(rs9==1&&frames>=rf9){ SDL_SetWindowSize(g_win,rw9,rh9); rs9=2; printf("[window] made %dx%d (REVA_RESIZETEST)\n",rw9,rh9); fflush(stdout); } }
        { SDL_Event ev; while(SDL_PollEvent(&ev)){
            if(ev.type==SDL_QUIT) g_run=0;
            /* (11-12) FULLSCREEN IS BASIC. Alt+Enter or F11 is fullscreen and back, on any build with a
             * window; the renderer is made again at whatever size the window comes to (gfxResize), and W and H with it, which the HUD's grid, the 2D
             * canvas and a game's fonts follow */
            if(ev.type==SDL_KEYDOWN&&!ev.key.repeat&&!g_noWindow&&(ev.key.keysym.sym==SDLK_F11||(ev.key.keysym.sym==SDLK_RETURN&&(ev.key.keysym.mod&KMOD_ALT)))){
                const int fs=(SDL_GetWindowFlags(g_win)&SDL_WINDOW_FULLSCREEN)!=0;
                if(!fs) SDL_SetWindowResizable(g_win,SDL_TRUE);
                SDL_SetWindowFullscreen(g_win,fs?0:SDL_WINDOW_FULLSCREEN_DESKTOP);
                printf("[window] %s\n",fs?"a window":"fullscreen"); fflush(stdout); }
            if(ev.type==SDL_WINDOWEVENT&&(ev.window.event==SDL_WINDOWEVENT_SIZE_CHANGED||ev.window.event==SDL_WINDOWEVENT_RESIZED)){
                int nw=W,nh=H; if(gfxResize(&nw,&nh)){ W=nw; H=nh; } }
            /* ESC is the start menu now (the keyboard pass): QUIT lives inside it, and
             * the window's own close box still ends the show. */
            if(ev.type==SDL_KEYDOWN&&ev.key.keysym.sym==SDLK_ESCAPE&&!ev.key.repeat) g_startEdge=1;
            /* ⭐ THE MARK EDITOR. A person places the marks more easily than a measurement guesses them.
             * 1-4 choose which mark, a left click puts it at that point on her,
             * and the file is written on every click so there is nothing to save. */
            if(g_markEdit&&ev.type==SDL_KEYDOWN){
                int k=ev.key.keysym.sym;
                if(!ev.key.repeat&&k>=SDLK_1&&k<=SDLK_4){ g_markSel=k-SDLK_1;
                    printf("[mark] selected %s%c",MARKNM[g_markSel],10); fflush(stdout); }
                (void)0; }
            if(g_markEdit&&ev.type==SDL_MOUSEBUTTONDOWN&&ev.button.button==SDL_BUTTON_LEFT
               &&g_markP){
                float dir[3], hit[3];
                float fov9=(g_camOn&&g_camFov>0.f)?g_camFov:52.f;
                markRay((float)ev.button.x,(float)ev.button.y,W,H,g_lastE,g_lastC,fov9,dir);
                if(markPick(g_markP,g_markW,g_markM,g_lastE,dir,hit)){
                    memcpy(g_markPt[g_markSel],hit,12); g_markGot[g_markSel]=1;
                    printf("[mark] %s <- rig %.4f %.4f %.4f%c",
                           MARKNM[g_markSel],hit[0],hit[1],hit[2],10); fflush(stdout);
                    markWrite(getenv("REVA_ANIMSUBJ")); }
                else { printf("[mark] miss -- click ON her%c",10); fflush(stdout); } } } }
        (void)first;
#else
        clipAuditFrame();
#endif
        frames++;
        if(wall_ms()-lastStat>5000){ if(g_dieN||g_hideN) printf("[ledger] die=%ld hide=%ld inflight=%ld%s\n",g_dieN,g_hideN,g_dieN-g_hideN,(g_dieN-g_hideN>3)?"  ZOMBIE ALARM":"");
            /* a game can have no bodies at all: 42%0 traps on x86 and quietly gives 0 on ARM */
            static const Slot noBody9; const Slot*sA9=NS>0?&slots[0]:&noBody9,*sB9=NS>0?&slots[42%NS]:&noBody9,*sC9=(CAMD.tgt>=0&&CAMD.tgt<NS)?&slots[CAMD.tgt]:sA9;
            printf("fps %.1f work %.2fms %dx%d | s0 %.1f,%.1f st%d sp%.2f | s42 %.1f,%.1f st%d sp%.2f | rv %.1f,%.1f tgt%d %.1f,%.1f\n",
            frames*1000.0/(wall_ms()-lastStat), workN?workAcc/workN:0.0, W,H, sA9->x,sA9->z,sA9->state,sA9->spd,
            sB9->x,sB9->z,sB9->state,sB9->spd,
            g_rvx,g_rvz,CAMD.tgt,sC9->x,sC9->z); printf("[tick] %.2f ticks/frame (%ld ticks, %.0f Hz)\n",frames?(double)tickAcc9/frames:0.0,g_tickTotal,1000.0/g_tickMs); ledgerPrint(); fflush(stdout); passPrint(frames); frames=0; workAcc=0; workN=0; tickAcc9=0; lastStat=wall_ms(); fflush(stdout); }
    }
    clipAuditExit();   /* ⭐ a scene cut off by the exit still gets its TOTAL */
    printf("exiting clean\n");
    return 0;
}
