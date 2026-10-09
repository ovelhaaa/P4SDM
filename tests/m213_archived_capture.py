"""Flash only hash-verified saved artifacts and stop at a complete heap row."""
from pathlib import Path
import hashlib, json, re, sys
archive=Path(sys.argv[1]).resolve()
manifest=json.loads((archive/'manifest.json').read_text())
for name, expected in manifest['files'].items():
    assert hashlib.sha256((archive/name).read_bytes()).hexdigest()==expected,name+' differs from archive'
environment=manifest['environment']
seconds,output=sys.argv[2:4]
original=Path('tests/physical_sd/capture.py').resolve()
source=original.read_text().replace("build = root / '.pio/build' / name",f"build = Path({archive.as_posix()!r})")
source=source.replace('    while time.monotonic() < stop:', '    tail = b""\n    while time.monotonic() < stop:')
source=source.replace("            print(data.decode('utf-8','replace'), end='', flush=True)",
    "            print(data.decode('utf-8','replace'), end='', flush=True)\n"
    "            tail = (tail + data)[-4096:]\n"
    "            if re.search(rb'\\[M5 memory\\][^\\r\\n]*largest_after=\\d+\\r?\\n', tail) or "
    "re.search(rb'\\[M213 memory\\] COMPLETE[^\\r\\n]*display_started=0\\r?\\n', tail): break")
sys.argv=[str(original),environment,seconds,output]
exec(compile(source,str(original),'exec'),dict(__file__=str(original),__name__='__main__',re=re))
