#define P4SDM_APP 1
#include "../src/app/model.h"
#include "../src/engine/track_tone.h"
#include "../synthESP32LowPassFilter_E.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
static unsigned allocations;
void *operator new(std::size_t n) {
  ++allocations;
  if (auto p = std::malloc(n))
    return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
// Wide oracle detects overflow and unexpected saturation in intended signals.
struct Wide {
  int64_t x = 0, y = 0;
  int f, fb;
  int next(int in) {
    auto feedback = (int64_t(fb) * (x - y)) >> 8;
    auto product = ((in - x) + feedback) * f;
    assert(product >= INT32_MIN && product <= INT32_MAX);
    x += product >> 8;
    assert(std::abs(x) <= 65535);
    assert(std::abs((x - y) * f) <= INT32_MAX);
    y += ((x - y) * f) >> 8;
    assert(std::abs(y) <= 65535);
    return int(y);
  }
};
int main() {
  static_assert(sizeof(app::Command) == 12);
  static_assert(sizeof(app::StepLocks) == 8);
  static_assert(sizeof(app::TriggerEvent) == 20);
  assert(p4tone::cutoff(0) == 255 && p4tone::cutoff(127) == 0);
  assert(p4tone::resonance(0, 0) == 255);
  assert(p4tone::resonance(64, 0) == 0 && p4tone::resonance(64, 127) == 160);
  for (int i = 0; i <= 127; ++i) {
    assert(p4tone::cutoff(i) == 255 + (-255 * i) / 127);
    assert(p4tone::cutoff_from_slider(i) == 127 - i);
    if (i)
      assert(p4tone::cutoff(i) < p4tone::cutoff(i - 1));
  }
  LowPassFilter a, b;
  a.setCutoffFreq(90);
  a.setResonance(180);
  b.setResonance(180);
  b.setCutoffFreq(90);
  for (int i = 0; i < 200; ++i)
    assert(a.next(i % 2 ? -32768 : 32767) == b.next(i % 2 ? -32768 : 32767));
  a.reset();
  b.reset();
  for (int i = 0; i < 200; ++i)
    assert(a.next(i == 0 ? 32767 : 0) == b.next(i == 0 ? 32767 : 0));
  // Edits retain history: identical configured zero-state filter differs.
  a.next(10000);
  a.setCutoffFreq(40);
  a.setResonance(100);
  b.reset();
  b.setCutoffFreq(40);
  b.setResonance(100);
  assert(a.next(0) != b.next(0));
  a.reset();
  b.reset();
  for (int i = 0; i < 200; ++i)
    assert(a.next(i == 0 ? 20000 : 0) == b.next(i == 0 ? 20000 : 0));
  int peak = 0;
  for (int cut = 0; cut <= 127; ++cut)
    for (int res : {0, 32, 64, 96, 127}) {
      for (int mode = 0; mode < 4; ++mode) {
        LowPassFilter filter;
        int f = p4tone::cutoff(cut), q = p4tone::resonance(cut, res);
        filter.setCutoffFreq(uint8_t(f));
        filter.setResonance(uint8_t(q));
        Wide oracle{0, 0, f, q + ((q * (255 - f)) >> 8)};
        for (int i = 0; i < 8192; ++i) {
          int in = mode == 0   ? (i == 0 ? 32767 : 0)
                   : mode == 1 ? (i % 2 ? -32768 : 32767)
                   : mode == 2 ? 32767
                               : int(32767 * std::sin(i * .071));
          int output = filter.next(in);
          assert(output == oracle.next(in));
          peak = std::max(peak, std::abs(output));
        }
      }
    }
  // Live coefficient changes during full scale active signal.
  LowPassFilter dynamic;
  for (int i = 0; i < 100000; ++i) {
    if (i % 100 == 0) {
      dynamic.setResonance(p4tone::resonance(i / 100 % 128, i / 300 % 128));
      dynamic.setCutoffFreq(p4tone::cutoff(i / 100 % 128));
    }
    assert(std::abs(dynamic.next(i % 2 ? -32768 : 32767)) <= 65535);
  }
  for (int32_t v : {-65535, -32768, -1, 0, 1, 32767, 65535}) {
    assert(p4tone::delay_send(v, 127) == v && p4tone::delay_send(v, 0) == 0);
    int previous = 0;
    for (int send = 0; send <= 127; ++send) {
      int now = std::abs(p4tone::delay_send(v, send));
      assert(now >= previous && now <= std::abs(v));
      previous = now;
    }
  }
  app::Engine e;
  auto before = allocations;
  for (int t = 0; t < 16; ++t) {
    assert(e.tracks[t].filter_cutoff == 0 &&
           e.tracks[t].filter_resonance == 0 && e.tracks[t].delay_send == 127);
    e.apply({app::Kind::FilterCutoff, uint8_t(t), t * 8});
    e.apply({app::Kind::FilterResonance, uint8_t(t), 127 - t * 8});
    e.apply({app::Kind::DelaySend, uint8_t(t), t * 7});
  }
  auto original = e.tracks[0];
  for (auto kind :
       {app::Kind::Source, app::Kind::CopyTrack, app::Kind::CopyPattern,
        app::Kind::SelectPattern, app::Kind::Euclidean, app::Kind::Randomize,
        app::Kind::Mutate}) {
    e.apply({kind, 0, 1});
    assert(e.tracks[0].filter_cutoff == original.filter_cutoff &&
           e.tracks[0].filter_resonance == original.filter_resonance &&
           e.tracks[0].delay_send == original.delay_send);
  }
  e.apply({app::Kind::Source, 0, 0});
  for (int t = 0; t < 16; ++t) {
    assert(e.tracks[t].filter_cutoff == t * 8 &&
           e.tracks[t].filter_resonance == 127 - t * 8 &&
           e.tracks[t].delay_send == t * 7);
    LowPassFilter isolated, reference;
    isolated.setCutoffFreq(p4tone::cutoff(t * 8));
    isolated.setResonance(p4tone::resonance(t * 8, 127 - t * 8));
    reference.setResonance(p4tone::resonance(t * 8, 127 - t * 8));
    reference.setCutoffFreq(p4tone::cutoff(t * 8));
    for (int i = 0; i < 100; ++i) {
      a.setResonance(uint8_t(i));
      assert(isolated.next(1000) == reference.next(1000));
    }
  }
  e.tracks[0].filter_cutoff = 64;
  e.tracks[0].filter_resonance = 100;
  e.tracks[0].delay_send = 64;
  auto event = app::resolve_event(
      0, e.tracks[0], 127, {app::VOLUME_LOCK | app::PAN_LOCK, 0, 20, 127, 0});
  assert(event.volume == 20 && event.pan == 127);
  assert(app::event_channel_gain(event.volume, event.pan, false) == 0);
  assert(app::event_channel_gain(event.volume, event.pan, true) == 20);
  LowPassFilter routing;
  routing.setCutoffFreq(p4tone::cutoff(64));
  routing.setResonance(p4tone::resonance(64, 100));
  int filtered = routing.next(32767);
  int dryR = (filtered * 20 * 255) >> 16;
  assert(p4tone::delay_send(dryR, 64) == int64_t(dryR) * 64 / 127);
  for (auto kind : {app::Kind::FilterCutoff, app::Kind::FilterResonance,
                    app::Kind::DelaySend}) {
    e.apply({kind, 0, -100});
    e.apply({kind, 0, 999});
  }
  assert(e.tracks[0].filter_cutoff == 127 &&
         e.tracks[0].filter_resonance == 127 && e.tracks[0].delay_send == 127);
  for (int i = 0; i < 100000; ++i)
    e.apply({app::Kind::DelaySend, uint8_t(i % 16), i % 128});
  assert(allocations == before);
  for (int id = 107; id <= 112; ++id) {
    auto r = app::widget(id);
    assert(r.h >= 52);
    assert(app::hit(app::Page::Tone, r.x + r.w / 2, r.y + r.h / 2) == id);
  }
  assert(app::hit(app::Page::Track, 100, 330) == 113);
  for (auto page : {app::Page::Track, app::Page::Tone}) {
    const int trackIds[] = {24, 25, 26, 27, 28, 37, 42, 43, 113};
    const int toneIds[] = {107, 108, 109, 110, 111, 112};
    const int *ids = page == app::Page::Track ? trackIds : toneIds;
    int count = page == app::Page::Track ? 9 : 6;
    for (int i = 0; i < count; ++i) {
      auto r = app::widget(ids[i]);
      assert(app::hit(page, r.x + r.w / 2, r.y + r.h / 2) == ids[i]);
      for (int j = i + 1; j < count; ++j) {
        auto other = app::widget(ids[j]);
        assert(r.x + r.w <= other.x || other.x + other.w <= r.x ||
               r.y + r.h <= other.y || other.y + other.h <= r.y);
      }
    }
  }
  assert(app::drag(107, 24) == 0 && app::drag(107, 775) == 127);
  std::printf("Track tone: mapping, coefficient refresh, history, wide "
              "numerical oracle, isolation, routing, locks, persistence, UI, "
              "no allocation PASS (peak=%d)\n",
              peak);
}
