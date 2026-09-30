#!/usr/bin/env python3
"""litcheck.py -- is this frame LIT? (2026-09-30)

    litcheck.py <frame.ppm|png> [--min 0.02]

Prints the fraction of pixels brighter than 40/255 and exits 0 when it is at least --min, 1 when
the frame is black or missing. The build matrix's black-frame gate for the lab frame and for the
twelve game doors. ⛔ It answers ONE question -- "did anything draw?" -- and a lit frame can still
be wrong: convert and LOOK before claiming anything renders.
"""
import sys
def main():
    args=[a for a in sys.argv[1:] if not a.startswith('--')]
    mn=0.02
    if '--min' in sys.argv: mn=float(sys.argv[sys.argv.index('--min')+1])
    if not args: print(__doc__); return 2
    try:
        from PIL import Image
        im=Image.open(args[0]).convert('L'); h=im.histogram()
        lit=sum(h[40:])/float(im.width*im.height)
    except Exception as e:
        print('%s: NO FRAME (%s)'%(args[0],e)); return 1
    ok=lit>=mn
    print('%s %dx%d lit %.3f %s'%(args[0],im.width,im.height,lit,'OK' if ok else 'BLACK (min %.3f)'%mn))
    return 0 if ok else 1
if __name__=='__main__': sys.exit(main())
