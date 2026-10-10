"""Final direct Linear configuration vs Nearest and independent Linear64."""
from pathlib import Path
import subprocess
import os
import sys

out=Path('.pio/m214-host');out.mkdir(parents=True,exist_ok=True)
reference=None
for mode,magnitude in [(0,0),(1,0),(1,1)]:
    flags=[f'-DP4SDM_INTERPOLATION={mode}',f'-DP4SDM_LINEAR_32BIT={magnitude}',
           f'-DP4SDM_LINEAR_MAGNITUDE={magnitude}','-DP4SDM_PCM_READ_CACHE=0',
           '-DP4SDM_PCM_PLACEMENT=1']
    exe=out/'replay.exe'
    command=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',*flags,
             'tests/m214_replay.cpp','-o',str(exe)]
    subprocess.run(command,check=True)
    trace=subprocess.check_output([str(exe)],text=True)
    print(mode,magnitude,trace,flush=True)
    if reference is None: reference=trace
    else: assert reference==trace,'Canonical musical state differs'
    if sys.platform!='win32':
        subprocess.run(command[:2]+['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']+command[3:],check=True)
        assert subprocess.check_output([str(exe)],text=True,
                   env={**os.environ,'UBSAN_OPTIONS':'halt_on_error=1'})==reference
print('M214 A-family supplemental replay PASS; full original A replay remains PENDING')
