/* ---- (11-11) A HEADSET IN THE WORLD (gfx_xr.inc is the renderer's side; gfx.h its calls) ----------------------------------------------------------------------
 * 11-11, for an HP Reverb G2. What a game does with a headset is what it does at a desk: it puts its
 * lens (E->lens) where the player's eyes are and turns it the way their body faces. With a headset on, that lens is the ANCHOR: the headset's seated space is put
 * there, turned to face the lens's way (its pitch is the player's own head's, never the lens's: the horizon stays level), its metres made the world's units, and
 * each eye is drawn where the headset says it is, through the runtime's own four angles. The controllers come in as pad 0's buttons and sticks (xrPad), and a game
 * that wants a hand where the player's hand is asks E->xrHand. Recentring (a stick pressed, or the menu button) makes where the head is now the anchor.
 * ⛔ What's drawn, and what pad 0 says: a tape records the pad, never a controller's place, so a session played in a headset can't be replayed. */
static int g_xrDraw=0, g_xrLive=0, g_xrZeroed=0, g_xrZeroAsk=0;
static GfxXrEye g_xrEye[2]; static GfxXrHand g_xrH[2];
static float g_xrAnc[4]={0.f,0.f,0.f,0.f};           /* the anchor: where the game's lens is, and the turn that takes the headset's space into the world */
static float g_xrZero[4]={0.f,0.f,0.f,0.f};          /* where the headset's own space is recentred to: a place, and a yaw */
static float g_xrVP[2][16], g_xrEyeAt[2][3];
static int g_xrLeft=0; static float g_xrTunnelAmt=0.f;   /* (11-11) a game's options: the controllers' sides swapped for a left-handed player; the comfort tunnel this frame */
#define XR_U 1.75f                                    /* the world's units to a metre */
static void xrQRot(const float q[4],const float v[3],float o[3]){   /* a vector turned by a quaternion (x y z w) */
    const float tx=2.f*(q[1]*v[2]-q[2]*v[1]), ty=2.f*(q[2]*v[0]-q[0]*v[2]), tz=2.f*(q[0]*v[1]-q[1]*v[0]);
    o[0]=v[0]+q[3]*tx+(q[1]*tz-q[2]*ty); o[1]=v[1]+q[3]*ty+(q[2]*tx-q[0]*tz); o[2]=v[2]+q[3]*tz+(q[0]*ty-q[1]*tx); }
static void xrYaw(float a,const float v[3],float o[3]){ const float c=cosf(a), s=sinf(a), x=v[0]*c+v[2]*s, z=-v[0]*s+v[2]*c; o[0]=x; o[1]=v[1]; o[2]=z; }   /* about up: (0,0,-1) goes to (-sin a, 0, -cos a) */
/* a direction, and a place, in the headset's space, as they are in the world */
static void xrDir(const float d[3],float o[3]){ float t[3]; xrYaw(-g_xrZero[3],d,t); xrYaw(g_xrAnc[3],t,o); }
static void xrAt(const float p[3],float o[3]){ const float d[3]={(p[0]-g_xrZero[0])*XR_U,(p[1]-g_xrZero[1])*XR_U,(p[2]-g_xrZero[2])*XR_U}; float t[3]; xrDir(d,t); o[0]=g_xrAnc[0]+t[0]; o[1]=g_xrAnc[1]+t[1]; o[2]=g_xrAnc[2]+t[2]; }
/* the head's middle, between the eyes, and the way it faces, in the headset's space */
static void xrHeadRaw(float p[3],float f[3]){ for(int k=0;k<3;k++) p[k]=0.5f*(g_xrEye[0].pos[k]+g_xrEye[1].pos[k]); const float m[3]={0.f,0.f,-1.f}; xrQRot(g_xrEye[0].rot,m,f); }
static void xrZero(void){ float p[3],f[3]; xrHeadRaw(p,f); g_xrZero[0]=p[0]; g_xrZero[1]=p[1]; g_xrZero[2]=p[2]; g_xrZero[3]=atan2f(-f[0],-f[2]); g_xrZeroed=1; g_xrZeroAsk=0;
    printf("[xr] recentred: the head is where the game's eyes are, facing the way they face\n"); fflush(stdout); }
/* a view through one eye: where it is and how it's turned in the world, and the runtime's four angles */
static void xrEyeVP(const GfxXrEye*e,float nearZ,float farZ,float vp[16],float at[3]){
    float r[3],u[3],b[3],rw[3],uw[3],bw[3]; const float X[3]={1.f,0.f,0.f},Y[3]={0.f,1.f,0.f},Z[3]={0.f,0.f,1.f};
    xrQRot(e->rot,X,r); xrQRot(e->rot,Y,u); xrQRot(e->rot,Z,b); xrDir(r,rw); xrDir(u,uw); xrDir(b,bw); xrAt(e->pos,at);
    M4 v,p; memset(v,0,64); v[0]=rw[0]; v[1]=uw[0]; v[2]=bw[0]; v[4]=rw[1]; v[5]=uw[1]; v[6]=bw[1]; v[8]=rw[2]; v[9]=uw[2]; v[10]=bw[2];
    v[12]=-(rw[0]*at[0]+rw[1]*at[1]+rw[2]*at[2]); v[13]=-(uw[0]*at[0]+uw[1]*at[1]+uw[2]*at[2]); v[14]=-(bw[0]*at[0]+bw[1]*at[1]+bw[2]*at[2]); v[15]=1.f;
    const float l=tanf(e->fov[0]), rr=tanf(e->fov[1]), t=tanf(e->fov[2]), d=tanf(e->fov[3]);
    memset(p,0,64); p[0]=2.f/(rr-l); p[8]=(rr+l)/(rr-l); p[5]=2.f/(t-d); p[9]=(t+d)/(t-d); p[10]=(farZ+nearZ)/(nearZ-farZ); p[11]=-1.f; p[14]=2.f*farZ*nearZ/(nearZ-farZ);
    m4_mul(vp,p,v); }
/* this drawn picture's: the anchor from the game's lens, the eyes from the headset. vp becomes a view wide enough for both (the frustum cull's), and eye and ctr
 * the head's middle and where it looks (the listener, the sun's window) */
static void xrViews(float eye[3],float ctr[3],float vp[16],float farZ){
    const float dx=ctr[0]-eye[0], dz=ctr[2]-eye[2]; if(dx*dx+dz*dz>1e-8f) g_xrAnc[3]=atan2f(dx,dz)+3.14159265f;
    g_xrAnc[0]=eye[0]; g_xrAnc[1]=eye[1]; g_xrAnc[2]=eye[2];
    if(!g_xrZeroed||g_xrZeroAsk) xrZero();
    for(int i=0;i<2;i++) xrEyeVP(&g_xrEye[i],0.1f,farZ,g_xrVP[i],g_xrEyeAt[i]);
    GfxXrEye c=g_xrEye[0]; for(int k=0;k<3;k++) c.pos[k]=0.5f*(g_xrEye[0].pos[k]+g_xrEye[1].pos[k]);
    c.fov[0]=fminf(g_xrEye[0].fov[0],g_xrEye[1].fov[0]); c.fov[1]=fmaxf(g_xrEye[0].fov[1],g_xrEye[1].fov[1]); c.fov[2]=fmaxf(g_xrEye[0].fov[2],g_xrEye[1].fov[2]); c.fov[3]=fminf(g_xrEye[0].fov[3],g_xrEye[1].fov[3]);
    float at[3]; xrEyeVP(&c,0.1f,farZ,vp,at);
    float p[3],f[3],fw[3]; xrHeadRaw(p,f); xrDir(f,fw); eye[0]=at[0]; eye[1]=at[1]; eye[2]=at[2]; ctr[0]=at[0]+fw[0]; ctr[1]=at[1]+fw[1]; ctr[2]=at[2]+fw[2]; }
/* the controllers as pad 0: a trigger or a grip is A (fire, take hold), the right hand's lower button B (use) and upper Y (reload), the left's lower X (an order)
 * and upper RB (crouch), the left stick walks and the right one turns. A stick pressed recentres, and the menu button is Back, which a game opens its options with.
 * Left-handed (E->xrHanded), the two hands' parts are swapped: the left hand's buttons are the right's, and its stick turns */
static void xrPad(EnginePad*d){
    const GfxXrHand*L=&g_xrH[g_xrLeft?1:0],*R=&g_xrH[g_xrLeft?0:1]; if(!L->on&&!R->on) return;
    unsigned b=0; if(R->trigger>0.55f||R->squeeze>0.55f||L->trigger>0.55f||L->squeeze>0.55f) b|=PAD_A;
    if(R->buttons&1) b|=PAD_B;
    if(R->buttons&2) b|=PAD_Y;
    if(L->buttons&1) b|=PAD_X;
    if(L->buttons&2) b|=PAD_RB;
    if((L->buttons|R->buttons)&8) b|=PAD_BACK;
    d->held|=b; d->connected=1;
    if(fabsf(L->stick[0])+fabsf(L->stick[1])>fabsf(d->x)+fabsf(d->y)){ d->x=L->stick[0]; d->y=L->stick[1]; }
    if(fabsf(R->stick[0])>fabsf(d->rx)) d->rx=R->stick[0];
    if(fabsf(R->stick[1])>fabsf(d->ry)) d->ry=R->stick[1];
    static unsigned was=0; const unsigned now=(L->buttons|R->buttons)&4u; if(now&~was) g_xrZeroAsk=1; was=now; }
