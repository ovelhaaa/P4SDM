#define P4SDM_APP 1
#include "../src/app/voice_state.h"
#include "../src/app/wav.h"
#include "../src/engine/track_tone.h"
#include "../synthESP32LowPassFilter_E.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
using namespace app;
static unsigned allocations;
void *operator new(std::size_t n) {
  ++allocations;
  if (auto p = std::malloc(n))
    return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
static Command command(Kind k, int value, int step = 0) {
  Command c{k, 0, value};
  c.step = uint8_t(step);
  return c;
}
static bool same(TriggerEvent a, TriggerEvent b) {
  return std::memcmp(&a, &b, sizeof(a)) == 0;
}
int main() {
  static_assert(sizeof(StepLocks) == 8 && sizeof(TriggerEvent) == 10);
  static_assert(sizeof(Pattern) == 2850);
  for (int amount = 0; amount <= 127; ++amount)
    for (int sample : {-65535, -32768, -1, 0, 1, 32767, 65535})
      assert(p4tone::resolved_delay_send(sample, uint8_t(amount)) ==
             p4tone::delay_send(sample, amount));
  Engine e;
  auto &base = e.tracks[0];
  base.filter_cutoff = 10;
  base.filter_resonance = 20;
  base.delay_send = 127;
  for (bool pcm : {false, true}) {
    base.source(pcm);
    for (int mask = 0; mask < 128; ++mask) {
      StepLocks l{uint8_t(mask), 72, 99, -100, 12, 90, 100, 32};
      auto v = resolve_event(0, base, 127, l);
      assert(v.filter_cutoff == ((mask & 16) ? 90 : 10));
      assert(v.filter_resonance == ((mask & 32) ? 100 : 20));
      assert(v.delay_send == ((mask & 64) ? 32 : 127));
      assert(v.locked_mask == (mask & (pcm ? 119 : 127)));
      VoiceState voice;
      voice.event = v;
      unsigned calls = 0;
      int cut = -1, res = -1;
      auto setter = [&](uint8_t c, uint8_t r) {
        ++calls;
        cut = c;
        res = r;
      };
      voice.apply(v, setter);
      Track edited = base;
      edited.filter_cutoff = 45;
      edited.filter_resonance = 55;
      edited.delay_send = 65;
      voice.apply(voice.effective(edited), setter);
      assert(cut == ((mask & 16) ? 90 : 45));
      assert(res == ((mask & 32) ? 100 : 55));
      assert(voice.delay_send == ((mask & 64) ? 32 : 65));
      const unsigned before = calls;
      edited.delay_send = 17;
      voice.apply(voice.effective(edited), setter, false);
      assert(calls ==
             before); // DelaySend-only command never touches coefficients.
      for (int pan : {-127, 0, 127})
        for (bool right : {false, true}) {
          int filtered = 12345;
          int dry =
              filtered * event_channel_gain(v.volume, pan, right) * 255 >> 16;
          assert(p4tone::delay_send(dry, v.delay_send) ==
                 (v.delay_send == 127 ? dry : dry * v.delay_send / 127));
          assert(p4tone::delay_send(dry, 0) == 0);
          assert(p4tone::delay_send(dry, 127) == dry);
        }
      assert(base.filter_cutoff == 10 && base.filter_resonance == 20 &&
             base.delay_send == 127);
    }
  }
  // Commands clamp all tone fields and freeze exact Pattern/Track/Step intent.
  Engine bounded;
  for (Kind k : {Kind::LockFilterCutoff, Kind::LockFilterResonance,
                 Kind::LockDelaySend}) {
    Command c{k, 7, -999};
    c.pattern = 3;
    c.step = 9;
    bounded.apply(c);
    const auto low = resolve_event(7, bounded.tracks[7], 127,
                                   bounded.patterns[3].locks[7][9]);
    assert((k != Kind::LockFilterCutoff || low.filter_cutoff == 0) &&
           (k != Kind::LockFilterResonance || low.filter_resonance == 0) &&
           (k != Kind::LockDelaySend || low.delay_send == 0));
    c.value = 999;
    bounded.apply(c);
    const auto high = resolve_event(7, bounded.tracks[7], 127,
                                    bounded.patterns[3].locks[7][9]);
    assert((k != Kind::LockFilterCutoff || high.filter_cutoff == 127) &&
           (k != Kind::LockFilterResonance || high.filter_resonance == 127) &&
           (k != Kind::LockDelaySend || high.delay_send == 127));
    assert(!bounded.patterns[4].locks[7][9].mask);
  }
  // Mapping sees the final pair, independently of which component was locked.
  Track zero;
  for (auto l :
       {StepLocks{16, 0, 0, 0, 0, 64, 0, 0}, StepLocks{48, 0, 0, 0, 0, 0, 0, 0},
        StepLocks{32, 0, 0, 0, 0, 0, 64, 0}}) {
    auto v = resolve_event(0, zero, 127, l);
    assert(p4tone::resonance(v.filter_cutoff, v.filter_resonance) ==
           (v.filter_cutoff == 64      ? 0
            : v.filter_resonance == 64 ? 64 * 160 / 127
                                       : 255));
  }
  e.apply(command(Kind::LockFilterCutoff, 100));
  e.apply(command(Kind::LockFilterResonance, 100));
  e.apply(command(Kind::LockDelaySend, 32));
  const auto original = e.patterns[0].locks[0][0];
  VoiceState sounding;
  unsigned coefficients = 0;
  auto set = [&](uint8_t, uint8_t) { ++coefficients; };
  auto first = resolve_event(0, base, 127, original);
  sounding.event = first;
  sounding.apply(first, set);
  auto following = resolve_event(0, base, 127);
  sounding.event = following;
  sounding.apply(following, set);
  assert(sounding.cutoff == 10 && sounding.resonance == 20 &&
         sounding.delay_send == 127);
  // Rapid coefficient variation retains history and stays in M11's envelope.
  LowPassFilter filter;
  LowPassFilter fresh;
  filter.setCutoffFreq(p4tone::cutoff(20));
  filter.setResonance(p4tone::resonance(20, 10));
  filter.next(10000);
  filter.setCutoffFreq(p4tone::cutoff(100));
  filter.setResonance(p4tone::resonance(100, 100));
  fresh.setCutoffFreq(p4tone::cutoff(100));
  fresh.setResonance(p4tone::resonance(100, 100));
  assert(filter.next(0) != fresh.next(0));
  const int cuts[] = {20, 100, 40, 120}, resonances[] = {10, 100, 40, 127};
  filter.reset();
  int64_t x = 0, y = 0;
  for (int i = 0; i < 100000; ++i) {
    int n = i % 4;
    filter.setCutoffFreq(p4tone::cutoff(cuts[n]));
    filter.setResonance(p4tone::resonance(cuts[n], resonances[n]));
    int input = i % 2 ? -32768 : 32767;
    int f = p4tone::cutoff(cuts[n]),
        q = p4tone::resonance(cuts[n], resonances[n]);
    int fb = q + ((q * (255 - f)) >> 8);
    int64_t feedback_product = fb * (x - y);
    assert(feedback_product >= INT32_MIN && feedback_product <= INT32_MAX);
    int64_t product = (input - x + (feedback_product >> 8)) * f;
    assert(product >= INT32_MIN && product <= INT32_MAX);
    x = std::max<int64_t>(-65535, std::min<int64_t>(65535, x + (product >> 8)));
    int64_t second = (x - y) * f;
    assert(second >= INT32_MIN && second <= INT32_MAX);
    y += second >> 8;
    assert(filter.next(input) == y && std::abs(y) <= 65535);
  }
  // Parent snapshots survive edits to both Pattern and Track through four hits.
  e.patterns[0].track_steps[0] = 1;
  e.patterns[0].meta[0][0] = {127, 100, 4};
  e.bpm = 240;
  e.apply(command(Kind::Play, 1));
  TriggerEvent parent{};
  unsigned hits = 0;
  for (int i = 0; i < 2757; ++i) {
    e.sample([&](TriggerEvent v) {
      if (!hits)
        parent = v;
      assert(same(v, parent));
      ++hits;
      sounding.event = v;
      sounding.apply(v, set);
    });
    if (!i) {
      e.apply(command(Kind::LockFilterCutoff, 50));
      e.apply(command(Kind::LockFilterResonance, 60));
      e.apply(command(Kind::LockDelaySend, 70));
      base.filter_cutoff = 11;
      base.filter_resonance = 21;
      base.delay_send = 126;
    }
  }
  assert(hits == 4 && sounding.cutoff == 100 && sounding.resonance == 100 &&
         sounding.delay_send == 32);
  const unsigned applied = coefficients;
  assert(sounding.apply(parent, set) == 4 && coefficients == applied);
  assert(resolve_event(0, base, 127, e.patterns[0].locks[0][0]).filter_cutoff ==
         50);
  // Rejected and suppressed parents cannot alter active event/DSP/RNG
  // ownership.
  for (int gate = 0; gate < 3; ++gate) {
    e.apply(command(Kind::Play, 1));
    e.patterns[0].meta[0][0].probability = gate == 0 ? 0 : 100;
    base.muted = gate == 1;
    e.solos = gate == 2 ? 2 : 0;
    e.sample([&](TriggerEvent) { assert(false); });
    assert(same(sounding.event, parent) && coefficients == applied);
  }
  TriggerEvent pad{};
  e.audition(0, [&](TriggerEvent v) { pad = v; });
  assert(!pad.locked_mask && pad.filter_cutoff == 11 &&
         pad.filter_resonance == 21 && pad.delay_send == 126);
  e.solos = 0;
  base.muted = false;
  e.patterns[0].length = 1;
  e.patterns[1].length = 1;
  e.patterns[1].track_steps[0] = 1;
  e.patterns[0].meta[0][0] = {127, 100, 4};
  e.apply(command(Kind::Play, 1));
  e.sample([](TriggerEvent) {});
  Command switch_pattern{Kind::SelectPattern, 0, 1};
  e.apply(switch_pattern);
  unsigned boundary_hits = 0;
  for (int i = 0; i < 2757; ++i)
    e.sample([&](TriggerEvent v) {
      if (e.playing_pattern == 1) {
        ++boundary_hits;
        assert(!v.locked_mask && v.filter_cutoff == 11 && v.delay_send == 126);
      } else
        assert(v.filter_cutoff == 50 && v.delay_send == 70);
    });
  assert(boundary_hits == 1);
  // Every transform moves the whole trivially copyable lock record.
  e.patterns[0].length = 16;
  e.patterns[0].locks[0][0] = original;
  e.apply(command(Kind::Rotate, 1));
  assert(std::memcmp(&original, &e.patterns[0].locks[0][1], 8) == 0);
  e.apply(command(Kind::Reverse, 0));
  assert(std::memcmp(&original, &e.patterns[0].locks[0][14], 8) == 0);
  e.apply(command(Kind::CopyTrack, 3));
  assert(std::memcmp(e.patterns[0].locks[0], e.patterns[0].locks[3], 128) == 0);
  for (Kind k : {Kind::CopyPattern, Kind::Duplicate}) {
    e.apply(command(k, 2));
    assert(std::memcmp(e.patterns[0].locks, e.patterns[2].locks,
                       sizeof(e.patterns[0].locks)) == 0);
  }
  StepLocks lane[16];
  std::memcpy(lane, e.patterns[0].locks[0], sizeof(lane));
  for (Kind k :
       {Kind::Euclidean, Kind::Randomize, Kind::Mutate, Kind::Source}) {
    e.apply(command(k, 1));
    assert(std::memcmp(lane, e.patterns[0].locks[0], sizeof(lane)) == 0);
  }
  auto mask_before_clear = e.patterns[0].track_steps[0];
  auto meta_before_clear = e.patterns[0].meta[0][14];
  e.apply(command(Kind::ClearStepLocks, 0, 14));
  assert(mask_before_clear == e.patterns[0].track_steps[0] &&
         std::memcmp(&meta_before_clear, &e.patterns[0].meta[0][14],
                     sizeof(StepMeta)) == 0);
  assert(!e.patterns[0].locks[0][14].mask);
  e.apply(command(Kind::LockDelaySend, 0));
  assert(e.patterns[0].locks[0][0].mask & 64);
  e.apply(command(Kind::UnlockParam, 112));
  assert(!(e.patterns[0].locks[0][0].mask & 112));
  // UI: 52+ px controls, no overlap, exact base initialization and explicit
  // off.
  for (Page page : {Page::Locks, Page::ToneLocks}) {
    int ids[11];
    int count = 0;
    for (int id = page == Page::Locks ? 96 : 114;
         id <= (page == Page::Locks ? 105 : 122); ++id)
      ids[count++] = id;
    if (page == Page::Locks)
      ids[count++] = 121;
    for (int i = 0; i < count; ++i) {
      auto r = widget(ids[i]);
      assert(r.h >= 52 && r.x >= 0 && r.y >= 0 && r.x + r.w <= 800 &&
             r.y + r.h <= 480);
      assert(hit(page, r.x + r.w / 2, r.y + r.h / 2) == ids[i]);
      assert(hit(page, r.x, r.y) == ids[i] &&
             hit(page, r.x + r.w - 1, r.y + r.h - 1) == ids[i]);
      for (int global = 29; global <= 35; ++global) {
        auto b = widget(global);
        assert(r.x + r.w <= b.x || b.x + b.w <= r.x || r.y + r.h <= b.y ||
               b.y + b.h <= r.y);
      }
      for (int j = i + 1; j < count; ++j) {
        auto b = widget(ids[j]);
        assert(r.x + r.w <= b.x || b.x + b.w <= r.x || r.y + r.h <= b.y ||
               b.y + b.h <= r.y);
      }
    }
  }
  Ui ui;
  ui.selected_step = 3;
  e.selected_pattern = 0;
  const int values[] = {11, 21, 126};
  for (int n = 0; n < 3; ++n) {
    Command c{};
    assert(ui.lock_action(115 + n * 2, 0, true, e, c) && c.value == values[n]);
    e.apply(c);
    assert(ui.lock_action(115 + n * 2, 0, true, e, c) &&
           c.kind == Kind::UnlockParam);
    e.apply(c);
  }
  const auto before_allocations = allocations;
  for (int i = 0; i < 100000; ++i) {
    e.apply(command(Kind::LockDelaySend, i % 128));
    auto v = resolve_event(0, base, 127, e.patterns[0].locks[0][0]);
    sounding.event = v;
    sounding.apply(v, set);
    sounding.apply(sounding.effective(base), set);
  }
  assert(allocations == before_allocations);
  std::cout << "M12 tone locks: PASS; StepLocks=" << sizeof(StepLocks)
            << " TriggerEvent=" << sizeof(TriggerEvent)
            << " Pattern=" << sizeof(Pattern) << " bank=" << sizeof(e.patterns)
            << " Engine=" << sizeof(Engine)
            << " VoiceState=" << sizeof(VoiceState)
            << " mirror=" << 2 * sizeof(Engine) << '\n';
}
