#pragma once
#include "sample_playback.h"
#include <cstdint>
#include <cstring>

#ifndef P4SDM_PCM_CACHE_FRAMES
#define P4SDM_PCM_CACHE_FRAMES 32
#endif
#ifndef P4SDM_PCM_CACHE_FILL
#define P4SDM_PCM_CACHE_FILL 0
#endif
#ifndef P4SDM_PCM_CACHE_METRICS
#define P4SDM_PCM_CACHE_METRICS 0
#endif

namespace sampler {
// Audio-owned experimental locality candidate. Invalidate at every block,
// after transfers/project boundaries and before rendering. Only PCM is cached;
// events, positions, filters, sends and mixing retain their sample-major order.
struct PcmReadCache {
  static constexpr uint32_t capacity = P4SDM_PCM_CACHE_FRAMES;
  static_assert(capacity == 16 || capacity == 32 || capacity == 64 || capacity == 128);
  static_assert(P4SDM_PCM_CACHE_FILL >= 0 && P4SDM_PCM_CACHE_FILL <= 2);
  const int16_t *source = nullptr;
  Region bounds{};
  uint32_t first = 0, count = 0;
  int16_t values[capacity]{};
#if P4SDM_PCM_CACHE_METRICS
  uint32_t hits = 0, misses = 0, reads = 0;
#endif
  void invalidate() { count = 0; }
  inline __attribute__((always_inline)) int16_t read(
      const int16_t *pcm, Region region, uint32_t frame) {
    // Public diagnostic callers may supply an empty/invalid region. Voice still
    // validates the hot path before calling; silence must never touch PCM.
    if (!pcm || region.end <= region.start || frame < region.start || frame >= region.end) {
      invalidate();
      return 0;
    }
    return read_valid(pcm,region,frame);
  }
  // Only validated Voice/lookup taps may enter here. Keep the public read()
  // guard for diagnostics and invalid state; avoid repeating it for each tap.
  inline __attribute__((always_inline)) int16_t read_valid(
      const int16_t *pcm, Region region, uint32_t frame) {
    if (source != pcm || bounds.start != region.start || bounds.end != region.end ||
        frame < first || frame-first >= count) {
      source = pcm;
      bounds = region;
      first = frame & ~(capacity-1);
      if (first < region.start) first = region.start;
      count = region.end-first < capacity ? region.end-first : capacity;
#if P4SDM_PCM_CACHE_METRICS
      ++misses; reads += count;
#endif
      // Do not read outside the current slice, including short/reverse slices.
#if P4SDM_PCM_CACHE_FILL == 1
      std::memcpy(values, pcm+first, count*sizeof(int16_t));
#elif P4SDM_PCM_CACHE_FILL == 2
      uint32_t n = 0;
      for (; n+4 <= count; n+=4) {
        values[n] = pcm[first+n]; values[n+1] = pcm[first+n+1];
        values[n+2] = pcm[first+n+2]; values[n+3] = pcm[first+n+3];
      }
      for (; n < count; ++n) values[n] = pcm[first+n];
#else
      for (uint32_t n = 0; n < count; ++n) values[n] = pcm[first+n];
#endif
    } else {
#if P4SDM_PCM_CACHE_METRICS
      ++hits;
#endif
    }
    return values[frame-first];
  }
};
} // namespace sampler
