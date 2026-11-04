/* engine/bake.inc -- baked light (engine plan, Phase 7). A grid of probes over a set, traced once against its real triangles: how much sky each point can see, whether the sun reaches it, and how much sunlight bounces to it.
 * REVA_BAKE=<file> makes it: load the set in the no-display build and it writes the file and exits, so `python anim_bench/bake.py` or any build line can run it at the cook. Keep the file as sets/<name>.probes and a set picks it up at load.
 * It's cheap at run time, one texture read, which is why it's the upgrade that helps both ends of the ladder. ⛔ A bake is for the sun the set says: a day clock moves the sun, but the sky term still holds. */
#define PRB_MAGIC "PRB1"
static void probeBake(const char*name,const char*out){
    if(!g_sp.col){ printf("[bake] %s: no triangles to trace\n",name); fflush(stdout); exit(1); }
    clock_t c0=clock();
    float x0=g_sp.x0,z0=g_sp.z0,x1=x0+(float)g_sp.W*SP_CELL,z1=z0+(float)g_sp.H*SP_CELL,top=2.f;
    for(int i=0;i<g_sp.n;i++) if(g_sp.sp[i].y1>top) top=g_sp.sp[i].y1;
    int w=(int)((x1-x0)/1.5f)+2,d=(int)((z1-z0)/1.5f)+2,h=(int)((top+4.f)/1.25f)+2;
    w=w>96?96:w<2?2:w; d=d>96?96:d<2?2:d; h=h>32?32:h<2?2:h;
    float dx=(x1-x0)/(float)(w-1),dz=(z1-z0)/(float)(d-1),dy=(top+4.f)/(float)(h-1);
    #define PRB_RAYS 128
    float dirs[PRB_RAYS][3];   /* over the upper half of the sky, more of them overhead, as a surface's light is */
    for(int i=0;i<PRB_RAYS;i++){ float u=((float)i+0.5f)/(float)PRB_RAYS,a=(float)i*2.3999632f,r=sqrtf(u),y=sqrtf(1.f-u); dirs[i][0]=r*cosf(a); dirs[i][1]=y; dirs[i][2]=r*sinf(a); }
    float sl=sqrtf(g_sun[0]*g_sun[0]+g_sun[1]*g_sun[1]+g_sun[2]*g_sun[2]),sun[3]={0.f,1.f,0.f}; if(sl>0.f){ sun[0]=g_sun[0]/sl; sun[1]=g_sun[1]/sl; sun[2]=g_sun[2]/sl; }
    unsigned char*px=(unsigned char*)calloc((size_t)w*h*d,4); if(!px) exit(1);
    for(int yi=0;yi<h;yi++) for(int zi=0;zi<d;zi++) for(int xi=0;xi<w;xi++){ float p[3]={x0+(float)xi*dx,0.12f+(float)yi*dy,z0+(float)zi*dz};
        int sky=0,bounce=0,tiny=0;
        for(int k=0;k<PRB_RAYS;k++){ float e[3]={p[0]+dirs[k][0]*50.f,p[1]+dirs[k][1]*50.f,p[2]+dirs[k][2]*50.f},t=2.f;
            if(!spRay(p,e,&t)){ sky++; continue; }
            if(t*50.f<0.3f) tiny++;
            float q[3]={p[0]+dirs[k][0]*(t*50.f-0.08f),p[1]+dirs[k][1]*(t*50.f-0.08f),p[2]+dirs[k][2]*(t*50.f-0.08f)},s2[3]={q[0]+sun[0]*60.f,q[1]+sun[1]*60.f,q[2]+sun[2]*60.f};
            if(!spRay(q,s2,&t)) bounce++; }
        float s3[3]={p[0]+sun[0]*60.f,p[1]+sun[1]*60.f,p[2]+sun[2]*60.f},t3=2.f; int lit=!spRay(p,s3,&t3);
        unsigned char*o=px+(((size_t)yi*d+zi)*w+xi)*4;
        o[0]=(unsigned char)(255*sky/PRB_RAYS); o[1]=(unsigned char)(lit?255:0); o[2]=(unsigned char)(255*bounce/PRB_RAYS); o[3]=(unsigned char)(tiny>PRB_RAYS/3?0:255); }   /* alpha 0: inside something */
    /* a probe inside something would bleed dark through the surface beside it: it takes the brightest neighbour that isn't */
    for(int pass=0;pass<3;pass++) for(int yi=0;yi<h;yi++) for(int zi=0;zi<d;zi++) for(int xi=0;xi<w;xi++){ unsigned char*o=px+(((size_t)yi*d+zi)*w+xi)*4; if(o[3]) continue;
        const unsigned char*best=NULL; for(int ny=yi-1;ny<=yi+1;ny++) for(int nz=zi-1;nz<=zi+1;nz++) for(int nx=xi-1;nx<=xi+1;nx++){ if(ny<0||nz<0||nx<0||ny>=h||nz>=d||nx>=w) continue; const unsigned char*q=px+(((size_t)ny*d+nz)*w+nx)*4; if(q[3]&&(!best||q[0]>best[0])) best=q; }
        if(best){ o[0]=best[0]; o[1]=best[1]; o[2]=best[2]; o[3]=128; } }
    /* and smoothed across the floor plan: a probe a step apart can see quite different things, and that reads as smudges on the ground */
    { unsigned char*sm=(unsigned char*)malloc((size_t)w*h*d*4); if(sm){ for(int pass=0;pass<2;pass++){ memcpy(sm,px,(size_t)w*h*d*4);
            for(int yi=0;yi<h;yi++) for(int zi=0;zi<d;zi++) for(int xi=0;xi<w;xi++){ int sum[3]={0,0,0},n=0;
                for(int nz=zi-1;nz<=zi+1;nz++) for(int nx=xi-1;nx<=xi+1;nx++){ if(nz<0||nx<0||nz>=d||nx>=w) continue; const unsigned char*q=sm+(((size_t)yi*d+nz)*w+nx)*4; for(int k=0;k<3;k++) sum[k]+=q[k]; n++; }
                unsigned char*o=px+(((size_t)yi*d+zi)*w+xi)*4; for(int k=0;k<3;k++) o[k]=(unsigned char)(sum[k]/n); } }
        free(sm); } }
    FILE*f=fopen(out,"wb"); if(!f){ printf("[bake] can't write %s\n",out); fflush(stdout); exit(1); }
    int dims[3]={w,h,d}; float geo[6]={x0,0.12f,z0,dx,dy,dz}; fwrite(PRB_MAGIC,1,4,f); fwrite(dims,4,3,f); fwrite(geo,4,6,f); fwrite(px,4,(size_t)w*h*d,f); fclose(f); free(px);
    printf("[bake] %s: %d x %d x %d probes, %.1f s -> %s\n",name,w,h,d,(double)(clock()-c0)/CLOCKS_PER_SEC,out); fflush(stdout); exit(0); }
#ifdef REVA_VK
static void probesLoad(const char*name){
    if(!g_gfxProbesOn) return;
    gfxProbes(NULL,0,0,0,NULL);
    char path[256]; snprintf(path,sizeof path,"sets/%s.probes",name); FILE*f=fopen(path,"rb"); if(!f) return;
    char m[4]; int dims[3]; float geo[6];
    if(fread(m,1,4,f)!=4||memcmp(m,PRB_MAGIC,4)||fread(dims,4,3,f)!=3||fread(geo,4,6,f)!=6){ printf("[probes] %s isn't a probe file\n",path); fflush(stdout); fclose(f); return; }
    size_t n=(size_t)dims[0]*dims[1]*dims[2]*4; unsigned char*px=(unsigned char*)malloc(n);
    if(px&&fread(px,1,n,f)==n){ gfxProbes(px,dims[0],dims[1],dims[2],geo); printf("[probes] %s: %d x %d x %d\n",path,dims[0],dims[1],dims[2]); fflush(stdout); } else { printf("[probes] %s is short\n",path); fflush(stdout); }
    free(px); fclose(f); }
#endif
