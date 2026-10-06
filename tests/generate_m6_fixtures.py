"""Generate public-domain synthetic M6 fixtures; output outside tracked source."""
import argparse, math, struct, wave
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
for name,frames,channels in [('short_mono',64,1),('medium_mono',44100,1),('long_mono',176400,1),('stereo',88200,2)]:
    with wave.open(str(a.output/(name+'.wav')),'wb') as f:
        f.setparams((channels,2,44100,frames,'NONE','not compressed'))
        pcm=bytearray()
        for i in range(frames):
            for c in range(channels):pcm.extend(struct.pack('<h',int(4000*math.sin(2*math.pi*(220+c*110)*i/44100))))
        f.writeframes(pcm)
b=(a.output/'short_mono.wav').read_bytes()
extra=b'JUNK'+struct.pack('<I',3)+b'abc\0'+b'LIST'+struct.pack('<I',4)+b'test'+b'PAD '+struct.pack('<I',1)+b'X\0'
x=b[:12]+extra+b[12:];x=x[:4]+struct.pack('<I',len(x)-8)+x[8:];(a.output/'padded_chunks.wav').write_bytes(x)
for name,offset,value in [('unsupported_depth',34,24),('unsupported_encoding',20,3)]:
    x=bytearray(b);struct.pack_into('<H',x,offset,value);(a.output/(name+'.wav')).write_bytes(x)
(a.output/'truncated.wav').write_bytes(b[:-3])
print('Generated 8 synthetic WAV fixtures in',a.output)
