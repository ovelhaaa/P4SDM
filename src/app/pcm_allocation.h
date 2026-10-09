#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace sampler {
// Storage/startup only. Keep the heap owner distinct from the PCM view.
// Policy 0 retains ordinary placement, 1 aligns then staggers by one L2 line
// per Track, 2 aligns without staggering (experimental control).
struct PcmAllocation {
  void *base = nullptr;
  int16_t *data = nullptr;
  size_t bytes = 0;
  static constexpr size_t padding(unsigned policy, unsigned track) {
    return policy == 0 ? 0 : 63 + (policy == 1 ? track * 64 : 0);
  }
  static bool request(size_t pcm_bytes, unsigned policy, unsigned track,
                      size_t &total) {
    total = 0;
    if (!pcm_bytes || (pcm_bytes & 1) || policy > 2 || track >= 16) return false;
    const size_t extra = padding(policy, track);
    if (pcm_bytes > std::numeric_limits<size_t>::max() - extra) return false;
    total = pcm_bytes + extra;
    return true;
  }
  template<class Allocate> bool acquire(size_t pcm_bytes, unsigned policy,
                                        unsigned track, Allocate allocate) {
    if (base) return false;
    size_t total;
    if (!request(pcm_bytes, policy, track, total)) return false;
    void *owner = allocate(total);
    if (!owner) return false;
    uintptr_t address = reinterpret_cast<uintptr_t>(owner);
    const size_t offset = policy == 0 ? 0 :
      size_t((64 - (address & 63)) & 63) + (policy == 1 ? track * 64 : 0);
    base = owner;
    data = reinterpret_cast<int16_t *>(address + offset);
    bytes = total;
    return true;
  }
  template<class Free> void release(Free free) {
    if (base) free(base);
    base = nullptr; data = nullptr; bytes = 0;
  }
  PcmAllocation() = default;
  PcmAllocation(const PcmAllocation &) = delete;
  PcmAllocation &operator=(const PcmAllocation &) = delete;
};
} // namespace sampler
