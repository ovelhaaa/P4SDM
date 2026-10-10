"""Reproducible synthetic instrument proxies; no per-file normalization."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys
import wave
import zipfile
import numpy as np

p=argparse.ArgumentParser()
p.add_argument('--output',type=Path,default=Path('.pio/m214-listening'))
a=p.parse_args(); root=a.output; root.mkdir(parents=True,exist_ok=True)
binary=root/'render.exe'
flags=['-DP4SDM_LINEAR_32BIT=1','-DP4SDM_LINEAR_MAGNITUDE=1','-DP4SDM_PCM_READ_CACHE=0']
subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-Itests/fx_stubs',*flags,
                'tests/m214_render.cpp','-o',str(binary)],check=True)
reference=root/'reference.exe'
subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-Itests/fx_stubs',
                '-DP4SDM_LINEAR_32BIT=0','-DP4SDM_LINEAR_MAGNITUDE=0',
                'tests/m214_render.cpp','-o',str(reference)],check=True)
rate=44100; t=np.arange(rate)/rate; rng=np.random.default_rng(214)
attack=np.minimum(t/.005,1)
kick=np.sin(2*np.pi*(45*t+3.2*(1-np.exp(-t/.04))))*np.exp(-t/.14)*.7
snare=(rng.normal(0,.25,len(t))+np.sin(2*np.pi*180*t)*.18)*np.exp(-t/.07)*attack
noise=rng.normal(0,.15,len(t)); hat=(noise-np.roll(noise,1))*np.exp(-t/.025)*attack
melodic=sum(np.sin(2*np.pi*220*k*t)/k for k in range(1,15))*.22*attack*np.exp(-t/1.2)
tone=(np.sin(2*np.pi*440*t)+.2*np.sin(2*np.pi*880*t))*.55*attack
# A vowel/formant proxy, explicitly synthetic, not a recorded singer.
vocal=sum(np.sin(2*np.pi*120*k*t)*sum(np.exp(-((120*k-f)/bw)**2)
          for f,bw in [(700,100),(1200,150),(2500,200)]) for k in range(1,30))*.16*attack
phase=t%.25
loop=(np.sin(2*np.pi*70*t)*np.exp(-phase/.05)*.5+
      rng.normal(0,.15,len(t))*np.exp(-((t+.125)%.25)/.025))
sources=dict(kick=kick,snare=snare,hi_hat=hat,melodic=melodic,
             sustained_tone=tone,vocal_proxy=vocal,percussion_loop=loop)
rows=[]
for label,signal in sources.items():
    pcm=np.rint(np.clip(signal,-.9,.9)*32767).astype('<i2')
    source=root/(label+'.pcm'); pcm.tofile(source)
    source_hash=hashlib.sha256(pcm.tobytes()).hexdigest()
    for shift in (-12,-7,-1,0,1,7,12):
        pair=[]
        for mode,name in enumerate(('A','B')):
            raw=root/'output.pcm'
            subprocess.run([str(binary),str(source),str(raw),str(60+shift),str(mode)],check=True)
            y=np.fromfile(raw,dtype='<i2'); pair.append(y)
            filename=f'{label}_pitch_{shift:+03d}_{name}.wav'
            with wave.open(str(root/filename),'wb') as f:
                f.setparams((2,2,rate,0,'NONE','not compressed')); f.writeframes(y.tobytes())
            normalized=y.astype(float)/32768
            rms=float(np.sqrt(np.mean(normalized**2))); peak=float(np.max(np.abs(normalized)))
            rows.append(dict(file=filename,source_sha256=source_hash,pitch_semitones=shift,
                             mode='Nearest' if mode==0 else 'Linear32 magnitude',
                             integrated_rms_dbfs=20*np.log10(max(rms,1e-15)),
                             pcm_peak_dbfs=20*np.log10(max(peak,1e-15)),
                             clipped_samples=int(np.sum((y==-32768)|(y==32767)))))
        if shift==0: assert np.array_equal(*pair),'Unity A/B must be exact'
        subprocess.run([str(reference),str(source),str(root/'reference.pcm'),str(60+shift),'1'],check=True)
        assert np.array_equal(pair[1],np.fromfile(root/'reference.pcm',dtype='<i2')),'Linear32/64 component output differs'
        difference=pair[1].astype(float)-pair[0].astype(float)
        rows[-1]['pair_difference_rms_pcm']=float(np.sqrt(np.mean(difference**2)))
        for y,row in zip(pair,rows[-2:]):
            row['first_50ms_peak_pcm']=int(np.max(np.abs(y[:4410].astype(np.int32))))
manifest=dict(status='HUMAN LISTENING PENDING',synthetic=True,rate=rate,channels=2,
              processing='Actual Voice/Q16/fades, velocity 100, default filter, Delay time 12000/feedback 120/input 160/send 32; wet gain 1/4, common output gain 1/2',
              schedule='3 seconds: OneShot at 0 and 70560; reverse Gate slice 8192..57344 at 33075, release at 50715; default 32-frame fades',
              metrics_note='Integrated full-file RMS dBFS and PCM sample peak; not LUFS or reconstructed true peak',rows=rows)
(root/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(root/'README.md').write_text('''# M21.4 listening package

All seven source types are synthetic, including the vocal proxy. A is Nearest;
B is optimized Linear32 magnitude. Compare matching source/pitch pairs at a
fixed playback volume. No independent normalization. Processing and exact
sample timing are in manifest.json. Unity pairs are byte-identical PCM.

Listen for stepping, tonal clarity, hi-hat texture, transient strength, slice
edges, reverse/release clicks and Delay tails. At upward pitches both algorithms
can alias: Linear is not an antialias resampler. A smaller RMS is not acceptance.

For each pair, report preference A/B/no preference, transient or tonal defects,
pitch, source and playback system. Human approval remains PENDING until explicit
feedback is supplied. These component renders do not certify full-device sound.
''')
with zipfile.ZipFile(root/'m214-listening.zip','w',zipfile.ZIP_DEFLATED) as z:
    for file in sorted(root.glob('*.wav')): z.write(file,file.name)
    for name in ('README.md','manifest.json'): z.write(root/name,name)
Path('docs/m21/m214_audio_metrics.json').write_text(json.dumps(manifest,indent=2)+'\n')
if sys.platform!='win32':
    sanitized=root/'sanitized.exe'
    subprocess.run(['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer','-Itests/fx_stubs',*flags,
                    'tests/m214_render.cpp','-o',str(sanitized)],check=True)
    for mode in (0,1):
        subprocess.run([str(sanitized),str(root/'kick.pcm'),str(root/'sanitized.pcm'),'67',str(mode)],
                       env={**os.environ,'UBSAN_OPTIONS':'halt_on_error=1'},check=True)
print('98 paired WAVs, exact unity controls, fixed gain; human listening PENDING',root)
