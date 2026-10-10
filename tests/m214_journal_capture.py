"""Capture a hash-verified journal build through its explicit END marker."""
from pathlib import Path
import hashlib
import json
import re
import sys

archive=Path(sys.argv[1]).resolve()
manifest=json.loads((archive/'manifest.json').read_text())
for name,digest in manifest['files'].items():
    assert hashlib.sha256((archive/name).read_bytes()).hexdigest()==digest
original=Path('tests/physical_sd/capture.py').resolve()
source=original.read_text().replace("build = root / '.pio/build' / name",f"build = Path({archive.as_posix()!r})")
source=source.replace('    while time.monotonic() < stop:', '    tail=b""\n    while time.monotonic() < stop:')
source=source.replace("            print(data.decode('utf-8','replace'), end='', flush=True)",
    "            print(data.decode('utf-8','replace'), end='', flush=True)\n"
    "            tail=(tail+data)[-4096:]\n"
    "            if re.search(rb'\\[M214 journal\\] END records=\\d+\\r?\\n',tail): break")
sys.argv=[str(original),manifest['environment'],'500',sys.argv[2]]
exec(compile(source,str(original),'exec'),dict(__file__=str(original),__name__='__main__',re=re))
