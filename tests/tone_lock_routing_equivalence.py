"""Use the actual accepted M11 filter as the PCM regression oracle."""
import subprocess
import tempfile
from pathlib import Path
with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    source = subprocess.check_output(['git', 'show', '4afaa51cec574594b35d1f8bfee7938cced6de31:synthESP32LowPassFilter_E.h']).decode()
    (root/'m11_filter.h').write_text(source.replace('LowPassFilter', 'M11Filter').replace('LOWPASS_H_', 'M11_LOWPASS_H_'))
    tone = subprocess.check_output(['git', 'show', '4afaa51cec574594b35d1f8bfee7938cced6de31:src/engine/track_tone.h']).decode()
    (root/'m11_tone.h').write_text(tone.replace('namespace p4tone {', 'namespace m11tone {'))
    binary = Path('.pio/m12_tone_routing_equivalence.exe').resolve()
    binary.parent.mkdir(exist_ok=True)
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Itests/fx_stubs', '-I'+folder, 'tests/tone_lock_routing_equivalence.cpp', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
