#pragma once
#include <cstdint>
namespace sampler {
enum class Mode : uint8_t { OneShot, Gate };
// Canonical Track/accepted-parent state; never stores PCM ownership.
struct Playback {
  uint16_t start = 0, end = 65535;
  bool reverse = false;
  Mode mode = Mode::OneShot;
  uint8_t choke = 0, reserved = 0;
};
struct Region {
  uint32_t start = 0, end = 0; // [start,end), in decoded source frames
};
inline Region resolve_region(Playback p, uint32_t frames) {
  if (!frames)
    return {};
  uint32_t start = uint64_t(p.start) * frames / 65535;
  uint32_t end = uint64_t(p.end) * frames / 65535;
  // Nearest neighbor safely supports a single frame. Repair arbitrary state.
  if (start >= frames)
    start = frames - 1;
  if (end <= start)
    end = start + 1;
  return {start, end};
}
constexpr unsigned boundary_fade = 32;
} // namespace sampler
