#pragma once
#include "model.h"
namespace app {
// Routing belongs to the triggered voice, independently of canonical Track edits.
// Only trigger, explicit source/assignment or reset transitions may select it.
class VoiceRouting {
  uint16_t samples_ = 0;
public:
  void trigger(unsigned track, bool sample) {
    const uint16_t bit = uint16_t(1u << track);
    samples_ = uint16_t((samples_ & ~bit) | (sample ? bit : 0));
  }
  bool pcm(unsigned track) const { return samples_ & (1u << track); }
};
static_assert(sizeof(VoiceRouting) == 2);
// Immutable accepted event; base edits reconstruct only its unlocked controls.
inline StepLocks event_locks(const TriggerEvent &e) {
  return {uint8_t(e.locked_mask & ~SLICE_LOCK),
          e.pitch,
          e.volume,
          e.pan,
          e.wave,
          e.filter_cutoff,
          e.filter_resonance,
          e.delay_send};
}
struct VoiceState {
  TriggerEvent event{};
  uint8_t cutoff = 0, resonance = 0, delay_send = 127;
  bool applied = false;
  TriggerEvent effective(const Track &base) const {
    auto effective =
        resolve_event(event.track, base, event.velocity, event_locks(event));
    // Base edits refresh live controls, never re-resolve the accepted Slice
    // Lock.
    effective.playback = event.playback;
    effective.slice = base.sample ? event.slice : 0;
    effective.locked_mask |= base.sample ? event.locked_mask & SLICE_LOCK : 0;
    return effective;
  }
  TriggerEvent effective(const Track &base, bool sample) const {
    if (sample == base.sample) return effective(base);
    // A captured voice can keep its source after the canonical Source changes.
    // Resolve live base controls under that voice's source so Wave/Slice lock
    // applicability and the pitch domain remain attached to the sounding voice.
    Track sounding = base;
    sounding.sample = sample;
    sounding.pitch = sample ? base.sample_pitch : base.synth_pitch;
    return effective(sounding);
  }
  // Return coefficient/send changes and redundant coefficient work avoided.
  // Compare canonical pairs before mapping: the resolved 0/0 exception stays
  // entirely owned by the exact M11 DSP helper. No filter history is reset.
  template <class SetTone>
  unsigned apply(const TriggerEvent &e, SetTone set_tone, bool tone = true) {
    unsigned result = 0;
    if (tone) {
      if (!applied || cutoff != e.filter_cutoff ||
          resonance != e.filter_resonance) {
        set_tone(e.filter_cutoff, e.filter_resonance);
        cutoff = e.filter_cutoff;
        resonance = e.filter_resonance;
        applied = true;
        result |= 1;
      } else
        result |= 4;
    }
    if (delay_send != e.delay_send) {
      delay_send = e.delay_send;
      result |= 2;
    }
    return result;
  }
};
static_assert(std::is_trivially_copyable<TriggerEvent>::value);
static_assert(sizeof(TriggerEvent) == 20);
} // namespace app
