"""Build then archive each case before PlatformIO cleans another environment."""
from pathlib import Path
import datetime, json, subprocess, sys
root=Path(__file__).resolve().parents[1]
python=r'C:\.platformio\penv\Scripts\python.exe'
stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
archives=root/'.pio/m213-artifacts'/('build-'+stamp)
index_path=root/'docs/m21/m213_artifact_index.json'
index=json.loads(index_path.read_text()) if index_path.exists() else {}
for case in sys.argv[1:]:
    env='guition_m213_'+case
    print('BUILD '+case,flush=True)
    with (root/f'.pio/m213-{case}-build.log').open('w') as log:
        subprocess.run([python,'-m','platformio','run','-e',env],stdout=log,stderr=subprocess.STDOUT,check=True)
    archive=archives/case
    subprocess.run([sys.executable,'tests/archive_firmware.py',env,str(archive)],check=True)
    index[case]=str(archive)
    (root/'docs/m21/m213_artifact_index.json').write_text(json.dumps(index,indent=2)+'\n')
    (root/'docs/m21/m213_exact_builds.json').write_text(json.dumps(
        {k:json.loads((Path(v)/'manifest.json').read_text()) for k,v in index.items()},indent=2)+'\n')
