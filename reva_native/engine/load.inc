/* (step 21, 11-04) the next pack loaded keeps its first texture's pixels, for a mover's sheet of pictures to be cut from (engine/settrack.inc, stkSheet) */
static int g_texKeep=0,g_texKeepW=0,g_texKeepH=0,g_texKeepFlip=0; static unsigned char*g_texKeepPx=NULL;
/* ---- WHAT A MESH IS MADE OF (queue steps 22 and 23, 11-04). A pack says it in a MATL chunk after its body, by mesh name, so no bake is needed to say it and none can lose it
 * (skate_src/author/material.py writes one): a count, then for each a 40-byte name and six floats: how rough, how much a metal, how much fine detail, how deep for parallax,
 * its kind for a body (1 skin, 2 hair, 3 an eye, 4 cloth, 5 its texture's alpha says, pixel by pixel, 6 fur: then depth is how long it is in metres, detail how fine, and metal how curly, 0 straight to 1 wool), and which mesh of that name it means, counted from 1 in the pack's order (0: every one).
 * A mesh the chunk doesn't name is looked up by what its name or its pack's has in it: data/materials.txt first, a line `<word> <rough> <metal> [detail]`, then the engine's own
 * short list. A skinned mesh's kind comes from its name the same way. Nothing matching, it's an ordinary rough surface of no kind.
 * ⛔ The tables match words inside names, so keep their words long enough not to turn up by accident: "arm" is in "armour", and "face" in "surface", which is why kinds are only
 * read off meshes with bones. Hair's words come before the eye's: an eyebrow is hair. A kind's words are read in the part of a name ahead of its hide list (~h=), which names other pieces.
 * A wardrobe piece (wear~slot~piece~outfits) is cloth unless its slot or its name says it's hard or shines: shoes, a belt, a watch, a harness, latex. Those are left to the material words. */
static int g_loadQuiet;   /* declared further down: a pack that streams back says nothing the second time */
#define MAT_FILE_MAX 64
static struct { char key[24]; float r,m,d; } g_matFile[MAT_FILE_MAX]; static int g_matFileN=-1;
static const struct { const char*key; float r,m,d; } MAT_TABLE[]={
    {"chrome",0.12f,1.f,0.f},{"mirror",0.05f,1.f,0.f},{"gold",0.25f,1.f,0.f},{"steel",0.35f,1.f,0.f},{"metal",0.40f,1.f,0.f},{"iron",0.55f,1.f,0.f},
    {"glass",0.08f,0.f,0.f},{"plastic",0.40f,0.f,0.f},{"latex",0.22f,0.f,0.f},{"vinyl",0.25f,0.f,0.f},{"leather",0.50f,0.f,0.f},{"rubber",0.90f,0.f,0.f},
    {"wood",0.70f,0.f,0.30f},{"concrete",0.90f,0.f,0.60f},{"asphalt",0.92f,0.f,0.70f},{"brick",0.85f,0.f,0.50f},{"stone",0.85f,0.f,0.50f}};
static const struct { const char*key; int kind; float r; } MAT_KIND[]={
    {"person_body",5,0.60f},   /* a person built while the game runs: one mesh, its kinds painted into its texture's alpha (engine/people.inc) */
    {"hair",2,0.50f},{"brow",2,0.50f},{"lash",2,0.50f},{"beard",2,0.50f},{"pony",2,0.50f},{"braid",2,0.50f},
    {"fur",6,0.60f},{"fleece",6,0.60f},{"wool",6,0.60f},{"pelt",6,0.60f},{"fluff",6,0.60f},   /* before cloth's: a fur skirt is fur */
    {"eye",3,0.20f},{"iris",3,0.20f},{"cornea",3,0.20f},   /* with the eyes setting on the shader makes it wet itself; without it, this keeps its glint from running away */
    {"skin",1,0.55f},{"body",1,0.55f},{"face",1,0.55f},{"head",1,0.55f},
    {"cloth",4,0.85f},{"shirt",4,0.85f},{"coat",4,0.85f},{"dress",4,0.85f},{"skirt",4,0.85f},{"pants",4,0.85f},{"jacket",4,0.85f},{"hood",4,0.85f},{"scarf",4,0.85f},{"sock",4,0.85f},{"wear~",4,0.85f},{"cargo",4,0.85f},{"tank",4,0.85f},
    {"sleeve",4,0.85f},{"bandana",4,0.85f},{"panties",4,0.85f},{"glove",4,0.85f},{"robe",4,0.85f},{"tunic",4,0.85f},{"uniform",4,0.85f},{"trouser",4,0.85f},{"jeans",4,0.85f},{"_top",4,0.85f}};
static const char*const MAT_NOTCLOTH[]={"~shoes~","~feet~","~belt","~wrists~","~ears~","~harness~","~neck~","~arms~","~decals~","~thigh~","latex","vinyl","leather","tech","stud","metal","chain","rubber","plastic","glass","boot","heel","sneaker"};
static int matWordIn(const char*name,const char*key){ const size_t kl=strlen(key); if(!kl) return 0; for(const char*p=name;*p;p++){ size_t i=0; while(i<kl&&p[i]&&((p[i]|32)==(key[i]|32))) i++; if(i==kl) return 1; } return 0; }
/* (step 23) EYES NOBODY NAMED. A body from another tool often has its eyeballs as meshes under the body's own name (Anima's four are all `anima_body`), so no word finds them.
 * They're found by what an eye is: a small skinned mesh as deep as it's wide and tall, high on the body, a little off its middle line, with a twin mirrored across it. Only meshes
 * the words left as skin or as nothing are looked at, a few hundred of each one's vertices, through its own bones at rest. The pack's own chunk still has the last word. */
static void packEyesFind(Pack*P){
    enum { EC=24 }; struct { unsigned mi; float c[3],e; } cd[EC]; int nc=0;
    if(P->nMeshes<3||!P->nNodes||P->nNodes>4096u) return;
    float(*T)[3]=(float(*)[3])malloc(sizeof(float[3])*P->nNodes); float(*Q)[4]=(float(*)[4])malloc(sizeof(float[4])*P->nNodes); float(*S)[3]=(float(*)[3])malloc(sizeof(float[3])*P->nNodes);
    M4*W=(M4*)malloc(sizeof(M4)*P->nNodes); float*pal=(float*)malloc(sizeof(float)*16u*MAXB); float(*bb)[6]=(float(*)[6])malloc(sizeof(float[6])*P->nMeshes);
    if(T&&Q&&S&&W&&pal&&bb){ bodyRest(P,T,Q,S); bodyFK(P,T,Q,S,W);
        float lo[3]={1e9f,1e9f,1e9f},hi[3]={-1e9f,-1e9f,-1e9f};
        for(unsigned mi=0;mi<P->nMeshes;mi++){ PMesh*M=&P->meshes[mi]; float*b=bb[mi]; b[0]=b[1]=b[2]=1e9f; b[3]=b[4]=b[5]=-1e9f;
            if(!(M->flags&4)||M->boneCount<1||M->boneCount>MAXB||!M->verts) continue;
            bodyPalette(P,mi,W,pal);
            const float*vp=(const float*)(P->pk+M->offPos),*bd=(const float*)(P->pk+M->offBind); const unsigned char*vj=P->pk+M->offJoints,*vw=P->pk+M->offWeights;
            const unsigned step=M->verts>400u?M->verts/400u:1u;
            for(unsigned v=0;v<M->verts;v+=step){ const float*q=vp+v*3; float bp[3],x[3]={0,0,0},ws=0.f; for(int r=0;r<3;r++) bp[r]=bd[r]*q[0]+bd[4+r]*q[1]+bd[8+r]*q[2]+bd[12+r];
                for(int k=0;k<4;k++){ const float w=(float)vw[v*4+k]*(1.f/255.f); if(w<=0.f) continue; const float*m=pal+(vj[v*4+k]<M->boneCount?vj[v*4+k]:0)*16; ws+=w; for(int r=0;r<3;r++) x[r]+=w*(m[r]*bp[0]+m[4+r]*bp[1]+m[8+r]*bp[2]+m[12+r]); }
                if(ws<=0.f) continue;
                for(int r=0;r<3;r++){ if(x[r]<b[r]) b[r]=x[r]; if(x[r]>b[3+r]) b[3+r]=x[r]; } }
            if(b[0]<=b[3]) for(int r=0;r<3;r++){ if(b[r]<lo[r]) lo[r]=b[r]; if(b[3+r]>hi[r]) hi[r]=b[3+r]; } }
        const float H=hi[1]-lo[1],cx0=0.5f*(lo[0]+hi[0]);
        if(H>1e-4f){
            for(unsigned mi=0;mi<P->nMeshes&&nc<EC;mi++){ const float*b=bb[mi]; const PMesh*M=&P->meshes[mi]; const int k=(int)(P->meshMat[mi][4]+0.5f);
                if(b[0]>b[3]||M->verts<24u||M->verts>20000u||(k!=0&&k!=1)) continue;
                const float ex=b[3]-b[0],ey=b[4]-b[1],ez=b[5]-b[2],big=ex>ey?(ex>ez?ex:ez):(ey>ez?ey:ez),small=ex<ey?(ex<ez?ex:ez):(ey<ez?ey:ez);
                if(small<=0.f||big>small*1.25f||big<0.008f*H||big>0.045f*H) continue;                    /* as deep as it's wide and tall, and an eye's size against the body */
                const float cx=0.5f*(b[0]+b[3])-cx0,cy=(0.5f*(b[1]+b[4])-lo[1])/H;
                if(cy<0.80f||fabsf(cx)<0.005f*H||fabsf(cx)>0.035f*H) continue;                           /* in the head, and to one side of the middle, inside the face */
                cd[nc].mi=mi; cd[nc].c[0]=cx; cd[nc].c[1]=0.5f*(b[1]+b[4]); cd[nc].c[2]=0.5f*(b[2]+b[5]); cd[nc].e=big; nc++; }
            int got=0; unsigned char twin[EC]; memset(twin,0,sizeof twin);
            for(int a=0;a<nc;a++) for(int b=0;b<nc;b++){ if(a==b||cd[a].c[0]*cd[b].c[0]>=0.f) continue;
                if(fabsf(fabsf(cd[a].c[0])-fabsf(cd[b].c[0]))<0.004f*H&&fabsf(cd[a].c[1]-cd[b].c[1])<0.004f*H&&fabsf(cd[a].c[2]-cd[b].c[2])<0.004f*H&&cd[a].e<cd[b].e*1.2f&&cd[b].e<cd[a].e*1.2f) twin[a]=1; }
            for(int a=0;a<nc;a++) if(twin[a]){ float*o=P->meshMat[cd[a].mi]; o[4]=3.f; o[0]=0.20f; o[1]=0.f; got++; }
            if(got&&!g_loadQuiet) printf("[material] %s: %d meshes taken as eyes, by their shape and where they sit\n",P->label,got); } }
    free(T); free(Q); free(S); free(W); free(pal); free(bb); }
static void packMaterials(Pack*P){
    if(g_matFileN<0){ g_matFileN=0; FILE*f=fopen("data/materials.txt","r");
        if(f){ char ln[160]; while(fgets(ln,sizeof ln,f)&&g_matFileN<MAT_FILE_MAX){ float r=0.85f,m=0.f,d=0.f; char k[24]; if(ln[0]=='#'||sscanf(ln,"%23s %f %f %f",k,&r,&m,&d)<3) continue;
                snprintf(g_matFile[g_matFileN].key,24,"%s",k); g_matFile[g_matFileN].r=r<0.f?0.f:r>1.f?1.f:r; g_matFile[g_matFileN].m=m<0.f?0.f:m>1.f?1.f:m; g_matFile[g_matFileN].d=d<0.f?0.f:d>1.f?1.f:d; g_matFileN++; }
            fclose(f); clubOpened("data/materials.txt"); printf("[material] data/materials.txt: %d words\n",g_matFileN); } }
    P->meshMat=(float(*)[6])calloc(P->nMeshes?P->nMeshes:1,sizeof(float[6])); if(!P->meshMat) return;
    for(unsigned mi=0;mi<P->nMeshes;mi++){ const char*nm=packStr(P,P->meshes[mi].name); float*o=P->meshMat[mi]; int hit=0; if(!nm) nm="";
        for(int k=0;k<g_matFileN&&!hit;k++) if(matWordIn(nm,g_matFile[k].key)||matWordIn(P->label,g_matFile[k].key)){ o[0]=g_matFile[k].r; o[1]=g_matFile[k].m; o[2]=g_matFile[k].d; hit=1; }
        for(size_t k=0;k<sizeof MAT_TABLE/sizeof MAT_TABLE[0]&&!hit;k++) if(matWordIn(nm,MAT_TABLE[k].key)){ o[0]=MAT_TABLE[k].r; o[1]=MAT_TABLE[k].m; o[2]=MAT_TABLE[k].d; hit=1; }
        /* a body's surfaces: skinned meshes only */
        if(P->meshes[mi].boneCount>1){ char own[96]; snprintf(own,sizeof own,"%s",nm); { char*h9=strstr(own,"~h="); if(h9) *h9=0; }
            if(!strncmp(own,"wear~",5)){ char*t1=strchr(own+5,'~'),*t2=t1?strchr(t1+1,'~'):NULL; if(t2) *t2=0; }   /* a wardrobe piece: its slot and its name, and not the outfits it's in (furs is an outfit) */
            for(size_t k=0;k<sizeof MAT_KIND/sizeof MAT_KIND[0];k++) if(matWordIn(own,MAT_KIND[k].key)){ int kd=MAT_KIND[k].kind;
                if(kd==4) for(size_t q=0;q<sizeof MAT_NOTCLOTH/sizeof MAT_NOTCLOTH[0];q++) if(matWordIn(own,MAT_NOTCLOTH[q])){ kd=0; break; }
                o[4]=(float)kd; if(!hit&&kd) o[0]=MAT_KIND[k].r;
                if(kd==6){ o[3]=0.035f; o[2]=0.55f; }   /* fur nobody measured: three and a half centimetres, middling fine */
                break; } } }
    packEyesFind(P);
    /* the pack's own word beats the tables' */
    if(P->len>=16&&!memcmp(P->pk+P->len-4,"RPK2",4)){ unsigned f9[3]; memcpy(f9,P->pk+P->len-16,12);
        if(f9[2]==P->len&&(size_t)f9[0]+(size_t)f9[1]*16<=P->len-16) for(unsigned d9=0;d9<f9[1];d9++){ const unsigned char*e9=P->pk+f9[0]+d9*16; unsigned o9,s9; memcpy(&o9,e9+4,4); memcpy(&s9,e9+8,4);
            if(memcmp(e9,"MATL",4)||s9<4||(size_t)o9+s9>P->len) continue;
            unsigned n; memcpy(&n,P->pk+o9,4); if(n>4096u||4u+(size_t)n*64u>s9){ printf("[material] %s: a MATL chunk that doesn't add up, ignored\n",P->label); continue; }
            /* ⛔ (11-11) `<name>@N` IS THE NTH MESH OF THAT NAME IN THE PACK'S OWN ORDER (design/pack_format.md, material.py), and by now the blended ones
             * have been moved last (loadPack). Counted in that order, every row after a blended mesh of its name landed on the mesh after the one it
             * named: REVA's face (11 meshes all called anima_body, her brows and lashes blended) had her right eye shaded as hair (no catch light, its
             * pupil grey), her left as an eye, her teeth as hair (grey, streaked) and her brows and lashes as wet eyes (sparkles). meshFile is each
             * mesh's place in the file; REVA_MATLOLD=1 counts as before, the control */
            static int old9=-1; if(old9<0){ const char*e9=getenv("REVA_MATLOLD"); old9=(e9&&e9[0]&&e9[0]!='0'); }
            unsigned*byFile9=NULL;   /* the meshes in the file's order */
            if(P->meshFile&&!old9&&(byFile9=(unsigned*)malloc(sizeof(unsigned)*P->nMeshes))!=NULL){
                for(unsigned mi=0;mi<P->nMeshes;mi++) byFile9[mi]=mi;
                for(unsigned mi=0;mi<P->nMeshes;mi++) if(P->meshFile[mi]<P->nMeshes) byFile9[P->meshFile[mi]]=mi; }
            int got=0, moved=0; for(unsigned k=0;k<n;k++){ const unsigned char*r9=P->pk+o9+4+k*64; char nm9[41]; memcpy(nm9,r9,40); nm9[40]=0; float v[6]; memcpy(v,r9+40,24);
                for(int c=0;c<4;c++){ if(!(v[c]>=0.f)) v[c]=0.f; if(v[c]>(c==3?0.25f:1.f)) v[c]=c==3?0.25f:1.f; }
                if(!(v[4]>=0.f)||v[4]>6.f) v[4]=0.f;
                v[4]=(float)(int)(v[4]+0.5f); const int ord9=(v[5]>=1.f&&v[5]<4096.f)?(int)(v[5]+0.5f):0; int seen9=0, was9=-1, now9=-1; v[5]=0.f;
                for(unsigned fi=0;fi<P->nMeshes;fi++){ const unsigned mi=byFile9?byFile9[fi]:fi; const char*nm=packStr(P,P->meshes[mi].name);
                    if(nm&&!strcmp(nm,nm9)){ seen9++; if(!ord9||seen9==ord9){ memcpy(P->meshMat[mi],v,24); got++; now9=(int)mi; } } }
                if(ord9&&byFile9){ seen9=0; for(unsigned mi=0;mi<P->nMeshes&&was9<0;mi++){ const char*nm=packStr(P,P->meshes[mi].name); if(nm&&!strcmp(nm,nm9)&&++seen9==ord9) was9=(int)mi; }
                    if(was9!=now9) moved++; } }
            free(byFile9);
            printf("[material] %s: %u in its chunk, on %d meshes",P->label,n,got);
            if(moved) printf("; %d of its rows name a mesh by its place in the file, and went to another mesh before (REVA_MATLOLD=1)",moved);
            if(old9&&P->meshFile) printf(" -- counted in the engine's order (REVA_MATLOLD=1)");
            printf("\n"); } }
    /* say what a body's surfaces came out as, once, so a wrong word in a name shows */
    { int k9[7]={0,0,0,0,0,0,0}; for(unsigned mi=0;mi<P->nMeshes;mi++){ const int k=(int)(P->meshMat[mi][4]+0.5f); if(k>=0&&k<7) k9[k]++; }
      if(k9[1]+k9[2]+k9[3]+k9[4]+k9[5]+k9[6]&&!g_loadQuiet) printf("[material] %s: %d skin, %d hair, %d eye, %d cloth, %d fur%s, %d plain\n",P->label,k9[1],k9[2],k9[3],k9[4],k9[6],k9[5]?", and one painted pixel by pixel":"",k9[0]); } }
/* ⭐ A TRANSITION PLAYS ONCE IN A SCENE (ROBOT NEKO ep 9). The same measure as the idle rule: a clip
 * whose last frame is more than IDLE_LOOP_DEG from its first is a move, not a loop. A scene keeps a
 * line's clip for as long as no later line changes it -- kaeli's bail stayed on for eighteen lines and
 * she fell down every four seconds. Cached per clip; said once. */
/* the largest first-to-last rotation among the 16 canonical body bones; the all-track mean on a rig
 * with no canon. A mean over 111 nodes let kaeli's fall average under the line (ep 9). */
static float clipEndGapBodyDeg(Pack*P,PClip*c){
    if(!c||c->frames<2||!c->nTracks) return 0.f;
    unsigned char*tr=P->pk+c->offTracks; float mx=0.f; int n=0;
    for(unsigned t=0;t<c->nTracks;t++){
        unsigned node=*(unsigned*)(tr+t*12), type=*(unsigned*)(tr+t*12+4), offD=*(unsigned*)(tr+t*12+8);
        if(type!=0) continue;
        int canon=0; for(int k=0;k<16;k++) if(P->pb[k]==(int)node){ canon=1; break; }
        if(!canon) continue;
        float*a=(float*)(P->pk+offD),*b=a+(size_t)(c->frames-1)*4;
        float d=fabsf(a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+a[3]*b[3]); if(d>1.f) d=1.f;
        float g=2.f*acosf(d)*57.2957795f; if(g>mx) mx=g; n++; }
    return n?mx:clipLoopGapDeg(P,c); }
static int clipHoldsEnd(Pack*P,PClip*c,int slot,const char*nm){
    if(!P||!c||c->frames<2) return 0;
    static struct { const unsigned char*pk; PClip*c; float gap; } cache9[128]; static int n9=0;
    float gap=-1.f;
    for(int i=0;i<n9;i++) if(cache9[i].c==c&&cache9[i].pk==P->pk){ gap=cache9[i].gap; break; }
    if(gap<0.f){
        gap=clipEndGapBodyDeg(P,c);
        if(n9<128){ cache9[n9].pk=P->pk; cache9[n9].c=c; cache9[n9].gap=gap; n9++; }
        if(gap>IDLE_LOOP_DEG){
            printf("[hold] slot %d '%s' is a transition (a body bone ends %.0f deg from its start): in a scene it plays ONCE and holds its last frame\n",
                   slot,nm?nm:"?",gap); fflush(stdout); } }
    return gap>IDLE_LOOP_DEG; }
static unsigned char*g_packMem=NULL; static size_t g_packMemLen=0;   /* the streaming world: the bytes are already here, and they're the pack's to keep */
/* ⛔ 11-04: A LONG SESSION'S LOG. What a pack says about itself is the same every time it's read, and a world that streams reads the same ones over and over. So what comes back again says nothing: g_loadQuiet, set by whoever's bringing it back */
static int g_loadQuiet=0;
#define printf(...) do{ if(!g_loadQuiet) printf(__VA_ARGS__); }while(0)
static Pack loadPack(const char*path){
    Pack P; memset(&P,0,sizeof P);
    { const char*b=strrchr(path,'/'); b=b?b+1:path;
      if(!strncmp(b,"v1_",3)) b+=3;
      int k=0; for(const char*c=b;*c&&strcmp(c,".rpack")&&k<63;c++) P.label[k++]=(*c=='_')?' ':*c;
      P.label[k]=0; strcpy(P.artist,"");
      { char lo[64]; for(int k2=0;k2<64;k2++){ char c2=P.label[k2]; lo[k2]=(c2>='A'&&c2<='Z')?c2+32:c2; if(!c2) break; }
        P.fly=0;
        if(strstr(lo,"penguin")||strstr(lo,"mole")||strstr(lo,"crab")||strstr(lo,"lobster")||strstr(lo,"crawfish")) P.fly=0;   /* penguins famously don't (label also matches 'puffin' folder) */
        else if(strstr(lo,"whale")||strstr(lo,"dolphin")||strstr(lo,"shark")||strstr(lo,"fish")||strstr(lo,"seahorse")||strstr(lo,"puffer")||strstr(lo,"trout")||strstr(lo,"seal")) P.fly=1;   /* the space aquarium */
        else if(strstr(lo,"eagle")||strstr(lo,"heron")||strstr(lo,"robin")||strstr(lo,"goose")||strstr(lo,"duck")||strstr(lo,"bird")||strstr(lo,"bat ")||strstr(lo,"bat_")||strstr(lo,"dragonfly")||strstr(lo,"scorpionfly")||strstr(lo,"drone")||strstr(lo,"fly ")) P.fly=2; }
      FILE*cf=g_packMem?NULL:fopen("credits.txt","r");   /* lines: label|artist */
      if(cf){ char ln[192];
        while(fgets(ln,sizeof ln,cf)){ char*bar=strchr(ln,'|'); if(!bar) continue; *bar=0;
            char*nl=strchr(bar+1,'\n'); if(nl)*nl=0;
            nl=strchr(bar+1,'\r'); if(nl)*nl=0;   /* windows-written credits: the \r rendered as glyph garbage on the CRT */
            if(!strncmp(ln,P.label,strlen(P.label))){ strncpy(P.artist,bar+1,63); break; } }
        fclose(cf); } }
    if(g_packMem){ P.pk=g_packMem; P.len=g_packMemLen; }
    else { FILE*f=fopen(path,"rb");
    if(!f){ fprintf(stderr,"open pack FAILED: [%s] errno=%d (%s)\n",path,errno,strerror(errno));
            fflush(stderr); die("open pack"); }
    struct stat st; fstat(fileno(f),&st); P.len=st.st_size;
    P.pk=malloc(P.len); if(fread(P.pk,1,P.len,f)!=P.len) die("read"); fclose(f);
    netSigAdd(path,P.pk,P.len); }   /* (step 29) in a session, what this machine read goes into what the host holds against its own */
    /* ⭐ (step 28) nothing of a pack is believed until all of it has been checked against the file's own length (engine/packcheck.inc). A pack can come from anyone.
     * Once more after its chunks are merged, because that writes new tables: what the loader reads is then what was checked */
    { char why[200]; if(!packCheck(P.pk,P.len,why,sizeof why)){ fprintf(stderr,"[pack] %s is broken, and isn't loaded: %s\n",path,why); fflush(stderr); die("a broken pack"); }
      packChunksMerge(&P.pk,&P.len,path);
      if(!packCheck(P.pk,P.len,why,sizeof why)){ fprintf(stderr,"[pack] %s is broken once its chunks are in, and isn't loaded: %s\n",path,why); fflush(stderr); die("a broken pack"); } }
    packSumPrint(P.pk,path);
    unsigned*Hd=(unsigned*)(P.pk+4);
    P.nNodes=Hd[1]; P.nMeshes=Hd[2]; P.nMats=Hd[3]; P.nClips=Hd[4]; P.nTex=Hd[5];
    P.nodes=packNodes(P.pk,Hd[6],Hd[1]); P.meshes=(PMesh*)(P.pk+Hd[7]); P.clips=(PClip*)(P.pk+Hd[9]);
    P.nMorph=0;
    if(Hd[14]&&Hd[14]<P.len){ unsigned nM=*(unsigned*)(P.pk+Hd[14]);
        /* ⛔ THIS USED TO CLAMP AT EIGHT WITHOUT SAYING SO. A pack with more targets
         * loaded its first eight and lost the rest in silence -- and which eight depends on
         * bake order, so the failure would look like "some expressions do nothing". */
        if(nM>MORPH_MAX){ printf("[morph] %s carries %u targets, cap is %d -- DROPPING %u\n",
                                 path,nM,MORPH_MAX,nM-MORPH_MAX); nM=MORPH_MAX; }
        for(unsigned mi9=0;mi9<nM;mi9++){ unsigned*e9=(unsigned*)(P.pk+Hd[14]+4+mi9*16);
            P.morphMesh[mi9]=e9[0]; P.morphName[mi9]=e9[1]; P.morphSparse[mi9]=e9[2]; P.morphOff[mi9]=e9[3];
            /* the VERTEX RANGE this target touches, found once at load: a face morph on
             * a 294k-vertex body must not re-upload the whole position buffer every
             * frame (and the old blend silently clamped at 80k, which would have left
             * kaeli's face -- her head verts start at 46k -- half unmoved). */
            unsigned lo9=0xffffffffu, hi9=0; unsigned char*sd9=P.pk+e9[3];
            for(unsigned s9=0;s9<e9[2];s9++){ unsigned vi9=*(unsigned*)(sd9+s9*16);
                if(vi9<lo9) lo9=vi9;
                if(vi9>hi9) hi9=vi9; }
            P.morphLo[mi9]=lo9; P.morphHi[mi9]=hi9;
            if(getenv("REVA_MORPHDBG"))
                printf("[morph:load] %s morph %u '%s' mesh %u sparse %u range %u..%u\n",
                       path,mi9,packStr(&P,e9[1]),e9[0],e9[2],lo9,hi9); }
        P.nMorph=(int)nM; }
    /* THE FACE LANDMARKS. Marker nodes, zero weight, parented to the head: they ride
     * FK like anything else, so a camera (or a viseme) can ask where the mouth IS
     * rather than guessing an offset from the head bone -- which is how the face rig
     * ended up filming hair. */
    P.fmMouth=P.fmEyeL=P.fmEyeR=P.fmChin=P.fmMouthL=P.fmMouthR=P.fmJaw=-1;
    P.fmInterior=-1;
    for(unsigned m9=0;m9<P.nMeshes;m9++)
        if(!strcmp(packStr(&P,P.meshes[m9].name),"mouth_interior")) P.fmInterior=(int)m9;
    for(unsigned i9=0;i9<P.nNodes;i9++){ const char*nm9=packStr(&P,P.nodes[i9].name);
        if(!strcmp(nm9,"face_jaw")) P.fmJaw=(int)i9;
        else if(!strcmp(nm9,"face_mouth")) P.fmMouth=(int)i9;
        else if(!strcmp(nm9,"face_eye_l")) P.fmEyeL=(int)i9;
        else if(!strcmp(nm9,"face_eye_r")) P.fmEyeR=(int)i9;
        else if(!strcmp(nm9,"face_mouth_l")) P.fmMouthL=(int)i9;
        else if(!strcmp(nm9,"face_mouth_r")) P.fmMouthR=(int)i9;
        else if(!strcmp(nm9,"face_chin")) P.fmChin=(int)i9; }
    P.mouthBind[0]=P.mouthBind[1]=P.mouthBind[2]=0.f; P.mouthBind[3]=P.mouthBind[4]=1.f;
    P.mouthBind[5]=0.f;
    /* ⛔ AN EYE BONE WITH NO WEIGHTS ROTATES NOTHING. face_marks.py plants face_eye_l/_r
     * as MEASUREMENT marks -- they exist to be read, and on most bodies nothing is skinned
     * to them, so aiming one would be a silent no-op that looks like a working feature.
     * Checked against every mesh's bone map, once, and reported. */
    P.eyesRig=0;
    if(P.fmEyeL>=0&&P.fmEyeR>=0){
        for(unsigned mi=0;mi<P.nMeshes&&!P.eyesRig;mi++){
            PMesh*M=&P.meshes[mi];
            if(!(M->flags&4)||!M->boneCount) continue;
            unsigned short*bm=(unsigned short*)(P.pk+M->offBoneMap);
            for(unsigned b=0;b<M->boneCount;b++)
                if(bm[b]==(unsigned short)P.fmEyeL||bm[b]==(unsigned short)P.fmEyeR){ P.eyesRig=1; break; } } }
    if(P.fmMouth>=0&&P.fmEyeL>=0&&P.fmEyeR>=0&&P.nMeshes){
        M4*rw=(M4*)malloc(P.nNodes*sizeof(M4));
        for(unsigned i9=0;i9<P.nNodes;i9++){ M4 lc9;
            m4_trs(lc9,P.nodes[i9].t,P.nodes[i9].q,P.nodes[i9].s);
            if(P.nodes[i9].parent<0) memcpy(rw[i9],lc9,64);
            else m4_mul(rw[i9],rw[P.nodes[i9].parent],lc9); }
        /* THE SHADER SEES THE RAW MESH BUFFER. Both packs' bind matrices are IDENTITY,
         * so bp = bind*p is the buffer position, and on a UE export that is NOT rig
         * space (the trap the authoring tools already know: "raw buffers are not rig
         * space on UE packs"). reva's happens to be, which is why her aperture landed
         * and kaeli's opened a hole across her whole jaw. So the marks are carried
         * INTO mesh space through the mesh node's inverse, and the two scales are
         * measured there as well. */
        unsigned mn9=P.meshes[0].node; if(mn9>=P.nNodes) mn9=0;
        float*mw=rw[mn9];
        float a9[9]={mw[0],mw[1],mw[2], mw[4],mw[5],mw[6], mw[8],mw[9],mw[10]};
        float det=a9[0]*(a9[4]*a9[8]-a9[5]*a9[7])-a9[3]*(a9[1]*a9[8]-a9[2]*a9[7])
                 +a9[6]*(a9[1]*a9[5]-a9[2]*a9[4]);
        if(det>-1e-20f&&det<1e-20f) det=1e-20f;
        float in9[9]={ (a9[4]*a9[8]-a9[5]*a9[7])/det, (a9[2]*a9[7]-a9[1]*a9[8])/det, (a9[1]*a9[5]-a9[2]*a9[4])/det,
                       (a9[5]*a9[6]-a9[3]*a9[8])/det, (a9[0]*a9[8]-a9[2]*a9[6])/det, (a9[2]*a9[3]-a9[0]*a9[5])/det,
                       (a9[3]*a9[7]-a9[4]*a9[6])/det, (a9[1]*a9[6]-a9[0]*a9[7])/det, (a9[0]*a9[4]-a9[1]*a9[3])/det };
        float t9[3]={mw[12],mw[13],mw[14]};
        #define TOMESH(src,dst) { float d0=(src)[12]-t9[0],d1=(src)[13]-t9[1],d2=(src)[14]-t9[2];             dst[0]=in9[0]*d0+in9[3]*d1+in9[6]*d2; dst[1]=in9[1]*d0+in9[4]*d1+in9[7]*d2;             dst[2]=in9[2]*d0+in9[5]*d1+in9[8]*d2; }
        float mo[3],el[3],er[3];
        TOMESH(rw[P.fmMouth],mo); TOMESH(rw[P.fmEyeL],el); TOMESH(rw[P.fmEyeR],er);
        float io=el[0]-er[0]; if(io<0.f) io=-io;
        float eyeY=0.5f*(el[1]+er[1]);
        float fh=(eyeY-mo[1])/0.34f; if(fh<0.f) fh=-fh; if(fh<1e-6f) fh=1e-6f;
        P.mouthBind[0]=mo[0]; P.mouthBind[1]=mo[1]; P.mouthBind[2]=mo[2];
        /* THE APERTURE IS SIZED BY THE MOUTH, NOT BY THE EYES. It used to be
         * 0.42 x the interocular distance -- and the EYE marks are themselves placed
         * by proportion, so that was a guess resting on a guess. Forced open on kaeli
         * it cut a circle from her nose to her chin, wider than her mouth and
         * see-through to the hair behind her skull. face_marks.py now measures the
         * CORNERS; when they are in the rig the width is simply the distance between
         * them. The old ratio survives only for a body that predates the corners. */
        const char*src9="0.42 x IOD (no corner marks)";
        if(P.fmMouthL>=0&&P.fmMouthR>=0){
            float ml[3],mr[3];
            TOMESH(rw[P.fmMouthL],ml); TOMESH(rw[P.fmMouthR],mr);
            float dx9=ml[0]-mr[0], dy9=ml[1]-mr[1], dz9=ml[2]-mr[2];
            float w9=0.5f*sqrtf(dx9*dx9+dy9*dy9+dz9*dz9);
            if(w9>1e-6f){ P.mouthBind[3]=w9; src9="MEASURED corner marks";
                /* A MOUTH IS NOT ALWAYS LEVEL. kaeli's is a little off kilter on her model: lower than
                 * the aperture was put, and tilted. The aperture assumed the lip line ran along mesh +X; it now
                 * takes the angle the CORNER MARKS actually make, so tilting the marks
                 * tilts the opening with them. (The interior already followed the
                 * marks -- it builds its own frame from them.) */
                P.mouthBind[5]=atan2f(ml[1]-mr[1],ml[0]-mr[0]); }
            else P.mouthBind[3]=(io>1e-6f)?(0.42f*io):(0.20f*fh); }
        else P.mouthBind[3]=(io>1e-6f)?(0.42f*io):(0.20f*fh);
        #undef TOMESH
        P.mouthBind[4]=fh;
        if(getenv("REVA_MORPHDBG")||getenv("REVA_FACEDBG"))
            printf("[mouth] %s lip line in MESH space (%.3f,%.3f,%.3f) half-width %.3f (%s) faceh %.3f\n",
                   path,mo[0],mo[1],mo[2],P.mouthBind[3],src9,fh);
        free(rw); }
    P.mats=P.pk+Hd[8];
    P.meshTex=malloc(P.nMeshes*sizeof(int)); P.meshMatFlags=calloc(P.nMeshes,sizeof(unsigned));
    int *texFlip=calloc(P.nTex?P.nTex:1,sizeof(int)), *texNear=calloc(P.nTex?P.nTex:1,sizeof(int));
    P.meshNTex=malloc(P.nMeshes*sizeof(int));
    for(unsigned m=0;m<P.nMeshes;m++){ P.meshTex[m]=-1; P.meshNTex[m]=-1; }
    int nNrm=0;
    for(unsigned r=0;r<P.nMats;r++){ unsigned mesh=*(unsigned*)(P.mats+r*36);
        int tex=*(int*)(P.mats+r*36+24); unsigned fl=*(unsigned*)(P.mats+r*36+16);
        int ntex=*(int*)(P.mats+r*36+28)-1;   /* ⭐ the normal map (09-11): +1 in the file, so an old pack reads none */
        if(ntex<0||(unsigned)ntex>=P.nTex) ntex=-1;
        if(tex>=0&&(unsigned)tex<P.nTex){ if(fl&2) texFlip[tex]=1; if(fl&1) texNear[tex]=1; }
        if(ntex>=0){ if(fl&2) texFlip[ntex]=1; }
        if(mesh<P.nMeshes&&P.meshTex[mesh]==-1){ P.meshTex[mesh]=tex; P.meshMatFlags[mesh]=fl; P.meshNTex[mesh]=ntex; if(ntex>=0) nNrm++; } }
    if(nNrm) printf("[normal] %s: %d of %u meshes carry a normal map\n",P.label,nNrm,P.nMeshes);
    { int n9=0; for(unsigned m=0;m<P.nMeshes;m++) if(P.meshMatFlags[m]&16u) n9++;   /* (11-11) GFX_OCCLUSION: a mouth's inside */
      if(n9&&!g_loadQuiet) printf("[material] %s: %d meshes take their share of the room from their texture's alpha (flag 16)\n",P.label,n9); }
    P.tex=calloc(P.nTex?P.nTex:1,sizeof(unsigned));
    unsigned char*texTab=P.pk+Hd[10];
    /* ⭐ RPK2 (11-02, design/pack_format.md): a footer at the very end names chunks after the old body. TEXC holds what the cook compressed, every level ready for the GPU */
    unsigned char*texc=NULL; unsigned texcN=0;
    if(P.len>=16&&!memcmp(P.pk+P.len-4,"RPK2",4)){ unsigned f9[3]; memcpy(f9,P.pk+P.len-16,12);
        if(f9[2]!=P.len||(size_t)f9[0]+(size_t)f9[1]*16>P.len-16){ printf("[pack] %s: a broken RPK2 footer, ignored\n",path); fflush(stdout); }
        else for(unsigned d9=0;d9<f9[1];d9++){ unsigned char*e9=P.pk+f9[0]+d9*16; unsigned o9,s9; memcpy(&o9,e9+4,4); memcpy(&s9,e9+8,4);
            if(!memcmp(e9,"TEXC",4)&&s9>=4&&(size_t)o9+s9<=P.len){ texc=P.pk+o9; memcpy(&texcN,texc,4);
                if(texcN>(s9-4u)/32u){ printf("[pack] %s: a TEXC chunk that doesn't add up, ignored\n",path); fflush(stdout); texc=NULL; texcN=0; } } } }
    for(unsigned t=0;t<P.nTex;t++){ unsigned off=*(unsigned*)(texTab+t*16+8), len=*(unsigned*)(texTab+t*16+12);
        unsigned bcRec[8]; const unsigned*bc9=NULL; for(unsigned k9=0;k9<texcN;k9++){ memcpy(bcRec,texc+4+(size_t)k9*32,32); if(bcRec[0]==t){ bc9=bcRec; break; } }
        if(bc9&&!packTexcOk(bc9,P.len)){ printf("[pack] %s: texture %u's cooked copy doesn't fit the file, left out\n",path,t); fflush(stdout); bc9=NULL; }   /* (step 28) its size, its levels and where they are, before a byte of it is read */
        int tw,th,comp; unsigned char*img;
        if(bc9){
#ifdef REVA_VK
            if(gfxHasBC()&&(bc9[1]!=7||gfxHasBC7())){ P.tex[t] = gfxTextureNew(); gfxTextureBC(P.tex[t],(int)bc9[2],(int)bc9[3],(int)bc9[4],(int)bc9[1],P.pk+bc9[6],(int)(bc9[5]&1)); gfxTexturePage(P.tex[t]); continue; }
#endif
#ifdef REVA_HEADLESS
            P.tex[t] = gfxTextureNew(); continue;
#endif
            tw=(int)bc9[2]; th=(int)bc9[3]; img=bcDecode(P.pk+bc9[6],tw,th,(int)bc9[1]); }   /* no BC on this path: the top level as plain pixels */
        else { stbi_set_flip_vertically_on_load(texFlip[t]);
            img=stbi_load_from_memory(P.pk+off,(int)len,&tw,&th,&comp,4); }
        if(!img){ printf("[pack] %s: texture %u has no image\n",path,t); fflush(stdout); continue; }
        P.tex[t] = gfxTextureNew();
        gfxTexture(P.tex[t], tw, th, img);
        /* (11-06) the colour of a hair mesh's picture where it's opaque, as it was authored: its hairs, drawn one by one, take it (engine/strands.inc; Pack.hairRGB, its
         * fourth 1 once it's measured) */
        if(P.hairRGB[3]<=0.f){ int hair9=0; for(unsigned m9=0;m9<P.nMeshes&&!hair9;m9++){ if(P.meshTex[m9]!=(int)t) continue; const char*n9=packStr(&P,P.meshes[m9].name); if(n9&&(strstr(n9,"hair")||strstr(n9,"Hair"))) hair9=1; }
            if(hair9){ double s9[3]={0,0,0},w9=0.0; const size_t nn9=(size_t)tw*(size_t)th; for(size_t k9=0;k9<nn9;k9+=3){ const unsigned char*q9=img+k9*4; if(q9[3]<77) continue; const double a9=q9[3]/255.0; s9[0]+=q9[0]*a9; s9[1]+=q9[1]*a9; s9[2]+=q9[2]*a9; w9+=a9; }
                if(w9>0.0){ for(int c9=0;c9<3;c9++) P.hairRGB[c9]=(float)(s9[c9]/w9/255.0); P.hairRGB[3]=1.f; } } }
#ifdef REVA_VK
        gfxTexturePage(P.tex[t]);
#endif
        /* ⭐ MIPMAPS: the renderer makes them for every texture it's handed (the old road never did, and alpha-tested hair came out as salt-and-pepper stipple) */

        /* REPEAT, NOT CLAMP. A pack's UVs are not guaranteed to sit inside [0,1]:
         * MEASURED on SK_veh_Sedan_01, u runs -1.538..0.955, because the mesh was
         * authored expecting the sampler to wrap. Clamped, every negative coordinate
         * pins to u=0 -- the LEFT EDGE of the colour palette, which is where that atlas
         * keeps its labelled swatch chart -- so the car drove around wearing a picture
         * of the texture's own legend, mirrored text and all. It was parked beside the
         * porch in every act III close-up.
         * The props that were fine (mailbox 0.217..0.782, pickup 0.463..0.851) were fine
         * only because they happened to stay inside the tile; wrap mode never mattered
         * for them, so this cannot make them worse. */
        if(g_texKeep&&t==0&&!g_texKeepPx){ g_texKeepPx=img; g_texKeepW=tw; g_texKeepH=th; g_texKeepFlip=texFlip[t]; }   /* a mover's sheet of pictures: kept, and its line frees it */
        else stbi_image_free(img); }
    free(texFlip); free(texNear);
    M4 *world=malloc(P.nNodes*sizeof(M4));
    float mny=1e9f,mxy=-1e9f,mnx=1e9f,mxx=-1e9f,mnz=1e9f,mxz=-1e9f;
    for(unsigned i=0;i<P.nNodes;i++){ M4 loc; m4_trs(loc,P.nodes[i].t,P.nodes[i].q,P.nodes[i].s);
        if(P.nodes[i].parent<0) memcpy(world[i],loc,64); else m4_mul(world[i],world[P.nodes[i].parent],loc);
        if(P.nodes[i].kind==1){ float y=world[i][13]; if(y<mny)mny=y; if(y>mxy)mxy=y;
            float x=world[i][12]; if(x<mnx)mnx=x; if(x>mxx)mxx=x;
            float z=world[i][14]; if(z<mnz)mnz=z; if(z>mxz)mxz=z; } }
    /* ⛔ (11-04) A PACK WITH NO BONES AT ALL: a prop made by code with a plain root. Nothing was measured, its floor stayed at a billion, and a `prop` line stood it a billion
     * units under the world: the talk game's room was never on screen, and its walls never stopped a lens. Its own origin is its floor. */
    if(mny>1e8f){ mny=mxy=0.f; mnx=mxx=0.f; mnz=mxz=0.f; }
    /* ---- H1: mine the SECONDARY-MOTION CHAINS while the rest FK is still alive.
     * Families: tail/pony/braid/hair/bang/tress/ribbon, plus "ear" behind guards --
     * "forearm", "rear", "spear", "beard", "heart" and "gear" all contain it and are
     * not ears (a name you didn't audit is a name that will lie). A chain starts at a
     * matching bone whose parent does not match and walks matching children. */
    P.nCh=0;
    P.toon=0; P.trueScale=0; P.hairWhole=0; P.trueH=0.f; P.vcol=0; P.hcol=0; P.headOK=0; P.glow=0; P.nocast=0; P.open=0; P.sheer=0.f; P.mirOnly=0;
    for(unsigned i=0;i<P.nNodes;i++){ const char*nm9=packStr(&P,P.nodes[i].name);
        if(!strcmp(nm9,"ToonStyle")&&P.toon<1) P.toon=1;
        if(!strcmp(nm9,"ToonSet")) P.toon=2;
        if(!strcmp(nm9,"Glow")) P.glow=1;
        if(!strcmp(nm9,"NoCast")) P.nocast=1;
        if(!strcmp(nm9,"Open")) P.open=1;
        if(!strcmp(nm9,"MirrorOnly")) P.mirOnly=1;
        if(!strcmp(nm9,"Floor")) P.floor=1;
        if(!strncmp(nm9,"Alpha",5)&&nm9[5]>='0'&&nm9[5]<='9') P.sheer=(float)atoi(nm9+5)/100.f;
        if(!strcmp(nm9,"TrueScale")) P.trueScale=1;
        if(!strcmp(nm9,"HairWhole")) P.hairWhole=1;
        if(!strcmp(nm9,"VertexCollide")) P.vcol=1;
        if(!strcmp(nm9,"HairCollide")) P.hcol=1;   /* (11-06) the hair's rules alone: pack.inc, HAIR THAT COLLIDES */
        if(!strncmp(nm9,"HairStrands",11)){ const int n9=atoi(nm9+11); P.hstr=n9>=2?n9:10; }
        if(!strcmp(nm9,"HairKaleido")) P.hkal=1; }   /* (11-09) kaleidoscope hair: its colours flow (body.inc vkMesh, mesh.frag) */   /* (11-06) its hair, lock by lock: engine/strands.inc */   /* (11-06) the hair's rules alone: pack.inc, HAIR THAT COLLIDES */
    { const unsigned N=P.nNodes;
      /* ⭐ "cloth" (09-29): a garment's free panel -- REVA's jacket hanging at her elbows, her bandana -- is a
       * chain like a ponytail: rooted in the body, swung by the same solver. Godot's ClubHair mines it too. */
      #define CHNAME(nm) (strstr(nm,"tail")||strstr(nm,"pony")||strstr(nm,"braid")||strstr(nm,"cloth")|| \
          strstr(nm,"hair")||strstr(nm,"bang")||strstr(nm,"tress")||strstr(nm,"ribbon")|| \
          (strstr(nm,"ear")&&!strstr(nm,"arm")&&!strstr(nm,"rear")&&!strstr(nm,"spear")&& \
           !strstr(nm,"beard")&&!strstr(nm,"heart")&&!strstr(nm,"gear")))
      for(unsigned i=0;i<N&&P.nCh<CHMAXC;i++){
        char lo[96]; const char*s0=packStr(&P,P.nodes[i].name); unsigned k;
        for(k=0;k<95&&s0[k];k++) lo[k]=(char)((s0[k]>='A'&&s0[k]<='Z')?s0[k]+32:s0[k]);
        lo[k]=0;
        if(!CHNAME(lo)) continue;
        int par=P.nodes[i].parent; int parMatch=0;
        if(par>=0){ char lp[96]; const char*s1=packStr(&P,P.nodes[par].name);
            for(k=0;k<95&&s1[k];k++) lp[k]=(char)((s1[k]>='A'&&s1[k]<='Z')?s1[k]+32:s1[k]);
            lp[k]=0;
            parMatch=CHNAME(lp); }
        if(parMatch) continue;                      /* not a chain root */
        /* \u26d4 A BRANCH IS A CHAIN, AND THIS USED TO TAKE THE FIRST CHILD AND STOP. Anima
         * carries two seven-bone braids (hair_braids_01_l..07_l and 01_r..07_r) with 22,380
         * verts weighted to them; from the root `hair_braids` the walk found the leaf
         * `high_braids` first and quit, mining ONE chain of TWO bones and discarding both
         * braids. Hair branches -- braids and bunches come in pairs -- so a miner that keeps
         * one branch keeps the wrong one.
         * \u2b50 Walk EVERY matching child as its own chain. The 8-chain budget still caps
         * it, so a busy hair rig takes the first eight instead of nothing. */
        int starts[8]; int nStarts=0;
        { int anyKid=0;
          for(unsigned j=0;j<N&&nStarts<8;j++) if(P.nodes[j].parent==(int)i){
              char lc[96]; const char*s2=packStr(&P,P.nodes[j].name);
              for(k=0;k<95&&s2[k];k++) lc[k]=(char)((s2[k]>='A'&&s2[k]<='Z')?s2[k]+32:s2[k]);
              lc[k]=0;
              if(CHNAME(lc)){ starts[nStarts++]=(int)j; anyKid=1; } }
          if(!anyKid){ starts[0]=-1; nStarts=1; } }
        for(int sIdx=0;sIdx<nStarts&&P.nCh<CHMAXC;sIdx++){
        int n=0; unsigned cur=i;
        int first=starts[sIdx];
        while(n<14){ P.chB[P.nCh][n++]=(short)cur;
            int nxt=-1;
            if(n==1&&first>=0) nxt=first;           /* this branch, not merely the first */
            else for(unsigned j=0;j<N;j++) if(P.nodes[j].parent==(int)cur){
                char lc[96]; const char*s2=packStr(&P,P.nodes[j].name);
                for(k=0;k<95&&s2[k];k++) lc[k]=(char)((s2[k]>='A'&&s2[k]<='Z')?s2[k]+32:s2[k]);
                lc[k]=0;
                if(CHNAME(lc)){ nxt=(int)j; break; } }
            if(nxt<0) break;
            cur=(unsigned)nxt; }
        if(n<2) continue;                            /* a lone bone cannot swing */
        for(int q9=1;q9<n;q9++){ float*a=&world[P.chB[P.nCh][q9-1]][12],*b=&world[P.chB[P.nCh][q9]][12];
            float dx=b[0]-a[0],dy=b[1]-a[1],dz=b[2]-a[2];
            P.chLen[P.nCh][q9]=sqrtf(dx*dx+dy*dy+dz*dz); }
        P.chN[P.nCh]=(signed char)n; P.nCh++; } }
      if(P.nCh>=CHMAXC){ printf("[chains] \u26d4 %s: the %d-chain budget is FULL -- later chains were not mined\n",
                                P.label[0]?P.label:path,CHMAXC); fflush(stdout); }
      #undef CHNAME
    }
    /* ---- ⭐ JIGGLE BONES: a bone named for soft mass (breast) whose parent is not, and its first such child as
     * the tip. NOT a chain: a chain hangs and swings on gravity like a ponytail; soft mass springs back to the
     * animated pose. REVA_NOJIGGLE=1 is the control. */
    P.nJg=0;
    for(unsigned i=0;i<P.nNodes&&P.nJg<4;i++){ char lo[96]; const char*s0=packStr(&P,P.nodes[i].name); unsigned k;
        for(k=0;k<95&&s0[k];k++) lo[k]=(char)((s0[k]>='A'&&s0[k]<='Z')?s0[k]+32:s0[k]);
        lo[k]=0;
        if(!strstr(lo,"breast")) continue;
        int par=P.nodes[i].parent; if(par>=0){ char lp[96]; const char*s1=packStr(&P,P.nodes[par].name);
            for(k=0;k<95&&s1[k];k++) lp[k]=(char)((s1[k]>='A'&&s1[k]<='Z')?s1[k]+32:s1[k]);
            lp[k]=0;
            if(strstr(lp,"breast")) continue; }
        int tip=-1; for(unsigned j=0;j<P.nNodes;j++) if(P.nodes[j].parent==(int)i){ char lc[96]; const char*s2=packStr(&P,P.nodes[j].name);
            for(k=0;k<95&&s2[k];k++) lc[k]=(char)((s2[k]>='A'&&s2[k]<='Z')?s2[k]+32:s2[k]);
            lc[k]=0;
            if(strstr(lc,"breast")){ tip=(int)j; break; } }
        if(tip<0) continue;
        P.jgRoot[P.nJg]=(short)i; P.jgTip[P.nJg]=(short)tip; P.nJg++; }
    if(P.nJg){ printf("[jiggle] %s: %d jiggle bone(s) --",P.label[0]?P.label:path,P.nJg);
        for(int q=0;q<P.nJg;q++) printf(" %s",packStr(&P,P.nodes[P.jgRoot[q]].name));
        printf("\n"); fflush(stdout); }
    /* ---- ⭐ STRETCH BONES: Stretch_<x> and its Anchor_<x>, their rest line in the stretch bone's own frame ---- */
    P.nSt=0;
    for(unsigned i=0;i<P.nNodes&&P.nSt<8;i++){ const char*s0=packStr(&P,P.nodes[i].name);
        if(strncmp(s0,"Stretch_",8)) continue;
        char want[96]; snprintf(want,sizeof want,"Anchor_%s",s0+8);
        int a=-1; for(unsigned j=0;j<P.nNodes;j++) if(!strcmp(packStr(&P,P.nodes[j].name),want)){ a=(int)j; break; }
        if(a<0){ printf("[stretch] \u26d4 %s: %s has no %s -- it stays a plain bone\n",P.label[0]?P.label:path,s0,want);
            fflush(stdout); continue; }
        float*m=world[i]; float d[3]={world[a][12]-m[12],world[a][13]-m[13],world[a][14]-m[14]};
        float L=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(L<1e-6f) continue;
        float loc[3];                                    /* the rest line, in the bone's own (possibly scaled) frame */
        for(int c=0;c<3;c++){ float cl2=m[c*4]*m[c*4]+m[c*4+1]*m[c*4+1]+m[c*4+2]*m[c*4+2]; if(cl2<1e-12f) cl2=1.f;
            loc[c]=(m[c*4]*d[0]+m[c*4+1]*d[1]+m[c*4+2]*d[2])/cl2; }
        P.stB[P.nSt]=(short)i; P.stA[P.nSt]=(short)a; P.stLen[P.nSt]=L;
        P.stDir[P.nSt][0]=loc[0]; P.stDir[P.nSt][1]=loc[1]; P.stDir[P.nSt][2]=loc[2]; P.nSt++; }
    if(P.nSt){ printf("[stretch] %s: %d stretch bone(s) --",P.label[0]?P.label:path,P.nSt);
        for(int q=0;q<P.nSt;q++) printf(" %s->%s (%.3f)",packStr(&P,P.nodes[P.stB[q]].name),packStr(&P,P.nodes[P.stA[q]].name),P.stLen[q]);
        printf("\n"); fflush(stdout); }
    free(world);
    float span=(mxy-mny); P.scale=(span>1e-4f)?(3.15f/(span*1.13f)):1.0f;
    P.cx=(mnx+mxx)*0.5f; P.cz=(mnz+mxz)*0.5f; P.mny=mny;
    /* ⭐ AND ITS FOOTPRINT, from the same pass. Half the larger XZ span: a circle that
     * contains the thing, which is what the collider set is built out of. Costs nothing --
     * the extents were already being computed two lines up and thrown away. */
    /* ⛔ AND IT CANNOT COME FROM THE BONE CLOUD. mnx/mxx above are measured over
     * nodes[i].kind==1 -- BONES -- and a prop has none, so every mailbox, van and tree
     * measured zero and the collider set came back empty on a street full of things.
     * Props are MESHES. Measured over the vertices, which is the only thing they have. */
    { float px0=1e9f,px1=-1e9f,pz0=1e9f,pz1=-1e9f,py0=1e9f,py1=-1e9f;
      for(unsigned mi=0;mi<P.nMeshes;mi++){ PMesh*M=&P.meshes[mi];
        float*vp=(float*)(P.pk+M->offPos);
        unsigned step=(M->verts>2048)?(M->verts/2048):1;      /* a bound needs a sample, not every vertex */
        for(unsigned v=0;v<M->verts;v+=step){
            float x=vp[v*3+0], y=vp[v*3+1], z=vp[v*3+2];
            if(x<px0)px0=x;
            if(x>px1)px1=x;
            if(z<pz0)pz0=z;
            if(z>pz1)pz1=z;
            if(y<py0)py0=y;
            if(y>py1)py1=y; } }
      P.propY0=(py1>py0)?py0:0.f; P.propY1=(py1>py0)?py1:0.f;   /* ⭐ the vertex height range (09-11): effH is a 3.15 FLOOR, a crate is 0.3 tall */
      float ex=(px1>px0)?(px1-px0)*0.5f:0.f, ez=(pz1>pz0)?(pz1-pz0)*0.5f:0.f;
      P.propR=(ex>ez?ex:ez); if(!(P.propR>0.f)) P.propR=0.f;
      P.propEx=ex>0.f?ex:0.f; P.propEz=ez>0.f?ez:0.f;
      P.propCx=(px1>px0)?(px0+px1)*0.5f:0.f; P.propCz=(pz1>pz0)?(pz0+pz1)*0.5f:0.f; }   /* one grounding rule for EVERYONE (reva and the giantess stood on another plane) */
    P.effH=3.15f;
    { unsigned*Hd2=(unsigned*)(P.pk+4); float*szb=(float*)(P.pk+Hd2[12]); float meshY=szb[9];
      if(meshY==meshY&&meshY>0){ float mh=meshY*P.scale; if(mh>P.effH) P.effH=mh; }
      /* GIANTESS GROUNDING (she stood in the floor up to her waist): mixed-convention rigs carry a MESH
       * far below the bone cloud -- the bind Y floor (sizing ibmY0) is the true sole. Ground by
       * whichever is lower; bone-only grounding sank her to the waist. */
      float iy0=szb[7];
      if(szb[10]==1.0f&&iy0==iy0) P.mny=iy0;   /* MEASURED packs: ground at the bench-verified sole EXACTLY (floaters like pakistan had junk bone floors) */
      else if(iy0==iy0&&iy0<P.mny) P.mny=iy0;
      /* SPECK RESCUE — MEASURED packs only (the giants: legacy page meshY has
       * per-family semantics; rescaling by it blew spy/manny/thailand-class up 4-5x). The
       * measured flag (szb[10]) means meshY is bench-verified raw render units. */
      if(szb[10]==1.0f&&meshY==meshY&&meshY>1e-4f){ float mh2=meshY*P.scale; if(mh2<1.6f) P.scale=3.15f/(meshY*1.05f); } }
    /* SKY HIERARCHY (the starfish is the largest thing in the sky, everyone else smaller) */
    if(P.fly){ char lo3[64]; for(int k3=0;k3<64;k3++){ char c3=P.label[k3]; lo3[k3]=(c3>='A'&&c3<='Z')?c3+32:c3; if(!c3) break; }
        if(!strstr(lo3,"starfish")&&!strstr(lo3,"star fish")) P.scale*=0.55f; }   /* the starfish is UNTOUCHABLE -- it reigns at its own size */
    /* THE GIANTESS, ON PURPOSE: hina's rig is HONEST now (re-seated rest,
     * measured sizing -> feet ON the floor, sane deformation); the giant is a deliberate 2.3x. */
    if(strstr(P.label,"hina")) P.scale*=2.3f;
    if(P.trueScale){ P.scale=1.75f;                 /* TRUE SIZE: metres -> club units, never normalised */
        P.trueH=(mxy-mny)*1.75f;
        printf("[size] %s: TRUE SIZE, %.2f m -> %.2f club units tall\n",P.label,(mxy-mny),(mxy-mny)*1.75f); fflush(stdout); }
    P.avoidR=2.4f*fminf(3.f,fmaxf(1.f,P.effH/3.15f));   /* the giantess gets giantess clearance */
    P.walkIdx=-1;
    for(unsigned c=0;c<P.nClips;c++){ const char*nm=packStr(&P,P.clips[c].name);
        if(!strncmp(nm,"w_",2)||strstr(nm,"alk")){ P.walkIdx=(int)c; break; } }
    /* S4a BEHAVIOR ROLES (the packs already hold plenty of authored movement): mine the
     * clips this pack ALREADY carries for the vocabulary the modes need. Lowercased name
     * match; -1 falls back to the procedural fills at play time. REVA_CLIPDUMP=1 = census. */
    P.runIdx=-1; P.atkIdx=-1; P.downIdx=-1; P.idleIdx=-1;
    for(unsigned c=0;c<P.nClips;c++){ const char*nm=packStr(&P,P.clips[c].name);
        /* S7 "idle" DOES NOT MEAN STANDING. crowd_susan ships idle, laying_idle and
         * sleeping_idle; a bare substring match on "idle" put her to sleep on the floor
         * for the whole of IDLE. Names that describe the FLOOR are not stands. */
        char lo[96]; int k=0; for(;k<95&&nm[k];k++){ char ch=nm[k]; lo[k]=(ch>='A'&&ch<='Z')?ch+32:ch; } lo[k]=0;
        if(P.runIdx<0&&(strstr(lo,"run")||strstr(lo,"sprint")||strstr(lo,"flee"))) P.runIdx=(int)c;
        if(P.atkIdx<0&&(strstr(lo,"attack")||strstr(lo,"punch")||strstr(lo,"kick")||strstr(lo,"slash")||strstr(lo,"swing")||strstr(lo,"strike")||strstr(lo,"jab"))) P.atkIdx=(int)c;
        if(P.downIdx<0&&(strstr(lo,"death")||strstr(lo,"dying")||strstr(lo,"dead")||strstr(lo,"fall")||strstr(lo,"down")||strstr(lo,"defeat"))) P.downIdx=(int)c;
        if(P.idleIdx<0&&!strstr(lo,"tpose")&&!strstr(lo,"t-pose")&&!floorClipName(lo)
           &&(strstr(lo,"idle")||strstr(lo,"stand"))
           &&clipLoopsOK(&P,&P.clips[c],P.label,nm)) P.idleIdx=(int)c; }
    for(int k=0;k<NCANON;k++) P.pb[k]=-1;
    P.pbOK=0;
    P.nodeCanon=malloc(P.nNodes*sizeof(short));
    for(unsigned i=0;i<P.nNodes;i++) P.nodeCanon[i]=-1;
    for(unsigned i=0;i<P.nNodes;i++){ if(P.nodes[i].kind!=1) continue;
        int c=canonOf(packStr(&P,P.nodes[i].name));
        if(c>=0){ if(P.pb[c]<0){ P.pb[c]=(int)i; P.pbOK++; }
                  if(P.nodeCanon[i]<0) P.nodeCanon[i]=(short)c; } }
    /* P5 SPINE CHAIN BY TOPOLOGY. Name digits lie in both directions: UE spine_01/02/03
     * all collapse to canon 1 (two of three spine bones DROPPED from every transfer --
     * a 45-degree authored bow arrived as 27), and kaeli's Spine02 is her LOWEST spine
     * bone. Walk the real parent chain from the neck to the hips: every spine-ish node
     * between them takes canon 1,2,3 in hips-to-neck order. */
    if(P.pb[0]>=0&&P.pb[4]>=0){
        int chain[8]; int cn9=0;
        for(int a=P.nodes[P.pb[4]].parent;a>=0&&a!=P.pb[0]&&cn9<8;a=P.nodes[a].parent){
            const char*nm9=packStr(&P,P.nodes[a].name); char lo9[64]; int k9=0;
            for(;k9<63&&nm9[k9];k9++){ char c9=nm9[k9]; lo9[k9]=(c9>='A'&&c9<='Z')?c9+32:c9; } lo9[k9]=0;
            if(strstr(lo9,"spine")||strstr(lo9,"chest")) chain[cn9++]=a; }
        if(cn9>0){
            for(int k9=1;k9<=3;k9++){ if(P.pb[k9]>=0){ P.pbOK--; } P.pb[k9]=-1; }
            for(int k9=0;k9<cn9&&k9<3;k9++){        /* chain[] is neck-side first: reverse */
                int node=chain[cn9-1-k9];
                P.pb[1+k9]=node; P.pbOK++;
                P.nodeCanon[node]=(short)(1+k9); } } }
    /* S5a THE GRIP. The canon stops at the forearm (no hand bone), but every rig parents
     * its hand to that forearm -- so the forearm's first child IS the hand, whatever the
     * rig calls it. That is where a gun or a bat rides. */
    P.gripNode=-1;
    /* P2: "the forearm's FIRST child" put manny's weapons (and the pose dump's hand) at
     * his ELBOW -- UE rigs hang a zero-length leaf off every joint, and it enumerates
     * first. The honest rule, same spirit as the feet below: prefer a child NAMED hand;
     * otherwise the child that sits FARTHEST down the bone. Zero-length leaves lose. */
    P.gripL=-1;
    for(int side9=0;side9<2;side9++){ int fore9=side9?P.pb[8]:P.pb[11]; if(fore9<0) continue;
        float bestD=-1.f; int pick=-1;
        for(unsigned i=0;i<P.nNodes;i++){ if(P.nodes[i].parent!=fore9) continue;
            const char*nm2=packStr(&P,P.nodes[i].name); char lo2[32]; int k2=0;
            for(;k2<31&&nm2[k2];k2++){ char c2=nm2[k2]; lo2[k2]=(c2>='A'&&c2<='Z')?c2+32:c2; } lo2[k2]=0;
            float d2=P.nodes[i].t[0]*P.nodes[i].t[0]+P.nodes[i].t[1]*P.nodes[i].t[1]+P.nodes[i].t[2]*P.nodes[i].t[2];
            if(strstr(lo2,"hand")) d2+=1e12f;
            if(d2>bestD){ bestD=d2; pick=(int)i; } }
        if(side9) P.gripL=(pick>=0)?pick:fore9; else P.gripNode=(pick>=0)?pick:fore9; }
    /* ⛔ A TWIST BONE IS NOT A FOREARM, AND THE CHILD SEARCH CANNOT SEE PAST ONE.
     * On anima the canonical forearm resolves to `lowerarm_twist_01_r` while `hand_r`
     * hangs off `lowerarm_r` -- so no child was named hand, the grip fell back to the
     * twist bone itself, and every prop she was handed rode the middle of her forearm.
     * With her arms at her sides that is exactly hip height, which is where
     * a coffee mug floated for the whole porch scene.
     * ⭐ A HAND IS A BONE WITH "hand" IN ITS NAME. If walking the chain missed it, take
     * it by name -- matching the side on a trailing `_r`/`_l` (not a bare substring, or
     * `hand_root` would answer for the right hand). */
    for(int side9=0;side9<2;side9++){
        int cur9=side9?P.gripL:P.gripNode;
        char cl[40]; int k3=0;
        if(cur9>=0){ const char*cn=packStr(&P,P.nodes[cur9].name);
            for(;k3<39&&cn[k3];k3++){ char c3=cn[k3]; cl[k3]=(c3>='A'&&c3<='Z')?c3+32:c3; } }
        cl[k3]=0;
        /* ...unless it is the SAME bone as the right hand, which would make `@lhand` a lie. A GUARD,
         * never observed: ⛔ it was written while chasing a `[grip]` print that called packStr twice
         * in one printf (one static buffer, so both hands printed the same name). The election
         * itself was right. */
        if(strstr(cl,"hand")&&!(side9&&cur9==P.gripNode)) continue;   /* already a hand: leave it alone */
        for(unsigned i=0;i<P.nNodes;i++){
            const char*nm3=packStr(&P,P.nodes[i].name); char lo3[40]; int k4=0;
            for(;k4<39&&nm3[k4];k4++){ char c4=nm3[k4]; lo3[k4]=(c4>='A'&&c4<='Z')?c4+32:c4; } lo3[k4]=0;
            if(!strstr(lo3,"hand")) continue;
            size_t L3=strlen(lo3);
            int isR=(L3>=2&&lo3[L3-2]=='_'&&lo3[L3-1]=='r')||strstr(lo3,"right")!=NULL;
            int isL=(L3>=2&&lo3[L3-2]=='_'&&lo3[L3-1]=='l')||strstr(lo3,"left")!=NULL;
            if(side9?isL:isR){
                if(side9) P.gripL=(int)i; else P.gripNode=(int)i;
                printf("[grip] %s: %s hand taken by NAME (%s) -- the chain walk found a twist bone\n",
                       P.label,side9?"left":"right",nm3);
                break; } } }
    if(0){
        float bestD=-1.f;
        for(unsigned i=0;i<P.nNodes;i++){ if(P.nodes[i].parent!=P.pb[11]) continue;
            const char*nm2=packStr(&P,P.nodes[i].name); char lo2[32]; int k2=0;
            for(;k2<31&&nm2[k2];k2++){ char c2=nm2[k2]; lo2[k2]=(c2>='A'&&c2<='Z')?c2+32:c2; } lo2[k2]=0;
            float d2=P.nodes[i].t[0]*P.nodes[i].t[0]+P.nodes[i].t[1]*P.nodes[i].t[1]+P.nodes[i].t[2]*P.nodes[i].t[2];
            if(strstr(lo2,"hand")) d2+=1e12f;   /* a named hand always wins */
            if(d2>bestD){ bestD=d2; P.gripNode=(int)i; } }
        if(P.gripNode<0) P.gripNode=P.pb[11]; }
    /* S6 THE FEET. First try was "the lower leg's first child" -- which on the UE
     * mannequin picked the same bone the canon had already called the knee (shin length
     * 0.000, knee angle 0, feet floating a metre up: the lab caught it immediately).
     * The honest rule is geometric: of everything hanging off that leg bone, the FOOT is
     * whatever rests LOWEST in the bind pose. Rig-name independent, and it finds the toe
     * on rigs that have one. */
    P.footL=P.footR=-1;
    if(P.pb[13]>=0||P.pb[15]>=0){
        M4*RW=(M4*)malloc((size_t)P.nNodes*sizeof(M4));
        if(RW){
            for(unsigned i=0;i<P.nNodes;i++){ M4 loc; m4_trs(loc,P.nodes[i].t,P.nodes[i].q,P.nodes[i].s);
                if(P.nodes[i].parent<0) memcpy(RW[i],loc,64); else m4_mul(RW[i],RW[P.nodes[i].parent],loc); }
            for(int side=0;side<2;side++){
                int leg=side?P.pb[15]:P.pb[13]; if(leg<0) continue;
                int best=-1; float bestY=1e9f;
                for(unsigned i=0;i<P.nNodes;i++){
                    int a=(int)i, hops=0;
                    while(a>=0&&hops<10){ if(a==leg) break; a=P.nodes[a].parent; hops++; }
                    if(a!=leg||(int)i==leg) continue;      /* must be BELOW the knee */
                    if(RW[i][13]<bestY){ bestY=RW[i][13]; best=(int)i; } }
                if(best<0) best=leg;                        /* nothing hangs off it: fall back */
                if(side) P.footR=best; else P.footL=best; }
            free(RW); } }
    /* ⭐ ep 9: the wheels, by name -- the rig's own word for what touches the floor */
    P.nWheel=0;
    for(unsigned i=0;i<P.nNodes&&P.nWheel<8;i++){
        const char*nm9=packStr(&P,P.nodes[i].name); char lw9[64]; int k9=0;
        for(;nm9[k9]&&k9<63;k9++) lw9[k9]=(char)((nm9[k9]>='A'&&nm9[k9]<='Z')?nm9[k9]+32:nm9[k9]);
        lw9[k9]=0;
        if(strstr(lw9,"wheel")) P.wheel[P.nWheel++]=(int)i; }
    P.wheelYaw=0.f;
    if(P.nWheel>=2){
        /* the board's axis is the long side of the wheels' REST spread -- a rig's own geometry */
        M4*RW2=(M4*)malloc((size_t)P.nNodes*sizeof(M4));
        if(RW2){
            for(unsigned i=0;i<P.nNodes;i++){ M4 loc; m4_trs(loc,P.nodes[i].t,P.nodes[i].q,P.nodes[i].s);
                if(P.nodes[i].parent<0) memcpy(RW2[i],loc,64); else m4_mul(RW2[i],RW2[P.nodes[i].parent],loc); }
            float x0=1e9f,x1=-1e9f,z0=1e9f,z1=-1e9f;
            for(int w9=0;w9<P.nWheel;w9++){ float wx9=RW2[P.wheel[w9]][12], wz9=RW2[P.wheel[w9]][14];
                if(wx9<x0)x0=wx9;
                if(wx9>x1)x1=wx9;
                if(wz9<z0)z0=wz9;
                if(wz9>z1)z1=wz9; }
            P.wheelYaw=((z1-z0)>=(x1-x0))?0.f:-1.5707963f;
            printf("[load] %d wheel contact(s) elected -- this body stands on its wheels, not its feet; its board runs along rig %s (spread x %.2f z %.2f)\n",
                   P.nWheel,P.wheelYaw==0.f?"Z":"X",x1-x0,z1-z0); fflush(stdout);
            free(RW2); } }
    else if(P.nWheel){ printf("[load] %d wheel contact(s) elected -- this body stands on its wheels, not its feet\n",P.nWheel); fflush(stdout); }
    /* ================= P5 CANONICAL BASIS (computed at load) =========================
     * Everything the absolute cross-rig transfer needs, from THIS rig's own rest
     * geometry: world positions (M4 chain) and world rotations (quat chain), then per
     * canon bone a frame A (X toward the canonical child, Z = rig-forward projected),
     * the ancestor linkage, and the character frame G. Proven in Python first. */
    P.cbOK=0; for(int c9=0;c9<NCANON;c9++) P.cbV[c9]=0;
    if(P.pb[0]>=0&&P.pb[12]>=0&&P.pb[14]>=0&&P.nNodes<=1024){
        M4*RW=(M4*)malloc((size_t)P.nNodes*sizeof(M4));
        float(*wq)[4]=(float(*)[4])malloc((size_t)P.nNodes*16);
        if(RW&&wq){
            for(unsigned i=0;i<P.nNodes;i++){ M4 loc; m4_trs(loc,P.nodes[i].t,P.nodes[i].q,P.nodes[i].s);
                int pa=P.nodes[i].parent;
                if(pa<0){ memcpy(RW[i],loc,64); memcpy(wq[i],P.nodes[i].q,16); }
                else { m4_mul(RW[i],RW[pa],loc); qmul(wq[i],wq[pa],P.nodes[i].q); }
                float l9=sqrtf(wq[i][0]*wq[i][0]+wq[i][1]*wq[i][1]+wq[i][2]*wq[i][2]+wq[i][3]*wq[i][3]);
                if(l9>1e-6f){ wq[i][0]/=l9; wq[i][1]/=l9; wq[i][2]/=l9; wq[i][3]/=l9; } }
            /* rig forward from its own hips */
            float lat[3]={ RW[P.pb[12]][12]-RW[P.pb[14]][12], 0.f, RW[P.pb[12]][14]-RW[P.pb[14]][14] };
            float ll=sqrtf(lat[0]*lat[0]+lat[2]*lat[2]);
            if(ll>1e-6f){
                lat[0]/=ll; lat[2]/=ll;
                float fx=lat[2]*1.f-0.f, fz=0.f-lat[0]*1.f;   /* cross(lat, up) */
                float fwd[3]={fx,0.f,fz};
                float fl9=sqrtf(fx*fx+fz*fz); fwd[0]/=fl9; fwd[2]/=fl9;
                { float up9[3]={0,1,0}, zz[3]={fwd[1]*up9[2]-fwd[2]*up9[1],fwd[2]*up9[0]-fwd[0]*up9[2],fwd[0]*up9[1]-fwd[1]*up9[0]};
                  m3q(fwd,up9,zz,P.cbG); }
                int CH[NCANON];
                CH[0]=P.pb[1]; CH[1]=(P.pb[2]>=0)?P.pb[2]:P.pb[4]; CH[2]=(P.pb[3]>=0)?P.pb[3]:P.pb[4];
                CH[3]=P.pb[4]; CH[4]=P.pb[5]; CH[5]=-1; CH[6]=P.pb[7]; CH[7]=P.pb[8]; CH[8]=P.gripL;
                CH[9]=P.pb[10]; CH[10]=P.pb[11]; CH[11]=P.gripNode; CH[12]=P.pb[13]; CH[13]=P.footL;
                CH[14]=P.pb[15]; CH[15]=P.footR;
                /* ⭐ a finger joint aims at the next joint of the same finger; the last joint aims
                 * at its first child (the tip), and a joint with neither is skipped (09-11) */
                for(int c9=16;c9<NCANON;c9++){ int b9=P.pb[c9]; CH[c9]=-1; if(b9<0) continue;
                    int jn=(c9-16)%3;
                    if(jn<2&&P.pb[c9+1]>=0) CH[c9]=P.pb[c9+1];
                    else { for(unsigned k=0;k<P.nNodes;k++) if(P.nodes[k].parent==b9){ CH[c9]=(int)k; break; } } }
                int nv=0;
                for(int c9=0;c9<NCANON;c9++){
                    int b9=P.pb[c9]; if(b9<0) continue;
                    float X[3];
                    if(CH[c9]>=0){ X[0]=RW[CH[c9]][12]-RW[b9][12]; X[1]=RW[CH[c9]][13]-RW[b9][13]; X[2]=RW[CH[c9]][14]-RW[b9][14]; }
                    else if(c9==5&&P.pb[4]>=0){ X[0]=RW[b9][12]-RW[P.pb[4]][12]; X[1]=RW[b9][13]-RW[P.pb[4]][13]; X[2]=RW[b9][14]-RW[P.pb[4]][14]; }
                    else continue;
                    float xl=sqrtf(X[0]*X[0]+X[1]*X[1]+X[2]*X[2]); if(xl<1e-6f) continue;
                    X[0]/=xl; X[1]/=xl; X[2]/=xl;
                    float pr=X[0]*fwd[0]+X[2]*fwd[2];
                    float Z[3]={fwd[0]-X[0]*pr, -X[1]*pr, fwd[2]-X[2]*pr};
                    float zl=sqrtf(Z[0]*Z[0]+Z[1]*Z[1]+Z[2]*Z[2]);
                    if(zl<1e-4f){   /* bone points forward: fall back to up */
                        float pr2=X[1];
                        Z[0]=-X[0]*pr2; Z[1]=1.f-X[1]*pr2; Z[2]=-X[2]*pr2;
                        zl=sqrtf(Z[0]*Z[0]+Z[1]*Z[1]+Z[2]*Z[2]);
                        if(zl<1e-4f) continue; }
                    Z[0]/=zl; Z[1]/=zl; Z[2]/=zl;
                    float Y[3]={Z[1]*X[2]-Z[2]*X[1], Z[2]*X[0]-Z[0]*X[2], Z[0]*X[1]-Z[1]*X[0]};
                    m3q(X,Y,Z,P.cbA[c9]);
                    memcpy(P.cbW[c9],wq[b9],16);
                    int pa=P.nodes[b9].parent;
                    if(pa>=0) memcpy(P.cbWpar[c9],wq[pa],16);
                    else { P.cbWpar[c9][0]=0; P.cbWpar[c9][1]=0; P.cbWpar[c9][2]=0; P.cbWpar[c9][3]=1; }
                    P.cbAnc[c9]=-1;
                    for(int a=pa;a>=0;a=P.nodes[a].parent){
                        int cc=(P.nodeCanon&&P.nodeCanon[a]>=0)?P.nodeCanon[a]:-1;
                        if(cc>=0&&P.pb[cc]==a){ P.cbAnc[c9]=cc; break; } }
                    if(P.cbAnc[c9]>=0){ float ic[4]; qconj(ic,wq[P.pb[P.cbAnc[c9]]]);
                        qmul(P.cbPre[c9],ic,P.cbWpar[c9]); }
                    P.cbV[c9]=1; nv++; }
                if(nv>=12&&P.cbV[0]&&P.cbV[7]&&P.cbV[10]&&P.cbV[12]&&P.cbV[14]) P.cbOK=1;
                { int nf=0; for(int c9=16;c9<NCANON;c9++) if(P.cbV[c9]) nf++;
                  if(nf) printf("[canon] %s: %d body + %d finger canon bones carry a basis\n",P.label,nv-nf,nf); }
                if(P.pb[12]>=0&&P.pb[13]>=0&&P.footL>=0){
                    float dx1=RW[P.pb[13]][12]-RW[P.pb[12]][12], dy1=RW[P.pb[13]][13]-RW[P.pb[12]][13], dz1=RW[P.pb[13]][14]-RW[P.pb[12]][14];
                    float dx2=RW[P.footL][12]-RW[P.pb[13]][12], dy2=RW[P.footL][13]-RW[P.pb[13]][13], dz2=RW[P.footL][14]-RW[P.pb[13]][14];
                    P.cbLeg=sqrtf(dx1*dx1+dy1*dy1+dz1*dz1)+sqrtf(dx2*dx2+dy2*dy2+dz2*dz2); } } }
        free(RW); free(wq); }
    /* S6e: refuse a locomotion role whose clip is not an upright gait */
    if(P.footL>=0&&P.footR>=0&&P.pb[0]>=0){
        if(P.runIdx>=0&&!clipUpright(&P,P.runIdx)){
            printf("REJECT run clip '%s' on %s: not an upright gait\n",packStr(&P,P.clips[P.runIdx].name),P.label);
            P.runIdx=-1; }
        if(P.walkIdx>=0&&!clipUpright(&P,P.walkIdx)){
            printf("REJECT walk clip '%s' on %s: not an upright gait\n",packStr(&P,P.clips[P.walkIdx].name),P.label);
            P.walkIdx=-1; } }
    P.nDance=0;
    for(unsigned c=0;c<P.nClips&&P.nDance<48;c++){ const char*nm=packStr(&P,P.clips[c].name);
        if(nm[0]=='m'&&nm[1]=='_') P.danceIdx[P.nDance++]=(int)c; }
    if(!P.nDance) for(unsigned c=0;c<P.nClips&&P.nDance<48;c++) P.danceIdx[P.nDance++]=(int)c;
    /* S6d THE AUTHORED SPEED. An in-place cycle cannot tell you how fast it was walked;
     * its root-motion sibling can, and the bake measures it (skate_src/mob1/
     * make_gait_sidecar.py). Reading the number beats estimating it: the forward-
     * kinematics estimate came in 16% slow on the one clip that could be checked, and
     * 16% slow is a body outrunning its own feet. */
    P.gaitUPS=NULL;
    if(P.nClips){ char gp[512]; snprintf(gp,sizeof gp,"%s",path);
        char*dot=strrchr(gp,'.'); if(dot) strcpy(dot,".gait"); else strcat(gp,".gait");
        FILE*gf=fopen(gp,"r");
        if(gf){ P.gaitUPS=(float*)calloc(P.nClips,sizeof(float));
            char ln[256]; int got=0;
            while(P.gaitUPS&&fgets(ln,sizeof ln,gf)){
                if(ln[0]=='#'||ln[0]=='\n') continue;
                char nm[128]; float v;
                if(sscanf(ln,"%127s %f",nm,&v)!=2) continue;
                for(unsigned c=0;c<P.nClips;c++)
                    if(!strcmp(packStr(&P,P.clips[c].name),nm)){ P.gaitUPS[c]=v; got++; break; } }
            fclose(gf);
            printf("gait sidecar: %s -> %d clips\n",gp,got); } }
    { static int cdump=-1; if(cdump<0) cdump=getenv("REVA_CLIPDUMP")?1:0;   /* census AFTER pb: pbOK is real here */
      if(cdump){ printf("CLIPS %s:",P.label);
          for(unsigned c=0;c<P.nClips;c++) printf(" [%u]%s(%uf)",c,packStr(&P,P.clips[c].name),P.clips[c].frames);
          printf("  -> walk=%d run=%d atk=%d down=%d idle=%d pb=%d\n",P.walkIdx,P.runIdx,P.atkIdx,P.downIdx,P.idleIdx,P.pbOK); } }
    /* \u2b50 BLENDED MESHES GO LAST, ONCE, HERE. Every draw path walks m=0..nMeshes in
     * order, so fixing the ORDER at load fixes the draw order for the hero, the partner, a
     * set actor, the crowd and the multi-cam wall together -- rather than teaching nine
     * loops to sort. A stable partition: opaque keep their order, blended keep theirs. */
    /* ⛔ AND EVERY PER-MESH ARRAY MOVES WITH IT. FOUR things are indexed by mesh
     * POSITION -- P.meshes, P.meshTex, P.meshMatFlags and P.morphMesh -- and permuting
     * only the first
     * desynchronises the others: her hair drew with the DRESS's texture, pale blue ribbons
     * across her face. That was the second symptom of the same mistake; the first was the
     * VBOs, which is why the reorder now happens before they are built. Build the permutation
     * ONCE and apply it to all three. */
    { unsigned nb=0; for(unsigned m=0;m<P.nMeshes;m++) if(P.meshes[m].flags&8u) nb++;
      if(nb&&nb<P.nMeshes){
        unsigned*ord=(unsigned*)malloc(sizeof(unsigned)*P.nMeshes); unsigned k=0;
        for(unsigned m=0;m<P.nMeshes;m++) if(!(P.meshes[m].flags&8u)) ord[k++]=m;
        for(unsigned m=0;m<P.nMeshes;m++) if(  P.meshes[m].flags&8u ) ord[k++]=m;
        PMesh*tm=(PMesh*)malloc(sizeof(PMesh)*P.nMeshes);
        int*tt=(int*)malloc(sizeof(int)*P.nMeshes);
        int*tn=(int*)malloc(sizeof(int)*P.nMeshes);   /* the fifth per-mesh array (09-11): the normal map slot */
        unsigned*tf=(unsigned*)malloc(sizeof(unsigned)*P.nMeshes);
        for(unsigned m=0;m<P.nMeshes;m++){ tm[m]=P.meshes[ord[m]];
            tt[m]=P.meshTex?P.meshTex[ord[m]]:-1;
            tn[m]=P.meshNTex?P.meshNTex[ord[m]]:-1;
            tf[m]=P.meshMatFlags?P.meshMatFlags[ord[m]]:0u; }
        memcpy(P.meshes,tm,sizeof(PMesh)*P.nMeshes);
        if(P.meshTex) memcpy(P.meshTex,tt,sizeof(int)*P.nMeshes);
        if(P.meshNTex) memcpy(P.meshNTex,tn,sizeof(int)*P.nMeshes);
        free(tn);
        if(P.meshMatFlags) memcpy(P.meshMatFlags,tf,sizeof(unsigned)*P.nMeshes);
        /* \u26d4 AND THE MORPHS ARE THE FOURTH SUCH ARRAY -- the warning above listed
         * three because there were three when it was written. P.morphMesh is filled from
         * the header BEFORE this block runs, so each morph held a FILE mesh index into a
         * list that had just been permuted.
         * It did not read as "no expression": Anima's jaw is recorded against pack mesh 2
         * (anima_body, 67,300 verts) and engine mesh 2 after the reorder is anima_BOOTS
         * with 51,660, so the blend pushed face deltas into the boots' vertex buffer past
         * its end -- the old renderer's buffer update failed (0x502) and her face moved 1.55% of a panel,
         * which looks like a weak morph and is actually a morph on her shoes. */
        { unsigned*inv=(unsigned*)malloc(sizeof(unsigned)*P.nMeshes);
          for(unsigned m=0;m<P.nMeshes;m++) inv[ord[m]]=m;
          for(int m9=0;m9<P.nMorph;m9++)
              if(P.morphMesh[m9]<P.nMeshes) P.morphMesh[m9]=inv[P.morphMesh[m9]];
          free(inv); }
        free(tm); free(tt); free(tf);
        P.meshFile=ord;   /* (11-11) kept: each mesh's place in the file, which a MATL chunk's `<name>@N` counts by (packMaterials) */
        printf("[alpha] %s: %u blended mesh(es) moved last%c",P.label,nb,10); fflush(stdout); } }
    /* ⭐ THE WARDROBE (09-29): read the tags AFTER the reorder above -- a per-mesh array built before it would be
     * the fifth one to point at the wrong meshes. */
    P.meshOff=P.meshKind=P.meshPiece=NULL; P.meshHiders=NULL; P.nPiece=0; P.nOutfit=0; P.outfit=-1; P.worn=0;
    { int any=0; for(unsigned m=0;m<P.nMeshes;m++){ const char*n=packStr(&P,P.meshes[m].name);
          if(n&&(!strncmp(n,"wear~",5)||!strncmp(n,"skin~h=",7))){ any=1; break; } }
      if(any){
        P.meshOff=(unsigned char*)calloc(P.nMeshes,1); P.meshKind=(unsigned char*)calloc(P.nMeshes,1);
        P.meshPiece=(unsigned char*)calloc(P.nMeshes,1); P.meshHiders=(unsigned long long*)calloc(P.nMeshes,sizeof(unsigned long long));
        for(unsigned m=0;m<P.nMeshes;m++){ const char*n=packStr(&P,P.meshes[m].name); if(!n) continue;
            char tmp[256];
            if(!strncmp(n,"wear~",5)){                        /* wear~slot~piece~o1+o2[~h=a,b] */
                snprintf(tmp,sizeof tmp,"%s",n+5); char*d=strchr(tmp,'.'); if(d) *d=0;
                char*pc=strchr(tmp,'~'); if(!pc) continue; *pc++=0;
                char*of=strchr(pc,'~'); if(of) *of++=0;
                /* ⭐ A GARMENT UNDER A GARMENT (09-29, a suit): the part of a shirt a jacket covers is its own
                 * mesh, `~h=jacket` -- drawn only while the shirt is worn AND the jacket is not. Layers never poke
                 * through each other, and the shirt is whole again in shirtsleeves */
                char*hd=of?strstr(of,"~h="):NULL; if(hd){ *hd=0; hd+=3; }
                int id=wardPiece(&P,pc); P.meshKind[m]=2; P.meshPiece[m]=(unsigned char)id;
                if(of) for(char*t=strtok(of,"+");t;t=strtok(NULL,"+")) P.pieceOutfits[id]|=1u<<wardOutfit(&P,t);
                if(hd){ char hb[128]; snprintf(hb,sizeof hb,"%s",hd);
                    for(char*t=strtok(hb,",");t;t=strtok(NULL,",")) P.meshHiders[m]|=1ull<<wardPiece(&P,t); } }
            else if(!strncmp(n,"skin~h=",7)){                /* skin~h=a,b */
                snprintf(tmp,sizeof tmp,"%s",n+7); char*d=strchr(tmp,'.'); if(d) *d=0;
                P.meshKind[m]=1;
                for(char*t=strtok(tmp,",");t;t=strtok(NULL,",")) P.meshHiders[m]|=1ull<<wardPiece(&P,t); } }
        printf("[wardrobe] %s: %d pieces, %d outfit(s):",P.label[0]?P.label:path,P.nPiece,P.nOutfit);
        for(int i=0;i<P.nOutfit;i++) printf(" %s",P.outfitName[i]);
        printf("\n"); fflush(stdout);
        const char*e=getenv("REVA_OUTFIT");
        if(!(e&&e[0]&&wardWear(&P,e))&&P.nOutfit) wardWear(&P,P.outfitName[0]); } }
    /* ⛔ AND IT MUST HAPPEN BEFORE THE VBOs ARE BUILT. P.vbo is indexed by mesh
     * POSITION, so reordering P.meshes afterwards leaves every mesh drawing with
     * another mesh's vertex buffers. Her face came apart into floating slabs with
     * hair across her eyes, and I read that as a transparency fault for two rounds
     * before noticing the reorder ran after the upload loop. */
    P.vbo=calloc(P.nMeshes*6,sizeof(unsigned));
    for(unsigned m=0;m<P.nMeshes;m++){ PMesh*M=&P.meshes[m]; if(!(M->flags&4)||M->boneCount>MAXB) continue;
        /* ⛔ NO BLEND STATE HERE -- this loop UPLOADS buffers, it does not draw. */
        unsigned b[6]; gfxBuffersNew(6,b); memcpy(P.vbo+m*6,b,sizeof b);
         gfxBufferData(b[0], M->verts*12, P.pk+M->offPos, 0);
        if(M->flags&1){  gfxBufferData(b[1], M->verts*12, P.pk+M->offNorm, 0); }
         gfxBufferData(b[2], M->verts*4, P.pk+M->offJoints, 0);
         gfxBufferData(b[3], M->verts*4, P.pk+M->offWeights, 0);
        if(M->idxCount){  gfxBufferData(b[4], M->idxCount*4, P.pk+M->offIdx, 0); }
        if(M->flags&2){  gfxBufferData(b[5], M->verts*8, P.pk+M->offUV, 0); } }
    /* ⭐ REVA_HIDEMESH=<substring> -- drop any mesh whose NAME contains it, at load, for
     * EVERY draw path at once. Every path already gates on flags&4, so clearing that bit
     * here is the one place that hides a mesh from the hero, the partner, an actor, the
     * multi-cam wall and the shadow pass together.
     * ⭐ WHY IT EXISTS: anim_bench/pathcheck.py asks "did this path draw the garment", and
     * three attempts to answer that by COLOUR all failed -- a loose match caught kaeli's
     * blue hair, a tight one rejected the lit garment, and a hue match counted the SKY.
     * Rendering the same body with and without the mesh and differencing needs no colour
     * classification at all: the difference IS the garment. */
    /* ⭐ A COMMA-SEPARATED LIST, because a garment is rarely one mesh. Camilia's
     * underwear is `Bra` AND `Underwear_Bottoms`, which share no substring, so "show her
     * with and without underwear" was impossible with a single needle. */
    { const char*hm=getenv("REVA_HIDEMESH");
      if(hm&&hm[0]) for(unsigned m=0;m<P.nMeshes;m++){
          const char*nm=packStr(&P,P.meshes[m].name);
          if(!nm) continue;
          const char*a=hm;
          while(*a){
              const char*c=strchr(a,','); size_t n=c?(size_t)(c-a):strlen(a);
              if(n>0&&n<128){ char needle[128]; memcpy(needle,a,n); needle[n]=0;
                  if(strstr(nm,needle)){ P.meshes[m].flags&=~4u;
                      printf("[hide] %s / %s%c",P.label,nm,10); fflush(stdout); break; } }
              if(!c) break;
              a=c+1; } } }
    P.drawable=0;
    for(unsigned m=0;m<P.nMeshes;m++) if((P.meshes[m].flags&4)&&P.meshes[m].boneCount<=MAXB) P.drawable=1;
    if(!P.drawable) printf("INVISIBLE PACK (all meshes skipped): %s\n",P.label);
    packMaterials(&P);   /* (step 22) what each mesh is made of, once the meshes are in their final order */
    return P;
}
#undef printf

