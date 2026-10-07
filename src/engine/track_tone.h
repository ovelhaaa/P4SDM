#pragma once
#include <cstdint>
namespace p4tone {
constexpr int bounded(int v) { return v < 0 ? 0 : v > 127 ? 127 : v; }
// Arduino map uses signed division toward zero, not unsigned floor.
constexpr uint8_t cutoff(int v) {
  return uint8_t(255 - bounded(v) * 255 / 127);
}
// Preserve the accepted open/default pair exactly. Edited tone uses q <= 160.
constexpr uint8_t resonance(int cut, int res) {
  return cut == 0 && res == 0 ? 255 : uint8_t(bounded(res) * 160 / 127);
}
constexpr int cutoff_from_slider(int position) {
  return 127 - bounded(position);
}
// Post-pan channel input is bounded to +/-65535; product <= 8322945.
// Full send is an exact identity, zero is exact silence, signed truncation.
// Trigger/update boundaries guarantee amount <=127. The hot path needs no
// second clamp or lock/Track lookup.
inline int32_t resolved_delay_send(int32_t sample, uint8_t amount) {
  return amount == 127 ? sample : sample * amount / 127;
}
inline int32_t delay_send(int32_t sample, int amount) {
  return amount == 127 ? sample : sample * bounded(amount) / 127;
}
} // namespace p4tone
