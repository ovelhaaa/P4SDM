"""Genuine M20 renderer timing and retained M19 realtime qualification."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import os
p=argparse.ArgumentParser()
p.add_argument('log'); p.add_argument('--synth',action='store_true')
a=p.parse_args()
data=Path(a.log).read_text(errors='replace')
assert 'stack overflow' not in data and 'Guru Meditation' not in data
environment=os.environ.copy()
if re.search(r'\[M6 SD\].*mounted=1',data): environment['P4SDM_PHYSICAL_SD']='1'
else: environment.pop('P4SDM_PHYSICAL_SD',None)
subprocess.run([sys.executable,'tests/analyze_m19.py',a.log]+(['--synth'] if a.synth else []),check=True,env=environment)
rows={}
for line in Path(a.log).read_text(errors='replace').splitlines():
    if line.startswith('[M20 '):
        r={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)}
        rows[(line.split(']')[0],r.get('page',-1))]=r
for page in (0,1,11,4,15,13):
    r=rows[('[M20 page',page)]
    assert r['frames']>0 and r['render_us']>0 and r['dirty_bytes']==768000,r
r=rows[('[M20 updates',-1)]
assert r['slider_us']>0 and 0<r['slider_bytes']<96000,r
assert r['playhead_us']>0 and 0<r['playhead_bytes']<96000,r
assert 0<r['interval_us']<250000,r
print('M20 full pages, dirty slider/playhead, bounded UI interval and inherited M19 >=20% realtime headroom PASS')
