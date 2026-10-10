"""Fail-closed M21.4 evidence; previous milestone logs cannot qualify this one."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import sys
from m213_capture import validate,validate_physical,fields

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
    text=path.read_text(errors='replace'); result=validate_physical(text)
    # The append-only host log inserts its own command marker between serial
    # read chunks. Remove only those known markers to reconstruct device rows;
    # retain the byte-exact raw log and never ignore an incomplete heap row.
    device_text=re.sub(r'\n# HOST[^\n]*\n','',text)
    loads=[fields(line) for line in device_text.splitlines() if line.startswith('[M6 load]')]
    heap_ok=bool(loads) and all(r.get('ps_free',0)>=4194304 and
                  0<r.get('largest',0)<=r.get('ps_free',0) for r in loads)
    injected=[fields(line) for line in text.splitlines() if line.startswith('[M214 injected]')]
    injection_ok=({r.get('op') for r in injected}=={14,16,17} and
                  all(r.get('accepted')==0 and all(r.get(k)==1 for k in
                    ('preserved','payload_equal','free_equal')) for r in injected))
    power=[fields(line) for line in text.splitlines() if line.startswith('[M214 power]')]
    reset_ok=(len(power)==2 and all(r.get('brownout')==0 for r in power) and
              power[-1].get('uptime_ms',0)>power[0].get('uptime_ms',0) and
              power[-1].get('reset_reason')==power[0].get('reset_reason'))
    recovery='HOST M214 recovery/unmounted/short-read/allocation failures COMPLETE' in text
    crc_ok=(bool(re.search(r'\[Q CORRUPT\].*backup_bytes=52704 ok=1',text)) and
            bool(re.search(r'\[Q CORRUPT RESTORE\].*ok=1',text)) and
            bool(re.search(r'\[Q STATE\] bpm=143 .*project_busy=0 restoring=0.*PROJECT READY / 0 MISSING',text)))
    reference_ok=(bool(re.search(r'PROJECT READY / [1-9]\d* MISSING',text)) and
                  '[Q RENAME] restore=0 ok=1' in text and '[Q RENAME] restore=1 ok=1' in text)
    missing_segment=text.split('# HOST PROJECT M214_MISSING_QUAL',1)[-1]
    missing_ok=('# HOST PROJECT M214_MISSING_QUAL' in text and '[M6 load]' not in missing_segment and
                bool(re.search(r'\[Q STATE\] bpm=144 .*project_busy=0 restoring=0',missing_segment)))
    result.update(post_allocation_heap_checks=heap_ok,injected_failure_checks=injection_ok,
                  reset_reason_checks=reset_ok,power=power,recovery_checks=recovery,
                  crc_generation_checks=crc_ok,missing_reference_checks=reference_ok,missing_project_checks=missing_ok)
    result['accepted']=result['accepted'] and heap_ok and injection_ok and reset_ok and recovery and crc_ok and reference_ok and missing_ok
    physical.append(dict(log=path.as_posix(),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),**result))
pending=[]
replay_path=Path('docs/m21/m214_a_replay.json')
replay=json.loads(replay_path.read_text()) if replay_path.is_file() else {}
replay_capture=Path(replay.get('capture','MISSING'))
replay_ok=(replay.get('status')=='PASS' and replay_capture.is_file() and
           hashlib.sha256(replay_capture.read_bytes()).hexdigest()==replay.get('capture_sha256'))
if not replay_ok: pending.append('complete deterministic replay of original A UI/project/transient commands')
listening=json.loads(Path('docs/m21/m214_listening_acceptance.json').read_text())
if listening.get('status')!='APPROVED': pending.append('explicit human musical listening approval')
if not all(c['accepted'] for c in cases): pending.append('fresh A/B/C and sustained physical qualification')
for policy in (0,1):
    if not any(c['accepted'] and f'/SD_{policy}.log' in c['log'] for c in physical):
        pending.append(f'final physical SD qualification policy {policy}')
result=dict(status='PARTIAL — technical qualification incomplete' if pending else 'PASS — technical and musical acceptance',baseline_sha='1c4c926326cc959cad711a74e5c058e08a718f2a',
            production_default='Linear32 magnitude, direct PCM, staggered placement',candidate='guition_app_linear_candidate',
            cases=cases,physical=physical,pending=pending,human_listening=listening['status'],original_A_replay=replay)
Path('docs/m21/m214_results.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
for c in cases: print(c['case'],c.get('timing',{}).get('max'),c['accepted'])
print(result['status'])
if a.require_captures:
    assert all(c['accepted'] for c in cases),'Fresh physical timing/integrity failed or missing'
    assert any(c['accepted'] and 'SD_1' in c['log'] for c in physical),'Final SD evidence missing'
if a.require_technical and pending:
    raise SystemExit('M214 technical promotion blocked: '+pending[0])
