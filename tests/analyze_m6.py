"""Qualify a real M6 no-card capture; fail incomplete serial records."""
import argparse,re,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('log');a=p.parse_args()
s=Path(a.log).read_text(errors='replace')
mounts=[x for x in s.splitlines() if x.startswith('[M6 SD]')]
assert len(mounts)==1 and 'mounted=0' in mounts[0] and 'NO CARD' in mounts[0]
assert '[M6 budget]' in s and 'samples=0' in s
subprocess.run([sys.executable,'tests/analyze_m5.py',a.log,'--stress'],check=True)
print('M6 no-card boot, one mount attempt, synth/UI/audio and stable heap: PASS')
print('PHYSICAL SD VALIDATION PENDING; this capture does not qualify resident sample playback.')
