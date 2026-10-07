#pragma once
#include "wav.h"
#include <atomic>
namespace samples {
extern sampler::Voice voices[16];
extern sampler::Transfer transfer;
extern std::atomic<unsigned> qualification_phase;
extern std::atomic<bool> load_active;
struct Preview {
  sampler::Waveform waveform{};
  uint32_t frames = 0;
  char name[96]{};
};
extern std::atomic<unsigned> waveform_builds, waveform_build_max,
    waveform_large_us;
void analyze(sampler::Sample &s, bool responsive = true);
void publish_preview(unsigned track, const sampler::Sample *s);
void preview(unsigned track, Preview &out);
void start();
bool initialized();
bool request(unsigned track, int index);
bool assigned(unsigned track);
bool has_sample(unsigned track);
unsigned count();
bool busy();
unsigned revision();
void describe(int index, char *destination, unsigned size);
void track_name(unsigned track, char *destination, unsigned size);
void message(char *destination, unsigned size);
} // namespace samples
