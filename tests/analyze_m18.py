"""M18 device evidence: retain every M17 assertion and dense workload budget."""
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
def metrics(tag):
    lines = [s for s in data.splitlines() if s.startswith(tag)]
    assert len(lines) == 1, tag
    return {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', lines[0])}
m = metrics('[M18 performance]')
u = metrics('[M18 UI]')
r = metrics('[M18 runtime]')
assert all(v == 0 for k,v in r.items() if k != 'ui_bytes'), r
assert m['state_bytes'] == 10 and m['engine_bytes'] == 52224 and m['publication_bytes'] == 20
subprocess.run([sys.executable, 'tests/analyze_m17.py', a.log, '--engine-bytes', str(m['engine_bytes'])] + (['--normal'] if a.normal else []) + (['--synth'] if a.synth else []), check=True)
keys = ('override_requests','override_accepts','override_loops','override_cancels','returns','fill_requests','fill_accepts','fill_completions','fill_to_override','fill_to_arrangement','mute_edits','solo_edits','boundaries','boundary_max')
if a.normal:
    assert all(m[k] == 0 for k in keys)
    assert all(v == 0 for v in u.values())
else:
    assert all(m[k] > 0 for k in keys), m
    assert m['fill_accepts'] == m['fill_completions']
    assert m['mute_edits'] >= 16 and m['solo_edits'] >= 16
    assert m['boundary_max'] <= 256 / 44100 * 1e6 * .8
    assert u['entries'] >= 10 and u['pads'] >= 7 and u['fills'] >= 3 and u['toggles'] >= 32 and u['switches'] > 0
print('M18 Performance and all inherited M17 qualification PASS; physical SD/touch/listening remain separately pending')
