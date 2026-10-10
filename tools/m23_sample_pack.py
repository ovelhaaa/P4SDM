"""Reproducible M23 musical pack. Python + numpy==2.2.6 scipy==1.15.3.

External files are fetched only from pinned public URLs, with SHA-256 checks.
No project writes. All output is RIFF PCM16 mono at 44100 Hz.
"""
from pathlib import Path
import argparse, hashlib, json, math, urllib.request, wave, zipfile
import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly, butter, sosfilt

ROOT=Path(__file__).resolve().parents[1]
RATE=44100
def sha(data): return hashlib.sha256(data).hexdigest()
def canonical(value):
    if isinstance(value,float):return round(value,12)
    if isinstance(value,dict):return {k:canonical(v) for k,v in value.items()}
    if isinstance(value,list):return [canonical(v) for v in value]
    return value
def read_audio(path):
    rate,x=wavfile.read(path)
    if x.dtype.kind=='i': x=x.astype(np.float64)/(2**(x.dtype.itemsize*8-1))
    elif x.dtype.kind=='u': x=(x.astype(np.float64)-128)/128
    else: x=x.astype(np.float64)
    mono=x.mean(axis=1) if x.ndim==2 else x
    phase_ratio=float(np.sqrt(np.mean(mono**2))/max(1e-12,np.sqrt(np.mean(x**2))))
    if phase_ratio<0.25: raise ValueError(f'severe mono cancellation: {path}')
    if rate!=RATE:
        g=math.gcd(rate,RATE);mono=resample_poly(mono,RATE//g,rate//g)
    return mono,dict(original_rate=int(rate),mono_rms_ratio=phase_ratio)

def finish(x,category,trim=True):
    x=np.asarray(x,dtype=np.float64).copy()
    x-=np.mean(x)
    if trim:
        active=np.flatnonzero(np.abs(x)>max(.0001,float(np.max(np.abs(x)))*.001))
        if len(active): x=x[max(0,int(active[0])-32):min(len(x),int(active[-1])+441)]
    fade=min(len(x)//4,441)
    x[:32]*=np.linspace(0,1,min(32,len(x)))
    x[-fade:]*=np.linspace(1,0,fade)
    # Remove residual DC with a tapered window so endpoints remain zero.
    w=np.ones(len(x));w[:32]=np.linspace(0,1,min(32,len(x)));w[-fade:]=np.linspace(1,0,fade)
    x-=np.sum(x)/np.sum(w)*w
    ceilings={'kick':.78,'snare':.65,'hat_closed':.43,'hat_open':.43,'clap':.60,'bass':.70,'loop':.76,'texture':.40,'vocal':.45}
    peak=float(np.max(np.abs(x)));ceiling=ceilings.get(category,.60)
    gain=min(1.0,ceiling/max(peak,1e-12));x*=gain
    pcm=np.rint(x*32767).astype('<i2')
    return pcm,dict(dc_removal=True,start_fade_frames=32,end_fade_frames=fade,attenuation_gain=gain,trim=trim)

def synthesized(kind,note=60,variant=0,duration=1):
    t=np.arange(round(duration*RATE))/RATE
    f=440*2**((note-69)/12);rng=np.random.Generator(np.random.PCG64(2300+variant))
    if kind=='kick':
        phase=2*np.pi*np.cumsum(48+100*np.exp(-t*35))/RATE
        return .75*np.sin(phase)*np.exp(-t*8)+.07*rng.normal(size=len(t))*np.exp(-t*180)
    if kind=='bass':
        return sum(.46/h**1.6*np.sin(2*np.pi*f*h*t)*np.exp(-t*(2.4+h*.5)) for h in range(1,9))*(1-np.exp(-t*500))
    if kind=='mallet':
        ratios=([1,2,3,4.2],[1,2.76,5.4,8.9],[1,3,6,9],[1,1.25,1.5,2])[variant%4]
        return sum(.42/(i+1)**1.6*np.sin(2*np.pi*f*r*t)*np.exp(-t*(2+i*1.7)) for i,r in enumerate(ratios))*(1-np.exp(-t*600))
    if kind=='shaker':
        noise=sosfilt(butter(2,4000,fs=RATE,btype='highpass',output='sos'),rng.normal(size=len(t)))
        return .20*noise*(1-np.exp(-t*500))*np.exp(-t*22)
    if kind=='texture':
        x=sum(.09*np.sin(2*np.pi*f*r*t+variant)*np.sin(np.pi*t/duration)**2 for r in ([1,1.25,1.5,2,3] if variant==0 else [1,1.2,1.5,2,3]))
        return x*(.7+.3*np.sin(2*np.pi*.8*t))
    if kind=='vocal':
        # Original synthetic vowel, harmonic formant envelope; no human voice.
        centers=[700,1100,2500] if variant==0 else [350,1700,2700]
        x=np.zeros(len(t))
        for h in range(1,65):
            amplitude=sum(np.exp(-.5*((h*f-c)/110)**2) for c in centers)/h**.65
            x+=amplitude*.22*np.sin(2*np.pi*h*f*t)
        return x*(1-np.exp(-t*30))*np.exp(-t*4)
    raise ValueError(kind)

def build(destination,cache):
    lock=json.loads((ROOT/'tools/m23_sources.json').read_text())
    destination.mkdir(parents=True,exist_ok=True);cache.mkdir(parents=True,exist_ok=True)
    bundle=ROOT/'assets/m23_sources.zip'
    if bundle.exists():
        with zipfile.ZipFile(bundle) as z:
            for source in lock:
                target=cache/source['cache_name']
                if not target.exists():
                    data=z.read(source['cache_name'])
                    if sha(data)!=source['source_sha256']:raise ValueError('bundled source hash mismatch')
                    target.write_bytes(data)
    entries=[];audio={}
    for source in lock:
        path=cache/source['cache_name']
        if not path.exists():
            data=urllib.request.urlopen(source['download_url'],timeout=60).read()
            if sha(data)!=source['source_sha256']: raise ValueError('source hash mismatch '+source['filename'])
            path.write_bytes(data)
        if sha(path.read_bytes())!=source['source_sha256']:raise ValueError('cache hash mismatch '+source['filename'])
        x,conversion=read_audio(path)
        if source.get('first_hit_seconds'):
            onset=np.flatnonzero(np.abs(x)>max(.004,np.max(np.abs(x))*.12))[0]
            x=x[max(0,onset-64):onset+round(source['first_hit_seconds']*RATE)]
        add(destination,entries,audio,source['filename'],source['category'],x,source,conversion)
    for kind,category,names,notes,duration in [
        ('kick','kick',['M23_KICK_MODEL_01.wav'],[36],.6),
        ('shaker','shaker',['M23_SHAKER_SYNTH_01.wav'],[60],.4),
        ('bass','bass',['M23_BASS_PLUCK_C2.wav','M23_BASS_PLUCK_E2.wav','M23_BASS_PLUCK_G2.wav','M23_BASS_PLUCK_A1.wav'],[36,40,43,33],1.2),
        ('mallet','melodic',['M23_MALLET_C4.wav','M23_BELL_C4.wav','M23_PLUCK_G3.wav','M23_CHORD_CMAJ.wav'],[60,60,55,60],1.8),
        ('texture','texture',['M23_TEXTURE_CMAJ.wav','M23_TEXTURE_AMIN.wav'],[60,57],3),
        ('vocal','vocal',['M23_VOCAL_SYNTH_AH.wav','M23_VOCAL_SYNTH_OH.wav'],[48,48],.9)]:
        for v,(name,note) in enumerate(zip(names,notes)):
            source=dict(source_type='original_synthesis',source_url=None,original_author='P4SDM M23 procedural generator',license='CC0-1.0',root_note=note,description=f'{kind} synthesis, variant {v}; original generated audio')
            add(destination,entries,audio,name,category,synthesized(kind,note,v,duration),source,{'algorithm':kind,'seed':2300+v})
    for bpm in [100,120]:
        length=round(4*60/bpm*RATE);x=np.zeros(length)
        for beat in range(16):
            hits=[('M23_HAT_CLOSED_ELECTRO_01.wav',.40)]
            if beat in [0,8]:hits.append(('M23_KICK_ELECTRO_01.wav',.85))
            if beat in [4,12]:hits.append(('M23_SNARE_ACOUSTIC_01.wav',.70))
            for name,gain in hits:
                hit=audio[name]/32767.0*gain;offset=round(beat*60/bpm/4*RATE)
                np.add.at(x,(offset+np.arange(len(hit)))%length,hit)
        name=f'M23_LOOP_{bpm}BPM_01.wav'
        source=dict(source_type='derived_composition',source_url=None,original_author='P4SDM M23',license='CC0-1.0 + Unlicense',bpm=bpm,description='One bar, 4/4; kick/snare/hat with circular tails',ingredients=['M23_KICK_ELECTRO_01.wav','M23_SNARE_ACOUSTIC_01.wav','M23_HAT_CLOSED_ELECTRO_01.wav'])
        add(destination,entries,audio,name,'loop',x,source,{'beats':4,'circular_mix':True},trim=False)
    assert len(entries)==42,len(entries)
    manifest=dict(version=1,rate=RATE,format='RIFF integer PCM16',samples=entries,total_bytes=sum(e['file_bytes'] for e in entries),largest_resident_bytes=max(e['frames']*2 for e in entries),musical_acceptance='PENDING human listening')
    manifest=canonical(manifest)
    (destination/'manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n',newline='\n')
    (destination/'ATTRIBUTION.txt').write_text('M23 musical sample library\nLaSonotheque: Joseph Sardin, CC0-1.0. Individual URLs and original hashes in manifest.\nuzu-drumkit: Mot4i, Switch Angel and Ludens, The Unlicense; electronic/analog processed sources, never described as acoustic recordings.\nOriginal M23 synthesis dedicated to CC0-1.0 by this pack. Loops retain both ingredient licenses.\nhttps://creativecommons.org/publicdomain/zero/1.0/\nhttps://github.com/tidalcycles/uzu-drumkit/blob/'+next(s['commit'] for s in lock if 'commit' in s)+'/LICENSE\n',newline='\n')
    kits(destination,audio)
    return manifest

def add(destination,entries,audio,name,category,x,source,conversion,trim=True):
    pcm,processing=finish(x,category,trim);path=destination/name
    # Host rebuild is deterministic; abort on collisions, including existing output.
    import io
    buf=io.BytesIO()
    with wave.open(buf,'wb') as w:w.setnchannels(1);w.setsampwidth(2);w.setframerate(RATE);w.writeframes(pcm.tobytes())
    data=buf.getvalue()
    if path.exists() and path.read_bytes()!=data:raise ValueError('output collision '+name)
    path.write_bytes(data);audio[name]=pcm
    entry={**source,'filename':name,'category':category,'instrument':category,'duration':len(pcm)/RATE,'frames':len(pcm),'channels':1,'root_note':source.get('root_note'),'bpm':source.get('bpm'),'suggested_use':'Gate or OneShot; loops at listed BPM, sample pitch 60 plays original root','conversion':{**conversion,**processing},'sha256':sha(data),'file_bytes':len(data),'peak':float(np.max(np.abs(pcm.astype(np.int32))))/32768,'rms':float(np.sqrt(np.mean((pcm.astype(np.float64)/32768)**2))),'clipped_frames':int(np.count_nonzero(np.abs(pcm.astype(np.int32))>=32767)),'dc':float(np.mean(pcm))/32768,'endpoint_delta':int(pcm[-1])-int(pcm[0])}
    if entry['frames']*2>4*1024**2:raise ValueError('resident budget')
    entries.append(entry)

def kits(destination,audio):
    hybrid=['KICK_MODEL_01','KICK_ELECTRO_01','SNARE_ACOUSTIC_01','SNARE_ELECTRO_01','CLAP_01','HAT_CLOSED_ACOUSTIC_01','HAT_OPEN_01','SHAKER_01','TOM_LOW','TOM_HIGH','PERC_DJEMBE','RIM_01','PERC_AGOGO','BASS_PLUCK_C2','MALLET_C4','VOCAL_SYNTH_AH']
    electronic=['KICK_ELECTRO_01','KICK_ELECTRO_02','SNARE_ELECTRO_01','SNARE_ELECTRO_02','CLAP_01','HAT_CLOSED_ELECTRO_01','HAT_OPEN_01','SHAKER_SYNTH_01','KICK_ELECTRO_03','RIM_02','PERC_COWBELL','CLAP_02','HAT_CLOSED_ELECTRO_02','BASS_PLUCK_A1','PLUCK_G3','TEXTURE_AMIN']
    organic=['KICK_MODEL_01','TOM_LOW','SNARE_ACOUSTIC_01','SNARE_ACOUSTIC_02','PERC_DJEMBE','HAT_CLOSED_ACOUSTIC_01','HAT_CLOSED_ACOUSTIC_02','TAMBOURINE_01','TOM_MID','TOM_HIGH','PERC_AGOGO','RIM_01','PERC_TRIANGLE','BASS_PLUCK_C2','MALLET_C4','BELL_C4']
    result={label:[dict(track=i+1,midi_channel=10,midi_note=36+i,filename='M23_'+name+'.wav') for i,name in enumerate(names)] for label,names in [('hybrid',hybrid),('electronic',electronic),('organic_acoustic_led',organic)]}
    assert all(e['filename'] in audio for kit in result.values() for e in kit)
    (destination/'kits.json').write_text(json.dumps(result,indent=2)+'\n',newline='\n')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=ROOT/'assets/m23');p.add_argument('--cache',type=Path,default=ROOT/'.pio/m23-source-cache')
    a=p.parse_args();m=build(a.output,a.cache);print(f"M23: {len(m['samples'])} WAVs, {m['total_bytes']} bytes; largest resident {m['largest_resident_bytes']}")
