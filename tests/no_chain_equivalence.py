"""Compare with actual accepted M16, including its slice dependency."""
import subprocess
import tempfile
from pathlib import Path
with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    source = subprocess.check_output(['git','show','dbc9505:src/app/model.h']).decode()
    for name in ('sample_playback.h','slices.h'):
        source = source.replace('"'+name+'"', '"'+(Path('src/app')/name).resolve().as_posix()+'"')
    (root/'m16_model.h').write_text(source.replace('namespace app {','namespace m16 {'))
    binary = Path('.pio/m17_equivalence.exe').resolve()
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+folder,'tests/no_chain_equivalence.cpp','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
