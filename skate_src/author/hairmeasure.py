"""hairmeasure.py FOLDER [--top N] [--csv FILE] -- what a head's simulated hair is doing, from the engine's REVA_STRANDDUMP files.

REVA_STRANDDUMP=<folder>[,every[,from]] (engine/strands.inc, hsDump) writes, for each head of simulated hair, where every point of every lock is, its
speed and where the haircut had it, every so many stepped frames, and once its lengths at rest. This reads a folder of them and says, per head:
  * how fast its points move against the haircut (mm/s at her true size): a head standing still, long after settling, should read about nothing;
  * how often a point's motion turns back on itself from one frame to the next (a wiggle: the worms of 11-11);
  * how long each segment is against its own length, down the lock (constraints that disagree show as segments that can't keep their length);
  * how straight the locks are (tip to root over the lock's length), and the worst locks, with where their roots are on her head.
Dump every frame (every=1) for the speeds to mean anything.
"""
import glob, os, struct, sys
import numpy as np


def rest(path):
    b = open(path, "rb").read()
    magic, ns, npt, f = struct.unpack_from("<4i", b, 0)
    sc, lenk, dt, sub = struct.unpack_from("<4f", b, 16)
    n = ns * npt
    a = np.frombuffer(b, np.float32, offset=32)
    return dict(ns=ns, np=npt, sc=sc, lenk=lenk, len=a[:n].reshape(ns, npt), geo=a[n:2 * n].reshape(ns, npt), rr=a[2 * n:3 * n].reshape(ns, npt),
                rp=a[3 * n:6 * n].reshape(ns, npt, 3))


def frame(path):
    b = open(path, "rb").read()
    magic, ns, npt, f = struct.unpack_from("<4i", b, 0)
    sc, lenk, dt, sub = struct.unpack_from("<4f", b, 16)
    a = np.frombuffer(b, np.float32, offset=32)
    n = ns * npt
    E = a[:16].reshape(4, 4).T
    Ei = a[16:32].reshape(4, 4).T
    X = a[32:32 + 3 * n].reshape(ns, npt, 3)
    V = a[32 + 3 * n:32 + 6 * n].reshape(ns, npt, 3)
    T = a[32 + 6 * n:32 + 9 * n].reshape(ns, npt, 3)
    return dict(f=f, sc=sc, lenk=lenk, dt=dt, sub=sub, E=E, Ei=Ei, X=X, V=V, T=T)


def pct(a, q):
    return float(np.percentile(a, q)) if a.size else 0.0


def measure(folder, stem, top=8, csv=None, quiet=False):
    R = rest(os.path.join(folder, stem + "_rest.bin"))
    files = sorted(glob.glob(os.path.join(folder, stem + "_[0-9][0-9][0-9][0-9][0-9].bin")))
    F = [frame(p) for p in files]
    if len(F) < 3:
        print(f"{stem}: {len(F)} frames, too few"); return None
    ns, npt = R["ns"], R["np"]
    sc, lenk, dt = F[0]["sc"], F[0]["lenk"], F[0]["dt"]
    mm = 1000.0 / sc
    X = np.stack([f["X"] for f in F]).astype(np.float64)       # frames, locks, points, 3
    T = np.stack([f["T"] for f in F]).astype(np.float64)
    bad = ~np.isfinite(X).all(axis=(0, 2, 3))
    D = X - T                                                    # each point against the haircut's place for it
    dD = np.diff(D, axis=0)                                      # its motion against the haircut, a frame
    sp = np.linalg.norm(dD, axis=3)[:, :, 1:] * mm / dt         # mm/s, roots left out
    root = np.linalg.norm(np.diff(X[:, :, 0], axis=0), axis=2) * mm / dt
    # a wiggle: this frame's motion against the haircut points back along the last frame's, and both are more than a hair's breadth a frame
    a, b = dD[:-1, :, 1:], dD[1:, :, 1:]
    la, lb = np.linalg.norm(a, axis=3), np.linalg.norm(b, axis=3)
    thr = 0.5 / mm                                               # half a millimetre a frame
    back = (np.einsum("flpk,flpk->flp", a, b) < -0.5 * la * lb) & (la > thr) & (lb > thr)
    # segments against their own length
    seg = np.linalg.norm(np.diff(X, axis=2), axis=3)             # frames, locks, np-1
    own = (R["len"][:, 1:] * lenk).astype(np.float64)
    ok = own > 1e-7
    ratio = np.where(ok, seg / np.where(ok, own, 1.0), 1.0)
    cut = (R["len"][:, 1:] * sc).astype(np.float64)              # the haircut's own segment, at her size
    # how straight: tip from root over the length it's simulated at
    reach = np.linalg.norm(X[:, :, -1] - X[:, :, 0], axis=2) / np.maximum(own.sum(axis=1), 1e-9)
    lockmax = sp.max(axis=(0, 2)); lockmean = sp.mean(axis=(0, 2)); lockback = back.sum(axis=(0, 2))
    out = dict(stem=stem, frames=len(F), ns=ns, np=npt, sc=sc, lenk=lenk, dt=dt, med=pct(sp, 50), p90=pct(sp, 90), p99=pct(sp, 99), p999=pct(sp, 99.9), max=float(sp.max()),
               root=float(root.mean()), fast=int((lockmax > 100.0).sum()), wig=int((lockback >= 3).sum()), back=float(back.mean()),
               seg=[float(ratio[:, :, j].mean()) for j in range(npt - 1)], segsd=float(ratio.std(axis=0).mean()), reach5=pct(reach, 5), curled=int((reach.min(axis=0) < 0.6).sum()), nan=int(bad.sum()))
    if quiet:
        return out
    print(f"== {stem}: {len(F)} frames (stepped {F[0]['f']}..{F[-1]['f']}), {ns} locks of {npt}, drawn x{sc:.3f}, lengths x{lenk:.3f} ({100 * lenk / sc:.0f}% of the cut), dt {dt * 1000:.1f} ms")
    if out["nan"]: print(f"   {out['nan']} locks have points that aren't numbers")
    print(f"   her head (the roots): {out['root']:.1f} mm/s on average")
    print(f"   points against the haircut, mm/s: median {out['med']:.1f}, 90th {out['p90']:.1f}, 99th {out['p99']:.1f}, 99.9th {out['p999']:.1f}, most {out['max']:.0f}")
    print(f"   locks with a point past 100 mm/s at some frame: {out['fast']} of {ns};  locks that turn back on themselves 3 times or more: {out['wig']};  turn-backs a point a frame: {100 * out['back']:.3f}%")
    print("   segments against their own length, root to tip: " + " ".join(f"{r:.2f}" for r in out["seg"]) + f"   (changing by {100 * out['segsd']:.2f}% frame to frame)")
    print(f"   reach (tip from root over its length): 5th percentile {out['reach5']:.2f}; locks ever under 0.6 (curled or folded): {out['curled']}")
    # the worst locks, and where on her head their roots are (her skull's own frame: x to her left, y up, z to her front, 1 at its surface)
    E = F[-1]["E"]
    rootsE = (np.c_[X[-1, :, 0], np.ones(ns)] @ E.T)[:, :3]
    order = np.argsort(-lockmax)[:top]
    print("   the fastest locks: lock, most mm/s, mean mm/s, turn-backs, reach, root in her skull's frame, tip's rr at rest, how far its 2nd point is from the haircut's (mm)")
    for s in order:
        d2 = np.linalg.norm(D[-1, s, 2]) * mm
        print(f"     {s:5d}  {lockmax[s]:7.0f} {lockmean[s]:7.1f} {int(lockback[s]):4d}  {reach[:, s].min():.2f}..{reach[:, s].max():.2f}  ({rootsE[s, 0]:+.2f} {rootsE[s, 1]:+.2f} {rootsE[s, 2]:+.2f})  {R['rr'][s, -1]:.2f}  {d2:.1f}")
    if csv:
        with open(csv, "w") as o:
            o.write("lock,max_mm_s,mean_mm_s,turnbacks,reach_min,rx,ry,rz\n")
            for s in range(ns): o.write(f"{s},{lockmax[s]:.1f},{lockmean[s]:.2f},{int(lockback[s])},{reach[:, s].min():.3f},{rootsE[s, 0]:.3f},{rootsE[s, 1]:.3f},{rootsE[s, 2]:.3f}\n")
    out["lockmax"] = lockmax; out["rootsE"] = rootsE; out["reach"] = reach
    return out


def main():
    a = sys.argv[1:]
    if not a: print(__doc__); return
    folder = a[0]; top = int(a[a.index("--top") + 1]) if "--top" in a else 8; csv = a[a.index("--csv") + 1] if "--csv" in a else None
    stems = sorted(os.path.basename(p)[:-9] for p in glob.glob(os.path.join(folder, "*_rest.bin")))
    if not stems: print("no *_rest.bin in", folder); return
    for s in stems: measure(folder, s, top, csv)


if __name__ == "__main__":
    main()
