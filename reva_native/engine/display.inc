/* ---- THE SCREEN (11-03, engine plan Phase 4; the research is research_notes/.../designing_for_all_tvs.md) ------------------------------------------------------------------
 * jacob: "you should be designing this for all tvs, not specifically tuning it to mine. and please dont break my tv". Nothing can ask a TV what it is, so the player is the sensor:
 * the first boot shows the one signal its region's sets are built for, and a short setup asks about the rest. What it learns is one small text file, which a person can read and
 * edit from any PC, or delete. Recovery needs no picture: hold a button at power-on, or cut the power twice within 30 seconds of starting, and it asks again.
 * ⛔ Only the four standard composite modes are ever emitted (the whitelist in main()), and 50 Hz is off here until someone with a PAL set has tested it. */
typedef struct { const char*name; int w,h,il,hz; } VideoMode;
static const VideoMode VM[4]={{"480i",720,480,1,60},{"240p",720,240,0,60},{"576i",720,576,1,50},{"288p",720,288,0,50}};
static int vmOf(const char*n){ if(n) for(int i=0;i<4;i++) if(!strcmp(n,VM[i].name)) return i; return -1; }
static int vmOfSize(int W,int H){ for(int i=0;i<4;i++) if(W==VM[i].w&&H==VM[i].h) return i; return -1; }

/* found: a file said this. mode: which one to boot in. ok240: the player kept 240p on this set. allow50: 50 Hz may be offered. fit: the player fitted the edges.
 * edge: what the set hides at the left, right, top and bottom, in raster pixels, per mode. stretch: the picture's width against the standard's, from the circle test. reading: the smallest letters they can read. */
typedef struct { int found,mode,ok240,allow50,fit; int edge[4][4]; float stretch; int reading; } ScreenFit;
static ScreenFit g_fit;
static int g_fitReset=0;   /* recovery asked for a fresh start: setup runs and says so */

static void scrDefaults(void){ memset(&g_fit,0,sizeof g_fit); g_fit.stretch=1.f;
    for(int m=0;m<4;m++){ g_fit.edge[m][0]=g_fit.edge[m][1]=VM[m].w/20; g_fit.edge[m][2]=g_fit.edge[m][3]=VM[m].h/20; } }   /* action safe: 90% */
static int scrRead(const char*path){
    FILE*f=fopen(path,"r"); if(!f) return 0;
    int other=0; char ln[160]; while(fgets(ln,sizeof ln,f)){ char k[32],a[16]; int e[4]; float v;
        if(ln[0]=='#') continue;
        if(sscanf(ln,"reading %15s",a)==1){ int r=tvReadOf(a); if(r>=0) g_fit.reading=r; else printf("[screen] %s: no reading size %s\n",path,a); continue; }   /* the one line a PC writes: on its own it says nothing about a TV, so a first boot still asks */
        other=1;
        if(sscanf(ln,"mode %15s",a)==1){ int m=vmOf(a); if(m>=0) g_fit.mode=m; else printf("[screen] %s: no mode %s\n",path,a); }
        else if(sscanf(ln,"edges %15s %d %d %d %d",a,&e[0],&e[1],&e[2],&e[3])==5){ int m=vmOf(a); if(m>=0){ memcpy(g_fit.edge[m],e,sizeof e); g_fit.fit=1; } }
        else if(sscanf(ln,"stretch %f",&v)==1) g_fit.stretch=(v<0.9f)?0.9f:(v>1.1f)?1.1f:v;
        else if(sscanf(ln,"ok240 %d",&e[0])==1) g_fit.ok240=e[0];
        else if(sscanf(ln,"allow50 %d",&e[0])==1) g_fit.allow50=e[0];
        else if(sscanf(ln,"%31s",k)==1) printf("[screen] %s: line not understood: %s",path,ln); }
    fclose(f); g_fit.found=other; printf("[screen] %s: boot in %s, 240p %s, edges %s, stretch %.3f, %s letters\n",path,VM[g_fit.mode].name,g_fit.ok240?"kept":"not asked for",g_fit.fit?"fitted":"default",g_fit.stretch,TV_READ[g_fit.reading].name); fflush(stdout); return 1; }
/* REVA_DISPLAY names a file. A file on the Pi's boot partition wins, because that's the one a person can edit from a PC with no picture. Setup writes the game's own save folder. */
static void scrLoad(void){ scrDefaults(); const char*e=getenv("REVA_DISPLAY");
    if(e&&e[0]) scrRead(e);
    else if(!scrRead("/boot/firmware/aigame_display.txt")) scrRead("save/display.txt");
    e=getenv("REVA_READING");
    if(e&&e[0]){ int r=tvReadOf(e); if(r>=0) g_fit.reading=r; else printf("[screen] REVA_READING: small, medium or large, not %s\n",e); } }
__attribute__((unused)) static void scrSave(void){ if(getenv("REVA_NOSAVE")) return;
#ifdef _WIN32
    _mkdir("save");
#else
    mkdir("save",0777);
#endif
    FILE*f=fopen("save/display.txt","w"); if(!f){ printf("[screen] cannot write save/display.txt\n"); return; }
    fprintf(f,"# what setup learned about this screen. Safe to edit, or to delete: then it asks again.\nreading %s\n",TV_READ[g_fit.reading].name);
    if(g_fit.found) fprintf(f,"mode %s\nok240 %d\nstretch %.3f\n",VM[g_fit.mode].name,g_fit.ok240,g_fit.stretch);
    if(g_fit.allow50) fprintf(f,"allow50 1\n");
    if(g_fit.found&&g_fit.fit) for(int m=0;m<4;m++) fprintf(f,"edges %s %d %d %d %d\n",VM[m].name,g_fit.edge[m][0],g_fit.edge[m][1],g_fit.edge[m][2],g_fit.edge[m][3]);
    fclose(f); printf("[screen] saved save/display.txt\n"); fflush(stdout); }
/* two power cuts within 30 seconds of starting: this counts starts, and scrBootOk clears them once a run has lasted */
__attribute__((unused)) static void scrBootCount(void){ if(getenv("REVA_NOSAVE")) return;
    int n=0; FILE*f=fopen("save/boot.txt","r"); if(f){ if(fscanf(f,"%d",&n)!=1) n=0; fclose(f); } n++;
    if(n>=3){ printf("[screen] started %d times in a row without lasting 30 s: setup again\n",n); remove("save/display.txt"); scrDefaults(); g_fitReset=1; n=0; fflush(stdout); }
    f=fopen("save/boot.txt","w"); if(f){ fprintf(f,"%d\n",n); fclose(f); } }
__attribute__((unused)) static void scrBootOk(void){ remove("save/boot.txt"); }
/* the text's grid sits in the middle of what the player can see; the 3D picture stays the whole raster, so a screen fit never changes what a frame costs */
static void scrApply(int W,int H){ int m=vmOfSize(W,H); g_tvCx=g_tvCy=-1; g_tvRead=g_fit.reading;
    if(m>=0&&g_fit.fit){ const int*e=g_fit.edge[m]; g_tvCx=(e[0]+W-e[1])/2; g_tvCy=(e[2]+H-e[3])/2; }
    tvInit(W,H,g_dispAspect>0.f); }

/* ---- changing the mode. On the direct road, between frames, and never by anything but the whitelist */
#if defined(REVA_VK)&&!defined(REVA_SDL)
static drmModeConnector*g_vConn=NULL; static int g_vFd=-1;
/* the connector's colour system. ⛔ This set can't show PAL, and this only runs when a 50 Hz mode has been asked for by name */
static void tvModeSet(const char*want){ drmModeObjectProperties*op=drmModeObjectGetProperties(g_vFd,g_vConn->connector_id,DRM_MODE_OBJECT_CONNECTOR); if(!op) return;
    for(unsigned i=0;i<op->count_props;i++){ drmModePropertyRes*pr=drmModeGetProperty(g_vFd,op->props[i]); if(!pr) continue;
        if(!strcmp(pr->name,"TV mode")) for(int k=0;k<pr->count_enums;k++) if(!strcmp(pr->enums[k].name,want)){ if(drmModeConnectorSetProperty(g_vFd,g_vConn->connector_id,pr->prop_id,pr->enums[k].value)) printf("[video] TV mode %s refused\n",want); else printf("[video] TV mode %s\n",want); }
        drmModeFreeProperty(pr); }
    drmModeFreeObjectProperties(op); }
#endif
static int g_vNow=-1;
static void videoSwitch(int m,int*W,int*H){
    if(m<0||m>3||m==g_vNow) return;
#if defined(REVA_VK)&&!defined(REVA_SDL)
    if(!g_vConn) return;
    for(int i=0;i<g_vConn->count_modes;i++){ drmModeModeInfo*q=&g_vConn->modes[i];
        if(q->hdisplay==VM[m].w&&q->vdisplay==VM[m].h&&!!(q->flags&DRM_MODE_FLAG_INTERLACE)==VM[m].il){
            if(g_vNow>=0&&VM[g_vNow].hz!=VM[m].hz) tvModeSet(VM[m].hz==50?"PAL":"NTSC");
            drmModeModeInfo mi=*q; gfxKmsRemode(&mi,VM[m].w,VM[m].h); *W=VM[m].w; *H=VM[m].h; g_vNow=m;
            g_ledFieldMs=(double)mi.htotal*(double)mi.vtotal/(double)mi.clock/((mi.flags&DRM_MODE_FLAG_INTERLACE)?2.0:1.0);
            g_ledLast=0.0;   /* the gap across a change of mode isn't a late frame: it happens behind a cut (the plan's rulings) */
            printf("[video] now %s\n",VM[m].name); fflush(stdout); scrApply(*W,*H); return; } }
    printf("[video] the driver doesn't list %s\n",VM[m].name);
#else
    (void)W; (void)H; g_vNow=m;   /* a window stays the size it is */
    printf("[video] %s asked for: this road stays as it is\n",VM[m].name); fflush(stdout);
#endif
}
/* what a game asked for since the last frame. 240p only once the player has kept it on this set: REVA_VIDEO says the same thing by hand */
__attribute__((unused)) static void videoPoll(int*W,int*H){
    if(g_vNow<0) g_videoWant=-1;   /* a window has no modes */
    if(g_videoWant>=0){ int m=g_videoWant; g_videoWant=-1;
        if(getenv("REVA_VIDEO")&&getenv("REVA_VIDEO")[0]&&!getenv("REVA_VIDEOFLIP")) return;
        if(m==1&&!g_fit.ok240&&!getenv("REVA_VIDEOFLIP")){ printf("[video] 240p wanted, and this set hasn't been asked: staying in 480i\n"); fflush(stdout); return; }
        videoSwitch(m,W,H); }
    /* REVA_VIDEOFLIP=<seconds>: a check that mode changes hold up, between 480i and 240p only */
    { static int on=-1; static long ticks=0; static long every=0; if(on<0){ const char*e=getenv("REVA_VIDEOFLIP"); on=(e&&e[0])?1:0; every=on?(long)(atof(e)*60.0):0; }
      if(on&&every>0&&++ticks>=every){ ticks=0; g_videoWant=(g_vNow==1)?0:1; } } }

/* ---- SETUP: the short flow a first boot walks through, and the pause menu's SCREEN SETUP. It draws on the TV's grid, says each step out loud, and takes about three minutes.
 * ⛔ Rules from the research, for any set: a test pattern is dim and doesn't stay still for long; nothing sits at full white; and every trial of a mode goes back by itself. */
enum { SU_SEE, SU_SEE50, SU_OFFER, SU_KEEP, SU_EDGE, SU_ROUND, SU_BLACK, SU_WHITE, SU_READ, SU_DONE, SU_PATTERN, SU_END };
static int g_suVoice=-1;
static void suSay(const char*name){
    static int slot[16]; static const char*names[16]; int s=-1,n=0;
    for(;n<16&&names[n];n++) if(!strcmp(names[n],name)){ s=slot[n]; break; }
    if(s<0&&n<16&&!names[n]){ char p[320]; SDL_AudioSpec sp; Uint8*buf=NULL; Uint32 len=0;
        snprintf(p,sizeof p,"voice/%s.wav",name); int ok=SDL_LoadWAV(p,&sp,&buf,&len)!=NULL;
        if(!ok&&g_argv&&g_argv[0]){ const char*e=g_argv[0]; const char*q=strrchr(e,'/'); const char*q2=strrchr(e,'\\'); if(q2>q) q=q2;
            if(q){ snprintf(p,sizeof p,"%.*s/voice/%s.wav",(int)(q-e),e,name); ok=SDL_LoadWAV(p,&sp,&buf,&len)!=NULL; } }
        if(ok){ clubOpened(p); s=sfxBankFromWav(name,buf,len,&sp); SDL_FreeWAV(buf); }
        else printf("[setup] no voice for %s\n",name);
        names[n]=name; slot[n]=s; }
    if(g_suVoice>=0) sfxStop(g_suVoice);
    g_suVoice=(s>=0)?sfxPlaySlot(s,1.f,NULL,0):-1; }
static void suRect(float x0,float y0,float x1,float y1,float r,float g,float b,int W,int H){ float v[12]; tvRect(v,x0,y0,x1,y1); hudTris(v,6,(float)W,(float)H,r,g,b,1.f); }
static void suFrameRect(float x0,float y0,float x1,float y1,float t,float r,float g,float b,int W,int H){
    suRect(x0,y0,x1,y0+t,r,g,b,W,H); suRect(x0,y1-t,x1,y1,r,g,b,W,H); suRect(x0,y0,x0+t,y1,r,g,b,W,H); suRect(x1-t,y0,x1,y1,r,g,b,W,H); }
/* a block that keeps moving: on a signal check it's what says the picture is live, and it means nothing here ever holds still */
static void suMover(long t,int W,int H,float c){ float A=g_dispAspect>0.f?g_dispAspect:(float)W/(float)H, s=(float)H*0.16f, sx=s*(float)W/((float)H*A);
    float px=(float)fabs(fmod((double)t*2.0,(double)(W-(int)sx)*2.0)-(double)(W-(int)sx)), py=(float)fabs(fmod((double)t*1.0,(double)(H-(int)s)*2.0)-(double)(H-(int)s));
    suRect(px,py,px+sx,py+s,0.2f*c,0.55f*c,0.75f*c,W,H); }
static void suCircle(float cx,float cy,float rx,float ry,float t,float r,float g,float b,int W,int H){ float v[12*64]; float*o=v;
    for(int i=0;i<64;i++){ float a0=(float)i/64.f*6.28318f,a1=(float)(i+1)/64.f*6.28318f,c0=cosf(a0),s0=sinf(a0),c1=cosf(a1),s1=sinf(a1),tx=t*rx/ry;
        float ax=cx+c0*rx,ay=cy+s0*ry,bx=cx+c1*rx,by=cy+s1*ry,cx0=cx+c1*(rx-tx),cy0=cy+s1*(ry-t),dx=cx+c0*(rx-tx),dy=cy+s0*(ry-t);
        *o++=ax;*o++=ay;*o++=bx;*o++=by;*o++=cx0;*o++=cy0; *o++=ax;*o++=ay;*o++=cx0;*o++=cy0;*o++=dx;*o++=dy; }
    hudTris(v,64*6,(float)W,(float)H,r,g,b,1.f); }
/* the test patterns, written from what the 240p Test Suite's manual says each is for. Nothing above 75% but the three small patches */
static const char*SU_PAT[4]={"GRID: EVERY LINE STRAIGHT","BARS: CLEAN AND IN ORDER","STRIPES: ONE PIXEL AND TWO","MOTION: SOLID, NO COMBING"};
static void suPattern(int p,long t,int W,int H){
    if(p==0){ for(int x=0;x<=W;x+=W/18) suRect((float)x,0.f,(float)x+2.f,(float)H,0.6f,0.6f,0.6f,W,H);
              for(int y=0;y<=H;y+=H/12) suRect(0.f,(float)y,(float)W,(float)y+(H>300?2.f:1.f),0.6f,0.6f,0.6f,W,H); }
    else if(p==1){ static const float C[7][3]={{.75f,.75f,.75f},{.75f,.75f,0},{0,.75f,.75f},{0,.75f,0},{.75f,0,.75f},{.75f,0,0},{0,0,.75f}};
              for(int i=0;i<7;i++) suRect((float)W*i/7.f,(float)H*0.15f,(float)W*(i+1)/7.f,(float)H*0.8f,C[i][0],C[i][1],C[i][2],W,H); }
    else if(p==2){ for(int x=W/8;x<W/2;x+=2) suRect((float)x,(float)H*0.2f,(float)x+1.f,(float)H*0.75f,0.6f,0.6f,0.6f,W,H);
              for(int x=W/2;x<W*7/8;x+=4) suRect((float)x,(float)H*0.2f,(float)x+2.f,(float)H*0.75f,0.6f,0.6f,0.6f,W,H); }
    else { float s=(float)(W/18),off=(float)(t%(long)(s*2.f));
              for(int y=0;y<6;y++) for(int x=-1;x<9;x++) if((x+y)&1) suRect(off+(float)x*s*2.f,(float)H*0.2f+(float)y*(float)H*0.1f,off+(float)x*s*2.f+s*2.f,(float)H*0.2f+(float)(y+1)*(float)H*0.1f,0.55f,0.55f,0.55f,W,H); } }
/* one run of setup. first: 1 is a first boot, which starts by asking if there's a picture at all. 0 is the whole of it from the menu, and 2 is only the reading size */
static void setupRun(int*W,int*H,int first){
    printf("[setup] %s\n",first==1?"first boot: asking about this screen":first==2?"the size of the text, from the menu":"asked for from the menu"); fflush(stdout);
    sfxInit();
    scrApply(*W,*H);
    int st=first==2?SU_READ:first?SU_SEE:SU_OFFER, enter=1, pa=1,pb=1,px=1,sv=0,sh=0,edge=0,pat=0,readSel=g_fit.reading; long t=0,inSt=0,lastSay=0; int back=g_vNow;
    static long ex9=-2; if(ex9==-2){ const char*e=getenv("REVA_EXITTICK"); ex9=e?atol(e):-1; }
#ifdef REVA_VK
    const char*shots=getenv("REVA_SETUPSHOT");
#endif
    while(st!=SU_END&&g_run){
        bridgePoll();
        t++; g_tickTotal++; double tsec=(double)t/60.0;
        jsPoll(tsec); kbPoll(tsec);
#ifdef REVA_SDL
        SDL_PumpEvents();
#endif
        inputTape(tsec); bridgeInput();
        /* the stick as steps: one on the push, then a run while it's held */
        int vx=(g_lx<-0.5f)?1:(g_lx>0.5f)?-1:0, vy=(g_ly<-0.5f)?-1:(g_ly>0.5f)?1:0;
        { static long hold=0; if(vx||vy) hold++; else hold=0; sv=(vy&&(hold==1||(hold>18&&hold%3==0)))?vy:0; sh=(vx&&(hold==1||(hold>18&&hold%3==0)))?vx:0; }
        int a=g_btnA&&!pa, b=g_btnB&&!pb, x=g_btnX&&!px; pa=g_btnA; pb=g_btnB; px=g_btnX;
        int w=*W,h=*H,m=vmOfSize(w,h); if(m<0) m=0;
        if(enter){ enter=0; inSt=0; lastSay=t; static const char*SAY[SU_END]={"setup_see","setup_see","setup_240","setup_keep","setup_edges","setup_round","setup_black","setup_white","setup_read","setup_done",NULL};
            if(SAY[st]) suSay(SAY[st]);
            printf("[setup] step %d\n",st); fflush(stdout); }
        inSt++;
        gfxFrameBegin();
        /* a screen left alone for two minutes goes dim */
        float dim=(inSt>7200&&st!=SU_SEE)?0.45f:1.f;
        switch(st){
        case SU_SEE:
            suMover(t,w,h,1.f);
            tvTextC(9,2,"CAN YOU SEE THIS?",0.95f,0.95f,0.95f);
            tvTextC(13,1,"PRESS ANY BUTTON",0.95f,0.9f,0.4f);
            if(t-lastSay>480){ lastSay=t; suSay((g_fit.allow50&&inSt>720)?"setup_50":"setup_see"); }
            /* ⛔ 50 Hz is behind allow50 until a PAL set has tested it: a set like jacob's must never see it by accident. With it on, a long hold is the way there, and a tap still means "I can see this" */
            if(g_fit.allow50){ static long held=0; int any=g_btnA||g_btnB||g_btnX||g_btnStart;
                if(inSt>720) tvTextC(17,1,"NO PICTURE? HOLD A BUTTON",0.8f,0.8f,0.8f);
                if(any) held++;
                else { if(held>0&&held<180){ st=SU_OFFER; enter=1; } held=0; }
                if(held>=180&&inSt>720){ held=0; back=m; videoSwitch(2,W,H); st=SU_SEE50; enter=1; } }
            else if(a||b||x||g_btnStart){ st=SU_OFFER; enter=1; }
            break;
        case SU_SEE50: { long left=12-inSt/60;   /* no press and it goes back by itself: this set may show nothing at all */
            suMover(t,w,h,1.f);
            tvTextC(9,2,"CAN YOU SEE THIS?",0.95f,0.95f,0.95f);
            tvTextC(13,1,"PRESS ANY BUTTON",0.95f,0.9f,0.4f);
            if(inSt>30&&(g_btnA||g_btnB||g_btnX||g_btnStart)){ g_fit.mode=2; st=SU_OFFER; enter=1; }
            else if(left<0){ videoSwitch(back<0?0:back,W,H); st=SU_SEE; enter=1; }
            break; }
        case SU_OFFER:
            suMover(t,w,h,0.6f);
            tvTextC(6,2,"A SHARPER PICTURE?",0.95f,0.95f,0.95f);
            tvTextC(10,1,"240P LOOKS SHARPER ON A TUBE TV.",0.8f,0.8f,0.8f);
            tvTextC(11,1,"SOME FLAT TVS WON'T SHOW IT.",0.8f,0.8f,0.8f);
            tvTextC(14,1,"A TRY IT   B NOT NOW",0.95f,0.9f,0.4f);
            if(a){ back=m; videoSwitch(VM[m].hz==50?3:1,W,H); st=SU_KEEP; enter=1; }
            else if(b){ g_fit.ok240=0; g_fit.mode=(VM[m].hz==50)?2:0; st=SU_EDGE; enter=1; edge=0; }
            break;
        case SU_KEEP: { long left=15-inSt/60; char n[24]; snprintf(n,sizeof n,"%ld",left<0?0:left);
            suMover(t,w,h,0.6f);
            tvTextC(6,2,"KEEP THIS PICTURE?",0.95f,0.95f,0.95f);
            tvTextC(10,2,n,0.95f,0.9f,0.4f);
            tvTextC(14,1,"A KEEP IT   B GO BACK",0.95f,0.9f,0.4f);
            if(a){ g_fit.ok240=1; g_fit.mode=m; st=SU_EDGE; enter=1; edge=0; }
            else if(b||left<0){ g_fit.ok240=0; g_fit.mode=(back<0)?0:back; videoSwitch(back<0?0:back,W,H); st=SU_EDGE; enter=1; edge=0; }
            break; }
        case SU_EDGE: { int*e=g_fit.edge[m]; int lim[4]={w/6,w/6,h/6,h/6}; int d=(edge<2)?sh:sv; if(edge==1||edge==3) d=-d;
            e[edge]+=d; if(e[edge]<0) e[edge]=0; if(e[edge]>lim[edge]) e[edge]=lim[edge];
            float L=(float)e[0],R=(float)(w-e[1]),T=(float)e[2],B=(float)(h-e[3]),th=(h>300)?2.f:1.f;
            suFrameRect(L,T,R,B,th,0.6f*dim,0.6f*dim,0.6f*dim,w,h);
            { static const float tri=10.f; float c=0.75f*dim;   /* a bright bar on the edge being moved */
              if(edge==0) suRect(L,T,L+tri,B,c,c*0.9f,c*0.3f,w,h); else if(edge==1) suRect(R-tri,T,R,B,c,c*0.9f,c*0.3f,w,h);
              else if(edge==2) suRect(L,T,R,T+tri*(h>300?1.f:0.5f),c,c*0.9f,c*0.3f,w,h); else suRect(L,B-tri*(h>300?1.f:0.5f),R,B,c,c*0.9f,c*0.3f,w,h); }
            static const char*EN[4]={"THE LEFT EDGE","THE RIGHT EDGE","THE TOP EDGE","THE BOTTOM EDGE"};
            tvTextC(8,2,EN[edge],0.95f*dim,0.95f*dim,0.95f*dim);
            tvTextC(12,1,"MOVE IT UNTIL YOU CAN SEE ALL OF IT",0.8f*dim,0.8f*dim,0.8f*dim);
            tvTextC(15,1,edge<2?"STICK LEFT AND RIGHT   A NEXT":"STICK UP AND DOWN   A NEXT",0.95f*dim,0.9f*dim,0.4f*dim);
            g_fit.fit=1;
            if(a){ if(++edge>=4){ st=SU_ROUND; enter=1; } else inSt=0; }
            break; }
        case SU_ROUND: { g_fit.stretch+=0.002f*(float)sh; if(g_fit.stretch<0.92f) g_fit.stretch=0.92f; if(g_fit.stretch>1.08f) g_fit.stretch=1.08f;
            float asp=(g_dispAspect>0.f?15.f/11.f:(float)w/(float)h)*g_fit.stretch, ry=(float)h*(g_tvRead?0.22f:0.28f), rx=ry*(float)w/((float)h*asp), cy9=(float)h*(g_tvRead?0.27f:0.38f);   /* bigger letters want more of the screen: the test works at any size */
            suCircle((float)w*0.5f,cy9,rx,ry,(h>300)?4.f:2.f,0.6f*dim,0.6f*dim,0.6f*dim,w,h);
            suFrameRect((float)w*0.5f-rx,cy9-ry,(float)w*0.5f+rx,cy9+ry,(h>300)?2.f:1.f,0.4f*dim,0.4f*dim,0.4f*dim,w,h);
            tvTextC(19,1,"MAKE THE CIRCLE ROUND",0.95f*dim,0.95f*dim,0.95f*dim);
            tvTextC(21,1,"STICK LEFT AND RIGHT   A NEXT",0.95f*dim,0.9f*dim,0.4f*dim);
            if(a){ st=SU_BLACK; enter=1; }
            break; }
        case SU_BLACK: { /* BT.814's logic: one stripe just shows and its neighbour clearly does. The stripes are 1.8% and 4%: RGB 5 and 10 */
            suRect((float)w*0.30f,(float)h*0.1f,(float)w*0.45f,(float)h*(g_tvRead?0.44f:0.65f),5.f/255.f,5.f/255.f,5.f/255.f,w,h);
            suRect((float)w*0.55f,(float)h*0.1f,(float)w*0.70f,(float)h*(g_tvRead?0.44f:0.65f),10.f/255.f,10.f/255.f,10.f/255.f,w,h);
            tvTextC(17,1,"TURN YOUR TV'S BRIGHTNESS UNTIL",0.8f*dim,0.8f*dim,0.8f*dim);
            tvTextC(18,1,"THE LEFT STRIPE JUST SHOWS",0.8f*dim,0.8f*dim,0.8f*dim);
            tvTextC(21,1,"A NEXT",0.95f*dim,0.9f*dim,0.4f*dim);
            if(a){ st=SU_WHITE; enter=1; }
            break; }
        case SU_WHITE: { /* three small patches, never a white field: all three stay different, and the picture doesn't grow */
            static const float PW[3]={0.90f,0.95f,1.f};
            for(int i=0;i<3;i++){ float c=PW[i]*(dim<1.f?0.5f:1.f); suRect((float)w*(0.26f+0.18f*i),(float)h*0.25f,(float)w*(0.26f+0.18f*i+0.12f),(float)h*0.45f,c,c,c,w,h); }
            tvTextC(15,1,"ALL THREE SHOULD LOOK DIFFERENT",0.8f*dim,0.8f*dim,0.8f*dim);
            tvTextC(16,1,"IF NOT, TURN THE CONTRAST DOWN",0.8f*dim,0.8f*dim,0.8f*dim);
            tvTextC(19,1,"A NEXT",0.95f*dim,0.9f*dim,0.4f*dim);
            if(a){ st=SU_READ; enter=1; }
            break; }
        case SU_READ: { /* a short line at each size, and the player picks the smallest they can read. Each says something different, so a big one can't give a small one away, and all three are the same brightness. What this screen says for itself is at the biggest size, and out loud */
            static const char*RL[3]={"SCORE 83906","LEVEL 5 OF 8","2 SHIPS LEFT"};
            if(sv) readSel=(readSel+sv+3)%3;
            for(int i=0;i<3;i++){ const int row9=3+i*2; tvUse(2);
                const int y9=g_tvY0+row9*8*g_tvSy,h9=8*g_tvSy;
                if(i==readSel){ tvDraw(0.f,(float)row9,1,">",0.95f*dim,0.9f*dim,0.4f*dim); tvDraw((float)g_tvCols-1.f,(float)row9,1,"<",0.95f*dim,0.9f*dim,0.4f*dim); }
                tvUse(i);
                tvDraw(((float)g_tvCols-(float)strlen(RL[i]))*0.5f,(float)(y9+(h9-8*g_tvSy)/2-g_tvY0)/(float)(8*g_tvSy),1,RL[i],0.95f*dim,0.95f*dim,0.95f*dim); }
            tvUse(2);
            tvDraw(0.f,0.f,1,"PICK THE SMALLEST",0.95f*dim,0.95f*dim,0.95f*dim);
            tvDraw(2.5f,1.f,1,"YOU CAN READ",0.95f*dim,0.95f*dim,0.95f*dim);
            tvDraw(0.5f,10.f,1,"STICK PICK  A OK",0.95f*dim,0.9f*dim,0.4f*dim);
            tvUse(g_tvRead);
            if(a){ g_fit.reading=g_tvRead=readSel; tvUse(readSel); printf("[setup] %s letters: the grid is %d x %d\n",TV_READ[readSel].name,g_tvCols,g_tvRows); fflush(stdout); st=(first==2)?SU_END:SU_DONE; enter=1; }
            else if(b&&first==2) st=SU_END;
            break; }
        case SU_DONE:
            suMover(t,w,h,0.5f);
            tvTextC(8,2,"ALL SET",0.95f,0.95f,0.95f);
            tvTextC(12,1,"HOLD A BUTTON AT POWER-ON",0.8f,0.8f,0.8f);
            tvTextC(13,1,"TO DO THIS AGAIN",0.8f,0.8f,0.8f);
            tvTextC(16,1,"A PLAY   X TEST PATTERNS",0.95f,0.9f,0.4f);
            if(a) st=SU_END;
            else if(x){ st=SU_PATTERN; enter=1; pat=0; }
            break;
        case SU_PATTERN:
            suPattern(pat,t,w,h);
            tvTextC(0,1,SU_PAT[pat],0.6f,0.6f,0.6f);
            tvTextC(23,1,"A NEXT   B DONE",0.95f,0.9f,0.4f);
            if(a){ pat=(pat+1)%4; inSt=0; }
            if(b||inSt>3600){ st=SU_DONE; enter=1; }   /* a pattern gets a minute */
            break;
        default: st=SU_END; }
        tvFlush();
        { GfxView v9; memset(&v9,0,sizeof v9); for(int i=0;i<16;i+=5){ v9.vp[i]=1.f; v9.sunVP[i]=1.f; } v9.knee=1.f; v9.tsec=(float)tsec; v9.tick=g_tickTotal; v9.reset=enter; gfxFrameEnd(&v9); }
        bridgeFrameDone(w,h);
#ifdef REVA_VK
        if(shots&&inSt==30){ unsigned char*px9=(unsigned char*)malloc((size_t)w*h*4);   /* REVA_SETUPSHOT=<folder>: one picture of each step, with REVA_SHOT set so the renderer keeps a copy */
            if(px9&&gfxRead(px9,w,h)){ char p9[400]; snprintf(p9,sizeof p9,"%s/setup_%d_%d.ppm",shots,st,st==SU_PATTERN?pat:edge); FILE*f9=fopen(p9,"wb");
                if(f9){ fprintf(f9,"P6\n%d %d\n255\n",w,h); for(int y=h-1;y>=0;y--) for(int xx=0;xx<w;xx++) fwrite(px9+((size_t)y*w+xx)*4,1,3,f9); fclose(f9); } }
            free(px9); }
#endif
#if defined(REVA_SDL)&&!defined(REVA_HEADLESS)
        if(g_fixedStepMs<=0.0) SDL_Delay(16);   /* a window's present doesn't pace; the direct road's flip does */
#endif
        if(ex9>0&&g_tickTotal>=ex9){ fflush(stdout); exit(0); }
    }
    if(first!=2) g_fit.found=1;
    if(g_dispAspect>0.f&&!getenv("REVA_ASPECT")) g_dispAspect=15.f/11.f*g_fit.stretch;
    scrSave();
    scrApply(*W,*H);
    if(g_suVoice>=0){ sfxStop(g_suVoice); g_suVoice=-1; }
    printf("[setup] done: %s, 240p %s, stretch %.3f\n",VM[g_fit.mode].name,g_fit.ok240?"kept":"not",g_fit.stretch); fflush(stdout); }
/* a first boot, or recovery's: on the tube, with nobody having named a mode. A held button at power-on starts it over */
static void setupAtBoot(int*W,int*H,int composite){
    if(getenv("REVA_SETUP")&&getenv("REVA_SETUP")[0]=='1'){ setupRun(W,H,1); return; }
    if(!composite||getenv("REVA_NOSETUP")||(getenv("REVA_VIDEO")&&getenv("REVA_VIDEO")[0])||getenv("REVA_REPLAY")||getenv("REVA_FIXEDSTEP")) return;
    scrBootCount();
    for(int i=0;i<25;i++){ jsPoll((double)i*0.01);
#ifndef REVA_SDL
        usleep(10000);
#endif
    }
    if(g_btnStart||(g_btnA&&g_btnB)){ printf("[screen] a button held at power-on: setup again\n"); fflush(stdout); remove("save/display.txt"); scrDefaults(); g_fitReset=1; g_btnStart=g_btnA=g_btnB=0; }
    if(!g_fit.found||g_fitReset) setupRun(W,H,1); }
