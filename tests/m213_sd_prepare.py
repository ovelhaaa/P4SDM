"""Create an ignored SD console, including storage-owned layout diagnostics.

No SD writes, no flash. No console code is added to the production UI.
"""
from pathlib import Path
import subprocess, sys, shutil
subprocess.run([sys.executable, 'tests/physical_sd/prepare.py'], check=True)
target = Path('.pio/m19p')
shutil.copytree('boards',target/'boards',dirs_exist_ok=True)
store = target/'src/app/qstore.inc'
s = store.read_text()
s = s.replace('  if(op==1) qseed();', '''  if(op==8) {
    const sampler::Sample *resident[16];
    for(unsigned t=0;t<16;++t) {
      resident[t]=owned[t];
      if(owned[t]) sample_layout::address(t,*owned[t],
#if P4SDM_M213_LAYOUT
        static_cast<LoadedSample *>(owned[t])->pcm.base,
#else
        owned[t]->data,
#endif
        [](const char *format, auto... args) { qprintf(format,args...); });
    }
    static unsigned tag=0;
    const bool ok=sample_layout::benchmark(resident,++tag,
      [](const char *format, auto... args) { qprintf(format,args...); });
    qprintf("[M213 physical] tag=%u complete=%u\\n",tag,unsigned(ok));
  }
  if(op==9) {
    const auto *before=owned[0]; target=0;
    bool accepted=load_name("m213_missing_qualification.wav");
    qprintf("[M213 missing] accepted=%u preserved=%u\\n",unsigned(accepted),unsigned(before==owned[0]));
  }
  static void *fragments[64]{};
  if(op==10 || op==12) {
    for(auto &p:fragments) { heap_caps_free(p); p=nullptr; }
    for(unsigned i=0;i<64;++i) {
      const size_t n=op==10 ? 65536 : 524288;
      const size_t free=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
      if(free < reserve+1048576+n) break;
      fragments[i]=heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
      if(!fragments[i]) break;
    }
    if(op==10) for(unsigned i=0;i<64;i+=2) { heap_caps_free(fragments[i]); fragments[i]=nullptr; }
    qheap(op==10 ? "fragmented" : "pressure");
  }
  if(op==13) for(auto &p:fragments) { heap_caps_free(p); p=nullptr; }
  if(op==1) qseed();''')
store.write_text(s)
# Keep diagnostic loader and UI report lines intact on native USB. These edits
# apply only to the ignored console copy; audio never takes this mutex.
store_text=store.read_text()
prefix=store_text.split('void qheap',1)[0]
prefix=prefix.replace('char line[512];', 'if(m213_console_mutex) xSemaphoreTake(m213_console_mutex,portMAX_DELAY);\n  char line[512];')
prefix=prefix.replace('at+=48','at+=32').replace('std::min(48,','std::min(32,')
prefix=prefix.replace('vTaskDelay(1);','vTaskDelay(std::max<TickType_t>(1,pdMS_TO_TICKS(4)));')
prefix=prefix.replace('\n}\n','\n  if(m213_console_mutex) xSemaphoreGive(m213_console_mutex);\n}\n')
store.write_text('void qheap'+store_text.split('void qheap',1)[1])
loader=target/'src/app/samples.cpp'
text=loader.read_text().replace('namespace samples {',
    '#include "freertos/semphr.h"\nextern SemaphoreHandle_t m213_console_mutex;\nnamespace samples {\n'+prefix,1)
loader.write_text(text.replace('Serial.printf(', 'qprintf('))
app=target/'src/app_main.cpp'
text=app.read_text().replace('namespace {\n// Native USB/JTAG',
    '#include "freertos/semphr.h"\nSemaphoreHandle_t m213_console_mutex=nullptr;\nnamespace {\n// Native USB/JTAG',1)
text=text.replace('void summary_printf(const char *format, ...) {',
    'void summary_printf(const char *format, ...) {\n  if(m213_console_mutex) xSemaphoreTake(m213_console_mutex,portMAX_DELAY);',1)
text=text.replace('}\napp::Queue<64> commands;',
    '  if(m213_console_mutex) xSemaphoreGive(m213_console_mutex);\n}\napp::Queue<64> commands;',1)
text=text.replace('  Serial.setTxBufferSize(2048);',
    '  m213_console_mutex=xSemaphoreCreateMutex();\n  if(!m213_console_mutex) { Serial.println("[M5 INIT FAIL] console mutex"); vTaskDelete(nullptr); return; }\n  Serial.setTxBufferSize(2048);',1)
app.write_text(text)
console=target/'src/qconsole.inc'
s=console.read_text().replace('else if(!strcmp(line,"STATE")) {', '''else if(!strcmp(line,"INDEX")) {
      char name[96];
      for(unsigned i=0;i<samples::count();++i) {
        samples::describe(i,name,sizeof(name));
        summary_printf("[M213 index] index=%u name=%s\\n",i,name);
      }
    } else if(!strcmp(line,"STATE")) {''')
console.write_text(s)
with (target/'platformio.ini').open('a') as f:
    for policy in (0,1,2):
        f.write(f'\n[env:guition_m213_sd_{policy}]\nextends = env:guition_app\n'
                f'build_flags = ${{env:guition_app.build_flags}} -DP4SDM_M213_DIAGNOSTICS=1 '
                f'-DP4SDM_M213_LAYOUT={policy} -DP4SDM_PCM_READ_CACHE=1 '
                '-DP4SDM_INTERPOLATION=1 -DP4SDM_LINEAR_32BIT=1 -DP4SDM_LINEAR_MAGNITUDE=1\n')
    f.write('\n[env:guition_m213_sd_direct_1]\nextends = env:guition_app\n'
            'build_flags = ${env:guition_app.build_flags} -DP4SDM_M213_DIAGNOSTICS=1 '
            '-DP4SDM_M213_LAYOUT=1 -DP4SDM_INTERPOLATION=1 -DP4SDM_LINEAR_32BIT=1 '
            '-DP4SDM_LINEAR_MAGNITUDE=1\n')
print('M213 SD console prepared; STORE 8 reads resident PCM only')
