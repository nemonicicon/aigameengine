"""patch_cloth2.py -- the draw hook: skin it, simulate it, upload it, draw it unskinned.

The solver went in with patch_cloth.py and had nothing calling it. This is the four steps
that make a cloth mesh actually behave like cloth in the partner's draw:

  1. SKIN IT ON THE CPU for this frame -- the palette is already built two lines above, so
     the animated world position of every garment vertex is a matrix multiply away. That
     pose is what the pins follow and what the free particles are pulled gently toward.
  2. BUILD THE BODY. Spheres at the canonical bones, radii from the rig's own leg length,
     so a tall body and a short one get proportionate collision without a tuned number.
  3. STEP.
  4. UPLOAD AND DRAW UNSKINNED. ⛔ The solver's output is already in WORLD space, so the
     draw uses an IDENTITY model and an IDENTITY palette -- otherwise the GPU would skin
     the simulation a second time and the garment would fly off into the set.
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


sub("""        skinPal(mPal,uDqS,dq&&P->dqsOK,pal,(int)M->boneCount,mi<8?P->meshDqS[mi]:1.f);
        glUniformMatrix4fv(mBind,1,GL_FALSE,(float*)(P->pk+M->offBind));
        GLuint*b=&P->vbo[mi*6];""",
    """        /* ======================= CLOTH ==========================================
         * ⭐ Skin it on the CPU, simulate it, upload it, and draw it UNSKINNED. The
         * palette is already built one line above, so the animated world position of
         * every garment vertex is one matrix multiply away -- that pose is what the pins
         * follow and what the free particles are pulled gently toward. */
        int isCloth9=0;
        if(!g_clothOff&&meshIsCloth(P,mi)&&M->verts<CLOTH_MAXV){
            Cloth*C=&g_cloth[g_castI];
            static float skinW[CLOTH_MAXV*3];
            float*rp=(float*)(P->pk+M->offPos);
            unsigned char*jp=(unsigned char*)(P->pk+M->offJoints);
            unsigned char*wp=(unsigned char*)(P->pk+M->offWeights);
            float*bindm=(float*)(P->pk+M->offBind);
            for(unsigned v=0;v<M->verts;v++){
                float bp[3]={rp[v*3],rp[v*3+1],rp[v*3+2]};
                float bq[3];   /* bind * position */
                for(int k=0;k<3;k++) bq[k]=bindm[k]*bp[0]+bindm[4+k]*bp[1]+bindm[8+k]*bp[2]+bindm[12+k];
                float acc[3]={0,0,0}, wsum=0.f;
                for(int q=0;q<4;q++){
                    float w=wp[v*4+q]/255.f; if(w<=0.f) continue;
                    float*pm=pal+jp[v*4+q]*16;
                    for(int k=0;k<3;k++)
                        acc[k]+=w*(pm[k]*bq[0]+pm[4+k]*bq[1]+pm[8+k]*bq[2]+pm[12+k]);
                    wsum+=w; }
                if(wsum<1e-4f){ acc[0]=bq[0]; acc[1]=bq[1]; acc[2]=bq[2]; }
                /* into WORLD, through the same model matrix the body is drawn with */
                for(int k=0;k<3;k++)
                    skinW[v*3+k]=model[k]*acc[0]+model[4+k]*acc[1]+model[8+k]*acc[2]+model[12+k]; }
            if(!C->seeded||C->pk!=P||C->mesh!=(int)mi) clothInit(C,P,mi,skinW);
            /* ⭐ THE BODY, as spheres at the canonical bones. Radii come from the rig's own
             * leg length, so a tall body and a short one collide proportionately without a
             * number anybody had to tune. */
            float sph[CLOTH_MAXS][4]; int nsph=0;
            { float sc9=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]);
              float legU=1.f;
              if(P->pb[0]>=0&&P->footL>=0) legU=fabsf(W[P->pb[0]][13]-W[P->footL][13]);
              if(legU<1e-4f) legU=1.f;
              int bn[6]={P->pb[0],P->pb[1],P->pb[2],P->pb[3],P->pb[12],P->pb[14]};
              float rr[6]={0.26f,0.25f,0.25f,0.23f,0.16f,0.16f};
              for(int q=0;q<6&&nsph<CLOTH_MAXS;q++){
                  if(bn[q]<0) continue;
                  float wpv[3];
                  for(int k=0;k<3;k++)
                      wpv[k]=model[k]*W[bn[q]][12]+model[4+k]*W[bn[q]][13]+model[8+k]*W[bn[q]][14]+model[12+k];
                  sph[nsph][0]=wpv[0]; sph[nsph][1]=wpv[1]; sph[nsph][2]=wpv[2];
                  sph[nsph][3]=rr[q]*legU*sc9; nsph++; } }
            clothStep(C,skinW,(const float(*)[4])sph,nsph,(float)g_chDt,1.f);
            glBindBuffer(GL_ARRAY_BUFFER,P->vbo[mi*6]);
            glBufferSubData(GL_ARRAY_BUFFER,0,(GLsizeiptr)(C->nv*12),C->cur);
            isCloth9=1; }
        if(isCloth9){
            /* ⛔ IDENTITY MODEL AND IDENTITY PALETTE. The solver's output is already in
             * WORLD space; skinning it again would throw the garment across the set. */
            static const float I9[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            glUniformMatrix4fv(mModel,1,GL_FALSE,I9);
            for(unsigned b9=0;b9<M->boneCount&&b9<MAXB;b9++) memcpy(pal+b9*16,I9,64);
            skinPal(mPal,uDqS,0,pal,(int)M->boneCount,1.f);
            glUniformMatrix4fv(mBind,1,GL_FALSE,I9);
        } else {
            skinPal(mPal,uDqS,dq&&P->dqsOK,pal,(int)M->boneCount,mi<8?P->meshDqS[mi]:1.f);
            glUniformMatrix4fv(mBind,1,GL_FALSE,(float*)(P->pk+M->offBind)); }
        GLuint*b=&P->vbo[mi*6];""",
    "the cloth draw hook")

sub("""        if(M->idxCount){ glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,b[4]); glDrawElements(GL_TRIANGLES,M->idxCount,GL_UNSIGNED_INT,0); }
        else glDrawArrays(GL_TRIANGLES,0,M->verts); }
    return 1; }
static float clipStride(Pack*P,int clip){""",
    """        if(M->idxCount){ glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,b[4]); glDrawElements(GL_TRIANGLES,M->idxCount,GL_UNSIGNED_INT,0); }
        else glDrawArrays(GL_TRIANGLES,0,M->verts);
        if(isCloth9) glUniformMatrix4fv(mModel,1,GL_FALSE,model);   /* hand the body back its matrix */ }
    return 1; }
static float clipStride(Pack*P,int clip){""",
    "restore the model matrix after a cloth mesh")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
