#pragma once
#include "wav.h"
#include <atomic>
namespace samples {
extern sampler::Voice voices[16];
extern sampler::Transfer transfer;
extern std::atomic<unsigned> qualification_phase;
extern std::atomic<bool> load_active;
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
