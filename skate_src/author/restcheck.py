"""Does the pack's OWN rest pose reconstruct through its OWN stored inverse binds?
   v' = sum_i w_i * (W[boneMap[j_i]] . IBM[j_i]) . v      should give v' == v.
   This is the test that matters; the IBM = inv(bone)*meshWorld formula is only one way
   a baker might have produced them."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rpack import Rpack
from fingers import m4_trs, m4_mul, m4_inv_affine, xform, MESH
def worlds(p):
    W=[]
    for nd in p.nodes():
        loc=m4_trs(nd["t"],nd["q"],nd["s"])
        W.append(loc if nd["parent"]<0 else m4_mul(W[nd["parent"]],loc))
    return W
for path in [a for a in sys.argv[1:] if not a.startswith("--")]:
    p=Rpack(path); W=worlds(p)
    rec=list(MESH.unpack_from(p.buf,p.offMeshes))
    (mName,mNode,verts,idxCount,boneCount,flags,offPos,offNorm,offUV,
     offJ,offWt,offIdx,offBM,offIBM,offBind)=rec
    bm=list(struct.unpack_from("<%dH"%boneCount,p.buf,offBM))
    pal=[m4_mul(W[bm[b]], list(struct.unpack_from("<16f",p.buf,offIBM+b*64)))
         for b in range(boneCount)]
    pos=struct.unpack_from("<%df"%(verts*3),p.buf,offPos)
    J=struct.unpack_from("<%dB"%(verts*4),p.buf,offJ)
    Wt=struct.unpack_from("<%dB"%(verts*4),p.buf,offWt)
    worst=0.0; n=0
    for v in range(verts):
        vx=[pos[v*3],pos[v*3+1],pos[v*3+2]]
        acc=[0.0,0.0,0.0]; tw=0.0
        for k in range(4):
            w=Wt[v*4+k]/255.0
            if w<=0: continue
            j=J[v*4+k]
            if j>=boneCount: continue
            q=xform(pal[j],vx)
            acc=[acc[i]+w*q[i] for i in range(3)]; tw+=w
        if tw<=0: continue
        d=max(abs(acc[i]-vx[i]) for i in range(3))
        worst=max(worst,d); n+=1
    print("%-26s verts %6d skinned %6d   worst rest-pose drift %.4e"
          %(os.path.basename(path),verts,n,worst))

# --- where is the drift? (run with --where) ---
if "--where" in sys.argv:
    path = [a for a in sys.argv[1:] if not a.startswith("--")][0]
    p=Rpack(path); W=worlds(p)
    rec=list(MESH.unpack_from(p.buf,p.offMeshes))
    (mName,mNode,verts,idxCount,boneCount,flags,offPos,offNorm,offUV,
     offJ,offWt,offIdx,offBM,offIBM,offBind)=rec
    bm=list(struct.unpack_from("<%dH"%boneCount,p.buf,offBM))
    pal=[m4_mul(W[bm[b]], list(struct.unpack_from("<16f",p.buf,offIBM+b*64)))
         for b in range(boneCount)]
    pos=struct.unpack_from("<%df"%(verts*3),p.buf,offPos)
    J=struct.unpack_from("<%dB"%(verts*4),p.buf,offJ)
    Wt=struct.unpack_from("<%dB"%(verts*4),p.buf,offWt)
    names=p.node_names()
    ds=[]
    for v in range(verts):
        vx=[pos[v*3],pos[v*3+1],pos[v*3+2]]
        acc=[0.0,0.0,0.0]; tw=0.0; dom=-1; dw=-1
        for k in range(4):
            w=Wt[v*4+k]/255.0
            if w<=0: continue
            j=J[v*4+k]
            if j>=boneCount: continue
            if w>dw: dw,dom=w,j
            q=xform(pal[j],vx)
            acc=[acc[i]+w*q[i] for i in range(3)]; tw+=w
        if tw<=0: continue
        ds.append((max(abs(acc[i]-vx[i]) for i in range(3)), v, vx[1], dom))
    ds.sort(reverse=True)
    import statistics
    print("drift percentiles: p50 %.3e  p90 %.3e  p99 %.3e  max %.3e"
          %(ds[len(ds)//2][0], ds[len(ds)//10][0], ds[len(ds)//100][0], ds[0][0]))
    print("worst 6 vertices (drift, y, dominant bone):")
    for d,v,y,dom in ds[:6]:
        print("   %.4e  y=%.3f  %s"%(d,y,names[bm[dom]] if dom>=0 else "-"))
    # the FACE band: vertices above the mouth mark's y
    face=[d for d,v,y,dom in ds if y>1.44]
    if face:
        face.sort(reverse=True)
        print("face region (y>1.44): %d verts, worst %.3e, p50 %.3e"
              %(len(face),face[0],face[len(face)//2]))
