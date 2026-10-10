"""Reuse accepted SD qualification in an isolated console with reserved M22 names."""
from pathlib import Path
import shutil
import subprocess
import sys
subprocess.run([sys.executable,'tests/m214_sd_prepare.py'],check=True)
target=Path('.pio/m22-sd')
shutil.copytree('.pio/m214-sd',target,dirs_exist_ok=True,ignore=shutil.ignore_patterns('.pio'))
for path in (target/'src').rglob('*'):
    if path.is_file() and path.suffix in ('.cpp','.h','.inc'):
        path.write_text(path.read_text().replace('M214_LAYOUT_QUAL','M22_LAYOUT_QUAL').replace('M214_MISSING_QUAL','M22_MISSING_QUAL'))
print('M22 reserved diagnostic SD console prepared; production source untouched')
