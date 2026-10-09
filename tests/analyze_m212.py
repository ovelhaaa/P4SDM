"""Keep engineering results distinct from production acceptance.

No absent physical/listening/profiling evidence is inferred from green builds.
--require-acceptance fails closed while the documented gaps remain.
"""
from pathlib import Path
import argparse,hashlib,json,re
p=argparse.ArgumentParser();p.add_argument('--require-acceptance',action='store_true');a=p.parse_args()
def fields(line): return {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)}
memory=Path('docs/GUITION_M212_MEMORY_FINAL_SERIAL.log').read_text(errors='replace')
assert '[M212 benchmark] COMPLETE' in memory
config=fields(next(x for x in memory.splitlines() if x.startswith('[M212 memory]')))
registers=fields(next(x for x in memory.splitlines() if x.startswith('[M212 registers]')))
assert config['revision']==103 and config['psram_bytes']==33554432
assert config['sdk_speed_mhz']==200 and registers['effective_hz']==200000000
bandwidth=[dict(fields(x),MBps=float(re.search(r'MBps=([\d.]+)',x)[1])) for x in memory.splitlines() if x.startswith('[M212 bandwidth]')]
assert len(bandwidth)==43
assert len({x['checksum'] for x in bandwidth if x['order']<5})==1,'content mismatch'
fills=[fields(x) for x in memory.splitlines() if x.startswith('[M212 fill]')]
assert len(fills)==12 and len({x['checksum'] for x in fills})==1
cases=[]
for label in ('A_NEAREST','A_LINEAR32','B_NEAREST','B_LINEAR32','C_NEAREST','C_LINEAR32','B_MAGNITUDE','B_VALIDATED','B_FULL_GAIN'):
    path=Path(f'docs/GUITION_M212_{label}_SERIAL.log')
    if not path.exists(): continue
    text=path.read_text(errors='replace')
    rows={re.match(r'\[[^\]]+\]',x)[0]:fields(x) for x in text.splitlines() if re.match(r'\[[^\]]+\]',x)}
    faults=[marker for marker in ('task_wdt:','Guru Meditation','panic\'ed','stack overflow') if marker in text]
    m=rows.get('[M5]',{})
    clean=not faults and bool(m) and m.get('misses')==m.get('failures')==m.get('timeouts')==0 and '[M5 memory]' in rows
    entry=dict(case=label,log=path.as_posix(),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),clean_capture=clean,faults=faults,timing=m,
      reserve_pass=bool(m) and m['max']<=4643.991,
      headroom_percent=100*(1-m['max']/(256*1e6/44100)) if m else None,
      memory=rows.get('[M5 memory]',{}),trace=rows.get('[M212 trace]',{}),chain=rows.get('[M17 chain]',{}))
    mem=entry['memory']
    entry['heap_snapshots_equal']=bool(mem) and all(mem.get(k+'_before')==mem.get(k+'_after') for k in ('ps','internal','largest'))
    if label[0] in 'BC' and entry['trace']:
        scenario=2 if label[0]=='B' else 3
        expected=978631659 if scenario==2 else 3904423500
        trace=entry['trace']
        residency=[fields(x) for x in text.splitlines() if x.startswith('[M212 residency]')]
        entry['trace_matches_host']=trace['hash']==expected and trace['events']==126480 and trace['chain_loops']==230 and trace['repeat_x8']==7168
        entry['all_voice_residency']=len(residency)==16 and all(x['active_frames']==x['total_frames']==5292032 for x in residency)
    cases.append(entry)
matrix=[]
for size in (16,32,64,128):
    for fill in (0,1):
        path=Path(f'docs/GUITION_M212_MATRIX_{size}_{fill}_SERIAL.log')
        if not path.exists(): continue
        text=path.read_text(errors='replace')
        rows={re.match(r'\[[^\]]+\]',x)[0]:fields(x) for x in text.splitlines() if re.match(r'\[[^\]]+\]',x)}
        counts=rows.get('[M212 cache counts]',{})
        metric=rows.get('[M5]',{})
        stages=[fields(x) for x in text.splitlines() if x.startswith('[M211 block profile]')]
        matrix.append(dict(frames=size,fill=fill,log=path.as_posix(),timing=metric,counts=counts,
          hit_ratio=counts['hits']/(counts['hits']+counts['misses']) if counts else None,
          trace=rows.get('[M212 trace]',{}),profile=stages,
          stack=rows.get('[M211 profile memory]',{}),memory=rows.get('[M5 memory]',{})))
if len(matrix)==8:
    assert len({x['trace']['hash'] for x in matrix})==1,'Matrix control workload differs'
    assert all(x['timing']['blocks']==10336 and len(x['profile'])==5 for x in matrix)
pending=['matched complete inherited A Chain/event coverage','DSP substage attribution and instrumentation overhead',
         'capacity/fill qualification across A/C and unprofiled overhead controls','DMA underrun/backlog and scheduling-delay counters',
         'fresh cache-enabled resident musical WAV/physical SD regression',
         'extended heap/PCM integrity monitoring','actual musical listening','human touchscreen usability',
         'fresh extended SYNTH device regression','remote GitHub Actions result']
result=dict(status='PARTIAL',production=dict(interpolation=0,pcm_read_cache=0,linear_32bit=0),memory_config=config,
  registers=registers,electrical_clock_independently_verified=False,bandwidth=bandwidth,fills=fills,cases=cases,matrix=matrix,pending=pending)
Path('docs/m21/m212_results.json').write_text(json.dumps(result,indent=2)+'\n')
print('M212 hardware/content diagnostics PASS; production acceptance PARTIAL')
for case in cases: print(case['case'],case['timing'].get('max'),case['headroom_percent'],case['clean_capture'],case.get('trace_matches_host'))
if a.require_acceptance: raise SystemExit('M21.2 PARTIAL: missing evidence; defaults must remain unchanged')
