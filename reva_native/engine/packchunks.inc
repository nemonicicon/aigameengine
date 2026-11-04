/* ---- WHAT'S AUTHORED ON A BODY, AS CHUNKS (queue step 3; design/pack_format.md, and skate_src/rpackchunks.py is the same thing in Python) --------------------------------------
 * A bake starts over and writes a new body, so whatever was authored on the old pack now goes in chunks after the new one, named rather than numbered: marks in FACE, morphs in MRPH, clips in CLIP.
 * Here, as a pack loads, what the body hasn't got is added from them, the way an authoring tool appends and repoints: new tables after the file's own bytes, and the header pointed at them, in memory. So nothing past this
 * function knows, and a pack with no such chunks isn't touched. A name in the body beats a name in a chunk. What won't fit is said out loud, and left out.
 * Names aren't unique: a rig can have two nodes of a name, and a morph's name can be on several meshes or twice on one. So a track says which it means: `name`, or `name<tab>1` for the second of that name, and a morph is always
 * `mesh<tab>name<tab>which`. Two meshes can share a name and a size as well, so a mesh is `name`, or `name<ctrl-A>1` for the second. */
typedef struct { unsigned off,n; const unsigned char*b; } PChunk;
static unsigned pcU32(const unsigned char*p){ unsigned v; memcpy(&v,p,4); return v; }
/* a chunk's table: a count, then that many rows of this size. 0 if it doesn't fit, and then the chunk's treated as not there */
static unsigned pcCount(PChunk*c,unsigned row,const char*tag,const char*path){ if(!c->n) return 0; unsigned k=c->n>=4?pcU32(c->b):0xffffffffu;
    if(k>(c->n-4)/row){ printf("[pack] %s: a broken %s chunk, ignored\n",path,tag); fflush(stdout); c->n=0; return 0; }
    return k; }
/* a name in a chunk */
static int pcName(const PChunk*c,unsigned at,const unsigned char**s,unsigned*l){ if(at>=c->n||c->n-at<4) return 0; *l=pcU32(c->b+at); if(*l>c->n-at-4) return 0; *s=c->b+at+4; return 1; }
static void packChunksMerge(unsigned char**pkp,size_t*lenp,const char*path){
    unsigned char*pk=*pkp; size_t len=*lenp;
    if(len<80||memcmp(pk+len-4,"RPK2",4)) return;
    unsigned dOff=pcU32(pk+len-16),dN=pcU32(pk+len-12),fs=pcU32(pk+len-8);
    if(fs!=len||(size_t)dOff+(size_t)dN*16!=len-16) return;   /* loadPack says so, when it looks for the compressed textures */
    PChunk cF={0,0,NULL},cM={0,0,NULL},cC={0,0,NULL};
    for(unsigned i=0;i<dN;i++){ const unsigned char*e=pk+dOff+i*16; unsigned o=pcU32(e+4),n=pcU32(e+8); if((size_t)o+n>len) continue;
        PChunk*c=!memcmp(e,"FACE",4)?&cF:!memcmp(e,"MRPH",4)?&cM:!memcmp(e,"CLIP",4)?&cC:NULL;
        if(c){ c->off=o; c->n=n; c->b=pk+o; } }
    if(!cF.n&&!cM.n&&!cC.n) return;
    unsigned H[15]; memcpy(H,pk+4,60);
    unsigned nF=pcCount(&cF,52,"FACE",path),nM=pcCount(&cM,20,"MRPH",path),nC=pcCount(&cC,20,"CLIP",path);
    if(!nF&&!nM&&!nC) return;
    /* the most this can add: bigger tables, and every name in the chunks */
    unsigned sn0=pcU32(pk+H[11]),m0=(H[14]&&(size_t)H[14]+4<=len)?pcU32(pk+H[14]):0; const unsigned char*st0=pk+H[11]+4; size_t blob0=0,names=0,tracks=0;
    for(unsigned i=0;i<sn0;i++){ size_t e=(size_t)pcU32(st0+i*8)+pcU32(st0+i*8+4); if(e>blob0) blob0=e; }
    #define PC_NAME(c,at) { const unsigned char*s9; unsigned l9; if(pcName(&(c),(at),&s9,&l9)) names+=l9; }
    for(unsigned i=0;i<nF;i++) PC_NAME(cF,pcU32(cF.b+4+i*52));
    for(unsigned i=0;i<nM;i++) PC_NAME(cM,pcU32(cM.b+4+i*20));
    for(unsigned i=0;i<nC;i++){ PC_NAME(cC,pcU32(cC.b+4+i*20)); unsigned nT=pcU32(cC.b+4+i*20+12); if(nT<=cC.n/12) tracks+=nT; }
    #undef PC_NAME
    size_t extra=((size_t)H[1]+nF)*52+4+((size_t)m0+nM)*16+((size_t)H[4]+nC)*20+tracks*12+4+((size_t)sn0+nF+nM+nC)*8+blob0+names+64+16;
    pk=(unsigned char*)realloc(pk,len+extra); if(!pk){ printf("[pack] %s: no memory for its chunks\n",path); fflush(stdout); exit(1); }
    *pkp=pk; if(cF.n) cF.b=pk+cF.off; if(cM.n) cM.b=pk+cM.off; if(cC.n) cC.b=pk+cC.off; st0=pk+H[11]+4;
    /* working copies of the tables, in plain memory */
    size_t snMax=(size_t)sn0+nF+nM+nC; unsigned*se=(unsigned*)malloc((snMax+1)*8); unsigned char*sb=(unsigned char*)malloc(blob0+names+1); unsigned char*nd=(unsigned char*)malloc(((size_t)H[1]+nF+1)*52); unsigned*mm=(unsigned*)malloc(((size_t)m0+nM+1)*16); unsigned char*cl=(unsigned char*)malloc(((size_t)H[4]+nC+1)*20);
    if(!se||!sb||!nd||!mm||!cl){ printf("[pack] %s: no memory for its chunks\n",path); fflush(stdout); exit(1); }
    memcpy(se,st0,(size_t)sn0*8); memcpy(sb,st0+(size_t)sn0*8,blob0); memcpy(nd,pk+H[6],(size_t)H[1]*52); if(m0) memcpy(mm,pk+H[14]+4,(size_t)m0*16); memcpy(cl,pk+H[9],(size_t)H[4]*20);
    size_t sn=sn0,sbN=blob0; unsigned nodes=H[1],morphs=m0,clips=H[4]; size_t at=(len+15)&~(size_t)15; int gotF=0,gotM=0,gotC=0;
    #define PC_EQ(ix,s,l) ((ix)<sn&&se[(size_t)(ix)*2+1]==(l)&&!memcmp(sb+se[(size_t)(ix)*2],(s),(l)))
    #define PC_ADD(s,l) (se[sn*2]=(unsigned)sbN,se[sn*2+1]=(l),memcpy(sb+sbN,(s),(l)),sbN+=(l),(unsigned)sn++)
    const PMesh*ms=(const PMesh*)(pk+H[7]);
    #define PC_MESH(r,rl,out) { const unsigned char*x9=(const unsigned char*)memchr((r),1,(rl)); unsigned bl9=x9?(unsigned)(x9-(r)):(rl),w9=0; if(x9) for(const unsigned char*d9=x9+1;d9<(r)+(rl);d9++) w9=w9*10+(unsigned)(*d9-'0'); out=-1; for(unsigned k9=0;k9<H[2]&&out<0;k9++) if(PC_EQ(ms[k9].name,(r),bl9)){ if(!w9) out=(int)k9; else w9--; } }
    #define PC_NODE(s,l,out) { out=-1; for(unsigned q9=0;q9<nodes;q9++){ unsigned nm9; memcpy(&nm9,nd+(size_t)q9*52,4); if(PC_EQ(nm9,s,l)){ out=(int)q9; break; } } }
    /* marks: a parent comes first, in the chunk as in the table */
    for(unsigned i=0;i<nF;i++){ const unsigned char*r=cF.b+4+i*52,*s,*ps; unsigned l,pl; int par=-1,was;
        if(!pcName(&cF,pcU32(r),&s,&l)) continue;
        PC_NODE(s,l,was); if(was>=0) continue;
        if(pcU32(r+4)!=0xffffffffu){ if(!pcName(&cF,pcU32(r+4),&ps,&pl)) continue; PC_NODE(ps,pl,par); if(par<0){ printf("[pack] %s: the mark %.*s has no parent here, left out\n",path,(int)l,s); fflush(stdout); continue; } }
        unsigned char*o=nd+(size_t)nodes*52; unsigned ix=PC_ADD(s,l); memcpy(o,&ix,4); memcpy(o+4,&par,4); memcpy(o+8,r+8,44); nodes++; gotF++; }
    /* morphs: on the mesh of that name and size. Their data stays where it is in the chunk */
    { for(unsigned i=0;i<nM;i++){ const unsigned char*r=cM.b+4+i*20,*s,*ms9; unsigned l,ml,verts=pcU32(r+8),n=pcU32(r+12),off=pcU32(r+16);
        if(!pcName(&cM,pcU32(r),&s,&l)||!pcName(&cM,pcU32(r+4),&ms9,&ml)) continue;
        if(off>cM.n||n>(cM.n-off)/16){ printf("[pack] %s: the morph %.*s runs past its chunk, left out\n",path,(int)l,s); fflush(stdout); continue; }
        int mi=-1; PC_MESH(ms9,ml,mi); if(mi>=0&&ms[mi].verts!=verts) mi=-1;
        if(mi<0){ printf("[pack] %s: the morph %.*s is for a mesh %.*s of %u vertices, which this body hasn't got, left out\n",path,(int)l,s,(int)ml,ms9,verts); fflush(stdout); continue; }
        int bad=0; for(unsigned v=0;v<n&&!bad;v++) if(pcU32(cM.b+off+(size_t)v*16)>=verts) bad=1;
        if(bad){ printf("[pack] %s: the morph %.*s moves a vertex past its mesh, left out\n",path,(int)l,s); fflush(stdout); continue; }
        int was=0; for(unsigned k=0;k<m0&&!was;k++) if(mm[k*4]==(unsigned)mi&&PC_EQ(mm[k*4+1],s,l)) was=1;
        if(was) continue;
        unsigned ix=PC_ADD(s,l); mm[morphs*4]=(unsigned)mi; mm[morphs*4+1]=ix; mm[morphs*4+2]=n; mm[morphs*4+3]=cM.off+off; morphs++; gotM++; } }
    /* clips: tracks name their node or morph, and their data stays where it is in the chunk */
    { unsigned dropped=0;
      for(unsigned i=0;i<nC;i++){ const unsigned char*r=cC.b+4+i*20,*s; unsigned l,frames=pcU32(r+8),nT=pcU32(r+12),oT=pcU32(r+16); float fps; memcpy(&fps,r+4,4);
        if(!pcName(&cC,pcU32(r),&s,&l)) continue;
        if(oT>cC.n||nT>(cC.n-oT)/12){ printf("[pack] %s: the clip %.*s runs past its chunk, left out\n",path,(int)l,s); fflush(stdout); continue; }
        int was=0; for(unsigned k=0;k<H[4]&&!was;k++){ unsigned nm9; memcpy(&nm9,cl+(size_t)k*20,4); if(PC_EQ(nm9,s,l)) was=1; }
        if(was) continue;
        size_t tab=(at+3)&~(size_t)3; unsigned kept=0;
        for(unsigned t=0;t<nT;t++){ const unsigned char*tr=cC.b+oT+t*12,*ts; unsigned tl,typ=pcU32(tr+4),off=pcU32(tr+8); int idx=-1;
            if(!pcName(&cC,pcU32(tr),&ts,&tl)||typ>3) { dropped++; continue; }
            size_t want=(size_t)frames*(typ==0?4:typ==3?1:3)*4;
            if(off>cC.n||want>cC.n-off){ dropped++; continue; }
            /* which one it means: the parts between its tabs, and a number at the end */
            const unsigned char*t1=(const unsigned char*)memchr(ts,'\t',tl);
            if(typ==3){ const unsigned char*t2=t1?(const unsigned char*)memchr(t1+1,'\t',(size_t)(ts+tl-t1-1)):NULL;
                if(t1&&t2){ unsigned ml9=(unsigned)(t1-ts),nl9=(unsigned)(t2-t1-1),want9=0; int mesh9=-1; for(const unsigned char*d9=t2+1;d9<ts+tl;d9++) want9=want9*10+(unsigned)(*d9-'0');
                    PC_MESH(ts,ml9,mesh9);
                    for(unsigned k=0;k<morphs&&idx<0;k++) if(mm[k*4]==(unsigned)mesh9&&PC_EQ(mm[k*4+1],t1+1,nl9)){ if(!want9) idx=(int)k; else want9--; } } }
            else { unsigned nl9=t1?(unsigned)(t1-ts):tl,want9=0; if(t1) for(const unsigned char*d9=t1+1;d9<ts+tl;d9++) want9=want9*10+(unsigned)(*d9-'0');
                for(unsigned k=0;k<nodes&&idx<0;k++){ unsigned nm9; memcpy(&nm9,nd+(size_t)k*52,4); if(PC_EQ(nm9,ts,nl9)){ if(!want9) idx=(int)k; else want9--; } } }
            if(idx<0){ dropped++; continue; }
            unsigned char*o=pk+tab+(size_t)kept*12; unsigned od=cC.off+off,uix=(unsigned)idx; memcpy(o,&uix,4); memcpy(o+4,&typ,4); memcpy(o+8,&od,4); kept++; }
        if(!kept&&nT){ printf("[pack] %s: nothing the clip %.*s moves is here, left out\n",path,(int)l,s); fflush(stdout); continue; }
        at=tab+(size_t)kept*12;
        unsigned char*o=cl+(size_t)clips*20; unsigned ix=PC_ADD(s,l),ot=(unsigned)tab; memcpy(o,&ix,4); memcpy(o+4,&fps,4); memcpy(o+8,&frames,4); memcpy(o+12,&kept,4); memcpy(o+16,&ot,4); clips++; gotC++; }
      if(dropped){ printf("[pack] %s: %u clip tracks in its chunks move things this body hasn't got, left out\n",path,dropped); fflush(stdout); } }
    #undef PC_NODE
    #undef PC_MESH
    #undef PC_ADD
    #undef PC_EQ
    /* the new tables go after everything, and the header points at them */
    at=(at+15)&~(size_t)15;
    memcpy(pk+at,nd,(size_t)nodes*52); H[1]=nodes; H[6]=(unsigned)at; at+=(size_t)nodes*52; at=(at+15)&~(size_t)15;
    if(cM.n||m0){ memcpy(pk+at,&morphs,4); memcpy(pk+at+4,mm,(size_t)morphs*16); H[14]=(unsigned)at; at+=4+(size_t)morphs*16; at=(at+15)&~(size_t)15; }
    memcpy(pk+at,cl,(size_t)clips*20); H[4]=clips; H[9]=(unsigned)at; at+=(size_t)clips*20; at=(at+15)&~(size_t)15;
    { unsigned sc=(unsigned)sn; memcpy(pk+at,&sc,4); memcpy(pk+at+4,se,sn*8); memcpy(pk+at+4+sn*8,sb,sbN); H[11]=(unsigned)at; at+=4+sn*8+sbN; at=(at+15)&~(size_t)15; }
    /* the footer again at the new end, so loadPack still finds the file's chunks */
    { unsigned nl=(unsigned)(at+16); memcpy(pk+at,&dOff,4); memcpy(pk+at+4,&dN,4); memcpy(pk+at+8,&nl,4); memcpy(pk+at+12,"RPK2",4); *lenp=nl; }
    H[13]=dOff; memcpy(pk+4,H,60);
    free(se); free(sb); free(nd); free(mm); free(cl);
    printf("[pack] %s: %d marks, %d morphs and %d clips from its chunks\n",path,gotF,gotM,gotC); fflush(stdout); }
/* REVA_PACKSUM=1: what a pack means once it's merged, the same sum as `rpackchunks.py sum`, so the two readers can be held against each other */
static void packSumPrint(const unsigned char*pk,const char*path){
    static int on=-1; if(on<0) on=getenv("REVA_PACKSUM")?1:0;
    if(!on) return;
    unsigned H[15]; memcpy(H,pk+4,60);
    unsigned long long h=14695981039346656037ull;
    #define PS_FNV(p,n) { const unsigned char*b9=(const unsigned char*)(p); for(size_t k9=0;k9<(size_t)(n);k9++){ h^=b9[k9]; h*=1099511628211ull; } }
    #define PS_STR(ix) { unsigned o9=pcU32(pk+H[11]+4+(size_t)(ix)*8),l9=pcU32(pk+H[11]+4+(size_t)(ix)*8+4),n9=pcU32(pk+H[11]); PS_FNV(pk+H[11]+4+(size_t)n9*8+o9,l9); }
    for(unsigned i=0;i<H[1];i++){ const unsigned char*r=pk+H[6]+(size_t)i*52; PS_STR(pcU32(r)); unsigned z=0; PS_FNV(&z,4); PS_FNV(r+4,48); }
    for(unsigned i=0;i<H[4];i++){ const unsigned char*r=pk+H[9]+(size_t)i*20; PS_STR(pcU32(r)); PS_FNV(r+4,4); PS_FNV(r+8,4); PS_FNV(r+12,4);
        unsigned frames=pcU32(r+8),nT=pcU32(r+12),oT=pcU32(r+16);
        for(unsigned t=0;t<nT;t++){ const unsigned char*tr=pk+oT+(size_t)t*12; unsigned typ=pcU32(tr+4); PS_FNV(tr,4); PS_FNV(tr+4,4); PS_FNV(pk+pcU32(tr+8),(size_t)frames*(typ==0?4:typ==3?1:3)*4); } }
    unsigned nM=H[14]?pcU32(pk+H[14]):0;
    for(unsigned i=0;i<nM;i++){ const unsigned char*r=pk+H[14]+4+(size_t)i*16; PS_STR(pcU32(r+4)); PS_FNV(r,4); PS_FNV(r+8,4); PS_FNV(pk+pcU32(r+12),(size_t)pcU32(r+8)*16); }
    #undef PS_STR
    #undef PS_FNV
    printf("[packsum] %s nodes %u clips %u morphs %u %016llx\n",path,H[1],H[4],nM,h); fflush(stdout); }
