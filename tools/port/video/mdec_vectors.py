#!/usr/bin/env python3
"""Independent Python matrix IDCT oracle, synthetic tables and coefficients only."""
import hashlib
from pathlib import Path
import struct
import sys

def clamp(v, lo=-1024, hi=1023): return max(lo, min(hi, v))

def content():
    # Deliberately nonstandard asymmetric scale and quant tables exercise uploads.
    scale = [[((x*31 + u*17 + 13) % 63 - 31)*512+(u*3+x)%8 for x in range(8)] for u in range(8)]
    zig = sorted(range(64), key=lambda i: (i//8+i%8, i//8 if (i//8+i%8)%2 else i%8))
    hashes=[]
    for q in (0, 1, 63):
        blocks=[]
        for block in range(6):
            qt=[(i*7+(0 if block>=2 else 11))%253+1 for i in range(64)]
            # DC, adjacent AC, a skipped AC, then EOB; negative extreme included.
            levels=[(0, [-511,511,-100,100,7,-7][block]), (1,511), (5,-512)]
            a=[[0]*8 for _ in range(8)]
            for k,v in levels:
                val=v*2 if q==0 else v*qt[k] if k==0 else (v*qt[k]*q+4)//8
                pos=k if q==0 else zig[k]
                a[pos//8][pos%8]=clamp(val)
            # Direct 2-D matrix product S^T * A * S, with a single final rounding.
            a=[[(sum((scale[z][y]//8)*a[z][w]*(scale[w][x]//8)
                     for z in range(8) for w in range(8)) + 33554431)//67108864
                for x in range(8)] for y in range(8)]
            blocks.append(a)
        for rgb24 in (0,1):
            for stp in (0,1):
                pixels=bytearray()
                for y in range(16):
                    for x in range(16):
                        cr, cb = (blocks[b][y//2][x//2] for b in (0,1))
                        lum=blocks[2+(y//8)*2+x//8][y%8][x%8]
                        rgb=[clamp(lum+(cr*1436+512)//1024,-128,127)+128,
                             clamp(lum+(-cb*352-cr*731+512)//1024,-128,127)+128,
                             clamp(lum+(cb*1815+512)//1024,-128,127)+128]
                        if rgb24: pixels.extend(rgb)
                        else: pixels.extend(struct.pack('<H',sum((c>>3)<<(5*i) for i,c in enumerate(rgb)) | stp<<15))
                hashes.append(hashlib.sha256(pixels).hexdigest())
    return '/* Synthetic oracle: tools/port/video/mdec_vectors.py */\nstatic const char *const mdec_hashes[] = {\n'+''.join('    "'+h+'",\n' for h in hashes)+'};\n'

if __name__=='__main__':
    path=Path(__file__).resolve().parents[3]/'port/backend/tests/mdec_vectors.h'
    if '--check' in sys.argv:
        if path.read_text()!=content(): raise SystemExit('stale MDEC vectors')
    else: path.write_text(content())
