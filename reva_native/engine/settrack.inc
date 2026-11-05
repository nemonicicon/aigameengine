/* THE SET'S TIMELINE (11-03, design/pdoom_video.md): a set whose lights, beams and props play back against the scene's clock.
 * jacob's music video wants a light show on the beat and a meter that climbs with the song, and a set was a thing that
 * sat still once loaded. The show is written by code, frame by frame, and the engine just plays it:
 *
 *   track <file> [seconds]         play <file>; the scene's clock plus [seconds] is the file's clock (a take that starts mid-song)
 *   mover <stem> [r g b] [grid=CxR]   a prop the track places. The Nth `mover` line is the track's Nth mover row. Hidden until placed.
 *                                  grid=CxR: its texture is a sheet of C x R pictures, and the track says which one it shows
 *   clipstart <seconds>            every z_rm_ clip, a set actor's or a scene body's, starts this far in (engine/williams.inc, footlock.inc)
 *
 * The file: one text line `TRK4 <fps> <frames> <lights> <beams> <movers> <looks> <cams> <posts>\n`, then little-endian floats, per frame:
 *   sky r g b  |  lights x y z r g b  |  beams x y z ux uy uz length r0 r1 r g b k
 *   |  movers x y z yaw pitch roll sx sy sz  r g b wash  alpha  picture  |  looks x y z on
 *   |  cam ex ey ez ax ay az fov roll on  |  post exposure sepia flash burn weaveX weaveY grain focus dof shutter haze grade
 * (step 25, 11-05) cams and posts are each 0 or 1. THE LENS: where it is, what it's aimed at, how wide (degrees up and down), how far rolled, and while `on` is over a
 * half it's the picture's lens, over a scene's own and the lab's. It's read between the file's frames, so a move written at 30 a second is smooth at 60; a jump of more
 * than 2.5 units in one frame is a cut and isn't eased. THE POST ROW is film's look for the frame (engine/post.inc): exposure, haze and grade under 0 leave the set's
 * own; focus 0 leaves the lens's, and under 0 focuses on the aim. TRK3 had neither row.
 * A mover at scale 0 is not drawn. (x y z) is where the prop's own origin goes. A piece of the show squashes and stretches (design/pdoom_references.md).
 * (step 21, 11-04) r g b wash: a colour, and how much of it goes over the mover's own texture or colour, 0 none to 1 all of it. alpha: 1 solid, down to 0 not there,
 * so smoke can thin out and not only shrink. picture: which cell of its sheet, counted along the rows from 0, and -1 is its own texture. A look is a place for scene
 * slot N's head and eyes to go to, while its `on` is over a half: it beats who's talking, and a body told not to look.
 * TRK2 had no colour, alpha, picture or looks, and TRK1 one scale and the prop stood on its lowest point: both still read. Beams and lights replace what the set's
 * own lines said, once the track has a frame. A set with no `track` line never comes here. */
static int g_stkMW=7;
static void trackLoad(const char*path,float off){
    if(g_stk){ free(g_stk); g_stk=NULL; }
    g_stkFrames=0; g_stkLast=-1; g_stkNK=0; memset(g_stkLook,0,sizeof g_stkLook);
    FILE*f=fopen(path,"rb");
    if(!f){ printf("[track] CANNOT OPEN %s -- the set will stand still\n",path); return; }
    char hd[128]; float fps=30.f; int fr=0,nl=0,nb=0,nm=0,nk=0,nc=0,np=0,ver=0,got=0;
    g_stkNC=g_stkNP=0; memset(g_stkCam,0,sizeof g_stkCam); memset(g_stkPost,0,sizeof g_stkPost);
    if(!fgets(hd,sizeof hd,f)||(got=sscanf(hd,"TRK%d %f %d %d %d %d %d %d %d",&ver,&fps,&fr,&nl,&nb,&nm,&nk,&nc,&np))<6||fr<=0||!(fps>0.f&&fps<1000.f)   /* (step 28) a rate that's a number: `fps<=0` let not-a-number through, and it went on to pick frames */
       ||nl<0||nb<0||nm<0||nl>64||nb>256||nm>8192||fr>4000000){
        printf("[track] %s is not a TRK file\n",path); fclose(f); return; }
    if(ver<3||got<7||nk<0||nk>64) nk=0;
    if(ver<4||got<9||nc<0||nc>1||np<0||np>1){ nc=0; np=0; }
    g_stkMW=ver>=3?15:ver>=2?9:7;
    int stride=3+nl*6+nb*13+nm*g_stkMW+nk*4+nc*9+np*12; size_t n=(size_t)fr*(size_t)stride;
    float*d=(float*)malloc(n*sizeof(float));
    if(!d||fread(d,sizeof(float),n,f)!=n){ printf("[track] %s is short: wanted %d frames of %d floats\n",path,fr,stride); free(d); fclose(f); return; }
    fclose(f);
    /* (step 28) every float of it is held to being a number of a world's size before a frame is read: a place, a colour, a turn, a picture's number. What isn't is 0, and it's said */
    { size_t bad9=0; for(size_t i9=0;i9<n;i9++) if(!(d[i9]>-1e8f&&d[i9]<1e8f)){ d[i9]=0.f; bad9++; } if(bad9){ printf("[track] %s: %zu of its numbers aren't numbers, and are taken as 0\n",path,bad9); fflush(stdout); } }
    g_stk=d; g_stkFrames=fr; g_stkFps=fps; g_stkOff=off; g_stkStride=stride; g_stkNL=nl; g_stkNB=nb; g_stkNM=nm; g_stkNK=nk; g_stkNC=nc; g_stkNP=np;
    printf("[track] %s: %d frames at %.0f, %d lights, %d beams, %d movers, %d looks%s%s, starting at %.2f s\n",path,fr,fps,nl,nb,nm,nk,nc?", the lens":"",np?", the post's look":"",off);
    if(nl>NLIGHT-1) printf("[track] %d lights, the set may have %d: the rest are ignored\n",nl,NLIGHT-1);
    if(nb>BEAM_MAX) printf("[track] %d beams, the engine has %d: the rest are ignored\n",nb,BEAM_MAX);
    if(nk>STK_LOOKS) printf("[track] %d looks, a scene has %d bodies to give them to: the rest are ignored\n",nk,STK_LOOKS); }

/* a mover's sheet of pictures: its first texture cut into cols x rows, each cell a texture of its own, counted along the rows from the top left of the picture.
 * The pixels are the ones loadPack just decoded and was asked to keep. A pack that was in memory already, or a compressed one, has none to cut, and says so */
static int stkSheet(int pk,int cols,int rows){
    for(int i=0;i<g_stkNSheet;i++) if(g_stkSheet[i].pk==pk) return 1;
    if(g_stkNSheet>=STK_SHEETS||cols<1||rows<1||cols*rows>256){ printf("[set] mover: a sheet is up to 256 pictures, and a set has %d sheets\n",STK_SHEETS); return 0; }
    const int n=cols*rows; unsigned*tex=(unsigned*)malloc(sizeof(unsigned)*(size_t)n); if(!tex) return 0;
    gfxTexturesNew(n,tex);
    if(g_texKeepPx&&g_texKeepW>=cols&&g_texKeepH>=rows){ const int cw=g_texKeepW/cols,ch=g_texKeepH/rows; unsigned char*cell=(unsigned char*)malloc((size_t)cw*(size_t)ch*4);
        for(int k=0;cell&&k<n;k++){ const int cx=k%cols,cy=g_texKeepFlip?rows-1-k/cols:k/cols;
            for(int y=0;y<ch;y++) memcpy(cell+(size_t)y*cw*4,g_texKeepPx+((size_t)(cy*ch+y)*g_texKeepW+(size_t)cx*cw)*4,(size_t)cw*4);
            gfxTexture(tex[k],cw,ch,cell); }
        free(cell); printf("[set] mover: a sheet of %d x %d pictures, %d x %d each\n",cols,rows,cw,ch); }
    else printf("[set] mover: nothing to cut its sheet from (the pack was in memory already, or it's compressed): its pictures are blank\n");
    g_stkSheet[g_stkNSheet].pk=pk; g_stkSheet[g_stkNSheet].n=n; g_stkSheet[g_stkNSheet].tex=tex; g_stkNSheet++; return 1; }
static void stkSheetsDrop(void){ for(int i=0;i<g_stkNSheet;i++){ gfxTexturesDrop(g_stkSheet[i].n,g_stkSheet[i].tex); free(g_stkSheet[i].tex); } g_stkNSheet=0; }

static void trackApply(void){
    if(!g_stk||g_stkFrames<=0) return;
    float st=g_stkScrub>=0.f?g_stkScrub:(g_scnT>0.f?g_scnT:0.f)+g_stkOff;
    if(g_stkScrub<-1.5f) st=(float)fmod(now_ms()*0.001-g_stkPlay0,(double)g_stkFrames/(double)g_stkFps);   /* trackt=play */
    int fr=(int)(st*g_stkFps+0.5f); if(fr<0) fr=0; if(fr>=g_stkFrames) fr=g_stkFrames-1;
    /* (step 25) the lens and the post's look are read every frame, between the file's frames */
    if(g_stkNC||g_stkNP){ float fp=st*g_stkFps; if(fp<0.f) fp=0.f; if(fp>(float)(g_stkFrames-1)) fp=(float)(g_stkFrames-1);
        const int f0=(int)fp, f1=f0+1<g_stkFrames?f0+1:f0; const float u=fp-(float)f0;
        const size_t off9=3u+(size_t)g_stkNL*6u+(size_t)g_stkNB*13u+(size_t)g_stkNM*(size_t)g_stkMW+(size_t)g_stkNK*4u;
        const float*a=g_stk+(size_t)f0*(size_t)g_stkStride+off9,*b=g_stk+(size_t)f1*(size_t)g_stkStride+off9;
        if(g_stkNC){ const float dx=b[0]-a[0],dy=b[1]-a[1],dz=b[2]-a[2]; const int cut=((a[8]>0.5f)!=(b[8]>0.5f))||dx*dx+dy*dy+dz*dz>6.25f;
            for(int k=0;k<8;k++) g_stkCam[k]=cut?a[k]:a[k]+(b[k]-a[k])*u;
            g_stkCam[8]=a[8]; a+=9; b+=9; }
        if(g_stkNP) for(int k=0;k<12;k++) g_stkPost[k]=(a[k]<0.f||b[k]<0.f)?a[k]:a[k]+(b[k]-a[k])*u; }
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
        if(g_stkMW>=15){ const float a=v[13]<0.f?0.f:v[13]>1.f?1.f:v[13];   /* its colour, how solid it is, and which picture */
            if(a<=0.004f){ I->hid=1; continue; }
            I->tint[0]=v[9]; I->tint[1]=v[10]; I->tint[2]=v[11]; I->wash=v[12]<0.f?0.f:v[12]>1.f?1.f:v[12]; I->fade=1.f-a; I->pic=v[14]>=0.f?(int)(v[14]+0.5f)+1:0; }
        Pack*Q=&g_setPk[I->pk]; float k=(Q->scale>1e-6f)?Q->scale:1.f, eff=sc*k;
        float qy[4],qx[4],qz[4],q1[4],q2[4];
        qaxis(qy,0,1,0,v[3]*0.0174532925f); qaxis(qx,1,0,0,v[4]*0.0174532925f); qaxis(qz,0,0,1,v[5]*0.0174532925f);
        qmul(q1,qy,qx); qmul(q2,q1,qz);
        float t3[3]={v[0],g_stkMW>=9?v[1]:v[1]-Q->mny*eff,v[2]}, s3[3]={eff,sy*k,sz*k};
        m4_trs(I->m,t3,q2,s3); }
    /* where each scene body's to look, if the track says */
    { const float*lk=d+(size_t)g_stkNM*(size_t)g_stkMW; for(int k=0;k<g_stkNK&&k<STK_LOOKS;k++) memcpy(g_stkLook[k],lk+k*4,16); } }
/* (step 25) this frame's lens and look: what stands (a ctl line, the set's, a scene line's, a game's), and over it what the timeline says for the frame.
 * eye and aim are the frame's lens as everything before this decided it; if the timeline has the lens they're written over */
static void lensNow(float*eye,float*aim){
    g_lensNow.roll=g_camRoll; g_lensNow.focus=g_lensFocus; g_lensNow.dof=g_lensDof; g_lensNow.shutter=g_lensShutter;
    g_lensNow.sepia=g_filmSepia; g_lensNow.flash=g_filmFlash; g_lensNow.burn=g_filmBurn; g_lensNow.grain=g_filmGrain; g_lensNow.weave[0]=g_filmWeave[0]; g_lensNow.weave[1]=g_filmWeave[1];
    g_lensNow.exposure=g_lensNow.haze=g_lensNow.grade=-1.f; g_lensNow.fov=-1.f; g_lensNow.camOn=0;
    if(g_stk&&g_stkNC&&g_stkCam[8]>0.5f){ g_lensNow.camOn=1; memcpy(eye,g_stkCam,12); memcpy(aim,g_stkCam+3,12); g_lensNow.fov=g_stkCam[6]; g_lensNow.roll=g_stkCam[7]; }
    if(g_stk&&g_stkNP){ const float*q=g_stkPost;
        if(q[0]>0.f) g_lensNow.exposure=q[0];
        g_lensNow.sepia=fmaxf(g_lensNow.sepia,q[1]); g_lensNow.flash+=q[2]; g_lensNow.burn=fmaxf(g_lensNow.burn,q[3]); g_lensNow.weave[0]+=q[4]; g_lensNow.weave[1]+=q[5]; g_lensNow.grain=fmaxf(g_lensNow.grain,q[6]);
        if(q[7]!=0.f){ g_lensNow.focus=q[7]; g_lensNow.dof=q[8]; }
        if(q[9]>0.f) g_lensNow.shutter=q[9];
        if(q[10]>=0.f) g_lensNow.haze=q[10];
        if(q[11]>=0.f) g_lensNow.grade=q[11]; }
    memcpy(g_lensNow.eye,eye,12); memcpy(g_lensNow.aim,aim,12);
    if(g_lensNow.focus<0.f){ const float dx=aim[0]-eye[0],dy=aim[1]-eye[1],dz=aim[2]-eye[2]; g_lensNow.focus=sqrtf(dx*dx+dy*dy+dz*dz); } }
