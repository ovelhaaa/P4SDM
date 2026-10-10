"""Prepare inherited storage/audio diagnostic console with production M21.4 DSP.

The production loader, waveform/analyzer, queues and audio render are unchanged.
Only the serial observation/control console is present in the ignored copy.
"""
from pathlib import Path
import shutil,subprocess,sys
subprocess.run([sys.executable,'tests/m214_sd_prepare.py'],check=True)
target=Path('.pio/m23-loader')
shutil.copytree('.pio/m214-sd',target,dirs_exist_ok=True,ignore=shutil.ignore_patterns('.pio'))
console=target/'src/qconsole.inc'
s=console.read_text().replace('for(unsigned i=0;i<4;i++)','for(unsigned i=0;i<16;i++)')
console.write_text(s)
with (target/'platformio.ini').open('a') as f:
    f.write('''
[env:guition_m23_loader]
extends = env:guition_app_base
build_flags = ${env:guition_app_base.build_flags} -DP4SDM_M213_DIAGNOSTICS=1 -DP4SDM_INTERPOLATION=1 -DP4SDM_LINEAR_32BIT=1 -DP4SDM_LINEAR_MAGNITUDE=1 -DP4SDM_PCM_READ_CACHE=0 -DP4SDM_PCM_PLACEMENT=1
''')
print('M23 ignored loader observation console prepared; no hardware write')
