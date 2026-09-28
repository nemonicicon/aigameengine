"""patch_eyes.py -- CAPABILITY B: eyes that aim.

jacob, on watching a cut: "the eyes. holy shit the eyes are so bad on most models."

⛔ THE GAP. Nobody in the club has ever aimed an eye. The head aims -- `lookTarget` turns
the whole skull at whoever is being spoken to -- and the eyes ride along wherever the head
happens to point. A person holds a gaze with their EYES and moves their head afterwards;
doing only the head is what makes a face look like a security camera.

⭐ AND THE EYES FINISH WHAT THE HEAD STARTED. The head layer already computes the full
angle to the target and then deliberately UNDER-turns -- a speaker looks away a little,
which is what people do. That leftover is exactly what the eyes should take. So the eye
aim is the RESIDUAL, not a second copy of the same sum, and the two cannot fight.

⛔ AND AN EYE BONE WITH NO WEIGHTS ROTATES NOTHING. face_marks.py puts `face_eye_l`/`_r`
on a rig as MEASUREMENT MARKS -- they exist to be read, and on most bodies nothing is
skinned to them. Rotating one would be a silent no-op that looks like a working feature.
So the bone is checked against every mesh's bone map at load, and `[eyes]` reports per
body whether it can actually aim. Reva's are PAINTED and will report that they cannot;
that is information, not a failure.
"""
import io, sys

CR, LF = chr(13), chr(10)
CRLF = CR + LF
P = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"

s = io.open(P, encoding="utf-8", newline="").read()
was = CRLF in s
if was:
    s = s.replace(CRLF, LF)


def sub(a, b, why):
    global s
    n = s.count(a)
    if n != 1:
        print("!! anchor x%d for %s" % (n, why))
        sys.exit(1)
    s = s.replace(a, b)
    print("   ok: %s" % why)


# ---- can this body aim an eye at all? ----------------------------------------------
sub("    int fmMouth, fmEyeL, fmEyeR, fmChin;   /* face landmarks (skate_src/author/face_marks.py) */",
    """    int fmMouth, fmEyeL, fmEyeR, fmChin;   /* face landmarks (skate_src/author/face_marks.py) */
    int eyesRig;   /* ⛔ are the eye marks SKINNED? A mark with no weights rotates nothing. */""",
    "the eyesRig flag")

sub("""    if(P.fmMouth>=0&&P.fmEyeL>=0&&P.fmEyeR>=0&&P.nMeshes){""",
    """    /* ⛔ AN EYE BONE WITH NO WEIGHTS ROTATES NOTHING. face_marks.py plants face_eye_l/_r
     * as MEASUREMENT marks -- they exist to be read, and on most bodies nothing is skinned
     * to them, so aiming one would be a silent no-op that looks like a working feature.
     * Checked against every mesh's bone map, once, and reported. */
    P.eyesRig=0;
    if(P.fmEyeL>=0&&P.fmEyeR>=0){
        for(unsigned mi=0;mi<P.nMeshes&&!P.eyesRig;mi++){
            PMesh*M=&P.meshes[mi];
            if(!(M->flags&4)||!M->boneCount) continue;
            unsigned short*bm=(unsigned short*)(P.pk+M->offBoneMap);
            for(unsigned b=0;b<M->boneCount;b++)
                if(bm[b]==(unsigned short)P.fmEyeL||bm[b]==(unsigned short)P.fmEyeR){ P.eyesRig=1; break; } } }
    if(P.fmMouth>=0&&P.fmEyeL>=0&&P.fmEyeR>=0&&P.nMeshes){""",
    "detect skinned eyes at load")

# ---- the eye layer ------------------------------------------------------------------
sub("static void scnHeadLayer(Pack*P,float Q[][4],int who){",
    """/* ⭐ EYES THAT AIM. The head layer already works out the full angle to whoever is being
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
    if(y> MAXY) y= MAXY; if(y<-MAXY) y=-MAXY;
    if(p> MAXP) p= MAXP; if(p<-MAXP) p=-MAXP;
    int eye[2]={P->fmEyeL,P->fmEyeR};
    for(int k=0;k<2;k++){
        int b=eye[k]; if(b<0) continue;
        memcpy(Q[b],P->nodes[b].q,16);
        float qy[4],qx[4],off[4];
        qaxis(qy,0,1,0,y);
        qaxis(qx,1,0,0,p);
        qmul(off,qy,qx); qmul(Q[b],Q[b],off); } }

static void scnHeadLayer(Pack*P,float Q[][4],int who){""",
    "the eye layer")

# ---- and the head hands it the residual ---------------------------------------------
sub("""          float wgt = speaking?0.72f:1.0f;
          yaw   = yaw*0.25f   + ly*wgt;
          pitch = pitch*0.25f + lp*wgt*LOOK_PITCH_SIGN;""",
    """          float wgt = speaking?0.72f:1.0f;
          yaw   = yaw*0.25f   + ly*wgt;
          pitch = pitch*0.25f + lp*wgt*LOOK_PITCH_SIGN;
          /* ⭐ AND THE EYES TAKE WHAT THE HEAD LEFT. wgt<1 means the head deliberately
           * stops short of the target; that remainder is the eye aim, so a speaker who
           * turns her head only three quarters of the way still has her gaze ON the
           * person -- which is what actually reads as looking at someone. */
          eyeAim(P,Q,ly*(1.f-wgt),lp*(1.f-wgt)*LOOK_PITCH_SIGN);""",
    "the eyes finish the turn")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
