#!/usr/bin/env python3
"""Verify the native Oodle0 port (build: g++ tools/gr2_test.cpp libvb_assets.a -> /tmp/gr2_test) against the real granny2.dll running in Unicorn."""
import sys,glob,subprocess,os
sys.path.insert(0,'.')
from gr2_parse import Gr2; from gr2_oodle0_emu import Emu
e=Emu(); ok=bad=0; nsec=0
os.makedirs('/tmp/g2o',exist_ok=True)
files=sorted(glob.glob('../assets/**/*.skel',recursive=True)+glob.glob('../assets/**/*.anim',recursive=True))
for p in files:
    g=Gr2(p); tag='/tmp/g2o/x'
    r=subprocess.run(['/tmp/gr2_test',p,tag],capture_output=True,text=True)
    for i,s in enumerate(g.secs):
        if s['comp']==1 and s['size']:
            ref=e.decompress(g.d[s['off']:s['off']+s['size']],s['f16'],s['f8'],s['exp'])
            mine=open('%s.%d'%(tag,i),'rb').read(); nsec+=1
            if ref==mine: ok+=1
            else: bad+=1; print('DIFF',p,i)
print('files',len(files),'sections',nsec,'match',ok,'diff',bad)
