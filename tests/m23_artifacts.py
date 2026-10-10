"""Record immutable build hashes and compare disabled production to baseline."""
from pathlib import Path
import hashlib,json,re,subprocess
root=Path(__file__).resolve().parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
result=dict(source_sha=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),sources={p.relative_to(root).as_posix():sha(p.read_bytes()) for p in (root/'src').rglob('*') if p.is_file()},platformio_sha256=sha((root/'platformio.ini').read_bytes()),images={})
result['source_sha_role']='Parent commit at evidence generation; exact compiled working-tree bytes are identified by sources/platformio_sha256'
result['source_working_tree_modified']=bool(subprocess.check_output(['git','diff','--name-only','HEAD','--','src','platformio.ini'],text=True).strip())
for folder in (root/'.pio/m23-release').iterdir():
    if folder.is_dir():result['images'][folder.name]={p.name:sha(p.read_bytes()) for p in folder.iterdir() if p.name in ['firmware.bin','firmware.elf','firmware.map','bootloader.bin','partitions.bin']}
baseline=(root/'.pio/m23-baseline/guition_app.bin').read_bytes()
production=(root/'.pio/m23-release/guition_app/firmware.bin').read_bytes()
assert len(baseline)==len(production)
differences=[i for i,(a,b) in enumerate(zip(baseline,production)) if a!=b]
time=re.search(rb'Software Info:\n\0(\d\d:\d\d:\d\d)\0',baseline)
assert time
allowed=[(176,208),(time.start(1),time.end(1)),(len(baseline)-33,len(baseline))]
assert all(any(start<=i<end for start,end in allowed) for i in differences)
result['disabled_production_baseline']=dict(baseline_sha256=sha(baseline),production_sha256=sha(production),bytes=len(baseline),changed_bytes=len(differences),all_other_bytes_identical=True,excluded_fields='ESP app ELF SHA, Software Info build time, image checksum/trailing SHA256',excluded_ranges=allowed)
hardware=root/'.pio/m23-hardware'
result['schematic_commit']=subprocess.check_output(['git','-C',str(hardware),'rev-parse','HEAD'],text=True).strip()
result['schematics']={p.name:sha(p.read_bytes()) for p in (hardware/'schematic').glob('*.webp')}
(root/'docs/m23/artifacts.json').write_text(json.dumps(result,indent=2)+'\n')
print('M23 archived hashes; production identical outside build metadata/integrity fields PASS')
