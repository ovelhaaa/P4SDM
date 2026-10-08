import sys,time,re,json,serial,datetime,subprocess,os
from pathlib import Path
root=Path(__file__).resolve().parents[2]
body=(root/'src/app/model.h').read_text().split('enum class Kind : uint8_t {',1)[1].split('};',1)[0]
kinds=[s.strip() for s in body.split(',')]
K={name:i for i,name in enumerate(kinds)}
port=serial.Serial(port=None,baudrate=115200,timeout=.1)
port.dtr=False;port.rts=False;port.port='COM13';port.open()
log=(root/'docs'/('GUITION_M19_PHYSICAL_SD_'+sys.argv[1].upper()+'_SERIAL.log')).open('a',encoding='utf-8')
log.write(f'\n# HOST SESSION UTC {datetime.datetime.now(datetime.timezone.utc).isoformat()} mode={sys.argv[1]}\n');log.flush()
def read(seconds):
    end=time.monotonic()+seconds; data=''
    port.timeout=min(.02,seconds)
    while time.monotonic()<end:
        b=port.read(min(32768,port.in_waiting) or 1)
        if b:
            s=b.decode('utf-8','replace');log.write(s);log.flush();data+=s;print(s,end='',flush=True)
    if 'Guru Meditation' in data and mode!='console': raise AssertionError('device panic during '+mode)
    return data
def cmd(line,seconds=.25):
    log.write('\n# HOST '+line+'\n');log.flush();port.write((line+'\n').encode());return read(seconds)
def wait_for(marker,limit=45,initial=''):
    text=initial;stop=time.monotonic()+limit
    while marker not in text and time.monotonic()<stop:
        text+=read(.25)
    assert marker in text,'timeout waiting for '+marker+'\n'+text
    return text+read(.15)
def send(kind,t=0,value=0,p=0,s=0,seconds=.1):
    if kind=='Step': value=s
    return cmd(f'CMD {K[kind]} {t} {value} {p} {s}',seconds)
def state(): return wait_for('[Q PLAYBACK]',10,cmd('STATE',.2))
def load(t,index):
    text=cmd(f'LOAD {t} {index}',.2)
    assert '[Q ACK] load=1' in text,text
    if index not in (6,7): wait_for('[M6 load]',initial=text)
    else: read(.5)
def store(op): return wait_for(f'[Q STORE DONE] op={op}',60,cmd(f'STORE {op}',.25))
def project(name='M19P_SD_QUAL'):
    cmd('PROJECT '+name,.2);read(4)
    text=state();assert 'project_busy=0 restoring=0' in text and 'PROJECT READY /' in text,text
def save():
    cmd('SAVE M19P_SD_QUAL',.2);read(2)
def analyze(target=0,sense=1):
    send('TransientAnalyze',value=target|(sense<<8));read(.7)
    stop=time.monotonic()+15
    while time.monotonic()<stop:
        text=state()
        if re.search(r'\[Q TRANSIENT\] state=3 ',text): return text
        read(.4)
    raise AssertionError('analysis did not produce Ready proposal')
def apply():
    text=state();m=re.search(r'\[Q TRANSIENT\].*request=(\d+)',text); assert m
    send('TransientApply',value=int(m[1]));read(.5)
def reset():
    # Let esptool handle the Windows/native USB boot/reset sequence. A bare
    # RTS pulse can be ignored; a naive DTR pulse can enter download mode.
    port.close()
    interpreter=os.environ.get('P4SDM_ESPTOOL_PYTHON',r'C:\.platformio\penv\Scripts\python.exe')
    executable=os.environ.get('P4SDM_ESPTOOL',r'C:\.platformio\penv\Scripts\esptool.exe')
    env=os.environ.copy();env['PYTHONIOENCODING']='utf-8'
    result=subprocess.run([interpreter,executable,'--chip','esp32p4','--port','COM13','--before','default-reset','--after','hard-reset','chip-id'],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,encoding='utf-8',errors='replace')
    log.write('\n# HOST ESPrun reset\n'+result.stdout);log.flush();print(result.stdout,flush=True)
    assert result.returncode==0 and 'Hard resetting via RTS' in result.stdout
    port.open()
    text=read(5)
    assert '[M6 SD] mounted=1' in text and '[M6 index]' in text,'fresh application boot not observed: '+text
mode=sys.argv[1].removeprefix('fixed_')
if mode=='seed':
    reset();text=store(1);assert '[Q STORE DONE] op=1' in text and 'Guru Meditation' not in text,text
    store(2);state()
elif mode=='console':
    cmd(' '.join(sys.argv[2:]),4)
elif mode=='finishseed':
    read(40);store(2);state()
elif mode=='loads':
    cmd('MEASURE 1');
    for index in range(9):
        load(0,index);send('Trigger');read(.3);state()
    cmd('MEASURE 0');cmd('METRICS',1)
elif mode=='slicing':
    load(0,3)
    send('Pitch',value=67);send('Source',value=0);send('Pitch',value=43);send('Source',value=1);state();send('Trigger');read(.5)
    for n in (4,8,16):
        send('SliceDivide',value=n);send('SliceSelect',value=2);send('SliceAudition',s=2);state()
    send('SliceStart',value=9000,s=1);send('SliceEnd',value=19000,s=1);state()
    before=state();analyze(8);send('TransientCancel');after=state()
    def bank(text):return re.findall(r'\[Q SLICE\] track=0.*',text)
    assert bank(before)==bank(after),'CANCEL changed bank'
    analyze(8);apply();state()
    for index in (0,1,2,3,4,8):
        load(0,index)
        for sense in (0,1,2):
            for target in (0,8,16):
                analyze(target,sense);send('TransientCancel')
elif mode=='stale':
    load(0,5);send('TransientAnalyze',value=272,seconds=.04);send('SampleStart',value=1024,seconds=.05);read(2);state()
    send('SampleResetRegion');send('TransientAnalyze',value=272,seconds=.04);cmd('LOAD 0 3',.04);read(6);state()
elif mode=='cycles':
    load(0,3);state()
    cmd('MEASURE 1')
    for cycle in range(12):
        log.write(f'\n# CYCLE {cycle}\n');log.flush()
        load(0,1);load(0,3);analyze(8);apply();state()
    cmd('MEASURE 0');cmd('METRICS',1)
elif mode=='integrated':
    project();load(0,3);state();cmd('MEASURE 1')
    for cycle in range(4):
        log.write(f'\n# INTEGRATED CYCLE {cycle}\n');log.flush()
        load(0,1);load(0,3);analyze(8);apply();save();project();state()
    cmd('MEASURE 0');cmd('METRICS',1);store(7)
elif mode=='save':
    send('Play',value=0);load(0,3);load(1,2);send('Source',t=2,value=0)
    for t,pitch in ((0,36),(1,39)):
        send('Source',t,value=0);send('Pitch',t,value=pitch);send('Source',t,value=1)
    send('SampleMode',value=1);send('SampleChoke',value=1)
    send('SampleMode',t=1,value=0);send('SampleChoke',t=1,value=1)
    send('Pitch',t=0,value=64);send('Pitch',t=1,value=58);send('Pitch',t=2,value=51)
    send('Bpm',value=137);send('Swing',value=63)
    send('FilterCutoff',value=48);send('FilterResonance',value=37);send('DelaySend',value=91)
    send('FilterCutoff',t=1,value=72);send('DelaySend',t=1,value=44)
    send('SampleStart',value=1024);send('SampleEnd',value=62000);send('SliceDivide',value=8);send('SliceStart',value=9400,s=1);send('SliceEnd',value=19000,s=1)
    send('SliceEnable',value=1)
    for p in range(4):
        send('ClearPattern',p=p)
        for t in range(3):
            for s in range(0,16,4):send('Step',t,1,p,s)
        for s in range(4):send('LockSlice',value=s,p=p,s=s)
    send('Mute',t=3,value=1);send('Solo',t=2,value=1)
    send('ChainClear')
    for p,repeats in enumerate((2,1,3,1)):
        send('ChainAdd',value=p,s=p);send('ChainRepeats',value=repeats,s=p)
    send('ChainLoop',value=1);send('ChainMode',value=1);state();save();store(7)
    send('Bpm',value=143);save();store(7);state()
elif mode=='restore':
    reset();project();state();cmd('MEASURE 1');send('Play',value=1)
    for i in range(60): cmd('TRANSPORT',.5)
    send('Play',value=0);cmd('MEASURE 0');cmd('METRICS',1);state();store(7)
elif mode=='playback':
    load(0,3);load(1,2);send('Play',value=0);send('SliceEnable',value=0);send('SampleStart',value=4000);send('SampleEnd',value=60000)
    cmd('MEASURE 1')
    send('SampleReverse',value=0);send('Trigger');read(.5);send('SampleReverse',value=1);send('Trigger');read(.5)
    send('SampleMode',value=1);send('Trigger');read(.3);send('GateRelease');read(.3)
    send('SampleReverse',value=0);send('SampleMode',value=0);send('SampleChoke',value=1);send('SampleChoke',t=1,value=1)
    send('Trigger');read(.15);send('Trigger',t=1);read(.3);send('Trigger',t=1);read(.3)
    send('SampleMode',value=1);send('Step',value=1,p=0,s=0);send('Play',value=1);read(2);send('Play',value=0)
    send('Trigger');read(.2);send('Source',value=0);read(.2);send('Source',value=1)
    cmd('MEASURE 0');cmd('METRICS',1);state()
elif mode=='missing':
    send('Play',value=0);store(3);reset();project();state();cmd('MEASURE 1');send('Trigger',t=1);read(.5);cmd('MEASURE 0');cmd('METRICS',1)
    store(4);reset();project();state()
elif mode=='fallback':
    send('Play',value=0);store(5);store(7);reset();project();state();store(6);store(7);project();state()
elif mode=='dense':
    project();send('ChainMode',value=0);send('Bpm',value=240)
    send('SelectPattern',value=0);send('ClearPattern',p=0);send('PatternLength',value=16,p=0)
    for t in range(16):
        load(t,3)
        send('Mute',t,0);send('Solo',t,0);send('SampleMode',t,0);send('SampleReverse',t,0);send('SampleChoke',t,0)
        send('SliceDivide',t,16);send('FilterCutoff',t,30+t*4);send('FilterResonance',t,96);send('DelaySend',t,127)
        for s in range(16):
            for kind,value in (('Step',1),('Velocity',127),('Probability',100),('Ratchet',4),('LockPitch',36+(t+s)%48),('LockVolume',127),('LockPan',(t*17+s*13)%255-127),('LockWave',(t+s)%16),('LockFilterCutoff',20 if s%4==0 else 100 if s%4==1 else 40 if s%4==2 else 120),('LockFilterResonance',10 if s%4==0 else 100 if s%4==1 else 40 if s%4==2 else 127),('LockDelaySend',96),('LockSlice',s)):
                send(kind,t,value,0,s,seconds=.01)
    load(0,5);send('SliceDivide',value=16);cmd('PAGE 17');send('Play',value=1);read(2);send('PerfRepeatStart',value=8);read(1);cmd('TRANSPORT')
    cmd('MEASURE 1')
    for i in range(6):
        analyze(16);send('TransientCancel');send('SliceSelect',value=i)
    load(0,3);analyze(16);apply();read(5)
    send('PerfRepeatStop');send('Play',value=0);cmd('MEASURE 0');cmd('METRICS',1);state()
else:raise SystemExit('unknown mode')
port.close();log.close()
