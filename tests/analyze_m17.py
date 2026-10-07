"""Strict M17 qualification, retaining all inherited dense workload assertions."""
import argparse
import re
import subprocess
import sys
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('log')
p.add_argument('--normal', action='store_true')
p.add_argument('--synth', action='store_true')
a=p.parse_args()
data=Path(a.log).read_text(errors='replace')
def metrics(tag):
    rows=[s for s in data.splitlines() if s.startswith(tag)]
    assert len(rows)==1, tag
    return {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',rows[0])}
c=metrics('[M17 chain]')
assert c['entry_bytes']==2 and c['chain_bytes']==66 and c['engine_bytes']==52160
subprocess.run([sys.executable,'tests/analyze_m15.py',a.log,'--engine-bytes',str(c['engine_bytes'])]+(['--normal'] if a.normal else [])+(['--synth'] if a.synth else []),check=True)
m=metrics('[M16 project]')
assert m['state_bytes']==53192 and m['file_bytes']==52704
assert m['fixture_errors']==0 and m['physical_pending']==1
budget=256/44100*1e6*.8
if a.normal:
    assert all(c[k]==0 for k in ('starts','advances','repeats','loops','stops','switches','boundary_max','max_length'))
    assert all(m[k]==0 for k in ('snapshot_blocks','snapshot_max','apply_blocks','apply_max','fixtures','dirty'))
else:
    assert m['fixtures']==3 and m['snapshot_blocks']==51 and m['apply_blocks']==312
    assert 0<m['snapshot_max']<=budget and 0<m['apply_max']<=budget
    assert all(c[k]>0 for k in ('starts','advances','repeats','loops','stops','switches','boundary_max'))
    assert c['min_length']==1 and c['max_length']>=4
    assert c['boundary_max']<=budget and c['seen']==0xffffffff
    u=metrics('[M17 UI]')
    assert u['entries']>0 and u['edits']>=10 and u['rows']>=8 and u['scrolls']>=2
print('M17 Chain/V2, inherited dense workload, memory stability, snapshot/apply and >=20% headroom PASS; PHYSICAL V2 PROJECT MIGRATION / CHAIN PERSISTENCE PENDING')
