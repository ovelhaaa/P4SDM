#include "transient_service.h"
#include <Arduino.h>
#include "samples.h"
#include "projects.h"
#include "esp_timer.h"
#if P4SDM_TRANSIENT_STRESS
#include "esp_heap_caps.h"
#endif
namespace transients {
std::atomic<bool> active{false};
namespace {
portMUX_TYPE mutex = portMUX_INITIALIZER_UNLOCKED;
Status published;
Metrics counters;
uint32_t generations[16]{}, revisions[16]{};
const sampler::Sample *requested_pcm = nullptr;
transient::Sensitivity sensitivity = transient::Sensitivity::Medium;
transient::Target target = transient::Target::Natural;
unsigned ticket = 0;
transient::Owner owner(unsigned track, const app::Track &t) {
  return {generations[track], revisions[track], t.playback.start, t.playback.end, uint8_t(track)};
}
void state(State value) { published.state = value; ++published.revision; }
}
void invalidate(unsigned track, bool replacement) {
  if (track >= 16) return;
  portENTER_CRITICAL(&mutex);
  ++revisions[track];
  if (replacement) ++generations[track];
  if (published.proposal.owner.track == track &&
      (published.state == State::Requested || published.state == State::Analyzing || published.state == State::Ready)) {
    ++ticket; ++counters.stale_discarded; state(State::Stale);
  }
  portEXIT_CRITICAL(&mutex);
}
void reset() { for (unsigned t = 0; t < 16; ++t) invalidate(t, true); }
bool command(app::Command c, app::Engine &engine, const sampler::Sample *sample) {
  if (c.track >= 16) return false;
  if (c.kind == app::Kind::Source || c.kind == app::Kind::SampleStart ||
      c.kind == app::Kind::SampleEnd || c.kind == app::Kind::SampleResetRegion)
    invalidate(c.track);
  if (c.kind != app::Kind::TransientAnalyze && c.kind != app::Kind::TransientApply &&
      c.kind != app::Kind::TransientCancel) return false;
  bool applied = false;
  portENTER_CRITICAL(&mutex);
  auto &track = engine.tracks[c.track];
  if (c.kind == app::Kind::TransientCancel) {
    ++counters.cancel;
    if (published.state == State::Requested || published.state == State::Analyzing || published.state == State::Ready) {
      ++ticket; ++counters.cancelled; state(State::Cancelled);
    }
  } else if (c.kind == app::Kind::TransientAnalyze) {
    // One bounded job; the UI can cancel before issuing another request.
    if (!active.load() && published.state != State::Requested && !projects::busy()) {
      ++ticket; ++counters.requests;
      published.request_id = ticket;
      published.proposal = {}; published.proposal.owner = owner(c.track, track);
      published.progress = 0;
      const unsigned mode = unsigned(c.value) & 255;
      target = mode == 4 ? transient::Target::Four : mode == 8 ? transient::Target::Eight :
               mode == 16 ? transient::Target::Sixteen : transient::Target::Natural;
      sensitivity = transient::Sensitivity(app::clamp((c.value >> 8) & 255, 0, 2));
      counters.target = unsigned(target); counters.sensitivity = unsigned(sensitivity);
      requested_pcm = sample;
      state(track.sample && sample && sample->data ? State::Requested : State::Unavailable);
    }
  } else if (published.state == State::Ready && published.proposal.owner.track == c.track &&
             uint32_t(c.value) == published.request_id) {
    if (track.sample && sample == requested_pcm && published.proposal.matches(owner(c.track, track))) {
      track.slices = published.proposal.bank;
      track.slice_enabled = true;
      ++counters.apply;
      counters.min_count = std::min<uint32_t>(counters.min_count, track.slices.count);
      counters.max_count = std::max<uint32_t>(counters.max_count, track.slices.count);
      state(State::Applied); applied = true;
    } else { ++counters.stale_discarded; state(State::Stale); }
  }
  portEXIT_CRITICAL(&mutex);
  return applied;
}
void status(Status &out) {
  portENTER_CRITICAL(&mutex); out = published; portEXIT_CRITICAL(&mutex);
}
void metrics(Metrics &out) {
  portENTER_CRITICAL(&mutex); out = counters; portEXIT_CRITICAL(&mutex);
}
void poll() {
  const sampler::Sample *pcm;
  transient::Owner captured;
  transient::Target mode;
  transient::Sensitivity sense;
  unsigned request_ticket;
  portENTER_CRITICAL(&mutex);
  if (published.state != State::Requested) { portEXIT_CRITICAL(&mutex); return; }
  pcm = requested_pcm; captured = published.proposal.owner;
  mode = target; sense = sensitivity; request_ticket = ticket;
  state(State::Analyzing); active.store(true);
  portEXIT_CRITICAL(&mutex);
  // Storage owns sample lifetimes. No project poll or load/retirement can run
  // during this call. Validate pointer before dereferencing an old request.
  if (!samples::resident(captured.track, pcm)) {
    portENTER_CRITICAL(&mutex);
    if (ticket == request_ticket) { ++counters.stale_discarded; state(State::Stale); }
    active.store(false); portEXIT_CRITICAL(&mutex); return;
  }
  sampler::Playback domain; domain.start = captured.start; domain.end = captured.end;
  transient::Detector detector(pcm->data, pcm->frames, pcm->channels, pcm->rate, domain, sense);
  const auto began = esp_timer_get_time();
  bool done = false, abandoned = false;
  uint32_t chunk_max = 0;
  while (!done) {
    const auto chunk_start = esp_timer_get_time();
    done = detector.process();
    chunk_max = std::max(chunk_max, uint32_t(esp_timer_get_time() - chunk_start));
    portENTER_CRITICAL(&mutex);
    abandoned = ticket != request_ticket;
    if (!abandoned) {
      published.progress = unsigned(uint64_t(detector.analyzed) * 100 /
          std::max<uint32_t>(1u, sampler::resolve_region(domain, pcm->frames).end - sampler::resolve_region(domain, pcm->frames).start));
      ++published.revision;
    }
    portEXIT_CRITICAL(&mutex);
    if (abandoned) break;
    vTaskDelay(1); // once per 4096 source frames, not per audio/analysis block
  }
  const auto bank = detector.select(mode);
  portENTER_CRITICAL(&mutex);
  counters.frames += detector.analyzed;
  counters.duration_max = std::max(counters.duration_max, uint32_t(esp_timer_get_time() - began));
  counters.chunk_max = std::max(counters.chunk_max, chunk_max);
  const uint32_t stack_free = uxTaskGetStackHighWaterMark(nullptr);
  counters.storage_stack_min = counters.storage_stack_min ? std::min(counters.storage_stack_min, stack_free) : stack_free;
  if (!abandoned && request_ticket == ticket) {
    ++counters.completed; ++counters.proposals;
    counters.candidates = detector.count; counters.selected = bank.count;
    published.proposal.bank = bank; published.progress = 100; state(State::Ready);
  }
  active.store(false); portEXIT_CRITICAL(&mutex);
}
#if P4SDM_TRANSIENT_STRESS
std::atomic<unsigned> qualification_done{0};
uint32_t qualification_us[3]{}, qualification_chunk_max = 0;
namespace {
int16_t *qualification_pcm = nullptr;
std::atomic<unsigned> qualification_job{0};
}
void qualification_setup() {
  qualification_pcm = static_cast<int16_t *>(heap_caps_malloc(4194304, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!qualification_pcm) { Serial.println("[M19 fixture] FAIL allocation"); return; }
  uint32_t random = 17;
  for (unsigned n = 0; n < 2097152; ++n) {
    random = random * 1664525 + 1013904223;
    const unsigned phase = n % 5000;
    qualification_pcm[n] = phase >= 3000 && phase < 3500
        ? int16_t((int(random >> 16) - 32768) * int(3500 - phase) / 500) : 0;
  }
}
void qualification_request(unsigned frames) { qualification_job.store(frames, std::memory_order_release); }
void qualification_poll() {
  const unsigned frames = qualification_job.exchange(0, std::memory_order_acquire);
  if (!frames || !qualification_pcm) return;
  active.store(true);
  transient::Detector detector(qualification_pcm, frames, 1, 44100, {}, transient::Sensitivity::Medium);
  const auto start = esp_timer_get_time();
  bool done = false;
  uint32_t worst = 0;
  while (!done) {
    const auto began = esp_timer_get_time();
    done = detector.process();
    worst = std::max(worst, uint32_t(esp_timer_get_time() - began));
    vTaskDelay(1);
  }
  const auto proposal = detector.select(transient::Target::Sixteen);
  const unsigned slot = frames == 44100 ? 0 : frames == 176400 ? 1 : 2;
  qualification_us[slot] = uint32_t(esp_timer_get_time() - start);
  qualification_chunk_max = std::max(qualification_chunk_max, worst);
  portENTER_CRITICAL(&mutex);
  counters.frames += detector.analyzed;
  counters.candidates = detector.count; counters.selected = proposal.count;
  counters.chunk_max = std::max(counters.chunk_max, worst);
  counters.duration_max = std::max(counters.duration_max, qualification_us[slot]);
  const uint32_t stack_free = uxTaskGetStackHighWaterMark(nullptr);
  counters.storage_stack_min = counters.storage_stack_min ? std::min(counters.storage_stack_min, stack_free) : stack_free;
  portEXIT_CRITICAL(&mutex);
  active.store(false); qualification_done.fetch_add(1, std::memory_order_release);
}
#endif
} // namespace transients
