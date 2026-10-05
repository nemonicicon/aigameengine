#!/usr/bin/env python3
"""brres2rpack.py -- a Wii BRRES model into the engine's RPK1 v2 pack, T-POSED where it has limbs (2026-10-05).

    brres2rpack.py <in.brres|.szs|.arc> <out.rpack> [--model NAME] [--pick SUBSTR] [--anim extra.brres ...]
                   [--notpose] [--map pelvis=HipN,...] [--bones] [--fps 30] [--scale S] [--facing +z|-z]
                   [--nearest] [--dae out.dae] [--sheet out.png]

  1. brres.scene_from_brres   the MDL0 as the club's scene: bones -> nodes, objects -> meshes baked to
                              rig space (single-bound parts through their bone, weighted parts through
                              the node mixes), TEX0 -> PNG, CHR0 -> clips resampled to --fps
                              (--anim adds the CHR0s of other archives: Brawl keeps motion in a second file)
  2. tpose.canon_map          which bone is which limb: the club's canon, the Nintendo matcher
                              (HipN, LLegJ, ArmR1 ...), then --map. A model with NO limbs (a statue on
                              dummy bones) skips the T-pose and says so: rig it first (statue2body)
  3. tpose.solve/bake         the limbs re-aimed and the skin re-baked in the T-pose
  4. rpackwrite               the pack; --dae also writes the same body as COLLADA with its PNGs
  5. the sheet                <out>_tpose.png, front / side / top with the skeleton: LOOK at it
"""
import sys, os, argparse
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import brres, tpose, daelib
import rpackwrite as RW


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--model', default=None, help='which MDL0 (default: the first)')
    ap.add_argument('--pick', default=None, help='inside an .szs/.arc: the brres whose path contains this')
    ap.add_argument('--anim', action='append', default=[], help='another .brres whose CHR0s animate this model')
    ap.add_argument('--notpose', action='store_true')
    ap.add_argument('--map', default='')
    ap.add_argument('--bones', action='store_true')
    ap.add_argument('--fps', type=float, default=30.0)
    ap.add_argument('--scale', type=float, default=1.0)
    ap.add_argument('--facing', default=None)
    ap.add_argument('--nearest', action='store_true', help='point-sample every texture (the Wii look)')
    ap.add_argument('--dae', default=None)
    ap.add_argument('--height', type=float, default=None, help='scale the body to stand this tall (club bodies: ~1.7)')
    ap.add_argument('--yaw', type=float, default=0.0, help='turn the whole scene about Y by this many degrees first (a body facing -z: 180)')
    ap.add_argument('--glb', default=None, help='also write the body as a binary glTF (the statue-rigging path reads this)')
    ap.add_argument('--sheet', default=None)
    a = ap.parse_args()

    sc = brres.scene_from_brres(a.src, a.model, fps=a.fps, pick=a.pick, scale=a.scale)
    nodes, meshes, clips, textures, names = sc['nodes'], sc['meshes'], sc['clips'], sc['textures'], sc['names']
    for extra in a.anim:
        clips += brres.clips_from_archive(extra, sc['model'], fps=a.fps, scale=a.scale)
    if not meshes:
        raise SystemExit('%s: no object decoded' % a.src)
    if a.nearest:
        for m in meshes:
            m['nearest'] = True
    if a.bones:
        tpose.print_bones(nodes, names)
    if a.yaw:
        import math as _m
        RW.rotate_scene(nodes, meshes, clips, [0.0, _m.sin(_m.radians(a.yaw) / 2), 0.0, _m.cos(_m.radians(a.yaw) / 2)])
        print('  yaw: scene turned %.1f deg about Y' % a.yaw)
    if a.height:
        RW.fit_height(nodes, meshes, clips, a.height)
    RM = tpose.canon_map(nodes, names, tpose.parse_map(a.map))
    limbs = [k for k in ('upperarm_l', 'upperarm_r', 'thigh_l', 'thigh_r') if RM[k] >= 0]
    if a.notpose:
        pass
    elif not limbs:
        print('  tpose: no limb bone resolved -- this is a statue (rigid parts on dummy bones); the body is written AS IS.')
        print('         rig it first: rpack2gltf --mesh, statue2body rig, autoweights, glb2rpack_anim, then tpose.py')
    else:
        print('  before:')
        tpose.measure(nodes, RM)
        qov = tpose.solve_tpose(nodes, RM, facing=a.facing)
        print('  after:')
        ang = tpose.measure(nodes, RM, qov)
        tpose.bake_tpose(nodes, meshes, clips, qov)
        worst = max((v for v in ang.values() if v is not None), default=0.0)
        if worst > 2.0:
            print('  !! a resolved limb is still %.1f deg off its axis -- the map or the chain end is wrong' % worst)
    RW.write_pack(a.dst, nodes, meshes, clips, textures)
    RW.bounds_report(nodes, meshes)
    sheet = a.sheet or os.path.splitext(a.dst)[0] + '_tpose.png'
    tpose.cloud_sheet(meshes, nodes, sheet, RM=RM)
    print('  sheet %s -- LOOK at it' % sheet)
    if a.dae:
        daelib.write_dae(a.dae, nodes, meshes, clips, textures, names)
    if a.glb:
        import glbwrite
        glbwrite.write_glb(a.glb, nodes, meshes, clips, textures, names)
    return 0


if __name__ == '__main__':
    sys.exit(main())
