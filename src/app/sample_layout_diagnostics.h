#pragma once
#include "wav.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"

namespace sample_layout {
template<class Print> void address(unsigned track, const sampler::Sample &s,
                                   const void *base, Print print) {
  const uintptr_t p = reinterpret_cast<uintptr_t>(s.data);
  print("[M213 address] track=%u pcm=%p base=%p bytes=%u owner_usable_bytes=%u frames=%u mod64=%u mod128=%u mod256=%u mod4096=%u mod131072=%u external=%u requested_caps=%u free=%u largest=%u\n",
    track, s.data, base, s.allocation, unsigned(heap_caps_get_allocated_size(const_cast<void *>(base))),s.frames, unsigned(p%64), unsigned(p%128),
    unsigned(p%256), unsigned(p%4096), unsigned(p%131072),
    unsigned(esp_ptr_external_ram(s.data)), unsigned(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),
    unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
    unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
}
// Caller holds storage ownership of all samples for the entire operation.
// Audio can read them concurrently; it cannot retire them. No file contents
// are printed and no PCM is modified. Eviction is a sweep, not proven cold.
template<class Print> bool benchmark(const sampler::Sample *const *samples,
                                      unsigned tag, Print print, bool concurrent_audio=true) {
  for (unsigned t=0;t<16;++t)
    if (!samples[t] || !samples[t]->data || samples[t]->frames < 256) return false;
  auto *evict = static_cast<uint8_t *>(heap_caps_malloc(1024*1024, MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if (!evict) return false;
  memset(evict, 1, 1024*1024);
  alignas(4) int16_t scratch[16][32];
  uint32_t sweep = 0;
  for (unsigned cold=0;cold<2;++cold) for (unsigned order=0;order<3;++order) {
    uint64_t total=0; uint32_t worst=0, checksum=0;
    for (unsigned rep=0;rep<32;++rep) {
      if (cold) for (unsigned n=0;n<1024*1024;n+=64)
        sweep += static_cast<volatile uint8_t *>(evict)[n];
      const auto start=esp_timer_get_time();
      if (order==0) {
        for(unsigned n=0;n<256;++n) for(unsigned t=0;t<16;++t)
          checksum += uint32_t(int32_t(static_cast<volatile int16_t *>(samples[t]->data)[n]));
      } else if(order==1) {
        for(unsigned t=0;t<16;++t) for(unsigned n=0;n<256;++n)
          checksum += uint32_t(int32_t(static_cast<volatile int16_t *>(samples[t]->data)[n]));
      } else {
        for(unsigned chunk=0;chunk<256;chunk+=32) {
          for(unsigned t=0;t<16;++t) for(unsigned n=0;n<32;++n)
            scratch[t][n]=static_cast<volatile int16_t *>(samples[t]->data)[chunk+n];
          for(unsigned n=0;n<32;++n) for(unsigned t=0;t<16;++t)
            checksum += uint32_t(int32_t(scratch[t][n]));
        }
      }
      const uint32_t us=uint32_t(esp_timer_get_time()-start);
      total+=us; if(us>worst) worst=us;
      vTaskDelay(1);
    }
    print("[M213 locality] tag=%u order=%u eviction=%u repeats=32 reads=131072 us=%llu max=%u checksum=%u sweep=%u concurrent_audio=%u cold_proven=0\n",
      tag,order,cold,(unsigned long long)total,worst,checksum,sweep,unsigned(concurrent_audio));
  }
  heap_caps_free(evict);
  print("[M213 locality complete] tag=%u\n",tag);
  return true;
}
} // namespace sample_layout
