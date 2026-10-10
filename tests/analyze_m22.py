"""Fresh M22 capture gate; real serial stress uses scripted UI, never finger QA."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
from m213_capture import validate

p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('--synth',action='store_true');p.add_argument('--output',type=Path)
a=p.parse_args();text=a.log.read_text(errors='replace')
subprocess.run([sys.executable,'tests/analyze_m20.py',str(a.log)]+(['--synth'] if a.synth else []),check=True)
result=validate(text,None,13782)
assert result['accepted'],result
assert '[M21 configuration] mode=1' in text or a.synth
latency={}
for line in text.splitlines():
    if line.startswith('[M22 latency]'):
        phase=re.search(r'phase=(\w+)',line)[1]
        latency[phase]={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)}
assert set(latency)=={'poll_call','dispatch','state_update','enqueue_ack','prepare','present_call','full_page_prepare','controller_to_submission'},latency
for phase in ('poll_call','prepare','present_call','full_page_prepare'):
    assert latency[phase]['count']>0 and latency[phase]['max_us']>0
for row in latency.values():
    assert row['p50_upper_us']<=row['p95_upper_us']<=row['p99_upper_us']
result.update(latency=latency,touch_observations='controller packets only; human finger interaction not established',pixel_visible_latency='NOT MEASURED',ui_actions='scripted',physical_ux='PENDING')
if a.output: a.output.write_text(json.dumps(result,indent=2)+'\n')
print('M22 fresh scripted real-device UI/audio stress, zero faults, stable heaps, >=20% reserve PASS; physical UX pending')
