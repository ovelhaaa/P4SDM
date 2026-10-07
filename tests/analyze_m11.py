"""Qualify genuine M11 tone/send stress evidence, retaining M10 assertions."""
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
    return {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)', lines[0])}
subprocess.run([sys.executable,'tests/analyze_m10.py',args.log] + (['--normal'] if args.normal else []), check=True)
tone = row('[M11 tone]')
ui = row('[M11 UI]')
assert 'task_wdt' not in data and 'Aborting.' not in data
if args.normal:
    assert tone['cutoff'] == tone['resonance'] == tone['send'] == tone['blocks'] == 0
else:
    for name in ('cutoff','resonance','send'):
        assert tone[name] > 500
        assert tone[name+'_min'] == 0 and tone[name+'_max'] == 127
    assert tone['blocks'] > 500 and 0 < tone['max'] <= 256/44100*1e6*.8
    assert tone['dense'] >= 2000 and 0 < tone['dense_max'] <= 256/44100*1e6*.8
    assert ui['entries'] >= 3 and ui['edits'] >= 10
    assert ui['widgets_max'] == 1 and ui['edit_full'] == 0
print('M11 tone/send commands, dense filters, bounded TONE UI and >=20% headroom: PASS')
