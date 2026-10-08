"""Validate genuine per-mode records; reject unqualified alternatives explicitly.

The production mode must pass 20% headroom. An alternate failing that limit is
recorded as rejected, never silently accepted or substituted in realtime.
Inherited analyzers remain strict and run for the qualified modes separately.
"""
from pathlib import Path
import json,re
deadline=5804.989
def rows(path,allow_rejected_failure=False):
    text=Path(path).read_text(errors='replace')
    if not allow_rejected_failure:
        assert not any(s in text for s in ('Guru Meditation','task_wdt:','stack overflow','panic'))
    result={}
    for line in text.splitlines():
        if line.startswith('[') and ']' in line:
            result[line.split(']')[0]+']']={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)}
    return text,result
summary=[]
for mode,name in enumerate(('NEAREST','LINEAR','HERMITE')):
    path=f'docs/GUITION_M21_{name}_FINAL_SERIAL.log'
    text,r=rows(path)
    config=r['[M21 configuration]'];assert config['mode']==mode and config['voice_bytes']==64
    assert config['scratch_bytes']==config['table_bytes']==0
    m=r['[M5]'];assert m['blocks']==13782 and m['active_max']==16
    assert m['misses']==m['failures']==m['timeouts']==0
    mem=r['[M5 memory]']
    for key in ('ps','internal','largest'): assert mem[key+'_before']==mem[key+'_after'],mem
    assert r['[M13 dense]']['active_min']==16
    assert r['[M181 x8]']['active_min']==16 and r['[M181 x8]']['full_blocks']>500
    for key in ('reverse','gate','releases','choke_ops','retriggers'):assert r['[M13 playback]'][key]>0
    assert r['[M19 overlap]']['blocks']>0 and r['[M19 overlap]']['x8_blocks']>0
    assert r['[M16 project]']['file_bytes']==52704
    frac=r['[M21 fractional]'];assert frac['mode']==mode and frac['full_blocks']>0
    cases=set()
    for line in text.splitlines():
        if line.startswith('[M21 voices]'):
            v={k:int(n) for k,n in re.findall(r'(\w+)=(\d+)',line)}
            assert v['blocks']==128 and v['voices']==16 and v['frames']==256
            cases.add((v['mode'],v['scenario']))
    assert cases=={(a,b) for a in range(3) for b in range(6)},cases
    coverage=r['[M17 chain]']['loops']>0
    summary.append(dict(mode=name,max_us=m['max'],headroom_percent=100*(1-m['max']/deadline),realtime_qualified=m['max']<=deadline*.8,chain_cycle_covered=coverage,qualified=m['max']<=deadline*.8 and coverage,
                        fractional_max_us=frac['max'],full_fractional_max_us=frac['full_max'],full_fractional_blocks=frac['full_blocks'],
                        psram_free=mem['ps_after'],largest_free=mem['largest_after'],internal_free=mem['internal_after']))
# The accepted default stays Nearest pending actual listening. No automatic
# selector is permitted. Check the compiled fallback, rather than picking the
# fastest mode silently from this table.
source=Path('src/app/sample_interpolation.h').read_text()
production=int(re.search(r'#define P4SDM_INTERPOLATION (\d+)',source)[1])
assert summary[production]['qualified'],summary[production]
for name in ('LINEAR','HERMITE'):
    text=Path(f'docs/GUITION_M21_SD_{name}_SERIAL.log').read_text(errors='replace')
    # Retain failed attempts; evaluate only the final fresh host session.
    text=text.rsplit('# HOST SESSION UTC ',1)[-1]
    assert '# HOST M21 SD workflows completed;' in text
    assert 'M21_INTERP_QUAL/A.P4P bytes=52704' in text
    assert re.search(r'\[Q SLOT\].*M21_INTERP_QUAL.*version=2 .*valid=1',text)
    metric=[{k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)} for line in text.splitlines() if line.startswith('[Q METRICS]')][-1]
    assert metric['misses']==metric['failures']==metric['timeouts']==0 and metric['active_max']>=2
    assert re.search(r'repeat_hits=[1-9]\d*',text)
worst=[]
for mode,name in ((0,'NEAREST'),(1,'LINEAR')):
    path=f'docs/GUITION_M21_{name}_WORST_FRACTIONAL_SERIAL.log'
    text,r=rows(path,allow_rejected_failure=mode==1)
    config=r['[M21 configuration]']
    assert config['mode']==mode
    assert all(config[k]==1 for k in ('forced_fractional','full_regions','forward_only'))
    complete='[M5 memory]' in r
    item=dict(mode=name,complete=complete,watchdog='task_wdt:' in text,panic='Guru Meditation' in text)
    if item['watchdog'] or item['panic']: assert not complete and mode==1
    if complete:
        frac=r['[M21 fractional]'];assert frac['full_blocks']>0
        item.update(r['[M21 isolated preparation]'])
        item.update(full_fractional_blocks=frac['full_blocks'],total_max_us=r['[M5]']['max'])
        item['equivalent_headroom_percent']=100*(1-item['equivalent_max']/deadline)
    else:
        item['result']='watchdog/panic; rejected' if item['watchdog'] or item['panic'] else 'capture timeout; unqualified; no timing inference'
    worst.append(item)
Path('docs/m21/device_results.json').write_text(json.dumps(dict(mixed=summary,worst_fractional=worst,production_mode=production,milestone_accepted=False),indent=2)+'\n')
for row in summary:
    print(f"M21 {row['mode']}: {row['max_us']} us / {row['headroom_percent']:.1f}% headroom / chain cycle={row['chain_cycle_covered']} / qualified={row['qualified']}")
print('M21 per-mode safety, retained dense workload, all six raw scenarios, SD workflows and stable heaps PASS; actual listening/touch remain pending')
