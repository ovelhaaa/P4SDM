"""Same physical ownership/SD workload as M21.3, new logs and reserved project."""
from pathlib import Path
import sys
source=Path('tests/m213_sd.py').read_text()
source=source.replace('GUITION_M213_SD_', 'GUITION_M214_SD_')
source=source.replace('M213_LAYOUT_QUAL','M214_LAYOUT_QUAL')
source=source.replace('M213 SD policy=', 'M214 SD policy=')
source=source.replace('    for t in range(16): load(t,wav[\'mixed\'])', """    load(0,wav['mixed'])
    for op in (14,16,17):
        text=store(op)
        assert f'op={op} accepted=0 preserved=1 payload_equal=1 free_equal=1' in text,text
    for t in range(16): load(t,wav['mixed'])""")
source=source.replace("    wait_for('[M6 load]',initial=text)", """    wait_for('[M6 load]',initial=text)
    # The load report precedes the audio handoff and retirement cleanup.
    # Wait for that existing ownership boundary before requesting replacement.
    stop=time.monotonic()+20
    while time.monotonic()<stop:
        if 'sample_busy=0' in state(): break
        read(.1)
    else: raise AssertionError('sample ownership handoff did not finish')""")
source=source.replace('    reset()\n', "    reset(); cmd('POWER',1)\n")
source=source.replace('    log.write(f\'\\n# HOST', "    cmd('POWER',1)\n    log.write(f'\\n# HOST")
extra='''    # Reserved diagnostic files only. Test generation/CRC fallback and a
    # missing generated WAV reference; always restore before leaving the test.
    send('Play',value=0)
    for op in (14,16,17):
        text=store(op)
        assert f'op={op} accepted=0 preserved=1 payload_equal=1 free_equal=1' in text,text
    load(0,wav['mixed'])
    send('Bpm',value=143);save()
    send('Bpm',value=144);save()
    try:
        assert 'ok=1' in store(5)
        project();assert 'bpm=143' in state()
    finally:
        store(6)
    project();assert 'bpm=144' in state()
    try:
        assert 'ok=1' in store(3)
        text=cmd('PROJECT M214_LAYOUT_QUAL',.2)
        assert '[Q ACK] project=1' in text
        stop=time.monotonic()+120
        while time.monotonic()<stop:
            read(.5); text=state()
            if 'project_busy=0 restoring=0' in text: break
        assert re.search(r'PROJECT READY / [1-9]\\d* MISSING',text),text
    finally:
        store(4)
    project();assert 'PROJECT READY / 0 MISSING' in state()
    before=state()
    cmd('PROJECT M214_MISSING_QUAL',.2);read(3)
    after=state()
    assert 'project_busy=0 restoring=0' in after and 'bpm=144' in after,after
    assert '[M6 load]' not in after,'Missing project must not replace resident PCM'
    log.write('\\n# HOST M214 recovery/unmounted/short-read/allocation failures COMPLETE\\n');log.flush()
'''
source=source.replace("    cmd('POWER',1)\n    log.write(f'\\n# HOST",extra+"    cmd('POWER',1)\n    log.write(f'\\n# HOST")
exec(compile(source,'tests/m213_sd.py','exec'))
