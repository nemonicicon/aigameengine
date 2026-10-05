#!/usr/bin/env python3
"""statue2tpose.py -- a STATUE (a .brres / .dae / .glb body with no limb skeleton) to a T-POSED, rigged
body, in one command: the chain that put Noxus and Kanden into T-poses on 2026-10-05.

    statue2tpose.py <in.brres|.dae|.glb> <outstem> [--height 1.9] [--yaw 180] [--marks marks.txt]
                    [--facing +z] [--pick SUBSTR] [--model NAME] [--nearest] [--up z] [--unit 0.01]

  1. the importer      brres2rpack / dae2rpack (--notpose, --height, --yaw, --glb): the body in rig
                       space at club height, facing +z, as <outstem>_raw.rpack + .glb
     (a .glb input skips this step and is used as is)
  2. statue2body strip the dummy skeleton out (every object of a Brawl-era rip hangs on a TopN/rotN
                       chain that is not a rig)
  3. statue2body rig   22 Mixamo-named joints from the landmark laws -- or from --marks, the joints
                       the operator measured off statue_marks.py's sheets (a hunter mid-stance breaks
                       the laws; see that file). --facing +z asserts the facing when the chest test
                       reads a hunch backwards
  4. autoweights       distance + visibility binding, top 4 influences
  5. glb2rpack_anim    the rigged body as a pack (--keepnames)
  6. tpose             limbs re-aimed, skin re-baked in the T-pose: <outstem>_tpose.rpack / .dae /
                       .glb, and <outstem>_tpose_tpose.png -- LOOK at it, then render it in the lab
                       (linux_lab.py <outstem>_tpose.rpack tpose <dir>)

Every stage prints its own receipt; the chain stops at the first failure.
"""
import sys, os, subprocess, shutil
HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable


def run(args, label):
    print('=== %s' % label)
    print('    ' + ' '.join(os.path.basename(a) if a.startswith('/') else a for a in args))
    r = subprocess.run([PY] + args, text=True, capture_output=True)
    out = (r.stdout or '') + (r.stderr or '')
    for ln in out.rstrip().splitlines()[-14:]:
        print('    ' + ln)
    if r.returncode != 0:
        raise SystemExit('%s FAILED (exit %d)' % (label, r.returncode))


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    src, stem = argv[1], argv[2]
    opt = lambda k, d=None: argv[argv.index(k) + 1] if k in argv else d
    height, yaw, marks, facing = opt('--height'), opt('--yaw'), opt('--marks'), opt('--facing')
    os.makedirs(os.path.dirname(os.path.abspath(stem)) or '.', exist_ok=True)
    ext = os.path.splitext(src)[1].lower()
    raw_glb = stem + '_raw.glb'
    if ext == '.glb':
        shutil.copy(src, raw_glb)
    else:
        imp = 'dae2rpack.py' if ext == '.dae' else 'brres2rpack.py'
        a = [os.path.join(HERE, imp), src, stem + '_raw.rpack', '--notpose', '--glb', raw_glb]
        if height: a += ['--height', height]
        if yaw: a += ['--yaw', yaw]
        if '--pick' in argv and imp == 'brres2rpack.py': a += ['--pick', opt('--pick')]
        if '--model' in argv and imp == 'brres2rpack.py': a += ['--model', opt('--model')]
        if '--nearest' in argv: a += ['--nearest']
        if '--up' in argv and imp == 'dae2rpack.py': a += ['--up', opt('--up')]
        if '--unit' in argv and imp == 'dae2rpack.py': a += ['--unit', opt('--unit')]
        run(a, 'import ' + os.path.basename(src))
    run([os.path.join(HERE, 'statue2body.py'), 'strip', raw_glb, stem + '_statue.glb'], 'strip the dummy skeleton')
    a = [os.path.join(HERE, 'statue2body.py'), 'rig', stem + '_statue.glb', stem + '_rig.glb']
    if marks: a += ['--marks', marks]
    if facing: a += ['--facing', facing]
    run(a, 'rig (landmarks%s)' % (' + measured marks' if marks else ''))
    run([os.path.join(HERE, 'author', 'autoweights.py'), stem + '_rig.glb', stem + '_bound.glb'], 'autoweights')
    run([os.path.join(HERE, 'glb2rpack_anim.py'), stem + '_bound.glb', stem + '_body.rpack', '--keepnames'], 'bake the rigged body')
    run([os.path.join(HERE, 'tpose.py'), stem + '_body.rpack', stem + '_tpose.rpack', '--dae', stem + '_tpose.dae', '--glb', stem + '_tpose.glb'], 'T-pose')
    print('=== done: %s_tpose.rpack / .dae / .glb; sheet %s_tpose_tpose.png -- LOOK, then the lab' % (stem, stem))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
