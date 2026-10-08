"""Compare the cache candidate against untouched accepted M21 Git sources."""
from pathlib import Path
import subprocess
import sys
import tempfile

baseline = '7d90acea7aa18ddc7af1b8c5ad00d6b44cd4b16c'
with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    (root/'accepted').mkdir()
    for name in ('wav.h', 'sample_interpolation.h', 'waveform.h', 'sample_playback.h'):
        source = subprocess.check_output(['git', 'show', f'{baseline}:src/app/{name}']).decode()
        (root/'accepted'/name).write_text(source.replace('namespace sampler {', 'namespace accepted {'))
    binary = root/'cache.exe'
    for mode in range(3):
        command = ['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                   f'-DP4SDM_INTERPOLATION={mode}', '-DP4SDM_LINEAR_32BIT=1', '-I'+folder,
                   'tests/pcm_cache_test.cpp', '-o', str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
    if sys.platform != 'win32':
        subprocess.run(['g++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-DP4SDM_LINEAR_32BIT=1', '-I'+folder,
                        'tests/pcm_cache_test.cpp', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
