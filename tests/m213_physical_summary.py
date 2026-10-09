"""Summarize addresses without confusing mapped addresses with bus addresses."""
from pathlib import Path
import itertools,json,re
summaries=[]
for path in sorted(Path('docs').glob('GUITION_M213_SD_*_SERIAL.log')):
    text=path.read_text(errors='replace')
    pending=[];snapshots=[]
    for line in text.splitlines():
        if line.startswith('[M213 address]'):
            row={k:int(v,16 if v.startswith('0x') else 10) for k,v in re.findall(r'(\w+)=(0x[0-9a-fA-F]+|\d+)',line)}
            pending.append(row);pending=pending[-16:]
        if line.startswith('[M213 locality]') and 'order=0 eviction=0 ' in line:
            tag=int(re.search(r'tag=(\d+)',line)[1])
            valid=len(pending)==16 and {r['track'] for r in pending}==set(range(16))
            if not valid:
                snapshots.append(dict(tag=tag,complete_addresses=False));continue
            pairs=[abs(a['pcm']-b['pcm']) for a,b in itertools.combinations(pending,2)]
            by_address=sorted(pending,key=lambda r:r['pcm'])
            disjoint=all(a['pcm']+a['frames']*2<=b['pcm'] for a,b in zip(by_address,by_address[1:]))
            snapshots.append(dict(tag=tag,complete_addresses=True,addresses=list(pending),
              minimum_distance=min(pairs),maximum_distance=max(pairs),
              pairs_equal_mod131072=sum(d%131072==0 for d in pairs),
              pairs_same_line_mod131072=sum((a['pcm']//64-b['pcm']//64)%2048==0
                for a,b in itertools.combinations(pending,2)),pcm_disjoint=disjoint))
            assert disjoint,'Resident PCM views overlap: '+str(path)
    summaries.append(dict(log=path.as_posix(),complete='COMPLETE; no listening observed' in text,
                          snapshots=snapshots))
Path('docs/m21/m213_physical_layouts.json').write_text(json.dumps(summaries,indent=2)+'\n')
for s in summaries:
    print(s['log'],s['complete'],[(r['tag'],r.get('pairs_equal_mod131072'),r.get('pairs_same_line_mod131072')) for r in s['snapshots']])
