/* ================= THE CONVERSATION SCENE ====================================
 * jacob: "lets get reva and kaeli to the point where they can have a conversation...
 * use tts for each of them AND have subtitles... clearly show their faces".
 *
 * A .scn file (built by scenes/compile_scene.py) carries the timeline; the voices are
 * ONE mixed wav, started once at scene time zero. Nothing is scheduled after that, so
 * picture and sound cannot drift: the subtitle, the camera cut and the jaw all read
 * the same clock the mix was built from.
 *
 * The MOUTH is driven by the line's own loudness envelope at 30Hz, so it opens on the
 * sound it is actually making. Faces have no bones on either body -- the jaw, smile
 * and brow are rpack v3 MORPHS (homecoming H6, authored by skate_src/author/face_morphs.py). */
#define SCN_MAXL 256   /* ⭐ (10-23) 64 -> 256: a long narration is said as its sentences, each a line */
#define SCN_MAXE 2048  /* ⭐ (10-23) 512 -> 2048 (68 s): a narration slot runs to 84 s */
/* THE STAGING, per line (compile_scene.py writes a `stage` record beside every line).
 * A conversation in which both bodies hold one pose for a minute and a half is a radio
 * play with a camera pointed at it. What a director actually gives is: where they
 * stand, which way they are facing, whether they will look at each other, what the
 * lens is doing, and what each body is playing -- so that is what a line carries.
 * SHOT_* is the framing; the values are eased toward, never snapped to, which is what
 * makes `pyaw=46` read as a woman turning her back rather than a body teleporting. */
/* ⭐ SHOT_MEDIUM IS APPENDED, NOT INSERTED (ROBOT NEKO ep 4). The size LADDER runs
 * wide -> medium -> two -> ots -> tight/solo, but the enum's numbers are printed by
 * [scncam] and read by shotgrammar.py's own table, so a new size goes on the END and the
 * ladder lives in the grammar tool. SHOT_N is the count, for anyone sizing a table. */
enum { SHOT_OTS=0, SHOT_TIGHT, SHOT_TWO, SHOT_WIDE, SHOT_SOLO, SHOT_MEDIUM, SHOT_N };
/* A LENS PER SHOT. The club renders at 42 deg vertical -- a wide angle, chosen to hold
 * a whole dancefloor -- and on a conversation it is the wrong glass: faces come out
 * small, the near shoulder in an over-the-shoulder comes out vast beside them, and two
 * people a metre apart sit in a lake of black. These are the focal lengths a scene is
 * actually shot on, and every distance below is derived from the one above it:
 * frame height = 2*D*tan(fov/2), a head is ~0.45 club units, so D = 0.45 / (frac * that). */
/* ⛔ EIGHT LONG, NOT FIVE. Every read of this table is SHOT_FOV[shot&7] -- the mask was
 * there to keep a bad index in range and the table was five entries long, so 5, 6 and 7 read
 * off the end of it. Nothing had ever written a 5 until medium existed.
 * ⭐ SOLO IS 38 NOW (ep 4). It used to be 30 and it used to be a two-shot with the
 * listener's profile filling half the frame; it is the listener's POINT OF VIEW now, taken
 * from her own eye mark, which stands the lens about a pair-gap from the speaker -- and at
 * 30 deg from 1.3 units a head does not fit with head room. 38 is the glass that holds it,
 * and the slight wide-angle is what "nearly into the lens" looks like.
 * MEDIUM is 34: between two's 30 and wide's 40. It is not what sizes the shot (the distance
 * is solved from the hips and the heads), it is what decides how close the lens stands to
 * hold that height -- and closer is what makes a hand reaching toward it loom. */
static const float SHOT_FOV[8]={ 30.f, 22.f, 30.f, 40.f, 38.f, 34.f, 30.f, 30.f };
static const char*SHOT_NAME[8]={ "ots","tight","two","wide","solo","medium","?","?" };
/* From the EYE mark to a clear frame line above the head: half a ~0.45-unit head plus a
 * hand of air. Club units (~1.75x metric), like every other lens number here. */
#define SCN_HEADTOP 0.36f
/* ⭐ AND ABOVE A RAISED HAND (ROBOT NEKO ep 3): the wrist mark, plus the fingers and a margin. */
#define SCN_HANDTOP 0.30f
static float g_scnWant=-1e9f;   /* the frame top a two/wide needs THIS frame; the tripod reads it */
/* ⛔ THE FRAME'S OWN SHAPE WAS NEVER PUBLISHED. A lens that has to GUARANTEE both bodies
 * fit ACROSS needs the aspect ratio, and W/H are locals in main(). Set where the projection
 * is built; 16:9 until the first frame has been through it. */
static float g_aspect=1.7778f;
/* ⭐ reverse= (ROBOT NEKO ep 4): mirror the lens to the OTHER side of the pair's axis.
 * Crossing the line is the gag, so this is not a bug being fixed -- it is one flip of the
 * side sign, which moves the camera, the looking room and the key/rim light together. */
static int g_scnRev=0;
/* ⛔ AND A SHOT SIZE COULD ONLY EVER BE SAID BY A .scn. Every lens in this block is
 * scene-only by construction (it is gated on two published head marks), so a new framing
 * could not be LOOKED AT without authoring and voicing a scene for it -- against this
 * repo's first law. `shotsize=` and `reverse=` from the ctl override the script for every
 * line; `auto` hands it back. -1 = auto. */
static int g_shotForce=-1, g_revForce=-1;
static int scnRevNow(void){ return (g_revForce>=0)?g_revForce:g_scnRev; }
/* ⭐ jawgain= (ep 4): how far the PUPPET jaw opens in `mouth hinge` mode. 1.0 is what
 * every scene shot so far used; ep 4 runs about 0.5. Sticky per line, and a ctl key. */
static float g_jawGain=1.f, g_jawGainCtl=1.f;
typedef struct { int spk; float t0,dur; char text[1536]; int capA,capN; unsigned char env[SCN_MAXE]; int nEnv;   /* ⭐ (10-23) 192 -> 1536, and its caption pages */
                 unsigned char vis[SCN_MAXE]; int nVis;   /* the jaw SHAPE, walked off the text */
                 float pgap,pyaw,pside; int hlook,plook,shot;
                 float hx,hz; int hasMark;   /* the pair's world mark for this line */
                 /* slot 2's blocking. Additive: nothing that exists changes, and they are
                  * STICKY like every other stage key, so a scene sets them once. */
                 float p2gap,p2yaw,p2side,p2brow; int p2look, p2has; char p2clip[32];
                 float p3gap,p3yaw,p3side; int p3has; char p3clip[32];   /* ⭐ ep 7: the fourth body */
                 float scr[7]; int scrHas;                               /* ⭐ ep 8: the screen's transform */
                 float cam[7]; int camHas;                               /* ⭐ ep 8: THE FREE CAMERA. 0 none, 1 place, 2 off */
                 /* ⭐ WHAT THEY ARE HOLDING. Sticky, and `off` clears it -- a woman has to
                  * be able to put a coffee down. */
                 /* ⛔ 64, NOT 32. `v1_town_lantern@0.42@0@lhand@FFD9A0@light:6` is 44
                  * characters: the spec grew a colour in ep 2 and a light in ep 4, and a
                  * 32-byte field truncated the tail SILENTLY -- the light simply never
                  * happened and nothing said why. */
                 char hprop[PROP_SPEC], pprop[PROP_SPEC], p2prop[PROP_SPEC];
                 /* ⭐ THE SECOND HAND (ep 5). Same spec, same parser, same 64 bytes -- the
                  * truncation note above applies word for word. */
                 char hprop2[PROP_SPEC], pprop2[PROP_SPEC], p2prop2[PROP_SPEC];
                 /* ⭐ ON A PROP. A named mark in the set, which overrides the pair
                  * relation for that body -- furniture-relative instead of
                  * person-relative, and the two cannot both be true. */
                 char pmark[32], p2mark[32], p3mark[32];
                 /* ⭐ AND THE HERO ON ONE (09-11). Only the partner slots could take a mark,
                  * so reva could not sit on a bed -- the cold open was blocked on it. */
                 char hmark[32];
                 /* ⭐ ep 10: PATHS. path[slot] = "mark,mark,..." off hpath/ppath/p2path/p3path, rolled AROUND the
                  * set, or `@route`; spd[slot] off hspd/pspd/p2spd/p3spd, u/s, <0 = unset; loop[slot] off
                  * hloop/ploop/p2loop/p3loop, <0 = unset. 512: a planner's 28-mark loop is ~230 chars.
                  * pseg/pnext (filled after the load): the line this path's RUN of identical values starts on,
                  * and the line the next path's run starts on (-1: none -- a line drops the path first). */
                 char path[4][512]; float spd[4]; int loop[4], pseg[4], pnext[4];
                 /* ⭐ ep 11: THE HALF-PIPE. pipe[slot] off hpipe/ppipe/p2pipe/p3pipe ("<name>[,<air>]", "off", or ""
                  * = none in force); pipeAir[slot] off hair/pair/p2air/p3air (the air one-shots, "clip,clip"). */
                 char pipe[4][48], pipeAir[4][128];
                 /* ⭐ ep 10: THE TRACKING LENS. camtrack=slot,dist,height,azim,fov[,aimh] and campan=slot[,aimh];
                  * trkN/panN = how many numbers came, trkHas/panHas 0 none, 1 set, 2 off. */
                 float trk[6], pan[2]; int trkN, trkHas, panN, panHas;
                 float hld[5]; int hldN, hldHas;   /* ⭐ camhold=filmer,subject[,fov[,aimh[,drop]]] */
                 /* ⭐ ep 11: THE MOVING LENSES. cammove=ex,ey,ez,ax,ay,az[,fov] is the END of a move whose START is
                  * the line's cam= (movHas 1); camorbit=slot,radius,height,deg0,deg1[,fov[,aimh]] (orbN numbers,
                  * orbHas 0 none, 1 set, 2 off); secs = camsecs=, either one's travel time (<0: the line's whole
                  * span); camshake=amp[,hz] (shkHas 1 said). A height written `=h` is ABSOLUTE world y (trkAbs for
                  * camtrack, orbAbs for camorbit), not a height above the body. */
                 float mov[7], orb[7], secs, shk[2]; int movHas, orbN, orbHas, orbAbs, trkAbs, shkHas;
                 float camRoll, shut, sepia, grain; char focusSaid[24];   /* (step 25) camroll= shutter= sepia= grain= (under -9000: not said) and focus= as it was written */
                 int silent;            /* ⭐ ep 10: an ACTION line -- nothing said, so no subtitle and no mouth */
                 float hbrow,pbrow;     /* -1..1; -9 = leave it to the reactive brow */
                 float hgt;             /* camera height off the eyeline: -low +high */
                 float step;            /* ⭐ STOP MOTION: exposures/sec from this line on; <0 = unset */
                 float tod, tone;       /* ⭐ time of day and the highlight knee, on the cut; <0 = unset */
                 float feed, fish;      /* ⭐ ep 2: her camera's frame and her camera's lens; <0 = unset */
                 int snap;              /* ⭐ NOT sticky: this line's marks LAND on the cut (a jump cut) */
                 int reverse;           /* ⭐ (ep 4) mirror the lens across the pair's axis; sticky */
                 float jawgain;         /* ⭐ (ep 4) the hinge jaw's opening scale; <0 = unset */
                 char hclip[32],pclip[32];
                 char htrick[96],ptrick[96],p2trick[96],p3trick[96];   /* ⭐ ep 10: clip@sec[,clip@sec] one-shots; NOT sticky */
                 char hpuff[64],ppuff[64],p2puff[64],p3puff[64];       /* ⭐ ep 12: sec[,sec] exhales off the mouth; NOT sticky */
                 char hdoll[16],pdoll[16],p2doll[16],p3doll[16];       /* ⭐ THE RAGDOLL: sec into the line she goes LIMP, or `off`; NOT sticky */
                 char hover[32],pover[32],p2over[32];   /* ⭐ upper-body layers (09-11); sticky, `off` clears */
                 char set[48];          /* ⭐ 10-22: this line plays on ANOTHER set (a cutaway); NOT sticky -- home after */
                 char home[48];         /* ⭐ 10-22: the scene's HOME set from this line on (sticky in the compiler) */
                 char outfit[4][24];    /* ⭐ 10-22: houtfit/poutfit/p2outfit/p3outfit -- dressed on the cut (sticky) */
                 char eyes[4][28];      /* (step 23) heyes/peyes/p2eyes/p3eyes: r,g,b[,amount] or off -- that body's eyes glow from this line on (sticky) */
                 char roll[4][96];      /* ⭐ 10-22: hroll/proll/p2roll/p3roll -- the body's placement turned round an axis */
                 float at[3]; int atHas;   /* ⭐ 10-22: camat=x,y,z: camorbit circles this WORLD point, not a body */
                 /* ⭐ THE REACTION SHOT. Which body the LENS treats as the subject, when
                  * that is not the one talking. -1 = the speaker, as always. Moves the
                  * camera only: the voice, the mouth and the subtitle are untouched. */
                 int focus;
                 /* ⭐ (10-23) WHO THE LINE IS SAID TO: a slot, -1 unsaid. The speaker's head finds them (THE PERFORMANCE
                  * LAYER); NOT sticky -- blocking.py writes it on every speech line it knows the listener of. */
                 int to; } ScnLine;
static int numAt(const char*p){ if(!p) return 0;
    if(*p=='-'||*p=='+') p++;
    return (*p>='0'&&*p<='9')||*p=='.'; }
/* ⭐ ep 11: A LENS KEY'S NUMBERS, comma-separated, into v[] (fields that do not come keep the caller's defaults).
 * Field `absAt` may be written `=h` -- an ABSOLUTE world height -- and *abs says whether it was. sscanf's
 * "%f,%f,..." stops dead at that `=`, which is why camtrack's parse no longer uses it. Returns the count. */
static int lensNums(const char*q,float*v,int maxn,int absAt,int*abs){
    int n=0; if(abs) *abs=0;
    while(q&&n<maxn){
        if(n==absAt&&*q=='='){ q++; if(abs) *abs=1; }
        if(!numAt(q)) break;
        char*e; float f=strtof(q,&e);
        if(e==q) break;
        v[n++]=f; q=e;
        if(*q!=',') break;
        q++; }
    return n; }
static ScnLine g_scnL[SCN_MAXL]; static int g_scnN=0;
/* ⭐ (10-23) CAPTION PAGES: compile_scene.py's `cap t0 t1 text` records (seconds into their line), each line's own
 * run of them (capA, capN) -- see capDraw */
#define SCN_MAXCAP 4096
typedef struct { float t0,t1; char text[208]; } ScnCap;
static ScnCap g_scnCap[SCN_MAXCAP]; static int g_scnCapN=0;
static float g_scnT=-1.f, g_scnLen=0.f;
/* ⭐ THE SCENE CLOCK AS THE BODIES SEE IT. g_scnT has to stay smooth -- it is what the wav
 * was mixed against, and what the subtitles and the cuts run on -- but a jaw or a head
 * drift reading it would move on every frame of a stop-motion take. Latched once per
 * exposure in the main loop; identical to g_scnT whenever stop motion is off. */
static float g_scnTA=-1.f;
static int   g_scnSnap=0;   /* set on a cut whose line says snap=1; the ease block spends it */
static float scnBodyT(void){ return (g_stepFps>0.f&&g_scnTA>=0.f)?g_scnTA:g_scnT; }
static int   g_scnLook[2]={1,1};        /* per body: will she meet the other's eyes */
static int   g_scnShot=SHOT_OTS;
static float g_scnSide=1.f;             /* which side of the eyeline this setup is on */
static float g_scnBrow[2]={-9.f,-9.f};  /* per body, -9 = the reactive brow */
static float g_scnHgt=0.f;              /* ctl hgt=: raise or drop the lens off the eyeline */
static int   g_scnStaged=-1;            /* the line whose clips are already loaded */
static char  g_scnHome[64], g_scnAway[64]; static int g_scnHomeOK=0;   /* ⭐ 10-22: the set a scene is staged on, and the
                                                                       * cutaway (set=) it is on now, "" at home */
static float g_scnHomeSun[4], g_scnHomeSunC[3], g_scnHomeSky[3];
/* WHO IS IN THIS SCENE, for the caption. Speaker indices are POSITIONAL -- 0 is the hero
 * the lab loaded -- so a name cannot be keyed on them, and four of this film's seven
 * scenes are between the two people who are not reva. */
static char  g_scnWho[SCN_MAXCAST][16]={"REVA","KAELI","SUSAN","ARCHIVE","-"};
/* ⭐ EVERY BODY IN A SCENE HAS ITS OWN FACE (10-18, THE WORLD AT THE END OF TIME 2.1: Viktor, Wanda and Amelia). One
 * array served every slot after the hero, read through each slot's own morph table: a third speaker's jaw went to
 * whichever slot the draw loop had touched last, and slots 2-3 never blinked. Row 0 is unused (the hero keeps
 * g_heroMorphW); g_partMorphW is the CURRENT slot's row, so every per-draw use below reads its own body's face. */
static float g_castMorphW[SCN_MAXCAST][MORPH_MAX];
#define g_partMorphW (g_castMorphW[(g_castI>=1&&g_castI<SCN_MAXCAST)?g_castI:1])
static Uint8*g_scnWav=0; static Uint32 g_scnWavLen=0;
static int g_scnVoice=-1;              /* the scene track's voice on the mixer (09-11) */
/* both heads in WORLD, published by whoever draws them: the conversation camera aims
 * at faces, and a guessed head height is exactly the kind of constant this bench keeps
 * catching (measured, per body, every frame). */
static float g_heroHeadW[3]={0,0,0};
static float g_heroMouthW[3]={0,0,0}, g_heroEyeW[3]={0,0,0};
/* the WORLD distance between the two eye marks -- a head's size, measured off the rig
 * every frame, which is what lets coverage match across bodies built to different
 * proportions. IOD_NOM is the value the shot distances above were tuned against. */
static float g_heroIOD=0.f;
#define IOD_NOM 0.151f   /* measured off the rig: kaeli 0.151, reva 0.131 */
/* THE FACE'S OWN FORWARD, from the eye marks -- not the body's heading. A head yaws
 * inside its idle, so a lens pointed along the BODY put kaeli's mouth 20-odd pixels
 * off centre in the mouth panel and every lateral calibration inherited the error
 * (worse: the error changed with the clip, so two passes disagreed). The eye line is
 * anatomy carried by FK, so the cross of it with world up is where the face looks. */
static float g_heroFaceFwd[3]={0,0,1};

static int   g_heroFaceOK=0;
static int g_heroHeadOK=0;
/* ⭐ THE CAST'S MARKS, BY INDEX. Slot 0 is the HERO and is deliberately not in the array:
 * she is drawn by the main path and her marks live in g_heroHeadW. Everyone else is
 * g_cast[i], measured by her own draw. These four are what let the shot solver stop
 * naming names -- with two bodies it could say "hero or partner"; with three it cannot. */
static const float* castHead(int i){ return (i<=0)?g_heroHeadW:g_cast[i].headW; }
static const float* castEye (int i){ return (i<=0)?g_heroEyeW :g_cast[i].eyeW ; }
static int   castHeadOK(int i){ return (i<=0)?g_heroHeadOK:g_cast[i].headOK; }
static int   castFaceOK(int i){ return (i<=0)?g_heroFaceOK:g_cast[i].faceOK; }
static float castIOD   (int i){ return (i<=0)?g_heroIOD   :g_cast[i].iod;    }
static int   g_scnPrevSpk=1;   /* who the speaker is answering: the last person to talk */

/* ================= WHAT THEY ARE CARRYING ======================================
 * ⭐ Every piece of this already existed and was pointed somewhere else: gripNode is the
 * real hand (found by name at load, already carrying weapons in the arena modes),
 * drawProp already draws a prop pack at a matrix 550 times a frame, and the set's loader
 * already turns a stem into a loaded pack. None of the three had ever been introduced to
 * the other two inside a scene.
 *
 * ⛔ THE GRIP IS A POINT, NOT A FRAME. The prop hangs AT the hand with the body's own yaw
 * -- right for a mug, a torch or a hammer at rest, wrong for anything whose orientation
 * has to read down a sightline. A deliberate stop: this film needs things CARRIED. */
#define HELD_MAX 8
typedef struct { char stem[32]; int pk; float ext, loY, hgt; float lo[3], hi[3]; float deckY;
                 /* ⭐ ep 12: the ends of its longest side (mesh space) -- `tip` the one AWAY from the grip (the
                  * origin), where a cigarette burns, `nearP` the one in the fingers (the filter at the lips) --
                  * and its GLOW: the mesh whose name carries `#rrggbb*` (the ember), its colour and centre */
                 float tip[3], nearP[3]; int glow; float glowC[3], glowP[3]; } HeldProp;   /* deckY (ep 10): the highest point over the middle 40% of the long axis -- a board's flat deck top, not its kick tips */   /* ext: the mesh's longest side, measured; lo/hi its box (ep 10: the board's fit) */
static HeldProp g_held[HELD_MAX]; static int g_nHeld=0;
static int heldFind(const char*stem);   /* ⭐ ep 12: the light collector asks it for the ember (defined below) */
static const HeldProp*heldPropOf(int pk){ for(int h=0;h<g_nHeld;h++) if(g_held[h].pk==pk&&pk>=0) return &g_held[h]; return NULL; }
/* ⭐ ep 12: WHERE EACH HELD PROP WAS DRAWN -- the matrix drawHeldOne built, per body per hand, and the
 * frame it was built on. The wisp leaves from here and the ember's light sits here: one matrix, so the
 * smoke cannot come off a different place than the cigarette is drawn. */
static float    g_heldM[SCN_MAXCAST][2][16];
static unsigned g_heldMSt[SCN_MAXCAST][2];
static int      g_heldMPk[SCN_MAXCAST][2];
/* ⭐ ep 12: THE DRAG. 0..1 per body per hand, eased on the exposure clock while the thing in that hand is
 * at the mouth (smokeTick); the ember's light and glow run at 1 + 2 x its ease -- about 3x on a pull. */
static float    g_heldDrag[SCN_MAXCAST][2];
static float heldDragK(int i,int which){
    if(i<0||i>=SCN_MAXCAST||which<0||which>1) return 1.f;
    float u=g_heldDrag[i][which]; if(u<0.f) u=0.f; if(u>1.f) u=1.f;
    return 1.f+2.f*u*u*(3.f-2.f*u); }
/* ⛔ 64, WITH THE ScnLine FIELD. `v1_town_lantern@0.42@0@lhand@FFD9A0@light:6` is 42
 * characters -- the spec grew a colour in ep 2 and a light in ep 4. */
static char  g_castProp[SCN_MAXCAST][PROP_SPEC];        /* what each body is holding, by slot */
/* ⭐ AND THE OTHER HAND (ROBOT NEKO ep 5). A body could hold exactly ONE thing, so a
 * torch and a mug were mutually exclusive and a character who needed both had to put one
 * down between shots. A SECOND slot per body, parsed by the same heldParse, drawn by the
 * same drawHeldOne and lit by the same heldLightOne -- `hprop2=`/`pprop2=`/`p2prop2=`.
 * ⛔ NOT a second parser and NOT a second draw path: ep 4's note above says why. */
static char  g_castProp2[SCN_MAXCAST][PROP_SPEC];       /* ...and what is in the other hand */
static char  g_castMark[SCN_MAXCAST][32];        /* ...and which set mark she is ON, if any */
static SetMark*g_heroMk=NULL;                    /* slot 0's resolved mark this frame (09-11), or NULL */
/* ⭐ ep 9: WHERE EACH BODY WAS LAST DRAWN (world x, z, facing in radians) -- a walk to a new
 * mark starts from here -- and the floor height its placement used, for a board under it. */
static float g_castAt[SCN_MAXCAST][3]; static int g_castAtOK[SCN_MAXCAST];
static float g_castFloorY[SCN_MAXCAST];
static float g_castGripW[SCN_MAXCAST][3];        /* their grips, measured, in world space */
/* ⭐ AND THE WHOLE FRAME, not just the point. The rotation fell out of the same FK and was
 * being thrown away one line later -- so a torch could not point and a hammer could not
 * swing. With the basis, a held prop inherits the hand: it turns with the wrist and
 * follows any clip with nothing authored. */
static float g_castGripM[SCN_MAXCAST][16];
/* ⭐ AND THREE MORE PLACES A PROP CAN HANG FROM. Published from the same FK as the grip.
 * FOUR, not arbitrary: copying every bone's world matrix per body per frame to make one
 * addressable is a lot of work for a feature whose whole use is "on a person somewhere
 * obvious", and these are the four things actually hang off. A fifth is one line. */
enum { ATT_HAND=0, ATT_HEAD, ATT_CHEST, ATT_HIP, ATT_LHAND, ATT_DECK, ATT_MARK, ATT_BOARD, ATT_BONE, ATT_BONE2, ATT_N };   /* ⭐ ep 9: DECK under a rider, MARK on the set; ep 10: BOARD off the clip's board channel */
static float g_castAttM[SCN_MAXCAST][ATT_N][16];
static int   g_castAttOK[SCN_MAXCAST][ATT_N];
/* ⭐ ep 12: A PROP ON ANY BONE. `@hand` is the wrist on a UE rig (anima's gripNode is hand_r), and a
 * cigarette is held between two FINGERS -- 0.21 u from where the wrist is, which is the whole distance
 * between a drag and a hand across her face. So `where` may name a bone of the body holding the thing
 * (`@middle_01_r`): heldAttach asks for it here, attPublish finds it on whatever pack that slot is
 * drawing and publishes its world frame as ATT_BONE (prop) / ATT_BONE2 (prop2). One frame late, like
 * every other attach point. */
static char  g_castBoneReq[SCN_MAXCAST][2][32];
static int   g_castBoneNd[SCN_MAXCAST][2];
static const Pack*g_castBonePk[SCN_MAXCAST][2];
static char  g_castBoneHas[SCN_MAXCAST][2][32];   /* the name the cached node was resolved for */
static int attOf(const char*nm){
    if(!nm||!nm[0]) return ATT_HAND;
    if(!strcmp(nm,"head"))  return ATT_HEAD;
    if(!strcmp(nm,"chest")) return ATT_CHEST;
    if(!strcmp(nm,"hip"))   return ATT_HIP;
    if(!strcmp(nm,"lhand")) return ATT_LHAND;
    if(!strcmp(nm,"deck"))  return ATT_DECK;
    if(!strcmp(nm,"board")) return ATT_BOARD;   /* ⭐ ep 10: published by boardPublish, never by attPublish */
    return ATT_HAND; }
/* publish all four for whichever body is being drawn */
static void attPublish(Pack*P9,M4*W9,const float*model,int slot){
    int chest=-1; for(int c=3;c>=1;c--) if(P9->pb[c]>=0){ chest=P9->pb[c]; break; }
    /* ⭐ AND THE OTHER HAND (ROBOT NEKO ep 2). Every held prop rode the RIGHT hand, and on a
     * pair staged so the lens sits on their left, that hand is on the far side of the body:
     * reva's coffee was drawn every frame at (33.93,1.65,-18.38) and hidden behind her for the
     * whole take. gripL was already elected by the same code as gripNode -- it only needed
     * publishing. `@lhand` in a prop string; the forearm if a rig has no left hand. */
    int nodes[ATT_N]={ (P9->gripNode>=0)?P9->gripNode:P9->pb[11], P9->pb[5], chest, P9->pb[0],
                       (P9->gripL>0)?P9->gripL:P9->pb[8] };
    for(int k=0;k<=ATT_LHAND;k++){   /* the bone-hung points; DECK and MARK are built below */
        g_castAttOK[slot][k]=0;
        if(nodes[k]<0) continue;
        m4_mul(g_castAttM[slot][k],(float*)model,W9[nodes[k]]);
        g_castAttOK[slot][k]=1; }
    for(int w9=0;w9<2;w9++){   /* ⭐ ep 12: a bone a held prop asked for by name */
        g_castAttOK[slot][ATT_BONE+w9]=0;
        const char*rq9=g_castBoneReq[slot][w9]; if(!rq9[0]) continue;
        if(g_castBonePk[slot][w9]!=P9||strcmp(g_castBoneHas[slot][w9],rq9)){
            g_castBonePk[slot][w9]=P9; snprintf(g_castBoneHas[slot][w9],32,"%s",rq9); g_castBoneNd[slot][w9]=-1;
            for(unsigned n9=0;n9<P9->nNodes;n9++){ const char*nm9=packStr(P9,P9->nodes[n9].name);
                int eq9=1; for(int c9=0;;c9++){ char a9=nm9[c9], b9=rq9[c9];
                    if(a9>='A'&&a9<='Z') a9+=32;
                    if(b9>='A'&&b9<='Z') b9+=32;
                    if(a9!=b9){ eq9=0; break; } if(!a9) break; }
                if(eq9){ g_castBoneNd[slot][w9]=(int)n9; break; } }
            printf("[held] slot %d%s: '%s' %s on %s%c",slot,w9?" prop2":"",rq9,
                   g_castBoneNd[slot][w9]>=0?"is a BONE -- the prop rides it":"is NOT a bone (nor a body point, nor a set mark)",P9->label,10);
            fflush(stdout); }
        int nd9=g_castBoneNd[slot][w9]; if(nd9<0) continue;
        m4_mul(g_castAttM[slot][ATT_BONE+w9],(float*)model,W9[nd9]);
        g_castAttOK[slot][ATT_BONE+w9]=1; }
    /* ⭐ THE DECK (ROBOT NEKO ep 9): a board under a rider. NOT hung off the feet -- a push lifts
     * one of them, and a board that followed their midpoint would climb the pushing leg. It is
     * the body's own ROOT on the floor its placement used, its long axis ACROSS the body (a
     * skater stands sideways on the deck), and the authored stance puts both feet over it. */
    { float lx=model[0], lz=model[2]; float L9=sqrtf(lx*lx+lz*lz);
      if(L9>1e-6f){ lx/=L9; lz/=L9; } else { lx=1.f; lz=0.f; }
      float*D=g_castAttM[slot][ATT_DECK]; memset(D,0,64);
      D[0]=lz;  D[2]=-lx;        /* X = Y x Z                                */
      D[5]=1.f;                  /* Y: up                                    */
      D[8]=lx;  D[10]=lz;        /* Z: the board's long axis, across the body */
      D[12]=model[12]; D[13]=g_castFloorY[slot]; D[14]=model[14]; D[15]=1.f;
      g_castAttOK[slot][ATT_DECK]=1; }
    g_castAttOK[slot][ATT_MARK]=0; }
static int   g_castGripOK[SCN_MAXCAST];

/* ⭐ A HELD PROP THAT IS A LIGHT (ROBOT NEKO ep 4) --------------------------------------
 *     stem@size@pitch@where@RRGGBB@light        (or @light:6 for the power)
 * The sixth field turns the thing in her hand into a PRACTICAL: a point light at the exact
 * world position drawHeld puts the mesh, coloured by the prop's own tint, lighting the
 * bodies, the set, the actors and the faces like any other practical -- and the prop draws
 * EMISSIVE, so it reads as the source instead of as a dark lump held by someone lit by it.
 * (ep 2's lesson, one rung up: a prop the audience must watch gets a colour. A prop the
 * audience must watch AT NIGHT has to make its own.)
 *
 * ⛔ THE SHADER HAS SIX PRACTICALS AND A SET COULD CLAIM ALL SIX. A set may now take
 * NLIGHT-1 of them and held lights fill DOWNWARD from the top, never below what the set
 * claimed -- so there is always at least one, and one that will not fit says so.
 * (NLIGHT has been 6 since Z6; the `[set] light cap is 3` message had been stale since.)
 *
 * ONE PARSER. drawHeld used to pick the string apart inline; the light needs the same
 * fields, and two parsers of one string is how a light ends up in the other hand. */
#define HELD_LIGHT_POW 5.f
/* ⭐ ep 12: AND IT CAN SMOKE. `@smoke` (or `@smoke:<rate>`, 1 = a cigarette) makes the prop's FAR TIP
 * -- the end of its longest side away from the grip -- give off a wisp (smokeTick). The fields after
 * the colour are KEYWORDS, in either order: `@light:0.5@smoke` and `@smoke@light:0.5` are one prop. */
static void specTooLong(const char*v){
    printf("[scene] ⛔ a prop spec is longer than PROP_SPEC (%d) and was CUT to '%s' -- the fields past the cut are lost\n",PROP_SPEC,v);
    fflush(stdout); }
typedef struct { char stem[32]; float size, pitch; char where[32]; float tint[3]; float lpow; float smoke; float lreach; float along; } HeldSpec;
static int heldParse(const char*s,HeldSpec*H){
    H->stem[0]=0; H->size=0.30f; H->pitch=0.f; H->where[0]=0; H->lpow=0.f; H->smoke=0.f; H->lreach=0.f; H->along=-1.f;
    H->tint[0]=0.82f; H->tint[1]=0.80f; H->tint[2]=0.76f;   /* the old grey, when no colour is named */
    if(!s||!s[0]||!strncmp(s,"off",3)) return 0;
    const char*p=s; int fi=0;
    for(;;){
        const char*e=strchr(p,'@');
        int n=e?(int)(e-p):(int)strlen(p);
        char f[64]; if(n>63) n=63; memcpy(f,p,n); f[n]=0;
        switch(fi){
            case 0: snprintf(H->stem,32,"%.31s",f); break;
            case 1: H->size=(float)atof(f); break;
            case 2: H->pitch=(float)atof(f)*0.0174533f; break;
            /* ⭐ ep 12: `where:0.3` -- the hand holds the prop 30% of the way from its grip end to its far tip,
             * instead of by the end. A cigarette is held a third of the way along with the filter out on the palm
             * side; held by its end, the filter sat IN the knuckle and the drag put her knuckles on her face. */
            case 3: { char*c9=strchr(f,':'); if(c9){ *c9=0; H->along=(float)atof(c9+1);
                          if(H->along<0.f) H->along=0.f;
                          if(H->along>1.f) H->along=1.f; }
                      snprintf(H->where,32,"%.31s",f); } break;
            case 4: { unsigned hx9=0;
                      if(sscanf(f,"%x",&hx9)==1){ H->tint[0]=((hx9>>16)&255)/255.f;
                          H->tint[1]=((hx9>>8)&255)/255.f; H->tint[2]=(hx9&255)/255.f; } } break;
            default: if(!strncmp(f,"light",5)){ H->lpow=(f[5]==':')?(float)atof(f+6):HELD_LIGHT_POW;
                        if(H->lpow<0.f) H->lpow=0.f;
                        { const char*r9=(f[5]==':')?strchr(f+6,':'):NULL; H->lreach=r9?(float)atof(r9+1):0.f; } }   /* ⭐ ep 12: light:<power>:<reach> */
                    else if(!strncmp(f,"smoke",5)){ H->smoke=(f[5]==':')?(float)atof(f+6):1.f;   /* ⭐ ep 12 */
                        if(H->smoke<0.f) H->smoke=0.f; }
                    else if(f[0]){ static char said9[PROP_SPEC]={0};
                        if(strcmp(said9,s)){ snprintf(said9,PROP_SPEC,"%s",s);
                            printf("[held] '%s': field %d '%s' is not understood -- after the colour it is `light[:<power>[:<reach>]]` or `smoke[:<rate>]`%c",s,fi+1,f,10); fflush(stdout); } }
                    break; }
        if(!e) break;
        p=e+1; fi++;
        if(fi>7) break; }
    if(H->size<0.01f) H->size=0.30f;
    return H->stem[0]?1:0; }
static int heldAny(void){
    for(int i=0;i<SCN_MAXCAST;i++){
        if(g_castProp [i][0]&&strncmp(g_castProp [i],"off",3)) return 1;
        if(g_castProp2[i][0]&&strncmp(g_castProp2[i],"off",3)) return 1; }
    return 0; }
/* ⭐ WHERE THE FIRST SLOT'S PROP ACTUALLY LANDED -- resolved exactly the way it is drawn,
 * and deliberately NOT recursive: it is what slot 2 measures itself against. -1 = nothing
 * held, or nowhere to hang it. */
static int heldAtt1(int i){
    HeldSpec H; if(!heldParse(g_castProp[i],&H)) return -1;
    int a=attOf(H.where);
    if(!g_castAttOK[i][a]) a=ATT_HAND;
    if(!g_castAttOK[i][a]) return -1;
    return a; }
/* ⭐ ONE ATTACH RESOLVER for both slots and for both the mesh and the light -- the same
 * reason there is ONE parser: a torch whose light and whose mesh disagreed about which
 * hand it was in is the exact bug ep 4's comment warns about, and a second slot doubles
 * the chances of it.
 * ⛔ AN UNSPECIFIED SECOND PROP MUST NOT LAND IN THE FIRST ONE'S FIST. attOf("") is
 * ATT_HAND, so a script that said `hprop2=` and no `@where` would have drawn both things
 * at the same matrix, interpenetrating, and nothing would have said why. Slot 2 with no
 * `where` takes THE OTHER HAND from whatever slot 1 resolved to.
 * If the script names the same point for both ON PURPOSE it is obeyed -- the director's
 * word beats a guess -- but it is said out loud once. */
static int heldAttach(int i,const HeldSpec*H,int which,int*collide){
    if(collide) *collide=0;
    int a1=(which==1)?heldAtt1(i):-1;
    int att;
    if(H->where[0])   att=attOf(H->where);              /* the script said where: obey it */
    else if(which==0) att=ATT_HAND;                     /* the old default, unchanged */
    else              att=(a1==ATT_HAND)?ATT_LHAND:ATT_HAND;
    /* ⭐ A PROP CAN REST ON A MARK (ROBOT NEKO ep 9). A `where` that names no part of a body is
     * looked up as a SET MARK, and the thing lies there -- at the mark's height, turned to its
     * yaw. It is how a skateboard is on the ground until someone bends down and picks it up:
     * `@m_board` on one line, `@hand` on the next, cut on the action. */
    if(H->where[0]&&att==ATT_HAND&&strcmp(H->where,"hand")){
        SetMark*MK9=markFind(H->where);
        if(MK9){ float a9=MK9->yaw*0.0174533f, c9=cosf(a9), s9=sinf(a9);
            float*D=g_castAttM[i][ATT_MARK]; memset(D,0,64);
            D[0]=c9; D[2]=-s9; D[5]=1.f; D[8]=s9; D[10]=c9;
            D[12]=MK9->x; D[13]=MK9->y; D[14]=MK9->z; D[15]=1.f;
            g_castAttOK[i][ATT_MARK]=1; att=ATT_MARK; }
        else if(i>=0&&i<SCN_MAXCAST&&which>=0&&which<2){
            /* ⭐ ep 12: not a mark -- ask the body for a BONE of that name (attPublish answers, and says so once) */
            if(strcmp(g_castBoneReq[i][which],H->where)) snprintf(g_castBoneReq[i][which],32,"%s",H->where);
            if(g_castAttOK[i][ATT_BONE+which]) return ATT_BONE+which;
            if(g_castBonePk[i][which]&&g_castBoneNd[i][which]<0&&!strcmp(g_castBoneHas[i][which],H->where)){
                static char miss9[32];
                if(strcmp(miss9,H->where)){ snprintf(miss9,32,"%s",H->where);
                    printf("[held] where '%s' is neither a body point, a mark in the loaded set, nor a bone of that body -- the hand\n",H->where); fflush(stdout); } }
            else return -1; } }
    if(!g_castAttOK[i][att]){
        /* the preferred point is not on this rig: for slot 2 try the other hand, then the
         * ATT_HAND fallback slot 1 has always used. */
        int oth=(att==ATT_HAND)?ATT_LHAND:ATT_HAND;
        if(which==1&&g_castAttOK[i][oth]) att=oth;
        else att=ATT_HAND; }
    if(!g_castAttOK[i][att]) return -1;
    if(which==1&&a1>=0&&att==a1&&collide) *collide=1;
    return att; }
/* The held lights, recomputed once per frame off the attach points the body draws publish.
 * (One frame old, like the props themselves -- drawHeld runs before the hero is drawn.)
 * ⛔ TWO PER BODY NOW. This array was SCN_MAXCAST long because a body could only hold one
 * thing; the second slot doubles what the loop can collect, and a cap that does not move
 * with the thing feeding it is precisely how ep 4 shipped a set that uploaded 3 of its
 * lamps. It is a guard as well as a size -- the collector refuses past it, loudly. */
#define HELD_LIGHT_MAX (SCN_MAXCAST*2)
static float g_hlP[HELD_LIGHT_MAX][3], g_hlC[HELD_LIGHT_MAX][3];
static float g_hlK[HELD_LIGHT_MAX];      /* ⭐ ep 12: each held light's reach, as extra falloff (lightReachK) */
static int   g_hlEmber[HELD_LIGHT_MAX];   /* ⭐ ep 12: this held light is a smoking prop's coal -- the smoke takes it up close */
static int   g_nHL=0;
/* ONE COLLECTOR, called once per slot per body. Inlining this twice is how the two hands
 * drift apart one fix at a time. */
static void heldLightOne(int i,const char*spec,int which,char*said){
    HeldSpec H; if(!heldParse(spec,&H)) return;
    if(H.lpow<=0.f) return;
    const char*w9=which?" prop2":"";
    int tell9=strcmp(said,spec)!=0;
    if(tell9) snprintf(said,PROP_SPEC,"%s",spec);
    int att9=heldAttach(i,&H,which,NULL);
    if(att9<0){
        /* ⛔ A LIGHT THAT CANNOT BE PLACED FAILS LIKE ANY OTHER HELD REFUSAL. */
        if(tell9){ printf("[held] slot %d%s '%s' is a light but REFUSED: no attach point published (that body never published a grip)%c",i,w9,H.stem,10); fflush(stdout); }
        return; }
    if(g_nHL>=HELD_LIGHT_MAX){ static int said9=0;
        if(!said9){ said9=1;
            printf("[held] more than %d held lights in one frame -- the rest stay dark (HELD_LIGHT_MAX)%c",HELD_LIGHT_MAX,10); fflush(stdout); }
        return; }
    /* ⛔ A HELD PRACTICAL IS MEASURED AGAINST THE SKY IT WILL BE SEEN UNDER, like a set's
     * lamps. The falloff is 1/(1+dd*0.055): HALF BRIGHTNESS AT 4.26 CLUB UNITS and no range
     * at all, so a power that looks right on a face at arm's length is a floodlight on a
     * house nine units away. ROBOT NEKO ep 5 wrote `light:6` for a TORCH and it lit an entire
     * clapboard wall to white -- at tod=21.5 the sky ambient is 0.038 and that torch is ~80x
     * it. `design/the_set.md` has said "a lamp you are standing next to is about 1.5, not 6"
     * since Z6; nothing ever checked, so both light-authoring surfaces ignored it. */
    if(tell9){ float amb9=(g_sky[0]+g_sky[1]+g_sky[2])/3.f; if(amb9<1e-4f) amb9=1e-4f;
        float ratio9=(H.lpow*0.53f)/amb9;
        printf("[held] slot %d%s '%s' is a light (power %.1f -- %.0fx the sky ambient %.3f at 4u)%c",
               i,w9,H.stem,H.lpow,ratio9,amb9,10);
        if(ratio9>8.f) printf("[held]   WARNING: that will SWAMP this sky -- the frame will read "
                              "as daylight however dark sun=/tod= is set. A sane practical is ~1.5.%c",10);
        fflush(stdout); }
    /* ⭐ ep 12: A PROP WITH A GLOW MESH (the ember) LIGHTS FROM THE GLOW, IN THE GLOW'S COLOUR. A lit
     * cigarette in F2EFE8 paper was a white lamp in her fist; the light is the coal at its tip, orange,
     * where drawHeldOne last put it. And a smoking prop's light rises with the DRAG (heldDragK). */
    float lp9[3], lc9[3]={H.tint[0],H.tint[1],H.tint[2]}, pw9=H.lpow;
    memcpy(lp9,g_castAttM[i][att9]+12,12);
    { int pk9=heldFind(H.stem); const HeldProp*HP9=heldPropOf(pk9);
      if(HP9&&HP9->glow&&g_heldMSt[i][which]&&g_heldMPk[i][which]==pk9&&g_heldMSt[i][which]+2u>=g_frameStamp){
          const float*M9=g_heldM[i][which],*q9=HP9->glowP;
          for(int a=0;a<3;a++) lp9[a]=M9[a]*q9[0]+M9[4+a]*q9[1]+M9[8+a]*q9[2]+M9[12+a];
          memcpy(lc9,HP9->glowC,12); }
      if(H.smoke>0.f) pw9*=heldDragK(i,which); }
    memcpy(g_hlP[g_nHL],lp9,12);
    g_hlEmber[g_nHL]=(H.smoke>0.f);
    g_hlK[g_nHL]=lightReachK(H.lreach);
    g_hlC[g_nHL][0]=lc9[0]*pw9;
    g_hlC[g_nHL][1]=lc9[1]*pw9;
    g_hlC[g_nHL][2]=lc9[2]*pw9;
    g_nHL++; }
static void heldLightsTick(void){
    static unsigned stamp9=0xffffffffu;
    static char saidL9[SCN_MAXCAST][2][PROP_SPEC];
    if(stamp9==g_frameStamp) return;
    stamp9=g_frameStamp;
    g_nHL=0;
    for(int i=0;i<SCN_MAXCAST;i++){
        heldLightOne(i,g_castProp [i],0,saidL9[i][0]);
        heldLightOne(i,g_castProp2[i],1,saidL9[i][1]); } }
/* Merge the held lights into an NLIGHT-long practical array on its way to the shader.
 * `used` is how many slots the caller filled from 0 up; the held ones fill from the top
 * down and stop at `used`, loudly. Every site that uploads lp/lc calls this, which is what
 * makes a held light light EVERYTHING rather than only whatever was patched last. */
static float g_stampK[NLIGHT];   /* ⭐ ep 12: the reach (extra falloff) of each slot lightsStamp just filled; 0 = the club's */
static void lightsStamp(float*lp,float*lc,int used){
    memset(g_stampK,0,sizeof g_stampK);
    heldLightsTick();
    if(g_nHL<=0) return;
    if(used<0) used=0;
    int slot=NLIGHT-1;
    for(int i=0;i<g_nHL;i++,slot--){
        if(slot<used){ static int said9=0;
            if(!said9){ said9=1;
                printf("[held] %d held light(s) but only %d free practical slot(s) -- the caller had filled %d of %d; the rest stay dark%c",
                       g_nHL,NLIGHT-used,used,NLIGHT,10); fflush(stdout); }
            break; }
        memcpy(lp+slot*3,g_hlP[i],12); memcpy(lc+slot*3,g_hlC[i],12); g_stampK[slot]=g_hlK[i]; } }

/* ⛔ RESOLVE ONCE. A miss is remembered AS a miss, so a typo costs one failed fopen
 * instead of sixty a second. Returns an index into g_setPk, or -1. */
static int heldFind(const char*stem){
    if(!stem||!stem[0]||!strcmp(stem,"off")) return -1;
    for(int i=0;i<g_nHeld;i++) if(!strcmp(g_held[i].stem,stem)) return g_held[i].pk;
    if(g_nHeld>=HELD_MAX||g_setNP>=SET_MAXP){ static int n9=0;
        if(n9++<3){ printf("[prop] held '%s' REFUSED: %s is full%c",stem,g_nHeld>=HELD_MAX?"HELD_MAX":"SET_MAXP",10); fflush(stdout); }
        return -1; }
    char pp[512]; snprintf(pp,sizeof pp,"props/%s.rpack",stem);
    FILE*f=fopen(pp,"rb");
    int idx=-1;
    float ext9=0.f, loY9=0.f, hiY9=0.f, lo9[3]={0,0,0}, hi9[3]={0,0,0}, deckY9=-1e9f;
    if(f){ fclose(f); g_setPk[g_setNP]=loadPack(pp); idx=g_setNP++;
           g_setLod[idx][0]=g_setLod[idx][1]=g_setLod[idx][2]=-1;   /* (11-05) this place doesn't come through setPack either: its row says "no lighter copies", and not whatever was there (engine/stream.inc has what that cost) */
           /* ⭐ HOW BIG IS IT -- off EVERY mesh's own vertices (a prop is one node at identity, so
            * its buffer IS its rest shape). This is what makes a held size a WORLD size. */
           { Pack*HP=&g_setPk[idx]; float lo[3]={1e9f,1e9f,1e9f}, hi[3]={-1e9f,-1e9f,-1e9f};
             for(unsigned mi=0;mi<HP->nMeshes;mi++){ PMesh*M9=&HP->meshes[mi]; float*vp=(float*)(HP->pk+M9->offPos);
                 for(unsigned v=0;v<M9->verts;v++) for(int k=0;k<3;k++){ float c=vp[v*3+k];
                     if(c<lo[k]) lo[k]=c;
                     if(c>hi[k]) hi[k]=c; } }
             for(int k=0;k<3;k++) if(hi[k]-lo[k]>ext9) ext9=hi[k]-lo[k];
             loY9=lo[1]; hiY9=hi[1]; memcpy(lo9,lo,12); memcpy(hi9,hi,12);
             /* ⭐ ep 10: a board's DECK TOP is the highest point over the MIDDLE of its long axis -- its kicks rise past it */
             { int lx9=(hi[0]-lo[0])>=(hi[2]-lo[2]); float c9=lx9?0.5f*(lo[0]+hi[0]):0.5f*(lo[2]+hi[2]);
               float w9=0.2f*(lx9?(hi[0]-lo[0]):(hi[2]-lo[2]));
               /* ...and over the middle of its WIDTH: a deck is CONCAVE, its rails stand ~0.01 u above where a foot rests */
               float cc9=lx9?0.5f*(lo[2]+hi[2]):0.5f*(lo[0]+hi[0]), cw9=0.25f*(lx9?(hi[2]-lo[2]):(hi[0]-lo[0]));
               for(unsigned mi=0;mi<HP->nMeshes;mi++){ PMesh*M9=&HP->meshes[mi]; float*vp=(float*)(HP->pk+M9->offPos);
                   for(unsigned v=0;v<M9->verts;v++){ float a9=vp[v*3+(lx9?0:2)], b9=vp[v*3+(lx9?2:0)];
                       if(fabsf(a9-c9)<=w9&&fabsf(b9-cc9)<=cw9&&vp[v*3+1]>deckY9) deckY9=vp[v*3+1]; } } } }
           printf("[prop] held '%s' loaded as pack %d (%u meshes), longest side %.3f%c",stem,idx,g_setPk[idx].nMeshes,ext9,10); }
    else printf("[prop] held '%s' NOT FOUND (props/%s.rpack)%c",stem,stem,10);
    /* ⭐ ep 12: THE TWO ENDS AND THE GLOW. The far tip is the end of the longest side farther from the
     * origin (the grip); a mesh named `...#rrggbb*` is the part that glows -- its vertex centroid is where
     * the ember is, and its colour is the light's. */
    { HeldProp*HQ=&g_held[g_nHeld]; float c9[3]; int ax9=0;
      for(int k=0;k<3;k++) c9[k]=0.5f*(lo9[k]+hi9[k]);
      for(int k=1;k<3;k++) if(hi9[k]-lo9[k]>hi9[ax9]-lo9[ax9]) ax9=k;
      memcpy(HQ->tip,c9,12); memcpy(HQ->nearP,c9,12);
      int far9=(fabsf(hi9[ax9])>=fabsf(lo9[ax9]));
      HQ->tip[ax9]=far9?hi9[ax9]:lo9[ax9]; HQ->nearP[ax9]=far9?lo9[ax9]:hi9[ax9];
      HQ->glow=0; HQ->glowC[0]=1.f; HQ->glowC[1]=0.42f; HQ->glowC[2]=0.12f; memcpy(HQ->glowP,HQ->tip,12);
      if(idx>=0){ Pack*HP=&g_setPk[idx];
          for(unsigned mi=0;mi<HP->nMeshes&&!HQ->glow;mi++){ const char*mn=packStr(HP,HP->meshes[mi].name);
              const char*h9=mn?strchr(mn,'#'):NULL;
              if(!h9||strlen(h9)<8||h9[7]!='*') continue;
              char hx9[7]; memcpy(hx9,h9+1,6); hx9[6]=0; unsigned v9=(unsigned)strtoul(hx9,NULL,16);
              HQ->glowC[0]=((v9>>16)&255)/255.f; HQ->glowC[1]=((v9>>8)&255)/255.f; HQ->glowC[2]=(v9&255)/255.f;
              PMesh*M9=&HP->meshes[mi]; const float*vp=(const float*)(HP->pk+M9->offPos); double s9[3]={0,0,0};
              for(unsigned v=0;v<M9->verts;v++) for(int k=0;k<3;k++) s9[k]+=vp[v*3+k];
              if(M9->verts){ for(int k=0;k<3;k++) HQ->glowP[k]=(float)(s9[k]/(double)M9->verts); HQ->glow=1; } }
          printf("[prop] held '%s': far tip (%.3f,%.3f,%.3f), grip end (%.3f,%.3f,%.3f)%s",stem,HQ->tip[0],HQ->tip[1],HQ->tip[2],
                 HQ->nearP[0],HQ->nearP[1],HQ->nearP[2],HQ->glow?"":"\n");
          if(HQ->glow) printf(", GLOWS at (%.3f,%.3f,%.3f) in %02X%02X%02X\n",HQ->glowP[0],HQ->glowP[1],HQ->glowP[2],
                 (int)(HQ->glowC[0]*255.f+0.5f),(int)(HQ->glowC[1]*255.f+0.5f),(int)(HQ->glowC[2]*255.f+0.5f)); } }
    snprintf(g_held[g_nHeld].stem,32,"%s",stem); g_held[g_nHeld].pk=idx; g_held[g_nHeld].ext=ext9; g_held[g_nHeld].loY=loY9; g_held[g_nHeld].hgt=hiY9-loY9;
    memcpy(g_held[g_nHeld].lo,lo9,12); memcpy(g_held[g_nHeld].hi,hi9,12); g_held[g_nHeld].deckY=deckY9;
    if(idx>=0&&deckY9>-1e8f){ printf("[prop] held '%s': middle-of-length top %.3f, box top %.3f (a board's deck vs its kicks)\n",stem,deckY9,hiY9); fflush(stdout); }
    g_nHeld++;
    fflush(stdout);
    return idx; }

/* ⭐ THE BOARD CHANNEL (ROBOT NEKO ep 10) --------------------------------------------------------
 *     stem@size@pitch@board
 * `@deck` put a board under a rider's ROOT and could never flip, pop or leave the floor: the board
 * is where the body is, not where the move says. `@board` hangs it off the CLIP instead. For the
 * clip a body is playing, a companion `<clip>_bd` in the same pack carries the pose of the BOARD
 * FRAME in the rider's model space (node 0: a type 1 translation and a type 0 rotation; origin the
 * centre of the deck's top, +X toward the nose / the lead foot, +Y the deck's up-normal). It is
 * sampled at exactly the phase the body was -- the same exposure, the same transition hold, the
 * same interpolation -- so a kickflip turns under the feet that were authored against it and
 * nothing is solved at runtime. The prop is drawn at bodyModel x channel x FIT (drawHeldOne).
 * With no channel for the current clip the board keeps its last channel pose RELATIVE TO THE BODY;
 * before any channel at all it lies on the floor under the root, and says so. There is NO deck
 * lift: a z_rm_ clip owns Y and its feet are authored ON the deck. */
static float g_boardChT[SCN_MAXCAST][3], g_boardChQ[SCN_MAXCAST][4];
static int   g_boardChOK[SCN_MAXCAST];         /* a channel pose has been sampled for this slot */
static float g_boardXfKeep[SCN_MAXCAST];       /* how much of last frame's channel pose to keep: the hero's crossfade */
/* ...AND THE BOARD AS DRAWN, for the clipping audit: mesh space -> world, the mesh's own box, and
 * the frame it was drawn on. boardCornersW() turns them into eight world corners. The board FRAME
 * (deck-top centre, rider's scale included) is g_castAttM[slot][ATT_BOARD]. */
static float    g_boardM[SCN_MAXCAST][16];
static float    g_boardLo[SCN_MAXCAST][3], g_boardHi[SCN_MAXCAST][3];
static unsigned g_boardStamp[SCN_MAXCAST];
static int      g_boardDrawn[SCN_MAXCAST];
static int      g_heldBoardPass=0;             /* drawHeld (0) hangs everything else; drawBoards (1) the boards */
static int boardSpecOf(int slot,HeldSpec*H){
    if(slot<0||slot>=SCN_MAXCAST) return 0;
    const char*sp9[2]={g_castProp[slot],g_castProp2[slot]};
    for(int k=0;k<2;k++) if(heldParse(sp9[k],H)&&!strcmp(H->where,"board")) return 1;
    return 0; }
static int castOnBoard(int slot){ HeldSpec H; return boardSpecOf(slot,&H); }
/* the companion of (pack, clip) for one slot -- resolved once per clip, cached, and said once */
#define BOARD_CACHE 32
static int boardChannelOf(int slot,Pack*cp,int ci){
    static struct { const unsigned char*pk; int c, bd; } K9[SCN_MAXCAST][BOARD_CACHE];
    static int nK9[SCN_MAXCAST], ring9[SCN_MAXCAST];
    if(slot<0||slot>=SCN_MAXCAST||!cp||ci<0||(unsigned)ci>=cp->nClips) return -1;
    for(int k=0;k<nK9[slot];k++) if(K9[slot][k].pk==cp->pk&&K9[slot][k].c==ci) return K9[slot][k].bd;
    /* ⛔ packStr returns ONE static buffer: the search below overwrites it, and the first smoke test
     * printed 'z_board_bail' (the pack's last clip) for every clip. Copy the name out first. */
    char nm[64]; snprintf(nm,sizeof nm,"%s",packStr(cp,cp->clips[ci].name));
    char want[72]; snprintf(want,sizeof want,"%s_bd",nm);
    int bd=-1;
    for(unsigned c=0;c<cp->nClips;c++) if(!strcmp(packStr(cp,cp->clips[c].name),want)){ bd=(int)c; break; }
    if(bd<0) printf("[board] slot %d '%s' has no board channel -- the board keeps its last channel pose\n",slot,nm);
    else {
        PClip*bc=&cp->clips[bd], *cc=&cp->clips[ci];
        int hasT=0, hasR=0; unsigned char*tr=cp->pk+bc->offTracks;
        for(unsigned t=0;t<bc->nTracks;t++){ unsigned node=*(unsigned*)(tr+t*12), type=*(unsigned*)(tr+t*12+4);
            if(node!=0) continue;
            if(type==1) hasT=1; else if(type==0) hasR=1; }
        if(!hasT||!hasR||!bc->frames){
            printf("[board] slot %d '%s': channel '%s' REFUSED -- it needs a translation AND a rotation track on node 0 "
                   "(has T%d R%d, %u frames); the board keeps its last channel pose\n",slot,nm,want,hasT,hasR,bc->frames);
            bd=-1; }
        else {
            printf("[board] slot %d '%s' -> channel '%s'\n",slot,nm,want);
            if(bc->frames!=cc->frames||fabsf(bc->fps-cc->fps)>1e-3f)
                printf("[board]   WARNING: the channel is %u frames @ %g fps and the clip %u @ %g -- sampled at the body's phase regardless\n",
                       bc->frames,(double)bc->fps,cc->frames,(double)cc->fps); } }
    fflush(stdout);
    int w9=(nK9[slot]<BOARD_CACHE)?nK9[slot]++:(ring9[slot]++%BOARD_CACHE);
    K9[slot][w9].pk=cp->pk; K9[slot][w9].c=ci; K9[slot][w9].bd=bd;
    return bd; }
/* node 0 of a channel clip at a phase -- fmod'd exactly like the body's own samplers; frac=0 is the
 * nearest-frame sampling labDrawPartner still uses for a body's own clip */
static int boardChanSample(Pack*P,PClip*c,double ph,int frac,float T[3],float Q[4]){
    if(!c->frames) return 0;
    double fp=fmod(ph,(double)c->frames); if(fp<0) fp+=c->frames;
    unsigned f0=(unsigned)fp, f1=(f0+1)%c->frames; float ft=frac?(float)(fp-(double)f0):0.f;
    unsigned char*tr=P->pk+c->offTracks; int got=0;
    for(unsigned t=0;t<c->nTracks;t++){
        unsigned node=*(unsigned*)(tr+t*12), type=*(unsigned*)(tr+t*12+4), offD=*(unsigned*)(tr+t*12+8);
        if(node!=0) continue;
        float*d=(float*)(P->pk+offD);
        if(type==0){ qnlerp(Q,d+(size_t)f0*4,d+(size_t)f1*4,ft); got|=2; }
        else if(type==1){ float*a=d+(size_t)f0*3,*b=d+(size_t)f1*3;
            for(int k=0;k<3;k++) T[k]=a[k]+(b[k]-a[k])*ft;
            got|=1; } }
    return got==3; }
/* ⭐ ep 10: WHERE A @board RIDER'S FLOOR IS, in the rig's own units. The skate clips were authored on their own
 * floor (the stand's lowest sole vertex) and a rig's bind mesh can reach below it -- reva's does, by 0.038 u, so
 * placing her by P->mny sank her board into the scan. The ride channel knows the answer exactly: in
 * z_rm_sk_ride_bd's first frame the board is flat with its wheels ON the authored floor. NAN when there is no
 * ride channel to read (the caller keeps P->mny). Cached per slot and pack. */
static float boardAuthoredFloor(int slot,Pack*P,float sc){
    if(!P||slot<0||slot>=SCN_MAXCAST||sc<=1e-6f) return NAN;
    static const unsigned char*pk9[SCN_MAXCAST]; static float fl9[SCN_MAXCAST], sc9[SCN_MAXCAST];
    if(pk9[slot]==P->pk&&fabsf(sc9[slot]-sc)<1e-5f) return fl9[slot];
    float out=NAN;
    HeldSpec H;
    if(boardSpecOf(slot,&H)){
        int pk=heldFind(H.stem); float deck=-1e9f, loY=0.f, ext=0.f;
        for(int h=0;pk>=0&&h<g_nHeld;h++) if(g_held[h].pk==pk){ deck=g_held[h].deckY; loY=g_held[h].loY; ext=g_held[h].ext; break; }
        int ci=-1; for(unsigned c=0;c<P->nClips;c++) if(!strcmp(packStr(P,P->clips[c].name),"z_rm_sk_ride_bd")){ ci=(int)c; break; }
        float T[3], Q[4];
        if(deck>-1e8f&&ext>1e-5f&&ci>=0&&boardChanSample(P,&P->clips[ci],0.0,0,T,Q)){
            out=T[1]-(deck-loY)*(H.size/ext)/sc;
            printf("[board] slot %d %s: authored floor %.4f rig (P->mny %.4f -- %+.3f u)\n",slot,P->label,out,P->mny,(P->mny-out)*sc); fflush(stdout); } }
    pk9[slot]=P->pk; fl9[slot]=out; sc9[slot]=sc;
    return out; }
/* PUBLISH ATT_BOARD for one body, from the same FK pass that publishes its grip. `cp/ci` is the clip
 * the body was sampled from THIS frame (its own pack, or the bank it borrowed from), `ph` the phase it
 * was sampled at AFTER any hold, `frac` whether that sampler interpolates. */
static void boardPublish(int slot,Pack*cp,int ci,double ph,int frac,const float*model){
    if(slot<0||slot>=SCN_MAXCAST) return;
    HeldSpec H; if(!boardSpecOf(slot,&H)) return;
    int bd=boardChannelOf(slot,cp,ci);
    float T[3], Q[4];
    if(bd>=0&&boardChanSample(cp,&cp->clips[bd],ph,frac,T,Q)){
        float keep=g_boardXfKeep[slot];
        if(g_boardChOK[slot]&&keep>1e-3f){      /* the body is crossfading: so is its board */
            qnlerp(Q,Q,g_boardChQ[slot],keep);
            for(int k=0;k<3;k++) T[k]+=(g_boardChT[slot][k]-T[k])*keep; }
        memcpy(g_boardChT[slot],T,12); memcpy(g_boardChQ[slot],Q,16); g_boardChOK[slot]=1; }
    float*D=g_castAttM[slot][ATT_BOARD];
    if(g_boardChOK[slot]){
        static const float one9[3]={1.f,1.f,1.f};
        M4 ch; m4_trs(ch,g_boardChT[slot],g_boardChQ[slot],one9);
        m4_mul(D,(float*)model,ch); }
    else {
        /* no channel pose yet: flat on the floor under the root, nose along the rider's +X */
        float deckH=0.f; int pk=heldFind(H.stem);
        for(int h9=0;pk>=0&&h9<g_nHeld;h9++) if(g_held[h9].pk==pk){
            deckH=g_held[h9].hgt*((g_held[h9].ext>1e-5f)?(H.size/g_held[h9].ext):H.size); break; }
        float sc=sqrtf(model[4]*model[4]+model[5]*model[5]+model[6]*model[6]); if(sc<1e-6f) sc=1.f;
        M4 fl; memset(fl,0,64); fl[0]=fl[5]=fl[10]=fl[15]=1.f;
        fl[13]=(g_castFloorY[slot]+deckH-model[13])/sc;
        m4_mul(D,(float*)model,fl);
        static int said9[SCN_MAXCAST];
        if(!said9[slot]){ said9[slot]=1;
            printf("[board] slot %d: no channel pose yet -- the board lies on the floor under the root until a clip carries one\n",slot); fflush(stdout); } }
    g_castAttOK[slot][ATT_BOARD]=1; }
/* ⭐ FOR THE CLIPPING AUDIT (ep 10): the eight world corners of the board slot `slot` drew this frame
 * or the last -- out[0..3] its mesh box's TOP ring (max Y: the deck), out[4..7] its BOTTOM ring (min
 * Y: the wheels), each ring (lo x,lo z) (hi x,lo z) (hi x,hi z) (lo x,hi z) in the mesh's own axes.
 * Returns 0 when that slot drew no board. */
static int boardCornersW(int slot,float out[8][3]){
    if(slot<0||slot>=SCN_MAXCAST||!g_boardDrawn[slot]||g_boardStamp[slot]+1u<g_frameStamp) return 0;
    const float*M=g_boardM[slot],*lo=g_boardLo[slot],*hi=g_boardHi[slot];
    for(int k=0;k<8;k++){
        int r=k%4; float p[3]={ (r==1||r==2)?hi[0]:lo[0], (k<4)?hi[1]:lo[1], (r>=2)?hi[2]:lo[2] };
        for(int a=0;a<3;a++) out[k][a]=M[a]*p[0]+M[4+a]*p[1]+M[8+a]*p[2]+M[12+a]; }
    return 1; }

/* ⭐ ONE-SHOT TRICKS INSIDE A LINE (ROBOT NEKO ep 10) -------------------------------------------
 *     htrick=clip@sec[,clip@sec...]   ptrick=  p2trick=  p3trick=
 * `sec` after the line's staging applied, on an EXPOSURE, the slot's body cuts to the clip at phase
 * 0 and plays it ONCE -- no loop, no hold -- then cuts back to the clip the line gave it, at phase 0.
 * Not sticky: the next line's staging hands every body back. State here, because labDrawPartner and
 * the hero's samplers read it; the firing lives after the lab's globals (trickTick). */
#define TRICK_MAX 8
typedef struct { char clip[64]; float at; } TrickEv;
typedef struct { int clip; Pack*dnP; int dnC; float hold; int rm; } TrickBase;
static TrickEv   g_trk[SCN_MAXCAST][TRICK_MAX];
static int       g_trkN[SCN_MAXCAST], g_trkNext[SCN_MAXCAST];
static int       g_trkOn[SCN_MAXCAST];      /* a trick is playing on this slot */
static int       g_trkDone[SCN_MAXCAST];    /* ...and its last frame has been exposed */
static int       g_trkFresh[SCN_MAXCAST];   /* cut THIS frame: the sampler shows phase 0, not phase 0 + a step */
static TrickBase g_trkBase[SCN_MAXCAST];    /* what the line gave the slot, for the cut back */
static int       g_trkLine=-1;
static float     g_trkT0=0.f;               /* scene seconds at which that line's staging applied */
static void trickReset(void){
    for(int s=0;s<SCN_MAXCAST;s++){ g_trkN[s]=g_trkNext[s]=0; g_trkOn[s]=g_trkDone[s]=g_trkFresh[s]=0; }
    g_trkLine=-1; }
/* a playing trick reaches its last frame and stops there; trickTick cuts back on the next exposure */
static void trickClampEnd(int slot,double*ph,const PClip*c){
    if(!c||c->frames<1) return;
    if(*ph>=(double)(c->frames-1)){ *ph=(double)(c->frames-1); g_trkDone[slot]=1; } }

/* ⭐ A BODY ON A DECK STANDS ON IT (ROBOT NEKO ep 9). Lifted by the board's own measured height
 * at its held size, so the soles land on the deck and the wheels stay on the floor. 0 when
 * neither prop slot is `@deck`. */
static float castDeckLift(int slot){
    if(slot<0||slot>=SCN_MAXCAST) return 0.f;
    const char*sp9[2]={g_castProp[slot],g_castProp2[slot]};
    for(int k=0;k<2;k++){ HeldSpec H; if(!heldParse(sp9[k],&H)||strcmp(H.where,"deck")) continue;
        int pk=heldFind(H.stem); if(pk<0) return 0.f;
        for(int h9=0;h9<g_nHeld;h9++) if(g_held[h9].pk==pk){
            float k9=(g_held[h9].ext>1e-5f)?(H.size/g_held[h9].ext):H.size;
            return g_held[h9].hgt*k9; } }
    return 0.f; }
#define castRides(s) (castDeckLift(s)>0.f||castOnBoard(s))   /* ⭐ ep 10: on its @board channel a body rides too -- with NO lift */

/* ⭐ EVERY BODY WALKS WHERE IT GOES (ROBOT NEKO ep 9) ---------------------------------------
 * jacob on ep 8: "reva and kaeli slide around together without walking". Two causes, both
 * read out of this file rather than guessed:
 *  1. the partner's gait was keyed on g_partSpd -- how fast her GAP and SIDE change, i.e. her
 *     offset from the hero. When the PAIR walks that offset is constant, so she read as
 *     standing still and slid along beside a walking reva. Speed is now measured in the
 *     WORLD, per body, where the body is actually drawn.
 *  2. a mark change TELEPORTED the body (model[12]=MK->x), and slots 2 and 3 shared slot 1's
 *     walk state. A body sent to a new mark now WALKS there -- turns to face the line, goes,
 *     comes to rest on the mark, turns onto its yaw -- with a walk state of its own.
 *     `snap=1` still lands on the cut.
 * A body with wheels (Kickflip), or standing on a `@deck`, ROLLS: the same travel, no gait.
 * Travel advances on the EXPOSURE clock, so a stop-motion puppet is moved between exposures. */
typedef struct {
    int    on;                  /* travelling to (or turning onto) its mark */
    float  x, z, yaw, v;        /* this exposure's spot, facing (rad), speed along the line */
    float  wx, wz; int winit;   /* last exposure's DRAWN spot, for the measured speed */
    float  spd;                 /* measured world speed, eased -- what the gait keys on */
    float  walkMix; double walkPh; Pack*walkP; int walkC;
    float  px, pz, pyaw; int pon;   /* PUBLISHED on the exposure -- what every draw reads (ep 9) */
    /* ⭐ ep 10: A PATH. `hold` = stopped where a path ended, facing its last tangent: the draw keeps that
     * spot and facing instead of snapping onto the mark's yaw. trav is the travel heading (atan2(dx,dz),
     * the yaw convention); bank is the carve, + tipping the top toward (cos trav, -sin trav). The p-
     * forms are published with px/pz, on the exposure. */
    int    path, hold;
    float  ps, plen;            /* arc length rolled / the whole path, u */
    float  trav, bank;
    float  ptrav, pbank; int phold;
    int    lapping, laps, loopSaid;   /* ⭐ hloop: circling a closed route (no braking), laps done, said it cannot */
    int    seg;                       /* ⭐ the path run this body is rolling (its line), for what comes next */
    int    rn, ri; float rx[64], rz[64];   /* ⭐ a walk ROUTED round the furniture: corners, then the mark (rtRoute) */
    float  stuck;                           /* ⭐ (10-22) seconds of walking that went nowhere: past 0.6 it is placed */
} Loco;
static int wrrtRoute(int slot,float sx,float sz,float tx,float tz,float*ox,float*oz,int max);   /* defined after the audit grid */
static Loco  g_loco[SCN_MAXCAST];
static float g_stageV9=0.f;     /* the pair's own walk speed, when a line carries hx/hz */
/* ⭐ PLACE AND POSE CHANGE ON THE SAME FRAME (ep 9). The scene tick that advances a walk runs after
 * the draw, so a travelling body used to change pose on the exposure and position one frame later --
 * two changing frames per exposure at 60fps. The draws read this, refreshed once per frame and only
 * on an exposure. */
static void pipePublishAll(void);   /* ⭐ ep 11: a half-pipe ride publishes on the same exposure (below) */
static void locoPublish(void){
    static unsigned stamp9=0xffffffffu;
    if(stamp9==g_frameStamp) return;
    stamp9=g_frameStamp;
    if(g_stepFps>0.f&&g_stepHeld) return;      /* a held exposure shows the spot it already showed */
    for(int s=0;s<SCN_MAXCAST;s++){ Loco*L=&g_loco[s]; L->px=L->x; L->pz=L->z; L->pyaw=L->yaw; L->pon=L->on;
        L->ptrav=L->trav; L->pbank=L->bank; L->phold=L->hold; }   /* ⭐ ep 10: the carve and a path's end ride the same exposure */
    pipePublishAll(); }
#define LOCO_CRUISE 2.10f       /* u/s -- 1.2 m/s at 1.75 u/m: a stroll across a set */
#define LOCO_TURN   4.5f        /* rad/s the facing swings while walking */
#define LOCO_RIDE   1.50f       /* u/s -- z_board_push's planted foot sweeps the floor at ~1.5 (reva 1.48, kaeli 1.60) */

/* The speed a walking body should have at the next exposure.
 *   v     its speed now, u/s          left  distance still to go along the line, u
 *   dt    the exposure step, s        returns the new speed, u/s
 *   cruise the pace it is heading for, u/s: LOCO_CRUISE on foot, LOCO_RIDE on a board
 * Used by a mark walk AND by the pair walk (hx/hz). The caller clamps the step to `left`,
 * so this cannot overshoot the mark -- what it decides is how getting there FEELS. */
/* ⭐ ep 10: the trapezoid with its two rates named, so a path can say its own (3.0 up, 2.5 down) */
static float locoProfile(float v,float left,float dt,float cruise,float acc,float brk){
    float up=v+acc*dt; if(up>cruise) up=cruise;
    float stopV=sqrtf(2.f*brk*(left>0.f?left:0.f));   /* the fastest that still stops in `left` */
    float w=(up<stopV)?up:stopV;
    if(w<0.25f&&left>0.03f) w=0.25f;                  /* never stall short: a coarse step rounds the last cm away */
    return w; }
static float locoSpeed(float v,float left,float dt,float cruise){
    /* ⭐ A TRAPEZOID, NOT A DRAG (ep 9). Build toward cruise at a measured acceleration, never
     * faster than the speed that can still brake to zero in the ground left -- which is what
     * puts the stop ON the mark. A board builds and brakes gentler than feet: it coasts. */
    int ride9=(cruise<=LOCO_RIDE+0.01f);
    const float A9=ride9?2.2f:4.2f;          /* u/s^2: a walker is at pace in ~0.5 s */
    const float B9=ride9?1.6f:3.4f;          /* u/s^2: braking; ~0.65 u from walking pace */
    return locoProfile(v,left,dt,cruise,A9,B9); }

/* ⭐ ep 10: RIDERS GO AROUND THE PARK, NOT MARK TO MARK ----------------------------------------------------
 * jacob wants ep 10 skated AROUND the Southbank Undercroft, and a mark walk is a straight line. A PATH is
 * `hpath=a,b,c` on a scene line (ppath/p2path/p3path for the other slots): a CENTRIPETAL CATMULL-ROM
 * (alpha 0.5, the Barry-Goldman pyramid) through
 *      C0 = where the body stands now, then the marks in order, with both ends duplicated
 * sampled LOCO_PSEG times a segment and rolled by ARC LENGTH, so speed along it is speed in the world.
 * ⛔ Agent D's path checker clears these same curves against the scan -- ONE definition, so change neither
 * without the other. What the definition needs said out loud:
 *   - a duplicated end is a knot interval of ZERO; that level of the pyramid takes the shared point (its
 *     limit) instead of dividing by zero. The curve then has zero parametric speed at both ends, so
 *     headings come from the sampled chords, never from the derivative;
 *   - control points closer than 0.05 u are one point (the last mark always survives), and a C0 more
 *     than 30 u from the first mark is not a roll: the body is PLACED on that mark, as a mark walk is.
 * Speed builds at 3.0 u/s^2 to the slot's spd (3.0 u/s) and brakes at 2.5 onto the last mark. A body that
 * arrives KEEPS the last tangent's facing (`hold`), and the path's last mark becomes the body's mark. */
#define LOCO_PATHMAX 64          /* marks on one path (a planner's loop is 24-28, and was REFUSED at 12) */
#define LOCO_PSEG    24          /* dense spline samples per segment -- what the arc length is measured on */
#define LOCO_PDS     0.10f       /* u between the samples a body ROLLS on (resampled by arc length) */
#define LOCO_PU      4096        /* ...and how many one path may have: past ~410 u they spread out */
#define LOCO_PDENSE  ((LOCO_PATHMAX+2)*LOCO_PSEG+1)   /* dense samples: every segment, C0's and a lead-in's included */
#define LOCO_PSPD    3.0f        /* u/s: a path's cruise unless the line says hspd= */
#define LOCO_PACC    3.0f        /* u/s^2 */
#define LOCO_PBRK    2.5f        /* u/s^2 */
#define LOCO_G       17.15f      /* g in club units/s^2 (9.8 m/s^2 x 1.75 u/m) */
#define LOCO_BANKMAX 0.3141593f  /* 18 degrees */
typedef struct { int n, closed; float ds, len; float x[LOCO_PU], z[LOCO_PU], h[LOCO_PU], k[LOCO_PU]; char end[32]; char spec[512]; } LocoPath;
static LocoPath g_lpath[SCN_MAXCAST];
static char  g_castPath[SCN_MAXCAST][512];   /* the path in force per slot, off the staged line ("" = none) */
static float g_castSpd[SCN_MAXCAST];         /* its speed, u/s; 0 = LOCO_PSPD */
static int   g_castLoop[SCN_MAXCAST];        /* hloop=1: keep circling a CLOSED route */
static int   g_castPathSeg[SCN_MAXCAST];     /* the staged line's path RUN (its first line), -1 = none */
static int   g_locoSeg[SCN_MAXCAST]={-1,-1,-1,-1,-1};   /* the run each slot last STARTED; a run starts once, in order */
static int   g_locoEarly[SCN_MAXCAST]={-1,-1,-1,-1,-1}; /* ⭐ ep 13: the run a slot was HANDED ON TO early (its line not staged yet), or -1 */
static int   g_locoPathReset=0;              /* scnLoad: a replayed scene rolls its paths again */
static int   g_leanOff=0;                    /* ctl lean=0: no carve */
/* ⭐ ep 10: THE TRACKING LENS's state (camTrackTick, below locoTick). Declared here because scnLoad clears it. */
typedef struct {
    int   mode;                         /* 0 off, 1 camtrack, 2 campan, 3 cammove, 4 camorbit, 5 camhold (the filmer's own camera) */
    int   holder;                       /* camhold: the slot whose hand the camera is in */
    float drop;                         /* camhold: u below the hand -- the phone dropped toward the ground */
    int   slot, snap, line;
    int   habs;                         /* ep 11: `height` is ABSOLUTE world y (`=h`), not a height above the body */
    float dist, height, azim, fov, aimh;   /* camorbit: dist is its radius */
    float eye[3];                       /* campan: the pinned eye */
    float eS[3], eV[3], aS[3], aV[3];   /* the spring: eye and aim, and their velocities */
    float yF, yV;                       /* ep 11: the body's height through a fast pre-filter; yV = its vertical speed */
    float mA[7], mB[7];                 /* ep 11: cammove's start and end -- eye, aim, fov */
    float d0, d1;                       /* ep 11: camorbit's world headings, degrees */
    float mt, mT;                       /* ep 11: lens seconds into the move/sweep, and its length */
    int   wc; float wcP[3];             /* ⭐ 10-22: camorbit round a WORLD point (camat=), not a body */
} CamTrk;
static CamTrk g_camTrk;
#define CAMTRK_W 6.0f                   /* rad/s: the lens spring, critically damped */
#define CAMHOLD_W 9.0f                  /* rad/s: a hand-held camera's AIM -- quicker than a dolly, still late */
#define CAMTRK_WY 20.0f                 /* rad/s: ep 11, the height pre-filter. MEASURED (scratch harness, a rider rolling
                                         * 0 -> 5 u -> 0 every 2.4 s, lens at 60): the aim trails her by 1.83 u with no lead,
                                         * 1.38 at 8 rad/s, 0.93 at 15, 0.79 at 20, 0.64 at 30 -- and a height stepped at
                                         * 12/s adds 0.0012 u of ripple at 20 (0.0025 at 30). 20 is the knee. */
/* ⭐ ep 11: camshake -- amp (u, the most the eye strays) and rate (Hz). Sticky; 0 = off. Its own clock is the lens's. */
static float g_camShk[2]={0.f,1.5f};
static float g_camShkT=0.f;

static float locoWrap(float a){ while(a>3.14159265f) a-=6.28318531f; while(a<-3.14159265f) a+=6.28318531f; return a; }
/* ⭐ A RIDER TRAVELS ALONG ITS BOARD, not along its chest (ep 9) -- ONE rule for the mark walk, the paths
 * and the lens: @deck runs the board across the body, so it faces -90 off its travel with its LEFT side
 * leading (regular); a wheeled rig says which way its own board runs (wheelYaw, measured at load). */
static int locoIsRider(int s){
    Pack*rp=(s==0)?g_heroPk:((s>0&&s<SCN_MAXCAST)?g_cast[s].pk:NULL);
    return (rp&&rp->nWheel)||castRides(s); }
static float locoRideOff(int s){
    Pack*rp=(s==0)?g_heroPk:((s>0&&s<SCN_MAXCAST)?g_cast[s].pk:NULL);
    if(rp&&rp->nWheel) return rp->wheelYaw;
    return castRides(s)?-1.5707963f:0.f; }

/* ⭐ ep 11: RIDING A HALF-PIPE ------------------------------------------------------------------------------
 *     hpipe=<name>[,<air>]   ppipe=  p2pipe=  p3pipe=     air: u above the coping at the apex (2.0); `off` stops
 *     hair=<clip>[,<clip>...]   pair=  p2air=  p3air=      a one-shot at the START of each air, cycling the list
 * A slot on a pipe runs the VERT CYCLE in its cross-section, at the station along the axis nearest where it
 * stood: DROP IN off the nearer coping (a body standing on that DECK first rolls across it to the lip; then it is
 * tipped over the coping in PIPE_DROP and falls from rest), down under g
 * (LOCO_G), across the flat, up the far transition and wall, AIRBORNE past the coping at the vertical speed
 * that reaches `air`, back down the same wall, and again -- until the slot's pipe value changes. Energy loss is
 * ignored and pumping is implied: the energy is topped up toward g (coping + air) only while she is going DOWN,
 * by at most PIPE_PUMP along the travel. In the air she turns 180 about the world vertical: upright at the apex,
 * pitched into the wall at both ends. The body's rotation is RotA(pitch) Ry(yaw) throughout, so up is the
 * surface normal on the ramp (0 on the flat, 90 into the vert) and an air only takes the pitch to 0 and back
 * while the yaw turns (frontside, then backside). The stance is the rider convention (locoRideOff: the board
 * along the travel, LEFT side leading); only an air turns it, so a stall at the lip rides back down fakie.
 * Place + pose are PUBLISHED on the exposure with the loco's (ep 9), and the body's AUTHORED floor
 * (boardAuthoredFloor) is what touches the surface, so a @board rider's board follows by construction. */
#define PIPE_DROP   0.40f      /* s: tipping over the coping before the fall */
#define PIPE_PUMP   6.0f       /* u/s^2: the most pumping may add along the travel */
#define PIPE_SUB    0.002f     /* s: the integration substep */
#define PIPE_ROLLA  4.0f       /* u/s^2 and u/s: rolling across a deck to the lip */
#define PIPE_ROLLV  3.0f
#define PIPE_TORSO  1.6f       /* u up her body to her torso: what a lens's aimh (1.6 by default) is aimed at */
typedef struct {
    int   on, pi; char name[32];
    float air;                 /* the target: the board's contact over the coping at the apex */
    float w;                   /* the station along the axis */
    float s, v;                /* arc length along the cross-section (signed, pipeAt's) and its rate */
    int   stance;              /* +1: the leading side points +C; -1: -C. Only an air turns it */
    float yaw, yawNow;         /* flat-frame yaw (rider heading + locoRideOff): at rest, and turning in an air */
    int   phase;               /* 0 tipping in off the coping, 1 on the ramp, 2 in the air, 3 rolling across a deck */
    float t;                   /* seconds into this phase */
    int   side;                /* the wall it dropped in off / launched from */
    float fT, fv0, spin;       /* the air: its length, launch speed and turn */
    float lu, lv, lu0, yaw0;   /* phase 3: |u| on the deck, the roll's speed toward the lip, where it began, and the
                                * facing it began with (the roll turns her onto the ride's: a kickturn, not a snap) */
    float vu;                  /* the speed across the axis (+C), for a lens's lead */
    int   airs;
    float x, y, z, pitch;      /* the continuous placement: the contact point and the pitch about A */
    float px, py, pz, ppitch, pyaw; int pon, ppi;   /* ...as PUBLISHED on the exposure */
} PipeRide;
static PipeRide g_prd[SCN_MAXCAST];
static char g_castPipe[SCN_MAXCAST][48];      /* the staged line's hpipe/ppipe/p2pipe/p3pipe ("" = none) */
static char g_castPipeAir[SCN_MAXCAST][128];  /* ...and its hair/pair/p2air/p3air list */
static char g_pipePend[SCN_MAXCAST][32];      /* an air's trick: trickTick fires it on the next exposure */
static int pipeFind(const char*nm){ for(int i=0;i<g_nPipe;i++) if(!strcmp(g_pipe[i].name,nm)) return i; return -1; }
static int pipeRides(int s,int pi){ return s>=0&&s<SCN_MAXCAST&&g_prd[s].on&&g_prd[s].pi==pi; }
static float pipeHdg(const HalfPipe*H,int dir){ float h=atan2f(H->cr[0],H->cr[2]); return (dir>0)?h:locoWrap(h+3.14159265f); }
static void pipeRotA(const float*A,float p,const float*v,float*o){   /* Rodrigues: v about the unit axis A by p */
    float c=cosf(p), s=sinf(p), d=A[0]*v[0]+A[1]*v[1]+A[2]*v[2];
    float x[3]={A[1]*v[2]-A[2]*v[1], A[2]*v[0]-A[0]*v[2], A[0]*v[1]-A[1]*v[0]};
    for(int k=0;k<3;k++) o[k]=v[k]*c+x[k]*s+A[k]*d*(1.f-c); }
/* where the ride has her NOW: the contact point, the pitch about the axis and the turning yaw */
static void pipePlace(PipeRide*Rd,const HalfPipe*H){
    float u,h,th;
    Rd->yawNow=Rd->yaw;
    if(Rd->phase==1) pipeAt(H,Rd->s,&u,&h,&th);
    else { pipeAt(H,(float)Rd->side*pipeHalfLen(H),&u,&h,&th);
        if(Rd->phase==3){ u=(float)Rd->side*Rd->lu; th=0.f;                    /* upright on the deck, rolling to the lip */
            float span=Rd->lu0-(0.5f*H->F+H->R), f=(span>1e-3f)?(Rd->lu0-Rd->lu)/span:1.f; if(f>1.f) f=1.f; if(f<0.f) f=0.f;
            Rd->yawNow=Rd->yaw0+locoWrap(Rd->yaw-Rd->yaw0)*f*f*(3.f-2.f*f); }
        else if(Rd->phase==0){ float f=Rd->t/PIPE_DROP; if(f>1.f) f=1.f;
            th=(float)Rd->side*1.5707963f*0.5f*(1.f-cosf(3.14159265f*f)); }   /* upright on the lip -> into the wall */
        else { float t=Rd->t, f=(Rd->fT>1e-4f)?t/Rd->fT:1.f; if(f>1.f) f=1.f;
            h+=Rd->fv0*t-0.5f*LOCO_G*t*t; if(h<H->R+H->V) h=H->R+H->V;
            th=(float)Rd->side*1.5707963f*(1.f-sinf(3.14159265f*f));        /* upright at the apex */
            Rd->yawNow=Rd->yaw+Rd->spin*f*f*(3.f-2.f*f); } }
    float o[3]; pipeW(H,u,h,Rd->w,o);
    Rd->x=o[0]; Rd->y=o[1]; Rd->z=o[2]; Rd->pitch=th; }
static void pipePublishAll(void){
    for(int s=0;s<SCN_MAXCAST;s++){ PipeRide*Rd=&g_prd[s];
        Rd->px=Rd->x; Rd->py=Rd->y; Rd->pz=Rd->z; Rd->ppitch=Rd->pitch; Rd->pyaw=Rd->yawNow; Rd->pon=Rd->on; Rd->ppi=Rd->pi; } }
/* one crossing, coping to coping, at the target energy -- the number a lens wants to be cut on */
static float pipeCrossTime(const HalfPipe*H,float air){
    float S=pipeHalfLen(H), s=-S, v=sqrtf(2.f*LOCO_G*air), t=0.f;
    if(v<0.3f) v=0.3f;
    while(s<S&&t<30.f){ float u,h,th; pipeAt(H,s,&u,&h,&th); v-=LOCO_G*sinf(th)*PIPE_SUB; if(v<0.05f) v=0.05f; s+=v*PIPE_SUB; t+=PIPE_SUB; }
    return t; }
/* THE RIDE, per slot, from locoTick: the line's value starts / keeps / ends it, and st8 (the exposure step)
 * advances it in PIPE_SUB substeps. REVA_PIPEDBG=1 says where every rider is, a few times a second. */
static void pipeTickSlot(int s,float st8){
    if(s<0||s>=SCN_MAXCAST) return;
    PipeRide*Rd=&g_prd[s];
    char nm[32]; float air=2.0f; const char*v=g_castPipe[s]; int k=0;
    if(strcmp(v,"off")) while(v[k]&&v[k]!=','&&k<31){ nm[k]=v[k]; k++; }
    nm[k]=0;
    if(nm[0]&&v[k]==','){ air=(float)atof(v+k+1); if(!(air>=0.f)) air=0.f; }
    if(!nm[0]){ if(Rd->on){ printf("[pipe] slot %d off '%s' after %d air%s\n",s,Rd->name,Rd->airs,Rd->airs==1?"":"s"); fflush(stdout); }
        Rd->on=0; Rd->pon=0; return; }
    int pi=pipeFind(nm);
    if(pi<0){ static char said9[SCN_MAXCAST][32];
        if(strcmp(said9[s],nm)){ snprintf(said9[s],sizeof said9[s],"%s",nm);
            printf("[pipe] slot %d: NO halfpipe '%s' in set '%s' -- not riding\n",s,nm,g_setCur); fflush(stdout); }
        Rd->on=0; Rd->pon=0; return; }
    const HalfPipe*H=&g_pipe[pi];
    float S=pipeHalfLen(H), top=H->R+H->V, lip=0.5f*H->F+H->R;
    if(!Rd->on||strcmp(Rd->name,nm)){      /* A NEW RIDE: the station nearest the body, off the nearer coping */
        memset(Rd,0,sizeof*Rd);
        Rd->on=1; Rd->pi=pi; snprintf(Rd->name,sizeof Rd->name,"%s",nm); Rd->air=air;
        float dx=0.f, dz=0.f;
        if(g_castAtOK[s]){ dx=g_castAt[s][0]-H->c[0]; dz=g_castAt[s][1]-H->c[2]; }
        float w=dx*H->ax[0]+dz*H->ax[2], u=dx*H->cr[0]+dz*H->cr[2], au=fabsf(u);
        float lim=0.5f*H->W-((H->W>4.f)?1.0f:0.25f*H->W);   /* keep the body and board off the open ends */
        if(w>lim) w=lim;
        if(w<-lim) w=-lim;
        /* standing ON that deck (its height, behind the lip): roll across it to the lip first, not a jump to it */
        int deck9=g_castAtOK[s]&&au>lip+0.05f&&au<=lip+H->D+0.5f&&fabsf(g_castFloorY[s]-(H->c[1]+top))<0.6f;
        Rd->w=w; Rd->side=(u<0.f)?-1:1; Rd->stance=-Rd->side; Rd->phase=deck9?3:0; Rd->t=0.f;
        Rd->lu=Rd->lu0=deck9?((au<lip+H->D)?au:lip+H->D):lip; Rd->lv=0.f;
        Rd->s=(float)Rd->side*S; Rd->v=0.f;
        Rd->yaw=Rd->yawNow=locoWrap(pipeHdg(H,Rd->stance)+locoRideOff(s));
        float yb9=g_castAtOK[s]?g_castAt[s][2]:Rd->yaw;   /* the facing she was drawn with */
        Rd->yaw0=deck9?yb9:Rd->yaw;
        pipePlace(Rd,H);
        Rd->px=Rd->x; Rd->py=Rd->y; Rd->pz=Rd->z; Rd->ppitch=Rd->pitch; Rd->pyaw=Rd->yawNow; Rd->pon=1; Rd->ppi=pi;   /* on the cut */
        float cr9=pipeCrossTime(H,air), fl9=2.f*sqrtf(2.f*LOCO_G*air)/LOCO_G, a9[3];
        pipeW(H,(float)Rd->side*lip,top,w,a9);
        printf("[pipe] slot %d on '%s': station %.2f along the axis, %s the %s coping at (%.2f,%.2f,%.2f), riding at yaw %.0f "
               "(%s %.0f deg onto it); air %.2f u -> apex y %.2f; wall to wall %.2f s (%.2f across + %.2f in the air)%s\n",
               s,nm,w,deck9?"rolls across the deck and drops in off":"drops in off",Rd->side>0?"+C":"-C",a9[0],a9[1],a9[2],
               Rd->yaw*57.29578f,deck9?"kickturns across the deck":"turned on the cut",locoWrap(Rd->yaw-yb9)*57.29578f,
               air,H->c[1]+top+air,cr9+fl9,cr9,fl9,g_castAtOK[s]?"":" -- the body had no spot yet, so the pipe's middle");
        fflush(stdout); }
    Rd->air=air; Rd->pi=pi;
    if(st8<=1e-6f) return;
    float Et=LOCO_G*(top+Rd->air), t=st8;
    while(t>1e-7f){ float h=(t<PIPE_SUB)?t:PIPE_SUB; t-=h;
        if(Rd->phase==3){ Rd->lv+=PIPE_ROLLA*h; if(Rd->lv>PIPE_ROLLV) Rd->lv=PIPE_ROLLV;
            Rd->lu-=Rd->lv*h; if(Rd->lu<=lip){ Rd->lu=lip; Rd->phase=0; Rd->t=0.f; } continue; }
        if(Rd->phase==0){ Rd->t+=h; if(Rd->t>=PIPE_DROP){ Rd->phase=1; Rd->t=0.f; Rd->s=(float)Rd->side*S; Rd->v=0.f; } continue; }
        if(Rd->phase==2){ Rd->t+=h;
            if(Rd->t>=Rd->fT){            /* back onto the same wall, coming down it the other way round */
                Rd->phase=1; Rd->t=0.f; Rd->s=(float)Rd->side*S; Rd->v=-(float)Rd->side*Rd->fv0;
                Rd->yaw=locoWrap(Rd->yaw+Rd->spin); Rd->stance=-Rd->stance; }
            continue; }
        float u,hh,th; pipeAt(H,Rd->s,&u,&hh,&th);
        Rd->v-=LOCO_G*sinf(th)*h;
        if(Rd->v*th<=0.f){                /* pumping, implied: on the way DOWN (or across the flat) only */
            float E=0.5f*Rd->v*Rd->v+LOCO_G*hh, want=Et-E, cap=fabsf(Rd->v)*PIPE_PUMP*h;
            if(want>cap) want=cap;
            if(want<-cap) want=-cap;
            float v2=Rd->v*Rd->v+2.f*want; if(v2<0.f) v2=0.f;
            Rd->v=((Rd->v<0.f)?-1.f:1.f)*sqrtf(v2); }
        Rd->s+=Rd->v*h;
        if((Rd->s>=S&&Rd->v>0.f)||(Rd->s<=-S&&Rd->v<0.f)){
            int sd=(Rd->s>0.f)?1:-1; float v0=fabsf(Rd->v);
            if(v0<0.3f){ Rd->s=(float)sd*S; Rd->v=0.f; continue; }   /* a stall at the lip: gravity takes it back, fakie */
            Rd->phase=2; Rd->side=sd; Rd->fv0=v0; Rd->fT=2.f*v0/LOCO_G; Rd->t=(fabsf(Rd->s)-S)/v0; Rd->s=(float)sd*S;
            Rd->spin=(Rd->airs&1)?-3.14159265f:3.14159265f;   /* one way, then the other */
            Rd->airs++;
            /* the air's trick: the next clip off the slot's list, fired through the trick scheduler */
            char clip[32]={0}; const char*AL=g_castPipeAir[s];
            if(AL[0]&&strcmp(AL,"off")){ int n=1; for(const char*q=AL;*q;q++) if(*q==',') n++;
                const char*q=AL; for(int i=0;i<(Rd->airs-1)%n&&q;i++){ q=strchr(q,','); if(q) q++; }
                int m=0; while(q&&q[m]&&q[m]!=','&&m<31){ clip[m]=q[m]; m++; } clip[m]=0; }
            if(clip[0]) snprintf(g_pipePend[s],sizeof g_pipePend[s],"%s",clip);
            /* which way the chest faces at the apex, against the wall it left: at the wall = frontside */
            float ya=Rd->yaw+0.5f*Rd->spin, fw=(float)sd*(sinf(ya)*H->cr[0]+cosf(ya)*H->cr[2]);
            printf("[pipe] slot %d air %d apex y=%.2f '%s' (%.2f u over the coping, %.2f s, %s)\n",
                   s,Rd->airs,H->c[1]+top+v0*v0/(2.f*LOCO_G),clip[0]?clip:"-",v0*v0/(2.f*LOCO_G),Rd->fT,
                   fw>0.3f?"frontside":(fw<-0.3f?"backside":"turning side-on"));
            fflush(stdout); } }
    { float u9,h9,th9;                     /* the speed across the axis, for a lens's lead */
      if(Rd->phase==1){ pipeAt(H,Rd->s,&u9,&h9,&th9); Rd->vu=Rd->v*cosf(th9); }
      else Rd->vu=(Rd->phase==3)?-(float)Rd->side*Rd->lv:0.f; }
    pipePlace(Rd,H);
    { static int dbg9=-1; static unsigned n9=0; if(dbg9<0){ const char*e9=getenv("REVA_PIPEDBG"); dbg9=(e9&&e9[0]!='0'); }
      if(dbg9&&(n9++%6)==0){ static const char*PH9[4]={"drop-in","ramp","air","deck roll"};
          printf("[pipe] dbg slot %d %s s %.2f v %.2f contact (%.2f,%.2f,%.2f) pitch %.0f yaw %.0f\n",s,PH9[Rd->phase],Rd->s,Rd->v,
                 Rd->x,Rd->y,Rd->z,Rd->pitch*57.29578f,Rd->yawNow*57.29578f); fflush(stdout); } } }
/* THE BODY ON IT: RotA(pitch) Ry(yaw) at her scale, her authored floor on the contact point along the surface's
 * up (a @deck rider stands the board's height higher). Both draws call it after every placement of their own. */
static int pipeModel(int s,float*model,Pack*P,float sc){
    if(s<0||s>=SCN_MAXCAST||!P) return 0;
    locoPublish();
    PipeRide*Rd=&g_prd[s];
    if(!Rd->pon||Rd->ppi<0||Rd->ppi>=g_nPipe) return 0;
    const HalfPipe*H=&g_pipe[Rd->ppi];
    float af=castOnBoard(s)?boardAuthoredFloor(s,P,sc):NAN;
    if(af!=af) af=P->mny;
    float cy=cosf(Rd->pyaw), sy=sinf(Rd->pyaw);
    const float bx[3]={cy,0.f,-sy}, by[3]={0.f,1.f,0.f}, bz[3]={sy,0.f,cy};
    float X[3],Y[3],Z[3];
    pipeRotA(H->ax,Rd->ppitch,bx,X); pipeRotA(H->ax,Rd->ppitch,by,Y); pipeRotA(H->ax,Rd->ppitch,bz,Z);
    float up=castDeckLift(s)-af*sc;
    memset(model,0,64);
    for(int k=0;k<3;k++){ model[k]=X[k]*sc; model[4+k]=Y[k]*sc; model[8+k]=Z[k]*sc; }
    model[12]=Rd->px+Y[0]*up; model[13]=Rd->py+Y[1]*up; model[14]=Rd->pz+Y[2]*up; model[15]=1.f;
    g_castFloorY[s]=Rd->py;
    return 1; }
/* FOR THE LENSES (camtrack / campan / camorbit read a body through locoWhere): the rider's CONTINUOUS spot, up the
 * wall and in the air included. A lens aims at spot + (0,aimh,0), which is an upright body's torso -- so the spot
 * handed over is the one an UPRIGHT body would stand on with its torso where hers is (PIPE_TORSO along the
 * surface's up): on the flat that is the contact itself, on the vert it is 1.6 u toward the pipe's middle and 1.6 u
 * down. A steady heading (the pipe's +C -- a rider reverses at every wall) and her speed across the axis along
 * it, so the lens's lead points the way she is actually going. */
static int pipeWhere(int s,float*x,float*y,float*z,float*trav,float*vel){
    if(s<0||s>=SCN_MAXCAST||!g_prd[s].on||g_prd[s].pi<0||g_prd[s].pi>=g_nPipe) return 0;
    const PipeRide*Rd=&g_prd[s]; const HalfPipe*H=&g_pipe[Rd->pi];
    const float Y0[3]={0.f,1.f,0.f}; float N[3]; pipeRotA(H->ax,Rd->pitch,Y0,N);
    *x=Rd->x+N[0]*PIPE_TORSO; *y=Rd->y+(N[1]-1.f)*PIPE_TORSO; *z=Rd->z+N[2]*PIPE_TORSO;
    *trav=pipeHdg(H,1); *vel=Rd->vu;
    return 1; }

/* one level of the Barry-Goldman pyramid; a zero knot interval means the two points coincide */
static void crLerp(float*o,const float*a,const float*b,float ta,float tb,float t){
    float d=tb-ta;
    if(d<1e-6f){ o[0]=b[0]; o[1]=b[1]; return; }
    float wa=(tb-t)/d, wb=(t-ta)/d;
    o[0]=wa*a[0]+wb*b[0]; o[1]=wa*a[1]+wb*b[1]; }
static float crKnot(const float*a,const float*b){   /* |b-a|^0.5 -- centripetal */
    float dx=b[0]-a[0], dz=b[1]-a[1]; return sqrtf(sqrtf(dx*dx+dz*dz)); }
/* the point u (0..1) of the way from P1 to P2 */
static void crPoint(const float*P0,const float*P1,const float*P2,const float*P3,float u,float*o){
    float t1=crKnot(P0,P1), t2=t1+crKnot(P1,P2), t3=t2+crKnot(P2,P3);
    float t=t1+(t2-t1)*u, A1[2],A2[2],A3[2],B1[2],B2[2];
    crLerp(A1,P0,P1,0.f,t1,t); crLerp(A2,P1,P2,t1,t2,t); crLerp(A3,P2,P3,t2,t3,t);
    crLerp(B1,A1,A2,0.f,t2,t); crLerp(B2,A2,A3,t1,t3,t);
    crLerp(o,B1,B2,t1,t2,t); }

/* Builds slot s's path off `spec` -- a mark list, or `@name` for a set's `route` -- from C0 (when the body has a
 * spot) through every mark the set carries. Returns the marks found; *len = arc length; *placed = 1 when C0 was
 * not used. `rolling`/`trav`: the body is already moving, that way.
 * THE NEIGHBOURS. Both ends are duplicated (the agreed definition), EXCEPT
 *   - a CLOSED route's seam, which WRAPS: the segment into its last mark looks on to the mark after its first,
 *     and a lap that starts ON its first mark looks back to the mark before its last -- the join from the last
 *     mark to the first is then the same curve from both sides, so laps are seamless;
 *   - a body already ROLLING, whose start looks back along its own travel (a phantom point one chord behind
 *     it), so a hand-off from one path to the next carries its direction instead of kinking -- and when the
 *     path turns more than ~20 degrees away from that travel, a LEAD-IN point one carve radius ahead
 *     (v^2 / (g tan 18), at most half way to the first mark), so it CARVES round instead of hooking on the
 *     spot (measured: a hand-off to a mark behind turned 72 degrees in one exposure without it).
 * A CLOSED route needs nothing else: C0 on its first mark merges into it (points closer than 0.05 u are one),
 * so no knot interval is zero, and braking reads the ARC length left, never the chord from C0 to the last mark.
 * THE SAMPLES. Dense (LOCO_PSEG a segment) for the arc length, then RESAMPLED every LOCO_PDS u: a body rolls on
 * even spacing, and heading and curvature are differenced over it. ⛔ Dense samples bunch at a duplicated end
 * (zero parametric speed), and a curvature taken over those millimetres is float noise the carve would show. */
/* ⭐ (10-22) THE ROLL. A body's whole placement -- after its mark, its walk, its lean and its half-pipe -- turned about
 * an axis through a point, by an angle that runs at a rate: hroll= proll= p2roll= p3roll=ax,ay,az,dx,dy,dz,degps[,deg0]
 * (sticky; `off` hands it back). Viktor "trotted around the barrel like a veteran" (scene 2.2): he runs in place on the
 * mark at the drum's floor and the roll carries him round its axis at his own speed over its radius, head always to the
 * hub -- the lens, still in the drum's frame, sees him go up the wall and over the top. The rotation is applied to the
 * matrix the body is DRAWN with, and to its spot for the tracking lenses (locoWhere): a pan follows him round. */
typedef struct { int on; float a[3], d[3], rate, a0, t0; char spec[96]; } CastRoll;
static CastRoll g_castRoll[SCN_MAXCAST];
static void rollModel(int s,float*model){
    if(s<0||s>=SCN_MAXCAST||!g_castRoll[s].on) return;
    CastRoll*R=&g_castRoll[s];
    float th=R->a0+R->rate*(scnBodyT()-R->t0);
    float c=cosf(th), sn=sinf(th), C1=1.f-c, x=R->d[0], y=R->d[1], z=R->d[2];
    float M[9]={ c+x*x*C1,   y*x*C1+z*sn, z*x*C1-y*sn,      /* Rodrigues, column-major */
                 x*y*C1-z*sn, c+y*y*C1,   z*y*C1+x*sn,
                 x*z*C1+y*sn, y*z*C1-x*sn, c+z*z*C1 };
    for(int col=0;col<3;col++){ float v[3]={model[col*4],model[col*4+1],model[col*4+2]};
        for(int r=0;r<3;r++) model[col*4+r]=M[r]*v[0]+M[3+r]*v[1]+M[6+r]*v[2]; }
    float p[3]={model[12]-R->a[0],model[13]-R->a[1],model[14]-R->a[2]};
    for(int r=0;r<3;r++) model[12+r]=R->a[r]+M[r]*p[0]+M[3+r]*p[1]+M[6+r]*p[2]; }
/* the same turn applied to a point: the spot a tracking lens follows */
static void rollPoint(int s,float*pt){
    float m[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, pt[0],pt[1],pt[2],1};
    rollModel(s,m); pt[0]=m[12]; pt[1]=m[13]; pt[2]=m[14]; }
static int locoWhere(int s,float*x,float*y,float*z,float*trav,float*vel);   /* below: where a body IS (a ragdoll's hips included) */
static int locoPathBuild(int s,const char*spec,int haveC0,float c0x,float c0z,float vel,float trav,float*len,int*placed){
    LocoPath*LP=&g_lpath[s];
    static float mk[LOCO_PATHMAX][2], pts[LOCO_PATHMAX+2][2];
    static float DX[LOCO_PDENSE], DZ[LOCO_PDENSE], DS[LOCO_PDENSE];
    int nm=0, np=0, dn=0, startIsFirst=1, lead=0;
    LP->n=0; LP->closed=0; LP->ds=0.f; LP->len=0.f; LP->end[0]=0; *len=0.f; *placed=0;
    const char*list=spec;
    if(!strncmp(spec,"@around:",8)){
        /* ⭐ ep 13: A ROUTE AROUND A BODY. `@around:<slot>[,<radius>[,<points>]]` -- a closed circle of marks round
         * where that slot IS right now (its hips, when it is a ragdoll on the floor), entered at the point nearest
         * this body and run the way it is already travelling. Rebuilt every lap from LP->spec, so it re-centres on a
         * body that moves. Built for the filmer circling her fallen friend: a fixed route has to know where a slam
         * will end, and two takes of the same fall came to rest 8 u apart. */
        int os9=1, np9=8; float r9=3.2f;
        sscanf(spec+8,"%d,%f,%d",&os9,&r9,&np9);
        if(os9<0||os9>=SCN_BODIES) os9=0;
        if(r9<0.6f) r9=0.6f;
        if(np9<4) np9=4;
        if(np9>LOCO_PATHMAX-1) np9=LOCO_PATHMAX-1;
        float cx9,cy9,cz9,ct9,cv9;
        if(!locoWhere(os9,&cx9,&cy9,&cz9,&ct9,&cv9)){ printf("[path] slot %d: '%s' -- slot %d is nowhere yet\n",s,spec,os9); fflush(stdout); return 0; }
        float a0=haveC0?atan2f(c0x-cx9,c0z-cz9):0.f, dir9=1.f;
        if(vel>0.05f){ float tx=cosf(a0), tz=-sinf(a0); if(tx*sinf(trav)+tz*cosf(trav)<0.f) dir9=-1.f; }
        for(int k=0;k<=np9;k++){ float a=a0+dir9*(float)k*6.2831853f/(float)np9;
            mk[nm][0]=cx9+sinf(a)*r9; mk[nm][1]=cz9+cosf(a)*r9; nm++; }
        snprintf(LP->end,sizeof LP->end,"%s",g_castMark[s]);   /* the mark she was placed by stays hers: a circle is not a mark */
        printf("[path] slot %d: a %d-point circle of radius %.1f round slot %d at (%.2f,%.2f), %s from above\n",
               s,np9,r9,os9,cx9,cz9,dir9>0.f?"clockwise":"anticlockwise"); fflush(stdout); }
    else {
    if(spec[0]=='@'){ SetRoute*R=routeFind(spec+1);
        if(!R){ printf("[path] slot %d: route '%s' NOT FOUND in the loaded set (`route <name> <mark> ...`)\n",s,spec+1); fflush(stdout); return 0; }
        list=R->spec; }
    for(const char*p=list;*p;){
        char nmb[32]; int k=0;
        while(*p&&*p!=','){ if(k<31) nmb[k++]=*p; p++; }
        nmb[k]=0; if(*p==',') p++;
        if(!nmb[0]) continue;
        SetMark*MK=markFind(nmb);
        if(!MK){ printf("[path] slot %d: mark '%s' NOT FOUND in the loaded set -- skipped\n",s,nmb); fflush(stdout); continue; }
        if(nm>=LOCO_PATHMAX){ printf("[path] slot %d: more than %d marks -- '%s' and after REFUSED (LOCO_PATHMAX)\n",s,LOCO_PATHMAX,nmb); fflush(stdout); break; }
        mk[nm][0]=MK->x; mk[nm][1]=MK->z; nm++; snprintf(LP->end,sizeof LP->end,"%s",MK->name); }
    }
    if(!nm) return 0;
    { float cx=mk[0][0]-mk[nm-1][0], cz=mk[0][1]-mk[nm-1][1]; LP->closed=(nm>=3&&cx*cx+cz*cz<0.05f*0.05f); }
    if(haveC0&&(mk[0][0]-c0x)*(mk[0][0]-c0x)+(mk[0][1]-c0z)*(mk[0][1]-c0z)<=30.f*30.f){ pts[0][0]=c0x; pts[0][1]=c0z; np=1; startIsFirst=0; }
    else *placed=1;
    for(int i=0;i<nm;i++){
        if(np>0){ float dx=mk[i][0]-pts[np-1][0], dz=mk[i][1]-pts[np-1][1];
            if(dx*dx+dz*dz<0.05f*0.05f){
                if(i==0) startIsFirst=1;                         /* C0 stands on the first mark: one point */
                if(i==nm-1){ pts[np-1][0]=mk[i][0]; pts[np-1][1]=mk[i][1]; }
                continue; } }
        pts[np][0]=mk[i][0]; pts[np][1]=mk[i][1]; np++; }
    if(np<2){ LP->x[0]=pts[0][0]; LP->z[0]=pts[0][1]; LP->h[0]=0.f; LP->k[0]=0.f; LP->n=1; return nm; }
    if(vel>0.05f&&!startIsFirst){   /* the lead-in: carve into a path that turns away from the travel */
        float tdx=sinf(trav), tdz=cosf(trav), tx=pts[1][0]-pts[0][0], tz=pts[1][1]-pts[0][1], tl=sqrtf(tx*tx+tz*tz);
        float cosA=(tl>1e-4f)?(tx*tdx+tz*tdz)/tl:1.f, r=vel*vel/(LOCO_G*0.3249197f);
        if(r>0.5f*tl) r=0.5f*tl;
        if(cosA<0.94f&&r>0.3f){
            memmove(pts+2,pts+1,sizeof(pts[0])*(size_t)(np-1));
            pts[1][0]=pts[0][0]+tdx*r; pts[1][1]=pts[0][1]+tdz*r; np++; lead=1; } }
    float ph[2]; const float*pre=pts[0], *post=pts[np-1];
    if(LP->closed&&startIsFirst&&np>=3) pre=pts[np-2];
    else if(vel>0.05f){ float cx=pts[1][0]-pts[0][0], cz=pts[1][1]-pts[0][1], cl=sqrtf(cx*cx+cz*cz);
        if(cl<0.5f) cl=0.5f;
        ph[0]=pts[0][0]-sinf(trav)*cl; ph[1]=pts[0][1]-cosf(trav)*cl; pre=ph; }
    int f1=startIsFirst?0:(lead?2:1);   /* where the route's first mark sits in pts */
    if(LP->closed&&f1+1<np-1) post=pts[f1+1];
    /* 1. dense, for the arc length */
    for(int j=0;j<np-1;j++){
        const float*P0=(j>0)?pts[j-1]:pre, *P1=pts[j], *P2=pts[j+1], *P3=(j+2<np)?pts[j+2]:post;
        for(int i=0;i<LOCO_PSEG;i++){ float o[2]; crPoint(P0,P1,P2,P3,(float)i/(float)LOCO_PSEG,o);
            DX[dn]=o[0]; DZ[dn]=o[1]; dn++; } }
    DX[dn]=pts[np-1][0]; DZ[dn]=pts[np-1][1]; dn++;
    /* ⭐ THE LINE IS PUSHED OUT OF THE SET. A rider is ~0.34 u across at the hips; a board sticks out further,
     * so the line keeps 0.42 u. The ENDS are left alone: a mark inside a feature is a staging decision (a body
     * standing on a bank, a board set down on a ledge), and moving someone's mark out from under them is worse
     * than the clip. */
    { int moved=0; float worst=0.f;
      for(int i=1;i<dn-1;i++){ float x=DX[i], z=DZ[i], mv=0.f;
          if(colSdfResolve(&x,&z,0.42f,&mv)&&mv>1e-4f){
              DX[i]=x; DZ[i]=z; moved++; if(mv>worst) worst=mv; } }
      if(moved){ printf("[path] slot %d: %d of %d samples were inside the set -- the line is pushed out (deepest %.2f u)\n",
                        s,moved,dn,worst); fflush(stdout); } }
    DS[0]=0.f;
    for(int i=1;i<dn;i++){ float dx=DX[i]-DX[i-1], dz=DZ[i]-DZ[i-1]; DS[i]=DS[i-1]+sqrtf(dx*dx+dz*dz); }
    float total=DS[dn-1];
    /* 2. even, for rolling: segs equal steps of ds, the last sample exactly on the end */
    int segs=(int)ceilf(total/LOCO_PDS);
    if(segs<1) segs=1;
    if(segs>LOCO_PU-1) segs=LOCO_PU-1;
    float ds=total/(float)segs;
    int jj=0;
    for(int u=0;u<=segs;u++){
        float su=(u==segs)?total:(float)u*ds;
        while(jj<dn-2&&DS[jj+1]<su) jj++;
        float seg=DS[jj+1]-DS[jj], f=(seg>1e-9f)?(su-DS[jj])/seg:0.f;
        if(f<0.f) f=0.f;
        if(f>1.f) f=1.f;
        LP->x[u]=DX[jj]+(DX[jj+1]-DX[jj])*f; LP->z[u]=DZ[jj]+(DZ[jj+1]-DZ[jj])*f; }
    LP->n=segs+1; LP->ds=ds; LP->len=total;
    /* 3. heading and signed curvature (dheading/ds), central differences over the even samples */
    for(int u=0;u<LP->n;u++){ int a=(u>0)?u-1:0, b=(u<LP->n-1)?u+1:LP->n-1;
        float dx=LP->x[b]-LP->x[a], dz=LP->z[b]-LP->z[a];
        LP->h[u]=(dx*dx+dz*dz>1e-12f)?atan2f(dx,dz):((u>0)?LP->h[u-1]:0.f); }
    for(int u=0;u<LP->n;u++){ int a=(u>0)?u-1:0, b=(u<LP->n-1)?u+1:LP->n-1;
        LP->k[u]=(b>a)?locoWrap(LP->h[b]-LP->h[a])/((float)(b-a)*ds):0.f; }
    /* ⭐ a CLOSED route's SEAM heading, off the curve itself on both sides of its first mark: one-sided
     * differences at the two ends disagree by a sample's worth of the corner (measured 2.7 degrees), and a lap
     * rebuilt from here must start on exactly the heading this one ends on */
    if(LP->closed&&np>=3){
        int i2=f1+1, i3=f1+2; float qa[2], qb[2];
        if(i2>np-1) i2=np-1;
        if(i3>np-1) i3=np-1;
        crPoint(pts[np-3],pts[np-2],pts[np-1],post,0.999f,qa);
        crPoint(pts[np-2],pts[np-1],pts[i2],pts[i3],0.001f,qb);
        float sx=qb[0]-qa[0], sz=qb[1]-qa[1];
        if(sx*sx+sz*sz>1e-12f){ float hs=atan2f(sx,sz); LP->h[LP->n-1]=hs; if(startIsFirst) LP->h[0]=hs; } }
    *len=total;
    return nm; }
/* where the path is `d` u along: position, heading, curvature */
static void locoPathAt(int s,float d,float*x,float*z,float*h,float*k){
    LocoPath*LP=&g_lpath[s];
    int e=LP->n-1;
    if(e<=0||d<=0.f){ *x=LP->x[0]; *z=LP->z[0]; *h=LP->h[0]; *k=(e>0)?LP->k[0]:0.f; return; }
    if(d>=LP->len){ *x=LP->x[e]; *z=LP->z[e]; *h=LP->h[e]; *k=LP->k[e]; return; }
    float fu=d/LP->ds; int i=(int)fu;
    if(i>e-1) i=e-1;
    float f=fu-(float)i;
    if(f>1.f) f=1.f;
    *x=LP->x[i]+(LP->x[i+1]-LP->x[i])*f;
    *z=LP->z[i]+(LP->z[i+1]-LP->z[i])*f;
    *h=LP->h[i]+locoWrap(LP->h[i+1]-LP->h[i])*f;
    *k=LP->k[i]+(LP->k[i+1]-LP->k[i])*f; }
/* how long the trapezoid takes over `len` from a standstill -- what the log promises */
static float locoPathTime(float len,float spd){
    float dA=spd*spd/(2.f*LOCO_PACC), dB=spd*spd/(2.f*LOCO_PBRK);
    if(dA+dB<=len) return spd/LOCO_PACC+spd/LOCO_PBRK+(len-dA-dB)/spd;
    float vp=sqrtf(len/(1.f/(2.f*LOCO_PACC)+1.f/(2.f*LOCO_PBRK)));
    return vp/LOCO_PACC+vp/LOCO_PBRK; }
/* LAND a path: on its last mark, facing along its last tangent, published at once. A snap=1 line does this
 * to every path, starting or rolling -- the jump cut lands every walk on its mark, and a path's mark is its
 * last one (the scene tick's snap reset clears `on` right after, and `hold` is what survives it). */
static void locoPathLand(int s,Loco*L,int snap){
    LocoPath*LP=&g_lpath[s]; int e=(LP->n>0)?LP->n-1:0;
    L->x=LP->x[e]; L->z=LP->z[e]; L->v=0.f; L->ps=L->plen; L->bank=0.f;
    L->on=0; L->path=0; L->hold=1; L->lapping=0;
    if(LP->n>1){ L->trav=LP->h[e]; L->yaw=LP->h[e]+locoRideOff(s); }
    L->px=L->x; L->pz=L->z; L->pyaw=L->yaw; L->pon=L->on; L->ptrav=L->trav; L->pbank=L->bank; L->phold=L->hold;
    printf("[path] slot %d arrived at %s%s\n",s,LP->end,snap?" (snap)":""); fflush(stdout); }
/* a path run STARTS (its line was staged, or the path before it ran out): build it from where the body stands and
 * start rolling -- or land, on snap=1. ⭐ SPEED CARRIES ACROSS PATHS: a body already rolling keeps its speed and
 * ramps from there, so a rider handed from one path to the next never stops in between. */
static void locoPathStart(int s,Loco*L,int snap,char*wasMark,const char*spec,int seg){
    int live=(L->on||L->hold), rolling=(L->on&&L->v>0.05f), have=1;
    float c0x=0.f, c0z=0.f, yaw0=L->yaw;
    if(live){ c0x=L->x; c0z=L->z; }
    else if(g_castAtOK[s]){ c0x=g_castAt[s][0]; c0z=g_castAt[s][1]; yaw0=g_castAt[s][2]; }
    else have=0;
    float len=0.f; int placed=0;
    int nm=locoPathBuild(s,spec,have,c0x,c0z,rolling?L->v:0.f,L->trav,&len,&placed);
    LocoPath*LP=&g_lpath[s];
    if(!nm){ printf("[path] slot %d: '%s' names no mark in the loaded set -- it stays where it is\n",s,spec); fflush(stdout); return; }
    snprintf(LP->spec,sizeof LP->spec,"%s",spec);   /* what a lap rebuilds from */
    float spd=(g_castSpd[s]>0.f)?g_castSpd[s]:LOCO_PSPD, off9=locoRideOff(s);
    printf("[path] slot %d: %d marks, %.2f u, ~%.1f s  (%s; from %.2f,%.2f%s; ends on '%s'; %.1f u/s%s%s%s)\n",
           s,nm,len,locoPathTime(len,spd),(spec[0]=='@')?spec:"a mark list",LP->x[0],LP->z[0],
           placed?" -- PLACED on the first mark":"",LP->end,spd,locoIsRider(s)?"; rides":"",
           LP->closed?"; a CLOSED loop":"",rolling?"; rolling in":"");
    fflush(stdout);
    /* the path ENDS on its last mark, and that is now the mark this body is placed by */
    snprintf(g_castMark[s],32,"%s",LP->end); snprintf(wasMark,32,"%s",LP->end);
    if(!rolling) L->v=0.f;
    if(placed||!live) L->yaw=placed?(LP->h[0]+off9):yaw0;
    L->x=LP->x[0]; L->z=LP->z[0]; L->trav=LP->h[0];
    L->path=1; L->ps=0.f; L->plen=len; L->hold=0; L->on=1; L->lapping=0; L->laps=0; L->loopSaid=0; L->seg=seg;
    if(snap||len<0.03f){ locoPathLand(s,L,snap); return; }   /* snap=1 (or nowhere to go): LAND on the last mark */
    /* a body standing still is published AT ONCE, like a changed mark: it starts where it was drawn. A rolling
     * one is NOT -- its own spot runs an exposure ahead of what is drawn, and ep 9's law is that place and pose
     * change on the same frame, so it publishes on its next exposure like every other step. */
    if(!live){ L->px=L->x; L->pz=L->z; L->pyaw=L->yaw; L->pon=L->on; L->ptrav=L->trav; L->pbank=L->bank; L->phold=L->hold; } }
/* ⭐ hloop: a lap of a closed route is over. The same route is built again from where the body is -- its first
 * mark, which C0 merges into, so the seam WRAPS -- and it rolls straight on. 0 = cannot. */
static int locoPathLap(int s,Loco*L){
    LocoPath*LP=&g_lpath[s];
    float ex=(LP->n>0)?LP->x[LP->n-1]:L->x, ez=(LP->n>0)?LP->z[LP->n-1]:L->z, len=0.f; int placed=0;
    int nm=locoPathBuild(s,LP->spec,1,ex,ez,L->v,L->trav,&len,&placed);
    if(!nm||len<0.03f){ printf("[path] slot %d: '%s' could not be built for another lap -- it stops\n",s,LP->spec); fflush(stdout); return 0; }
    L->ps=0.f; L->plen=len; L->laps++;
    printf("[path] slot %d lap %d (%.2f u, %.2f u/s)\n",s,L->laps+1,len,L->v); fflush(stdout);
    return 1; }
/* one exposure of a path: speed off the trapezoid, place by arc length, facing off the tangent. Returns -1 -- or,
 * when the path ran out while the NEXT path's line is still to come, how far past its end this step went, so
 * locoTick hands the body straight on to that path at speed. */
static float locoPathRoll(int s,Loco*L,float st8){
    LocoPath*LP=&g_lpath[s];
    if(LP->n<2){ L->on=0; L->path=0; L->hold=1; L->lapping=0; return -1.f; }
    float off9=locoRideOff(s), spd=(g_castSpd[s]>0.f)?g_castSpd[s]:LOCO_PSPD;
    float left=L->plen-L->ps, ret=-1.f;
    /* ⭐ hloop=1 on a CLOSED route: circle at spd with NO braking until a line changes the path or says hloop=0.
     * Let go, it brakes as soon as the ground left on this lap can stop it -- otherwise it goes round once more. */
    int inForce=(L->seg>=0&&L->seg==g_castPathSeg[s]);
    int want=LP->closed&&g_castLoop[s]&&inForce;
    if(g_castLoop[s]&&inForce&&!LP->closed&&!L->loopSaid){ L->loopSaid=1;
        printf("[path] slot %d: hloop=1 but '%s' is not a closed route (its last mark is not its first) -- it stops at the end\n",s,LP->spec); fflush(stdout); }
    if(want) L->lapping=1;
    else if(L->lapping&&left>L->v*L->v/(2.f*LOCO_PBRK)+0.05f) L->lapping=0;
    /* ⭐ and a path whose SUCCESSOR's line is still to come does not brake at all */
    int nx=(s<4&&L->seg>=0&&L->seg<g_scnN)?g_scnL[L->seg].pnext[s]:-1;   /* pnext holds slots 0..3 */
    int succ=(!L->lapping&&nx>=0&&nx>g_locoSeg[s]);
    if(left>0.03f||L->lapping||succ){
        /* more than ~50 degrees off the line and standing still: turn first, as a mark walk does */
        if(fabsf(locoWrap(L->trav+off9-L->yaw))>0.87f&&L->v<0.05f) L->v=0.f;
        else { L->v=locoProfile(L->v,(L->lapping||succ)?1e4f:left,st8,spd,LOCO_PACC,LOCO_PBRK);
               float stp=L->v*st8;
               if(L->lapping) while(stp>0.f){ float room=L->plen-L->ps;
                   if(stp<room){ L->ps+=stp; stp=0.f; }
                   else { stp-=room; L->ps=L->plen; if(!locoPathLap(s,L)){ L->lapping=0; break; } } }
               else if(succ&&stp>=left){ ret=stp-left; L->ps=L->plen; }
               else { if(stp>left) stp=left; L->ps+=stp; } }
        float x9,z9,h9,k9; locoPathAt(s,L->ps,&x9,&z9,&h9,&k9);
        L->x=x9; L->z=z9; L->trav=h9;
        /* a rider faces ACROSS the tangent, left side leading; a walker along it. A tight corner taken at speed
         * turns faster than a walk's LOCO_TURN, so the rate grows with v*k. */
        float dy=locoWrap(h9+off9-L->yaw), mx=(LOCO_TURN+L->v*fabsf(k9))*st8;
        L->yaw+=(dy>mx)?mx:((dy<-mx)?-mx:dy);
        return ret; }
    int e=LP->n-1; L->x=LP->x[e]; L->z=LP->z[e]; L->trav=LP->h[e]; L->v=0.f; L->ps=L->plen;
    /* ...and at the end it KEEPS the last tangent's facing -- never the mark's yaw */
    float dy=locoWrap(LP->h[e]+off9-L->yaw), mx=LOCO_TURN*st8;
    L->yaw+=(dy>mx)?mx:((dy<-mx)?-mx:dy);
    if(fabsf(dy)<0.02f){ L->yaw=LP->h[e]+off9; L->on=0; L->path=0; L->hold=1;
        printf("[path] slot %d arrived at %s%s\n",s,LP->end,L->laps?" (after its laps)":""); fflush(stdout); }
    return -1.f; }
static int rdActive(int slot);   /* the ragdoll, defined far below: a limp body is not walked anywhere */
static void locoTick(float st8,int snap){
    static char was[SCN_MAXCAST][32];
    if(g_locoPathReset){ g_locoPathReset=0;
        for(int s=0;s<SCN_MAXCAST;s++){ Loco*L=&g_loco[s]; g_locoSeg[s]=-1; g_locoEarly[s]=-1; L->path=L->hold=L->phold=L->lapping=0; L->seg=-1; L->bank=L->pbank=0.f;
            g_prd[s].on=g_prd[s].pon=0; g_pipePend[s][0]=0; } }   /* ⭐ ep 11: and drops in again */
    for(int s=0;s<SCN_MAXCAST;s++){
        if(s>0&&!g_cast[s].pk) continue;
        Loco*L=&g_loco[s];
        if(rdActive(s)){ snprintf(was[s],32,"%.31s",g_castMark[s]); g_locoSeg[s]=g_castPathSeg[s];
                         L->on=0; L->path=0; L->hold=1; L->spd=0.f; continue; }   /* ⛔ she is on the floor; nothing moves her */
        /* 0. ⭐ ep 10: the script gave this body a PATH. A new RUN of values starts it from where the body stands;
         *    the same value carried onto the next line is the same run and does not restart it. Runs start in
         *    order and once -- one already handed on early is not started again. Before the mark check,
         *    because it moves the body's mark to the path's last one. */
        if(g_castPath[s][0]&&g_castPathSeg[s]>g_locoSeg[s]){
            g_locoSeg[s]=g_castPathSeg[s];
            if(strcmp(g_castPath[s],"off")) locoPathStart(s,L,snap,was[s],g_castPath[s],g_castPathSeg[s]); }
        /* 1. the script moved this body's mark: walk there from where it was last drawn */
        if(strcmp(was[s],g_castMark[s])){
            snprintf(was[s],32,"%.31s",g_castMark[s]);
            SetMark*MK=markFind(g_castMark[s]);
            L->on=0; L->path=0; L->hold=0; L->lapping=0; L->rn=0; L->ri=0; L->stuck=0.f;   /* ⭐ ep 10: a new mark ends a path, and lets go of where one stopped */
            if(MK&&!snap&&g_castAtOK[s]){
                float dx=MK->x-g_castAt[s][0], dz=MK->z-g_castAt[s][1], d=sqrtf(dx*dx+dz*dz);
                if(d>30.f){ printf("[walk] slot %d: %.1fu to '%s' is not a walk -- placed\n",s,d,MK->name); fflush(stdout); }
                else if(d>0.05f){ L->on=1; L->x=g_castAt[s][0]; L->z=g_castAt[s][1]; L->yaw=g_castAt[s][2]; L->v=0.f; L->trav=atan2f(dx,dz);
                    printf("[walk] t %.2f slot %d -> mark '%s': %.2fu from (%.2f,%.2f) to (%.2f,%.2f)\n",
                           g_scnT,s,MK->name,d,L->x,L->z,MK->x,MK->z); fflush(stdout);
                    L->rn=wrrtRoute(s,L->x,L->z,MK->x,MK->z,L->rx,L->rz,64); L->ri=0; } }
            /* a changed mark publishes AT ONCE: a walk starts where the body already stands, and a
             * placed mark lands on the cut -- neither may wait for the next exposure */
            L->px=L->x; L->pz=L->z; L->pyaw=L->yaw; L->pon=L->on; L->ptrav=L->trav; L->phold=L->hold; }
        /* 2. walk it (⭐ ep 10: a path rolls in 2b instead) */
        if(L->on&&!L->path&&st8>1e-6f){
            SetMark*MK=markFind(g_castMark[s]);
            if(!MK) L->on=0;
            else {
                float dx=MK->x-L->x, dz=MK->z-L->z, left=sqrtf(dx*dx+dz*dz);
                /* ⭐ A ROUTED WALK heads for its next corner; a corner reached hands on to the one after */
                while(L->ri<L->rn){ float cx=L->rx[L->ri]-L->x, cz=L->rz[L->ri]-L->z; float cl=sqrtf(cx*cx+cz*cz);
                    if(cl<0.12f){ L->ri++; continue; }
                    float rest=0.f; { float px9=L->rx[L->ri], pz9=L->rz[L->ri];
                        for(int k9=L->ri+1;k9<L->rn;k9++){ rest+=sqrtf((L->rx[k9]-px9)*(L->rx[k9]-px9)+(L->rz[k9]-pz9)*(L->rz[k9]-pz9)); px9=L->rx[k9]; pz9=L->rz[k9]; }
                        rest+=sqrtf((MK->x-px9)*(MK->x-px9)+(MK->z-pz9)*(MK->z-pz9)); }
                    dx=cx; dz=cz; left=cl+rest; if(left<0.031f) left=0.031f;
                    { float sc9=cl/left; dx/=(sc9>1e-4f?sc9:1e-4f); dz/=(sc9>1e-4f?sc9:1e-4f); }   /* keep dx/left the corner's direction */
                    break; }
                int ride9=locoIsRider(s);
                /* ⭐ A RIDER TRAVELS ALONG ITS BOARD, not along its chest -- a skater stands sideways.
                 * @deck runs the board across the body (-90); a wheeled rig says which way its own
                 * board runs (wheelYaw, measured at load: Kickflip's is along his Z, so 0). */
                float off9=locoRideOff(s);   /* ⭐ ep 10: the one rule, shared with the paths and the lens */
                float want=(left>0.03f)?(atan2f(dx,dz)+off9):MK->yaw*0.0174533f;
                float dy=want-L->yaw;
                while(dy> 3.14159265f) dy-=6.28318531f;
                while(dy<-3.14159265f) dy+=6.28318531f;
                float mx=LOCO_TURN*st8;
                L->yaw+=(dy>mx)?mx:((dy<-mx)?-mx:dy);
                if(left>0.03f){
                    /* more than ~50 degrees off the line: turn first -- nobody walks backwards to a mark */
                    if(fabsf(dy)>0.87f) L->v=0.f;
                    else { L->v=locoSpeed(L->v,left,st8,ride9?LOCO_RIDE:LOCO_CRUISE); if(L->v<0.f) L->v=0.f;
                           float stp=L->v*st8; if(stp>left) stp=left;
                           float bx9=L->x, bz9=L->z;
                           L->x+=dx/left*stp; L->z+=dz/left*stp; L->trav=atan2f(dx,dz);
                           /* ⭐ THE WORLD IS SOLID while she is travelling; the final approach is the mark's own
                            * (a chair's or a bed's mark stands inside its furniture on purpose) */
                           { float mx9=MK->x-L->x, mz9=MK->z-L->z; if(mx9*mx9+mz9*mz9>0.64f) colResolve(&L->x,&L->z,0.34f); }
                           /* ⭐ (10-22) A WALK THAT CANNOT GET THERE IS PLACED, LOUDLY. With no way round the furniture
                            * the walk goes straight, and the collider holds the walker against the first thing in the
                            * way for the rest of the scene (Wanda outside the freezatorium's shut doors, Viktor at the
                            * corner of the breakfast table). A third of the step for 0.6 s of walking is stuck. */
                           { float mv9=sqrtf((L->x-bx9)*(L->x-bx9)+(L->z-bz9)*(L->z-bz9));
                             if(stp>1e-4f&&mv9<0.33f*stp) L->stuck+=st8; else L->stuck=0.f;
                             if(L->stuck>0.6f){
                                 float sl9=sqrtf((MK->x-L->x)*(MK->x-L->x)+(MK->z-L->z)*(MK->z-L->z));
                                 printf("[walk] t %.2f slot %d STUCK %.2fu short of '%s' at (%.2f,%.2f) -- the furniture holds it; PLACED on the mark\n",
                                        g_scnT,s,sl9,MK->name,L->x,L->z); fflush(stdout);
                                 L->x=MK->x; L->z=MK->z; L->yaw=MK->yaw*0.0174533f; L->v=0.f; L->on=0; L->rn=0; L->ri=0; L->stuck=0.f; } } } }
                else { L->x=MK->x; L->z=MK->z; L->v=0.f;
                       if(fabsf(dy)<0.02f){ L->on=0; printf("[walk] t %.2f slot %d arrived on '%s'\n",g_scnT,s,MK->name);   /* (10-23) timed: foley.py */ fflush(stdout); } } } }
        /* 2b. ⭐ ep 10: ROLL IT ALONG ITS PATH, by arc length -- or land it, on a snap=1 line */
        if(L->on&&L->path&&snap) locoPathLand(s,L,1);
        else if(L->on&&L->path&&st8>1e-6f){
            float over=locoPathRoll(s,L,st8);
            int nx=(over>=0.f&&s<4&&L->seg>=0&&L->seg<g_scnN)?g_scnL[L->seg].pnext[s]:-1;
            if(nx>=0){   /* ⭐ it ran out before the next path's line: hand it straight on, at speed */
                /* ⭐ ep 13: A PATH HANDED ON EARLY IS THAT LINE'S PATH -- its speed, its loop, its run. Started at the
                 * old run's speed under the old line's loop flag, the filmer's 2.4 u/s orbit began at 6.4, was not "in
                 * force" until its own line was staged, STOPPED after one lap -- and a stopped run with the same value
                 * never restarts. The next line's spd/loop are adopted here and PINNED (g_locoEarly) against the lines
                 * in between, whose carried values are the run that just ran out. */
                const ScnLine*NL=&g_scnL[nx];
                if(NL->spd[s]>0.f) g_castSpd[s]=NL->spd[s];
                g_castLoop[s]=(NL->loop[s]>0); g_castPathSeg[s]=nx; g_locoEarly[s]=nx;
                printf("[path] slot %d: '%s' ran out before line %d -- '%s' starts early: rolling in at %.2f u/s, to that line's %.2f u/s%s\n",
                       s,g_lpath[s].spec,nx,NL->path[s],L->v,(g_castSpd[s]>0.f)?g_castSpd[s]:LOCO_PSPD,g_castLoop[s]?", looping":""); fflush(stdout);
                g_locoSeg[s]=nx;
                locoPathStart(s,L,0,was[s],NL->path[s],nx);
                if(L->on&&L->path&&over>0.f){ float x9,z9,h9,k9; L->ps=(over<L->plen)?over:L->plen;
                    locoPathAt(s,L->ps,&x9,&z9,&h9,&k9); L->x=x9; L->z=z9; L->trav=h9; } } }
        /* 2c. ⭐ ep 10: THE CARVE. A rider on a path banks into the curve -- atan2(v^2 k, g), clamped to 18
         *     degrees, eased with a 0.15 s time constant -- and comes back upright once it stops. */
        if(st8>1e-6f){ float bt=0.f;
            if(!g_leanOff&&L->on&&L->path&&locoIsRider(s)){ float x9,z9,h9,k9; locoPathAt(s,L->ps,&x9,&z9,&h9,&k9);
                bt=atan2f(L->v*L->v*k9,LOCO_G); if(bt>LOCO_BANKMAX) bt=LOCO_BANKMAX; if(bt<-LOCO_BANKMAX) bt=-LOCO_BANKMAX; }
            L->bank+=(bt-L->bank)*(1.f-expf(-st8/0.15f)); }
        /* 3. its speed, measured where it was DRAWN -- which is what makes the pair walk carry slot 1 */
        if(g_castAtOK[s]&&st8>1e-6f){
            float x=g_castAt[s][0], z=g_castAt[s][1];
            if(!L->winit){ L->wx=x; L->wz=z; L->winit=1; }
            float ddx=x-L->wx, ddz=z-L->wz, v=sqrtf(ddx*ddx+ddz*ddz)/st8;
            if(v>12.f) v=0.f;                                  /* a jump is not a walk */
            L->wx=x; L->wz=z;
            L->spd+=(v-L->spd)*(1.f-expf(-st8/0.12f)); }
        /* 4. ⭐ ep 11: A HALF-PIPE RIDE -- the draw's pipeModel() places the body over whatever 1-3 decided */
        if(s<4) pipeTickSlot(s,st8); }
    g_heroStageSpd=g_loco[0].spd;
    { float m=0.f; for(int s=1;s<SCN_MAXCAST;s++) if(g_loco[s].spd>m) m=g_loco[s].spd; g_partSpd=m; } }

/* ⭐ ep 10: A LENS THAT RIDES WITH A BODY -----------------------------------------------------------------
 * camtrack: eye = P + Ry(azim)(-travel) dist + (0,height,0), aim = P + (0,aimh,0). P is the slot's
 * CONTINUOUS spot (locoWhere: the loco's own, never held back for publishing, so the lens glides while the
 * puppet steps) and travel is the way it is rolling, or leads when it stands. azim 0 chases, 180 leads
 * looking back, 90 rides beside on the side it faces. campan: the pinned eye stays, the aim follows.
 * Both go through a critically damped spring at CAMTRK_W. A spring like that trails a steadily moving
 * target by 2v/w, so the target is led by exactly that. A NEW framing is a cut; the spring only smooths. */
static int rdHipsWorld(int slot,float*o);   /* defined with the ragdoll, below */
static int locoWhere(int s,float*x,float*y,float*z,float*trav,float*vel){
    if(s<0||s>=SCN_BODIES) return 0;
    if(pipeWhere(s,x,y,z,trav,vel)) return 1;   /* ⭐ ep 11: a pipe rider is where the ride has her, up the wall included */
    { float hp[3]; if(rdHipsWorld(s,hp)){ *x=hp[0]; *y=hp[1]; *z=hp[2]; *trav=g_loco[s].trav; *vel=0.f; return 1; } }
    Loco*L=&g_loco[s];
    if(L->on||L->hold){ *x=L->x; *z=L->z; *trav=L->trav; *vel=L->on?L->v:0.f; }
    else if(g_castAtOK[s]){ *x=g_castAt[s][0]; *z=g_castAt[s][1]; *trav=g_castAt[s][2]-locoRideOff(s); *vel=0.f; }
    else return 0;
    *y=g_castFloorY[s];
    if(s<SCN_MAXCAST&&g_castRoll[s].on){ float p9[3]={*x,*y,*z}; rollPoint(s,p9); *x=p9[0]; *y=p9[1]; *z=p9[2]; }   /* (10-22) */
    return 1; }
/* ⭐ WHERE THE CAMERA HAND IS, in world space -- the contact pass already publishes every body's forearm ends
 * each frame, so the filmer's wrist is free and exact. CT_FAR's far end is the right hand (the grip hand). */
static int camHandAt(int slot,float*o);
static void camSpring(float*x,float*v,float xt,float w,float dt){   /* exact step of x'' = w^2(xt-x) - 2w x' */
    float y=*x-xt, e=expf(-w*dt), j=*v+w*y;
    *x=xt+(y+j*dt)*e;
    *v=(*v-w*j*dt)*e; }
/* ⭐ ep 11: MOVING LENSES -----------------------------------------------------------------------------------
 * cammove (mode 3): eye, aim and fov eased (smoothstep) from the line's cam= to cammove= over mT lens seconds.
 * camorbit (mode 4): the lens circles a body. Its CENTRE is the tracking lens's aim -- the same continuous spot,
 * led and sprung -- but the circle itself is exact: eye = C - radius*(sin h, 0, cos h), the heading h eased from
 * deg0 to deg1, so the lens LOOKS along world heading h (camtrack's azim with the travel pinned to world +z:
 * 0 = on the body's -z side looking +z, 90 = on its -x side looking +x). A sprung circle would cut its own
 * corner and arrive late; a sprung centre only smooths the body. deg0 == deg1 is a WORLD-frame follow -- the
 * half-pipe's lens, since camtrack's azim turns with a rider's travel and a pipe rider reverses at every wall.
 * UP THE WALL (all three body lenses): the spring trails a steady climb by 2v/w exactly as it trails a roll, so
 * the height is led the same way, by its own speed through a fast pre-filter (a height stepped once an exposure
 * would spike a raw difference every step). `=h` pins the eye at world y h: the lens tilts up after her instead
 * of rising with her. Every clock here is the lens's own (dt, never the exposure clock): the look is puppets
 * stepping in front of a lens that glides. */
static float lensEase(float t){ if(t<0.f) t=0.f; if(t>1.f) t=1.f; return t*t*(3.f-2.f*t); }
/* the fov a placed lens with none is DRAWN at (the projection's own choice), so a move can ease out of "none" */
static float lensFovNone(void){ return (g_scnT>=0.f)?SHOT_FOV[g_scnShot&7]:42.f; }
static void camTrackTick(float dt){
    CamTrk*T=&g_camTrk;
    if(T->mode!=5) g_camHoldSlot=-1;
    if(!T->mode||!g_camOn) return;
    if(dt<0.f) dt=0.f;
    if(dt>0.25f) dt=0.25f;
    static int dbg=-1, dbgN=0; if(dbg<0){ const char*e=getenv("REVA_LENSDBG"); dbg=(e&&e[0]!='0'); }
    if(T->mode==3){   /* ep 11: CAMMOVE -- no body, no spring: the ease IS the smoothing */
        T->mt+=dt;
        float e=lensEase((T->mT>1e-4f)?T->mt/T->mT:1.f);
        for(int k=0;k<3;k++){ g_camPos[k]=T->mA[k]+(T->mB[k]-T->mA[k])*e; g_camAim[k]=T->mA[3+k]+(T->mB[3+k]-T->mA[3+k])*e; }
        if(T->mA[6]>0.f&&T->mB[6]>0.f) g_camFov=T->mA[6]+(T->mB[6]-T->mA[6])*e;
        if(dbg&&(++dbgN%30)==0){ printf("[lens] dbg cammove %.2f/%.2fs eye %.2f,%.2f,%.2f aim %.2f,%.2f,%.2f fov %.1f\n",
                T->mt,T->mT,g_camPos[0],g_camPos[1],g_camPos[2],g_camAim[0],g_camAim[1],g_camAim[2],g_camFov); fflush(stdout); }
        return; }
    if(T->mode==4) T->mt+=dt;   /* the sweep keeps the line's time even while its body has no spot */
    float x,y,z,tr,vel;
    if(T->mode==4&&T->wc){ x=T->wcP[0]; y=T->wcP[1]; z=T->wcP[2]; tr=0.f; vel=0.f; }   /* ⭐ 10-22: a ship has no spot */
    else if(!locoWhere(T->slot,&x,&y,&z,&tr,&vel)){
        static int said=-2;
        if(said!=T->line){ said=T->line; printf("[lens] line %d: slot %d has no spot yet -- the lens holds\n",T->line,T->slot); fflush(stdout); }
        return; }
    float dx=sinf(tr), dz=cosf(tr), ff=vel*2.f/CAMTRK_W;
    if(T->snap){ T->yF=y; T->yV=0.f; }
    else camSpring(&T->yF,&T->yV,y,CAMTRK_WY,dt);
    float fy=T->yV*2.f/CAMTRK_W;   /* ep 11: the height's lead, as ff is the roll's */
    float aim[3]={x+dx*ff, y+fy+T->aimh, z+dz*ff}, eye[3];
    if(T->mode==1){ float a=tr+T->azim*0.0174533f;
        eye[0]=x-sinf(a)*T->dist+dx*ff; eye[1]=T->habs?T->height:(y+fy+T->height); eye[2]=z-cosf(a)*T->dist+dz*ff; }
    else if(T->mode==5){   /* ⭐ THE CAMERA IN HER HAND */
        g_camHoldSlot=T->holder;
        float hp[3]; int got=camHandAt(T->holder,hp);
        if(!got){ float hx,hy2,hz,htr,hv;
            if(locoWhere(T->holder,&hx,&hy2,&hz,&htr,&hv)){ hp[0]=hx+sinf(htr)*0.25f; hp[1]=hy2+1.45f; hp[2]=hz+cosf(htr)*0.25f; got=1; } }
        if(!got) memcpy(hp,T->eS,12);
        /* the camera body sits a little in FRONT of the fist, toward what she is filming */
        float to[3]={aim[0]-hp[0],aim[1]-hp[1],aim[2]-hp[2]};
        float tl=sqrtf(to[0]*to[0]+to[1]*to[1]+to[2]*to[2]); if(tl>1e-4f){ to[0]/=tl; to[1]/=tl; to[2]/=tl; }
        for(int k=0;k<3;k++) eye[k]=hp[k]+to[k]*0.20f;
        /* ...and OUT, away from her own chest: a camera held at the wrist with the subject off to one side
         * looks straight across the operator's body. An arm's length clear of it is what a filmer does. */
        { float fx2,fy2,fz2,ftr,fv;
          if(locoWhere(T->holder,&fx2,&fy2,&fz2,&ftr,&fv)){
              float ox=hp[0]-fx2, oz=hp[2]-fz2; float ol=sqrtf(ox*ox+oz*oz);
              if(ol>1e-3f){ eye[0]+=ox/ol*0.22f; eye[2]+=oz/ol*0.22f; } } }
        eye[1]+=0.04f-T->drop; }
    else memcpy(eye,T->eye,12);
    if(T->snap){ T->snap=0; memcpy(T->eS,eye,12); memcpy(T->aS,aim,12); memset(T->eV,0,12); memset(T->aV,0,12); }
    else if(T->mode==5){
        /* ⛔ THE BOB IS THE SHOT. Springing the eye of a held camera would smooth away the very thing that makes
         * it read as footage, so the eye is raw and only the AIM is sprung -- an operator tracking through a
         * viewfinder, always a little behind the trick. */
        memcpy(T->eS,eye,12); memset(T->eV,0,12);
        for(int k=0;k<3;k++) camSpring(&T->aS[k],&T->aV[k],aim[k],CAMHOLD_W,dt); }
    else for(int k=0;k<3;k++){ camSpring(&T->eS[k],&T->eV[k],eye[k],CAMTRK_W,dt); camSpring(&T->aS[k],&T->aV[k],aim[k],CAMTRK_W,dt); }
    if(T->mode==2){ memcpy(T->eS,T->eye,12); memset(T->eV,0,12); }   /* a pan never moves its eye */
    if(T->mode==4){   /* ep 11: the circle, exact, round the sprung centre */
        float h=(T->d0+(T->d1-T->d0)*lensEase((T->mT>1e-4f)?T->mt/T->mT:1.f))*0.0174533f;
        T->eS[0]=T->aS[0]-sinf(h)*T->dist;
        T->eS[1]=T->habs?T->height:(T->aS[1]-T->aimh+T->height);
        T->eS[2]=T->aS[2]-cosf(h)*T->dist;
        memset(T->eV,0,12); }
    memcpy(g_camPos,T->eS,12); memcpy(g_camAim,T->aS,12);
    if((T->mode==1||T->mode==4||T->mode==5)&&T->fov>0.f) g_camFov=T->fov;
    if(dbg&&(++dbgN%30)==0){ printf("[lens] dbg mode %d slot %d body y %.2f vy %.2f eye %.2f,%.2f,%.2f aim %.2f,%.2f,%.2f\n",
            T->mode,T->slot,y,T->yV,g_camPos[0],g_camPos[1],g_camPos[2],g_camAim[0],g_camAim[1],g_camAim[2]); fflush(stdout); } }
/* ⭐ ep 11: camshake's noise -- smooth VALUE noise (a lattice of hashed values, eased between with a quintic so the
 * hand has no kinks), two octaves, per channel. White noise would be a vibrating tripod, not a person holding it. */
static float lensHash(int i,int ch){
    unsigned h=(unsigned)i*374761393u+(unsigned)ch*668265263u;
    h=(h^(h>>13))*1274126177u; h^=h>>16;
    return (float)(h&0xFFFFu)/32767.5f-1.f; }
static float lensNoise1(float t,int ch){
    float f=floorf(t), u=t-f; int i=(int)f;
    u=u*u*u*(u*(u*6.f-15.f)+10.f);
    float a=lensHash(i,ch), b=lensHash(i+1,ch);
    return a+(b-a)*u; }
/* adds the shake to a lens about to be drawn: amp on the eye, a third of it on the aim -- never to g_camPos, which is
 * the pinned lens itself and would walk off a frame at a time */
static void camShakeApply(float dt,float*eye,float*ctr){
    if(dt>0.f&&dt<0.25f) g_camShkT+=dt;
    float A=g_camShk[0]; if(A<=0.f) return;
    float hz=(g_camShk[1]>0.f)?g_camShk[1]:1.5f, t=g_camShkT*hz;
    for(int k=0;k<6;k++){
        float n=(lensNoise1(t,k)+0.5f*lensNoise1(t*2.13f+17.f,k+6))*(1.f/1.5f);
        if(k<3) eye[k]+=n*A; else ctr[k-3]+=n*A*(1.f/3.f); } }

/* ⭐ ep 10: THE CARVE, APPLIED. Roll a body's model matrix about its travel axis through the ground contact
 * (x, floor, z) by the bank the loco PUBLISHED on this exposure, so the lean steps with the place and the
 * pose. BOTH draws call it, and whatever is built off the model after it -- the grips, the attach points,
 * a board hung off the body -- leans with the body. R turns up toward p = (cos trav, 0, -sin trav) by
 * the bank: R = I + sin(b)(p u^T - u p^T) + (cos(b)-1)(u u^T + p p^T), the travel axis left alone. */
static void locoLeanModel(float*model,int slot,float floorY){
    if(g_leanOff||slot<0||slot>=SCN_MAXCAST) return;
    float bk=g_loco[slot].pbank; if(fabsf(bk)<1e-4f) return;
    float th=g_loco[slot].ptrav, px=cosf(th), pz=-sinf(th), cb=cosf(bk), sb=sinf(bk), c1=cb-1.f;
    const float Rl[9]={ 1.f+c1*px*px, sb*px, c1*px*pz,
                        -sb*px,       cb,    -sb*pz,
                        c1*px*pz,     sb*pz, 1.f+c1*pz*pz };
    const float Cl[3]={model[12],floorY,model[14]};
    for(int c=0;c<4;c++){ float*v=model+c*4; float a0=v[0], a1=v[1], a2=v[2];
        if(c==3){ a0-=Cl[0]; a1-=Cl[1]; a2-=Cl[2]; }
        float n0=Rl[0]*a0+Rl[1]*a1+Rl[2]*a2, n1=Rl[3]*a0+Rl[4]*a1+Rl[5]*a2, n2=Rl[6]*a0+Rl[7]*a1+Rl[8]*a2;
        if(c==3){ n0+=Cl[0]; n1+=Cl[1]; n2+=Cl[2]; }
        v[0]=n0; v[1]=n1; v[2]=n2; } }

/* ⭐ THE THINGS IN THEIR HANDS, drawn after the bodies so the grips are measured. One
 * prop per cast slot, hung at the grip with the body's own yaw. */
/* ONE DRAW, called once per held slot per body. ⛔ Copying this loop body for the second
 * slot is the same mistake as a second parser: every later fix would have to be made twice
 * and one of them would be missed. */
static void drawHeldOne(int i,const char*spec,int which){
        const char*w9=which?" prop2":"";
        /* ⭐ A REFUSAL IS SAID OUT LOUD (ROBOT NEKO ep 2). Every skip below was silent, so a script
         * put a coffee in two women's hands and a torch in one, the take came back with nothing
         * in anyone's hand, and the log did not say a word. Once per slot per prop string. */
        /* ⭐ ep 10: A BOARD IS DRAWN IN ITS OWN PASS (drawBoards, after the cast has been posed), everything
         * else here where it always was -- and it never falls back to the hand while its rider has not
         * published a board frame yet. Before the said9 latch, so the pass that skips does not eat the log. */
        { HeldSpec Hb9; int isB9=heldParse(spec,&Hb9)&&!strcmp(Hb9.where,"board");
          if(isB9!=g_heldBoardPass) return;
          if(isB9&&!g_castAttOK[i][ATT_BOARD]) return; }
        static char said9[SCN_MAXCAST][2][PROP_SPEC];
        int tell9=strcmp(said9[i][which],spec)!=0;
        if(tell9) snprintf(said9[i][which],PROP_SPEC,"%s",spec);
        if(!spec[0]||!strncmp(spec,"off",3)) return;
        if(!g_castGripOK[i]){
            if(tell9){ printf("[held] slot %d%s '%s' REFUSED: that body never published a grip%c",i,w9,spec,10); fflush(stdout); }
            return; }
        /* ⛔ A TOWN PROP IS SIZED FOR A STREET, NOT A HAND. v1_town_lantern went into
         * susan's fist at lamppost scale and read as furniture. `stem@scale` sizes it;
         * without one, 0.30, which is about the difference between a thing on a post and
         * a thing you carry. */
        /* stem@scale@pitch -- ⛔ THE HAND'S AXES ARE NOT THE PROP'S. A UE forearm runs
         * down the bone and a torch points out of the fist, so a prop gets turned into the
         * hand here, once, in the scene file, rather than every asset being re-baked to a
         * convention nobody ever wrote down. */
        /* stem@scale@pitch@where@RRGGBB@light -- ONE parser (heldParse, above), shared with
         * the practical pass, so the light and the mesh can never disagree about which hand
         * the thing is in or what colour it is. `where` is hand (default), lhand, head,
         * chest or hip; `light` makes it a practical and draws this mesh emissive. */
        HeldSpec HS9; heldParse(spec,&HS9);
        const char*stem9=HS9.stem; const char*where9=HS9.where;
        float hs=HS9.size, hp=HS9.pitch, lpow9=HS9.lpow;

        /* the SHARED resolver -- the same one the practical pass uses, so the light and the
         * mesh cannot end up in different hands, and so an unspecified second prop takes
         * the other hand instead of silently overlapping the first. */
        int coll9=0;
        int att9=heldAttach(i,&HS9,which,&coll9);
        if(att9<0){
            if(tell9){ printf("[held] slot %d%s '%s' REFUSED: no attach point published%c",i,w9,spec,10); fflush(stdout); }
            return; }
        if(coll9&&tell9){
            printf("[held] slot %d prop2 '%s': BOTH props resolve to the same attach point (%s) -- they will overlap; name a different `@where` on one of them%c",
                   i,stem9,where9[0]?where9:"hand",10); fflush(stdout); }
        if(hs<0.01f) hs=0.30f;
        int pk=heldFind(stem9);
        if(pk<0){ if(tell9){ printf("[held] slot %d%s '%s' REFUSED: prop pack not available%c",i,w9,stem9,10); fflush(stdout); }
                  return; }
        /* ⭐ THE HAND'S OWN FRAME -- ⛔ NORMALISED FIRST. g_castGripM is model * W[bone] and
         * the model carries the RIG's scale, which on kmoves is 0.018. Multiplying the
         * held size on top of that would have shrunk every prop by the body's scale
         * factor and left a torch the size of a grain of rice. Take the basis, drop its
         * length, then apply the held scale. */
        /* ⭐ A HELD SIZE IS A WORLD SIZE -- the longest side of the thing, in club units. The
         * film's scripts were written to exactly that contract (mug 0.21, torch 0.42, hammer 0.58,
         * board 2.45 = metres x 1.75) and this multiplied the MESH by it instead: the props are
         * authored in metres, so the coffee came out a tenth of its size and was never seen in
         * any take. Scale by size / the measured longest side. */
        float ext9=0.f; for(int h9=0;h9<g_nHeld;h9++) if(g_held[h9].pk==pk){ ext9=g_held[h9].ext; break; }
        float k9=(ext9>1e-5f)?(hs/ext9):hs;
        M4 g; memcpy(g,g_castAttM[i][att9],64);
        for(int c9=0;c9<3;c9++){
            float*col=g+c9*4;
            float L=sqrtf(col[0]*col[0]+col[1]*col[1]+col[2]*col[2]);
            if(L>1e-6f){ col[0]/=L; col[1]/=L; col[2]/=L; } }
        M4 rot; { float cp=cosf(hp), sp=sinf(hp);
          memset(rot,0,64);
          rot[0]=k9; rot[5]=cp*k9; rot[6]=sp*k9; rot[9]=-sp*k9; rot[10]=cp*k9; rot[15]=1.f; }
        M4 m; m4_mul(m,g,rot);
        m[12]=g_castAttM[i][att9][12];
        m[13]=g_castAttM[i][att9][13];
        m[14]=g_castAttM[i][att9][14];
        /* ⭐ ep 9: ON THE FLOOR, the prop's lowest point rests on it, wherever its origin is */
        if(att9==ATT_DECK||att9==ATT_MARK){ for(int h9=0;h9<g_nHeld;h9++) if(g_held[h9].pk==pk){ m[13]-=g_held[h9].loY*k9; break; } }
        /* ⭐ ep 10: ON ITS CHANNEL, THE FIT. The board frame's origin is the deck top's centre and its +X the
         * nose, so the MESH is mapped onto that: its longest HORIZONTAL side onto +X, the centre of its top
         * (max Y) onto the origin, scaled uniformly so its longest side is the spec's size in WORLD units --
         * g was normalised above, so the rider's own scale (kmoves 0.018) is already out of it. The pitch
         * field is ignored: the channel owns the orientation. */
        if(att9==ATT_BOARD){
            float lo9[3]={0,0,0}, hi9[3]={0,0,0};
            float dk9=-1e9f;
            for(int h9=0;h9<g_nHeld;h9++) if(g_held[h9].pk==pk){ memcpy(lo9,g_held[h9].lo,12); memcpy(hi9,g_held[h9].hi,12); dk9=g_held[h9].deckY; break; }
            for(int a9=0;a9<3;a9++) if(hi9[a9]<lo9[a9]){ lo9[a9]=0.f; hi9[a9]=0.f; }
            /* ⛔ ep 10: the channel's origin is the FLAT deck top. The mesh's max Y is the kick tips -- 0.223 u against
             * a 0.161 u deck at size 1.4 -- and seating that at the origin sank the board 0.06 u under every foot. */
            float cx9=0.5f*(lo9[0]+hi9[0]), cy9=(dk9>-1e8f&&dk9<=hi9[1])?dk9:hi9[1], cz9=0.5f*(lo9[2]+hi9[2]);
            M4 fit9; memset(fit9,0,64); fit9[15]=1.f;
            if((hi9[2]-lo9[2])>(hi9[0]-lo9[0])){   /* long along mesh Z: R_y(+90) takes Z to +X and X to -Z */
                fit9[2]=-k9; fit9[5]=k9; fit9[8]=k9;
                fit9[12]=-k9*cz9; fit9[13]=-k9*cy9; fit9[14]=k9*cx9; }
            else { fit9[0]=k9; fit9[5]=k9; fit9[10]=k9;
                fit9[12]=-k9*cx9; fit9[13]=-k9*cy9; fit9[14]=-k9*cz9; }
            m4_mul(m,g,fit9);                      /* g still carries the board frame's world origin */
            memcpy(g_boardM[i],m,64); memcpy(g_boardLo[i],lo9,12); memcpy(g_boardHi[i],hi9,12);
            g_boardStamp[i]=g_frameStamp; g_boardDrawn[i]=1;
            /* ⭐ ep 10: the board is audited like a body (B1's matrix, C's hook) -- by the points a board actually
             * has, not its box: four wheels (trucks ~0.30 of the length either side, 0.35 of the width out) and
             * the two tips just under the kick top. The box's lower corners hang below the kicks, so every tail
             * strike read as 0.19 u into the floor on the first take of THE FLOOR. */
            { const float*lo9b=g_boardLo[i],*hi9b=g_boardHi[i]; int lz9=(hi9b[2]-lo9b[2])>(hi9b[0]-lo9b[0]);
              float cL=lz9?0.5f*(lo9b[2]+hi9b[2]):0.5f*(lo9b[0]+hi9b[0]), hL=lz9?0.5f*(hi9b[2]-lo9b[2]):0.5f*(hi9b[0]-lo9b[0]);
              float cW=lz9?0.5f*(lo9b[0]+hi9b[0]):0.5f*(lo9b[2]+hi9b[2]), hW=lz9?0.5f*(hi9b[0]-lo9b[0]):0.5f*(hi9b[2]-lo9b[2]);
              float P9[6][3]; int n9=0;
              for(int a=-1;a<=1;a+=2) for(int b=-1;b<=1;b+=2){ float l=cL+(float)a*0.60f*hL, w=cW+(float)b*0.70f*hW;
                  P9[n9][0]=lz9?w:l; P9[n9][1]=lo9b[1]; P9[n9][2]=lz9?l:w; n9++; }
              for(int a=-1;a<=1;a+=2){ float l=cL+(float)a*0.98f*hL; P9[n9][0]=lz9?cW:l; P9[n9][1]=hi9b[1]-0.04f; P9[n9][2]=lz9?l:cW; n9++; }
              static const char*NM9[6]={"board.wheel0","board.wheel1","board.wheel2","board.wheel3","board.tip0","board.tip1"};
              const float*M9=g_boardM[i];
              for(int k=0;k<n9;k++){ const float*p=P9[k];
                  clipAuditPoint(i,NM9[k],M9[0]*p[0]+M9[4]*p[1]+M9[8]*p[2]+M9[12],M9[1]*p[0]+M9[5]*p[1]+M9[9]*p[2]+M9[13],
                                          M9[2]*p[0]+M9[6]*p[1]+M9[10]*p[2]+M9[14]); } } }
        /* ⛔ AN UNTEXTURED PROP MUST TAKE THE TINT PATH. The authored primitives (mug,
         * hammer, board, torch) carry no texture, and the textured branch samples an
         * unbound unit -- so the coffee came out a BLACK CYLINDER in her hand and nothing
         * said a word. useTex=0 hands the renderer a flat colour instead, which is exactly
         * what the town kit's untextured props already do. */
        /* ⭐ A HELD LIGHT DRAWS AS ITS OWN COLOUR, UNLIT. Shaded like everything else it is
         * the DARKEST thing in the frame -- the practical sits inside it, so every one of its
         * own normals faces away from it -- and a torch that is darker than what it lights is
         * the one thing the shot cannot survive. uEmis hands the fragment stage `base` and
         * nothing else: no sun, no sky, no practicals. It always takes the tint path, so a
         * textured lamp still glows the colour the script named. */
        { Pack*HP=&g_setPk[pk];
          if(lpow9>0.f||HP->nTex==0){   }
          else                      {   } }
        /* ⭐ ep 12: HELD PART-WAY ALONG (`where:0.3`). The point `along` of the way from the prop's grip end (nearP)
         * to its far tip goes to the attach point, instead of its origin -- so everything downstream (the ember's
         * light, the tip's smoke, the drag's filter-to-mouth test) reads the prop where it is actually held. */
        if(HS9.along>=0.f&&att9!=ATT_BOARD){ const HeldProp*HA9=heldPropOf(pk);
            if(HA9){ float q9[3]; for(int a9=0;a9<3;a9++) q9[a9]=HA9->nearP[a9]+HS9.along*(HA9->tip[a9]-HA9->nearP[a9]);
                float w9q[3]; for(int a9=0;a9<3;a9++) w9q[a9]=m[a9]*q9[0]+m[4+a9]*q9[1]+m[8+a9]*q9[2];
                m[12]-=w9q[0]; m[13]-=w9q[1]; m[14]-=w9q[2];
                if(tell9){ printf("[held] slot %d%s '%s' held %.0f%% along its length (grip end -> tip): the point at (%.3f,%.3f,%.3f) in the prop is on the %s%c",
                                  i,w9,stem9,HS9.along*100.f,q9[0],q9[1],q9[2],where9[0]?where9:"hand",10); fflush(stdout); } } }
        /* ⛔ REPORT THE POINT IT RESOLVED TO, NOT THE STRING. A second prop with no `@where`
         * is deliberately NOT where the string says (it is the other hand), and a log that
         * printed the empty default would describe a prop that is not there. */
        if(tell9){ static const char*ATTN9[ATT_N]={"hand","head","chest","hip","lhand","deck","mark","board","bone","bone2"};
                   printf("[held] slot %d%s '%s' -> pack %d on the %s at (%.2f,%.2f,%.2f), size %.2f%c",
                          i,w9,stem9,pk,ATTN9[att9],m[12],m[13],m[14],hs,10);
                   printf("[held]   mesh longest side %.3f -> scaled x%.2f%c",ext9,k9,10); fflush(stdout); }
        /* ⭐ ep 12: EACH MESH MAY NAME ITS OWN COLOUR (`#rrggbb` in its name), and `#rrggbb*` marks the part that glows: the ember, not the
         * paper. vkMesh reads the name. (The old renderer also dimmed the ember with the drag, heldDragK: not carried across yet.) */
        drawProp(&g_setPk[pk],m);
        memcpy(g_heldM[i][which],m,64); g_heldMSt[i][which]=g_frameStamp; g_heldMPk[i][which]=pk; }

static void drawHeld(void){
    for(int i=0;i<SCN_MAXCAST;i++){
        drawHeldOne(i,g_castProp [i],0);
        drawHeldOne(i,g_castProp2[i],1); } }

/* ⭐ THE BOARDS (ROBOT NEKO ep 10), drawn after the cast in the frame the riders were posed in.
 * drawHeld runs in the set pass BEFORE any body is drawn, so everything it hangs is one frame old --
 * invisible on a mug, and on a stop-motion exposure a kickflip's board a whole exposure behind the
 * feet on the frame it lands. Same drawHeldOne, same parser, same resolver: only the pass differs.
 * REVA_BOARDDBG=1 says where each board landed, deck top and wheels, against its rider's floor. */
static void drawBoards(void){
    g_heldBoardPass=1;
    for(int i=0;i<SCN_MAXCAST;i++){
        drawHeldOne(i,g_castProp [i],0);
        drawHeldOne(i,g_castProp2[i],1); }
    g_heldBoardPass=0;
    { static int dbg9=-1; if(dbg9<0){ const char*e9=getenv("REVA_BOARDDBG"); dbg9=(e9&&e9[0]!='0'); }
      static unsigned n9=0;
      if(dbg9&&(n9++%30)==0){
          for(int i=0;i<SCN_MAXCAST;i++){ float c9[8][3];
              if(!boardCornersW(i,c9)) continue;
              float top9=-1e9f, bot9=1e9f;
              for(int k=0;k<8;k++){ if(k<4&&c9[k][1]>top9) top9=c9[k][1]; if(k>=4&&c9[k][1]<bot9) bot9=c9[k][1]; }
              printf("[board] slot %d frame origin (%.2f,%.2f,%.2f)  deck top y %.3f  wheels y %.3f  floor %.3f%s\n",
                     i,g_castAttM[i][ATT_BOARD][12],g_castAttM[i][ATT_BOARD][13],g_castAttM[i][ATT_BOARD][14],
                     top9,bot9,g_castFloorY[i],g_boardChOK[i]?"":"  (no channel yet)"); }
          fflush(stdout); } } }

