"""Physical pack qualification through inherited production loader/audio paths.

Diagnostic serial console; RAM-only edits; no project save or storage mutation.
Always restores normal production firmware, including on failed assertions.
"""
from pathlib import Path
import hashlib,json,re,shutil,subprocess,sys,time
import serial
root=Path(__file__).resolve().parents[1]
shutil.copytree(root/'.pio/m23-loader/.pio/build/guition_m23_loader',root/'.pio/build/guition_m23_loader',dirs_exist_ok=True)
pack=root/'assets/m23';manifest=json.loads((pack/'manifest.json').read_text());entries={e['filename']:e for e in manifest['samples']}
model=(root/'src/app/model.h').read_text().split('enum class Kind : uint8_t {',1)[1].split('};',1)[0]
kinds={name.strip():i for i,name in enumerate(model.split(','))}
log=(root/'docs/m23/LOADER_SERIAL.log').open('w',encoding='utf-8');port=None
result=dict(status='PENDING',loads=[],musical_listening='PENDING',instrumentation='inherited serial console added; production loader/DSP unchanged')
def capture(name,seconds,destination):
    subprocess.run([sys.executable,'tests/physical_sd/capture.py',name,str(seconds),destination],cwd=root,stdout=subprocess.DEVNULL,check=True)
try:
    capture('guition_m23_loader',8,'docs/m23/LOADER_BOOT_SERIAL.log')
    port=serial.Serial(port=None,baudrate=115200,timeout=.02);port.dtr=False;port.rts=False;port.port='COM13';port.open()
    def read(seconds):
        stop=time.monotonic()+seconds;data=bytearray()
        while time.monotonic()<stop:
            b=port.read(min(port.in_waiting,16384) or 1)
            if b:data.extend(b)
        text=data.decode('utf-8','replace');log.write(text);log.flush()
        assert not any(s in text for s in ['Guru Meditation','watchdog','ASSERT_FAILED'])
        return text
    def cmd(text,seconds=.15):
        log.write('# HOST '+text+'\n');port.write((text+'\n').encode());port.flush();return read(seconds)
    def send(kind,track=0,value=0,pattern=0,step=0):
        text=cmd(f'CMD {kinds[kind]} {track} {value} {pattern} {step}')
        assert '[Q ACK] sent=1' in text,text
    indextext=cmd('INDEX',4)
    indexes={name:int(index) for index,name in re.findall(r'\[M213 index\] index=(\d+) name=(\S+)',indextext)}
    assert set(entries)<=set(indexes),indexes
    def load(track,name):
        text=cmd(f'LOAD {track} {indexes[name]}',.3)
        assert '[Q ACK] load=1' in text,text
        stop=time.monotonic()+15
        while time.monotonic()<stop:
            state=cmd('STATE',.4)
            match=re.search(rf'\[Q TRACK\] track={track} .*?frames=(\d+) waveform=([0-9a-f]+) name=(\S+)',state)
            if match and match[3]==name:
                assert int(match[1])==entries[name]['frames']
                return dict(track=track,filename=name,frames=int(match[1]),waveform_hash=match[2])
        raise AssertionError('loader did not publish '+name)
    for name in entries:
        result['loads'].append(load(0,name));print('Production loader accepted',len(result['loads']),name,flush=True)
    kit=json.loads((pack/'kits.json').read_text())['hybrid']
    result['kit']=[load(e['track']-1,e['filename']) for e in kit]
    # Live controls exercise normal audio command paths without changing files.
    for kind,value in [('Pitch',48),('Pitch',72),('Pitch',60),('SampleReverse',1),('SampleReverse',0),('SliceDivide',4),('SliceEnable',1),('Trigger',0),('SampleMode',1),('Trigger',0),('GateRelease',0),('SampleMode',0)]:
        send(kind,0,value)
    send('TransientAnalyze',0,0|(1<<8));read(2)
    result['analyzer_state']=cmd('STATE',.5)
    assert re.search(r'\[Q TRANSIENT\] state=3 ',result['analyzer_state']),result['analyzer_state']
    send('TransientCancel');send('SliceEnable',0,0)
    send('ClearPattern',0,0)
    for t in range(16):
        for step in [0,4,8,12]:send('Step',t,step)
    cmd('MEASURE 1');send('Play',0,1);read(20);send('Play',0,0);cmd('MEASURE 0');read(.5)
    metrics=cmd('METRICS',1)
    match=re.search(r'\[Q METRICS\] (.+)',metrics);assert match,metrics
    result['audio']={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',match[1])}
    for key in ['misses','failures','timeouts']:assert result['audio'][key]==0
    assert result['audio']['active_max']==16 and result['audio']['max']<=4644,result['audio']
    result['final_state']=cmd('STATE',1);result['status']='PHYSICAL SD + NORMAL LOADER PASS'
finally:
    if port:port.close()
    log.close()
    (root/'docs/m23/loader_results.json').write_text(json.dumps(result,indent=2)+'\n')
    capture('guition_app',8,'docs/m23/PRODUCTION_RESTORED_SERIAL.log')
