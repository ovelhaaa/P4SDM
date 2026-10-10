"""Strict real-card qualification; serial control is not human touchscreen QA."""
import argparse
from pathlib import Path
import re
from m213_capture import validate_physical
p=argparse.ArgumentParser();p.add_argument('log',type=Path);a=p.parse_args()
text=a.log.read_text(errors='replace');result=validate_physical(text)
assert result['accepted'],result
assert 'SAVE M22_LAYOUT_QUAL' in text and 'PROJECT M22_LAYOUT_QUAL' in text
assert '[Q CORRUPT]' in text and 'backup_bytes=52704 ok=1' in text
assert '[Q CORRUPT RESTORE]' in text and '[Q RENAME] restore=1 ok=1' in text
for op in (14,16,17):
    assert f'op={op} accepted=0 preserved=1 payload_equal=1 free_equal=1' in text
assert re.search(r'PROJECT READY / [1-9]\d* MISSING',text)
assert 'HOST M214 recovery/unmounted/short-read/allocation failures COMPLETE' in text
print('M22 real SD/WAV/replacement/Project/CRC/missing/injected-failure/Repeat qualification PASS; quantile coverage remains explicitly limited; human UX pending')
