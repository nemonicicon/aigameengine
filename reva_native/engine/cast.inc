/* ONE CAST BODY, AS DRAWN: called beside attPublish with the very W and model the skin used. */
static void clipAuditBody(Pack*P,M4*W,const float*model,int slot){
    if(g_caOn<0) caInit();
    if(g_caOn<=0||g_scnT<0.f||!P||!W||!model||slot<0||slot>=CA_SLOTS) return;
    caLatch();
    CaSlot*S=&g_caS[slot]; CaRest*R=caRestOf(P);
    S->drawn=1; snprintf(S->label,sizeof S->label,"%s",P->label[0]?P->label:"?");
    caSupport(slot,S);   /* ⭐ ep 12 */
    float sc=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]);
    float H=R->headH*sc*1.12f;
    if(!(H>0.5f)) H=3.15f;
    enum{ J_HIPS,J_CHEST,J_NECK,J_HEAD,J_CROWN,J_SHL,J_ELL,J_HNL,J_SHR,J_ELR,J_HNR,
          J_THL,J_KNL,J_ANL,J_TOL,J_SOL,J_THR,J_KNR,J_ANR,J_TOR,J_SOR,J_N };
    static const char*NM[J_N]={"hips","chest","neck","head","crown","shoulderL","elbowL","handL",
        "shoulderR","elbowR","handR","thighL","kneeL","ankleL","toeL","soleL","thighR","kneeR","ankleR","toeR","soleR"};
    int chest=(P->pb[3]>=0)?P->pb[3]:((P->pb[2]>=0)?P->pb[2]:P->pb[1]);
    int nd[J_N]={P->pb[0],chest,P->pb[4],P->pb[5],-1,P->pb[7],P->pb[8],P->gripL,P->pb[10],P->pb[11],P->gripNode,
                 P->pb[12],P->pb[13],P->footL,R->toe[0],-1,P->pb[14],P->pb[15],P->footR,R->toe[1],-1};
    float J[J_N][3]; int ok[J_N];
    for(int j=0;j<J_N;j++){ ok[j]=(nd[j]>=0&&(unsigned)nd[j]<P->nNodes);
        if(ok[j]) ptWorld(W[nd[j]],model,J[j]); }
    if(ok[J_HEAD]){ float ax=0.f, ay=1.f, az=0.f;       /* the crown: up the neck->head line, a tenth of her height */
        if(ok[J_NECK]){ float dx=J[J_HEAD][0]-J[J_NECK][0], dy=J[J_HEAD][1]-J[J_NECK][1], dz=J[J_HEAD][2]-J[J_NECK][2];
            float l=sqrtf(dx*dx+dy*dy+dz*dz);
            if(l>1e-5f){ ax=dx/l; ay=dy/l; az=dz/l; } }
        J[J_CROWN][0]=J[J_HEAD][0]+ax*0.10f*H; J[J_CROWN][1]=J[J_HEAD][1]+ay*0.10f*H; J[J_CROWN][2]=J[J_HEAD][2]+az*0.10f*H;
        ok[J_CROWN]=1; }
    for(int s=0;s<2;s++){ int an=s?J_ANR:J_ANL, so=s?J_SOR:J_SOL;   /* the sole: the ankle less its rest height */
        if(ok[an]){ memcpy(J[so],J[an],12); J[so][1]-=R->ankH[s]*sc; ok[so]=1; } }
    S->np=0;
    for(int j=0;j<J_N&&S->np<CA_MAXPT;j++) if(ok[j]){ memcpy(S->pt[S->np].p,J[j],12); S->pt[S->np].nm=NM[j]; S->np++; }
    static const char*WN[8]={"wheel0","wheel1","wheel2","wheel3","wheel4","wheel5","wheel6","wheel7"};
    for(int w=0;w<P->nWheel&&w<8&&S->np<CA_MAXPT;w++){ int wn=P->wheel[w];
        if(wn<0||(unsigned)wn>=P->nNodes) continue;
        float*p=S->pt[S->np].p; ptWorld(W[wn],model,p);
        float h=R->whH[w]*sc; p[1]-=(h>0.15f)?0.15f:h;              /* the wheel's contact, not its hub */
        S->pt[S->np].nm=WN[w]; S->np++; }
    /* rb: this limb as a capsule OTHER bodies are tested against; rw: its flesh against the world.
     * Thousandths of her height -- a foot is thin, a thigh is not. */
    static const struct{ signed char a,b; unsigned char rb,rw; const char*nm; } CAP[]={
        {J_HIPS,J_CHEST,70,60,"spine"},{J_CHEST,J_NECK,70,50,"chest"},{J_NECK,J_CROWN,50,45,"head"},
        {J_SHL,J_ELL,35,30,"upperarmL"},{J_ELL,J_HNL,35,25,"forearmL"},{J_SHR,J_ELR,35,30,"upperarmR"},{J_ELR,J_HNR,35,25,"forearmR"},
        {J_THL,J_KNL,35,40,"thighL"},{J_KNL,J_ANL,35,30,"shinL"},{J_ANL,J_TOL,35,15,"footL"},
        {J_THR,J_KNR,35,40,"thighR"},{J_KNR,J_ANR,35,30,"shinR"},{J_ANR,J_TOR,35,15,"footR"} };
    S->nc=0; S->nsm=0;
    for(unsigned c=0;c<sizeof CAP/sizeof CAP[0]&&S->nc<CA_MAXCAP;c++){ int a=CAP[c].a, b=CAP[c].b;
        if(!ok[a]||!ok[b]) continue;
        memcpy(S->ca[S->nc],J[a],12); memcpy(S->cb[S->nc],J[b],12);
        S->cr[S->nc]=(float)CAP[c].rb*0.001f*H; S->nc++;
        /* ⭐ THE WHOLE LIMB, NOT ITS TWO ENDS: a shin through a cabinet has its knee over the top and its
         * ankle at the face. A sample every 0.05 u, both ends included. */
        float dx=J[b][0]-J[a][0], dy=J[b][1]-J[a][1], dz=J[b][2]-J[a][2];
        int n=(int)ceil(sqrtf(dx*dx+dy*dy+dz*dz)/0.05f);
        if(n<1) n=1;
        if(n>40) n=40;
        for(int i=0;i<=n&&S->nsm<CA_MAXSMP;i++){ float u=(float)i/(float)n; float*q=S->sm[S->nsm];
            q[0]=J[a][0]+dx*u; q[1]=J[a][1]+dy*u; q[2]=J[a][2]+dz*u; q[3]=(float)CAP[c].rw*0.001f*H;
            S->smn[S->nsm++]=CAP[c].nm; } } }
void clipAuditPoint(int slot,const char*what,float x,float y,float z){
    if(g_caOn<0) caInit();
    if(g_caOn<=0||g_scnT<0.f) return;
    if(slot<0||slot>=CA_SLOTS){ static int said=0;
        if(!said){ said=1; printf("[clipaudit] clipAuditPoint: slot %d is not a cast slot (0..%d) -- ignored\n",slot,CA_SLOTS-1); fflush(stdout); }
        return; }
    if(!what) what="point";
    caLatch();
    CaAtt*A=NULL;
    for(int i=0;i<g_caNAtt;i++)
        if(g_caAtt[i].slot==slot&&!strncmp(g_caAtt[i].what,what,sizeof g_caAtt[i].what-1)){ A=&g_caAtt[i]; break; }
    if(!A){
        if(g_caNAtt>=CA_MAXATT){ static int said2=0;
            if(!said2){ said2=1; printf("[clipaudit] %d attachment points in one frame is the cap (CA_MAXATT) -- the rest are NOT tested\n",CA_MAXATT); fflush(stdout); }
            return; }
        A=&g_caAtt[g_caNAtt++]; A->slot=slot; snprintf(A->what,sizeof A->what,"%s",what); }
    A->p[0]=x; A->p[1]=y; A->p[2]=z; }
void clipAuditBox(int slot,const char*what,const float*model,const float*lo,const float*hi){
    if(g_caOn<0) caInit();
    if(g_caOn<=0||g_scnT<0.f||!model||!lo||!hi) return;
    for(int c=0;c<8;c++){ float l[3]={(c&1)?hi[0]:lo[0],(c&2)?hi[1]:lo[1],(c&4)?hi[2]:lo[2]};
        char nm[28]; snprintf(nm,sizeof nm,"%.20s.c%d",what?what:"box",c);
        clipAuditPoint(slot,nm,model[0]*l[0]+model[4]*l[1]+model[8]*l[2]+model[12],
                               model[1]*l[0]+model[5]*l[1]+model[9]*l[2]+model[13],
                               model[2]*l[0]+model[6]*l[1]+model[10]*l[2]+model[14]); } }
static float caSegDist(const float*a,const float*b,const float*p){
    float ab[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]}, ap[3]={p[0]-a[0],p[1]-a[1],p[2]-a[2]};
    float l=ab[0]*ab[0]+ab[1]*ab[1]+ab[2]*ab[2];
    float t=(l>1e-9f)?(ap[0]*ab[0]+ap[1]*ab[1]+ap[2]*ab[2])/l:0.f;
    if(t<0.f) t=0.f;
    if(t>1.f) t=1.f;
    float d[3]={ap[0]-ab[0]*t,ap[1]-ab[1]*t,ap[2]-ab[2]*t};
    return sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); }
static float caTest(int self,const float*p,float rad,int bodies,char*into){
    int kind=0; float best;
    /* ⭐ ep 12: over the furniture this body rests on, its top (less the sink) is the floor -- see caSupport */
    float st9; const char*nm9;
    if(caSoftBed(self,p,&st9,&nm9)){ best=(st9>-1e8f)?(st9-CA_SINK)-p[1]:0.f-p[1]; snprintf(into,12,"%s",(st9>-1e8f)?"bedding":"floor"); }
    else { best=caGridDepth(&g_caG,p[0],p[1],p[2],rad,&kind);
           snprintf(into,12,"%s",kind?"obstacle":"floor"); }
    /* ⭐ ep 11: the half-pipes are not in the grid: each answers for itself, from the same numbers it rides on */
    for(int i=0;i<g_nPipe;i++){ int pk=0; float d=pipeDepth(&g_pipe[i],p,rad,pipeRides(self,i),&pk);
        if(d>best){ best=d; snprintf(into,12,"%s",pk?"pipe box":"pipe"); } }
    /* ⭐ against the MEASURED shapes (the contact pass's): a fixed-fraction capsule called an arm resting on a belly
     * 0.1 u deep. The old capsules stand in only for a body the contact pass never measured. */
    if(bodies){ int who=-1; float pen=ctBodyPen(self,p,rad,0,NULL,&who);
        if(who>=0&&pen>best){ best=pen; snprintf(into,12,"slot %d",(short)who); } }
    for(int o=0;bodies&&o<CA_SLOTS;o++){ if(o==self||!g_caS[o].drawn||g_ctBody[o].n) continue;
        const CaSlot*B=&g_caS[o];
        for(int c=0;c<B->nc;c++){ float pen=B->cr[c]-caSegDist(B->ca[c],B->cb[c],p);
            if(pen>best){ best=pen; snprintf(into,12,"slot %d",o); } } }
    return best; }
static void caNote(CaSlot*S,int slot,const char*what,const char*into,float d,const float*p){
    if(d>S->lw){ S->lw=d; S->lt=g_caT; memcpy(S->lp,p,12);
        snprintf(S->lwhat,sizeof S->lwhat,"%s",what); snprintf(S->linto,sizeof S->linto,"%s",into); }
    if(d>S->tw){ S->tw=d; S->tt=g_caT; S->tl=g_caLine; memcpy(S->tp,p,12);
        snprintf(S->twhat,sizeof S->twhat,"%s",what); snprintf(S->tinto,sizeof S->tinto,"%s",into); }
    if(g_caCsv) fprintf(g_caCsv,"%.3f,%d,%d,%s,%s,%s,%.4f,%.3f,%.3f,%.3f\n",
                        g_caT,g_caLine,slot,S->label,what,into,d,p[0],p[1],p[2]); }
static void caLineReport(void){
    for(int s=0;s<CA_SLOTS;s++){ CaSlot*S=&g_caS[s];
        if(g_caLine>=0&&S->lb>0)
            printf("[clipaudit] line %d slot %d %s: %d bad frames, worst %.3f u (%s into %s) at t=%.2f (%.2f,%.2f,%.2f) [%d frames tested]\n",
                   g_caLine,s,S->label,S->lb,S->lw,S->lwhat,S->linto,S->lt,S->lp[0],S->lp[1],S->lp[2],S->lf);
        S->lf=S->lb=0; S->lw=0.f; S->lwhat[0]=S->linto[0]=0; }
    fflush(stdout);
    if(g_caCsv) fflush(g_caCsv); }
static void caSceneEnd(const char*why){
    if(!g_caInScene) return;
    caLineReport();
    int any=0, bad=0, tot=0;
    printf("[clipaudit] TOTAL (%s) scene %d, %d lines, %.1fs:\n",why,g_caScenes,g_scnN,g_scnLen);
    for(int s=0;s<CA_SLOTS;s++){ CaSlot*S=&g_caS[s];
        if(!S->tf) continue;
        any=1; tot+=S->tf; bad+=S->tb;
        if(S->tb) printf("[clipaudit] TOTAL slot %d %s: %d frames tested, %d bad, worst %.3f u (%s into %s) line %d t=%.2f (%.2f,%.2f,%.2f)\n",
                         s,S->label,S->tf,S->tb,S->tw,S->twhat,S->tinto,S->tl,S->tt,S->tp[0],S->tp[1],S->tp[2]);
        else printf("[clipaudit] TOTAL slot %d %s: %d frames tested, 0 bad\n",s,S->label,S->tf); }
    if(!any) printf("[clipaudit] TOTAL: NO BODY WAS TESTED -- nothing drew a cast body while the scene played\n");
    else printf("[clipaudit] TOTAL %s: %d bad of %d body-frames\n",bad?"CLIPPING":"CLEAN",bad,tot);
    fflush(stdout);
    if(g_caCsv) fflush(g_caCsv);
    g_caInScene=0; g_caLine=-1; }
static void caSceneBegin(void){
    g_caInScene=1; g_caLine=-1; g_caScenes++;
    for(int s=0;s<CA_SLOTS;s++){ CaSlot*S=&g_caS[s];
        S->lf=S->lb=S->tf=S->tb=0; S->lw=S->tw=0.f; S->tl=-1;
        S->lwhat[0]=S->linto[0]=S->twhat[0]=S->tinto[0]=0; }
    printf("[clipaudit] ON for scene %d (%d lines, %.1fs): every cast body + clipAuditPoint() vs the floor, %s, and each other; bad past %.2f u\n",
           g_caScenes,g_scnN,g_scnLen,g_caGridSay,CA_TOL);
    fflush(stdout); }
static void caClearFrame(void){
    for(int s=0;s<CA_SLOTS;s++) g_caS[s].drawn=0;
    g_caNAtt=0; g_caFed=0; }
/* THE FRAME: every body and point fed since the last one, tested together (a body can only be
 * inside another that was drawn in the same frame). Called once, just before the swap. */
static void clipAuditFrame(void){
    if(g_caOn<0) caInit();
    if(g_caOn<=0) return;
    caGridSync();
    if(g_caFed){
    { float t=g_caFT; int li=g_caFLine;
      if(!g_caInScene||t+0.25f<g_caLastT){ if(g_caInScene) caSceneEnd("scene restarted"); caSceneBegin(); }
      g_caLastT=t; g_caT=t;
      /* ⛔ AND A SILENCE BELONGS TO WHOEVER JUST SPOKE. scnStageLine() is -1 in a beat while the
       * staging (held8) keeps the last line's marks, so every beat's frames went to "line -1" and
       * were never reported: the fixture's TOTAL had 47 bad frames and its lines only 36. */
      if(li<0&&g_caLine>=0) li=g_caLine;
      if(li!=g_caLine){ caLineReport(); g_caLine=li; } }
    for(int s=0;s<CA_SLOTS;s++){ CaSlot*S=&g_caS[s];
        int hasAtt=0;
        for(int a=0;a<g_caNAtt;a++) if(g_caAtt[a].slot==s){ hasAtt=1; break; }
        if(!S->drawn&&!hasAtt) continue;
        if(!S->drawn&&!S->label[0]) snprintf(S->label,sizeof S->label,"(points only)");
        S->lf++; S->tf++;
        int bad=0; char into[12];
        for(int i=0;S->drawn&&i<S->np;i++){ float d=caTest(s,S->pt[i].p,0.f,1,into);
            if(d>CA_TOL){ bad=1; caNote(S,s,S->pt[i].nm,into,d,S->pt[i].p); } }
        for(int i=0;S->drawn&&i<S->nsm;i++){ float d=caTest(s,S->sm[i],S->sm[i][3],0,into);
            if(d>CA_TOL){ bad=1; caNote(S,s,S->smn[i],into,d,S->sm[i]); } }
        for(int a=0;a<g_caNAtt;a++){ if(g_caAtt[a].slot!=s) continue;
            float d=caTest(s,g_caAtt[a].p,0.f,1,into);
            if(d>CA_TOL){ bad=1; caNote(S,s,g_caAtt[a].what,into,d,g_caAtt[a].p); } }
        if(bad){ S->lb++; S->tb++; } } }
    if(g_scnT<0.f&&g_caInScene) caSceneEnd("scene done");   /* after the last frame has been tested */
    caClearFrame(); }
static void clipAuditExit(void){
    static int done=0;
    if(done||g_caOn<=0) return;
    done=1;
    if(g_caInScene) caSceneEnd("exit");
    else if(!g_caScenes){ printf("[clipaudit] exit: the audit was ON but no scene frame was ever tested\n"); fflush(stdout); }
    if(g_caCsv){ fclose(g_caCsv); g_caCsv=NULL; } }

static void capInit(void);
static int scnLoad(const char*name){
    { static int cap9=0; if(!cap9){ cap9=1; capInit(); } }   /* ⭐ (10-23) the captions' typeface. Here since 11-02: a game with no scene never reads a system font */
    char pth[512]; snprintf(pth,sizeof pth,"scenes/%s.scn",name);
    FILE*f=fopen(pth,"rb");
    if(!f){ printf("[scene] %s NOT FOUND\n",pth); fflush(stdout); return 0; }
    g_scnN=0; g_scnLen=0.f; g_scnCapN=0; g_scnShpN=0; g_scnShpUsed=0; g_trSecN=0; g_trNoseN=0; memset(g_trStamp,0,sizeof g_trStamp);   /* ⭐ (11-06) and the lines' shape tracks */
    char wav[128]={0}; static char ln[32768];   /* ⭐ ep 10: 4096 -> 8192 -- four mark-list paths on one stage record; (10-23) -> 32768: an 84 s line's mouth envelope */
    while(fgets(ln,sizeof ln,f)){
        /* ⛔ ep 10: a record longer than the buffer used to be split in two, silently -- its tail lost */
        if(!strchr(ln,'\n')&&!feof(f)){ int c9; while((c9=fgetc(f))!=EOF&&c9!='\n'){ }
            printf("[scene] %s: a line longer than %d chars was CUT -- its tail is dropped\n",pth,(int)sizeof ln-1); fflush(stdout); }
        if(!strncmp(ln,"scene ",6)){ int nl=0; sscanf(ln,"scene %*d %127s %f %d",wav,&g_scnLen,&nl);
            snprintf(g_scnWho[0],16,"REVA"); snprintf(g_scnWho[1],16,"KAELI");
            snprintf(g_scnWho[2],16,"SUSAN"); snprintf(g_scnWho[3],16,"-"); }
        else if(!strncmp(ln,"cast ",5))
            sscanf(ln,"cast %15s %15s %15s %15s %15s",g_scnWho[0],g_scnWho[1],g_scnWho[2],g_scnWho[3],g_scnWho[4]);
        else if(!strncmp(ln,"line ",5)&&g_scnN<SCN_MAXL){
            ScnLine*L=&g_scnL[g_scnN];
            int used=0;
            if(sscanf(ln,"line %d %f %f %n",&L->spk,&L->t0,&L->dur,&used)>=3&&used>0){
                const char*t=ln+used; int k9=0;
                while(*t&&*t!='\n'&&*t!='\r'&&k9<(int)sizeof L->text-1) L->text[k9++]=*t++;
                L->text[k9]=0; L->nEnv=0; L->nVis=0; L->capA=g_scnCapN; L->capN=0;
                /* staging defaults, so a .scn written before staging existed still plays */
                L->pgap=1.15f; L->pyaw=180.f; L->pside=0.12f;
                L->p2gap=1.45f; L->p2yaw=214.f; L->p2side=-1.25f;
                L->p2look=0; L->p2brow=0.f; L->p2has=0; L->p2clip[0]=0;
                /* ⭐ ep 7: slot 3 stands further out than slot 2 by default, on the far
                 * side, so a four-hander does not pile the last two on top of each other. */
                L->p3gap=1.10f; L->p3yaw=196.f; L->p3side=-2.60f;
                L->p3has=0; L->p3clip[0]=0;
                L->scrHas=0; L->camHas=0;
                L->hprop[0]=L->pprop[0]=L->p2prop[0]=0;
                L->hprop2[0]=L->pprop2[0]=L->p2prop2[0]=0;
                L->pmark[0]=L->p2mark[0]=L->p3mark[0]=L->hmark[0]=0;
                /* ⭐ ep 10: no path, no speed, no tracking lens -- and a line with no text is an ACTION line */
                for(int k10=0;k10<4;k10++){ L->path[k10][0]=0; L->spd[k10]=-1.f; L->loop[k10]=L->pseg[k10]=L->pnext[k10]=-1;
                    L->pipe[k10][0]=L->pipeAir[k10][0]=0; }   /* ⭐ ep 11 */
                L->trkN=L->trkHas=L->panN=L->panHas=L->hldN=L->hldHas=0;
                L->movHas=L->orbN=L->orbHas=L->orbAbs=L->trkAbs=L->shkHas=0; L->secs=-1.f; L->set[0]=0; L->atHas=0;
                L->home[0]=0; for(int o12=0;o12<4;o12++){ L->outfit[o12][0]=0; L->roll[o12][0]=0; L->eyes[o12][0]=0; }   /* ⭐ ep 11 */
                L->silent=(L->text[0]==0);
                L->hlook=L->plook=1; L->shot=SHOT_OTS;
                L->hbrow=L->pbrow=-9.f; L->hgt=0.f; L->step=-1.f; L->tod=-1.f; L->tone=-1.f; L->feed=-1.f; L->fish=-1.f; L->snap=0; L->camRoll=L->shut=L->sepia=L->grain=-9999.f; L->focusSaid[0]=0;
                L->reverse=0; L->jawgain=-1.f; L->to=-1; memset(L->face,0,sizeof L->face);
                L->hover[0]=L->pover[0]=L->p2over[0]=0;
                L->htrick[0]=L->ptrick[0]=L->p2trick[0]=L->p3trick[0]=0;   /* ⭐ ep 10 */
                L->hpuff[0]=L->ppuff[0]=L->p2puff[0]=L->p3puff[0]=0;       /* ⭐ ep 12 */
                snprintf(L->hclip,sizeof L->hclip,"z_upright");
                snprintf(L->pclip,sizeof L->pclip,"z_upright");
                g_scnN++; } }
        else if(!strncmp(ln,"cap ",4)&&g_scnN>0){   /* ⭐ (10-23) a caption page of the line just read */
            ScnLine*L=&g_scnL[g_scnN-1]; int used=0; float a9=0.f,b9=0.f;
            if(g_scnCapN<SCN_MAXCAP&&sscanf(ln,"cap %f %f %n",&a9,&b9,&used)>=2&&used>0){
                ScnCap*C=&g_scnCap[g_scnCapN]; const char*t=ln+used; int k9=0;
                while(*t&&*t!='\n'&&*t!='\r'&&k9<(int)sizeof C->text-1) C->text[k9++]=*t++;
                C->text[k9]=0; C->t0=a9; C->t1=b9;
                if(!L->capN) L->capA=g_scnCapN;
                g_scnCapN++; L->capN++; } }
        else if(!strncmp(ln,"vis ",4)&&g_scnN>0){
            ScnLine*L=&g_scnL[g_scnN-1];
            int n9=0, used=0; sscanf(ln,"vis %d %n",&n9,&used);
            const char*t=ln+used; L->nVis=0;
            while(*t&&L->nVis<SCN_MAXE&&L->nVis<n9){
                while(*t==' ') t++;
                if(*t<'0'||*t>'9') break;
                int v9=0; while(*t>='0'&&*t<='9'){ v9=v9*10+(*t-'0'); t++; }
                L->vis[L->nVis++]=(unsigned char)(v9>255?255:v9); } }
        else if(!strncmp(ln,"tractsec ",9)){   /* ⭐ (11-06) where a section of the speaker's airway lies in her (engine/scene.inc, tractDraw) */
            int k9=-1, used=0; char bn9[32]={0};
            if(sscanf(ln,"tractsec %d %31s %n",&k9,bn9,&used)>=2&&used>0&&k9>=0&&k9<TR_NSEC){
                const char*t=ln+used; float s9[9]; int got=0;
                for(;got<9;got++){ char*e9=0; const float x9=strtof(t,&e9); if(e9==t) break; s9[got]=x9; t=e9; }
                if(got==9){ memcpy(g_trSec[k9],s9,sizeof s9); snprintf(g_trBone,sizeof g_trBone,"%s",bn9); if(k9+1>g_trSecN) g_trSecN=k9+1; }
                else { printf("[scene] %s: tractsec %d has %d of its 9 numbers\n",pth,k9,got); fflush(stdout); } } }
        else if(!strncmp(ln,"tractnose ",10)){   /* ...and her nose's passage */
            int n9=0, used=0;
            if(sscanf(ln,"tractnose %d %n",&n9,&used)>=1&&used>0){ const char*t=ln+used; g_trNoseN=0;
                for(int i=0;i<n9&&i<TR_NOSE;i++){ float p9[3]; int got=0;
                    for(;got<3;got++){ char*e9=0; const float x9=strtof(t,&e9); if(e9==t) break; p9[got]=x9; t=e9; }
                    if(got<3) break;
                    memcpy(g_trNose[g_trNoseN++],p9,sizeof p9); } } }
        else if(!strncmp(ln,"shp ",4)&&g_scnN>0){   /* ⭐ (11-06) a named shape on the line's speaker, 30 a second (scene.inc: ScnShp) */
            char nm9[32]={0}; int n9=0, used=0;
            if(sscanf(ln,"shp %31s %d %n",nm9,&n9,&used)>=2&&used>0&&n9>0){
                if(g_scnShpN>=SCN_SHPMAX||g_scnShpUsed+n9>SCN_SHPPOOL){
                    printf("[scene] %s: line %d's '%s' track REFUSED -- a scene holds %d tracks and %d values\n",pth,g_scnN-1,nm9,SCN_SHPMAX,SCN_SHPPOOL); fflush(stdout); }
                else { ScnShp*S9=&g_scnShp[g_scnShpN]; S9->line=g_scnN-1; S9->at=g_scnShpUsed; S9->n=0; snprintf(S9->name,sizeof S9->name,"%s",nm9);
                    const char*t=ln+used;
                    while(*t&&S9->n<n9){
                        while(*t==' ') t++;
                        const int neg=(*t=='-'); if(neg) t++;
                        if(*t<'0'||*t>'9') break;
                        int v9=0; while(*t>='0'&&*t<='9'){ if(v9<100000) v9=v9*10+(*t-'0'); t++; }
                        if(v9>32767) v9=32767;
                        g_scnShpV[S9->at+S9->n++]=(short)(neg?-v9:v9); }
                    if(S9->n<n9){ printf("[scene] %s: line %d's '%s' track says %d values and has %d\n",pth,g_scnN-1,nm9,n9,S9->n); fflush(stdout); }
                    g_scnShpUsed+=S9->n; if(S9->n>0) g_scnShpN++; } } }
        else if(!strncmp(ln,"stage ",6)&&g_scnN>0){
            ScnLine*L=&g_scnL[g_scnN-1];
            const char*q;
            if((q=strstr(ln,"p2gap=")))  { L->p2gap =(float)atof(q+6); L->p2has=1; }
            if((q=strstr(ln,"p2yaw=")))  { L->p2yaw =(float)atof(q+6); L->p2has=1; }
            if((q=strstr(ln,"p2side="))) { L->p2side=(float)atof(q+7); L->p2has=1; }
            if((q=strstr(ln,"p3gap=")))  { L->p3gap =(float)atof(q+6); L->p3has=1; }
            if((q=strstr(ln,"p3yaw=")))  { L->p3yaw =(float)atof(q+6); L->p3has=1; }
            if((q=strstr(ln,"p3side="))) { L->p3side=(float)atof(q+7); L->p3has=1; }
            /* ⭐ ep 8: screen=x,y,z,yaw,pitch,w,h -- where Holly is, and which way up */
            /* ⭐ cam=ex,ey,ez,ax,ay,az[,fov] -- an AUTHORED lens for this line, or
             * cam=off to hand it back to the shot ladder. Not sticky, on purpose. */
            if((q=strstr(ln,"cam="))){
                if(!strncmp(q+4,"off",3)) L->camHas=2;
                else { float c7[7]={0,0,0,0,0,0,-1.f};
                    int n8=sscanf(q+4,"%f,%f,%f,%f,%f,%f,%f",
                                  &c7[0],&c7[1],&c7[2],&c7[3],&c7[4],&c7[5],&c7[6]);
                    if(n8>=6){ for(int k8=0;k8<7;k8++) L->cam[k8]=c7[k8]; L->camHas=1; }
                    else printf("[cam] line: need ex,ey,ez,ax,ay,az[,fov]\n"); } }
            if((q=strstr(ln,"screen="))){
                float s7[7]={0,0,0,0,0,0,0};
                int n7=sscanf(q+7,"%f,%f,%f,%f,%f,%f,%f",&s7[0],&s7[1],&s7[2],&s7[3],&s7[4],&s7[5],&s7[6]);
                if(n7>=3){ for(int k7=0;k7<7;k7++) L->scr[k7]=s7[k7]; L->scrHas=n7; } }
            if((q=strstr(ln,"p2look=")))   L->p2look=atoi(q+7);
            if((q=strstr(ln,"p2brow=")))   L->p2brow=(float)atof(q+7);
            if((q=strstr(ln,"hprop="))){ int k=0; q+=6;
                while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<PROP_SPEC-1) L->hprop[k++]=*q++;
                L->hprop[k]=0; if(*q&&*q!=' '&&*q!='\n'&&*q!='\r') specTooLong(L->hprop); }
            /* ⛔ `pprop=` matches inside `p2prop=` -- the same trap as pgap/pclip. */
            { q=strstr(ln,"pprop="); while(q&&q>ln&&q[-1]>='0'&&q[-1]<='9') q=strstr(q+1,"pprop=");
              if(q){ int k=0; q+=6;
                while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<PROP_SPEC-1) L->pprop[k++]=*q++;
                L->pprop[k]=0; if(*q&&*q!=' '&&*q!='\n'&&*q!='\r') specTooLong(L->pprop); } }
            /* ⭐ THE SECOND HAND (ep 5). ⛔ THE `=` IS WHAT KEEPS THESE APART: the keys above
             * are searched WITH their `=`, so "hprop=" cannot match inside "hprop2=" (a `2`
             * sits where the `=` must be) and "hprop2=" cannot match inside "hprop=". The
             * digit guard is still carried on the p-forms, exactly as above. */
            if((q=strstr(ln,"hprop2="))){ int k=0; q+=7;
                while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<PROP_SPEC-1) L->hprop2[k++]=*q++;
                L->hprop2[k]=0; if(*q&&*q!=' '&&*q!='\n'&&*q!='\r') specTooLong(L->hprop2); }
            { q=strstr(ln,"pprop2="); while(q&&q>ln&&q[-1]>='0'&&q[-1]<='9') q=strstr(q+1,"pprop2=");
              if(q){ int k=0; q+=7;
                while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<PROP_SPEC-1) L->pprop2[k++]=*q++;
                L->pprop2[k]=0; if(*q&&*q!=' '&&*q!='\n'&&*q!='\r') specTooLong(L->pprop2); } }
            if((q=strstr(ln,"p2prop2="))){ int k=0; q+=8;
                while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<PROP_SPEC-1) L->p2prop2[k++]=*q++;
                L->p2prop2[k]=0; if(*q&&*q!=' '&&*q!='\n'&&*q!='\r') specTooLong(L->p2prop2); }
            /* ⛔ `pmark=` matches inside `p2mark=` -- the pgap/pclip/pprop trap again. */
            { q=strstr(ln,"pmark="); while(q&&q>ln&&q[-1]>='0'&&q[-1]<='9') q=strstr(q+1,"pmark=");
              if(q){ int k=0; q+=6;
                while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<31) L->pmark[k++]=*q++;
                L->pmark[k]=0; } }
            if((q=strstr(ln,"p2mark="))){ int k=0; q+=7;
                while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<31) L->p2mark[k++]=*q++;
                L->p2mark[k]=0; }
            /* ⭐ ep 9: and slot 3 -- Kickflip rolls to his marks the way the others walk to theirs */
            if((q=strstr(ln,"p3mark="))){ int k=0; q+=7;
                while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<31) L->p3mark[k++]=*q++;
                L->p3mark[k]=0; }
            if((q=strstr(ln,"hmark="))){ int k=0; q+=6;
                while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<31) L->hmark[k++]=*q++;
                L->hmark[k]=0; }
            /* ⭐ ep 10: PATHS, their SPEEDS, THE TRACKING LENS and an ACTION line's dur=. Every key is matched
             * WITH its leading space (a `stage` record is space-separated), so `ppath=` cannot hit inside
             * `p2path=` and `pspd=` cannot hit inside `p2spd=`. */
            { static const char*const PK10[4]={" hpath="," ppath="," p2path="," p3path="};
              static const char*const SK10[4]={" hspd="," pspd="," p2spd="," p3spd="};
              static const char*const LK10[4]={" hloop="," ploop="," p2loop="," p3loop="};
              for(int s10=0;s10<4;s10++){
                  if((q=strstr(ln,PK10[s10]))){ int k=0; q+=strlen(PK10[s10]);
                      while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<511) L->path[s10][k++]=*q++;
                      L->path[s10][k]=0;
                      if(*q&&*q!=' '&&*q!='\n'&&*q!='\r'){ printf("[path] line %d: %s value longer than 511 chars was CUT -- name it with a set `route` and say @name\n",g_scnN-1,PK10[s10]+1); fflush(stdout); } }
                  if((q=strstr(ln,SK10[s10]))) L->spd[s10]=(float)atof(q+strlen(SK10[s10]));
                  if((q=strstr(ln,LK10[s10]))) L->loop[s10]=atoi(q+strlen(LK10[s10])); } }
            if((q=strstr(ln," feed="))){ L->feed=(float)atoi(q+6); if(L->feed<0.f)L->feed=0.f; if(L->feed>2.f)L->feed=2.f; }
            if((q=strstr(ln," fish="))){ L->fish=(float)atof(q+6); if(L->fish<0.f)L->fish=0.f; if(L->fish>1.f)L->fish=1.f; }
            if((q=strstr(ln," camroll="))) L->camRoll=(float)atof(q+9);   /* (step 25) the lens and film's look, per line */
            if((q=strstr(ln," focus="))){ int k9=0; q+=7; while(q[k9]&&q[k9]!=' '&&q[k9]!='\t'&&q[k9]!='\n'&&q[k9]!='\r'&&k9<23){ L->focusSaid[k9]=q[k9]; k9++; } L->focusSaid[k9]=0; }
            if((q=strstr(ln," shutter="))) L->shut=(float)atof(q+9);
            if((q=strstr(ln," sepia="))) L->sepia=(float)atof(q+7);
            if((q=strstr(ln," grain="))) L->grain=(float)atof(q+7);
            if((q=strstr(ln," camhold="))){ q+=9;
                if(!strncmp(q,"off",3)) L->hldHas=2;
                else { float h12[5]={0.f,1.f,-1.f,1.4f,0.f}; int dummy12=0;
                    int n12=lensNums(q,h12,5,-1,&dummy12);
                    if(n12>=2){ memcpy(L->hld,h12,sizeof h12); L->hldN=n12; L->hldHas=1; }
                    else printf("[lens] line %d: camhold needs filmer,subject[,fov[,aimh]]\n",g_scnN-1); } }
            if((q=strstr(ln," camtrack="))){ q+=10;
                if(!strncmp(q,"off",3)) L->trkHas=2;
                else { float t10[6]={0.f,6.f,2.f,0.f,-1.f,1.6f};
                    int n10=lensNums(q,t10,6,2,&L->trkAbs);   /* ⭐ ep 11: height `=h` is absolute world y */
                    if(n10>=5){ memcpy(L->trk,t10,sizeof t10); L->trkN=n10; L->trkHas=1; }
                    else printf("[lens] line %d: camtrack needs slot,dist,height,azim,fov[,aimh]\n",g_scnN-1); } }
            /* ⭐ ep 11: cammove= (the END; the line's cam= is the start), camorbit=, camsecs=, camshake=. Leading
             * spaces, like camtrack: none of them can hit inside another key, and no older key sits inside them
             * (`cam=` is not in `cammove=`; the shake's rate is a VALUE, never an `hz=` key -- that is the hero's z). */
            if((q=strstr(ln," cammove="))){ q+=9;
                float m11[7]={0.f,0.f,0.f,0.f,0.f,0.f,-1.f};
                if(lensNums(q,m11,7,-1,NULL)>=6){ memcpy(L->mov,m11,sizeof m11); L->movHas=1; }
                else printf("[lens] line %d: cammove needs ex,ey,ez,ax,ay,az[,fov]\n",g_scnN-1); }
            if((q=strstr(ln," camorbit="))){ q+=10;
                if(!strncmp(q,"off",3)) L->orbHas=2;
                else { float o11[7]={0.f,6.f,2.f,0.f,0.f,-1.f,1.6f};
                    int n11=lensNums(q,o11,7,2,&L->orbAbs);
                    if(n11>=5){ memcpy(L->orb,o11,sizeof o11); L->orbN=n11; L->orbHas=1; }
                    else printf("[lens] line %d: camorbit needs slot,radius,height,deg0,deg1[,fov[,aimh]]\n",g_scnN-1); } }
            if((q=strstr(ln," camat="))){ float a12[3]={0.f,0.f,0.f};   /* ⭐ 10-22 */
                if(sscanf(q+7,"%f,%f,%f",&a12[0],&a12[1],&a12[2])==3){ memcpy(L->at,a12,12); L->atHas=1; }
                else printf("[lens] line %d: camat needs x,y,z\n",g_scnN-1); }
            if((q=strstr(ln," home="))){ int k12=0; q+=6;   /* ⭐ 10-22: the home set changes */
                while(*q&&*q!='\n'&&*q!='\r'&&*q!=' '&&k12<47) L->home[k12++]=*q++;
                L->home[k12]=0; }
            { static const char*const OK12[4]={" houtfit="," poutfit="," p2outfit="," p3outfit="};
              for(int o12=0;o12<4;o12++) if((q=strstr(ln,OK12[o12]))){ int k12=0; q+=strlen(OK12[o12]);
                  while(*q&&*q!='\n'&&*q!='\r'&&*q!=' '&&k12<23) L->outfit[o12][k12++]=*q++;
                  L->outfit[o12][k12]=0; } }
            { static const char*const EK12[4]={" heyes="," peyes="," p2eyes="," p3eyes="};   /* (step 23) eyes that glow */
              for(int o12=0;o12<4;o12++) if((q=strstr(ln,EK12[o12]))){ int k12=0; q+=strlen(EK12[o12]);
                  while(*q&&*q!='\n'&&*q!='\r'&&*q!=' '&&k12<27) L->eyes[o12][k12++]=*q++;
                  L->eyes[o12][k12]=0; } }
            { static const char*const RK12[4]={" hroll="," proll="," p2roll="," p3roll="};
              for(int o12=0;o12<4;o12++) if((q=strstr(ln,RK12[o12]))){ int k12=0; q+=strlen(RK12[o12]);
                  while(*q&&*q!='\n'&&*q!='\r'&&*q!=' '&&k12<95) L->roll[o12][k12++]=*q++;
                  L->roll[o12][k12]=0; } }
            if((q=strstr(ln," set="))){ int k12=0; q+=5;   /* ⭐ 10-22: a cutaway */
                while(*q&&*q!='\n'&&*q!='\r'&&*q!=' '&&k12<47) L->set[k12++]=*q++;
                L->set[k12]=0; }
            if((q=strstr(ln," camsecs=")))  L->secs=(float)atof(q+9);
            if((q=strstr(ln," camshake="))){ q+=10;
                float s11[2]={0.f,1.5f};
                if(!strncmp(q,"off",3)||lensNums(q,s11,2,-1,NULL)>=1){ L->shk[0]=s11[0]; L->shk[1]=s11[1]; L->shkHas=1; }
                else printf("[lens] line %d: camshake needs amp[,hz]\n",g_scnN-1); }
            if((q=strstr(ln," campan="))){ q+=8;
                if(!strncmp(q,"off",3)) L->panHas=2;
                else { float p10[2]={0.f,1.6f};
                    int n10=sscanf(q,"%f,%f",&p10[0],&p10[1]);
                    if(n10>=1){ L->pan[0]=p10[0]; L->pan[1]=p10[1]; L->panN=n10; L->panHas=1; }
                    else printf("[lens] line %d: campan needs slot[,aimh]\n",g_scnN-1); } }
            if(strstr(ln," dur=")) L->silent=1;
            /* ⭐ ep 11: THE HALF-PIPE -- hpipe=<name>[,<air>] and hair=<clip>[,<clip>...] per slot. Matched WITH the
             * leading space like the ep 10 keys: ` pair=` cannot hit inside ` p2air=`, nor ` ppipe=` in ` p2pipe=`. */
            { static const char*const PP11[4]={" hpipe="," ppipe="," p2pipe="," p3pipe="};
              static const char*const PA11[4]={" hair="," pair="," p2air="," p3air="};
              for(int s11=0;s11<4;s11++){
                  if((q=strstr(ln,PP11[s11]))){ int k=0; q+=strlen(PP11[s11]);
                      while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<47) L->pipe[s11][k++]=*q++;
                      L->pipe[s11][k]=0; }
                  if((q=strstr(ln,PA11[s11]))){ int k=0; q+=strlen(PA11[s11]);
                      while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<127) L->pipeAir[s11][k++]=*q++;
                      L->pipeAir[s11][k]=0;
                      if(*q&&*q!=' '&&*q!='\n'&&*q!='\r'){ printf("[pipe] line %d: %s longer than 127 chars was CUT\n",g_scnN-1,PA11[s11]+1); fflush(stdout); } } } }
            /* ⭐ hover= / pover= / p2over= (09-11). `pover=` sits inside `p2over=`: the digit guard. */
            if((q=strstr(ln,"hover="))){ int k=0; q+=6;
                while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<31) L->hover[k++]=*q++;
                L->hover[k]=0; }
            { q=strstr(ln,"pover="); while(q&&q>ln&&q[-1]>='0'&&q[-1]<='9') q=strstr(q+1,"pover=");
              if(q){ int k=0; q+=6;
                while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<31) L->pover[k++]=*q++;
                L->pover[k]=0; } }
            if((q=strstr(ln,"p2over="))){ int k=0; q+=7;
                while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<31) L->p2over[k++]=*q++;
                L->p2over[k]=0; }
            if((q=strstr(ln,"p2prop="))){ int k=0; q+=7;
                while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<PROP_SPEC-1) L->p2prop[k++]=*q++;
                L->p2prop[k]=0; if(*q&&*q!=' '&&*q!='\n'&&*q!='\r') specTooLong(L->p2prop); }
            if((q=strstr(ln,"p2clip="))){ int k=0; q+=7;
                while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<31) L->p2clip[k++]=*q++;
                L->p2clip[k]=0; }
            if((q=strstr(ln,"p3clip="))){ int k=0; q+=7;
                while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<31) L->p3clip[k++]=*q++;
                L->p3clip[k]=0; }
            /* ⛔ `pgap=` matches inside `p2gap=`. Skip any hit whose preceding character is
             * a digit -- the same trap that let `pclip=` set the HERO's clip. */
            { q=strstr(ln,"pgap="); while(q&&q>ln&&q[-1]>='0'&&q[-1]<='9') q=strstr(q+1,"pgap=");
              if(q) L->pgap=(float)atof(q+5); }
            /* ⭐ focus=hero|partner -- who the LENS is on, when it is not the speaker. */
            L->to=-1;   /* ⭐ (10-23) ` to=<slot>`: leading space, nothing else ends in "to" */
            if((q=strstr(ln," to="))&&numAt(q+4)) L->to=atoi(q+4);
            L->focus=-1;
            if((q=strstr(ln,"focus="))){
                if(!strncmp(q+6,"hero",4)) L->focus=0;
                else if(!strncmp(q+6,"partner2",8)) L->focus=2;
                else if(!strncmp(q+6,"partner",7)) L->focus=1; }
            /* ⛔ AN EMPTY VALUE IS NOT A MARK. compile_scene writes the whole stage dict
             * on every line, so EVERY line carries a bare `hx= hz=` -- and atof("") is
             * 0.0, which armed hasMark and eased the pair toward WORLD ORIGIN, out of
             * their own set, on every line of dialogue in the film. porch and boarding
             * walked off in three seconds and the rest of those takes are sky. */
            if((q=strstr(ln,"hx="))&&numAt(q+3)) { L->hx=(float)atof(q+3); L->hasMark|=1; }
            if((q=strstr(ln,"hz="))&&numAt(q+3)) { L->hz=(float)atof(q+3); L->hasMark|=2; }
            if((q=strstr(ln,"pyaw=")))  L->pyaw =(float)atof(q+5);
            if((q=strstr(ln,"pside="))) L->pside=(float)atof(q+6);
            if((q=strstr(ln,"hlook="))) L->hlook=atoi(q+6)?1:0;
            if((q=strstr(ln,"plook="))) L->plook=atoi(q+6)?1:0;
            if((q=strstr(ln,"hgt=")))   L->hgt  =(float)atof(q+4);
            if((q=strstr(ln," step=")))  L->step =(float)atof(q+6);   /* leading space: nothing else ends in "step" */
            if((q=strstr(ln," tod=")))   L->tod  =(float)atof(q+5);
            if((q=strstr(ln," tone=")))  L->tone =(float)atof(q+6);
            if((q=strstr(ln," snap=")))  L->snap =atoi(q+6);
            /* ⭐ reverse= / jawgain= (ep 4). Leading space: every key on a `stage` record is
             * space-separated, and neither word is the tail of another key. */
            if((q=strstr(ln," reverse=")))  L->reverse=atoi(q+9)?1:0;
            if((q=strstr(ln," jawgain=")))  L->jawgain=(float)atof(q+9);
            if((q=strstr(ln,"hbrow="))) L->hbrow=(q[6]=='a')?-9.f:(float)atof(q+6);
            if((q=strstr(ln,"pbrow="))) L->pbrow=(q[6]=='a')?-9.f:(float)atof(q+6);
            /* (11-11) a face held through the line, each body its own: ` hface=blink:1,smile:0.7` (the leading space keeps `pface=` off `p2face=`) */
            { static const char*const FK9[SCN_BODIES]={" hface="," pface="," p2face="," p3face="};
              for(int f9=0;f9<SCN_BODIES;f9++) if((q=strstr(ln,FK9[f9]))){ const char*v9=q+strlen(FK9[f9]); size_t k9=0;
                  while(v9[k9]&&v9[k9]!=' '&&v9[k9]!='\n'&&v9[k9]!='\r'&&k9+1<sizeof L->face[0]){ L->face[f9][k9]=v9[k9]; k9++; }
                  L->face[f9][k9]=0; } }
            /* ⛔ AN UNKNOWN SHOT NAME USED TO FALL TO OTS WITHOUT A WORD. A typo, or a size
             * this build does not have, came back as "the default coverage" and the only way to
             * find it was to notice that a line was framed wrong. It says so now -- and the match
             * is anchored at both ends, so `twofold` is not `two`. */
            if((q=strstr(ln,"shot="))){ q+=5;
                static const struct { const char*nm; int v; } SNM[]={
                    {"ots",SHOT_OTS},{"tight",SHOT_TIGHT},{"two",SHOT_TWO},
                    {"wide",SHOT_WIDE},{"solo",SHOT_SOLO},{"medium",SHOT_MEDIUM} };
                int f9=-1;
                for(int k9=0;k9<6;k9++){ size_t n9=strlen(SNM[k9].nm);
                    if(!strncmp(q,SNM[k9].nm,n9)&&(q[n9]==' '||q[n9]=='\n'||q[n9]=='\r'||q[n9]==0)){ f9=SNM[k9].v; break; } }
                if(f9<0){ char bad9[24]; int kb=0;
                    while(q[kb]&&q[kb]!=' '&&q[kb]!='\n'&&q[kb]!='\r'&&kb<23){ bad9[kb]=q[kb]; kb++; } bad9[kb]=0;
                    printf("[scene] line %d: unknown shot '%s' -- using ots\n",g_scnN-1,bad9); fflush(stdout);
                    f9=SHOT_OTS; }
                L->shot=f9; }
            if((q=strstr(ln,"hclip="))){ int k=0; q+=6;
                while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<31) L->hclip[k++]=*q++;
                L->hclip[k]=0; }
            if((q=strstr(ln,"pclip="))){ int k=0; q+=6;
                while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<31) L->pclip[k++]=*q++;
                L->pclip[k]=0; }
            /* ⭐ ep 10: htrick= ptrick= p2trick= p3trick=, each clip@sec[,clip@sec...]. A hit must START a
             * word (a space before it) -- the pgap/pclip/pprop trap, closed by construction this time. */
            { static const char*TK9[4]={"htrick=","ptrick=","p2trick=","p3trick="};
              char*TD9[4]={L->htrick,L->ptrick,L->p2trick,L->p3trick};
              for(int t9=0;t9<4;t9++){
                  q=strstr(ln,TK9[t9]);
                  while(q&&q>ln&&q[-1]!=' ') q=strstr(q+1,TK9[t9]);
                  if(!q) continue;
                  int k=0; q+=strlen(TK9[t9]);
                  while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<95) TD9[t9][k++]=*q++;
                  TD9[t9][k]=0;
                  if(*q&&*q!=' '&&*q!='\n'&&*q!='\r'){
                      printf("[trick] line %d: %s is longer than 95 characters -- TRUNCATED to '%s'\n",g_scnN-1,TK9[t9],TD9[t9]);
                      fflush(stdout); } } }
            /* ⭐ ep 12: hpuff= ppuff= p2puff= p3puff=, each sec[,sec...] -- an EXHALE off that body's mouth, `sec`
             * into the line. Same word-start guard as the tricks: "ppuff=" must not match inside "p2puff=". */
            { static const char*PK9[4]={"hpuff=","ppuff=","p2puff=","p3puff="};
              char*PD9[4]={L->hpuff,L->ppuff,L->p2puff,L->p3puff};
              for(int t9=0;t9<4;t9++){
                  q=strstr(ln,PK9[t9]);
                  while(q&&q>ln&&q[-1]!=' ') q=strstr(q+1,PK9[t9]);
                  if(!q) continue;
                  int k=0; q+=strlen(PK9[t9]);
                  while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<63) PD9[t9][k++]=*q++;
                  PD9[t9][k]=0;
                  if(*q&&*q!=' '&&*q!='\n'&&*q!='\r'){
                      printf("[smoke] line %d: %s is longer than 63 characters -- TRUNCATED to '%s'\n",g_scnN-1,PK9[t9],PD9[t9]);
                      fflush(stdout); } } }
            /* ⭐ hdoll= pdoll= p2doll= p3doll=, each `sec` (into the line) or `off` -- that body goes LIMP, or is
             * handed back to its clip. Same word-start guard: "pdoll=" must not match inside "p2doll=". */
            { static const char*DK9[4]={"hdoll=","pdoll=","p2doll=","p3doll="};
              char*DD9[4]={L->hdoll,L->pdoll,L->p2doll,L->p3doll};
              for(int t9=0;t9<4;t9++){
                  q=strstr(ln,DK9[t9]);
                  while(q&&q>ln&&q[-1]!=' ') q=strstr(q+1,DK9[t9]);
                  if(!q) continue;
                  int k=0; q+=strlen(DK9[t9]);
                  while(*q&&*q!=' '&&*q!='\n'&&*q!='\r'&&k<15) DD9[t9][k++]=*q++;
                  DD9[t9][k]=0; } } }
        else if(!strncmp(ln,"env ",4)&&g_scnN>0){
            ScnLine*L=&g_scnL[g_scnN-1];
            int n9=0, used=0; sscanf(ln,"env %d %n",&n9,&used);
            const char*t=ln+used; L->nEnv=0;
            while(*t&&L->nEnv<SCN_MAXE&&L->nEnv<n9){
                while(*t==' ') t++;
                if(*t<'0'||*t>'9') break;
                int v9=0; while(*t>='0'&&*t<='9'){ v9=v9*10+(*t-'0'); t++; }
                L->env[L->nEnv++]=(unsigned char)(v9>255?255:v9); } } }
    fclose(f);
    /* ⭐ ep 10: WHICH PATH COMES NEXT. The compiler carries a path's value on every line while it is in force,
     * so a RUN of identical values is ONE path, named by the line it starts on (pseg), and the next run (pnext)
     * is the path that takes over -- -1 when a line drops the path first. The loco rolls a path that has a
     * successor WITHOUT braking, so a rider handed from one path to the next never stops in between. */
    for(int s10=0;s10<4;s10++){
        for(int l=0;l<g_scnN;l++){ ScnLine*A=&g_scnL[l];
            A->pseg[s10]=(!A->path[s10][0])?-1:((l>0&&!strcmp(g_scnL[l-1].path[s10],A->path[s10]))?g_scnL[l-1].pseg[s10]:l); }
        int nx10=-1;
        for(int l=g_scnN-1;l>=0;l--){ ScnLine*A=&g_scnL[l];
            A->pnext[s10]=nx10;
            if(!A->path[s10][0]) nx10=-1;
            else if(A->pseg[s10]==l) nx10=l; } }
    /* ⭐ THE SCENE TRACK IS A MIXER VOICE (09-11). It used to open its own device and queue the
     * whole wav; now it is bank slot "_scene" on the one device, started the same instant. */
    sfxStop(g_scnVoice); g_scnVoice=-1;
    if(g_scnWav){ SDL_FreeWAV(g_scnWav); g_scnWav=0; }
    char wp[512]; snprintf(wp,sizeof wp,"scenes/%s",wav);
    SDL_AudioSpec spec;
    if(getenv("REVA_SCENEFPS")){   /* a fixed-step capture: the wav is muxed in later */
        printf("[scene] fixed-step capture: audio not played\n"); fflush(stdout); }
    else if(!SDL_LoadWAV(wp,&spec,&g_scnWav,&g_scnWavLen))
        printf("[scene] no audio (%s): %s\n",wp,SDL_GetError());
    else { int sl=sfxBankFromWav("_scene",g_scnWav,g_scnWavLen,&spec);
        g_scnVoice=sfxPlaySlot(sl,1.f,NULL,0);
        if(g_scnVoice<0) printf("[scene] audio: the mixer has no device -- silent\n");
        SDL_FreeWAV(g_scnWav); g_scnWav=0; }
    /* ⛔ THERE WAS A "default focus to -1 if unset" LINE HERE AND IT ATE focus=hero.
     * hero IS 0, so `if(!focus)` was true for exactly the value that means "the hero" and
     * reset it to "the speaker". The parse already writes -1 explicitly on every line, so
     * the default was redundant as well as wrong. */
    /* ⭐ 10-22: a scene (re)started while it is AWAY on a cutaway goes home first -- 2.1 ends in space, and a replay
     * would otherwise take space for its home */
    if(g_scnHomeOK&&g_scnAway[0]&&g_scnHome[0]&&!strcmp(g_setCur,g_scnAway)){
        setLoad(g_scnHome); memcpy(g_sun,g_scnHomeSun,16); memcpy(g_sunC,g_scnHomeSunC,12); memcpy(g_sky,g_scnHomeSky,12);
        printf("[scene] restarted away on '%s' -- home to '%s' first\n",g_scnAway,g_scnHome); fflush(stdout); }
    g_scnT=0.f; g_scnStaged=-1; g_scnTA=0.f; g_scnHomeOK=0; g_scnAway[0]=0;
    memset(g_castRoll,0,sizeof g_castRoll);   /* ⭐ 10-22: and every body stands */
    trickReset();   /* ⭐ ep 10: no trick outlives the scene that asked for it */
    puffReset();    /* ⭐ ep 12: ...and no smoke, exhale or drag outlives the take that made it */
    g_locoPathReset=1; g_camTrk.mode=0;   /* ⭐ ep 10: a replayed scene rolls its paths again, and starts on no tracking lens */
    g_camShk[0]=0.f; g_camShk[1]=1.5f;    /* ⭐ ep 11: ...and on a steady one */
    g_stepFps=g_stepCtl;   /* a scene starts on the ctl's exposure rate; its lines may change it */
    g_tone=g_toneCtl;
    g_jawGain=g_jawGainCtl; g_scnRev=0;   /* ⭐ (ep 4) and on the ctl's jaw gain, facing the normal way */
    printf("[scene] %s: %d lines, %.1fs, wav %s (%u bytes)\n",name,g_scnN,g_scnLen,wav,g_scnWavLen);
    if(g_scnShpN){ printf("[scene] %s: %d shape track(s) on its lines:",name,g_scnShpN);   /* ⭐ (11-06) */
        for(int k9=0;k9<g_scnShpN&&k9<12;k9++) printf(" %s",g_scnShp[k9].name);
        printf("%s\n",g_scnShpN>12?" ...":""); }
    fflush(stdout);
    return 1; }

