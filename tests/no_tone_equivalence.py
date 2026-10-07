"""Compile against the actual accepted M10 source, without maintaining a fake oracle."""
import subprocess
import tempfile
from pathlib import Path
with tempfile.TemporaryDirectory() as folder:
    root=Path(folder)
    source=subprocess.check_output(['git','show','232c35c36428ef19de6910318ea98cddf54ad052:src/app/model.h']).decode()
    (root/'m10_model.h').write_text(source.replace('namespace app {','namespace m10 {'))
    binary=Path('.pio/m11_no_tone_equivalence.exe').resolve()
    binary.parent.mkdir(exist_ok=True)
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+folder,
                    'tests/no_tone_equivalence.cpp','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
