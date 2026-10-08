"""Generated, level-matched musical proxies; preparation is not listening."""
from pathlib import Path
import subprocess, wave
import numpy as np
root=Path('.pio/m21-quality');root.mkdir(parents=True,exist_ok=True)
binary=root/'render.exe'
subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','tests/interpolation_render.cpp','-o',str(binary)],check=True)
rate=44100; n=np.arange(rate*3);time=n/rate;rng=np.random.default_rng(2101)
phase=time%0.5
kick=np.sin(2*np.pi*(45*phase+80*.04*(1-np.exp(-phase/.04))))*np.exp(-phase/.14)
snare=(rng.normal(0,.4,len(n))+np.sin(2*np.pi*180*time)*.25)*np.exp(-((time+.25)%.5)/.055)
hat=rng.normal(0,.12,len(n))*np.exp(-(time%.125)/.018)
melody=sum(np.sin(2*np.pi*220*k*time)/k for k in range(1,15))*.3
melody*=np.minimum(1,phase/.015)*np.exp(-phase/.8)
for label,source in [('percussion',kick*.7+snare+hat),('melodic',melody)]:
    pcm=np.rint(np.clip(source,-1,1)*26000).astype('<i2');pcm.tofile(root/'listen.pcm')
    for pitch,step in [(48,32768),(67,98193)]:
        count=(len(pcm)*65536-1)//step+1
        subprocess.run([str(binary),str(root/'listen.pcm'),str(root/'listen-output.pcm'),str(step),'0',str(count),'0'],check=True)
        output=np.fromfile(root/'listen-output.pcm',dtype='<i2').reshape(3,count).astype(float)
        fade=np.minimum(np.minimum(np.arange(count),np.arange(count)[::-1]),32)/32
        output*=fade
        rms=np.sqrt(np.mean(output**2,axis=1));peak=np.max(np.abs(output),axis=1)
        target=min(5000,float(np.min(30000*rms/peak)))
        for mode,name in enumerate(('Nearest','Linear','Hermite4')):
            y=np.rint(output[mode]*target/rms[mode]).astype('<i2')
            with wave.open(str(root/f'{label}_p{pitch}_{name}_matched.wav'),'wb') as f:
                f.setparams((1,2,rate,0,'NONE','not compressed'));f.writeframes(y.tobytes())
print('Prepared 12 level-matched generated musical proxies; human listening pending')
