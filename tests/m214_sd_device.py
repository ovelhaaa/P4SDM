"""Sequential fresh SD captures, with matching archives and final restoration."""
from pathlib import Path
import datetime
import json
import subprocess
import sys

stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
logs=Path('docs/m21/m214-captures')/stamp
logs.mkdir(parents=True,exist_ok=False)
index_path=Path('docs/m21/m214_artifacts.json')
try:
    for policy in sys.argv[1:]:
        key='SD_'+policy
        index=json.loads(index_path.read_text())
        print('SD CAPTURE',policy,flush=True)
        with Path(f'.pio/m214-{stamp}-{key}-flash.log').open('w',encoding='utf-8') as log:
            subprocess.run([sys.executable,'tests/m213_archived_capture.py',
                            index[key]['archive'],'8',str(logs/(key+'_BOOT.log'))],
                           stdout=log,stderr=subprocess.STDOUT,check=True)
        # Existing host console deliberately writes append-only diagnostic logs.
        # A unique label keeps every incomplete attempt available for inspection.
        label=stamp+'_'+policy
        with Path(f'.pio/m214-{label}-host.log').open('w',encoding='utf-8') as log:
            subprocess.run([sys.executable,'tests/m214_sd.py',label],
                           stdout=log,stderr=subprocess.STDOUT,check=True)
        source=Path('docs')/('GUITION_M214_SD_'+label.upper()+'_SERIAL.log')
        output=logs/(key+'.log')
        source.rename(output)
        index=json.loads(index_path.read_text())
        index[key]['capture']=output.as_posix()
        index_path.write_text(json.dumps(index,indent=2)+'\n')
finally:
    with Path(f'.pio/m214-{stamp}-sd-restore.log').open('w',encoding='utf-8') as log:
        subprocess.run([sys.executable,'tests/m213_archived_capture.py',
                        '.pio/m214-artifacts/baseline/production','8',str(logs/'restored.log')],
                       stdout=log,stderr=subprocess.STDOUT,check=True)
