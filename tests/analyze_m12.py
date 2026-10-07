"""Qualify genuine M12 captures; keep all inherited assertions and budgets."""
import argparse
import re
import subprocess
import sys
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('log')
parser.add_argument('--normal', action='store_true')
args = parser.parse_args()
data = Path(args.log).read_text(errors='replace')

def row(prefix):
    lines = [line for line in data.splitlines() if line.startswith(prefix)]
    assert len(lines) == 1, f'missing or repeated {prefix}'
    return {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', lines[0])}

subprocess.run([sys.executable, 'tests/analyze_m11.py', args.log,
                '--pattern-bytes', '2850'] + (['--normal'] if args.normal else []), check=True)
locks = row('[M12 locks]')
assert locks['step_bytes'] == 8 and locks['event_bytes'] == 10
assert locks['voice_bytes'] == 224
assert row('[M10 locks]')['engine_bytes'] == 46592
audio = row('[M5]')
assert audio['blocks'] * 256 / 44100 >= 60
assert 'task_wdt' not in data and 'Guru Meditation' not in data
if args.normal:
    for name in ('cutoff', 'resonance', 'send', 'coefficients', 'avoided',
                 'send_changes', 'coefficient_blocks', 'coefficient_max',
                 'edit_blocks', 'edit_max', 'commands', 'idle_residue'):
        assert locks[name] == 0, f'normal residue: {name}'
    assert row('[M12 UI]')['entries'] == row('[M12 UI]')['edits'] == 0
else:
    for name in ('cutoff', 'resonance', 'send', 'coefficients', 'avoided',
                 'send_changes', 'coefficient_blocks', 'edit_blocks', 'commands'):
        assert locks[name] > 0, f'missing coverage: {name}'
    for name in ('coefficient_max', 'edit_max'):
        assert 0 < locks[name] <= 256 / 44100 * 1e6 * .8
        assert locks[name] <= audio['max']
    assert locks['avoided'] > locks['coefficients'], 'ratchets not reusing tone'
    ui = row('[M12 UI]')
    assert ui['entries'] >= 3 and ui['edits'] >= 10
    assert 0 < ui['widgets_max'] <= 8 and ui['edit_full'] == 0
print('M12 tone locks, coefficient/edit blocks, cache, memory and >=20% headroom: PASS')
