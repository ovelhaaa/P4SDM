"""Generate actual accepted M17 codec/model dependencies from Git."""
import subprocess
import tempfile
from pathlib import Path
# Shared sampler types are unchanged; reject baseline comparisons if that changes.
for dependency in ('sample_playback.h', 'slices.h'):
    accepted = subprocess.check_output(['git','show','589e718:src/app/'+dependency]).decode()
    assert accepted == (Path('src/app')/dependency).read_text(encoding='utf-8'), dependency
with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    for name in ('model.h', 'project.h', 'sample_playback.h', 'slices.h'):
        source = subprocess.check_output(['git', 'show', '589e718:src/app/' + name]).decode()
        source = source.replace('namespace app {', 'namespace m17 {').replace('app::', 'm17::').replace('namespace project {', 'namespace legacy {')
        for dependency in ('model.h', 'sample_playback.h', 'slices.h'):
            if dependency == 'model.h': source = source.replace('"model.h"', '"m17_model.h"')
            else: source = source.replace('"'+dependency+'"', '"'+(Path('src/app')/dependency).resolve().as_posix()+'"')
        (root / ('m17_' + name)).write_text(source)
    binary = Path('.pio/m18_equivalence.exe').resolve()
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+folder,'tests/no_performance_equivalence.cpp','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
