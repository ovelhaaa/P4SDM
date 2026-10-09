"""Archive exact diagnostics before flash; never label rebuilt ELFs as matches."""
from pathlib import Path
import argparse, hashlib, json, shutil, subprocess, datetime
p=argparse.ArgumentParser()
p.add_argument('environment'); p.add_argument('destination',type=Path)
p.add_argument('--pio-home',type=Path,default=Path('C:/.platformio'))
a=p.parse_args()
build=Path('.pio/build')/a.environment
a.destination.mkdir(parents=True,exist_ok=False)
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
files={}
for name in ('firmware.elf','firmware.bin','firmware.map','bootloader.bin','partitions.bin'):
    file=build/name
    if not file.is_file(): raise SystemExit(f'Missing exact build artifact: {file}')
    shutil.copy2(file,a.destination/name); files[name]=sha(file)
sources={str(f).replace('\\','/'):sha(f) for f in Path('src').rglob('*') if f.is_file()}
for file in [Path('platformio.ini'),Path('boards/jc4880p4.json'),*Path('.').glob('*.ino'),*Path('.').glob('*.h')]:
    sources[file.as_posix()]=sha(file)
packages={}
for name in ('framework-arduinoespressif32','framework-arduinoespressif32-libs','toolchain-riscv32-esp'):
    file=a.pio_home/'packages'/name/'package.json'
    packages[name]=json.loads(file.read_text())
sdk=a.pio_home/'packages/framework-arduinoespressif32-libs/esp32p4_es/qio_qspi/include/sdkconfig.h'
shutil.copy2(sdk,a.destination/'sdkconfig.h')
shutil.copy2('platformio.ini',a.destination/'platformio.ini')
manifest=dict(environment=a.environment,utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
    baseline_sha=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
    dirty=subprocess.check_output(['git','status','--porcelain'],text=True),
    files=files,sources=sources,sdk_sha256=sha(sdk),packages=packages)
(a.destination/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(a.destination)
