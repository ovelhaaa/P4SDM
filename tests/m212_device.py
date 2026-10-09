"""Sequential physical capture, exact artifact archive, restore production.

Run only against the dedicated connected qualification device. This runs the
existing inherited A script (which includes its project fixture operations).
It does not replace musical listening or physical SD regression.
"""
from pathlib import Path
import subprocess,sys,json
root=Path(__file__).resolve().parents[1]
python=r'C:\.platformio\penv\Scripts\python.exe'
cases=(('B_NEAREST','guition_m212_B_nearest'),('B_LINEAR32','guition_m212_B_linear32'),
       ('C_NEAREST','guition_m212_C_nearest'),('C_LINEAR32','guition_m212_C_linear32'),
       ('A_NEAREST','guition_pcm_cache_nearest_mixed'),('A_LINEAR32','guition_m212_A_linear32'))
results=[]
try:
    for label,env in cases:
        print(f'CAPTURE {label}',flush=True)
        archive=Path('.pio/m212-artifacts')/label
        subprocess.run([sys.executable,'tests/archive_firmware.py',env,str(archive)],check=True)
        capture=root/f'docs/GUITION_M212_{label}_SERIAL.log'
        with (root/f'.pio/m212-{label}-capture.log').open('w',encoding='utf-8') as log:
            r=subprocess.run([python,'tests/capture_interpolation.py',env,'165',str(capture)],stdout=log,stderr=subprocess.STDOUT)
        manifest=json.loads((archive/'manifest.json').read_text())
        results.append(dict(case=label,environment=env,files=manifest['files'],
                            baseline_sha=manifest['baseline_sha'],sdk_sha256=manifest['sdk_sha256'],
                            archive=str(archive),capture_exit=r.returncode))
        (root/'docs/m21/m212_builds.json').write_text(json.dumps(results,indent=2)+'\n')
        if r.returncode: raise SystemExit(f'Capture failed: {label}')
finally:
    print('RESTORE production Nearest/cache-off',flush=True)
    with (root/'.pio/m212-production-build.log').open('w',encoding='utf-8') as log:
        subprocess.run([python,'-m','platformio','run','-e','guition_app'],stdout=log,stderr=subprocess.STDOUT,check=True)
    subprocess.run([sys.executable,'tests/archive_firmware.py','guition_app','.pio/m212-artifacts/production'],check=True)
    with (root/'.pio/m212-production-restore.log').open('w',encoding='utf-8') as log:
        subprocess.run([python,'tests/physical_sd/capture.py','guition_app','8','docs/GUITION_M212_NORMAL_SERIAL.log'],stdout=log,stderr=subprocess.STDOUT,check=True)
