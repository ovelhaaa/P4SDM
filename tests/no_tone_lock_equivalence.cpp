#include "../src/app/model.h"
#include "m11_model.h"
#include <cassert>
#include <iostream>
#include <utility>
#include <vector>
int main() {
  for (unsigned seed : {1u, 0x4d385031u, 0xffffffffu})
    for (int swing : {50, 60, 75}) {
      app::Engine now;
      m11::Engine old;
      now.seed = old.seed = seed;
      now.swing = old.swing = swing;
      now.bpm = old.bpm = 240;
      for (int t = 0; t < 16; ++t) {
        now.tracks[t].filter_cutoff = old.tracks[t].filter_cutoff =
            uint8_t(t * 7);
        now.tracks[t].filter_resonance = old.tracks[t].filter_resonance =
            uint8_t(t * 8);
        now.tracks[t].delay_send = old.tracks[t].delay_send = uint8_t(t * 8);
      }
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
            now.patterns[p].locks[t][i] = {
                uint8_t((i + t) % 16), uint8_t((i * 7 + t) % 128),
                uint8_t((i + t * 5) % 128), int8_t((i * 13 + t) % 255 - 127),
                uint8_t((i + t) % 16)};
            const auto l = now.patterns[p].locks[t][i];
            old.patterns[p].locks[t][i] = {l.mask, l.pitch, l.volume, l.pan,
                                           l.wave};
            auto m = now.patterns[p].meta[t][i];
            old.patterns[p].meta[t][i] = {m.velocity, m.probability,
                                          m.ratchets};
          }
        }
      }
      now.apply({app::Kind::Play, 0, 1});
      old.apply({m11::Kind::Play, 0, 1});
      for (int i = 0; i < 400000; ++i) {
        if (i == 50000 || i == 170000) {
          int p = i == 50000 ? 1 : 2;
          now.apply({app::Kind::SelectPattern, 0, p});
          old.apply({m11::Kind::SelectPattern, 0, p});
        }
        if (i == 80000 || i == 130000) {
          int mute = i == 80000;
          now.apply({app::Kind::Mute, 3, mute});
          old.apply({m11::Kind::Mute, 3, mute});
        }
        if (i == 90000 || i == 140000) {
          int solo = i == 90000;
          now.apply({app::Kind::Solo, 6, solo});
          old.apply({m11::Kind::Solo, 6, solo});
        }
        std::vector<uint64_t> a, b;
        now.sample([&](app::TriggerEvent ev) {
          assert(ev.filter_cutoff == old.tracks[ev.track].filter_cutoff &&
                 ev.filter_resonance == old.tracks[ev.track].filter_resonance &&
                 ev.delay_send == old.tracks[ev.track].delay_send);
          a.push_back(ev.track | uint64_t(ev.velocity) << 8 |
                      uint64_t(ev.pitch) << 16 | uint64_t(ev.volume) << 24 |
                      uint64_t(uint8_t(ev.pan)) << 32 |
                      uint64_t(ev.wave) << 40 | uint64_t(ev.locked_mask) << 48);
        });
        old.sample([&](m11::TriggerEvent ev) {
          b.push_back(ev.track | uint64_t(ev.velocity) << 8 |
                      uint64_t(ev.pitch) << 16 | uint64_t(ev.volume) << 24 |
                      uint64_t(uint8_t(ev.pan)) << 32 |
                      uint64_t(ev.wave) << 40 | uint64_t(ev.locked_mask) << 48);
        });
        assert(a == b && now.rng == old.rng && now.phase == old.phase &&
               now.step == old.step);
        assert(now.playing_pattern == old.playing_pattern &&
               now.ratchet_events == old.ratchet_events &&
               now.probability_skipped == old.probability_skipped);
      }
    }
  std::cout << "M12 vs actual M11 no-tone-lock sample-by-sample equivalence: "
               "PASS; M11 Engine="
            << sizeof(m11::Engine) << " M12 Engine=" << sizeof(app::Engine)
            << "\n";
}
