/* engine/packcheck.inc -- A PACK IS CHECKED BEFORE ANY OF IT IS BELIEVED (queue step 28, 11-05: "fuzz-tested file readers").
 * A pack is a file of tables that point at one another by number: a mesh names a node, a vertex names a bone, an index names a vertex, a track names its data by an
 * offset. The loader used to follow every one of those on trust, and the fuzzer (anim_bench/fuzzcheck.py) made it read and write outside the file a dozen different
 * ways with one changed byte. A pack can come from anyone, so this reads the whole of it once, before the loader touches it, and holds every table, every offset and
 * every index to the file's own length. These are design/pack_format.md's rules, the ones anim_bench/packdump.py --check tests in Python: the engine now enforces them.
 * A pack that fails is refused, by name and with the reason, and nothing of it is used.
 * ⛔ Anything the loader or the engine comes to read out of a pack by a number the pack supplies needs its line here first. */
static unsigned pkU32(const unsigned char*p){ unsigned v; memcpy(&v,p,4); return v; }
/* do n rows of this size, starting at off, lie inside len bytes? */
static int pkFits(unsigned long long len,unsigned long long off,unsigned long long n,unsigned long long row){ return off<=len&&(row==0||n<=(len-off)/row); }
static int pkFinite(const unsigned char*p,unsigned n){ for(unsigned i=0;i<n;i++){ float f; memcpy(&f,p+(size_t)i*4,4); if(!(f-f==0.f)) return 0; } return 1; }
#define PK_BAD(...) do{ snprintf(why,cap,__VA_ARGS__); return 0; }while(0)
static int packCheck(const unsigned char*pk,size_t len,char*why,size_t cap){
    if(!pk||len<64||memcmp(pk,"RPK1",4)) PK_BAD("it isn't a pack");
    unsigned H[15]; memcpy(H,pk+4,60);
    const unsigned nNodes=H[1],nMeshes=H[2],nMats=H[3],nClips=H[4],nTex=H[5];
    if(H[0]!=2) PK_BAD("it's version %u of the format, and this engine reads 2",H[0]);
    if(nNodes>65535u||nMeshes>65535u||nMats>(1u<<20)||nClips>65535u||nTex>65535u) PK_BAD("it counts more than any body has: %u nodes, %u meshes, %u materials, %u clips, %u textures",nNodes,nMeshes,nMats,nClips,nTex);
    if(H[13]>len) PK_BAD("it says it's %u bytes long and the file is %zu",H[13],len);
    if(!pkFits(len,H[6],nNodes,52)) PK_BAD("its nodes run past the end of the file");
    if(!pkFits(len,H[7],nMeshes,60)) PK_BAD("its meshes run past the end of the file");
    if(!pkFits(len,H[8],nMats,36)) PK_BAD("its materials run past the end of the file");
    if(!pkFits(len,H[9],nClips,20)) PK_BAD("its clips run past the end of the file");
    if(!pkFits(len,H[10],nTex,16)) PK_BAD("its textures' table runs past the end of the file");
    if(!pkFits(len,H[12],12,4)) PK_BAD("its size block runs past the end of the file");
    /* names: a count, then an offset and a length each, then the letters */
    if(!pkFits(len,H[11],1,4)) PK_BAD("its names aren't in the file");
    const unsigned nStr=pkU32(pk+H[11]);
    if(!pkFits(len,(unsigned long long)H[11]+4,nStr,8)) PK_BAD("its names' table runs past the end of the file");
    { const unsigned long long sb=(unsigned long long)H[11]+4+(unsigned long long)nStr*8;
      for(unsigned i=0;i<nStr;i++){ const unsigned char*e=pk+H[11]+4+(size_t)i*8; if(!pkFits(len,sb+pkU32(e),pkU32(e+4),1)) PK_BAD("name %u runs past the end of the file",i); } }
    /* morphs, counted before the clips that move them */
    unsigned nMorph=0;
    if(H[14]){ if(!pkFits(len,H[14],1,4)) PK_BAD("its morphs aren't in the file");
        nMorph=pkU32(pk+H[14]); if(!pkFits(len,(unsigned long long)H[14]+4,nMorph,16)) PK_BAD("its morphs' table runs past the end of the file"); }
    /* nodes: a parent comes before its children, because everything walks the table once */
    for(unsigned i=0;i<nNodes;i++){ const unsigned char*r=pk+H[6]+(size_t)i*52; const int par=(int)pkU32(r+4);
        if(pkU32(r)>=nStr) PK_BAD("node %u's name isn't in the names",i);
        if(par<-1||par>=(int)i) PK_BAD("node %u's parent (%d) doesn't come before it",i,par);
        if(pkU32(r+8)>2u) PK_BAD("node %u is of a kind (%u) nobody knows",i,pkU32(r+8));
        if(!pkFinite(r+12,10)) PK_BAD("node %u's rest pose has something in it that isn't a number",i); }
    /* meshes: every array the engine reads for it lies in the file, a vertex's bones are bones the mesh has, a triangle's corners are vertices it has */
    for(unsigned m=0;m<nMeshes;m++){ unsigned f[15]; memcpy(f,pk+H[7]+(size_t)m*60,60); const unsigned verts=f[2],idx=f[3],bones=f[4];
        if(f[0]>=nStr) PK_BAD("mesh %u's name isn't in the names",m);
        if(f[1]>=nNodes) PK_BAD("mesh %u hangs on node %u, and there are %u",m,f[1],nNodes);
        if(bones>256u) PK_BAD("mesh %u has %u bones, and 256 is the most",m,bones);
        if(idx%3u) PK_BAD("mesh %u's indices (%u) aren't whole triangles",m,idx);
        if(!pkFits(len,f[6],verts,12)) PK_BAD("mesh %u's positions run past the end of the file",m);
        if(!pkFits(len,f[7],verts,12)) PK_BAD("mesh %u's normals run past the end of the file",m);
        if(!pkFits(len,f[8],verts,8)) PK_BAD("mesh %u's texture coordinates run past the end of the file",m);
        if(!pkFits(len,f[9],verts,4)||!pkFits(len,f[10],verts,4)) PK_BAD("mesh %u's bone weights run past the end of the file",m);
        if(!pkFits(len,f[11],idx,4)) PK_BAD("mesh %u's indices run past the end of the file",m);
        if(!pkFits(len,f[12],bones,2)||!pkFits(len,f[13],bones,64)) PK_BAD("mesh %u's bones run past the end of the file",m);
        if(!pkFits(len,f[14],16,4)) PK_BAD("mesh %u's bind matrix runs past the end of the file",m);
        if(bones){ for(unsigned b=0;b<bones;b++){ unsigned short n9; memcpy(&n9,pk+f[12]+(size_t)b*2,2); if(n9>=nNodes) PK_BAD("mesh %u's bone %u is node %u, and there are %u",m,b,(unsigned)n9,nNodes); }
            const unsigned char*j=pk+f[9]; for(size_t v=0;v<(size_t)verts*4;v++) if(j[v]>=bones) PK_BAD("mesh %u has a vertex on bone %u, and it has %u",m,(unsigned)j[v],bones);
            if(!pkFinite(pk+f[13],bones*16u)) PK_BAD("mesh %u's bones have something in them that isn't a number",m); }
        if((f[5]&4u)&&!pkFinite(pk+f[14],16)) PK_BAD("mesh %u's bind matrix has something in it that isn't a number",m);
        { const unsigned char*ix=pk+f[11]; for(unsigned k=0;k<idx;k++) if(pkU32(ix+(size_t)k*4)>=verts) PK_BAD("mesh %u has a triangle on vertex %u, and it has %u",m,pkU32(ix+(size_t)k*4),verts); } }
    /* materials: the mesh and the two textures each names */
    for(unsigned r=0;r<nMats;r++){ const unsigned char*e=pk+H[8]+(size_t)r*36; const int tex=(int)pkU32(e+24),ntex=(int)pkU32(e+28);
        if(pkU32(e)>=nMeshes) PK_BAD("material %u is for mesh %u, and there are %u",r,pkU32(e),nMeshes);
        if(tex<-1||(tex>=0&&(unsigned)tex>=nTex)) PK_BAD("material %u names texture %d, and there are %u",r,tex,nTex);
        if(ntex<0||(unsigned)ntex>nTex) PK_BAD("material %u names normal map %d, and there are %u textures",r,ntex-1,nTex); }
    /* clips: a rate, a length, and for each track a thing it moves and enough numbers for every frame */
    for(unsigned c=0;c<nClips;c++){ const unsigned char*e=pk+H[9]+(size_t)c*20; float fps; memcpy(&fps,e+4,4); const unsigned frames=pkU32(e+8),nT=pkU32(e+12),oT=pkU32(e+16);
        if(pkU32(e)>=nStr) PK_BAD("clip %u's name isn't in the names",c);
        if(!(fps>0.f)||!(fps<100000.f)) PK_BAD("clip %u plays at %g frames a second",c,(double)fps);
        if(frames<1u||frames>(1u<<24)) PK_BAD("clip %u has %u frames",c,frames);
        if(nT>(1u<<20)||!pkFits(len,oT,nT,12)) PK_BAD("clip %u's tracks run past the end of the file",c);
        for(unsigned t=0;t<nT;t++){ const unsigned char*tr=pk+oT+(size_t)t*12; const unsigned node=pkU32(tr),typ=pkU32(tr+4);
            if(typ>3u) PK_BAD("clip %u has a track of a kind (%u) nobody knows",c,typ);
            if(typ<3u&&node>=nNodes) PK_BAD("clip %u moves node %u, and there are %u",c,node,nNodes);
            if(typ==3u&&node>=(unsigned)MORPH_MAX) PK_BAD("clip %u moves morph %u, and %d is the most",c,node,MORPH_MAX);   /* (11-11) the cap, not a 128 of its own */
            if(!pkFits(len,pkU32(tr+8),(unsigned long long)frames*(typ==0u?4u:typ==3u?1u:3u),4)) PK_BAD("clip %u's track %u runs past the end of the file",c,t); } }
    /* textures: each an image file somewhere in this one */
    for(unsigned t=0;t<nTex;t++){ const unsigned char*e=pk+H[10]+(size_t)t*16; if(pkU32(e+12)&&!pkFits(len,pkU32(e+8),pkU32(e+12),1)) PK_BAD("texture %u runs past the end of the file",t); }
    /* morphs: a mesh, a name, and a list of vertices of that mesh with where each goes */
    for(unsigned i=0;i<nMorph;i++){ const unsigned char*e=pk+H[14]+4+(size_t)i*16; const unsigned mesh=pkU32(e),n=pkU32(e+8),off=pkU32(e+12);
        if(mesh>=nMeshes) PK_BAD("morph %u is on mesh %u, and there are %u",i,mesh,nMeshes);
        if(pkU32(e+4)>=nStr) PK_BAD("morph %u's name isn't in the names",i);
        if(!pkFits(len,off,n,16)) PK_BAD("morph %u runs past the end of the file",i);
        const unsigned verts=pkU32(pk+H[7]+(size_t)mesh*60+8);
        for(unsigned s=0;s<n;s++) if(pkU32(pk+off+(size_t)s*16)>=verts) PK_BAD("morph %u moves vertex %u of a mesh that has %u",i,pkU32(pk+off+(size_t)s*16),verts); }
    return 1; }
#undef PK_BAD
/* ⭐ A TEXT FILE'S NUMBERS ARE HELD TO BEING NUMBERS (step 28). A set, a graph or a conversation is read a line at a time with sscanf and atof, which are happy to
 * hand back infinity, not-a-number, or 1e308, and every one of those went on to size a grid, run a loop or index a table (the fuzzer hung the engine with one
 * word). So a line is looked over first: any piece of it that is a number has to be a finite one no bigger than `most`. Words aren't numbers: `inferno` is a name.
 * 1 if the line can be read */
static int txtNumsOk(const char*ln,double most){
    for(const char*p=ln;*p;){ while(*p&&strchr(" \t\r\n=,:;@><()[]\"",*p)) p++;
        if(!*p) break;
        const char*e=p; while(*e&&!strchr(" \t\r\n=,:;@><()[]\"",*e)) e++;
        const size_t n=(size_t)(e-p); char t[48];
        if(n<sizeof t){ memcpy(t,p,n); t[n]=0; const char*q=(t[0]=='+'||t[0]=='-')?t+1:t;
            if((q[0]>='0'&&q[0]<='9')||q[0]=='.'){ char*end; const double v=strtod(t,&end); if(end!=t&&(!(v-v==0.0)||v>most||v<-most)) return 0; }
            else { char lo[10]; size_t k=0; for(;q[k]&&k<9;k++) lo[k]=(char)(q[k]>='A'&&q[k]<='Z'?q[k]+32:q[k]); lo[k]=0;
                   if(!q[k]&&(!strcmp(lo,"inf")||!strcmp(lo,"nan")||!strcmp(lo,"infinity"))) return 0; } }
        else { const char*q=(*p=='+'||*p=='-')?p+1:p; if((q[0]>='0'&&q[0]<='9')||q[0]=='.'){ int digits=1; for(const char*c=q;c<e;c++) if(!((*c>='0'&&*c<='9')||*c=='.')) digits=0; if(digits) return 0; } }   /* forty digits in a row is no size of anything */
        p=e; }
    return 1; }
/* a cooked texture's record, out of a pack's TEXC chunk: is every level of it inside the chunk's own bytes? fmt 1 is 8 bytes a block of four by four, 3 and 7 are 16 */
static int packTexcOk(const unsigned*r,size_t len){ const unsigned fmt=r[1],w=r[2],h=r[3],levels=r[4],off=r[6],size=r[7];
    if((fmt!=1u&&fmt!=3u&&fmt!=7u)||w<1u||h<1u||w>16384u||h>16384u||levels<1u||levels>15u||!pkFits(len,off,size,1)) return 0;
    unsigned long long need=0; for(unsigned l=0,x=w,y=h;l<levels;l++,x=x>1u?x/2u:1u,y=y>1u?y/2u:1u) need+=(unsigned long long)((x+3u)/4u)*((y+3u)/4u)*(fmt==1u?8u:16u);
    return need<=size; }
