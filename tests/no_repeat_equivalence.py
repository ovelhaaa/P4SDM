"""Generate actual accepted M18 codec/model dependencies from Git."""
import subprocess
import tempfile
from pathlib import Path
# Shared sampler types are unchanged; reject baseline comparisons if that changes.
for dependency in ('sample_playback.h', 'slices.h'):
    accepted = subprocess.check_output(['git','show','eb24dc1:src/app/'+dependency]).decode()
    assert accepted == (Path('src/app')/dependency).read_text(encoding='utf-8'), dependency
with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    for name in ('model.h', 'project.h', 'sample_playback.h', 'slices.h'):
        source = subprocess.check_output(['git', 'show', 'eb24dc1:src/app/' + name]).decode()
        source = source.replace('namespace app {', 'namespace m18 {').replace('app::', 'm18::').replace('namespace project {', 'namespace legacy {')
        for dependency in ('model.h', 'sample_playback.h', 'slices.h'):
            if dependency == 'model.h': source = source.replace('"model.h"', '"m18_model.h"')
            else: source = source.replace('"'+dependency+'"', '"'+(Path('src/app')/dependency).resolve().as_posix()+'"')
        (root / ('m18_' + name)).write_text(source)
    binary = Path('.pio/m181_equivalence.exe').resolve()
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+folder,'tests/no_repeat_equivalence.cpp','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
