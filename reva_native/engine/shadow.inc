/* ================= SUN SHADOW MAPS (09-11) ==========================================
 * The audit ranked "shadow maps for the world" fourth: the club's whole shadow budget was
 * one 256^2 silhouette of reva projected onto the dance floor, plus blob ellipses under the
 * crowd (gated off in sets). Every street frame had bodies with no contact to the ground.
 * Since step 18 (11-04) the map itself is the renderer's (a depth image, sampled with four taps and
 * a slope-scaled bias); what's here is the window and who casts.
 *   The pass: an orthographic window (+-g_shadowExt units) round the lens's aim, snapped to
 *   the map's own texel grid so a moving window does not shimmer, rendered from the sun
 *   direction the tod= curve already produces. Casters: the set's props (only those inside the
 *   window), and every skinned body the frame stashes -- the hero, the cast, the set actors,
 *   and up to 32 crowd slots -- one frame late, exactly as the floor mask always was.
 *   Receivers: every mesh the renderer draws; only the SUN term is shadowed. Cut-out casters (leaves, hair) alpha-test in the pass.
 *   ctl shadow=0|1 (default: on whenever a sun is up), shadowext=<units>; REVA_SHADOW=0. */
#define SUNMAP g_sunMap   /* the ladder sets it at boot (REVA_TIER); declared beside the tiers */
#define NSUNCAST 96
typedef struct { Pack*P; int mi; M4 model; float pal[MAXB*16]; unsigned vb0, vb1; int world; unsigned owner; unsigned ib, ni; } SunCast;   /* ib, ni: (11-07) the light copy it was drawn from, or 0 */
static SunCast g_sunCast[NSUNCAST]; static int g_nSunCast=0, g_nSunCrowd=0;
static int g_shadow=-1, g_shadowOn=0, g_shadowPass=0; static float g_shadowExt=28.f;
static M4 g_sunVP={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; static float g_sunFocus[3]={0,0,0};
static void sunStash(Pack*P,int mi,const float*pal,int nb,const float*model,int crowd){
    if(!g_shadowOn||nb<=0||nb>MAXB) return;
    if(crowd&&g_nSunCrowd>=32) return;                 /* the crowd may not crowd the hero out */
    if(g_nSunCast>=NSUNCAST){ static int said=0; if(!said){ said=1; printf("[shadow] caster stash FULL at %d -- raise NSUNCAST\n",NSUNCAST); } return; }
    float dx=model[12]-g_sunFocus[0], dz=model[14]-g_sunFocus[2], lim=g_shadowExt*1.6f*g_shadowFar;
    if(dx>lim||dx<-lim||dz>lim||dz<-lim) return;       /* outside the window: cannot cast into it */
    SunCast*S=&g_sunCast[g_nSunCast++]; if(crowd) g_nSunCrowd++;
    S->P=P; S->mi=mi; memcpy(S->model,model,64); memcpy(S->pal,pal,(size_t)nb*64); S->vb0=S->vb1=0; S->world=0;
    S->owner=g_vkOwner; S->ib=g_vkIdxIb; S->ni=g_vkIdxN;   /* (11-07) the sun sees the copy the picture does */
}
/* ⭐ HANGING CLOTH CASTS TOO (10-18): its own buffers hold WORLD positions, so it is replayed with an identity model,
 * palette and bind -- the body's matrix only says whether it stands inside the sun's window */
static void sunStashW(Pack*P,int mi,unsigned vb0,unsigned vb1,int nb,const float*bodyModel){
    if(!g_shadowOn||nb<=0||nb>MAXB||g_nSunCast>=NSUNCAST) return;
    float dx=bodyModel[12]-g_sunFocus[0], dz=bodyModel[14]-g_sunFocus[2], lim=g_shadowExt*1.6f*g_shadowFar;
    if(dx>lim||dx<-lim||dz>lim||dz<-lim) return;
    static const float I9[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    SunCast*S=&g_sunCast[g_nSunCast++];
    S->P=P; S->mi=mi; memcpy(S->model,I9,64); for(int b=0;b<nb;b++) memcpy(S->pal+b*16,I9,64);
    S->vb0=vb0; S->vb1=vb1; S->world=1; S->ib=S->ni=0; }

static void actorsDraw(float*pal){
    g_leadN=0;
    if(!g_setNA||!g_setActorsOn) return;   /* ctl actors=0: the world, empty */
    for(int i=0;i<g_setNA;i++){ SetActor*A=&g_setA[i];
        Pack*P=&g_setPk[A->pk];
        if(P->meshOff&&P->worn!=A->worn){ P->worn=A->worn; wardApply(P); }   /* ⭐ (10-23) her own outfit */
        float a=((A->nWp||A->mindOn)?A->cyaw:A->yaw)*0.0174532925f;
        float eff=A->sc*((P->scale>1e-6f)?P->scale:1.f);
        float px,pz;
        /* ⭐ A z_rm_ CLIP OWNS ITS ROOT ON A SET ACTOR TOO (10-18), as it does on the hero and the scene cast: a boy
         * lying in a warming pan is INSIDE the pan's collider and must not be pushed out of it, his feet are not
         * what he stands on, and the walk's bounce has no business on a body asleep. */
        int rmOwn9=0;
        { const char*cn9=(A->dnP&&A->dnC>=0&&(unsigned)A->dnC<A->dnP->nClips)?packStr(A->dnP,A->dnP->clips[A->dnC].name)
                        :((A->clip>=0&&(unsigned)A->clip<P->nClips)?packStr(P,P->clips[A->clip].name):"");
          rmOwn9=(cn9&&!strncmp(cn9,"z_rm_",5)); }
        if(A->nWp||A->mindOn){ px=A->cx; pz=A->cz; }      /* Z7a: the path owns her place -- or her mind does (09-11) */
        else { px=A->x+sinf(a)*A->travel; pz=A->z+cosf(a)*A->travel;
               if(!A->skate&&!rmOwn9) colResolve(&px,&pz,0.42f); }
        /* ⛔ A SKATER RIDES SIDEWAYS. The four-angle sheet caught it at once: from the
         * FRONT she was square to the direction of travel, riding down the board like a
         * snowboarder. A rider's shoulders are PERPENDICULAR to where they are going --
         * that is what a stance is, and no side view could ever have shown it. The BODY
         * takes the extra quarter turn; the BOARD does not, because the deck still points
         * the way it rolls. */
        float ba=a;                       /* the deck's heading */
        if(A->skate) a+=1.5708f*SK_STANCE;
        float ca=cosf(a), sa=sinf(a);
        M4 m; memset(m,0,64);
        m[0]=ca*eff; m[2]=-sa*eff; m[5]=eff; m[8]=sa*eff; m[10]=ca*eff; m[15]=1.f;
        /* Z4: a borrowed clip that goes to the ground takes the body down with it. */
        float hipDrop=(A->dnP&&A->dnC>=0)?donorHipDrop(A->dnP,A->dnC,A->phase,P):0.f;
        m[12]=px; m[13]=A->y-P->mny*eff+A->air+hipDrop*eff; m[14]=pz;

        SkPose sp; float ft[14]={0,0,0,0,0,0,0,0,0,0,0,0,0,0};
        int isk=A->skate;
        if(isk) skateTick(A,g_actorDt,&sp);
        /* ⛔ THE PLANT IS A GROUND CONTRACT. Holding the soles at rest height while she
         * is airborne cancels the tuck exactly -- PZ_TUCK pulls the knees to the chest
         * and the plant puts them straight back. It releases as she leaves the deck. */
        float pmix=1.f-A->air/0.30f; if(pmix<0.f) pmix=0.f; if(pmix>1.f) pmix=1.f;
        /* ⛔ NOT ON A GROUND CLIP AND NOT IN THE AIR. donorHipDrop already puts a crawl
         * or a lay on the floor -- the "feet" of a body on its front are above its
         * pelvis, and planting them would fight the correction that is already right.
         * A->air is a deliberate lift and a plant cancels it exactly. */
        int gnd=(!isk && !rmOwn9 && A->air<=0.001f && hipDrop>-0.001f && hipDrop<0.001f);
        g_actorOver=A->over; g_actorOverW=A->overW;   /* her layer, for the length of the call (09-11) */
        { static int on9=-1; if(on9<0){ const char*e9=getenv("REVA_ACTORCHAINS"); on9=!(e9&&e9[0]=='0'); }
          g_actorChainSlot=(on9&&(P->nCh||P->nJg)&&i<CH_ACTORS)?SCN_MAXCAST+i:-1;
          if(on9&&P->nCh&&i>=CH_ACTORS){ static int told9=0; if(!told9){ told9=1;
              printf("[chains] actor %d and beyond carry chains but have no slot (CH_ACTORS=%d): their hair holds its pose\n",i,CH_ACTORS);
              fflush(stdout); } } }
        { static float mw9[SET_MAXA][MORPH_MAX]; static int mwas9[SET_MAXA];
          g_actorMorphW=(i<SET_MAXA)?mw9[i]:NULL; g_actorMorphWas=(i<SET_MAXA)?&mwas9[i]:NULL; }
        g_actorHeldSlot=(A->prop[0]||A->prop2[0])?SCN_VOICE:-1;   /* (11-11) she holds something: her hands published into the voice slot (actorHeldSync) */
        g_vkOwner=((unsigned)GFX_KIND_ACTOR<<28)|(((unsigned)i&0xffffu)<<12);
        if(A->eye[3]>0.f) memcpy(g_vkEye,A->eye,16);   /* (step 23) her eyes glow, if her line said so */
        /* (11-11) `actorwash`: her skin and clothes mixed toward a mover's colour this frame, as a scene's body is by `slotwash` (body.inc; her hair keeps its own) */
        if(A->washA>0.f&&A->washM>=0&&A->washM<SLOTWASH_MOV){ memcpy(g_bodyWash,g_stkMovCol[A->washM],12); g_bodyWash[3]=A->washA; g_bodyWashHair=A->washHair; }
        else { g_bodyWash[3]=0.f; g_bodyWashHair=0; }
        actorDraw(P,isk?-1:A->clip,isk?NULL:A->dnP,isk?-1:A->dnC,
                  A->phase,m,pal,isk?&sp:NULL,ft,pmix,
                  rmOwn9?NULL:&A->wms,g_actorDt,
                  /* a stable per-rider signature: the index and the pack, not a clock */
                  (float)(((i*2654435761u)^((unsigned)A->pk*40503u))%1000u)/1000.f,
                  gnd,
                  /* `look camera` resolves to wherever the lens is THIS frame */
                  A->hasLook==2 ? g_camPos : (A->hasLook==1 ? A->look : NULL),
                  CLOTH_ACTOR0+(i%(CLOTH_SLOTS-CLOTH_ACTOR0)));
        g_vkEye[3]=0.f; g_bodyWash[3]=0.f; g_bodyWashHair=0;   /* (11-11) and her wash ends with her */
        g_actorChainSlot=-1; g_actorMorphW=NULL; g_actorMorphWas=NULL; g_actorHeldSlot=-1;
        /* both ends into world space, through the body's own matrix */
        { float*nq=ft+8, *hq=ft+11;
          A->neckW[0]=m[0]*nq[0]+m[4]*nq[1]+m[8]*nq[2]+m[12];
          A->neckW[1]=m[1]*nq[0]+m[5]*nq[1]+m[9]*nq[2]+m[13];
          A->neckW[2]=m[2]*nq[0]+m[6]*nq[1]+m[10]*nq[2]+m[14];
          A->handW[0]=m[0]*hq[0]+m[4]*hq[1]+m[8]*hq[2]+m[12];
          A->handW[1]=m[1]*hq[0]+m[5]*hq[1]+m[9]*hq[2]+m[13];
          A->handW[2]=m[2]*hq[0]+m[6]*hq[1]+m[10]*hq[2]+m[14]; }
        if(A->board>=0){
            Pack*B=&g_setPk[A->board];
            float be=(B->scale>1e-6f)?B->scale:1.f;
            /* ⭐ THE BOARD FOLLOWS THE FEET. Posing legs onto a fixed deck is an IK
             * problem with a hundred quiet ways to be wrong; putting the deck under the
             * midpoint of the feet the FK actually produced is right by construction. */
            float bx=px, by=A->y+0.10f, bz=pz;
            if(isk&&(ft[1]!=0.f||ft[4]!=0.f)){
                /* ⛔ RIG SPACE, NOT WORLD. The foot bones come out of the pack's own
                 * frame; the model matrix is what puts them in the world, and every rig
                 * is authored in different units. Transform first or the board is placed
                 * from a number that means nothing. */
                float fw[6];
                float lowest=(ft[1]<ft[4])?ft[1]:ft[4];
                float plant=((ft[6]-lowest)*eff + SK_DECK_H)*pmix;   /* the SAME lift actorDraw applied */
                for(int fk=0;fk<2;fk++){
                    const float*v=ft+fk*3;
                    fw[fk*3+0]=m[0]*v[0]+m[4]*v[1]+m[8] *v[2]+m[12];
                    fw[fk*3+1]=m[1]*v[0]+m[5]*v[1]+m[9] *v[2]+m[13]+plant;
                    fw[fk*3+2]=m[2]*v[0]+m[6]*v[1]+m[10]*v[2]+m[14]; }
                float mx=(fw[0]+fw[3])*0.5f, my=(fw[1]+fw[4])*0.5f, mz=(fw[2]+fw[5])*0.5f;
                float dx2=fw[0]-fw[3], dz2=fw[2]-fw[5];
                A->footY[0]=fw[1]; A->footY[1]=fw[4];
                A->footSpread=sqrtf(dx2*dx2+dz2*dz2);
                A->kneeZ=ft[7];
                bx=mx; bz=mz; by=my-SK_DECK_H;   /* the deck sits just under the soles */
            }
            M4 bm; memset(bm,0,64);
            float bc=cosf(ba+A->deckYaw), bs=sinf(ba+A->deckYaw);
            float cr=cosf(A->deckRoll), sr=sinf(A->deckRoll);
            /* ⛔ A KICKFLIP SPINS ABOUT THE DECK'S LONG AXIS. Measured off the prop's own
             * vertices: 1.372 in Z, 0.375 in X, 0.211 in Y -- the length is Z. The first
             * version rolled about X, the SHORT axis, so the board went end over end and
             * hung vertical in the air. Yaw (the shove-it) times roll about Z (the flip). */
            bm[0]= bc*cr*be; bm[1]= sr*be; bm[2]=-bs*cr*be; bm[3]=0;
            bm[4]=-bc*sr*be; bm[5]= cr*be; bm[6]= bs*sr*be; bm[7]=0;
            bm[8]= bs*be;    bm[9]= 0.f;   bm[10]= bc*be;   bm[11]=0;
            bm[12]=bx; bm[13]=by; bm[14]=bz; bm[15]=1.f;
            drawProp(B,bm); } }
    if(getenv("REVA_SKDBG")){ static float lg=0.f; lg+=g_actorDt;
        if(lg>1.0f){ lg=0.f;
            for(int i=0;i<g_setNA;i++){ SetActor*A=&g_setA[i]; if(!A->skate) continue;
                printf("[skate] %d  feet y %.3f / %.3f  spread %.3f  deck y %.3f  ankle-vs-knee %+.3f %-8s trick %s\n",
                       i,A->footY[0],A->footY[1],A->footSpread,
                       (A->footY[0]+A->footY[1])*0.5f-SK_DECK_H, A->kneeZ,
                       (A->kneeZ<0.f)?"ok":"INVERTED", A->act?"YES":"-"); }
            fflush(stdout); } } }

/* collected after every actor has been drawn, because a lead needs BOTH ends measured */
static void leadsCollect(void){
    g_leadN=0;
    for(int i=0;i<g_setNA&&g_leadN<LEAD_MAX;i++){ SetActor*A=&g_setA[i];
        if(A->leash<0||A->leash>=g_setNA||A->leash==i) continue;
        SetActor*B=&g_setA[A->leash];
        memcpy(g_leadA[g_leadN],A->neckW,12);
        memcpy(g_leadB[g_leadN],B->handW,12);
        g_leadN++; } }
static void rbDraw(void);   /* rigid bodies (09-11), defined below */
static void objDraw(void);   /* what a game made: engine/objects.inc */
static void stFarDraw(void);
/* ⭐ PLACED SCREENS, ONE DRAW PATH (ROBOT NEKO ep 9). A screen is a quad the renderer draws showing Holly's
 * target (gfxScreen); see the two call sites. ⛔ ORDER IS THE WHOLE POINT: drawSet() paints its sky first
 * with no depth test, so a screen drawn before the set comes out as a sky-coloured hole in the room
 * (Holly "blank" in ep 8 and in ep 9's first take). With a set loaded this is drawn AFTER the world. */
#ifdef REVA_VK
#define SCR_VK(m) do{ if(!g_noDraw) gfxScreen(holly_fbo_texture(),m,g_scrAmp); }while(0)
#else
#define SCR_VK(m)
#endif
static void scrDrawPlaced(void){
    if(g_scrN<=0||!holly_fbo_texture()) return;
    { static int said7=0; if(!said7){ said7=1;
        printf("[screen] draw: tex %u | amp %.2f | %s\n",(unsigned)holly_fbo_texture(),g_scrAmp,
               g_setNI?"after the world (a set is loaded)":"no set: in the venue pass");
        fflush(stdout); } }
    /* ⛔ HOLLY IS LIT FOR A NIGHTCLUB (mean 6 / max 166 off-screen). A placed screen EMITS, so the
     * `amp` lifting him is correct, not a cheat. `screenamp=` overrides it per shot. */
    /* ep 9 HOLLY PROBE (REVA_HOLLYDBG=1): how often his head has been drawn into its target */
    { static int hd9=-1, hn9=0; if(hd9<0) hd9=getenv("REVA_HOLLYDBG")?1:0;
      if(hd9&&(hn9++%20)==0){
          printf("[holly] probe draw %d (frame %u): tex %u  renders %u\n",hn9,(unsigned)g_frameStamp,(unsigned)holly_fbo_texture(),holly_fbo_renders());
          fflush(stdout); } }
    for(int si=0;si<g_scrN;si++){
        const float*S3=g_scr[si];
        float W3=S3[5], H3=S3[6];
        M4 mm3; memset(mm3,0,64);
        /* BILLBOARD: pitch <= -900 means "face the lens, wherever it is". Basis off the view vector. */
        if(S3[4]<=-900.f*0.0174533f){
            float fx=g_camEye[0]-S3[0], fy=g_camEye[1]-S3[1], fz=g_camEye[2]-S3[2];
            float fl=sqrtf(fx*fx+fy*fy+fz*fz); if(fl<1e-4f) fl=1.f;
            fx/=fl; fy/=fl; fz/=fl;
            float rx=-fz, rz=fx;                      /* up(0,1,0) x fwd, flattened */
            float rl=sqrtf(rx*rx+rz*rz); if(rl<1e-4f){ rx=1.f; rz=0.f; rl=1.f; }
            rx/=rl; rz/=rl;
            float ux=fy*rz, uy=fz*rx-fx*rz, uz=-fy*rx;   /* fwd x right */
            float ul=sqrtf(ux*ux+uy*uy+uz*uz); if(ul<1e-4f){ ux=0;uy=1;uz=0; ul=1.f; }
            ux/=ul; uy/=ul; uz/=ul;
            mm3[0]=rx*W3; mm3[1]=0.f*W3; mm3[2]=rz*W3;
            mm3[4]=ux*H3; mm3[5]=uy*H3;  mm3[6]=uz*H3;
            mm3[8]=fx;    mm3[9]=fy;     mm3[10]=fz;
            mm3[12]=S3[0]; mm3[13]=S3[1]; mm3[14]=S3[2]; mm3[15]=1.f;
            SCR_VK(mm3);
            continue; }
        float cy=cosf(S3[3]), sy3=sinf(S3[3]);
        float cp=cosf(S3[4]), sp=sinf(S3[4]);
        /* column-major T * Ry * Rx * S, half-extents folded into the scale */
        mm3[0]= cy*W3;      mm3[1]= 0.f;    mm3[2] =-sy3*W3;
        mm3[4]= sy3*sp*H3;  mm3[5]= cp*H3;  mm3[6] = cy*sp*H3;
        mm3[8]= sy3*cp;     mm3[9]=-sp;     mm3[10]= cy*cp;
        mm3[12]=S3[0]; mm3[13]=S3[1]; mm3[14]=S3[2]; mm3[15]=1.f;
        SCR_VK(mm3); } }
/* ⭐ ep 11: THE HALF-PIPES, drawn from drawSet as props are (in the sun pass too: they cast).
 * A flat bottom sits ON the set's floor, so it is pulled a hair toward the lens to win that tie -- never in the
 * sun pass, where the same pull would be self-shadow acne. */
static void pipeDraw(void){
    if(!g_nPipe) return;
    static const M4 ident={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    for(int i=0;i<g_nPipe;i++){ HalfPipe*H=&g_pipe[i];
        if(!H->vbo[0]) pipeBuild(H);
        if(!H->nv) continue;
        if(!g_noDraw){ const int cnt9[3]={H->nSurf,H->nSide,H->nv-H->nSurf-H->nSide}; const float k9[3][3]={{H->col[0],H->col[1],H->col[2]},{H->col[0]*0.80f,H->col[1]*0.80f,H->col[2]*0.80f},{0.40f,0.41f,0.44f}};
            static const float up9[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0.003f,0,1};   /* the same tie with the floor: a hair up, and never in the sun's pass */
            for(int q9=0;q9<3;q9++){ if(cnt9[q9]<=0) continue; GfxMesh m9; memset(&m9,0,sizeof m9); m9.id=((unsigned)GFX_KIND_PROP<<28)|((0xf200u+(unsigned)i)<<12)|(unsigned)q9; m9.vb[0]=H->vk[q9*2]; m9.vb[1]=H->vk[q9*2+1]; m9.nVerts=(unsigned)cnt9[q9]; m9.model=g_shadowPass?(const float*)ident:up9; memcpy(m9.tint,k9[q9],12); m9.flags=g_shadowPass?GFX_SHADOW:0; gfxMesh(&m9); } }
    } }
static void drawSet(void){
    if(!g_setNI&&!g_nPipe&&!g_setSpace) return;   /* ⭐ ep 11: a set may be only a half-pipe -- (10-22) or only space */
    /* ⭐ THE SKY GOES DOWN FIRST, and only for a SET. The club's eleven doors have their
     * own deliberate backdrops -- a black room, a star field, a dance floor -- and a
     * daylight sky behind them would be wrong. Gating here also leaves shotsweep's lab and
     * face-rig shots byte-identical, which is what separates a rendering change from a
     * rendering regression. */
    /* ⭐ FRUSTUM CULL (09-11). The set renderer was "deliberately dumb: no culling, no batching,
     * no LOD" and drew all 559 of parsons' props every frame whichever way the lens faced.
     * Measured first: 10.4 ms a frame in a door on the street, 13.8 on a lab wide. Each
     * instance carries a sphere (the pack's XZ radius and rendered height, at the instance's
     * scale) tested against the six planes of this pass's view-projection (Gribb-Hartmann,
     * off g_watVP). ctl cull=0 for the A/B; REVA_CULLDBG=1 prints the count. */
    if(!g_cullEnvDone){ g_cullEnvDone=1; const char*e=getenv("REVA_NOCULL"); if(e&&e[0]&&e[0]!='0'){ g_cullOn=0; printf("[cull] OFF (REVA_NOCULL)\n"); } }
    float pl[6][4]; int cullOn=g_cullOn&&!g_shadowPass&&g_gameViews.n<=1; int drawn=0;
    if(cullOn){ const float*m=g_watVP;
        for(int k=0;k<3;k++){ for(int c=0;c<4;c++){ pl[k*2][c]=m[c*4+3]+m[c*4+k]; pl[k*2+1][c]=m[c*4+3]-m[c*4+k]; } }
        for(int k=0;k<6;k++){ float l=sqrtf(pl[k][0]*pl[k][0]+pl[k][1]*pl[k][1]+pl[k][2]*pl[k][2]); if(l>1e-6f) for(int c=0;c<4;c++) pl[k][c]/=l; } }
    g_vkEye[3]=0.f;   /* (step 23) a crowd body's glowing eyes don't carry over onto what the set draws */
    for(int i=0;i<g_setNI;i++){
        SetInst*I=&g_setI[i];
        if(I->hid) continue;
        if(!g_shadowPass&&g_gloss.on&&g_setPk[I->pk].floor) continue;   /* ⭐ (10-23) a glossy floor goes down LAST (glossFloorPass) */
        if(g_shadowPass){ /* ⭐ only what the sun window holds: a 300-unit street is mostly outside it */
            float dx=I->m[12]-g_sunFocus[0], dz=I->m[14]-g_sunFocus[2], lim=g_shadowExt*1.7f*g_shadowFar;
            if(dx>lim||dx<-lim||dz>lim||dz<-lim) continue;
            if(g_setPk[I->pk].nocast) continue;   /* ⭐ a ceiling, a room's walls (10-18) */
            if(I->fade>0.5f) continue; }   /* more than half faded out: no shadow any more */
        else if(cullOn){ Pack*Q=&g_setPk[I->pk];
            float sc=sqrtf(I->m[0]*I->m[0]+I->m[1]*I->m[1]+I->m[2]*I->m[2]); if(sc<1e-6f) sc=1.f;
            float hh=Q->effH*0.5f, rr=sqrtf(Q->propR*Q->propR+hh*hh)*sc*1.25f+1.0f;   /* margin: a 23x9 px sliver of a tree at the frame edge went missing at 1.15+0.5 */
            /* a mover stretched on one axis: tall and thin was being culled while it was on screen */
            { float sy9=sqrtf(I->m[4]*I->m[4]+I->m[5]*I->m[5]+I->m[6]*I->m[6]), sz9=sqrtf(I->m[8]*I->m[8]+I->m[9]*I->m[9]+I->m[10]*I->m[10]);
              if(fabsf(sy9-sc)>sc*0.01f||fabsf(sz9-sc)>sc*0.01f){ float sh9=sc>sz9?sc:sz9, r9=Q->propR*sh9, h9=hh*sy9; rr=sqrtf(r9*r9+h9*h9)*1.25f+1.0f; float cy9=I->m[13]+hh*sy9, cx9=I->m[12]+I->m[0]*Q->propCx+I->m[8]*Q->propCz, cz9=I->m[14]+I->m[2]*Q->propCx+I->m[10]*Q->propCz; int out9=0;
                  for(int k=0;k<6&&!out9;k++) if(pl[k][0]*cx9+pl[k][1]*cy9+pl[k][2]*cz9+pl[k][3]<-rr) out9=1;
                  if(out9) continue;
                  goto cull9done; } }
            /* ⛔ THE SPHERE SITS ON THE BONE CLOUD'S CENTRE, NOT THE INSTANCE ORIGIN. A ground
             * tile is anchored at its MAX corner and extends 28 units back; centred on the
             * origin, the near tile was culled and the bottom 14% of a wide went missing. */
            float cx=I->m[12]+I->m[0]*Q->propCx+I->m[8]*Q->propCz, cy=I->m[13]+hh*sc, cz=I->m[14]+I->m[2]*Q->propCx+I->m[10]*Q->propCz; int out=0;
            /* ⛔ (11-05) AND NOT A BODY'S HEIGHT OFF THE FLOOR WHEN ITS TRIANGLES ARE SOMEWHERE ELSE. A streamed city cell stands at y 0 and its ground is 50 to 130 units up a hill (Bike Lane Bash's
             * Seattle): this sphere sat at y 1.6, and a lens looking level along the Jose Rizal Bridge's deck culled the cell it stood in, so the deck under it was missing. Where the vertices' own
             * height range isn't inside the sphere, the sphere is put round them; where it is, nothing changes */
            { const float y09=I->m[13]+Q->propY0*sc,y19=I->m[13]+Q->propY1*sc;
              static int old9=-1; if(old9<0){ const char*e9=getenv("REVA_OLDCULL"); old9=(e9&&e9[0]=='1')?1:0; }   /* the control: the sphere as it was */
              if(!old9&&Q->propY1>Q->propY0&&(y19>cy+rr||y09<cy-rr)){ const float h9=0.5f*(y19-y09),r9=Q->propR*sc; cy=0.5f*(y09+y19); rr=sqrtf(r9*r9+h9*h9)*1.25f+1.0f; } }
            for(int k=0;k<6&&!out;k++) if(pl[k][0]*cx+pl[k][1]*cy+pl[k][2]*cz+pl[k][3]<-rr) out=1;
            if(out) continue; }
        cull9done:
        drawn++;
        setInstLook(I);
        g_vkOwner=((unsigned)GFX_KIND_PROP<<28)|(((unsigned)i&0xffffu)<<12);
        /* ⭐ A ROOM CARRIES ITS OWN LIGHT (ROBOT NEKO ep 9). A photogrammetry texture IS the light
         * the place had when it was photographed; lit again by the club's sun it went dark and flat
         * (the first frame inside the Undercroft read as night). uEmis is a gain on the texture. */
        { Pack*Qd9=&g_setPk[I->pk]; const float*Md9=I->m; float m9[16];
          /* (step 24) LEVEL OF DETAIL: how big it is in the picture picks which copy of it is drawn. Its shadow comes from a lighter one sooner */
          if(g_setLod[I->pk][0]!=-1||g_setLod[I->pk][2]!=-1){ static const float bias9[5]={0.5f,0.75f,1.f,1.4f,2.f};
              const float sc9=sqrtf(I->m[0]*I->m[0]+I->m[1]*I->m[1]+I->m[2]*I->m[2]), hh9=fmaxf(Qd9->effH,Qd9->propR*2.f)*sc9;
              const float ex9=I->m[12]-g_camEye[0], ey9=I->m[13]-g_camEye[1], ez9=I->m[14]-g_camEye[2], d9=sqrtf(ex9*ex9+ey9*ey9+ez9*ez9)+1e-3f;
              const float big9=hh9/d9*bias9[g_gfxLod<0?0:g_gfxLod>4?4:g_gfxLod]*(g_shadowPass?0.5f:1.f); int lv9=-1;
              if(big9<0.030f&&!g_shadowPass&&setLod(I->pk,2)>=0) lv9=2; else if(big9<0.085f&&setLod(I->pk,1)>=0) lv9=1; else if(big9<0.22f&&setLod(I->pk,0)>=0) lv9=0;
              if(lv9<0&&big9<0.22f&&g_setLod[I->pk][0]==-2) (void)setLod(I->pk,0);
              if(lv9>=0) Qd9=&g_setPk[g_setLod[I->pk][lv9]];
              { static int dbg9=-1; if(dbg9<0){ const char*e9=getenv("REVA_LODDBG"); dbg9=(e9&&e9[0]!='0')?1:0; if(dbg9) printf("[lod] REVA_LODDBG: a prop drawn as its lod1 is washed green, its lod2 yellow, its far picture red\n"); }
                if(dbg9&&lv9>=0&&!g_shadowPass){ static const float c9[3][3]={{0.2f,1.f,0.2f},{1.f,0.9f,0.1f},{1.f,0.15f,0.1f}}; g_vkUse=0.55f; memcpy(g_vkTint,c9[lv9],12); } } }
          /* (step 24) FOLIAGE SWAYS: its top leans with the set's wind, in waves that cross the field, and its foot stays where it's planted */
          if(I->sway>0.f){ const float t9=(float)(now_ms()*0.001); float wx9=g_windV[0], wz9=g_windV[2], wl9=sqrtf(wx9*wx9+wz9*wz9); if(wl9<0.05f){ wx9=0.3f; wz9=0.1f; wl9=0.316f; }
              const float amt9=I->sway*fminf(1.6f,0.35f+0.65f*wl9)*(0.55f+0.45f*sinf(t9*(1.3f+0.25f*wl9)+I->swayPh)+0.12f*sinf(t9*3.7f+I->swayPh*1.9f));
              const float yl9=sqrtf(I->m[4]*I->m[4]+I->m[5]*I->m[5]+I->m[6]*I->m[6]);
              memcpy(m9,I->m,64); m9[4]+=wx9/wl9*amt9*yl9; m9[6]+=wz9/wl9*amt9*yl9; Md9=m9; }
          drawProp(Qd9,Md9); }
        setInstLookOff(); }
    rbDraw();   /* ⭐ the loose objects, in both passes (09-11) */
    objDraw();
    stFarDraw();
    pipeDraw();   /* ⭐ ep 11: the half-pipes, in both passes */
    if(g_shadowPass) return;
    { static int n=0; static int dbg=-1; if(dbg<0){ const char*e=getenv("REVA_CULLDBG"); dbg=e?atoi(e):0; if(e&&dbg<1) dbg=1; }
      if(dbg>=2){ printf("[cull] f%lu %d of %d props drawn (%s)\n",g_frameNo,drawn,g_setNI,cullOn?"frustum":"cull=0"); fflush(stdout); }
      else if(dbg&&(n++%300)==0){ printf("[cull] %d of %d props drawn (%s)\n",drawn,g_setNI,cullOn?"frustum":"cull=0"); fflush(stdout); } }
    sclDraw();
    /* water LAST: it is transparent, so everything it should be seen through has to be in
     * the depth buffer before it draws */
    watDraw(); }

