#include "../src/app/waveform.h"
#include <algorithm>
#include <cassert>
#include <vector>
using namespace sampler;
int main() {
  Waveform w;
  build_waveform(w, nullptr, 0);
  for (auto c : w.columns)
    assert(c.min == 0 && c.max == 0);
  for (unsigned n : {1u, 7u, 360u, 720u, 721u, 2097152u}) {
    std::vector<int16_t> pcm(n, 1234);
    build_waveform(w, pcm.data(), n);
    for (auto c : w.columns)
      assert(c.min == 1234 && c.max == 1234);
    for (unsigned mode = 0; mode < 3; ++mode) {
      for (unsigned i = 0; i < n; ++i)
        pcm[i] = mode == 0   ? (i == n / 2 ? 32767 : 0)
                 : mode == 1 ? (i & 1 ? -32768 : 32767)
                             : int16_t(i);
      unsigned yields = 0;
      build_waveform(w, pcm.data(), n, [&] { ++yields; });
      assert(yields == 360);
      for (unsigned i = 0; i < 360; ++i) {
        unsigned a = uint64_t(i) * n / 360,
                 b = std::max(a + 1, unsigned(uint64_t(i + 1) * n / 360));
        auto bounds = std::minmax_element(pcm.begin() + a, pcm.begin() + b);
        assert(w.columns[i].min == *bounds.first &&
               w.columns[i].max == *bounds.second);
      }
    }
  }
}
