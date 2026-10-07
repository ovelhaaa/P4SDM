"""Use the actual accepted M10 filter as the PCM regression oracle."""
import subprocess
import tempfile
from pathlib import Path
with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    source = subprocess.check_output(['git', 'show', '232c35c36428ef19de6910318ea98cddf54ad052:synthESP32LowPassFilter_E.h']).decode()
    (root/'m10_filter.h').write_text(source.replace('LowPassFilter', 'M10Filter').replace('LOWPASS_H_', 'M10_LOWPASS_H_'))
    binary = Path('.pio/m11_tone_equivalence.exe').resolve()
    binary.parent.mkdir(exist_ok=True)
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Itests/fx_stubs', '-I'+folder, 'tests/tone_equivalence.cpp', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
