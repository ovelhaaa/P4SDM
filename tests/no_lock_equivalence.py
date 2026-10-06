"""Compile against the actual accepted M9 source, without maintaining a fake oracle."""
import subprocess
import tempfile
from pathlib import Path
with tempfile.TemporaryDirectory() as folder:
    root=Path(folder)
    source=subprocess.check_output(['git','show','250c5cd26d5d99388bbdd1c6a41371caad4a1ef9:src/app/model.h']).decode()
    (root/'m9_model.h').write_text(source.replace('namespace app {','namespace m9 {'))
    binary=root/'equivalence.exe'
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+folder,
                    'tests/no_lock_equivalence.cpp','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
