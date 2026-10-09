"""Read existing qualification WAVs; only write M213_LAYOUT_QUAL project.

Run on the dedicated physical device after m213_sd_prepare/build/upload.
Indices are resolved from actual indexed filenames, never assumed SD ordering.
"""
from pathlib import Path
import sys, re
policy=sys.argv[1]
mode='m213'
common=Path('tests/physical_sd/run.py').read_text().split('\nmode=sys.argv[1]',1)[0]
common=common.replace('Path(__file__).resolve().parents[2]','Path(__file__).resolve().parents[1]')
common=common.replace('GUITION_M19_PHYSICAL_SD_','GUITION_M213_SD_').replace('M19P_SD_QUAL','M213_LAYOUT_QUAL')
exec(compile(common,'tests/physical_sd/run.py','exec'))
def state():
    # Native USB may drop the leading fragment while SD restoration emits a
    # burst. Retry only this read-only query, preserving all original output.
    for attempt in range(3):
        port.write(b'\n');read(.1)
        text=cmd('STATE',.3)
        stop=time.monotonic()+20
        while '[Q PLAYBACK]' not in text and time.monotonic()<stop:
            text+=read(.25)
        if '[Q PLAYBACK]' in text: return text+read(.15)
        log.write(f'\n# HOST read-only STATE retry={attempt+1}\n');log.flush()
    raise AssertionError('complete STATE response missing')
def project(name='M213_LAYOUT_QUAL'):
    text=cmd('PROJECT '+name,.2)
    assert '[Q ACK] project=1' in text,text
    stop=time.monotonic()+120
    while time.monotonic()<stop:
        read(1);text=state()
        if 'project_busy=0 restoring=0' in text and 'PROJECT READY /' in text:
            assert 'PROJECT READY / 0 MISSING' in text,text
            return
    raise AssertionError('Project restoration incomplete')
try:
    reset()
    index_rows=cmd('INDEX',2)
    indices={name:int(i) for i,name in re.findall(r'\[M213 index\] index=(\d+) name=(\S+)',index_rows)}
    required=['mixed','kick','snare','stereo','oneshot','large','bad_depth','truncated']
    wav={key:indices['m19p_'+key+'.wav'] for key in required}
    send('Play',value=0)
    for t in range(16): load(t,wav['mixed'])
    store(8)
    for cycle in range(2):
        for t in reversed(range(16)):
            load(t,wav['kick'] if (t+cycle)&1 else wav['mixed'])
    store(8)
    sequence=['kick','snare','mixed','stereo','oneshot']
    for t in range(16): load(t,wav[sequence[t%5]])
    load(0,wav['large']); store(8)
    for key in ('bad_depth','truncated'):
        text=cmd(f"LOAD 0 {wav[key]}",1)
        assert '[Q ACK] load=1' in text
        assert '[M6 load]' not in text
    store(9); store(8)
    store(10)
    for t in range(15,-1,-1): load(t,wav[sequence[(t+1)%5]])
    store(8)
    store(12)
    text=cmd(f"LOAD 0 {wav['large']}",2)
    assert '[Q ACK] load=1' in text and '[M6 load]' not in text
    assert 'Sample exceeds PSRAM budget' in state()
    store(13); load(0,wav['large']); store(8)
    save(); project(); store(8)
    cmd('MEASURE 1')
    for t in range(16):
        send('Pitch',t,47+t); send('SampleReverse',t,t&1)
        send('SliceDivide',t,4); send('SliceEnable',t,1)
        send('SampleMode',t,0); send('SliceAudition',t,s=2)
    send('SampleMode',value=1); send('Trigger'); read(.2); send('GateRelease')
    send('SampleChoke',value=1); send('Trigger'); load(0,wav['mixed'])
    analyze(8); send('TransientCancel'); analyze(8); apply()
    send('ChainMode',value=0);send('SelectPattern',value=0);send('ClearPattern',p=0)
    send('Bpm',value=240);send('PatternLength',value=16,p=0)
    for t in range(16):
        send('Mute',t,0);send('Solo',t,0);send('SampleMode',t,0)
        for s in (0,4,8,12):
            send('Step',t,1,p=0,s=s);send('LockSlice',t,s%4,p=0,s=s)
    send('Play',value=1);read(1);send('PerfRepeatStart',value=8);read(2)
    load(1,wav['stereo']);send('PerfRepeatStop');read(1);send('Play',value=0)
    cmd('MEASURE 0');cmd('METRICS',2);save();project();store(8);state()
    log.write(f'\n# HOST M213 SD policy={policy} COMPLETE; no listening observed\n');log.flush()
finally:
    port.close();log.close()
