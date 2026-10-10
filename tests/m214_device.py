"""Build/archive exact M21.4 firmware, then capture on the dedicated COM13 board.

Each run has a unique directory. Never overwrite historical captures. Always
restore the preserved pre-M21.4 production image, even after a failed flash.
"""
from pathlib import Path
import argparse
import datetime
import json
import subprocess
import sys

p = argparse.ArgumentParser()
p.add_argument('operation', choices=['build', 'capture'])
p.add_argument('cases', nargs='+')
a = p.parse_args()
index_path = Path('docs/m21/m214_artifacts.json')
index = json.loads(index_path.read_text()) if index_path.exists() else {}
stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
root = Path('.pio/m214-artifacts') / stamp
root.mkdir(parents=True, exist_ok=False)
pio_python = r'C:\.platformio\penv\Scripts\python.exe'
environments = {'candidate': 'guition_app_linear_candidate',
                'fallback': 'guition_app_nearest_legacy'}
try:
    for case in a.cases:
        # Read the latest complete index for each case. Run builds to completion
        # before capture: two writers to one index are not supported.
        index = json.loads(index_path.read_text()) if index_path.exists() else {}
        environment = environments.get(case, 'guition_m214_' + case)
        print(a.operation.upper(), case, flush=True)
        if a.operation == 'build':
            with (root / (case + '-build.log')).open('w') as log:
                subprocess.run([pio_python, '-m', 'platformio', 'run', '-e', environment],
                               stdout=log, stderr=subprocess.STDOUT, check=True)
            archive = root / case
            subprocess.run([sys.executable, 'tests/archive_firmware.py', environment,
                            str(archive)], check=True)
            index[case] = {'archive': archive.as_posix(),
                           'manifest': json.loads((archive / 'manifest.json').read_text())}
            index_path.write_text(json.dumps(index, indent=2) + '\n')
        else:
            output = Path('docs/m21/m214-captures') / stamp / (case + '.log')
            output.parent.mkdir(parents=True, exist_ok=True)
            with (root / (case + '-capture.log')).open('w', encoding='utf-8') as log:
                subprocess.run([sys.executable, 'tests/m213_archived_capture.py',
                                index[case]['archive'],
                                '650' if 'sustained' in case else '175', str(output)],
                               stdout=log, stderr=subprocess.STDOUT, check=True)
            index = json.loads(index_path.read_text())
            index[case]['capture'] = output.as_posix()
            index_path.write_text(json.dumps(index, indent=2) + '\n')
finally:
    if a.operation == 'capture':
        output = Path('docs/m21/m214-captures') / stamp / 'restored.log'
        output.parent.mkdir(parents=True, exist_ok=True)
        with (root / 'restore.log').open('w', encoding='utf-8') as log:
            subprocess.run([sys.executable, 'tests/m213_archived_capture.py',
                            '.pio/m214-artifacts/baseline/production', '8', str(output)],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
