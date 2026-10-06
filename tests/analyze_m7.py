"""Validate genuine M7 no-card device evidence, including actual audio transitions."""
import argparse, re, subprocess, sys
from pathlib import Path
p = argparse.ArgumentParser(); p.add_argument('log'); a = p.parse_args()
s = Path(a.log).read_text(errors='replace')
def row(prefix):
    line = next((line for line in s.splitlines() if line.startswith(prefix)), None)
    assert line, f'missing {prefix}'
    return {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', line)}
subprocess.run([sys.executable, 'tests/analyze_m5.py', a.log], check=True)
audio = row('[M5]'); ui = row('[M5 UI]'); patterns = row('[M7 patterns]'); applied = row('[M7 applied]')
assert audio['blocks'] * 256 / 44100 >= 60
assert ui['seconds'] >= 63 and ui['submitted'] == ui['completed']
assert audio['active_max'] == 16 and audio['nonzero'] > 0
assert audio['max'] < 256 / 44100 * 1e6
assert patterns['switches'] >= 8 and patterns['loops'] > patterns['switches']
assert patterns['normal_frames'] > 0 and patterns['dirty_max'] < 768000
assert patterns['pattern_bytes'] <= 34 and patterns['command_bytes'] <= 12
for key in ('long_to_short', 'short_to_long'):
    assert applied[key] > 0, f'missing real transition: {key}'
for key in ('queue_replacements', 'copies', 'clears', 'lengths', 'solos', 'mutes', 'steps'):
    assert applied[key] >= 4, f'not enough real audio operations: {key}'
assert 'Guru Meditation' not in s and 'INIT FAIL' not in s
print('M7 actual multi-pattern transitions, >=60s audio, PCM, deadlines, dirty rendering and stable heaps: PASS')
