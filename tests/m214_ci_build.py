"""Portable build/archive matrix; CI images are distinct from hardware captures."""
from pathlib import Path
import subprocess
import sys

for environment in ('guition_app_linear_candidate','guition_app_nearest_legacy',
                    'guition_m214_A','guition_m214_B','guition_m214_C',
                    'guition_m214_SAMPLE_sustained','guition_m214_SYNTH_sustained'):
    subprocess.run([sys.executable,'-m','platformio','run','-e',environment],check=True)
    subprocess.run([sys.executable,'tests/archive_firmware.py',environment,
                    '.pio/m214-ci-artifacts/'+environment,
                    '--pio-home',str(Path.home()/'.platformio')],check=True)
