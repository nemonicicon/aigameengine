#ifdef REVA_SDL
/* ================= CHARACTER SELECT (jacob 08-25: "a character select screen with reva
 * and kaeli as a start") =============================================================
 * Runs BEFORE the crowd loads: its own scene on the shared GL context, its own little
 * loop, returns the chosen .rpack path (static storage). Windows-only for now -- the Pi
 * copy of this file is a forked ancestor and keeps the plain argv[3] behavior.
 *
 * Roster: chars.txt lines "file.rpack|NAME" when present, else the built-in pair.
 * Both characters stand on the dark stage playing their own dance clip; the hovered one
 * gets the light, the turntable and her name in hot pink. Pad left/right + A, or
 * arrows + Enter. ESC quits the program entirely.
 *
 * Bench instruments (env, off unless asked): REVA_SELSHOT=<frame> dumps the select
 * screen to select_shot.ppm at that frame; REVA_AUTOPICK=<idx> confirms that slot ~20
 * frames later, so the whole flow can be judged on pixels with no one at the pad. */
/* a menu's own loop, on the new renderer: a frame of its own. What the old road reads back comes from the renderer's copy of the last frame */
#ifdef REVA_VK
static void menuFrameEnd(const float*vp,float r,float g,float b,double tsec){ GfxView v9; memset(&v9,0,sizeof v9); for(int i=0;i<16;i+=5){ v9.vp[i]=1.f; v9.sunVP[i]=1.f; } if(vp) memcpy(v9.vp,vp,64); v9.clear[0]=r; v9.clear[1]=g; v9.clear[2]=b; v9.knee=1.f; v9.tsec=(float)tsec; v9.tick=g_tickTotal; v9.lights=g_frameLight; v9.nLights=g_frameNL; g_frameNL=0; gfxFrameEnd(&v9); }
#define MENU_BEGIN() do{ g_noDraw=0; gfxFrameBegin(); }while(0)
#define MENU_END(vp,r,g,b,t) menuFrameEnd(vp,r,g,b,t)
#define MENU_HUD(v,n,W,H,r,g,b,a) gfxHud(v,n,(float)(W),(float)(H),r,g,b,a)
#define MENU_READ(px,W,H) gfxRead(px,W,H)
#else
#define MENU_BEGIN()
#define MENU_END(vp,r,g,b,t)
#define MENU_HUD(v,n,W,H,r,g,b,a)
#define MENU_READ(px,W,H) glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,px)
#endif
typedef struct { char path[320]; char name[32]; } SelChar;
static const char* charSelect(SDL_Window*win,int W,int H){
    static SelChar cand[8]; int nc=0;
    FILE*rf=fopen("chars.txt","r");
    if(rf){ char ln[320];
        while(fgets(ln,sizeof ln,rf)&&nc<8){ char*bar=strchr(ln,'|'); char*nl=strpbrk(ln,"\r\n");
            if(nl)*nl=0;
            if(!bar||bar==ln) continue;
            *bar=0;
            snprintf(cand[nc].path,sizeof cand[nc].path,"%s",ln);
            snprintf(cand[nc].name,sizeof cand[nc].name,"%s",bar+1); nc++; }
        fclose(rf); }
    if(!nc){ static const char*def[2][2]={{"v1_reva.rpack","REVA"},{"v1_kaeli.rpack","KAELI"}};
        for(int i=0;i<2;i++){ snprintf(cand[nc].path,sizeof cand[nc].path,"%s",def[i][0]);
            snprintf(cand[nc].name,sizeof cand[nc].name,"%s",def[i][1]); nc++; } }
    { int k=0; for(int i=0;i<nc;i++){ FILE*f=fopen(cand[i].path,"rb"); if(f){ fclose(f); cand[k++]=cand[i]; } }
      nc=k; }
    if(nc==0){ printf("charSelect: no character packs found -- floor runs hostless\n"); return NULL; }
    if(nc==1){ printf("charSelect: only %s present, auto-picked\n",cand[0].path); return cand[0].path; }

    static Pack cp[8]; int prev[8]; float dsc[8],dgy[8],dcx[8],dcz[8];
    int maxN=1;
    for(int i=0;i<nc;i++){ cp[i]=loadPack(cand[i].path);
        if((int)cp[i].nNodes>maxN) maxN=(int)cp[i].nNodes;
        /* preview clip: her own dance (p_/m_/c_), else anything */
        prev[i]=-1;
        for(unsigned c=0;c<cp[i].nClips;c++){ const char*nm=packStr(&cp[i],cp[i].clips[c].name);
            if((nm[0]=='p'||nm[0]=='m'||nm[0]=='c')&&nm[1]=='_'){ prev[i]=(int)c; break; } }
        if(prev[i]<0&&cp[i].nClips) prev[i]=0;
        /* display normalization by the MESH, not the bone cloud: rigs full of control
         * bones (reva's page export) measure 10x taller than the character, and the
         * hero block's corrections have not run yet. Rest pose ~= bind pose, so raw
         * bind-space position bounds are the character's real extent. */
        float mnx=1e9f,mxx=-1e9f,mny=1e9f,mxy=-1e9f,mnz=1e9f,mxz=-1e9f; int any=0;
        for(unsigned m=0;m<cp[i].nMeshes;m++){ PMesh*M=&cp[i].meshes[m];
            if(!(M->flags&4)||M->boneCount>MAXB||MOFF(&cp[i],m)) continue;
            meshAlphaState(M);
            float*pp=(float*)(cp[i].pk+M->offPos);
            for(unsigned v=0;v<M->verts;v++){ float x=pp[v*3],y=pp[v*3+1],z=pp[v*3+2];
                if(x<mnx)mnx=x;
                if(x>mxx)mxx=x;
                if(y<mny)mny=y;
                if(y>mxy)mxy=y;
                if(z<mnz)mnz=z;
                if(z>mxz)mxz=z;
                any=1; } }
        if(!any||mxy-mny<1e-4f){ dsc[i]=cp[i].scale; dgy[i]=-cp[i].mny*dsc[i]; dcx[i]=0; dcz[i]=0; }
        else { dsc[i]=3.0f/(mxy-mny); dgy[i]=-mny*dsc[i];
               dcx[i]=-(mnx+mxx)*0.5f*dsc[i]; dcz[i]=-(mnz+mxz)*0.5f*dsc[i]; }
        printf("charSelect: %-20s %u clips, preview %d, display scale %.4f\n",
               cand[i].name,cp[i].nClips,prev[i],dsc[i]); }

    /* the floor's own skinned look, compiled locally so the pick shows the real thing */
    const char*svs=
      "attribute vec3 p; attribute vec3 n; attribute vec4 j; attribute vec4 w; attribute vec2 t;\n"
      "uniform mat4 vp; uniform mat4 model; uniform mat4 bind; uniform mat4 pal["
      MAXB_STR(MAXB) "];\n"
      "varying vec3 vn; varying vec2 vt; varying vec3 vwp;\n"
      "void main(){ vec4 bp=bind*vec4(p,1.0); vt=t;\n"
      "  mat4 skin=w.x*pal[int(j.x)]+w.y*pal[int(j.y)]+w.z*pal[int(j.z)]+w.w*pal[int(j.w)];\n"
      "  vec4 wp=model*skin*bp; vwp=wp.xyz; vn=normalize((model*skin*(bind*vec4(n,0.0))).xyz);\n"
      "  gl_Position=vp*wp; }\n";
    const char*sfs=
      "#extension GL_OES_standard_derivatives : enable\n"   /* dFdx/dFdy for the normal map's tangent frame (09-11) */
      "precision mediump float; varying vec3 vn; varying vec2 vt; varying vec3 vwp;\n"
      "varying vec3 vFace;\n"
      "uniform vec3 tint; uniform float useTex; uniform sampler2D smp;\n"
      "uniform float uOpen;\n"
      "uniform vec3 lp[6]; uniform vec3 lc[6]; uniform float lk[6];\n"
      "uniform vec4 uSun; uniform vec3 uSunC; uniform vec3 uSky;\n"
      "uniform vec3 uEye; uniform vec4 uFog;\n"   /* ⭐ FOG (09-11): rgb + density; 0 = off */
      "uniform sampler2D uShMap; uniform float uShOn; varying vec4 vShp;\n"   /* ⭐ THE SUN'S SHADOW MAP (09-11) */
      "uniform sampler2D nmp; uniform float uNrm;\n"   /* ⭐ THE NORMAL MAP (09-11): unit 1, per mesh */
      "uniform float uTone;\n"
      "void main(){ vec3 N=normalize(vn);\n"
      /* ⭐ THE NORMAL MAP (09-11). The tangent frame is built from the screen-space
       * derivatives of position and UV (the cotangent-frame method; OES_standard_derivatives
       * is present), so the pack carries no tangents and a flipped V is handled by the
       * derivatives themselves. Only the SHADING normal changes: silhouettes and shadows keep
       * the mesh. REVA_NONORMAL=1 forces uNrm 0 for the A/B. */
      "  if(uNrm>0.5){ vec3 nt=texture2D(nmp,vt).xyz*2.0-1.0;\n"
      "    vec3 dp1=dFdx(vwp), dp2=dFdy(vwp); vec2 du1=dFdx(vt), du2=dFdy(vt);\n"
      "    vec3 dp2p=cross(dp2,N), dp1p=cross(N,dp1);\n"
      "    vec3 T=dp2p*du1.x+dp1p*du2.x, B=dp2p*du1.y+dp1p*du2.y;\n"
      "    float im=inversesqrt(max(dot(T,T),dot(B,B))+1e-12);\n"
      "    N=normalize(T*im*nt.x+B*im*nt.y+N*max(nt.z,0.2)); }\n"
      "  float l=0.3+0.7*max(0.0,dot(N,normalize(vec3(0.4,0.8,0.6))));\n"
      "  vec3 li=vec3(0.0);\n"
      "  for(int i=0;i<6;i++){ vec3 D=lp[i]-vwp; float dd=dot(D,D);\n"
      "    li+=lc[i]*max(0.0,dot(N,D*inversesqrt(dd+0.001)))/(1.0+dd*max(0.055+lk[i],0.001)); }\n"
      "  vec4 tx=texture2D(smp,vt); if(useTex>0.5&&tx.a<0.4) discard;\n"
      "  vec3 base=mix(tint,tx.rgb,useTex);\n"
      /* uSun.w == 0 -> the club's own dark-room rig, byte for byte. Above zero it is
       * a real directional over a sky ambient, which is the only thing that reads as
       * daylight on a street. */
      "  vec3 lit = (uSun.w > 0.0)\n"
      "     ? base*(uSky + uSunC*uSun.w*max(0.0,dot(N,uSun.xyz))) + base*li\n"
      "     : base*(l*0.42+0.13+li);\n"
      "  gl_FragColor=vec4(lit,1.0); }\n";
    GLuint sp=glCreateProgram();
    glAttachShader(sp,sh(GL_VERTEX_SHADER,svs)); glAttachShader(sp,sh(GL_FRAGMENT_SHADER,sfs));
    glBindAttribLocation(sp,0,"p"); glBindAttribLocation(sp,1,"n"); glBindAttribLocation(sp,2,"j");
    glBindAttribLocation(sp,3,"w"); glBindAttribLocation(sp,4,"t");
    glLinkProgram(sp); { GLint ok; glGetProgramiv(sp,GL_LINK_STATUS,&ok); if(!ok) die("sel link"); }
    GLint suVP=glGetUniformLocation(sp,"vp"), suModel=glGetUniformLocation(sp,"model");
    GLint suBind=glGetUniformLocation(sp,"bind"), suPal=glGetUniformLocation(sp,"pal");
    GLint suTint=glGetUniformLocation(sp,"tint"), suUse=glGetUniformLocation(sp,"useTex");
    GLint suLp=glGetUniformLocation(sp,"lp"), suLc=glGetUniformLocation(sp,"lc");
    glUseProgram(sp); glUniform1i(glGetUniformLocation(sp,"smp"),0);
    /* 2D text program (stb_easy_font quads in pixel space) */
    const char*tvs="attribute vec2 p; uniform vec2 scr;\n"
      "void main(){ gl_Position=vec4(p.x/scr.x*2.0-1.0, 1.0-p.y/scr.y*2.0, 0.0, 1.0); }\n";
    const char*tfs="precision mediump float; uniform vec4 col; void main(){ gl_FragColor=col; }\n";
    GLuint tp=glCreateProgram();
    glAttachShader(tp,sh(GL_VERTEX_SHADER,tvs)); glAttachShader(tp,sh(GL_FRAGMENT_SHADER,tfs));
    glBindAttribLocation(tp,0,"p");
    glLinkProgram(tp); { GLint ok; glGetProgramiv(tp,GL_LINK_STATUS,&ok); if(!ok) die("sel text link"); }
    GLint tuScr=glGetUniformLocation(tp,"scr"), tuCol=glGetUniformLocation(tp,"col");
    GLuint tvbo; glGenBuffers(1,&tvbo);

    M4 *world=malloc((size_t)maxN*sizeof(M4));
    float (*T)[3]=malloc((size_t)maxN*12), (*Q)[4]=malloc((size_t)maxN*16), (*S)[3]=malloc((size_t)maxN*12);
    static float pal[64*16];
    double ph[8]={0}; int sel=0, done=-1, dirPrev=0, aPrev=1, stPrev=1;
    long selShot=-1, autoPick=-1, fno=0;
    { const char*e=getenv("REVA_SELSHOT"); if(e) selShot=atol(e);
      e=getenv("REVA_AUTOPICK"); if(e) autoPick=atol(e); }
    double t0=now_ms(), tPrev=t0;
    printf("CHARACTER SELECT: %d on the stage\n",nc);
    while(done<0&&g_run){ if(g_fixedStepMs<=0.0) clockTick();
        clockTick();
        double tn=now_ms(), tsec=(tn-t0)/1000.0; float dt=(float)((tn-tPrev)/1000.0); tPrev=tn;
        if(dt>0.1f) dt=0.1f;
        fno++;
        jsPoll(tsec); kbPoll(tsec);   /* A/D pick, SPACE or click confirms */
        SDL_SetRelativeMouseMode(SDL_FALSE);
        /* pad: g_lx is the FLOOR's inverted axis (stick left => g_lx>0) */
        int dir=(g_lx>0.55f)?-1:(g_lx<-0.55f)?1:0;
        if(dir&&!dirPrev){ sel=(sel+dir+nc)%nc; sfxPlay("ui_move",0.5f,NULL,0); }
        dirPrev=dir;
        if((g_btnA&&!aPrev)||(g_btnStart&&!stPrev)){ done=sel; sfxPlay("ui_confirm",0.6f,NULL,0); }
        aPrev=g_btnA; stPrev=g_btnStart;
        { SDL_Event ev; while(SDL_PollEvent(&ev)){
            if(ev.type==SDL_QUIT){ printf("select: quit\n"); exit(0); }
            if(ev.type==SDL_KEYDOWN){ SDL_Keycode kk=ev.key.keysym.sym;
                if(kk==SDLK_ESCAPE){ printf("select: quit\n"); exit(0); }
                if(kk==SDLK_LEFT){ sel=(sel+nc-1)%nc; sfxPlay("ui_move",0.5f,NULL,0); }
                if(kk==SDLK_RIGHT){ sel=(sel+1)%nc; sfxPlay("ui_move",0.5f,NULL,0); }
                if(kk==SDLK_RETURN||kk==SDLK_SPACE){ done=sel; sfxPlay("ui_confirm",0.6f,NULL,0); } }
            if(ev.type==SDL_MOUSEWHEEL&&ev.wheel.y) sel=(sel+(ev.wheel.y<0?1:nc-1))%nc; } }
        if(autoPick>=0&&fno>=((selShot>0)?selShot+20:40)) done=(int)(autoPick%nc);

        glViewport(0,0,W,H);
        glClearColor(0.016f,0.010f,0.045f,1.f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        MENU_BEGIN();
        glEnable(GL_DEPTH_TEST); glDisable(GL_BLEND);
        M4 pr,lv,vp2;
        m4_persp(pr,46.f,VIEW_ASPECT(W,H),0.1f,100.f);
        m4_lookat(lv,0.f,2.05f,7.6f, 0.f,1.55f,0.f);
        m4_mul(vp2,pr,lv);
        glUseProgram(sp); glUniformMatrix4fv(suVP,1,GL_FALSE,vp2);
        for(int i=0;i<nc;i++){ Pack*P=&cp[i];
            float px=((float)i-(nc-1)*0.5f)*3.1f;
            int hot=(i==sel);
            ph[i]+= (double)dt*(hot?1.0:0.65)* (P->nClips&&prev[i]>=0?P->clips[prev[i]].fps:30.f);
            for(unsigned n2=0;n2<P->nNodes;n2++){ memcpy(T[n2],P->nodes[n2].t,12);
                memcpy(Q[n2],P->nodes[n2].q,16); memcpy(S[n2],P->nodes[n2].s,12); }
            if(P->nClips&&prev[i]>=0)   /* P1.5: fractional -- the select row previews smoothly too */
                clipSampleF(P,&P->clips[prev[i]],ph[i],T,Q,S);
            for(unsigned n2=0;n2<P->nNodes;n2++){ M4 loc; m4_trs(loc,T[n2],Q[n2],S[n2]);
                if(P->nodes[n2].parent<0) memcpy(world[n2],loc,64);
                else m4_mul(world[n2],world[P->nodes[n2].parent],loc); }
            float yaw=hot?0.35f*sinf((float)tsec*0.9f):0.f;
            float ca=cosf(yaw),sa=sinf(yaw),scl=dsc[i];
            M4 model; memset(model,0,64);
            model[0]=ca*scl; model[2]=-sa*scl; model[5]=scl; model[8]=sa*scl; model[10]=ca*scl; model[15]=1;
            model[12]=px+dcx[i]*ca; model[13]=dgy[i]; model[14]=dcz[i];
            float glow=hot?1.0f:0.30f;
            float tint[3]={glow,glow,glow};
            float lpv[NLIGHT*3]={px,3.3f,1.8f,  -4.5f,2.5f,3.0f,  4.5f,2.5f,3.0f,
                                 0,-99,0, 0,-99,0, 0,-99,0};
            /* ⛔ THIS WAS float lcv[9] AND THE UPLOAD BELOW SENDS NLIGHT*3 = 18. Nine floats
             * of whatever was next on the stack went to the shader as three more lamps. */
            float lcv[NLIGHT*3]={hot?1.15f:0.10f,hot?0.55f:0.05f,hot?1.30f:0.12f,
                          0.10f,0.04f,0.16f, 0.10f,0.04f,0.16f, 0,0,0, 0,0,0, 0,0,0};
            for(unsigned mi=0;mi<P->nMeshes;mi++){ PMesh*M=&P->meshes[mi];
                if(!(M->flags&4)||M->boneCount>MAXB||MOFF(P,mi)) continue;
            meshAlphaState(M);
                glUniformMatrix4fv(suModel,1,GL_FALSE,model);
                glUniform3fv(suTint,1,tint);
                glUniform3fv(suLp,NLIGHT,lpv); glUniform3fv(suLc,NLIGHT,lcv);
                bodyPalette(P,mi,world,pal);
                glUniformMatrix4fv(suPal,M->boneCount,GL_FALSE,pal);
                glUniformMatrix4fv(suBind,1,GL_FALSE,(float*)(P->pk+M->offBind));
                GLuint*b=&P->vbo[mi*6];
                int tex=P->meshTex[mi];
                if(tex>=0&&(M->flags&2)&&P->tex[tex]){ glBindTexture(GL_TEXTURE_2D,P->tex[tex]); glUniform1f(suUse,1.0f); }
                else glUniform1f(suUse,0.0f);
                glBindBuffer(GL_ARRAY_BUFFER,b[0]); glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,0);
                if(M->flags&1){ glBindBuffer(GL_ARRAY_BUFFER,b[1]); glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,0,0); }
                else glDisableVertexAttribArray(1);
                glBindBuffer(GL_ARRAY_BUFFER,b[2]); glEnableVertexAttribArray(2); glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_FALSE,0,0);
                glBindBuffer(GL_ARRAY_BUFFER,b[3]); glEnableVertexAttribArray(3); glVertexAttribPointer(3,4,GL_UNSIGNED_BYTE,GL_TRUE,0,0);
                if(M->flags&2){ glBindBuffer(GL_ARRAY_BUFFER,b[5]); glEnableVertexAttribArray(4); glVertexAttribPointer(4,2,GL_FLOAT,GL_FALSE,0,0); }
                else glDisableVertexAttribArray(4);
                bodyDrawGL(P,mi,world,b,0,-1);
#ifdef REVA_VK
                g_vkOwner=((unsigned)GFX_KIND_CAST<<28)|((unsigned)i<<12); vkMesh(P,mi,model,(float*)(P->pk+M->offBind),pal,M->boneCount,tint,(tex>=0&&(M->flags&2)&&P->tex[tex])?1.f:0.f,0,NULL);
#endif
                }
#ifdef REVA_VK
            if(hot) for(int k9=0;k9<3;k9++) frameLightAdd(lpv[k9*3],lpv[k9*3+1],lpv[k9*3+2],lcv[k9*3]*1.5f,lcv[k9*3+1]*1.5f,lcv[k9*3+2]*1.5f,0.22f,0.f,0.f,0.f,0.f);   /* the old road's lamps were per body; here a lamp reaches its neighbour, so only the one picked gets them */
#endif
            }
        /* text overlay */
        { static char fb2[48000]; static float tv2[24000];
          glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
          glUseProgram(tp); glUniform2f(tuScr,(float)W,(float)H);
          float ux=(float)W/1280.f, uy=(float)H/720.f;   /* 720p logical layout on any panel */
          #define SEL_TEXT(X,Y,SC,TXT,R,G,B,A) do{ \
              int nq=stb_easy_font_print(0,0,(char*)(TXT),NULL,fb2,sizeof fb2); \
              int nv=0; static const int idq[6]={0,1,2,0,2,3}; \
              for(int i2=0;i2<nq&&nv<23000;i2++) \
                  for(int k2=0;k2<6;k2++){ float*vv=(float*)(fb2+i2*64+idq[k2]*16); \
                      tv2[nv++]=(X)+vv[0]*(SC); tv2[nv++]=(Y)+vv[1]*(SC); } \
              glUniform4f(tuCol,R,G,B,A); \
              glBindBuffer(GL_ARRAY_BUFFER,tvbo); glBufferData(GL_ARRAY_BUFFER,nv*4,tv2,GL_DYNAMIC_DRAW); \
              glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0); \
              for(int k2=1;k2<5;k2++) glDisableVertexAttribArray(k2); \
              glDrawArrays(GL_TRIANGLES,0,nv/2); MENU_HUD(tv2,nv/2,W,H,R,G,B,A); }while(0)
          { const char*ttl="CHARACTER SELECT";
            float sc2=4.0f*ux; float tw=stb_easy_font_width((char*)ttl)*sc2;
            SEL_TEXT(((float)W-tw)*0.5f,54.f*uy,sc2,ttl,0.49f,0.98f,1.0f,0.95f); }
          for(int i=0;i<nc;i++){ float px=((float)W)*0.5f+((float)i-(nc-1)*0.5f)*W*0.26f;
              float sc2=(i==sel?3.4f:2.4f)*ux; float tw=stb_easy_font_width(cand[i].name)*sc2;
              float pu=(i==sel)?(0.75f+0.25f*sinf((float)tsec*6.f)):0.38f;
              SEL_TEXT(px-tw*0.5f,(float)H*0.82f,sc2,cand[i].name,
                       (i==sel)?1.0f*pu:0.6f,(i==sel)?0.48f*pu:0.6f,(i==sel)?0.88f*pu:0.65f,0.95f); }
          SEL_TEXT(28.f*ux,(float)H-40.f*uy,1.5f*ux,"LEFT/RIGHT PICK    A OR ENTER CONFIRM    ESC QUIT",
                   0.22f,1.0f,0.54f,0.75f);
          #undef SEL_TEXT
          glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST); }
        MENU_END(vp2,0.016f,0.010f,0.045f,tsec);
        if(selShot>=0&&fno==selShot){ unsigned char*px2=(unsigned char*)malloc((size_t)W*H*4);
            while(glGetError()!=GL_NO_ERROR){}
            MENU_READ(px2,W,H);
            FILE*sf=fopen("select_shot.ppm","wb");
            if(sf){ fprintf(sf,"P6\n%d %d\n255\n",W,H);
                unsigned char*rw=(unsigned char*)malloc((size_t)W*3);
                for(int y=H-1;y>=0;y--){ for(int x=0;x<W;x++){ rw[x*3]=px2[((size_t)y*W+x)*4];
                    rw[x*3+1]=px2[((size_t)y*W+x)*4+1]; rw[x*3+2]=px2[((size_t)y*W+x)*4+2]; }
                    fwrite(rw,1,(size_t)W*3,sf); } free(rw); fclose(sf);
                printf("select shot: select_shot.ppm %dx%d frame %ld\n",W,H,fno); }
            free(px2); fflush(stdout); }
        SDL_GL_SwapWindow(win);
    }
    if(done<0){ printf("select: interrupted\n"); exit(0); }
    printf("SELECTED: %s (%s)\n",cand[done].name,cand[done].path);
    /* strike the stage: the hero block re-loads the chosen pack fresh */
    for(int i=0;i<nc;i++){ Pack*P=&cp[i];
        for(unsigned m=0;m<P->nMeshes;m++) if(P->vbo[m*6]) glDeleteBuffers(6,P->vbo+m*6);
        for(unsigned t2=0;t2<P->nTex;t2++) if(P->tex[t2]) glDeleteTextures(1,&P->tex[t2]);
        free(P->vbo); free(P->tex); free(P->meshTex); free(P->meshMatFlags); free(P->nodes); free(P->pk); }
    glDeleteProgram(sp); glDeleteProgram(tp); glDeleteBuffers(1,&tvbo);
    free(world); free(T); free(Q); free(S);
    return cand[done].path;
}

/* no doors here: REVA_MODE or REVA_AUTOMODE names a mode by number (the floor, the skate bench, the lab), and otherwise the game plays */
static int modeSelect(SDL_Window*win,int W,int H){ (void)win; (void)W; (void)H;
    const char*e=getenv("REVA_MODE"); if(!e||!e[0]) e=getenv("REVA_AUTOMODE");
    { const char*v9=getenv("REVA_AUTOVAR"); if(v9&&v9[0]) g_modeVariant=atoi(v9); }
    { const char*k9=getenv("REVA_SKATE"); if(k9&&k9[0]=='1') return MODE_SKATE; }
    if(e&&e[0]){ int m=atoi(e); if(m>=0&&m<MODE_N&&MODE_NAME[m]){ printf("MODE: %s\n",MODE_NAME[m]); return m; } printf("[mode] no mode %s here: the game plays\n",e); }
    printf("MODE: %s\n",MODE_NAME[MODE_GAME]); return MODE_GAME; }
#endif
#ifndef CLUB_HAS_TRUETYPE
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>   /* the captions' fonts */
#endif

