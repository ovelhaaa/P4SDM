#include "../src/app/transient_service.cpp"
#include <cassert>
#include <cstring>
#include <iostream>
static bool allocation_forbidden = false;
void *operator new(std::size_t n) {
  assert(!allocation_forbidden);
  if (auto p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
static app::Engine engine;
static sampler::Sample sample;
static const sampler::Sample *resident_pcm = &sample;
static unsigned interruption = 0;
namespace projects { bool busy() { return false; } }
namespace samples { bool resident(unsigned t, const sampler::Sample *p) { return t == 3 && p == resident_pcm; } }
void vTaskDelay(unsigned) {
  if (interruption == 1) transients::command({app::Kind::TransientCancel, 3, 0}, engine, &sample);
  if (interruption == 2) { transients::invalidate(3, true); resident_pcm = nullptr; }
  if (interruption == 3) {
    const app::Command c{app::Kind::SampleStart, 3, 1000};
    transients::command(c, engine, &sample); engine.apply(c);
  }
  interruption = 0;
}
static transients::Status status() { transients::Status s; transients::status(s); return s; }
static void request(unsigned target = 16) {
  transients::command({app::Kind::TransientAnalyze, 3, int(target | 256)}, engine, &sample);
}
int main() {
  static std::array<int16_t, 100000> pcm{};
  for (unsigned i = 0; i < 20; ++i) pcm[3000 + i * 4500] = int16_t(10000 + i * 1000);
  sample.data = pcm.data(); sample.frames = unsigned(pcm.size());
  allocation_forbidden = true;
  engine.tracks[3].sample = true;
  engine.tracks[3].slices.divide({}, 8);
  const auto original = engine.tracks[3];
  request(); transients::poll(); assert(status().state == transients::State::Ready);
  assert(!std::memcmp(&original, &engine.tracks[3], sizeof(original)));
  transients::command({app::Kind::TransientCancel, 3, 0}, engine, &sample);
  assert(!std::memcmp(&original, &engine.tracks[3], sizeof(original)));
  for (unsigned scenario : {1u, 2u, 3u}) {
    resident_pcm = &sample; engine.tracks[3] = original; request(); interruption = scenario;
    transients::poll();
    assert(status().state == (scenario == 1 ? transients::State::Cancelled : transients::State::Stale));
    assert(!transients::command({app::Kind::TransientApply, 3, int(status().request_id)}, engine, &sample));
    assert(!std::memcmp(&original.slices, &engine.tracks[3].slices, sizeof(original.slices)));
  }
  resident_pcm = &sample; engine.tracks[3] = original;
  request(); transients::poll();
  assert(status().proposal.bank.count == 16);
  assert(!transients::command({app::Kind::TransientApply, 3, int(status().request_id - 1)}, engine, &sample));
  assert(!transients::command({app::Kind::TransientApply, 4, 0}, engine, &sample));
  assert(transients::command({app::Kind::TransientApply, 3, int(status().request_id)}, engine, &sample));
  const auto bank = engine.tracks[3].slices;
  assert(bank.count == 16 && bank.selected == 0 && engine.tracks[3].slice_enabled);
  assert(engine.tracks[3].pitch == original.pitch && engine.tracks[3].playback.reverse == original.playback.reverse);
  assert(!project::musical(app::Kind::TransientAnalyze) && !project::musical(app::Kind::TransientCancel));
  // The firmware marks SliceDivide dirty only after command returns true.
  assert(project::musical(app::Kind::SliceDivide));
  sampler::Voice voice; voice.assign(&sample);
  for (unsigned slice = 0; slice < 16; ++slice) {
    app::StepLocks locks; locks.mask = app::SLICE_LOCK; locks.slice = uint8_t(slice);
    auto event = app::resolve_event(3, engine.tracks[3], 127, locks);
    assert(event.slice == slice + 1 && event.playback.start == bank.slices[slice].start);
    event.playback.reverse = true; event.playback.mode = sampler::Mode::Gate; event.playback.choke = 4;
    voice.trigger(65536, event.playback, true);
    const auto playing = voice.region;
    engine.tracks[3].slices.edit(slice, true, bank.slices[slice].start + 1);
    assert(voice.region.start == playing.start && voice.region.end == playing.end);
    assert(voice.playback.reverse && voice.release());
  }
  engine.tracks[3].slices = bank;
  engine.patterns[0].track_steps[3] = 0xffff;
  for (auto &l : engine.patterns[0].locks[3]) { l.mask = app::SLICE_LOCK; l.slice = 15; }
  engine.apply({app::Kind::Play, 0, 1}); engine.apply({app::Kind::PerfRepeatStart, 0, 8});
  app::TriggerEvent accepted;
  engine.sample([&](app::TriggerEvent e) { accepted = e; });
  assert(accepted.slice == 16);
  engine.tracks[3].slices.reset({});
  unsigned children = 0;
  for (unsigned i = 0; i < 10000; ++i)
    engine.sample([&](app::TriggerEvent e) {
      assert(!std::memcmp(&accepted, &e, sizeof(e))); ++children;
    });
  assert(children > 7);
  // Reverted trims and same-pointer replacement still invalidate generations.
  engine.tracks[3] = original; request(); transients::poll();
  transients::invalidate(3); transients::invalidate(3);
  assert(status().state == transients::State::Stale);
  assert(!transients::command({app::Kind::TransientApply, 3, int(status().request_id)}, engine, &sample));
  transients::Metrics m; transients::metrics(m);
  assert(m.stale_discarded >= 3 && m.cancelled >= 2 && m.apply == 1);
  allocation_forbidden = false;
  std::cout << "M19 proposal/cancel/owner/stale/SliceLock/reverse/Gate/Repeat PASS\n";
}

