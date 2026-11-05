/* ---- THE BRIDGE (11-03, engine plan Phase 5) ------------------------------------------------------------------------------------------------------------------------
 * An AI on the dev console needs to see what the running game is doing and try things, without a window or a person. REVA_BRIDGE=<port> opens one local TCP port, a line of JSON each way:
 * {"cmd":"step","n":60} and {"ok":true,"tick":1234}. A bare `step 60` works too. `discover` lists everything; anim_bench/bridge.py is the client. A wrong line gets told what a right one looks like.
 * While it's on the clock is fixed, one tick a pass, so a session driven this way replays exactly. It reads the engine and presses buttons: it doesn't reach into a game, and it costs nothing when it's off. */
#include <stdarg.h>
int bridgeOpen(int port); int bridgeLine(char*out,int cap); void bridgeSend(const char*s);
static int g_brOn=0,g_brHold=0,g_brStepWait=0; static long g_brSteps=0;
static struct { long n; float lx,ly; int a,b,x,y,st,seen,live,stWas; } g_brIn[4];   /* each pad's buttons, held for n more ticks */
static char g_brShot[260];
static const char* jsStr(const char*ln,const char*key,char*out,int cap){ char k[48]; snprintf(k,sizeof k,"\"%s\"",key);
    const char*p=strstr(ln,k); if(!p) return NULL; p=strchr(p+strlen(k),':'); if(!p) return NULL; p++; while(*p==' ') p++; if(*p!='"') return NULL; p++;
    int n=0; while(*p&&*p!='"'&&n<cap-1) out[n++]=*p++; out[n]=0; return out; }
static int jsNum(const char*ln,const char*key,double*v){ char k[48]; snprintf(k,sizeof k,"\"%s\"",key);
    const char*p=strstr(ln,k); if(!p) return 0; p=strchr(p+strlen(k),':'); if(!p) return 0; return sscanf(p+1,"%lf",v)==1; }
static void brSay(const char*fmt,...){ char b[2048]; va_list a; va_start(a,fmt); vsnprintf(b,sizeof b,fmt,a); va_end(a); bridgeSend(b); }
static void bridgeDo(const char*ln){
    char cmd[32]="",sv[260]=""; double n=0.0; int hasN=0;
    if(ln[0]=='{'){ jsStr(ln,"cmd",cmd,sizeof cmd); hasN=jsNum(ln,"n",&n); }
    else { int got=sscanf(ln,"%31s %259s",cmd,sv); if(got==2) hasN=(sscanf(sv,"%lf",&n)==1); }
    if(!strcmp(cmd,"discover")||!cmd[0]){
        brSay("{\"ok\":%s,\"commands\":{\"get_state\":\"tick, mode, game, flips, late, the hero\",\"step\":\"n: hold the clock, run n ticks, reply when they're done\",\"run\":\"let the clock go\",\"hold\":\"stop the clock\","
              "\"input\":\"pad (0-3, default 0), x y (the stick, -1..1, up and right positive), a b xb yb start (0 or 1), n ticks\",\"screenshot\":\"path: the next frame as a PPM\",\"hash\":\"the sim's hash, this tick\",\"rng_state\":\"the dice\","
              "\"get\":\"name: hero_x hero_z hero_head game_t\",\"set\":\"name, v: hero_x hero_z hero_head\",\"quit\":\"end the run\"}}",cmd[0]?"true":"false"); return; }
    if(!strcmp(cmd,"get_state")){
        brSay("{\"ok\":true,\"tick\":%ld,\"held\":%d,\"mode\":\"%s\",\"game\":\"%s\",\"raster\":[%d,%d],\"flips\":%ld,\"late\":%ld,\"hero\":[%.4f,%.4f,%.4f],\"paused\":%d}",g_tickTotal,g_brHold,MODE_NAME[g_mode]?MODE_NAME[g_mode]:"?",g_game?g_game->name:"",g_tvW,g_tvH,g_ledTotN,g_ledTotLate,g_rvx,g_rvz,g_rvHead,g_pause); return; }
    if(!strcmp(cmd,"step")){ if(!hasN||n<1.0){ brSay("{\"ok\":false,\"error\":\"step needs a count\",\"try\":\"{\\\"cmd\\\":\\\"step\\\",\\\"n\\\":60}\"}"); return; } g_brHold=1; g_brSteps=(long)n; g_brStepWait=1; return; }
    if(!strcmp(cmd,"hold")){ g_brHold=1; brSay("{\"ok\":true,\"tick\":%ld}",g_tickTotal); return; }
    if(!strcmp(cmd,"run")){ g_brHold=0; g_brSteps=0; brSay("{\"ok\":true,\"tick\":%ld}",g_tickTotal); return; }
    if(!strcmp(cmd,"input")){ double v; int k=jsNum(ln,"pad",&v)?(int)v:0;
        if(k<0||k>3){ brSay("{\"ok\":false,\"error\":\"pads are 0 to 3\",\"try\":\"{\\\"cmd\\\":\\\"input\\\",\\\"pad\\\":1,\\\"a\\\":1,\\\"n\\\":10}\"}"); return; }
        g_brIn[k].seen=1; g_brIn[k].n=(jsNum(ln,"n",&v)&&v>0.0)?(long)v:1; g_brIn[k].lx=jsNum(ln,"x",&v)?(float)-v:0.f; g_brIn[k].ly=jsNum(ln,"y",&v)?(float)-v:0.f;
        g_brIn[k].a=jsNum(ln,"a",&v)&&v!=0.0; g_brIn[k].b=jsNum(ln,"b",&v)&&v!=0.0; g_brIn[k].x=jsNum(ln,"xb",&v)&&v!=0.0; g_brIn[k].y=jsNum(ln,"yb",&v)&&v!=0.0; g_brIn[k].st=jsNum(ln,"start",&v)&&v!=0.0;
        brSay("{\"ok\":true,\"pad\":%d,\"ticks\":%ld}",k,g_brIn[k].n); return; }
    if(!strcmp(cmd,"screenshot")){ if(ln[0]=='{') jsStr(ln,"path",sv,sizeof sv);
#ifdef REVA_HEADLESS
        brSay("{\"ok\":false,\"error\":\"this build draws nothing\"}"); return;
#else
        if(!sv[0]){ brSay("{\"ok\":false,\"error\":\"screenshot needs a path\",\"try\":\"{\\\"cmd\\\":\\\"screenshot\\\",\\\"path\\\":\\\"out.ppm\\\"}\"}"); return; }
        snprintf(g_brShot,sizeof g_brShot,"%s",sv); return;
#endif
    }
    if(!strcmp(cmd,"hash")){ brSay("{\"ok\":true,\"tick\":%ld,\"exact\":\"%016llx\",\"coarse\":\"%016llx\"}",g_tickTotal,g_shEx,g_shCo); return; }
    if(!strcmp(cmd,"rng_state")){ brSay("{\"ok\":true,\"dice\":\"%016llx\"}",g_dice); return; }
    if(!strcmp(cmd,"get")||!strcmp(cmd,"set")){ char nm[32]=""; if(ln[0]=='{') jsStr(ln,"name",nm,sizeof nm); else snprintf(nm,sizeof nm,"%.31s",sv);
        float*fv=!strcmp(nm,"hero_x")?&g_rvx:!strcmp(nm,"hero_z")?&g_rvz:!strcmp(nm,"hero_head")?&g_rvHead:!strcmp(nm,"game_t")?&g_gameT:NULL;
        if(!fv){ brSay("{\"ok\":false,\"error\":\"no value called %s\",\"try\":\"hero_x hero_z hero_head game_t\"}",nm); return; }
        if(cmd[0]=='s'){ double v; if(!jsNum(ln,"v",&v)){ brSay("{\"ok\":false,\"error\":\"set needs v\",\"try\":\"{\\\"cmd\\\":\\\"set\\\",\\\"name\\\":\\\"hero_x\\\",\\\"v\\\":1.5}\"}"); return; } *fv=(float)v; }
        brSay("{\"ok\":true,\"%s\":%.6f}",nm,*fv); return; }
    if(!strcmp(cmd,"quit")){ brSay("{\"ok\":true}"); g_run=0; return; }
    brSay("{\"ok\":false,\"error\":\"no command called %s\",\"try\":\"discover\"}",cmd); }
/* before main reads the clock: the bridge runs on a fixed step */
static void bridgeInit(void){ const char*e=getenv("REVA_BRIDGE"); if(!e||!e[0]) return;
    if(!getenv("REVA_FIXEDSTEP")){
#ifdef _WIN32
        _putenv("REVA_FIXEDSTEP=60");
#else
        setenv("REVA_FIXEDSTEP","60",1);
#endif
    }
    if(!bridgeOpen(atoi(e))){ printf("[bridge] can't open port %s\n",e); fflush(stdout); return; }
    g_brOn=1; g_brHold=getenv("REVA_BRIDGEHOLD")!=NULL; printf("[bridge] listening on 127.0.0.1:%s%s\n",e,g_brHold?", holding":""); fflush(stdout); }
/* the top of every pass: answer whatever's waiting, and wait here while the clock is held */
static void bridgePoll(void){ if(!g_brOn) return;
    if(g_brStepWait&&g_brSteps==0){ g_brStepWait=0; brSay("{\"ok\":true,\"tick\":%ld}",g_tickTotal); }
    int waited=0; char ln[1024];
    for(;;){ while(bridgeLine(ln,sizeof ln)) bridgeDo(ln);
        if(!g_brHold||g_brSteps>0||!g_run) break;
        waited=1; SDL_Delay(2); }
    if(waited) g_ledLast=0.0;   /* a held clock isn't a late frame */
    if(g_brSteps>0) g_brSteps--; }
/* after the polls and the tape: the buttons it's holding */
static void bridgeInput(void){ if(!g_brOn) return;
    for(int k=0;k<4;k++){ if(!g_brIn[k].seen) continue;
        int on=g_brIn[k].n>0; if(on) g_brIn[k].n--;
        /* one more tick when it runs out, to let go: a run with no pad has nothing else to clear them */
        if(on||g_brIn[k].live){ float lx=on?g_brIn[k].lx:0.f,ly=on?g_brIn[k].ly:0.f; int a=on&&g_brIn[k].a,b=on&&g_brIn[k].b,x=on&&g_brIn[k].x,y=on&&g_brIn[k].y,st=on&&g_brIn[k].st;
            if(st&&!g_brIn[k].stWas) g_startEdge=1;
            g_brIn[k].stWas=st;
            if(k==0){ g_lx=lx; g_ly=ly; g_btnA=a; g_btnB=b; g_btnX=x; g_btnY=y; g_btnStart=st; if(on) g_manual=1; }
            else { PadRaw*r=&g_padRaw[k]; memset(r,0,sizeof *r); r->lx=lx; r->ly=ly; r->held=(a?PAD_A:0u)|(b?PAD_B:0u)|(x?PAD_X:0u)|(y?PAD_Y:0u)|(st?PAD_START:0u); } }
        g_brIn[k].live=on;
        if(k>0) g_padRaw[k].connected=1; } }
/* the frame that was asked for */
static void bridgeFrameDone(int W,int H){ if(!g_brOn||!g_brShot[0]||g_noDraw) return;
    gfxShotSize(&W,&H);   /* (step 25) the glass's size */
    unsigned char*px=(unsigned char*)malloc((size_t)W*H*4); FILE*f=px?fopen(g_brShot,"wb"):NULL;
    if(!f){ brSay("{\"ok\":false,\"error\":\"can't write %s\"}",g_brShot); free(px); g_brShot[0]=0; return; }
    if(!gfxRead(px,W,H)) memset(px,0,(size_t)W*(size_t)H*4);
    fprintf(f,"P6\n%d %d\n255\n",W,H); for(int y=H-1;y>=0;y--) for(int x=0;x<W;x++) fwrite(px+((size_t)y*W+x)*4,1,3,f);
    fclose(f); free(px); brSay("{\"ok\":true,\"path\":\"%s\",\"tick\":%ld}",g_brShot,g_tickTotal); g_brShot[0]=0; }
