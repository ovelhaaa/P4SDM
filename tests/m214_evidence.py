"""Verify immutable local matching firmware and package the physical evidence."""
from pathlib import Path
import hashlib
import json
import zipfile

index=json.loads(Path('docs/m21/m214_artifacts.json').read_text())
inventory=[]
with zipfile.ZipFile('.pio/m214-release-evidence.zip','w',zipfile.ZIP_DEFLATED) as z:
    for case,entry in index.items():
        archive=Path(entry['archive']); manifest=entry['manifest']
        assert json.loads((archive/'manifest.json').read_text())==manifest
        for name,digest in manifest['files'].items():
            file=archive/name
            assert hashlib.sha256(file.read_bytes()).hexdigest()==digest,file
            z.write(file,case+'/'+name)
        for name in ('manifest.json','sdkconfig.h','platformio.ini'):
            z.write(archive/name,case+'/'+name)
        assert hashlib.sha256((archive/'sdkconfig.h').read_bytes()).hexdigest()==manifest['sdk_sha256']
        capture=entry.get('capture')
        if capture:
            file=Path(capture); z.write(file,'captures/'+case+'.log')
            inventory.append(dict(case=case,archive=entry['archive'],capture=capture,
                                  capture_sha256=hashlib.sha256(file.read_bytes()).hexdigest(),
                                  firmware_sha256=manifest['files']['firmware.bin']))
Path('docs/m21/m214_inventory.json').write_text(json.dumps(inventory,indent=2)+'\n')
print('Exact matching local ELF/BIN/MAP/bootloader/partitions verified and packaged')
