/* ---- THE STREAMING WORLD (11-03, engine plan Phase 6) --------------------------------------------------------------------------------------------------------------------
 * A set line `cell <stem> <x> <y> <z> <yaw> <scale>` is a prop that's only in memory while the hero is near it, so a world can be bigger than memory. `streaming <in> <out>` says how near: a cell comes in inside the first distance and goes away past the second.
 * A thread of its own reads the file, so the disk can't reach a frame. The main thread then puts in at most one a tick: the pack's tables, its geometry into memory that's already mapped, and its texture as a copy that doesn't wait for the GPU. Evicting one is also at most one a tick, and the renderer holds what it frees until no frame in flight can be using it.
 * Then the thread builds the cell's collision from it, so a world that's streamed can be walked on (engine/spans.inc); a cell is solid once that's done. Its cost is the question this is here to answer, so it says what it did every few seconds: [stream]. A cell the hero got too close to before it was whole is counted as late.
 * FAR DETAIL, queue step 12: `fardetail <stem> <distance>` names one pack with a mesh for each cell, named after it, in the world's own coordinates. It stays in memory, and a cell that isn't here shows that instead, out to that distance, so the horizon isn't empty and a cell doesn't appear from nothing. They cast no shadows and aren't solid. The stand-in's as cheap as you make it.
 * BODIES THAT STREAM, queue step 12: E->bodyPack asks for a body's pack by name while the game runs, from bodies/ and then packs/, and this thread reads it. engine/objects.inc has the rest of it.
 * ⛔ What's drawn and what's solid then depend on the disk, so anything measured or compared (REVA_FIXEDSTEP, the no-display build) does it all on the spot, and the receipt doesn't depend on how fast a disk is. A tape recorded live replays the same as long as nothing was late, because a cell is whole long before anyone reaches it. A set with cells is walked by spans (`climb`), because the old distance field would rebuild every time a cell came or went. */
#define ST_MAXQ 64
#define ST_LATE_NEAR 0.5f
enum { ST_AWAY, ST_READING, ST_READ, ST_HERE };
typedef struct { char stem[56]; float x,y,z,yaw,sc; SDL_atomic_t state; int slot,inst,late; unsigned char*bytes; size_t len; SpGrid*grid; SDL_atomic_t gridBusy; int said; float m[16]; int farMesh; float fx,fz,fr; } StCell;
#define ST_GRIDJOB 0x40000000
#define ST_BODYJOB 0x20000000
#define ST_VOICEJOB 0x10000000
static void voiceRead(int k);   /* engine/face.inc */
#define ST_MAXBODY 128
typedef struct { char stem[56],path[128]; SDL_atomic_t state; unsigned char*bytes; size_t len; int pack,done,want,idle,said,built; EngineBodySpec spec; } StBody;
static StBody g_stBody[ST_MAXBODY]; static int g_stNBody=0;
static struct { StCell*c; int n,cap; float rin,rout; int on,sync,nHere; SDL_Thread*th; SDL_atomic_t wr,rd,busy; int q[ST_MAXQ];
    long nIn,nOut,nLate,statT; double worstRead,worstIn,worstOut,sumIn; size_t bytesHere; SDL_atomic_t readUs; char farStem[56]; float farDist; int farPk; } g_st;

static unsigned char* stReadFile(const char*p,size_t*len);
/* a body's bytes: off the disk, or built from its measurements. Either way it's the thread's job */
static unsigned char* stBodyBytes(StBody*B,size_t*len){ if(!B->built) return stReadFile(B->path,len); unsigned char*b=peopleBytes(&B->spec,len);
    { const char*d=getenv("REVA_BUILDDUMP"); if(d){ FILE*f=fopen(d,"wb"); if(f){ fwrite(b,1,*len,f); fclose(f); } } }
    return b; }
static void stPath(const StCell*C,char*p,size_t cap){ snprintf(p,cap,"props/%s.rpack",C->stem); }
static unsigned char* stReadFile(const char*path,size_t*len){ FILE*f=fopen(path,"rb"); if(!f) return NULL; fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    unsigned char*b=n>0?(unsigned char*)malloc((size_t)n):NULL; if(b&&fread(b,1,(size_t)n,f)!=(size_t)n){ free(b); b=NULL; } fclose(f); *len=(size_t)n; return b; }
static int stThread(void*ud){ (void)ud;
    for(;;){ int rd=SDL_AtomicGet(&g_st.rd); if(rd==SDL_AtomicGet(&g_st.wr)){ SDL_Delay(2); continue; }
        int job=g_st.q[rd%ST_MAXQ];
        if(job&ST_VOICEJOB){ voiceRead(job&~ST_VOICEJOB); SDL_AtomicSet(&g_st.rd,rd+1); continue; }
        if(job&ST_BODYJOB){ StBody*B=&g_stBody[job&~ST_BODYJOB]; B->bytes=stBodyBytes(B,&B->len); SDL_AtomicSet(&B->state,ST_READ); SDL_AtomicSet(&g_st.rd,rd+1); continue; }
        StCell*C=&g_st.c[job&~ST_GRIDJOB]; char p[96]; Uint64 t0=SDL_GetPerformanceCounter();
        /* the pack's in place and won't move until this says it's done: its columns */
        if(job&ST_GRIDJOB){ C->grid=spGridOfPack(g_setPk[C->slot].pk,C->m); SDL_AtomicSet(&C->gridBusy,2); SDL_AtomicSet(&g_st.rd,rd+1); continue; }
        stPath(C,p,sizeof p);
        C->bytes=stReadFile(p,&C->len);
        { int us=(int)((SDL_GetPerformanceCounter()-t0)*1000000/SDL_GetPerformanceFrequency()); if(us>SDL_AtomicGet(&g_st.readUs)) SDL_AtomicSet(&g_st.readUs,us); }
        SDL_AtomicSet(&C->state,ST_READ); SDL_AtomicSet(&g_st.rd,rd+1); }
    return 0; }
/* a line of the set */
static void stCellLine(const char*ln){ char st[56]; float x,y,z,yaw=0.f,sc=1.f; if(sscanf(ln,"cell %55s %f %f %f %f %f",st,&x,&y,&z,&yaw,&sc)<4){ printf("[set] a cell needs: stem x y z [yaw] [scale]\n"); return; }
    if(g_st.n>=g_st.cap){ g_st.cap=g_st.cap?g_st.cap*2:1024; g_st.c=(StCell*)realloc(g_st.c,(size_t)g_st.cap*sizeof(StCell)); }
    StCell*C=&g_st.c[g_st.n++]; memset(C,0,sizeof *C); snprintf(C->stem,sizeof C->stem,"%s",st); C->x=x; C->y=y; C->z=z; C->yaw=yaw; C->sc=sc; C->slot=C->inst=-1;
    g_st.on=1; g_spOn=1; }
static void stStreamingLine(const char*ln){ float a,b; if(sscanf(ln,"streaming %f %f",&a,&b)==2){ g_st.rin=a; g_st.rout=b; } }
static void stFarLine(const char*ln){ if(sscanf(ln,"fardetail %55s %f",g_st.farStem,&g_st.farDist)!=2){ printf("[set] fardetail needs: stem distance\n"); g_st.farStem[0]=0; return; } g_st.farPk=-2; }
/* the first time they're asked for: which of the pack's meshes is whose, and how big each one is */
static void stFarReady(void){ g_st.farPk=setPack(g_st.farStem); if(g_st.farPk<0){ g_st.farStem[0]=0; return; } Pack*Q=&g_setPk[g_st.farPk]; int found=0;
    for(int i=0;i<g_st.n;i++){ StCell*C=&g_st.c[i]; C->farMesh=-1;
        if((unsigned)i<Q->nMeshes&&!strcmp(packStr(Q,Q->meshes[i].name),C->stem)) C->farMesh=i;
        else for(unsigned m=0;m<Q->nMeshes;m++) if(!strcmp(packStr(Q,Q->meshes[m].name),C->stem)){ C->farMesh=(int)m; break; }
        if(C->farMesh<0) continue;
        found++; const PMesh*M=&Q->meshes[C->farMesh]; const float*v=(const float*)(Q->pk+M->offPos); float x0=1e9f,x1=-1e9f,z0=1e9f,z1=-1e9f;
        for(unsigned k=0;k<M->verts;k++){ const float x=v[k*3],z=v[k*3+2]; if(x<x0) x0=x; if(x>x1) x1=x; if(z<z0) z0=z; if(z>z1) z1=z; }
        C->fx=(x0+x1)*0.5f; C->fz=(z0+z1)*0.5f; C->fr=hypotf(x1-x0,z1-z0)*0.5f+12.f; }
    printf("[stream] far detail: %s, %d of %d cells have one, out to %.0f u\n",g_st.farStem,found,g_st.n,g_st.farDist); fflush(stdout); }
static void packFree(Pack*P){
    if(P->vbo){ glDeleteBuffers((GLsizei)(P->nMeshes*6),P->vbo); free(P->vbo); }
    if(P->tex){ glDeleteTextures((GLsizei)P->nTex,P->tex); free(P->tex); }
    free(P->nodes); free(P->pk); free(P->meshTex); free(P->meshMatFlags); free(P->meshNTex); free(P->nodeCanon); free(P->gaitUPS); free(P->meshOff); free(P->meshKind); free(P->meshPiece); free(P->meshHiders);
    memset(P,0,sizeof *P); }
/* the cell's bytes are read: make it a pack and put it in the world */
static void stInstall(StCell*C){ char p[96]; double t0=wall_ms(); stPath(C,p,sizeof p);
    if(!C->bytes){ printf("[stream] can't read %s\n",p); fflush(stdout); SDL_AtomicSet(&C->state,ST_AWAY); C->x=1e9f; return; }
    int slot=g_setNFree?g_setFree[--g_setNFree]:(g_setNP<SET_MAXP?g_setNP++:-1);
    if(slot<0||g_setNI>=SET_MAXI){ static int said=0; if(!said){ said=1; printf("[stream] more cells near at once than the set's tables hold\n"); fflush(stdout); } free(C->bytes); C->bytes=NULL; SDL_AtomicSet(&C->state,ST_AWAY); return; }
    g_packMem=C->bytes; g_packMemLen=C->len;
#ifdef REVA_VK
    if(!g_st.sync) gfxUploadAsync(1);
#endif
    g_loadQuiet=C->said; C->said=1;
    g_setPk[slot]=loadPack(p);
    g_loadQuiet=0;
#ifdef REVA_VK
    gfxUploadAsync(0);
#endif
    g_packMem=NULL; C->bytes=NULL;
    snprintf(g_setNm[slot],64,"%s",C->stem);
    Pack*Q=&g_setPk[slot]; SetInst*I=&g_setI[g_setNI]; memset(I,0,sizeof *I); I->pk=slot; I->mover=-1;
    float eff=C->sc*((Q->scale>1e-6f)?Q->scale:1.f),a=C->yaw*0.0174532925f,ca=cosf(a),sa=sinf(a);
    I->m[0]=ca*eff; I->m[2]=-sa*eff; I->m[5]=eff; I->m[8]=sa*eff; I->m[10]=ca*eff; I->m[15]=1.f; I->m[12]=C->x; I->m[13]=C->y-Q->mny*eff; I->m[14]=C->z;
    C->slot=slot; C->inst=g_setNI++; g_st.bytesHere+=Q->len; g_st.nHere++; SDL_AtomicSet(&C->state,ST_HERE); g_st.nIn++;
    memcpy(C->m,I->m,64);
    /* its collision: on the spot when this run is being measured, and otherwise the thread's. If its queue's full it's done on the next tick that has room */
    if(g_st.sync){ C->grid=spGridOfPack(Q->pk,C->m); spLiveAdd(C->grid); SDL_AtomicSet(&C->gridBusy,3); }
    else SDL_AtomicSet(&C->gridBusy,0);
    double ms=wall_ms()-t0; g_st.sumIn+=ms; if(ms>g_st.worstIn) g_st.worstIn=ms; }
static void stEvict(StCell*C){ double t0=wall_ms(); int last=g_setNI-1;
    if(C->grid){ spLiveDrop(C->grid); spGridFree(C->grid); free(C->grid); C->grid=NULL; }
    SDL_AtomicSet(&C->gridBusy,0);
    g_st.bytesHere-=g_setPk[C->slot].len; packFree(&g_setPk[C->slot]); g_setNm[C->slot][0]=0; g_setFree[g_setNFree++]=C->slot;
    if(C->inst!=last){ g_setI[C->inst]=g_setI[last]; for(int i=0;i<g_st.n;i++) if(g_st.c[i].inst==last){ g_st.c[i].inst=C->inst; break; } }
    g_setNI=last; C->inst=C->slot=-1; C->late=0; g_st.nHere--; g_st.nOut++; SDL_AtomicSet(&C->state,ST_AWAY);
    double ms=wall_ms()-t0; if(ms>g_st.worstOut) g_st.worstOut=ms; }
/* once a tick: who's wanted, who's done with, and one of each */
static void stStart(void){ if(g_st.th) return; g_st.sync=getenv("REVA_FIXEDSTEP")!=NULL;
#ifdef REVA_HEADLESS
    g_st.sync=1;
#endif
    if(!g_st.sync) g_st.th=SDL_CreateThread(stThread,"stream",NULL);
    else g_st.th=(SDL_Thread*)1; }
static void stStep(float x,float z,long tick){ if(!g_st.on||!g_st.n) return;
    { static int said=0; const int fresh=!g_st.th; stStart();
      if(!(g_st.rin>0.f)) { g_st.rin=70.f; g_st.rout=90.f; }
      if(fresh||!said){ said=1; printf("[stream] %d cells, in at %.0f u, out at %.0f u, %s\n",g_st.n,g_st.rin,g_st.rout,g_st.sync?"read on the spot: this run is being measured":"read by a thread"); fflush(stdout); } }
    float rin2=g_st.rin*g_st.rin,rout2=g_st.rout*g_st.rout,near2=rin2*ST_LATE_NEAR*ST_LATE_NEAR; int installed=0,evicted=0;
    /* what's right around the start, before the first frame: there's nothing on the screen yet to go late */
    { static int first=1; if(first&&!g_st.sync){ first=0; for(int i=0;i<g_st.n;i++){ StCell*C=&g_st.c[i]; float dx=C->x-x,dz=C->z-z;
            if(dx*dx+dz*dz<near2&&SDL_AtomicGet(&C->state)==ST_AWAY){ char p[96]; stPath(C,p,sizeof p); C->bytes=stReadFile(p,&C->len); g_st.sync=1; stInstall(C); g_st.sync=0; } } } }
    for(int i=0;i<g_st.n;i++){ StCell*C=&g_st.c[i]; float dx=C->x-x,dz=C->z-z,d2=dx*dx+dz*dz; int s=SDL_AtomicGet(&C->state);
        if(s==ST_AWAY){ if(d2<rin2){
                if(g_st.sync){ char p[96]; stPath(C,p,sizeof p); C->bytes=stReadFile(p,&C->len); stInstall(C); }
                else { int wr=SDL_AtomicGet(&g_st.wr); if(wr-SDL_AtomicGet(&g_st.rd)<ST_MAXQ){ g_st.q[wr%ST_MAXQ]=i; SDL_AtomicSet(&C->state,ST_READING); SDL_AtomicSet(&g_st.wr,wr+1); } } } }
        else if(s==ST_READ){ if(d2>=rout2){ free(C->bytes); C->bytes=NULL; SDL_AtomicSet(&C->state,ST_AWAY); } else if(!installed){ installed=1; stInstall(C); } }
        else if(s==ST_HERE){ int gb=SDL_AtomicGet(&C->gridBusy);
            if(gb==0){ int wr=SDL_AtomicGet(&g_st.wr); if(wr-SDL_AtomicGet(&g_st.rd)<ST_MAXQ){ SDL_AtomicSet(&C->gridBusy,1); g_st.q[wr%ST_MAXQ]=i|ST_GRIDJOB; SDL_AtomicSet(&g_st.wr,wr+1); } }
            else if(gb==2){ spLiveAdd(C->grid); SDL_AtomicSet(&C->gridBusy,3); }
            if(d2>rout2&&(!evicted||g_st.sync)&&gb!=1){ evicted=1; stEvict(C); } }
        if(d2<near2&&!C->late&&(SDL_AtomicGet(&C->state)!=ST_HERE||SDL_AtomicGet(&C->gridBusy)!=3)){ C->late=1; g_st.nLate++; } }
    if(tick-g_st.statT>=300){ g_st.statT=tick; double rd=SDL_AtomicGet(&g_st.readUs)/1000.0; if(rd>g_st.worstRead) g_st.worstRead=rd;
        printf("[stream] %d here, %.1f MB, %ld in and %ld out so far, %ld late. Worst read %.1f ms; putting one in %.2f ms on average, worst %.2f; taking one out, worst %.2f\n",g_st.nHere,(double)g_st.bytesHere/1048576.0,g_st.nIn,g_st.nOut,g_st.nLate,g_st.worstRead,g_st.nIn?g_st.sumIn/(double)g_st.nIn:0.0,g_st.worstIn,g_st.worstOut); fflush(stdout); } }
/* a new set: wait for whatever's being read, and everything goes */
static void stClear(void){ if(!g_st.on) return;
    if(!g_st.sync&&g_st.th) while(SDL_AtomicGet(&g_st.rd)!=SDL_AtomicGet(&g_st.wr)) SDL_Delay(1);
    for(int i=0;i<g_st.n;i++){ StCell*C=&g_st.c[i]; free(C->bytes); C->bytes=NULL; C->grid=NULL; }   /* the live grids go with spClear */
    g_st.n=0; g_st.on=0; g_st.nHere=0; g_st.farStem[0]=0; g_st.farPk=-1; }
