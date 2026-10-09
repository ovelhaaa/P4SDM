"""Exact experimental rounding and Linux sanitizer coverage."""
from pathlib import Path
import subprocess,sys
out=Path('.pio/m212-host');out.mkdir(parents=True,exist_ok=True)
exe=out/'magnitude.exe'
command=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','tests/linear_magnitude_test.cpp','-o',str(exe)]
subprocess.run(command,check=True);subprocess.run([str(exe)],check=True)
if sys.platform!='win32':
    subprocess.run(command[:2]+['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']+command[3:],check=True)
    subprocess.run([str(exe)],check=True)
