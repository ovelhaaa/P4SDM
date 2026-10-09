from pathlib import Path
import argparse, hashlib, json
from m213_capture import fields, validate, validate_physical
p=argparse.ArgumentParser();p.add_argument('--require-acceptance',action='store_true');p.add_argument('--require-case',action='append',default=[]);p.add_argument('--require-physical',action='append',default=[]);a=p.parse_args()
cases=[]
for path in sorted(Path('docs').glob('GUITION_M213_*_SERIAL.log')):
    label=path.stem.removeprefix('GUITION_M213_').removesuffix('_SERIAL')
    if label.startswith('SD_') or label in ('MEMORY','NORMAL'): continue
    scenario=2 if label.startswith('B_') else 3 if label.startswith('C_') else None
    entry=dict(case=label,log=path.as_posix(),sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    entry.update(validate(path.read_text(errors='replace'),scenario,103360 if 'SUSTAINED' in label else 20672 if scenario else 13782))
    cases.append(entry)
for label in a.require_case:
    assert any(c['case']==label and c['accepted'] for c in cases),f'Case not qualified: {label}'
physical=[]
for path in sorted(Path('docs').glob('GUITION_M213_SD_*_SERIAL.log')):
    text=path.read_text(errors='replace')
    locality=[fields(line) for line in text.splitlines() if line.startswith('[M213 locality]')]
    groups={r['tag'] for r in locality}
    for tag in groups:
        rows=[r for r in locality if r['tag']==tag]
        assert len(rows)==6 and len({r['checksum'] for r in rows})==1,'Missing rows or content mismatch'
        assert all(r['reads']==131072 and r['repeats']==32 for r in rows)
    physical.append(dict(log=path.as_posix(),**validate_physical(text),locality=locality,
      addresses=[line for line in text.splitlines() if line.startswith('[M213 address]')]))
for label in a.require_physical:
    assert any(c['log']==f'docs/GUITION_M213_SD_{label}_SERIAL.log' and c['accepted'] for c in physical),f'Physical case not qualified: {label}'
memory=Path('docs/GUITION_M213_MEMORY_SERIAL.log')
locality=[]
if memory.exists():
    text=memory.read_text(errors='replace')
    assert '[M213 memory] COMPLETE' in text
    locality=[fields(line) for line in text.splitlines() if line.startswith('[M213 locality]')]
    assert len(locality)==30 and len({r['checksum'] for r in locality})==1
    assert all(r['reads']==131072 and r['concurrent_audio']==0 for r in locality)
pending=['genuine musical A/B listening','full A immutable control-event equivalence',
         'production promotion decision after all required physical/sustained/CI checks']
result=dict(status='PARTIAL',baseline_sha='246434cf7024f7158954014f9f0857f21d9a9b1e',
    production=dict(interpolation=0,pcm_read_cache=0,linear_32bit=0,sample_layout=0),
    cases=cases,synthetic_locality=locality,physical=physical,pending=pending)
Path('docs/m21/m213_results.json').write_text(json.dumps(result,indent=2)+'\n')
for c in cases: print(c['case'],c['timing'].get('max'),c['accepted'],c['complete'])
print('M213 engineering capture validation; production PARTIAL')
if a.require_acceptance: raise SystemExit('M213 PARTIAL: '+', '.join(pending))
