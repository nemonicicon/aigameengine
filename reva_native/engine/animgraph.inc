/* ---- THE ANIMATION GRAPH (11-03, engine plan Phase 6) ---------------------------------------------------------------------------------------------------------------------
 * A text file beside a game says which clip a body plays, when that changes, and when something in a clip happens. A game asks for it with E->animGraph("hero.anim") for the hero and E->bodyGraph(i, "chaser.anim") for any body, and it's read again when it changes, like the rules:
 *   state run w_run loop gait     a clip, `loop` or `once`, at a rate: a number, or `gait`, which keeps the feet on the ground at its speed
 *   when any jump jump            from a state or `any`, to a state, when: a flag a game set with E->animSet or E->bodyAnimSet, which is then used up; name>v or name<v on a value; or `end`, when a once clip has finished. The first that's true wins
 *   event punch 0.35 hit          this far through the state's clip, tell the game: GameExport.clipEvent
 * The engine sets `speed`, and the hero's `grounded`. A body's `grounded` is its game's to set, and starts at 1. Each body keeps its own exact place in the clip, in the tick and whether or not it's on screen, so a state and an event fall on the same tick on any machine at any frame rate.
 * A body's clip can be its own, or borrowed by name from a bank (the moves bank, then the zombie bank) if its rig can take it. A body with no graph picks its clips the old way, by bodyLoco and the crowd's ladder. */
#define AG_MAXS 32
#define AG_MAXW 64
#define AG_MAXE 32
#define AG_MAXP 16
#define AG_MAXG 16
#define AG_MAXPACKS 1024
typedef struct { char name[24],clip[32]; int c,once,gait; float rate; } AgState;   /* c: this state's clip on the hero's pack */
typedef struct { int from,to,kind,param; float v; } AgWhen;   /* kind 0 a flag, 1 >, 2 <, 3 the end of a once clip */
typedef struct { int st; float u; char name[24]; } AgEvent;
typedef struct { Pack*src; int c,own; } AgRef;   /* a state's clip on one pack: found once. src NULL and c 0 hasn't been looked for, and c -1 there isn't one */
typedef struct { int on,dirty; char path[160]; long stamp; AgState s[AG_MAXS]; int nS; AgWhen w[AG_MAXW]; int nW; AgEvent e[AG_MAXE]; int nE; char pn[AG_MAXP][24]; int nP; Pack*heroP; AgRef*ref; int refN; } AgGraph;
typedef struct { int on,g,cur,done; float u,pv[AG_MAXP]; } AgRun;   /* one body's place in one graph */
static AgGraph g_agG[AG_MAXG]; static int g_agNG=0;
static AgRun g_agHero; static AgRun g_agBody[1024]; static int g_agBodyEver=0;
static int agParam(AgGraph*G,const char*n){ for(int i=0;i<G->nP;i++) if(!strcmp(G->pn[i],n)) return i;
    if(G->nP>=AG_MAXP){ printf("[anim] more than %d values: %s doesn't get one\n",AG_MAXP,n); return 0; }
    snprintf(G->pn[G->nP],24,"%.23s",n); return G->nP++; }
static int agStateOf(const AgGraph*G,const char*n){ for(int i=0;i<G->nS;i++) if(!strcmp(G->s[i].name,n)) return i; return -1; }
static long agStamp(const char*path){ struct stat st; return stat(path,&st)==0?(long)st.st_mtime:0L; }
/* a body keeps playing what it was through a new version of its file, if that still has a state of the name */
static void agKeep(AgRun*R,const AgGraph*G,const char(*was)[24],int nWas,int g){ if(!R->on||R->g!=g) return;
    int k=(R->cur<nWas)?agStateOf(G,was[R->cur]):-1; if(k>=0) R->cur=k; else { R->cur=0; R->u=0.f; R->done=0; } }
/* a whole file or nothing changes, as near as it can: what's in it that makes sense is what it plays */
static void agLoad(int g){ AgGraph*G=&g_agG[g]; FILE*f=fopen(G->path,"r"); G->dirty=0; G->stamp=agStamp(G->path);
    if(!f){ printf("[anim] can't read %s\n",G->path); fflush(stdout); return; }
    clubOpened(G->path);
    char was[AG_MAXS][24]; int nWas=G->nS; for(int i=0;i<nWas;i++) snprintf(was[i],24,"%s",G->s[i].name);
    free(G->ref); G->ref=NULL; G->refN=0; G->heroP=NULL;
    G->nS=G->nW=G->nE=0; char ln[160];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#') continue;
        if(!txtNumsOk(ln,1e6)){ printf("[anim] %s: a line with a number in it that isn't one, left out: %.60s\n",G->path,ln); fflush(stdout); continue; }   /* (step 28) a rate of 1e308 hung the loop that wraps a clip */
        char a[32],c[32],d[32];
        if(!strncmp(ln,"state ",6)&&G->nS<AG_MAXS){ AgState*S=&G->s[G->nS]; memset(S,0,sizeof *S);
            int n=sscanf(ln,"state %23s %31s %31s %31s",S->name,S->clip,c,d); if(n<3) continue;
            S->once=!strcmp(c,"once"); S->rate=1.f;
            if(n==4){ if(!strcmp(d,"gait")) S->gait=1; else { S->rate=(float)atof(d); if(!(S->rate>-64.f&&S->rate<64.f)) S->rate=1.f; } }   /* (step 28) a rate is a speed, held to one */
            S->c=-1; G->nS++; continue; }
        if(!strncmp(ln,"event ",6)&&G->nE<AG_MAXE){ AgEvent*E9=&G->e[G->nE];
            if(sscanf(ln,"event %31s %f %23s",a,&E9->u,E9->name)==3){ if(!(E9->u>=0.f)) E9->u=0.f; else if(E9->u>1.f) E9->u=1.f; E9->st=agStateOf(G,a); if(E9->st<0) printf("[anim] %s: an event on a state that isn't there: %s\n",G->path,a); else G->nE++; }
            continue; } }
    /* the states are all known now: the transitions can name them in any order */
    rewind(f);
    while(fgets(ln,sizeof ln,f)){ char a[32],b[32],c[40]; if(strncmp(ln,"when ",5)||G->nW>=AG_MAXW||!txtNumsOk(ln,1e6)) continue;
        if(sscanf(ln,"when %31s %31s %39s",a,b,c)!=3) continue;
        AgWhen*W=&G->w[G->nW]; W->from=strcmp(a,"any")?agStateOf(G,a):-1; W->to=agStateOf(G,b);
        if((W->from<0&&strcmp(a,"any"))||W->to<0){ printf("[anim] %s: a transition names a state that isn't there: %s %s\n",G->path,a,b); continue; }
        char*o=strpbrk(c,"<>");
        if(!strcmp(c,"end")){ W->kind=3; W->param=0; W->v=0.f; }
        else if(o){ W->kind=*o=='>'?1:2; W->v=(float)atof(o+1); *o=0; W->param=agParam(G,c); }
        else { W->kind=0; W->param=agParam(G,c); W->v=0.f; }
        G->nW++; }
    fclose(f);
    agParam(G,"speed"); agParam(G,"grounded");
    agKeep(&g_agHero,G,(const char(*)[24])was,nWas,g);
    for(int i=0;i<1024;i++) agKeep(&g_agBody[i],G,(const char(*)[24])was,nWas,g);
    G->on=G->nS>0;
    printf("[anim] %s: %d states, %d transitions, %d events\n",G->path,G->nS,G->nW,G->nE); fflush(stdout); }
/* a path's graph, the first time it's asked for. Read when it's next needed */
static int agGraph(const char*path){ for(int i=0;i<g_agNG;i++) if(!strcmp(g_agG[i].path,path)) return i;
    if(g_agNG>=AG_MAXG){ printf("[anim] more than %d graphs: %s doesn't get one\n",AG_MAXG,path); fflush(stdout); return -1; }
    AgGraph*G=&g_agG[g_agNG]; snprintf(G->path,sizeof G->path,"%s",path); G->dirty=1; return g_agNG++; }
/* once a tick, wherever one's in use: anything that changed on disk */
static void agPoll(void){ static int n=0; const int look=++n>=60; if(look) n=0;
    for(int g=0;g<g_agNG;g++){ AgGraph*G=&g_agG[g]; if(look&&!G->dirty&&agStamp(G->path)!=G->stamp) G->dirty=1;
        if(G->dirty) agLoad(g); } }
static int apiAnimGraph(const char*path){ int g=agGraph(path); if(g<0) return 0; g_agHero.g=g; g_agHero.on=1; g_agG[g].dirty=1; return agStamp(path)!=0L; }
static void apiAnimSet(const char*name,float v){ if(!g_agHero.on) return; g_agHero.pv[agParam(&g_agG[g_agHero.g],name)]=v; }
static int apiAnimIn(const char*state){ if(!g_agHero.on) return 0; const AgGraph*G=&g_agG[g_agHero.g]; return G->on&&g_agHero.cur<G->nS&&!strcmp(G->s[g_agHero.cur].name,state); }
/* which transition is true, if any. Returns 1 if it changed state */
static int agTransit(AgGraph*G,AgRun*R,float*prev){
    for(int i=0;i<G->nW;i++){ const AgWhen*W=&G->w[i]; if(W->from>=0&&W->from!=R->cur) continue; if(W->to==R->cur&&W->kind!=0) continue;
        int go=W->kind==3?R->done:W->kind==1?R->pv[W->param]>W->v:W->kind==2?R->pv[W->param]<W->v:R->pv[W->param]!=0.f;
        if(!go) continue;
        if(W->kind==0) R->pv[W->param]=0.f;
        R->cur=W->to; R->u=0.f; R->done=0; *prev=-1.f; return 1; }
    return 0; }
/* on through the clip by a tick, and tell the game what it passed. who: a body's index, or -1 for the hero */
static void agAdvance(AgGraph*G,AgRun*R,const PClip*c,float r,float dt,int fresh,float prev,int who){ const AgState*S=&G->s[R->cur]; float len=c->frames>1?(float)c->frames/c->fps:1.f, wrapped=0.f;
    if(!fresh) R->u+=dt*r/len;
    if(S->once){ if(R->u>=1.f){ R->u=1.f; R->done=1; } }
    else if(R->u>=1.f){ R->u-=floorf(R->u); wrapped=1.f; if(!(R->u>=0.f&&R->u<1.f)) R->u=0.f; }   /* (step 28) once round or a hundred times, in one go: the same answer as taking 1 off until it's under 1, and it can't run for ever */
    for(int i=0;i<G->nE;i++){ const AgEvent*E9=&G->e[i]; if(E9->st!=R->cur) continue;
        if(wrapped>0.f?(E9->u>prev||E9->u<=R->u):(E9->u>prev&&E9->u<=R->u)){ if(g_game&&g_game->clipEvent) g_game->clipEvent(who,E9->name); } } }
/* one tick for the hero: which clip, and how far through it */
static void agTick(Pack*P,float dt,int*want,float*rate,float*hold){
    if(!g_agHero.on) return;
    AgGraph*G=&g_agG[g_agHero.g]; AgRun*R=&g_agHero;
    if(G->dirty) agLoad(g_agHero.g);
    if(!G->on) return;
    if(G->heroP!=P){ G->heroP=P; for(int i=0;i<G->nS;i++){ AgState*S=&G->s[i]; S->c=-1; for(unsigned k=0;k<P->nClips;k++) if(!strcmp(packStr(P,P->clips[k].name),S->clip)){ S->c=(int)k; break; }
            if(S->c<0) printf("[anim] %s: this body has no clip called %s\n",G->path,S->clip); } }
    R->pv[agParam(G,"speed")]=g_manSpd; R->pv[agParam(G,"grounded")]=g_feet.ground?1.f:0.f;
    float prev=R->u; const int fresh=agTransit(G,R,&prev);
    const AgState*S=&G->s[R->cur]; if(S->c<0) return;
    const PClip*c=&P->clips[S->c];
    agAdvance(G,R,c,S->gait?gaitRate(P,S->c,g_manSpd):S->rate,dt,fresh,prev,-1);
    *want=S->c; *rate=0.f; *hold=S->once?R->u*(float)(c->frames-1)/(float)c->frames:R->u; }
/* ---- every other body. Its clip by name on its own pack: its own, or a bank's if its rig can take it */
static const AgRef* agRefOf(AgGraph*G,int st,int pack){ const int nP=AG_MAXPACKS;
    if(!G->ref||G->refN!=nP){ free(G->ref); G->ref=(AgRef*)calloc((size_t)AG_MAXS*(size_t)nP,sizeof(AgRef)); G->refN=nP; }
    AgRef*r=&G->ref[st*nP+pack]; if(r->src||r->c<0) return r;
    Pack*P=&g_packs[pack]; const char*nm=G->s[st].clip;
    for(unsigned k=0;k<P->nClips;k++) if(!strcmp(packStr(P,P->clips[k].name),nm)){ r->src=P; r->c=(int)k; r->own=1; return r; }
    int c=-1; Pack*bp=P->pbOK>=10?bankFind(nm,&c):NULL;
    if(bp&&donorOK(bp,c,P)){ r->src=bp; r->c=c; r->own=0; return r; }
    if(g_zombP&&P->pbOK>=10) for(unsigned k=0;k<g_zombP->nClips;k++) if(!strcmp(packStr(g_zombP,g_zombP->clips[k].name),nm)&&donorOK(g_zombP,(int)k,P)){ r->src=g_zombP; r->c=(int)k; r->own=0; return r; }
    r->c=-1; printf("[anim] %s: %s has no clip called %s, and can't borrow one\n",G->path,P->label,nm); fflush(stdout);
    return r; }
static void agBodyReset(int i){ AgRun*R=&g_agBody[i]; if(!R->on) return; const AgGraph*G=&g_agG[R->g]; R->cur=0; R->u=0.f; R->done=0; memset(R->pv,0,sizeof R->pv);
    for(int k=0;k<G->nP;k++) if(!strcmp(G->pn[k],"grounded")) R->pv[k]=1.f; }
static void apiBodyGraph(int i,const char*path){ if(i<0||i>=1024||i>=g_NS) return; AgRun*R=&g_agBody[i];
    if(!path||!path[0]){ R->on=0; return; }
    int g=agGraph(path); if(g<0){ R->on=0; return; }
    if(g_agG[g].dirty) agLoad(g);
    R->g=g; R->on=g_agG[g].on; g_agBodyEver=1; agBodyReset(i); }
static void apiBodyAnimSet(int i,const char*name,float v){ if(i<0||i>=1024||!g_agBody[i].on) return; g_agBody[i].pv[agParam(&g_agG[g_agBody[i].g],name)]=v; }
static int apiBodyAnimIn(int i,const char*state){ if(i<0||i>=1024||!g_agBody[i].on) return 0; const AgGraph*G=&g_agG[g_agBody[i].g]; return G->on&&g_agBody[i].cur<G->nS&&!strcmp(G->s[g_agBody[i].cur].name,state); }
/* once a tick, in the sim, seen or not. A hidden one waits */
static void agBodyTick(int si,int pack,float spd,float dt){ AgRun*R=&g_agBody[si]; if(!R->on||g_slotHide[si]) return; AgGraph*G=&g_agG[R->g]; if(!G->on) return;
    R->pv[agParam(G,"speed")]=spd;
    float prev=R->u; const int fresh=agTransit(G,R,&prev);
    const AgRef*r=agRefOf(G,R->cur,pack); if(r->c<0) return;
    const AgState*S=&G->s[R->cur]; const PClip*c=&r->src->clips[r->c];
    agAdvance(G,R,c,S->gait?gaitRate(r->src,r->c,spd):S->rate,dt,fresh,prev,si); }
/* what the crowd's draw asks: its clip, how far through, and a key that's different per state, so a change of state crossfades. 0 if it has no graph, or that state has no clip on this body */
static int agBodyPose(int si,int pack,Pack**src,int*ci,int*own,double*fp,int*key){ const AgRun*R=&g_agBody[si]; if(!R->on) return 0; AgGraph*G=&g_agG[R->g]; if(!G->on||R->cur>=G->nS) return 0;
    const AgRef*r=agRefOf(G,R->cur,pack); if(r->c<0) return 0;
    const PClip*c=&r->src->clips[r->c]; *src=r->src; *ci=r->c; *own=r->own; *key=R->cur;
    *fp=(double)R->u*(double)(G->s[R->cur].once?(c->frames>0?c->frames-1:0):c->frames);
    return 1; }
static void agHash(int nb){ if(!g_agBodyEver) return; g_shDumpTag="anim";
    for(int i=0;i<nb&&i<1024;i++){ const AgRun*R=&g_agBody[i]; if(!R->on||g_slotHide[i]) continue; float f9[2]={(float)R->cur,R->u}; simHashMix(f9,2); } }
