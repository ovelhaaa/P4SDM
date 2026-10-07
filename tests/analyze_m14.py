"""Strict genuine M14 captures, retaining M3-M13 budgets and coverage."""
import argparse
import re
import subprocess
import sys
from pathlib import Path
p = argparse.ArgumentParser()
p.add_argument('log')
p.add_argument('--step-bytes', type=int, default=8)
p.add_argument('--pattern-bytes', type=int, default=2850)
p.add_argument('--engine-bytes', type=int, default=47968)
p.add_argument('--normal', action='store_true')
p.add_argument('--synth', action='store_true')
a = p.parse_args()
data = Path(a.log).read_text(errors='replace')
def row(prefix):
    lines = [s for s in data.splitlines() if s.startswith(prefix)]
    assert len(lines) == 1, f'missing/repeated {prefix}'
    return {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)', lines[0])}
subprocess.run([sys.executable,'tests/analyze_m13.py',a.log,
    '--track-bytes','116','--engine-bytes',str(a.engine_bytes),'--step-bytes',str(a.step_bytes),'--pattern-bytes',str(a.pattern_bytes)] +
    (['--normal'] if a.normal else []) + (['--synth'] if a.synth else []),check=True)
m = row('[M14 slices]')
u = row('[M14 UI]')
assert m['slice_bytes'] == 4 and m['bank_bytes'] == 66
assert u['cache_bytes'] == 1440 and u['edit_full'] == 0
limit = 256/44100*1e6*.8
if a.normal or a.synth:
    for k in ('triggers','auditions','index_min','index_max','frames_min','frames_max',
              'active_changes','divides','adds','deletes','resets','edit_blocks','edit_max'):
        assert m[k] == 0, f'non-slice residue: {k}'
    for k in ('entries','edits','waveform_first_us','waveform_redraw_max_us',
              'slice_redraw_max_us','build_count','build_max_us','large_us','page_max_us','drag_dirty_max'):
        assert u[k] == 0, f'non-slice UI/load residue: {k}'
else:
    for k in ('triggers','auditions','active_changes','divides','adds','deletes','resets','edit_blocks'):
        assert m[k] > 0, f'missing slice coverage: {k}'
    assert m['index_min'] == 0 and m['index_max'] == 15
    assert 0 < m['frames_min'] < m['frames_max'] <= 65536
    assert 0 < m['edit_max'] <= row('[M5]')['max'] <= limit
    assert u['entries'] >= 3 and u['edits'] >= 20
    assert u['build_count'] == 17 and u['build_max_us'] >= u['large_us'] > 0
    assert 0 < u['drag_dirty_max'] < 768000 and u['page_max_us'] >= u['waveform_first_us']
    assert u['waveform_first_us'] > 0 and u['waveform_redraw_max_us'] > 0 and u['slice_redraw_max_us'] > 0
print('M14 slices/waveform, unchanged dense workload, heaps, normal/synth and >=20% headroom: PASS')
