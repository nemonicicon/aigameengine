/* ================= SUN SHADOW MAPS (09-11) ==========================================
 * The audit ranked "shadow maps for the world" fourth: the club's whole shadow budget was
 * one 256^2 silhouette of reva projected onto the dance floor, plus blob ellipses under the
 * crowd (gated off in sets). Every street frame had bodies with no contact to the ground.
 * Measured before building: this ANGLE/D3D11 context has NO OES_depth_texture, so the sun's
 * depth is PACKED INTO RGBA8 (24 bits over the ortho range) and the receiver unpacks it.
 *   The pass: an orthographic window (+-g_shadowExt units) round the lens's aim, snapped to
 *   the map's own texel grid so a moving window does not shimmer, rendered from the sun
 *   direction the tod= curve already produces. Casters: the set's props (only those inside the
 *   window), and every skinned body the frame stashes -- the hero, the cast, the set actors,
 *   and up to 32 crowd slots -- one frame late, exactly as the floor mask always was.
 *   Receivers: all four mesh programs (uShVP/uShMap/uShOn), four taps, slope-scaled bias;
 *   only the SUN term is shadowed. Cut-out casters (leaves, hair) alpha-test in the pass.
 *   ctl shadow=0|1 (default: on whenever a sun is up), shadowext=<units>; REVA_SHADOW=0. */
#define SUNMAP g_sunMap   /* the ladder sets it at boot (REVA_TIER); declared beside the tiers */
#define NSUNCAST 96
typedef struct { Pack*P; int mi; M4 model; float pal[MAXB*16]; GLuint vb0, vb1; int world; unsigned owner; } SunCast;
static SunCast g_sunCast[NSUNCAST]; static int g_nSunCast=0, g_nSunCrowd=0;
static int g_shadow=-1, g_shadowOn=0, g_shadowPass=0; static float g_shadowExt=28.f;
static GLuint g_sunFbo=0,g_sunTex=0,g_sunDs=0,g_sunProg=0;
static GLint  g_su_vp=-1,g_su_model=-1,g_su_bind=-1,g_su_pal=-1,g_su_use=-1;
static M4 g_sunVP={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; static float g_sunFocus[3]={0,0,0};
static void sunStash(Pack*P,int mi,const float*pal,int nb,const float*model,int crowd){
    if(!g_shadowOn||nb<=0||nb>MAXB) return;
    if(crowd&&g_nSunCrowd>=32) return;                 /* the crowd may not crowd the hero out */
    if(g_nSunCast>=NSUNCAST){ static int said=0; if(!said){ said=1; printf("[shadow] caster stash FULL at %d -- raise NSUNCAST\n",NSUNCAST); } return; }
    float dx=model[12]-g_sunFocus[0], dz=model[14]-g_sunFocus[2], lim=g_shadowExt*1.6f*g_shadowFar;
    if(dx>lim||dx<-lim||dz>lim||dz<-lim) return;       /* outside the window: cannot cast into it */
    SunCast*S=&g_sunCast[g_nSunCast++]; if(crowd) g_nSunCrowd++;
    S->P=P; S->mi=mi; memcpy(S->model,model,64); memcpy(S->pal,pal,(size_t)nb*64); S->vb0=S->vb1=0; S->world=0;
#ifdef REVA_PACKET
    S->owner=g_vkOwner;
#endif
}
/* ⭐ HANGING CLOTH CASTS TOO (10-18): its own buffers hold WORLD positions, so it is replayed with an identity model,
 * palette and bind -- the body's matrix only says whether it stands inside the sun's window */
static void sunStashW(Pack*P,int mi,GLuint vb0,GLuint vb1,int nb,const float*bodyModel){
    if(!g_shadowOn||nb<=0||nb>MAXB||g_nSunCast>=NSUNCAST) return;
    float dx=bodyModel[12]-g_sunFocus[0], dz=bodyModel[14]-g_sunFocus[2], lim=g_shadowExt*1.6f*g_shadowFar;
    if(dx>lim||dx<-lim||dz>lim||dz<-lim) return;
    static const float I9[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    SunCast*S=&g_sunCast[g_nSunCast++];
    S->P=P; S->mi=mi; memcpy(S->model,I9,64); for(int b=0;b<nb;b++) memcpy(S->pal+b*16,I9,64);
    S->vb0=vb0; S->vb1=vb1; S->world=1; }

static void actorsDraw(GLint mModel,GLint mPal,GLint mBind,float*pal,GLint uUse,GLint uTint){
    g_leadN=0;
    if(!g_setNA||!g_setActorsOn) return;   /* ctl actors=0: the world, empty */
    glActiveTexture(GL_TEXTURE0);
    glUniform1f(uUse,1.f); glUniform3f(uTint,1.f,1.f,1.f);
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
#ifdef REVA_PACKET
        g_vkOwner=((unsigned)GFX_KIND_ACTOR<<28)|(((unsigned)i&0xffffu)<<12);
#endif
        actorDraw(P,isk?-1:A->clip,isk?NULL:A->dnP,isk?-1:A->dnC,
                  A->phase,m,mModel,mPal,mBind,pal,isk?&sp:NULL,ft,pmix,
                  rmOwn9?NULL:&A->wms,g_actorDt,
                  /* a stable per-rider signature: the index and the pack, not a clock */
                  (float)(((i*2654435761u)^((unsigned)A->pk*40503u))%1000u)/1000.f,
                  gnd,
                  /* `look camera` resolves to wherever the lens is THIS frame */
                  A->hasLook==2 ? g_camPos : (A->hasLook==1 ? A->look : NULL),
                  CLOTH_ACTOR0+(i%(CLOTH_SLOTS-CLOTH_ACTOR0)), uUse, uTint);
        g_actorChainSlot=-1; g_actorMorphW=NULL; g_actorMorphWas=NULL;
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
            drawProp(B,bm,mModel,mPal,mBind); } }
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
static void rbDraw(GLint uModel,GLint uPal,GLint uBind,GLint uUse,GLint uTint);   /* rigid bodies (09-11), defined below */
static void objDraw(GLint uModel,GLint uPal,GLint uBind,GLint uUse,GLint uTint);   /* what a game made: engine/objects.inc */
static void stFarDraw(GLint uModel,GLint uPal,GLint uBind,GLint uUse,GLint uTint);
/* ⭐ PLACED SCREENS, ONE DRAW PATH (ROBOT NEKO ep 9). The screen program and quad are made in main
 * and handed here; see the two call sites. ⛔ ORDER IS THE WHOLE POINT: drawSet() paints its sky first
 * with no depth test, so a screen drawn before the set comes out as a sky-coloured hole in the room
 * (Holly "blank" in ep 8 and in ep 9's first take). With a set loaded this is drawn AFTER the world. */
static GLuint g_jbProg=0, g_jbVbo=0;
static GLint  g_jbVP=-1, g_jbModel=-1, g_jbTex=-1, g_jbAmp=-1, g_jbProbe=-1;
#ifdef REVA_VK
#define SCR_VK(m) do{ if(!g_noDraw) gfxScreen(holly_fbo_texture(),m,g_scrAmp); }while(0)
#else
#define SCR_VK(m)
#endif
static void scrDrawPlaced(const float*vp){
    if(g_scrN<=0||!holly_fbo_texture()||!g_jbProg) return;
    { static int said7=0; if(!said7){ said7=1;
        printf("[screen] draw: prog %u vbo %u tex %u | uni vp %d model %d tex %d amp %d | amp %.2f | %s\n",
               (unsigned)g_jbProg,(unsigned)g_jbVbo,(unsigned)holly_fbo_texture(),
               (int)g_jbVP,(int)g_jbModel,(int)g_jbTex,(int)g_jbAmp,g_scrAmp,
               g_setNI?"after the world (a set is loaded)":"no set: in the venue pass");
        fflush(stdout); } }
    GLint prevProg9=0; glGetIntegerv(GL_CURRENT_PROGRAM,&prevProg9);
    GLboolean cull9=glIsEnabled(GL_CULL_FACE);
    glUseProgram(g_jbProg); glUniformMatrix4fv(g_jbVP,1,GL_FALSE,vp);
    glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0); glUniform1i(g_jbTex,0);
    { static int pb9=-1; if(pb9<0){ const char*e=getenv("REVA_SCRPROBE"); pb9=(e&&e[0]!='0'); }
      glUniform1f(g_jbProbe,pb9?1.f:0.f); }
    /* ⛔ HOLLY IS LIT FOR A NIGHTCLUB (mean 6 / max 166 off-screen). A placed screen EMITS, so the
     * shader's `amp` lifting him is correct, not a cheat. `screenamp=` overrides it per shot. */
    glUniform1f(g_jbAmp,g_scrAmp);
    glBindTexture(GL_TEXTURE_2D,holly_fbo_texture());
    /* ep 9 HOLLY PROBE (REVA_HOLLYDBG=1): target rendered? complete? texture still a texture?
     * REVA_HOLLYDUMP=<prefix> reads the target back (the first three probes only). */
    { static int hd9=-1, hn9=0; if(hd9<0) hd9=getenv("REVA_HOLLYDBG")?1:0;
      if(hd9&&(hn9++%20)==0){
          GLint bound9=0; glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound9);
          GLint cur9=0; glGetIntegerv(GL_CURRENT_PROGRAM,&cur9);
          printf("[holly] probe draw %d (frame %u): program %d (jbProg %u)  tex %u isTexture %d bound %d  fbo status 0x%x  renders %u\n",
                 hn9,(unsigned)g_frameStamp,(int)cur9,(unsigned)g_jbProg,(unsigned)holly_fbo_texture(),
                 (int)glIsTexture(holly_fbo_texture()),(int)bound9,holly_fbo_status(),holly_fbo_renders());
          fflush(stdout);
          const char*dp9=getenv("REVA_HOLLYDUMP");
          if(dp9&&hn9/20<3){ char p9[512]; snprintf(p9,sizeof p9,"%s_%03d.ppm",dp9,hn9/20); holly_fbo_dump(p9); } } }
    glBindBuffer(GL_ARRAY_BUFFER,g_jbVbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,20,(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,20,(void*)12);
    for(int k2=2;k2<5;k2++) glDisableVertexAttribArray(k2);
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
            glUniformMatrix4fv(g_jbModel,1,GL_FALSE,mm3); SCR_VK(mm3);
            glDrawArrays(GL_TRIANGLES,0,6);
            continue; }
        float cy=cosf(S3[3]), sy3=sinf(S3[3]);
        float cp=cosf(S3[4]), sp=sinf(S3[4]);
        /* column-major T * Ry * Rx * S, half-extents folded into the scale */
        mm3[0]= cy*W3;      mm3[1]= 0.f;    mm3[2] =-sy3*W3;
        mm3[4]= sy3*sp*H3;  mm3[5]= cp*H3;  mm3[6] = cy*sp*H3;
        mm3[8]= sy3*cp;     mm3[9]=-sp;     mm3[10]= cy*cp;
        mm3[12]=S3[0]; mm3[13]=S3[1]; mm3[14]=S3[2]; mm3[15]=1.f;
        glUniformMatrix4fv(g_jbModel,1,GL_FALSE,mm3); SCR_VK(mm3);
        glDrawArrays(GL_TRIANGLES,0,6); }
    glDisableVertexAttribArray(1);
    if(cull9) glEnable(GL_CULL_FACE);
    glUseProgram((GLuint)prevProg9); }
/* ⭐ ep 11: THE HALF-PIPES, drawn from drawSet with the props' own lit program (in the sun pass too: they cast).
 * A flat bottom sits ON the set's floor, so it is pulled a hair toward the lens to win that tie -- never in the
 * sun pass, where the same pull would be self-shadow acne. */
static void pipeDraw(GLint uModel,GLint uPal,GLint uBind,GLint uUse,GLint uTint){
    if(!g_nPipe) return;
    static const M4 ident={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    GLboolean cull9=glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE); glDisable(GL_BLEND); glDepthMask(GL_TRUE);
    if(!g_shadowPass){ glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.f,-2.f); }
    glUniformMatrix4fv(uModel,1,GL_FALSE,ident);
    glUniformMatrix4fv(uPal,1,GL_FALSE,ident);
    glUniformMatrix4fv(uBind,1,GL_FALSE,ident);
    if(g_curNrmLoc>=0&&!g_shadowPass) glUniform1f(g_curNrmLoc,0.f);
    glUniform1f(uUse,0.f);
    for(int i=0;i<g_nPipe;i++){ HalfPipe*H=&g_pipe[i];
        if(!H->vbo[0]) pipeBuild(H);
        if(!H->nv) continue;
#ifdef REVA_PACKET
        if(!g_noDraw){ const int cnt9[3]={H->nSurf,H->nSide,H->nv-H->nSurf-H->nSide}; const float k9[3][3]={{H->col[0],H->col[1],H->col[2]},{H->col[0]*0.80f,H->col[1]*0.80f,H->col[2]*0.80f},{0.40f,0.41f,0.44f}};
            static const float up9[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0.003f,0,1};   /* the same tie with the floor: a hair up, and never in the sun's pass */
            for(int q9=0;q9<3;q9++){ if(cnt9[q9]<=0) continue; GfxMesh m9; memset(&m9,0,sizeof m9); m9.id=((unsigned)GFX_KIND_PROP<<28)|((0xf200u+(unsigned)i)<<12)|(unsigned)q9; m9.vb[0]=H->vk[q9*2]; m9.vb[1]=H->vk[q9*2+1]; m9.nVerts=(unsigned)cnt9[q9]; m9.model=g_shadowPass?(const float*)ident:up9; memcpy(m9.tint,k9[q9],12); m9.flags=g_shadowPass?GFX_SHADOW:0; gfxMesh(&m9); } }
#endif
        glBindBuffer(GL_ARRAY_BUFFER,H->vbo[0]); glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,0);
        glBindBuffer(GL_ARRAY_BUFFER,H->vbo[1]); glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,0,0);
        glDisableVertexAttribArray(2); glVertexAttrib4f(2,0.f,0.f,0.f,0.f);
        glDisableVertexAttribArray(3); glVertexAttrib4f(3,1.f,0.f,0.f,0.f);
        glDisableVertexAttribArray(4);
        glUniform3f(uTint,H->col[0],H->col[1],H->col[2]);                    glDrawArrays(GL_TRIANGLES,0,H->nSurf);
        glUniform3f(uTint,H->col[0]*0.80f,H->col[1]*0.80f,H->col[2]*0.80f); glDrawArrays(GL_TRIANGLES,H->nSurf,H->nSide);
        glUniform3f(uTint,0.40f,0.41f,0.44f);                                 glDrawArrays(GL_TRIANGLES,H->nSurf+H->nSide,H->nv-H->nSurf-H->nSide); }
    if(!g_shadowPass){ glPolygonOffset(0.f,0.f); glDisable(GL_POLYGON_OFFSET_FILL); }
    if(cull9) glEnable(GL_CULL_FACE);
    glUniform1f(uUse,1.f); glUniform3f(uTint,1.f,1.f,1.f); }
static int g_skyEarly=0;   /* this frame's sky is already down: see THE CROWD WAS HOLES */
static void drawSet(GLint uModel,GLint uPal,GLint uBind,GLint uUse,GLint uTint){
    if(!g_setNI&&!g_nPipe&&!g_setSpace) return;   /* ⭐ ep 11: a set may be only a half-pipe -- (10-22) or only space */
    /* ⛔ CLAIM UNIT 0. drawProp binds its texture to whatever unit happens to be active,
     * and the frame has already been through a shadow pass that leaves unit 1 live. The
     * sampler reads unit 0, so the palette went to a unit nobody samples and the shader
     * alpha-tested against the SHADOW mask instead -- alpha 0, discard, an empty frame
     * with every diagnostic reporting success. */
    /* ⭐ THE SKY GOES DOWN FIRST, and only for a SET. The club's eleven doors have their
     * own deliberate backdrops -- a black room, a star field, a dance floor -- and a
     * daylight sky behind them would be wrong. Gating here also leaves shotsweep's lab and
     * face-rig shots byte-identical, which is what separates a rendering change from a
     * rendering regression. */
    if(!g_shadowPass){ if(g_skyEarly) g_skyEarly=0; else skyDraw(); }   /* the sun pass wants casters only: no sky, no cloth, no water */
    glActiveTexture(GL_TEXTURE0);
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
    for(int i=0;i<g_setNI;i++){
        SetInst*I=&g_setI[i];
        if(I->hid) continue;
        if(!g_shadowPass&&g_gloss.on&&g_setPk[I->pk].floor) continue;   /* ⭐ (10-23) a glossy floor goes down LAST (glossFloorPass) */
        if(g_shadowPass){ /* ⭐ only what the sun window holds: a 300-unit street is mostly outside it */
            float dx=I->m[12]-g_sunFocus[0], dz=I->m[14]-g_sunFocus[2], lim=g_shadowExt*1.7f*g_shadowFar;
            if(dx>lim||dx<-lim||dz>lim||dz<-lim) continue;
            if(g_setPk[I->pk].nocast) continue; }   /* ⭐ a ceiling, a room's walls (10-18) */
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
            for(int k=0;k<6&&!out;k++) if(pl[k][0]*cx+pl[k][1]*cy+pl[k][2]*cz+pl[k][3]<-rr) out=1;
            if(out) continue; }
        cull9done:
        drawn++;
        if(I->tinted){ glUniform1f(uUse,0.f); glUniform3f(uTint,I->tint[0],I->tint[1],I->tint[2]); }
        else         { glUniform1f(uUse,1.f); glUniform3f(uTint,1.f,1.f,1.f); }
#ifdef REVA_PACKET
        if(I->tinted){ g_vkUse=0.f; memcpy(g_vkTint,I->tint,12); } else { g_vkUse=1.f; g_vkTint[0]=g_vkTint[1]=g_vkTint[2]=1.f; }
        g_vkOwner=((unsigned)GFX_KIND_PROP<<28)|(((unsigned)i&0xffffu)<<12);
#endif
        /* ⭐ A ROOM CARRIES ITS OWN LIGHT (ROBOT NEKO ep 9). A photogrammetry texture IS the light
         * the place had when it was photographed; lit again by the club's sun it went dark and flat
         * (the first frame inside the Undercroft read as night). uEmis is a gain on the texture. */
        if(I->room&&!g_shadowPass){ GLint pc9=0; glGetIntegerv(GL_CURRENT_PROGRAM,&pc9);
            GLint uE9=pc9?glGetUniformLocation((GLuint)pc9,"uEmis"):-1;
            if(uE9>=0) glUniform1f(uE9,g_roomLit);
            drawProp(&g_setPk[I->pk],I->m,uModel,uPal,uBind);
            if(uE9>=0) glUniform1f(uE9,0.f); }
        else drawProp(&g_setPk[I->pk],I->m,uModel,uPal,uBind); }
    rbDraw(uModel,uPal,uBind,uUse,uTint);   /* ⭐ the loose objects, in both passes (09-11) */
    objDraw(uModel,uPal,uBind,uUse,uTint);
    stFarDraw(uModel,uPal,uBind,uUse,uTint);
    pipeDraw(uModel,uPal,uBind,uUse,uTint);   /* ⭐ ep 11: the half-pipes, in both passes */
    if(g_shadowPass) return;
    { static int n=0; static int dbg=-1; if(dbg<0){ const char*e=getenv("REVA_CULLDBG"); dbg=e?atoi(e):0; if(e&&dbg<1) dbg=1; }
      if(dbg>=2){ printf("[cull] f%lu %d of %d props drawn (%s)\n",g_frameNo,drawn,g_setNI,cullOn?"frustum":"cull=0"); fflush(stdout); }
      else if(dbg&&(n++%300)==0){ printf("[cull] %d of %d props drawn (%s)\n",drawn,g_setNI,cullOn?"frustum":"cull=0"); fflush(stdout); } }
    glUniform1f(uUse,1.f); glUniform3f(uTint,1.f,1.f,1.f);
    sclDraw(uModel,uPal,uBind,uUse,uTint);
    /* water LAST: it is transparent, so everything it should be seen through has to be in
     * the depth buffer before it draws */
    watDraw(uModel,uPal,uBind,uUse,uTint,g_labAlpha); }

