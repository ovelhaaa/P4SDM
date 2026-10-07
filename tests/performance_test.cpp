#include "../src/app/project.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <tuple>
static bool realtime = false;
void *operator new(std::size_t n) {
  assert(!realtime);
  if (auto p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
using namespace app;
void tick(Engine &e) { e.sample([](TriggerEvent) {}); }
void wrap(Engine &e) {
  auto loops = e.loops;
  do { tick(e); } while (e.playing && e.loops == loops);
}
void play(Engine &e) { e.apply({Kind::Play, 0, 1}); tick(e); }
auto event_key(TriggerEvent e, bool ratchet) {
  return std::make_tuple(e.track, e.velocity, e.pitch, e.volume, e.pan,
                        e.wave, e.locked_mask, e.filter_cutoff,
                        e.filter_resonance, e.delay_send, e.playback.start,
                        e.playback.end, e.playback.reverse, e.playback.mode,
                        e.slice, e.sequenced, ratchet);
}
int main() {
  static Engine e;
  static Engine reference;
  static project::State canonical, saved, decoded;
  static uint8_t before[project::file_bytes], after[project::file_bytes];
  realtime = true;
  // Exhaust all local and other-track solo combinations, across all 16 Tracks.
  for (int t = 0; t < 16; ++t) for (int flags = 0; flags < 64; ++flags) {
    e = Engine{};
    auto bit = uint16_t(1u << t), other = uint16_t(1u << ((t + 1) % 16));
    e.tracks[t].muted = flags & 1;
    e.performance.mutes = flags & 2 ? bit : 0;
    e.solos = (flags & 4 ? bit : 0) | (flags & 16 ? other : 0);
    e.performance.solos = (flags & 8 ? bit : 0) | (flags & 32 ? other : 0);
    bool eligible = !(flags & 3) && (!(flags & 60) || (flags & 12));
    assert(e.sequence_enabled(t) == eligible);
  }
  for (int length : {1, 3, 7, 12, 16}) for (int swing : {50, 60, 75}) {
    e = Engine{};
    e.selected_pattern = 2;
    e.swing = swing;
    e.patterns[7].length = e.patterns[9].length = uint8_t(length);
    play(e);
    e.apply({Kind::PerfOverride, 0, 6});
    e.apply({Kind::PerfOverrideCancel, 0, 0});
    wrap(e);
    assert(e.playing_pattern == 2 && !e.performance.owns());
    e.apply({Kind::PerfOverride, 0, 6});
    e.apply({Kind::PerfOverride, 0, 7});
    assert(e.playing_pattern == 2);
    wrap(e);
    assert(e.playing_pattern == 7 && e.performance.return_pattern == 2);
    e.apply({Kind::SelectPattern, 0, 15});
    assert(e.selected_pattern == 15 && e.queued_pattern == -1);
    for (int n = 0; n < 3; ++n) { wrap(e); assert(e.playing_pattern == 7); }
    e.apply({Kind::PerfOverride, 0, 8});
    assert(e.playing_pattern == 7);
    wrap(e);
    assert(e.playing_pattern == 8 && e.performance.return_pattern == 2);
    e.apply({Kind::PerfFill, 0, 5});
    e.apply({Kind::PerfFill, 0, 9});
    wrap(e);
    assert(e.playing_pattern == 9 && e.performance.fill_active == 9);
    auto phase = e.phase;
    auto rng = e.rng;
    e.apply({Kind::PerfFill, 0, 10}); // Ignored while active.
    assert(e.performance.fill_pending == -1 && e.rng == rng && e.phase == phase);
    const auto fill_loop = e.loops;
    unsigned onsets = 0;
    while (e.loops == fill_loop) {
      onsets += e.first || e.phase >= e.duration;
      tick(e);
    }
    assert(onsets == unsigned(length));
    assert(e.playing_pattern == 8 && e.performance.fill_active == -1);
    e.apply({Kind::PerfFill, 0, 9});
    wrap(e);
    e.apply({Kind::PerfOverrideCancel, 0, 0});
    assert(e.playing_pattern == 9);
    wrap(e);
    assert(e.playing_pattern == 2 && !e.performance.owns());
    e.apply({Kind::PerfFill, 0, 9});
    e.apply({Kind::PerfFillCancel, 0, 0});
    wrap(e);
    assert(e.playing_pattern == 2 && !e.performance.owns());
    e.apply({Kind::PerfFill, 0, 9});
    wrap(e);
    e.apply({Kind::SelectPattern, 0, 14});
    wrap(e);
    assert(e.playing_pattern == 2 && e.selected_pattern == 14);
  }
  // Requested two-entry golden sequences have no duplicated/lost repeats.
  for (bool fill : {false, true}) {
    e = Engine{}; e.chain.length = 2; e.mode = TransportMode::Chain;
    e.chain.entries[0] = {0, 2}; e.chain.entries[1] = {1, 2};
    for (auto &p : e.patterns) p.length = 1;
    play(e); assert(e.playing_pattern == 0 && e.chain_repeat == 0);
    e.apply({fill ? Kind::PerfFill : Kind::PerfOverride, 0, fill ? 9 : 7});
    wrap(e); assert(e.playing_pattern == (fill ? 9 : 7) && e.chain_repeat == 1);
    if (!fill) {
      wrap(e); assert(e.playing_pattern == 7 && e.chain_repeat == 1);
      e.apply({Kind::PerfOverrideCancel, 0, 0});
    }
    wrap(e); assert(e.playing_pattern == 0 && e.chain_repeat == 1);
    wrap(e); assert(e.playing_pattern == 1 && e.chain_repeat == 0);
    wrap(e); assert(e.playing_pattern == 1 && e.chain_repeat == 1);
    wrap(e); assert(!e.playing);
  }
  // Exact next-loop context: every entry/repeat, with excursion at that loop.
  for (int repeats : {1, 2, 4, 16}) for (int entry = 0; entry < 3; ++entry)
    for (int repeat = 0; repeat < repeats; ++repeat) for (bool fill : {false, true}) {
      e = Engine{};
      e.chain.length = 3; e.chain.loop = true; e.mode = TransportMode::Chain;
      for (int p = 0; p < 16; ++p) e.patterns[p].length = 1;
      for (int i = 0; i < 3; ++i) e.chain.entries[i] = {uint8_t(i), uint8_t(repeats)};
      play(e);
      for (int i = 0; i < entry * repeats + repeat; ++i) wrap(e);
      assert(e.chain_entry == entry && e.chain_repeat == repeat);
      e.apply({fill ? Kind::PerfFill : Kind::PerfOverride, 0, 9});
      wrap(e);
      const int next_entry = repeat + 1 == repeats ? (entry + 1) % 3 : entry;
      const int next_repeat = (repeat + 1) % repeats;
      assert(e.chain_entry == next_entry && e.chain_repeat == next_repeat && e.playing_pattern == 9);
      if (!fill) {
        for (int n = 0; n < 3; ++n) {
          wrap(e);
          assert(e.chain_entry == next_entry && e.chain_repeat == next_repeat && e.playing_pattern == 9);
        }
        e.apply({Kind::PerfOverrideCancel, 0, 0});
      }
      wrap(e);
      assert(e.chain_entry == next_entry && e.chain_repeat == next_repeat && e.playing_pattern == next_entry);
      wrap(e);
      assert(e.chain_repeat == (next_repeat + 1) % repeats);
    }
  // Requests and accepted boundaries do not reseed the continuous RNG.
  e = Engine{}; e.patterns[0].length = 1; e.patterns[9].length = 1;
  play(e); e.rng = 0x12345678;
  e.apply({Kind::PerfOverride, 0, 9}); wrap(e);
  assert(e.rng == 0x12345678);
  e.apply({Kind::PerfFill, 0, 8}); wrap(e);
  assert(e.rng == 0x12345678);
  e.apply({Kind::PerfOverrideCancel, 0, 0}); wrap(e);
  assert(e.rng == 0x12345678);
  // Queue accepted before performance ownership is cleared at launch.
  e = Engine{}; e.patterns[0].length = 1;
  play(e); e.apply({Kind::SelectPattern, 0, 3});
  e.apply({Kind::PerfOverride, 0, 8}); wrap(e);
  assert(e.performance.return_pattern == 0 && e.queued_pattern == -1);
  e.apply({Kind::PerfOverrideCancel, 0, 0}); wrap(e);
  assert(e.playing_pattern == 0 && e.selected_pattern == 3);
  // Active excursions have exactly the same sample clock/events as the usual
  // Pattern queue following the same audible sequence, including odd lengths.
  for (int length : {1, 3, 7, 12, 16}) for (int swing : {50, 60, 75}) {
    e = Engine{}; e.bpm = 240; e.swing = swing;
    e.patterns[0].length = 3;
    e.patterns[7].length = uint8_t(length); e.patterns[9].length = 7;
    for (int p : {0, 7, 9}) for (int t = 0; t < 16; ++t) {
      e.patterns[p].track_steps[t] = 0xffff;
      for (int step = 0; step < 16; ++step) {
        e.patterns[p].meta[t][step] = {uint8_t(30 + step), 53, 4};
        e.patterns[p].locks[t][step] = {127, 71, 63, -20, 4, 83, 41, 27, 0};
      }
    }
    reference = e; play(e); play(reference);
    bool fill_queued_return = false;
    for (int i = 0; i < 200000; ++i) {
      if (i == 10000) {
        e.apply({Kind::PerfOverride, 0, 7});
        reference.apply({Kind::SelectPattern, 0, 7});
      }
      if (i == 60000) {
        e.apply({Kind::PerfFill, 0, 9});
        reference.apply({Kind::SelectPattern, 0, 9});
      }
      if (i == 120000) {
        e.apply({Kind::PerfOverrideCancel, 0, 0});
        reference.apply({Kind::SelectPattern, 0, 0});
      }
      decltype(event_key({}, false)) a[64], b[64];
      unsigned na = 0, nb = 0;
      e.sample([&](TriggerEvent ev, bool r) { assert(na < 64); a[na++] = event_key(ev, r); });
      reference.sample([&](TriggerEvent ev, bool r) { assert(nb < 64); b[nb++] = event_key(ev, r); });
      assert(na == nb);
      for (unsigned n = 0; n < na; ++n) assert(a[n] == b[n]);
      assert(e.phase == reference.phase && e.step == reference.step &&
             e.rng == reference.rng && e.playing_pattern == reference.playing_pattern);
      if (e.performance.fill_active >= 0 && !fill_queued_return) {
        reference.apply({Kind::SelectPattern, 0, 7});
        fill_queued_return = true;
      }
    }
    assert(fill_queued_return && e.perf_metrics.fill_completions == 1);
  }
  // Natural finite Chain STOP also leaves neutral performance controls.
  e = Engine{}; e.chain.length = 1; e.mode = TransportMode::Chain;
  e.patterns[0].length = 1;
  play(e); e.apply({Kind::PerfMute, 2, 1}); wrap(e);
  assert(!e.playing && !e.performance.mutes);
  // A final finite-chain excursion finishes, then stops without an extra loop.
  e = Engine{}; e.chain.length = 1; e.mode = TransportMode::Chain;
  e.patterns[0].length = 1; e.patterns[9].length = 3;
  play(e); e.apply({Kind::PerfFill, 0, 9}); wrap(e);
  assert(e.playing && e.performance.return_end);
  wrap(e); assert(!e.playing && !e.performance.requested());
  // All locks/slices still resolve through the usual parent/ratchet event path.
  e = Engine{}; e.patterns[0].length = 1; e.patterns[9].length = 1;
  e.tracks[0].assigned(); e.tracks[0].slices.divide({}, 16);
  e.patterns[9].track_steps[0] = 1;
  e.patterns[9].meta[0][0] = {77, 100, 4};
  e.patterns[9].locks[0][0] = {255, 71, 63, -20, 4, 83, 41, 27, 15};
  play(e); e.apply({Kind::PerfFill, 0, 9});
  unsigned emitted = 0, releases = 0;
  auto callback = [&](TriggerEvent ev, bool) {
    assert(ev.sequenced && ev.velocity == 77 && ev.pitch == 71 && ev.volume == 63 && ev.pan == -20);
    assert(ev.locked_mask == 247 && ev.filter_cutoff == 83 && ev.filter_resonance == 41 && ev.delay_send == 27 && ev.slice == 16);
    ++emitted;
  };
  auto loops = e.loops;
  while (e.loops == loops) e.sample(callback, [&](int) { ++releases; });
  loops = e.loops;
  while (e.loops == loops) e.sample(callback, [&](int) { ++releases; });
  assert(emitted == 4 && releases >= 32);
  // Suppression precedes RNG and lock resolution; audition remains independent.
  e.performance.mutes = 1; e.patterns[0] = e.patterns[9];
  e.patterns[0].meta[0][0].probability = 50;
  auto rng = e.rng; emitted = 0; wrap(e);
  assert(e.rng == rng && e.pending[0].next == e.pending[0].count);
  e.audition(0, [&](TriggerEvent) { ++emitted; }); assert(emitted == 1);
  // V2 byte identity before/during performance; load/new/STOP reset overlays.
  e = Engine{};
  for (unsigned i = 0; i <= 16; ++i) project::snapshot_part(e, canonical, i);
  assert(project::encode(canonical, 3, before, sizeof(before)));
  play(e); e.apply({Kind::PerfOverride, 0, 8}); wrap(e);
  e.apply({Kind::PerfFill, 0, 9}); wrap(e);
  e.apply({Kind::PerfMute, 3, 1}); e.apply({Kind::PerfSolo, 4, 1});
  for (unsigned i = 0; i <= 16; ++i) project::snapshot_part(e, saved, i);
  assert(project::encode(saved, 3, after, sizeof(after)));
  assert(!std::memcmp(before, after, sizeof(before)));
  assert(project::decode(after, sizeof(after), decoded) == project::Error::Ok);
  for (unsigned i = 0; i <= 16; ++i) project::apply_part(e, decoded, i);
  assert(!e.playing && !e.performance.requested() && !e.performance.mutes && !e.performance.solos);
  play(e); e.apply({Kind::PerfOverride, 0, 8}); wrap(e);
  e.apply({Kind::PerfMute, 3, 1}); e.apply({Kind::Play, 0, 0});
  assert(!e.performance.requested() && !e.performance.mutes);
  play(e); assert(e.playing_pattern == e.selected_pattern);
  project::defaults(decoded);
  e.apply({Kind::PerfSolo, 2, 1}); project::apply_part(e, decoded, 0);
  assert(!e.performance.solos && !e.playing);
  for (unsigned k = unsigned(Kind::PerfOverride); k <= unsigned(Kind::PerfClearMix); ++k)
    assert(!project::musical(Kind(k)));
  assert(project::musical(Kind::Step));
  // UI geometry has large nonoverlapping cells and exact touch routing.
  for (int id = 184; id <= 204; ++id) {
    auto r = widget(id);
    assert(r.w >= 100 && r.h >= 60 && r.y + r.h <= 480);
    assert(hit(Page::Performance, r.x + r.w/2, r.y + r.h/2) == id);
  }
  auto play_button = widget(206);
  assert(hit(Page::Performance, play_button.x + 8, play_button.y + 8) == 206);
  realtime = false;
  std::cout << "M18 performance PASS (allocation guard, mix truth table, Pattern/Chain returns, Fill, lengths/swing, locks/slices, V2, resets, UI); state="
            << sizeof(PerformanceState) << " engine=" << sizeof(Engine) << '\n';
}
