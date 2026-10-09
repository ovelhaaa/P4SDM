from pathlib import Path
import subprocess, sys
out=Path('.pio/m213-host'); out.mkdir(parents=True,exist_ok=True)
exe=out/'layout.exe'
command=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','tests/m213_layout_test.cpp','-o',str(exe)]
subprocess.run(command,check=True);subprocess.run([str(exe)],check=True)
if sys.platform!='win32':
    subprocess.run(command[:2]+['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']+command[3:],check=True)
    subprocess.run([str(exe)],check=True)
