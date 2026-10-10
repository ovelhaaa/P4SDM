"""Deterministic accepted Engine/Voice trace equivalence, not full DSP timing."""
from pathlib import Path
import subprocess
out=Path('.pio/m212-host');out.mkdir(parents=True,exist_ok=True)
reference=None
for mode,cache,magnitude,validated,full_gain in ((0,0,0,0,0),(0,1,0,0,0),(1,0,0,0,0),(1,0,1,0,0),(1,1,0,0,0),(1,1,1,0,0),(1,1,0,1,0),(1,1,0,0,1)):
    exe=out/'trace.exe'
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',
        f'-DP4SDM_INTERPOLATION={mode}',f'-DP4SDM_PCM_READ_CACHE={cache}',
        '-DP4SDM_LINEAR_32BIT=1',f'-DP4SDM_LINEAR_MAGNITUDE={magnitude}',f'-DP4SDM_PCM_CACHE_VALIDATED={validated}',f'-DP4SDM_FULL_GAIN_FASTPATH={full_gain}','tests/m212_trace.cpp','-o',str(exe)],check=True)
    trace=subprocess.check_output([str(exe)],text=True)
    print(f'mode={mode} cache={cache} magnitude={magnitude} validated={validated} full_gain={full_gain}\n{trace}',flush=True)
    if reference is None: reference=trace
    else: assert trace==reference,'Musical/control/transport trace differs'
print('M212 B/C deterministic complete Chain/Repeat/voice trace PASS')
