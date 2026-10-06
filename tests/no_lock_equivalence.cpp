#include "../src/app/model.h"
#include "m9_model.h"
#include <cassert>
#include <iostream>
#include <utility>
#include <vector>
int main() {
  for (unsigned seed : {1u, 0x4d385031u, 0xffffffffu})
    for (int swing : {50, 60, 75}) {
      app::Engine now;
      m9::Engine old;
      now.seed = old.seed = seed;
      now.swing = old.swing = swing;
      now.bpm = old.bpm = 240;
      for (int p = 0; p < 3; ++p) {
        now.patterns[p].length = old.patterns[p].length =
            uint8_t(p == 1 ? 7 : 16);
        for (int t = 0; t < 16; ++t) {
          now.patterns[p].track_steps[t] = old.patterns[p].track_steps[t] =
              uint16_t(0x9563 + t * 977);
          for (int i = 0; i < 16; ++i) {
            now.patterns[p].meta[t][i] = {uint8_t(1 + (i * 23 + t * 11) % 127),
                                          uint8_t((i * 17 + t * 13) % 101),
                                          uint8_t(1 + (i + t) % 4)};
            auto m = now.patterns[p].meta[t][i];
            old.patterns[p].meta[t][i] = {m.velocity, m.probability,
                                          m.ratchets};
          }
        }
      }
      now.apply({app::Kind::Play, 0, 1});
      old.apply({m9::Kind::Play, 0, 1});
      for (int i = 0; i < 400000; ++i) {
        if (i == 50000 || i == 170000) {
          int p = i == 50000 ? 1 : 2;
          now.apply({app::Kind::SelectPattern, 0, p});
          old.apply({m9::Kind::SelectPattern, 0, p});
        }
        if (i == 80000 || i == 130000) {
          int mute = i == 80000;
          now.apply({app::Kind::Mute, 3, mute});
          old.apply({m9::Kind::Mute, 3, mute});
        }
        if (i == 90000 || i == 140000) {
          int solo = i == 90000;
          now.apply({app::Kind::Solo, 6, solo});
          old.apply({m9::Kind::Solo, 6, solo});
        }
        std::vector<std::pair<int, int>> a, b;
        now.sample([&](app::TriggerEvent ev) {
          assert(!ev.locked_mask);
          a.emplace_back(ev.track, ev.velocity);
        });
        old.sample([&](m9::TriggerEvent ev) {
          b.emplace_back(ev.track, ev.velocity);
        });
        assert(a == b && now.rng == old.rng && now.phase == old.phase &&
               now.step == old.step);
        assert(now.playing_pattern == old.playing_pattern &&
               now.ratchet_events == old.ratchet_events &&
               now.probability_skipped == old.probability_skipped);
      }
    }
  std::cout << "M10 vs actual M9 no-lock sample-by-sample equivalence: PASS; M9 Engine="
            << sizeof(m9::Engine) << " M10 Engine=" << sizeof(app::Engine) << "\n";
}
