"""Prepare inherited storage/audio diagnostic console with production M21.4 DSP.

The production loader, waveform/analyzer, queues and audio render are unchanged.
Only the serial observation/control console is present in the ignored copy.
"""
from pathlib import Path
import shutil,subprocess,sys
subprocess.run([sys.executable,'tests/m214_sd_prepare.py'],check=True)
target=Path('.pio/m23-loader')
shutil.copytree('.pio/m214-sd',target,dirs_exist_ok=True,ignore=shutil.ignore_patterns('.pio'))
console=target/'src/qconsole.inc'
s=console.read_text().replace('for(unsigned i=0;i<4;i++)','for(unsigned i=0;i<16;i++)')
s=s.replace('else if(sscanf(line,"LOAD %d %d",&t,&value)==2)', '''else if(sscanf(line,"TRACK %d",&t)==1 && t>=0 && t<16) {
      samples::Preview preview; samples::preview(t,preview);
      uint32_t hash=2166136261u; const auto *raw=reinterpret_cast<const uint8_t*>(&preview.waveform);
      for(unsigned j=0;j<sizeof(preview.waveform);++j) hash=(hash^raw[j])*16777619;
      summary_printf("[M23 TRACK] track=%u frames=%u waveform=%08x name=%s\\n",unsigned(t),preview.frames,hash,preview.name);
    } else if(sscanf(line,"LOAD %d %d",&t,&value)==2)''')
console.write_text(s)
with (target/'platformio.ini').open('a') as f:
    f.write('''
[env:guition_m23_loader]
extends = env:guition_app_base
build_flags = ${env:guition_app_base.build_flags} -DP4SDM_M213_DIAGNOSTICS=1 -DP4SDM_INTERPOLATION=1 -DP4SDM_LINEAR_32BIT=1 -DP4SDM_LINEAR_MAGNITUDE=1 -DP4SDM_PCM_READ_CACHE=0 -DP4SDM_PCM_PLACEMENT=1
''')
print('M23 ignored loader observation console prepared; no hardware write')
