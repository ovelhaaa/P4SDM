"""M19 genuine device evidence, retaining every M18.1 assertion."""
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
text = Path(a.log).read_text(errors='replace')
def row(tag):
    lines = [line for line in text.splitlines() if line.startswith(tag)]
    assert len(lines) == 1, tag
    return {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', lines[0])}
subprocess.run([sys.executable, 'tests/analyze_m181.py', a.log] +
               (['--normal'] if a.normal else []) + (['--synth'] if a.synth else []), check=True)
m = row('[M19 analysis]')
o = row('[M19 overlap]')
assert o['candidate_bytes'] == 512 and o['proposal_bytes'] == 84
assert o['detector_bytes'] <= 640 and o['status_bytes'] <= 104 and o['storage_stack'] == 7000
if a.normal:
    assert all(value == 0 for value in m.values()), m
    assert all(o[k] == 0 for k in ('blocks', 'worst', 'misses', 'x8_blocks', 'ui_interval_max_us'))
else:
    t = row('[M19 timing]')
    assert t['completed'] == 3 and t['pcm_bytes'] == 4194304
    assert all(t[k] > 0 for k in ('one_second_us', 'loop_us', 'large_us', 'chunk_max_us'))
    assert t['large_us'] < 5_000_000 and t['chunk_max_us'] < 20_000
    assert m['frames'] >= 44100 + 176400 + 2097152
    assert o['blocks'] > 10 and o['x8_blocks'] > 10
    assert o['misses'] == 0 and 0 < o['worst'] <= 256 / 44100 * 1e6 * .8
    assert 0 < o['ui_interval_max_us'] < 100_000
    if a.synth:
        assert all(m[k] == 0 for k in ('requests', 'completed', 'proposals', 'apply', 'cancel', 'stale_discarded'))
    else:
        assert m['requests'] >= 3 and m['completed'] >= 3 and m['proposals'] >= 3
        assert m['apply'] == 1 and m['cancelled'] >= 1 and m['stale_discarded'] >= 1
        assert 1 <= m['min_count'] <= m['max_count'] <= 16
assert 'Guru Meditation' not in text and 'task_wdt' not in text
print('M19 bounded offline PCM analysis, proposal ownership, concurrent x8, UI/heaps and >=20% audio headroom PASS')
