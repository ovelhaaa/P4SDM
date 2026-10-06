"""Compare eight original FX output streams with the M3.1 implementation."""
from pathlib import Path
import os
import subprocess

root=Path(__file__).resolve().parents[1]
out=root/'.pio'
out.mkdir(exist_ok=True)
baseline=subprocess.check_output(['git','show','fed1b614:fx.h'],cwd=root)
(out/'fx_original.h').write_bytes(baseline)
suffix='.exe' if os.name=='nt' else ''
common=['g++','-std=c++17','-Wall','-Wextra','-Werror','-Itests/fx_stubs','tests/fx_safety_test.cpp']
results=[]
for name,flags in [('original',['-DFX_BASELINE','-Wno-misleading-indentation']),('current',[])]:
    binary=out/(f'fx_{name}_comparison'+suffix)
    subprocess.run(common+flags+['-o',str(binary)],cwd=root,check=True)
    results.append(subprocess.check_output([str(binary)],cwd=root))
assert results[0]==results[1], 'Original/current FX output digests differ'
print(results[1].decode(),end='')
print('All eight FX match original output across 132352 deterministic stereo frames each.')
