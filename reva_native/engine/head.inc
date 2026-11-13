/* THE HEAD LAYER. Heads were not moving. Two bodies standing perfectly still
 * for a minute read as mannequins with a radio playing, however good the voices are.
 * This rides ON TOP of whatever clip is playing, in the bone's own frame (post-multiply
 * = local axes; both rigs are Mixamo-shaped, so local X is the nod and local Y the
 * turn): a slow drift for both, the SPEAKER moving with her own loudness, the LISTENER
 * nodding on a slower clock of her own. Split across neck and head so the whole column
 * bends rather than the skull swivelling on a fixed neck. */
/* ---- THE JAW, AS A BONE ---------------------------------------------------------
 * The reference rig said it first (v1_crowd_goblin's CC_Base_JawRoot): a jaw is a
 * BONE with the lower face skinned to it, not a morph. skate_src/author/jaw.py grows
 * one at the measured hinge and binds the band; this swings it.
 *
 * It is driven by the SCENE ENVELOPE -- the speaker's own loudness, the same 30Hz
 * track the subtitles and the head layer already ride -- rather than by a morph
 * weight. The morph could only ever be a shape; the bone carries teeth and a tongue
 * with it, and works at any mesh density.
 *
 * Direction is not a guess: the chin sits below and in front of the hinge, so a
 * POSITIVE rotation about the head's lateral axis takes it down and back, which is
 * what a jaw does. The jaw node is authored with an identity rest rotation, so its
 * local frame is the head's and this needs no basis change. */
static void jawLayer(Pack*P,float Q[][4],int who,float open){
    { static const char*e9=(const char*)1;
      if(e9==(const char*)1){ e9=getenv("REVA_JAWFORCE"); if(e9) g_jawForce=(float)atof(e9); } }
    if(g_jawForce>=0.f) open=g_jawForce;
    if(open<0.f) open=0.f;
    if(open>1.f) open=1.f;
    g_jawOpen[who?1:0]=open;
    if(!P||P->fmJaw<0) return;
    float off[4]; qaxis(off,1,0,0,open*JAW_MAX_RAD);
    qmul(Q[P->fmJaw],Q[P->fmJaw],off); }
/* ---- THE LOOK-AT: a listener who is not looking at the speaker is not listening ----
 * The face marks make this cheap and honest: both bodies publish their eye line in
 * world every frame, so "look at her" is the angle between my body's forward and the
 * line to her eyes -- no target bone to guess at, and it stays right under any clip
 * because the marks ride FK.
 *
 * ⛔ NEITHER LEAD CAN AIM AN EYEBALL. reva has 26 vertices within a third of an
 * interocular distance of her eye mark -- her eyes are PAINTED -- and the co-star's eye
 * region is a handful of flat shells (lids and lashes), not spheres. So the aiming
 * that can actually happen here is the HEAD, and that is what this does. The eye-bone
 * path below is live for the six packs that do carry real eye bones (the rabbit,
 * the medic, two bought models, the giant, the toony robot); on a body without them it is skipped
 * rather than faked. */
/* qaxis(off,1,0,0,a) sends forward (0,0,1) to (0,-sin a,cos a): POSITIVE pitch tips
 * the face DOWN, so a target above needs the negative. Derived, not guessed. */
#define LOOK_PITCH_SIGN (-1.0f)
static int g_lookOn=1;                  /* ctl look=0/1 */
static float g_partYaw=0.f;             /* the partner's facing, set where she is drawn */
static int lookTarget(int who,float bodyYaw,float*yawOut,float*pitchOut){
    if(g_faceDbg<0){ const char*e9=getenv("REVA_FACEDBG"); g_faceDbg=(e9&&e9[0]!='0'); }
    if(g_faceDbg>0){ static int sd=0; sd++; if(sd%120==1)
        printf("[look:dbg] who %d lookOn %d heroFaceOK %d partFaceOK %d scnT %.2f\n",
               who,g_lookOn,g_heroFaceOK,g_partFaceOK,g_scnT); }
    if(!g_lookOn||!g_heroFaceOK||!g_partFaceOK) return 0;
    /* REFUSING TO LOOK AT SOMEONE IS A PERFORMANCE. A scene line may switch either
     * body's gaze off, and that -- not the words -- is what carries an argument: she
     * answers without turning her head, and the moment she does turn it lands. */
    if(g_scnT>=0.f&&!g_scnLook[who&1]) return 0;
    /* ⭐ a body riding a half-pipe is pitched into the wall and turning in the air -- this solve is for an
     * UPRIGHT body at bodyYaw, so on a pipe it stands down (the idle drift stays) */
    if(g_prd[who?((g_castI>=1&&g_castI<SCN_MAXCAST)?g_castI:1):0].pon) return 0;
    const float*me   = who? g_partEyeW : g_heroEyeW;
    const float*them = who? g_heroEyeW : g_partEyeW;
    float dx=them[0]-me[0], dy=them[1]-me[1], dz=them[2]-me[2];
    float d=sqrtf(dx*dx+dz*dz);
    if(d<1e-4f) return 0;
    /* express it in the BODY's frame, so what comes out is a head offset rather than
     * a world angle -- the head layer applies offsets, not absolutes */
    float f = dx*sinf(bodyYaw)+dz*cosf(bodyYaw);
    float r = dx*cosf(bodyYaw)-dz*sinf(bodyYaw);
    float yaw=atan2f(r,f), pitch=atan2f(dy,d);
    /* a neck has limits; past them a head snaps round and reads as a doll */
    if(yaw>0.62f) yaw=0.62f;
    if(yaw<-0.62f) yaw=-0.62f;
    if(pitch>0.32f) pitch=0.32f;
    if(pitch<-0.32f) pitch=-0.32f;
    *yawOut=yaw; *pitchOut=pitch; return 1; }
/* ⭐ EYES THAT AIM. The head layer already works out the full angle to whoever is being
 * spoken to and then deliberately UNDER-turns -- a speaker looks away a little, which is
 * what people do. That leftover is what the eyes take, so the two cannot fight: the head
 * goes most of the way and the EYES finish it, which is the order a person does it in.
 *
 * ⛔ Clamped to a human range. An eyeball has about 35 degrees of yaw and 20 of pitch
 * before the lid stops it, and past that it reads as a doll rather than a person. */
static void eyeAim(Pack*P,float Q[][4],float dyaw,float dpitch){
    if(!P||!P->eyesRig) return;
    const float MAXY=0.61f, MAXP=0.35f;          /* ~35 and ~20 degrees */
    float y=dyaw, p=dpitch;
    if(y> MAXY) y= MAXY;
    if(y<-MAXY) y=-MAXY;
    if(p> MAXP) p= MAXP;
    if(p<-MAXP) p=-MAXP;
    int eye[2]={P->fmEyeL,P->fmEyeR};
    for(int k=0;k<2;k++){
        int b=eye[k]; if(b<0) continue;
        memcpy(Q[b],P->nodes[b].q,16);
        float qy[4],qx[4],off[4];
        qaxis(qy,0,1,0,y);
        qaxis(qx,1,0,0,p);
        qmul(off,qy,qx); qmul(Q[b],Q[b],off); } }


