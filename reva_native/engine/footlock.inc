/* ================= FOOT-LOCK SERVO (S6c) ===========================================
 * gaitRate() derives a playback rate from a clip's AVERAGE contact-foot speed. Average
 * is a guess about a specific instant: a clip whose stride varies through the cycle, or
 * a rig whose measurement was slightly off, still slides. So close the loop.
 *
 * The condition for a planted foot is exact and local:
 *     d(foot_world)/dt = body_velocity + R * d(foot_local)/dphase * dphase/dt = 0
 * Both terms are observable every frame -- the body moved a known amount, and the clip
 * moved the foot a known amount in the body's frame. Any residual IS the slip, so feed
 * it back into the phase rate and the error is driven to zero continuously, whatever the
 * clip, rig or speed.
 *
 * MEASURED AND REJECTED (2026-08-26). It is worse. Compounding a per-frame correction
 * with no damping runs to a rail: Manny's certified walk went 0.15 -> 0.42 and his run
 * 0.20 -> 1.67, and reva's walk 0.32 -> 1.00. The regression suite caught it against the
 * known-good baseline within one run, which is the whole reason the baseline exists.
 * Kept behind REVA_SERVO=1 as a documented negative result: a loop that needs its gain
 * hand-tuned is NOT correct by construction, while the open-loop derivation in
 * gaitRate() -- solve the clip for its own contact speed -- is. Anyone reviving this
 * needs contact-phase gating, damping toward 1, and to beat the baseline before it
 * becomes the default. */
static char g_shotNow[512]={0};    /* live capture request: written by the lab control file */
static float g_servo=1.f;          /* live multiplier on the derived rate */
static float g_servoSlip=0.f;      /* |residual| this frame, for the lab HUD */
static float g_runMul=0.f;
static float runMul(void){ if(g_runMul<=0.f){ const char*e=getenv("REVA_RUNMUL");
        g_runMul=e?(float)atof(e):0.42f; } return g_runMul; }
static float gaitRate(Pack*P,int clip,float spd){
    if(getenv("REVA_NOAUTOGAIT")) return spd*gaitMul();
    if(clip<0||(unsigned)clip>=P->nClips) return spd*gaitMul();
    PClip*c=&P->clips[clip];
    /* MEASURED at bake time from the root-motion sibling. OPT-IN (REVA_GAITSIDECAR=1):
     * exact for a clip played on its OWN rig, but these are transferred onto other
     * bodies and stride scales with the target's leg length, so the raw authored number
     * regressed the certified baseline (manny run skate 0.20 -> 0.33). It needs a
     * source/target limb-length ratio before it can be the default; the sidecar and the
     * measurement are kept because the number itself is right. */
    static int useSide=-1; if(useSide<0) useSide=getenv("REVA_GAITSIDECAR")?1:0;
    if(useSide&&P->gaitUPS&&P->gaitUPS[clip]>0.05f){
        float r2=spd/P->gaitUPS[clip]*g_servo;
        if(r2<0.05f) r2=0.05f;
        if(r2>6.f) r2=6.f;
        return r2; }
    float fpf=clipStrideCached(P,clip);      /* club units the planted foot covers per frame */
    if(fpf<0.004f||c->fps<=0.f||!c->frames) return spd*gaitMul();   /* not a gait: keep the old law */
    float r=spd/(c->fps*fpf)*g_servo;   /* the loop closes here */
    if(r<0.05f) r=0.05f;
    if(r>6.f) r=6.f;
    return r; }
static int   g_heroFOK=0;
#define BC_MAX 96
static struct { float x,z,t; int side; } g_bc[BC_MAX]; static int g_bcN=0;
static float g_labSlip=0.f, g_labTravel=0.f;   /* the live foot-skate accumulator */
static int   g_labCam=0;                       /* live camera angle 0 side 1 front 2 3q 3 top */
#define LAB_GRID_STEP 2.35f            /* spacing between turntable copies, club units */
static float g_labSpeed=-1.f;                  /* live speed override, <0 = the state's own */
static int   g_labFreeze=0;                    /* hold the pose so every camera shoots the SAME instant */
static int   g_labGrid=0;                      /* TURNTABLE: N yaws of the same instant, in ONE frame */
static int   g_labArm=0;                       /* S8: buttons live only after all are seen RELEASED in-mode
                                                * (the char-select confirm leaked in as NEXT STATE) */
static Pack *g_heroPk=NULL;                    /* P0: the lab subject's pack, reachable from the ctl parser */
static Pack *g_movesPk=NULL;                   /* P5: the authored-moves pack (v1_moves), donor for every body */
static int   g_labDnC=-1;                      /* P5: ctl clip= found the clip in the MOVES pack, not the subject */
/* ...or in a BANK. NULL keeps the old behaviour (the authored-moves pack), so every
 * ctl clip= that worked before still resolves exactly as it did. */
static Pack *g_labDnP=NULL;
static int   g_labClip=-1;                     /* P0: ctl clip=<name> -- play THIS clip, state tree overridden */
/* ⭐ THE HERO CAN HOLD A POSE TOO. Same clip@phase as the partner -- reva sitting up
 * in the dark is a POSE, and every clip in the club is a verb. */
static float g_heroHold=-1.f;
static int   g_labWphReset=0;                  /* P0: and start it from frame 0 on the next hero frame */
static int   g_labOnce=0;                      /* P1: play the override clip ONCE, then hand back to the state tree */
static int   g_labRM=0;                        /* P3: the override clip is z_rm_* -- it OWNS the root's Y */
static float g_rmY=-999.f;                     /* P3: the frozen ground offset while a clip owns Y */
static float g_rmEndW[3]={0,0,0};              /* P4: the pelvis's world xz (+y) under an rm clip -- consumed at hand-back */
static int   g_rmActive=0;                     /* P5: an rm clip owns the root THIS frame (the wall aims at the pelvis) */
/* ⭐ ep 10: THE TRICK SCHEDULER. Its state and trickClampEnd sit with the board channel (the samplers read
 * them); the firing lives here, after the lab's clip globals it writes. */
static void trickSaveBase(int s){
    TrickBase*B=&g_trkBase[s];
    if(s==0){ B->clip=g_labClip; B->dnP=g_labDnP; B->dnC=g_labDnC; B->hold=g_heroHold; B->rm=g_labRM; }
    else { CastSlot*C=&g_cast[s]; B->clip=C->clip; B->dnP=C->dnP; B->dnC=C->dnC; B->hold=C->hold; B->rm=0; } }
/* hand a slot back the clip its line gave it, at phase 0 (or at its held pose) */
static void trickRestore(int s,int loud){
    TrickBase*B=&g_trkBase[s];
    if(s==0){ g_labClip=B->clip; g_labDnP=B->dnP; g_labDnC=B->dnC; g_heroHold=B->hold;
              /* ⛔ only when something consumes it: the cast's phase loop also reads g_labWphReset, and a
               * reset nobody spends zeroes every partner's phase on every frame */
              if(B->clip>=0||B->dnC>=0) g_labWphReset=1;
              if(B->rm&&!g_labRM) g_rmY=-999.f;
              g_labRM=B->rm; }
    else { CastSlot*C=&g_cast[s]; C->clip=B->clip; C->dnP=B->dnP; C->dnC=B->dnC; C->hold=B->hold;
           Pack*pp9=C->dnP?C->dnP:C->pk; int ci9=C->dnP?C->dnC:C->clip;
           C->phase=(C->hold>=0.f&&pp9&&ci9>=0&&(unsigned)ci9<pp9->nClips)?(double)C->hold*(double)pp9->clips[ci9].frames:0.0; }
    g_trkOn[s]=0; g_trkDone[s]=0;
    if(loud){ g_trkFresh[s]=1;
        printf("[trick] line %d slot %d: trick done -- back to the line's clip at phase 0\n",g_trkLine,s); fflush(stdout); } }
/* cut one slot to a trick: its own pack first, then a bank through the canonical transfer, as a line's clip is */
static void trickFire(int s,const TrickEv*T){
    Pack*own=(s==0)?g_heroPk:g_cast[s].pk;
    int fo=-1;
    for(unsigned c9=0;own&&c9<own->nClips;c9++) if(!strcmp(packStr(own,own->clips[c9].name),T->clip)){ fo=(int)c9; break; }
    Pack*bp=NULL; int bc=-1;
    if(fo<0&&own&&own->cbOK&&own->pbOK>=10) bp=bankFind(T->clip,&bc);
    if(fo<0&&!bp){
        printf("[trick] line %d slot %d '%s' NOT FOUND -- not in %s's own pack and not in any bank it can borrow from: THE TRICK DOES NOT FIRE\n",
               g_trkLine,s,T->clip,own?own->label:"(no body)");
        fflush(stdout); return; }
    PClip*pc=(fo>=0)?&own->clips[fo]:&bp->clips[bc];
    if(g_trkOn[s]){ printf("[trick] line %d slot %d '%s' cuts the trick before it short\n",g_trkLine,s,T->clip); }
    else trickSaveBase(s);
    int rm=(strncmp(T->clip,"z_rm_",5)==0);
    if(s==0){
        if(fo>=0){ g_labClip=fo; g_labDnC=-1; } else { g_labDnP=bp; g_labDnC=bc; g_labClip=-1; }
        g_heroHold=-1.f; g_labWphReset=1;
        if(rm&&!g_labRM) g_rmY=-999.f;
        g_labRM=rm; }
    else { CastSlot*C=&g_cast[s];
        if(fo>=0){ C->clip=fo; C->dnP=NULL; C->dnC=-1; } else { C->dnP=bp; C->dnC=bc; }
        C->hold=-1.f; C->phase=0.0; }
    g_trkOn[s]=1; g_trkDone[s]=0; g_trkFresh[s]=1;
    float len9=(pc->fps>0.f)?(float)pc->frames/pc->fps:0.f;
    printf("[trick] line %d slot %d '%s' at t=%.2f (len %.2fs) -- +%.2fs into the line%s%s\n",
           g_trkLine,s,T->clip,scnBodyT(),len9,scnBodyT()-g_trkT0,
           fo>=0?"":", borrowed from a bank",rm?", z_rm_ owns the root":"");
    fflush(stdout); }
/* A LINE'S STAGING APPLIES: every body still on a trick is handed back, and this line's tricks are armed */
static void trickArm(int li){
    if(li<0||li>=g_scnN) return;
    const ScnLine*L=&g_scnL[li];
    for(int s=0;s<SCN_MAXCAST;s++){
        if(g_trkOn[s]) trickRestore(s,0);
        g_trkN[s]=0; g_trkNext[s]=0; g_trkDone[s]=0; }
    g_trkLine=li; g_trkT0=g_scnT;
    const char*src[4]={L->htrick,L->ptrick,L->p2trick,L->p3trick};
    for(int s=0;s<4&&s<SCN_MAXCAST;s++){
        const char*p=src[s];
        while(p&&*p){
            const char*e=strchr(p,',');
            int n=e?(int)(e-p):(int)strlen(p);
            char ev[64]; if(n>63) n=63;
            memcpy(ev,p,(size_t)n); ev[n]=0;
            char*at=strchr(ev,'@');
            if(!at||at==ev) printf("[trick] line %d slot %d: '%s' is not clip@sec -- ignored\n",li,s,ev);
            else if(g_trkN[s]>=TRICK_MAX) printf("[trick] line %d slot %d: more than %d tricks -- '%s' dropped (TRICK_MAX)\n",li,s,TRICK_MAX,ev);
            else { *at=0; TrickEv*T=&g_trk[s][g_trkN[s]++];
                snprintf(T->clip,sizeof T->clip,"%.63s",ev); T->at=(float)atof(at+1);
                if(T->at>L->dur+1.f)
                    printf("[trick] line %d slot %d '%s' at +%.2fs is past the line's end (+%.2fs) -- it fires only while this line still has the stage\n",
                           li,s,T->clip,T->at,L->dur);
                /* in time order, whatever order the script wrote them in */
                for(int k=g_trkN[s]-1;k>0&&g_trk[s][k].at<g_trk[s][k-1].at;k--){
                    TrickEv t9=g_trk[s][k]; g_trk[s][k]=g_trk[s][k-1]; g_trk[s][k-1]=t9; } }
            p=e?e+1:NULL; } }
    fflush(stdout); }
/* ONCE PER FRAME, before any body's clip is chosen or advanced. Tricks cut ON an exposure, never between
 * two: a trick's first exposure is its frame 0, and the base clip comes back at its own frame 0. */
static void trickTick(void){
    for(int s=0;s<SCN_MAXCAST;s++) g_trkFresh[s]=0;
    if(g_scnT<0.f||g_trkLine<0) return;
    if(g_stepFps>0.f&&g_stepHeld) return;
    float t=scnBodyT()-g_trkT0;
    for(int s=0;s<SCN_MAXCAST;s++){
        if(s==0?!g_heroPk:!g_cast[s].pk) continue;
        if(g_trkOn[s]&&g_trkDone[s]) trickRestore(s,1);
        /* ⭐ ep 11: a half-pipe air's one-shot (hair=), on the exposure the air is first shown */
        if(g_pipePend[s][0]){ TrickEv T11; snprintf(T11.clip,sizeof T11.clip,"%.31s",g_pipePend[s]); T11.at=t;
            g_pipePend[s][0]=0; trickFire(s,&T11); }
        if(g_trkNext[s]<g_trkN[s]&&t>=g_trk[s][g_trkNext[s]].at) trickFire(s,&g_trk[s][g_trkNext[s]++]); } }
static int g_dumpNear=-1;   /* S4d instrument: nearest-to-hero slot, for REVA_POSEDUMP=-5 */
static long g_dieN=0, g_hideN=0;   /* the death ledger: slotDie calls vs completed hides. Divergence = zombies. */
static void dngAdd(float x,float z); static void crpAdd(float x,float z,int team);
static void slotDie(int i);
static void wpnFeed(const char*s){
    memcpy(g_feed[2],g_feed[1],96); g_feedT[2]=g_feedT[1];
    memcpy(g_feed[1],g_feed[0],96); g_feedT[1]=g_feedT[0];
    snprintf(g_feed[0],96,"%s",s); g_feedT[0]=6.f; }
static void wpnReset(void){
    memset(g_slotWpn,0,sizeof g_slotWpn); memset(g_slotWCd,0,sizeof g_slotWCd);
    memset(g_slotWAmmo,0,sizeof g_slotWAmmo);
    for(int i=0;i<1024;i++) g_slotWvar[i]=-1;
    memset(g_wpick,0,sizeof g_wpick); memset(g_wproj,0,sizeof g_wproj);
    memset(g_feed,0,sizeof g_feed); g_feedT[0]=g_feedT[1]=g_feedT[2]=0.f;
    for(int d=0;d<DNG_MAX;d++) g_dng[d].t=99.f;
    for(int c=0;c<CRP_MAX;c++) g_crp[c].t=99.f;
    memset(g_slotExempt,0,sizeof g_slotExempt); memset(g_slotFleeT,0,sizeof g_slotFleeT);
    memset(g_slotBhv,0,sizeof g_slotBhv); memset(g_slotBhvT,0,sizeof g_slotBhvT);
    for(int i=0;i<1024;i++) g_slotFoe[i]=-1; }
__attribute__((unused)) static void wpnDropPick(float x,float z,int kind){
    if(x<-FLOOR_X+4.f)x=-FLOOR_X+4.f;
    if(x>FLOOR_X-4.f)x=FLOOR_X-4.f;
    if(z<FLOOR_ZN+4.f)z=FLOOR_ZN+4.f;
    if(z>FLOOR_ZF-1.5f)z=FLOOR_ZF-1.5f;
    for(int p=0;p<WPICK_MAX;p++) if(!g_wpick[p].on){
        g_wpick[p].on=1; g_wpick[p].x=x; g_wpick[p].z=z; g_wpick[p].kind=kind;
        g_wpick[p].def=wpnPickDef(kind,(unsigned)(rand())); return; } }
__attribute__((unused)) static int wpnRollKind(void){ float r=frnd(); return r<0.55f?WPN_PISTOL:(r<0.85f?WPN_SPREAD:WPN_BEAM); }
static int wpnNearestFoe(float x,float z,float maxR,int self,int team){
    /* the search every mode was rewriting inline: nearest LIVING grounded slot; team<0
     * means free-for-all, otherwise only slots off that team count as foes */
    int best=-1; float bd=maxR;
    for(int i=0;i<g_NS;i++){ if(i==self||g_slotHide[i]||!g_slotDrawable[i]||g_slotAlt[i]>0.f||g_slotBhv[i]==BHV_DOWN) continue;
        if(team>=0&&g_slotTeam[i]==team) continue;
        float dd=hypotf(g_slots[i].x-x,g_slots[i].z-z);
        if(dd<bd){ bd=dd; best=i; } }
    return best; }
static void wpnKillSlot(int victim,int owner,int kind){
    if(victim<0||victim>=g_NS||g_slotHide[victim]||g_slotBhv[victim]==BHV_DOWN) return;
    slotDie(victim);   /* S4a: the body falls first; the hide door closes ~1s later */
    gmBurst(g_slots[victim].x,1.4f,g_slots[victim].z,1.4f,0.9f,0.2f,8,1.5f);
    dngAdd(g_slots[victim].x,g_slots[victim].z); crpAdd(g_slots[victim].x,g_slots[victim].z,g_slotTeam[victim]);
    char f[96];
    if(owner<0){ GM.kills++; GM.score+=120;
        snprintf(f,sizeof f,"YOU > %.30s  [%s]",g_packs[g_slots[victim].pack].label,WPN_NAME[kind]); }
    else snprintf(f,sizeof f,"%.22s > %.22s  [%s]",g_packs[g_slots[owner].pack].label,
                  g_packs[g_slots[victim].pack].label,WPN_NAME[kind]);
    wpnFeed(f); }
static void wpnHurtHero(float dmg,const char*how){
    if(GM.stage!=0||GM.ifr>0.f) return;
    GM.hp-=dmg; GM.ifr=0.35f;
    sfxPlay("hurt",0.8f,NULL,0);
    gmBurst(g_rvx,1.3f,g_rvz,1.4f,0.3f,0.2f,7,1.1f);
    dngAdd(g_rvx,g_rvz);
    if(GM.hp<=0.f){ GM.hp=0.f; GM.stage=3; GM.deaths++; gmBanner(how,9.f); } }
static void wpnSpawnProj(float x,float z,float dx,float dz,int owner,int kind){
    for(int p=0;p<WPROJ_MAX;p++) if(!g_wproj[p].on){
        g_wproj[p].on=1; g_wproj[p].x=x; g_wproj[p].y=1.4f; g_wproj[p].z=z;
        g_wproj[p].dx=dx; g_wproj[p].dz=dz; g_wproj[p].ttl=GAME_ON?g_tune.shotLife:1.35f;
        g_wproj[p].owner=owner; g_wproj[p].kind=kind; return; } }
__attribute__((unused)) static void wpnFire(int owner,float x,float z,float aim,int kind){
    { float p9[3]={x,1.4f,z}; sfxPlay("shot",owner<0?0.9f:0.7f,owner<0?NULL:p9,0); }   /* the hero's own gun is not spatial: it is at the ear */
    if(kind==WPN_PISTOL) wpnSpawnProj(x,z,sinf(aim)*30.f,cosf(aim)*30.f,owner,kind);
    else if(kind==WPN_SPREAD){ for(int s=-2;s<=2;s++){ float a=aim+s*0.15f;
        wpnSpawnProj(x,z,sinf(a)*22.f,cosf(a)*22.f,owner,kind); } }
    else if(kind==WPN_BEAM){   /* hitscan: the first body near the ray, out to 26u */
        float bx=sinf(aim),bz=cosf(aim); int hit=-1; float hd=26.f;
        for(int i=0;i<g_NS;i++){ if(i==owner||g_slotHide[i]||!g_slotDrawable[i]||g_slotAlt[i]>0.f||g_slotBhv[i]==BHV_DOWN) continue;
            float rx=g_slots[i].x-x,rz=g_slots[i].z-z; float t=rx*bx+rz*bz;
            if(t<0.8f||t>hd) continue;
            if(hypotf(rx-bx*t,rz-bz*t)<0.9f){ hd=t; hit=i; } }
        if(owner>=0){ float rx=g_rvx-x,rz=g_rvz-z; float t=rx*bx+rz*bz;
            if(t>0.8f&&t<hd&&hypotf(rx-bx*t,rz-bz*t)<0.9f){ hd=t; hit=-2; } }
        gmLine(x,1.35f,z,x+bx*hd,1.35f,z+bz*hd,0.9f,0.4f,1.4f);
        gmLine(x,1.5f,z,x+bx*hd,1.5f,z+bz*hd,1.3f,0.8f,1.5f);
        if(hit==-2) wpnHurtHero(20.f,"BEAMED DOWN - B TO GET UP");
        else if(hit>=0) wpnKillSlot(hit,owner,WPN_BEAM); } }
static void wpnProjStep(float dtf){
    for(int f9=0;f9<3;f9++) if(g_feedT[f9]>0.f) g_feedT[f9]-=dtf;
    for(int p=0;p<WPROJ_MAX;p++){ if(!g_wproj[p].on) continue;
        g_wproj[p].ttl-=dtf;
        if(g_wproj[p].ttl<=0.f){ g_wproj[p].on=0; continue; }
        float nx=g_wproj[p].x+g_wproj[p].dx*dtf, nz=g_wproj[p].z+g_wproj[p].dz*dtf;
        int dead=0, cab9=GAME_ON;   /* in the zombie cab a bullet is for the dead: it passes a neighbour, and the world stops it */
        for(int i=0;i<g_NS;i++){ if(i==g_wproj[p].owner||g_slotHide[i]||!g_slotDrawable[i]||g_slotAlt[i]>0.f||g_slotBhv[i]==BHV_DOWN) continue;
            if(cab9&&g_slotTeam[i]!=3) continue;
            if(hypotf(g_slots[i].x-nx,g_slots[i].z-nz)<(cab9?g_tune.shotHit:0.95f)){
                if(cab9) g_game->bulletKill(i); else wpnKillSlot(i,g_wproj[p].owner,g_wproj[p].kind);
                dead=1; break; } }
        if(cab9&&!dead){ float wx9=nx,wz9=nz; if(colResolve(&wx9,&wz9,0.06f)>0.f) dead=1; }
        if(!dead&&g_wproj[p].owner>=0&&hypotf(g_rvx-nx,g_rvz-nz)<0.95f){
            wpnHurtHero(14.f,"GUNNED DOWN - B TO GET UP"); dead=1; }
        if(dead){ gmBurst(nx,1.4f,nz,1.2f,1.0f,0.4f,6,0.9f); g_wproj[p].on=0; continue; }
        g_wproj[p].x=nx; g_wproj[p].z=nz;
        gmLine(nx-g_wproj[p].dx*0.045f,1.4f,nz-g_wproj[p].dz*0.045f,nx,1.4f,nz,1.5f,1.2f,0.4f); } }
static void wpnPickStep(void){
    for(int p=0;p<WPICK_MAX;p++){ if(!g_wpick[p].on) continue;
        const float*c=WPN_COL[g_wpick[p].kind];
        gmRing(g_wpick[p].x,g_wpick[p].z,0.9f,0.15f,c[0],c[1],c[2],16);
        gmLine(g_wpick[p].x,0.15f,g_wpick[p].z,g_wpick[p].x,1.6f,g_wpick[p].z,c[0],c[1],c[2]);
        wpnPropAt(g_wpick[p].x,1.05f+sinf(g_gameT*2.2f+p)*0.12f,g_wpick[p].z,   /* the real gun, turning over the marker */
                  g_gameT*1.7f+p,g_wpick[p].def);
        if(hypotf(g_rvx-g_wpick[p].x,g_rvz-g_wpick[p].z)<1.4f){
            GM.wKind=g_wpick[p].kind; GM.wAmmo=WPN_AMMO[g_wpick[p].kind]; GM.wCd=0.f;
            GM.wVar=g_wpick[p].def;   /* you carry the one you actually stepped over */
            sfxPlay("pickup",0.8f,NULL,0);
            char b[64]; snprintf(b,sizeof b,"%s UP - A FIRES",WPN_NAME[g_wpick[p].kind]);
            gmBanner(b,2.f); g_wpick[p].on=0; } } }
__attribute__((unused)) static int wpnHeroSwing(float reach){
    /* the birthright: a 120-degree arc in front of her; the caller draws the sweep */
    int n=0;
    for(int i=0;i<g_NS;i++){ if(g_slotHide[i]||!g_slotDrawable[i]||g_slotAlt[i]>0.f) continue;
        float dx=g_slots[i].x-g_rvx,dz=g_slots[i].z-g_rvz; float d=hypotf(dx,dz);
        if(d>reach) continue;
        float a=fabsf(fmodf(atan2f(dx,dz)-g_rvHead+9.42477f,6.28318f)-3.14159f);
        if(a>1.05f) continue;
        wpnKillSlot(i,-1,WPN_MELEE); n++; }
    return n; }
