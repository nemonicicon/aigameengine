#!/usr/bin/env python3
"""dae2rpack.py -- a COLLADA (.dae) body or prop into the engine's RPK1 v2 pack, T-POSED (2026-10-05).

    dae2rpack.py <in.dae> <out.rpack> [--notpose] [--map pelvis=Hip,upperarm_l=ArmL1,...] [--bones]
                 [--fps 30] [--scale S] [--facing +z|-z] [--dae out.dae] [--nearest] [--sheet out.png]

What happens, in order (each stage prints its receipt):
  1. daelib.read_dae   the scene as the club's shape, every mesh baked to rig space at the file's
                       own rest pose (up axis, units, bind-shape and node transforms settled here)
  2. tpose.canon_map   which node is which limb: the club's canon, the Nintendo/generic matcher,
                       then --map. UNRESOLVED limbs are printed; --bones lists the rig to map them
  3. tpose.solve/bake  the limbs re-aimed into a T-pose and the skin RE-BAKED there: positions,
                       normals, inverse binds and the rest pose itself (--notpose keeps the file's)
  4. rpackwrite        the pack; --dae also writes the T-posed body back out as COLLADA (+ PNGs)
  5. the sheet         <out>_tpose.png: front / side / top of the skinned cloud with the skeleton --
                       LOOK at it; the angle table is a number, the sheet is the frame

Clip names are kept as the file names them (lint with `rpack.py lint`); a COLLADA export rarely
carries the club's roles, and the T-pose is the point. A clip that drove the old rest gets hold
tracks for every bone the T-pose moved, so it still plays as authored.
"""
import sys, os, argparse
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import daelib, tpose
import rpackwrite as RW


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--notpose', action='store_true', help="keep the file's rest pose")
    ap.add_argument('--map', default='', help='canon overrides: key=NodeName,... (keys: pelvis spine_01..03 neck_01 head clavicle_l upperarm_l lowerarm_l hand_l thigh_l calf_l foot_l ball_l and _r)')
    ap.add_argument('--bones', action='store_true', help='list the rig (index, depth, name, world position) and continue')
    ap.add_argument('--fps', type=float, default=30.0)
    ap.add_argument('--scale', type=float, default=1.0, help='applied after the units (a 1.7 m body at --scale 1 is club size)')
    ap.add_argument('--facing', default=None, help='+z or -z; default: read from the feet')
    ap.add_argument('--dae', default=None, help='also write the (T-posed) body as COLLADA here')
    ap.add_argument('--nearest', action='store_true', help='point-sample the textures (pixel art, N64/Wii era)')
    ap.add_argument('--up', default=None, help="y, z or x: the axis the file's data is really up along, when its asset block lies")
    ap.add_argument('--unit', type=float, default=None, help='metres per file unit, overriding the asset block')
    ap.add_argument('--height', type=float, default=None, help='scale the body to stand this tall (club bodies: ~1.7)')
    ap.add_argument('--yaw', type=float, default=0.0, help='turn the whole scene about Y by this many degrees first (a body facing -z: 180)')
    ap.add_argument('--glb', default=None, help='also write the body as a binary glTF (the statue-rigging path reads this)')
    ap.add_argument('--sheet', default=None, help='the cloud sheet path (default <dst>_tpose.png)')
    a = ap.parse_args()

    sc = daelib.read_dae(a.src, fps=a.fps, up=a.up, unit=a.unit)
    nodes, meshes, clips, textures, names = sc['nodes'], sc['meshes'], sc['clips'], sc['textures'], sc['names']
    if not meshes:
        raise SystemExit('%s: no mesh instanced in the scene' % a.src)
    if a.scale != 1.0:
        RW.scale_scene(nodes, meshes, clips, a.scale)
    if a.bones:
        tpose.print_bones(nodes, names)
    for m in meshes:
        m['nearest'] = a.nearest
    if a.yaw:
        import math as _m
        RW.rotate_scene(nodes, meshes, clips, [0.0, _m.sin(_m.radians(a.yaw) / 2), 0.0, _m.cos(_m.radians(a.yaw) / 2)])
        print('  yaw: scene turned %.1f deg about Y' % a.yaw)
    if a.height:
        RW.fit_height(nodes, meshes, clips, a.height)
    RM = tpose.canon_map(nodes, names, tpose.parse_map(a.map))
    if not a.notpose:
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
