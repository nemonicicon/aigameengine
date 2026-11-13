"""Sample a mesh's UVs against candidate sheets and report the dominant colours."""
import json, struct, sys, os
from collections import Counter
from PIL import Image
CT={5120:('b',1),5121:('B',1),5122:('h',2),5123:('H',2),5125:('I',4),5126:('f',4)}
NC={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}
def read_glb(p):
    d=open(p,'rb').read(); off=12; js=None; bn=b''
    while off<len(d):
        cl,ct=struct.unpack('<I4s',d[off:off+8]); c=d[off+8:off+8+cl]
        if ct==b'JSON': js=json.loads(c)
        elif ct[:3]==b'BIN': bn=c
        off+=8+cl; off+=(-off)%4
    return js,bn
def acc(js,bn,i):
    a=js['accessors'][i]; fmt,sz=CT[a['componentType']]; n=NC[a['type']]
    bv=js['bufferViews'][a['bufferView']]
    base=bv.get('byteOffset',0)+a.get('byteOffset',0); stride=bv.get('byteStride') or sz*n
    return [struct.unpack_from('<'+fmt*n,bn,base+k*stride) for k in range(a['count'])]

mesh=sys.argv[1]
sheets=sys.argv[2:]
js,bn=read_glb(r'skate_src/town/export/%s.glb'%mesh)
uvs=[]
for m in js['meshes']:
    for pr in m['primitives']:
        if 'TEXCOORD_0' in pr['attributes']:
            uvs+=acc(js,bn,pr['attributes']['TEXCOORD_0'])
print("%s: %d uvs, u %.3f..%.3f  v %.3f..%.3f"%(mesh,len(uvs),
      min(u[0] for u in uvs),max(u[0] for u in uvs),
      min(u[1] for u in uvs),max(u[1] for u in uvs)))
for sh in sheets:
    im=Image.open(r'skate_src/town/export/%s.png'%sh).convert('RGB')
    W,H=im.size; px=im.load()
    c=Counter()
    for u,v in uvs:
        # the baker FLIPS the sheet vertically, so sample as the engine will
        x=int((u%1.0)*(W-1)); y=int(((v)%1.0)*(H-1))
        c[px[x,y]]+=1
    top=c.most_common(4)
    print("   %-28s top: %s"%(sh,"  ".join("rgb%s x%d"%(k,n) for k,n in top)))
