"""Fail-closed processing-only timing and immutable B/C trace validation."""
import re

def validate_physical(text):
    """Validate recorded physical timing; host completion alone cannot pass."""
    metrics=[fields(line) for line in text.splitlines() if line.startswith('[Q METRICS]')]
    playback=[fields(line) for line in text.splitlines() if line.startswith('[Q PLAYBACK]')]
    timing=metrics[-1] if metrics else {}
    faults=[x for x in ('task_wdt:', 'Guru Meditation', "panic'ed", 'stack overflow',
                        'Brownout detector', '[M5 INIT FAIL]') if x in text]
    complete='COMPLETE; no listening observed' in text
    repeat=max((row.get('repeat_hits',0) for row in playback),default=0)
    chain=max((row.get('chain_loops',0) for row in playback),default=0)
    safe=(bool(timing) and timing.get('blocks',0)>0 and not faults and
          all(timing.get(k)==0 for k in ('misses','failures','timeouts')) and
          timing.get('active_max')==16 and timing.get('nonzero',0)>0 and
          0<=timing.get('p99',-1)<=timing.get('max',-1))
    reserve=bool(timing) and timing.get('max',5805)<=4643
    restored='project_status=PROJECT READY / 0 MISSING' in text
    return dict(complete=complete,timing=timing,faults=faults,safe=safe,
                reserve_pass=reserve,repeat_hits=repeat,chain_loops=chain,
                project_restored=restored,accepted=complete and safe and reserve and repeat>0 and chain>0 and restored,
                headroom_percent=100*(1-timing['max']/(256*1e6/44100)) if timing else None)
def fields(line):
    return {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)}
def validate(text, scenario=None, blocks=20672):
    rows={re.match(r'\[[^\]]+\]',line)[0]:fields(line) for line in text.splitlines() if re.match(r'\[[^\]]+\]',line)}
    faults=[x for x in ('task_wdt:','Guru Meditation','panic\'ed','stack overflow','[M5 INIT FAIL]','FAIL allocation','Brownout detector') if x in text]
    if text.count('[AUDIO] I2S init')>1: faults.append('repeated audio startup')
    monitors=[fields(line) for line in text.splitlines() if line.startswith('[M213 monitor]')]
    if any(row.get('corrupt',1) for row in monitors): faults.append('PCM checksum changed')
    timing=rows.get('[M5]',{})
    quantiles=[timing.get(k,-1) for k in ('p50','p95','p99','max')]
    if timing and (any(v<0 for v in quantiles) or quantiles!=sorted(quantiles)):
        faults.append('invalid timing quantiles')
    complete=bool(timing) and '[M5 memory]' in rows and timing.get('blocks')==blocks
    if blocks==103360 and (len(monitors)<19 or any(r.get('resident')!=(16 if scenario else 0) for r in monitors)):
        faults.append('sustained PCM monitoring incomplete')
    safe=complete and not faults and all(timing.get(k)==0 for k in ('misses','failures','timeouts'))
    trace_ok=True
    if scenario:
        trace=rows.get('[M212 trace]',{})
        expected=({2:978631659,3:3904423500}[scenario] if blocks==20672 else
                  {2:2738615385,3:2484366403}[scenario] if blocks==103360 else None)
        residency=[fields(line) for line in text.splitlines() if line.startswith('[M212 residency]')]
        trace_ok=(expected is not None and trace.get('hash')==expected and trace.get('scenario')==scenario and
                  trace.get('events')==(126480 if blocks==20672 else 618000) and
                  trace.get('chain_loops')==(230 if blocks==20672 else 1190) and trace.get('repeat_x8')==7168 and
                  len(residency)==16 and {r.get('track') for r in residency}==set(range(16)) and
                  all(r.get('active_frames')==r.get('total_frames')==blocks*256 for r in residency))
    reserve=complete and timing.get('max',5805)<=4643
    return dict(complete=complete,safe=safe,faults=faults,timing=timing,memory=rows.get('[M5 memory]',{}),
                trace_matches=trace_ok,reserve_pass=reserve,accepted=safe and trace_ok and reserve,monitors=monitors,
                headroom_percent=100*(1-timing['max']/(256*1e6/44100)) if complete else None)
