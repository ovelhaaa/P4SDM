"""Bundle licensed source originals for offline, source-hash-verified rebuilds."""
from pathlib import Path
import hashlib,json,zipfile
root=Path(__file__).resolve().parents[1]
lock=json.loads((root/'tools/m23_sources.json').read_text())
with zipfile.ZipFile(root/'assets/m23_sources.zip','w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    def write(name,data):
        info=zipfile.ZipInfo(name,date_time=(1980,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o644<<16;z.writestr(info,data)
    for s in sorted(lock,key=lambda s:s['cache_name']):
        data=(root/'.pio/m23-source-cache'/s['cache_name']).read_bytes()
        assert hashlib.sha256(data).hexdigest()==s['source_sha256'];write(s['cache_name'],data)
    write('sources.json',json.dumps(lock,indent=2).encode()+b'\n')
    write('LICENSE-UZU.txt',(root/'.pio/m23-uzu/LICENSE').read_bytes())
    write('LICENSES.txt',b'LaSonotheque recordings: CC0-1.0, Joseph Sardin. Individual license URLs in sources.json.\nhttps://creativecommons.org/publicdomain/zero/1.0/\nuzu originals: The Unlicense (full LICENSE-UZU.txt).\n')
print('Offline licensed source bundle:',(root/'assets/m23_sources.zip').stat().st_size,'bytes')
