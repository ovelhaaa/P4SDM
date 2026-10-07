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
void onset(Engine &e) {
  auto step = e.step; auto loops = e.loops;
  do { tick(e); } while (e.playing && step == e.step && loops == e.loops);
}
void play(Engine &e) { e.apply({Kind::Play, 0, 1}); }
bool same(TriggerEvent a, TriggerEvent b) {
  return !std::memcmp(&a, &b, sizeof(a));
}
int main() {
  static Engine e, reference;
  static project::State a, b, decoded;
  static uint8_t bytes_a[project::file_bytes], bytes_b[project::file_bytes];
  realtime = true;
  // Exact offsets, long/short swing windows, and thousands of origin-derived
  // rational windows. No initial offset-zero duplicate and no rounded drift.
  for (unsigned rate : {2u, 4u, 8u}) for (int swing : {50, 60, 75})
    for (int parity : {0, 1}) {
      e = Engine{}; e.bpm = 137; e.swing = swing;
      e.patterns[0].track_steps[0] = 0xffff;
      for (auto &m : e.patterns[0].meta[0]) m.ratchets = 4;
      play(e);
      if (parity) { tick(e); e.phase = e.duration; }
      e.apply({Kind::PerfRepeatStart, 0, int(rate)});
      const uint64_t duration = Engine::straight * 2 * unsigned(parity ? 100 - swing : swing) / 100;
      const uint64_t delta = unsigned(e.bpm) * 4;
      uint64_t hit = 0, releases = 0;
      const uint64_t count = 1000 * rate;
      for (uint64_t i = 0; hit < count; ++i) {
        e.sample([&](TriggerEvent ev, bool child) {
          const uint64_t w = hit / rate, k = hit % rate;
          const uint64_t offset = w * duration + (duration * k + rate - 1) / rate;
          assert(i == (offset + delta - 1) / delta);
          assert(ev.track == 0 && child == (hit != 0));
          ++hit;
        }, [&](int) { ++releases; });
        assert(e.step == parity && e.loops == 0 && e.ratchet_events == 0);
      }
      assert(releases == 16 && e.performance.repeat.capture.count == 1);
      assert(e.repeat_metrics.windows[Engine::repeat_index(rate)] == 1000);
    }
  // Test durations not divisible by the subdivisions, in the same rational
  // units, including every ceiling offset and fractional sample remainder.
  for (unsigned rate : {2u, 4u, 8u}) for (uint64_t duration : {997ull, 1999ull, 3001ull}) {
    e = Engine{}; e.bpm = 30; e.patterns[0].track_steps[0] = 1;
    play(e); e.apply({Kind::PerfRepeatStart, 0, int(rate)});
    unsigned initial = 0;
    e.sample([&](TriggerEvent, bool child) { assert(!child); ++initial; });
    assert(initial == 1);
    e.duration = duration;
    e.performance.repeat.next_offset = (duration + rate - 1) / rate;
    uint64_t hit = 1;
    for (uint64_t sample = 1; hit < rate * 10000; ++sample) {
      e.sample([&](TriggerEvent, bool child) {
        const auto offset = (hit / rate) * duration + (duration * (hit % rate) + rate - 1) / rate;
        assert(child && sample == (offset + 119) / 120); ++hit;
      });
    }
    assert(e.step == 0 && e.repeat_metrics.windows[Engine::repeat_index(rate)] == 10000);
  }
  // Latest pending rate, invalid rates and cancellation before acceptance.
  e = Engine{}; play(e);
  for (int invalid : {-1, 0, 1, 3, 5, 16}) e.apply({Kind::PerfRepeatStart, 0, invalid});
  assert(!e.performance.repeat.requested);
  e.apply({Kind::PerfRepeatStart, 0, 2}); e.apply({Kind::PerfRepeatStart, 0, 8});
  e.apply({Kind::PerfRepeatStop, 0, 0}); tick(e);
  assert(!e.performance.repeat.active && e.repeat_metrics.cancelled == 1);
  // Rate updates at a window boundary; release at every sub-index completes
  // the current window, then advances exactly one normal step.
  for (unsigned rate : {2u, 4u, 8u}) for (unsigned sub = 0; sub < rate; ++sub) {
    e = Engine{}; e.patterns[0].track_steps[0] = 0xffff;
    play(e); e.apply({Kind::PerfRepeatStart, 0, int(rate)}); tick(e);
    while (e.performance.repeat.next <= sub) tick(e);
    e.apply({Kind::PerfRepeatStop, 0, 0});
    assert(e.performance.repeat.active == rate && !e.performance.repeat.requested);
    onset(e);
    assert(e.step == 1 && !e.performance.repeat.active && e.repeat_metrics.releases == 1);
    assert(e.repeat_metrics.hits == rate - 1 && e.ratchet_events == 0);
  }
  e = Engine{}; e.patterns[0].track_steps[0] = 1;
  play(e); e.apply({Kind::PerfRepeatStart, 0, 2}); tick(e);
  e.apply({Kind::PerfRepeatRate, 0, 8});
  assert(e.performance.repeat.active == 2);
  while (e.repeat_metrics.windows[2] == 0) tick(e);
  assert(e.performance.repeat.active == 8 && e.repeat_metrics.changes == 1);
  // Sixteen distinct immutable parents: all locks, samples/synth, source,
  // resolved Slice Lock, Track/Pattern/Slice edits and manual audition.
  e = Engine{};
  for (int t = 0; t < 16; ++t) {
    if (t % 2) { e.tracks[t].assigned(); e.tracks[t].slices.divide({}, 16); }
    e.patterns[0].track_steps[t] = 0xffff;
    e.patterns[0].meta[t][0] = {uint8_t(70+t), 100, 4};
    e.patterns[0].locks[t][0] = {255, uint8_t(48+t), uint8_t(60+t), int8_t(t-8), uint8_t(t), uint8_t(70+t), uint8_t(30+t), uint8_t(80+t), uint8_t(t)};
  }
  play(e); e.apply({Kind::PerfRepeatStart, 0, 8});
  TriggerEvent accepted[16]{}; bool sources[16]{};
  unsigned n = 0;
  e.sample([&](TriggerEvent ev, bool child, bool source) {
    assert(!child); accepted[n] = ev; sources[n++] = source;
  });
  assert(n == 16 && e.performance.repeat.capture.count == 16 && e.repeat_metrics.captured_max == 16);
  const auto capture = e.performance.repeat.capture;
  for (int t = 0; t < 16; ++t) {
    e.tracks[t].pitch = 5; e.tracks[t].volume = 7; e.tracks[t].pan = 50;
    e.tracks[t].wave = 15; e.tracks[t].filter_cutoff = 1;
    e.tracks[t].filter_resonance = 2; e.tracks[t].delay_send = 3;
    e.tracks[t].slices.reset({}); e.tracks[t].source(!sources[t]);
    e.patterns[0].locks[t][0] = {}; e.patterns[0].track_steps[t] = 0;
  }
  unsigned audition = 0;
  e.audition(4, [&](TriggerEvent) { ++audition; });
  assert(audition == 1 && !std::memcmp(&capture, &e.performance.repeat.capture, sizeof(capture)));
  n = 0;
  while (n < 16) e.sample([&](TriggerEvent ev, bool child, bool source) {
    assert(child && same(ev, accepted[ev.track]) && source == sources[ev.track]); ++n;
  });
  assert(n == 16 && e.ratchet_events == 0);
  // Only current runtime performance masks affect already captured children.
  e.apply({Kind::PerfMute, 1, 1}); e.apply({Kind::PerfSolo, 2, 1});
  unsigned emitted = 0; auto old_hits = e.repeat_metrics.hits;
  while (e.repeat_metrics.hits == old_hits) e.sample([&](TriggerEvent ev, bool, bool) {
    assert(ev.track == 2 && same(ev, accepted[2])); ++emitted;
  });
  assert(emitted == 1 && e.repeat_metrics.suppressed >= 15);
  assert(!std::memcmp(&capture, &e.performance.repeat.capture, sizeof(capture)));
  // RNG matches a transport paused after the accepted step, not a stream of
  // newly resolved parents. Skipped probability Tracks never enter capture.
  e = Engine{}; e.patterns[0].length = 3;
  for (int t = 0; t < 16; ++t) {
    e.patterns[0].track_steps[t] = 7;
    for (int step = 0; step < 3; ++step) e.patterns[0].meta[t][step].probability = uint8_t(t*6);
  }
  reference = e; play(e); play(reference);
  e.apply({Kind::PerfRepeatStart, 0, 8}); tick(e); tick(reference);
  auto rng = e.rng; assert(rng == reference.rng);
  assert(e.performance.repeat.capture.count == e.probability_passed && e.probability_skipped > 0);
  bool captured[16]{};
  for (unsigned i = 0; i < e.performance.repeat.capture.count; ++i) captured[e.performance.repeat.capture.events[i].track] = true;
  while (e.repeat_metrics.windows[2] < 100) e.sample([&](TriggerEvent ev, bool child) {
    assert(child && captured[ev.track]);
  });
  assert(e.rng == rng && e.edit_seed == reference.edit_seed);
  e.apply({Kind::PerfRepeatStop, 0, 0}); onset(e);
  reference.phase = reference.duration; tick(reference);
  assert(e.step == reference.step && e.rng == reference.rng && e.probability_passed == reference.probability_passed);
  // Empty accepted step remains silent, with no stale capture or RNG draws.
  e = Engine{}; play(e); e.apply({Kind::PerfRepeatStart, 0, 8}); tick(e);
  assert(e.repeat_metrics.empty == 1 && e.performance.repeat.capture.count == 0);
  rng = e.rng;
  for (int i = 0; i < 50000; ++i) e.sample([](TriggerEvent) { assert(false); });
  assert(e.rng == rng && e.step == 0);
  // Final-step Repeat freezes Chain repeat accounting until one release wrap.
  for (int repeat = 0; repeat < 2; ++repeat) {
    e = Engine{}; e.mode = TransportMode::Chain; e.chain.length = 2;
    e.chain.entries[0] = {0, 2}; e.chain.entries[1] = {1, 2};
    e.patterns[0].length = e.patterns[1].length = 1;
    play(e); tick(e); if (repeat) onset(e);
    e.apply({Kind::PerfRepeatStart, 0, 8}); onset(e);
    const auto entry = e.chain_entry, done = e.chain_repeat; const auto loops = e.loops;
    while (e.repeat_metrics.windows[2] < 50) tick(e);
    assert(e.chain_entry == entry && e.chain_repeat == done && e.loops == loops && e.step == 0);
    e.apply({Kind::PerfRepeatStop, 0, 0}); onset(e);
    assert(e.loops == loops + 1);
    assert(e.chain_entry == (done == 1 ? entry + 1 : entry));
  }
  // Chain edits retain the frozen numerical index until the next legal wrap.
  e = Engine{}; e.mode = TransportMode::Chain; e.chain.length = 2;
  e.chain.entries[0] = {0, 2}; e.chain.entries[1] = {1, 2};
  e.patterns[0].length = e.patterns[1].length = e.patterns[2].length = 1;
  play(e); e.apply({Kind::PerfRepeatStart, 0, 8}); tick(e);
  Command edit{Kind::ChainPattern, 0, 2}; edit.step = 1; e.apply(edit);
  edit = {Kind::ChainRepeats, 0, 1}; edit.step = 0; e.apply(edit);
  for (int i = 0; i < 50000; ++i) tick(e);
  assert(e.chain_entry == 0 && e.chain_repeat == 0 && e.playing_pattern == 0);
  e.apply({Kind::PerfRepeatStop, 0, 0}); onset(e);
  assert(e.chain_entry == 1 && e.chain_repeat == 0 && e.playing_pattern == 2);
  // Cancellation under a frozen Override waits for its normal Pattern wrap.
  e = Engine{}; e.patterns[0].length = 1; e.patterns[8].length = 3;
  play(e); tick(e); e.apply({Kind::PerfOverride, 0, 8}); onset(e);
  e.apply({Kind::PerfRepeatStart, 0, 4}); onset(e);
  e.apply({Kind::PerfOverrideCancel, 0, 0});
  for (int i = 0; i < 50000; ++i) tick(e);
  assert(e.performance.override_active == 8 && e.step == 1);
  e.apply({Kind::PerfRepeatStop, 0, 0}); onset(e);
  assert(e.playing_pattern == 8 && e.step == 2);
  onset(e); assert(e.playing_pattern == 0 && e.perf_metrics.returns == 1);
  // Repeat inside Fill/Override, pending Fill and cancellation underneath.
  e = Engine{}; e.patterns[0].length = e.patterns[8].length = 1;
  e.patterns[9].length = 3;
  play(e); tick(e); e.apply({Kind::PerfOverride, 0, 8}); onset(e);
  e.apply({Kind::PerfFill, 0, 9}); onset(e); assert(e.performance.fill_active == 9);
  e.apply({Kind::PerfRepeatStart, 0, 4}); onset(e); assert(e.step == 1);
  auto perf_loops = e.perf_metrics.override_loops;
  e.apply({Kind::PerfOverrideCancel, 0, 0});
  while (e.repeat_metrics.windows[1] < 20) tick(e);
  assert(e.performance.fill_active == 9 && !e.perf_metrics.fill_completions && e.perf_metrics.override_loops == perf_loops);
  e.apply({Kind::PerfRepeatStop, 0, 0}); onset(e); assert(e.step == 2 && e.performance.fill_active == 9);
  onset(e); assert(e.playing_pattern == 0 && e.perf_metrics.fill_completions == 1);
  e.apply({Kind::PerfRepeatStart, 0, 2}); onset(e);
  e.apply({Kind::PerfFill, 0, 9});
  for (int i = 0; i < 50000; ++i) tick(e);
  assert(e.performance.fill_pending == 9 && e.performance.fill_active < 0);
  e.apply({Kind::PerfRepeatStop, 0, 0}); onset(e);
  assert(e.performance.fill_active == 9);
  // Length changes affect only the next normal onset, including safe wrap.
  e = Engine{}; e.patterns[0].length = 16; play(e); tick(e);
  for (int i = 0; i < 6; ++i) onset(e);
  e.apply({Kind::PerfRepeatStart, 0, 8}); onset(e); assert(e.step == 7);
  e.patterns[0].length = 3;
  for (int i = 0; i < 50000; ++i) tick(e);
  assert(e.step == 7);
  e.apply({Kind::PerfRepeatStop, 0, 0}); onset(e); assert(e.step == 0 && e.loops == 1);
  // Save is byte-identical, Repeat commands are runtime only, and every reset
  // discards capture and both pending/active states.
  e = Engine{};
  for (unsigned part = 0; part <= 16; ++part) project::snapshot_part(e, a, part);
  play(e); e.apply({Kind::PerfRepeatStart, 0, 8}); tick(e);
  for (unsigned part = 0; part <= 16; ++part) project::snapshot_part(e, b, part);
  assert(project::encode(a, 11, bytes_a, sizeof(bytes_a)) && project::encode(b, 11, bytes_b, sizeof(bytes_b)));
  assert(!std::memcmp(bytes_a, bytes_b, sizeof(bytes_a)));
  assert(project::decode(bytes_b, sizeof(bytes_b), decoded) == project::Error::Ok);
  project::apply_part(e, decoded, 0); assert(!e.performance.repeat.active && !e.performance.repeat.capture.count);
  play(e); e.apply({Kind::PerfRepeatStart, 0, 4}); tick(e);
  e.apply({Kind::Play, 0, 0}); assert(!e.performance.repeat.active && !e.performance.repeat.requested);
  project::defaults(decoded); project::apply_part(e, decoded, 0);
  for (auto k : {Kind::PerfRepeatStart, Kind::PerfRepeatRate, Kind::PerfRepeatStop}) assert(!project::musical(k));
  // Hold owns its action independently of page/selection/drift. A full queue
  // retains release until a later successful submission; no stuck Repeat.
  Queue<2> queue; RepeatGate gate;
  auto send = [&](Command c) { return queue.push(c); };
  assert(gate.press(8, send));
  gate.release(send); assert(gate.held && gate.release_pending);
  assert(!gate.press(2, send));
  Command c{}; assert(queue.pop(c) && c.kind == Kind::PerfRepeatStart && c.value == 8);
  gate.flush(send); assert(!gate.held && !gate.release_pending);
  assert(queue.pop(c) && c.kind == Kind::PerfRepeatStop);
  for (int id = 207; id <= 209; ++id) {
    auto r = widget(id);
    assert(r.w >= 200 && r.h >= 100);
    assert(hit(Page::PerformanceRepeat, r.x, r.y) == id);
    assert(hit(Page::PerformanceRepeat, r.x+r.w-1, r.y+r.h-1) == id);
  }
  realtime = false;
  std::cout << "M18.1 Repeat PASS: exact timing/drift, snapshots/RNG, 16 events, ratchet replacement, Chain/Fill/Override, resets/V2, hold recovery/allocation guard; capture="
            << sizeof(RepeatCapture) << " repeat=" << sizeof(RepeatState) << " performance=" << sizeof(PerformanceState) << " engine=" << sizeof(Engine) << '\n';
}
