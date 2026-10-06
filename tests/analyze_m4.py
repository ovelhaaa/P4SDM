"""Derive coexistence tables only from complete real M4 serial measurements."""
import argparse
import math
import re
import statistics
from pathlib import Path
parser=argparse.ArgumentParser(); parser.add_argument('log',type=Path)
parser.add_argument('--display-log',type=Path); args=parser.parse_args()
log=args.log.read_text(); stages={}; stage=None
for line in log.splitlines():
    if match:=re.match(r'\[M4\] stage=(\d+) mode=(\w+) audio_core=0 audio_priority=(\d+) ui_core=(\d+)',line):
        stage=stages[int(match[1])]={'mode':match[2],'ui_core':int(match[4]),'raw':[]}
    elif match:=re.fullmatch(r'\[M4\] raw stage=(\d+) block=(\d+) us=([\d,]+)',line):
        raw=stages[int(match[1])]['raw']; assert len(raw)==int(match[2]),'missing ordered blocks'
        raw.extend(map(int,match[3].split(',')))
    elif stage is not None:
        if match:=re.fullmatch(r'\[M4\] (render|write|cycle) us (.+)',line):
            stage[match[1]]={key:float(value) for key,value in re.findall(r'(\w+)=([\d.]+)',match[2])}
        elif line.startswith('[M4] UI '): stage['ui']={key:float(value) for key,value in re.findall(r'(\w+)=([\d.]+)',line)}
        elif line.startswith('[M4] touch requested_'): stage['touch']={key:float(value) for key,value in re.findall(r'(\w+)=([\d.]+)',line)}
        elif line.startswith('[M4] blocks=') or line.startswith('[M4] PCM '):
            stage.update({key:int(value) for key,value in re.findall(r'(\w+)=(-?\d+)(?=\s|$)',line)})
        elif line.startswith('[M4] memory_'):
            stage['mem_start' if 'memory_start' in line else 'mem_end']={key:int(value) for key,value in re.findall(r'(\w+)=(\d+)',line)}
assert list(stages)==list(range(6)),'incomplete modes'
assert '[M4] result=' in log and '[MEM] after audio shutdown ' in log,'incomplete final report'
assert 'shared_bus_unchanged=1' in log
budget=256*1e6/44100
for index,s in stages.items():
    assert s['blocks']==len(s['raw'])==517 and s['active_min']==s['active_max']==16
    values=sorted(s['raw'])
    for key,value in [('min',values[0]),('max',values[-1])]+[(f'p{p}',values[math.ceil(len(values)*p/100)-1]) for p in (50,95,99)]:
        assert s['render'][key]==value,(index,key)
    assert abs(statistics.mean(values)-s['render']['avg'])<=.0051
    assert sum(value>=budget for value in values)==s['render_misses']
    s['category']='FAIL' if s['render_misses'] else 'SAFE' if values[-1]<=.8*budget else 'TIGHT'
    if index%3: assert s['ui']['frames']>0 and s['ui']['core_observed']==s['ui_core']
def table(headers,rows):
    print('| '+' | '.join(headers)+' |'); print('| '+' | '.join(['---']*len(headers))+' |')
    for row in rows: print('| '+' | '.join(map(str,row))+' |')
    print()
table(['Stage','UI core','p99 us','Max us','Delta p99/max vs M3.2 Delay us','Headroom p99/worst us','Render misses','Result'],[
    [s['mode'],s['ui_core'],int((r:=s['render'])['p99']),int(r['max']),f"{r['p99']-2898:.0f}/{r['max']-3592:.0f}",f"{budget-r['p99']:.3f}/{budget-r['max']:.3f}",s['render_misses'],s['category']]
    for s in stages.values()])
table(['Stage/UI core','Min us','Average us','p50 us','p95 us','p99 us','Max us','First block us','Top 3 block:us'],[
    [f"{s['mode']}/{s['ui_core']}"]+[f"{s['render'][key]:.2f}" if key=='avg' else int(s['render'][key]) for key in ('min','avg','p50','p95','p99','max')]+[s['raw'][0],', '.join(f'{i}:{s["raw"][i]}' for i in sorted(range(517),key=lambda i:s['raw'][i],reverse=True)[:3])]
    for s in stages.values()])
table(['Stage/UI core','Blocks/active','Peak L/R/mono','Nonzero frames','Silent blocks','Rail frames','Write errors/timeouts','Cycle crossings'],[
    [f"{s['mode']}/{s['ui_core']}",f"{s['blocks']}/16",f"{s['peak_L']}/{s['peak_R']}/{s['peak_mono']}",s['nonzero_frames'],s['silent_blocks'],s['rail_frames'],f"{s['write_failures']}/{s['timeouts']}",s['cycle_misses']]
    for s in stages.values()])
table(['Stage/UI core','Requested FPS','Achieved FPS','Frames','Update avg/max us','Skipped frames','UI errors','Touch polls/Hz','Touch errors','Fresh touch/press/release'],[
    [f"{s['mode']}/{s['ui_core']}",int((u:=s['ui'])['requested_fps']),f"{u['achieved_fps']:.3f}",int(u['frames']),f"{u['update_avg_us']:.2f}/{u['update_max_us']:.0f}",int(u['skipped']),int(u['errors']),f"{int((t:=s['touch'])['polls'])}/{t['achieved_hz']:.3f}",int(t['errors']),f"{t['fresh_updates']:.0f}/{t['presses']:.0f}/{t['releases']:.0f}"]
    for s in stages.values()])
table(['Stage/UI core','PSRAM before/after','Largest block before/after','Internal before/after'],[
    [f"{s['mode']}/{s['ui_core']}"]+[f"{s['mem_start'][key]}/{s['mem_end'][key]}" for key in ('psram_free','largest','internal_free')]
    for s in stages.values()])
memory=re.findall(r'\[MEM\] (.+) PSRAM total=(\d+) free=(\d+) largest=(\d+) internal_free=(\d+)',log)
table(['Snapshot','PSRAM free bytes','PSRAM largest bytes','Internal free bytes'],[[name,free,largest,internal] for name,total,free,largest,internal in memory])
for line in log.splitlines():
    if line.startswith('[M4] heap_loss') or line.startswith('[M4] touch_coverage') or line.startswith('[M4] result='): print(line)
print('Validated six modes, 3102 ordered audio block timings and the genuine 16-voice workload.')
if args.display_log:
    events=re.findall(r'\[M4 TOUCH\] (PRESS|RELEASE) raw=(\d+),(\d+) logical=(\d+),(\d+) count=(\d+)',args.display_log.read_text())
    assert events,'no physical touch events captured'
    for kind,raw_x,raw_y,x,y,count in events:
        raw_x,raw_y,x,y,count=map(int,(raw_x,raw_y,x,y,count))
        assert 0<=raw_x<480 and 0<=raw_y<800
        assert (x,y)==(raw_y,479-raw_x),'touch transform mismatch'
        assert (count>0)==(kind=='PRESS')
    print(f'Validated {len(events)} genuine standalone touch transitions and their native/logical transforms; exact corner coverage remains a physical test.')
