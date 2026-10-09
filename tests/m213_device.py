"""Capture compiled M213 cases sequentially; archive before every flash.

Build environments first. Even failed/incomplete captures are retained.
Always restore the previous production configuration on exit.
"""
from pathlib import Path
import argparse, datetime, json, subprocess, sys
p=argparse.ArgumentParser();p.add_argument('cases',nargs='+');a=p.parse_args()
python=r'C:\.platformio\penv\Scripts\python.exe'
root=Path(__file__).resolve().parents[1]
stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
archive_root=root/'.pio/m213-artifacts'/stamp
results=[]
try:
    for case in a.cases:
        env='guition_m213_'+case
        label=case.upper()
        print('CAPTURE '+label,flush=True)
        # PlatformIO may remove unselected environment directories when the
        # configuration changes. Build/archive/flash are one serial operation.
        with (root/f'.pio/m213-{label}-build.log').open('w',encoding='utf-8') as log:
            subprocess.run([python,'-m','platformio','run','-e',env],stdout=log,stderr=subprocess.STDOUT,check=True)
        archive=archive_root/label
        subprocess.run([sys.executable,'tests/archive_firmware.py',env,str(archive)],check=True)
        with (root/f'.pio/m213-{label}-capture.log').open('w',encoding='utf-8') as log:
            r=subprocess.run([sys.executable,'tests/capture_interpolation.py',env,'650' if 'sustained' in case else '175',f'docs/GUITION_M213_{label}_SERIAL.log'],stdout=log,stderr=subprocess.STDOUT)
        manifest=json.loads((archive/'manifest.json').read_text())
        results.append(dict(case=label,environment=env,archive=str(archive),manifest=manifest,capture_exit=r.returncode))
        (root/f'docs/m21/m213_builds_{stamp}.json').write_text(json.dumps(results,indent=2)+'\n')
        if r.returncode: raise SystemExit(f'Flash/capture failed: {label}')
finally:
    print('RESTORE production Nearest/cache-off',flush=True)
    with (root/'.pio/m213-production-build.log').open('w') as log:
        subprocess.run([python,'-m','platformio','run','-e','guition_app'],stdout=log,stderr=subprocess.STDOUT,check=True)
    subprocess.run([sys.executable,'tests/archive_firmware.py','guition_app',str(archive_root/'production')],check=True)
    with (root/'.pio/m213-production-restore.log').open('w') as log:
        subprocess.run([sys.executable,'tests/physical_sd/capture.py','guition_app','8','docs/GUITION_M213_NORMAL_SERIAL.log'],stdout=log,stderr=subprocess.STDOUT,check=True)
