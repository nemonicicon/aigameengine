/* ================= THE PANIC LAYER (OVERVIEW #2; campaign S2) =======================
 * A general rule, not a per-mode hack: any slot that is not on a team, not armed and
 * not EXEMPT, within R of a recent kill, gets a FLEE command away from the danger
 * point. Special ones (the narrative cast, faction heroes) carry the exempt flag and
 * hold ground. The crowd scatters; the fighters stand out by contrast. */
static void dngAdd(float x,float z){
    g_dng[g_dngHead].x=x; g_dng[g_dngHead].z=z; g_dng[g_dngHead].t=0.f;
    g_dngHead=(g_dngHead+1)%DNG_MAX; }
static void pnicStep(float dtf){
    for(int d=0;d<DNG_MAX;d++) g_dng[d].t+=dtf;
    for(int i=0;i<g_NS;i++){
        if(g_slotFleeT[i]>0.f){ g_slotFleeT[i]-=dtf;
            if(g_slotFleeT[i]<=0.f){ g_slotSpdMul[i]=0.f;   /* the panic passes: back to the party */
                g_slots[i].state=0; g_slots[i].moving=0; g_slots[i].tT=1.f+frnd()*2.f;
                if(g_slotBhv[i]==BHV_RUN) g_slotBhv[i]=BHV_NONE; } }
        if(g_slotHide[i]||!g_slotDrawable[i]||g_slotAlt[i]>0.f||g_slotBhv[i]==BHV_DOWN) continue;
        if(g_slotExempt[i]||g_slotTeam[i]||g_slotWpn[i]>=WPN_PISTOL) continue;
        for(int d=0;d<DNG_MAX;d++){ if(g_dng[d].t>2.5f) continue;
            float dx=g_slots[i].x-g_dng[d].x, dz=g_slots[i].z-g_dng[d].z;
            float dd=hypotf(dx,dz);
            if(dd>14.f||dd<0.01f) continue;
            g_slots[i].tx=g_slots[i].x+dx/dd*16.f; g_slots[i].tz=g_slots[i].z+dz/dd*16.f;
            g_slots[i].state=1; g_slots[i].moving=1; g_slots[i].mate=-1;
            if(g_slotSpdMul[i]<2.6f) g_slotSpdMul[i]=2.6f;
            g_slotFleeT[i]=2.f+frnd()*2.f;
            g_slotBhv[i]=BHV_RUN; g_slotBhvT[i]=0.f;   /* panic RUNS (run clip where the pack has one) */
            break; } } }
/* corpse markers: a death leaves a fading X instead of a person vanishing untraced */
static void crpAdd(float x,float z,int team){
    g_crp[g_crpHead].x=x; g_crp[g_crpHead].z=z; g_crp[g_crpHead].t=0.f; g_crp[g_crpHead].team=team;
    g_crpHead=(g_crpHead+1)%CRP_MAX; }
static void crpStep(float dtf){
    for(int c=0;c<CRP_MAX;c++){ if(g_crp[c].t>5.f) continue;
        g_crp[c].t+=dtf; float f=1.f-g_crp[c].t/5.f; if(f<=0.f) continue;
        float r=g_crp[c].team==1?0.3f*f:1.2f*f, g=0.35f*f, b=g_crp[c].team==1?1.3f*f:(g_crp[c].team==2?0.3f*f:1.0f*f);
        gmLine(g_crp[c].x-0.7f,0.14f,g_crp[c].z-0.7f,g_crp[c].x+0.7f,0.14f,g_crp[c].z+0.7f,r,g,b);
        gmLine(g_crp[c].x-0.7f,0.14f,g_crp[c].z+0.7f,g_crp[c].x+0.7f,0.14f,g_crp[c].z-0.7f,r,g,b); } }
/* S4a: deaths get a BODY. slotDie freezes the slot and raises BHV_DOWN; the anim layer
 * plays the pack's down clip (or topples the model procedurally) while bhvStep counts
 * to 0.95s, and only THEN does the g_slotHide door close. The falling are ghosts to
 * every targeting loop. */
static void slotDie(int i){
    if(g_slots&&i>=0&&i<g_NS){ float p9[3]={g_slots[i].x,1.2f,g_slots[i].z}; sfxPlay("kill",0.7f,p9,0); }
    if(i<0||i>=g_NS||g_slotHide[i]||g_slotBhv[i]==BHV_DOWN) return;
    g_dieN++;
    g_slotBhv[i]=BHV_DOWN; g_slotBhvT[i]=0.f;
    g_slots[i].moving=0; g_slots[i].state=0; g_slots[i].spd=0.f; g_slotSpdMul[i]=0.01f;
    g_slotSel[i]=0; }
static void bhvStep(float dtf){
    for(int i=0;i<g_NS;i++){ if(!g_slotBhv[i]) continue;
        g_slotBhvT[i]+=dtf;
        if(g_slotBhv[i]==BHV_ATK&&g_slotBhvT[i]>0.65f){ g_slotBhv[i]=BHV_NONE; g_slotBhvT[i]=0.f; }
        else if(g_slotBhv[i]==BHV_DOWN&&g_slotBhvT[i]>0.95f){
            g_slotBhv[i]=BHV_NONE; g_slotBhvT[i]=0.f;
            g_slotHide[i]=1; g_slotBan[i]=1; g_hideN++;
            { static int pd9=-2; if(pd9==-2){ const char*e=getenv("REVA_POSEDUMP"); pd9=e?atoi(e):-9999; }
              if(pd9==i||pd9==-5) printf("[bhvev] gameT=%.2f si=%d DOWN expired -> hidden\n",g_gameT,i); } } } }
/* the pass itself: at the top of the frame, before the main target binds */
static void sunPass(void){
    int want=(g_shadow>=0)?g_shadow:(g_sun[3]>0.f);
    g_shadowOn=want?1:0;
    { static int said=-1; if(said!=g_shadowOn){ said=g_shadowOn;
        if(g_shadowOn) printf("[shadow] sun pass ON: %dx%d, window +-%.0f round the lens aim\n",SUNMAP,SUNMAP,g_shadowExt);
        else printf("[shadow] off (no sun up, or shadow=0)\n");
        fflush(stdout); } }
    if(!g_shadowOn){ g_nSunCast=g_nSunCrowd=0; return; }
    float L[3]={g_sun[0],g_sun[1],g_sun[2]}; float ll=sqrtf(L[0]*L[0]+L[1]*L[1]+L[2]*L[2]);
    if(ll<1e-5f){ L[0]=0.3f; L[1]=1.f; L[2]=0.2f; ll=sqrtf(1.13f); }
    L[0]/=ll; L[1]/=ll; L[2]/=ll;
    /* the light's basis: fz points from the scene TO the sun (the eye sits up the sun ray) */
    float fz[3]={L[0],L[1],L[2]}; float up[3]={0,1,0}; if(fabsf(L[1])>0.97f){ up[1]=0; up[2]=1; }
    float sx[3]={ up[1]*fz[2]-up[2]*fz[1], up[2]*fz[0]-up[0]*fz[2], up[0]*fz[1]-up[1]*fz[0] };
    float sl=sqrtf(sx[0]*sx[0]+sx[1]*sx[1]+sx[2]*sx[2]); sx[0]/=sl; sx[1]/=sl; sx[2]/=sl;
    float uy[3]={ fz[1]*sx[2]-fz[2]*sx[1], fz[2]*sx[0]-fz[0]*sx[2], fz[0]*sx[1]-fz[1]*sx[0] };
    /* ⭐ TEXEL SNAP: quantise the focus in the light's own basis, so the window slides in whole
     * texels and a still scene under a moving lens does not crawl */
    float E=g_shadowExt, tx=2.f*E/(float)SUNMAP;
    float C[3]={g_sunFocus[0],g_sunFocus[1],g_sunFocus[2]};
    float cx=C[0]*sx[0]+C[1]*sx[1]+C[2]*sx[2], cy=C[0]*uy[0]+C[1]*uy[1]+C[2]*uy[2];
    float qx=floorf(cx/tx)*tx-cx, qy=floorf(cy/tx)*tx-cy;
    for(int i=0;i<3;i++) C[i]+=sx[i]*qx+uy[i]*qy;
    float ex=C[0]+fz[0]*120.f, ey=C[1]+fz[1]*120.f, ez=C[2]+fz[2]*120.f;
    M4 view; view[0]=sx[0];view[1]=uy[0];view[2]=fz[0];view[3]=0; view[4]=sx[1];view[5]=uy[1];view[6]=fz[1];view[7]=0;
    view[8]=sx[2];view[9]=uy[2];view[10]=fz[2];view[11]=0;
    view[12]=-(sx[0]*ex+sx[1]*ey+sx[2]*ez); view[13]=-(uy[0]*ex+uy[1]*ey+uy[2]*ez); view[14]=-(fz[0]*ex+fz[1]*ey+fz[2]*ez); view[15]=1;
    M4 ortho; memset(ortho,0,64); { float zn=1.f, zf=260.f; ortho[0]=1.f/E; ortho[5]=1.f/E; ortho[10]=-2.f/(zf-zn); ortho[14]=-(zf+zn)/(zf-zn); ortho[15]=1.f; }
    m4_mul(g_sunVP,ortho,view);
    g_shadowPass=1; if(g_setNI||g_nPipe) drawSet(); else objDraw(); g_shadowPass=0;
    for(int ci=0;ci<g_nSunCast;ci++){ SunCast*S=&g_sunCast[ci]; Pack*Pc=S->P; PMesh*M=&Pc->meshes[S->mi];
        unsigned bw9[6]; unsigned*b=&Pc->vbo[S->mi*6];
        if(S->vb0){ memcpy(bw9,b,sizeof bw9); bw9[0]=S->vb0; if(S->vb1) bw9[1]=S->vb1; b=bw9; }
        { static const float wh9[3]={1.f,1.f,1.f}; g_vkOwner=S->owner; g_vkIdxIb=S->ib; g_vkIdxN=S->ni; vkMesh(Pc,(unsigned)S->mi,S->model,S->world?NULL:(float*)(Pc->pk+M->offBind),S->pal,M->boneCount,wh9,1.f,GFX_SHADOW,b); g_vkIdxIb=g_vkIdxN=0; }
    }
    { static int said2=0; if(!said2&&g_nSunCast>0){ said2=1; printf("[shadow] first pass: %d stashed casters (%d crowd), %d props in the set\n",g_nSunCast,g_nSunCrowd,g_setNI); fflush(stdout); } }
    g_nSunCast=g_nSunCrowd=0; }

static void camDirector(double tsec,double dt,int NS,void*slots_,float*eye,float*ctr){
    bpmPoll();   /* moved above the debug return -- REVA_CAM runs were freezing bpm/SoC on the HUD */
    if(GAME_ON&&g_gameLens.on){ for(int i9=0;i9<3;i9++){ eye[i9]=g_gameLens.e[i9]; ctr[i9]=g_gameLens.a[i9]; g_lastE[i9]=eye[i9]; g_lastC[i9]=ctr[i9]; } return; }   /* a game that put its lens somewhere */
    /* GAME MODES pin their own lens BEFORE the director's dwell logic -- that logic has
     * early returns (the shot-9 arrival hold), so a pin at the shot switch is dead code
     * on exactly the frames the director is busy. Action modes ride the follow cam, the
     * RTS looks straight down at its cursor, the platformer stands side-on to the lane.
     * The showcase keeps the dance floor for DANCE and ARCADE. */
    if(GAME_ON) CAMD.shot=22;   /* a game: the lens its tuning sets */
    else if(g_mode==MODE_ANIM) CAMD.shot=23;   /* the lab: side-on, tracking, so the gait reads */
    /* ⭐ A TRACKING LENS IS A PLACED LENS. It pins the shot that owns g_camOn, and it is gated out of
     * first person and the F310 chase cam below -- both fire BEFORE the shot switch, and the camera law
     * (CLAUDE.md) is that the chase cam has already eaten three game lenses. */
    int trk10=(g_camTrk.mode&&g_camOn);
    if(trk10) CAMD.shot=23;
    /* S4g INSTRUMENT: REVA_CAMCHAT=1 finds a pair the social sim has put INTO conversation
     * and orbits them. The chase cam lives on the hero, so crowd states were unwatchable;
     * this is how the chat/idle bodies get eyes on them. REVA_CAMSLOT=<n> parks on one slot. */
    { static int ccInit=0, ccOn=0, ccPin=-1, ccWpn=0; static int ccSlot=-1;
      if(!ccInit){ ccInit=1; ccOn=getenv("REVA_CAMCHAT")?1:0;
                   ccWpn=getenv("REVA_CAMWPN")?1:0;
                   const char*e=getenv("REVA_CAMSLOT"); ccPin=e?atoi(e):-1; }
      if(ccWpn){   /* park on whoever is carrying: the weapon rack, in the field */
          if(ccSlot<0||g_slotHide[ccSlot]||g_slotWpn[ccSlot]<WPN_PISTOL){
              ccSlot=-1;
              for(int i=0;i<g_NS;i++)
                  if(!g_slotHide[i]&&g_slotDrawable[i]&&g_slotAlt[i]<=0.f&&g_slotWpn[i]>=WPN_PISTOL){ ccSlot=i; break; } } }
      else if(ccOn||ccPin>=0){
          if(ccPin>=0) ccSlot=ccPin;
          else if(ccSlot<0||g_slotHide[ccSlot]||g_slots[ccSlot].state!=3){
              ccSlot=-1;
              for(int i=0;i<g_NS;i++)
                  if(!g_slotHide[i]&&g_slotDrawable[i]&&g_slotAlt[i]<=0.f
                     &&g_slots[i].state==3&&g_slots[i].mate>=0){ ccSlot=i; break; } } }
      if((ccOn||ccWpn||ccPin>=0)){
          if(ccSlot>=0&&g_slots){ Slot*s=&g_slots[ccSlot];
              Slot*o=(s->mate>=0&&s->mate<g_NS)?&g_slots[s->mate]:s;
              float mx=(s->x+o->x)*0.5f, mz=(s->z+o->z)*0.5f;
              static float ccR=0.f; if(ccR==0.f){ const char*e2=getenv("REVA_CAMCHATR"); ccR=e2?(float)atof(e2):6.5f; }
              float a=(float)tsec*0.16f;
              eye[0]=mx+sinf(a)*ccR; eye[1]=2.3f; eye[2]=mz+cosf(a)*ccR;
              ctr[0]=mx; ctr[1]=1.35f; ctr[2]=mz;
              memcpy(g_lastE,eye,12); memcpy(g_lastC,ctr,12); return; } } }
    if(getenv("REVA_CAM")){   /* debug lens: park on reva for mesh diagnosis */
        float a=(float)tsec*0.25f;
        eye[0]=g_rvx+sinf(a)*3.2f; eye[1]=2.0f; eye[2]=g_rvz+cosf(a)*3.2f;
        ctr[0]=g_rvx; ctr[1]=1.7f; ctr[2]=g_rvz; return; }
    /* ⭐ FIRST PERSON, gated HERE, before the chase cam -- the camera law (CLAUDE.md): the F310
     * chase fires before the shot switch whenever g_manual=1 and has eaten three lenses.
     * The eye sits a face-depth ahead of the head bone the draw published last frame (one
     * frame of lag, invisible at 60); the mouse already turns HER in keyboard mode, so the
     * look is her heading plus the pad's right stick, and pitch is the stick or the mouse. */
    { static int fpvEnv=-1; if(fpvEnv<0){ const char*e=getenv("REVA_FPV"); fpvEnv=(e&&e[0]!='0')?1:0; if(fpvEnv) g_fpv=1; } }
    g_fpvNow=0;
    if(g_fpv&&g_manual&&!trk10&&!DOOR_OWNS_LENS&&g_mode!=MODE_ANIM&&!GAME_ON){
        float yaw=g_rvHead+(g_kbActive?0.f:g_rx*1.35f);
        float pitch=g_kbActive?(-g_mPitch*0.95f):(-g_ry*0.95f);
        if(pitch>1.35f)pitch=1.35f;
        if(pitch<-1.35f)pitch=-1.35f;
        float hy=g_heroHeadOK?(g_heroHeadW[1]+0.10f):(2.55f+g_rvAlt);
        float fx=sinf(yaw), fz=cosf(yaw);
        eye[0]=g_rvx+fx*0.30f; eye[1]=hy; eye[2]=g_rvz+fz*0.30f;
        float cp=cosf(pitch);
        ctr[0]=eye[0]+fx*cp; ctr[1]=eye[1]+sinf(pitch); ctr[2]=eye[2]+fz*cp;
        for(int i2=0;i2<3;i2++){ g_lastE[i2]=eye[i2]; g_lastC[i2]=ctr[i2]; }
        g_fpvNow=1; CAMD.t=0;
        { static int nf=0; if(++nf==30){ printf("[fpv] eye %.2f,%.2f,%.2f head %s\n",eye[0],eye[1],eye[2],g_heroHeadOK?"published":"ESTIMATED (no head mark)"); fflush(stdout); } }   /* the 30th frame: the draw has published by then; frame 1 is always ESTIMATED */
        return; }
    if(g_manual&&!trk10&&!DOOR_OWNS_LENS&&g_mode!=MODE_ANIM&&!GAME_ON){   /* F310 chase cam: glued behind her; right stick orbits + tilts. The platformer, the RTS and the zombie cab own their lens outright -- the chase cam fires before the shot switch and eats their cases (three victims now: pin any new game lens HERE first). */
        float az=g_rvHead+g_rx*1.35f;
        float hgt=2.6f+g_rvAlt*0.85f-g_ry*2.0f; if(hgt<0.8f)hgt=0.8f;
        static float se[3],sc[3]; static int ini=0;
        float de[3]={ g_rvx-sinf(az)*6.5f, hgt, g_rvz-cosf(az)*6.5f };
        float dc[3]={ g_rvx+sinf(az)*4.0f, 1.4f+g_rvAlt*0.9f, g_rvz+cosf(az)*4.0f };
        if(!ini){ memcpy(se,de,12); memcpy(sc,dc,12); ini=1; }
        float k=fminf(1,(float)dt*3.0f);
        for(int i2=0;i2<3;i2++){ se[i2]+=(de[i2]-se[i2])*k; sc[i2]+=(dc[i2]-sc[i2])*k; }
        memcpy(eye,se,12); memcpy(ctr,sc,12);
        if(g_boltT>0){ float sh8=g_boltT*0.55f;
            eye[0]+=(frnd()-0.5f)*sh8; eye[1]+=(frnd()-0.5f)*sh8*0.6f; eye[2]+=(frnd()-0.5f)*sh8; }
        for(int i2=0;i2<3;i2++){ g_lastE[i2]=eye[i2]; g_lastC[i2]=ctr[i2]; }
        CAMD.t=0; return; }
    typedef struct { int pack; int clip; unsigned phase; float x,z,rotY; } SlotL;
    #define SLOTAT(i) ((SlotL*)((char*)slots_+(size_t)(i)*(g_slotStride?g_slotStride:(int)sizeof(SlotL))))
    SlotL*slots=(SlotL*)slots_; (void)slots;
    CAMD.t+=dt;
    float bar=60.f/g_bpm*4.f; if(bar<1.2f)bar=1.2f;
    if(CAMD.t>=CAMD.dur){
        CAMD.t=0; CAMD.seed=frnd()*6.28f;
        for(int i2=0;i2<3;i2++){ g_flyE[i2]=g_lastE[i2]; g_flyC[i2]=g_lastC[i2]; }   /* EVERY cut flies from here (no hard cuts) */
        /* FULL-ROSTER PARADE (07-23, at every transition to a new npc): every cut is a
         * SHOWCASE of the next roster member. Coprime stride hits each slot exactly once per
         * lap; only measured bans (auto-quarantine) and undrawable packs skip forward. */
        { static int paradeIdx=-1, paradeStride=0;
          if(NS>0){
              if(paradeStride==0){ int k=(int)(NS*0.618f); if(k<1)k=1;
                  for(;;k++){ int a=k%NS?k%NS:1, b=NS; while(b){ int t2=a%b; a=b; b=t2; }
                      if(a==1){ paradeStride=k%NS?k%NS:1; break; } } }
              int cand=-1;
              for(int tries=0;tries<NS&&cand<0;tries++){
                  paradeIdx=(paradeIdx+paradeStride)%NS;
                  if(!g_slotBan[paradeIdx]&&g_slotDrawable[paradeIdx]&&!g_slotNoShow[paradeIdx]) cand=paradeIdx; }
              if(cand<0) cand=(paradeIdx=(paradeIdx+1)%NS);
              CAMD.tgt=cand; }
          CAMD.shot=9; CAMD.dur=bar*(3.5f+frnd()*1.5f); }
    }
    /* ⛔ M0 (11-01): THE PARADE'S CUT RUNS ON ITS BAR CLOCK IN EVERY MODE -- it set shot 9 over the lens the zombie cab
     * pinned at the top, so every few bars a frame (and the fly back from it) showed a spotlight on a stranger's lawn
     * mid-game: two frames of a twelve-frame burst. The cab's pin wins here too. (The RTS, the platformer and the action
     * modes pin the same way at the top and are open to the same cut -- unverified.) */
    if(GAME_ON) CAMD.shot=22;
    float p=(float)(CAMD.t/(CAMD.dur>0.001?CAMD.dur:0.001));
    float rx=RVX, rz=RVZ;
    /* SKATE MODE: the parade/spotlight director is a dancefloor idea -- it orbits wide and,
     * inside a roofed park, flies the camera straight through the walls. Lock to the reva
     * third-person shot so the camera rides with her. */
    if(g_skate&&!trk10) CAMD.shot=6;
    switch(CAMD.shot){
    case 23: {   /* A PLACED LENS BEATS EVERYTHING. A set is framed by the shot, not by
                  * an orbit of the hero -- and once a scene has scenery, the lab's own
                  * lens is only one of the answers. */
        if(g_camOn){
            /* \u2b50 THE MOVE, STEPPED ON THE FRAME dt. g_chDt is what the main loop publishes
             * and what REVA_FIXEDSTEP replaces, so the same take twice gives the same camera
             * and a 30fps capture is not a different move from the 60fps window. Wall time
             * would have made a shot's length depend on how fast frames hit the disk. */
            /* ⭐ A MOVE WAITS FOR FRAME ONE OF THE TAKE. The identical gate the SET
             * clock uses (actorsTick): when a capture is armed and this is not a scene,
             * hold until g_capBegun. b-roll captures start at frame 300 -- ten seconds of
             * virtual time at a 30fps fixed step -- so a move armed at the boot-time ctl
             * poll would be OVER before the first frame was written, and the take would be
             * a held shot with a mysteriously wrong lens. That is exactly what three of my
             * own tests measured as "0.0% changed" before I noticed the window.
             * Live (no capture), it runs immediately. */
            { static int cgate=-1;
              if(cgate<0) cgate=(getenv("REVA_SHOT")&&!getenv("REVA_SCENEFPS"))?1:0;
              if(cgate&&!g_capBegun) g_camMoveT=0.f; }
            if(g_camMoveOn&&g_camMoveDur>1e-4f){
                g_camMoveT+=(float)((g_stepFps>0.f)?g_camDt:g_chDt);   /* the lens keeps its own clock under stop motion */
                float t=g_camMoveT/g_camMoveDur; if(t>1.f){ t=1.f; g_camMoveOn=0; }
                float e=g_camEase?(t*t*(3.f-2.f*t)):t;      /* smoothstep: eases in AND out */
                for(int k=0;k<3;k++){
                    g_camPos[k]=g_camA[k]+(g_camB[k]-g_camA[k])*e;
                    g_camAim[k]=g_camAimA[k]+(g_camAimB[k]-g_camAimA[k])*e; }
                if(g_camFovA>0.f&&g_camFovB>0.f) g_camFov=g_camFovA+(g_camFovB-g_camFovA)*e; }
            camTrackTick((float)((g_stepFps>0.f)?g_camDt:g_chDt));   /* ⭐ a tracking lens rewrites the placed one */
            memcpy(eye,g_camPos,12); memcpy(ctr,g_camAim,12);
            camShakeApply((float)((g_stepFps>0.f)?g_camDt:g_chDt),eye,ctr);   /* ⭐ on top of ANY placed lens */
            memcpy(g_lastE,eye,12); memcpy(g_lastC,ctr,12); return; }
        /* THE ANIM LAB: locked side-on and tracking, the angle a gait is judged from.
                  * REVA_ANIMCAM=front|3q|top swings it; the lab is for LOOKING, so it is fixed
                  * and repeatable rather than pretty. */
        static int lc=-2; if(lc==-2){ const char*e=getenv("REVA_ANIMCAM");
            lc=(!e)?0:(!strcmp(e,"front")?1:(!strcmp(e,"3q")?2:(!strcmp(e,"top")?3:0)));
            g_labCam=lc; }
        /* THE CONVERSATION CAMERA. Coverage, not a lab lens: over the listener's
         * shoulder onto whoever is speaking, cutting on the line (never lerping --
         * a cut is what a conversation is edited with), with a two-shot every fifth
         * line so the room and the body language exist. Both head positions are
         * MEASURED and published by the draw, so the framing follows the actual
         * faces of two differently-sized bodies. */
        /* ⛔ AND IF THE HEAD MARKS ARE NOT THERE, FRAME THE MARK. The solver below is
         * gated on both faces having been MEASURED, and when either is missing the whole
         * block is skipped -- so the lens simply stays wherever the dance floor's own director
         * last left it, which is an empty horizon with a shadow at the bottom of it. Five
         * of fourteen takes in the first full shoot came back like that, and nothing
         * logged a word: porch and boarding entirely, denial and it_starts for their
         * opening third. A scene that knows where its STAGE is can always frame the
         * stage, and a wide of the right patch of street is a shot. This says so once. */
        if(g_scnT>=0.f&&g_scnN>0&&g_stageOn&&!(g_heroHeadOK&&g_partHeadOK)){
            static int said9=0;
            if(!said9){ said9=1;
                printf("[scene] NO HEAD MARKS (hero %d partner %d) -- framing the stage instead",
                       g_heroHeadOK,g_partHeadOK); printf("%c",10); fflush(stdout); }
            float ya=g_stage[2]*0.0174532925f;
            float fx9=sinf(ya), fz9=cosf(ya);          /* the way the pair faces */
            eye[0]=g_stage[0]-fx9*4.60f+fz9*2.60f;
            eye[1]=2.35f+g_scnHgt;
            eye[2]=g_stage[1]-fz9*4.60f-fx9*2.60f;
            ctr[0]=g_stage[0]; ctr[1]=1.55f; ctr[2]=g_stage[1];
        }
        { static int n9=0; if(getenv("REVA_CAMDBG")&&(n9++%60)==0)
            printf("[cam] scnT=%.2f scnN=%d stageOn=%d headOK=%d/%d manual=%d eye=%.1f,%.1f,%.1f ctr=%.1f,%.1f,%.1f\n",
                   g_scnT,g_scnN,g_stageOn,g_heroHeadOK,g_partHeadOK,g_manual,
                   eye[0],eye[1],eye[2],ctr[0],ctr[1],ctr[2]); }
        if(g_scnT>=0.f&&g_scnN>0&&castHeadOK(0)&&castHeadOK(1)){
            static int held9=0; int li9=scnActive(); if(li9>=0) held9=li9;
            ScnLine*L9=&g_scnL[held9];
            /* ⭐ SPEAKER AND LISTENER BY CAST INDEX. The listener is whoever spoke last
             * -- with two people that was "the other one" and needed no thought; with
             * three it is a real question and this is the answer that needs no authoring.
             * It is also what an over-the-shoulder is for: the person being ANSWERED goes
             * in the foreground. */
            int spk9=L9->spk; if(spk9<0||spk9>=SCN_MAXCAST) spk9=0;
            /* ⭐ AN OFF-SCREEN VOICE HAS NO HEAD TO FRAME. A line spoken by a cast slot with no
             * body (the director calling cut) would aim the lens at a head position that was
             * never published -- world origin. The shot stays on whoever spoke last. */
            if(!castHeadOK(spk9)) spk9=(g_scnPrevSpk>=0&&g_scnPrevSpk<SCN_MAXCAST&&castHeadOK(g_scnPrevSpk))?g_scnPrevSpk:0;
            int lis9=g_scnPrevSpk; if(lis9==spk9||lis9<0||lis9>=SCN_MAXCAST) lis9=(spk9==0)?1:0;
            /* ⭐ THE REACTION SHOT. `focus=` overrides who the LENS is on -- the voice, the
             * mouth and the subtitle stay with the real speaker. Without this the camera can
             * only ever watch whoever is talking, which in a film of two people listening to
             * each other means half the performance is off-screen. */
            { static int f9=0; if(getenv("REVA_FOCUSDBG")&&(f9++%45)==0)
                printf("[focus] line %d spk %d focus %d headOK(focus) %d%c",
                       held9,spk9,L9->focus,L9->focus>=0?castHeadOK(L9->focus):-1,10),fflush(stdout); }
            if(L9->focus>=0&&L9->focus<SCN_MAXCAST&&castHeadOK(L9->focus)&&L9->focus!=spk9){
                int other9=spk9; spk9=L9->focus; lis9=other9; }
            if(!castHeadOK(lis9)) lis9=(spk9==0)?1:0;
            const float*SH=castHead(spk9);   /* speaker */
            const float*LH=castHead(lis9);   /* listener */
            float dx9=SH[0]-LH[0], dz9=SH[2]-LH[2];
            float dl9=sqrtf(dx9*dx9+dz9*dz9); if(dl9<1e-3f) dl9=1.f;
            dx9/=dl9; dz9/=dl9;
            float sx9=-dz9, sz9=dx9;
            /* ⛔ DO NOT CROSS THE LINE. The side used to alternate with the line INDEX,
             * which flips the camera across the eyeline whenever two consecutive lines
             * share a speaker -- and half this scene is one woman talking twice. The
             * side has to follow the SPEAKER: because dx9/sx9 already flip when the
             * speaker and listener swap, one sign per speaker puts every shot and every
             * reverse on the same side of the axis, which is the whole grammar. */
            /* ⛔ THE EYELINE MUST SURVIVE A THIRD PERSON. `spk==0 ? +1 : -1` is only
             * meaningful for one pair. Keyed on the ORDER of the pair instead, every pair
             * keeps its own consistent axis and no cut crosses the line -- which is the
             * grammar the two-body version was written to protect. */
            float sd9=(spk9<lis9)?1.f:-1.f;
            /* ⭐ CROSS THE LINE ON PURPOSE. The camera always sits on ONE
             * side of the pair's axis -- that is the whole eyeline grammar above, and it is
             * correct. `reverse=1` mirrors it to the other side: who is on screen-left swaps and
             * a different background comes up behind them. One sign carries the position, the
             * looking room and (via g_scnRev in the light rig) the key and the rim. */
            if(scnRevNow()) sd9=-sd9;
            int shot9=(g_shotForce>=0)?g_shotForce:L9->shot;
            /* FRAME ON THE EYES, NOT THE HEAD BONE. The head mark sits inside the
             * skull; aiming at it centres a face that should be sitting on the upper
             * third, and on two bodies 0.46 apart in height it centres the wrong one.
             * The eye marks are already published for the look-at, so use them, and
             * fall back to the head only if a body has no face rig. */
            int fok9=(castFaceOK(spk9)&&castFaceOK(lis9));
            const float*SE=fok9?castEye(spk9):SH;
            const float*LE=fok9?castEye(lis9):LH;
            /* SIZE-MATCHED COVERAGE. Framing is only "the same shot" if the head fills
             * the same part of the frame, and these two heads are not the same size --
             * so the distance is scaled by the speaker's own measured interocular.
             * Clamped: a body with no face rig, or a bad mark, must not fly the lens
             * into a skull or out to the horizon. */
            float sio9=castIOD(spk9);
            float fit9=(sio9>1e-4f)?(sio9/IOD_NOM):1.f;
            if(fit9<0.55f) fit9=0.55f;
            if(fit9>1.60f) fit9=1.60f;
            g_scnWant=-1e9f;
            if(shot9==SHOT_TWO||shot9==SHOT_WIDE||shot9==SHOT_MEDIUM){
                float mx9=(SH[0]+LH[0])*0.5f, mz9=(SH[2]+LH[2])*0.5f;
                float my9=(SE[1]+LE[1])*0.5f;
                float R9;
                if(shot9==SHOT_MEDIUM){
                    /* ⭐ MEDIUM: A HIPS-UP TWO-SHOT, SOLVED FROM BOTH EDGES.
                     * `two` and `wide` pick a distance off the lens and then CHECK the top of
                     * frame. A medium is defined by both edges at once -- the bottom sits at the
                     * lower pair of hips, which is what puts the hands and whatever is in them
                     * large in frame, and the top clears the higher head or the higher raised
                     * hand. So the distance is solved from the height it has to hold rather than
                     * chosen and corrected.
                     * The hips are MEASURED, not guessed: ATT_HIP is the pelvis bone's own world
                     * matrix, already published every frame for the held props. */
                    float bot9=1e9f; int nb9=0;
                    int ms9[2]={spk9,lis9};
                    for(int b9=0;b9<2;b9++){ int s9=ms9[b9]; if(s9<0||s9>=SCN_MAXCAST) continue;
                        if(g_castAttOK[s9][ATT_HIP]){ float hy9=g_castAttM[s9][ATT_HIP][13];
                            if(hy9<bot9) bot9=hy9;
                            nb9++; } }
                    if(!nb9){ bot9=((SE[1]<LE[1])?SE[1]:LE[1])-1.15f;
                        static int saidM9=-99; if(saidM9!=held9){ saidM9=held9;
                            printf("[lens] line %d medium: neither body published a hip -- the bottom edge is a guess 1.15 below the lower eye\n",held9); fflush(stdout); } }
                    bot9-=0.75f;                 /* MID-THIGH, a cowboy (a first take measured hip-0.20 as the same frame as `two`: the two is already hips-up; the hands hang ~0.45 below the pelvis bone and want room under them) */
                    float top9=((SE[1]>LE[1])?SE[1]:LE[1])+SCN_HEADTOP;
                    for(int b9=0;b9<2;b9++){ int s9=ms9[b9]; if(s9<0||s9>=SCN_MAXCAST) continue;
                        if(g_castAttOK[s9][ATT_HAND]&&g_castAttM[s9][ATT_HAND][13]+SCN_HANDTOP>top9)   top9=g_castAttM[s9][ATT_HAND][13]+SCN_HANDTOP;
                        if(g_castAttOK[s9][ATT_LHAND]&&g_castAttM[s9][ATT_LHAND][13]+SCN_HANDTOP>top9) top9=g_castAttM[s9][ATT_LHAND][13]+SCN_HANDTOP; }
                    float need9=(top9-bot9)*1.02f;   /* 2% of air, so the guarantee below never has to move it */
                    if(need9<0.90f) need9=0.90f;
                    float hfm9=SHOT_FOV[SHOT_MEDIUM]*0.5f*0.0174532925f;
                    R9=need9*0.5f/tanf(hfm9);
                    /* AND BOTH OF THEM HAVE TO FIT ACROSS. The pair's axis is square to the view
                     * direction, so their separation is frame WIDTH: half the gap plus a shoulder
                     * and a margin each side, against the frame's own half width. */
                    { float halfw9=dl9*0.5f+0.72f;
                      float ar9=(g_aspect>0.8f)?g_aspect:1.7778f;
                      float Dl9=halfw9/(tanf(hfm9)*ar9);
                      if(Dl9>R9) R9=Dl9; }
                    float cy9=(top9+bot9)*0.5f;
                    eye[0]=mx9+sx9*R9*sd9; eye[1]=cy9+g_scnHgt; eye[2]=mz9+sz9*R9*sd9;
                    ctr[0]=mx9; ctr[1]=cy9; ctr[2]=mz9;
                } else {
                /* TWO at 30 deg from 3.20 fills ~70% of the width with the pair instead
                 * of stranding them in the middle third; WIDE stays open at 40 deg
                 * because the space IS the point once she has turned to go. */
                R9=((shot9==SHOT_WIDE)?4.60f:3.20f)*(0.5f+0.5f*fit9);   /* a two-shot holds both, so only half-weight the speaker's head */
                float hi9=(shot9==SHOT_WIDE)?0.45f:0.16f;
                float lo9=(shot9==SHOT_WIDE)?1.30f:0.45f;
                /* ⭐ HEAD ROOM IS A MEASUREMENT, NOT A CONSTANT (Mascelli, Composition:
                 * head room "varies with combinations of players"). The drop below the
                 * mid-eye height puts eyes on the upper third -- correct for two people of
                 * similar height on the same floor, and WRONG the moment they are not.
                 * ⛔ A two-person bed scene is exactly that case: the co-star is LYING ON A BED and reva
                 * is STANDING, their eyes ~1.5 units apart, so the midpoint is already low
                 * and dropping a further 1.30 aimed the lens at the bedspread and cut BOTH
                 * their heads off the top of the frame. Found by fixing the shot grammar --
                 * one rule from this book immediately exposed the next.
                 * The HIGHER head sets the top frame line, so the aim may only fall as far
                 * as that head allows. */
                { float spread9=fabsf(SE[1]-LE[1]);
                  /* ⛔ AND A WIDE MUST GET HIGHER, NOT JUST AIM LOWER. Two people at very
                   * different heights are usually that way because one is ON something --
                   * a bed, a step, the ground -- and the thing they are on sits between the
                   * lens and them. A bed scene's wide came back with the bed across the
                   * bottom third of frame. Rise with the spread and look down over it. */
                  if(shot9==SHOT_WIDE) hi9+=spread9*0.62f;
                  float room9=lo9-spread9*0.55f;
                  if(room9<0.10f) room9=0.10f;
                  lo9=room9;
                  /* ⭐ AND RE-AIMING IS NOT ENOUGH ON ITS OWN. Once the two heads are far
                   * enough apart vertically, no aim height holds both -- the shot itself
                   * has to OPEN. The wide recovered with the drop alone; the two-shot still
                   * cut reva's head off, because a "two" framed for two people standing
                   * cannot contain one standing and one in bed. Pull back by the spread. */
                  R9+=spread9*1.15f; }
                eye[0]=mx9+sx9*R9*sd9; eye[1]=my9+hi9+g_scnHgt; eye[2]=mz9+sz9*R9*sd9;
                ctr[0]=mx9; ctr[1]=my9-lo9; ctr[2]=mz9;
                }
                /* ⭐ HEAD ROOM IS A GUARANTEE, NOT AN AIM. (Mascelli: a player must never touch
                 * a frame edge -- design/cinematography.md listed head room as "nothing
                 * measures this yet".) Everything above AIMS the lens; nothing checked that
                 * the frame it made CONTAINS a head. ⛔ On a sunlit street a standing reva2
                 * beside the shorter co-star lost the top of her head in every two-shot and
                 * both women lost theirs in every wide (2026-09-10): the drop
                 * below mid-eye is sized for two people of a height, and the spread terms for
                 * someone lying in a bed.
                 * So measure the frame: its TOP LINE at the pair's distance, from the eye
                 * height, the pitch and the lens's own half-fov, must clear the higher EYE
                 * plus a head-top allowance. If it does not, a TWO re-aims (a two-shot is about
                 * faces; the solve is closed-form) and a WIDE pulls back (a wide is about the
                 * space round them, so it keeps its feet). Says so once per cut. */
                { float hf9=SHOT_FOV[shot9&7]*0.5f*0.0174532925f;
                  float want9=((SE[1]>LE[1])?SE[1]:LE[1])+SCN_HEADTOP;
                  /* ⭐ AND WHATEVER THEY RAISE ABOVE THEIR HEADS. UPDATE ends on reva
                   * throwing both arms over her head, and the wide kept her EYES in frame and cut her
                   * hands off at the forearm for the whole line: head room measured the head. Both
                   * hands of both bodies are already published (g_castAttM, for held props), so the
                   * top line clears the higher raised hand as well. */
                  float hand9=-1e9f; int hs9[2]={spk9,lis9};
                  for(int b9=0;b9<2;b9++){ int s9=hs9[b9]; if(s9<0||s9>=SCN_MAXCAST) continue;
                      if(g_castAttOK[s9][ATT_HAND]&&g_castAttM[s9][ATT_HAND][13]>hand9) hand9=g_castAttM[s9][ATT_HAND][13];
                      if(g_castAttOK[s9][ATT_LHAND]&&g_castAttM[s9][ATT_LHAND][13]>hand9) hand9=g_castAttM[s9][ATT_LHAND][13]; }
                  int byHand9=(hand9+SCN_HANDTOP>want9);
                  if(byHand9) want9=hand9+SCN_HANDTOP;
                  float r0=R9, c0=ctr[1]; int moved9=0;
                  for(int it9=0;it9<24;it9++){
                      float d9=(R9>0.5f)?R9:0.5f;
                      float pit9=atan2f(eye[1]-ctr[1],d9);          /* down is positive */
                      float line9=eye[1]-d9*tanf(pit9-hf9);         /* top frame line, at them */
                      if(line9>=want9) break;
                      moved9=1;
                      if(shot9==SHOT_TWO){
                          float th9=hf9+atan2f(eye[1]-want9,d9);
                          ctr[1]=eye[1]-d9*tanf(th9); break; }
                      R9*=1.06f; eye[0]=mx9+sx9*R9*sd9; eye[2]=mz9+sz9*R9*sd9; }
                  g_scnWant=want9;
                  if(moved9){ static int said9=-99, saidH9=-99; if(byHand9?(saidH9!=held9):(said9!=held9)){ if(byHand9) saidH9=held9; else said9=held9;
                      printf("[headroom] line %d %s: top %s %.2f needs the frame to %.2f -- %s %.2f -> %.2f\n",
                             held9,SHOT_NAME[shot9&7],byHand9?"raised hand":"eye",byHand9?hand9:want9-SCN_HEADTOP,want9,
                             shot9==SHOT_TWO?"aim":"distance",shot9==SHOT_TWO?c0:r0,
                             shot9==SHOT_TWO?ctr[1]:R9); fflush(stdout); } } }
            } else if(shot9==SHOT_SOLO){
                /* ⭐ SOLO IS THE LISTENER'S POINT OF VIEW.
                 *
                 * ⛔ IT WAS NOT A SINGLE AT ALL. The comment two branches down admits it: at
                 * 4.10 out and 54 deg round, with two people standing a metre apart, the
                 * "clean single" came back as a two-shot with the listener's profile down half
                 * the frame -- the same picture `two` gives, on a different lens. Every attempt
                 * to push her out by moving sideways failed for the reason recorded there: the
                 * angle that must clear the half-frustum is measured from the VIEW AXIS, and the
                 * view axis is already swung toward her.
                 *
                 * So stop trying to walk round her and stand WHERE SHE IS. The lens goes to her
                 * own eye mark -- a hand behind it and a little to one side -- and looks at the
                 * speaker's eyes. She is behind the lens, which is the only place a body is
                 * reliably out of frame, and the speaker is nearly into it.
                 *
                 * ⛔ THE NEAR PLANE IS WHAT REMOVES HER, and it only works from close in. A
                 * scene renders on a 0.30 near plane; her own face runs from about 0.25 behind
                 * her eye mark to about 0.18 in front of it, so a lens 0.10 behind the mark has
                 * every part of her inside 0.30 and the clip takes her whole head and chest.
                 * Stand further back "to be safe" and the opposite happens: her skull leaves the
                 * clip and fills the frame again. Nothing is back-face culled in this engine, so
                 * being inside her head is not an option either -- 0.10 is the measurement, not
                 * a taste. A hand she reaches toward the speaker DOES cross 0.30, and appears,
                 * which is what a point of view should do. */
                const float NEARP9=0.30f;     /* == the scene projection's near plane */
                float back9=0.10f, off9=0.17f;
                if(dl9+back9<=NEARP9+0.25f){
                    /* ⛔ AND SAY WHY WHEN IT CANNOT BE DONE. Standing closer than this the
                     * SPEAKER is inside the near plane too and there is no single to take. */
                    static int saidS9=-99; if(saidS9!=held9){ saidS9=held9;
                        printf("[lens] line %d solo: the pair's heads are %.2f apart -- from the listener's eyes the speaker is inside the %.2f near plane, so no clean single exists; using ots\n",
                               held9,dl9,NEARP9); fflush(stdout); }
                    float Do9=2.95f*fit9, tho9=0.873f;
                    float bko9=Do9*cosf(tho9), ofo9=Do9*sinf(tho9);
                    eye[0]=SE[0]-dx9*bko9+sx9*ofo9*sd9;
                    eye[1]=SE[1]+0.10f+g_scnHgt;
                    eye[2]=SE[2]-dz9*bko9+sz9*ofo9*sd9;
                    ctr[0]=SE[0]-sx9*0.22f*sd9; ctr[1]=SE[1]-0.21f; ctr[2]=SE[2]-sz9*0.22f*sd9;
                } else {
                    eye[0]=LE[0]-dx9*back9+sx9*off9*sd9;
                    eye[1]=LE[1]+0.05f+g_scnHgt;
                    eye[2]=LE[2]-dz9*back9+sz9*off9*sd9;
                    /* HEAD ROOM IS STILL A GUARANTEE, and a raised hand still grows the frame --
                     * the tripod reads g_scnWant to know when to let go. */
                    float want9=SE[1]+SCN_HEADTOP;
                    if(spk9>=0&&spk9<SCN_MAXCAST){
                        if(g_castAttOK[spk9][ATT_HAND]&&g_castAttM[spk9][ATT_HAND][13]+SCN_HANDTOP>want9)   want9=g_castAttM[spk9][ATT_HAND][13]+SCN_HANDTOP;
                        if(g_castAttOK[spk9][ATT_LHAND]&&g_castAttM[spk9][ATT_LHAND][13]+SCN_HANDTOP>want9) want9=g_castAttM[spk9][ATT_LHAND][13]+SCN_HANDTOP; }
                    g_scnWant=want9;
                    ctr[0]=SE[0]; ctr[1]=SE[1]-0.10f; ctr[2]=SE[2];   /* eyes onto the upper third */
                    { float hf9=SHOT_FOV[SHOT_SOLO]*0.5f*0.0174532925f;
                      float ex9=eye[0]-SE[0], ez9=eye[2]-SE[2];
                      float d9=sqrtf(ex9*ex9+ez9*ez9); if(d9<0.35f) d9=0.35f;
                      float pit9=atan2f(eye[1]-ctr[1],d9);
                      float line9=eye[1]-d9*tanf(pit9-hf9);
                      if(line9<want9){
                          float th9=hf9+atan2f(eye[1]-want9,d9);
                          float nc9=eye[1]-d9*tanf(th9);
                          if(nc9>SE[1]-0.02f){
                              /* The lens cannot be moved back without the listener returning to
                               * frame, and it cannot be widened without becoming a different
                               * shot. Say which number ran out. */
                              static int saidF9=-99; if(saidF9!=held9){ saidF9=held9;
                                  printf("[lens] line %d solo: they stand %.2f apart, so the frame is only %.2f tall on the %.0f deg lens and the speaker's head (wants %.2f above her eyes) will not fit -- aiming at her eyeline; open the gap or use tight\n",
                                         held9,d9,2.f*d9*tanf(hf9),SHOT_FOV[SHOT_SOLO],want9-SE[1]); fflush(stdout); }
                              nc9=SE[1]-0.02f; }
                          ctr[1]=nc9; } }
                    { static int saidP9=-99; if(saidP9!=held9){ saidP9=held9;
                        printf("[lens] line %d solo: listener behind the lens\n",held9); fflush(stdout); } }
                }
            } else if(shot9==SHOT_TIGHT){
                /* CLEAN SINGLES -- no foreground body at all, which is what separates
                 * these two from the over-the-shoulder.
                 *
                 * ⛔ THE UNITS ARE THE ENGINE'S, NOT METRES. Sized first the way you would
                 * size a close-up on a human-scale set (0.52 back, 0.46 across), and the
                 * engine is about 1.75x metric -- heads sit at y 2.62 and 3.08 -- so the
                 * lens landed 0.70 from a head: inside the hair, and inside the near
                 * plane. Whole shots came back as an empty horizon and nothing logged a
                 * word about it.
                 *
                 * ⛔ AND YOU CANNOT SHOOT ONE OF TWO PEOPLE STANDING 1.24 APART WITHOUT
                 * THE OTHER IN SHOT. An attempt to exclude the listener by anchoring to
                 * her instead put her BACK across half the frame: the angle that has to
                 * clear the 34.3 deg half-frustum is measured from the VIEW AXIS, and
                 * the view axis is already swung ~25 deg toward her. Measured properly
                 * there is no lateral offset that both keeps the speaker's face frontal
                 * and pushes the listener out.
                 * So TIGHT is an over-the-shoulder CLOSE-UP and the foreground shoulder
                 * is a feature -- the thing that was actually wrong was that it was the
                 * brightest object in frame, and that is a LIGHTING fault, fixed by
                 * keying the speaker rather than the midpoint. SOLO is the one that
                 * really excludes, and it only has to work at the distance it is used:
                 * 3.00 back and 1.30 across clears her by 42 deg at the 2.2 gap the
                 * scene has walked out to by then. */
                /* Polar, not cartesian: pick the DISTANCE from the head fraction the
                 * lens gives, then swing it round the subject by an angle. TIGHT 2.75
                 * out on the 22 (head ~42% of frame height); SOLO 5.20 on the 30, a
                 * medium single with room to be alone in. 45 deg off the eyeline is a
                 * three-quarter -- both eyes, one cheek -- and it is also what pushes
                 * the listener past the frame edge on the long glass: she sits 70 deg
                 * off the axis against a 19 deg half-frustum, so all that survives of
                 * her is the strip of shoulder an over-the-shoulder is supposed to have. */
                /* (SOLO's old numbers -- 4.10 out, 54 deg round -- lived here too; the branch
                 * above replaced them with a point of view, so these are TIGHT's alone now.) */
                float D9 =2.75f*fit9;
                float th9=0.838f;                              /* 48 deg */
                float bk9=D9*cosf(th9), of9=D9*sinf(th9);
                float up9=0.05f;
                float dn9=0.13f;                               /* an eighth of a frame: eyes onto the upper third */
                /* LOOKING ROOM. Aiming a little PAST her, toward the listener, pushes
                 * her off centre AWAY from the foreground -- which is the whole layout
                 * of an over-the-shoulder: shoulder hard against one edge, face on the
                 * opposite third, her eyeline crossing the frame between them. It was
                 * off for TIGHT while the shoulder was still huge; on the long lens it
                 * is a strip, so the composition can have it back. */
                float lr9=0.16f;
                eye[0]=SE[0]-dx9*bk9+sx9*of9*sd9;
                eye[1]=SE[1]+up9+g_scnHgt;
                eye[2]=SE[2]-dz9*bk9+sz9*of9*sd9;
                ctr[0]=SE[0]-sx9*lr9*sd9; ctr[1]=SE[1]-dn9; ctr[2]=SE[2]-sz9*lr9*sd9;
            } else {
                /* THE LENS LIVED INSIDE HER HAIR. At 0.90 behind the listener the
                 * near head is 0.28u of hair at arm's length: it filled the frame and
                 * hid the speaker completely on every one of the co-star's listening lines.
                 * An over-the-shoulder needs the near shoulder as a FOREGROUND EDGE,
                 * which means standing well back of it and further off the axis. */
                /* tighter: faces have to be big enough that a 2cm jaw and a
                 * blink actually reach the screen (the mouths were not moving) */
                /* Re-derived on the 30: 3.25 out, 42 deg round. The old rule stood
                 * 1.30 behind the LISTENER, which made the framing depend on how far
                 * apart the scene had them standing -- the same shot read as a medium
                 * at pgap 0.88 and a wide at 2.20. Anchored to the speaker it holds. */
                float D9=2.95f*fit9, th9=0.873f;               /* 50 deg */
                float bk9=D9*cosf(th9), of9=D9*sinf(th9);
                eye[0]=SE[0]-dx9*bk9+sx9*of9*sd9;
                eye[1]=SE[1]+0.10f+g_scnHgt;
                eye[2]=SE[2]-dz9*bk9+sz9*of9*sd9;
                ctr[0]=SE[0]-sx9*0.22f*sd9; ctr[1]=SE[1]-0.21f; ctr[2]=SE[2]-sz9*0.22f*sd9; }
            /* REVA_SCNCAM=1: what the conversation lens actually did, per cut. A shot
             * that lands on the back of someone's head is not diagnosable from the
             * picture -- the picture cannot tell you which of SH, LH, d or the sign
             * was wrong. */
            /* ⛔ SMOOTH THE LENS, AND SNAP IT ON THE CUT. Everything above is built
             * from the speaker's eye mark, which rides a head that the head layer moves
             * every frame -- so the whole frame followed her speech twitch (the whole scene shook when they talked).
             * A 0.20s time constant kills the
             * jitter and still tracks a real move: she has three seconds to turn round,
             * and the ease on her mark takes 0.42s of that.
             * The snap matters as much as the smoothing -- a lens that LERPS between two
             * setups is a whip pan, not a cut. */
            { static float smE[3],smC[3]; static int smL=-999; static float shotT9=0.f, wantLock9=-1e9f;
              float stp=(float)dt; if(stp<=0.f||stp>0.25f) stp=1.f/60.f;
              /* ⭐ UNDER STOP MOTION THE LENS IS ON A TRIPOD. A stop-motion camera
               * is locked off between cuts; this one kept easing toward the speaker's eye mark, and
               * every exposure moved that mark (the head layer, the joint boil, a head turning away)
               * -- so the lens moved on EVERY frame and the twelve-a-second image underneath read as
               * a moving one. Measured: background band 1.2-6.9% per frame against the first shoot's 0.5-0.8%.
               * So: settle for 0.45s after the cut, then HOLD until the next cut -- unless somebody
               * is actually travelling (a walk-in), when it follows as before. Off stop motion,
               * nothing changes. */
              if(smL!=held9){ smL=held9; shotT9=0.f; memcpy(smE,eye,12); memcpy(smC,ctr,12); wantLock9=g_scnWant; }
              else { shotT9+=stp;
                     /* ⭐ THE TRIPOD LETS GO FOR A RAISED ARM. It locks 0.45s after the
                      * cut, and revas arms reach the top of frame after that -- the head room above could
                      * re-aim the TARGET all it liked while the drawn lens held the old frame. When the
                      * frame a shot needs has GROWN since the lock, the lens eases out to it and locks
                      * again when it arrives. It never tightens back in on its own. */
                     int grow9=(g_scnWant>wantLock9+0.08f);
                     int tripod9=(g_stepFps>0.f)&&(shotT9>0.45f)&&(g_partSpd<0.15f)&&(g_heroStageSpd<0.15f)&&!grow9;
                     if(!tripod9){ float k=1.f-expf(-stp/0.20f);
                         for(int q9=0;q9<3;q9++){ smE[q9]+=(eye[q9]-smE[q9])*k;
                                                  smC[q9]+=(ctr[q9]-smC[q9])*k; } }
                     if(shotT9<=0.45f) wantLock9=g_scnWant;
                     else if(grow9){
                         static int saidT9=-99; if(saidT9!=held9){ saidT9=held9;
                             printf("[tripod] line %d: the frame must grow to %.2f (locked at %.2f) -- easing out\n",held9,g_scnWant,wantLock9); fflush(stdout); }
                         float e9=fabsf(smE[0]-eye[0])+fabsf(smE[1]-eye[1])+fabsf(smE[2]-eye[2])+fabsf(smC[1]-ctr[1]);
                         if(e9<0.03f) wantLock9=g_scnWant; } }
              if(getenv("REVA_SHAKEDBG")){ static float pE[3]={0,0,0}; static int pn=0;
                  float rw=0.f,sm=0.f;
                  for(int q9=0;q9<3;q9++){ rw+=(eye[q9]-pE[q9])*(eye[q9]-pE[q9]);
                                           sm+=(smE[q9]-pE[q9])*(smE[q9]-pE[q9]); }
                  if(pn++>2&&smL==held9) printf("[shake] raw %.5f  smoothed %.5f\n",sqrtf(rw),sqrtf(sm));
                  memcpy(pE,smE,12); fflush(stdout); }
              memcpy(eye,smE,12); memcpy(ctr,smC,12); }
            if(getenv("REVA_SCNCAM")){ static int lp9=-99;
                if(held9!=lp9){ lp9=held9;
                    printf("[scncam] t %.1f line %d spk %d shot %d sd %+.0f  IOD h %.3f p %.3f fit %.2f  hgt %+.2f"
                           "  eye(%.2f %.2f %.2f) ctr(%.2f %.2f %.2f)\n",
                           g_scnT,held9,L9->spk,shot9,sd9,g_heroIOD,g_partIOD,fit9,L9->hgt,
                           eye[0],eye[1],eye[2],ctr[0],ctr[1],ctr[2]); fflush(stdout); } }
            memcpy(g_lastE,eye,12); memcpy(g_lastC,ctr,12); return; }
        lc=g_labCam;   /* live: the control file can swing the lens mid-run */
        float bx=g_rvx, bz=g_rvz, h=g_rvHead;
        float ex,ez,ey=1.75f;
        /* one framing distance for every angle, so shots can be compared directly */
        float R9=6.4f;
        if(lc==1){      ex=bx+sinf(h)*R9;                ez=bz+cosf(h)*R9; }
        else if(lc==2){ ex=bx+sinf(h+2.4f)*R9;           ez=bz+cosf(h+2.4f)*R9; }
        else if(lc==3){ ex=bx+0.01f; ez=bz+0.02f; ey=7.2f; }
        else {          ex=bx+sinf(h+1.5708f)*R9;        ez=bz+cosf(h+1.5708f)*R9; }
        eye[0]=ex; eye[1]=ey; eye[2]=ez;
        ctr[0]=bx; ctr[1]=1.0f+g_rvAlt*0.8f; ctr[2]=bz;
        memcpy(g_lastE,eye,12); memcpy(g_lastC,ctr,12); return; }
    case 22: {   /* the zombie cab: tight top-down on the hero, the whole yard in frame.
         * 11-01, the zombie game's M0: the GDD's eye is TOP-DOWN 3/4 (the zombie prototype's own angle), not
         * straight down -- bodies read as people, not as cut-outs. REVA_ZAMNCAM=height,back
         * tunes it without a rebuild; the old lens is 24,8.5. Default 19,13 (~56 degrees). */
        static float zh=-1.f, zb=13.f; if(zh<0){ zh=19.f; const char*e=getenv("REVA_ZAMNCAM");
            if(e&&e[0]){ float a=0,b=0; if(sscanf(e,"%f,%f",&a,&b)==2){ zh=a; zb=b; } } }
        eye[0]=rx; eye[1]=zh; eye[2]=rz+zb;
        ctr[0]=rx; ctr[1]=0.8f; ctr[2]=rz-1.0f;
        /* ⭐ the rules' lens (the zombie prototype's arm, 11-02): from her chest, at a pitch, never turning. REVA_ZAMNCAM still wins; lens_arm 0 is the old one */
        if(g_tune.lensArm>0.f&&!getenv("REVA_ZAMNCAM")){ float pr9=g_tune.lensPitch*0.0174533f;
            eye[1]=g_tune.lensAimY+g_feet.y+g_tune.lensArm*sinf(pr9); eye[2]=rz+g_tune.lensArm*cosf(pr9);
            ctr[1]=g_tune.lensAimY+g_feet.y; ctr[2]=rz; }
        for(int i2=0;i2<3;i2++){ g_lastE[i2]=eye[i2]; g_lastC[i2]=ctr[i2]; }
        return; }
    case 4: { float a=CAMD.seed+p*1.6f, R=11.5f;   /* raised orbit, wider ring for the big floor */
        eye[0]=rx*0.4f+sinf(a)*R; eye[1]=4.6f+sinf(p*3.14f)*1.2f; eye[2]=rz*0.4f+cosf(a)*R*0.85f;
        ctr[0]=rx*0.6f; ctr[1]=1.5f; ctr[2]=rz*0.6f-0.6f; } break;
    case 5: { SlotL*s=SLOTAT(CAMD.tgt); float d=3.0f-p*0.5f;   /* hero push-in */
        eye[0]=s->x+sinf(CAMD.seed)*d; eye[1]=0.7f+p*0.6f; eye[2]=s->z+cosf(CAMD.seed)*d;
        ctr[0]=s->x; ctr[1]=1.8f; ctr[2]=s->z; } break;
    case 6: {
        /* INSPECTION CAMERA. A chase cam shows one angle, and one angle is how a bad pose
         * hides. REVA_CAMANG parks the camera at a fixed bearing around her (degrees, 0 =
         * behind), REVA_CAMDIST sets the range, REVA_CAMH the height. */
        static int camAng=-999; static float camDist=5.8f, camH=2.35f;
        if(camAng==-999){ const char*e=getenv("REVA_CAMANG"); camAng=e?atoi(e):-1;
                          const char*d=getenv("REVA_CAMDIST"); if(d) camDist=(float)atof(d);
                          const char*h2=getenv("REVA_CAMH"); if(h2) camH=(float)atof(h2); }
        if(g_skate && camAng>=-360 && camAng!=-1){
            float a2=SK.head+(float)camAng*0.0174532925f;
            eye[0]=SK.x-sinf(a2)*camDist; eye[1]=SK.y+camH; eye[2]=SK.z-cosf(a2)*camDist;
            ctr[0]=SK.x; ctr[1]=SK.y+1.35f; ctr[2]=SK.z;
        } else if(g_skate){   /* CHASE CAM: behind the deck, rising and falling with her */
            float bx=sinf(SK.head), bz=cosf(SK.head);
            static float cy=0.f; static int cyInit=0;
            float wantY=SK.y+2.35f;
            if(!cyInit){ cy=wantY; cyInit=1; }
            cy+=(wantY-cy)*0.30f;
            /* never trail the board by more than this: easing alone let a fast drop outrun the
             * camera, which reads as the camera refusing to descend */
            if(cy-wantY> 1.10f) cy=wantY+1.10f;
            if(wantY-cy> 1.10f) cy=wantY-1.10f;
            eye[0]=SK.x-bx*5.8f; eye[1]=cy;          eye[2]=SK.z-bz*5.8f;
            ctr[0]=SK.x+bx*1.4f; ctr[1]=SK.y+1.35f;  ctr[2]=SK.z+bz*1.4f;
        } else {
            eye[0]=rx+sinf(CAMD.seed)*4.6f; eye[1]=3.0f; eye[2]=rz+cosf(CAMD.seed)*4.6f;   /* reva 3rd */
            ctr[0]=rx; ctr[1]=1.9f; ctr[2]=rz; } } break;
    case 7: { float base=atan2f(0-rx,-9.f-rz), la=base+sinf((float)CAMD.t*0.55f)*0.7f;   /* reva POV sweeps her town */
        eye[0]=rx; eye[1]=3.15f; eye[2]=rz;
        ctr[0]=rx+sinf(la)*5.f; ctr[1]=1.5f; ctr[2]=rz+cosf(la)*5.f; } break;
    case 8: { float x=-10.f+p*20.f;   /* lateral dolly across the wide floor */
        eye[0]=x; eye[1]=1.6f; eye[2]=3.8f; ctr[0]=x*0.25f; ctr[1]=1.6f; ctr[2]=-4.5f; } break;
    case 1: { float a=CAMD.seed+p*0.5f, R=4.0f;   /* god's eye over the deep floor */
        eye[0]=sinf(a)*R; eye[1]=CAMD.p1; eye[2]=-9.0f+cosf(a)*R;
        ctr[0]=rx*0.35f; ctr[1]=0.4f; ctr[2]=-8.5f+rz*0.15f; } break;
    case 2: { float e=p*p*(3-2*p), d=3.6f+e*12.0f, y=1.3f+e*6.5f, a2=CAMD.seed*0.35f-0.6f;   /* crane pull-out */
        eye[0]=sinf(a2)*d*0.5f; eye[1]=y; eye[2]=d*0.85f+0.6f;
        ctr[0]=rx*0.5f; ctr[1]=1.5f-e*0.6f; ctr[2]=-1.5f-e*5.0f; } break;
    case 3: { float a2=(CAMD.seed<3.14f?-1.f:1.f)*(0.5f+0.3f*fabsf(sinf(CAMD.seed))), d=CAMD.p1+p*1.2f;   /* raised 3/4 */
        eye[0]=rx*0.4f+sinf(a2)*d; eye[1]=CAMD.p2; eye[2]=rz*0.3f+cosf(a2)*d*0.9f+2.0f;
        ctr[0]=rx*0.6f; ctr[1]=1.4f; ctr[2]=rz*0.4f-3.5f; } break;
    case 9: {
        if(g_rvTravel&&!g_rvArrived){   /* JOURNEY (camera on reva AT ALL TIMES -- it is
             * HER traveling to each npc; a full floor crossing takes what it takes):
             * over-shoulder tracking shot riding behind her, looking down her heading.
             * Post-arrival micro-chases stay in the ORBIT and the dwell clock keeps counting. */
            static float se[3]={0,3,8}, sc[3]={0,1.6f,0}; static int init=0;
            float de[3]={ g_rvx-sinf(g_rvHead)*7.0f, 2.9f+g_rvAlt*0.9f, g_rvz-cosf(g_rvHead)*7.0f };
            float dc[3]={ g_rvx+sinf(g_rvHead)*5.0f, 1.5f+g_rvAlt*0.8f, g_rvz+cosf(g_rvHead)*5.0f };
            /* INERTIA (to show speed): the chase cam swings to the OUTSIDE of every
             * carve and the look-point leads INTO it; the low-pass below turns that into lag */
            { float px2=cosf(g_rvHead), pz2=-sinf(g_rvHead);
              de[0]+=px2*(-g_rvCarve)*3.4f; de[2]+=pz2*(-g_rvCarve)*3.4f;
              dc[0]+=px2*( g_rvCarve)*1.5f; dc[2]+=pz2*( g_rvCarve)*1.5f;
              de[1]+=fabsf(g_rvCarve)*0.5f; }
            if(!init){ memcpy(se,de,12); memcpy(sc,dc,12); init=1; }
            float k=fminf(1.f,(float)dt*2.2f);   /* low-pass: her heading slews, the lens glides */
            for(int i2=0;i2<3;i2++){ se[i2]+=(de[i2]-se[i2])*k; sc[i2]+=(dc[i2]-sc[i2])*k; }
            memcpy(eye,se,12); memcpy(ctr,sc,12);
            CAMD.t=0;   /* dwell clock starts when she ARRIVES */
            g_wasTrav=1;
            for(int i2=0;i2<3;i2++){ g_lastE[i2]=eye[i2]; g_lastC[i2]=ctr[i2]; }
            return; }   /* no fly-blend during the journey -- the ride IS the shot */
        SlotL*s=SLOTAT(CAMD.tgt);
        if(g_wasTrav){ g_wasTrav=0;   /* r5: NO sweep-in at the showcase -- the orbit picks
             * up at the follow-cam's exact azimuth and the stale pre-journey fly anchor dies */
            CAMD.seed=atan2f(g_lastE[0]-s->x,g_lastE[2]-s->z);
            for(int i2=0;i2<3;i2++){ g_flyE[i2]=g_lastE[i2]; g_flyC[i2]=g_lastC[i2]; } }
        float a=CAMD.seed+p*0.9f;   /* SHOWCASE: orbit the pair */
        float galt=(CAMD.tgt>=0&&CAMD.tgt<1024)?g_slotAlt[CAMD.tgt]:0.f;   /* fliers get an aerial orbit */
        float hh=g_showH>0?g_showH:3.15f; float d=hh*2.0f+galt*0.5f;   /* zoomed out (most were cut off) */
        eye[0]=s->x+sinf(a)*d; eye[1]=hh*0.72f+galt*0.9f; eye[2]=s->z+cosf(a)*d;
        ctr[0]=s->x*0.68f+g_rvx*0.32f; ctr[1]=hh*0.5f+galt*0.75f; ctr[2]=s->z*0.68f+g_rvz*0.32f; } break;   /* (fly-in is global now) */
    default: { float a2=sinf(CAMD.seed)*0.55f, R=CAMD.p1+p*2.0f;   /* parametric wide master */
        eye[0]=sinf(a2)*R; eye[1]=CAMD.p2+p*0.4f; eye[2]=cosf(a2)*R*0.9f+0.4f;
        ctr[0]=0; ctr[1]=1.35f; ctr[2]=-5.5f; } break;
    }
    /* The global fly-in blends between CUTS. Skate mode has no cuts -- it pins one shot --
     * but the director underneath still keeps ending and restarting its shot, so this kept
     * re-triggering and dragging the camera back toward stale eye positions. That is why it
     * would not leave a certain height. There is nothing to blend from here. */
    if(!g_skate)
    { float fp=(float)(CAMD.t/(CAMD.dur>0.001?CAMD.dur:1.0));   /* GLOBAL FLY: blends from the previous camera; the arc scales with the DISTANCE so a continuous handoff doesn't bob */
      if(fp<0.22f){ float k=fp/0.22f; k=k*k*(3-2*k);
          float fd=hypotf(g_flyE[0]-eye[0],g_flyE[2]-eye[2]);
          float arc=sinf(k*3.14159f)*fminf(3.5f,fd*0.18f);
          for(int i2=0;i2<3;i2++){ eye[i2]=g_flyE[i2]*(1-k)+eye[i2]*k; ctr[i2]=g_flyC[i2]*(1-k)+ctr[i2]*k; }
          eye[1]+=arc; } }
    /* ⭐ THE MARK EDITOR STAGES ITSELF. In the lab she walks and the director wanders, so
     * without this a person would be clicking a moving figure through a moving lens. Locked to
     * her head, at a face-filling distance, until it's moved by hand. */
    /* ⭐ WHERE HER HEAD IS, printed once, so `driver.py marks` can point a lens at it.
     * ⛔ This is also all that survives of an in-engine camera lock that did not work: there
     * are SIX places that write g_lastE and this one is labelled "remember for fly-ins" --
     * it runs ONCE, not per frame. The debug line printing exactly once in a 700-frame run
     * is what gave it away. Pinning HER (REVA_ANIMSTATE=0 -> "IDLE [PINNED]") and using a
     * plain ctl `cam=` does the whole job with mechanisms that already work. */
    { static int once9=0;
      if(g_markEdit&&g_heroHeadOK&&!once9){ once9=1;
          printf("[mark] head at %.4f %.4f %.4f%c",
                 g_heroHeadW[0],g_heroHeadW[1],g_heroHeadW[2],10); fflush(stdout); } }
    for(int i2=0;i2<3;i2++){ g_lastE[i2]=eye[i2]; g_lastC[i2]=ctr[i2]; }   /* remember for fly-ins */
}

