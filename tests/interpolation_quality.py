"""Deterministic actual-C++ renderer versus a 384-tap Blackman windowed sinc.

No listening claims. Upward shifts use a pitch-dependent reference cutoff;
the production interpolators deliberately have no antialias filter.
"""
from pathlib import Path
import csv, json, subprocess, wave
import numpy as np
root=Path('.pio/m21-quality'); root.mkdir(parents=True,exist_ok=True)
report=Path('docs/m21'); report.mkdir(parents=True,exist_ok=True)
binary=root/'render.exe'
subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','tests/interpolation_render.cpp','-o',str(binary)],check=True)
rate=44100
names=['Nearest','Linear','Hermite4']
pitches={48:32768,53:43740,59:61857,60:65536,61:69433,67:98193,72:131072}
n=np.arange(8192); rng=np.random.default_rng(2101)
def sinc_reference(pcm, positions, ratio, radius=192):
    indices=np.floor(positions).astype(int)[:,None]+np.arange(1-radius,1+radius)
    distance=positions[:,None]-indices
    cutoff=min(1.0,1/ratio)
    window=np.where(np.abs(distance)<radius,.42+.5*np.cos(np.pi*distance/radius)+.08*np.cos(2*np.pi*distance/radius),0)
    kernel=cutoff*np.sinc(cutoff*distance)*window
    kernel/=kernel.sum(axis=1)[:,None]
    return np.sum(pcm[np.clip(indices,0,len(pcm)-1)]*kernel,axis=1)
def render(pcm,step,start,count=2048):
    pcm.astype('<i2').tofile(root/'input.pcm')
    subprocess.run([str(binary),str(root/'input.pcm'),str(root/'output.pcm'),str(step),str(start),str(count),'0'],check=True)
    return np.fromfile(root/'output.pcm',dtype='<i2').reshape(3,count).astype(float)
def tone_metrics(y, frequency):
    t=np.arange(len(y))/rate
    fit=np.column_stack([np.cos(2*np.pi*frequency*t),np.sin(2*np.pi*frequency*t),np.ones(len(y))])
    coeff=np.linalg.lstsq(fit,y,rcond=None)[0]
    residual=y-fit@coeff
    harmonics=[]
    for order in range(2,6):
        hz=frequency*order
        if hz<rate/2:
            harmonics.extend([np.cos(2*np.pi*hz*t),np.sin(2*np.pi*hz*t)])
    if harmonics:
        full=np.column_stack([fit,*harmonics])
        remaining=y-full@np.linalg.lstsq(full,y,rcond=None)[0]
        nonharmonic=float(np.mean(remaining**2))
    else: nonharmonic=float(np.mean(residual**2))
    spur=float(np.mean(residual**2))
    return float(np.hypot(*coeff[:2])),spur,max(0,spur-nonharmonic),nonharmonic
rows=[];convergence=[]
for frequency in (100,500,1000,3000,5000,10000,15000,18000):
    pcm=np.rint(16000*np.sin(2*np.pi*frequency*n/rate)).astype(np.int16)
    for pitch,step in pitches.items():
        ratio=step/65536; start=256*65536; positions=(start+np.arange(2048)*step)/65536
        ref=sinc_reference(pcm.astype(float),positions,ratio)
        longer=sinc_reference(pcm.astype(float),positions,ratio,radius=384)
        convergence.append(dict(frequency=frequency,pitch=pitch,rms_change=float(np.sqrt(np.mean((ref-longer)**2))),peak_change=float(np.max(np.abs(ref-longer)))))
        output=render(pcm,step,start)
        folded=abs((frequency*ratio+rate/2)%rate-rate/2)
        feasible=frequency*ratio<rate/2
        ref_amp,_,_,_=tone_metrics(ref,folded)
        for mode,y in enumerate(output):
            error=y-ref; amp,spur,harmonic,nonharmonic=tone_metrics(y,folded)
            rows.append(dict(fixture=f'sine_{frequency}',frequency=frequency,pitch=pitch,ratio=ratio,mode=names[mode],feasible=int(feasible),rms_error=float(np.sqrt(np.mean(error**2))),peak_error=float(np.max(np.abs(error))),amplitude_error_db=float(20*np.log10(max(amp,1e-12)/max(ref_amp,1e-12))),spurious_energy=spur,harmonic_energy=harmonic,nonharmonic_energy=nonharmonic,alias_tone_rms=0 if feasible else amp/np.sqrt(2)))
fixtures={
 'dc':np.full(8192,12000.),
 'impulse':np.where(n==768,32767.,0.),
 'step':np.where(n>=768,24000.,-24000.),
 'ramp':np.linspace(-30000,30000,8192),
 'chirp':16000*np.sin(2*np.pi*(100*n/rate+(18000-100)*n*n/(2*8192*rate))),
 'noise':rng.uniform(-24000,24000,8192),
 'clipped_burst':np.clip(50000*np.sin(2*np.pi*1000*n/rate)*np.exp(-np.maximum(n-512,0)/900),-32768,32767)*(n>=512),
 'smooth_transient':24000*np.exp(-((n-900)/100)**2)*np.sin(2*np.pi*500*n/rate),
 'alternating':np.where(n%2,32767.,-32768.),
}
for fixture,source in fixtures.items():
    pcm=np.rint(source).astype(np.int16)
    for pitch,step in pitches.items():
        ratio=step/65536; start=256*65536; positions=(start+np.arange(2048)*step)/65536
        ref=sinc_reference(pcm.astype(float),positions,ratio)
        for mode,y in enumerate(render(pcm,step,start)):
            error=y-ref
            rows.append(dict(fixture=fixture,frequency=0,pitch=pitch,ratio=ratio,mode=names[mode],feasible=1,rms_error=float(np.sqrt(np.mean(error**2))),peak_error=float(np.max(np.abs(error))),amplitude_error_db=0,spurious_energy=0,harmonic_energy=0,nonharmonic_energy=0,alias_tone_rms=0))
            if fixture in ('clipped_burst','smooth_transient','chirp') and pitch in (48,67):
                # Same PCM gain, controls, duration. These are generated audition
                # candidates; human preference has not been observed.
                with wave.open(str(root/f'{fixture}_p{pitch}_{names[mode]}.wav'),'wb') as f:
                    f.setparams((1,2,rate,0,'NONE','not compressed'));f.writeframes(y.astype('<i2').tobytes())
with (report/'quality.csv').open('w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
with (report/'reference_convergence.csv').open('w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=list(convergence[0]));writer.writeheader();writer.writerows(convergence)
# Full-scale overshoot sweep, before clamp and after actual renderer rounding.
overshoot=[]
phase=np.arange(65536)/65536
for label,taps in [('positive_plateau',(-32768,32767,32767,-32768)),('negative_plateau',(32767,-32768,-32768,32767)),('alternating',(-32768,32767,-32768,32767)),('step',(-32768,-32768,32767,32767))]:
    xm1,x0,x1,x2=taps;a=(-xm1+3*x0-3*x1+x2)/2;b=(2*xm1-5*x0+4*x1-x2)/2;c=(x1-xm1)/2
    pre=((a*phase+b)*phase+c)*phase+x0
    clipped=np.clip(pre,-32768,32767)
    overshoot.append(dict(fixture=label,pre_min=float(pre.min()),pre_max=float(pre.max()),saturated=int(np.count_nonzero(pre!=clipped)),clipping_rms=float(np.sqrt(np.mean((pre-clipped)**2)))))
(report/'overshoot.json').write_text(json.dumps(overshoot,indent=2)+'\n')
# Quality regressions on smooth low/mid-band tones; all unity paths are exact.
for r in rows:
    if r['pitch']==60: assert r['rms_error']<1e-7,r
for freq in (100,500,1000,3000):
    for pitch in (48,53,59,61,67):
        selected=[r for r in rows if r['frequency']==freq and r['pitch']==pitch]
        # At 100 Hz PCM16 quantization can make Linear/Hermite identical.
        assert selected[2]['rms_error'] <= selected[1]['rms_error']+1,selected
        assert selected[1]['rms_error'] < selected[0]['rms_error'],selected
lines=['# M21 objective comparison','', 'Actual C++ lookup; PCM16 sources; 2048 output frames starting at logical frame 256; Q16 ratios from unchanged pitch table. Reference: 384 taps, symmetric Blackman window, normalized DC gain, cutoff min(1,1/ratio). Edge replication. No voice fades in this reconstruction comparison. Full results: quality.csv. Reference sensitivity to doubling the kernel to 768 taps is recorded in reference_convergence.csv.','', 'Spurious energy is residual mean square after least-squares fundamental/DC fit. CSV separates harmonic energy (orders 2–5 below Nyquist) from remaining nonharmonic energy. Above-Nyquist cases report folded-tone RMS separately; their sinc reference removes source content above output Nyquist. None of the production methods is an antialias resampler.','', '| Source Hz | Pitch | Nearest RMS | Linear RMS | Hermite RMS |','|---:|---:|---:|---:|---:|']
for freq in (100,500,1000,3000,5000,10000,15000,18000):
    for pitch in pitches:
        selected=[r for r in rows if r['frequency']==freq and r['pitch']==pitch]
        lines.append(f'| {freq} | {pitch} | '+' | '.join(f"{r['rms_error']:.2f}" for r in selected)+' |')
lines+=['','Cubic overshoot (65,536 phases each):','', '| Fixture | Pre min | Pre max | Saturated phases | Clipping RMS |','|---|---:|---:|---:|---:|']
for r in overshoot: lines.append(f"| {r['fixture']} | {r['pre_min']:.2f} | {r['pre_max']:.2f} | {r['saturated']} | {r['clipping_rms']:.2f} |")
lines+=['',f"Reference 384→768 taps: worst sine RMS change {max(r['rms_change'] for r in convergence):.2f} PCM units; all per-case changes are in reference_convergence.csv.",'','Generated matched-control audition WAVs are in .pio/m21-quality (ignored). No actual listening or copyrighted musical recordings are claimed.']
(report/'QUALITY.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,axes=plt.subplots(1,2,figsize=(12,4))
    for ax,pitch in zip(axes,(48,67)):
        for mode in names:
            points=[r for r in rows if r['frequency'] and r['pitch']==pitch and r['mode']==mode]
            ax.semilogy([r['frequency'] for r in points],[max(r['rms_error'],.001) for r in points],marker='o',label=mode)
        ax.set(title=f'MIDI {pitch}',xlabel='Source frequency (Hz)',ylabel='RMS error (PCM units)');ax.grid(True);ax.legend()
    fig.tight_layout();fig.savefig(report/'error.png',dpi=150);plt.close(fig)
except ImportError: pass
print(f'M21 quality PASS: {len(rows)} rows, unity exact, low/mid-band ordering verified; upper band/alias/overshoot reported')
