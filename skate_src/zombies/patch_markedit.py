"""patch_markedit.py -- POINT AT HER FACE AND CLICK. jacob places the marks himself.

jacob: "man i could place them for you eaasier"

⭐ HE IS RIGHT AND IT IS NOT CLOSE. Placing a face mark is a thing a person does by LOOKING,
and I have just spent six render-sweeps converging on where kaeli's eyes are by bisection --
too wide, then too high, then too low -- while he could have pointed at them in a second.
face_calibrate.py cannot do it either (her liner and fringe defeat the darkest-texel rule),
so the automatic path is genuinely out. That leaves the person who can see.

    REVA_MARKEDIT=1  +  REVA_NOMOUSEGRAB=1

    1 2 3 4     choose eye_l / eye_r / mouth / chin
    LEFT CLICK  put that mark where you clicked on her
    the file is written on EVERY click, so there is nothing to save and nothing to lose

⭐ THE CLICK LANDS IN RIG SPACE, WHICH IS WHAT A MARK IS. The ray is intersected against her
SKINNED triangles -- where she actually is on screen, in whatever pose she is in -- but the
point handed back is the barycentric blend of that triangle's REST positions. So a mark
placed on a posed, walking, tilted body is still correct in the rest frame the pack stores.
⛔ WHICH IS ALSO THE BUG THAT COST ME THREE ROUNDS: I was judging marks on `idle`, where her
head is tilted at the floor, so I was correcting a projection instead of a position. Doing
the transform properly means the pose no longer matters at all.

Then: `face_marks.py <pack> --from marks_<subject>.txt` bakes exactly those points in.
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


BLOCK = r'''
/* ==================== MARK EDITOR ===================================================
 * jacob: "man i could place them for you eaasier".
 * ⭐ AND HE CAN. Placing a face mark is a thing a person does by LOOKING, and I converged
 * on kaeli's eyes by bisecting render sweeps -- too wide, then too high, then too low --
 * while he could have pointed at them. face_calibrate.py cannot do it on her either (heavy
 * liner and a blue fringe defeat the darkest-texel rule), so the automatic path is out and
 * the person who can see is the right instrument.
 *
 *   REVA_MARKEDIT=1 + REVA_NOMOUSEGRAB=1
 *   1 2 3 4      choose eye_l / eye_r / mouth / chin
 *   LEFT CLICK   place it. The file is written on every click; nothing to save.
 *
 * ⭐ THE CLICK LANDS IN RIG SPACE. The ray hits her SKINNED triangles -- where she really is
 * on screen, in whatever pose she is in -- and the point returned is the barycentric blend
 * of that triangle's REST positions. A mark placed on a posed, tilted, walking body is
 * therefore still correct in the rest frame the pack stores, which is exactly the mistake
 * that cost me three rounds by hand. */
static int   g_markEdit=0, g_markSel=0;
static float g_markPt[4][3]; static int g_markGot[4]={0,0,0,0};
static const char*MARKNM[4]={"face_eye_l","face_eye_r","face_mouth","face_chin"};

/* world ray from a screen pixel, using the frame's own view-projection */
static void markRay(float sx,float sy,int W,int H,const float*vp,const float*eye,float*dir){
    /* invert vp by solving for two points on the ray: unproject at z=-1 and z=+1 is the
     * textbook way and needs the inverse; instead, march the eye toward the pixel using the
     * fact that vp maps world -> clip, and do it by finite difference on a plane 10 units
     * out. Cheap, exact enough to pick a triangle, and needs no matrix inverse. */
    float ndx=(2.f*sx/(float)W)-1.f, ndy=1.f-(2.f*sy/(float)H);
    /* basis: the rows of vp give the clip axes; the camera forward is row2 of the view part */
    float r[3]={vp[0],vp[4],vp[8]}, u[3]={vp[1],vp[5],vp[9]}, f[3]={vp[3],vp[7],vp[11]};
    float rl=sqrtf(r[0]*r[0]+r[1]*r[1]+r[2]*r[2]); if(rl<1e-6f) rl=1.f;
    float ul=sqrtf(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]); if(ul<1e-6f) ul=1.f;
    float fl=sqrtf(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]); if(fl<1e-6f) fl=1.f;
    for(int k=0;k<3;k++) dir[k]=f[k]/fl + (r[k]/rl)*(ndx/rl*fl) + (u[k]/ul)*(ndy/ul*fl);
    float dl=sqrtf(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]); if(dl<1e-6f) dl=1.f;
    for(int k=0;k<3;k++) dir[k]/=dl;
    (void)eye; }

/* ray vs the subject's skinned triangles; returns the REST position of the hit */
static int markPick(Pack*P,M4*world,const float*model,const float*ro,const float*rd,float*restOut){
    float bestT=1e18f; int got=0;
    for(unsigned mi=0;mi<P->nMeshes;mi++){
        PMesh*M=&P->meshes[mi];
        if(!(M->flags&4)||!M->boneCount||M->boneCount>MAXB) continue;
        if(strstr(packStr(P,M->name),"~cloth")) continue;
        unsigned short*bm=(unsigned short*)(P->pk+M->offBoneMap);
        float*ibm=(float*)(P->pk+M->offIBM);
        float*rp=(float*)(P->pk+M->offPos);
        unsigned char*jp=(unsigned char*)(P->pk+M->offJoints);
        unsigned char*wp=(unsigned char*)(P->pk+M->offWeights);
        unsigned*ix=(unsigned*)(P->pk+M->offIdx);
        if(!M->idxCount) continue;
        static float pal[MAXB*16];
        for(unsigned b=0;b<M->boneCount;b++){ M4 pm; m4_mul(pm,world[bm[b]],*(M4*)(ibm+b*16)); memcpy(pal+b*16,pm,64); }
        for(unsigned t=0;t+2<M->idxCount;t+=3){
            unsigned id[3]={ix[t],ix[t+1],ix[t+2]};
            float wv[3][3];
            for(int c=0;c<3;c++){
                unsigned v=id[c]; float acc[3]={0,0,0}, ws=0.f;
                for(int q=0;q<4;q++){ float w=wp[v*4+q]/255.f; if(w<=0.f) continue;
                    float*pm=pal+jp[v*4+q]*16;
                    for(int k=0;k<3;k++) acc[k]+=w*(pm[k]*rp[v*3]+pm[4+k]*rp[v*3+1]+pm[8+k]*rp[v*3+2]+pm[12+k]);
                    ws+=w; }
                if(ws<1e-4f){ acc[0]=rp[v*3]; acc[1]=rp[v*3+1]; acc[2]=rp[v*3+2]; }
                for(int k=0;k<3;k++) wv[c][k]=model[k]*acc[0]+model[4+k]*acc[1]+model[8+k]*acc[2]+model[12+k]; }
            /* Moller-Trumbore */
            float e1[3],e2[3],pv[3],tv[3],qv[3];
            for(int k=0;k<3;k++){ e1[k]=wv[1][k]-wv[0][k]; e2[k]=wv[2][k]-wv[0][k]; }
            pv[0]=rd[1]*e2[2]-rd[2]*e2[1]; pv[1]=rd[2]*e2[0]-rd[0]*e2[2]; pv[2]=rd[0]*e2[1]-rd[1]*e2[0];
            float det=e1[0]*pv[0]+e1[1]*pv[1]+e1[2]*pv[2];
            if(fabsf(det)<1e-9f) continue;
            float inv=1.f/det;
            for(int k=0;k<3;k++) tv[k]=ro[k]-wv[0][k];
            float u=(tv[0]*pv[0]+tv[1]*pv[1]+tv[2]*pv[2])*inv;
            if(u<0.f||u>1.f) continue;
            qv[0]=tv[1]*e1[2]-tv[2]*e1[1]; qv[1]=tv[2]*e1[0]-tv[0]*e1[2]; qv[2]=tv[0]*e1[1]-tv[1]*e1[0];
            float v2=(rd[0]*qv[0]+rd[1]*qv[1]+rd[2]*qv[2])*inv;
            if(v2<0.f||u+v2>1.f) continue;
            float tt=(e2[0]*qv[0]+e2[1]*qv[1]+e2[2]*qv[2])*inv;
            if(tt<=0.01f||tt>=bestT) continue;
            bestT=tt; got=1;
            /* ⭐ THE REST POSITION, not the world one -- that is what a mark IS. */
            for(int k=0;k<3;k++)
                restOut[k]=rp[id[0]*3+k]*(1.f-u-v2)+rp[id[1]*3+k]*u+rp[id[2]*3+k]*v2; } }
    return got; }

static void markWrite(const char*subj){
    char pth[256]; snprintf(pth,sizeof pth,"anim_bench/marks_%s.txt",subj?subj:"subject");
    FILE*f=fopen(pth,"wb"); if(!f) return;
    fprintf(f,"# placed by hand in the mark editor (REVA_MARKEDIT=1). Rig space.\n");
    fprintf(f,"# bake with: face_marks.py <pack> --from %s\n",pth);
    for(int i=0;i<4;i++) if(g_markGot[i])
        fprintf(f,"%s %.6f %.6f %.6f\n",MARKNM[i],g_markPt[i][0],g_markPt[i][1],g_markPt[i][2]);
    fclose(f);
    printf("[mark] wrote %s%c",pth,10); fflush(stdout); }
'''

sub("static void scnHeadLayer(Pack*P,float Q[][4],int who){",
    BLOCK + "\nstatic void scnHeadLayer(Pack*P,float Q[][4],int who){",
    "the mark editor")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
