#pragma once
#include <cstdint>
namespace sampler {
// Shared by the optional device runner and allocation-free host checks.
inline const char *qualification_fixture(unsigned step) {
  static constexpr const char *files[] = {
      "short_mono.wav", "stereo.wav", "long_mono.wav", "short_mono.wav",
      "unsupported_depth.wav", "truncated.wav"};
  return step < 6 ? files[step] : nullptr;
}
constexpr int qualification_pitch(unsigned phase) {
  return phase == 2 ? 48 : phase == 3 ? 72 : 60;
}
constexpr bool memory_loss_suspect(uint64_t before_free, uint64_t after_free,
                                   uint64_t before_largest, uint64_t after_largest) {
  return before_free > after_free + 4096 || before_largest > after_largest + 4096;
}
} // namespace sampler
