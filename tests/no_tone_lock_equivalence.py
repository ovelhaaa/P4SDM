"""Compile against the actual accepted M11 source, without maintaining a fake oracle."""
import subprocess
import tempfile
from pathlib import Path
with tempfile.TemporaryDirectory() as folder:
    root=Path(folder)
    source=subprocess.check_output(['git','show','4afaa51cec574594b35d1f8bfee7938cced6de31:src/app/model.h']).decode()
    (root/'m11_model.h').write_text(source.replace('namespace app {','namespace m11 {'))
    binary=Path('.pio/m12_no_tone_lock_equivalence.exe').resolve()
    binary.parent.mkdir(exist_ok=True)
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+folder,
                    'tests/no_tone_lock_equivalence.cpp','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
