#!/usr/bin/env python3
"""brres2dae.py -- a Wii BRRES model as COLLADA (.dae) for Blender, T-POSED where it has limbs (2026-10-05).

    brres2dae.py <in.brres|.szs|.arc> <out.dae> [--model NAME] [--pick SUBSTR] [--anim extra.brres ...]
                 [--notpose] [--map pelvis=HipN,...] [--bones] [--fps 30] [--scale S] [--facing +z|-z]

The same read and the same T-pose as brres2rpack.py; the writer is daelib.write_dae: Y-up metres,
joints as JOINT nodes, a skin per object, the textures as PNG files beside the .dae, every CHR0 as a
baked-matrix animation. Blender's importer reads that form. A sheet (<out>_tpose.png) is written too.
"""
import sys, os, argparse
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import brres, tpose, daelib


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--model', default=None)
    ap.add_argument('--pick', default=None)
    ap.add_argument('--anim', action='append', default=[])
    ap.add_argument('--notpose', action='store_true')
    ap.add_argument('--map', default='')
    ap.add_argument('--bones', action='store_true')
    ap.add_argument('--fps', type=float, default=30.0)
    ap.add_argument('--scale', type=float, default=1.0)
    ap.add_argument('--facing', default=None)
    a = ap.parse_args()
    sc = brres.scene_from_brres(a.src, a.model, fps=a.fps, pick=a.pick, scale=a.scale)
    nodes, meshes, clips, textures, names = sc['nodes'], sc['meshes'], sc['clips'], sc['textures'], sc['names']
    for extra in a.anim:
        clips += brres.clips_from_archive(extra, sc['model'], fps=a.fps, scale=a.scale)
    if a.bones:
        tpose.print_bones(nodes, names)
    RM = tpose.canon_map(nodes, names, tpose.parse_map(a.map))
    limbs = [k for k in ('upperarm_l', 'upperarm_r', 'thigh_l', 'thigh_r') if RM[k] >= 0]
    if not a.notpose and limbs:
        tpose.measure(nodes, RM)
        qov = tpose.solve_tpose(nodes, RM, facing=a.facing)
        tpose.measure(nodes, RM, qov)
        tpose.bake_tpose(nodes, meshes, clips, qov)
    elif not a.notpose:
        print('  tpose: no limb bone resolved -- a statue; written as is')
    daelib.write_dae(a.dst, nodes, meshes, clips, textures, names)
    sheet = os.path.splitext(a.dst)[0] + '_tpose.png'
    tpose.cloud_sheet(meshes, nodes, sheet, RM=RM)
    print('  sheet %s -- LOOK at it' % sheet)
    return 0


if __name__ == '__main__':
    sys.exit(main())
