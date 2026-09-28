"""The body the cloth collides with is MEASURED, and there is more of it.

jacob: "its also clipping into her model" and "the cloth you are creating is too thick, her
pajamas should be threadbare, sheer, loose, old, comfortable".

⛔ THE GARMENT WAS COLLIDING WITH SIX SPHERES AND SIX GUESSED NUMBERS -- hips, two spine
bones, chest, and two feet, at radii 0.26/0.25/0.25/0.23/0.16/0.16 of the leg length. Six
spheres cannot cover a torso, and NOTHING covered the arms, the thighs or the shins at all,
so of course the fabric went through her. authorlib has body_capsules() which measures this
properly, but that is author-time Python and the solver runs in C.

⭐ SO THE ENGINE MEASURES IT, ONCE, THE SAME WAY THE GARMENT GENERATOR DOES: every vertex
knows its dominant bone, so give each canonical bone the spread of the vertices that belong
to it. A thick thigh gets a thick capsule and a thin wrist does not, on any body, with
nothing to tune -- and now every canonical bone has one instead of six of them.

⛔ MEASURED IN REST, ON REST POSITIONS. The radius is a property of the body, not of the
pose it happens to be in this frame.
"""
import io

P = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"
LF = chr(10)
CRLF = chr(13) + LF

s = io.open(P, encoding="utf-8", newline="").read()
was = CRLF in s
if was:
    s = s.replace(CRLF, LF)


def sub(a, b, why):
    global s
    n = s.count(a)
    if n != 1:
        raise SystemExit("!! anchor x%d for %s" % (n, why))
    s = s.replace(a, b)
    print("   ok: %s" % why)


sub("""    int eyesRig;   /* ⛔ are the eye marks SKINNED? A mark with no weights rotates nothing. */""",
    """    int eyesRig;   /* ⛔ are the eye marks SKINNED? A mark with no weights rotates nothing. */
    /* ⭐ HOW THICK THIS BODY IS, PER CANONICAL BONE, measured from its own vertices. The
     * cloth solver used six spheres and six guessed numbers and nothing at all on the arms
     * or legs, which is why a garment went through her. */
    float boneR[NCANON]; int boneRok;""",
    "Pack carries measured bone radii")

sub("""static int clothPrep(int slot,Pack*P,unsigned mi,float*pal,const float*model,M4*world,
                     GLint uModel,GLint uPal,GLint uBind,GLint uDqS){""",
    """/* ⭐ MEASURE THE BODY. Every vertex knows its dominant bone, so each canonical bone gets
 * the spread of the vertices that belong to it -- a thick thigh gets a thick capsule and a
 * thin wrist does not, on any body, with nothing to tune. Done once, in REST, because a
 * radius is a property of the body and not of this frame's pose. */
static void measureBoneR(Pack*P){
    if(P->boneRok) return;
    P->boneRok=1;
    for(int c=0;c<NCANON;c++) P->boneR[c]=0.f;
    static M4 W[1024];
    if(P->nNodes>1024) return;
    for(unsigned i=0;i<P->nNodes;i++){ M4 loc;
        m4_trs(loc,P->nodes[i].t,P->nodes[i].q,P->nodes[i].s);
        if(P->nodes[i].parent<0) memcpy(W[i],loc,64);
        else m4_mul(W[i],W[P->nodes[i].parent],loc); }
    /* node -> canonical slot */
    static short c_of[1024];
    for(unsigned i=0;i<P->nNodes;i++) c_of[i]=-1;
    for(int c=0;c<NCANON;c++) if(P->pb[c]>=0&&(unsigned)P->pb[c]<P->nNodes) c_of[P->pb[c]]=(short)c;
    static float acc[NCANON]; static int cnt[NCANON];
    for(int c=0;c<NCANON;c++){ acc[c]=0.f; cnt[c]=0; }
    for(unsigned m=0;m<P->nMeshes;m++){
        PMesh*M=&P->meshes[m];
        if(!(M->flags&4)||!M->boneCount||M->boneCount>MAXB) continue;
        if(strstr(packStr(P,M->name),"~cloth")) continue;   /* do not measure the clothes */
        unsigned short*bm=(unsigned short*)(P->pk+M->offBoneMap);
        float*rp=(float*)(P->pk+M->offPos);
        unsigned char*jp=(unsigned char*)(P->pk+M->offJoints);
        unsigned char*wp=(unsigned char*)(P->pk+M->offWeights);
        unsigned step=(M->verts>4000)?(M->verts/4000):1; if(!step) step=1;
        for(unsigned v=0;v<M->verts;v+=step){
            int best=-1, bw=-1;
            for(int q=0;q<4;q++){ int w=wp[v*4+q]; if(w>bw){ bw=w; best=jp[v*4+q]; } }
            if(best<0||best>=(int)M->boneCount||bw<=0) continue;
            unsigned nd=bm[best]; if(nd>=P->nNodes) continue;
            int c=c_of[nd]; if(c<0) continue;
            float dx=rp[v*3]-W[nd][12], dy=rp[v*3+1]-W[nd][13], dz=rp[v*3+2]-W[nd][14];
            acc[c]+=sqrtf(dx*dx+dy*dy+dz*dz); cnt[c]++; } }
    int got=0;
    for(int c=0;c<NCANON;c++) if(cnt[c]>8){ P->boneR[c]=acc[c]/(float)cnt[c]*1.35f; got++; }
    printf("[cloth] body measured: %d of %d canonical bones have a radius%c",got,NCANON,10);
    fflush(stdout); }

static int clothPrep(int slot,Pack*P,unsigned mi,float*pal,const float*model,M4*world,
                     GLint uModel,GLint uPal,GLint uBind,GLint uDqS){""",
    "measureBoneR")

sub("""        { float sc9=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]);
          float legU=1.f;
          if(P->pb[0]>=0&&P->footL>=0) legU=fabsf(world[P->pb[0]][13]-world[P->footL][13]);
          if(legU<1e-4f) legU=1.f;
          int bn[6]={P->pb[0],P->pb[1],P->pb[2],P->pb[3],P->pb[12],P->pb[14]};
          float rr[6]={0.26f,0.25f,0.25f,0.23f,0.16f,0.16f};
          for(int q=0;q<6&&nsph<CLOTH_MAXS;q++){
              if(bn[q]<0) continue;
              float wv[3];
              for(int k=0;k<3;k++)
                  wv[k]=model[k]*world[bn[q]][12]+model[4+k]*world[bn[q]][13]+model[8+k]*world[bn[q]][14]+model[12+k];
              sph[nsph][0]=wv[0]; sph[nsph][1]=wv[1]; sph[nsph][2]=wv[2];
              sph[nsph][3]=rr[q]*legU*sc9; nsph++; } }""",
    """        /* ⭐ EVERY CANONICAL BONE, AT ITS MEASURED THICKNESS. Six spheres with guessed
         * radii covered the spine and the feet and left the arms, thighs and shins bare,
         * which is exactly where a garment went through her. */
        { float sc9=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]);
          measureBoneR(P);
          for(int c=0;c<NCANON&&nsph<CLOTH_MAXS;c++){
              int bn=P->pb[c];
              if(bn<0||P->boneR[c]<=0.f) continue;
              float wv[3];
              for(int k=0;k<3;k++)
                  wv[k]=model[k]*world[bn][12]+model[4+k]*world[bn][13]+model[8+k]*world[bn][14]+model[12+k];
              sph[nsph][0]=wv[0]; sph[nsph][1]=wv[1]; sph[nsph][2]=wv[2];
              sph[nsph][3]=P->boneR[c]*sc9; nsph++; } }""",
    "collide against the measured body")

sub("#define CLOTH_MAXS 12", "#define CLOTH_MAXS 20", "room for every canonical bone")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
