"""Validate and summarize real M4.1 hardware measurements, including failures."""
import argparse
import math
import re
import statistics
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('logs',nargs='+',type=Path)
args=parser.parse_args()
stages={}
for path in args.logs:
    log=path.read_text()
    assert '[M41] heap_loss ' in log and '[MEM] M41 after audio shutdown ' in log, f'incomplete {path}'
    assert 'shared_bus_unchanged=1' in log
    stage=None
    for line in log.splitlines():
        if m:=re.match(r'\[M41\] phase=(\d+) name=(\w+) .+ window_s=([\d.]+)',line):
            index=int(m[1]);assert index not in stages,'duplicate measured phase'
            stage=stages[index]={'name':m[2],'seconds':float(m[3]),'raw':[],'parts':{}}
        elif m:=re.match(r'\[M41\] raw phase=(\d+) block=(\d+) us=([\d,]+)$',line):
            raw=stages[int(m[1])]['raw'];assert len(raw)==int(m[2]);raw.extend(map(int,m[3].split(',')))
        elif stage is not None:
            numbers={key:float(value) for key,value in re.findall(r'(\w+)=(-?[\d.]+)(?=\s|$)',line)}
            if line.startswith('[M41] blocks='): stage['audio']=numbers
            elif line.startswith('[M41] render '): stage['render']=numbers
            elif line.startswith('[M41] UI '): stage['ui']=numbers
            elif line.startswith('[M41] touch polls='): stage['touch']=numbers
            elif line.startswith('[M41] memory '): stage['memory']=numbers
            elif line.startswith('[M41] transition '): stage['transition']=numbers
            elif m:=re.match(r'\[M41\] part=(\w+) avg=([\d.]+) max=(\d+)$',line):
                stage['parts'][m[1]]=(float(m[2]),int(m[3]))
    events=re.findall(r'\[M41\] event phase=(\d+) ms=(\d+) kind=(\d+) raw=(\d+),(\d+) logical=(\d+),(\d+)',log)
    for p,ms,kind,rx,ry,x,y in events:
        rx,ry,x,y=map(int,(rx,ry,x,y));assert 0<=rx<480 and 0<=ry<800
        assert (x,y)==(ry,479-rx),'physical event transform mismatch'
    print(f'{path.name}: {len(events)} genuine touch events validated.')
budget=256e6/44100
for index,s in stages.items():
    raw=s['raw'];a=s['audio'];r=s['render'];values=sorted(raw)
    assert len(raw)==a['blocks']==(10336 if index==14 else 517)
    assert a['active_min']==a['active_max']==16
    assert sum(v>=budget for v in raw)==a['render_misses']
    assert abs(statistics.mean(raw)-r['avg'])<=.0051
    assert r['min']==min(raw) and r['max']==max(raw)
    for p in (50,95,99): assert r[f'p{p}']==values[math.ceil(len(raw)*p/100)-1]
    assert abs(r['headroom_worst_us']-(budget-max(raw)))<.001
    mem=s['memory']
    for before,after in [('ps_before','ps_after'),('internal_before','internal_after'),('largest_before','largest_after')]:
        assert mem[before]==mem[after],f'heap change phase{index}'
    if 9<=index<=13:
        t=s['transition'];around=raw[80:180]
        assert t['max_us']==max(around) and t['misses']==sum(v>=budget for v in around)

def table(headers,rows):
    print('| '+' | '.join(headers)+' |');print('| '+' | '.join(['---']*len(headers))+' |')
    for row in rows:print('| '+' | '.join(map(str,row))+' |')
    print()

table(['Phase','Blocks','Min/mean us','p50/p95/p99 us','Max us','Misses','p99/max delta M3.2 us','Worst reserve','Result'],[
    [s['name'],len(s['raw']),f"{s['render']['min']:.0f}/{s['render']['avg']:.2f}",'/'.join(f"{s['render'][k]:.0f}" for k in ('p50','p95','p99')),int(s['render']['max']),int(s['audio']['render_misses']),f"{s['render']['p99']-2898:.0f}/{s['render']['max']-3592:.0f}",f"{100*(budget-s['render']['max'])/budget:.2f}%",'FAIL' if s['audio']['render_misses'] else 'SAFE' if s['render']['max']<=.8*budget else 'TIGHT']
    for s in stages.values()])
table(['Phase','Requested/achieved FPS','Frames/skips','Draw avg/max us','Repair avg/max us','PPA avg/max us','Cache avg/max us','Submit avg/max us','Wait avg/max us','Total avg/max us'],[
    [s['name'],f"{s['ui']['requested_fps']:.0f}/{s['ui']['achieved_fps']:.3f}",f"{s['ui']['frames']:.0f}/{s['ui']['skipped']:.0f}"]+[f"{s['parts'][key][0]:.2f}/{s['parts'][key][1]}" for key in ('draw','repair','ppa','cache','submit','wait','total')]
    for s in stages.values()])
table(['Phase','Dirty/repair bytes avg','Peak L/R','Nonzero/silent/rail','Write errors/timeouts','UI/touch errors','Touch Hz','Press/release/drag','Audio activity blocks/misses'],[
    [s['name'],f"{s['parts']['dirty_bytes'][0]:.2f}/{s['parts']['repair_bytes'][0]:.2f}",f"{s['audio']['peak_L']:.0f}/{s['audio']['peak_R']:.0f}",f"{s['audio']['nonzero']:.0f}/{s['audio']['silent']:.0f}/{s['audio']['rails']:.0f}",f"{s['audio']['write_errors']:.0f}/{s['audio']['timeouts']:.0f}",f"{s['ui']['errors']:.0f}/{s['touch']['errors']:.0f}",f"{s['touch']['hz']:.3f}",'/'.join(f"{s['touch'][k]:.0f}" for k in ('presses','releases','drags')),f"{s['touch']['activity_blocks']:.0f}/{s['touch']['activity_misses']:.0f}"]
    for s in stages.values()])
table(['Transition','Request block','In flight','Max around transition us','Misses blocks80–179','Idle observations'],[
    [s['name']]+[int(s['transition'][key]) for key in ('request_block','inflight','max_us','misses','idle_observations')]
    for i,s in stages.items() if 9<=i<=13])
table(['Phase','PSRAM before/after','Largest before/after','Internal before/after'],[
    [s['name']]+[f"{s['memory'][a]:.0f}/{s['memory'][b]:.0f}" for a,b in [('ps_before','ps_after'),('largest_before','largest_after'),('internal_before','internal_after')]]
    for s in stages.values()])
for path in args.logs:
    for line in path.read_text().splitlines():
        if line.startswith('[MEM]') or line.startswith('[M41] heap_loss') or line.startswith('[M41] touch_coverage'):print(line)
if 7 in stages and 14 in stages:
    accepted=all(stages[i]['audio']['render_misses']==0 and stages[i]['ui']['achieved_fps']>=29 for i in (7,14))
    print(f'LIGHT short +60s timing/FPS acceptance: {"PASS" if accepted else "FAIL"}; physical visuals and coverage must be verified separately.')
print(f'Validated {len(stages)} phases and {sum(len(s["raw"]) for s in stages.values())} ordered original-render timings.')
