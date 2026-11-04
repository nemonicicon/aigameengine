/* THE SET'S TIMELINE (11-03, design/pdoom_video.md): a set whose lights, beams and props play back against the scene's clock.
 * jacob's music video wants a light show on the beat and a meter that climbs with the song, and a set was a thing that
 * sat still once loaded. The show is written by code, frame by frame, and the engine just plays it:
 *
 *   track <file> [seconds]     play <file>; the scene's clock plus [seconds] is the file's clock (a take that starts mid-song)
 *   mover <stem> [r g b]       a prop the track places. The Nth `mover` line is the track's Nth mover row. Hidden until placed.
 *
 * The file: one text line `TRK2 <fps> <frames> <lights> <beams> <movers>\n`, then little-endian floats, per frame:
 *   sky r g b  |  lights x y z r g b  |  beams x y z ux uy uz length r0 r1 r g b k  |  movers x y z yaw pitch roll sx sy sz
 * A mover at scale 0 is not drawn. (x y z) is where the prop's own origin goes. A piece of the show squashes and stretches (design/pdoom_references.md); TRK1 had one scale and stood the prop on its lowest point, and still reads. Beams and lights replace what the set's own lines said, once the track has a frame.
 * A set with no `track` line never comes here. */
static int g_stkMW=7;
static void trackLoad(const char*path,float off){
    if(g_stk){ free(g_stk); g_stk=NULL; }
    g_stkFrames=0; g_stkLast=-1;
    FILE*f=fopen(path,"rb");
    if(!f){ printf("[track] CANNOT OPEN %s -- the set will stand still\n",path); return; }
    char hd[128]; float fps=30.f; int fr=0,nl=0,nb=0,nm=0,ver=0;
    if(!fgets(hd,sizeof hd,f)||sscanf(hd,"TRK%d %f %d %d %d %d",&ver,&fps,&fr,&nl,&nb,&nm)!=6||fr<=0||fps<=0.f){
        printf("[track] %s is not a TRK file\n",path); fclose(f); return; }
    g_stkMW=ver>=2?9:7;
    int stride=3+nl*6+nb*13+nm*g_stkMW; size_t n=(size_t)fr*(size_t)stride;
    float*d=(float*)malloc(n*sizeof(float));
    if(!d||fread(d,sizeof(float),n,f)!=n){ printf("[track] %s is short: wanted %d frames of %d floats\n",path,fr,stride); free(d); fclose(f); return; }
    fclose(f);
    g_stk=d; g_stkFrames=fr; g_stkFps=fps; g_stkOff=off; g_stkStride=stride; g_stkNL=nl; g_stkNB=nb; g_stkNM=nm;
    printf("[track] %s: %d frames at %.0f, %d lights, %d beams, %d movers, starting at %.2f s\n",path,fr,fps,nl,nb,nm,off);
    if(nl>NLIGHT-1) printf("[track] %d lights, the set may have %d: the rest are ignored\n",nl,NLIGHT-1);
    if(nb>BEAM_MAX) printf("[track] %d beams, the engine has %d: the rest are ignored\n",nb,BEAM_MAX); }

static void trackApply(void){
    if(!g_stk||g_stkFrames<=0) return;
    float st=(g_scnT>0.f?g_scnT:0.f)+g_stkOff;
    int fr=(int)(st*g_stkFps+0.5f); if(fr<0) fr=0; if(fr>=g_stkFrames) fr=g_stkFrames-1;
    if(fr==g_stkLast) return;
    g_stkLast=fr;
    const float*d=g_stk+(size_t)fr*(size_t)g_stkStride;
    g_sky[0]=d[0]; g_sky[1]=d[1]; g_sky[2]=d[2]; d+=3;
    int nl=g_stkNL<NLIGHT-1?g_stkNL:NLIGHT-1;
    for(int k=0;k<g_stkNL;k++,d+=6){ if(k>=nl) continue;
        for(int c=0;c<3;c++){ g_setLp[k*3+c]=d[c]; g_setLc[k*3+c]=d[3+c]; }
        g_setLk[k]=0.f; }
    g_setNL=nl;
    int nb=g_stkNB<BEAM_MAX?g_stkNB:BEAM_MAX;
    for(int k=0;k<g_stkNB;k++,d+=13){ if(k>=nb) continue;
        for(int c=0;c<3;c++){ g_beamA[k][c]=d[c]; g_beamU[k][c]=d[3+c]; g_beamC[k][c]=d[9+c]; }
        g_beamL[k]=d[6]; g_beamR[k][0]=d[7]; g_beamR[k][1]=d[8]; g_beamK[k]=d[12]; g_beamF[k]=1.f; g_beamT[k]=0.6f; }
    g_nBeam=nb;
    for(int i=0;i<g_setNI;i++){ SetInst*I=&g_setI[i];
        if(I->mover<0) continue;
        if(I->mover>=g_stkNM){ I->hid=1; continue; }
        const float*v=d+I->mover*g_stkMW; float sc=v[6], sy=g_stkMW>=9?v[7]:sc, sz=g_stkMW>=9?v[8]:sc;
        I->hid=(sc<=1e-5f||sy<=1e-5f||sz<=1e-5f); if(I->hid) continue;
        Pack*Q=&g_setPk[I->pk]; float k=(Q->scale>1e-6f)?Q->scale:1.f, eff=sc*k;
        float qy[4],qx[4],qz[4],q1[4],q2[4];
        qaxis(qy,0,1,0,v[3]*0.0174532925f); qaxis(qx,1,0,0,v[4]*0.0174532925f); qaxis(qz,0,0,1,v[5]*0.0174532925f);
        qmul(q1,qy,qx); qmul(q2,q1,qz);
        float t3[3]={v[0],g_stkMW>=9?v[1]:v[1]-Q->mny*eff,v[2]}, s3[3]={eff,sy*k,sz*k};
        m4_trs(I->m,t3,q2,s3); } }
