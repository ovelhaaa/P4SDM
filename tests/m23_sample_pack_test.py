"""Verify PCM, source pins, provenance, kits, budgets and byte reproducibility."""
from pathlib import Path
import hashlib,json,subprocess,sys,tempfile,wave
import numpy as np
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
from m23_sample_pack import build
pack=root/'assets/m23';m=json.loads((pack/'manifest.json').read_text())
assert len(m['samples'])==42
required=['filename','instrument','description','duration','frames','channels','root_note','bpm','suggested_use','source_type','source_url','original_author','license','conversion','sha256','peak','rms','clipped_frames','dc']
for e in m['samples']:
    assert all(k in e for k in required)
    path=pack/e['filename'];data=path.read_bytes()
    assert data[:4]==b'RIFF' and data[8:12]==b'WAVE' and int.from_bytes(data[4:8],'little')+8==len(data)
    assert hashlib.sha256(data).hexdigest()==e['sha256']
    with wave.open(str(path),'rb') as w:
        assert (w.getframerate(),w.getsampwidth(),w.getnchannels(),w.getcomptype())==(44100,2,1,'NONE')
        assert w.getnframes()==e['frames'] and w.getnframes()*2<=4194304
        pcm=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').astype(np.int32)
    assert np.max(np.abs(pcm))<32767 and abs(np.mean(pcm))<1
    assert pcm[0]==pcm[-1]==0 and e['rms']>.0001
    assert e['clipped_frames']==0 and abs(e['dc'])<1/32768
    if e['bpm']:assert len(pcm)==round(4*60/e['bpm']*44100) and e['endpoint_delta']==0
    if e['source_type'].startswith(('acoustic','external')):
        assert e['source_sha256'] and e['license_evidence_url'] and e['source_url']
assert m['total_bytes']==sum((pack/e['filename']).stat().st_size for e in m['samples'])
kits=json.loads((pack/'kits.json').read_text())
for kit in kits.values():
    assert len(kit)==16
    budget=sum(next(e['frames']*2 for e in m['samples'] if e['filename']==t['filename']) for t in kit)
    assert budget<16*1024**2
    assert [t['midi_note'] for t in kit]==list(range(36,52))
with tempfile.TemporaryDirectory(dir=root/'.pio') as temp:
    rebuilt=Path(temp)/'rebuilt';build(rebuilt,root/'.pio/m23-source-cache')
    for path in pack.iterdir():assert path.read_bytes()==(rebuilt/path.name).read_bytes(),path
print('M23 42 PCM16 WAVs, RIFF/loops/endpoints/DC/clipping/licenses/source hashes/kit budgets/reproducibility PASS')
