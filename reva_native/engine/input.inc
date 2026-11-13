/* ⭐ A CAPTURE IGNORES INPUT. A stray click of the mouse spoiled a run.
 * The mouse turns the hero and the buttons are
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
static int g_keysLast=0;   /* (11-12) the last input was the keys and mouse (1) or pad 0's controller (0): EnginePad.keys, for a game's prompts */
static int g_pad0Here=0;   /* (11-12) a controller is plugged in as pad 0: without one, a game's prompts are the keys' */
static int g_keysNow=0;    /* (11-12) pad 0's keys flag this tick: after the polls (the keys last, or no pad), or from the tape (v7) or the wire */
typedef struct { float lx,ly,rx,ry,rt; unsigned held; int connected; float mdx,mdy; float lt; int ptrOn; float px,py; unsigned pb; int hit; int keys; } PadRaw;   /* ptrOn, px, py, pb, hit (11-12): a session's other players' pointers, and a tape's */   /* lt (11-12): the left trigger; held has the stick clicks, the D-pad and the triggers as buttons too */   /* in the globals' own terms: lx is negated and ly is down. mdx, mdy (11-11): a session's other players' mice */
static PadRaw g_padRaw[4];
static EnginePad g_pads[4];
static float g_lt=0.f; static unsigned g_btnMore=0u;   /* (11-12) pad 0's left trigger, and its buttons past Back (L3, R3, the D-pad): beside the old globals, from the pad, the keys, the tape or the wire */
static float g_mdx=0.f, g_mdy=0.f;       /* this frame's mouse delta, pixels (RTS cursor; and since 11-10 a game's, as pads[0].mx and my) */
/* (11-12) THE POINTER: pad 0's, where the mouse is on the 2D canvas (under -999 off), its buttons and what the game's hud said it was over (input,
 * so on the tape and the wire); this machine's own mouse now, for a hud (E->pointerAt); and what the hud said this frame, for the next poll */
static float g_ptrX=-1000.f, g_ptrY=-1000.f; static unsigned g_ptrB=0u; static int g_ptrHit=0;
static float g_ptrLiveX=-1000.f, g_ptrLiveY=-1000.f; static int g_ptrHitNext=0;
static void sprToCanvas(float px,float py,float*cx,float*cy);   /* engine/sprite.inc */
static int apiPointerAt(float*x,float*y){ if(x) *x=g_ptrLiveX; if(y) *y=g_ptrLiveY; return g_ptrLiveX>-999.f; }
static void apiPointerOver(int id){ g_ptrHitNext=id; }
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
    r->lx=-SDL_GameControllerGetAxis(p,SDL_CONTROLLER_AXIS_LEFTX)/32767.f; r->ly=SDL_GameControllerGetAxis(p,SDL_CONTROLLER_AXIS_LEFTY)/32767.f;   /* left/right was inverted */
    r->rx=SDL_GameControllerGetAxis(p,SDL_CONTROLLER_AXIS_RIGHTX)/32767.f; r->ry=SDL_GameControllerGetAxis(p,SDL_CONTROLLER_AXIS_RIGHTY)/32767.f;
    r->rt=SDL_GameControllerGetAxis(p,SDL_CONTROLLER_AXIS_TRIGGERRIGHT)/32767.f;
    r->lt=SDL_GameControllerGetAxis(p,SDL_CONTROLLER_AXIS_TRIGGERLEFT)/32767.f;   /* (11-12) */
    /* the d-pad is the stick, wherever a stick is read, when the stick's left alone */
    if(!g_tune.padOwn&&fabsf(r->lx)<0.3f&&fabsf(r->ly)<0.3f){ if(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_DPAD_LEFT)) r->lx=1.f; else if(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) r->lx=-1.f;
        if(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_DPAD_UP)) r->ly=-1.f; else if(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_DPAD_DOWN)) r->ly=1.f; }
    r->held=(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_A)?PAD_A:0u)|(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_B)?PAD_B:0u)|(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_X)?PAD_X:0u)|(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_Y)?PAD_Y:0u)
           |(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_LEFTSHOULDER)?PAD_LB:0u)|(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)?PAD_RB:0u)|(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_START)?PAD_START:0u)|(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_BACK)?PAD_BACK:0u);   /* (11-06) Back, or View */
    r->held|=(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_LEFTSTICK)?PAD_L3:0u)|(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_RIGHTSTICK)?PAD_R3:0u)
           |(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_DPAD_UP)?PAD_UP:0u)|(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_DPAD_DOWN)?PAD_DOWN:0u)
           |(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_DPAD_LEFT)?PAD_LEFT:0u)|(SDL_GameControllerGetButton(p,SDL_CONTROLLER_BUTTON_DPAD_RIGHT)?PAD_RIGHT:0u);   /* (11-12) the rest of its face */
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
        g_lx=r->lx; g_ly=r->ly; g_rx=r->rx; g_ry=r->ry; g_rt=r->rt; g_lt=r->lt; g_btnMore=r->held&0x3f00u;
        g_btnA=(r->held&PAD_A)!=0; g_btnB=(r->held&PAD_B)!=0; g_btnX=(r->held&PAD_X)!=0; g_btnY=(r->held&PAD_Y)!=0; g_btnLB=(r->held&PAD_LB)!=0; g_btnRB=(r->held&PAD_RB)!=0; g_btnStart=(r->held&PAD_START)!=0;
        { int bk=SDL_GameControllerGetButton(g_padDev[0],SDL_CONTROLLER_BUTTON_BACK); g_btnBack=bk;
          /* BACK = the old Start: hand the show back to the host */
          if(bk&&!pBack){ g_manual=0; printf("MANUAL released: the host resumes\n"); }
          pBack=bk; }
        if((g_btnA&&!pA)||(g_btnB&&!pB)||(g_btnX&&!pX)||fabsf(r->lx)>0.3f||fabsf(r->ly)>0.3f||fabsf(r->rx)>0.3f||fabsf(r->ry)>0.3f){
            g_keysLast=0;
            if(!g_manual&&!g_pause) printf("MANUAL: the player takes the floor\n");
            if(!g_pause){ g_manual=1; g_inT=tsec; } }
        pA=g_btnA; pB=g_btnB; pX=g_btnX; }
#endif
    /* ⛔ With no pad nothing ever cleared these: kbPoll only writes what's held, so on a machine with a keyboard and no pad, a key let go of stayed down */
    if(!pad0){ g_lx=g_ly=g_rx=g_ry=g_rt=g_lt=0.f; g_btnA=g_btnB=g_btnX=g_btnY=g_btnLB=g_btnRB=g_btnStart=g_btnBack=0; g_btnMore=0u; }
    g_pad0Here=pad0;
    /* (11-12) a game is the pad's always (main.inc's input policy sets g_manual every frame for one): no idle handback, which had printed this line every
     * frame from 75 s on in a game nobody touched, 35,500 times in the skate game's checks */
    if(g_manual&&tsec-g_inT>75&&!GAME_ON&&!DOOR_ON_THE_PAD){ g_manual=0; printf("MANUAL idle: the host resumes\n"); }
}
/* what a game reads, once a tick, after the polls and the tape. Each stick goes through a round dead zone, so a stick left alone is exactly zero */
#define PAD_DEAD 0.16f
static void padStick(float*x,float*y){ float m=hypotf(*x,*y); if(m<PAD_DEAD){ *x=*y=0.f; return; }
    float s=((m>1.f?1.f:m)-PAD_DEAD)/(1.f-PAD_DEAD)/m; *x*=s; *y*=s; }
static void padsUpdate(void){
    for(int k=0;k<4;k++){ EnginePad*d=&g_pads[k]; unsigned was=d->held;
        d->mx=k==0?g_mdx:g_padRaw[k].mdx; d->my=k==0?g_mdy:g_padRaw[k].mdy;   /* (11-10) the mouse, for a game: pad 0's, as the keyboard is, and in a session each player's own (11-11) */
        { const unsigned wasB=d->pb; const PadRaw*r9=&g_padRaw[k]; const int on9=k==0?g_ptrX>-999.f:r9->ptrOn;   /* (11-12) the pointer */
          d->px=on9?(k==0?g_ptrX:r9->px):-1000.f; d->py=on9?(k==0?g_ptrY:r9->py):-1000.f; d->pb=on9?((k==0?g_ptrB:r9->pb)&7u):0u; d->hit=on9?(k==0?g_ptrHit:r9->hit):0;
          d->pbPressed=d->pb&~wasB; }
        if(k==0){ d->connected=g_pad0On; d->x=-g_lx; d->y=-g_ly; d->rx=g_rx; d->ry=-g_ry; d->rt=g_rt; d->lt=g_lt;
            d->held=(g_btnA?PAD_A:0)|(g_btnB?PAD_B:0)|(g_btnX?PAD_X:0)|(g_btnY?PAD_Y:0)|(g_btnLB?PAD_LB:0)|(g_btnRB?PAD_RB:0)|(g_btnStart?PAD_START:0)|(g_btnBack?PAD_BACK:0)|(g_btnMore&0x3f00u); }
        else { const PadRaw*r=&g_padRaw[k]; d->connected=r->connected; d->x=-r->lx; d->y=-r->ly; d->rx=r->rx; d->ry=-r->ry; d->rt=r->rt; d->lt=r->lt; d->held=r->held&0x3fffu; }
        d->held|=(d->rt>0.5f?(unsigned)PAD_RT:0u)|(d->lt>0.5f?(unsigned)PAD_LT:0u);   /* (11-12) each trigger as a button, from what it reads, whoever pulled it */
        if(k==0&&g_xrLive) xrPad(d);   /* (11-11) a headset's controllers (engine/xr.inc) */
        padStick(&d->x,&d->y); padStick(&d->rx,&d->ry);
        d->keys=k==0?g_keysNow:g_padRaw[k].keys;   /* (11-12) input: the poll's (the keys last, or no pad plugged in, as their InputDeviceManager), the tape's or the wire's */
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
static unsigned actButton(const char*w){ static const struct { const char*n; unsigned m; } B[]={{"a",PAD_A},{"b",PAD_B},{"x",PAD_X},{"y",PAD_Y},{"lb",PAD_LB},{"rb",PAD_RB},{"start",PAD_START},{"back",PAD_BACK},
        {"l3",PAD_L3},{"r3",PAD_R3},{"up",PAD_UP},{"down",PAD_DOWN},{"left",PAD_LEFT},{"right",PAD_RIGHT},{"lt",PAD_LT},{"rt",PAD_RT}};   /* (11-12) the rest of the pad */
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

/* ================= KEYBOARD + MOUSE (08-25: every mode has keyboard and mouse controls,
 * on WASD, not the arrow keys) ============
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
/* (11-12) A GAME'S OWN KEYS (E->keyPad): each line is some keys and the pad 0 buttons they hold. The mouse's buttons are keys -1, -2 and -3 */
#define KEYPAD_MAX 48
static struct { int key[6]; int n; unsigned bits; } g_keyPad[KEYPAD_MAX]; static int g_nKeyPad=0;
static int keyPadName(const char*w){
#ifdef REVA_SDL
    if(w[0]&&!w[1]){ if(w[0]>='a'&&w[0]<='z') return SDL_SCANCODE_A+(w[0]-'a'); if(w[0]>='1'&&w[0]<='9') return SDL_SCANCODE_1+(w[0]-'1'); if(w[0]=='0') return SDL_SCANCODE_0; }
    static const struct { const char*n; int k; } K[]={{"space",SDL_SCANCODE_SPACE},{"tab",SDL_SCANCODE_TAB},{"enter",SDL_SCANCODE_RETURN},{"backspace",SDL_SCANCODE_BACKSPACE},
        {"lshift",SDL_SCANCODE_LSHIFT},{"rshift",SDL_SCANCODE_RSHIFT},{"lctrl",SDL_SCANCODE_LCTRL},{"rctrl",SDL_SCANCODE_RCTRL},{"lalt",SDL_SCANCODE_LALT},{"ralt",SDL_SCANCODE_RALT},
        {"up",SDL_SCANCODE_UP},{"down",SDL_SCANCODE_DOWN},{"left",SDL_SCANCODE_LEFT},{"right",SDL_SCANCODE_RIGHT},{"mouse1",-1},{"mouse2",-2},{"mouse3",-3},
        {"escape",SDL_SCANCODE_ESCAPE}};   /* escape (11-12): a game that owns its pause (EngineTuning.pauseOwn) */
    for(unsigned i=0;i<sizeof K/sizeof K[0];i++) if(!strcmp(w,K[i].n)) return K[i].k;
#endif
    return 0; }
static void apiKeyPad(const char*keys,unsigned buttons){ if(!keys||!buttons) return;
    if(g_nKeyPad>=KEYPAD_MAX){ printf("[keys] more than %d lines of a game's keys: %s isn't laid out\n",KEYPAD_MAX,keys); fflush(stdout); return; }
    char buf[160]; snprintf(buf,sizeof buf,"%s",keys); int n=0; int k6[6];
    for(char*t=strtok(buf," ,");t&&n<6;t=strtok(NULL," ,")){
        if(!strcmp(t,"shift")){ k6[n++]=keyPadName("lshift"); if(n<6) k6[n++]=keyPadName("rshift"); continue; }
        if(!strcmp(t,"ctrl")){ k6[n++]=keyPadName("lctrl"); if(n<6) k6[n++]=keyPadName("rctrl"); continue; }
        if(!strcmp(t,"alt")){ k6[n++]=keyPadName("lalt"); if(n<6) k6[n++]=keyPadName("ralt"); continue; }
        const int k=keyPadName(t); if(k) k6[n++]=k; else { printf("[keys] no key called %s\n",t); fflush(stdout); } }
    if(!n) return;
    memcpy(g_keyPad[g_nKeyPad].key,k6,sizeof k6); g_keyPad[g_nKeyPad].n=n; g_keyPad[g_nKeyPad].bits=buttons; g_nKeyPad++; }
/* (g_mdx and g_mdy, this frame's mouse delta, are declared at the top now, beside the pads that carry them to a game) */
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
static FILE*g_tapeRec=0,*g_tapeRep=0; static int g_tapeV7=0; static int g_tapeDone=0,g_tapeV2=0,g_tapeV3=0,g_tapeV4=0,g_tapeV5=0,g_tapeV6=0;   /* v6 (11-12): the pointers */   /* v5 (11-12): the left triggers and pad 0's buttons past Back */ static unsigned g_tapeSeedUsed=0;   /* v4 (11-11): pads 1 to 3's mouse too */
/* (step 29) in a session the tape is written after the pads of every machine are in (engine/net.inc), so what it holds is the session: four pads, as any tape */
static int g_tapeLate=0; static void tapeRecord(void);
static float tapeF(unsigned u){ float f; memcpy(&f,&u,4); return f; }
static unsigned tapeU(float f){ unsigned u; memcpy(&u,&f,4); return u; }
/* a replay's seed, from its header: the dice are rolled before the first poll, so main asks for this before it seeds them */
static int tapeSeedOf(const char*path,unsigned*seed){ FILE*f=fopen(path,"r"); if(!f) return 0; char ln[320]; int ok=0;
    if(fgets(ln,sizeof ln,f)){ const char*s=strstr(ln,"seed "); if((strstr(ln,"tape v2")||strstr(ln,"tape v3")||strstr(ln,"tape v4")||strstr(ln,"tape v5")||strstr(ln,"tape v6")||strstr(ln,"tape v7"))&&s&&sscanf(s,"seed %u",seed)==1) ok=1; } fclose(f); return ok; }
static void inputTape(double tsec){
    static int init=0;
    if(!init){ init=1; const char*r=getenv("REVA_RECORD"); const char*q=getenv("REVA_REPLAY");
        if(r&&r[0]){ g_tapeRec=fopen(r,"w");
            if(g_tapeRec){ fprintf(g_tapeRec,"# reva input tape v7  seed %u  tick %.4f ms  fixedstep %.4f ms  (tick lx ly rx ry rt A B X Y LB RB St Bk edge mdx mdy manual kb fpv, then pads 1 to 3: on lx ly rx ry rt held mdx mdy, then p5: pad 0 lt and its buttons past Back, pads 1 to 3 lt, then p6: pad 0's pointer x y buttons over, pads 1 to 3's on x y buttons over; floats as bits)\n",g_tapeSeedUsed,g_tickMs,g_fixedStepMs);
                for(int a9=0;a9<g_nAct;a9++) fprintf(g_tapeRec,"# binds %s %x %x %x %x\n",g_binds[a9].name,g_binds[a9].mask[0],g_binds[a9].mask[1],g_binds[a9].mask[2],g_binds[a9].mask[3]);
                printf("[tape] RECORDING %s\n",r); }
            else printf("[tape] cannot write %s\n",r); }
        if(q&&q[0]){ g_tapeRep=fopen(q,"r");
            if(g_tapeRep){ char h9[320];
                           for(;;){ int c=fgetc(g_tapeRep); ungetc(c,g_tapeRep); if(c!='#'||!fgets(h9,sizeof h9,g_tapeRep)) break;
                               /* ⛔ (11-13) A LINE LONGER THAN h9 IS READ TO ITS END. v6's header (11-12) grew to 345 characters, past this buffer: what was left of it was taken for the
                                * first tick, which it isn't, so the replay ended at frame 1 and played neutral. No tape recorded from 11-12 replayed, a session's among them */
                               if(!strchr(h9,'\n')){ int c9; while((c9=fgetc(g_tapeRep))!=EOF&&c9!='\n'){} }
                               if(strstr(h9,"tape v7")) g_tapeV2=g_tapeV3=g_tapeV4=g_tapeV5=g_tapeV6=g_tapeV7=1; else if(strstr(h9,"tape v6")) g_tapeV2=g_tapeV3=g_tapeV4=g_tapeV5=g_tapeV6=1; else if(strstr(h9,"tape v5")) g_tapeV2=g_tapeV3=g_tapeV4=g_tapeV5=1; else if(strstr(h9,"tape v4")) g_tapeV2=g_tapeV3=g_tapeV4=1; else if(strstr(h9,"tape v3")) g_tapeV2=g_tapeV3=1; else if(strstr(h9,"tape v2")) g_tapeV2=1;
                               char an[24]; unsigned m9[4]; if(sscanf(h9,"# binds %23s %x %x %x %x",an,&m9[0],&m9[1],&m9[2],&m9[3])==5){ int i9=actFind(an); if(i9>=0) memcpy(g_binds[i9].mask,m9,sizeof m9); else printf("[tape] it was recorded with an action this game doesn't have: %s\n",an); } }
                           printf("[tape] REPLAY %s (v%d) -- the pad and keyboard are ignored\n",q,g_tapeV7?7:g_tapeV6?6:g_tapeV5?5:g_tapeV4?4:g_tapeV3?3:g_tapeV2?2:1); }
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
            g_btnStart=st; g_btnBack=bk; g_startEdge=ed; g_mdx=mdx; g_mdy=mdy; g_lt=0.f; g_btnMore=0u;   /* (11-12) a v5 tape's p5 says otherwise */
            g_ptrX=g_ptrY=-1000.f; g_ptrB=0u; g_ptrHit=0;   /* (11-12) and a v6 tape's p6 */
            if(man&&!g_manual) g_inT=tsec;
            g_manual=man; g_kbActive=kb; g_fpv=fpv; g_keysNow=kb!=0;   /* (11-12) a tape before v7 has no keys flag: the keyboard's, as best it can say */
            memset(g_padRaw,0,sizeof g_padRaw);
            if(g_tapeV3){ const char*q9=ln+used9; for(int k=1;k<4;k++){ PadRaw*r=&g_padRaw[k]; unsigned f9[5],h9; int on9,n9=0;
                if(sscanf(q9," %d %x %x %x %x %x %x%n",&on9,&f9[0],&f9[1],&f9[2],&f9[3],&f9[4],&h9,&n9)!=7) break;
                r->connected=on9; r->lx=tapeF(f9[0]); r->ly=tapeF(f9[1]); r->rx=tapeF(f9[2]); r->ry=tapeF(f9[3]); r->rt=tapeF(f9[4]); r->held=h9; q9+=n9;
                if(g_tapeV4){ unsigned m9x,m9y; int n8=0; if(sscanf(q9," %x %x%n",&m9x,&m9y,&n8)==2){ r->mdx=tapeF(m9x); r->mdy=tapeF(m9y); q9+=n8; } } }   /* (11-11) that pad's mouse */
                if(g_tapeV5){ unsigned a5,b5,c5,d5,e5; int n5=0; if(sscanf(q9," p5 %x %x %x %x %x%n",&a5,&b5,&c5,&d5,&e5,&n5)==5){ g_lt=tapeF(a5); g_btnMore=b5&0x3f00u;
                    g_padRaw[1].lt=tapeF(c5); g_padRaw[2].lt=tapeF(d5); g_padRaw[3].lt=tapeF(e5); q9+=n5; } }   /* (11-12) the left triggers and pad 0's other buttons */
                if(g_tapeV6){ unsigned a6,b6,c6,d6; int n6=0; if(sscanf(q9," p6 %x %x %x %x%n",&a6,&b6,&c6,&d6,&n6)==4){ g_ptrX=tapeF(a6); g_ptrY=tapeF(b6); g_ptrB=c6&7u; g_ptrHit=(int)d6; q9+=n6;   /* (11-12) the pointers */
                    for(int k=1;k<4;k++){ int on6=0,m6=0; if(sscanf(q9," %d %x %x %x %x%n",&on6,&a6,&b6,&c6,&d6,&m6)!=5) break; PadRaw*r=&g_padRaw[k]; r->ptrOn=on6; r->px=tapeF(a6); r->py=tapeF(b6); r->pb=c6&7u; r->hit=(int)d6; q9+=m6; } } }
                if(g_tapeV7){ unsigned k7; int n7=0; if(sscanf(q9," p7 %x%n",&k7,&n7)==1){ g_keysNow=(int)(k7&1u); for(int k=1;k<4;k++) g_padRaw[k].keys=(int)((k7>>k)&1u); q9+=n7; } }   /* (11-12) which device each pad last used */
                { unsigned m9; g_tapeMusicIn=sscanf(q9," m %x",&m9)==1?tapeF(m9):-1.f; }
                g_pad0On=strstr(q9," u")==NULL; } }   /* (step 29) a session's tape: pad 1's machine had left */
        else { g_tapeDone=1; g_pad0On=1; memset(g_padRaw,0,sizeof g_padRaw); printf("[tape] replay ended at frame %lu -- neutral from here\n",g_frameNo); fflush(stdout);
               g_lx=g_ly=g_rx=g_ry=g_rt=g_lt=0.f; g_btnA=g_btnB=g_btnX=g_btnY=g_btnLB=g_btnRB=g_btnStart=g_btnBack=g_startEdge=0; g_btnMore=0u; g_mdx=g_mdy=0.f; g_ptrX=g_ptrY=-1000.f; g_ptrB=0u; g_ptrHit=0; } }
    if(!g_tapeLate) tapeRecord(); }
static void tapeRecord(void){
    if(g_tapeRec){ fprintf(g_tapeRec,"%ld %08x %08x %08x %08x %08x %d %d %d %d %d %d %d %d %d %08x %08x %d %d %d",
        g_tickTotal,tapeU(g_lx),tapeU(g_ly),tapeU(g_rx),tapeU(g_ry),tapeU(g_rt),g_btnA,g_btnB,g_btnX,g_btnY,g_btnLB,g_btnRB,g_btnStart,g_btnBack,g_startEdge,tapeU(g_mdx),tapeU(g_mdy),g_manual,g_kbActive,g_fpv);
        for(int k=1;k<4;k++){ const PadRaw*r=&g_padRaw[k]; fprintf(g_tapeRec," %d %08x %08x %08x %08x %08x %x %08x %08x",r->connected,tapeU(r->lx),tapeU(r->ly),tapeU(r->rx),tapeU(r->ry),tapeU(r->rt),r->held,tapeU(r->mdx),tapeU(r->mdy)); }
        fprintf(g_tapeRec," p5 %08x %x %08x %08x %08x",tapeU(g_lt),g_btnMore&0x3f00u,tapeU(g_padRaw[1].lt),tapeU(g_padRaw[2].lt),tapeU(g_padRaw[3].lt));   /* (11-12) v5 */
        fprintf(g_tapeRec," p6 %08x %08x %x %x",tapeU(g_ptrX),tapeU(g_ptrY),g_ptrB,(unsigned)g_ptrHit);   /* (11-12) v6: the pointers */
        for(int k=1;k<4;k++){ const PadRaw*r=&g_padRaw[k]; fprintf(g_tapeRec," %d %08x %08x %x %x",r->ptrOn,tapeU(r->px),tapeU(r->py),r->pb,(unsigned)r->hit); }
        { unsigned k7=g_keysNow?1u:0u; for(int k=1;k<4;k++) if(g_padRaw[k].keys) k7|=1u<<k; fprintf(g_tapeRec," p7 %x",k7); }   /* (11-12) v7: each pad's keys flag */
        { const float m9=musicClockRead(); if(m9>=0.f) fprintf(g_tapeRec," m %08x",tapeU(m9)); }
        if(!g_pad0On) fputs(" u",g_tapeRec);   /* (step 29) pad 1 is unplugged: only ever in a session, once its machine has gone */   /* what the music clock will read this tick, for a replay to read the same */
        fputc('\n',g_tapeRec); } }
__attribute__((unused)) static float g_mLook=0.f, g_mPitch=0.f;  /* integrated look, mapped onto the right stick */
static void kbPoll(double tsec){
    /* ⛔ THE CAPTURE LOCK SWALLOWED THE BENCH'S OWN KEYBOARD. capLocked() exists so a stray
     * click cannot move a body mid-take -- right -- but every headless run IS a capture,
     * so REVA_KBSIM, which exists precisely to test the key mapping headlessly, could
     * never run under the harness that needed it. The collision A/B measured a hero who
     * had not been told to walk. A simulated key is deliberate; only hardware is locked. */
    { static const char*ksim=(const char*)1; if(ksim==(const char*)1){ ksim=getenv("REVA_KBSIM"); if(!ksim) ksim=getenv("REVA_PTRTEST"); }   /* (11-12) the pointer test is deliberate input too */
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
    { int wx=0,wy=0; const Uint32 mbs=SDL_GetMouseState(&wx,&wy); SDL_Window*fw=SDL_GetMouseFocus(); g_ptrX=g_ptrY=-1000.f; g_ptrB=0u;   /* (11-12) the pointer, when it's free over the picture */
      if(fw&&!SDL_GetRelativeMouseMode()){ int ww=0,wh=0; SDL_GetWindowSize(fw,&ww,&wh); float px9=0.f,py9=0.f;
          if(ww>0&&wh>0&&gfxPointer((float)wx/(float)ww,(float)wy/(float)wh,&px9,&py9)){ sprToCanvas(px9,py9,&g_ptrX,&g_ptrY);
              g_ptrB=((mbs&SDL_BUTTON(SDL_BUTTON_LEFT))?1u:0u)|((mbs&SDL_BUTTON(SDL_BUTTON_RIGHT))?2u:0u)|((mbs&SDL_BUTTON(SDL_BUTTON_MIDDLE))?4u:0u); } }
      { static int tp=-1; static float tx,ty; static int tf;   /* REVA_PTRTEST=x,y[,frame]: the pointer held at that point of the canvas, and a left click at that frame (a check with no hands) */
        if(tp<0){ const char*e=getenv("REVA_PTRTEST"); tf=-1; tp=e?sscanf(e,"%f,%f,%d",&tx,&ty,&tf)>=2:0; }
        if(tp){ g_ptrX=tx; g_ptrY=ty; g_ptrB=(tf>=0&&(long)g_frameNo>=tf&&(long)g_frameNo<tf+2)?1u:0u; } }
      g_ptrLiveX=g_ptrX; g_ptrLiveY=g_ptrY; g_ptrHit=g_ptrX>-999.f?g_ptrHitNext:0; }
    if(g_nKeyPad){   /* (11-12) a game's own keys: they say which buttons, and the engine's own key buttons below are off */
        const int altDown=ks[SDL_SCANCODE_LALT]||ks[SDL_SCANCODE_RALT];   /* (11-12) Alt+Enter is fullscreen, not the game's Enter */
        for(int i=0;i<g_nKeyPad;i++){ int on=0; for(int j=0;j<g_keyPad[i].n&&!on;j++){ const int k=g_keyPad[i].key[j];
                if(altDown&&(k==SDL_SCANCODE_RETURN||k==SDL_SCANCODE_KP_ENTER)) continue;
                on=k>0?ks[k]!=0:k==-1?(mb&SDL_BUTTON(SDL_BUTTON_LEFT))!=0:k==-2?(mb&SDL_BUTTON(SDL_BUTTON_RIGHT))!=0:(mb&SDL_BUTTON(SDL_BUTTON_MIDDLE))!=0; }
            if(!on) continue;
            const unsigned b=g_keyPad[i].bits; used=1;
            g_btnA|=(b&PAD_A)!=0; g_btnB|=(b&PAD_B)!=0; g_btnX|=(b&PAD_X)!=0; g_btnY|=(b&PAD_Y)!=0; g_btnLB|=(b&PAD_LB)!=0; g_btnRB|=(b&PAD_RB)!=0;
            g_btnStart|=(b&PAD_START)!=0; g_btnBack|=(b&PAD_BACK)!=0; g_btnMore|=b&0x3f00u;
            if(b&PAD_RT){ g_rt=1.f; }
            if(b&PAD_LT){ g_lt=1.f; } } }
    int kA=!g_nKeyPad&&(ks[SDL_SCANCODE_SPACE]||sSpc||(mb&SDL_BUTTON(SDL_BUTTON_LEFT)));
    int kB=!g_nKeyPad&&(ks[SDL_SCANCODE_E]||sE||(mb&SDL_BUTTON(SDL_BUTTON_RIGHT)));
    int kX=!g_nKeyPad&&(ks[SDL_SCANCODE_Q]||sQ||(mb&SDL_BUTTON(SDL_BUTTON_MIDDLE)));
    int kY=!g_nKeyPad&&ks[SDL_SCANCODE_R];
    int kRT=!g_nKeyPad&&(ks[SDL_SCANCODE_LSHIFT]||ks[SDL_SCANCODE_RSHIFT]);
    if(kA){ g_btnA=1; used=1; } if(kB){ g_btnB=1; used=1; }
    if(kX){ g_btnX=1; used=1; } if(kY){ g_btnY=1; used=1; }
    if(!g_nKeyPad){ if(ks[SDL_SCANCODE_Z]){ g_btnLB=1; used=1; } if(ks[SDL_SCANCODE_C]){ g_btnRB=1; used=1; } }
    if(kRT){ g_rt=1.f; used=1; }
    { static int pTab=0; int tb=!g_nKeyPad&&ks[SDL_SCANCODE_TAB];
      if(tb&&!pTab){ g_manual=0; printf("MANUAL released (TAB): the host resumes\n"); }
      pTab=tb; }
    { static int pV=0; int v=!g_nKeyPad&&(ks[SDL_SCANCODE_V]||(sim&&strchr(sim,'v')));   /* V: first person on/off (not with a game's own keys) */
      if(v&&!pV){ g_fpv=!g_fpv; printf("[fpv] %s (V)\n",g_fpv?"FIRST PERSON":"third person"); fflush(stdout); used=1; }
      if(v&&GAME_ON) g_btnBack=1;   /* (11-06) in a game, V is the pad's Back: the game decides what it does */
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
        g_keysLast=1;
        g_kbActive=1;
        if(!g_pause){ if(!g_manual) printf("MANUAL: the player takes the floor (keyboard)\n");
            g_manual=1; g_inT=tsec; } }
#ifdef __ANDROID__
    touchPoll(tsec);   /* (11-06) the phone's touch controls, over what the pad and the keys said */
#endif
#else
    (void)tsec;
#endif
    g_keysNow=g_keysLast||!g_pad0Here;   /* (11-12) after both polls; the tape and the wire, which come after, may say otherwise */
}

