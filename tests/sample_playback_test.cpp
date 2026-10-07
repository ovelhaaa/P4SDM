#define P4SDM_APP 1
#include "../src/app/voice_state.h"
#include "../src/engine/track_tone.h"
#include "../synthESP32LowPassFilter_E.h"
#include "fx_stubs/esp_heap_caps.h"
struct SerialStub {
  void printf(const char *, ...) {}
  void println(const char *) {}
} Serial;
struct EspStub {
  int getFreePsram() { return 0; }
} ESP;
#define P4SDM_HEADLESS 1
#define free fx_test_free
#include "../fx.h"
#undef free
#include "../src/app/wav.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>
static unsigned allocations = 0;
void *operator new(std::size_t n) {
  ++allocations;
  if (auto p = std::malloc(n))
    return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
using namespace sampler;
int main() {
  for (uint32_t frames : {1u, 2u, 3u, 17u, 1000u, 2000000u, UINT32_MAX})
    for (uint16_t a : {0, 1, 32767, 65534, 65535})
      for (uint16_t b : {0, 1, 32767, 65534, 65535}) {
        auto r = resolve_region({a, b}, frames);
        assert(r.start < r.end && r.end <= frames);
        assert(r.start == (uint64_t(a) * frames / 65535 < frames
                               ? uint64_t(a) * frames / 65535
                               : frames - 1));
      }
  assert(resolve_region({}, 0).end == 0);
  int16_t pcm[] = {10, 20, 30, 40, 50};
  Sample sample;
  sample.data = pcm;
  sample.frames = 5;
  Voice v;
  v.assign(&sample);
  for (bool rev : {false, true})
    for (int pitch : {48, 60, 72}) {
      Playback p;
      p.reverse = rev;
      v.trigger(pitch_increment(pitch), p, false, 0);
      unsigned n = 0;
      while (v.active) {
        unsigned offset = unsigned(uint64_t(n) * pitch_increment(pitch) >> 16);
        unsigned index = rev ? 4 - offset : offset;
        assert(v.frame_index() == index && v.next() == pcm[index]);
        ++n;
      }
      assert(n == (pitch == 48 ? 10u : pitch == 60 ? 5u : 3u));
    }
  Playback trimmed{13107, 52428}; // frames [1,4)
  for (bool rev : {false, true}) {
    trimmed.reverse = rev;
    v.trigger(65536, trimmed, false, 0);
    for (unsigned n = 0; n < 3; ++n)
      assert(v.next() == pcm[rev ? 3 - n : 1 + n]);
    assert(!v.active);
  }
  for (Playback endpoint : {Playback{0, 13107}, Playback{52428, 65535}}) {
    auto r = resolve_region(endpoint, 5);
    assert(r.end - r.start == 1);
    for (bool rev : {false, true}) {
      endpoint.reverse = rev;
      v.trigger(65536, endpoint, false, 0);
      assert(v.next() == pcm[r.start] && !v.active);
    }
  }
  // M6 stereo downmix remains frame-aligned before reverse traversal.
  const uint8_t stereo[] = {10, 0, 30, 0, 40, 0, 80, 0, 100, 0, 200, 0};
  int16_t decoded[3];
  for (unsigned n = 0; n < 3; ++n)
    decoded[n] = decode_mono(stereo + n * 4, 2);
  Sample stereo_sample;
  stereo_sample.data = decoded;
  stereo_sample.frames = 3;
  stereo_sample.original_channels = 2;
  v.assign(&stereo_sample);
  v.trigger(65536, {0, 65535, true}, false, 0);
  assert(v.next() == 150 && v.next() == 60 && v.next() == 20 && !v.active);
  v.assign(&sample);
  // Exact frame guards, including arbitrary inverted/one-frame regions and
  // pitch extremes.
  unsigned before = allocations;
  for (unsigned frames : {1u, 2u, 3u, 5u}) {
    sample.frames = frames;
    for (uint16_t a : {0, 1, 32767, 65534, 65535})
      for (uint16_t b : {0, 1, 32767, 65534, 65535})
        for (bool rev : {false, true})
          for (int pitch : {0, 48, 60, 72, 127}) {
            v.trigger(pitch_increment(pitch), {a, b, rev});
            unsigned n = 0;
            while (v.active) {
              assert(v.frame_index() < frames);
              assert(v.frame_index() >= v.region.start &&
                     v.frame_index() < v.region.end);
              v.next();
              assert(++n <= 160);
            }
          }
  }
  assert(before == allocations);
  int16_t constant[512];
  for (auto &p : constant)
    p = 32000;
  sample.data = constant;
  sample.frames = 512;
  v.assign(&sample);
  v.trigger(65536);
  for (int n = 0; n < 512; ++n) {
    int gain = std::min({n, 32, 511 - n});
    assert(v.next() == 1000 * gain);
  }
  assert(!v.active && v.next() == 0);
  Playback gate;
  gate.mode = Mode::Gate;
  v.trigger(65536, gate);
  for (unsigned n = 0; n < 64; ++n)
    v.next();
  assert(v.release());
  for (int n = 0; n < 32; ++n)
    assert(v.next() == 1000 * (31 - n));
  assert(!v.active);
  v.trigger(65536);
  assert(!v.release());
  for (int n = 0; n < 64; ++n)
    v.next();
  assert(v.release(true));
  v.next();
  gate.reverse = true;
  gate.start = 10000;
  gate.end = 50000;
  v.trigger(131072, gate);
  assert(!v.releasing && v.attack == 0 && v.position == 0 &&
         v.playback.reverse && v.increment == 131072);
  assert(v.next() == 0);
  v.set_increment(32768);
  assert(v.increment == 32768);
  // Choke routing uses active voice's accepted group; no Track lookup or FX
  // reset.
  Voice voices[16];
  for (auto &voice : voices)
    voice.assign(&sample);
  PlaybackMetrics metrics;
  Playback group1;
  group1.choke = 1;
  Playback group2;
  group2.choke = 2;
  trigger_voice(voices, 0, 65536, group1, false, false, metrics);
  trigger_voice(voices, 2, 65536, group2, false, false, metrics);
  trigger_voice(voices, 1, 65536, group1, false, false, metrics);
  assert(voices[0].releasing && !voices[1].releasing && !voices[2].releasing);
  trigger_voice(voices, 0, 65536, group1, false, true, metrics);
  assert(!voices[0].releasing && !voices[1].releasing &&
         metrics.choke_ops == 3 && metrics.choked == 1);
  trigger_voice(voices, 3, 65536, {}, false, false, metrics);
  assert(!voices[2].releasing);
  // Replacement immediately stops release before acknowledgement permits
  // freeing old PCM.
  Transfer transfer;
  transfer.active[0] = &sample;
  voices[0].release(true);
  Sample replacement;
  replacement.data = pcm;
  replacement.frames = 5;
  transfer.publish(&replacement, 0);
  assert(transfer.consume(voices) == 0);
  assert(!voices[0].active && !voices[0].releasing &&
         voices[0].sample == &replacement && transfer.retired == &sample);
  app::Queue<3> pad_queue;
  app::PadGate pad;
  assert(pad.press(5, pad_queue));
  assert(pad_queue.push({app::Kind::Trigger, 9, 0})); // full: release must wait
  pad.release(pad_queue);
  assert(pad.release_pending && pad.track == 5);
  assert(!pad.press(7, pad_queue));
  app::Command pad_command{};
  assert(pad_queue.pop(pad_command) && pad_command.track == 5);
  pad.flush(pad_queue);
  assert(!pad.release_pending && pad.track == -1);
  assert(pad_queue.pop(pad_command) && pad_command.track == 9);
  assert(pad_queue.pop(pad_command) &&
         pad_command.kind == app::Kind::GateRelease && pad_command.track == 5);
  assert(pad.press(7, pad_queue));
  pad.release(pad_queue);
  assert(pad_queue.pop(pad_command) && pad_command.track == 7 &&
         pad_command.kind == app::Kind::Trigger);
  assert(pad_queue.pop(pad_command) && pad_command.track == 7 &&
         pad_command.kind == app::Kind::GateRelease);
  app::Engine e;
  e.tracks[0].assigned();
  e.tracks[1].assigned();
  e.tracks[0].playback = gate;
  e.tracks[0].playback.choke = 1;
  auto &pattern = e.patterns[0];
  pattern.track_steps[0] = 1;
  pattern.meta[0][0] = {127, 100, 4};
  e.apply({app::Kind::Bpm, 0, 240});
  e.apply({app::Kind::Play, 0, 1});
  voices[0].assign(&sample);
  voices[1].assign(&sample);
  metrics = {};
  app::TriggerEvent parent{};
  unsigned hits = 0, released_steps = 0;
  auto hit = [&](app::TriggerEvent ev, bool ratchet) {
    if (!hits)
      parent = ev;
    else {
      assert(ratchet && ev.playback.start == parent.playback.start &&
             ev.playback.reverse == parent.playback.reverse &&
             ev.playback.choke == 1);
    }
    trigger_voice(voices, ev.track, pitch_increment(ev.pitch), ev.playback,
                  ev.sequenced, ratchet, metrics);
    ++hits;
  };
  auto release = [&](int t) {
    if (t == 0)
      ++released_steps;
    if (voices[t].sequenced)
      metrics.releases += voices[t].release();
  };
  e.sample(hit, release);
  e.apply({app::Kind::SampleStart, 0, 45000});
  e.apply({app::Kind::SampleReverse, 0, 0});
  e.apply({app::Kind::SampleChoke, 0, 8});
  assert(voices[0].playback.start == gate.start && voices[0].playback.reverse);
  for (unsigned n = 1; n <= 2757; ++n)
    e.sample(hit, release);
  assert(hits == 4 && released_steps == 2 && voices[0].releasing &&
         metrics.choke_ops == 1);
  // Rejected/suppressed parents never execute a choke scan; manual audition
  // ignores performance gates.
  for (int suppressed = 0; suppressed < 3; ++suppressed) {
    e.apply({app::Kind::Play, 0, 0});
    e.tracks[0].muted = suppressed == 1;
    e.solos = suppressed == 2 ? 2 : 0;
    pattern.meta[0][0].probability = suppressed == 0 ? 0 : 100;
    e.apply({app::Kind::Play, 0, 1});
    unsigned count = 0;
    e.sample([&](app::TriggerEvent) { ++count; });
    assert(count == 0);
    e.audition(0, [&](app::TriggerEvent ev) {
      assert(!ev.sequenced);
      ++count;
    });
    assert(count == 1);
  }
  app::Track first;
  first.playback = gate;
  first.assigned();
  assert(first.playback.start == 0 && first.playback.end == 65535 &&
         first.playback.mode == Mode::OneShot);
  first.playback = gate;
  first.assigned();
  assert(first.playback.start == gate.start);
  e.apply({app::Kind::SampleMode, 0, 1});
  e.apply({app::Kind::SampleChoke, 0, 999});
  e.apply({app::Kind::SampleResetRegion, 0, 0});
  assert(e.tracks[0].playback.start == 0 && e.tracks[0].playback.end == 65535 &&
         !e.tracks[0].playback.reverse &&
         e.tracks[0].playback.mode == Mode::Gate &&
         e.tracks[0].playback.choke == 8);
  before = allocations;
  for (unsigned n = 0; n < 100000; ++n) {
    e.apply({app::Kind::SampleStart, uint8_t(n % 16), int(n)});
    trigger_voice(voices, n % 16, 65536,
                  {0, 65535, bool(n & 1), Mode::Gate, uint8_t(n % 9)}, false,
                  false, metrics);
    voices[n % 16].next();
    voices[n % 16].release();
  }
  assert(before == allocations);
  for (int id = 124; id <= 130; ++id) {
    auto r = app::widget(id);
    assert(r.h >= 52);
    for (int x : {r.x, r.x + r.w - 1})
      for (int y : {r.y, r.y + r.h - 1})
        assert(app::hit(app::Page::SamplePlayback, x, y) == id);
  }
  auto entry = app::widget(123);
  assert(app::hit(app::Page::Sample, entry.x, entry.y) == 123);
  // Normal default central PCM is bit-identical, excluding intentional fade
  // edges.
  v.assign(&sample);
  v.trigger(65536);
  for (int n = 0; n < 512; ++n) {
    int16_t value = v.next();
    if (n >= 32 && n < 480)
      assert(value == constant[n]);
  }
  // Actual post-source filter/send/Delay route: choke retires dry PCM,
  // preserving filter history and already-written stereo Delay tails.
  SimpleDelay delay;
  assert(delay.init(512));
  delay.setTime(128);
  delay.setFeedback(120);
  delay.setInputLevel(160);
  LowPassFilter filter;
  filter.reset();
  app::Track tone_track;
  tone_track.assigned();
  tone_track.filter_cutoff = 100;
  tone_track.filter_resonance = 50;
  auto event = app::resolve_event(
      0, tone_track, 127,
      {uint8_t(app::FILTER_CUTOFF_LOCK | app::DELAY_SEND_LOCK), 0, 0, 0, 0, 40,
       0, 127});
  app::VoiceState tone_voice;
  tone_voice.event = event;
  tone_voice.apply(event, [&](uint8_t c, uint8_t r) {
    filter.setCutoffFreq(p4tone::cutoff(c));
    filter.setResonance(p4tone::resonance(c, r));
  });
  v.assign(&sample);
  v.trigger(65536);
  bool tail = false;
  unsigned pcm_zero = 0;
  before = allocations;
  for (int n = 0; n < 400; ++n) {
    if (n == 64)
      assert(v.release(true));
    int16_t dry = v.next();
    if (n >= 96) {
      assert(dry == 0 && !v.active);
      ++pcm_zero;
    }
    int16_t filtered = int16_t(filter.next(
        app::scale_velocity(dry, app::velocity_gain(event.velocity))));
    int32_t left = 0, right = 0;
    int16_t send =
        int16_t(p4tone::resolved_delay_send(filtered, tone_voice.delay_send));
    delay.process(send, send, left, right);
    if (n >= 128 && (left || right))
      tail = true;
  }
  assert(tail && pcm_zero > 0 && allocations == before);
  std::printf(
      "M13 "
      "region/reverse/pitch/fades/gate/choke/retrigger/snapshots/replacement/"
      "no-allocation/UI PASS; Voice=%zu Track=%zu Event=%zu Engine=%zu\n",
      sizeof(Voice), sizeof(app::Track), sizeof(app::TriggerEvent),
      sizeof(app::Engine));
}
