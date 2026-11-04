/* ---- THE ONE BODY (11-02, engine plan Phase 2; the plan is design/engine_capabilities.md section 21) --------------------------------------
 * The hero, a crowd slot, a scene's cast and a set's actor are four owners of state over one body. These are the steps they all take, each written once, so a renderer or a
 * rig only has to get them right here. Bottom-up, one step at a time: `driver.py check` (the sim and the packet) and pixel A/Bs on GL say each step changed nothing. */
typedef struct Face Face; static Face*g_faceCur=NULL;   /* the face of the body being drawn (engine/face.inc) */
/* the rest pose, ready to be posed over */
static void bodyRest(Pack*P,float(*T)[3],float(*Q)[4],float(*S)[3]){
    for(unsigned i=0;i<P->nNodes;i++){ memcpy(T[i],P->nodes[i].t,12); memcpy(Q[i],P->nodes[i].q,16); memcpy(S[i],P->nodes[i].s,12); } }
/* a pose to world matrices, parents first */
static void bodyFK(Pack*P,float(*T)[3],float(*Q)[4],float(*S)[3],M4*W){
    for(unsigned i=0;i<P->nNodes;i++){ M4 loc; m4_trs(loc,T[i],Q[i],S[i]);
        if(P->nodes[i].parent<0) memcpy(W[i],loc,64); else m4_mul(W[i],W[P->nodes[i].parent],loc); } }
/* one mesh's palette. It is the bone's world matrix times its INVERSE BIND -- skipping the inverse bind leaves every vertex at its bone's origin, which is a pile of confetti,
 * not a body. The map is unsigned SHORT. */
static void bodyPalette(Pack*P,unsigned mi,M4*W,float*pal){ PMesh*M=&P->meshes[mi];
    unsigned short*bm=(unsigned short*)(P->pk+M->offBoneMap); float*ibm=(float*)(P->pk+M->offIBM);
    for(unsigned b=0;b<M->boneCount;b++){ M4 pm; m4_mul(pm,W[bm[b]<P->nNodes?bm[b]:0],*(M4*)(ibm+b*16)); memcpy(pal+b*16,pm,64); } }
/* one mesh's vertex streams, for the program in use */
static void bodyStreams(const PMesh*M,const GLuint*b){
    glBindBuffer(GL_ARRAY_BUFFER,b[0]); glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,0);
    if(M->flags&1){ glBindBuffer(GL_ARRAY_BUFFER,b[1]); glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,0,0); }
    glBindBuffer(GL_ARRAY_BUFFER,b[2]); glEnableVertexAttribArray(2); glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_FALSE,0,0);
    glBindBuffer(GL_ARRAY_BUFFER,b[3]); glEnableVertexAttribArray(3); glVertexAttribPointer(3,4,GL_UNSIGNED_BYTE,GL_TRUE,0,0);
    if(M->flags&2){ glBindBuffer(GL_ARRAY_BUFFER,b[5]); glEnableVertexAttribArray(4); glVertexAttribPointer(4,2,GL_FLOAT,GL_FALSE,0,0); }
    else glDisableVertexAttribArray(4); }
/* the GL draw itself, with the toon pass on it */
static void bodyDrawGL(Pack*P,unsigned mi,M4*W,const GLuint*b,int mirror,GLint mModel){ PMesh*M=&P->meshes[mi];
    int toon9=toonOn(P,P->toon); toonCelSet(toon9); colSet(P,W,mi);
    if(M->idxCount){ glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,b[4]); glDrawElements(GL_TRIANGLES,M->idxCount,GL_UNSIGNED_INT,0); }
    else glDrawArrays(GL_TRIANGLES,0,M->verts);
    toonInkDraw(toon9,packStr(P,M->name),M->idxCount,M->verts);
    if(mirror) mirInst(mModel,toon9,packStr(P,M->name),M->idxCount,M->verts,0); }
#ifdef REVA_PACKET
/* one mesh of a pack, into the packet: what the GL calls beside it say, as data. g_vkOwner says whose it is: kind << 28 | owner << 12 */
static int g_shadowPass; static void meshTintOf(Pack*P,unsigned mi,float*c); static float meshAlphaOf(Pack*P,unsigned mi);
static const float* faceMeshPos(Pack*P,unsigned mi,float*mouth);
static int toonOn(const void*Pv,int toon); static void toonTell(void);   /* engine/lab.inc */
static float g_vkOpen=0.f;   /* a scene's body, this mesh: how far its mouth is open */
static int g_vkMirror=0;   /* this mesh shows in the set's mirrors and its glossy floor. 1 it does, 2 its backs don't, 4 with no line round it, 8 only there: a mirror's backing, seen in another */
/* what a mirror shows: one more copy of the mesh through each, and through the glossy floor. One bounce, and what's behind the glass isn't in it. The renderer leaves them out if it isn't drawing mirrors, so the packet's the same */
static void vkMirrorCopies(GfxMesh*m,const float*model){
    if(!g_nMir&&!g_gloss.on) return;
    if(g_mirOff<0){ const char*e=getenv("REVA_NOMIRROR"); g_mirOff=(e&&e[0]!='0')?1:0; if(g_mirOff) printf("[mirror] REVA_NOMIRROR: mirrors are dark openings\n"); }
    if(g_mirOff) return;
    const int nFl=(g_gloss.on&&!g_glossDraw)?1:0,f0=m->flags; const float*m0=m->model,*o0=m->mouth;
    for(int k=0;k<g_nMir+nFl;k++){ const Mirror*M=k<g_nMir?&g_mir[k]:&g_glossM; const int fl=k>=g_nMir;
        if((model[12]-M->p[0])*M->n[0]+(model[13]-M->p[1])*M->n[1]+(model[14]-M->p[2])*M->n[2]<-0.5f) continue;
        float mm[16]; m4_mul(mm,(float*)M->R,(float*)model);
        m->model=mm; m->mouth=fl?NULL:o0; m->flags=(f0&~GFX_GLOSS)|GFX_MIRRORED|(fl?GFX_REFL:0)|((g_vkMirror&2)?GFX_MIRCULL:0)|((g_vkMirror&4)?GFX_NOINK:0);
        gfxMesh(m); }
    m->model=m0; m->mouth=o0; m->flags=f0; }
static float g_vkTint[3]={1.f,1.f,1.f},g_vkUse=1.f; static unsigned g_vkOwner=0,g_vkPalShared=0; static int g_vkBodyFlags=0,g_vkBodySkip=0;
static void vkMesh(Pack*P,unsigned mi,const float*model,const float*bind,const float*pal,unsigned bones,const float*tint,float use,int flags,const unsigned*vbOver){
    if(g_noDraw||g_vkBodySkip) return;
    PMesh*M=&P->meshes[mi]; const GLuint*b=vbOver?vbOver:&P->vbo[mi*6]; GfxMesh m; memset(&m,0,sizeof m);
    m.id=g_vkOwner|(mi&0xfffu);
    for(int i=0;i<6;i++) m.vb[i]=b[i];
    if(!(M->flags&1)) m.vb[1]=0;
    if(!(M->flags&2)) m.vb[5]=0;
    m.nIndex=M->idxCount; m.nVerts=M->verts;
    int tx=P->meshTex[mi]; m.tex=(tx>=0&&(M->flags&2))?P->tex[tx]:0;
    m.model=model; m.bind=bind; m.pal=pal; m.bones=bones; m.palShared=g_vkPalShared;
    if(m.tex){ m.tint[0]=tint[0]; m.tint[1]=tint[1]; m.tint[2]=tint[2]; m.useTex=use; }
    else { float c9[3]; meshTintOf(P,mi,c9); m.tint[0]=c9[0]; m.tint[1]=c9[1]; m.tint[2]=c9[2]; m.useTex=0.f; }
    m.flags=flags|g_vkBodyFlags|((P->meshMatFlags[mi]&4)?GFX_CUTOUT:0)|((M->flags&8u)?GFX_BLEND:0);
    if(M->flags&8u) m.alpha=meshAlphaOf(P,mi);   /* a sheer mesh says how sheer in its name: #rrggbbaa */
    if(P->sheer>0.f&&P->sheer<1.f&&!(flags&GFX_SHADOW)){ m.flags|=GFX_BLEND; m.alpha=P->sheer; }   /* and a sheer prop in a marker */
    if(g_glossDraw) m.flags|=GFX_GLOSS;
    { const char*gn9=packStr(P,M->name); const char*gh9=gn9?strchr(gn9,'#'):NULL; if(gh9&&strlen(gh9)>=8&&gh9[7]=='*') m.flags|=GFX_GLOW; }   /* #rrggbb* glows: a penlight in a fan's hand */
    if(P->glow) m.flags|=GFX_GLOW;
    else if(P->toon&&!(flags&GFX_SHADOW)&&toonOn(P,P->toon)){ m.flags|=GFX_TOON|(P->toon==2?GFX_TOONROOM:0); { const char*nm9=packStr(P,M->name); if(nm9&&strstr(nm9,"~face~")) m.flags|=GFX_NOINK; } toonTell(); }   /* the toon pass: the renderer does the line itself */
    float mo9[8]; if(g_faceCur&&!(flags&GFX_SHADOW)){ m.pos=faceMeshPos(P,mi,mo9); if(mo9[6]>0.f) m.mouth=mo9; }   /* a face that's moved, and a mouth that's open */
    else if(g_vkOpen>0.002f&&!(flags&GFX_SHADOW)){ mo9[0]=P->mouthBind[0]; mo9[1]=P->mouthBind[1]; mo9[2]=P->mouthBind[2]; mo9[3]=P->mouthBind[5]; mo9[4]=P->mouthBind[3]; mo9[5]=P->mouthBind[4]; mo9[6]=g_vkOpen; mo9[7]=0.f; m.mouth=mo9; }
    if(!(g_vkMirror&8)) gfxMesh(&m);
    if(g_vkMirror&&!(flags&GFX_SHADOW)) vkMirrorCopies(&m,model); }
#endif
/* one posed mesh, drawn: into the packet and through GL. cloth is clothPrep's answer: 2 is hanging cloth, whose vertices are already in the world, in the buffers the caller passes.
 * mirror: this path's body also shows in the set's mirrors. */
static void bodyDrawMesh(Pack*P,unsigned mi,const float*model,M4*W,const float*pal,const GLuint*b,const float*tint,float use,int cloth,int mirror,GLint mModel){
    (void)model; (void)pal; (void)tint; (void)use; (void)cloth;
#ifdef REVA_PACKET
    g_vkMirror=mirror?1:0;
    if(cloth==2){ static const float I9[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; vkMesh(P,mi,I9,NULL,NULL,0,tint,use,0,b); }
    else vkMesh(P,mi,model,(float*)(P->pk+P->meshes[mi].offBind),pal,P->meshes[mi].boneCount,tint,use,0,b);
    g_vkMirror=0;
#endif
    /* ⭐ (11-03) `#rrggbb*` in a mesh's name GLOWS on a body too, as it does on a held prop: a penlight in a fan's hand. uEmis draws it unlit at its own colour */
    const char*gn9=packStr(P,P->meshes[mi].name); const char*gh9=gn9?strchr(gn9,'#'):NULL; GLint ue9=-1;
    if(gh9&&strlen(gh9)>=8&&gh9[7]=='*'){ GLint pc9=0; glGetIntegerv(GL_CURRENT_PROGRAM,&pc9); if(pc9){ ue9=glGetUniformLocation((GLuint)pc9,"uEmis"); if(ue9>=0) glUniform1f(ue9,1.f); } }
    bodyDrawGL(P,mi,W,b,mirror,mModel);
    if(ue9>=0) glUniform1f(ue9,0.f); }
