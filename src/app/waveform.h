#pragma once
#include <cstdint>
namespace sampler {
constexpr unsigned waveform_columns = 360; // two screen pixels per bin
struct WaveColumn {
  int16_t min = 0, max = 0;
};
struct Waveform {
  WaveColumn columns[waveform_columns]{};
};
// Storage/load context only. Yield once per bin for long resident samples.
template <class Yield>
void build_waveform(Waveform &out, const int16_t *pcm, uint32_t frames,
                    Yield yield) {
  for (unsigned i = 0; i < waveform_columns; ++i) {
    auto &c = out.columns[i];
    c = {};
    if (!pcm || !frames)
      continue;
    const uint32_t begin = uint64_t(i) * frames / waveform_columns;
    uint32_t end = uint64_t(i + 1) * frames / waveform_columns;
    if (end <= begin)
      end = begin + 1; // repeat source frame for short samples
    c.min = c.max = pcm[begin];
    for (uint32_t f = begin + 1; f < end; ++f) {
      if (pcm[f] < c.min)
        c.min = pcm[f];
      if (pcm[f] > c.max)
        c.max = pcm[f];
    }
    yield();
  }
}
inline void build_waveform(Waveform &out, const int16_t *pcm, uint32_t frames) {
  build_waveform(out, pcm, frames, [] {});
}
static_assert(sizeof(Waveform) == 1440);
} // namespace sampler
