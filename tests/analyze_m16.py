"""M16 boundary qualification plus the accepted M15 workload budgets."""
import argparse
import re
import subprocess
import sys
from pathlib import Path
p = argparse.ArgumentParser()
p.add_argument('log')
p.add_argument('--normal', action='store_true')
p.add_argument('--synth', action='store_true')
a = p.parse_args()
subprocess.run([sys.executable, 'tests/analyze_m15.py', a.log] + (['--normal'] if a.normal else []) + (['--synth'] if a.synth else []), check=True)
lines = [s for s in Path(a.log).read_text(errors='replace').splitlines() if s.startswith('[M16 project]')]
assert len(lines) == 1
m = {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)', lines[0])}
assert m['state_bytes'] == 53128 and m['file_bytes'] == 52637
assert m['fixture_errors'] == 0 and m['physical_pending'] == 1
if a.normal:
    assert all(m[k] == 0 for k in ('snapshot_blocks','snapshot_max','apply_blocks','apply_max','fixtures','dirty'))
else:
    assert m['fixtures'] == 3 and m['snapshot_blocks'] == 51 and m['apply_blocks'] == 312
    assert 0 < m['snapshot_max'] <= 256/44100*1e6*.8
    assert 0 < m['apply_max'] <= 256/44100*1e6*.8
print('M16 snapshot/apply, RAM V1 roundtrips, zero errors and >=20% headroom: PASS; PHYSICAL PROJECT SAVE/LOAD PENDING')
