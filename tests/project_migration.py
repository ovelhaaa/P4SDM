"""Generate actual accepted M16 codec/model dependencies from Git."""
import subprocess
import tempfile
from pathlib import Path
with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    for name in ('model.h', 'project.h', 'sample_playback.h', 'slices.h'):
        source = subprocess.check_output(['git', 'show', 'dbc9505:src/app/' + name]).decode()
        source = source.replace('namespace app {', 'namespace m16 {').replace('app::', 'm16::').replace('namespace project {', 'namespace legacy {')
        for dependency in ('model.h', 'sample_playback.h', 'slices.h'):
            if dependency == 'model.h': source = source.replace('"model.h"', '"m16_model.h"')
            else: source = source.replace('"'+dependency+'"', '"'+(Path('src/app')/dependency).resolve().as_posix()+'"')
        (root / ('m16_' + name)).write_text(source)
    binary = Path('.pio/migration.exe').resolve()
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+folder,'tests/project_migration.cpp','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
