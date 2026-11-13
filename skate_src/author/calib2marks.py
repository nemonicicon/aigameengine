#!/usr/bin/env python3
"""calib2marks.py -- hand face_calibrate's measurement straight to face_marks.

    calib2marks.py <pack>            measure, write the override file, apply the marks

The two tools already speak the same frame (rig space) and the same three points, and
the documented loop between them was a human copying three lines of printout into a
file. That is a transcription step in the middle of a measurement, which is exactly
where a digit gets dropped -- and a mark that is wrong by a digit puts a camera in
someone's hair without failing.

⛔ IT REFUSES RATHER THAN GUESSES. face_calibrate returns eyes=None when it cannot find
a symmetric dark pair (kaeli: her liner defeats the darkest-texel rule), and inventing a
pair from proportions is how the marks silently become fiction. No eyes, no file.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from face_calibrate import calibrate

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    pack = sys.argv[1]
    eye, mouth = calibrate(pack)
    if not eye or not mouth:
        print("!! refusing: face_calibrate found %s -- marks not written"
              % ("no mouth" if eye else "no symmetric eye pair"))
        return 1
    out = os.path.splitext(pack)[0] + ".marks.txt"
    with open(out, "w") as f:
        f.write("# measured by face_calibrate.py off the painted face -- rig space\n")
        f.write("face_eye_l  %.6f %.6f %.6f\n" % (eye["lx"], eye["y"], eye["z"]))
        f.write("face_eye_r  %.6f %.6f %.6f\n" % (eye["rx"], eye["y"], eye["z"]))
        f.write("face_mouth  %.6f %.6f %.6f\n" % (mouth["x"], mouth["y"], mouth["z"]))
    print("wrote %s" % out)
    r = subprocess.run([sys.executable, os.path.join(HERE, "face_marks.py"),
                        pack, "--from", out, "--report"])
    return r.returncode


sys.exit(main())
