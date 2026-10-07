"""Qualify genuine M10 captures while retaining M3–M9 regression analyzers."""
import argparse
import re
import subprocess
import sys
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('log')
parser.add_argument('--normal', action='store_true')
parser.add_argument('--sample', action='store_true', help='resident PCM: WAVE locks must remain suppressed')
parser.add_argument('--pattern-bytes', type=int, default=2082)
args=parser.parse_args()
data=Path(args.log).read_text(errors='replace')
def row(prefix):
    lines=[line for line in data.splitlines() if line.startswith(prefix)]
    assert len(lines)==1, f'missing or repeated {prefix}'
    return {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',lines[0])}
locks=row('[M10 locks]')
if args.normal:
    subprocess.run([sys.executable,'tests/analyze_m5.py',args.log],check=True)
    audio=row('[M5]')
    assert audio['active_max']==audio['nonzero']==audio['peak']==0
    assert row('[M8 groove]')['parents']==row('[M8 groove]')['ratchets']==0
    assert all(locks[key]==0 for key in ('locked','unlocked','pitch','volume','pan','wave'))
    assert row('[M5 UI]')['pads']==row('[M5 UI]')['steps']==0
    assert locks['pattern_bytes']==args.pattern_bytes and locks['bank_bytes']==args.pattern_bytes*16
    print('M10 normal no-card idle, no parameter residue, stable heaps: PASS')
    sys.exit(0)
subprocess.run([sys.executable,'tests/analyze_m9.py',args.log,'--pattern-bytes',str(args.pattern_bytes)],check=True)
for key in ('locked','unlocked','pitch','volume','pan','wave'):
    if key == 'wave' and args.sample:
        assert locks[key] == 0, 'SAMPLE unexpectedly resolved a WAVE lock'
    else:
        assert locks[key]>0, f'unexercised {key}'
assert locks['pattern_bytes']==args.pattern_bytes and locks['bank_bytes']==args.pattern_bytes*16
assert locks['locked']+locks['unlocked']==row('[M8 groove]')['passed']
worst=row('[M10 worst]')
assert worst['blocks']>=2000, 'missing all-lock 16-track 4x dense envelope'
assert 0<worst['max']<=256/44100*1e6*.8, 'locked block headroom <20%'
assert worst['max']<=row('[M5]')['max']
ui=row('[M10 UI]')
assert ui['entries']>=3 and ui['edits']>=10
assert 0<ui['widgets_max']<=8 and ui['edit_full']==0
print('M10 locks + all-lock dense realtime + bounded dirty UI: PASS')
