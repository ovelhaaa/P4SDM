"""Capture a saved build matrix without depending on live build directories."""
from pathlib import Path
import json, subprocess, sys
index=json.loads(Path('docs/m21/m213_artifact_index.json').read_text())
for case in sys.argv[1:]:
    print('CAPTURE '+case,flush=True)
    with Path(f'.pio/m213-{case.upper()}-capture.log').open('w',encoding='utf-8') as log:
        subprocess.run([sys.executable,'tests/m213_archived_capture.py',index[case],
          '650' if 'sustained' in case else '35' if case=='memory' else '175',
          f'docs/GUITION_M213_{case.upper()}_SERIAL.log'],stdout=log,stderr=subprocess.STDOUT,check=True)
