/* engine/net.inc -- ONLINE PLAY, the engine's side (queue step 29, 11-05). club_net.c is the wire, and its first comment says what a session is: every machine
 * works out the same world from the same dice and the same pads, tick by tick, and all that crosses the wire is pads.
 *
 * REVA_NET=host[:port[:pads]] or REVA_NET=join:address[:port] starts a run as a session (a game asks for the same with E->netHost and E->netJoin, which start the
 * engine again in one). REVA_NETLOCAL=<n> is how many of the session's pads are this machine's (1), REVA_NETWORD=<word> keeps a stranger out, REVA_NETDELAY=<ticks>
 * is the host's own choice of delay, REVA_NETTIMEOUT=<seconds> is how long a silent machine is waited for (10).
 *
 * A session is a whole run, from tick 0. Four moments:
 *   netBoot    the game's known and nothing's loaded: the players gather, on a screen of the engine's own. Then the host's dice and the host's saves are everyone's.
 *   netStart   everything's loaded: each machine says what it loaded and where its players have their buttons, the host says go, and every clock starts at 0.
 *   netPoll    the top of every pass: has this machine every pad for the next tick? If not it waits here, and the picture holds. That's all a slow wire ever does.
 *   netInput   after the polls: this machine's pads go out, to be played `delay` ticks on, and every pad for THIS tick comes in over the globals and g_padRaw, exactly
 *              where a tape puts them. So nothing downstream knows a session from a tape, and a session recorded with REVA_RECORD is an ordinary tape of four pads.
 * ⛔ WHAT'S DRAWN MAY DIFFER BETWEEN MACHINES, AND THE RULES MAY NOT. E->netHere(player) says which pads are this machine's, for a game to choose its lens or its
 * split by: ask it in draw and hud, never in tick. Every tick's hash of the rules is held against the other machines' as it runs, and a game that parts is told the tick.
 * ⛔ The engine's own pause menu is off in a session (its screens are one machine's), a session's music clock is the tick's and not the sound card's, and a machine's
 * own saves aren't read: the host's are. */
static int g_netDelay=0, g_netLocalKb=0, g_netLocalFpv=0;
static unsigned long long g_netSig=0; static unsigned g_netSigN=0;
static double g_netNoteAt=0.0,g_netTickWall=0.0; static char g_netNote[96];

/* what this machine has loaded, as one number: each pack and set by its name, its length and its bytes, added up so the order doesn't matter. The host holds every
 * machine's against its own before it says go, so two versions of a game are told so at the start and not found out by a hash a minute in */
static void netSigAdd(const char*name,const void*bytes,size_t len){
    if(!g_netOn||!name) return;
    const char*b=name; for(const char*c=name;*c;c++) if(*c=='/'||*c=='\\') b=c+1;
    unsigned long long hn=1469598103934665603ull,hb=1469598103934665603ull;
    for(const char*c=b;*c;c++){ char ch=*c; if(ch>='A'&&ch<='Z') ch=(char)(ch+32); hn^=(unsigned char)ch; hn*=1099511628211ull; }
    const unsigned char*p=(const unsigned char*)bytes; for(size_t i=0;i<len;i++){ hb^=p[i]; hb*=1099511628211ull; }
    g_netSig+=(hn^(hb*0x9E3779B97F4A7C15ull))+(unsigned long long)len; g_netSigN++;
    { static int say=-1; if(say<0) say=getenv("REVA_NETSAY")?1:0; if(say){ printf("[net] loaded %s, %zu bytes, %016llx\n",b,len,hb); fflush(stdout); } } }
static void netSigFile(const char*path){ if(!g_netOn) return; FILE*f=fopen(path,"rb"); if(!f) return;
    size_t cap=1u<<16,n=0; unsigned char*d=(unsigned char*)malloc(cap);
    for(;d;){ const size_t got=fread(d+n,1,cap-n,f); n+=got; if(n<cap) break; if(cap>=(1u<<26)) break; unsigned char*e=(unsigned char*)realloc(d,cap*2); if(!e) break; d=e; cap*=2; }
    fclose(f); if(d) netSigAdd(path,d,n); free(d); }

/* while the engine waits with no game running: the window's own events, so closing it is noticed. Only that one is taken: the rest wait for the game's loop */
static void netEvents(void){
#if defined(REVA_SDL)&&!defined(REVA_HEADLESS)
    SDL_PumpEvents(); SDL_Event ev9; while(SDL_PeepEvents(&ev9,1,SDL_GETEVENT,SDL_QUIT,SDL_QUIT)>0) g_run=0;
#endif
}
/* a picture of the engine's own, for when no game is being drawn: the wait for players, the wait for everyone to load, and a long wait in the middle of a game */
static void netScreen(const char*l1,const char*l2,const char*l3,const char*l4){
#ifndef REVA_HEADLESS
    static long t9=0; t9++;
    gfxFrameBegin();
    if(l1&&l1[0]) tvTextC(8,2,l1,0.95f,0.95f,0.95f);
    if(l2&&l2[0]) tvTextC(12,1,l2,0.8f,0.8f,0.8f);
    if(l3&&l3[0]) tvTextC(14,1,l3,0.8f,0.8f,0.8f);
    if(l4&&l4[0]) tvTextC(18,1,l4,0.95f,0.9f,0.4f);
    tvFlush();
    { GfxView v9; memset(&v9,0,sizeof v9); for(int i=0;i<16;i+=5){ v9.vp[i]=1.f; v9.sunVP[i]=1.f; } v9.knee=1.f; v9.tsec=(float)t9/60.f; v9.tick=g_tickTotal; v9.reset=1; gfxFrameEnd(&v9); }
    /* REVA_NETSHOT=<file>: the waiting screen's twentieth frame as a PPM, to look at (set REVA_SHOT too, so the renderer keeps a copy to read) */
    if(t9==20){ const char*sp9=getenv("REVA_NETSHOT"); int w9=g_tvW,h9=g_tvH; if(sp9&&sp9[0]&&w9>0&&h9>0){ gfxShotSize(&w9,&h9); unsigned char*px9=(unsigned char*)malloc((size_t)w9*h9*4); FILE*f9=px9?fopen(sp9,"wb"):NULL;
        if(f9){ if(!gfxRead(px9,w9,h9)) memset(px9,0,(size_t)w9*(size_t)h9*4); fprintf(f9,"P6\n%d %d\n255\n",w9,h9); for(int y=h9-1;y>=0;y--) for(int x=0;x<w9;x++) fwrite(px9+((size_t)y*w9+x)*4,1,3,f9); fclose(f9); printf("[net] the waiting screen: %s\n",sp9); fflush(stdout); }
        free(px9); } }
    netEvents();
#else
    (void)l1; (void)l2; (void)l3; (void)l4;
#endif
}
static void netNap(void){
#ifdef REVA_HEADLESS
    SDL_Delay(1);
#else
    SDL_Delay(g_fixedStepMs>0.0?1:8);   /* a window's present doesn't pace; the direct road's flip does, and this is short beside it */
#endif
}
static void netAtExit(void){ if(g_netOn){ if(netState()>=NET_PLAY){ char s[256]; netStats(s,sizeof s); printf("[net] %s\n",s); fflush(stdout); } netClose(); } }
/* it can't go on. A run a game started (E->netHost, E->netJoin) goes back to the game alone, with the reason for E->netWhy; a run started by hand says why and ends */
static void netEnd(const char*title,const char*why){
    printf("[net] %s: %s\n",title,why?why:""); fflush(stdout);
    if(getenv("REVA_NETBACK")) apiNetRelaunch(NULL,NULL,why);
#ifndef REVA_HEADLESS
    { char w[TV_COLS+1]; snprintf(w,sizeof w,"%.35s",why?why:""); for(char*c=w;*c;c++) if(*c>='a'&&*c<='z') *c=(char)(*c-32);
      for(int i=0;i<240&&g_run;i++){ netScreen(title,w,NULL,NULL); netNap(); if(g_fixedStepMs>0.0&&i>20) break; } }
#endif
    netClose(); g_netOn=0; exit(1); }

/* THE PLAYERS GATHER: called once the game's known and before anything of it is loaded */
static void netBoot(void){
    const char*spec=getenv("REVA_NET"); if(!spec||!spec[0]) return;
    char why[200]=""; int nl=1; { const char*e=getenv("REVA_NETLOCAL"); if(e&&atoi(e)>0) nl=atoi(e); }
    if(!netOpen(spec,g_game->name,nl,getenv("REVA_NETWORD"),why,sizeof why)) netEnd("CAN'T GO ONLINE",why);
    g_netOn=1; atexit(netAtExit);
    if(netIsHost()) slotNetOffer(g_game->name,netSaveOffer);
    char mine[3][24]; const int nm=netIsHost()?netMyAddresses(mine,3):0;
    if(nm>0){ printf("[net] the others join this machine at %s%s%s\n",mine[0],nm>1?" or ":"",nm>1?mine[1]:""); fflush(stdout); }
    int pa=1,pb=1,begun=0; unsigned seed=g_tapeSeedUsed; const double t0=wall_ms();
    for(;;){
        netPump();
        if(netState()==NET_OVER) netEnd("NOT JOINED",netWhy());
        int have=0,want=0,seat=0; netLobby(&have,&want,&seat);
        jsPoll((wall_ms()-t0)*0.001); kbPoll((wall_ms()-t0)*0.001);
        const int a=(g_btnA||g_btnStart)&&!pa, b=g_btnB&&!pb; pa=g_btnA||g_btnStart; pb=g_btnB;
        if(b||!g_run){ printf("[net] given up before it started\n"); fflush(stdout); if(getenv("REVA_NETBACK")) apiNetRelaunch(NULL,NULL,""); exit(0); }
        char l2[64]="",l3[64]="",l4[64]="";
        if(netIsHost()){
            if(!begun&&(have>=want||(a&&have>netLocalSeats()))){ begun=1; netHostBegin(seed); }
            if(begun&&netSettled()) break;
            snprintf(l2,sizeof l2,"%d OF %d PADS ARE HERE   PORT %d",have,want,netPort());
            /* this machine's addresses, for telling the others: the first two, because a machine with a second network on it can't know which one they're on */
            if(nm>1) snprintf(l3,sizeof l3,"%s OR %s",mine[0],mine[1]); else if(nm>0) snprintf(l3,sizeof l3,"%s",mine[0]);
            snprintf(l4,sizeof l4,"%s",have>netLocalSeats()?"A START NOW   B GIVE UP":"B GIVE UP");
            netScreen(begun?"STARTING":"WAITING FOR PLAYERS",l2,l3,l4); }
        else {
            if(netBegun(&seed)) break;
            if(seat<0) snprintf(l2,sizeof l2,"%.40s  PORT %d",netHostName(),netPort()); else snprintf(l2,sizeof l2,"YOU ARE PLAYER %d",seat+1);
            if(want>0) snprintf(l3,sizeof l3,"%d OF %d PADS ARE HERE",have,want);
            netScreen(seat<0?"LOOKING FOR THE HOST":"WAITING FOR THE HOST",l2,l3,"B GIVE UP"); }
        netNap(); }
    netBegun(&seed);
    g_tapeSeedUsed=seed; srand(seed);   /* the session's dice, before the game rolls any */
    slotNetBegin(netIsHost()); for(int i=0;i<netSaveCount();i++){ int n=0; const char*d=netSaveBytes(i,&n); slotNetPut(netSaveName(i),d,n); }
    printf("[net] the players are here: %d pads, this machine's from pad %d, the dice are %u and the saves are the host's (%d)\n",netSeats(),netFirstSeat()+1,seed,netSaveCount()); fflush(stdout); }

/* EVERYTHING'S LOADED: straight before the first tick. Every machine says what it's playing, the host says go, and every clock starts from nothing */
static void netStart(int NS){
    if(!g_netOn) return;
    { char b[256]; snprintf(b,sizeof b,"%s|api %d|state %zu|bodies %d|tick %.6f|actions %d",g_game->name,ENGINE_API_VERSION,g_game->stateSize,NS,g_tickMs,g_nAct); netSigAdd("the game",b,strlen(b));
      for(int a=0;a<g_nAct;a++) netSigAdd("an action",g_binds[a].name,strlen(g_binds[a].name)); }
    if(getenv("REVA_NETSAY")){ printf("[net] what this machine loaded adds up to %016llx, %u things\n",g_netSig,g_netSigN); fflush(stdout); }
    static unsigned masks[NET_ACTS][NET_SEATS]; memset(masks,0,sizeof masks);
    for(int a=0;a<g_nAct&&a<NET_ACTS;a++) for(int k=0;k<NET_SEATS;k++) masks[a][k]=g_binds[a].mask[k];
    netReady(g_netSig,g_netSigN,g_nAct,&masks[0][0]);
    int pb=1; const double t0=wall_ms();
    for(;;){
        netPump();
        if(netState()==NET_OVER) netEnd("IT'S OVER BEFORE IT BEGAN",netWhy());
        if(netGo(&g_netDelay,&masks[0][0])) break;
        { const double in9=netGoIn(); if(in9>=0.0&&in9<60.0){ SDL_Delay(1); continue; } }   /* the last moments before every machine starts: nothing but the clock */
        jsPoll((wall_ms()-t0)*0.001); kbPoll((wall_ms()-t0)*0.001);
        { const int b=g_btnB&&!pb; pb=g_btnB; if(b||!g_run){ printf("[net] given up before it started\n"); fflush(stdout); if(getenv("REVA_NETBACK")) apiNetRelaunch(NULL,NULL,""); exit(0); } }
        netScreen("EVERYONE'S HERE","WAITING FOR THE OTHERS TO LOAD",NULL,"B GIVE UP"); netNap(); }
    for(int a=0;a<g_nAct&&a<NET_ACTS;a++) for(int k=0;k<NET_SEATS;k++) g_binds[a].mask[k]=masks[a][k];   /* every pad's buttons are where its own player put them, on every machine */
    g_tickTotal=0; g_virtMs=0.0; g_tickAcc=0.0; g_tickOwed=0; g_frameNo=0;
    g_netLocalKb=g_kbActive; g_netLocalFpv=g_fpv; g_startEdge=0; g_tapeLate=1; g_netTickWall=wall_ms();
    printf("[net] playing: a button is played %d ticks after it's pressed, on every machine\n",g_netDelay); fflush(stdout); }

/* the top of every pass: has this machine every pad for the next tick? */
static void netPoll(void){
    if(!g_netOn) return;
    netPump();
    if(netState()!=NET_PLAY) return;
    const long T=g_tickTotal+1; if(netHave(T)) return;
    const double t0=wall_ms(); double shown=0.0;
    while(g_run&&netState()==NET_PLAY&&!netHave(T)){
        SDL_Delay(1); netPump(); netEvents();
        const double w=wall_ms()-t0;
        if(w>500.0&&w-shown>120.0){ shown=w; char l2[64]; const int s9=netWaitingOn(); if(s9>=0) snprintf(l2,sizeof l2,"WAITING FOR PLAYER %d",s9+1); else snprintf(l2,sizeof l2,"WAITING"); netScreen("HOLD ON",l2,NULL,NULL); } }
    /* how long past its time the tick is, for the wait. Waiting isn't lateness by itself: a machine with nothing to do until the next tick is due waits here as happily as in its clock.
     * Under a fixed step there's no time to be late by, so the whole wait is counted, for a test to read */
    const double ms=wall_ms()-t0, late=g_fixedStepMs>0.0?ms:(wall_ms()-(g_netTickWall+g_tickMs)); if(late>2.0) netWaited(late);
    g_ledLast=0.0;   /* a wait for the wire isn't a late frame */
    { static int say=-1; if(say<0) say=getenv("REVA_NETSAY")?1:0; if(say&&ms>20.0){ printf("[net] tick %ld waited %.0f ms for player %d\n",T,ms,netWaitingOn()+1); fflush(stdout); } } }

/* before the polls: what the keyboard's own switches were on this machine, which the last tick's merge wrote over with pad 1's */
static void netPreInput(void){ if(g_netOn&&netState()>=NET_PLAY){ g_kbActive=g_netLocalKb; g_fpv=g_netLocalFpv;
    /* ⛔ and this machine's own pads start from nothing. The polls write them fresh on a machine with hands, but a run with none (a shot, a tape that's run out, no display)
     * leaves what the merge put there, and that would go back out as this machine's own: every other player's bat following pad 1's, a few ticks late */
    g_lx=g_ly=g_rx=g_ry=g_rt=0.f; g_mdx=g_mdy=0.f; g_btnA=g_btnB=g_btnX=g_btnY=g_btnLB=g_btnRB=g_btnStart=g_btnBack=0; memset(g_padRaw,0,sizeof g_padRaw); } }
static short netQ(float x){ if(!(x==x)) x=0.f; if(x>1.f) x=1.f; if(x<-1.f) x=-1.f; return (short)lrintf(x*32767.f); }
static short netQi(float x){ if(!(x==x)) x=0.f; if(x>32767.f) x=32767.f; if(x<-32767.f) x=-32767.f; return (short)lrintf(x); }
/* after the polls, the tape and the bridge: this machine's pads go out, and every pad for this tick comes in */
static void netInput(void){
    if(!g_netOn||netState()<NET_PLAY) return;
    g_netTickWall=wall_ms();
    const long T=g_tickTotal; NetSeat loc[NET_SEATS]; memset(loc,0,sizeof loc);
    loc[0].lx=netQ(g_lx); loc[0].ly=netQ(g_ly); loc[0].rx=netQ(g_rx); loc[0].ry=netQ(g_ry); loc[0].rt=netQ(g_rt); loc[0].mdx=netQi(g_mdx); loc[0].mdy=netQi(g_mdy);
    loc[0].held=(unsigned short)((g_btnA?PAD_A:0)|(g_btnB?PAD_B:0)|(g_btnX?PAD_X:0)|(g_btnY?PAD_Y:0)|(g_btnLB?PAD_LB:0)|(g_btnRB?PAD_RB:0)|(g_btnStart?PAD_START:0));
    loc[0].flags=(unsigned char)(NETF_ON|(g_kbActive?NETF_KB:0)|(g_fpv?NETF_FPV:0)|(g_btnBack?NETF_BACK:0));
    for(int j=1;j<netLocalSeats()&&j<NET_SEATS;j++){ const PadRaw*r=&g_padRaw[j]; loc[j].lx=netQ(r->lx); loc[j].ly=netQ(r->ly); loc[j].rx=netQ(r->rx); loc[j].ry=netQ(r->ry); loc[j].rt=netQ(r->rt); loc[j].held=(unsigned short)(r->held&0x7fu); loc[j].flags=NETF_ON; }
    g_netLocalKb=g_kbActive; g_netLocalFpv=g_fpv;
    netLocal(T,loc);
    NetSeat all[NET_SEATS]; netTake(T,all);
    { const NetSeat*s=&all[0]; g_lx=(float)s->lx/32767.f; g_ly=(float)s->ly/32767.f; g_rx=(float)s->rx/32767.f; g_ry=(float)s->ry/32767.f; g_rt=(float)s->rt/32767.f; g_mdx=(float)s->mdx; g_mdy=(float)s->mdy;
      g_btnA=(s->held&PAD_A)!=0; g_btnB=(s->held&PAD_B)!=0; g_btnX=(s->held&PAD_X)!=0; g_btnY=(s->held&PAD_Y)!=0; g_btnLB=(s->held&PAD_LB)!=0; g_btnRB=(s->held&PAD_RB)!=0; g_btnStart=(s->held&PAD_START)!=0;
      g_kbActive=(s->flags&NETF_KB)!=0; g_fpv=(s->flags&NETF_FPV)!=0; g_btnBack=(s->flags&NETF_BACK)!=0; g_pad0On=(s->flags&NETF_ON)!=0; }
    for(int k=1;k<NET_SEATS;k++){ const NetSeat*s=&all[k]; PadRaw*r=&g_padRaw[k]; r->lx=(float)s->lx/32767.f; r->ly=(float)s->ly/32767.f; r->rx=(float)s->rx/32767.f; r->ry=(float)s->ry/32767.f; r->rt=(float)s->rt/32767.f; r->held=s->held; r->connected=(s->flags&NETF_ON)!=0; }
    g_startEdge=0;   /* the engine's own pause menu is one machine's: not in a session */
    g_manual=1;      /* a game is the pad's (the loop says so a few lines on): said here so that every machine's tape of the session is the same file */
    tapeRecord(); }
/* the tick's done and its hash is whole: held against the others' */
static void netTickDone(void){
    if(!g_netOn) return;
    const int was=netState(); netHash(g_tickTotal,g_shEx);
    if(was==NET_PLAY&&netState()==NET_OVER){ snprintf(g_netNote,sizeof g_netNote,"%.90s",netWhy()); for(char*c=g_netNote;*c;c++) if(*c>='a'&&*c<='z') *c=(char)(*c-32); g_netNoteAt=wall_ms(); }
    { static int say=-1; if(say<0) say=getenv("REVA_NETSAY")?1:0; if(say&&g_tickTotal%600==0){ char s[256]; netStats(s,sizeof s); printf("[net] %s\n",s); fflush(stdout); } } }
/* in the frame's text: for a few seconds after a session ends, why. The game goes on, on this machine alone */
static void netHud(void){
    if(!g_netOn) return;
    if(netState()==NET_OVER&&g_netNoteAt==0.0){ snprintf(g_netNote,sizeof g_netNote,"%.90s",netWhy()); for(char*c=g_netNote;*c;c++) if(*c>='a'&&*c<='z') *c=(char)(*c-32); g_netNoteAt=wall_ms(); }
    if(g_netNoteAt>0.0&&wall_ms()-g_netNoteAt<6000.0){ tvTextC(1,1,"ONLINE PLAY HAS ENDED",0.95f,0.9f,0.4f); char l[TV_COLS+1]; snprintf(l,sizeof l,"%.35s",g_netNote); tvTextC(2,1,l,0.8f,0.8f,0.8f); } }
