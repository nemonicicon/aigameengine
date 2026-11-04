/* ================= THE POST PASS (09-11) ==========================================
 * The audit ranked "a post pass" fifth for the game and the film alike: the club had no
 * bloom, no anti-aliasing and no fog, the tone knee lived in two mesh shaders (and not the
 * skate, hologram, sky or neon programs), and the film graded in ffmpeg. One full-screen
 * FBO now carries the whole 3D frame: bloom (threshold -> quarter-res -> separable blur ->
 * add), FXAA, and the SAME tone curve the mesh shaders used, applied once to everything.
 * The HUD draws after it, sharp. Measured before building: OES_depth24 and
 * EXT_color_buffer_half_float are present; OES_depth_texture is NOT (the shadow pass has
 * to pack depth into colour).
 *   post=0|1   bloom=<add strength>   fxaa=0|1   fog=<density>[,r,g,b]   (ctl; REVA_POST /
 *   REVA_BLOOM / REVA_FXAA / REVA_FOG env). Default: ON in the game doors, OFF in the lab
 *   and the film (mode 11), so every strict bench shot and every scene stays byte-identical
 *   until it asks -- the lab keeps its in-shader knee. A frame with the pass on says so once. */
#ifndef GL_DEPTH_COMPONENT24_OES
#define GL_DEPTH_COMPONENT24_OES 0x81A6
#endif
static int   g_post=-1;                     /* -1 = by mode; 0/1 forced (ctl/env) */
static int   g_postOn=0;                    /* the decision for THIS frame */
/* ⛔ MEASURED, not guessed (09-11): bloom 0.30 over a 0.72 threshold turned a noon street
 * into glare -- white siding and a sunlit lawn are both above 0.72, so the whole frame
 * bloomed. 0.16 over 0.86 leaves the neon and the sun's own highlights as the only
 * sources. And with the knee unset (tone=0) the post pass rolls at 0.70, the value the
 * film measured (23.1% -> 0.0% clipped), because a pass that can stop clipping should. */
/* ⛔ AND 0.16 OVER 0.86 STILL VEILED THE STREET: in an 8-bit buffer a 0.9 sky and a 1.0 neon
 * tracer are the same kind of pixel, so anything below ~0.95 blooms the whole sky. Only
 * saturated pixels bloom: 0.12 over 0.95. (An HDR buffer is what separates "bright" from
 * "white"; EXT_color_buffer_half_float is present when that day comes.) */
static float g_bloom=0.12f; static int g_fxaa=1; static float g_bloomThr=0.95f, g_postKnee=0.70f;
/* ⭐ (10-23) THE FILM'S LOOK, in the post pass (jacob: "a general like shader/reflections pass"): grade= a gentle S, the
 * shadows a little cool and the highlights a little warm, a touch more colour (0 off, 1 the show's); vig= the corners
 * fall away (0..1). ctl grade= vig=, REVA_GRADE / REVA_VIG. REVA_SSAA=2: the 3D frame drawn at twice the size into
 * the post target and resolved by the post quad's own bilinear tap (a 2x2 box): a take's ink lines and edges
 * antialiased, the HUD still drawn sharp at the window's size. Boot only; never on the multi-cam wall (its panels are
 * laid out in screen pixels). */
static float g_grade=0.f, g_vig=0.f; static int g_ssaa=1; static GLint g_pu_grade=-1, g_pu_vig=-1;
static int g_labGrid;   /* a tentative definition: the lab's own is below */
/* ---- RENDER SCALE (2026-09-30): the ladder's biggest rung ------------------------------------
 * A Pi 4 is fill-bound: its GPU shades about a gigapixel a second in practice, and 720p with a
 * shadow pass and a post pass is three screens a frame. So the 3D frame is drawn into a SUB-RECT
 * of the post target (g_rW x g_rH = g_rscale of the screen) and the post quad, which already
 * runs once over every pixel, upsamples it: no second target, no reallocation, changeable
 * every frame. Every fetch in the two post shaders goes through fetch(): uv*uvs, clamped to
 * the rect. The anim lab (mode ANIM) is an instrument and always draws at 1:1; its multi-cam
 * grid is laid out in screen pixels and would land off the rect. */
static float g_rscale=1.f; static int g_rW=0,g_rH=0,g_rsOn=0; static GLint g_pu_uvs=-1,g_bu_uvs=-1;
static void tierApply(int t,int boot){
    const Tier*T=&TIERS[t]; g_tier=t;
    g_rscale=T->rscale; g_bloom=T->bloom; g_fxaa=T->fxaa; g_clothSub=T->clothSub;
    if(boot) g_sunMap=T->sunMap;
    printf("[tier] %s: render %.0f%%, sun map %d%s, bloom %.2f, fxaa %d, cloth x%d%s\n",T->name,T->rscale*100.f,
           boot?T->sunMap:g_sunMap,boot?"":" (boot only)",T->bloom,T->fxaa,T->clothSub,""); fflush(stdout); }
#define MAINVIEW(W,H) glViewport(0,0,(g_mainFB&&g_mainFB==g_postFbo)?g_rW:(W),(g_mainFB&&g_mainFB==g_postFbo)?g_rH:(H))
static float g_fish=0.f;      /* ⭐ 0 = the lens we had, 1 = a full equidistant fisheye; ~0.6 is the VX look */
static float g_tape=0.f;      /* ⭐ the tape frame: 4:3 pillarbox, vignette, grain, a REC dot */
static int   g_camHoldSlot=-1;   /* ⭐ the slot whose camera this shot is coming from: never drawn */
static float g_tapeT=0.f;     /* its own clock, for the grain and the blink */
static float g_fishTh=0.f;    /* the DIAGONAL half-angle the final frame covers, radians (for the shader) */
static float g_fishAsp=1.f;
/* The 3D pass must cover the same total field RECTILINEARLY, so the warp has real geometry to bend at the edge
 * instead of stretched border pixels. Centre scale is preserved by construction: at r->0 the fisheye's
 * d(theta)/dr is th, and matching it to the rectilinear tan(th_authored) is what sets the target field. */
static float fishFov(float fov,float asp){
    if(g_fish<=0.f){ g_fishTh=0.f; return fov; }
    g_fishAsp=asp;
    float d=sqrtf(1.f+asp*asp);
    float th=atanf(d*tanf(fov*0.5f*0.0174532925f));       /* the authored diagonal half-angle */
    /* A FISHEYE IS A WIDE LENS AS WELL AS A BENT ONE. Morphing the projection at the authored fov moved the
     * field by six degrees and looked like nothing: a Century on a VX sees ~180, and the filmer stands a
     * foot away because of it. `fish` opens the field toward 120 degrees diagonal AND bends it. */
    const float FISH_MAX=1.05f;                           /* 60 deg off axis = 120 diagonal */
    float tgt=th+((FISH_MAX>th?FISH_MAX:th)-th)*g_fish;
    if(tgt>1.45f) tgt=1.45f;                              /* 83 degrees off axis: past that a rectilinear render dies */
    g_fishTh=tgt;
    float f=2.f*atanf(tanf(tgt)/d)*57.2957795f;
    return (f>170.f)?170.f:f; }
static float g_fog[4]={0.f,0.f,0.f,0.f};    /* rgb, density (0 = off) */
static int   g_fogAuto=1;                   /* colour from the sky horizon unless a scene names one */
static GLuint g_postFbo=0,g_postTex=0,g_postDs=0; static int g_postW=0,g_postH=0;
static GLuint g_blmFbo[2]={0,0},g_blmTex[2]={0,0}; static int g_blmW=0,g_blmH=0;
static GLuint g_postProg=0,g_blurProg=0,g_postVbo=0;
static GLint  g_pu_tex=-1,g_pu_blm=-1,g_pu_px=-1,g_pu_tone=-1,g_pu_bloom=-1,g_pu_fxaa=-1,g_pu_fish=-1,g_pu_asp=-1,g_pu_tht=-1,g_pu_feed=-1,g_pu_feedT=-1;
static GLint  g_bu_tex=-1,g_bu_px=-1,g_bu_dir=-1,g_bu_thr=-1;
static GLuint g_mainFB=0;                   /* where "the screen" is this frame: 0, or the post FBO */
/* the sky's horizon colour on the C side -- ONE formula; the skybox and the water used to
 * carry it as two identical literals, and the fog wants the same answer */
static void skyHorizonC(float*hz){ hz[0]=g_sky[0]*1.85f+0.10f; hz[1]=g_sky[1]*1.80f+0.11f; hz[2]=g_sky[2]*1.70f+0.12f; }
static GLuint postTex(int w,int h){ GLuint t; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    return t; }
static void postInit(int W,int H){
    { const char*e=getenv("REVA_SSAA"); if(e&&e[0]){ int s=atoi(e); g_ssaa=(s>=2)?2:1; } }
    if(g_ssaa>1){ W*=g_ssaa; H*=g_ssaa; printf("[post] SSAA x%d: the 3D frame at %dx%d\n",g_ssaa,W,H); }
    const char*vsQ="attribute vec2 p; varying vec2 uv; void main(){ uv=p*0.5+0.5; gl_Position=vec4(p,0.0,1.0); }\n";
    /* threshold + 9-tap gaussian along dir (px units). thr>0 only on the first (downsample) pass. */
    const char*fsB=
      "precision mediump float; varying vec2 uv; uniform sampler2D tex; uniform vec2 px; uniform vec2 dir; uniform float thr; uniform vec2 uvs;\n"
      "vec3 fetch(sampler2D t,vec2 q){ return texture2D(t,min(q,vec2(1.0))*uvs).rgb; }\n"
      "void main(){ float w[5]; w[0]=0.227; w[1]=0.194; w[2]=0.121; w[3]=0.054; w[4]=0.016; vec3 c=vec3(0.0);\n"
      "  for(int i=-4;i<=4;i++){ vec3 s=fetch(tex,uv+dir*px*float(i));\n"
      "    if(thr>0.0) s=max(s-vec3(thr),vec3(0.0))/(1.0-thr);\n"
      "    c+=s*w[i<0?-i:i]; }\n"
      "  gl_FragColor=vec4(c,1.0); }\n";
    /* FXAA (the compact 3.11-style edge blend) -> + bloom -> the highlight knee, once, on everything */
    const char*fsP=
      "precision mediump float; varying vec2 uv; uniform sampler2D tex; uniform sampler2D blm; uniform vec2 px;\n"
      "uniform float tone; uniform float bloom; uniform float fxaa; uniform float fish; uniform float asp; uniform float tht;\n"
      "uniform float feed; uniform float feedT; uniform vec2 uvs; uniform float grade; uniform float vig;\n"
      "vec3 fetch(sampler2D t,vec2 q){ return texture2D(t,min(q,vec2(1.0))*uvs).rgb; }\n"
      "float fhash(vec2 p){ return fract(sin(dot(p,vec2(12.9898,78.233)))*43758.5453); }\n"
      "float luma(vec3 c){ return dot(c,vec3(0.299,0.587,0.114)); }\n"
      /* \u2b50 THE FISHEYE, as a projection and not a polynomial: the radius on the frame is the ANGLE off the
       * lens axis (r = f*theta), so the source is sampled at tan(theta) -- which is where a rectilinear render
       * put that ray. The corner maps to the corner; `fish` blends the whole thing back to the identity. */
      "vec2 warpA(vec2 t,float aa){ if(fish<=0.0) return t;\n"
      "  vec2 c=t*2.0-1.0; c.x*=aa;\n"
      "  float rmax=sqrt(1.0+aa*aa); float rn=length(c)/rmax;\n"
      "  if(rn<1e-5) return t;\n"
      "  float src=tan(rn*tht)/tan(tht);\n"
      "  c*=mix(rn,src,fish)/rn;\n"
      "  c.x/=aa; return c*0.5+0.5; }\n"
      /* \u2b50 seven-segment digits, drawn in the shader: the phone's timer, without dragging a font in */
      "float seg(vec2 p,vec2 a,vec2 b,float w){ vec2 pa=p-a, ba=b-a;\n"
      "  float h=clamp(dot(pa,ba)/dot(ba,ba),0.0,1.0);\n"
      "  return step(length(pa-ba*h),w); }\n"
      "float digit(vec2 p,int d){\n"
      "  if(p.x<-0.1||p.x>0.6||p.y<-0.1||p.y>1.1) return 0.0;\n"
      "  float on=0.0; float w=0.055;\n"
      "  bool A=(d!=1&&d!=4), B=(d!=5&&d!=6), C=(d!=2), D=(d!=1&&d!=4&&d!=7),\n"
      "       E=(d==0||d==2||d==6||d==8), F=(d!=1&&d!=2&&d!=3&&d!=7), G=(d!=0&&d!=1&&d!=7);\n"
      "  if(A) on=max(on,seg(p,vec2(0.05,1.0),vec2(0.45,1.0),w));\n"
      "  if(B) on=max(on,seg(p,vec2(0.5,0.95),vec2(0.5,0.55),w));\n"
      "  if(C) on=max(on,seg(p,vec2(0.5,0.45),vec2(0.5,0.05),w));\n"
      "  if(D) on=max(on,seg(p,vec2(0.05,0.0),vec2(0.45,0.0),w));\n"
      "  if(E) on=max(on,seg(p,vec2(0.0,0.45),vec2(0.0,0.05),w));\n"
      "  if(F) on=max(on,seg(p,vec2(0.0,0.95),vec2(0.0,0.55),w));\n"
      "  if(G) on=max(on,seg(p,vec2(0.05,0.5),vec2(0.45,0.5),w));\n"
      "  return on; }\n"
      "void main(){\n"
      /* \u2b50 THE PHONE'S FRAME FIRST. Everything after this is inside her screen. */
      "  vec2 fu=uv; float fasp=asp; float wf=1.0;\n"
      "  if(feed>0.5){\n"
      "    fasp=(feed>1.5)?(16.0/9.0):(9.0/16.0);\n"
      "    wf=clamp(fasp/asp,0.05,1.0);\n"
      "    float dx=(uv.x-0.5)/wf;\n"
      "    if(abs(dx)>0.5){ gl_FragColor=vec4(0.0,0.0,0.0,1.0); return; }\n"
      "    fu=vec2(dx+0.5,uv.y); }\n"
      "  vec2 fw=warpA(fu,fasp);\n"
      "  vec2 uvw=vec2(0.5+(fw.x-0.5)*wf,fw.y);\n"
      "  vec3 col=fetch(tex,uvw);\n"
      "  if(fxaa>0.5){\n"
      "    vec3 nw=fetch(tex,uvw+vec2(-1.0,-1.0)*px), ne=fetch(tex,uvw+vec2(1.0,-1.0)*px);\n"
      "    vec3 sw=fetch(tex,uvw+vec2(-1.0,1.0)*px),  se=fetch(tex,uvw+vec2(1.0,1.0)*px);\n"
      "    float lnw=luma(nw),lne=luma(ne),lsw=luma(sw),lse=luma(se),lm=luma(col);\n"
      "    float lmin=min(lm,min(min(lnw,lne),min(lsw,lse))), lmax=max(lm,max(max(lnw,lne),max(lsw,lse)));\n"
      "    vec2 d=vec2(-((lnw+lne)-(lsw+lse)),((lnw+lsw)-(lne+lse)));\n"
      "    float dr=max((lnw+lne+lsw+lse)*(0.25*0.125),1.0/128.0);\n"
      "    float rd=1.0/(min(abs(d.x),abs(d.y))+dr);\n"
      "    d=min(vec2(8.0),max(vec2(-8.0),d*rd))*px;\n"
      "    vec3 a=0.5*(fetch(tex,uvw+d*(1.0/3.0-0.5))+fetch(tex,uvw+d*(2.0/3.0-0.5)));\n"
      "    vec3 b=a*0.5+0.25*(fetch(tex,uvw+d*-0.5)+fetch(tex,uvw+d*0.5));\n"
      "    float lb=luma(b); col=(lb<lmin||lb>lmax)?a:b; }\n"
      "  col+=fetch(blm,uvw)*bloom;\n"
      /* ⛔ THE KNEE IS ON LUMINANCE HERE, NOT PER CHANNEL. The mesh shaders' per-channel knee
       * (the film's, measured on faces) rolls a lawn's green alone and leaves its red and blue
       * -- a saturated bright colour comes out grey. Rolling the luminance and scaling the
       * colour keeps the lawn green and still stops the siding clipping. */
      "  if(tone>0.0){ float l=luma(col); float ov=max(l-tone,0.0); float wp=0.96;\n"
      "    float l2=min(l,tone)+(wp-tone)*(1.0-exp(-ov/(wp-tone)));\n"
      "    col*=l2/max(l,1e-4); }\n"
      /* \u2b50 (10-23) THE GRADE and THE VIGNETTE: after the knee, before a camera's own frame */
      "  if(grade>0.0){ float l0=luma(col); vec3 s=clamp(col,0.0,1.0); s=s*s*(3.0-2.0*s); col=mix(col,s,0.16*grade);\n"
      "    col+=vec3(-0.012,-0.002,0.020)*grade*(1.0-smoothstep(0.0,0.45,l0));\n"
      "    col*=mix(vec3(1.0),vec3(1.03,1.005,0.965),grade*smoothstep(0.45,1.0,l0));\n"
      "    float l1=luma(col); col=mix(vec3(l1),col,1.0+0.07*grade); }\n"
      "  if(vig>0.0){ vec2 dv=(fu-0.5)*vec2(fasp,1.0)/sqrt(fasp*fasp+1.0)*2.0; col*=1.0-vig*0.32*smoothstep(0.55,1.25,length(dv)); }\n"
      /* \u2b50 THE TAPE FRAME. The picture is squeezed to 4:3 inside the delivery frame, the corners fall away,
       * the grain moves, and the dot blinks once a second. All of it AFTER the tone knee, because a camera's
       * own frame is not part of the scene's exposure. */
      "  if(feed>0.5){\n"
      "    float r=length((fu-0.5)*vec2(1.10,1.0));\n"
      "    col*=mix(1.0,0.84,smoothstep(0.42,0.80,r));\n"                    /* a phone lens falls off a little */
      "    float g=fhash(fu*vec2(720.0,1280.0)+vec2(feedT*37.0,feedT*29.0));\n"
      "    col+=(g-0.5)*0.022;\n"                                            /* sensor noise, not tape grain */
      /* the red dot and the running timer, top left of HER screen */
      "    float sc=(feed>1.5)?1.0:(9.0/16.0);\n"                            /* keep the chrome square either way */
      "    vec2 q=vec2((fu.x-0.055)/sc,(fu.y-0.945)/1.0);\n"
      "    float blink=step(0.35,fract(feedT));\n"
      "    if(blink>0.5&&length(q*vec2(1.0,1.0))<0.013) col=mix(col,vec3(0.95,0.12,0.10),0.95);\n"
      "    float tsec=floor(mod(feedT,60.0)); float tmin=floor(mod(feedT/60.0,10.0));\n"
      "    vec2 tp=vec2((fu.x-0.085)/(0.030*sc),(fu.y-0.935)/0.024);\n"
      "    float ink=0.0;\n"
      "    ink=max(ink,digit(tp,int(tmin)));\n"
      "    ink=max(ink,step(0.0,1.0)*step(length(vec2((tp.x-0.85)*1.0,(tp.y-0.30)))*1.0,0.07));\n"
      "    ink=max(ink,step(length(vec2((tp.x-0.85),(tp.y-0.72))),0.07));\n"
      "    ink=max(ink,digit(tp-vec2(1.15,0.0),int(floor(tsec/10.0))));\n"
      "    ink=max(ink,digit(tp-vec2(1.85,0.0),int(mod(tsec,10.0))));\n"
      "    col=mix(col,vec3(1.0),clamp(ink,0.0,1.0)*0.92);\n"
      "  }\n"
      "  gl_FragColor=vec4(col,1.0); }\n";
    g_postW=W; g_postH=H; g_blmW=W/4; g_blmH=H/4;
    glGenFramebuffers(1,&g_postFbo); g_postTex=postTex(W,H);
    glGenRenderbuffers(1,&g_postDs); glBindRenderbuffer(GL_RENDERBUFFER,g_postDs);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24_OES,W,H);
    glBindFramebuffer(GL_FRAMEBUFFER,g_postFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,g_postTex,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,g_postDs);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){
        printf("[post] FBO incomplete (0x%x) -- post pass DISABLED\n",glCheckFramebufferStatus(GL_FRAMEBUFFER)); g_postFbo=0; glBindFramebuffer(GL_FRAMEBUFFER,0); return; }
    for(int i=0;i<2;i++){ glGenFramebuffers(1,&g_blmFbo[i]); g_blmTex[i]=postTex(g_blmW,g_blmH);
        glBindFramebuffer(GL_FRAMEBUFFER,g_blmFbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,g_blmTex[i],0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){ printf("[post] bloom FBO %d incomplete -- post pass DISABLED\n",i); g_postFbo=0; glBindFramebuffer(GL_FRAMEBUFFER,0); return; } }
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    GLuint p=glCreateProgram(); glAttachShader(p,sh(GL_VERTEX_SHADER,vsQ)); glAttachShader(p,sh(GL_FRAGMENT_SHADER,fsP));
    glBindAttribLocation(p,0,"p"); glLinkProgram(p); { GLint ok; glGetProgramiv(p,GL_LINK_STATUS,&ok); if(!ok) die("post link"); }
    g_postProg=p; g_pu_tex=glGetUniformLocation(p,"tex"); g_pu_blm=glGetUniformLocation(p,"blm"); g_pu_px=glGetUniformLocation(p,"px"); g_pu_uvs=glGetUniformLocation(p,"uvs");
    g_pu_tone=glGetUniformLocation(p,"tone"); g_pu_bloom=glGetUniformLocation(p,"bloom"); g_pu_fxaa=glGetUniformLocation(p,"fxaa");
    g_pu_fish=glGetUniformLocation(p,"fish"); g_pu_asp=glGetUniformLocation(p,"asp"); g_pu_tht=glGetUniformLocation(p,"tht");
    g_pu_feed=glGetUniformLocation(p,"feed"); g_pu_feedT=glGetUniformLocation(p,"feedT");
    g_pu_grade=glGetUniformLocation(p,"grade"); g_pu_vig=glGetUniformLocation(p,"vig");
    GLuint b=glCreateProgram(); glAttachShader(b,sh(GL_VERTEX_SHADER,vsQ)); glAttachShader(b,sh(GL_FRAGMENT_SHADER,fsB));
    glBindAttribLocation(b,0,"p"); glLinkProgram(b); { GLint ok; glGetProgramiv(b,GL_LINK_STATUS,&ok); if(!ok) die("blur link"); }
    g_blurProg=b; g_bu_tex=glGetUniformLocation(b,"tex"); g_bu_px=glGetUniformLocation(b,"px"); g_bu_uvs=glGetUniformLocation(b,"uvs"); g_bu_dir=glGetUniformLocation(b,"dir"); g_bu_thr=glGetUniformLocation(b,"thr");
    static const float q[12]={-1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1};
    glGenBuffers(1,&g_postVbo); glBindBuffer(GL_ARRAY_BUFFER,g_postVbo); glBufferData(GL_ARRAY_BUFFER,sizeof q,q,GL_STATIC_DRAW);
    { const char*e; if((e=getenv("REVA_FEED"))&&e[0]){ g_tape=(float)atoi(e); if(g_tape<0.f)g_tape=0.f; if(g_tape>2.f)g_tape=2.f; if(g_tape>0.f) g_post=1; printf("[feed] %s (REVA_FEED)\n",(g_tape>0.f)?"ON":"off"); }
      if((e=getenv("REVA_FISH"))&&e[0]){ g_fish=(float)atof(e); if(g_fish<0.f)g_fish=0.f; if(g_fish>1.f)g_fish=1.f;
          if(g_fish>0.f) g_post=1;
          printf("[fish] %.2f (REVA_FISH)\n",g_fish); }
      if((e=getenv("REVA_POST"))&&e[0]) g_post=atoi(e)?1:0;
      if((e=getenv("REVA_BLOOM"))&&e[0]) g_bloom=(float)atof(e);
      if((e=getenv("REVA_FXAA"))&&e[0]) g_fxaa=atoi(e)?1:0;
      if((e=getenv("REVA_GRADE"))&&e[0]) g_grade=(float)atof(e);
      if((e=getenv("REVA_VIG"))&&e[0]) g_vig=(float)atof(e);
      if((e=getenv("REVA_FOG"))&&e[0]){ float d=0,r=0,g=0,bb=0; int n=sscanf(e,"%f,%f,%f,%f",&d,&r,&g,&bb);
          g_fog[3]=d; if(n==4){ g_fog[0]=r; g_fog[1]=g; g_fog[2]=bb; g_fogAuto=0; } } }
    printf("[post] ready %dx%d, bloom %dx%d (post=%d bloom=%.2f fxaa=%d fog=%.3f)\n",W,H,g_blmW,g_blmH,g_post,g_bloom,g_fxaa,g_fog[3]); fflush(stdout); }
static void postQuad(void){ glBindBuffer(GL_ARRAY_BUFFER,g_postVbo); glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0); for(int k=1;k<5;k++) glDisableVertexAttribArray(k);
    glDrawArrays(GL_TRIANGLES,0,6); }
/* the decision for this frame, made once at the top of it so every uniform upload agrees */
static void postDecide(int mode,int modeAnim){
    int want=(g_post>=0)?g_post:(mode!=modeAnim);
    if(g_ssaa>1) want=(mode==modeAnim&&g_labGrid>1)?0:1;   /* ⭐ (10-23) the post quad IS the resolve */
    g_rsOn=(g_rscale<0.999f&&mode!=modeAnim&&g_postFbo)?1:0; if(g_rsOn) want=1;   /* the post quad IS the upsampler */
    g_rW=g_rsOn?(int)(g_postW*g_rscale+0.5f):g_postW; g_rH=g_rsOn?(int)(g_postH*g_rscale+0.5f):g_postH;
    if(g_rW<64) g_rW=64;
    if(g_rH<64) g_rH=64;
    g_postOn=(want&&g_postFbo)?1:0;
    static int said=-1; if(said!=g_postOn){ said=g_postOn; printf("[post] %s\n",g_postOn?"ON: bloom + fxaa + tone in one pass":"off"); fflush(stdout); } }
static void postBegin(int W,int H){ if(!g_postOn){ g_mainFB=0; return; }
    g_mainFB=g_postFbo; glBindFramebuffer(GL_FRAMEBUFFER,g_postFbo); glViewport(0,0,g_rW,g_rH); (void)W; (void)H; }
static void postEnd(int W,int H,float tone){ if(!g_postOn) return;
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    float rs=g_rsOn?g_rscale:1.f; int bW=(int)(g_blmW*rs+0.5f), bH=(int)(g_blmH*rs+0.5f); if(bW<16) bW=16; if(bH<16) bH=16;
    if(g_bloom>0.f){ glUseProgram(g_blurProg); glUniform1i(g_bu_tex,0); if(g_bu_uvs>=0) glUniform2f(g_bu_uvs,rs,rs);
        /* pass 1: threshold + horizontal blur, full -> quarter */
        glBindFramebuffer(GL_FRAMEBUFFER,g_blmFbo[0]); glViewport(0,0,bW,bH);
        glBindTexture(GL_TEXTURE_2D,g_postTex); glUniform2f(g_bu_px,1.f/g_rW,1.f/g_rH); glUniform2f(g_bu_dir,2.f,0.f); glUniform1f(g_bu_thr,g_bloomThr); postQuad();
        /* pass 2: vertical blur, quarter -> quarter */
        glBindFramebuffer(GL_FRAMEBUFFER,g_blmFbo[1]);
        glBindTexture(GL_TEXTURE_2D,g_blmTex[0]); glUniform2f(g_bu_px,1.f/bW,1.f/bH); glUniform2f(g_bu_dir,0.f,1.f); glUniform1f(g_bu_thr,0.f); postQuad();
        /* pass 3: horizontal again, wider (the quarter-res taps are 4 screen px each) */
        glBindFramebuffer(GL_FRAMEBUFFER,g_blmFbo[0]);
        glBindTexture(GL_TEXTURE_2D,g_blmTex[1]); glUniform2f(g_bu_dir,1.f,0.f); postQuad(); }
    glBindFramebuffer(GL_FRAMEBUFFER,0); glViewport(0,0,W,H); g_mainFB=0;
    glUseProgram(g_postProg); glUniform1i(g_pu_tex,0); glUniform1i(g_pu_blm,1);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,g_bloom>0.f?g_blmTex[0]:g_postTex);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,g_postTex);
    glUniform2f(g_pu_px,1.f/g_rW,1.f/g_rH); if(g_pu_uvs>=0) glUniform2f(g_pu_uvs,rs,rs); glUniform1f(g_pu_tone,tone>0.f?tone:g_postKnee); glUniform1f(g_pu_bloom,g_bloom>0.f?g_bloom:0.f); glUniform1f(g_pu_fxaa,(float)(g_fxaa&&g_ssaa<2));
    if(g_pu_grade>=0) glUniform1f(g_pu_grade,g_grade);
    if(g_pu_vig>=0) glUniform1f(g_pu_vig,g_vig);
    glUniform1f(g_pu_fish,(g_fishTh>0.f)?g_fish:0.f); glUniform1f(g_pu_asp,(H>0)?(VIEW_ASPECT(W,H)):1.777f);
    glUniform1f(g_pu_tht,(g_fishTh>0.f)?g_fishTh:0.8f);
    g_tapeT+=(float)g_animDtS; if(g_tapeT>1e6f) g_tapeT=0.f;
    glUniform1f(g_pu_feed,g_tape); glUniform1f(g_pu_feedT,g_tapeT);
    postQuad();
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); }
