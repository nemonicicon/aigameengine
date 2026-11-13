/* ================= THE SKATER: PHYSICS, TRICKS, POINTS ==============================
 * 08-21: points and tricks, as an arcade skating game has them. She stands on her board
 * correctly, the board angles with the slope, and gravity acts on it: a test bench for a skating game.
 *
 * The model is a point mass constrained to the park heightfield, carrying its speed along
 * the forward axis of the deck. Gravity is applied as the DOWNHILL COMPONENT OF THE SURFACE
 * TANGENT, and that single term is the whole pumping model: dropping into the bowl she
 * accelerates and climbing the far wall she pays it back, with no special case for either.
 * Launching is not scripted either -- when the surface falls away faster than her path can
 * follow it, she is simply no longer touching it.
 *
 * Units: reva is 3.45 units for a ~1.75m person, so 1 unit is about half a metre and real
 * gravity, 9.81 m/s^2, is ~19.5 units/s^2.
 *
 * Pad:  RT push   A ollie   X kickflip   Y heelflip   B shove-it   LB/RB grab
 *       left stick: carve on the ground, spin in the air
 */
#define SK_G      19.5f     /* 9.81 m/s^2 at this scale */
#define SK_PUSHACC 11.0f   /* renamed: SK_PUSH is a STATE in the tree above */
#define SK_MAXSPD 17.0f
#define SK_FRIC   0.30f
#define SK_OLLIE  6.2f      /* ~1 unit of pop, about half a metre */
#define SK_FLIPT  0.42f     /* seconds for a flip to come all the way round */

enum { TR_NONE=0, TR_KICK, TR_HEEL, TR_SHOVE };

typedef struct {
    float x,y,z;        /* the contact point of the board */
    float spd;          /* speed along the deck */
    float head;         /* yaw */
    float vx,vy,vz;     /* world velocity while airborne */
    int   air;
    float airT;
    float up[3];        /* board up, eased toward the surface normal */
    /* trick state */
    int   trick;
    float trickT;       /* 0..1 through the current flip */
    float flipDeg;      /* board rotation about its long axis  (kick/heel) */
    float shovDeg;      /* board rotation about its own up     (shove-it) */
    float spinDeg;      /* body yaw accumulated this air       (180s) */
    int   grind, grindRail;   /* locked to a rail */
    float grindT, grindDir, grindPts;
    int   manual;             /* rolling on the back wheels */
    int   onFoot;             /* bailed: he is walking, the board is lying where it fell */
    float footT;              /* time since the bail, for the get-up */
    float bX,bY,bZ,bHead;     /* where the board was dropped */
    float gaitPh;             /* walk cycle phase, advanced by DISTANCE so feet never slide */
    float crouch;             /* 0..1, held */
    float footVY; int footAir;/* on-foot jump */
    float manualT;
    int   grabbing;
    float grabT;
    /* scoring */
    long  score;
    long  comboPts;
    int   comboMult;
    char  combo[192];
    char  banner[240];
    float bannerT;
} Skater;
static Skater SK;

static void skComboAdd(const char*name,int pts){
    if(SK.combo[0]) strncat(SK.combo," + ",sizeof(SK.combo)-strlen(SK.combo)-1);
    strncat(SK.combo,name,sizeof(SK.combo)-strlen(SK.combo)-1);
    SK.comboPts += pts;
    SK.comboMult++;
}
static void skStart(int tr){ SK.trick=tr; SK.trickT=0.f; g_skTrickFired=1;
    g_skTrickWhich=(tr==TR_HEEL||tr==TR_SHOVE)?1:0;
    printf("TRICK start: id %d at t=%.2f\n",tr,g_skTime); fflush(stdout); }

/* A flip caught mid-rotation is a bail -- that is the entire risk in a trick, and without
 * it there would be no reason to ever land early. The whole banked combo is lost. */
/* BAILING. Landing mid-flip throws him off; the board stays where it fell and he is on his
 * feet. This is what makes the park feel like a place rather than a rail you are stuck to. */
static void skFall(void){
    SK.onFoot=1; SK.footT=0.f;
    SK.bX=SK.x; SK.bY=SK.y; SK.bZ=SK.z; SK.bHead=SK.head;
    SK.air=0; SK.grind=0; SK.manual=0;
    SK.trick=TR_NONE; SK.flipDeg=0.f; SK.shovDeg=0.f;
    SK.spd*=0.35f;
}
static void skBail(void){
    SK.combo[0]=0; SK.comboPts=0; SK.comboMult=1;
    SK.trick=TR_NONE; SK.flipDeg=0.f; SK.shovDeg=0.f;
    snprintf(SK.banner,sizeof SK.banner,"BAIL"); SK.bannerT=1.4f;
    SK.spd*=0.25f;
}
static void skLandClean(void){
    /* spins are scored on landing, by completed 180s */
    int halves=(int)(SK.spinDeg/165.f);   /* 165 not 180: a hair of forgiveness */
    if(halves>=2){ char nm[32]; snprintf(nm,sizeof nm,"%d",halves*180);
                   skComboAdd(nm,100*halves*(halves-1)); }
    if(SK.comboPts>0){
        long got=SK.comboPts*(SK.comboMult>0?SK.comboMult:1);
        SK.score+=got;
        snprintf(SK.banner,sizeof SK.banner,"%s  %ld x%d",SK.combo,SK.comboPts,SK.comboMult);
        SK.bannerT=2.2f;
    }
    SK.combo[0]=0; SK.comboPts=0; SK.comboMult=1;
    SK.trick=TR_NONE; SK.flipDeg=0.f; SK.shovDeg=0.f; SK.spinDeg=0.f;
}

static void skStep(double dtd){
    float dt=(float)dtd;
    if(dt<=0.f) return;
    if(dt>0.05f) dt=0.05f;              /* a stall must not teleport her through the park */
    g_skTime += dt;
    static int init=0,pA=0,pX=0,pB=0,pY=0;
    if(!init){ init=1; SK.x=0.f; SK.z=6.f; SK.y=parkH(0.f,6.f); SK.head=3.14159f;
               { const char*e=getenv("REVA_ONFOOT"); if(e&&e[0]=='1'){ SK.onFoot=1; SK.bX=SK.x; SK.bZ=SK.z; SK.bY=SK.y; SK.bHead=SK.head; } }
               SK.comboMult=1; SK.up[0]=0.f; SK.up[1]=1.f; SK.up[2]=0.f; }

    /* ---- ON FOOT. He walks the park like a person: the stick steers and drives, the
     * heightfield still carries him, and A near the board puts him back on it. ---- */
    if(SK.onFoot){
        SK.footT+=dt;
        /* REVA_AUTOWALK drives the stick forward, so the gait can be watched without a hand
         * on the pad -- motion is the only way to judge it. */
        float alx=g_lx, aly=g_ly;
        { static int aw=-1; if(aw<0){ const char*e=getenv("REVA_AUTOWALK"); aw=e?atoi(e):0; }
          if(aw==1){ alx=0.f; aly=-0.55f; } else if(aw==2){ alx=0.f; aly=-1.0f; } }
        float mag=hypotf(alx,aly);
        if(mag>1.f) mag=1.f;
        float want=(mag>0.15f)?(mag*9.0f):0.f;          /* walk at ~3, run at ~9 */
        SK.spd += (want-SK.spd)*fminf(1.f,dt*3.5f);
        if(mag>0.15f){
            float tgt=atan2f(alx,-aly);
            float dh=tgt-SK.head;
            while(dh> 3.14159265f) dh-=6.28318531f;
            while(dh<-3.14159265f) dh+=6.28318531f;
            SK.head += dh*fminf(1.f,dt*7.f);
        }
        /* ---- JUMP AND CROUCH ON FOOT. The same verbs the board has, minus the board:
         * crouch loads, release jumps, gravity brings him down, and the landing absorbs. */
        {
            float cw = (g_btnLB||g_btnRB||g_ly>0.55f) ? 1.f : 0.f;
            SK.crouch += (cw-SK.crouch)*fminf(1.f,dt*9.f);
            if(!SK.footAir && g_btnA&&!pA && SK.footT>0.35f
               && hypotf(SK.x-SK.bX,SK.z-SK.bZ)>=2.2f){          /* near the board, A remounts instead */
                SK.footAir=1; SK.footVY=6.0f+SK.crouch*2.6f;      /* a loaded crouch jumps higher */
            }
        }
        if(SK.footAir){
            SK.footVY -= SK_G*dt;
            SK.y += SK.footVY*dt;
            float g2=parkH(SK.x,SK.z);
            if(SK.y<=g2){ SK.y=g2; SK.footAir=0; SK.footVY=0.f; }
        }
        /* the gait advances with GROUND COVERED, not with time -- that is what stops the feet
         * skating, and it makes walk and run one continuous thing rather than two clips */
        { float stride=0.9f+SK.spd*0.06f;
          SK.gaitPh += (SK.spd*dt)/fmaxf(0.35f,stride);
          if(SK.gaitPh>1.f) SK.gaitPh-=floorf(SK.gaitPh); }
        SK.x += sinf(SK.head)*SK.spd*dt;
        SK.z += cosf(SK.head)*SK.spd*dt;
        if(!SK.footAir) SK.y = parkH(SK.x,SK.z);
        { const float PB=PARK_EXT-1.5f;
          if(SK.x<-PB)SK.x=-PB;
          if(SK.x>PB)SK.x=PB;
          if(SK.z<-PB)SK.z=-PB;
          if(SK.z>PB)SK.z=PB; }
        SK.up[0]=0.f; SK.up[1]=1.f; SK.up[2]=0.f;
        /* back on the board */
        /* Remount by walking back to it. Being on foot is a MODE, not a failure state --
         * he can walk, run, jump and crouch out here, so there is no reason to teleport
         * the board to him. */
        if(g_btnA&&!pA && SK.footT>0.35f && hypotf(SK.x-SK.bX,SK.z-SK.bZ)<2.2f){
            SK.onFoot=0; SK.x=SK.bX; SK.z=SK.bZ; SK.y=parkH(SK.bX,SK.bZ); SK.head=SK.bHead;
            SK.spd=1.2f;
        }
        if(SK.bannerT>0.f) SK.bannerT-=dt;
        pA=g_btnA; pX=g_btnX; pB=g_btnB; pY=g_btnY;
        return;
    }

    float f[3]={sinf(SK.head),0.f,cosf(SK.head)};
    float n[3]; parkN(SK.x,SK.z,n);

    /* ---- GRINDING: position is the rail, not the heightfield. Points accrue for as long
     * as she holds it, and the combo only banks when she comes off cleanly -- riding a rail
     * to the end is worth more than tapping it, which is the whole tension of a grind. */
    if(SK.grind){
        Rail*R=&g_rail[SK.grindRail];
        float d[3]={R->b[0]-R->a[0],R->b[1]-R->a[1],R->b[2]-R->a[2]};
        float dl=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(dl<1e-5f) dl=1.f;
        SK.spd -= 0.55f*SK.spd*dt;                       /* rails scrub speed */
        SK.grindT += SK.grindDir*SK.spd*dt/dl;
        SK.grindPts += 55.f*dt;
        SK.head = atan2f(d[0]*SK.grindDir, d[2]*SK.grindDir);
        SK.x=R->a[0]+d[0]*SK.grindT;
        SK.y=R->a[1]+d[1]*SK.grindT + R->r + 0.02f;
        SK.z=R->a[2]+d[2]*SK.grindT;
        SK.up[0]=0.f; SK.up[1]=1.f; SK.up[2]=0.f;
        int off = (SK.grindT<0.f||SK.grindT>1.f||SK.spd<0.6f);
        int pop = (g_btnA&&!pA);
        if(off||pop){
            char nm[48]; snprintf(nm,sizeof nm,"50-50 %s",R->name);
            skComboAdd(nm,(int)SK.grindPts+40);
            SK.grind=0; SK.air=1; SK.airT=0.f; SK.spinDeg=0.f;
            SK.vx=sinf(SK.head)*SK.spd; SK.vz=cosf(SK.head)*SK.spd;
            SK.vy=pop?SK_OLLIE:1.2f;
        }
        if(SK.bannerT>0.f) SK.bannerT-=dt;
        pA=g_btnA; pX=g_btnX; pB=g_btnB; pY=g_btnY;
        return;
    }

    /* ---- steer: carve on the ground, spin in the air ---- */
    if(!SK.air){
        float rate=2.4f*(0.30f+fminf(1.f,fabsf(SK.spd)/9.f));
        SK.head += g_lx*rate*dt;
    } else {
        float sp=g_lx*4.2f*dt;
        SK.head += sp;
        SK.spinDeg += fabsf(sp)*57.29578f;
    }

    if(!SK.air){
        /* the tangent of the surface, along the deck */
        float d=f[0]*n[0]+f[1]*n[1]+f[2]*n[2];
        float tf[3]={f[0]-n[0]*d, f[1]-n[1]*d, f[2]-n[2]*d};
        float tl=sqrtf(tf[0]*tf[0]+tf[1]*tf[1]+tf[2]*tf[2]); if(tl<1e-5f) tl=1.f;
        tf[0]/=tl; tf[1]/=tl; tf[2]/=tl;
        SK.spd += (-SK_G*tf[1])*dt;                 /* THE pumping term */
        if(g_rt>0.15f) SK.spd += SK_PUSHACC*g_rt*dt;   /* right trigger pushes */
        SK.spd -= SK_FRIC*SK.spd*dt;
        if(SK.spd> SK_MAXSPD) SK.spd= SK_MAXSPD;
        if(SK.spd<-SK_MAXSPD) SK.spd=-SK_MAXSPD;

        float nx=SK.x+tf[0]*SK.spd*dt, nz=SK.z+tf[2]*SK.spd*dt, ny=SK.y+tf[1]*SK.spd*dt;
        float surf=parkH(nx,nz);
        if(ny>surf+0.03f){          /* the lip fell away: she is flying, and nothing said so */
            SK.air=1; SK.airT=0.f; SK.spinDeg=0.f;
            SK.vx=tf[0]*SK.spd; SK.vy=tf[1]*SK.spd; SK.vz=tf[2]*SK.spd;
            SK.x=nx; SK.y=ny; SK.z=nz;
        } else { SK.x=nx; SK.z=nz; SK.y=surf; }

        /* ---- A TRICK PRESSED ON THE GROUND POPS AND DOES IT. Requiring an ollie first
         * meant the trick buttons did nothing at all while rolling, which is not how any of
         * this reads to a player: you press kickflip, you kickflip. The pop comes with it. */
        {
            int wantTr = (g_btnX&&!pX) ? TR_KICK : (g_btnY&&!pY) ? TR_HEEL : (g_btnB&&!pB) ? TR_SHOVE : -1;
            { static double atNext=-1; static int atOn=-1;
              if(atOn<0){ const char*e=getenv("REVA_AUTOTRICK"); atOn=(e&&e[0]=='1')?1:0; }
              if(atOn){ if(atNext<0) atNext=2.0; if(g_skTime>atNext){ atNext+=3.0; wantTr=TR_KICK; } } }
            if(wantTr>=0){
                SK.air=1; SK.airT=0.f; SK.spinDeg=0.f;
                SK.vx=tf[0]*SK.spd; SK.vz=tf[2]*SK.spd;
                SK.vy=tf[1]*SK.spd+SK_OLLIE*0.92f;
                SK.y+=0.03f;
                g_skOllieChg=0.f; g_skPopT=1.f;
                skStart(wantTr);
                actPlay(ACT_OLLIE,(int)(sizeof ACT_OLLIE/sizeof ACT_OLLIE[0]));
            }
        }

        /* MANUAL: hold the stick back and she rolls the nose up. It scores slowly but it
         * is the glue between two tricks -- the combo never breaks while it holds. */
        if(g_ly<-0.55f && SK.spd>1.4f){
            if(!SK.manual){ SK.manual=1; SK.manualT=0.f; }
            SK.manualT+=dt;
        } else if(SK.manual){
            SK.manual=0;
            if(SK.manualT>0.35f) skComboAdd("MANUAL",(int)(SK.manualT*90.f));
        }

        /* ---- THE OLLIE IS LOADED, NOT INSTANT. Hold A and she crouches, storing it; let go
         * and she pops, and how deep she got is how high she goes. That is both the genre
         * control and the reason there is something to SEE before she leaves the ground. */
        g_skPushing = (g_rt>0.15f);
        if(g_btnA){
            g_skOllieChg += dt*3.4f;
            if(g_skOllieChg>1.f) g_skOllieChg=1.f;
            SK.spd -= SK.spd*0.35f*dt;          /* crouching scrubs a little speed */
        } else if(g_skOllieChg>0.004f){   /* a TAP has to work: one frame only charges ~0.03 */
            float ch=g_skOllieChg;
            SK.air=1; SK.airT=0.f; SK.spinDeg=0.f;
            SK.vx=tf[0]*SK.spd; SK.vz=tf[2]*SK.spd;
            SK.vy=tf[1]*SK.spd+SK_OLLIE*(0.55f+0.60f*ch);
            SK.y+=0.03f;
            g_skOllieChg=0.f;
            g_skPopT=1.f;
            actPlay(ACT_OLLIE,(int)(sizeof ACT_OLLIE/sizeof ACT_OLLIE[0]));   /* load -> snap -> tuck -> level */
            /* A JUMP IS NOT A TRICK. Firing the flip clip off the ollie button meant every
             * jump was a kickflip and there was no way to simply pop. */
        }
    }

    if(SK.air){
        SK.airT+=dt;
        SK.vy-=SK_G*dt;
        SK.x+=SK.vx*dt; SK.y+=SK.vy*dt; SK.z+=SK.vz*dt;

        if(SK.trick==TR_NONE){
            if     (g_btnX&&!pX) skStart(TR_KICK);
            else if(g_btnY&&!pY) skStart(TR_HEEL);
            else if(g_btnB&&!pB) skStart(TR_SHOVE);
        }
        /* the flip advances whether or not she is still airborne -- a baked rig lands
         * before our physics does, and a deck frozen mid-rotation looks broken */
        if(SK.trick!=TR_NONE){
            SK.trickT += dt/SK_FLIPT;
            float u=SK.trickT>1.f?1.f:SK.trickT;
            if(SK.trick==TR_KICK)  SK.flipDeg=-360.f*u;
            if(SK.trick==TR_HEEL)  SK.flipDeg= 360.f*u;
            if(SK.trick==TR_SHOVE) SK.shovDeg= 360.f*u;
            if(SK.trickT>=1.f){
                if(SK.trick==TR_KICK)  skComboAdd("KICKFLIP",100);
                if(SK.trick==TR_HEEL)  skComboAdd("HEELFLIP",100);
                if(SK.trick==TR_SHOVE) skComboAdd("SHOVE-IT",75);
                SK.trick=TR_NONE; SK.trickT=0.f; SK.flipDeg=0.f; SK.shovDeg=0.f;
            }
        }
        /* grabs pay by the second held, so a big air is worth committing to */
        if(g_btnRB||g_btnLB){
            if(!SK.grabbing){ SK.grabbing=1; SK.grabT=0.f; }
            SK.grabT+=dt;
        } else if(SK.grabbing){
            SK.grabbing=0;
            if(SK.grabT>0.12f) skComboAdd("INDY",(int)(60+SK.grabT*220.f));
        }

        float surf=parkH(SK.x,SK.z);
            /* ---- CATCH A RAIL. Only on the way down, and only from above: dropping onto
             * it locks her to the line. This is why rails are declared as lines rather than
             * inferred from the mesh -- what she locks onto IS what was drawn. */
            if(!SK.grind && SK.vy<0.f){
                int ri; float rt,lat,dy;
                if(railNear(SK.x,SK.y,SK.z,&ri,&rt,&lat,&dy) && lat<0.55f && dy>-0.20f && dy<0.70f){
                    Rail*R=&g_rail[ri];
                    float d[3]={R->b[0]-R->a[0],R->b[1]-R->a[1],R->b[2]-R->a[2]};
                    float dl=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(dl<1e-5f) dl=1.f;
                    float along=(SK.vx*d[0]+SK.vz*d[2])/dl;
                    SK.grind=1; SK.grindRail=ri; SK.grindT=rt;
                    SK.grindDir=(along>=0.f)?1.f:-1.f;
                    SK.grindPts=0.f; SK.air=0; SK.vy=0.f;
                    SK.spd=fabsf(along)<2.0f?2.0f:fabsf(along);
                    SK.trick=TR_NONE; SK.flipDeg=0.f; SK.shovDeg=0.f;
                }
            }

        if(SK.y<=surf){
            SK.y=surf; SK.air=0;
            SK.spd=sqrtf(SK.vx*SK.vx+SK.vz*SK.vz);
            if(SK.grabbing){ SK.grabbing=0; if(SK.grabT>0.12f) skComboAdd("INDY",(int)(60+SK.grabT*220.f)); }
            /* caught mid-flip = bail; otherwise bank it */
            if(SK.trick!=TR_NONE && SK.trickT>0.18f && SK.trickT<0.86f){ skBail(); skFall(); }
            else { SK.trick=TR_NONE; SK.flipDeg=0.f; SK.shovDeg=0.f; skLandClean(); }
        }
    }

    /* keep her inside the park */
    { const float PB=PARK_EXT-1.5f;
      if(SK.x<-PB){SK.x=-PB;SK.spd*=0.4f;} if(SK.x>PB){SK.x=PB;SK.spd*=0.4f;}
      if(SK.z<-PB){SK.z=-PB;SK.spd*=0.4f;} if(SK.z>PB){SK.z=PB;SK.spd*=0.4f;} }

    /* THE UP VECTOR OF THE BOARD: eased onto the surface normal while rolling, back toward
     * world-up in the air. Easing rather than snapping is what stops the deck twitching over
     * the coarse grid, and it is the same vector her body leans against. */
    { float tgt[3];
      if(SK.air){ tgt[0]=0.f; tgt[1]=1.f; tgt[2]=0.f; }
      else parkN(SK.x,SK.z,tgt);
      float k=fminf(1.f,dt*(SK.air?6.f:14.f));
      for(int i2=0;i2<3;i2++) SK.up[i2]+= (tgt[i2]-SK.up[i2])*k;
      float l2=sqrtf(SK.up[0]*SK.up[0]+SK.up[1]*SK.up[1]+SK.up[2]*SK.up[2]);
      if(l2<1e-5f){ SK.up[0]=0.f; SK.up[1]=1.f; SK.up[2]=0.f; }
      else { SK.up[0]/=l2; SK.up[1]/=l2; SK.up[2]/=l2; } }

    if(SK.bannerT>0.f) SK.bannerT-=dt;
    if(SK.air) g_skOllieChg=0.f;
    g_skPopT -= g_skPopT*fminf(1.f,dt*7.0f);
    pA=g_btnA; pX=g_btnX; pB=g_btnB; pY=g_btnY;
}
static float g_showH=0;   /* showcased model's rendered height (set by the draw loop for the director) */
static int g_slotStride=0;   /* sizeof(Slot) from main -- the director's local mirror struct went STALE as Slot grew, so slot indexing read garbage (THE empty-spotlight bug) */
static float g_lastE[3]={0,4,10}, g_lastC[3]={0,1,0};   /* last frame's camera, for fly-in transitions */
static int g_wasTrav=0;   /* travel->dwell handoff flag: reseed the orbit at the follow-cam azimuth */
static int g_slotBan[1024];   /* spotlight quarantine: MEASURED bans only now (auto-quarantine) */
static int g_slotDrawable[1024];
static float g_slotAlt[1024];   /* sky-layer altitude per slot (0 = on the floor) */
static int g_slotNoShow[1024];   /* no spotlights for 'Character NN' generic labels (they still dance) */
static int g_slotHide[1024];   /* SQUID SENTINEL verdicts: degenerate palette caught live -> hidden + named in the log */   /* parade skip for packs that draw nothing (goblin/rabbit bone-heavy) */
static float g_flyE[3], g_flyC[3];
#define RVX g_rvx
#define RVZ g_rvz
typedef struct { int shot; double t,dur; float seed,p1,p2; int tgt; } Camd;
static Camd CAMD={0,0,4,0,9,2.2,0};
static float g_bpm=120.f; static double g_bpmT=0;
static double g_beat=0, g_barT=0; static float g_soc=0;   /* band beat clock + SoC temp for the HUD */
/* the OLD UI's music readout -- genre/chord/key/section from the band */
static char g_genre[20]="", g_chord[28]="", g_key[10]="", g_sect[14]="", g_style[20]="";
static int g_bpb=4;
static void jstr(const char*buf,const char*key,char*out,int cap){
    char pat[32]; snprintf(pat,sizeof pat,"\"%s\"",key);
    const char*p=strstr(buf,pat); out[0]=0; if(!p) return;
    p=strchr(p+strlen(pat),':'); if(!p) return; p++;
    while(*p==' ') p++;
    if(*p!='"') return;
    p++;
    int k=0; while(*p&&*p!='"'&&k<cap-1) out[k++]=*p++;
    out[k]=0; }
static double unix_now(void){ struct timespec ts; clock_gettime(CLOCK_REALTIME,&ts); return ts.tv_sec+ts.tv_nsec/1e9; }
static void bpmPoll(void){
    double now=now_ms(); if(now-g_bpmT<1000) return; g_bpmT=now;
    FILE*f=fopen("/dev/shm/reva_state.json","r");
    if(f){ char buf[8192]; size_t n=fread(buf,1,sizeof buf-1,f); buf[n]=0; fclose(f);
        char*p=strstr(buf,"\"bpm\""); if(p){ p=strchr(p,':'); if(p){ float b=(float)atof(p+1); if(b>40&&b<300) g_bpm=b; } }
        p=strstr(buf,"\"beat\""); if(p){ p=strchr(p,':'); if(p) g_beat=atof(p+1); }
        p=strstr(buf,"\"bar_t\""); if(p){ p=strchr(p,':'); if(p) g_barT=atof(p+1); }
        p=strstr(buf,"\"bpb\""); if(p){ p=strchr(p,':'); if(p){ int b=atoi(p+1); if(b>0&&b<32) g_bpb=b; } }
        jstr(buf,"genre",g_genre,sizeof g_genre); jstr(buf,"chord",g_chord,sizeof g_chord);
        jstr(buf,"key",g_key,sizeof g_key); jstr(buf,"section",g_sect,sizeof g_sect);
        jstr(buf,"style",g_style,sizeof g_style); }
    f=fopen("/sys/class/thermal/thermal_zone0/temp","r");
    if(f){ int t=0; if(fscanf(f,"%d",&t)==1) g_soc=t/1000.f; fclose(f); }
}
static double beatPhase(void){   /* the page's formula: audible-beat-locked, 30ms synth latency */
    /* ⛔ THIS IS THE LAST WALL CLOCK, and it drives the whole floor. unix_now() is the
     * real time of day, so the bar phase -- and with it every lit floor tile and every
     * beat-locked limb -- lands somewhere different on every run. It is why two
     * captures of the same door at the same frame differed by 8-79% of pixels even
     * with the engine clock fixed, the RNG seeded, the sky grounded and the lens
     * pinned: not the crowd, not the dancers, the BEAT. Under REVA_FIXEDSTEP the band
     * is ignored and the PLL free-runs off the virtual clock instead (the tgt<=0 path
     * below), which is deterministic. The show is unaffected -- it never sets it. */
    if(g_fixedStepMs>0.0) return 0;
    if(g_barT>0) return g_beat+(unix_now()-g_barT-0.03)*(g_bpm/60.0);
    return 0; }
/* PLL beat clock (every character visibly reset at once, every 8 seconds):
 * the raw band clock SNAPS when a new bar posts -- every beat-locked limb jumped together.
 * Local phase advances smoothly and SLEWS toward the band; only a wild (>2 beat) error snaps. */
static double g_pll=0; static double g_pllT=0;
static double beatPLL(double tsec){
    double now=now_ms()/1000.0, dt=(g_pllT>0)?(now-g_pllT):0; g_pllT=now;
    if(dt<0||dt>0.5) dt=0;
    g_pll+=dt*(g_bpm/60.0);
    double tgt=beatPhase(); if(tgt<=0) return g_pll>0?g_pll:(g_pll=tsec*(g_bpm/60.0));
    double err=tgt-g_pll;
    if(err>2||err<-2) g_pll=tgt;
    else if(err>0.04||err<-0.04) g_pll+=err*(dt*0.06);   /* deadband + gentle slew (the jerk at each bar change: band bar_t jitter was yanking the whole floor) */
    return g_pll; }
static void hsv(float h,float*r,float*g,float*b){ h=h-floorf(h); float i=floorf(h*6),f=h*6-i;
    float q=1-f; switch(((int)i)%6){ case 0:*r=1;*g=f;*b=0;break; case 1:*r=q;*g=1;*b=0;break;
    case 2:*r=0;*g=1;*b=f;break; case 3:*r=0;*g=q;*b=1;break; case 4:*r=f;*g=0;*b=1;break; default:*r=1;*g=0;*b=q; } }
/* ⛔ (11-02) This divided by RAND_MAX, and THE DICE roll 31 bits on every platform while mingw's RAND_MAX is 32767: since the
 * dice came home on 10-22, every frnd() on WINDOWS was 0..65536, not 0..1, in every door. Linux hid it (glibc's is 31 bits). */
static float frnd(void){ int r=rand(); return (float)r/(g_diceLibc>0?(float)RAND_MAX:2147483648.f); }
