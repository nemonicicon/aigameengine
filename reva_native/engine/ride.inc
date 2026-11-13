/* ---- A RIDDEN VEHICLE (11-05, the street game's M0; design/engine_plan.md, "The street game against the queue": the first thing the engine lacked) -----------------------------------
 * Something with two wheels that a body stands on and rides: a scooter, a bike, a board. A game says what it is in numbers (EngineRide: its size, its weight and its rider's, its motor,
 * its brakes, its tyres, how its front end wobbles), says what the rider's hands and feet are doing each tick (E->rideControl), and the engine does the rest on the walked world:
 *   - its two wheels stand on what's really there (E->surface's height under each), so it pitches with the street, and its grade is the rise between them over its wheelbase;
 *   - it goes by forces, in SI units: gravity along that grade, the tyres' rolling resistance by the kind of surface under each wheel, air drag against the set's wind, the motor (its
 *     power and its top pull, and nothing past its top speed), a kick from the rider's foot, and two brakes, each held to its tyre's grip on that surface, with the load moving forward
 *     onto the front wheel as it slows;
 *   - a brake past its tyre's grip locks the wheel: a locked front can't steer and drops the rider if it slides too long (let go and it's saved); a locked rear in a turn comes round,
 *     and drops the rider past rearSlideFall degrees;
 *   - past wobbleOnset its front end swings, growing with the speed past it, dying when the bars are held still, and throwing the rider past wobbleThrow;
 *   - it steers like a bicycle (the bars reach less the faster it goes), leans into a turn, and stops at what's solid, and fast enough, that throws the rider;
 *   - it carries its rider: the hero (-1) or a body, placed on its deck, turned with it, and posed standing on it by EngineRidePose (the canonical bones, as the skate pose is).
 * When it throws its rider, `down` says so and `why`, and the engine stops placing the rider: a game sends the rider limp (E->bodyLimp) with the state's vx, vy, vz, and puts them
 * back on with E->ridePlace. Everything here is the tick's, in [simhash], the same on every machine. Its model is an object of the game's (E->prop), drawn turned with it.
 * REVA_RIDESAY=<ticks> prints each one's numbers that often. Nothing here runs until a game begins a ride, so no other game can see it. */
static const char* apiSurfaceName(int k);
#define RIDE_MAX 8
#define RIDE_U (1.f/0.572f)    /* engine units a metre, the same as CLUB_UNITS_PER_S2's */
#define RIDE_G 9.81f
typedef struct { int on,rider,obj; EngineRide sp; float thr,fb,rb,steer,steerWas; int kickReq; EngineRideState st; float acc,wobPh,kickT,airVy,downT,stepT; } Ride;
static Ride g_ride[RIDE_MAX+1]; static int g_rideEver=0;
static Ride* rideOf(int h){ return (h>=1&&h<=RIDE_MAX&&g_ride[h].on)?&g_ride[h]:NULL; }
static float rideCl(float v,float a,float b){ return !(v==v)?a:v<a?a:v>b?b:v; }
static int rideFinite(const float*f,int n){ for(int i=0;i<n;i++) if(!(f[i]-f[i]==0.f)) return 0; return 1; }
/* the ride whose rider this is, or NULL. -1 the hero */
static Ride* rideOfRider(int who){ if(!g_rideEver) return NULL; for(int h=1;h<=RIDE_MAX;h++) if(g_ride[h].on&&g_ride[h].rider==who&&!g_ride[h].st.down) return &g_ride[h]; return NULL; }
static float rideGroundAt(float x,float z,float y,int*kind){ float n[3]; if(!g_spOn){ if(kind) *kind=0; return 0.f; } return spSurface(x,z,y,n,kind); }
/* put somewhere at this height: on what's under it if that's there, and otherwise it falls from there, as E->heroDrop does (a streamed cell may not be in yet) */
static float rideDropY(float x,float z,float y){ const float g=rideGroundAt(x,z,y+0.3f,NULL); return g>y-SP_STEP?g:y; }
/* the numbers are checked once, out loud: a vehicle with no wheelbase or no weight can't be ridden, and one that isn't a number is refused */
static int rideSpecOk(const EngineRide*s){ if(!rideFinite((const float*)&s->wheelbase,(int)((offsetof(EngineRide,pose)-offsetof(EngineRide,wheelbase))/sizeof(float)))){ printf("[ride] a number in the ride isn't a number: refused\n"); fflush(stdout); return 0; }
    if(s->wheelbase<0.1f||s->mass+s->riderMass<1.f||s->grip<=0.f){ printf("[ride] a ride needs a wheelbase (%.3f m), a weight (%.1f kg) and a grip (%.2f): refused\n",s->wheelbase,s->mass+s->riderMass,s->grip); fflush(stdout); return 0; }
    return 1; }
static void rideTake(Ride*R,const EngineRide*s,size_t size){ EngineRide q; memset(&q,0,sizeof q); memcpy(&q,s,size<sizeof q?size:sizeof q); q.prop=NULL; if(!(q.pivotRate-q.pivotRate==0.f)) q.pivotRate=0.f; R->sp=q; }
static void apiPlaceTurned(int h,float x,float y,float z,float yaw,float pitch,float roll);
static int apiRideBegin(const EngineRide*s,size_t size,int rider){ if(!s||!rideSpecOk(s)) return 0;
    int h=0; for(int k=1;k<=RIDE_MAX;k++) if(!g_ride[k].on){ h=k; break; }
    if(!h){ printf("[ride] %d rides at once already: no more\n",RIDE_MAX); fflush(stdout); return 0; }
    Ride*R=&g_ride[h]; memset(R,0,sizeof *R); rideTake(R,s,size); R->on=1; R->rider=rider; g_rideEver=1;
    float x=0.f,y=0.f,z=0.f,yaw=0.f;
    if(rider==-1){ x=g_rvx; z=g_rvz; yaw=g_rvHead; y=g_feet.y; }
    else if(rider>=0&&rider<g_NS){ x=g_slots[rider].x; z=g_slots[rider].z; yaw=g_slots[rider].rotY; y=g_slotFeet[rider]; }
    R->st.x=x; R->st.z=z; R->st.yaw=yaw; R->st.y=rideDropY(x,z,y);
    if(s->prop&&s->prop[0]){ int p=apiProp(s->prop); R->obj=p>=0?apiSpawn(p,x,R->st.y,z,yaw,1.f,0):0; if(!R->obj){ printf("[ride] no model called %s: the ride goes on with nothing drawn\n",s->prop); fflush(stdout); } }
    printf("[ride] %d: %.0f kg on %.2f m of wheelbase, motor %.0f W and %.0f N to %.2f m/s, rider %d\n",h,R->sp.mass+R->sp.riderMass,R->sp.wheelbase,R->sp.motorPower,R->sp.motorForce,R->sp.motorTop,rider); fflush(stdout);
    return h; }
static void apiRideSpec(int h,const EngineRide*s,size_t size){ Ride*R=rideOf(h); if(!R||!s||!rideSpecOk(s)) return; rideTake(R,s,size); }
static void apiRideControl(int h,float throttle,float front,float rear,float steer,int kick){ Ride*R=rideOf(h); if(!R) return;
    R->thr=rideCl(throttle,0.f,1.f); R->fb=rideCl(front,0.f,1.f); R->rb=rideCl(rear,0.f,1.f); R->steer=rideCl(steer,-1.f,1.f); if(kick) R->kickReq=1; }
static const EngineRideState* apiRideState(int h){ Ride*R=rideOf(h); return R?&R->st:NULL; }
static void apiRidePlace(int h,float x,float y,float z,float yaw){ Ride*R=rideOf(h); if(!R) return; const int kicks=R->st.kicks,steps=R->st.steps; const float m=R->st.metres;
    memset(&R->st,0,sizeof R->st); R->st.kicks=kicks; R->st.steps=steps; R->stepT=0.f; R->st.metres=m; R->st.x=x; R->st.z=z; R->st.yaw=yaw; R->st.y=rideDropY(x,z,y); R->acc=0.f; R->wobPh=0.f; R->kickT=0.f; R->airVy=0.f; R->downT=0.f; R->kickReq=0; }
static void apiRideRider(int h,int rider){ Ride*R=rideOf(h); if(R) R->rider=rider; }
static void apiRideKnock(int h,int why,float vx,float vz){ Ride*R=rideOf(h); if(!R||R->st.down) return; R->st.down=1; R->st.why=why; R->st.vx+=vx; R->st.vz+=vz; R->st.vy=2.f; R->downT=0.f; }
static void apiRideEnd(int h){ Ride*R=rideOf(h); if(!R) return; if(R->obj) apiDespawn(R->obj); memset(R,0,sizeof *R); }
static void rideClear(void){ for(int h=1;h<=RIDE_MAX;h++) g_ride[h].on=0; }
/* it throws its rider. vx vy vz: how the rider leaves it, in units a second */
static void rideThrow(Ride*R,int why){ EngineRideState*S=&R->st; if(S->down) return; S->down=1; S->why=why; R->downT=0.f;
    const float v=S->speed*RIDE_U; S->vx=sinf(S->yaw)*v; S->vz=cosf(S->yaw)*v; S->vy=fabsf(v)*0.25f+2.f;
    { static int say=-1; if(say<0) say=getenv("REVA_RIDESAY")?1:0; if(say){ printf("[ride] thrown (%d) at %.2f m/s, wobble %.1f, front %.2f s, rear %.1f deg\n",why,S->speed,S->wobble,S->frontSlide,S->rearSlide); fflush(stdout); } } }
/* one tick of one ride */
static void rideStep1(Ride*R,float dt){
    EngineRide*P=&R->sp; EngineRideState*S=&R->st; const float U=RIDE_U,m=P->mass+P->riderMass,wb=P->wheelbase;
    const float fx=sinf(S->yaw),fz=cosf(S->yaw);
    /* the two wheels on what's there, each within a step of where it was */
    int kf=0,kr=0; const float half=wb*0.5f*U;
    float hf=rideGroundAt(S->x+fx*half,S->z+fz*half,S->y+0.25f,&kf), hr=rideGroundAt(S->x-fx*half,S->z-fz*half,S->y+0.25f,&kr);
    S->kindFront=kf; S->kindRear=kr;
    const float grade=(hf-hr)/(wb*U); S->grade=grade;
    const float th=atanf(grade),sn=sinf(th),cs=cosf(th);
    if(S->down){   /* it slides to a stop on its side */
        R->downT+=dt; float sp=S->speed; const float dec=6.f*dt; sp=sp>dec?sp-dec:sp<-dec?sp+dec:0.f; S->speed=sp;
        float nx=S->x+fx*sp*cs*dt*U,nz=S->z+fz*sp*cs*dt*U; const float y=spWalk(S->x,S->z,&nx,&nz,S->y,0.3f); S->x=nx; S->z=nz; S->y=g_spOn?y:0.f;
        S->lean+=(1.35f-S->lean)*fminf(1.f,dt*6.f); S->pitch=-th; S->wheelPower=S->batteryPower=0.f; S->wobble=0.f; S->frontLocked=S->rearLocked=0; S->kicking=0;
        S->metres+=fabsf(sp)*dt; return; }
    /* (11-05) an edge up at a crawl: the rider steps off and lifts it (stepUp, stepTime). REVA_NOSTEPUP=1 is its off-switch, for a control */
    { static int off=-1; if(off<0) off=getenv("REVA_NOSTEPUP")?1:0;
      const float lim=P->stepUp<0.2f?P->stepUp:0.2f;
      if(!off&&lim>0.f&&P->stepTime>0.f){
        if(R->stepT<=0.f&&fabsf(S->speed)<(P->kickTop>0.f?P->kickTop:1.5f)&&(R->thr>0.f||R->kickReq)){
            /* what's just ahead of the front wheel: a rise in 15 cm steeper than one in three is an edge, not a slope */
            const float ah=(wb*0.5f+0.15f)*U, he=rideGroundAt(S->x+fx*ah,S->z+fz*ah,S->y+lim*U+0.05f,NULL), rise=(he-hf)/U;
            if(rise>0.05f&&rise<=lim+0.01f){ R->stepT=P->stepTime; S->steps++; R->kickReq=0; } }
        if(R->stepT>0.f){ R->stepT-=dt; S->stepping=1;
            const float pace=(wb+0.3f)/P->stepTime; S->speed=pace; R->acc=0.f; S->wheelPower=S->batteryPower=0.f; S->wobble=0.f; S->frontLocked=S->rearLocked=0; S->kicking=0; R->kickT=0.f;
            float nx=S->x+fx*pace*dt*U,nz=S->z+fz*pace*dt*U; const float y=spWalk(S->x,S->z,&nx,&nz,S->y+lim*U,(P->width>0.f?P->width:0.5f)*0.5f*U);
            S->metres+=hypotf(nx-S->x,nz-S->z)/U; S->x=nx; S->z=nz; if(g_spOn) S->y=y>S->y-SP_STEP?y:S->y;
            S->pitch=-th; S->lean+=(0.f-S->lean)*fminf(1.f,dt*8.f); S->vx=fx*pace*U; S->vz=fz*pace*U; S->vy=0.f;
            if(R->stepT<=0.f){ R->stepT=0.f; S->speed=0.f; S->stepping=0; }
            return; } }
      S->stepping=0; }
    const float N=m*RIDE_G*cs, v=S->speed;
    /* the tyres: how they roll and grip on what's under each */
    const float rf=(kf>0&&kf<16&&P->rollOf[kf]>0.f)?P->rollOf[kf]:P->roll, rr=(kr>0&&kr<16&&P->rollOf[kr]>0.f)?P->rollOf[kr]:P->roll;
    const float gf=P->grip*((kf>0&&kf<16&&P->gripOf[kf]>0.f)?P->gripOf[kf]:1.f), gr=P->grip*((kr>0&&kr<16&&P->gripOf[kr]>0.f)?P->gripOf[kr]:1.f);
    /* the load on each wheel: even at rest, and onto the front as it slows */
    const float dN=rideCl(m*(-R->acc)*P->cgHeight/wb,-N*0.5f,N*0.5f), Nf=N*0.5f+dN, Nr=N*0.5f-dN;
    /* the air, against the set's wind along its way */
    const float wind=(g_windV[0]*fx+g_windV[2]*fz)/U, va=v-wind, Fair=-0.5f*P->airDensity*P->cda*va*fabsf(va);
    const float Fg=-m*RIDE_G*sn;
    /* the motor: its top pull, its power, and nothing past its top speed */
    float drive=0.f;
    if(R->thr>0.f){ const float vv=v>0.3f?v:0.3f; drive=P->motorForce; if(P->motorPower>0.f&&P->motorPower/vv<drive) drive=P->motorPower/vv;
        if(P->motorFade>0.f&&v>P->motorTop-P->motorFade) drive*=rideCl((P->motorTop-v)/P->motorFade,0.f,1.f); else if(v>=P->motorTop) drive=0.f;
        drive*=R->thr; }
    /* a kick from the rider's foot, while a foot can keep up */
    if(R->kickReq){ R->kickReq=0; if(R->kickT<=0.f&&v<P->kickTop&&P->kickTime>0.f){ R->kickT=P->kickTime; S->kicks++; } }
    float Fkick=0.f; if(R->kickT>0.f){ R->kickT-=dt; if(v<P->kickTop) Fkick=P->kickForce; } S->kicking=R->kickT>0.f;
    /* the brakes, each held to its tyre's grip: past it, the wheel locks and slides */
    const float dF=R->fb*P->brakeFront, dR=R->rb*P->brakeRear, moving=fabsf(v)>0.3f;
    /* a wheel locks past the tyre's grip, and once it's sliding it stays locked until the lever's below what the sliding tyre holds: without that, the load moving
     * back as it slides let it go and lock again every other tick, and a slide never built */
    S->frontLocked=moving&&dF>(S->frontLocked?P->slide:1.f)*gf*Nf; S->rearLocked=moving&&dR>(S->rearLocked?P->slide:1.f)*gr*Nr;
    const float Ff=S->frontLocked?P->slide*gf*Nf:dF, Fr=S->rearLocked?P->slide*gr*Nr:dR;
    const float Fother=Fg+Fair+drive+Fkick, Fres=(rf*Nf+rr*Nr)+Ff+Fr;
    float a, nv;
    if(fabsf(v)>0.05f){ const float sg=v>0.f?1.f:-1.f; a=(Fother-sg*Fres)/m; nv=v+a*dt; if((nv>0.f)!=(v>0.f)&&fabsf(Fother)<=Fres) nv=0.f; }
    else if(fabsf(Fother)<=Fres){ a=0.f; nv=0.f; }
    else { const float sg=Fother>0.f?1.f:-1.f; a=(Fother-sg*Fres)/m; nv=v+a*dt; }
    R->acc=(nv-v)/(dt>1e-6f?dt:1e-6f); S->speed=nv;
    S->wheelPower=drive*(nv>0.f?nv:0.f); S->batteryPower=P->motorEff>0.f?S->wheelPower/P->motorEff:S->wheelPower;
    S->drag=Fair; S->rolling=-(rf*Nf+rr*Nr); S->gravity=Fg;
    /* the front end's wobble: past its onset it swings, the more the faster; held still it dies, fought or braked on it grows */
    const float sp=fabsf(nv), held=fabsf(R->steer)<0.2f&&fabsf(R->steer-R->steerWas)<0.02f;
    float A=S->wobble;
    if(P->wobbleOnset>0.f&&sp>P->wobbleOnset){ if(A<0.3f) A=0.3f; const float grow=P->wobbleGrow*(sp-P->wobbleOnset)*(1.f+R->fb)*(held?1.f:1.5f), die=held?P->wobbleHold:0.f; A+=(grow-die)*A*dt; }
    else A-=(P->wobbleHold+1.f)*A*dt;
    if(A<0.01f) A=0.f;
    S->wobble=A; R->wobPh+=6.2831853f*(P->wobbleHz>0.f?P->wobbleHz:5.f)*dt; if(R->wobPh>6.2831853f) R->wobPh-=6.2831853f;
    /* the bars, and the wheel's way */
    float steer=R->steer*P->steerMax*(P->steerHalf>0.f?P->steerHalf/(P->steerHalf+sp):1.f)+A*sinf(R->wobPh);
    if(S->frontLocked) steer=0.f;
    float yawRate=-nv*tanf(steer*0.0174532925f)/wb;   /* the bars to the right (steer above 0) turn it right, which is its yaw going down: +x is its left */
    /* nearly stopped, the rider swings it round by hand, a foot down: against a wall, or to turn back, it can always be pointed somewhere else */
    if(sp<1.f&&P->pivotRate>0.f) yawRate+=-R->steer*P->pivotRate*(1.f-sp);
    const float lat=nv*yawRate, latMax=gr*RIDE_G;
    if(fabsf(lat)>latMax&&fabsf(nv)>0.5f) yawRate=(lat>0.f?latMax:-latMax)/nv;
    /* a locked rear in a turn comes round; let go and it straightens */
    if(S->rearLocked&&fabsf(yawRate)>0.05f){ const float d=fabsf(yawRate)*57.29578f*dt*2.5f; S->rearSlide+=d; yawRate*=1.f+S->rearSlide/30.f; }
    else S->rearSlide=fmaxf(0.f,S->rearSlide-90.f*dt);
    if(S->frontLocked) S->frontSlide+=dt*(1.f+2.f*fabsf(R->steer)); else S->frontSlide=fmaxf(0.f,S->frontSlide-2.f*dt);
    R->steerWas=R->steer;
    S->yaw+=yawRate*dt; if(S->yaw>3.14159265f) S->yaw-=6.2831853f; if(S->yaw<-3.14159265f) S->yaw+=6.2831853f;
    S->lean+=(-atanf(lat/RIDE_G)-S->lean)*fminf(1.f,dt*8.f); S->lean=rideCl(S->lean,-0.9f,0.9f);
    /* where it goes: what's solid stops it, and fast enough, throws its rider */
    const float step=nv*cs*dt*U, wx=S->x+sinf(S->yaw)*step, wz=S->z+cosf(S->yaw)*step; float nx=wx,nz=wz;
    const float stand=spWalk(S->x,S->z,&nx,&nz,S->y,(P->width>0.f?P->width:0.5f)*0.5f*U);
    const float went=hypotf(nx-S->x,nz-S->z), want=fabsf(step);
    S->metres+=went/U;
    if(want>1e-4f&&went<want*0.5f){ if(sp>P->crashSpeed&&P->crashSpeed>0.f){ S->x=nx; S->z=nz; rideThrow(R,4); return; } S->speed=0.f; R->acc=0.f; }
    S->x=nx; S->z=nz;
    /* off an edge it falls, and lands */
    const float gy=g_spOn?stand:0.f;
    if(R->airVy!=0.f||S->y>gy+SP_STEP){ R->airVy-=RIDE_G*U*dt; S->y+=R->airVy*dt; if(S->y<=gy){ const float hit=-R->airVy/U; S->y=gy; R->airVy=0.f; if(hit>6.f){ rideThrow(R,5); return; } } }
    else S->y+=(gy-S->y)*fminf(1.f,dt*20.f);
    S->pitch=-th;
    S->vx=sinf(S->yaw)*nv*U; S->vz=cosf(S->yaw)*nv*U; S->vy=0.f;
    if(S->frontSlide>P->frontSave&&P->frontSave>0.f) rideThrow(R,1);
    else if(S->rearSlide>P->rearSlideFall&&P->rearSlideFall>0.f) rideThrow(R,2);
    else if(P->wobbleThrow>0.f&&A>P->wobbleThrow) rideThrow(R,3); }
/* its rider on its deck: a body's place and heading. The hero's is placed in the hero's own block (rideHero) */
static void rideRiders(void){
    for(int h=1;h<=RIDE_MAX;h++){ Ride*R=&g_ride[h]; if(!R->on||R->st.down||R->rider<0||R->rider>=g_NS) continue;
        Slot*s=&g_slots[R->rider]; s->x=s->tx=R->st.x; s->z=s->tz=R->st.z; s->rotY=s->h=R->st.yaw; s->spd=0.f; s->moving=0;   /* it stands on the deck: no walk under it */
        g_slotFeet[R->rider]=R->st.y+R->sp.deckHeight*RIDE_U; } }
/* the model under the rider, turned as it is */
static void rideModels(void){
    for(int h=1;h<=RIDE_MAX;h++){ Ride*R=&g_ride[h]; if(!R->on||!R->obj) continue; apiPlaceTurned(R->obj,R->st.x,R->st.y,R->st.z,R->st.yaw,R->st.pitch,R->st.lean); } }
static void rideStep(float dt){ if(!g_rideEver||dt<=0.f) return;
    for(int h=1;h<=RIDE_MAX;h++) if(g_ride[h].on) rideStep1(&g_ride[h],dt);
    rideRiders(); rideModels();
    { static int say=-2; static long n=0; if(say==-2){ const char*e=getenv("REVA_RIDESAY"); say=e?atoi(e):0; }
      if(say>0&&(++n%say)==0) for(int h=1;h<=RIDE_MAX;h++) if(g_ride[h].on){ const EngineRideState*S=&g_ride[h].st;
          printf("[ride] %d: %.2f m/s (%.1f mph), grade %+.1f%%, wheel %.0f W, battery %.0f W, drag %.0f N, rolling %.0f N, gravity %.0f N, %s/%s, locked %d%d, wobble %.1f, kicks %d, %.0f m%s\n",
              h,S->speed,S->speed*2.23694f,S->grade*100.f,S->wheelPower,S->batteryPower,S->drag,S->rolling,S->gravity,apiSurfaceName(S->kindFront),apiSurfaceName(S->kindRear),S->frontLocked,S->rearLocked,S->wobble,S->kicks,S->metres,S->down?" DOWN":""); fflush(stdout); } } }
/* the hero, if she's riding: where she stands, which way, and how she's tipped. 1 if the ride placed her */
static int rideHero(float*x,float*z,float*head,float*pitch,float*roll){ Ride*R=rideOfRider(-1); if(!R) return 0;
    *x=R->st.x; *z=R->st.z; *head=R->st.yaw; *pitch=R->st.pitch; *roll=R->st.lean;
    g_feet.y=R->st.y+R->sp.deckHeight*RIDE_U; g_feet.vy=0.f; g_feet.ground=1; return 1; }
/* a body's model matrix turned with its ride: the same place and size, tipped and leaned */
static void rideBodyModel(int si,float*model){ Ride*R=rideOfRider(si); if(!R) return;
    const float sc=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]); float qy[4],qx[4],qz[4],qt[4],q[4],t[3]={model[12],model[13],model[14]},s3[3]={sc,sc,sc};
    qaxis(qy,0,1,0,R->st.yaw); qaxis(qx,1,0,0,R->st.pitch); qaxis(qz,0,0,1,R->st.lean); qmul(qt,qy,qx); qmul(q,qt,qz); m4_trs(model,t,q,s3); }
/* ---- (11-06) THE HANDS ON THE GRIPS AND THE FEET ON THE DECK, ON ANY RIG (EngineRide.reach) --------------------------------------------------------------------------------------
 * The angles in ridePose turn bones about their parents' axes, which is right only for a rig whose bones rest square to the body. On the co-star's (a bought character's, the UE4 skeleton) the
 * arms went out and up and the legs stayed straight. Here every limb is put where it has to be by its own two bones, in the body's model space (+z its front, +x its left, y up,
 * the deck's middle under its origin and the deck's top at its rest floor): a bone turns by the shortest arc from where its child is to where it has to be, so it keeps its
 * twist, and a knee or an elbow bends to the side it's told. The hips sink by the crouch and the back tips by the lean about the body's own side to side, the head keeps its line,
 * the feet stand on the deck at the height they rest at, flat, the back one turned out, the hands lie over the bar and the fingers close round it. A kick puts the back foot on
 * the ground beside the deck, pushes it back along the ground and brings it up again, and a swing takes the right hand off the bar and out. REVA_NORIDEREACH=1 is the angles, for
 * a control. What's drawn, never the rules: nothing here is in [simhash]. */
static void rideQv(const float*q,const float*v,float*o){   /* v turned by the unit quaternion q */
    const float tx=2.f*(q[1]*v[2]-q[2]*v[1]), ty=2.f*(q[2]*v[0]-q[0]*v[2]), tz=2.f*(q[0]*v[1]-q[1]*v[0]);
    o[0]=v[0]+q[3]*tx+(q[1]*tz-q[2]*ty); o[1]=v[1]+q[3]*ty+(q[2]*tx-q[0]*tz); o[2]=v[2]+q[3]*tz+(q[0]*ty-q[1]*tx); }
static float rideLen(const float*v){ return sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
/* where a node is in its body's model space, how it's turned, and its scale: walked down from the root through its parents. Q and T NULL: the rest pose */
static void rideWorld(const Pack*P,float Q[][4],float T[][3],int i,float*pos,float*rot,float*scl){
    int ch[64],n=0; for(int a=i;a>=0&&a<(int)P->nNodes&&n<64;a=P->nodes[a].parent) ch[n++]=a;
    float p[3]={0.f,0.f,0.f},r[4]={0.f,0.f,0.f,1.f},s=1.f;
    while(n>0){ const int b=ch[--n]; const float*tb=T?T[b]:P->nodes[b].t,*qb=Q?Q[b]:P->nodes[b].q; const float v[3]={tb[0]*s,tb[1]*s,tb[2]*s}; float w[3],r2[4];
        rideQv(r,v,w); p[0]+=w[0]; p[1]+=w[1]; p[2]+=w[2]; qmul(r2,r,qb); memcpy(r,r2,16); s*=P->nodes[b].s[0];
        const float l=sqrtf(r[0]*r[0]+r[1]*r[1]+r[2]*r[2]+r[3]*r[3]); if(l>1e-8f) for(int k=0;k<4;k++) r[k]/=l; }
    if(pos) memcpy(pos,p,12); if(rot) memcpy(rot,r,16); if(scl) *scl=s; }
/* bone b turned by dq, a turn in model space: taken into its parent's frame */
static void rideTurn(const Pack*P,float Q[][4],float T[][3],int b,const float*dq){
    float pr[4]={0.f,0.f,0.f,1.f},ic[4],t1[4],t2[4]; if(P->nodes[b].parent>=0) rideWorld(P,Q,T,P->nodes[b].parent,NULL,pr,NULL);
    qconj(ic,pr); qmul(t1,ic,dq); qmul(t2,t1,pr); qmul(t1,t2,Q[b]); memcpy(Q[b],t1,16); }
/* bone b turned so it lies as `want` in model space */
static void rideSetWorld(const Pack*P,float Q[][4],float T[][3],int b,const float*want){
    float pr[4]={0.f,0.f,0.f,1.f},ic[4]; if(P->nodes[b].parent>=0) rideWorld(P,Q,T,P->nodes[b].parent,NULL,pr,NULL);
    qconj(ic,pr); qmul(Q[b],ic,want); }
/* a limb of two bones (a the upper, b the lower, c its end) brought to reach tgt, bending toward pole. Out of reach, it points straight at it */
static void rideReach(const Pack*P,float Q[][4],float T[][3],int a,int b,int c,const float*tgt,const float*pole){
    if(a<0||b<0||c<0) return;
    float A[3],B[3],C[3],u[3],v[3],dq[4];
    rideWorld(P,Q,T,a,A,NULL,NULL); rideWorld(P,Q,T,b,B,NULL,NULL); rideWorld(P,Q,T,c,C,NULL,NULL);
    const float ab[3]={B[0]-A[0],B[1]-A[1],B[2]-A[2]}, bc[3]={C[0]-B[0],C[1]-B[1],C[2]-B[2]}, at[3]={tgt[0]-A[0],tgt[1]-A[1],tgt[2]-A[2]};
    const float L1=rideLen(ab),L2=rideLen(bc); float d=rideLen(at); if(L1<1e-5f||L2<1e-5f||d<1e-5f) return;
    const float dir[3]={at[0]/d,at[1]/d,at[2]/d};
    d=rideCl(d,fabsf(L1-L2)*1.001f+1e-5f,(L1+L2)*0.999f);
    /* the side it bends to, square to the way: the pole, or the way it bends now if the pole is along the way */
    float n[3]; const float pd=pole[0]*dir[0]+pole[1]*dir[1]+pole[2]*dir[2]; for(int k=0;k<3;k++) n[k]=pole[k]-dir[k]*pd;
    float nl=rideLen(n);
    if(nl<1e-4f){ const float bd=ab[0]*dir[0]+ab[1]*dir[1]+ab[2]*dir[2]; for(int k=0;k<3;k++) n[k]=ab[k]-dir[k]*bd; nl=rideLen(n); if(nl<1e-6f) return; }
    for(int k=0;k<3;k++) n[k]/=nl;
    const float ca=rideCl((L1*L1+d*d-L2*L2)/(2.f*L1*d),-1.f,1.f), sa=sqrtf(fmaxf(0.f,1.f-ca*ca));
    for(int k=0;k<3;k++){ u[k]=ab[k]/L1; v[k]=dir[k]*ca+n[k]*sa; }   /* the upper bone: from where its child is to where it has to be */
    qarc(dq,u,v); rideTurn(P,Q,T,a,dq);
    rideWorld(P,Q,T,b,B,NULL,NULL); rideWorld(P,Q,T,c,C,NULL,NULL);   /* and the lower: from where its end is now to the target */
    const float bc2[3]={C[0]-B[0],C[1]-B[1],C[2]-B[2]}, be[3]={A[0]+dir[0]*d-B[0],A[1]+dir[1]*d-B[1],A[2]+dir[2]*d-B[2]};
    const float l1=rideLen(bc2),l2=rideLen(be); if(l1<1e-6f||l2<1e-6f) return;
    for(int k=0;k<3;k++){ u[k]=bc2[k]/l1; v[k]=be[k]/l2; }
    qarc(dq,u,v); rideTurn(P,Q,T,b,dq); }
/* is this ride's rider put on it by its limbs? */
static int rideReachOn(const Ride*R){ static int off=-1; if(off<0) off=getenv("REVA_NORIDEREACH")?1:0;
    return !off&&R->sp.reach&&rideFinite(&R->sp.gripFwd,7); }
static void rideReachPose(const Ride*R,Pack*P,float Q[][4],float T[][3],float deck){   /* deck: the deck's top in the body's model space, where its rest floor stands */
    const EngineRide*sp=&R->sp; const EngineRidePose*z=&sp->pose; float dq[4];
    const float m=RIDE_U*((P->scale>1e-6f)?(1.f/P->scale):1.f);   /* a metre, in the body's model units */
    /* the hips sink, and stand over the middle between the feet: in model space, taken into the pelvis's parent's frame */
    if(P->pb[0]>=0){ const int pel=P->pb[0],pa=P->nodes[pel].parent; float pr[4]={0.f,0.f,0.f,1.f},ic[4],s=1.f,o[3]; const float d[3]={0.f,-z->crouch*m,(sp->footFront-sp->footBack)*0.5f*m};
        if(pa>=0) rideWorld(P,Q,T,pa,NULL,pr,&s);
        qconj(ic,pr); rideQv(ic,d,o); if(s>1e-6f) for(int k=0;k<3;k++) T[pel][k]+=o[k]/s; }
    /* the back tips forward about the body's own side to side, and the head keeps its line */
    if(z->lean!=0.f){ for(int k=1;k<=3;k++){ const int b=P->pb[k]; if(b<0) continue; qaxis(dq,1.f,0.f,0.f,z->lean/3.f); rideTurn(P,Q,T,b,dq); }
        if(P->pb[5]>=0){ qaxis(dq,1.f,0.f,0.f,-z->lean*0.6f); rideTurn(P,Q,T,P->pb[5],dq); } }
    /* the feet: the left in front, on the deck at the height each rests at, flat; the back one turned out. A kick takes the back one to the ground beside the deck */
    for(int side=0;side<2;side++){ const int th=P->pb[side?14:12],kn=P->pb[side?15:13],ft=side?P->footR:P->footL; if(th<0||kn<0||ft<0) continue;
        float ra[3],rq[4],tg[3],yq[4],w[4]; rideWorld(P,NULL,NULL,ft,ra,rq,NULL);
        tg[0]=(side?-1.f:1.f)*sp->footOut*m; tg[1]=ra[1]; tg[2]=side?-sp->footBack*m:sp->footFront*m;
        float turn=side?-sp->footTurn*0.0174532925f:0.f;
        if(side&&R->st.kicking&&sp->kickTime>0.f){ const float u=rideCl(1.f-R->kickT/sp->kickTime,0.f,1.f), gy=ra[1]-sp->deckHeight*m;
            const float px=-(sp->footOut+0.14f)*m, pz0=-0.05f*m, pz1=-0.55f*m;   /* beside the deck, from just behind the deck's middle back along the ground */
            if(u<0.2f){ const float e=u/0.2f, s=e*e*(3.f-2.f*e); tg[0]+=(px-tg[0])*s; tg[2]+=(pz0-tg[2])*s; tg[1]+=(gy-tg[1])*s+sinf(e*3.14159265f)*0.06f*m; }
            else if(u<0.75f){ const float e=(u-0.2f)/0.55f; tg[0]=px; tg[1]=gy; tg[2]=pz0+(pz1-pz0)*e; }
            else { const float e=(u-0.75f)/0.25f, s=e*e*(3.f-2.f*e); tg[0]=px+(tg[0]-px)*s; tg[2]=pz1+(tg[2]-pz1)*s; tg[1]=gy+(tg[1]-gy)*s+sinf(e*3.14159265f)*0.10f*m; }
            turn*=0.3f; }
        const float pole[3]={side?-0.25f:0.25f,0.f,1.f};   /* the knees forward, a little out */
        rideReach(P,Q,T,th,kn,ft,tg,pole);
        qaxis(yq,0.f,1.f,0.f,turn); qmul(w,yq,rq); rideSetWorld(P,Q,T,ft,w); }
    /* the hands: the wrist a little behind and over the grip, the hand laid over the bar, the fingers round it. A swing takes the right one out to the side */
    for(int side=0;side<2;side++){ const int up=P->pb[side?10:7],fo=P->pb[side?11:8],ha=side?P->gripNode:P->gripL; if(up<0||fo<0||ha<0) continue;
        const float sg=side?-1.f:1.f, sw=side?rideCl(z->swing,0.f,1.f):0.f;
        float tg[3]={sg*sp->gripHalf*m, deck+(sp->gripUp+0.03f)*m, (sp->gripFwd-0.06f)*m};
        if(sw>0.f){ float sh[3]; rideWorld(P,Q,T,up,sh,NULL,NULL); const float out[3]={sh[0]+sg*0.45f*m,sh[1]+0.05f*m,sh[2]+0.30f*m}; for(int k=0;k<3;k++) tg[k]+=(out[k]-tg[k])*sw; }
        const float pole[3]={sg*0.6f,-0.6f,-0.5f};   /* the elbows out, down and back */
        rideReach(P,Q,T,up,fo,ha,tg,pole);
        /* the hand forward and a little down, over the bar: from where its middle finger starts now */
        const int mid=P->pb[16+side*15+2*3];
        if(mid>=0){ float H[3],M[3],u[3]; rideWorld(P,Q,T,ha,H,NULL,NULL); rideWorld(P,Q,T,mid,M,NULL,NULL); for(int k=0;k<3;k++) u[k]=M[k]-H[k];
            const float ul=rideLen(u); if(ul>1e-6f){ const float vv[3]={0.f,-0.35f*(1.f-sw),1.f}, vl=rideLen(vv); float v[3]={vv[0]/vl,vv[1]/vl,vv[2]/vl}; for(int k=0;k<3;k++) u[k]/=ul; qarc(dq,u,v); rideTurn(P,Q,T,ha,dq); } }
        for(int f=1;f<5;f++) for(int j=0;j<3;j++){ const int b=P->pb[16+side*15+f*3+j]; if(b<0) continue; qaxis(dq,1.f,0.f,0.f,0.75f); rideTurn(P,Q,T,b,dq); } }
    /* REVA_REACHSAY=<frames>: where each hand and foot was told to go and where it got to, in metres from the deck's middle, that often */
    { static int say=-2; static long n=0; if(say==-2){ const char*e=getenv("REVA_REACHSAY"); say=e?atoi(e):0; }
      if(say>0&&(n++%say)==0){ const int end[4]={P->gripL,P->gripNode,P->footL,P->footR}; const char*nm[4]={"left hand","right hand","left foot","right foot"};
          printf("[reach] %s: deck at %.3f model units, a metre is %.3f, scale %.4f\n",P->label,deck,m,P->scale);
          for(int k=0;k<4;k++){ const int b=P->pb[k]; if(b<0) continue; float r3[3],p3[3]; rideWorld(P,NULL,NULL,b,r3,NULL,NULL); rideWorld(P,Q,T,b,p3,NULL,NULL);
              printf("[reach]   canon %d (%s) rests at %+.3f %+.3f %+.3f m, is at %+.3f %+.3f %+.3f m\n",k,packStr(P,P->nodes[b].name),r3[0]/m,(r3[1]-deck)/m,r3[2]/m,p3[0]/m,(p3[1]-deck)/m,p3[2]/m); }
          for(int k=0;k<4;k++){ if(end[k]<0) continue; float e3[3]; rideWorld(P,Q,T,end[k],e3,NULL,NULL);
              printf("[reach]   %-10s at %+.3f %+.3f %+.3f m\n",nm[k],e3[0]/m,(e3[1]-deck)/m,e3[2]/m); }
          for(int side=0;side<2;side++){ const int a=P->pb[side?10:7],b=P->pb[side?11:8],c=side?P->gripNode:P->gripL; if(a<0||b<0||c<0) continue; float A[3],B[3],C[3],ab[3],bc[3];
              rideWorld(P,Q,T,a,A,NULL,NULL); rideWorld(P,Q,T,b,B,NULL,NULL); rideWorld(P,Q,T,c,C,NULL,NULL); for(int k=0;k<3;k++){ ab[k]=B[k]-A[k]; bc[k]=C[k]-B[k]; }
              printf("[reach]   %s arm: shoulder %+.3f %+.3f %+.3f m, upper %.3f m, lower %.3f m, elbow %+.3f %+.3f %+.3f m\n",side?"right":"left",A[0]/m,(A[1]-deck)/m,A[2]/m,rideLen(ab)/m,rideLen(bc)/m,B[0]/m,(B[1]-deck)/m,B[2]/m); }
          fflush(stdout); } } }
/* the rider standing on the deck: its canonical bones, laid over what its clip gives (the riding pose, as the skate pose is) */
static void ridePose(const Ride*R,Pack*P,float Q[][4],float T[][3],float deck){ if(P->pbOK<=0) return; if(rideReachOn(R)){ rideReachPose(R,P,Q,T,deck); return; } const EngineRidePose*z=&R->sp.pose; float off[4],tq[4];
    if(P->pb[0]>=0){ const float isc=(P->scale>1e-6f)?(1.f/P->scale):1.f; T[P->pb[0]][1]-=z->crouch*RIDE_U*isc; }
    const float kick=R->st.kicking?sinf((1.f-R->kickT/(R->sp.kickTime>0.f?R->sp.kickTime:1.f))*3.14159265f):0.f;
    for(int side=0;side<2;side++){ const int th=P->pb[side?14:12],kn=P->pb[side?15:13]; const float lead=side?-1.f:1.f;   /* the left foot in front */
        if(th>=0){ memcpy(Q[th],P->nodes[th].q,16); qaxis(off,1,0,0,-(lead*z->legSplit+(side?kick*0.9f:0.f))); qaxis(tq,0,0,1,(side?-1.f:1.f)*z->stance); qmul(off,off,tq); qmul(Q[th],Q[th],off); }
        if(kn>=0){ memcpy(Q[kn],P->nodes[kn].q,16); qaxis(off,1,0,0,z->knee+(side?kick*0.6f:0.f)); qmul(Q[kn],Q[kn],off); } }
    for(int k=0;k<3;k++){ const int b=P->pb[1+k]; if(b<0) continue; qaxis(off,1,0,0,z->lean/3.f); qmul(Q[b],Q[b],off); }
    if(P->pb[5]>=0){ qaxis(off,1,0,0,-z->lean*0.6f); qmul(Q[P->pb[5]],Q[P->pb[5]],off); }
    for(int side=0;side<2;side++){ const int up=P->pb[side?10:7],fo=P->pb[side?11:8]; const float sg=side?-1.f:1.f;
        const float sw=side?rideCl(z->swing,0.f,1.f):0.f;   /* the right arm off the bars for a blow */
        /* the arms are turned in their parent's frame, over what the clip gives (an arm hanging at the side): forward about the body's side to side, out about its front to back.
         * ⛔ That's right for a rig whose bones rest square to the body (the people builder's, the tower's); a rig whose bones rest at other angles needs its own signs */
        if(up>=0){ qaxis(off,1,0,0,-z->armFwd*(1.f-sw*0.5f)); qaxis(tq,0,0,1,sg*(z->armSpread+sw*1.2f)); qmul(off,tq,off); qmul(Q[up],off,Q[up]); }
        if(fo>=0){ qaxis(off,1,0,0,-z->elbow*(1.f-sw)); qmul(Q[fo],off,Q[fo]); } } }   /* the forearm forward and up, about the same side to side */
static void rideHeroPose(Pack*P,float Q[][4],float T[][3],float deck){ const Ride*R=rideOfRider(-1); if(R) ridePose(R,P,Q,T,deck); }
/* (11-06) the hero is on a ride that puts her feet on it: she stands on her rest floor, not on her lowest foot, because a kick takes a foot under the deck */
static int rideHeroReach(void){ const Ride*R=rideOfRider(-1); return R&&rideReachOn(R); }
static void rideBodyPose(int si,Pack*P,float Q[][4],float T[][3]){ const Ride*R=rideOfRider(si); if(R) ridePose(R,P,Q,T,P->mny); }   /* a body stands its rest floor (mny) on its feet's height */
static void rideHash(void){ if(!g_rideEver) return;
    for(int h=1;h<=RIDE_MAX;h++){ const Ride*R=&g_ride[h]; if(!R->on) continue; const EngineRideState*S=&R->st;
        float f9[16]={S->x,S->y,S->z,S->yaw,S->speed,S->lean,S->wobble,S->frontSlide,S->rearSlide,(float)S->down,(float)S->why,(float)S->kicks,R->acc,R->kickT,R->airVy,(float)R->rider}; g_shDumpTag="ride"; simHashMix(f9,16);
        if(R->sp.stepUp>0.f){ float f2[2]={R->stepT,(float)S->steps}; simHashMix(f2,2); } } }
