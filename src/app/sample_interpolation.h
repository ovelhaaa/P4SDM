#pragma once
#include "sample_playback.h"
#include "pcm_read_cache.h"
#include <cstdint>

#ifndef P4SDM_INTERPOLATION
#define P4SDM_INTERPOLATION 0
#endif
namespace sampler {
enum class Interpolation : uint8_t { Nearest, Linear, Hermite4 };
static_assert(P4SDM_INTERPOLATION >= 0 && P4SDM_INTERPOLATION <= 2);
constexpr Interpolation default_interpolation = Interpolation(P4SDM_INTERPOLATION);

// PCM units, round to nearest (ties away from zero), then saturate. All
// polynomial inputs are PCM16, so these bounded operations cannot be NaN/Inf.
inline int16_t quantize(float value) {
  if (value >= 32767.0f) return 32767;
  if (value <= -32768.0f) return -32768;
  return int16_t(int32_t(value >= 0 ? value + 0.5f : value - 0.5f));
}
inline float linear_float(int16_t x0, int16_t x1, uint16_t fraction) {
  return float(x0) + (float(x1) - float(x0)) * (float(fraction) / 65536.0f);
}
// Catmull-Rom: m0=(x1-xm1)/2, m1=(x2-x0)/2. Horner coefficients
// a=(-xm1+3*x0-3*x1+x2)/2, b=(2*xm1-5*x0+4*x1-x2)/2,
// c=(x1-xm1)/2, d=x0. Return unclamped PCM units for overshoot analysis.
inline float hermite_float(int16_t xm1, int16_t x0, int16_t x1,
                           int16_t x2, uint16_t fraction) {
  const float t = float(fraction) / 65536.0f;
  const float a = (-float(xm1) + 3.0f*x0 - 3.0f*x1 + x2) * 0.5f;
  const float b = (2.0f*xm1 - 5.0f*x0 + 4.0f*x1 - x2) * 0.5f;
  const float c = (float(x1) - xm1) * 0.5f;
  return ((a*t + b)*t + c)*t + x0;
}
inline int16_t quantize_q16(int64_t value) {
  const int64_t rounded = (value >= 0 ? value + 32768 : value - 32768) / 65536;
  return int16_t(rounded > 32767 ? 32767 : rounded < -32768 ? -32768 : rounded);
}
inline int16_t linear_fixed(int16_t x0, int16_t x1, uint16_t fraction) {
  return quantize_q16(int64_t(x0)*65536 + int64_t(int32_t(x1)-x0)*fraction);
}
// Convex weighted numerator: each signed product and their sum fit int32.
// Bounds are [-32768*65536,32767*65536]. Round using the signed remainder
// rather than adding +/-32768 to a possibly full-scale int32 numerator.
// This candidate preserves the old Q16/int64 kernel as an independent reference.
inline int16_t linear_fixed32(int16_t x0, int16_t x1, uint16_t fraction) {
  const int32_t value = int32_t(x0)*(65536-int32_t(fraction)) +
                        int32_t(x1)*int32_t(fraction);
  int32_t rounded = value/65536;
  const int32_t remainder = value%65536;
  if (remainder >= 32768) ++rounded;
  else if (remainder <= -32768) --rounded;
  return int16_t(rounded); // convex interpolation cannot exceed PCM16 rails
}
inline int16_t hermite_fixed(int16_t xm1, int16_t x0, int16_t x1,
                             int16_t x2, uint16_t fraction) {
  const int32_t a = -int32_t(xm1) + 3*int32_t(x0) - 3*int32_t(x1) + x2;
  const int32_t b = 2*int32_t(xm1) - 5*int32_t(x0) + 4*int32_t(x1) - x2;
  const int32_t c = int32_t(x1) - xm1;
  // Doubled coefficients; retain Q16 until the final rounding. Truncated
  // intermediate divisions introduce less than one PCM unit of error.
  int64_t value = int64_t(a)*fraction + int64_t(b)*65536;
  value = value*fraction/65536 + int64_t(c)*65536;
  value = value*fraction/65536 + int64_t(x0)*131072;
  return quantize_q16(value/2);
}

// Caller supplies a valid bounded region within the PCM allocation. Invalid
// coordinates return silence without touching PCM. Taps replicate region edges
// in logical playback order, never wrap into an adjacent slice.
inline __attribute__((always_inline)) int16_t interpolation_tap(
    const int16_t *pcm, Region region, bool reverse, uint32_t logical,
    PcmReadCache *cache = nullptr) {
  const uint32_t frame = reverse ? region.end-1-logical : region.start+logical;
  return cache ? cache->read(pcm, region, frame) : pcm[frame];
}
// Voice validates transport before entering this hot path. No duplicated
// allocation/region checks per output frame; ownership stays with Voice.
inline __attribute__((always_inline)) int16_t lookup_valid(const int16_t *pcm, Region region, bool reverse,
                      uint64_t position, Interpolation mode = default_interpolation,
                      PcmReadCache *cache = nullptr) {
  const uint32_t length = region.end - region.start;
  const uint32_t k = uint32_t(position >> 16);
  const int16_t x0 = interpolation_tap(pcm,region,reverse,k,cache);
  const uint16_t fraction = uint16_t(position);
  if (mode == Interpolation::Nearest || !fraction) return x0;
  const int16_t x1 = interpolation_tap(pcm,region,reverse,k < length-1 ? k+1 : k,cache);
  if (mode == Interpolation::Linear) {
#if P4SDM_LINEAR_32BIT
    return linear_fixed32(x0, x1, fraction);
#else
    return linear_fixed(x0, x1, fraction);
#endif
  }
  const int16_t xm1 = interpolation_tap(pcm,region,reverse,k ? k-1 : 0,cache);
  const int16_t x2 = interpolation_tap(pcm,region,reverse,length-1-k >= 2 ? k+2 : length-1,cache);
  return quantize(hermite_float(xm1, x0, x1, x2, fraction));
}
inline int16_t lookup(const int16_t *pcm, Region region, bool reverse,
                      uint64_t position, Interpolation mode = default_interpolation) {
  if (!pcm || region.end <= region.start || (position >> 16) >= region.end-region.start) return 0;
  return lookup_valid(pcm,region,reverse,position,mode);
}
} // namespace sampler
