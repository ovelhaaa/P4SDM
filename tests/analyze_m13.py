"""Qualify genuine M13 device captures; no physical SD dependency."""
import argparse
import re
import subprocess
import sys
from pathlib import Path
p = argparse.ArgumentParser()
p.add_argument('log')
p.add_argument('--normal', action='store_true')
p.add_argument('--synth', action='store_true')
p.add_argument('--track-bytes', type=int, default=48)
p.add_argument('--engine-bytes', type=int, default=46880)
a = p.parse_args()
data = Path(a.log).read_text(errors='replace')
def row(prefix):
    lines = [s for s in data.splitlines() if s.startswith(prefix)]
    assert len(lines) == 1, f'missing/repeated {prefix}'
    return {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)', lines[0])}
subprocess.run([sys.executable, 'tests/analyze_m12.py', a.log,
    '--event-bytes','20','--voice-bytes','384','--engine-bytes',str(a.engine_bytes)]
    + (['--normal'] if a.normal else []) + (['--sample'] if not a.normal and not a.synth else []), check=True)
m = row('[M13 playback]')
assert m['voice_bytes'] == 64 and m['track_bytes'] == a.track_bytes and m['event_bytes'] == 20
assert 'NO CARD' in data
assert 'Guru Meditation' not in data and 'task_wdt' not in data
assert row('[M5]')['blocks'] * 256 / 44100 >= 60
limit = 256 / 44100 * 1e6 * .8
assert m['max'] == row('[M5]')['max'] and 0 < m['max'] <= limit
if a.normal or a.synth:
    assert '[M13 fixture]' not in data
    assert all(m[k] == 0 for k in ('triggers','reverse','gate','releases','ends','choke_ops','choked','retriggers','region_min','region_max','choke_max','retrigger_max'))
    assert row('[M13 UI]')['entries'] == row('[M13 UI]')['edits'] == 0
else:
    dense = row('[M13 dense]')
    assert dense['blocks'] >= 2068 and dense['active_min'] == 16, 'dense sample workload reduced'
    fixture = row('[M13 fixture]')
    assert fixture['voices'] == 16 and fixture['psram'] == 1 and fixture['frames'] == 65536 and fixture['bytes'] == 16 * 65536 * 2
    for k in ('triggers','reverse','gate','releases','ends','choke_ops','choked','retriggers'):
        assert m[k] > 0, f'unexercised {k}'
    assert 0 < m['region_min'] < m['region_max'] <= fixture['frames']
    assert 0 < m['choke_max'] <= limit and 0 < m['retrigger_max'] <= limit
    ui = row('[M13 UI]')
    assert ui['entries'] >= 3 and ui['edits'] >= 10 and 0 < ui['widgets_max'] <= 3 and ui['edit_full'] == 0
print('M13 playback coverage, heaps, inherited checks, 60s and >=20% headroom: PASS')
