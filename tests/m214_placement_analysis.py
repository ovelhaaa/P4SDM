"""Compare fresh ordinary/staggered WAV layouts and actual allocator overhead."""
from pathlib import Path
import json
import re
from m213_capture import fields

index=json.loads(Path('docs/m21/m214_artifacts.json').read_text())
cases={}
for policy in (0,1):
    raw=Path(index[f'SD_{policy}']['capture']).read_text()
    text=re.sub(r'\n# HOST[^\n]*\n','',raw)
    assert 'HOST M214 recovery/unmounted/short-read/allocation failures COMPLETE' in raw
    local=[fields(line) for line in text.splitlines() if line.startswith('[M213 locality]')]
    addresses=[line for line in text.splitlines() if line.startswith('[M213 address]')]
    for line in addresses:
        row=fields(line)
        pcm=re.search(r'pcm=(0x[0-9a-f]+)',line)
        base=re.search(r'base=(0x[0-9a-f]+)',line)
        assert pcm and base and all(k in row for k in ('track','bytes','frames','owner_usable_bytes'))
        data,owner=int(pcm[1],16),int(base[1],16)
        assert row['owner_usable_bytes']>=row['bytes']>=row['frames']*2
        if policy:
            assert data==((owner+63)&~63)+row['track']*64
            assert row['bytes']==row['frames']*2+63+row['track']*64
        else: assert data==owner and row['bytes']==row['frames']*2
    equal=re.search(r'\[M213 physical\] tag=1 complete=1\s*(\[Q HEAP\][^\n]+)',text)
    assert equal
    cases[policy]=dict(locality=local,equal_wav_heap=fields(equal[1]),address_rows=len(addresses))

rows=[]
for left,right in zip(cases[0]['locality'],cases[1]['locality']):
    assert all(left[k]==right[k] for k in ('tag','order','eviction','repeats','reads','checksum'))
    rows.append(dict(tag=left['tag'],order=left['order'],eviction=left['eviction'],
                     ordinary_us_per_4096_reads=left['us']/left['repeats'],
                     staggered_us_per_4096_reads=right['us']/right['repeats'],
                     ordinary_max_us=left['max'],staggered_max_us=right['max'],
                     checksum=left['checksum']))
assert len(cases[0]['locality'])==len(cases[1]['locality']) and len(rows)>=36
result=dict(status='PASS',equal_wav_heap={str(k):v['equal_wav_heap'] for k,v in cases.items()},
            actual_equal_wav_extra_psram=cases[0]['equal_wav_heap']['free']-cases[1]['equal_wav_heap']['free'],
            requested_equal_wav_padding=cases[1]['equal_wav_heap']['payload']-cases[0]['equal_wav_heap']['payload'],
            address_rows={str(k):v['address_rows'] for k,v in cases.items()},rows=rows,
            note='Real WAVs, concurrent audio; eviction sweeps do not prove cold cache. Lookup averages/maxima are not full-renderer speedup. Requested padding and actual heap delta differ.')
Path('docs/m21/m214_placement_results.json').write_text(json.dumps(result,indent=2)+'\n')
print('Fresh WAV locality, exact owning/view addresses and actual heap comparison PASS')
