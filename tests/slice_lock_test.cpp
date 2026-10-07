#include "../src/app/model.h"
#include "../src/app/voice_state.h"
#include "../src/app/wav.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
static unsigned allocations = 0;
void *operator new(std::size_t n) {
  ++allocations;
  if (void *p = std::malloc(n))
    return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
using namespace app;
static bool same(TriggerEvent a, TriggerEvent b) {
  return a.track == b.track && a.velocity == b.velocity && a.pitch == b.pitch &&
         a.volume == b.volume && a.pan == b.pan && a.wave == b.wave &&
         a.locked_mask == b.locked_mask && a.filter_cutoff == b.filter_cutoff &&
         a.filter_resonance == b.filter_resonance &&
         a.delay_send == b.delay_send && a.slice == b.slice &&
         a.sequenced == b.sequenced && a.playback.start == b.playback.start &&
         a.playback.end == b.playback.end &&
         a.playback.reverse == b.playback.reverse &&
         a.playback.mode == b.playback.mode &&
         a.playback.choke == b.playback.choke;
}
int main() {
  static_assert(sizeof(StepLocks) == 9 && alignof(StepLocks) == 1);
  static_assert(sizeof(TriggerEvent) == 20);
  Track t;
  t.assigned();
  t.slices.divide({}, 16);
  t.slices.selected = 2;
  t.playback.start = 123;
  t.playback.end = 45678;
  t.playback.reverse = true;
  t.playback.mode = sampler::Mode::Gate;
  t.playback.choke = 3;
  for (bool enabled : {false, true})
    for (int slice = 0; slice < 16; ++slice) {
      t.slice_enabled = enabled;
      for (unsigned mask = 0; mask < 256; ++mask) {
        StepLocks l{uint8_t(mask), 72, 90, -100, 15, 80, 70, 32,
                    uint8_t(slice)};
        auto e = resolve_event(0, t, 100, l);
        assert(e.locked_mask == (mask & 0xf7));
        int effective = mask & SLICE_LOCK ? slice : enabled ? 2 : -1;
        assert(e.slice == effective + 1);
        assert(e.playback.start == (effective < 0
                                        ? t.playback.start
                                        : t.slices.slices[effective].start));
        assert(
            e.playback.end ==
            (effective < 0 ? t.playback.end : t.slices.slices[effective].end));
        assert(e.playback.reverse && e.playback.mode == sampler::Mode::Gate &&
               e.playback.choke == 3);
        assert(t.slices.selected == 2);
        t.source(false);
        auto synth = resolve_event(0, t, 100, l);
        assert(synth.locked_mask == (mask & 0x7f) && synth.slice == 0);
        t.source(true);
        if (mask & PITCH_LOCK)
          assert(e.pitch == 72);
        if (mask & PAN_LOCK)
          assert(e.pan == -100);
        if (mask & FILTER_CUTOFF_LOCK)
          assert(e.filter_cutoff == 80);
        if (mask & DELAY_SEND_LOCK)
          assert(e.delay_send == 32);
      }
    }
  StepLocks l{};
  l.mask = SLICE_LOCK;
  l.slice = 15;
  t.slices.divide({}, 4);
  assert(resolve_event(0, t, 127, l).slice == 4 && l.slice == 15);
  t.slices.divide({}, 16);
  assert(resolve_event(0, t, 127, l).slice == 16);
  t.slices.count = 0;
  assert(resolve_event(0, t, 127, l).slice == 1);
  t.slices.divide({}, 16);
  assert(resolve_event(0, t, 127).slice == 3);
  assert(resolve_event(0, t, 127, {}, 5).slice == 6);
  Engine e;
  e.tracks[0] = t;
  e.patterns[0].track_steps[0] = 3;
  e.patterns[0].locks[0][0] = l;
  e.patterns[0].meta[0][0] = {100, 100, 4};
  e.apply({Kind::Play, 0, 1});
  unsigned hits = 0;
  TriggerEvent parent{};
  auto callback = [&](TriggerEvent event, bool ratchet) {
    if (!hits) {
      parent = event;
      assert(!ratchet && event.slice == 16);
    } else if (hits < 4)
      assert(ratchet && same(parent, event));
    else
      assert(!ratchet && event.slice == 3);
    ++hits;
  };
  e.sample(callback);
  e.patterns[0].locks[0][0].slice = 4;
  e.patterns[0].locks[0][0] = {};
  e.tracks[0].slices.erase(11);
  e.tracks[0].slices.reset({});
  e.tracks[0].slices.divide({}, 4);
  e.tracks[0].slices.edit(3, true, 1234);
  e.tracks[0].slices.selected = 2;
  while (hits < 5)
    e.sample(callback);
  // Rejected or suppressed parents never reach the choke/trigger boundary.
  for (int mode = 0; mode < 3; ++mode) {
    Engine suppressed;
    suppressed.tracks[0] = t;
    suppressed.patterns[0].track_steps[0] = 1;
    suppressed.patterns[0].locks[0][0] = l;
    if (mode == 0)
      suppressed.patterns[0].meta[0][0].probability = 0;
    if (mode == 1)
      suppressed.tracks[0].muted = true;
    if (mode == 2)
      suppressed.solos = 2;
    suppressed.apply({Kind::Play, 0, 1});
    unsigned calls = 0;
    for (int i = 0; i < 1000; ++i)
      suppressed.sample([&](TriggerEvent) { ++calls; });
    assert(calls == 0 && suppressed.tracks[0].slices.selected == 2);
  }
  Engine transforms;
  auto &pattern = transforms.patterns[0];
  for (int i = 0; i < 16; ++i) {
    pattern.locks[0][i].mask = SLICE_LOCK;
    pattern.locks[0][i].slice = i;
  }
  auto original = pattern;
  transforms.apply({Kind::Rotate, 0, 1});
  for (int i = 0; i < 16; ++i)
    assert(pattern.locks[0][(i + 1) % 16].slice == i);
  transforms.apply({Kind::Rotate, 0, -1});
  transforms.apply({Kind::Reverse, 0, 0});
  for (int i = 0; i < 16; ++i)
    assert(pattern.locks[0][15 - i].slice == i);
  transforms.apply({Kind::Reverse, 0, 0});
  transforms.apply({Kind::CopyTrack, 0, 1});
  assert(!std::memcmp(pattern.locks[0], pattern.locks[1],
                      sizeof(pattern.locks[0])));
  transforms.apply({Kind::CopyPattern, 0, 2});
  assert(!std::memcmp(&pattern, &transforms.patterns[2], sizeof(pattern)));
  transforms.apply({Kind::Duplicate, 0, 3});
  assert(!std::memcmp(&pattern, &transforms.patterns[3], sizeof(pattern)));
  for (Kind kind : {Kind::Euclidean, Kind::Randomize, Kind::Mutate,
                    Kind::ClearTrack, Kind::ClearPattern}) {
    transforms.apply({kind, 0, 5});
    assert(!std::memcmp(original.locks[0], pattern.locks[0],
                        sizeof(pattern.locks[0])));
  }
  Ui ui;
  ui.selected = 0;
  ui.selected_step = 7;
  transforms.tracks[0] = t;
  transforms.selected_pattern = 2;
  Command c{};
  transforms.patterns[2].locks[0][7] = {};
  transforms.tracks[0].slice_enabled = false;
  assert(ui.slice_lock_action(149, true, transforms, c));
  assert(c.pattern == 2 && c.track == 0 && c.step == 7 && c.value == 2);
  ui.selected = 3;
  ui.selected_step = 2;
  transforms.selected_pattern = 0;
  transforms.apply(c);
  assert(transforms.patterns[2].locks[0][7].slice == 2);
  ui.selected = 0;
  ui.selected_step = 7;
  transforms.selected_pattern = 2;
  assert(ui.slice_lock_action(151, true, transforms, c));
  transforms.apply(c);
  assert(transforms.patterns[2].locks[0][7].slice == 3);
  assert(ui.slice_lock_action(149, true, transforms, c));
  transforms.apply(c);
  assert(transforms.patterns[2].locks[0][7].mask == 0);
  transforms.tracks[0].source(false);
  assert(!ui.slice_lock_action(149, true, transforms, c));
  transforms.tracks[0].source(true);
  transforms.apply({Kind::LockSlice, 0, 15});
  transforms.tracks[0].source(false);
  assert(
      resolve_event(0, transforms.tracks[0], 127, pattern.locks[0][0]).slice ==
      0);
  transforms.tracks[0].source(true);
  assert(pattern.locks[0][0].slice == 15);
  auto meta = pattern.meta[0][0];
  auto mask = pattern.track_steps[0];
  transforms.apply({Kind::ClearStepLocks, 0, 0});
  assert(pattern.locks[0][0].mask == 0 && pattern.track_steps[0] == mask &&
         !std::memcmp(&meta, &pattern.meta[0][0], sizeof(meta)));
  for (int id = 149; id <= 152; ++id) {
    auto r = widget(id);
    assert(r.h >= 52 && r.y + r.h <= 420);
    assert(hit(Page::SampleLocks, r.x, r.y) == id);
    assert(hit(Page::SampleLocks, r.x + r.w - 1, r.y + r.h - 1) == id);
  }
  // Real sample trigger/choke route, with ratchets skipping the group scan.
  sampler::Voice voices[16];
  sampler::Sample sample;
  int16_t pcm[65536]{};
  sample.data = pcm;
  sample.frames = 65536;
  voices[0].assign(&sample);
  voices[1].assign(&sample);
  sampler::PlaybackMetrics metrics{};
  voices[1].trigger(65536, parent.playback);
  assert(sampler::trigger_voice(voices, 0, 65536, parent.playback, true, false,
                                metrics));
  assert(voices[1].releasing && metrics.choked > 0);
  for (unsigned i = 0; i < 100 && voices[1].active; ++i)
    voices[1].next();
  assert(!voices[1].active);
  auto scans = metrics.choke_ops;
  assert(sampler::trigger_voice(voices, 0, 65536, parent.playback, true, true,
                                metrics));
  assert(metrics.choke_ops == scans);
  VoiceState state;
  state.event = parent;
  auto effective = state.effective(e.tracks[0]);
  assert(effective.playback.start == parent.playback.start &&
         effective.playback.end == parent.playback.end &&
         effective.slice == parent.slice);
  const auto before = allocations;
  for (unsigned i = 0; i < 100000; ++i) {
    l.slice = i & 15;
    auto event = resolve_event(0, t, 127, l);
    voices[0].trigger(65536, event.playback);
    voices[0].next();
  }
  assert(before == allocations);
  std::cout << "M15 slice locks PASS StepLocks=" << sizeof(StepLocks)
            << " align=" << alignof(StepLocks) << " Pattern=" << sizeof(Pattern)
            << " bank=" << sizeof(Engine::patterns)
            << " Engine=" << sizeof(Engine) << " two=" << 2 * sizeof(Engine)
            << "\n";
}
