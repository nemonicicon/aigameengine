"""score_rigs.py -- the definition of done in CAMERA_PORT.md section 5, measured from main.gd's CAM log lines.

    python godot/demo/score_rigs.py out/c_<mode>.log ...   (the mode is read from the DEMO line)

CAM <frame> <t>  <eye xyz>  <forward xyz>  <the rider's hips on screen: x y inview>  <fov>
"""
import math, sys

import numpy as np


def load(path):
    mode, rows = None, []
    for ln in open(path, encoding="utf-8", errors="replace"):
        if ln.startswith("DEMO mode"):
            p = ln.split(); mode = p[2]; extras = dict(zip(p[3::2], p[4::2]))
        if ln.startswith("CAM "):
            v = ln.split()
            rows.append([float(x) for x in v[1:]])
    return mode, extras, np.array(rows)


def main():
    ok_all = True
    series = {}
    for path in sys.argv[1:]:
        mode, ex, A = load(path)
        t, eye, fwd, inview, fov = A[:, 1], A[:, 2:5], A[:, 5:8], A[:, 10], A[:, 11]
        d1 = np.linalg.norm(np.diff(eye, axis=0), axis=1)
        d2 = np.linalg.norm(np.diff(eye, 2, axis=0), axis=1)
        seen = inview.mean()
        heading = np.degrees(np.arctan2(fwd[:, 0], fwd[:, 2]))
        label = "%s%s" % (mode, "".join(" %s=%s" % kv for kv in ex.items() if kv[1] not in ("0", "0.000", "0.00")))
        series[label] = (d1, d2)
        verdict, why = True, []
        if mode == "tripod" and float(ex.get("shake", 0)) == 0:
            verdict = d1.max() < 1e-6; why.append("eye moved at most %.2e m" % d1.max())
        elif mode == "tripod":
            r = d2.max() / max(d1.max(), 1e-12)
            verdict = d1.max() > 1e-5 and r < 0.35
            why.append("shake: eye speed up to %.4f m/frame, frame-to-frame change of it up to %.4f (ratio %.2f -- smooth < 0.35)" % (d1.max(), d2.max(), r))
        elif mode == "pan":
            verdict = d1.max() < 1e-6 and seen >= 0.95; why.append("eye moved at most %.2e m, the rider in frame %.0f%%" % (d1.max(), 100 * seen))
        elif mode == "orbit":
            k = int(np.argmax(t >= 4.0 + 1e-6))
            err = abs(((heading[k] - 180.0) + 180.0) % 360.0 - 180.0)
            verdict = err < 0.5 and seen >= 0.95
            why.append("heading at t=%.3f s: %.2f deg (asked 180; error %.2f), the rider in frame %.0f%%" % (t[k], heading[k], err, 100 * seen))
        elif mode == "move":
            k = int(np.argmax(t >= 3.0 + 1e-6))
            err = float(np.linalg.norm(eye[k] - np.array([2.2, 1.3, 2.2])))
            verdict = err < 1e-3; why.append("eye at t=%.3f s is %.4f m from the move's end" % (t[k], err))
        else:   # track / hold
            verdict = seen >= 0.95; why.append("the rider in frame %.0f%% of %d frames" % (100 * seen, len(t)))
        if float(ex.get("fish", 0)) > 0:
            why.append("render fov %.1f deg for an authored 44 (the fisheye opens the field)" % fov.max())
            verdict = verdict and fov.max() > 44.5
        print("%-44s %s  %s" % (label, "PASS" if verdict else "FAIL", "; ".join(why)))
        ok_all &= verdict
    # the one that needs two takes: the phone bobs where the dolly glides
    h = [k for k in series if k.startswith("hold") and "feed" not in k]
    tr = [k for k in series if k.startswith("track")]
    if h and tr:
        rh = series[h[0]][1].mean(); rt = series[tr[0]][1].mean()
        v = rh > 3.0 * rt
        print("%-44s %s  mean frame-to-frame change of the eye's motion: hand-held %.5f vs track %.5f m (%.1fx; the bob > 3x)"
              % ("hold vs track (THE BOB IS THE SHOT)", "PASS" if v else "FAIL", rh, rt, rh / max(rt, 1e-12)))
        ok_all &= v
    print("ALL PASS" if ok_all else "NOT ALL PASS")
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
