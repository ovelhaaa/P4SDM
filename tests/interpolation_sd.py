"""Scripted physical SD workflows using the inherited temporary console.

Run prepare first, compile/upload that ignored copy with INTERPOLATION=1 or 2.
Only the reserved M21_INTERP_QUAL project is saved. Existing WAVs are read.
"""
from pathlib import Path
import sys
mode='m21'
common=Path('tests/physical_sd/run.py').read_text().split('\nmode=sys.argv[1]',1)[0]
common=common.replace('Path(__file__).resolve().parents[2]','Path(__file__).resolve().parents[1]')
common=common.replace('GUITION_M19_PHYSICAL_SD_','GUITION_M21_SD_').replace('M19P_SD_QUAL','M21_INTERP_QUAL')
exec(compile(common,'tests/physical_sd/run.py','exec'))
try:
    reset()
    inventory=store(2)
    # Require the known generated fixtures; never seed/format or mutate a WAV.
    assert 'm19p_' in inventory
    cmd('MEASURE 1')
    load(0,3);load(1,2)
    current=state()
    assert 'name=m19p_' in current,current
    send('Play',value=0)
    for t in range(2):
        send('SampleChoke',t,0);send('SampleResetRegion',t)
        send('SliceDivide',t,4);send('SliceEnable',t,1)
        send('SampleMode',t,0)
        for pitch in (48,59,61,67,72):
            send('Pitch',t,pitch)
            for reverse in (0,1):
                send('SampleReverse',t,reverse)
                send('SliceAudition',t,s=2);read(.12)
    # Gate release, choke, replacing a sounding PCM assignment.
    send('SampleMode',value=1);send('Trigger');read(.15);send('GateRelease')
    send('SampleChoke',value=1);send('SampleChoke',t=1,value=1)
    send('Trigger');send('Trigger',t=1);load(1,3);load(1,2)
    send('ChainMode',value=0);send('SelectPattern',value=0);send('ClearPattern',p=0)
    send('Bpm',value=240);send('PatternLength',value=16,p=0)
    for t in range(16):
        send('Mute',t,int(t>=2));send('Solo',t,0)
    for t in range(2):
        send('SampleMode',t,0);send('SampleChoke',t,0)
        for s in range(16):
            send('Step',t,1,p=0,s=s)
            send('LockPitch',t,59+t*2,p=0,s=s)
            send('LockSlice',t,s%4,p=0,s=s)
    send('Play',value=1);read(1)
    send('PerfRepeatStart',value=8);read(1)
    send('SliceSelect',value=3);send('LockSlice',value=1,p=0,s=0)
    load(1,3);read(.5)
    send('PerfRepeatStop');send('Play',value=0)
    before=state()
    assert 'repeat_hits=0 ' not in before,before
    save();send('Pitch',value=84);project();after=state()
    assert 'bytes=52704' in store(7)
    assert 'PROJECT READY /' in after
    cmd('MEASURE 0');cmd('METRICS',1)
    log.write('\n# HOST M21 SD workflows completed; listening/touch not observed\n');log.flush()
finally:
    port.close();log.close()
