#define P4SDM_APP 1
#include "../src/app/voice_state.h"
#include "../src/engine/track_tone.h"
#include "../synthESP32LowPassFilter_E.h"
#include "fx_stubs/esp_heap_caps.h"
#include "m11_filter.h"
#include "m11_tone.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
int main() {
  static M11Filter oldFilters[18]{};
  LowPassFilter newFilters[18];
  for (int t = 0; t < 18; ++t) {
    oldFilters[t].setResonance(255);
    oldFilters[t].setCutoffFreq(255);
    newFilters[t].setResonance(p4tone::resonance(0, 0));
    newFilters[t].setCutoffFreq(p4tone::cutoff(0));
  }
  SimpleDelay oldDelay, newDelay;
  assert(oldDelay.init(88200) && newDelay.init(88200));
  for (auto *d : {&oldDelay, &newDelay}) {
    d->setTime(12000);
    d->setFeedback(120);
    d->setInputLevel(160);
  }
  const int allocations = fx_alloc_calls;
  app::VoiceState voices[16];
  app::Track tracks[16];
  uint32_t rng = 42;
  int zeroTail = 0;
  for (int i = 0; i < 132352; ++i) {
    int32_t oldBus[2]{}, newBus[2]{}, oldDry[2]{}, newDry[2]{};
    for (int t = 0; t < 16; ++t) {
      rng ^= rng << 13;
      rng ^= rng >> 17;
      rng ^= rng << 5;
      // Deterministic source fixtures alternate oscillator-like and resident
      // PCM.
      int16_t source = i < 2000 ? int16_t(32767)
                       : t % 2  ? int16_t(rng)
                                : int16_t(30000 * std::sin(i * .025 * (t + 1)));
      auto &track = tracks[t];
      if (i % 257 == 0) {
        track.filter_cutoff = uint8_t((i / 257 + t * 7) % 128);
        track.filter_resonance = uint8_t((i / 257 * 11 + t * 13) % 128);
        track.delay_send = uint8_t((i / 257 * 17 + t * 19) % 128);
      }
      if (i >= 100000)
        track.delay_send = 0;
      oldFilters[t].setResonance(
          m11tone::resonance(track.filter_cutoff, track.filter_resonance));
      oldFilters[t].setCutoffFreq(m11tone::cutoff(track.filter_cutoff));
      auto event = app::resolve_event(t, track, i % 127 + 1,
                                      {uint8_t(t % 2 ? 6 : 0), 0, 20,
                                       int8_t(t % 3 == 0   ? -127
                                              : t % 3 == 1 ? 127
                                                           : 0),
                                       0});
      voices[t].event = event;
      voices[t].apply(event, [&](uint8_t c, uint8_t r) {
        newFilters[t].setResonance(p4tone::resonance(c, r));
        newFilters[t].setCutoffFreq(p4tone::cutoff(c));
      });
      int input =
          app::scale_velocity(source, app::velocity_gain(event.velocity));
      int a = oldFilters[t].next(input), b = newFilters[t].next(input);
      assert(a == b);
      for (int c = 0; c < 2; ++c) {
        int gain = app::event_channel_gain(event.volume, event.pan, c == 1);
        int oa = (a * gain * 255) >> 16, nb = (b * gain * 255) >> 16;
        oldDry[c] += oa;
        newDry[c] += nb;
        // Last section changes send to zero; dry must stay identical.
        oldBus[c] += m11tone::delay_send(oa, track.delay_send);
        newBus[c] += p4tone::resolved_delay_send(nb, voices[t].delay_send);
      }
    }
    for (int c = 0; c < 2; ++c) {
      assert(oldDry[c] == newDry[c]);
      assert(oldBus[c] == newBus[c]);
      assert(oldFilters[16 + c].next(oldDry[c]) ==
             newFilters[16 + c].next(newDry[c]));
    }
    int32_t ol, orr, nl, nr;
    oldDelay.process(soft_clip(oldBus[0]), soft_clip(oldBus[1]), ol, orr);
    newDelay.process(soft_clip(newBus[0]), soft_clip(newBus[1]), nl, nr);
    assert(ol == nl && orr == nr);
    assert(((int64_t(ol) * 100) >> 8) == ((int64_t(nl) * 100) >> 8));
    if (i >= 100000) {
      assert(newBus[0] == 0 && newBus[1] == 0);
      zeroTail += nl != 0 || nr != 0;
    }
  }
  assert(zeroTail > 0);
  assert(fx_alloc_calls == allocations);
  puts(
      "Actual M11 filter/mapping/send + M12 voice state: 132352 frames, "
      "varying unlocked tone/send bit-exact; zero-send dry unchanged and tails "
      "retained PASS");
}
