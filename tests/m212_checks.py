"""Configuration correctness matrix; device qualification remains separate."""
from pathlib import Path
import subprocess,sys
out=Path('.pio/m212-host');out.mkdir(parents=True,exist_ok=True)
for size in (16,32,64,128):
    for fill in range(3):
        flags=[f'-DP4SDM_PCM_CACHE_FRAMES={size}',f'-DP4SDM_PCM_CACHE_FILL={fill}','-DP4SDM_PCM_CACHE_METRICS=1','-DP4SDM_PCM_CACHE_VALIDATED=1']
        exe=out/'cache.exe'
        command=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',*flags,'tests/pcm_cache_matrix.cpp','-o',str(exe)]
        subprocess.run(command,check=True);subprocess.run([str(exe)],check=True)
        if sys.platform!='win32':
            subprocess.run(command[:2]+['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']+command[3:],check=True)
            subprocess.run([str(exe)],check=True)
