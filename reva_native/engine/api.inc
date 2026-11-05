/* ---- THE ENGINE'S SIDE OF engine.h (11-02, engine plan Phase 3): the table a game gets ---------------------------------------------------------------------------------------- */
_Static_assert(offsetof(Slot,spd)==offsetof(EngineBody,spd)&&offsetof(Slot,x)==offsetof(EngineBody,x)&&offsetof(Slot,tx)==offsetof(EngineBody,tx),"EngineBody must be the start of Slot");
_Static_assert(offsetof(Game,count)==offsetof(EngineRun,count)&&offsetof(Game,score)==offsetof(EngineRun,score)&&offsetof(Game,stage)==offsetof(EngineRun,stage),"EngineRun must be the start of Game");
static float apiFrnd(void){ return frnd(); }
static int apiRnd(void){ return rand(); }
static const char* apiBodyLabel(int i){ return g_packs[g_slots[i].pack].label; }
static void apiHeroPistol(void){ GM.wKind=WPN_PISTOL; GM.wVar=wpnPickDef(WPN_PISTOL,0u); }
static void apiBulletSpawn(float x,float z,float dx,float dz){ wpnSpawnProj(x,z,dx,dz,-1,WPN_PISTOL); }
static int apiBulletPeek(float*x,float*z){ int n=0; for(int p=0;p<WPROJ_MAX;p++) if(g_wproj[p].on){ if(!n){ *x=g_wproj[p].x; *z=g_wproj[p].z; } n++; } return n; }
static const char* apiBannerText(void){ return GM.msgT>0.f?GM.msg:NULL; }
static int vmOf(const char*n);
static int g_videoWant=-1;   /* what a game asked for: the loop changes it between frames (engine/display.inc) */
static void apiVideo(const char*mode){ g_videoWant=vmOf(mode); }
static void apiLens(float ex,float ey,float ez,float ax,float ay,float az){ g_gameLens.on=1; g_gameLens.e[0]=ex; g_gameLens.e[1]=ey; g_gameLens.e[2]=ez; g_gameLens.a[0]=ax; g_gameLens.a[1]=ay; g_gameLens.a[2]=az; }
static void apiLensFree(void){ g_gameLens.on=0; }
/* (step 26) the language: the file read, the page laid out again for it, and the choice kept for next time unless saves are blind */
static int langSet(const char*code,int keep){ const int ok=langLoad(code); if(g_tvOk&&g_tvW>0) tvUse(g_tvRead);
    if(keep&&!getenv("REVA_NOSAVE")){ FILE*f=fopen("save/language.txt","w"); if(f){ fprintf(f,"%s\n",ok?g_langCode:""); fclose(f); } }
    return ok; }
static int apiLanguage(const char*code){ return langSet(code?code:"",1); }
static const char* apiWord(const char*s){ return langWord(s); }
static const char* apiLanguageNow(void){ return g_langCode; }
/* at boot: REVA_LANG for this run, or what was chosen last time */
static void langBoot(void){ const char*e=getenv("REVA_LANG"); char c[32]="";
    if(e){ langSet(e,0); return; }
    if(getenv("REVA_NOSAVE")) return;
    FILE*f=fopen("save/language.txt","r"); if(!f) return;
    if(fscanf(f,"%15s",c)==1&&c[0]) langSet(c,0);
    fclose(f); }
static void apiLensLook(const EngineLensLook*l,size_t size){ EngineLensLook q; memset(&q,0,sizeof q); if(l) memcpy(&q,l,size<sizeof q?size:sizeof q);
    g_camRoll=q.roll; g_lensFocus=q.focus; g_lensDof=q.dof>0.f?q.dof:1.f; g_lensShutter=q.shutter; g_filmSepia=q.sepia; g_filmGrain=q.grain; g_filmFlash=q.flash; g_filmBurn=q.burn; }
static void apiViews(int n){ g_gameViews.n=n<1?1:n>4?4:n; g_tvNViews=g_gameViews.n; }
static void apiViewLens(int i,float ex,float ey,float ez,float ax,float ay,float az){ if(i<0||i>3) return; g_gameViews.e[i][0]=ex; g_gameViews.e[i][1]=ey; g_gameViews.e[i][2]=ez; g_gameViews.a[i][0]=ax; g_gameViews.a[i][1]=ay; g_gameViews.a[i][2]=az; if(i==0) apiLens(ex,ey,ez,ax,ay,az); }
static void apiShake(float amount,float seconds){ if(seconds<=0.f) return; g_gameLens.shake=amount; g_gameLens.shakeT=g_gameLens.shakeLen=seconds; }
static void apiTimeScale(float s){ g_timeScale=s<0.02f?0.02f:s>2.f?2.f:s; }
static float apiGround(float x,float y,float z){ return g_spOn?spGround(x,z,y):0.f; }
static int apiMark(const char*name,float*x,float*y,float*z){ for(int i=0;i<g_setNMk;i++) if(!strcmp(g_setMk[i].name,name)){ if(x) *x=g_setMk[i].x; if(y) *y=g_setMk[i].y; if(z) *z=g_setMk[i].z; return 1; } return 0; }
static EngineAPI g_api;
static void apiLightShadow(void){ if(g_frameNL>0) g_frameLight[g_frameNL-1].shadow=1; }
static void apiLight(float x,float y,float z,float r,float g,float b,float falloff){ frameLightAdd(x,y,z,r,g,b,falloff,0.f,0.f,0.f,0.f); }
static void apiSpot(float x,float y,float z,float dx,float dy,float dz,float r,float g,float b,float falloff,float deg){ frameLightAdd(x,y,z,r,g,b,falloff,dx,dy,dz,deg); }
/* step 20 (11-04): sound, music and a save of the game's own, in the table. The mixer and the slot store are the club's; a game reaches them here and nowhere else */
static int apiSound(const char*name,float gain,const float*pos,int loop){ return sfxPlayBus(BUS_SFX,name,gain,pos,loop); }
static void apiSoundBus(int bus,float gain,float seconds){ sfxBus(bus,gain,seconds); }
static int apiMusic(const char*path,float gain,int loop){ return musicPlay(path,gain,loop); }
static void apiMusicGain(float gain,float seconds){ sfxBus(BUS_MUSIC,gain,seconds); }
static const char* apiGameName(void){ return g_game&&g_game->name?g_game->name:"game"; }
static void apiSaveBegin(const char*slot,int version){ slotBegin(apiGameName(),slot,version); }
static void apiSaveSetI(const char*k,long v){ char b[32]; snprintf(b,sizeof b,"%ld",v); slotSet(k,b); }
static void apiSaveSetF(const char*k,float v){ char b[32]; snprintf(b,sizeof b,"%.9g",(double)v); slotSet(k,b); }
static int apiLoad(const char*slot){ return slotLoad(apiGameName(),slot); }
static long apiLoadGetI(const char*k,long def){ const char*s=slotGet(k,NULL); if(!s||!*s) return def; char*e=NULL; const long v=strtol(s,&e,10); return (e&&*e==0)?v:def; }
static float apiLoadGetF(const char*k,float def){ const char*s=slotGet(k,NULL); if(!s||!*s) return def; char*e=NULL; const double v=strtod(s,&e); return (e&&*e==0)?(float)v:def; }
/* what a game gets for anything it leaves out of its table */
static void gdVoid(void){}
static const char* gdStreet(void){ return "none"; }
static float gdTod(void){ return 12.f; }
static void gdStart(int onSet){ (void)onSet; }
static void gdTick(float dt){ (void)dt; }
static void gdDraw(const void*state){ (void)state; }
static void gdHost(float*heroHead,float dt){ (void)heroHead; (void)dt; }
static int gdBodyStep(int i,float dt){ (void)i; (void)dt; return 0; }
static float gdBodyUse(int i){ (void)i; return 1.f; }
static const char* gdBodyLoco(int i,float spd){ (void)i; (void)spd; return NULL; }
static const char* gdBodyIdle(int i){ (void)i; return NULL; }
static float gdBodyGait(int i){ (void)i; return 0.62f; }
static void gdBulletKill(int i){ (void)i; }
/* a game's table, with the gaps filled */
static void apiTake(const GameExport*g9){
    if(!g9||g9->version!=ENGINE_API_VERSION){ printf("[game] built for another engine\n"); fflush(stdout); exit(1); }
    static GameExport G; G=*g9;
    if(!G.name) G.name="A GAME";
    if(!G.boot) G.boot=gdVoid;
    if(!G.street) G.street=gdStreet;
    if(!G.tod) G.tod=gdTod;
    if(!G.start) G.start=gdStart;
    if(!G.tick) G.tick=gdTick;
    if(!G.draw) G.draw=gdDraw;
    if(!G.hud) G.hud=gdDraw;
    if(!G.host) G.host=gdHost;
    if(!G.bodyStep) G.bodyStep=gdBodyStep;
    if(!G.bodyUse) G.bodyUse=gdBodyUse;
    if(!G.bodyLoco) G.bodyLoco=gdBodyLoco;
    if(!G.bodyIdle) G.bodyIdle=gdBodyIdle;
    if(!G.bodyGait) G.bodyGait=gdBodyGait;
    if(!G.bulletKill) G.bulletKill=gdBulletKill;
    g_game=&G;
}
#ifdef REVA_GAMELIB
/* ⭐ DEV RELOAD (11-03, engine plan Phase 5, Linux): the game is a library, and when the file changes the running engine takes the new one between ticks. build_pi.sh game makes it in a second or two;
 * the engine, linked with -rdynamic, only gets rebuilt when it changes. A game that keeps everything in its state and says how big that is carries on where it was. */
#include <dlfcn.h>
static void*g_libH=NULL; static time_t g_libT=0;
static const char* gameLibPath(void){ const char*e=getenv("REVA_GAMELIB"); return (e&&e[0])?e:"./game.so"; }
static const GameExport* gameLibOpen(void){ struct stat st; const char*path=gameLibPath();
    g_libH=dlopen(path,RTLD_NOW|RTLD_LOCAL); if(!g_libH){ printf("[game] can't load %s: %s\n",path,dlerror()); fflush(stdout); exit(1); }
    const GameExport*(*entry)(const EngineAPI*)=(const GameExport*(*)(const EngineAPI*))dlsym(g_libH,"gameEntry");
    if(!entry){ printf("[game] %s has no gameEntry\n",path); fflush(stdout); exit(1); }
    g_libT=(stat(path,&st)==0)?st.st_mtime:0; return entry(&g_api); }
static void gameLibPoll(void){ static int n=0; struct stat st; if(!g_game||++n<60) return; n=0;
    if(stat(gameLibPath(),&st)!=0||st.st_mtime==g_libT) return;
    size_t sz=g_game->stateSize; void*keep=NULL; if(sz&&g_game->state){ keep=malloc(sz); if(keep) memcpy(keep,g_game->state,sz); }
    dlclose(g_libH); const GameExport*g9=gameLibOpen(); apiTake(g9);
    int kept=keep&&g_game->stateSize==sz&&g_game->state; if(kept) memcpy((void*)g_game->state,keep,sz);
    if(!kept) objClear();   /* its handles went with its state */
    free(keep); printf("[game] reloaded %s, %s\n",gameLibPath(),kept?"its state carried over":"its state starts over"); fflush(stdout); }
#else
#define gameLibPoll()
#endif
/* ---- online play (queue step 29): engine/net.inc is the engine's side and club_net.c the wire ---- */
static void envPut(const char*k,const char*v){
#ifdef _WIN32
    char b[400]; snprintf(b,sizeof b,"%s=%.300s",k,v?v:""); _putenv(b);
#else
    if(v&&v[0]) setenv(k,v,1); else unsetenv(k);
#endif
}
/* the engine starts again, into a session or out of one: the same program with the same arguments, which is how the start menu's MAIN MENU has always gone back.
 * A session is a whole run from tick 0, with the dice agreed before anything's loaded, so this is the only way into one from a game's own menu */
static void apiNetRelaunch(const char*spec,const char*word,const char*why){
    printf("[net] starting again, %s\n",spec?spec:"alone"); fflush(stdout);
    if(g_netOn){ netClose(); g_netOn=0; }
    saveFlush();
    envPut("REVA_NET",spec); envPut("REVA_NETWORD",word); envPut("REVA_NETBACK",spec?"1":NULL); envPut("REVA_NETWHY",why);
#ifdef _WIN32
    _execv(g_argv[0],(const char*const*)g_argv);
#else
    execv(g_argv[0],g_argv);
#endif
    printf("[net] couldn't start again\n"); fflush(stdout); exit(1); }
static int apiNetState(void){ if(!g_netOn) return 0; const int s=netState(); return s==NET_PLAY?2:s==NET_OVER?3:1; }
static int apiNetSeats(void){ return g_netOn?netSeats():0; }
static int apiNetHere(int player){ return (!g_netOn||netState()==NET_OFF)?1:netSeatMine(player); }
static int apiNetLag(void){ return g_netOn?netDelay():0; }
static const char* apiNetWhy(void){ if(g_netOn&&netState()==NET_OVER) return netWhy(); const char*e=getenv("REVA_NETWHY"); return e?e:""; }
static int netWordOk(const char*s,int most){ int n=0; for(;s&&*s;s++,n++){ const char c=*s; if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c==':'||c=='_')) return 0; } return n<=most; }
static void apiNetHost(int port,int pads,const char*word){ if(!netWordOk(word,48)){ printf("[net] a word is letters, digits, . - _ and no more than 48 of them\n"); fflush(stdout); return; }
    char s[64]; snprintf(s,sizeof s,"host:%d:%d",(port>0&&port<65536)?port:7777,pads<2?2:pads>4?4:pads); apiNetRelaunch(s,word,NULL); }
static void apiNetJoin(const char*address,const char*word){ if(!address||!address[0]||!netWordOk(address,100)||!netWordOk(word,48)){ printf("[net] an address is a name or four numbers, and a port after a colon if it isn't 7777\n"); fflush(stdout); return; }
    char s[128]; snprintf(s,sizeof s,"join:%s",address); apiNetRelaunch(s,word,NULL); }
static void apiNetLeave(void){ if(g_netOn||getenv("REVA_NET")) apiNetRelaunch(NULL,NULL,NULL); }
static void apiInit(void){
    g_api.version=ENGINE_API_VERSION;
    g_api.bodyStride=sizeof(Slot);
    g_api.hide=g_slotHide; g_api.team=g_slotTeam; g_api.ban=g_slotBan; g_api.drawable=g_slotDrawable; g_api.alt=g_slotAlt; g_api.spdMul=g_slotSpdMul;
    g_api.heroX=&g_rvx; g_api.heroZ=&g_rvz; g_api.heroHead=&g_rvHead; g_api.heroSpd=&g_manSpd; g_api.heroAlt=&g_manAlt; g_api.heroPistol=apiHeroPistol;
    g_api.run=(EngineRun*)&GM; g_api.tune=&g_tune;
    g_api.gameT=&g_gameT; g_api.frameNo=&g_frameNo; g_api.attract=&g_cabAttract; g_api.btnA=&g_btnA; g_api.btnB=&g_btnB;
    g_api.frnd=apiFrnd; g_api.rnd=apiRnd; g_api.collide=colResolve; g_api.bodyLabel=apiBodyLabel;
    g_api.bulletSpawn=apiBulletSpawn; g_api.bulletPeek=apiBulletPeek;
    g_api.line=gmLine; g_api.ring=gmRing; g_api.burst=gmBurst; g_api.banner=gmBanner; g_api.bannerText=apiBannerText;
    g_api.tvText=tvText; g_api.tvTextR=tvTextR; g_api.tvTextC=tvTextC; g_api.tvBar=tvBar; g_api.tvGrid=apiTvGrid; g_api.video=apiVideo; g_api.pads=g_pads;
    g_api.image=sprImage; g_api.sprite=sprDraw; g_api.rect=sprRect; g_api.clear=sprClear;
    g_api.action=apiAction; g_api.actionHeld=apiActionHeld; g_api.actionPressed=apiActionPressed;
    g_api.lens=apiLens; g_api.lensFree=apiLensFree; g_api.shake=apiShake; g_api.timeScale=apiTimeScale;
    g_api.uiMove=apiUiMove; g_api.uiList=apiUiList; g_api.nearby=apiNear; g_api.light=apiLight; g_api.spot=apiSpot; g_api.lightShadow=apiLightShadow;
    g_api.prop=apiProp; g_api.spawn=apiSpawn; g_api.despawn=apiDespawn; g_api.place=apiPlace; g_api.paint=apiPaint; g_api.object=apiObject; g_api.bodyNew=apiBodyNew; g_api.bodyGone=apiBodyGone; g_api.level=apiLevel; g_api.levelName=apiLevelName; g_api.path=apiPath; g_api.attach=apiAttach; g_api.detach=apiDetach; g_api.detachAll=apiDetachAll; g_api.musicClock=apiMusicClock; g_api.musicTime=apiMusicTime; g_api.musicBeat=apiMusicBeat; g_api.bodyBuild=apiBodyBuild; g_api.heroWear=apiHeroWear; g_api.heroFace=apiHeroFace; g_api.bodySay=apiBodySay; g_api.bodySaying=apiBodySaying; g_api.bodyShut=apiBodyShut; g_api.captions=apiCaptions; g_api.bodySayData=apiBodySayData; g_api.bodyMouth=apiBodyMouth; g_api.bodyShape=apiBodyShape; g_api.bodyLook=apiBodyLook; g_api.bodyLookBody=apiBodyLookBody; g_api.bodyLookOff=apiBodyLookOff; g_api.talkSeat=apiTalkSeat; g_api.talkStart=apiTalkStart; g_api.talking=apiTalking; g_api.talkStop=apiTalkStop; g_api.propDone=apiPropDone; g_api.bodyPack=apiBodyPack; g_api.bodyPackReady=apiBodyPackReady; g_api.bodyPackDone=apiBodyPackDone; g_api.bodyWear=apiBodyWear; g_api.tvView=apiTvView; g_api.uiView=apiUiView; g_api.uiBegin=apiUiBegin; g_api.uiEnd=apiUiEnd; g_api.uiLabel=apiUiLabel; g_api.uiGap=apiUiGap; g_api.uiEnable=apiUiEnable; g_api.uiButton=apiUiButton; g_api.uiToggle=apiUiToggle; g_api.uiSlider=apiUiSlider; g_api.uiChoice=apiUiChoice; g_api.uiText=apiUiText; g_api.uiTabs=apiUiTabs; g_api.uiBack=apiUiBack; g_api.uiColors=apiUiColors; g_api.uiSounds=apiUiSounds; g_api.push=apiPush; g_api.pushAt=apiPushAt; g_api.objectOnRay=apiObjectOnRay; g_api.feel=apiFeel; g_api.bodyLimp=apiBodyLimp; g_api.bodyRise=apiBodyRise; g_api.bodyLimpState=apiBodyLimpState;
    g_api.views=apiViews; g_api.viewLens=apiViewLens; g_api.particles=apiParticles; g_api.decal=apiDecal;
    g_api.particleKind=apiParticleKind; g_api.emitter=apiEmitter; g_api.emitterAt=apiEmitterAt; g_api.emitterRate=apiEmitterRate; g_api.emitterGone=apiEmitterGone; g_api.emitterBurst=apiBurst; g_api.decalAt=apiDecalAt; g_api.decalsClear=apiDecalsClear; g_api.lensLook=apiLensLook; g_api.language=apiLanguage; g_api.word=apiWord; g_api.languageNow=apiLanguageNow;
    g_api.feet=g_slotFeet; g_api.animGraph=apiAnimGraph; g_api.animSet=apiAnimSet; g_api.animIn=apiAnimIn; g_api.bodyGraph=apiBodyGraph; g_api.bodyAnimSet=apiBodyAnimSet; g_api.bodyAnimIn=apiBodyAnimIn;
    g_api.walk=spWalk; g_api.ground=apiGround; g_api.ray=spRay; g_api.inTrigger=spInTrigger; g_api.mark=apiMark; g_api.heroFeet=&g_feet.y; g_api.heroJump=spHeroJump;
    g_api.sound=apiSound; g_api.soundLoad=sfxPreload; g_api.soundStop=sfxStop; g_api.soundBus=apiSoundBus; g_api.music=apiMusic; g_api.musicStop=musicStop; g_api.musicGain=apiMusicGain;
    g_api.saveBegin=apiSaveBegin; g_api.saveSet=slotSet; g_api.saveSetI=apiSaveSetI; g_api.saveSetF=apiSaveSetF; g_api.saveEnd=slotEnd; g_api.load=apiLoad; g_api.loadGet=slotGet; g_api.loadGetI=apiLoadGetI; g_api.loadGetF=apiLoadGetF; g_api.talkView=apiTalkView; g_api.opened=clubOpened; g_api.bodyEyes=apiBodyEyes;
    g_api.netState=apiNetState; g_api.netSeats=apiNetSeats; g_api.netHere=apiNetHere; g_api.netLag=apiNetLag; g_api.netWhy=apiNetWhy; g_api.netHost=apiNetHost; g_api.netJoin=apiNetJoin; g_api.netLeave=apiNetLeave;
#ifdef REVA_GAMELIB
    const GameExport*g9=gameLibOpen();
#else
    const GameExport*g9=gameEntry(&g_api);
#endif
    apiTake(g9);
    printf("[game] %s\n",g_game->name); }
