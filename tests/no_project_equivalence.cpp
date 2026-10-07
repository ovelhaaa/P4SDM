#include "../src/app/model.h"
#include "m15_model.h"
#include <cassert>
#include <iostream>
#include <tuple>
#include <vector>
using Event = std::tuple<int, int, int, int, int, int, int, int, int, int, int,
                         int, int, int, int, bool>;
template <class T> Event key(T e) {
  return {e.track,
          e.velocity,
          e.pitch,
          e.volume,
          e.pan,
          e.wave,
          e.locked_mask,
          e.filter_cutoff,
          e.filter_resonance,
          e.delay_send,
          e.playback.start,
          e.playback.end,
          e.playback.reverse,
          int(e.playback.mode),
          e.slice,
          e.sequenced};
}
int main() {
  for (unsigned seed : {1u, 0x4d385031u, 0xffffffffu})
    for (int swing : {50, 60, 75}) {
      app::Engine now;
      m15::Engine old;
      now.seed = old.seed = seed;
      now.swing = old.swing = swing;
      now.bpm = old.bpm = 240;
      for (int t = 0; t < 16; ++t) {
        now.tracks[t].sample = old.tracks[t].sample = t % 3 != 0;
        now.tracks[t].slice_enabled = old.tracks[t].slice_enabled = t % 2;
        now.tracks[t].slices.divide({}, 16);
        old.tracks[t].slices.divide({}, 16);
        now.tracks[t].slices.selected = old.tracks[t].slices.selected = t;
        now.tracks[t].playback.reverse = old.tracks[t].playback.reverse = t % 2;
      }
      for (int p = 0; p < 3; ++p) {
        now.patterns[p].length = old.patterns[p].length = p == 1 ? 7 : 16;
        for (int t = 0; t < 16; ++t) {
          now.patterns[p].track_steps[t] = old.patterns[p].track_steps[t] =
              uint16_t(0x9563 + t * 977);
          for (int i = 0; i < 16; ++i) {
            auto &l = now.patterns[p].locks[t][i];
            l = {uint8_t((i * 13 + t * 17) & 255),
                 uint8_t(i * 7),
                 uint8_t(t * 7),
                 int8_t(i * 13 - 100),
                 uint8_t(i),
                 uint8_t(i * 8),
                 uint8_t(t * 8),
                 uint8_t(i * 7),
                 15};
            old.patterns[p].locks[t][i] = {
                l.mask, l.pitch,         l.volume,           l.pan,
                l.wave, l.filter_cutoff, l.filter_resonance, l.delay_send,
                l.slice};
            auto &m = now.patterns[p].meta[t][i];
            m = {uint8_t(1 + (i * 23 + t * 11) % 127),
                 uint8_t((i * 17 + t * 13) % 101), uint8_t(1 + (i + t) % 4)};
            old.patterns[p].meta[t][i] = {m.velocity, m.probability,
                                          m.ratchets};
          }
        }
      }
      now.apply({app::Kind::Play, 0, 1});
      old.apply({m15::Kind::Play, 0, 1});
      for (int i = 0; i < 400000; ++i) {
        if (i == 50000 || i == 170000) {
          int p = i == 50000 ? 1 : 2;
          now.apply({app::Kind::SelectPattern, 0, p});
          old.apply({m15::Kind::SelectPattern, 0, p});
        }
        if (i == 80000 || i == 130000) {
          int v = i == 80000;
          now.apply({app::Kind::Mute, 3, v});
          old.apply({m15::Kind::Mute, 3, v});
        }
        if (i == 90000 || i == 140000) {
          int v = i == 90000;
          now.apply({app::Kind::Solo, 6, v});
          old.apply({m15::Kind::Solo, 6, v});
        }
        std::vector<std::pair<Event, bool>> a, b;
        now.sample(
            [&](app::TriggerEvent e, bool r) { a.push_back({key(e), r}); });
        old.sample(
            [&](m15::TriggerEvent e, bool r) { b.push_back({key(e), r}); });
        assert(a == b && now.rng == old.rng && now.phase == old.phase &&
               now.step == old.step &&
               now.ratchet_events == old.ratchet_events &&
               now.probability_skipped == old.probability_skipped);
      }
    }
  std::cout << "Actual M15 idle-persistence equivalence PASS (3600000 samples, "
               "all eight locks)\n";
}
