#include <Arduino.h>
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "app/memory_audit.h"
#include <cstring>

namespace {
constexpr unsigned frames = 65536, span = frames*2, repeats = 32;
volatile uint32_t sink = 0;
alignas(64) int16_t scratch[16][128];
alignas(64) uint8_t internal[16384];
int16_t content(unsigned t, unsigned n) { return int16_t((n*7919+t*101)%65536-32768); }
void report(unsigned layout, unsigned order, unsigned cold, uint64_t us,
            uint32_t checksum, unsigned reads, unsigned bytes) {
  Serial.printf("[M212 bandwidth] layout=%u order=%u evicted=%u repeats=%u reads=%u bytes=%u us=%llu MBps=%.3f checksum=%lu\n",
    layout, order, cold, repeats, reads, bytes, (unsigned long long)us,
    double(bytes)/double(us), (unsigned long)checksum);
  delay(20);
}
void run(void *) {
  delay(1000);
  memory_audit();
  auto *evict = static_cast<uint32_t *>(heap_caps_malloc(1024*1024, MALLOC_CAP_SPIRAM));
  auto *base = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, 16*(span+1024), MALLOC_CAP_SPIRAM));
  if (!base || !evict || !esp_ptr_external_ram(base) || !esp_ptr_internal(internal)) {
    Serial.println("[M212 benchmark] FAIL allocation/location");
    heap_caps_free(base); heap_caps_free(evict); vTaskDelete(nullptr); return;
  }
  for (unsigned n=0; n<1024*1024/4; ++n) evict[n]=n;
  for (unsigned layout=0; layout<4; ++layout) {
    int16_t *pcm[16]{};
    for (unsigned t=0; t<16; ++t) {
      pcm[t] = layout==2 ? static_cast<int16_t *>(heap_caps_aligned_alloc(64,span,MALLOC_CAP_SPIRAM)) :
        layout==3 ? static_cast<int16_t *>(heap_caps_malloc(span, MALLOC_CAP_SPIRAM)) :
        reinterpret_cast<int16_t *>(base+t*span+(layout==1 ? t*64 : 0));
      if (!pcm[t]) { Serial.println("[M212 benchmark] FAIL separate allocation"); vTaskDelete(nullptr); return; }
      // Layout 2: separate 64-byte aligned; layout 3: realistic allocator.
      for (unsigned n=0; n<frames; ++n) pcm[t][n]=content(t,n);
      Serial.printf("[M212 address] layout=%u track=%u base=%p align64=%u\n",layout,t,pcm[t],unsigned(uintptr_t(pcm[t])%64));
      delay(2);
    }
    for (unsigned cold=0; cold<2; ++cold)
      for (unsigned order=0; order<5; ++order) {
        uint64_t elapsed=0; uint32_t checksum=0;
        // Same 4096 values/order indices; order changes only voice/gather traversal.
        for (unsigned rep=0; rep<repeats; ++rep) {
          if (cold) {
            const volatile uint32_t *p=evict;
            uint32_t sum=0; for (unsigned n=0;n<1024*1024/4;n+=16) sum+=p[n];
            sink=sum; // eviction attempt, not proof of cold cache
          }
          const auto start=esp_timer_get_time();
          if (order==0) {
            for(unsigned n=0;n<256;++n) for(unsigned t=0;t<16;++t)
              checksum+=uint32_t(int32_t(static_cast<volatile int16_t *>(pcm[t])[n]));
          } else if(order==1) {
            for(unsigned t=0;t<16;++t) for(unsigned n=0;n<256;++n)
              checksum+=uint32_t(int32_t(static_cast<volatile int16_t *>(pcm[t])[n]));
          } else {
            const unsigned size=32u<<(order-2);
            for(unsigned chunk=0;chunk<256;chunk+=size) {
              for(unsigned t=0;t<16;++t) for(unsigned n=0;n<size;++n)
                scratch[t][n]=static_cast<volatile int16_t *>(pcm[t])[chunk+n];
              for(unsigned n=0;n<size;++n) for(unsigned t=0;t<16;++t)
                checksum+=uint32_t(int32_t(scratch[t][n]));
            }
          }
          elapsed+=esp_timer_get_time()-start;
          vTaskDelay(1); // startup-only, outside measurement
        }
        sink=checksum; report(layout,order,cold,elapsed,checksum,4096*repeats,8192*repeats);
      }
    if(layout>=2) for(auto *p:pcm) heap_caps_free(p);
  }
  // Streaming working set greatly exceeds L2. memcpy includes CPU traffic;
  // transfer rate is useful effective throughput, not bus peak bandwidth.
  for(unsigned order=5;order<8;++order) {
    uint64_t elapsed=0; uint32_t checksum=0;
    for(unsigned rep=0;rep<repeats;++rep) {
      const auto start=esp_timer_get_time();
      for(unsigned offset=0;offset<16*span;offset+=sizeof(internal)) {
        if(order==5) std::memcpy(internal,base+offset,sizeof(internal));
        else if(order==6) std::memcpy(base+offset,internal,sizeof(internal));
        else { const volatile uint32_t *p=reinterpret_cast<uint32_t *>(base+offset);
          for(unsigned n=0;n<sizeof(internal)/4;++n) checksum+=p[n]; }
      }
      elapsed+=esp_timer_get_time()-start; sink=checksum+internal[0]; vTaskDelay(1);
    }
    report(0,order,0,elapsed,checksum,16*span/(order==7 ? 4 : 2)*repeats,16*span*repeats);
  }
  // Isolated fill alternatives. Same interleaved scratch consumer and content;
  // these figures do not include Voice/cache lookup or qualify a renderer.
  for(unsigned size : {16u,32u,64u,128u}) for(unsigned fill=0;fill<3;++fill) {
    uint64_t elapsed=0; uint32_t checksum=0;
    for(unsigned rep=0;rep<repeats;++rep) {
      const auto start=esp_timer_get_time();
      for(unsigned chunk=0;chunk<256;chunk+=size) {
        for(unsigned t=0;t<16;++t) {
          const auto *p=reinterpret_cast<int16_t *>(base+t*span)+chunk;
          if(fill==1) std::memcpy(scratch[t],p,size*2);
          else if(fill==2) for(unsigned n=0;n<size;n+=4) {
            scratch[t][n]=p[n];scratch[t][n+1]=p[n+1];scratch[t][n+2]=p[n+2];scratch[t][n+3]=p[n+3];
          } else for(unsigned n=0;n<size;++n) scratch[t][n]=p[n];
        }
        for(unsigned n=0;n<size;++n) for(unsigned t=0;t<16;++t)
          checksum+=uint32_t(int32_t(scratch[t][n]));
      }
      elapsed+=esp_timer_get_time()-start; vTaskDelay(1);
    }
    sink=checksum;
    Serial.printf("[M212 fill] frames=%u fill=%u fills=%u reads=%u bytes=%u us=%llu checksum=%lu\n",
      size,fill,16*(256/size)*repeats,4096*repeats,8192*repeats,(unsigned long long)elapsed,(unsigned long)checksum);
    delay(20);
  }
  heap_caps_free(base); heap_caps_free(evict);
  Serial.println("[M212 benchmark] COMPLETE audio_started=0 display_started=0 cold_proven=0");
  Serial.flush(); delay(100);
  vTaskDelete(nullptr);
}
}
void setup() { Serial.begin(115200); xTaskCreate(run,"memory_qualification",6000,nullptr,1,nullptr); }
void loop() { delay(1000); }
