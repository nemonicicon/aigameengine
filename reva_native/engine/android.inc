/* android.inc -- (11-06) THE ENGINE ON A PHONE. Android is the SDL road (REVA_SDL) on
 * Linux, and SDL2's own Java (org.libsdl.app.SDLActivity) starts the engine as libmain.so's SDL_main. What's here is what a phone needs that a PC doesn't:
 *   androidBoot   at the top of main: the log to a file in the app's own storage (adb: run-as <package> cat files/log.txt), the game's data out of the
 *                 APK into that storage the first time (and again when the APK's stamp changes), the working folder there, and play.bat's arguments
 *   touchPoll     the touch controls, read once a tick with the pad and the keys: a stick wherever the left thumb lands, a look stick wherever the right
 *                 thumb lands off a button, and the pad's buttons drawn on the right; they write the same globals the pad does
 *   touchDraw     the controls drawn over the frame, last, before every gfxFrameEnd
 * The packaging (the APK, its manifest, its key) is reva_native/android/build_apk.sh. Built for and run on a phone only: none of this is in a PC build. */
#include <android/log.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ftw.h>
#include <dirent.h>
#include <errno.h>

static void andSay(const char*s){ __android_log_print(ANDROID_LOG_INFO,"reva","%s",s); }
/* ---------------------------------------------------------------- (11-06) a phone never closes the game without saying why:
 * a crash writes its signal and where it was to the log and leaves a marker, and the next start shows them; an
 * exit before the game's loop has run shows the log's last lines first; a renderer that can't start says so (gfx_vk.c). Each is a box to screenshot */
#include <signal.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <unwind.h>
static char g_andBase[512];
static volatile int g_andRunning;   /* the game's loop has run (touchPoll): after this an exit is the game's own */
static void andTail(const char*path,char*out,size_t cap,int lines){   /* a file's last lines */
    out[0]=0; FILE*f=fopen(path,"rb"); if(!f) return;
    fseek(f,0,SEEK_END); long sz=ftell(f); long from=sz>6000?sz-6000:0; fseek(f,from,SEEK_SET);
    static char b[6100]; size_t n=fread(b,1,sizeof b-1,f); b[n]=0; fclose(f);
    int nl=0; char*s=b+n; while(s>b&&nl<=lines){ s--; if(*s=='\n') nl++; }
    if(*s=='\n') s++;
    snprintf(out,cap,"%s",s); }
static void andBox(const char*title,const char*text){
    __android_log_print(ANDROID_LOG_ERROR,"reva","%s: %s",title,text);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,title,text,NULL); }
typedef struct { uintptr_t pc[24]; int n; } AndTrace;
static _Unwind_Reason_Code andUnwind(struct _Unwind_Context*c,void*arg){ AndTrace*t=(AndTrace*)arg; uintptr_t pc=_Unwind_GetIP(c); if(pc&&t->n<24) t->pc[t->n++]=pc; return t->n<24?_URC_NO_REASON:_URC_END_OF_STACK; }
static void andCrash(int sig,siginfo_t*si,void*uc){
    (void)uc; char m[2048]; int k=0;
    k+=snprintf(m+k,sizeof m-k,"signal %d (%s) at address %p\n",sig,sig==SIGSEGV?"a bad memory read or write":sig==SIGABRT?"an abort":sig==SIGBUS?"a bus error":sig==SIGFPE?"arithmetic":sig==SIGILL?"an illegal instruction":"?",si?si->si_addr:NULL);
    AndTrace t; t.n=0; _Unwind_Backtrace(andUnwind,&t);
    for(int i=0;i<t.n&&k<(int)sizeof m-160;i++){ Dl_info di; const char*fn="?"; const char*lib="?"; uintptr_t off=0;
        if(dladdr((void*)t.pc[i],&di)){ fn=di.dli_sname?di.dli_sname:"?"; lib=di.dli_fname?strrchr(di.dli_fname,'/')?strrchr(di.dli_fname,'/')+1:di.dli_fname:"?"; off=t.pc[i]-(uintptr_t)di.dli_fbase; }
        k+=snprintf(m+k,sizeof m-k,"  %s %s +0x%lx\n",lib,fn,(unsigned long)off); }
    if(write(1,"[android] CRASHED: ",19)<0){} if(write(1,m,(size_t)k)<0){}
    char mk[600]; snprintf(mk,sizeof mk,"%s/crashed.txt",g_andBase); int fd=open(mk,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd>=0){ if(write(fd,m,(size_t)k)<0){} close(fd); }
    signal(sig,SIG_DFL); raise(sig); }
static void andAtExit(void){
    if(g_andRunning) return;
    char p[600],tail[1600]; snprintf(p,sizeof p,"%s/log.txt",g_andBase); fflush(stdout); andTail(p,tail,sizeof tail,14);
    andBox("The game stopped while starting",tail[0]?tail:"(the log is empty)"); }
static int andRm(const char*p,const struct stat*sb,int tf,struct FTW*fw){ (void)sb; (void)tf; (void)fw; return remove(p); }
static void andMkdirs(char*path){   /* every folder on the way to a file */
    for(char*s=path+1;*s;s++) if(*s=='/'){ *s=0; mkdir(path,0755); *s='/'; } }

static void androidBoot(int*argc,char***argv){
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");             /* a touch is a touch: no click (the pad's A) and no mouse look made up from it */
    SDL_SetHint(SDL_HINT_ORIENTATIONS,"LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON,"1");       /* the phone's back is the pad's Start (touchPoll), not the end of the game */
    const char*base=SDL_AndroidGetInternalStoragePath();
    if(!base){ andSay("no internal storage path"); return; }
    snprintf(g_andBase,sizeof g_andBase,"%s",base);
    char p[1024];
    /* the last run crashed: say how, with its log's last lines, before anything else */
    { char mk[600]; snprintf(mk,sizeof mk,"%s/crashed.txt",base); FILE*cf=fopen(mk,"rb");
      if(cf){ static char what[2100]; size_t n=fread(what,1,sizeof what-1,cf); what[n]=0; fclose(cf); remove(mk);
          char lp[600],tail[1400]; snprintf(lp,sizeof lp,"%s/log.txt",base); andTail(lp,tail,sizeof tail,10);
          static char box[3600]; snprintf(box,sizeof box,"%s\nThe log's last lines:\n%s",what,tail); andBox("The game crashed last time",box); } }
    { char prev[600]; snprintf(p,sizeof p,"%s/log.txt",base); snprintf(prev,sizeof prev,"%s/log_prev.txt",base); rename(p,prev); }
    snprintf(p,sizeof p,"%s/log.txt",base);
    if(freopen(p,"w",stdout)){ setvbuf(stdout,NULL,_IOLBF,0); dup2(fileno(stdout),2); }
    printf("[android] the app's storage: %s\n",base);
    { static char altstack[65536]; stack_t ss; ss.ss_sp=altstack; ss.ss_size=sizeof altstack; ss.ss_flags=0; sigaltstack(&ss,NULL);
      struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_sigaction=andCrash; sa.sa_flags=SA_SIGINFO|SA_ONSTACK; sigemptyset(&sa.sa_mask);
      const int sigs[]={SIGSEGV,SIGBUS,SIGFPE,SIGILL,SIGABRT}; for(unsigned i=0;i<sizeof sigs/sizeof sigs[0];i++) sigaction(sigs[i],&sa,NULL); }
    atexit(andAtExit);
    { SDL_version v; SDL_GetVersion(&v); printf("[android] SDL %d.%d.%d\n",v.major,v.minor,v.patch); }
    /* the APK's data: its stamp says which build it is. SDL_RWFromFile on a relative path looks in the app's storage first and then the APK, so the data
     * is unpacked into a folder named for the stamp (never at the top of the storage, where a stale file would be read in the APK's place) */
    char stamp[96]="";
    { SDL_RWops*rw=SDL_RWFromFile("apk_stamp.txt","rb"); if(rw){ size_t n=SDL_RWread(rw,stamp,1,sizeof stamp-1); stamp[n]=0; SDL_RWclose(rw); } }
    for(char*s=stamp;*s;s++) if(!isalnum((unsigned char)*s)&&*s!='_'&&*s!='-') *s=0;
    if(!stamp[0]){ printf("[android] the APK has no apk_stamp.txt: no data to unpack\n"); fflush(stdout); return; }
    char dir[1024]; snprintf(dir,sizeof dir,"%s/v_%s",base,stamp);
    snprintf(p,sizeof p,"%s/.unpacked",dir);
    if(access(p,F_OK)!=0){
        andBox("First start","The game's data is unpacked onto the phone now. It takes a little while with a black screen: leave it running.");
        const Uint32 t0=SDL_GetTicks();
        /* an older build's data goes first: the phone keeps one copy */
        { DIR*dd=opendir(base); struct dirent*de9;
          while(dd&&(de9=readdir(dd))){ if(strncmp(de9->d_name,"v_",2)) continue; char old[1400]; snprintf(old,sizeof old,"%s/%s",base,de9->d_name);
              printf("[android] an older build's data goes: %s\n",de9->d_name); nftw(old,andRm,16,FTW_DEPTH|FTW_PHYS); }
          if(dd) closedir(dd); }
        mkdir(dir,0755);
        SDL_RWops*lr=SDL_RWFromFile("files.txt","rb");
        if(!lr){ printf("[android] the APK has no files.txt\n"); fflush(stdout); return; }
        const Sint64 ln=SDL_RWsize(lr); char*list=(char*)malloc((size_t)ln+1); SDL_RWread(lr,list,1,(size_t)ln); list[ln]=0; SDL_RWclose(lr);
        static char buf[1<<20]; long files=0; double bytes=0.0;
        for(char*line=strtok(list,"\r\n");line;line=strtok(NULL,"\r\n")){
            if(!line[0]) continue;
            SDL_RWops*in=SDL_RWFromFile(line,"rb");
            if(!in){ printf("[android] missing from the APK: %s\n",line); continue; }
            char dst[1400]; snprintf(dst,sizeof dst,"%s/%s",dir,line); andMkdirs(dst);
            FILE*o=fopen(dst,"wb");
            if(!o){ printf("[android] can't write %s: %s\n",dst,strerror(errno)); SDL_RWclose(in); continue; }
            size_t n; while((n=SDL_RWread(in,buf,1,sizeof buf))>0){ fwrite(buf,1,n,o); bytes+=(double)n; }
            fclose(o); SDL_RWclose(in); files++;
            if(files%50==0){ printf("[android] unpacked %ld files, %.0f MB\n",files,bytes/1048576.0); fflush(stdout); } }
        free(list);
        FILE*mk=fopen(p,"w"); if(mk){ fputs(stamp,mk); fclose(mk); }
        printf("[android] unpacked %ld files, %.0f MB, in %.1f s\n",files,bytes/1048576.0,(SDL_GetTicks()-t0)/1000.0); fflush(stdout);
    }
    if(chdir(dir)!=0) printf("[android] can't work in %s: %s\n",dir,strerror(errno));
    else printf("[android] working in %s\n",dir);
    if(*argc<3){   /* (11-13) THE GAME'S OWN START ARGUMENTS, FROM ITS DATA: android_args.txt, one line of what its launcher says after the exe ("12 packs
                    * packs/v1_sample.rpack"). Until then one game's were written here, so every game's APK started as that one. Without the file: none of the
                    * pool and the game's packs folder, which is how a game with no bodies of its own starts */
        static char line[256],a0[]="game",z9[]="0",p9[]="packs"; static char*av[9]; int n=1,had=0; av[0]=a0;
        FILE*af=fopen("android_args.txt","r");
        if(af){ had=1; if(fgets(line,sizeof line,af)){ for(char*t=strtok(line," \t\r\n");t&&n<8;t=strtok(NULL," \t\r\n")) av[n++]=t; } fclose(af); }
        if(n<3){ av[1]=z9; av[2]=p9; n=3; }
        av[n]=NULL; *argc=n; *argv=av;
        printf("[android] starting with %d argument(s)%s\n",n-1,had?", from android_args.txt":": no android_args.txt, so none of the pool and the game's packs"); }
    fflush(stdout);
}

/* ---------------------------------------------------------------- the touch controls */
static int g_tW=1280,g_tH=720;   /* the drawable, for touchDraw (touchScreen, after gfxInit) */
static void touchScreen(int W,int H){ g_tW=W>0?W:1280; g_tH=H>0?H:720; }
typedef struct { SDL_FingerID id; int role; float ox,oy,x,y; int alive; } TFinger;   /* role: 1 the move stick, 2 the look stick, 10+ a button */
#define TF_MAX 10
static TFinger g_tf[TF_MAX];
static int g_touchUsed;   /* a finger has been on the glass: the controls are drawn from then on */
/* the buttons: where (in the screen's height from its right and bottom edges, or from its top for the shoulder row), how big, what, and its colour */
typedef struct { float rx,ry; int fromTop; float r; unsigned bit; const char*lab; float c[3]; } TButton;
#define TB_RT 0x10000u
#define TB_START 0x20000u
static const TButton g_tb[]={
    {0.20f,0.10f,0,0.075f,PAD_A,"A",{0.20f,0.75f,0.30f}}, {0.09f,0.22f,0,0.075f,PAD_B,"B",{0.85f,0.25f,0.22f}},
    {0.31f,0.22f,0,0.075f,PAD_X,"X",{0.25f,0.45f,0.90f}}, {0.20f,0.34f,0,0.075f,PAD_Y,"Y",{0.95f,0.80f,0.20f}},
    {0.44f,0.10f,0,0.060f,PAD_LB,"LB",{0.70f,0.70f,0.72f}}, {0.09f,0.10f,1,0.060f,PAD_RB,"RB",{0.70f,0.70f,0.72f}},
    {0.24f,0.10f,1,0.060f,TB_RT,"RT",{0.70f,0.70f,0.72f}}, {0.42f,0.10f,1,0.060f,PAD_BACK,"EYE",{0.55f,0.55f,0.60f}},
    {0.60f,0.10f,1,0.060f,TB_START,"MENU",{0.55f,0.55f,0.60f}},
};
#define TB_N ((int)(sizeof g_tb/sizeof g_tb[0]))
static void tbCentre(int i,float*cx,float*cy){   /* in pixels */
    const TButton*b=&g_tb[i]; const float H=(float)g_tH, W=(float)g_tW;
    *cx=W-b->rx*H; *cy=b->fromTop?b->ry*H:H-b->ry*H; }
static unsigned g_tHeld; static float g_tLx,g_tLy,g_tRx,g_tRy;
#define TSTICK_R 0.13f   /* a stick's reach, in the screen's height */
static void touchPoll(double tsec){
    (void)tsec;
    if(!g_andRunning){ g_andRunning=1; printf("[android] the game's loop is running\n"); fflush(stdout); }
    const float W=(float)g_tW, H=(float)g_tH;
    for(int k=0;k<TF_MAX;k++) g_tf[k].alive=0;
    const int nd=SDL_GetNumTouchDevices();
    for(int d=0;d<nd;d++){ const SDL_TouchID tid=SDL_GetTouchDevice(d); const int nf=SDL_GetNumTouchFingers(tid);
        for(int i=0;i<nf;i++){ SDL_Finger*f=SDL_GetTouchFinger(tid,i); if(!f) continue;
            const float x=f->x*W, y=f->y*H; int slot=-1, fresh=-1;
            for(int k=0;k<TF_MAX;k++){ if(g_tf[k].role&&g_tf[k].id==f->id){ slot=k; break; } if(!g_tf[k].role&&fresh<0) fresh=k; }
            if(slot<0){ if(fresh<0) continue; slot=fresh; TFinger*t=&g_tf[slot]; t->id=f->id; t->ox=x; t->oy=y; g_touchUsed=1;
                /* what it landed on: a button, else the left half's stick or the right half's look */
                int role=0; for(int b=0;b<TB_N;b++){ float cx,cy; tbCentre(b,&cx,&cy); const float rr=g_tb[b].r*H*1.25f; if((x-cx)*(x-cx)+(y-cy)*(y-cy)<rr*rr){ role=10+b; break; } }
                if(!role) role=x<W*0.5f?1:2;
                t->role=role; }
            g_tf[slot].x=x; g_tf[slot].y=y; g_tf[slot].alive=1; } }
    unsigned held=0; float lx=0,ly=0,rx=0,ry=0;
    for(int k=0;k<TF_MAX;k++){ TFinger*t=&g_tf[k];
        if(t->role&&!t->alive){ t->role=0; continue; }
        if(!t->role) continue;
        if(t->role>=10){ held|=g_tb[t->role-10].bit; continue; }
        float dx=(t->x-t->ox)/(TSTICK_R*H), dy=(t->y-t->oy)/(TSTICK_R*H); const float m=hypotf(dx,dy); if(m>1.f){ dx/=m; dy/=m; }
        if(t->role==1){ lx=-dx; ly=dy; }   /* the pad's terms: left is +1, forward is -1 */
        else { rx=dx; ry=dy; } }
    /* the phone's back button is the pad's Start */
    { const Uint8*ks=SDL_GetKeyboardState(NULL); if(ks&&ks[SDL_SCANCODE_AC_BACK]) held|=TB_START; }
    static unsigned prev;
    if((held&TB_START)&&!(prev&TB_START)) g_startEdge=1;
    prev=held; g_tHeld=held; g_tLx=lx; g_tLy=ly; g_tRx=rx; g_tRy=ry;
    if(!held&&lx==0.f&&ly==0.f&&rx==0.f&&ry==0.f) return;
    /* the same globals the pad writes, on top of what the pad and keys said */
    if(lx!=0.f||ly!=0.f){ g_lx=lx; g_ly=ly; }
    if(rx!=0.f||ry!=0.f){ g_rx=rx; g_ry=ry; }
    if(held&PAD_A) g_btnA=1; if(held&PAD_B) g_btnB=1; if(held&PAD_X) g_btnX=1; if(held&PAD_Y) g_btnY=1;
    if(held&PAD_LB) g_btnLB=1; if(held&PAD_RB) g_btnRB=1; if(held&PAD_BACK) g_btnBack=1; if(held&TB_START) g_btnStart=1; if(held&TB_RT) g_rt=1.f;
    if(!g_pause){ if(!g_manual) printf("MANUAL: the player takes the floor (touch)\n"); g_manual=1; g_inT=tsec; }
}
static void tDisc(float*v,int*n,float cx,float cy,float r,int seg){   /* a filled circle as triangles, into v (x,y pairs) */
    for(int i=0;i<seg;i++){ const float a0=6.2831853f*i/seg, a1=6.2831853f*(i+1)/seg;
        v[(*n)*2]=cx; v[(*n)*2+1]=cy; (*n)++;
        v[(*n)*2]=cx+r*cosf(a0); v[(*n)*2+1]=cy+r*sinf(a0); (*n)++;
        v[(*n)*2]=cx+r*cosf(a1); v[(*n)*2+1]=cy+r*sinf(a1); (*n)++; } }
static void tRing(float*v,int*n,float cx,float cy,float r0,float r1,int seg){
    for(int i=0;i<seg;i++){ const float a0=6.2831853f*i/seg, a1=6.2831853f*(i+1)/seg;
        const float x00=cx+r0*cosf(a0),y00=cy+r0*sinf(a0),x01=cx+r1*cosf(a0),y01=cy+r1*sinf(a0),x10=cx+r0*cosf(a1),y10=cy+r0*sinf(a1),x11=cx+r1*cosf(a1),y11=cy+r1*sinf(a1);
        const float q[12]={x00,y00,x01,y01,x11,y11,x00,y00,x11,y11,x10,y10}; memcpy(v+(*n)*2,q,sizeof q); *n+=6; } }
static void tLabel(const char*s,float cx,float cy,float scale,float r,float g,float b,float a){   /* stb_easy_font's quads, as triangles */
    static char qb[20000]; const int nq=stb_easy_font_print(0,0,(char*)s,NULL,qb,sizeof qb);
    const float tw=(float)stb_easy_font_width((char*)s)*scale, th=7.f*scale;
    static float v[6000*2]; int n=0;
    for(int q=0;q<nq&&n+6<6000;q++){ const float*p=(const float*)(qb+q*64);   /* four vertices of x,y,z and a colour, 16 bytes each */
        float X[4],Y[4]; for(int k=0;k<4;k++){ X[k]=cx-tw*0.5f+p[k*4]*scale; Y[k]=cy-th*0.5f+p[k*4+1]*scale; }
        const float t6[12]={X[0],Y[0],X[1],Y[1],X[2],Y[2],X[0],Y[0],X[2],Y[2],X[3],Y[3]}; memcpy(v+n*2,t6,sizeof t6); n+=6; }
    if(n) gfxHud(v,n,(float)g_tW,(float)g_tH,r,g,b,a); }
static void touchDraw(void){
    if(!g_touchUsed&&SDL_GetNumTouchDevices()<=0) return;
    const float W=(float)g_tW, H=(float)g_tH;
    static float v[40000]; int n=0;
    /* the sticks: where a thumb is, its ring and the knob; when none, a faint ring where the left thumb usually lands */
    int moveShown=0;
    for(int k=0;k<TF_MAX;k++){ const TFinger*t=&g_tf[k]; if(t->role!=1&&t->role!=2) continue;
        n=0; tRing(v,&n,t->ox,t->oy,TSTICK_R*H*0.94f,TSTICK_R*H,40); gfxHud(v,n,W,H,1.f,1.f,1.f,0.35f);
        float dx=t->x-t->ox, dy=t->y-t->oy; const float m=hypotf(dx,dy), R=TSTICK_R*H; if(m>R){ dx*=R/m; dy*=R/m; }
        n=0; tDisc(v,&n,t->ox+dx,t->oy+dy,R*0.42f,28); gfxHud(v,n,W,H,1.f,1.f,1.f,0.30f);
        if(t->role==1) moveShown=1; }
    if(!moveShown){ n=0; tRing(v,&n,0.22f*H,H-0.24f*H,TSTICK_R*H*0.94f,TSTICK_R*H,40); gfxHud(v,n,W,H,1.f,1.f,1.f,0.14f); }
    for(int b=0;b<TB_N;b++){ float cx,cy; tbCentre(b,&cx,&cy); const TButton*bt=&g_tb[b]; const int on=(g_tHeld&bt->bit)!=0;
        n=0; tDisc(v,&n,cx,cy,bt->r*H,28); gfxHud(v,n,W,H,bt->c[0],bt->c[1],bt->c[2],on?0.62f:0.30f);
        n=0; tRing(v,&n,cx,cy,bt->r*H*0.9f,bt->r*H,28); gfxHud(v,n,W,H,1.f,1.f,1.f,0.45f);
        tLabel(bt->lab,cx,cy,H*(strlen(bt->lab)>2?0.0042f:0.0065f),1.f,1.f,1.f,0.9f); }
}
