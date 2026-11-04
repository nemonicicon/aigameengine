/* ⭐ A CAPTURE IGNORES INPUT. jacob: "i messed that run up by clicking the mouse. it
 * effects the characters somehow." It does -- the mouse turns the hero and the buttons are
 * A and B, so a stray click during a take moves a body and there is NOTHING in the file
 * afterwards that says a human touched it. A frame is supposed to be a function of the
 * fixed clock and the script; input makes it a function of whatever the room was doing.
 * ⛔ Armed by REVA_SHOT or REVA_SCENEFPS, which are the two things that mean "this run is
 * being recorded" -- so an interactive session is untouched and no flag has to be
 * remembered at the moment it matters. */
static int capLocked(void){
    static int v=-1;
    if(v<0){ v=(getenv("REVA_SHOT")||getenv("REVA_SCENEFPS"))?1:0;
             if(v) printf("[cap] INPUT LOCKED for this run -- a click cannot move a body%c",10); }
    return v; }
/* ⭐ FOUR PADS (11-03, engine plan Phase 6). SDL reads them on every machine, the Pi's direct road too, since it links SDL for the mixer anyway: its table knows the layout of nearly any pad, so A is the button where A goes.
 * Pad 0 is the globals every mode reads, and the keyboard shares it. A pad that comes back takes the lowest free seat, so someone who pulled a cable gets theirs back. */
typedef struct { float lx,ly,rx,ry,rt; unsigned held; int connected; } PadRaw;   /* in the globals' own terms: lx is negated and ly is down */
static PadRaw g_padRaw[4];
static EnginePad g_pads[4];
#ifndef REVA_HEADLESS
static SDL_GameController* g_padDev[4];
static void padsOpen(void){
    for(int i=0;i<SDL_NumJoysticks();i++){ SDL_JoystickID id=SDL_JoystickGetDeviceInstanceID(i); int have=0,seat=-1;
        for(int k=3;k>=0;k--){ if(!g_padDev[k]) seat=k; else if(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_padDev[k]))==id) have=1; }
        if(have||seat<0||!SDL_IsGameController(i)) continue;
        g_padDev[seat]=SDL_GameControllerOpen(i);
        if(g_padDev[seat]){ const char*nm=SDL_GameControllerName(g_padDev[seat]); printf("[pad] %s is player %d\n",nm?nm:"a pad",seat+1); fflush(stdout); } } }
static void padRead(SDL_GameController*p,PadRaw*r){
    /* the F310 in XInput mode reports triggers as 0..32767 through this API, not the -32767..32767 of the raw joystick node */
    r->lx=-SDL_GameControllerGetAxis(p,SDL_CONTROLLER_AXIS_LEFTX)/32767.f; r->ly=SDL_GameControllerGetAxis(p,SDL_CONTROLLER_AXIS_LEFTY)/32767.f;   /* jacob: left/right was inverted */
    r->rx=SDL_GameControllerGetAxis(p,SDL_CONTROLLER_AXIS_RIGHTX)/32767.f; r->ry=SDL_GameControllerGetAxis(p,SDL_CONTROLLER_AXIS_RIGHTY)/32767.f;
    r->rt=SDL_GameControllerGetAxis(p,SDL_CONTROLLER_AXIS_TRIGGERRIGHT)/32767.f;
    /* the d-pad is the stick, wherever a stick is read, when the stick's left alone */
    if(fabsf(r->lx)<0.3f&&fabsf(r->ly)<0.3f){ if(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_DPAD_LEFT)) r->lx=1.f; else if(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) r->lx=-1.f;
        if(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_DPAD_UP)) r->ly=-1.f; else if(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_DPAD_DOWN)) r->ly=1.f; }
    r->held=(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_A)?PAD_A:0u)|(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_B)?PAD_B:0u)|(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_X)?PAD_X:0u)|(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_Y)?PAD_Y:0u)
           |(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_LEFTSHOULDER)?PAD_LB:0u)|(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)?PAD_RB:0u)|(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_START)?PAD_START:0u);
    r->connected=1; }
#endif
static void jsPoll(double tsec){
    if(capLocked()) return;
    int pad0=0;
#ifndef REVA_HEADLESS   /* no display and no hands: a run like this gets what a tape or the bridge gives it, and a bumped pad can't touch a receipt */
    { static int init=0; if(!init){ init=1;
#ifndef REVA_SDL   /* the direct road runs no event loop: it reads pads by hand, and nothing piles up in SDL's queue */
        if(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)!=0) printf("[pad] SDL can't read pads: %s\n",SDL_GetError());
        SDL_JoystickEventState(SDL_IGNORE); SDL_GameControllerEventState(SDL_IGNORE);
#endif
    } }
#ifndef REVA_SDL
    SDL_GameControllerUpdate();
#endif
    { static double lastTry=-9; if(tsec-lastTry>3||tsec<lastTry){ lastTry=tsec; padsOpen(); } }
    static int pStart[4];
    for(int k=0;k<4;k++){ SDL_GameController*p=g_padDev[k]; PadRaw*r=&g_padRaw[k];
        if(p&&!SDL_GameControllerGetAttached(p)){ printf("[pad] player %d's pad is gone\n",k+1); fflush(stdout); SDL_GameControllerClose(p); g_padDev[k]=p=NULL; if(k==0) g_manual=0; }
        if(!p){ memset(r,0,sizeof *r); pStart[k]=0; continue; }
        padRead(p,r);
        /* START = the start menu, from anyone's pad (edge consumed by the main loop) */
        int st=(r->held&PAD_START)!=0; if(st&&!pStart[k]) g_startEdge=1;
        pStart[k]=st; }
    if(g_padDev[0]){ const PadRaw*r=&g_padRaw[0]; static int pA=0,pB=0,pX=0,pBack=0; pad0=1;
        g_lx=r->lx; g_ly=r->ly; g_rx=r->rx; g_ry=r->ry; g_rt=r->rt;
        g_btnA=(r->held&PAD_A)!=0; g_btnB=(r->held&PAD_B)!=0; g_btnX=(r->held&PAD_X)!=0; g_btnY=(r->held&PAD_Y)!=0; g_btnLB=(r->held&PAD_LB)!=0; g_btnRB=(r->held&PAD_RB)!=0; g_btnStart=(r->held&PAD_START)!=0;
        { int bk=SDL_GameControllerGetButton(g_padDev[0],SDL_CONTROLLER_BUTTON_BACK); g_btnBack=bk;
          /* BACK = the old Start: hand the show back to the host */
          if(bk&&!pBack){ g_manual=0; printf("MANUAL released: the host resumes\n"); }
          pBack=bk; }
        if((g_btnA&&!pA)||(g_btnB&&!pB)||(g_btnX&&!pX)||fabsf(r->lx)>0.3f||fabsf(r->ly)>0.3f||fabsf(r->rx)>0.3f||fabsf(r->ry)>0.3f){
            if(!g_manual&&!g_pause) printf("MANUAL: jacob takes the floor\n");
            if(!g_pause){ g_manual=1; g_inT=tsec; } }
        pA=g_btnA; pB=g_btnB; pX=g_btnX; }
#endif
    /* ⛔ With no pad nothing ever cleared these: kbPoll only writes what's held, so on a machine with a keyboard and no pad, a key let go of stayed down */
    if(!pad0){ g_lx=g_ly=g_rx=g_ry=g_rt=0.f; g_btnA=g_btnB=g_btnX=g_btnY=g_btnLB=g_btnRB=g_btnStart=g_btnBack=0; }
    if(g_manual&&tsec-g_inT>75){ g_manual=0; printf("MANUAL idle: the host resumes\n"); }
}
/* what a game reads, once a tick, after the polls and the tape. Each stick goes through a round dead zone, so a stick left alone is exactly zero */
#define PAD_DEAD 0.16f
static void padStick(float*x,float*y){ float m=hypotf(*x,*y); if(m<PAD_DEAD){ *x=*y=0.f; return; }
    float s=((m>1.f?1.f:m)-PAD_DEAD)/(1.f-PAD_DEAD)/m; *x*=s; *y*=s; }
static void padsUpdate(void){
    for(int k=0;k<4;k++){ EnginePad*d=&g_pads[k]; unsigned was=d->held;
        if(k==0){ d->connected=1; d->x=-g_lx; d->y=-g_ly; d->rx=g_rx; d->ry=-g_ry; d->rt=g_rt;
            d->held=(g_btnA?PAD_A:0)|(g_btnB?PAD_B:0)|(g_btnX?PAD_X:0)|(g_btnY?PAD_Y:0)|(g_btnLB?PAD_LB:0)|(g_btnRB?PAD_RB:0)|(g_btnStart?PAD_START:0); }
        else { const PadRaw*r=&g_padRaw[k]; d->connected=r->connected; d->x=-r->lx; d->y=-r->ly; d->rx=r->rx; d->ry=-r->ry; d->rt=r->rt; d->held=r->held; }
        padStick(&d->x,&d->y); padStick(&d->rx,&d->ry);
        d->pressed=d->held&~was; } }
/* did anyone but pad 0 do anything this tick */
static int padsOthersTouched(void){ for(int k=1;k<4;k++){ const EnginePad*d=&g_pads[k]; if(d->connected&&(d->held||fabsf(d->x)>0.3f||fabsf(d->y)>0.3f)) return 1; } return 0; }
/* ---- ACTIONS: a name for what a button does, so a player can move it. A game asks for one at boot with the buttons it's on by default, and asks about it by player. save/controls.txt moves them:
 *   fire rb        fire is on RB, for everyone
 *   p2 fire a x    and on A or X for player 2
 * A tape carries the bindings it was recorded with, because the same pad on another binding is another session. */
#define ACT_MAX 32
static struct { char name[24]; unsigned mask[4]; } g_binds[ACT_MAX]; static int g_nAct=0;
static int actFind(const char*name){ for(int i=0;i<g_nAct;i++) if(!strcmp(g_binds[i].name,name)) return i; return -1; }
static int apiAction(const char*name,unsigned buttons){ int i=actFind(name); if(i>=0) return i;
    if(g_nAct>=ACT_MAX){ printf("[actions] more than %d: %s shares the first one's buttons\n",ACT_MAX,name); fflush(stdout); return 0; }
    i=g_nAct++; snprintf(g_binds[i].name,sizeof g_binds[i].name,"%s",name); for(int k=0;k<4;k++) g_binds[i].mask[k]=buttons; return i; }
static int apiActionHeld(int player,int a){ return player>=0&&player<4&&a>=0&&a<g_nAct&&(g_pads[player].held&g_binds[a].mask[player])!=0; }
static int apiActionPressed(int player,int a){ return player>=0&&player<4&&a>=0&&a<g_nAct&&(g_pads[player].pressed&g_binds[a].mask[player])!=0; }
static unsigned actButton(const char*w){ static const struct { const char*n; unsigned m; } B[]={{"a",PAD_A},{"b",PAD_B},{"x",PAD_X},{"y",PAD_Y},{"lb",PAD_LB},{"rb",PAD_RB},{"start",PAD_START}};
    for(unsigned i=0;i<sizeof B/sizeof B[0];i++) if(!strcmp(w,B[i].n)) return B[i].m;
    return 0u; }
/* once, after a game's boot. A replay takes its bindings from its tape instead */
static void actionsLoad(void){ if(getenv("REVA_NOSAVE")||getenv("REVA_REPLAY")) return;
    FILE*f=fopen("save/controls.txt","r"); if(!f) return; clubOpened("save/controls.txt"); char ln[160];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#') continue;
        char w[8][24]; int n=sscanf(ln,"%23s %23s %23s %23s %23s %23s %23s %23s",w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7]); int at=0,who=-1;
        if(n>=1&&w[0][0]=='p'&&w[0][1]>='1'&&w[0][1]<='4'&&!w[0][2]){ who=w[0][1]-'1'; at=1; }
        if(n<at+2) continue;
        int a=actFind(w[at]); if(a<0){ printf("[actions] controls.txt: this game has no action called %s\n",w[at]); continue; }
        unsigned m=0u; for(int i=at+1;i<n;i++){ unsigned b=actButton(w[i]); if(!b) printf("[actions] controls.txt: no button called %s\n",w[i]); m|=b; }
        if(m) for(int k=0;k<4;k++) if(who<0||who==k) g_binds[a].mask[k]=m; }
    fclose(f); fflush(stdout); }

/* ================= KEYBOARD + MOUSE (jacob 08-25: "i need keyboard and mouse controls
 * for all of these modes as well... avoid using the arrow keys, use wasd") ============
 * The pad is the club's native input; this speaks the SAME globals, so every mode, cab
 * and menu gets it at once with no per-mode work. Runs AFTER jsPoll and writes only what
 * is actually held, so a pad and a keyboard can be used together or one after the other.
 *
 *   WASD move           mouse look / RTS cursor      LMB or SPACE = A (act/jump/fire)
 *   RMB or E = B (bolt/back)   MMB or Q = X (nova/send)   R = Y      SHIFT = RT (lock/push)
 *   Z / C = LB / RB (grabs)    ESC = start menu           TAB = hand the show back
 *
 * Mouse look grabs the cursor while a game mode is live (released by the start menu and
 * the menus); REVA_NOMOUSEGRAB=1 keeps the cursor free for windowed work. */
static int g_kbActive=0;                 /* keyboard/mouse used: the HUD shows key hints */
static float g_mdx=0.f, g_mdy=0.f;       /* this frame's mouse delta, pixels (RTS cursor) */
/* ---- THE TAPE (2026-09-30): input record and replay -----------------------------------------
 * With the sim on whole ticks, the pad and keyboard state per frame is the only thing that makes
 * one session differ from another. REVA_RECORD=<file> writes it after the polls, one line a frame;
 * REVA_REPLAY=<file> writes it back at the same point, and the pad and keyboard are ignored. Under
 * REVA_FIXEDSTEP a replay is the recorded session exactly, and its [simhash] lines are the receipt:
 * any bug you can reproduce by hand is a regression test. It writes straight past the shot
 * instruments' input lock (that lock is for hardware), and past the AUTOA pulse, which it captures.
 * ⭐ (11-02) KEYED BY TICK. Every pass through the loop is one tick now, so a line is a tick, whatever the frame rate was when it was recorded. v2 writes the floats as
 * their exact bits, because a text number goes through each libc's own parser, and carries the seed, so a session recorded on the real clock replays exactly under
 * REVA_FIXEDSTEP at any cadence, on any machine. v1 tapes still read.
 * ⭐ (11-03) v3 adds pads 1 to 3 to the end of each line, and a `# binds` line per action in the header. Older tapes read as those three pads not being plugged in. */
static FILE*g_tapeRec=0,*g_tapeRep=0; static int g_tapeDone=0,g_tapeV2=0,g_tapeV3=0; static unsigned g_tapeSeedUsed=0;
static float tapeF(unsigned u){ float f; memcpy(&f,&u,4); return f; }
static unsigned tapeU(float f){ unsigned u; memcpy(&u,&f,4); return u; }
/* a replay's seed, from its header: the dice are rolled before the first poll, so main asks for this before it seeds them */
static int tapeSeedOf(const char*path,unsigned*seed){ FILE*f=fopen(path,"r"); if(!f) return 0; char ln[320]; int ok=0;
    if(fgets(ln,sizeof ln,f)){ const char*s=strstr(ln,"seed "); if((strstr(ln,"tape v2")||strstr(ln,"tape v3"))&&s&&sscanf(s,"seed %u",seed)==1) ok=1; } fclose(f); return ok; }
static void inputTape(double tsec){
    static int init=0;
    if(!init){ init=1; const char*r=getenv("REVA_RECORD"); const char*q=getenv("REVA_REPLAY");
        if(r&&r[0]){ g_tapeRec=fopen(r,"w");
            if(g_tapeRec){ fprintf(g_tapeRec,"# reva input tape v3  seed %u  tick %.4f ms  fixedstep %.4f ms  (tick lx ly rx ry rt A B X Y LB RB St Bk edge mdx mdy manual kb fpv, then pads 1 to 3: on lx ly rx ry rt held; floats as bits)\n",g_tapeSeedUsed,g_tickMs,g_fixedStepMs);
                for(int a9=0;a9<g_nAct;a9++) fprintf(g_tapeRec,"# binds %s %x %x %x %x\n",g_binds[a9].name,g_binds[a9].mask[0],g_binds[a9].mask[1],g_binds[a9].mask[2],g_binds[a9].mask[3]);
                printf("[tape] RECORDING %s\n",r); }
            else printf("[tape] cannot write %s\n",r); }
        if(q&&q[0]){ g_tapeRep=fopen(q,"r");
            if(g_tapeRep){ char h9[320];
                           for(;;){ int c=fgetc(g_tapeRep); ungetc(c,g_tapeRep); if(c!='#'||!fgets(h9,sizeof h9,g_tapeRep)) break;
                               if(strstr(h9,"tape v3")) g_tapeV2=g_tapeV3=1; else if(strstr(h9,"tape v2")) g_tapeV2=1;
                               char an[24]; unsigned m9[4]; if(sscanf(h9,"# binds %23s %x %x %x %x",an,&m9[0],&m9[1],&m9[2],&m9[3])==5){ int i9=actFind(an); if(i9>=0) memcpy(g_binds[i9].mask,m9,sizeof m9); else printf("[tape] it was recorded with an action this game doesn't have: %s\n",an); } }
                           printf("[tape] REPLAY %s (v%d) -- the pad and keyboard are ignored\n",q,g_tapeV3?3:g_tapeV2?2:1); }
            else printf("[tape] cannot read %s\n",q); }
        fflush(stdout); }
    if(g_tapeRep&&!g_tapeDone){ char ln[640]; unsigned long fr; int used9=0; float lx,ly,rx,ry,rt,mdx,mdy; int a,b,x,y,lb,rb,st,bk,ed,man,kb,fpv; unsigned u9[7]; int got9=0;
        if(fgets(ln,sizeof ln,g_tapeRep)){
            if(g_tapeV2){ got9=sscanf(ln,"%lu %x %x %x %x %x %d %d %d %d %d %d %d %d %d %x %x %d %d %d%n",&fr,&u9[0],&u9[1],&u9[2],&u9[3],&u9[4],&a,&b,&x,&y,&lb,&rb,&st,&bk,&ed,&u9[5],&u9[6],&man,&kb,&fpv,&used9);
                lx=tapeF(u9[0]); ly=tapeF(u9[1]); rx=tapeF(u9[2]); ry=tapeF(u9[3]); rt=tapeF(u9[4]); mdx=tapeF(u9[5]); mdy=tapeF(u9[6]);
                static int warned9=0; if(got9==20&&(long)fr!=g_tickTotal&&!warned9){ warned9=1; printf("[tape] ⛔ the tape says tick %lu here and the clock says %ld: this is not the session it was recorded in\n",fr,g_tickTotal); fflush(stdout); } }
            else got9=sscanf(ln,"%lu %f %f %f %f %f %d %d %d %d %d %d %d %d %d %f %f %d %d %d",&fr,&lx,&ly,&rx,&ry,&rt,&a,&b,&x,&y,&lb,&rb,&st,&bk,&ed,&mdx,&mdy,&man,&kb,&fpv); }
        if(got9==20){
            g_lx=lx; g_ly=ly; g_rx=rx; g_ry=ry; g_rt=rt; g_btnA=a; g_btnB=b; g_btnX=x; g_btnY=y; g_btnLB=lb; g_btnRB=rb;
            g_btnStart=st; g_btnBack=bk; g_startEdge=ed; g_mdx=mdx; g_mdy=mdy;
            if(man&&!g_manual) g_inT=tsec;
            g_manual=man; g_kbActive=kb; g_fpv=fpv;
            memset(g_padRaw,0,sizeof g_padRaw);
            if(g_tapeV3){ const char*q9=ln+used9; for(int k=1;k<4;k++){ PadRaw*r=&g_padRaw[k]; unsigned f9[5],h9; int on9,n9=0;
                if(sscanf(q9," %d %x %x %x %x %x %x%n",&on9,&f9[0],&f9[1],&f9[2],&f9[3],&f9[4],&h9,&n9)!=7) break;
                r->connected=on9; r->lx=tapeF(f9[0]); r->ly=tapeF(f9[1]); r->rx=tapeF(f9[2]); r->ry=tapeF(f9[3]); r->rt=tapeF(f9[4]); r->held=h9; q9+=n9; }
                { unsigned m9; g_tapeMusicIn=sscanf(q9," m %x",&m9)==1?tapeF(m9):-1.f; } } }
        else { g_tapeDone=1; memset(g_padRaw,0,sizeof g_padRaw); printf("[tape] replay ended at frame %lu -- neutral from here\n",g_frameNo); fflush(stdout);
               g_lx=g_ly=g_rx=g_ry=g_rt=0.f; g_btnA=g_btnB=g_btnX=g_btnY=g_btnLB=g_btnRB=g_btnStart=g_btnBack=g_startEdge=0; g_mdx=g_mdy=0.f; } }
    if(g_tapeRec){ fprintf(g_tapeRec,"%ld %08x %08x %08x %08x %08x %d %d %d %d %d %d %d %d %d %08x %08x %d %d %d",
        g_tickTotal,tapeU(g_lx),tapeU(g_ly),tapeU(g_rx),tapeU(g_ry),tapeU(g_rt),g_btnA,g_btnB,g_btnX,g_btnY,g_btnLB,g_btnRB,g_btnStart,g_btnBack,g_startEdge,tapeU(g_mdx),tapeU(g_mdy),g_manual,g_kbActive,g_fpv);
        for(int k=1;k<4;k++){ const PadRaw*r=&g_padRaw[k]; fprintf(g_tapeRec," %d %08x %08x %08x %08x %08x %x",r->connected,tapeU(r->lx),tapeU(r->ly),tapeU(r->rx),tapeU(r->ry),tapeU(r->rt),r->held); }
        { const float m9=musicClockRead(); if(m9>=0.f) fprintf(g_tapeRec," m %08x",tapeU(m9)); }   /* what the music clock will read this tick, for a replay to read the same */
        fputc('\n',g_tapeRec); } }
__attribute__((unused)) static float g_mLook=0.f, g_mPitch=0.f;  /* integrated look, mapped onto the right stick */
static void kbPoll(double tsec){
    /* ⛔ THE CAPTURE LOCK SWALLOWED THE BENCH'S OWN KEYBOARD. capLocked() exists so a stray
     * click cannot move a body mid-take -- right -- but every headless run IS a capture,
     * so REVA_KBSIM, which exists precisely to test the key mapping headlessly, could
     * never run under the harness that needed it. The collision A/B measured a hero who
     * had not been told to walk. A simulated key is deliberate; only hardware is locked. */
    { static const char*ksim=(const char*)1; if(ksim==(const char*)1) ksim=getenv("REVA_KBSIM");
      if(capLocked()&&!ksim) return; }
#ifdef REVA_SDL
    const Uint8*ks=SDL_GetKeyboardState(NULL);
    if(!ks) return;
    /* REVA_KBSIM=<chars> holds keys down for the bench (w a s d, space, e, q, m = mouse
     * right). SDL's key state comes from real hardware, so this is the only way to test
     * the MAPPING headlessly -- the axis signs are where an input layer actually breaks. */
    static const char*sim=NULL; static int simInit=0;
    if(!simInit){ simInit=1; sim=getenv("REVA_KBSIM"); }
    int sW=0,sA=0,sS=0,sD=0,sSpc=0,sE=0,sQ=0,sM=0;
    if(sim){ sW=!!strchr(sim,'w'); sA=!!strchr(sim,'a'); sS=!!strchr(sim,'s'); sD=!!strchr(sim,'d');
             sSpc=!!strchr(sim,' '); sE=!!strchr(sim,'e'); sQ=!!strchr(sim,'q'); sM=!!strchr(sim,'m'); }
    int used=0;
    float kx=0.f, ky=0.f;
    if(ks[SDL_SCANCODE_A]||sA) kx+=1.f;   /* g_lx is the floor's INVERTED axis: left is +1 */
    if(ks[SDL_SCANCODE_D]||sD) kx-=1.f;
    if(ks[SDL_SCANCODE_W]||sW) ky-=1.f;   /* stick-forward is negative, same as the pad */
    if(ks[SDL_SCANCODE_S]||sS) ky+=1.f;
    if(kx!=0.f||ky!=0.f){ float m=hypotf(kx,ky); if(m>1.f){ kx/=m; ky/=m; }
        g_lx=kx; g_ly=ky; used=1; }
    int mb=SDL_GetMouseState(NULL,NULL);
    int kA=ks[SDL_SCANCODE_SPACE]||sSpc||(mb&SDL_BUTTON(SDL_BUTTON_LEFT));
    int kB=ks[SDL_SCANCODE_E]||sE||(mb&SDL_BUTTON(SDL_BUTTON_RIGHT));
    int kX=ks[SDL_SCANCODE_Q]||sQ||(mb&SDL_BUTTON(SDL_BUTTON_MIDDLE));
    int kY=ks[SDL_SCANCODE_R];
    int kRT=ks[SDL_SCANCODE_LSHIFT]||ks[SDL_SCANCODE_RSHIFT];
    if(kA){ g_btnA=1; used=1; } if(kB){ g_btnB=1; used=1; }
    if(kX){ g_btnX=1; used=1; } if(kY){ g_btnY=1; used=1; }
    if(ks[SDL_SCANCODE_Z]){ g_btnLB=1; used=1; } if(ks[SDL_SCANCODE_C]){ g_btnRB=1; used=1; }
    if(kRT){ g_rt=1.f; used=1; }
    { static int pTab=0; int tb=ks[SDL_SCANCODE_TAB];
      if(tb&&!pTab){ g_manual=0; printf("MANUAL released (TAB): the host resumes\n"); }
      pTab=tb; }
    { static int pV=0; int v=ks[SDL_SCANCODE_V]||(sim&&strchr(sim,'v'));   /* V: first person on/off */
      if(v&&!pV){ g_fpv=!g_fpv; printf("[fpv] %s (V)\n",g_fpv?"FIRST PERSON":"third person"); fflush(stdout); used=1; }
      pV=v; }
    { int dx=0,dy=0; SDL_GetRelativeMouseState(&dx,&dy);
      if(sM){ dx+=9; }   /* the bench's steady rightward drag */
      g_mdx=(float)dx; g_mdy=(float)dy;
      if(dx||dy){ used=1;
          g_mLook+=dx*0.0045f; g_mPitch+=dy*0.0045f;
          if(g_mLook>1.f)g_mLook=1.f;
          if(g_mLook<-1.f)g_mLook=-1.f;
          if(g_mPitch>1.f)g_mPitch=1.f;
          if(g_mPitch<-1.f)g_mPitch=-1.f; }
      /* NOTE: the look accumulators are NOT fed to the right stick -- in keyboard mode the
       * mouse turns the CHARACTER (the drive block reads g_mdx) and the chase cam follows
       * her. Writing g_rx here as well would double-count the same motion. */ }
    { static int kd=-1, kn=0; if(kd<0) kd=getenv("REVA_HERODBG")?1:0;
      if(kd&&(kn++%60)==0){ printf("[kb] sim=%s sW=%d kx=%.1f ky=%.1f used=%d locked=%d -> g_lx %.2f g_ly %.2f\n",
                                    sim?sim:"-",sW,kx,ky,used,capLocked(),g_lx,g_ly); fflush(stdout); } }
    if(used){ if(!g_kbActive) printf("KEYBOARD+MOUSE: WASD move, mouse look, LMB act, ESC menu\n");
        g_kbActive=1;
        if(!g_pause){ if(!g_manual) printf("MANUAL: jacob takes the floor (keyboard)\n");
            g_manual=1; g_inT=tsec; } }
#else
    (void)tsec;
#endif
}

