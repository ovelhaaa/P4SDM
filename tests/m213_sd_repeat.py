"""Dense physical x8 Repeat follow-up; existing WAVs, reserved project only."""
from pathlib import Path
source=Path('tests/m213_sd.py').read_text().split('\ntry:\n',1)[0]
exec(compile(source,'tests/m213_sd.py','exec'))
try:
    reset();index_rows=cmd('INDEX',2)
    indices={name:int(i) for i,name in re.findall(r'\[M213 index\] index=(\d+) name=(\S+)',index_rows)}
    index=indices['m19p_mixed.wav']
    send('Play',value=0)
    for t in range(16): load(t,index)
    send('ChainMode',value=0);send('SelectPattern',value=0);send('ClearPattern',p=0)
    send('Bpm',value=240);send('PatternLength',value=16,p=0)
    for t in range(16):
        send('Mute',t,0);send('Solo',t,0);send('SampleMode',t,0)
        send('Pitch',t,59);send('SliceDivide',t,4);send('SliceEnable',t,1)
        for s in range(16):
            send('Step',t,1,p=0,s=s);send('LockPitch',t,59,p=0,s=s)
            send('LockSlice',t,s%4,p=0,s=s)
    cmd('MEASURE 1');send('Play',value=1);read(2)
    send('PerfRepeatStart',value=8);read(3)
    load(1,index);send('PerfRepeatStop');read(1);send('Play',value=0)
    current=state();hits=re.search(r'\[Q PLAYBACK\].*repeat_hits=(\d+)',current)
    assert hits and int(hits[1])>0,current
    cmd('MEASURE 0');cmd('METRICS',2);store(8)
    log.write('\n# HOST M213 dense physical x8 Repeat COMPLETE\n');log.flush()
finally:
    port.close();log.close()
