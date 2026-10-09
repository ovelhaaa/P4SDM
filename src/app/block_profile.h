#pragma once
#include <cstdint>
namespace block_profile {
// Diagnostic only. Five bounded histograms, 100 us bins; no allocation/logging
// on audio. Quantiles are bin upper bounds, exact maxima are kept separately.
struct Metric {
  uint64_t total = 0;
  uint32_t count = 0, maximum = 0, bins[101]{};
  void add(uint32_t us) {
    total += us; ++count;
    if (us > maximum) maximum = us;
    ++bins[us/100 < 100 ? us/100 : 100];
  }
  uint32_t percentile(unsigned percent) const {
    const uint32_t rank = (count*percent+99)/100;
    uint32_t seen = 0;
    for (unsigned i=0; i<101; ++i) {
      seen += bins[i];
      if (seen >= rank) return i==100 ? maximum : (i+1)*100-1;
    }
    return maximum;
  }
};
struct Profile {
  Metric stages[5]{}; // control/preparation, render, bookkeeping, write, complete
  uint32_t stack_high_water = 0;
};
} // namespace block_profile
