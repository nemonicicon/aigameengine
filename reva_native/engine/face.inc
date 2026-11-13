/* ---- FACES AND CONVERSATIONS (queue step 14, 11-04) ----------------------------------------------------------------------------------------------------------------------
 * Any body in any game can blink, say a line, look at something, and have its mouth fed from data, and a conversation is a text file.
 * A face is what's drawn and not the rules: nothing here reaches [simhash], [packet] sees all of it, and it moves a tick at a time, so a frame that owes two ticks draws what any other pattern draws.
 * What a face can do depends on what its pack has: a jaw bone or a shape called jaw (or jawOpen), eye bones or eyeLook shapes, a shape called blink (or eyeBlinkLeft and Right), and any shape by name.
 * Each body has its own face, so its moved positions go in the frame's packet for the meshes that have moved. A mesh past FACE_MAXV is film's size: those bodies share a face in the pack's own buffer (morphBlend), as film's always have.
 * A voice is read on the stream's thread and its loudness worked out there, so the disk can't reach a frame. Anything measured or compared reads it on the spot, as cells and bodies do.
 * ⛔ A line's length comes from its sound, and what's read when depends on the disk outside a measured run, so no rule may depend on the exact tick a line ends in a live game. */
#define FACE_POOL 64
#define FACE_SHAPES 16
#define FACE_MAXV 65536u
#define FACE_HERO 1024
#define VOICE_MAX 256
#define VOICE_IDLE 3600   /* a minute of ticks with nobody using it, and its memory goes. Asking again reads it again */
static int g_talkOn=0;   /* a conversation's up: its voices aren't placed in the world */
static void apiHeroFace(float yaw){ g_heroFaceY=yaw; g_heroFaceOn=1; }
/* (step 23) eyes that glow, on any body, -1 the hero: a colour and how much, 0 to put them out. What's drawn and not the rules */
static void apiBodyEyes(int i,float r,float g,float b,float amount){ float*o=i<0?g_heroEye:(i<1024?g_slotEye[i]:NULL); if(!o) return; o[0]=r; o[1]=g; o[2]=b; o[3]=amount<0.f?0.f:amount; g_eyeEver=1; }
static int g_faceHeroHashed=0;   /* the receipt's already had the hero's pose this frame, from before the face went on */
typedef struct { char name[24]; float want,now,rate; short idx[8]; int n; } FaceShape;
struct Face { int body,on; Pack*pk; FaceShape s[FACE_SHAPES]; int nS; int shared,wasAct; short iBlink[4],iJaw[8],iSmile[4]; int nBlink,nJaw,nSmile;
    float t,open,fed,blink,blinkAt,blinkT,nodT; unsigned dice;
    int lineOn,pending,voice,sfx,uses; float lineT,lineLen,rate; float*env,*vis; int nEnv,nVis,ownEnv; char words[168];
    int look,lookBody; float lookP[3],S[4],aim,yawT,pitchT;
    float eyeY; int eyeYOK;   /* (step 20) where its eyes really are, above its feet, as last posed: a seated body's are lower than its rest pose says */
    float w[MORPH_MAX]; };
typedef struct { char path[96]; SDL_atomic_t state; float*pcm; int n; float*env; int nEnv,slot,idle,uses,missing,want; } FaceVoice;
static Face g_faces[FACE_POOL]; static short g_faceOf[FACE_HERO+1]; static int g_faceN=0;
static FaceVoice g_fv[VOICE_MAX]; static int g_fvN=0;
static struct { char L[12][128]; int n; float t,len; int who,on; } g_cap;   /* a row is 31 letters, and a letter past ASCII is up to four bytes (step 26) */ static int g_faceCapOff=0;
/* who has this body's face, -1 being the hero */
static Face* faceOf(int i){ const int k=i<0?FACE_HERO:i; if(k>FACE_HERO||(i>=0&&(i>=g_NS||i>=1024))) return NULL; return g_faceOf[k]?&g_faces[g_faceOf[k]-1]:NULL; }
static Face* faceGet(int i){ const int k=i<0?FACE_HERO:i; if(k>FACE_HERO||(i>=0&&(i>=g_NS||i>=1024))) return NULL; if(g_faceOf[k]) return &g_faces[g_faceOf[k]-1];
    for(int n=0;n<FACE_POOL;n++){ Face*f=&g_faces[n]; if(f->on) continue; memset(f,0,sizeof *f); f->on=1; f->body=i; f->voice=-1; f->sfx=-1; f->fed=-1.f; f->dice=(unsigned)(k+1)*2654435761u; f->blinkAt=1.f+(float)(f->dice>>24)/128.f; g_faceOf[k]=(short)(n+1); g_faceN++; return f; }
    { static int said=0; if(!said){ said=1; printf("[face] more than %d faces at once: the next ones don't move\n",FACE_POOL); fflush(stdout); } }
    return NULL; }
/* which of a pack's shapes each name means: every one that carries it, because glTF repeats a shape on each mesh it touches */
static int faceIdx(Pack*P,const char*nm,short*o,int cap){ int n=0; for(int m=0;m<P->nMorph&&n<cap;m++) if(!strcmp(packStr(P,P->morphName[m]),nm)) o[n++]=(short)m; return n; }
static void faceBind(Face*f,Pack*P){ f->pk=P; for(int i=0;i<f->nS;i++) f->s[i].n=faceIdx(P,f->s[i].name,f->s[i].idx,8);
    f->nBlink=faceIdx(P,"blink",f->iBlink,4); if(!f->nBlink){ f->nBlink=faceIdx(P,"eyeBlinkLeft",f->iBlink,2); f->nBlink+=faceIdx(P,"eyeBlinkRight",f->iBlink+f->nBlink,2); }
    f->nJaw=faceIdx(P,"jaw",f->iJaw,8); if(!f->nJaw) f->nJaw=faceIdx(P,"jawOpen",f->iJaw,8); f->nSmile=faceIdx(P,"smile",f->iSmile,4);
    f->shared=0; for(int m=0;m<P->nMorph;m++) if(P->meshes[P->morphMesh[m]].verts>FACE_MAXV){ f->shared=1; break; } f->wasAct=0; }
/* ---- voices ---- */
static void voiceRead(int k){ FaceVoice*V=&g_fv[k]; int n=0; float*pcm=sfxDecodeFile(V->path,&n);
    if(!pcm||n<=0){ free(pcm); V->missing=1; V->pcm=NULL; V->n=0; V->env=NULL; V->nEnv=0; SDL_AtomicSet(&V->state,ST_READ); return; }
    const int hop=SFX_RATE/30,m=n/hop>0?n/hop:1; float*e=(float*)malloc((size_t)m*sizeof(float)),*o=(float*)malloc((size_t)m*sizeof(float));
    for(int i=0;i<m;i++){ double a=0.0; const int c=n<hop?n:hop; for(int j=0;j<c;j++){ const float s=pcm[i*hop+j]; a+=(double)s*(double)s; } e[i]=sqrtf((float)(a/(double)c)); o[i]=e[i]; }
    /* against the line's own loud end, so a quiet recording talks as much as a loud one */
    for(int i=1;i<m;i++){ const float x=o[i]; int j=i; while(j>0&&o[j-1]>x){ o[j]=o[j-1]; j--; } o[j]=x; }
    const float ref=o[(int)((float)m*0.95f)]>1e-5f?o[(int)((float)m*0.95f)]:1.f; for(int i=0;i<m;i++){ e[i]/=ref; if(e[i]>1.f) e[i]=1.f; } free(o);
    V->pcm=pcm; V->n=n; V->env=e; V->nEnv=m; SDL_AtomicSet(&V->state,ST_READ); }
static void voiceInstall(int k){ FaceVoice*V=&g_fv[k]; char nm[16]; snprintf(nm,sizeof nm,"v:%d",k); V->slot=V->pcm?sfxBankPCM(nm,V->pcm,V->n):-1; V->pcm=NULL; V->idle=0;
    if(V->missing){ static int said=0; if(said++<8){ printf("[voice] no sound called %s\n",V->path); fflush(stdout); } }
    SDL_AtomicSet(&V->state,ST_HERE); }
static void voiceBegin(int k){ FaceVoice*V=&g_fv[k]; stStart(); V->want=0; V->missing=0;
    if(g_st.sync){ voiceRead(k); voiceInstall(k); return; }
    const int wr=SDL_AtomicGet(&g_st.wr); if(wr-SDL_AtomicGet(&g_st.rd)>=ST_MAXQ){ V->want=1; return; }
    g_st.q[wr%ST_MAXQ]=k|ST_VOICEJOB; SDL_AtomicSet(&V->state,ST_READING); SDL_AtomicSet(&g_st.wr,wr+1); }
static int voiceAsk(const char*path){ int old=-1,best=-1;
    for(int k=0;k<g_fvN;k++){ if(!strcmp(g_fv[k].path,path)){ g_fv[k].idle=0; if(SDL_AtomicGet(&g_fv[k].state)==ST_AWAY) voiceBegin(k); return k; }
        if(SDL_AtomicGet(&g_fv[k].state)==ST_AWAY&&g_fv[k].idle>=best){ old=k; best=g_fv[k].idle; } }
    const int k=g_fvN<VOICE_MAX?g_fvN++:old; if(k<0){ static int said=0; if(!said){ said=1; printf("[voice] more than %d voices wanted at once\n",VOICE_MAX); fflush(stdout); } return -1; }
    FaceVoice*V=&g_fv[k]; snprintf(V->path,sizeof V->path,"%s",path); V->idle=0; V->uses=0; SDL_AtomicSet(&V->state,ST_AWAY); voiceBegin(k); return k; }
static void voiceDrop(int k){ FaceVoice*V=&g_fv[k]; if(V->slot>=0) sfxBankFree(V->slot); V->slot=-1; free(V->env); V->env=NULL; V->nEnv=0; V->n=0; SDL_AtomicSet(&V->state,ST_AWAY); }
/* ---- a line ---- */
static float faceTrack(const float*a,int n,float t,float rate){ if(!a||n<1) return 0.f; float x=t*rate; if(x<0.f) x=0.f; const int k=(int)x; if(k>=n-1) return a[n-1]; const float u=x-(float)k; return a[k]*(1.f-u)+a[k+1]*u; }
/* how far a mouth opens on each letter, and for how long: "the closures. everything." From scenes/compile_scene.py, whose viseme_track this is */
static void faceLetter(char c,float*jaw,float*len){ static const struct { char c; float j,l; } T[]={{'a',1.f,1.7f},{'e',.58f,1.5f},{'i',.34f,1.4f},{'o',.7f,1.6f},{'u',.46f,1.4f},{'y',.36f,1.2f},{'m',0.f,1.f},{'b',0.f,.8f},{'p',0.f,.8f},{'f',.16f,1.f},{'v',.18f,1.f},{'w',.24f,1.f},{'s',.18f,1.2f},{'z',.2f,1.1f},{'c',.22f,1.f},{'x',.22f,1.f},{'t',.26f,.8f},{'d',.28f,.8f},{'n',.24f,1.f},{'l',.3f,1.f},{'r',.34f,1.1f},{'g',.3f,.9f},{'k',.26f,.8f},{'h',.3f,.8f},{'j',.26f,1.f},{'q',.26f,1.f},{' ',.06f,.7f}};
    *jaw=.26f; *len=1.f; for(size_t i=0;i<sizeof T/sizeof T[0];i++) if(T[i].c==c){ *jaw=T[i].j; *len=T[i].l; return; } }
static float* faceVisOf(const char*text,float secs,int*nOut){ const int n=(int)(secs*30.f+0.5f)>0?(int)(secs*30.f+0.5f):1; float*o=(float*)malloc((size_t)n*sizeof(float)); float total=0.f; char prev=0;
    static float J[256],D[256]; int m=0;
    for(const char*p=text;*p&&m<256;p++){ char c=*p; if(c>='A'&&c<='Z') c=(char)(c-'A'+'a'); if(strchr(",.!?;:",c)){ J[m]=.05f; D[m]=1.4f; total+=1.4f; m++; prev=0; continue; }
        if(!((c>='a'&&c<='z')||c==' ')||(c==prev&&c!=' ')) continue;
        prev=c; faceLetter(c,&J[m],&D[m]); total+=D[m]; m++; }
    if(!m){ for(int i=0;i<n;i++) o[i]=0.f; *nOut=n; return o; }
    const float step=total/(float)n; float t=0.f,acc=0.f; int u=0; for(int i=0;i<n;i++){ while(u<m-1&&acc+D[u]<t+step*0.5f){ acc+=D[u]; u++; } o[i]=J[u]; t+=step; }
    /* a jaw has mass: straight between neighbours chatters at 30 Hz */
    for(int pass=0;pass<2;pass++){ float pv=o[0]; for(int i=0;i<n;i++){ const float c=o[i],nx=o[i+1<n?i+1:i]; o[i]=(pv+2.f*c+nx)*0.25f; pv=c; } }
    *nOut=n; return o; }
/* (step 26) the line in the language chosen, looked up whole before it's broken into rows: a conversation's file is translated a line at a time, like any other text.
 * Rows are counted in letters, which past ASCII aren't bytes; a row never ends inside one; and it can end beside a letter of the scripts that have no spaces (tvBreak's rule) */
static void capSet(const char*words,float secs,int who){ g_cap.n=0; g_cap.on=0; if(!words||!words[0]||g_faceCapOff) return; words=langWord(words); const char*p=words; const int cols=31;
    while(*p&&g_cap.n<12){ while(*p==' ') p++;
        const char*e=p,*br=NULL; unsigned was=0; int k=0,u=1;
        while(*e&&k<cols){ const unsigned c=tvLetter(e,&u); if(c==' '||(k&&(TV_WIDECH(c)||TV_WIDECH(was))&&!tvNoStart(c)&&!tvNoEnd(was))) br=e; e+=u; was=c; k++; }
        if(*e&&*e!=' '){ const unsigned c=tvLetter(e,&u); if((TV_WIDECH(c)||TV_WIDECH(was))&&!tvNoStart(c)&&!tvNoEnd(was)) br=e; if(br) e=br; }
        int n=(int)(e-p); if(n>(int)sizeof g_cap.L[0]-1) n=(int)sizeof g_cap.L[0]-1;
        memcpy(g_cap.L[g_cap.n],p,(size_t)n); g_cap.L[g_cap.n][n]=0; g_cap.n++; p=e; }
    g_cap.len=secs; g_cap.t=secs; g_cap.who=who; g_cap.on=1; }
static void faceLineEnd(Face*f){ if(f->sfx>=0) sfxStop(f->sfx); f->sfx=-1; if(f->uses&&f->voice>=0&&f->voice<g_fvN&&g_fv[f->voice].uses>0) g_fv[f->voice].uses--; f->uses=0; f->voice=-1;
    if(f->ownEnv) free(f->env);
    free(f->vis); f->env=f->vis=NULL; f->nEnv=f->nVis=0; f->ownEnv=0; f->lineOn=0; f->pending=0; f->lineLen=0.f; f->lineT=0.f; if(g_cap.on&&g_cap.who==f->body) g_cap.on=0; }
static void faceLineGo(Face*f){ f->pending=0; f->lineT=0.f; f->lineOn=1; f->rate=30.f; const FaceVoice*V=(f->voice>=0)?&g_fv[f->voice]:NULL; float secs=0.f;
    if(V&&!V->missing&&V->n>0){ secs=(float)V->n/(float)SFX_RATE; f->env=V->env; f->nEnv=V->nEnv; f->ownEnv=0; g_fv[f->voice].uses++; f->uses=1; g_fv[f->voice].idle=0;
        char nm[16]; snprintf(nm,sizeof nm,"v:%d",f->voice); float pos[3]; int spatial=0;
        if(!g_talkOn){ if(f->body<0){ pos[0]=g_rvx; pos[1]=g_feet.y+1.5f; pos[2]=g_rvz; } else { const Slot*s=&g_slots[f->body]; pos[0]=s->x; pos[1]=g_slotFeet[f->body]+1.5f; pos[2]=s->z; } spatial=1; }
        f->sfx=sfxPlayBus(BUS_VOICE,nm,1.f,spatial?pos:NULL,0); }
    else { f->voice=-1; if(!f->words[0]){ f->lineOn=0; f->lineLen=0.f; return; } secs=0.35f+0.066f*(float)strlen(f->words); if(secs<1.f) secs=1.f; }
    if(f->words[0]) f->vis=faceVisOf(f->words,secs,&f->nVis);
    f->lineLen=secs; capSet(f->words,secs,f->body); }
/* ---- the table ---- */
static float apiBodySay(int i,const char*sound,const char*words){ Face*f=faceGet(i); if(!f) return 0.f; faceLineEnd(f); snprintf(f->words,sizeof f->words,"%s",words?words:"");
    f->voice=(sound&&sound[0]&&strcmp(sound,"-"))?voiceAsk(sound):-1; f->pending=1;
    if(f->voice<0||SDL_AtomicGet(&g_fv[f->voice].state)==ST_HERE) faceLineGo(f);
    return f->lineLen; }
static void apiBodySayData(int i,const float*env,const float*vis,int n,float rate){ Face*f=faceGet(i); if(!f||n<1||rate<=0.f) return; faceLineEnd(f); f->words[0]=0;
    if(env){ f->env=(float*)malloc((size_t)n*sizeof(float)); memcpy(f->env,env,(size_t)n*sizeof(float)); f->nEnv=n; f->ownEnv=1; }
    if(vis){ f->vis=(float*)malloc((size_t)n*sizeof(float)); memcpy(f->vis,vis,(size_t)n*sizeof(float)); f->nVis=n; }
    f->rate=rate; f->lineOn=1; f->lineT=0.f; f->lineLen=(float)n/rate; }
static float apiBodySaying(int i){ const Face*f=faceOf(i); if(!f) return 0.f; if(f->pending) return 1.f; if(f->lineOn) return f->lineLen-f->lineT; return 0.f; }
static void apiBodyShut(int i){ Face*f=faceOf(i); if(f) faceLineEnd(f); }
static void apiBodyMouth(int i,float open){ Face*f=faceGet(i); if(f) f->fed=open<0.f?0.f:(open>1.f?1.f:open); }
static int apiBodyShape(int i,const char*name,float amount,float seconds){ Face*f=faceGet(i); if(!f||!name) return 0; FaceShape*S=NULL; for(int k=0;k<f->nS;k++) if(!strcmp(f->s[k].name,name)){ S=&f->s[k]; break; }
    if(!S){ if(f->nS>=FACE_SHAPES){ printf("[face] more than %d shapes on one face: %s isn't on\n",FACE_SHAPES,name); fflush(stdout); return 0; } S=&f->s[f->nS++]; memset(S,0,sizeof *S); snprintf(S->name,sizeof S->name,"%s",name);
        Pack*P=f->body<0?g_heroPk:&g_packs[g_slots[f->body].pack]; if(P&&f->pk!=P) faceBind(f,P); else if(P) S->n=faceIdx(P,name,S->idx,8); }
    S->want=amount; S->rate=seconds>0.f?fabsf(amount-S->now)/seconds:0.f; if(seconds<=0.f) S->now=amount; return S->n; }
static void apiBodyLook(int i,float x,float y,float z){ Face*f=faceGet(i); if(!f) return; f->look=1; f->lookP[0]=x; f->lookP[1]=y; f->lookP[2]=z; }
static void apiBodyLookBody(int i,int other){ Face*f=faceGet(i); if(!f) return; f->look=2; f->lookBody=other; }
static void apiBodyLookOff(int i){ Face*f=faceOf(i); if(f) f->look=0; }
static int apiAttach(int body,int prop,const char*bone,float x,float y,float z,float yaw,float pitch,float roll,float scale);   /* engine/gameworld.inc */
static void apiCaptions(int on){ g_faceCapOff=!on; if(!on) g_cap.on=0; }
static void faceBodyGone(int i){ Face*f=(i>=0&&i<1024)?faceOf(i):NULL; if(!f) return; faceLineEnd(f); f->on=0; g_faceOf[i]=0; g_faceN--; }
/* where a body's eyes are, from what the rules know of it: its place, its yaw, and how high the eyes are at rest. Not from a drawn pose, so a lens or a look that uses it comes out the same at any tick pattern */
static float faceEyeHeight(Pack*P){ if(P->faceEyeOK) return P->faceEyeH; P->faceEyeOK=1; P->faceEyeH=1.6f; if(!P->nNodes) return P->faceEyeH; M4*w=(M4*)malloc((size_t)P->nNodes*sizeof(M4));
    for(unsigned i=0;i<P->nNodes;i++){ M4 lc; m4_trs(lc,P->nodes[i].t,P->nodes[i].q,P->nodes[i].s); if(P->nodes[i].parent<0) memcpy(w[i],lc,64); else m4_mul(w[i],w[P->nodes[i].parent],lc); }
    const int a=P->fmEyeL,b=P->fmEyeR,h=P->pb[5]; if(a>=0&&b>=0) P->faceEyeH=(0.5f*(w[a][13]+w[b][13])-P->mny)*P->scale; else if(h>=0) P->faceEyeH=(w[h][13]-P->mny)*P->scale; free(w); return P->faceEyeH; }
static Pack* faceBodyAt(int b,float*eye,float*yaw){ Pack*P; float x,z,feet,ya; if(b<0){ P=g_heroPk; x=g_rvx; z=g_rvz; feet=g_feet.y; ya=g_rvHead; } else if(b<g_NS&&b<1024){ const Slot*s=&g_slots[b]; P=&g_packs[s->pack]; x=s->x; z=s->z; feet=g_slotFeet[b]; ya=s->rotY; } else return NULL;
    if(eye){ const Face*f=faceOf(b); eye[0]=x; eye[1]=feet+((f&&f->eyeYOK)?f->eyeY:(P?faceEyeHeight(P):1.6f)); eye[2]=z; } if(yaw) *yaw=ya; return P; }
/* (step 20, 11-04) after a body's posed: is it sitting? Its eyes well below where they stand (under 85% of it) says so, and then its look, and the conversation's lens, go to where they
 * really are and not to its standing head. It's latched, in steps of a twentieth of a unit, and let go only once it's nearly upright again: a standing body's sway never reaches the
 * lens, so a standing conversation is what it always was, and which frame did the latching doesn't change what's latched */
static void facePosed(Face*f,Pack*P,M4*w){ const int a=P->fmEyeL,b=P->fmEyeR,h=P->pb[5]; float y; if(a>=0&&b>=0) y=0.5f*(w[a][13]+w[b][13]); else if(h>=0) y=w[h][13]; else return;
    y=(y-P->mny)*P->scale; const float rest=faceEyeHeight(P),q=floorf(y*20.f+0.5f)/20.f;
    if(!f->eyeYOK){ if(y<0.85f*rest){ f->eyeYOK=1; f->eyeY=q; } }
    else if(y>0.93f*rest) f->eyeYOK=0;
    else if(fabsf(y-f->eyeY)>0.12f) f->eyeY=q; }
static float faceRand(Face*f){ f->dice=f->dice*1664525u+1013904223u; return (float)(f->dice>>16&0x7fffu)/32767.f; }
/* ---- once a tick, after the game's: a face has a clock of its own, and everything it does moves on it ---- */
static void faceStep(void){ if(!g_faceN&&!g_fvN) return; const float dt=simStepS(); int installed=0;
    for(int k=0;k<g_fvN;k++){ FaceVoice*V=&g_fv[k]; const int st=SDL_AtomicGet(&V->state);
        if(st==ST_AWAY&&V->want) voiceBegin(k);
        else if(st==ST_READ&&!installed){ installed=1; voiceInstall(k); }
        else if(st==ST_HERE&&!V->uses&&++V->idle>=VOICE_IDLE) voiceDrop(k); }
    if(g_cap.on){ g_cap.t-=dt; if(g_cap.t<=0.f) g_cap.on=0; }
    for(int n=0;n<FACE_POOL;n++){ Face*f=&g_faces[n]; if(!f->on) continue; f->t+=dt;
        for(int k=0;k<f->nS;k++){ FaceShape*S=&f->s[k]; if(S->now==S->want) continue; const float d=S->want-S->now,st=S->rate*dt; S->now=(st<=0.f||fabsf(d)<=st)?S->want:S->now+(d>0.f?st:-st); }
        /* a blink: every few seconds, and a sixth of a second long */
        if(f->blinkT>0.f){ f->blinkT+=dt; const float u=f->blinkT/0.16f; if(u>=1.f){ f->blinkT=0.f; f->blink=0.f; f->blinkAt=f->t+1.8f+4.4f*faceRand(f); } else f->blink=sinf(3.14159f*u); }
        else if(f->t>=f->blinkAt) f->blinkT=1e-4f;
        if(f->nodT>0.f){ f->nodT+=dt; if(f->nodT>=0.7f) f->nodT=0.f; }
        /* the line */
        if(f->pending&&f->voice>=0&&SDL_AtomicGet(&g_fv[f->voice].state)==ST_HERE) faceLineGo(f);
        float want=0.f;
        if(f->lineOn){ f->lineT+=dt; if(f->lineT>=f->lineLen){ faceLineEnd(f); } else { const float t=f->lineT;
            if(f->env||f->vis){ const float a=f->env?faceTrack(f->env,f->nEnv,t,f->rate):1.f,v=f->vis?faceTrack(f->vis,f->nVis,t,f->rate):1.f;
                if(f->env&&f->vis) want=v*(0.30f+0.70f*powf(a,0.55f));          /* speech's RMS lives at 0.2 to 0.4: linear leaves it mumbling */
                else if(f->env) want=a<0.03f?0.f:0.20f+0.80f*powf(a,0.55f);
                else want=v*0.85f; } } }
        if(f->fed>=0.f){ if(f->fed>want) want=f->fed; f->fed=-1.f; }
        { const float k=1.f-expf(-dt/(want>f->open?0.025f:0.05f)); f->open+=(want-f->open)*k; }
        /* the look: a turn is a critically damped spring, never a snap */
        float hy=0.f,hp=0.f; f->yawT=f->pitchT=0.f;
        if(f->look){ float me[3],to[3],by=0.f; if(faceBodyAt(f->body,me,&by)){ int ok=1; if(f->look==1){ to[0]=f->lookP[0]; to[1]=f->lookP[1]; to[2]=f->lookP[2]; } else if(!faceBodyAt(f->lookBody,to,NULL)) ok=0;
            const float dx=to[0]-me[0],dy=to[1]-me[1],dz=to[2]-me[2],d=sqrtf(dx*dx+dz*dz);
            if(ok&&d>1e-3f){ const float r=dx*cosf(by)-dz*sinf(by),fw=dx*sinf(by)+dz*cosf(by); f->yawT=atan2f(r,fw); f->pitchT=atan2f(dy,d)*LOOK_PITCH_SIGN;
                const float wgt=f->lineOn?0.72f:1.f; hy=f->yawT*wgt; hp=f->pitchT*wgt; } } }
        { const float W=10.f,h=dt*0.25f; for(int s=0;s<4;s++){ f->S[2]+=h*(W*W*(hy-f->S[0])-2.f*W*f->S[2]); f->S[3]+=h*(W*W*(hp-f->S[1])-2.f*W*f->S[3]); f->S[0]+=h*f->S[2]; f->S[1]+=h*f->S[3]; }
          f->aim+=((f->look?1.f:0.f)-f->aim)*(1.f-expf(-dt/0.25f)); } } }
/* ---- the pose: on what the clips did, and before the bones become matrices. It only reads the face's clock ---- */
static float faceStress(const Face*f,float t){ if(!f->env||f->nEnv<2) return 0.f; float s=0.f;
    for(int k=0;k<12;k++){ const float tau=((float)k+0.5f)/30.f,r=faceTrack(f->env,f->nEnv,t-tau,f->rate)-faceTrack(f->env,f->nEnv,t-tau-0.10f,f->rate); if(r>0.f) s+=r*sinf(3.14159f*tau/0.40f); }
    s=(s-1.7f)*1.25f; return s<0.f?0.f:(s>1.2f?1.2f:s); }
static void faceLayer(Face*f,Pack*P,float Q[][4]){ if(f->pk!=P) faceBind(f,P); float off[4];
    if(P->nMorph) memset(f->w,0,(size_t)P->nMorph*sizeof(float));
    for(int k=0;k<f->nS;k++) for(int j=0;j<f->s[k].n;j++) f->w[f->s[k].idx[j]]=f->s[k].now;
    for(int j=0;j<f->nBlink;j++) f->w[f->iBlink[j]]=f->blink;
    /* the jaw: a bone if it's got one, a shape if it hasn't. Never both */
    if(P->fmJaw>=0){ if(g_jawForce<0.f){ qaxis(off,1,0,0,f->open*JAW_MAX_RAD); qmul(Q[P->fmJaw],Q[P->fmJaw],off); } }   /* REVA_JAWFORCE, the bench's override, has done it already */
    else if(f->nJaw){ for(int j=0;j<f->nJaw;j++) f->w[f->iJaw[j]]=f->open; for(int j=0;j<f->nSmile;j++) if(f->w[f->iSmile[j]]<0.30f*f->open) f->w[f->iSmile[j]]=0.30f*f->open; }
    const int nk=P->pb[4],hd=P->pb[5]; const float t=f->t,me=(float)(f->body+1)*1.7f; float fc[3],yc=0.f,pc=0.f;
    /* a look is where it says, whatever the clip's doing with the head, and a little of the clip's nod is kept */
    if(f->aim>0.001f&&perfHeadFwd(P,Q,fc)){ yc=atan2f(fc[0],fc[2]); pc=atan2f(fc[1],sqrtf(fc[0]*fc[0]+fc[2]*fc[2])); }
    float yaw=f->S[0]-0.8f*f->aim*yc,pitch=f->S[1]+0.8f*f->aim*pc,roll=0.f,sway=0.f;
    if(yaw>3.14159f) yaw-=6.28318f;
    if(yaw<-3.14159f) yaw+=6.28318f;
    if(yaw>0.70f) yaw=0.70f;
    if(yaw<-0.70f) yaw=-0.70f;
    if(pitch>0.35f) pitch=0.35f;
    if(pitch<-0.35f) pitch=-0.35f;
    /* the speaker moves with their words */
    if(f->lineOn&&f->env){ const float lt=f->lineT; float va=0.f; for(int k=0;k<15;k++) va+=faceTrack(f->env,f->nEnv,lt-(float)k/30.f,f->rate); va=va/15.f*2.4f; if(va>1.f) va=1.f;
        const float dy=va*(0.080f*sinf(1.21f*t+me)+0.035f*sinf(2.63f*t+2.f*me)); yaw+=dy; sway=dy; roll+=va*0.030f*sinf(0.97f*t+1.3f*me); pitch+=va*0.020f*sinf(1.73f*t+0.7f*me)+0.075f*faceStress(f,lt); }
    /* a listener nods as the speaker finishes */
    if(f->nodT>0.f) pitch+=0.060f*sinf(3.14159f*f->nodT/0.7f);
    /* nobody holds dead still */
    yaw+=0.022f*sinf(0.37f*t+me*1.2f); pitch+=0.012f*sinf(0.53f*t+me*0.8f); roll+=0.010f*sinf(0.29f*t+me*0.4f);
    if(nk>=0){ qaxis(off,1,0,0,pitch*0.45f); qmul(Q[nk],Q[nk],off); qaxis(off,0,1,0,yaw*0.45f); qmul(Q[nk],Q[nk],off); qaxis(off,0,0,1,roll*0.45f); qmul(Q[nk],Q[nk],off); }
    if(hd>=0){ qaxis(off,1,0,0,pitch*0.55f); qmul(Q[hd],Q[hd],off); qaxis(off,0,1,0,yaw*0.55f); qmul(Q[hd],Q[hd],off); qaxis(off,0,0,1,roll*0.55f); qmul(Q[hd],Q[hd],off); }
    /* the eyes lead: on the target while the head's on its way there, and steady against the nods */
    if(f->look){ float ey=f->yawT-yaw,ep=f->pitchT-pitch; if(ey>3.14159f) ey-=6.28318f; if(ey<-3.14159f) ey+=6.28318f; eyeAim(P,Q,ey,ep); if(P->nMorph) eyeMorphAim(P,f->w,ey,ep*LOOK_PITCH_SIGN); }
    /* the breath, and the chest turning a little with a speaker's head */
    { const int ch=(P->pb[3]>=0)?P->pb[3]:P->pb[2]; if(ch>=0){ qaxis(off,1,0,0,-0.014f*sinf(6.2832f*0.24f*t+me)); qmul(Q[ch],Q[ch],off); if(sway!=0.f){ qaxis(off,0,1,0,0.22f*sway); qmul(Q[ch],Q[ch],off); } } }
    { static const char*dbg=(const char*)1; if(dbg==(const char*)1) dbg=getenv("REVA_TALKDBG"); if(dbg&&g_tickTotal%30==0&&!g_noDraw){ printf("[face] tick %ld body %d %s: jaw bone %d shapes %d (jaw %d blink %d) shared %d open %.3f line %d t %.2f of %.2f env %d vis %d blink %.2f look %d aim %.2f hole %d\n",(long)g_tickTotal,f->body,P->label,P->fmJaw,P->nMorph,f->nJaw,f->nBlink,f->shared,f->open,f->lineOn,f->lineT,f->lineLen,f->nEnv,f->nVis,f->blink,f->look,f->aim,P->fmMouth>=0&&P->fmEyeL>=0&&P->fmEyeR>=0); fflush(stdout); } }
    if(f->shared&&P->nMorph) morphBlend(P,f->w,&f->wasAct); }
static float g_facePos[FACE_MAXV*3];
/* one mesh of the body being drawn: its own positions if its shapes have moved them, and the hole in its mouth */
static const float* faceMeshPos(Pack*P,unsigned mi,float*mouth){ Face*f=g_faceCur; mouth[6]=0.f; if(!f||f->pk!=P) return NULL;
    { const float op9=g_jawForce>=0.f?g_jawForce:f->open; if(op9>0.002f&&P->fmMouth>=0&&P->fmEyeL>=0&&P->fmEyeR>=0&&(int)mi!=P->fmInterior){ mouth[0]=P->mouthBind[0]; mouth[1]=P->mouthBind[1]; mouth[2]=P->mouthBind[2]; mouth[3]=P->mouthBind[5]; mouth[4]=P->mouthBind[3]; mouth[5]=P->mouthBind[4]; mouth[6]=op9>1.f?1.f:op9; mouth[7]=0.f; } }
    if(f->shared) return NULL;
    const PMesh*M=&P->meshes[mi]; int any=0; for(int m=0;m<P->nMorph&&!any;m++) if(P->morphMesh[m]==mi&&fabsf(f->w[m])>1e-3f) any=1;
    if(!any||M->verts>FACE_MAXV) return NULL;
    memcpy(g_facePos,P->pk+M->offPos,(size_t)M->verts*12);
    for(int m=0;m<P->nMorph;m++){ if(P->morphMesh[m]!=mi) continue; const float w=f->w[m]; if(fabsf(w)<=1e-3f) continue; const unsigned char*sd=P->pk+P->morphOff[m];
        for(unsigned s=0;s<P->morphSparse[m];s++){ unsigned vi; float dl[3]; memcpy(&vi,sd+s*16,4); memcpy(dl,sd+s*16+4,12); if(vi>=M->verts) continue; float*d=g_facePos+(size_t)vi*3; d[0]+=dl[0]*w; d[1]+=dl[1]*w; d[2]+=dl[2]*w; } }
    return g_facePos; }
/* the words, on the TV's grid and over any picture */
static void faceHud(void){ if(!g_cap.on||!g_cap.n) return; const int pages=(g_cap.n+2)/3; float u=1.f-g_cap.t/(g_cap.len>0.f?g_cap.len:1.f); if(u<0.f) u=0.f; int pg=(int)(u*(float)pages); if(pg>=pages) pg=pages-1; const int first=pg*3,n=g_cap.n-first>3?3:g_cap.n-first;
    const float row=22.f-(float)n; int wide=0; for(int l=0;l<n;l++){ const int w=tvLen(g_cap.L[first+l]); if(w>wide) wide=w; }
    if(TV_FIT) g_tvGroup=99; else { UiDraw D; memset(&D,0,sizeof D); D.col=((float)TV_COLS-(float)wide)*0.5f; D.row=row; D.cols=(float)wide; D.frac=(float)n; uiPanelDraw(&D); }
    for(int l=0;l<n;l++) tvTextC(row+(float)l,1,g_cap.L[first+l],1.f,1.f,0.92f);
    g_tvGroup=0; }

/* ---- A CONVERSATION: a text file, talk/<name>.talk, read each time it's started. Who's in which seat is the game's; the engine plays it: who says what, who looks at whom, the lens, the words on screen, a question put to the player.
 *   say <seat> <sound or -> <the words>       a line. - is no sound: the mouth moves on the words alone, for as long as they take to read
 *   shot close|over <seat> | two | wide | auto | free     the lens from now on. free hands it back to the game. auto is the default
 *   side -1                                   which side of the line between them the lens stays on
 *   look <seat> <seat>|off|auto               auto: a listener looks at whoever's speaking, and a speaker at who spoke last
 *   shape <seat> <name> <amount> [seconds]    a shape on a face
 *   anim <seat> <name> <value>                a value for that body's animation graph
 *   wait <seconds>    press    skip 0|1       a beat; until someone presses A; whether A cuts a line short (it does until you say)
 *   ask <label> <words>                       lines of these in a row are a menu. The answer goes to that label, and to GameExport.talkEvent
 *   attach <seat> <prop> <bone> [x y z [yaw pitch roll [scale]]]    detach <seat>    a model on a bone, and all of them off again
 *   :label    goto <label>    event <name>    end    # a comment
 * The lens comes from where the bodies are by the rules, and from where their eyes were last posed (facePosed), so a seated body's shot frames its real head; a body not yet posed is taken at its rest height.
 * On a split screen, E->talkView says which player's picture the lens goes in (step 20). */
#define TALK_MAXL 512
#define TALK_SEATS 8
#define TALK_MAXLABEL 64
enum { TK_SAY,TK_SHOT,TK_SIDE,TK_LOOK,TK_SHAPE,TK_ANIM,TK_WAIT,TK_PRESS,TK_SKIP,TK_ASK,TK_GOTO,TK_EVENT,TK_END,TK_ATTACH,TK_DETACH };
enum { TS_AUTO,TS_CLOSE,TS_OVER,TS_TWO,TS_WIDE,TS_FREE };
typedef struct { short kind,seat,other; float v,w; char a[32]; char s[96]; char t[168]; } TalkLine;
static struct { int on,n,pc,saying,last,wait,press,skip,asking,nAuto; TalkLine L[TALK_MAXL]; struct { char nm[32]; int pc; } lab[TALK_MAXLABEL]; int nLab; int seat[TALK_SEATS],used[TALK_SEATS],look[TALK_SEATS];
    int shot,autoShot,subj,other; float side,waitT; EngineUi ui; int hadLens; float saveE[3],saveA[3],saveFov; char name[56];
    int view; float saveVE[3],saveVA[3]; } g_talk;   /* (step 20) which picture of a split screen the conversation's lens goes in; -1 is the whole screen's */
static void apiTalkSeat(int seat,int body){ if(seat>=0&&seat<TALK_SEATS){ g_talk.seat[seat]=body; g_talk.used[seat]=1; } }
static void apiTalkView(int view){ g_talk.view=view<0?-1:view>3?3:view; }
/* the lens the conversation sets, and the one it hands back: the whole screen's, or one player's picture of a split screen (the first picture's lens is also the screen's) */
static void talkLensPut(const float*e,const float*c,float fov){ g_tune.lensFov=fov;
    if(g_talk.view>=0&&g_gameViews.n>1){ memcpy(g_gameViews.e[g_talk.view],e,12); memcpy(g_gameViews.a[g_talk.view],c,12); if(g_talk.view!=0) return; }
    g_gameLens.on=1; memcpy(g_gameLens.e,e,12); memcpy(g_gameLens.a,c,12); }
static void talkLensBack(void){ g_gameLens.on=g_talk.hadLens; memcpy(g_gameLens.e,g_talk.saveE,12); memcpy(g_gameLens.a,g_talk.saveA,12); g_tune.lensFov=g_talk.saveFov;
    if(g_talk.view>=0&&g_gameViews.n>1){ memcpy(g_gameViews.e[g_talk.view],g_talk.saveVE,12); memcpy(g_gameViews.a[g_talk.view],g_talk.saveVA,12); } }
static int apiTalking(void){ return g_talk.on; }
static void talkLook(int k,int body){ if(k==-1) apiBodyLookOff(body); else if(k>=0&&k<TALK_SEATS&&g_talk.used[k]) apiBodyLookBody(body,g_talk.seat[k]); }
static void apiTalkStop(void){ if(!g_talk.on) return; g_talk.on=0; g_talkOn=0;
    for(int s=0;s<TALK_SEATS;s++){ if(!g_talk.used[s]) continue; Face*f=faceOf(g_talk.seat[s]); if(!f) continue; faceLineEnd(f); f->look=0; for(int k=0;k<f->nS;k++){ f->s[k].want=0.f; f->s[k].rate=2.f; } }
    talkLensBack(); g_cap.on=0; }
static int talkParse(const char*name){ char p[128],b[512]; snprintf(p,sizeof p,"talk/%.100s.talk",name); FILE*f=fopen(p,"rb"); if(!f){ printf("[talk] no conversation called %s\n",p); fflush(stdout); return 0; } clubOpened(p);
    g_talk.n=0; g_talk.nLab=0; int ln=0;
    while(fgets(b,sizeof b,f)){ ln++; char*q=b; while(*q==' '||*q=='\t') q++; size_t e=strlen(q); while(e&&(q[e-1]=='\n'||q[e-1]=='\r'||q[e-1]==' ')) q[--e]=0; if(!*q||*q=='#') continue;
        if(*q==':'){ if(g_talk.nLab<TALK_MAXLABEL){ snprintf(g_talk.lab[g_talk.nLab].nm,32,"%s",q+1); g_talk.lab[g_talk.nLab++].pc=g_talk.n; } continue; }
        if(g_talk.n>=TALK_MAXL){ printf("[talk] %s: more than %d lines, and the rest aren't there\n",p,TALK_MAXL); break; }
        TalkLine*L=&g_talk.L[g_talk.n]; memset(L,0,sizeof *L); char w0[16]="",w1[32]="",w2[96]=""; int c=0,seat=0; sscanf(q,"%15s%n",w0,&c); const char*r=q+c; while(*r==' ') r++;
        if(!strcmp(w0,"say")){ int k=0; L->kind=TK_SAY; if(sscanf(r,"%d %95s %n",&seat,w2,&k)<2){ printf("[talk] %s:%d: say <seat> <sound or -> <words>\n",p,ln); continue; } L->seat=(short)seat; snprintf(L->s,sizeof L->s,"%s",w2); snprintf(L->t,sizeof L->t,"%s",r+k); }
        else if(!strcmp(w0,"shot")){ L->kind=TK_SHOT; seat=-1; sscanf(r,"%31s %d",w1,&seat); L->seat=(short)seat; L->other=!strcmp(w1,"close")?TS_CLOSE:!strcmp(w1,"over")?TS_OVER:!strcmp(w1,"two")?TS_TWO:!strcmp(w1,"wide")?TS_WIDE:!strcmp(w1,"free")?TS_FREE:TS_AUTO; }
        else if(!strcmp(w0,"side")){ L->kind=TK_SIDE; L->v=(float)atof(r); }
        else if(!strcmp(w0,"look")){ L->kind=TK_LOOK; sscanf(r,"%d %31s",&seat,w1); L->seat=(short)seat; L->other=!strcmp(w1,"off")?-1:!strcmp(w1,"auto")?-2:(short)atoi(w1); }
        else if(!strcmp(w0,"shape")){ L->kind=TK_SHAPE; L->w=0.f; sscanf(r,"%d %31s %f %f",&seat,L->a,&L->v,&L->w); L->seat=(short)seat; }
        else if(!strcmp(w0,"anim")){ L->kind=TK_ANIM; sscanf(r,"%d %31s %f",&seat,L->a,&L->v); L->seat=(short)seat; }
        else if(!strcmp(w0,"wait")){ L->kind=TK_WAIT; L->v=(float)atof(r); }
        else if(!strcmp(w0,"press")) L->kind=TK_PRESS;
        else if(!strcmp(w0,"skip")){ L->kind=TK_SKIP; L->v=(float)atof(r); }
        else if(!strcmp(w0,"ask")){ int k=0; L->kind=TK_ASK; sscanf(r,"%31s %n",L->a,&k); snprintf(L->t,sizeof L->t,"%s",r+k); }
        else if(!strcmp(w0,"goto")){ L->kind=TK_GOTO; snprintf(L->a,sizeof L->a,"%s",r); }
        else if(!strcmp(w0,"event")){ L->kind=TK_EVENT; snprintf(L->a,sizeof L->a,"%s",r); }
        else if(!strcmp(w0,"end")) L->kind=TK_END;
        else if(!strcmp(w0,"attach")){ float v9[7]={0,0,0,0,0,0,1}; L->kind=TK_ATTACH; sscanf(r,"%d %31s %95s %f %f %f %f %f %f %f",&seat,L->a,L->s,&v9[0],&v9[1],&v9[2],&v9[3],&v9[4],&v9[5],&v9[6]); L->seat=(short)seat; memcpy(L->t,v9,sizeof v9); }
        else if(!strcmp(w0,"detach")){ L->kind=TK_DETACH; sscanf(r,"%d",&seat); L->seat=(short)seat; }
        else { printf("[talk] %s:%d: what's '%s'?\n",p,ln,w0); continue; }
        g_talk.n++; }
    fclose(f); return g_talk.n>0; }
static int apiTalkStart(const char*name){ if(g_talk.on) apiTalkStop(); if(!name||!talkParse(name)) return 0; snprintf(g_talk.name,sizeof g_talk.name,"%s",name);
    g_talk.on=1; g_talkOn=1; g_talk.pc=0; g_talk.saying=-1; g_talk.last=-1; g_talk.wait=0; g_talk.press=0; g_talk.skip=1; g_talk.asking=0; g_talk.nAuto=0; g_talk.shot=TS_AUTO; g_talk.autoShot=TS_TWO; g_talk.subj=-1; g_talk.other=-1; g_talk.side=1.f; g_talk.waitT=0.f; memset(&g_talk.ui,0,sizeof g_talk.ui);
    for(int s=0;s<TALK_SEATS;s++) g_talk.look[s]=-2;
    g_talk.hadLens=g_gameLens.on; memcpy(g_talk.saveE,g_gameLens.e,12); memcpy(g_talk.saveA,g_gameLens.a,12); g_talk.saveFov=g_tune.lensFov;
    if(g_talk.view>=0){ memcpy(g_talk.saveVE,g_gameViews.e[g_talk.view],12); memcpy(g_talk.saveVA,g_gameViews.a[g_talk.view],12); }
    /* every line it might say starts reading now, so there's nothing to wait for when it gets there */
    for(int i=0;i<g_talk.n;i++){ const TalkLine*L=&g_talk.L[i]; if(L->kind==TK_SAY&&strcmp(L->s,"-")) voiceAsk(L->s); }
    return 1; }
static int talkAnyA(void){ for(int k=0;k<4;k++) if(g_pads[k].connected&&(g_pads[k].pressed&PAD_A)) return 1; return 0; }
static int talkOtherSeat(int s){ if(g_talk.last>=0&&g_talk.last!=s) return g_talk.last; for(int k=0;k<TALK_SEATS;k++) if(k!=s&&g_talk.used[k]) return k; return -1; }
/* the lens, each tick: it follows them, and only ever cuts when a line says so */
static void talkLens(void){ if(g_talk.shot==TS_FREE){ talkLensBack(); return; }
    int a=g_talk.subj; if(a<0||a>=TALK_SEATS||!g_talk.used[a]){ for(a=0;a<TALK_SEATS&&!g_talk.used[a];a++){} if(a>=TALK_SEATS) return; }
    int b=g_talk.other; if(b<0||b>=TALK_SEATS||!g_talk.used[b]||b==a) b=talkOtherSeat(a);
    float A[3],B[3]; Pack*PA=faceBodyAt(g_talk.seat[a],A,NULL); if(!PA) return; const float h=faceEyeHeight(PA); int two=1; if(b<0||!faceBodyAt(g_talk.seat[b],B,NULL)){ two=0; float y=0.f; faceBodyAt(g_talk.seat[a],NULL,&y); B[0]=A[0]+sinf(y); B[1]=A[1]; B[2]=A[2]+cosf(y); }
    float dx=B[0]-A[0],dz=B[2]-A[2],d=sqrtf(dx*dx+dz*dz); if(d<1e-3f){ dx=0.f; dz=1.f; d=1.f; } dx/=d; dz/=d;
    /* one side of the line between them, whichever way round the shot is, so nobody seems to swap sides on a cut */
    float nx=-dz,nz=dx; if(a>b&&two){ nx=-nx; nz=-nz; } nx*=g_talk.side; nz*=g_talk.side;
    float e[3],c[3],fov=30.f; int shot=g_talk.shot==TS_AUTO?g_talk.autoShot:g_talk.shot; if(!two&&(shot==TS_OVER||shot==TS_TWO)) shot=TS_CLOSE;
    /* (step 20) one of them's sitting: their eyes are far apart in height. An over-the-shoulder then has the other's body between the lens and the face, so it's a clean single; and a two-shot frames between their heights, from a little further back. Two standing people are framed as they always were */
    const float dyAB=two?fabsf(A[1]-B[1]):0.f; const int sat=dyAB>0.15f*h; if(sat&&shot==TS_OVER) shot=TS_CLOSE;
    if(shot==TS_CLOSE){ const float D=0.685f*h; fov=28.f; e[0]=A[0]+dx*D*0.90f+nx*D*0.42f; e[1]=A[1]; e[2]=A[2]+dz*D*0.90f+nz*D*0.42f; c[0]=A[0]; c[1]=A[1]-0.03f*h; c[2]=A[2]; }
    else if(shot==TS_OVER){ fov=32.f; e[0]=B[0]+dx*0.62f*h+nx*0.24f*h; e[1]=B[1]+0.05f*h; e[2]=B[2]+dz*0.62f*h+nz*0.24f*h; c[0]=A[0]; c[1]=A[1]-0.05f*h; c[2]=A[2]; }
    else { const float my=sat?0.5f*(A[1]+B[1]):A[1]; const float D=(d+0.5f*h+(sat?dyAB:0.f))/0.7128f*(shot==TS_WIDE?2.2f:1.f); fov=shot==TS_WIDE?36.f:30.f; const float mx=0.5f*(A[0]+B[0]),mz=0.5f*(A[2]+B[2]); e[0]=mx+nx*D; e[1]=my+(shot==TS_WIDE?0.25f*h:0.f); e[2]=mz+nz*D; c[0]=mx; c[1]=my-(shot==TS_WIDE?0.35f*h:0.12f*h); c[2]=mz; }
    /* a wall between them and the lens: it comes in front of the wall */
    { float t=1.f; if(spRay(c,e,&t)&&t<1.f){ const float k=t*0.92f>0.15f?t*0.92f:0.15f; for(int i=0;i<3;i++) e[i]=c[i]+(e[i]-c[i])*k; } }
    talkLensPut(e,c,fov);
    { static const char*dbg=(const char*)1; if(dbg==(const char*)1) dbg=getenv("REVA_TALKDBG"); if(dbg&&g_tickTotal%30==0){ const Face*fa=faceOf(g_talk.seat[a]); printf("[talk] tick %ld shot %d on seat %d: A %.2f %.2f %.2f B %.2f %.2f %.2f h %.2f lens %.2f %.2f %.2f to %.2f %.2f %.2f fov %.0f; its head yaw %.2f pitch %.2f target %.2f %.2f\n",(long)g_tickTotal,shot,a,A[0],A[1],A[2],B[0],B[1],B[2],h,e[0],e[1],e[2],c[0],c[1],c[2],fov,fa?fa->S[0]:0.f,fa?fa->S[1]:0.f,fa?fa->yawT:0.f,fa?fa->pitchT:0.f); fflush(stdout); } } }
/* once a tick, after the game's */
static void talkStep(void){ if(!g_talk.on) return; const float dt=simStepS();
    if(g_talk.saying>=0){ const int body=g_talk.seat[g_talk.saying]; Face*f=faceOf(body);
        if(g_talk.skip&&talkAnyA()&&f) faceLineEnd(f);
        if(f&&(f->lineOn||f->pending)){ talkLens(); return; }
        /* it's finished: half the time, those listening nod */
        for(int s=0;s<TALK_SEATS;s++){ if(!g_talk.used[s]||s==g_talk.saying) continue; Face*h=faceOf(g_talk.seat[s]); if(h&&h->look==2&&perfRnd(s,g_talk.pc,9)<0.5f) h->nodT=1e-4f; }
        g_talk.last=g_talk.saying; g_talk.saying=-1; g_talk.waitT=0.22f; }
    if(g_talk.waitT>0.f){ g_talk.waitT-=dt; if(g_talk.waitT>0.f){ talkLens(); return; } }
    if(g_talk.press){ if(!talkAnyA()){ talkLens(); return; } g_talk.press=0; }
    if(g_talk.asking){ int i=g_talk.pc,go=-1; apiUiBegin(&g_talk.ui,-1,5.f,17.f,25.f,6.f); for(;i<g_talk.n&&g_talk.L[i].kind==TK_ASK;i++) if(apiUiButton(g_talk.L[i].t)&&go<0) go=i; apiUiEnd();
        if(go<0){ talkLens(); return; } const char*lab=g_talk.L[go].a; g_talk.asking=0; g_talk.pc=i; for(int k=0;k<g_talk.nLab;k++) if(!strcmp(g_talk.lab[k].nm,lab)) g_talk.pc=g_talk.lab[k].pc; if(g_game->talkEvent) g_game->talkEvent(lab); }
    for(int ops=0;ops<128;ops++){ if(g_talk.pc>=g_talk.n){ apiTalkStop(); return; } const TalkLine*L=&g_talk.L[g_talk.pc]; const int inSeat=L->seat>=0&&L->seat<TALK_SEATS&&g_talk.used[L->seat]; const int body=inSeat?g_talk.seat[L->seat]:-1;
        switch(L->kind){
        case TK_SAY: g_talk.pc++; if(!inSeat){ printf("[talk] %s: seat %d is empty\n",g_talk.name,L->seat); continue; }
            { const int s=L->seat,to=talkOtherSeat(s);
              for(int k=0;k<TALK_SEATS;k++){ if(!g_talk.used[k]) continue; const int lk=g_talk.look[k]; if(lk!=-2){ talkLook(lk,g_talk.seat[k]); continue; } if(k!=s) talkLook(s,g_talk.seat[k]); else if(to>=0) talkLook(to,g_talk.seat[k]); }
              /* left to itself: both of them first, then over a shoulder on whoever's speaking, and every third change a close-up. Nothing cuts while the same one carries on */
              if(g_talk.shot==TS_AUTO&&(g_talk.subj!=s||!g_talk.nAuto)){ g_talk.other=to; g_talk.subj=s; g_talk.autoShot=!g_talk.nAuto?TS_TWO:(g_talk.nAuto%3==2?TS_CLOSE:TS_OVER); g_talk.nAuto++; }
              g_talk.saying=s; apiBodySay(body,L->s,L->t); } talkLens(); return;
        case TK_SHOT: g_talk.pc++; g_talk.shot=L->other; if(L->seat>=0) { g_talk.subj=L->seat; g_talk.other=talkOtherSeat(L->seat); } continue;
        case TK_SIDE: g_talk.pc++; g_talk.side=L->v<0.f?-1.f:1.f; continue;
        case TK_LOOK: g_talk.pc++; if(inSeat){ g_talk.look[L->seat]=L->other; if(L->other!=-2) talkLook(L->other,body); } continue;
        case TK_SHAPE: g_talk.pc++; if(inSeat) apiBodyShape(body,L->a,L->v,L->w); continue;
        case TK_ANIM: g_talk.pc++; if(inSeat){ if(body<0) apiAnimSet(L->a,L->v); else apiBodyAnimSet(body,L->a,L->v); } continue;
        case TK_WAIT: g_talk.pc++; g_talk.waitT=L->v; talkLens(); return;
        case TK_PRESS: g_talk.pc++; g_talk.press=1; talkLens(); return;
        case TK_SKIP: g_talk.pc++; g_talk.skip=L->v!=0.f; continue;
        case TK_ASK: g_talk.asking=1; talkLens(); return;
        case TK_ATTACH: g_talk.pc++; if(inSeat){ float v9[7]; memcpy(v9,L->t,sizeof v9); apiAttach(body,apiProp(L->a),L->s,v9[0],v9[1],v9[2],v9[3],v9[4],v9[5],v9[6]); } continue;
        case TK_DETACH: g_talk.pc++; if(inSeat) apiDetachAll(body); continue;
        case TK_GOTO: g_talk.pc++; for(int k=0;k<g_talk.nLab;k++) if(!strcmp(g_talk.lab[k].nm,L->a)){ g_talk.pc=g_talk.lab[k].pc; break; } continue;
        case TK_EVENT: g_talk.pc++; if(g_game->talkEvent) g_game->talkEvent(L->a); continue;
        default: apiTalkStop(); return; } }
    { static int said=0; if(!said){ said=1; printf("[talk] %s: going round without saying anything\n",g_talk.name); fflush(stdout); } }
    talkLens(); }
