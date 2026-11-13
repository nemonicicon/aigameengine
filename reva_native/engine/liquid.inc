/* ---- LIQUID IN A CONTAINER (11-05, the street game's bag: design/engine_plan.md, "The street game against the queue", liquid in a container was on the list of what the engine lacked) ----
 * A drink, a soup, a milkshake: liquid in an upright round container, carried on a ride (E->rideBegin) or set down, open or under a lid. Its free surface is the first sloshing mode of
 * a liquid in an upright cylinder, taken as a damped pendulum in two directions (the carrier's forward and its side):
 *   - the surface's slope wants to lie against the carrier's acceleration, s = -a / (g + a_up): speeding up, the liquid climbs the back wall; turning, the outside one;
 *   - it swings toward that at the cylinder's own frequency, w^2 = g k tanh(k h) with k = 1.8412 / R (the first root of J1'), and dies at its damping ratio;
 *   - the surface at the wall stands R s above where it would be still; past the rim, it pours over the rim as a weir does, Q = (2/3) Cd sqrt(2g) L e^1.5, over a length of
 *     wall L about its radius, slowed by how thick it is, and what's left gets lower;
 *   - a carrier that throws its rider tips it over: an open one empties at the weir's rate with its whole height over the rim, while the ride is down.
 * A lid keeps everything in, and it still sloshes, so a game can read how hard it was shaken. The carrier's accelerations are worked out from the ride's own state each tick (its
 * speed, its turning, its height), and E->liquidPush adds any other. Everything here is the tick's, in [simhash] once a game begins one, the same on every machine.
 * REVA_LIQUIDSAY=<ticks> prints each one's numbers that often. Nothing here runs until a game begins one, so no other game can see it. */
#define LIQ_MAX 16
typedef struct { int on,ride,fresh; EngineLiquid sp; EngineLiquidState st; float sF,sS,vF,vS,pushF,pushS,pushU,pv,pyaw,py,pvy; } Liquid;
static Liquid g_liq[LIQ_MAX+1]; static int g_liqEver=0;
static Liquid* liqOf(int h){ return (h>=1&&h<=LIQ_MAX&&g_liq[h].on)?&g_liq[h]:NULL; }
static int liqSpecOk(const EngineLiquid*s){ const float*f=&s->radius; for(int i=0;i<5;i++) if(!(f[i]-f[i]==0.f)){ printf("[liquid] a number in the container isn't a number: refused\n"); fflush(stdout); return 0; }
    if(s->radius<0.005f||s->height<0.005f||s->fill<0.f||s->fill>s->height){ printf("[liquid] a container needs a radius (%.3f m), a height (%.3f m) and a fill between 0 and its height (%.3f m): refused\n",s->radius,s->height,s->fill); fflush(stdout); return 0; }
    return 1; }
static void liqTake(Liquid*L,const EngineLiquid*s,size_t size){ EngineLiquid q; memset(&q,0,sizeof q); memcpy(&q,s,size<sizeof q?size:sizeof q); if(q.thick<1.f) q.thick=1.f; if(q.damping<0.f) q.damping=0.f; L->sp=q;
    L->st.level=q.fill; L->st.litres=3.14159265f*q.radius*q.radius*q.fill*1000.f; }
static int apiLiquidBegin(const EngineLiquid*s,size_t size,int ride){ if(!s||!liqSpecOk(s)) return 0;
    int h=0; for(int k=1;k<=LIQ_MAX;k++) if(!g_liq[k].on){ h=k; break; }
    if(!h){ printf("[liquid] %d containers at once already: no more\n",LIQ_MAX); fflush(stdout); return 0; }
    Liquid*L=&g_liq[h]; memset(L,0,sizeof *L); liqTake(L,s,size); L->on=1; L->ride=ride; L->fresh=1; g_liqEver=1; return h; }
/* new numbers: a fill sets what's in it again (a refill, a cup filled at the fountain); the swing it has stays */
static void apiLiquidSpec(int h,const EngineLiquid*s,size_t size){ Liquid*L=liqOf(h); if(!L||!s||!liqSpecOk(s)) return; const float sp=L->st.spilt; liqTake(L,s,size); L->st.spilt=sp; }
static void apiLiquidCarry(int h,int ride){ Liquid*L=liqOf(h); if(L){ L->ride=ride; L->fresh=1; } }
static void apiLiquidPush(int h,float fwd,float side,float up){ Liquid*L=liqOf(h); if(!L) return; if(fwd-fwd==0.f) L->pushF+=fwd; if(side-side==0.f) L->pushS+=side; if(up-up==0.f) L->pushU+=up; }
static const EngineLiquidState* apiLiquidState(int h){ Liquid*L=liqOf(h); return L?&L->st:NULL; }
static void apiLiquidEnd(int h){ Liquid*L=liqOf(h); if(L) memset(L,0,sizeof *L); }
static void liqClear(void){ for(int h=1;h<=LIQ_MAX;h++) g_liq[h].on=0; }
static void liqStep1(Liquid*L,float dt){
    const EngineLiquid*P=&L->sp; EngineLiquidState*S=&L->st; float aF=L->pushF,aS=L->pushS,aU=L->pushU; L->pushF=L->pushS=L->pushU=0.f;
    const Ride*R=L->ride?rideOf(L->ride):NULL; int down=0;
    if(R){ const EngineRideState*Q=&R->st; down=Q->down;
        const float y=Q->y/RIDE_U;
        if(L->fresh||fabsf(Q->y-L->py)>SP_JUMP){ L->pv=Q->speed; L->pyaw=Q->yaw; L->py=Q->y; L->pvy=0.f; L->fresh=0; }   /* nothing to differ from yet, or it was put somewhere */
        float dyaw=Q->yaw-L->pyaw; if(dyaw>3.14159265f) dyaw-=6.2831853f; if(dyaw<-3.14159265f) dyaw+=6.2831853f;
        const float vy=(y-L->py/RIDE_U)/dt;
        aF+=(Q->speed-L->pv)/dt; aS+=Q->speed*dyaw/dt; aU+=rideCl((vy-L->pvy)/dt,-0.9f*RIDE_G,3.f*RIDE_G);
        L->pv=Q->speed; L->pyaw=Q->yaw; L->py=Q->y; L->pvy=vy; }
    S->accFwd=aF; S->accSide=aS; S->accUp=aU;
    /* the surface swings toward lying against the acceleration */
    const float g=RIDE_G+aU, gg=g>0.1f*RIDE_G?g:0.1f*RIDE_G, k=1.8412f/P->radius, hh=S->level>0.001f?S->level:0.001f, kh=k*hh, th=1.f-2.f/(expf(2.f*(kh<20.f?kh:20.f))+1.f), w=sqrtf(RIDE_G*k*th), z=P->damping;
    const float eF=-aF/gg, eS=-aS/gg;
    L->vF+=(-w*w*(L->sF-eF)-2.f*z*w*L->vF)*dt; L->sF+=L->vF*dt;
    L->vS+=(-w*w*(L->sS-eS)-2.f*z*w*L->vS)*dt; L->sS+=L->vS*dt;
    const float sMax=3.f; L->sF=rideCl(L->sF,-sMax,sMax); L->sS=rideCl(L->sS,-sMax,sMax);
    S->tiltFwd=atanf(L->sF); S->tiltSide=atanf(L->sS);
    const float s=sqrtf(L->sF*L->sF+L->sS*L->sS); S->slosh=P->radius*s;
    if(S->slosh>S->peak) S->peak=S->slosh;
    /* over the rim it pours */
    S->spilling=0;
    if(!P->lid&&S->level>0.f){ float e=down?S->level:S->level+S->slosh-P->height; if(down&&e<0.01f) e=0.01f;
        if(e>0.f){ const float Q=(2.f/3.f)*0.6f*sqrtf(2.f*RIDE_G)*P->radius*e*sqrtf(e)/P->thick, area=3.14159265f*P->radius*P->radius;
            float dl=Q*dt/area; if(dl>S->level) dl=S->level; S->level-=dl; S->spilt+=dl*area*1000.f; S->spilling=1; } }
    S->litres=3.14159265f*P->radius*P->radius*S->level*1000.f; }
static void liqStep(float dt){ if(!g_liqEver||dt<=0.f) return;
    for(int h=1;h<=LIQ_MAX;h++) if(g_liq[h].on) liqStep1(&g_liq[h],dt);
    { static int say=-2; static long n=0; if(say==-2){ const char*e=getenv("REVA_LIQUIDSAY"); say=e?atoi(e):0; }
      if(say>0&&(++n%say)==0) for(int h=1;h<=LIQ_MAX;h++) if(g_liq[h].on){ const EngineLiquidState*S=&g_liq[h].st;
          printf("[liquid] %d: %.3f l, level %.3f m, slosh %.3f m (peak %.3f), tilt %+.1f/%+.1f deg, acc %+.1f/%+.1f/%+.1f, spilt %.3f l%s\n",h,S->litres,S->level,S->slosh,S->peak,
              S->tiltFwd*57.29578f,S->tiltSide*57.29578f,S->accFwd,S->accSide,S->accUp,S->spilt,S->spilling?" SPILLING":""); fflush(stdout); } } }
static void liqHash(void){ if(!g_liqEver) return;
    for(int h=1;h<=LIQ_MAX;h++){ const Liquid*L=&g_liq[h]; if(!L->on) continue; const EngineLiquidState*S=&L->st;
        float f[10]={L->sF,L->sS,L->vF,L->vS,S->level,S->spilt,L->pv,L->pyaw,L->py,(float)L->ride}; g_shDumpTag="liquid"; simHashMix(f,10); } }
