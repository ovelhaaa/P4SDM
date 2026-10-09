"""Record immutable local firmware manifests and raw capture hashes."""
from pathlib import Path
import hashlib,json

archives=[]
for path in sorted(Path('.pio/m213-artifacts').rglob('manifest.json')):
    manifest=json.loads(path.read_text())
    for name,expected in manifest['files'].items():
        assert hashlib.sha256((path.parent/name).read_bytes()).hexdigest()==expected,str(path.parent/name)
    archives.append(dict(archive=path.parent.as_posix(),manifest=manifest))
assert archives,'No saved firmware evidence'
captures={path.as_posix():hashlib.sha256(path.read_bytes()).hexdigest()
          for path in sorted(Path('docs').glob('GUITION_M213*.log'))}
scripts={path.as_posix():hashlib.sha256(path.read_bytes()).hexdigest()
         for path in sorted(Path('tests').glob('*m213*.py'))}
Path('docs/m21/m213_firmware_inventory.json').write_text(json.dumps(
    dict(archives=archives,captures=captures,host_scripts=scripts),indent=2)+'\n')
print(f'M213 exact saved artifacts verified: {len(archives)} archives, {len(captures)} raw logs')
