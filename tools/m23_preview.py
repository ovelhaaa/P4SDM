"""Host listening mixes from the generated kits; these are NOT device captures."""
from pathlib import Path
import json,wave
import numpy as np
root=Path(__file__).resolve().parents[1];pack=root/'assets/m23'
kits=json.loads((pack/'kits.json').read_text());output=root/'.pio/m23-listening';output.mkdir(parents=True,exist_ok=True)
for label,kit in kits.items():
    bpm=100;step=round(44100*60/bpm/4);mix=np.zeros(step*64+44100)
    grid={0:[0,8],2:[4,12],4:[12],5:list(range(0,16,2)),6:[14],7:[3,7,11,15],8:[10],9:[15],10:[6],12:[2,9],13:[0,7,10],14:[2,6,10,14],15:[15]}
    for bar in range(4):
        for track,beats in grid.items():
            with wave.open(str(pack/kit[track]['filename']),'rb') as w:hit=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').astype(np.float64)/32768
            for beat in beats:
                offset=(bar*16+beat)*step;gain=.28 if track>=13 else .55 if track in (5,6,7) else .8
                gain*=.75 if beat%4 else 1
                n=min(len(hit),len(mix)-offset);mix[offset:offset+n]+=hit[:n]*gain
    mix*=min(1,.85/max(np.max(np.abs(mix)),1e-9));pcm=np.rint(mix*32767).astype('<i2')
    with wave.open(str(output/(label+'.wav')),'wb') as w:w.setnchannels(1);w.setsampwidth(2);w.setframerate(44100);w.writeframes(pcm.tobytes())
    print('Host listening preview:',output/(label+'.wav'))
