/* ================= RIGID BODIES (09-11) ============================================
 * The audit: "Rigid bodies, projectiles, triggers: none. Nothing flies with physics." A
 * thrown crate, a dropped mug, a grenade's arc, a bin knocked over -- every one wanted a
 * body with mass under gravity that bounces off the ground, the world's colliders and each
 * other. This is that layer: a sphere with an orientation, integrated at club gravity (the
 * hair solver's own derivation: 9.81 m/s^2 at 1.75 units a metre), restitution and
 * friction on the ground, the set's circle colliders as walls, pairwise sphere contact,
 * spin from the tangential slip, sleep when still. Drawn as its prop, rotating about its
 * own centre, and it casts a shadow like any prop.
 *   set:  rigid <stem> x y z [scale] [vx vy vz]     ctl: throw=<stem>@x,y,z@vx,vy,vz[@scale]
 *   game: Y (R on the keyboard) in a door throws a crate from her hands along her heading.
 *   REVA_RIGIDDBG=1 prints spawns, bounces and sleeps; anim_bench/mindcheck.py --tag rigid
 *   shoots sets/_rigid.set as a strip. (RB_MAX, RigidBody and the array are declared above setLoad.) */
static int rbSpawn(const char*stem,const float*p,const float*v,float sc){
    if(g_rbDbg<0) g_rbDbg=getenv("REVA_RIGIDDBG")?1:0;
    int pk=setPack(stem); if(pk<0){ printf("[rigid] '%s' NOT FOUND in props/\n",stem); return -1; }
    int i=-1; for(int k=0;k<RB_MAX;k++) if(!g_rb[k].on){ i=k; break; }
    if(i<0){ printf("[rigid] all %d bodies in use\n",RB_MAX); return -1; }
    RigidBody*B=&g_rb[i]; memset(B,0,sizeof*B); B->on=1; B->pk=pk; memcpy(B->p,p,12); memcpy(B->v,v,12); B->q[3]=1.f; B->sc=sc;
    Pack*Q=&g_setPk[pk]; float hh=(Q->propY1-Q->propY0)*0.5f;   /* the MESH's half height, not effH's 3.15 floor: a crate rested a unit in the air */
    B->r=fmaxf(0.12f,sqrtf(Q->propR*Q->propR+hh*hh)*sc*0.72f);   /* a sphere a little inside the box */
    B->rg=fmaxf(0.05f,hh*sc);   /* ⛔ the ground sees the box's half HEIGHT, or a flat crate rests 0.35 up on its sphere */
    B->rest=0.32f; B->fric=0.55f; if(i>=g_nRb) g_nRb=i+1;
    if(g_rbDbg){ printf("[rigid] %d %s at (%.1f,%.1f,%.1f) v (%.1f,%.1f,%.1f) r %.2f\n",i,stem,p[0],p[1],p[2],v[0],v[1],v[2],B->r); fflush(stdout); }
    return i; }
static void rbStep(float dt){
    if(!g_nRb||dt<=0.f) return;
    /* the same gate as the set clock: under a capture the world waits for the camera to roll,
     * or a bench shoots six crates already at rest after a 450-frame boot */
    { static int gate=-1; if(gate<0) gate=(getenv("REVA_SHOT")&&!getenv("REVA_SCENEFPS"))?1:0; if(gate&&!g_capBegun) return; }
    const float G=CLUB_UNITS_PER_S2;
    for(int i=0;i<g_nRb;i++){ RigidBody*B=&g_rb[i]; if(!B->on) continue;
        if(B->still>0.6f) continue;                       /* asleep */
        B->v[1]-=G*dt; float ad=1.f-0.03f*dt; B->v[0]*=ad; B->v[1]*=ad; B->v[2]*=ad;
        for(int k=0;k<3;k++) B->p[k]+=B->v[k]*dt;
        /* the ground */
        if(B->p[1]<B->rg){ B->p[1]=B->rg;
            if(B->v[1]<0.f){ float vn=-B->v[1]; B->v[1]=vn*B->rest; if(B->v[1]<0.35f) B->v[1]=0.f;
                float f=1.f-B->fric*fminf(1.f,dt*12.f+0.25f); B->v[0]*=f; B->v[2]*=f;
                B->w[0]=-B->v[2]/B->r; B->w[2]=B->v[0]/B->r;   /* it rolls the way it slides */
                B->hits++;
                if(vn>1.2f){ sfxPlay("hit",fminf(1.f,vn/9.f)*0.7f,B->p,0);
                    if(g_rbDbg){ printf("[rigid] %d bounce %d: hit the ground at %.1f u/s, up %.1f\n",i,B->hits,vn,B->v[1]); fflush(stdout); } } } }
        /* the world's walls: the set's circle colliders, in XZ */
        for(int c=0;c<g_nCol;c++){ float dx=B->p[0]-g_col[c].x, dz=B->p[2]-g_col[c].z, need=g_col[c].r+B->r; float d2=dx*dx+dz*dz;
            if(d2>=need*need||d2<1e-6f) continue;
            float d=sqrtf(d2); float nx=dx/d, nz=dz/d;
            B->p[0]+=nx*(need-d); B->p[2]+=nz*(need-d);
            float vn=B->v[0]*nx+B->v[2]*nz; if(vn<0.f){ B->v[0]-=(1.f+B->rest)*vn*nx; B->v[2]-=(1.f+B->rest)*vn*nz; B->hits++;
                if(-vn>1.2f){ sfxPlay("hit",fminf(1.f,-vn/9.f)*0.6f,B->p,0); if(g_rbDbg){ printf("[rigid] %d bounce %d: hit a wall at %.1f u/s\n",i,B->hits,-vn); fflush(stdout); } } } }
        /* each other */
        for(int j=i+1;j<g_nRb;j++){ RigidBody*C=&g_rb[j]; if(!C->on) continue;
            float dx=C->p[0]-B->p[0], dy=C->p[1]-B->p[1], dz=C->p[2]-B->p[2], need=B->r+C->r; float d2=dx*dx+dy*dy+dz*dz;
            if(d2>=need*need||d2<1e-6f) continue;
            float d=sqrtf(d2), nx=dx/d, ny=dy/d, nz=dz/d, pen=(need-d)*0.5f;
            B->p[0]-=nx*pen; B->p[1]-=ny*pen; B->p[2]-=nz*pen; C->p[0]+=nx*pen; C->p[1]+=ny*pen; C->p[2]+=nz*pen;
            float rv=(C->v[0]-B->v[0])*nx+(C->v[1]-B->v[1])*ny+(C->v[2]-B->v[2])*nz;
            if(rv<0.f){ float jn=-(1.f+B->rest)*rv*0.5f; B->v[0]-=jn*nx; B->v[1]-=jn*ny; B->v[2]-=jn*nz; C->v[0]+=jn*nx; C->v[1]+=jn*ny; C->v[2]+=jn*nz; C->still=0.f; } }
        /* spin, damped; then the quaternion */
        { float wd=1.f-fminf(1.f,dt*(B->p[1]<=B->rg+0.01f?2.5f:0.3f)); B->w[0]*=wd; B->w[1]*=wd; B->w[2]*=wd;
          float wq[4]={B->w[0]*0.5f*dt,B->w[1]*0.5f*dt,B->w[2]*0.5f*dt,0.f}, dq[4]; qmul(dq,wq,B->q);
          for(int k=0;k<4;k++) B->q[k]+=dq[k];
          float ql=sqrtf(B->q[0]*B->q[0]+B->q[1]*B->q[1]+B->q[2]*B->q[2]+B->q[3]*B->q[3]); if(ql>1e-6f) for(int k=0;k<4;k++) B->q[k]/=ql; }
        /* sleep */
        float sp=sqrtf(B->v[0]*B->v[0]+B->v[1]*B->v[1]+B->v[2]*B->v[2]);
        if(sp<0.12f&&B->p[1]<=B->rg+0.02f){ B->still+=dt; if(B->still>0.6f){ B->v[0]=B->v[1]=B->v[2]=0.f; B->w[0]=B->w[1]=B->w[2]=0.f;
            if(g_rbDbg){ printf("[rigid] %d at rest at (%.1f,%.1f,%.1f) after %d hits\n",i,B->p[0],B->p[1],B->p[2],B->hits); fflush(stdout); } } }
        else B->still=0.f; } }
static void rbDraw(void){
    for(int i=0;i<g_nRb;i++){ RigidBody*B=&g_rb[i]; if(!B->on) continue;
        Pack*Q=&g_setPk[B->pk]; float sc=B->sc*((Q->scale>1e-6f)?Q->scale:1.f); (void)sc;
        float x=B->q[0],y=B->q[1],z=B->q[2],w=B->q[3];
        float R[9]={ 1-2*(y*y+z*z), 2*(x*y+z*w), 2*(x*z-y*w),   2*(x*y-z*w), 1-2*(x*x+z*z), 2*(y*z+x*w),   2*(x*z+y*w), 2*(y*z-x*w), 1-2*(x*x+y*y) };
        float cx=Q->propCx*B->sc, cy=(Q->propY0+Q->propY1)*0.5f*B->sc, cz=Q->propCz*B->sc;   /* the mesh's centre, in the instance's units */
        M4 m; memset(m,0,64);
        m[0]=R[0]*B->sc; m[1]=R[1]*B->sc; m[2]=R[2]*B->sc; m[4]=R[3]*B->sc; m[5]=R[4]*B->sc; m[6]=R[5]*B->sc; m[8]=R[6]*B->sc; m[9]=R[7]*B->sc; m[10]=R[8]*B->sc; m[15]=1.f;
        m[12]=B->p[0]-(m[0]*cx+m[4]*cy+m[8]*cz); m[13]=B->p[1]-(m[1]*cx+m[5]*cy+m[9]*cz); m[14]=B->p[2]-(m[2]*cx+m[6]*cy+m[10]*cz);
        g_vkUse=1.f; g_vkTint[0]=g_vkTint[1]=g_vkTint[2]=1.f; g_vkOwner=((unsigned)GFX_KIND_PROP<<28)|((0xe000u+(unsigned)i)<<12);   /* it used to draw under whatever was drawn last's name and colour */
        drawProp(Q,m); } }

