"""Validate complete recorded A journal, then replay source-accurate kernels."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from m213_capture import validate,fields

p=argparse.ArgumentParser();p.add_argument('capture',type=Path,nargs='?');a=p.parse_args()
if a.capture is None:
    a.capture=Path(json.loads(Path('docs/m21/m214_artifacts.json').read_text())['A_journal']['capture'])
text=a.capture.read_text(errors='replace')
# The original uninstrumented A still passes its unchanged strict analyzer in
# analyze_m214.py. This journal is an equivalence probe, not a replacement A
# timing/coverage qualification: instrumentation shifts wall-clock UI actions.
assert validate(text,None,13782)['accepted'],'Journal timing/integrity failed'
chain_row=next(fields(line) for line in text.splitlines() if line.startswith('[M17 chain]'))
assert all(chain_row.get(k,0)>0 for k in ('starts','advances','repeats','stops','switches'))
header=re.search(r'\[M214 journal\] engine_bytes=(\d+) records=(\d+) overflow=(\d+) blocks=(\d+) hash=(\d+) events=(\d+) rng=(\d+) chain=(\d+) x8=(\d+)',text)
assert header,'Complete A journal header missing'
size,count,overflow,blocks,digest,events,rng,chain,x8=map(int,header.groups())
assert overflow==0 and blocks==13782 and x8>0
assert f'[M214 journal] END records={count}' in text
initial=bytearray()
for offset,data in re.findall(r'\[M214 initial\] offset=(\d+) data=([0-9a-f]+)',text):
    assert int(offset)==len(initial),'Initial Engine dump is incomplete'
    initial.extend(bytes.fromhex(data))
assert len(initial)==size
duration_rows=re.findall(r'\[M214 duration\] track=(\d+) active_frames=(\d+)',text)
assert len(duration_rows)==16 and [int(t) for t,n in duration_rows]==list(range(16))
durations=[int(n) for t,n in duration_rows]
records=re.findall(r'\[M214 record\] index=(\d+) block=(\d+) type=(\d+) part=(\d+) kind=(\d+) track=(\d+) value=(-?\d+) pattern=(\d+) step=(\d+) pitch_source=(\d+) bank=([0-9a-f]*)',text)
assert len(records)==count
out=Path('.pio/m214-host');out.mkdir(parents=True,exist_ok=True)
schedule=out/'original-A.txt'
with schedule.open('w') as f:
    f.write(f'{size} {count} {blocks} {digest} {events}\n'+initial.hex()+'\n')
    f.write(' '.join(map(str,durations))+'\n')
    previous=-1
    for index,*fields in records:
        assert int(index)==previous+1;previous=int(index)
        f.write(' '.join(fields[:-1])+ ' '+(fields[-1] or '-')+'\n')
reference=None;summary=None
for mode,bits,magnitude in [(0,0,0),(1,0,0),(1,1,1)]:
    flags=[f'-DP4SDM_INTERPOLATION={mode}',f'-DP4SDM_LINEAR_32BIT={bits}',
           f'-DP4SDM_LINEAR_MAGNITUDE={magnitude}','-DP4SDM_PCM_READ_CACHE=0']
    exe=out/'original-A.exe';canonical=out/f'original-A-{mode}-{bits}.bin'
    command=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',*flags,
             'tests/m214_journal_replay.cpp','-o',str(exe)]
    subprocess.run(command,check=True)
    output=subprocess.check_output([str(exe),str(schedule),str(canonical)],text=True)
    result=hashlib.sha256(canonical.read_bytes()).hexdigest()
    print(mode,bits,magnitude,output,result,flush=True)
    if reference is None: reference=result;summary=output
    else: assert reference==result and summary==output,'Canonical A musical state differs'
    if sys.platform!='win32':
        subprocess.run(command[:2]+['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']+command[3:],check=True)
        subprocess.run([str(exe),str(schedule),str(canonical)],
                       env={**os.environ,'UBSAN_OPTIONS':'halt_on_error=1'},check=True)
        assert hashlib.sha256(canonical.read_bytes()).hexdigest()==reference
result=dict(status='PASS',capture=a.capture.as_posix(),capture_sha256=hashlib.sha256(a.capture.read_bytes()).hexdigest(),
            canonical_sha256=reference,records=count,events=events,hash=digest,projects=3,rng=rng,chain=chain,x8=x8,active_frames=durations,
            journal_chain_metrics=chain_row,original_strict_A_qualification='Separate unchanged analyzer required by analyze_m214.py',
            note='Every recorded actual A accepted command/boundary/transient bank replayed by audio blocks; physical journal digest and all active durations match. Journal Chain transitions are replayed, but this instrumented probe has zero complete Chain loops and is not strict original A coverage evidence. Original wall-clock runs are not declared identical.')
Path('docs/m21/m214_a_replay.json').write_text(json.dumps(result,indent=2)+'\n')
print('Original A complete recorded musical replay PASS')
