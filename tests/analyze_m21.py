"""Validate genuine per-mode records; reject unqualified alternatives explicitly.

The production mode must pass 20% headroom. An alternate failing that limit is
recorded as rejected, never silently accepted or substituted in realtime.
Inherited analyzers remain strict and run for the qualified modes separately.
"""
from pathlib import Path
import json,re
deadline=5804.989
def rows(path):
    text=Path(path).read_text(errors='replace')
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
    summary.append(dict(mode=name,max_us=m['max'],headroom_percent=100*(1-m['max']/deadline),qualified=m['max']<=deadline*.8,
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
Path('docs/m21/device_results.json').write_text(json.dumps(summary,indent=2)+'\n')
for row in summary:
    print(f"M21 {row['mode']}: {row['max_us']} us / {row['headroom_percent']:.1f}% headroom / "+('QUALIFIED realtime' if row['qualified'] else 'REJECTED headroom'))
print('M21 per-mode safety, retained dense workload, all six raw scenarios, SD workflows and stable heaps PASS; actual listening/touch remain pending')
