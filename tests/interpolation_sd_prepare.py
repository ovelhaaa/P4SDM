"""Reproduce the ignored M21 SD console builds without production UI changes."""
from pathlib import Path
import subprocess,sys
subprocess.run([sys.executable,'tests/physical_sd/prepare.py'],check=True)
path=Path('.pio/m19p/platformio.ini')
with path.open('a',encoding='utf-8') as f:
    for name,mode in [('linear',1),('hermite',2)]:
        f.write(f'\n[env:guition_interp_sd_{name}]\nextends = env:guition_app\n'
                f'build_flags = ${{env:guition_app.build_flags}} -DP4SDM_INTERPOLATION={mode}\n')
print('Prepared SD console with stable Linear=1 / Hermite4=2 flags; no card writes or flash')
