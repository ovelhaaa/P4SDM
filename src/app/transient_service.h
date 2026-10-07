#pragma once
#include "model.h"
#include "transients.h"
namespace sampler { struct Sample; }
namespace transients {
enum class State : uint8_t { Idle, Requested, Analyzing, Ready, Applied, Cancelled, Stale, Unavailable };
struct Status {
  transient::Proposal proposal{};
  State state = State::Idle;
  unsigned progress = 0, revision = 0, request_id = 0;
};
struct Metrics {
  uint32_t requests = 0, completed = 0, cancelled = 0, stale_discarded = 0,
      frames = 0, candidates = 0, selected = 0, proposals = 0, apply = 0,
      cancel = 0, duration_max = 0, chunk_max = 0, min_count = 16, max_count = 0,
      storage_stack_min = 0;
  unsigned sensitivity = 1, target = 0;
};
extern std::atomic<bool> active;
// Audio task calls only bounded ownership/command operations, never Detector.
bool command(app::Command c, app::Engine &engine, const sampler::Sample *sample);
void invalidate(unsigned track, bool replacement = false);
void reset();
void status(Status &out);
void metrics(Metrics &out);
// Called only by sample_storage, which also owns PCM retirement/replacement.
void poll();
#if P4SDM_TRANSIENT_STRESS
// Test-only immutable PSRAM fixture, separate from the inherited 16 voices.
void qualification_setup();
void qualification_request(unsigned frames);
void qualification_poll();
extern std::atomic<unsigned> qualification_done;
extern uint32_t qualification_us[3], qualification_chunk_max;
#endif
} // namespace transients
