"""Physical follow-ups using matching, verified saved SD firmware images."""
from pathlib import Path
import subprocess, sys

def capture(archive, name):
    with Path(f'.pio/m213-{name}-flash.log').open('w') as log:
        subprocess.run([sys.executable, 'tests/m213_archived_capture.py', archive,
                        '8', f'docs/GUITION_M213_SD_{name}_BOOT_SERIAL.log'],
                       stdout=log, stderr=subprocess.STDOUT, check=True)

try:
    if '--direct-only' not in sys.argv:
        capture('.pio/m213-artifacts/SD_0_repeat_exact', '0_REPEAT')
        with Path('.pio/m213-SD_0_REPEAT-host.log').open('w', encoding='utf-8') as log:
            subprocess.run([sys.executable, 'tests/m213_sd_repeat.py', '0_REPEAT'],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
    capture('.pio/m213-artifacts/SD_direct_1_paced_exact', 'direct_1')
    with Path('.pio/m213-SD_direct_1-host.log').open('w', encoding='utf-8') as log:
        subprocess.run([sys.executable, 'tests/m213_sd.py', 'direct_1'],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    capture('.pio/m213-artifacts/SD_direct_1_paced_exact', 'direct_1_REPEAT')
    with Path('.pio/m213-SD_direct_1_REPEAT-host.log').open('w', encoding='utf-8') as log:
        subprocess.run([sys.executable, 'tests/m213_sd_repeat.py', 'direct_1_REPEAT'],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
finally:
    candidates=list(Path('.pio/m213-artifacts').glob('*/production/manifest.json'))
    if candidates:
        archive=max(candidates, key=lambda p:p.stat().st_mtime).parent
        with Path('.pio/m213-sd-followup-restore.log').open('w') as log:
            subprocess.run([sys.executable, 'tests/m213_archived_capture.py',
                            str(archive), '8', 'docs/GUITION_M213_NORMAL_SERIAL.log'],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
