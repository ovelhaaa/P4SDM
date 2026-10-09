#include <Arduino.h>
#include "app/pcm_allocation.h"
#include "app/memory_audit.h"
#include "app/sample_layout_diagnostics.h"
namespace {
template<class... A> void report(const char *format,A... args) {
  char line[512];const int n=snprintf(line,sizeof(line),format,args...);
  for(int at=0;at<n && at<511;at+=32) {
    Serial.write(reinterpret_cast<const uint8_t *>(line+at),std::min(32,std::min(n,511)-at));
    vTaskDelay(1);
  }
}
void run(void *) {
  memory_audit();
  static sampler::Sample samples[16];
  const sampler::Sample *views[16];
  constexpr unsigned span=131072;
  for(unsigned layout=0;layout<5;++layout) {
    sampler::PcmAllocation allocations[16];
    void *slab=nullptr;
    if(layout<2) slab=heap_caps_aligned_alloc(64,16*span+16*64,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    bool ok=layout>=2 || slab;
    for(unsigned i=0;ok && i<16;++i) {
      unsigned t=layout==3 ? 15-i : i;
      if(layout>=2) ok=allocations[t].acquire(span,layout==4 ? 2 : 0,t,[](size_t n) {
        return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
      });
      samples[t].data=layout<2 ? reinterpret_cast<int16_t *>(static_cast<uint8_t *>(slab)+t*span+(layout==1 ? t*64 : 0)) : allocations[t].data;
      if(!ok) break;
      samples[t].frames=span/2; samples[t].allocation=layout<2 ? span : allocations[t].bytes;
      for(unsigned n=0;n<span/2;++n) samples[t].data[n]=int16_t(((t*(span/2)+n)%97)*160-7680);
      views[t]=&samples[t];
    }
    if(ok) {
      for(unsigned t=0;t<16;++t) sample_layout::address(t,samples[t],layout<2 ? slab : allocations[t].base,
        [](const char *format,auto... args) { report(format,args...); });
      ok=sample_layout::benchmark(views,layout,[](const char *format,auto... args) { report(format,args...); },false);
    }
    for(auto &a:allocations) a.release([](void *p) { heap_caps_free(p); });
    heap_caps_free(slab);
    if(!ok) { Serial.println("[M213 memory] FAIL allocation"); vTaskDelete(nullptr); return; }
  }
  Serial.println("[M213 memory] COMPLETE layouts=5 audio_started=0 display_started=0");
  Serial.flush(); vTaskDelete(nullptr);
}
}
void setup() { Serial.begin(115200); xTaskCreate(run,"m213_memory",7000,nullptr,1,nullptr); }
void loop() { delay(1000); }
