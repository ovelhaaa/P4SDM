"""Validate a complete hardware log and derive tables from measured blocks only."""
import argparse
import math
import re
import statistics
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('log',type=Path)
args=parser.parse_args()
text=args.log.read_text()
stages={}
current=None
for line in text.splitlines():
    if match:=re.fullmatch(r'\[M3.2\] stage=(\w+) mask=(\d+) init=(\w+)',line):
        current=stages[match[1]]={'init':match[3], 'raw':[]}
    elif match:=re.fullmatch(r'\[M3.2\] raw stage=(\w+) block=(\d+) us=([\d,]+)',line):
        data=stages[match[1]]['raw']
        assert len(data)==int(match[2]), 'Missing ordered timing data'
        data.extend(map(int,match[3].split(',')))
    elif current is not None:
        if line.startswith('[M3.2] voices=') or line.startswith('[M3.2] PCM '):
            current.update({key:int(value) for key,value in re.findall(r'(\w+)=(-?\d+)(?=\s|$)',line)})
        elif match:=re.fullmatch(r'\[M3.2\] (render|write|cycle) us (.+)',line):
            current[match[1]]={key:float(value) for key,value in re.findall(r'(\w+)=([\d.]+)',match[2])}
expected=['Dry','Reverb','Delay','Chorus','Flanger','Tremolo','RingMod','Distortion','Bitcrusher','A','B','C','D']
assert list(stages)==expected, 'Incomplete stage log'
assert '[MEM] after shutdown ' in text, 'Incomplete final report'
budget=256*1e6/44100
for name,s in stages.items():
    assert s['init']=='OK', f'{name}: initialization failed; no measurement claim possible'
    assert s['blocks']==517 and len(s['raw'])==517, f'{name}: incomplete blocks'
    assert s['active_min']==s['active_max']==16, f'{name}: workload mismatch'
    ordered=sorted(s['raw'])
    for key,value in [('min',ordered[0]),('max',ordered[-1])]+[(f'p{p}',ordered[math.ceil(len(ordered)*p/100)-1]) for p in (50,95,99)]:
        assert s['render'][key]==value, f'{name}: {key} mismatch'
    assert abs(statistics.mean(ordered)-s['render']['avg'])<=0.0051
    assert s['render_misses']==sum(value>=budget for value in ordered)
    s['category']='FAIL' if s['render_misses'] else 'SAFE' if ordered[-1]<=budget*.8 else 'TIGHT'

def table(headers,rows):
    print('| '+' | '.join(headers)+' |')
    print('| '+' | '.join(['---']*len(headers))+' |')
    for row in rows: print('| '+' | '.join(map(str,row))+' |')
    print()

dry=stages['Dry']['render']
for names in [expected[:9],expected[9:]]:
    table(['Stage','p99 us','Max us','Delta p99/max vs dry us','Budget p99/max %','Headroom p99/worst us','Render misses','Result'],[
        [name,int((r:=stages[name]['render'])['p99']),int(r['max']),f"{r['p99']-dry['p99']:.0f} / {r['max']-dry['max']:.0f}",
         f"{r['p99']/budget*100:.1f} / {r['max']/budget*100:.1f}",f"{budget-r['p99']:.3f} / {budget-r['max']:.3f}",stages[name]['render_misses'],stages[name]['category']]
        for name in names])
table(['Stage','Min us','Average us','p50 us','p95 us','p99 us','Max us'],[
    [name]+[f"{s['render'][key]:.2f}" if key=='avg' else int(s['render'][key]) for key in ('min','avg','p50','p95','p99','max')]
    for name,s in stages.items()])
table(['Stage','Blocks','Active min/max','Peak L/R/mono','Nonzero mono frames','Silent blocks','Rail frames','Write failures/timeouts','Cycle crossings'],[
    [name,s['blocks'],f"{s['active_min']}/{s['active_max']}",f"{s['peak_L']}/{s['peak_R']}/{s['peak_mono']}",s['nonzero_frames'],s['silent_blocks'],s['rail_frames'],f"{s['write_failures']}/{s['timeouts']}",s['cycle_misses']]
    for name,s in stages.items()])
table(['Stage','First block us','Top 3 (block:us)','Quarter means us'],[
    [name,s['raw'][0],', '.join(f'{i}:{s["raw"][i]}' for i in sorted(range(517),key=lambda i:s['raw'][i],reverse=True)[:3]),
     ' / '.join(f'{statistics.mean(s["raw"][i*517//4:(i+1)*517//4]):.1f}' for i in range(4))]
    for name,s in stages.items()])
memory=re.findall(r'\[MEM\] (.+) PSRAM total=(\d+) free=(\d+) largest=(\d+) internal_free=(\d+)',text)
table(['Snapshot','PSRAM free bytes','PSRAM largest bytes','Internal free bytes'],[[name,free,largest,internal] for name,total,free,largest,internal in memory])
assert 'stress internal_loss=0' in text and 'stress psram_loss=0 init_heap_ok=1' in text
assert text.count('heap_restored=1')==8
print('All 13 stages and 6721 ordered block timings validated; heap restored after all eight individual initializations; no stress heap loss.')
