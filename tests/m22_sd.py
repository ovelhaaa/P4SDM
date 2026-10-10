"""Fresh physical SD tests; existing fixtures, reserved M22 projects, serial commands."""
from pathlib import Path
source=Path('tests/m214_sd.py').read_text()
source=source.replace('M214_LAYOUT_QUAL','M22_LAYOUT_QUAL').replace('M214_MISSING_QUAL','M22_MISSING_QUAL').replace('GUITION_M214_SD_','GUITION_M22_SD_')
exec(compile(source,'tests/m214_sd.py','exec'))
