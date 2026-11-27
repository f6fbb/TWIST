#!/usr/bin/env python3
"""twist_ref.py - Independent re-implementation of the TWIST encoder,
written from PROTOCOL.md only (reference for beacon firmware authors),
checked against twistenc.

usage (from the repository root, after make):  python3 tools/twist_ref.py
"""
import os, re, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
T = open(os.path.join(ROOT, 'core', 'twist_tables.h')).read()
def tab(name):
    m=re.search(r'%s_G\[(\d+)\]\[3\] = \{(.*?)\};'%name,T,re.S)
    rows=re.findall(r'\{(0x[0-9a-f]+)ULL,(0x[0-9a-f]+)ULL,(0x[0-9a-f]+)ULL\}',m.group(2))
    return [[int(w,16) for w in r] for r in rows]
def cc(c):
    if c==' ':return 0
    if 'A'<=c<='Z':return ord(c)-64
    return ord(c)-48+27
def packcall(call):
    c=call.upper()
    if not c[2].isdigit(): c=' '+c
    c=c.ljust(6)
    v=cc(c[0]); v=v*36+cc(c[1])-1; v=v*10+cc(c[2])-27
    for x in c[3:]: v=v*27+cc(x)
    return v
def packloc(l):
    l=l.upper(); v=((((ord(l[0])-65)*18+ord(l[1])-65)*10+int(l[2]))*10+int(l[3]))*577
    if len(l)==6: v+=1+(ord(l[4])-65)*24+ord(l[5])-65
    return v
def bits(v,n): return [(v>>(n-1-i))&1 for i in range(n)]
def crc(b,init):
    c=init
    for x in b:
        fb=((c>>15)&1)^x; c=(c<<1)&0xFFFF
        if fb: c^=0x1021
    return c
def frame(call,loc=None):
    if loc: m=bits(packcall(call),28)+bits(packloc(loc),25); init=0x1D0F; s=0x3C3C7; G=tab('TWL'); n=160
    else: m=bits(packcall(call),28); init=0xFFFF; s=0x5A5A1; G=tab('TWS'); n=119
    v=m+bits(crc(m,init),16)
    for i in range(len(v)):
        o=s&1; b=(s^(s>>2)^(s>>3)^(s>>5))&1; s=(s>>1)|(b<<19); v[i]^=o
    w=[0,0,0]
    for i,x in enumerate(v):
        if x: w=[a^b for a,b in zip(w,G[i])]
    c=[(w[j//64]>>(j%64))&1 for j in range(n)]
    ch=[1]
    for x in c: ch+= [0,1] if x else [1,0]
    return ''.join(map(str,ch))
ok=True
for args in (['F6FBB'],['F6FBB','JN18cs'],['F5ZZO','JN12'],['EA5XYZ','IM98wl'],['9A1A'],['OZ7IGY','JO65ab'],['K1JT','FN20']):
    out=subprocess.run([os.environ.get('TWISTENC', os.path.join(ROOT, 'twistenc'))]+args,capture_output=True,text=True).stdout
    ref=re.search(r'chips\s*: ([01]+)',out).group(1)
    mine=frame(*args); same=ref==mine; ok&=same
    print(' '.join(args), 'OK' if same else 'DIFF')
print('ALL OK' if ok else 'MISMATCH')
sys.exit(0 if ok else 1)
