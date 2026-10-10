"""Fail-closed M21.4 evidence; previous milestone logs cannot qualify this one."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys
from m213_capture import validate,validate_physical

p=argparse.ArgumentParser()
p.add_argument('--require-technical',action='store_true')
p.add_argument('--require-captures',action='store_true')
a=p.parse_args()
index=json.loads(Path('docs/m21/m214_artifacts.json').read_text())
cases=[]
for case,scenario,blocks in [('A',None,13782),('B',2,20672),('C',3,20672),
                             ('SAMPLE_sustained',2,103360),('SYNTH_sustained',None,103360)]:
    entry=index.get(case,{})
    path=Path(entry.get('capture','MISSING'))
    result=validate(path.read_text(errors='replace'),scenario,blocks) if path.is_file() else {'accepted':False,'complete':False}
    if case=='A' and path.is_file():
        subprocess.run([sys.executable,'tests/analyze_m20.py',str(path)],check=True)
    # The inherited validator's trace_matches=True for scenario=None means
    # no B/C digest applies. Never present that as immutable A equivalence.
    result['immutable_events']='PASS' if scenario and result.get('trace_matches') else 'NOT ESTABLISHED'
    if path.is_file(): result['sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
    cases.append(dict(case=case,log=path.as_posix(),**result))
physical=[]
for path in sorted(Path('docs/m21/m214-captures').rglob('SD_*.log')):
    physical.append(dict(log=path.as_posix(),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                         **validate_physical(path.read_text(errors='replace'))))
pending=['complete deterministic replay of original A UI/project/transient commands',
         'explicit human musical listening approval']
if not all(c['accepted'] for c in cases): pending.append('fresh A/B/C and sustained physical qualification')
if not any(c['accepted'] and 'SD_1' in c['log'] for c in physical): pending.append('final physical SD qualification')
result=dict(status='PARTIAL — technical qualification incomplete',baseline_sha='1c4c926326cc959cad711a74e5c058e08a718f2a',
            production_default='Nearest',candidate='guition_app_linear_candidate',
            cases=cases,physical=physical,pending=pending,human_listening='PENDING')
Path('docs/m21/m214_results.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n')
for c in cases: print(c['case'],c.get('timing',{}).get('max'),c['accepted'])
print(result['status'])
if a.require_captures:
    assert all(c['accepted'] for c in cases),'Fresh physical timing/integrity failed or missing'
    assert any(c['accepted'] and 'SD_1' in c['log'] for c in physical),'Final SD evidence missing'
if a.require_technical:
    raise SystemExit('M214 technical promotion blocked: '+pending[0])
