#pragma once
#include "midi_events.h"
namespace midi {
extern Queue<128> input;
extern std::atomic<uint32_t> generation;
struct Diagnostics {
  std::atomic<uint32_t> accepted{0},rx_parse_max{0},parse_queue_max{0},queue_block_max{0},bpm_milli{0},clock_jitter{0},missing{0},stale{0};
};
extern Diagnostics diagnostics;
bool start_host();
} // namespace midi
