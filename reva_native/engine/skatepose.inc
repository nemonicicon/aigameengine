/* ---- THE SKATE POSE, APPLIED TO ANY BODY ------------------------------------------
 * SkPose angles are radians ADDED to the rest pose, so this is a layer, not a
 * replacement -- the same shape as scnHeadLayer. Everything is expressed against the
 * canonical bone map (pb[]), which every baked body carries, so it costs nothing to
 * put a citizen on a board.
 *
 * hipDrop is a FRACTION of the body's own leg length, measured from its rest pose, or a
 * tall body and a short one crouch by different amounts and only one of them looks like
 * it is standing on something. */
/* deck height: a skateboard stands ~9cm off the ground, and the engine runs 1.75
 * units per metre. Used to lift the rider onto the board and to drop the board
 * back under the rider -- the same constant both ways. */
#define SK_DECK_H 0.16f
/* +1 regular, -1 goofy. A quarter turn off the direction of travel IS the stance. */
#define SK_STANCE 1.0f
/* WHICH WAY A KNEE FOLDS. The reference sign (+) is correct on every body tested --
 * reva2 and the town citizens both -- and it was settled by LOOKING: at +1 the knee
 * leads and the shin trails, at -1 the leg bows backwards into a hyperextension.
 *
 * ⛔ A rest-pose PRE-BEND PROBE was built for this and thrown away. It works on a rig
 * authored with a few degrees already in the knee (the citizens: 4051 -> 5810, dot
 * positive) and decides on noise for one that is straight (reva2: 0.062 -> 0.045, dot
 * NEGATIVE) -- so it flipped the one body that was already right. The lesson is the
 * bench's own: a check that has to guess which way is "forward" in an arbitrary rig
 * proves nothing, and two rounds went into tuning against it before that was obvious.
 * REVA_KNEESIGN overrides per run if a body ever needs it. */
static float skateKneeSign(Pack*P){ (void)P;
    static int init=0; static float v=1.f;
    if(!init){ init=1; const char*e=getenv("REVA_KNEESIGN"); if(e) v=(float)atof(e); }
    return v; }


/* ⭐ ONE SOURCE FOR THE RIDER'S ARMS. This code existed TWICE -- in skateApply for the
 * set's riders, and inlined in the hero's g_skate block -- so any fix applied to one copy
 * left the other wrong. That is how the mouth aperture survived two fixes.
 *
 * ⛔ AND IT TWINNED. Both arms took the same spread, the same forward and the SAME elbow,
 * differing only by a sign on the spread axis: a mirror pair hitting every extreme on the
 * same frame. Williams calls this out by name, and no tuning of the pose library could
 * have cured it, because the library only carries ONE set of arm numbers.
 *
 *   sig -- the body's signature, 0..1. DETERMINISTIC from the body, so a crew of six is
 *          six people and it is the same six every take. Noise would flicker.
 *   t   -- seconds, for a slow drift that is out of phase between the two arms. */
static void skateArms(Pack*P,const SkPose*z,float Q[][4],float sig,float t){
    float off[4],tq[4];
    static int am=-1, notw=-1;
    if(am<0){ const char*e=getenv("REVA_ARMMODE"); am=e?atoi(e):0; }
    if(notw<0) notw=getenv("REVA_TWIN")?1:0;
    for(int side=0;side<2;side++){
        int up=P->pb[side?10:7], fo=P->pb[side?11:8];
        float sgn=side?-1.f:1.f;
        if(up>=0) memcpy(Q[up],P->nodes[up].q,16);
        if(fo>=0) memcpy(Q[fo],P->nodes[fo].q,16);
        float spread=z->armSpread, fwd=z->armFwd, elb=z->elbow;
        if(!notw){
            /* 1. THE STANCE SPLIT -- which arm is over the nose is a fact the engine holds */
            float lead=(side==((SK_STANCE>0.f)?0:1))?1.f:-1.f;
            spread += lead*0.15f;
            fwd    += lead*0.20f;
            elb    -= lead*0.17f;
            /* 2. THE SIGNATURE */
            float s2=sig*6.2831853f;
            spread += sinf(s2+(float)side*2.1f)*0.10f;
            elb    += cosf(s2*1.7f+(float)side*1.3f)*0.13f;
            /* 3. AND THEY MUST NOT ARRIVE TOGETHER */
            float w=t*0.85f + sig*5.0f + (float)side*1.9f;
            fwd    += sinf(w)*0.10f;
            spread += sinf(w*0.63f+1.1f)*0.075f; }
        if(up>=0){
            /* WHICH axis spreads an arm, and whether the sides need opposite signs, is a
             * property of the rig -- a mirrored rig already reverses the motion and
             * flipping the sign as well throws one arm out. REVA_ARMMODE selects
             * axis(Z,Y,X) x sign(mirror,same) so it can be MEASURED on a new body. */
            float sg=(am&1)?1.f:sgn;
            float ax=0.f,ay=0.f,az=0.f;
            if(am<2){ az=1.f; } else if(am<4){ ay=1.f; } else { ax=1.f; }
            qaxis(off,ax,ay,az,sg*spread);
            qaxis(tq,1,0,0,fwd);
            qmul(off,off,tq); qmul(Q[up],Q[up],off); }
        if(fo>=0){ qaxis(off,1,0,0,elb); qmul(Q[fo],Q[fo],off); } } }

static void skateApply(Pack*P,const SkPose*z,float Q[][4],float T[][3],float legLen,
                       float sig,float t){
    (void)legLen;
    float off[4],tq[4];
    float ks=skateKneeSign(P);   /* which way THIS rig folds */
    /* hips sink -- authored in world units, so divide by the pack's scale to reach its
     * own rig. This is the reference's form and it is what keeps one number working on
     * bodies of different sizes. */
    if(P->pb[0]>=0){ float isc=(P->scale>1e-6f)?(1.f/P->scale):1.f;
                     T[P->pb[0]][1] -= z->hipDrop*isc; }
    /* LEGS. Reset to rest, splay for the stance, then thigh and knee BOTH positive about
     * X -- a knee folds the heel back, and getting that sign wrong is the inversion. */
    for(int side=0;side<2;side++){
        int th=P->pb[side?14:12], kn=P->pb[side?15:13];
        float sgn=side?-1.f:1.f;
        if(th>=0){ memcpy(Q[th],P->nodes[th].q,16);
            qaxis(off,0,0,1,sgn*0.42f);          /* stance width, across the deck */
            /* the THIGH and the KNEE do not necessarily share a sign: the knee folds
             * the shin back, the thigh swings the whole leg forward. Separately
             * overridable so each can be settled by looking. */
            { static float ts=-99.f; if(ts<-90.f){ const char*e=getenv("REVA_THIGHSIGN"); ts=e?(float)atof(e):-1.f; }   /* MEASURED by A/B: +1 reclines her with the feet ahead of the hips, -1 stacks her over the board */
              qaxis(tq,1,0,0,ts*z->thigh); }
            qmul(off,off,tq); qmul(Q[th],Q[th],off); }
        if(kn>=0){ memcpy(Q[kn],P->nodes[kn].q,16);
            qaxis(off,1,0,0,ks*z->knee); qmul(Q[kn],Q[kn],off); } }
    /* SPINE over the three chest bones, and the twist is what opens the shoulders down
     * the line of travel instead of square across the board. */
    for(int k=0;k<3;k++){ int b=P->pb[1+k]; if(b<0) continue;
        memcpy(Q[b],P->nodes[b].q,16);
        qaxis(off,1,0,0,z->spineFold*0.5f);
        qaxis(tq,0,1,0,z->spineTwist*0.42f);
        qmul(off,off,tq); qmul(Q[b],Q[b],off); }
    if(P->pb[5]>=0){ memcpy(Q[P->pb[5]],P->nodes[P->pb[5]].q,16);
        qaxis(off,0,1,0,z->headTurn);
        qaxis(tq,1,0,0,-z->spineFold*0.30f);
        qmul(off,off,tq); qmul(Q[P->pb[5]],Q[P->pb[5]],off); }
    skateArms(P,z,Q,sig,t);   /* the one source; see the note on it */ }

/* the rider's pose this frame: rolling, carving, or partway through a trick */
/* THE STIFFNESS TABLE, from the reference. Hips and feet heavy and quick; the spine
 * looser; the head and arms loosest. Reading the twelve numbers IS reading the timing. */
static const float SK_STF[12]={220.f,190.f,190.f,130.f,130.f, 95.f, 95.f, 95.f, 75.f,300.f,300.f,170.f};
static const float SK_DMP[12]={ 23.f, 21.f, 21.f, 17.f, 17.f, 13.f, 13.f, 13.f, 12.f, 32.f, 32.f, 21.f};

/* Every channel through its own spring. This is what gives the body mass -- and its
 * absence is what made the trick read as stiff and jerky. */
static void skateSpring(SetActor*A,SkPose*p,float dt){
    float*tp=(float*)p;
    if(!A->chInit){ A->chInit=1; for(int k=0;k<12;k++){ A->ch[k].x=tp[k]; A->ch[k].v=0.f; } }
    for(int k=0;k<12;k++){ springTo(&A->ch[k],tp[k],SK_STF[k],SK_DMP[k],dt); tp[k]=A->ch[k].x; } }

static void skateTick(SetActor*A,float dt,SkPose*out){
    if(A->trickIn>0.f) A->trickIn-=dt;
    if(!A->act&&A->trickIn<=0.f){          /* fire one, then wait a while */
        A->act=ACT_OLLIE; A->actN=3; A->actI=0; A->actT=0.f;
        A->trickIn=3.2f+(float)((int)(A->x*7.f)%40)*0.11f;
        A->deckRoll=0.f; A->deckYaw=0.f; A->air=0.f; }
    if(A->act){
        A->actT+=dt;
        while(A->act&&A->actT>=A->act[A->actI].dur){
            A->actT-=A->act[A->actI].dur; A->actI++;
            if(A->actI>=A->actN){ A->act=NULL; break; } }
        if(A->act){
            /* THE DECK SPINS UNDER THE RIDER. One authored trick pose chain plus a spin
             * on the deck's own axes is what makes a kickflip and a shove-it different
             * moves out of the same body animation. */
            A->deckRoll+=dt*14.0f; A->deckYaw+=dt*3.0f;
            /* ⛔ AND THEY HAVE TO LEAVE THE GROUND. The pose chain alone reads as a
             * shrug -- POP and TUCK tighten the body but nothing MOVES, so the trick
             * is invisible. An ollie is a HOP: a parabola across the whole action,
             * carrying the rider and the deck together so the feet never lose it. */
            { float tot=0.f, don=0.f;
              for(int k=0;k<A->actN;k++){ tot+=A->act[k].dur; if(k<A->actI) don+=A->act[k].dur; }
              float u=(tot>1e-4f)?((don+A->actT)/tot):0.f;
              if(u<0.f)u=0.f;
              if(u>1.f)u=1.f;
              A->air=0.95f*sinf(u*3.14159265f); }
            const SkPose*a=A->act[A->actI].p;
            const SkPose*b=(A->actI+1<A->actN)?A->act[A->actI+1].p:a;
            float u2=A->actT/(A->act[A->actI].dur>1e-4f?A->act[A->actI].dur:1.f);
            u2=u2*u2*(3.f-2.f*u2);        /* smoothstep: a key boundary is not a corner */
            poseLerp(out,a,b,u2);
            skateSpring(A,out,dt);
            return; } }
    A->deckRoll=0.f; A->deckYaw=0.f; A->air=0.f;
    /* a rider at speed is never straight-legged: lean between roll and carve */
    float c=0.5f+0.5f*sinf((float)A->phase*0.035f+A->x*0.13f);
    poseLerp(out,&PZ_ROLL,&PZ_CARVE,0.35f+c*0.65f);
    /* ⛔ AND A RIDER IS CROUCHED. The library's rolling poses are a gentle lean, which
     * on a straight-legged rig reads as someone standing still on a board. Deepen the
     * legs and sink the hips to match -- this is the difference between standing on a
     * skateboard and riding one. */
    out->knee    += 0.55f;      /* a rider's knees are always in it */
    out->thigh   += 0.34f;
    out->hipDrop += 0.16f;
    out->spineFold += 0.22f;    /* and the chest comes over the board, not stacked upright */
    /* ARMS DOWN. They were sitting level and forward -- a zombie reach. A rider's hands
     * hang around hip height with the elbows soft, and only come up to catch balance. */
    out->armSpread *= 0.45f;
    out->armFwd   -= 0.85f;
    out->elbow    += 0.35f;
    skateSpring(A,out,dt); }

static float g_actorDt=0.0166f;
/* labDrawPartner's chain with the hero-relative placement and the face layers removed.
 * Background actors do not talk, and keeping them off the conversation path means the
 * two-body system the film's dialogue runs on is not touched. */

