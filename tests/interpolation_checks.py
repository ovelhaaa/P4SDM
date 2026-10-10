"""Use untouched accepted Git headers as the independent playback control."""
from pathlib import Path
import sys
import subprocess
import tempfile
baseline = '4ac1015d0254a84c5e508317f2bea077257e0fe2'
flags = (['-DP4SDM_INTERPOLATION=1','-DP4SDM_LINEAR_32BIT=1',
          '-DP4SDM_LINEAR_MAGNITUDE=1','-DP4SDM_PCM_READ_CACHE=0']
         if '--linear-candidate' in sys.argv else [])
with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    (root/'accepted').mkdir()
    for name in ('wav.h','waveform.h','sample_playback.h'):
        source = subprocess.check_output(['git','show',f'{baseline}:src/app/{name}']).decode()
        (root/'accepted'/name).write_text(source.replace('namespace sampler {','namespace accepted {'))
    binary = root/'interpolation.exe'
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',*flags,'-I'+folder,
                    'tests/interpolation_test.cpp','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
    if sys.platform != 'win32':
        subprocess.run(['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',*flags,
                        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+folder,
                        'tests/interpolation_test.cpp','-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
