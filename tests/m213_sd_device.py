"""Sequential physical SD qualification; no seed or unrelated SD writes."""
from pathlib import Path
import subprocess, sys
root=Path(__file__).resolve().parents[1]
source=(root/'tests/physical_sd/capture.py').read_text()
source=source.replace("root / '.pio/build' / name", "root / '.pio/m19p/.pio/build' / name")
script=root/'.pio/m213_sd_capture.py'
source=source.replace("root = Path(__file__).resolve().parents[2]", "root = Path(__file__).resolve().parents[1]")
script.write_text(source)
try:
    for policy in sys.argv[1:]:
        print('SD POLICY '+policy,flush=True)
        with (root/f'.pio/m213-SD_{policy}-flash.log').open('w') as log:
            subprocess.run([sys.executable,str(script),'guition_m213_sd_'+policy,'8',f'docs/GUITION_M213_SD_{policy}_BOOT_SERIAL.log'],stdout=log,stderr=subprocess.STDOUT,check=True)
        with (root/f'.pio/m213-SD_{policy}-host.log').open('w',encoding='utf-8') as log:
            subprocess.run([sys.executable,'tests/m213_sd.py',policy],stdout=log,stderr=subprocess.STDOUT,check=True)
finally:
    # Restore the already archived production image, independent of later PIO
    # directory cleanup. Do not rebuild and pretend it matches that archive.
    files=list((root/'.pio/m213-artifacts').glob('*/production/firmware.bin'))
    if files:
        archive=max(files,key=lambda p:p.stat().st_mtime).parent
        restore=root/'.pio/m213_sd_restore.py'
        original=(root/'tests/physical_sd/capture.py').read_text().replace(
            "root = Path(__file__).resolve().parents[2]", "root = Path(__file__).resolve().parents[1]")
        original=original.replace("build = root / '.pio/build' / name",f"build = Path({archive.as_posix()!r})")
        restore.write_text(original)
        with (root/'.pio/m213-sd-restore.log').open('w') as log:
            subprocess.run([sys.executable,str(restore),'guition_app','8','docs/GUITION_M213_SD_RESTORED_SERIAL.log'],stdout=log,stderr=subprocess.STDOUT,check=True)
