"""Fail-closed processing-only timing and immutable B/C trace validation."""
import re
def fields(line):
    return {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)}
def validate(text, scenario=None, blocks=20672):
    rows={re.match(r'\[[^\]]+\]',line)[0]:fields(line) for line in text.splitlines() if re.match(r'\[[^\]]+\]',line)}
    faults=[x for x in ('task_wdt:','Guru Meditation','panic\'ed','stack overflow','[M5 INIT FAIL]','FAIL allocation') if x in text]
    timing=rows.get('[M5]',{})
    complete=bool(timing) and '[M5 memory]' in rows and timing.get('blocks')==blocks
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
                trace_matches=trace_ok,reserve_pass=reserve,accepted=safe and trace_ok and reserve,
                headroom_percent=100*(1-timing['max']/(256*1e6/44100)) if complete else None)
