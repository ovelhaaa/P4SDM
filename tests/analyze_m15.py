"""Strict M15 plus every inherited M14 capture requirement."""
import argparse
import re
import subprocess
import sys
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('log')
p.add_argument('--normal',action='store_true')
p.add_argument('--synth',action='store_true')
p.add_argument('--engine-bytes',default='52064')
a=p.parse_args()
subprocess.run([sys.executable,'tests/analyze_m14.py',a.log,'--step-bytes','9','--pattern-bytes','3106','--engine-bytes',a.engine_bytes]+(['--normal'] if a.normal else [])+(['--synth'] if a.synth else []),check=True)
data=Path(a.log).read_text(errors='replace')
lines=[s for s in data.splitlines() if s.startswith('[M15 locks]')]
assert len(lines)==1
m={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',lines[0])}
assert m['align']==1
if a.normal or a.synth:
    assert all(m[k]==0 for k in m if k!='align'), 'Slice Lock residue'
else:
    assert all(m[k]>0 for k in ('locked','unlocked','unsliced','clamped','edits','ui_edits'))
    assert m['requested_min']==m['resolved_min']==0
    assert m['requested_max']==m['resolved_max']==15
    assert m['requested_seen']==m['resolved_seen']==0xFFFF, 'not every slice index was exercised'
print('M15 slice lock coverage, inherited M14 workload, heaps and >=20% headroom: PASS')
