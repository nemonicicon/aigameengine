"""patch_timeline.py -- Z7a: an actor gets a TIMELINE.

Two acts of SEVENTY-FOUR are blocked on the same missing thing, and it is the same
thing twice: a set actor holds ONE clip at ONE place for a whole take.

  · Act III (`it_starts`) turns on a neighbour BECOMING one of them, in full sun, in an
    ordinary frame. Nothing could change a body's clip at a moment in time.
  · Act IV (`three_lawns`) is three women crossing three lawns while reva calls the gaps.
    Actors could only run a straight line that wrapped.

So the set file grows two line types, both attaching to the actor declared above them:

    actor v1_Character_29_Female  walking  30 0 21  200 1.0
    cue   12.0  zombie              # at t=12s she turns. one word.
    cue   14.2  z_walk_b
    path   2.5  40 0 -20            # be there by t=2.5 ...
    path   6.0  40 0  -8            # ... and there by t=6

⭐ A CUE IS THE TURN. `cue <t> zombie` re-runs the same casting note the actor line
supports, so a neighbour who has been walking to her mailbox for twelve seconds becomes a
shambler on the frame the script says, on her own body, through the canonical transfer.
That is Act III's hinge in one word.

⭐ A PATH IS BLOCKING. Waypoints in TIME, not speed -- "be at the gate by 6.0" is what a
director says, and it means the crossing can be cut to the dialogue instead of the
dialogue waiting for the crossing. Speed falls out of distance over time, the actor faces
the way she is going, and the clip's phase runs at gaitRate() for that speed, so nobody
slides. (The same lesson as the partner's staging walk, one layer down.)

⛔ IT NEEDS A CLOCK OF ITS OWN. `g_scnT` only exists while a scene is playing, and half
these shots have no dialogue over them at all. `g_setT` starts at zero when the set loads
and advances with the frame, so it is deterministic under REVA_FIXEDSTEP like everything
else on the bench.
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
BS_N = chr(92) + "n"


def sub(a, b, why):
    global s
    n = s.count(a)
    if n != 1:
        print("!! anchor x%d for %s" % (n, why))
        sys.exit(1)
    s = s.replace(a, b)
    print("   ok: %s" % why)


# ---------------------------------------------------------- 1. the structs
sub("""                 Pack *dnP; int dnC;           /* Z4: a clip borrowed from a bank, retargeted onto this body */
                 } SetActor;""",
    """                 Pack *dnP; int dnC;           /* Z4: a clip borrowed from a bank, retargeted onto this body */
                 /* ---- Z7a THE TIMELINE ---- */
                 SetCue cue[SET_MAXCUE]; int nCue, curCue;
                 SetWp  wp[SET_MAXWP];   int nWp;
                 float cx,cy,cz,cyaw,cspd;     /* where the path has her THIS frame */
                 int   onPath;
                 } SetActor;""",
    "SetActor gains cues and waypoints")

sub("""#define SET_MAXA 128""",
    """#define SET_MAXA 128
/* Z7a: an actor's timeline. Eight of each is a shot, not a screenplay -- a body that
 * needs more than eight beats or eight marks in one take wants to be two actors. */
#define SET_MAXCUE 8
#define SET_MAXWP  8
typedef struct { float t; char clip[32]; } SetCue;
typedef struct { float t, x, y, z; } SetWp;
static float g_setT=0.f;      /* THE SET CLOCK. g_scnT only exists while dialogue plays,
                               * and half these shots have none over them. */""",
    "timeline types + the set clock")

# ---------------------------------------------------------- 2. clip resolution, factored
sub("""static int setParse(const char*name,int depth);""",
    """/* ⭐ ONE PLACE THAT TURNS A CLIP NAME INTO A CLIP. Used by the actor line at load AND by
 * every cue afterwards, so a name means the same thing whenever it is said: the body's own
 * clip if it has one, otherwise borrowed off a bank through the canonical transfer, and
 * `zombie` is a casting note that picks a variant by actor index. */
static void actorClip(SetActor*A,Pack*AP,const char*nm,int idx,int moving){
    char acr[64]; snprintf(acr,sizeof acr,"%s",nm);
    if(!strcmp(nm,"zombie"))
        snprintf(acr,sizeof acr,moving?"z_walk_%c":"z_idle_%c",
                 (char)('a'+(idx%(moving?3:2))));
    A->dnP=NULL; A->dnC=-1; A->clip=-1;
    if(!strcmp(acr,"-")||!strcmp(acr,"skate")) return;
    for(unsigned c=0;c<AP->nClips;c++)
        if(!strcmp(packStr(AP,AP->clips[c].name),acr)){ A->clip=(int)c; return; }
    int bc=-1; Pack*bp=bankFind(acr,&bc);
    if(bp&&bp!=AP){
        if(AP->cbOK&&AP->pbOK>=10){ A->dnP=bp; A->dnC=bc; }
        else printf("[set] '%s' is in a bank, but this rig has no canonical basis (pbOK=%d)@N",
                    acr,AP->pbOK); }
    else if(!bp) printf("[set] clip '%s' NOT FOUND@N",acr); }
static int setParse(const char*name,int depth);""".replace("@N", BS_N),
    "actorClip helper")

# ---------------------------------------------------------- 3. setLoad uses it
sub("""            int ci=-1;
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
            A->pk=pi2; A->clip=ci; A->board=(na>=11&&ab[0])?setPack(ab):-1;""".replace("@N", BS_N),
    """            SetActor*A=&g_setA[g_setNA++];
            A->nCue=0; A->curCue=-1; A->nWp=0; A->onPath=0; A->cspd=0.f;
            actorClip(A,AP2,ac,g_setNA-1,asp>0.f);
            A->pk=pi2; A->board=(na>=11&&ab[0])?setPack(ab):-1;""",
    "setLoad uses the shared resolver")

# ---------------------------------------------------------- 4. parse cue / path
sub("""        if(!strncmp(ln,"light ",6)){""",
    """        /* Z7a: a cue and a waypoint attach to the actor declared above them. */
        if(!strncmp(ln,"cue ",4)){
            float ct; char cc[32];
            if(sscanf(ln,"cue %f %31s",&ct,cc)==2&&g_setNA>0){
                SetActor*A=&g_setA[g_setNA-1];
                if(A->nCue<SET_MAXCUE){ A->cue[A->nCue].t=ct;
                    snprintf(A->cue[A->nCue].clip,32,"%s",cc); A->nCue++; }
                else printf("[set] cue cap is %d per actor@N",SET_MAXCUE); }
            continue; }
        if(!strncmp(ln,"path ",5)){
            float pt,px2,py2,pz2;
            if(sscanf(ln,"path %f %f %f %f",&pt,&px2,&py2,&pz2)==4&&g_setNA>0){
                SetActor*A=&g_setA[g_setNA-1];
                if(A->nWp<SET_MAXWP){ A->wp[A->nWp].t=pt; A->wp[A->nWp].x=px2;
                    A->wp[A->nWp].y=py2; A->wp[A->nWp].z=pz2; A->nWp++; A->onPath=1; }
                else printf("[set] path cap is %d per actor@N",SET_MAXWP); }
            continue; }
        if(!strncmp(ln,"light ",6)){""".replace("@N", BS_N),
    "parse cue and path")

# ---------------------------------------------------------- 5. reset the clock
sub("""    g_setNI=0; g_setNA=0; g_setCur[0]=0;
    g_setNL=0;""",
    """    g_setNI=0; g_setNA=0; g_setCur[0]=0;
    g_setT=0.f;              /* Z7a: every load restarts the shot */
    g_setNL=0;""",
    "the set clock restarts on load")

# ---------------------------------------------------------- 6. tick it
sub("""static void actorsTick(float dt){
    g_actorDt=dt;
    for(int i=0;i<g_setNA;i++){ SetActor*A=&g_setA[i];
        Pack*P=&g_setPk[A->pk];""",
    """static void actorsTick(float dt){
    g_actorDt=dt;
    g_setT+=dt;
    for(int i=0;i<g_setNA;i++){ SetActor*A=&g_setA[i];
        Pack*P=&g_setPk[A->pk];
        /* ---- Z7a THE CUES. The last cue whose time has passed is the one playing. ---- */
        if(A->nCue){
            int want=-1;
            for(int k=0;k<A->nCue;k++) if(A->cue[k].t<=g_setT) want=k;
            if(want!=A->curCue){
                A->curCue=want;
                if(want>=0){
                    actorClip(A,P,A->cue[want].clip,i,(A->spd>0.f)||A->onPath);
                    A->phase=0.0;
                    printf("[cue] t=%.2f actor %d -> %s@N",g_setT,i,A->cue[want].clip);
                    fflush(stdout); } } }
        /* ---- Z7a THE PATH. Waypoints are TIMES, so speed falls out of the blocking. ---- */
        if(A->nWp){
            int seg=-1;
            for(int k=0;k<A->nWp;k++) if(A->wp[k].t<=g_setT) seg=k;
            float x0,y0,z0,x1,y1,z1,t0,t1,u;
            if(seg<0){ x0=A->x; y0=A->y; z0=A->z; t0=0.f;
                       x1=A->wp[0].x; y1=A->wp[0].y; z1=A->wp[0].z; t1=A->wp[0].t; }
            else if(seg>=A->nWp-1){ SetWp*L=&A->wp[A->nWp-1];
                       x0=x1=L->x; y0=y1=L->y; z0=z1=L->z; t0=0.f; t1=1.f; }
            else { x0=A->wp[seg].x; y0=A->wp[seg].y; z0=A->wp[seg].z; t0=A->wp[seg].t;
                   x1=A->wp[seg+1].x; y1=A->wp[seg+1].y; z1=A->wp[seg+1].z; t1=A->wp[seg+1].t; }
            u=(t1>t0)?((g_setT-t0)/(t1-t0)):1.f;
            if(u<0.f) u=0.f; if(u>1.f) u=1.f;
            A->cx=x0+(x1-x0)*u; A->cy=y0+(y1-y0)*u; A->cz=z0+(z1-z0)*u;
            float dx=x1-x0, dz=z1-z0, d=sqrtf(dx*dx+dz*dz);
            A->cspd=(t1>t0&&u<1.f)?(d/(t1-t0)):0.f;
            /* face the way she is going; club forward is +z */
            if(d>1e-3f&&A->cspd>0.f) A->cyaw=atan2f(dx,dz)*57.2957795f; }""".replace("@N", BS_N),
    "tick the cues and the path")

# ---------------------------------------------------------- 7. phase at walking speed
sub("""        if(A->dnP&&A->dnC>=0&&(unsigned)A->dnC<A->dnP->nClips)
            A->phase+=dt*A->dnP->clips[A->dnC].fps*A->rate;
        else if(A->clip>=0&&(unsigned)A->clip<P->nClips)
            A->phase+=dt*P->clips[A->clip].fps*A->rate;""",
    """        /* ⭐ ON A PATH, THE STRIDE MATCHES THE SPEED. gaitRate() is the club's measure of
         * how far a clip's planted foot travels per frame, so a body crossing a lawn in
         * four seconds takes the strides that carry her there instead of sliding. */
        float grate=A->rate;
        if(A->nWp&&A->cspd>0.02f){
            Pack*gp=A->dnP?A->dnP:P; int gc=A->dnP?A->dnC:A->clip;
            if(gc>=0) grate=A->rate*gaitRate(gp,gc,A->cspd); }
        if(A->dnP&&A->dnC>=0&&(unsigned)A->dnC<A->dnP->nClips)
            A->phase+=dt*A->dnP->clips[A->dnC].fps*grate;
        else if(A->clip>=0&&(unsigned)A->clip<P->nClips)
            A->phase+=dt*P->clips[A->clip].fps*grate;""",
    "clip phase follows path speed")

# ---------------------------------------------------------- 8. draw at the path position
sub("""        float a=A->yaw*0.0174532925f;
        float eff=A->sc*((P->scale>1e-6f)?P->scale:1.f);
        float px=A->x+sinf(a)*A->travel, pz=A->z+cosf(a)*A->travel;""",
    """        float a=(A->nWp?A->cyaw:A->yaw)*0.0174532925f;
        float eff=A->sc*((P->scale>1e-6f)?P->scale:1.f);
        float px,pz;
        if(A->nWp){ px=A->cx; pz=A->cz; }                 /* Z7a: the path owns her place */
        else { px=A->x+sinf(a)*A->travel; pz=A->z+cosf(a)*A->travel; }""",
    "draw at the path position")

if s != orig:
    out = s.replace(LF, CRLF) if WAS_CRLF else s
    io.open(P, "w", encoding="utf-8", newline="").write(out)
    print("patched crowd_kms.c (%s)" % ("CRLF" if WAS_CRLF else "LF"))
