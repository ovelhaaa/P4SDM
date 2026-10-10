"""Prepare diagnostic SD consoles with the exact final DSP/placement flags."""
from pathlib import Path
import shutil
import subprocess
import sys

subprocess.run([sys.executable,'tests/m213_sd_prepare.py'],check=True)
root=Path('.pio/m214-sd')
shutil.copytree('.pio/m19p',root,dirs_exist_ok=True,
                ignore=shutil.ignore_patterns('.pio'))
for file in (root/'src').rglob('*'):
    if file.is_file() and file.suffix in ('.h','.cpp','.inc'):
        text=file.read_text().replace('P4SDM_M213_LAYOUT','P4SDM_PCM_PLACEMENT')
        # Only a reserved M21.4 diagnostic project may be written.
        file.write_text(text.replace('M19P_SD_QUAL','M214_LAYOUT_QUAL'))
console=root/'src/qconsole.inc'
text=console.read_text()
text='#include "esp_system.h"\n'+text
text=text.replace('else if(!strcmp(line,"STATE")) {', '''else if(!strcmp(line,"POWER")) {
      const auto reason=esp_reset_reason();
      summary_printf("[M214 power] reset_reason=%u brownout=%u uptime_ms=%u\\n",
                     unsigned(reason),unsigned(reason==ESP_RST_BROWNOUT),millis());
    } else if(!strcmp(line,"STATE")) {''')
console.write_text(text)
loader=root/'src/app/samples.cpp'
text=loader.read_text().replace('namespace samples {',
    'namespace samples {\nbool m214_short_read=false, m214_allocation_failure=false;\n',1)
text=text.replace('file.read(chunk, n) != n',
                  'file.read(chunk, m214_short_read ? n-1 : n) != n')
text=text.replace('return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);',
                  'return m214_allocation_failure ? nullptr : heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);')
text=text.replace('(int16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);',
                  'm214_allocation_failure ? nullptr : (int16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);')
loader.write_text(text)
store=root/'src/app/qstore.inc'
text=store.read_text().replace('  if(op==1) qseed();', '''  if(op==14 || op==16 || op==17) {
    const auto *before=owned[0]; const size_t before_payload=payload;
    const size_t before_free=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    target=0;
    if(op==14) storage::unmount();
    m214_short_read=op==16; m214_allocation_failure=op==17;
    const bool accepted=load_name("m19p_mixed.wav");
    m214_short_read=m214_allocation_failure=false;
    qprintf("[M214 injected] op=%u accepted=%u preserved=%u payload_equal=%u free_equal=%u\\n",
            unsigned(op),unsigned(accepted),unsigned(before==owned[0]),
            unsigned(before_payload==payload),unsigned(before_free==heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    if(op==14) qprintf("[M214 remount] ok=%u\\n",unsigned(storage::mount()));
    else if(op==16) { storage::unmount(); qprintf("[M214 remount] ok=%u\\n",unsigned(storage::mount())); }
  }
  if(op==1) qseed();''')
store.write_text(text)
with (root/'platformio.ini').open('a') as f:
    for policy in (0,1):
        f.write(f'\n[env:guition_m214_sd_{policy}]\nextends = env:guition_app\n'
                'build_flags = ${env:guition_app.build_flags} -DP4SDM_M213_DIAGNOSTICS=1 '
                f'-DP4SDM_PCM_PLACEMENT={policy} -DP4SDM_INTERPOLATION=1 '
                '-DP4SDM_LINEAR_32BIT=1 -DP4SDM_LINEAR_MAGNITUDE=1 -DP4SDM_PCM_READ_CACHE=0\n')
print('M214 diagnostic SD consoles prepared; no flash or SD writes')
