"""Flash inherited capture implementation, stop only at a complete heap row."""
from pathlib import Path
source_path=Path('tests/physical_sd/capture.py').resolve()
source=source_path.read_text()
source=source.replace('import serial','import serial\nimport re')
source=source.replace('    while time.monotonic() < stop:', '    tail = b""\n    while time.monotonic() < stop:')
source=source.replace("            print(data.decode('utf-8','replace'), end='', flush=True)",
    "            print(data.decode('utf-8','replace'), end='', flush=True)\n"
    "            tail = (tail + data)[-4096:]\n"
    "            if re.search(rb'\\[M5 memory\\][^\\r\\n]*largest_after=\\d+\\r?\\n', tail): break")
exec(compile(source,str(source_path),'exec'),dict(__file__=str(source_path),__name__='__main__'))
