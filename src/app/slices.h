#pragma once
#include "sample_playback.h"
#include <algorithm>
#include <type_traits>
namespace sampler {
struct Slice {
  uint16_t start = 0, end = 65535;
};
// Absolute normalized sample coordinates; independent regions and stable
// indices. selected is the Track's audible default. UI inspection is separate.
struct SliceBank {
  Slice slices[16]{};
  uint8_t count = 1, selected = 0;
  unsigned index(int requested = -1) const {
    const unsigned n = std::max(1u, std::min(16u, unsigned(count)));
    return std::min(n - 1,
                    requested < 0 ? unsigned(selected) : unsigned(requested));
  }
  void reset(Playback domain) {
    if (domain.start == 65535)
      domain.start = 65534;
    if (domain.end <= domain.start)
      domain.end = domain.start + 1;
    slices[0] = {domain.start, domain.end};
    count = 1;
    selected = 0;
  }
  bool divide(Playback domain, unsigned n) {
    if (n != 2 && n != 4 && n != 8 && n != 16)
      return false;
    if (domain.end <= domain.start || unsigned(domain.end - domain.start) < n)
      return false;
    for (unsigned i = 0; i < n; ++i)
      slices[i] = {
          uint16_t(domain.start + uint32_t(domain.end - domain.start) * i / n),
          uint16_t(domain.start +
                   uint32_t(domain.end - domain.start) * (i + 1) / n)};
    count = uint8_t(n);
    selected = uint8_t(index());
    return true;
  }
  bool add(unsigned inspected) {
    if (count < 1 || count >= 16 || inspected >= count)
      return false;
    auto old = slices[inspected];
    if (old.end <= old.start || old.end - old.start < 2)
      return false;
    const uint16_t middle = uint16_t(old.start + (old.end - old.start) / 2);
    for (unsigned i = count; i > inspected + 1; --i)
      slices[i] = slices[i - 1];
    slices[inspected] = {old.start, middle};
    slices[inspected + 1] = {middle, old.end};
    if (selected > inspected)
      ++selected; // retain the identity of shifted slots
    ++count;
    selected = uint8_t(index());
    return true;
  }
  bool erase(unsigned inspected) {
    if (count <= 1 || count > 16 || inspected >= count)
      return false;
    for (unsigned i = inspected; i + 1 < count; ++i)
      slices[i] = slices[i + 1];
    --count;
    if (selected > inspected)
      --selected;
    selected = uint8_t(index());
    return true;
  }
  void edit(unsigned inspected, bool start, int value) {
    if (inspected >= std::min(16u, unsigned(count)))
      return;
    auto &s = slices[inspected];
    const int v = std::max(0, std::min(65535, value));
    if (start)
      s.start = uint16_t(std::min(v, std::max(0, int(s.end) - 1)));
    else
      s.end = uint16_t(std::max(v, std::min(65535, int(s.start) + 1)));
  }
  Playback resolve(Playback base, int requested = -1) const {
    const auto s = slices[index(requested)];
    base.start = s.start;
    base.end = s.end;
    return base; // resolve_region remains the defensive PCM boundary
  }
};
static_assert(sizeof(Slice) == 4 && sizeof(SliceBank) == 66);
static_assert(std::is_trivially_copyable<SliceBank>::value);
} // namespace sampler
