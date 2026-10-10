#define P4SDM_APP 1
#include "../src/app/wav.h"
#include "../src/app/model.h"
#include "../src/engine/track_tone.h"
#include "../synthESP32LowPassFilter_E.h"
#include "fx_stubs/esp_heap_caps.h"
#include <cassert>
#include <fstream>
#include <vector>
struct SerialStub { void printf(const char *, ...) {} void println(const char *) {} } Serial;
struct EspStub { int getFreePsram() { return 0; } } ESP;
#define P4SDM_HEADLESS 1
#define free fx_test_free
#include "../fx.h"
#undef free

// Offline source-accurate Voice, velocity, filter and Delay components. This
// deliberately excludes the complete UI/audio task and is not a hardware test.
int main(int argc, char **argv) {
  if (argc != 5) return 1;
  std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
  if (!input || input.tellg() <= 0) return 2;
  std::vector<int16_t> pcm(size_t(input.tellg()) / 2);
  input.seekg(0); input.read(reinterpret_cast<char *>(pcm.data()), pcm.size()*2);
  if (!input) return 3;
  const int pitch = std::atoi(argv[3]), mode = std::atoi(argv[4]);
  if (pitch < 0 || pitch > 127 || mode < 0 || mode > 1) return 4;
  sampler::Sample source; source.data=pcm.data(); source.frames=uint32_t(pcm.size());
  sampler::Voice voice; voice.assign(&source);
  LowPassFilter filter;
  filter.setCutoffFreq(p4tone::cutoff(0));
  filter.setResonance(p4tone::resonance(0,0));
  SimpleDelay delay; assert(delay.init(88200));
  delay.setTime(12000); delay.setFeedback(120); delay.setInputLevel(160);
  std::ofstream output(argv[2], std::ios::binary);
  const int allocations=fx_alloc_calls;
  for (unsigned frame=0;frame<44100*3;++frame) {
    // Same three accepted notes for both modes, including sliced reverse Gate.
    if (frame==0 || frame==33075 || frame==70560) {
      sampler::Playback playback;
      playback.start=frame==33075 ? 8192 : 0;
      playback.end=frame==33075 ? 57344 : 65535;
      playback.reverse=frame==33075;
      playback.mode=frame==33075 ? sampler::Mode::Gate : sampler::Mode::OneShot;
      voice.trigger(sampler::pitch_increment(pitch),playback,true);
    }
    if (frame==50715) voice.release();
    const auto sample=voice.next(sampler::Interpolation(mode));
    const int32_t dry=filter.next(app::scale_velocity(sample,app::velocity_gain(100)));
    int32_t left,right;
    const int32_t send=p4tone::delay_send(dry,32);
    delay.process(soft_clip(send),soft_clip(send),left,right);
    // Fixed common gain, preserve algorithm amplitude differences.
    for (int32_t wet : {left,right}) {
      const int32_t value=(dry + wet/4)/2;
      const int16_t y=int16_t(std::max(-32768,std::min(32767,value)));
      output.write(reinterpret_cast<const char *>(&y),2);
    }
  }
  assert(fx_alloc_calls==allocations);
  delay.release();
  return output ? 0 : 5;
}
