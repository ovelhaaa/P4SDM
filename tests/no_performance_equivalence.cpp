#include "../src/app/model.h"
#include "m17_project.h"
#include "../src/app/project.h"
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
    for (int swing : {50, 60, 75}) for (bool chain : {false, true}) {
      app::Engine now;
      m17::Engine old;
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
      if (chain) {
        now.chain.length = old.chain.length = 3;
        now.chain.loop = old.chain.loop = true;
        now.mode = app::TransportMode::Chain;
        old.mode = m17::TransportMode::Chain;
        for (int i = 0; i < 3; ++i) {
          now.chain.entries[i] = {uint8_t(i), uint8_t(i + 1)};
          old.chain.entries[i] = {uint8_t(i), uint8_t(i + 1)};
        }
      }
      now.apply({app::Kind::Play, 0, 1});
      old.apply({m17::Kind::Play, 0, 1});
      for (int i = 0; i < 400000; ++i) {
        if (i == 50000 || i == 170000) {
          int p = i == 50000 ? 1 : 2;
          now.apply({app::Kind::SelectPattern, 0, p});
          old.apply({m17::Kind::SelectPattern, 0, p});
        }
        if (i == 80000 || i == 130000) {
          int v = i == 80000;
          now.apply({app::Kind::Mute, 3, v});
          old.apply({m17::Kind::Mute, 3, v});
        }
        if (i == 90000 || i == 140000) {
          int v = i == 90000;
          now.apply({app::Kind::Solo, 6, v});
          old.apply({m17::Kind::Solo, 6, v});
        }
        std::vector<std::pair<Event, bool>> a, b;
        now.sample(
            [&](app::TriggerEvent e, bool r) { a.push_back({key(e), r}); });
        old.sample(
            [&](m17::TriggerEvent e, bool r) { b.push_back({key(e), r}); });
        assert(a == b && now.rng == old.rng && now.phase == old.phase &&
               now.step == old.step && now.playing_pattern == old.playing_pattern &&
               now.queued_pattern == old.queued_pattern && now.chain_entry == old.chain_entry &&
               now.chain_repeat == old.chain_repeat && now.playing == old.playing &&
               now.ratchet_events == old.ratchet_events &&
               now.probability_skipped == old.probability_skipped);
      }
      // Encode actual M17 canonical state and compare every V2 byte while all
      // runtime performance fields are populated on the M18 side.
      now.performance = {0xffff, 0xaaaa, 8, 8, 9, 10, 2, true};
      project::State a; legacy::State b;
      for (unsigned part = 0; part <= 16; ++part) {
        project::snapshot_part(now, a, part);
        legacy::snapshot_part(old, b, part);
      }
      uint8_t ba[project::file_bytes], bb[legacy::file_bytes];
      static_assert(project::version == legacy::version && project::file_bytes == legacy::file_bytes);
      assert(project::encode(a, 17, ba, sizeof(ba)) && legacy::encode(b, 17, bb, sizeof(bb)));
      assert(!std::memcmp(ba, bb, sizeof(ba)));
    }
  std::cout << "Actual M17 inactive-performance PATTERN/CHAIN and V2 bytes PASS (7200000 samples, "
               "all eight locks)\n";
  std::cout << "Engine M17=" << sizeof(m17::Engine) << " M18=" << sizeof(app::Engine)
            << " Ui M17=" << sizeof(m17::Ui) << " M18=" << sizeof(app::Ui) << "\n";
}
