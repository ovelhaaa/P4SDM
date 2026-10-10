"""Capture the original A journal and restore the preserved production image."""
from pathlib import Path
import datetime
import json
import subprocess
import sys

stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
root=Path('docs/m21/m214-captures')/stamp;root.mkdir(parents=True,exist_ok=False)
archive=sys.argv[1] if len(sys.argv)>1 else '.pio/m214-artifacts/A_journal_linear'
capture=root/'A_journal.log'
try:
    with Path('.pio/m214-journal-capture.log').open('w',encoding='utf-8') as log:
        subprocess.run([sys.executable,'tests/m214_journal_capture.py',archive,str(capture)],
                       stdout=log,stderr=subprocess.STDOUT,check=True)
    p=Path('docs/m21/m214_artifacts.json');index=json.loads(p.read_text())
    index['A_journal']={'archive':archive,'manifest':json.loads((Path(archive)/'manifest.json').read_text()),
                        'capture':capture.as_posix()}
    p.write_text(json.dumps(index,indent=2)+'\n')
    subprocess.run([sys.executable,'tests/m214_journal_checks.py',str(capture)],check=True)
finally:
    with Path('.pio/m214-journal-restore.log').open('w',encoding='utf-8') as log:
        subprocess.run([sys.executable,'tests/m213_archived_capture.py',
                        '.pio/m214-artifacts/baseline/production','8',str(root/'restored.log')],
                       stdout=log,stderr=subprocess.STDOUT,check=True)
