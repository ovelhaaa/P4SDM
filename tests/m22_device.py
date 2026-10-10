"""Archive exact final images, strictly qualify both loads, restore normal app."""
from pathlib import Path
import datetime
import json
import shutil
import subprocess
import sys
root=Path('docs/m22');root.mkdir(exist_ok=True)
stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
base=Path('.pio/m22-artifacts')/stamp
environments={'production':'guition_app','nearest':'guition_app_nearest_legacy','sample':'guition_m22_sample','synth':'guition_m22_synth'}
for case,environment in environments.items():
    subprocess.run([sys.executable,'tests/archive_firmware.py',environment,str(base/case)],check=True)
for case in ('SAMPLE','SYNTH'):
    for name in (case+'_SERIAL.log',case.lower()+'_results.json'):
        path=root/name
        if path.exists(): shutil.move(path,root/('INITIAL_'+name))
try:
    for case in ('sample','synth'):
        capture=root/(case.upper()+'_SERIAL.log')
        subprocess.run([sys.executable,'tests/m213_archived_capture.py',str(base/case),'120',str(capture)],check=True)
        manifest=json.loads((base/case/'manifest.json').read_text())
        capture.write_bytes(f"# M22 archive={base/case} source_sha={manifest['baseline_sha']} UI actions=scripted; no finger observation.\n".encode()+capture.read_bytes())
        subprocess.run([sys.executable,'tests/analyze_m22.py',str(capture),'--output',str(root/(case+'_results.json'))]+(['--synth'] if case=='synth' else []),check=True)
finally:
    subprocess.run([sys.executable,'tests/m213_archived_capture.py',str(base/'production'),'8',str(root/'PRODUCTION_BOOT_SERIAL.log')],check=True)
    manifests={p.parent.relative_to(Path('.pio/m22-artifacts')).as_posix():json.loads(p.read_text()) for p in Path('.pio/m22-artifacts').rglob('manifest.json')}
    (root/'artifacts.json').write_text(json.dumps(manifests,indent=2)+'\n')
print('Final exact-source M22 scripted device qualification passed; normal application restored; HUMAN PHYSICAL UX ACCEPTANCE PENDING')
