#pragma once
#include "wav.h"
#include "esp_timer.h"

// Diagnostic-only, runs before audio startup. Never changes the inherited
// sequencer workload. Real PSRAM, production compiler, all sixteen Voices.
template <sampler::Interpolation mode>
inline void qualify_interpolation_mode(sampler::Sample *samples, volatile int32_t &checksum) {
  sampler::Voice voices[16];
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
      uint32_t worst = 0;
      uint64_t total = 0;
      for (unsigned block = 0; block < 128; ++block) {
        for (unsigned t = 0; t < 16; ++t) {
          sampler::Playback p;
          p.reverse = scenario == 3 || (scenario >= 4 && (t & 1));
          p.mode = sampler::Mode::Gate;
          voices[t].assign(&samples[t]);
          voices[t].trigger(sampler::pitch_increment(scenario == 0 ? 60 :
              scenario == 2 ? 36 + int(t)*4 : 59), p);
          if (scenario == 4) {
            voices[t].region = {t*8, t*8+1+t%4};
            voices[t].set_increment(voices[t].increment);
          }
        }
        const auto start = esp_timer_get_time();
        for (unsigned n = 0; n < 256; ++n)
          for (unsigned t = 0; t < 16; ++t) {
            auto &v = voices[t];
            if (scenario == 4 && !v.active) {
              v.position = 0; v.active = true; v.attack = 0;
              v.set_increment(v.increment);
            }
            if (scenario == 5 && n == 64) v.release();
            checksum = checksum ^ v.next(mode);
          }
        const uint32_t us = uint32_t(esp_timer_get_time()-start);
        worst = us > worst ? us : worst;
        total += us;
        // Startup diagnostic is not a realtime task. Feed the idle watchdog
        // between, never inside, measured blocks.
        vTaskDelay(1);
      }
      Serial.printf("[M21 voices] mode=%u scenario=%u blocks=128 max=%u mean=%u voices=16 frames=256 checksum=%ld\n",
                    unsigned(mode), scenario, worst, unsigned(total/128), long(checksum));
    }
}
inline void qualify_interpolation(sampler::Sample *samples) {
  volatile int32_t checksum = 0;
  qualify_interpolation_mode<sampler::Interpolation::Nearest>(samples,checksum);
  qualify_interpolation_mode<sampler::Interpolation::Linear>(samples,checksum);
  qualify_interpolation_mode<sampler::Interpolation::Hermite4>(samples,checksum);
  // Arithmetic candidate comparison, including conversions/rounding, with
  // varying phases so the compiler cannot fold the polynomial to a constant.
  for (unsigned candidate = 0; candidate < 6; ++candidate) {
    const auto start = esp_timer_get_time();
    for (unsigned n = 0; n < 65536; ++n) {
      const uint16_t phase = uint16_t(n*97);
      // Vary taps as well as phase; a saturated constant plateau would allow
      // the compiler to eliminate much of a candidate's arithmetic.
      const unsigned k = n & 65532;
      const int16_t xm1 = samples[0].data[k], x0 = samples[0].data[k+1];
      const int16_t x1 = samples[0].data[k+2], x2 = samples[0].data[k+3];
      int16_t y = candidate == 0 ? sampler::linear_fixed(x0,x1,phase) :
          candidate == 1 ? sampler::quantize(sampler::linear_float(x0,x1,phase)) :
          candidate == 2 ? sampler::hermite_fixed(xm1,x0,x1,x2,phase) :
          candidate == 3 ? sampler::quantize(sampler::hermite_float(xm1,x0,x1,x2,phase)) :
          candidate == 4 ? sampler::linear_fixed32(x0,x1,phase) :
          sampler::linear_fixed32_magnitude(x0,x1,phase);
      checksum = checksum ^ y;
    }
    Serial.printf("[M21 arithmetic] candidate=%u iterations=65536 us=%u checksum=%ld\n",
                  candidate, unsigned(esp_timer_get_time()-start), long(checksum));
    vTaskDelay(1);
  }
}
