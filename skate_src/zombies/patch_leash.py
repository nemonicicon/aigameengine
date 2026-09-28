"""patch_leash.py -- cars at the kerb, and a LEASH on the dog.

jacob: "cars should be parked in the street next to the curb." and "there should be a leash
on the dog".

THE CARS were on the driveway at z = yz + 2.2*side (and before that at yz + 5*side, which
was the front WALL line -- every parked car in the film has been embedded in a house since
the street was first generated). A street parks at the kerb: the road spans z -14..14, so
a car sits just outside it at z ~= +-16.5, turned along the street rather than facing it.

THE LEASH is a line from the dog's collar to the walker's hand. It is drawn the same way
the face rig draws its marks -- one GL_LINES batch through tProg, which is already set up
and running in MODE_ANIM -- rather than as a prop, because both ends move every frame and
a set file places things once.

⛔ THE DOG IS AN ACTOR AND THE WALKER IS THE SCENE PARTNER. They live in different systems
(actors are placed by the set; the pair is placed by `stage=`), so the leash cannot be
expressed in the set file alone: the actor publishes its collar position, and the draw
that already knows where the partner is joins the two.
"""
import io, sys

CR, LF = chr(13), chr(10)
CRLF = CR + LF
BS_N = chr(92) + "n"


def load(p):
    s = io.open(p, encoding="utf-8", newline="").read()
    was = CRLF in s
    return (s.replace(CRLF, LF) if was else s), was


def save(p, s, was):
    io.open(p, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)


def sub(s, a, b, why):
    if s.count(a) != 1:
        print("!! anchor x%d for %s" % (s.count(a), why))
        sys.exit(1)
    print("   ok: %s" % why)
    return s.replace(a, b)


# ------------------------------------------------------------------ street.py
P = r"C:\MyVeryOwnVideoGames\reva\skate_src\town\street.py"
s = io.open(P, encoding="utf-8").read()
a = """                dz = yz + 2.2 * side          # z = -23.2: on the drive, clear of the wall
                vx = S.clear_spot(lx + 23.5, dz, 5.2)
                S.add(v, vx, 0, dz, 0 if side > 0 else 180)"""
b = """                # ⭐ AT THE KERB, ALONG THE STREET. The road spans z -14..14, so a parked
                # car sits just outside it and points down the street rather than at the
                # house. (It was on the driveway; before that it was at z=-26, which is
                # the front WALL line -- every car in the film was inside a house.)
                dz = 16.5 * side
                vx = S.clear_spot(lx + 20.0 + rng.uniform(-2.0, 2.0), dz, 5.6)
                S.add(v, vx, 0, dz, 90 if side > 0 else 270)"""
assert s.count(a) == 1
io.open(P, "w", encoding="utf-8").write(s.replace(a, b))
print("street.py: cars parked at the kerb")

# ------------------------------------------------------------------ engine
P2 = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"
t, was2 = load(P2)

t = sub(t, """                 Pack *dnP; int dnC;           /* Z4: a clip borrowed from a bank, retargeted onto this body */""",
        """                 Pack *dnP; int dnC;           /* Z4: a clip borrowed from a bank, retargeted onto this body */
                 int leash;                    /* 1: draw a lead from this actor to the walker */""",
        "SetActor gains a leash flag")

t = sub(t, """static float g_setT=0.f;      /* THE SET CLOCK.""",
        """/* where the leashed actor's collar is this frame, published by actorsDraw and consumed
 * by the draw that knows where the partner is -- the two live in different systems. */
static float g_leashAt[3]={0,0,0}; static int g_leashOn=0;
static float g_setT=0.f;      /* THE SET CLOCK.""",
        "leash globals")

t = sub(t, """        if(!strncmp(ln,"cue ",4)){""",
        """        if(!strncmp(ln,"leash",5)&&g_setNA>0){ g_setA[g_setNA-1].leash=1; continue; }
        if(!strncmp(ln,"cue ",4)){""",
        "parse the leash line")

t = sub(t, """            A->nCue=0; A->curCue=-1; A->nWp=0; A->onPath=0; A->cspd=0.f;""",
        """            A->nCue=0; A->curCue=-1; A->nWp=0; A->onPath=0; A->cspd=0.f; A->leash=0;""",
        "reset the flag")

t = sub(t, """static void actorsDraw(GLint mModel,GLint mPal,GLint mBind,float*pal,GLint uUse,GLint uTint){
    if(!g_setNA) return;""",
        """static void actorsDraw(GLint mModel,GLint mPal,GLint mBind,float*pal,GLint uUse,GLint uTint){
    g_leashOn=0;
    if(!g_setNA) return;""",
        "clear the leash each frame")

t = sub(t, """        m[12]=px; m[13]=A->y-P->mny*eff+A->air+hipDrop*eff; m[14]=pz;""",
        """        m[12]=px; m[13]=A->y-P->mny*eff+A->air+hipDrop*eff; m[14]=pz;
        /* the collar: a little above the body, a little ahead of it */
        if(A->leash){ g_leashAt[0]=px+sinf(a)*0.30f; g_leashAt[1]=m[13]+1.05f*eff;
                      g_leashAt[2]=pz+cosf(a)*0.30f; g_leashOn=1; }""",
        "publish the collar position")

# draw it beside the face gizmo, which already runs a GL_LINES batch in MODE_ANIM
t = sub(t, """                glDisable(GL_SCISSOR_TEST);
                glViewport(0,0,W,H);""",
        """                /* ⭐ THE LEAD. From the dog's collar to the walker's hand -- the partner's
                 * head is published every frame, and a hand is a forearm below it and a
                 * little forward. Drawn on the same GL_LINES batch the face marks use. */
                if(g_leashOn&&g_labPartner&&g_partFaceOK){
                    float hx9=g_partHeadW[0], hy9=g_partHeadW[1]-0.62f, hz9=g_partHeadW[2];
                    float lv[12]={ hx9,hy9,hz9, 0.10f,0.09f,0.08f,
                                   g_leashAt[0],g_leashAt[1],g_leashAt[2], 0.10f,0.09f,0.08f };
                    glUseProgram(tProg); glUniformMatrix4fv(tVP,1,GL_FALSE,pvp);
                    glBindBuffer(GL_ARRAY_BUFFER,trailVbo);
                    glBufferData(GL_ARRAY_BUFFER,sizeof lv,lv,GL_DYNAMIC_DRAW);
                    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,24,0);
                    glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,24,(void*)12);
                    for(int k3=2;k3<5;k3++) glDisableVertexAttribArray(k3);
                    glDrawArrays(GL_LINES,0,2);
                    glUseProgram(skSolid?PR[1].prog:hProg); }
                glDisable(GL_SCISSOR_TEST);
                glViewport(0,0,W,H);""",
        "draw the lead")

save(P2, t, was2)
print("crowd_kms.c patched")
