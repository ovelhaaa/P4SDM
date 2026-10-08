#pragma once
#include "wav.h"
#include "esp_timer.h"

// Startup only: identical read count, real resident PCM, no live Voice mutation.
// Results isolate access order/locality; they do not qualify the audio engine.
inline void qualify_pcm_locality(sampler::Sample *samples) {
  alignas(4) int16_t scratch[16][32];
  for (unsigned scenario = 0; scenario < 7; ++scenario) {
    uint32_t worst = 0, checksum = 0;
    uint64_t total = 0;
    for (unsigned block = 0; block < 128; ++block) {
      const unsigned base = (block * 256) % 32768;
      const auto start = esp_timer_get_time();
      if (scenario == 5) {
        for (unsigned t = 0; t < 16; ++t) {
          const volatile int16_t *pcm = samples[t].data;
          for (unsigned n = 0; n < 256; ++n)
            checksum += uint32_t(int32_t(pcm[base+n]));
        }
      } else if (scenario == 6) {
        for (unsigned chunk = 0; chunk < 256; chunk += 32) {
          for (unsigned t = 0; t < 16; ++t) {
            const volatile int16_t *pcm = samples[t].data;
            for (unsigned n = 0; n < 32; ++n)
              scratch[t][n] = pcm[base+chunk+n];
          }
          for (unsigned n = 0; n < 32; ++n)
            for (unsigned t = 0; t < 16; ++t)
              checksum += uint32_t(int32_t(scratch[t][n]));
        }
      } else {
        for (unsigned n = 0; n < 256; ++n)
          for (unsigned t = 0; t < 16; ++t) {
            const volatile int16_t *pcm = samples[scenario == 1 ? 0 : t].data;
            unsigned k = base + (scenario == 2 ? 255-n : scenario == 3 ? n*16 : n);
            if (scenario == 4) k += t*37;
            checksum += uint32_t(int32_t(pcm[k]));
          }
      }
      const uint32_t us = uint32_t(esp_timer_get_time()-start);
      total += us;
      if (us > worst) worst = us;
      vTaskDelay(1);
    }
    Serial.printf("[M211 locality] scenario=%u blocks=128 reads_per_block=4096 max=%u mean=%u checksum=%lu scratch_bytes=%u\n",
                  scenario, worst, unsigned(total/128), (unsigned long)checksum,
                  unsigned(sizeof(scratch)));
  }
}
