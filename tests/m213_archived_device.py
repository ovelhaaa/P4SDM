"""Capture a saved build matrix without depending on live build directories."""
from pathlib import Path
import json, subprocess, sys
index=json.loads(Path('docs/m21/m213_artifact_index.json').read_text())
try:
    for case in sys.argv[1:]:
        print('CAPTURE '+case,flush=True)
        with Path(f'.pio/m213-{case.upper()}-capture.log').open('w',encoding='utf-8') as log:
            subprocess.run([sys.executable,'tests/m213_archived_capture.py',index[case],
              '650' if 'sustained' in case else '35' if case=='memory' else '175',
              f'docs/GUITION_M213_{case.upper()}_SERIAL.log'],stdout=log,stderr=subprocess.STDOUT,check=True)
finally:
    candidates=list(Path('.pio/m213-artifacts').glob('*/production/manifest.json'))
    if candidates:
        archive=max(candidates,key=lambda p:p.stat().st_mtime).parent
        with Path('.pio/m213-archive-restore.log').open('w') as log:
            subprocess.run([sys.executable,'tests/m213_archived_capture.py',str(archive),'8',
              'docs/GUITION_M213_NORMAL_SERIAL.log'],stdout=log,stderr=subprocess.STDOUT,check=True)
