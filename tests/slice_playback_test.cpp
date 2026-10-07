#include "../src/app/model.h"
#include "../src/app/wav.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
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
using namespace sampler;
int main() {
  int16_t pcm[32];
  for (int i = 0; i < 32; ++i)
    pcm[i] = int16_t(i + 1);
  Sample sample;
  sample.data = pcm;
  sample.frames = 32;
  Voice v;
  v.assign(&sample);
  app::Track t;
  t.assigned();
  t.slices.divide({}, 4);
  t.slice_enabled = true;
  for (unsigned slice = 0; slice < 4; ++slice)
    for (bool reverse : {false, true})
      for (int pitch : {48, 60, 72})
        for (Mode mode : {Mode::Gate, Mode::OneShot}) {
          t.slices.selected = uint8_t(slice);
          t.playback.reverse = reverse;
          t.playback.mode = mode;
          t.pitch = pitch;
          auto e = app::resolve_event(0, t, 127);
          assert(e.slice == slice + 1);
          auto region = resolve_region(e.playback, 32);
          v.trigger(pitch_increment(e.pitch), e.playback, false, 0);
          uint64_t distance = 0;
          while (v.active) {
            unsigned offset = unsigned(distance >> 16);
            assert(
                v.next() ==
                pcm[reverse ? region.end - 1 - offset : region.start + offset]);
            distance += pitch_increment(pitch);
          }
        }
  t.slice_enabled = false;
  assert(app::resolve_event(0, t, 127).slice == 0);
  auto audition = app::resolve_event(0, t, 127, {}, 2);
  assert(audition.slice == 3 && t.slices.selected == 3);
  for (Slice corrupt :
       {Slice{0, 0}, Slice{65535, 0}, Slice{50000, 100}, Slice{65534, 65535}}) {
    t.slices.slices[0] = corrupt;
    auto event = app::resolve_event(0, t, 127, {}, 0);
    v.trigger(65536, event.playback, false, 0);
    assert(v.region.start < v.region.end && v.region.end <= 32);
    while (v.active) {
      assert(v.frame_index() < 32);
      v.next();
    }
  }
  app::Engine engine;
  auto &track = engine.tracks[0];
  track.assigned();
  track.slices.divide({}, 8);
  track.slice_enabled = true;
  track.slices.selected = 1;
  engine.patterns[0].track_steps[0] = 3;
  engine.patterns[0].meta[0][0] = {100, 100, 4};
  engine.apply({app::Kind::Play, 0, 1});
  app::TriggerEvent parent{};
  unsigned hits = 0;
  auto trigger = [&](app::TriggerEvent e, bool ratchet) {
    if (!hits) {
      parent = e;
      assert(!ratchet && e.slice == 2);
    } else if (hits < 4)
      assert(ratchet && std::memcmp(&e, &parent, sizeof(e)) == 0);
    else
      assert(!ratchet && e.slice == 8);
    ++hits;
  };
  engine.sample(trigger);
  v.trigger(65536, parent.playback, false, 0);
  auto accepted = v.region;
  engine.apply({app::Kind::SliceSelect, 0, 7});
  app::Command edit{app::Kind::SliceStart, 0, 1};
  edit.step = 1;
  engine.apply(edit);
  track.slices.erase(1);
  track.slices.reset({});
  track.slices.divide({}, 8);
  track.slices.selected = 7;
  assert(v.region.start == accepted.start && v.region.end == accepted.end);
  while (hits < 5)
    engine.sample(trigger);
  engine.patterns[0].meta[0][2] = {100, 0, 4};
  auto rejected = hits;
  while (engine.step < 2)
    engine.sample(trigger);
  assert(hits == rejected);
  // Coherent replacement: consume stops old voice; accepted normalized region
  // remaps.
  Transfer transfer;
  Voice voices[16];
  Sample old_sample = sample, next_sample;
  int16_t newer[64];
  for (int i = 0; i < 64; ++i)
    newer[i] = 1000 + i;
  next_sample.data = newer;
  next_sample.frames = 64;
  build_waveform(old_sample.waveform, old_sample.data, old_sample.frames);
  build_waveform(next_sample.waveform, newer, 64);
  transfer.publish(&old_sample, 0);
  assert(transfer.consume(voices) == 0);
  voices[0].trigger(65536, parent.playback, false, 0);
  auto retained = track.slices;
  transfer.publish(&next_sample, 0);
  assert(transfer.consume(voices) == 0);
  track.assigned();
  assert(!voices[0].active && transfer.retired.load() == &old_sample);
  assert(!std::memcmp(&retained, &track.slices, sizeof(retained)));
  std::memset(pcm, 0, sizeof(pcm));
  voices[0].trigger(65536, parent.playback, false, 0);
  assert(voices[0].region.start == resolve_region(parent.playback, 64).start);
  while (voices[0].active)
    assert(voices[0].next() >= 1000);
  assert(transfer.active[0]->waveform.columns[0].min == 1000);
  const auto allocated = allocations;
  for (unsigned i = 0; i < 100000; ++i) {
    track.slices.divide({}, 16);
    track.slices.edit(i % 16, true, 100);
    auto event = app::resolve_event(0, track, 127, {}, i % 16);
    v.trigger(65536, event.playback);
    v.next();
  }
  assert(allocations == allocated);
}
