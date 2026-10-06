"""Check genuine M5 device captures; scripted actions never establish physical touch QA."""
import argparse
import re
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('log')
p.add_argument('--stress',action='store_true')
a=p.parse_args()
s=Path(a.log).read_text(errors='replace')
def row(prefix):
    line=next((line for line in s.splitlines() if line.startswith(prefix)),None)
    assert line, f'missing {prefix}'
    return {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)}
audio=row('[M5]')
ui=row('[M5 UI]')
mem=row('[M5 memory]')
assert audio['blocks']>=10336
assert audio['misses']==audio['failures']==audio['timeouts']==audio['rails']==0
assert ui['touch_errors']==ui['rejected']==0
assert mem['ps_before']==mem['ps_after'] and mem['largest_before']==mem['largest_after']
assert mem['internal_before']==mem['internal_after'], 'internal heap change requires investigation'
if a.stress:
    assert audio['active_max']==16 and audio['nonzero']>0
    assert audio['max']<=256/44100*1e6*.8, 'less than 20% worst-case engine headroom'
    for key in ('pads','steps','drags','pages','transport','bpm'):
        assert ui[key]>0, f'missing scripted action {key}'
    assert ui['submitted']>ui['full']
    normal=(ui['dirty_avg']*ui['submitted']-ui['full']*768000)/(ui['submitted']-ui['full'])
    print(f'Estimated non-page dirty average {normal:.1f} bytes (aggregate rounding only).')
print('M5 measured checks PASS; physical touch, audible output and visual integrity remain separate.')
