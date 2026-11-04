/* ---- WHO'S NEAR (11-03, engine plan Phase 6) ---------------------------------------------------------------------------------------------------------------------------------
 * A crowd's question: who's close to this body? Asking every body about every other is 160,000 checks a tick at 400 bodies, and that was most of what 400 of the zombie game's dead cost. A grid over the floor plan, rebuilt once a tick from where the bodies are, answers from a handful.
 * The answer is every body that could be within the radius, in index order, and the caller still does its own exact test: code that used to walk the whole list gets the same bodies in the same order, so its hashes don't move, which is the proof that it's the same. The grid wraps, so a world of any size fits. */
#define NEAR_CELL 4.f
#define NEAR_DIM 64
#define NEAR_SLACK 1.f   /* how far a body can have moved since the grid was built, in the tick it's asked in */
static int g_nearHead[NEAR_DIM*NEAR_DIM]; static int g_nearNext[1024];
static int nearCell(float v){ int c=(int)floorf(v/NEAR_CELL)%NEAR_DIM; return c<0?c+NEAR_DIM:c; }
static void nearBuild(void){ for(int i=0;i<NEAR_DIM*NEAR_DIM;i++) g_nearHead[i]=-1;
    for(int i=g_NS-1;i>=0;i--){ if(i>=1024||g_slotHide[i]) continue; int c=nearCell(g_slots[i].z)*NEAR_DIM+nearCell(g_slots[i].x); g_nearNext[i]=g_nearHead[c]; g_nearHead[c]=i; } }
static int apiNear(float x,float z,float r,int*out,int max){ int n=0; float rr=r+NEAR_SLACK; int x0=(int)floorf((x-rr)/NEAR_CELL),x1=(int)floorf((x+rr)/NEAR_CELL),z0=(int)floorf((z-rr)/NEAR_CELL),z1=(int)floorf((z+rr)/NEAR_CELL);
    for(int cz=z0;cz<=z1&&n<max;cz++) for(int cx=x0;cx<=x1&&n<max;cx++){ int a=cz%NEAR_DIM,b=cx%NEAR_DIM; if(a<0) a+=NEAR_DIM; if(b<0) b+=NEAR_DIM;
        for(int i=g_nearHead[a*NEAR_DIM+b];i>=0&&n<max;i=g_nearNext[i]){ float dx=g_slots[i].x-x,dz=g_slots[i].z-z; if(dx*dx+dz*dz>rr*rr) continue;
            int k=n++; while(k>0&&out[k-1]>i){ out[k]=out[k-1]; k--; } out[k]=i; } }
    return n; }
