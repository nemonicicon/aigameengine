"""patch_z4.py -- wire the zombie bank into the actor system.

Z4: an actor can name ANY clip in the club and get it retargeted onto its own body.
The zombies are the first customer -- "the zombies ARE the neighbours" needs exactly
this and nothing more.

crowd_kms.c is CRLF on disk. Anchors here are written as ordinary python strings (LF),
so the file is normalised to LF for editing and put back as it was found. CR and LF are
spelled with chr() because a quoted heredoc in this environment eats one level of
backslash and would turn the escape into a real newline -- the trap in CLAUDE.md.
"""
import io, sys

P = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"
CR, LF = chr(13), chr(10)
CRLF = CR + LF

s = io.open(P, encoding="utf-8", newline="").read()
WAS_CRLF = CRLF in s
if WAS_CRLF:
    s = s.replace(CRLF, LF)
orig = s


def sub(a, b, why):
    global s
    n = s.count(a)
    if n != 1:
        print("!! anchor x%d for %s" % (n, why))
        sys.exit(1)
    s = s.replace(a, b)
    print("   ok: %s" % why)


# ---------------------------------------------------------------- 1. the bank table
a = """        else if((!strcmp(nm,"w_walk")||!strncmp(nm,"v_walk",6))&&g_dnNWalks<8){
            g_dnWalksP[g_dnNWalks]=pk; g_dnWalks[g_dnNWalks]=(int)c; g_dnNWalks++; } } }"""
b = a + """
/* ================= Z4 THE BANKS: a clip is addressable across packs =================
 * dnElect above wires clips to ROLES the crowd asks for by behaviour. A set file asks
 * the other way round -- by NAME, for one actor -- and until now a clip name resolved
 * only inside the actor's own pack, which is why a 900-vert town citizen could do
 * nothing but the walk and idle it shipped with.
 *
 * bankFind() closes that: name a clip that lives in ANY loaded bank and the actor plays
 * it through the canonical transfer. The zombie ingest is the first customer -- the film
 * says "the zombies ARE the neighbours", and this is that sentence in code: the same
 * citizen body, the same set line, a different clip name. */
#define NBANK 4
static Pack*g_bankP[NBANK]; static int g_nBank=0;
static void bankAdd(Pack*p){ if(p&&p->nClips&&g_nBank<NBANK) g_bankP[g_nBank++]=p; }
static Pack*bankFind(const char*nm,int*ci){
    for(int b=0;b<g_nBank;b++){ Pack*p=g_bankP[b];
        for(unsigned c=0;c<p->nClips;c++)
            if(!strcmp(packStr(p,p->clips[c].name),nm)){ *ci=(int)c; return p; } }
    return NULL; }
/* THE ZOMBIE BANK (v1_zmoves: 32 ZAMN clips on the UE5 mannequin). Held separately from
 * the donor roles on purpose -- a neighbour walking to her mailbox must never be handed
 * a shamble. Nothing here is elected; it is only ever asked for by name. */
static Pack*g_zombP=NULL;"""
sub(a, b, "bank table + zombie bank global")

# ---------------------------------------------------------------- 2. SetActor fields
a = """                 float kneeZ;                  /* ankle minus knee along forward: NEGATIVE is a real knee */
                 } SetActor;"""
b = """                 float kneeZ;                  /* ankle minus knee along forward: NEGATIVE is a real knee */
                 Pack *dnP; int dnC;           /* Z4: a clip borrowed from a bank, retargeted onto this body */
                 } SetActor;"""
sub(a, b, "SetActor donor fields")

# ---------------------------------------------------------------- 3. clip resolution
BS_N = chr(92) + "n"          # the two characters backslash + n, for a C string literal
a = """            int ci=-1;
            for(unsigned c2=0;c2<AP2->nClips;c2++)
                if(!strcmp(packStr(AP2,AP2->clips[c2].name),ac)){ ci=(int)c2; break; }
            if(ci<0&&strcmp(ac,"-")) printf("[set] actor %s: clip '%s' NOT FOUND@N",ap,ac);
            SetActor*A=&g_setA[g_setNA++];
            A->pk=pi2; A->clip=ci; A->board=(na>=11&&ab[0])?setPack(ab):-1;""".replace("@N", BS_N)
b = """            int ci=-1;
            /* ZOMBIE IS A CASTING NOTE, NOT A CLIP. "zombie" picks a variant off the
             * bank by actor index, because forty zombies playing one loop reads as forty
             * copies of one zombie -- the same reason the crowd rotates its walks. A
             * moving one shambles, a standing one sways. */
            char acr[64]; snprintf(acr,sizeof acr,"%s",ac);
            if(!strcmp(ac,"zombie")){
                if(asp>0.f) snprintf(acr,sizeof acr,"z_walk_%c",(char)('a'+(g_setNA%3)));
                else        snprintf(acr,sizeof acr,"z_idle_%c",(char)('a'+(g_setNA%2))); }
            for(unsigned c2=0;c2<AP2->nClips;c2++)
                if(!strcmp(packStr(AP2,AP2->clips[c2].name),acr)){ ci=(int)c2; break; }
            SetActor*A=&g_setA[g_setNA++];
            A->dnP=NULL; A->dnC=-1;
            /* Z4: not in its own pack -> look through the banks and retarget. */
            if(ci<0&&strcmp(acr,"-")&&strcmp(acr,"skate")){
                int bc=-1; Pack*bp=bankFind(acr,&bc);
                if(bp&&bp!=AP2){
                    if(AP2->cbOK&&AP2->pbOK>=10){ A->dnP=bp; A->dnC=bc; }
                    else printf("[set] actor %s: '%s' is in a bank, but this rig has no canonical basis (pbOK=%d)@N",
                                ap,acr,AP2->pbOK); }
                else if(!bp) printf("[set] actor %s: clip '%s' NOT FOUND@N",ap,acr); }
            A->pk=pi2; A->clip=ci; A->board=(na>=11&&ab[0])?setPack(ab):-1;""".replace("@N", BS_N)
sub(a, b, "cross-pack clip resolution + the zombie casting note")

# ---------------------------------------------------------------- 4. phase advance
a = """        if(A->clip>=0&&(unsigned)A->clip<P->nClips)
            A->phase+=dt*P->clips[A->clip].fps*A->rate;"""
b = """        /* the clip's own fps drives the phase -- and a BORROWED clip's fps lives in the
         * bank it came from, not in the body playing it */
        if(A->dnP&&A->dnC>=0&&(unsigned)A->dnC<A->dnP->nClips)
            A->phase+=dt*A->dnP->clips[A->dnC].fps*A->rate;
        else if(A->clip>=0&&(unsigned)A->clip<P->nClips)
            A->phase+=dt*P->clips[A->clip].fps*A->rate;"""
sub(a, b, "phase advance honours the borrowed clip's fps")

# ---------------------------------------------------------------- 5. actorDraw
a = """static void actorDraw(Pack*P,int clip,double phase,const float*model,"""
b = """static void actorDraw(Pack*P,int clip,Pack*dnP,int dnC,double phase,const float*model,"""
sub(a, b, "actorDraw signature")

a = """    if(clip>=0&&(unsigned)clip<P->nClips){
        PClip*c=&P->clips[clip];
        unsigned f=(unsigned)phase % (c->frames?c->frames:1);
        unsigned char*tr=P->pk+c->offTracks;
        for(unsigned t=0;t<c->nTracks;t++){
            unsigned node=*(unsigned*)(tr+t*12), type=*(unsigned*)(tr+t*12+4), offD=*(unsigned*)(tr+t*12+8);
            if(node>=P->nNodes) continue;
            float*d=(float*)(P->pk+offD); int comp=type==0?4:3;
            if(type>=3) continue;
            else if(type==0) memcpy(Q[node],d+(size_t)f*comp,16);
            else if(type==1) memcpy(T[node],d+(size_t)f*comp,12);
            else             memcpy(S[node],d+(size_t)f*comp,12); } }"""
b = """    /* Z4: A BORROWED CLIP GOES THROUGH THE CANONICAL TRANSFER. donorPoseF poses the
     * bank's rig, takes each canonical bone's WORLD delta and puts this body's frame
     * where the bank's is -- so one authored shamble is the same shamble on any of the
     * 105 citizens, whatever their rest pose happens to be.
     * AND IT IS ROTATION-ONLY: the 16 canonical bones move, everything else holds its
     * rest. The pelvis HEIGHT the crawl clips carry does not cross -- see design/zombies.md.
     * Otherwise the pack's own clip, through the P1.5 INTERPOLATING sampler: actorDraw
     * was written after it and had quietly gone back to the nearest-frame memcpy, which
     * is a stair-step at any capture rate that is not the bake rate. */
    if(dnP&&dnC>=0&&(unsigned)dnC<dnP->nClips) donorPoseF(dnP,dnC,phase,P,Q);
    else if(clip>=0&&(unsigned)clip<P->nClips) clipSampleF(P,&P->clips[clip],phase,T,Q,S);"""
sub(a, b, "actorDraw plays a borrowed clip, and stops stair-stepping its own")

# ---------------------------------------------------------------- 6. the call site
a = """        actorDraw(P,isk?-1:A->clip,A->phase,m,mModel,mPal,mBind,pal,isk?&sp:NULL,ft,pmix);"""
b = """        actorDraw(P,isk?-1:A->clip,isk?NULL:A->dnP,isk?-1:A->dnC,
                  A->phase,m,mModel,mPal,mBind,pal,isk?&sp:NULL,ft,pmix);"""
sub(a, b, "actorDraw call site")

# ---------------------------------------------------------------- 7. load the banks
a = """        if((df=fopen("v1_kaeli.rpack","rb"))){ fclose(df); dn2=loadPack("v1_kaeli.rpack"); dnElect(&dn2); }"""
b = """        if((df=fopen("v1_kaeli.rpack","rb"))){ fclose(df); dn2=loadPack("v1_kaeli.rpack"); dnElect(&dn2); }
        bankAdd(&dn1); bankAdd(&dn2);"""
sub(a, b, "mob1 + kaeli join the bank table")

a = """    g_slotStride=(int)sizeof(Slot);   /* the director indexes slots through this -- NEVER let its mirror struct go stale again */"""
b = ("""    /* Z4 THE ZOMBIE BANK, loaded outside the REVA_NODONOR guard: it is the film's asset,
     * not one of the crowd's donor roles, and killing the crowd's donors to get a clean
     * floor should not empty the street. */
    { static Pack zp; FILE*zf;
      if((zf=fopen("packs/v1_zmoves.rpack","rb"))){ fclose(zf);
          zp=loadPack("packs/v1_zmoves.rpack"); g_zombP=&zp; bankAdd(&zp);
          printf("ZOMBIE BANK: %u clips, %u nodes, pbOK=%d cbOK=%d@N",
                 zp.nClips,zp.nNodes,zp.pbOK,zp.cbOK); } }
""".replace("@N", BS_N)) + a
sub(a, b, "the zombie bank loads at startup")

if s != orig:
    out = s.replace(LF, CRLF) if WAS_CRLF else s
    io.open(P, "w", encoding="utf-8", newline="").write(out)
    print("patched crowd_kms.c (%s)" % ("CRLF" if WAS_CRLF else "LF"))
