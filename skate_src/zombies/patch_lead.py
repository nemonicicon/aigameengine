"""patch_lead.py -- a lead between two ACTORS: collar end and hand end.

jacob: "put the collar on reva, have her do the cat crawl, attacth the lease to the collar
and put the end of the line in revas hand. i want reva to walk kaeli like a dog."

Taking the last sentence as the intent: KAELI wears the collar and crawls, REVA holds the
other end. Neither of them is a set prop, so the lead cannot use the dog's version (which
runs from one actor to the conversation partner) -- it has to join two ACTORS, at two
NAMED BONES:

    leash <otherActorIndex>      # this actor's NECK  ->  that actor's HAND

⭐ WHICH IS ALSO WHY IT NEEDS BONES, NOT POSITIONS. A lead pinned to two body ORIGINS
floats in the air the moment either of them crouches -- and the whole point here is that
one of them is on all fours. `actorDraw` already reports the feet it measured after FK for
the skater's board; it now reports the neck and the grip hand the same way, and
`actorsDraw` puts them through the same model matrix the body is drawn with, so the ends
stay attached through any pose.

⛔ BOTH BODIES ALREADY WEAR A COLLAR. reva2 has a black choker and kaeli a gold band, so
nothing needs modelling -- the lead just has to terminate at the neck rather than near it.
"""
import io, sys

CR, LF = chr(13), chr(10)
CRLF = CR + LF
P = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"
s = io.open(P, encoding="utf-8", newline="").read()
was = CRLF in s
if was:
    s = s.replace(CRLF, LF)


def sub(a, b, why):
    global s
    if s.count(a) != 1:
        print("!! anchor x%d for %s" % (s.count(a), why))
        sys.exit(1)
    s = s.replace(a, b)
    print("   ok: %s" % why)


sub("""                 int leash;                    /* 1: draw a lead from this actor to the walker */""",
    """                 int leash;                    /* >=0: lead from THIS actor's neck to that actor's hand */
                 float neckW[3], handW[3];     /* measured after FK, in WORLD space */""",
    "SetActor carries its own neck and hand")

sub("""            A->nCue=0; A->curCue=-1; A->nWp=0; A->onPath=0; A->cspd=0.f; A->leash=0;""",
    """            A->nCue=0; A->curCue=-1; A->nWp=0; A->onPath=0; A->cspd=0.f; A->leash=-1;""",
    "no lead by default")

sub("""        if(!strncmp(ln,"leash",5)&&g_setNA>0){ g_setA[g_setNA-1].leash=1; continue; }""",
    """        if(!strncmp(ln,"leash",5)&&g_setNA>0){ int li=-1;
            if(sscanf(ln,"leash %d",&li)!=1) li=-1;
            g_setA[g_setNA-1].leash=li; continue; }""",
    "leash names the other end")

# actorDraw reports the neck and the grip hand, the same way it reports the feet
sub("""    if(footOut){
        for(int fk=0;fk<2;fk++){ int fb=fk?P->footR:P->footL;""",
    """    /* the lead's two ends, in RIG space -- the caller puts them through the model matrix
     * exactly as it does the feet, so they stay attached through any pose. */
    if(footOut){
        int nb9=(P->pb[4]>=0)?P->pb[4]:P->pb[5];        /* neck, else head */
        int hb9=(P->gripNode>=0)?P->gripNode:P->pb[11]; /* grip hand, else forearm_r */
        if(nb9>=0){ footOut[8]=W[nb9][12]; footOut[9]=W[nb9][13]; footOut[10]=W[nb9][14]; }
        if(hb9>=0){ footOut[11]=W[hb9][12]; footOut[12]=W[hb9][13]; footOut[13]=W[hb9][14]; }
        for(int fk=0;fk<2;fk++){ int fb=fk?P->footR:P->footL;""",
    "actorDraw measures the neck and the hand")

sub("""        SkPose sp; float ft[7]={0,0,0,0,0,0,0};""",
    """        SkPose sp; float ft[14]={0,0,0,0,0,0,0,0,0,0,0,0,0,0};""",
    "room for both ends")

sub("""        if(A->board>=0){
            Pack*B=&g_setPk[A->board];""",
    """        /* both ends into world space, through the body's own matrix */
        { float*nq=ft+8, *hq=ft+11;
          A->neckW[0]=m[0]*nq[0]+m[4]*nq[1]+m[8]*nq[2]+m[12];
          A->neckW[1]=m[1]*nq[0]+m[5]*nq[1]+m[9]*nq[2]+m[13];
          A->neckW[2]=m[2]*nq[0]+m[6]*nq[1]+m[10]*nq[2]+m[14];
          A->handW[0]=m[0]*hq[0]+m[4]*hq[1]+m[8]*hq[2]+m[12];
          A->handW[1]=m[1]*hq[0]+m[5]*hq[1]+m[9]*hq[2]+m[13];
          A->handW[2]=m[2]*hq[0]+m[6]*hq[1]+m[10]*hq[2]+m[14]; }
        if(A->board>=0){
            Pack*B=&g_setPk[A->board];""",
    "publish both ends in world space")

# replace the dog-era single leash publish with the actor-to-actor pair
sub("""        /* the collar: a little above the body, a little ahead of it */
        if(A->leash){ g_leashAt[0]=px+sinf(a)*0.30f; g_leashAt[1]=m[13]+1.05f*eff;
                      g_leashAt[2]=pz+cosf(a)*0.30f; g_leashOn=1; }""",
    "",
    "drop the origin-based collar guess")

sub("""static float g_leashAt[3]={0,0,0}; static int g_leashOn=0;""",
    """#define LEAD_MAX 4
static float g_leadA[LEAD_MAX][3], g_leadB[LEAD_MAX][3]; static int g_leadN=0;""",
    "a small table of leads")

sub("""static void actorsDraw(GLint mModel,GLint mPal,GLint mBind,float*pal,GLint uUse,GLint uTint){
    g_leashOn=0;
    if(!g_setNA) return;""",
    """static void actorsDraw(GLint mModel,GLint mPal,GLint mBind,float*pal,GLint uUse,GLint uTint){
    g_leadN=0;
    if(!g_setNA) return;""",
    "clear the leads each frame")

sub("""static void drawSet(GLint uModel,GLint uPal,GLint uBind,GLint uUse,GLint uTint){""",
    """/* collected after every actor has been drawn, because a lead needs BOTH ends measured */
static void leadsCollect(void){
    g_leadN=0;
    for(int i=0;i<g_setNA&&g_leadN<LEAD_MAX;i++){ SetActor*A=&g_setA[i];
        if(A->leash<0||A->leash>=g_setNA||A->leash==i) continue;
        SetActor*B=&g_setA[A->leash];
        memcpy(g_leadA[g_leadN],A->neckW,12);
        memcpy(g_leadB[g_leadN],B->handW,12);
        g_leadN++; } }
static void drawSet(GLint uModel,GLint uPal,GLint uBind,GLint uUse,GLint uTint){""",
    "collect the leads")

sub("""                if(g_mode==MODE_ANIM&&g_setNA)
                    actorsDraw(mModel,mPal,mBind,pal,g_labUse,g_labTint);""",
    """                if(g_mode==MODE_ANIM&&g_setNA){
                    actorsDraw(mModel,mPal,mBind,pal,g_labUse,g_labTint);
                    leadsCollect(); }""",
    "collect after drawing")

sub("""                if(g_leashOn&&g_labPartner&&g_partFaceOK){
                    float hx9=g_partHeadW[0], hy9=g_partHeadW[1]-0.62f, hz9=g_partHeadW[2];
                    float lv[12]={ hx9,hy9,hz9, 0.10f,0.09f,0.08f,
                                   g_leashAt[0],g_leashAt[1],g_leashAt[2], 0.10f,0.09f,0.08f };""",
    """                if(g_leadN>0){
                    static float lv[LEAD_MAX*12];
                    for(int li=0;li<g_leadN;li++){ float*v=lv+li*12;
                        v[0]=g_leadA[li][0]; v[1]=g_leadA[li][1]; v[2]=g_leadA[li][2];
                        v[3]=0.10f; v[4]=0.09f; v[5]=0.08f;
                        v[6]=g_leadB[li][0]; v[7]=g_leadB[li][1]; v[8]=g_leadB[li][2];
                        v[9]=0.10f; v[10]=0.09f; v[11]=0.08f; }""",
    "build the lead vertices")

sub("""                    glDrawArrays(GL_LINES,0,2);
                    glUseProgram(skSolid?PR[1].prog:hProg); }""",
    """                    glDrawArrays(GL_LINES,0,g_leadN*2);
                    glUseProgram(skSolid?PR[1].prog:hProg); }""",
    "draw them all")

sub("""                    glBufferData(GL_ARRAY_BUFFER,sizeof lv,lv,GL_DYNAMIC_DRAW);""",
    """                    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(g_leadN*12*sizeof(float)),lv,GL_DYNAMIC_DRAW);""",
    "upload only what is used")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("patched crowd_kms.c")
