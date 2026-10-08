"""Check recorded physical SD acceptance without replacing inherited analyzers.

Retried captures preserve all attempts; only the explicitly last host session
is evaluated. Failed/setup captures remain documented separately in M19P.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
deadline = 256 / 44100 * 1e6

def capture(name):
    path = root / 'docs' / f'GUITION_M19_PHYSICAL_SD_{name}_SERIAL.log'
    text = path.read_text(errors='replace').rsplit('# HOST SESSION UTC ', 1)[-1]
    assert 'Guru Meditation' not in text and 'task_wdt' not in text, path
    return text

def rows(text, tag):
    return [dict((k, int(v)) for k, v in re.findall(r'(\w+)=(\d+)', line))
            for line in text.splitlines() if line.startswith(tag)]

def metrics(text):
    result = rows(text, '[Q METRICS]')
    assert len(result) == 1
    m = result[0]
    assert m['blocks'] > 100 and m['stored'] == m['blocks'] <= 16384
    assert 0 < m['p99'] <= m['max'] <= deadline * .8, m
    assert all(m[k] == 0 for k in ('misses', 'failures', 'timeouts')), m
    return m

def cold(text):
    assert 'Hard resetting via RTS' in text
    assert '[M6 SD] mounted=1 width=4 clock_khz=20000' in text
    assert '[M6 index]' in text

loads = capture('FIXED_LOADS')
assert len(rows(loads, '[M6 load]')) == 7
for name in ('impulses', 'kick', 'snare', 'mixed', 'stereo', 'large', 'oneshot'):
    assert f'name=m19p_{name}.wav' in loads
assert 'unsupported depth' in loads and 'invalid RIFF length' in loads
assert metrics(loads)['nonzero'] > 0

restore = capture('FIXED_RESTORE')
cold(restore)
assert 'bpm=143 swing=63 playing=0 mode=1 chain_length=4 chain_loop=1' in restore
assert 'PROJECT READY / 0 MISSING' in restore
assert 'frames=176400 waveform=0720701a name=m19p_mixed.wav' in restore
assert 'frames=176400 waveform=5e370bb2 name=m19p_snare.wav' in restore
assert 'track=0 index=1 start=9400 end=19000' in restore
for entry, pattern, repeats in ((0, 0, 2), (1, 1, 1), (2, 2, 3), (3, 3, 1)):
    assert f'[Q CHAIN] index={entry} pattern={pattern} repeats={repeats}' in restore
    assert f'[Q PATTERN] index={pattern} length=16 masks=1111,1111,1111' in restore
    for step in range(4):
        assert f'[Q LOCK] pattern={pattern} step={step} mask=128 slice={step}' in restore
assert max(r['chain_loops'] for r in rows(restore, '[Q TRANSPORT]')) >= 2
assert metrics(restore)['nonzero'] > 0

missing = capture('FIXED_MISSING')
cold(missing)
assert '[Q RENAME] restore=0 ok=1' in missing and '[Q RENAME] restore=1 ok=1' in missing
assert 'PROJECT READY / 1 MISSING' in missing and 'PROJECT READY / 0 MISSING' in missing
assert metrics(missing)['nonzero'] == 0

fallback = capture('FIXED_FALLBACK')
cold(fallback)
assert 'valid=0 reason=CRC ERROR' in fallback
assert 'bpm=137 swing=63' in fallback and 'bpm=143 swing=63' in fallback
assert 'generation=3 crc=3850630c valid=1' in fallback
assert 'generation=4 crc=ae47d8f2 valid=1' in fallback
assert '[Q STORE DONE] op=6' in fallback

for name, count in (('FIXED_CYCLES', 12), ('FIXED_INTEGRATED', 4)):
    text = capture(name)
    heaps = rows(text, '[Q HEAP] tag=state')
    assert len(heaps) >= count
    # All settled states include intermediate analysis snapshots: no free-heap loss.
    assert len({(h['internal'], h['free']) for h in heaps}) == 1, heaps
    assert metrics(text)['overlap'] > 0
integrated = capture('FIXED_INTEGRATED')
assert 'generation=7 crc=2c4d1d2b valid=1' in integrated
assert 'generation=8 crc=2c4d1d2b valid=1' in integrated
regular = [line for line in integrated.splitlines()
           if line.startswith('[Q FILE]') and 'directory=0' in line]
assert len(regular) == 11 and all(' bytes=0 ' not in line for line in regular)
assert not any('.qmissing' in line or 'QBACKUP' in line for line in regular)

dense = capture('FIXED_DENSE')
m = metrics(dense)
assert m['active_max'] == 16 and m['nonzero'] > 0 and m['overlap'] > 100
assert 0 < m['overlap_max'] <= deadline * .8
assert 'repeat_active=8 capture_count=16' in dense
assert max(r['cancel'] for r in rows(dense, '[Q TRANSIENT]')) >= 6
assert rows(dense, '[Q TRANSIENT]')[-1]['state'] == 4  # Applied, after mixed-file replacement.
print('Physical synthetic SD load/reset/restore/fallback/heap/dense headroom PASS; music/touch/power-loss pending')
