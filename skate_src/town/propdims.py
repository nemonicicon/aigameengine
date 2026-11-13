import sys, os, glob
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "author"))
import rpack as R
for nm in ["bush_01","tree_01","tree_02","treelowpoly_01","mailbox_1","mailbox_2",
           "wall_01_yellow","veh_pickup_01","fence_101"]:
    p = "reva_native/props/v1_town_%s.rpack" % nm
    if not os.path.exists(p): print("%-18s (missing)" % nm); continue
    P = R.Rpack(p)
    try: pos = P.mesh_positions()
    except Exception as e: print("%-18s err %s"%(nm,e)); continue
    xs=[q[0] for q in pos]; ys=[q[1] for q in pos]; zs=[q[2] for q in pos]
    print("%-18s  X %7.2f  Y %7.2f  Z %7.2f   (verts %d)"
          % (nm, max(xs)-min(xs), max(ys)-min(ys), max(zs)-min(zs), len(pos)))
