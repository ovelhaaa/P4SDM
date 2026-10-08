#pragma once
#include "sample_playback.h"
#include <cstdint>

namespace sampler {
// Audio-owned experimental locality candidate. Invalidate at every block,
// after transfers/project boundaries and before rendering. Only PCM is cached;
// events, positions, filters, sends and mixing retain their sample-major order.
struct PcmReadCache {
  const int16_t *source = nullptr;
  Region bounds{};
  uint32_t first = 0, count = 0;
  int16_t values[32]{};
  void invalidate() { count = 0; }
  inline __attribute__((always_inline)) int16_t read(
      const int16_t *pcm, Region region, uint32_t frame) {
    if (source != pcm || bounds.start != region.start || bounds.end != region.end ||
        frame < first || frame-first >= count) {
      source = pcm;
      bounds = region;
      first = frame & ~uint32_t(31);
      if (first < region.start) first = region.start;
      count = region.end-first < 32 ? region.end-first : 32;
      // Do not read outside the current slice, including short/reverse slices.
      for (uint32_t n = 0; n < count; ++n) values[n] = pcm[first+n];
    }
    return values[frame-first];
  }
};
} // namespace sampler
