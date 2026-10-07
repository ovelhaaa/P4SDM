"""Strict immutable step-repeat qualification, retaining every M18 assertion."""
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
data = Path(a.log).read_text(errors='replace')
def row(tag):
    lines = [s for s in data.splitlines() if s.startswith(tag)]
    assert len(lines) == 1, tag
    return {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)', lines[0])}
m = row('[M181 repeat]')
x = row('[M181 x8]')
r = row('[M181 runtime]')
assert m['capture_bytes'] == 324 and m['repeat_bytes'] == 336
assert r['requested'] == r['active'] == r['count'] == 0
subprocess.run([sys.executable, 'tests/analyze_m18.py', a.log,
    '--engine-bytes','52616','--state-bytes','352','--publication-bytes','24'] +
    (['--normal'] if a.normal else []) + (['--synth'] if a.synth else []), check=True)
limit = 256 / 44100 * 1e6 * .8
if a.normal:
    assert all(v == 0 for k,v in m.items() if k not in ('capture_bytes','repeat_bytes'))
    assert all(v == 0 for v in x.values())
    assert all(v == 0 for v in r.values())
else:
    assert m['requests'] >= 2 and m['cancelled'] >= 1 and m['accepts'] >= 1
    assert all(m[k] > 0 for k in ('x2_windows','x4_windows','x8_windows','hits','x8_hits','changes','releases','suppressed','worst'))
    assert m['captured_max'] == 16 and m['changes'] >= 3
    assert m['x8_windows'] >= 50 and m['x8_hits'] >= 6000
    assert 0 < m['worst'] <= limit and 0 < x['worst'] <= limit
    assert x['blocks'] >= 700 and x['active_min'] == x['active_max'] == 16
    assert x['full_blocks'] >= 700
    assert x['avoided'] > x['coefficients'] and x['chokes'] == 0
    assert r['ui_presses'] > 0 and r['ui_releases'] > 0 and r['ui_edits'] > 0
print('M18.1 x2/x4/x8 immutable repeat, full 16-voice stress, cache/choke, inherited workload, normal/heaps and >=20% headroom PASS')
