#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <type_traits>
namespace app {
constexpr int clamp(int v, int lo, int hi) {
  return v < lo ? lo : v > hi ? hi : v;
}
enum class Kind : uint8_t {
  Play,
  Bpm,
  Step,
  Trigger,
  Volume,
  Pan,
  Pitch,
  Length,
  Wave,
  Delay,
  Mute,
  Source,
  Solo,
  SelectPattern,
  InspectPattern,
  PatternLength,
  CopyPattern,
  ClearPattern,
  Velocity,
  Probability,
  Ratchet,
  Swing,
  Rotate,
  Reverse,
  CopyTrack,
  ClearTrack,
  Duplicate,
  Euclidean,
  Randomize,
  Mutate,
  Reroll,
  LockPitch,
  LockVolume,
  LockPan,
  LockWave,
  LockFilterCutoff,
  LockFilterResonance,
  LockDelaySend,
  UnlockParam,
  ClearStepLocks,
  FilterCutoff,
  FilterResonance,
  DelaySend,
  Count
};
struct Command {
  Kind kind;
  uint8_t track;
  int value;
  // UI pitch edits retain their intended source across asynchronous
  // publication.
  enum class PitchSource : uint8_t { Current, Synth, Sample };
  PitchSource pitch_source = PitchSource::Current;
  uint8_t pattern =
      0; // Step/length/clear target; copy source; value is destination.
  uint8_t step =
      0; // Explicit groove edit intent; fits existing command padding.
};
// One producer (UI), one consumer (audio); reject rather than overwrite.
template <unsigned N> struct Queue {
  Command data[N]{};
  std::atomic<unsigned> write{0}, read{0};
  bool push(Command c) {
    auto w = write.load(std::memory_order_relaxed);
    auto next = (w + 1) % N;
    if (next == read.load(std::memory_order_acquire))
      return false;
    data[w] = c;
    write.store(next, std::memory_order_release);
    return true;
  }
  bool pop(Command &c) {
    auto r = read.load(std::memory_order_relaxed);
    if (r == write.load(std::memory_order_acquire))
      return false;
    c = data[r];
    read.store((r + 1) % N, std::memory_order_release);
    return true;
  }
};
struct Track {
  int volume = 80, pan = 0, pitch = 48, length = 32, wave = 0;
  uint8_t filter_cutoff = 0, filter_resonance = 0, delay_send = 127;
  bool muted = false, sample = false;
  int synth_pitch = 48, sample_pitch = 60;
  bool sample_configured = false;
  void source(bool pcm) {
    if (sample)
      sample_pitch = pitch;
    else
      synth_pitch = pitch;
    sample = pcm;
    pitch = sample ? sample_pitch : synth_pitch;
  }
  void assigned() {
    source(true);
    if (!sample_configured) {
      sample_pitch = 60;
      pitch = 60;
      sample_configured = true;
    }
  }
};
inline bool track_control_enabled(const Track &t, int id) {
  return id >= 24 && id < (t.sample ? 27 : 29);
}
struct StepMeta {
  uint8_t velocity = 100, probability = 100, ratchets = 1;
};
enum LockBit : uint8_t {
  PITCH_LOCK = 1,
  VOLUME_LOCK = 2,
  PAN_LOCK = 4,
  WAVE_LOCK = 8,
  FILTER_CUTOFF_LOCK = 16,
  FILTER_RESONANCE_LOCK = 32,
  DELAY_SEND_LOCK = 64
};
struct StepLocks {
  uint8_t mask = 0, pitch = 0, volume = 0;
  int8_t pan = 0;
  uint8_t wave = 0, filter_cutoff = 0, filter_resonance = 0, delay_send = 0;
};
struct TriggerEvent {
  uint8_t track, velocity, pitch = 0, volume = 0;
  int8_t pan = 0;
  uint8_t wave = 0, locked_mask = 0;
  uint8_t filter_cutoff = 0, filter_resonance = 0, delay_send = 127;
};
inline TriggerEvent resolve_event(int track, const Track &base, int velocity,
                                  StepLocks locks = {}) {
  return {uint8_t(track),
          uint8_t(velocity),
          uint8_t(locks.mask & PITCH_LOCK ? locks.pitch : base.pitch),
          uint8_t(locks.mask & VOLUME_LOCK ? locks.volume : base.volume),
          int8_t(locks.mask & PAN_LOCK ? locks.pan : base.pan),
          uint8_t(!base.sample && (locks.mask & WAVE_LOCK) ? locks.wave
                                                           : base.wave),
          uint8_t(locks.mask & (base.sample ? 119 : 127)),
          uint8_t(locks.mask & FILTER_CUTOFF_LOCK ? locks.filter_cutoff
                                                  : base.filter_cutoff),
          uint8_t(locks.mask & FILTER_RESONANCE_LOCK ? locks.filter_resonance
                                                     : base.filter_resonance),
          uint8_t(locks.mask & DELAY_SEND_LOCK ? locks.delay_send
                                               : base.delay_send)};
}
// Preserve the M9 linear attenuation pan law, including its endpoint rounding.
inline int event_channel_gain(int volume, int pan, bool right) {
  return volume * clamp(128 + (right ? pan : -pan), 0, 128) / 128;
}
static_assert(sizeof(StepLocks) == 8);
static_assert(std::is_trivially_copyable<StepLocks>::value);
// Q15 attenuation: unity at 127; bounded signed multiply, no PCM mutation.
inline uint16_t velocity_gain(int velocity) {
  return uint16_t(clamp(velocity, 1, 127) * 32768 / 127);
}
inline int16_t scale_velocity(int16_t pcm, uint16_t gain) {
  return int16_t(int32_t(pcm) * gain / 32768);
}
struct Pattern {
  uint16_t track_steps[16]{};
  uint8_t length = 16;
  StepMeta meta[16][16]{};
  StepLocks locks[16][16]{};
};
// Generation uses a local stream restarted from edit_seed for each operation.
// Never shares state with the playback probability stream.
struct EditRandom {
  uint32_t state;
  explicit EditRandom(uint32_t seed) : state(seed ? seed : 0x4d395031) {}
  uint32_t next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
  unsigned range(unsigned n) { return uint64_t(next()) * n >> 32; }
};
inline int generation_parameters(int density, int velocity, int probability,
                                 int ratchet) {
  return clamp(density, 0, 100) | (clamp(velocity, 0, 100) << 7) |
         (clamp(probability, 0, 100) << 14) | (clamp(ratchet, 0, 100) << 21);
}
// Positive displacement rotates right. Only the active region moves.
inline void rotate_lane(Pattern &p, int track, int displacement) {
  const int n = p.length;
  StepMeta original[16];
  StepLocks original_locks[16];
  for (int i = 0; i < n; ++i)
    original_locks[i] = p.locks[track][i];
  for (int i = 0; i < n; ++i)
    original[i] = p.meta[track][i];
  const uint16_t mask = p.track_steps[track];
  for (int i = 0; i < n; ++i) {
    int destination = (i + displacement + n) % n;
    const uint16_t bit = uint16_t(1u << destination);
    p.track_steps[track] = uint16_t((p.track_steps[track] & ~bit) |
                                    ((mask & (1u << i)) ? bit : 0));
    p.meta[track][destination] = original[i];
    p.locks[track][destination] = original_locks[i];
  }
}
inline void reverse_lane(Pattern &p, int track) {
  for (int i = 0; i < p.length / 2; ++i) {
    int j = p.length - 1 - i;
    std::swap(p.meta[track][i], p.meta[track][j]);
    std::swap(p.locks[track][i], p.locks[track][j]);
    if (((p.track_steps[track] >> i) ^ (p.track_steps[track] >> j)) & 1)
      p.track_steps[track] ^= uint16_t((1u << i) | (1u << j));
  }
}
inline bool pattern_occupied(const Pattern &p) {
  if (p.length != 16)
    return true;
  for (int t = 0; t < 16; ++t) {
    if (p.track_steps[t])
      return true;
    for (const auto &l : p.locks[t])
      if (l.mask)
        return true;
    for (const auto &m : p.meta[t])
      if (m.velocity != 100 || m.probability != 100 || m.ratchets != 1)
        return true;
  }
  return false;
}
static_assert(std::is_trivially_copyable<Pattern>::value);
static_assert(sizeof(Pattern) == 2850);
static_assert(sizeof(Command) <= 12);
struct Engine {
  Pattern patterns[16];
  uint16_t solos = 0;
  int playing_pattern = 0, selected_pattern = 0, queued_pattern = -1;
  uint32_t switches = 0, loops = 0, long_to_short = 0, short_to_long = 0,
           queue_replacements = 0;
  Track tracks[16];
  int bpm = 120, step = -1;
  bool playing = false, delay = true;
  uint64_t phase = 0;
  bool first = false;
  static constexpr uint64_t straight = 44100ull * 60;
  static constexpr uint32_t default_seed = 0x4d385031;
  uint32_t seed = default_seed, rng = default_seed;
  uint32_t edit_seed = 0x4d395031;
  int swing = 50, pair_swing = 50;
  uint64_t duration = straight, next_ratchet = UINT64_MAX;
  struct Pending {
    TriggerEvent event{};
    uint8_t count = 1, next = 1;
  };
  Pending pending[16]{};
  uint32_t step_events = 0, probability_passed = 0, probability_skipped = 0,
           ratchet_events = 0, pending_max = 0, swing_changes = 0;
  uint8_t velocity_min = 127, velocity_max = 0;
  uint32_t locked_parents = 0, unlocked_parents = 0, lock_events[7]{};
  uint32_t random() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
  }
  void cancel_ratchets() {
    next_ratchet = UINT64_MAX;
    for (auto &p : pending)
      p.next = p.count;
  }
  template <class Trigger> void emit(Trigger &trigger, TriggerEvent event) {
    velocity_min = std::min(velocity_min, event.velocity);
    velocity_max = std::max(velocity_max, event.velocity);
    if constexpr (std::is_invocable<Trigger, TriggerEvent>::value)
      trigger(event);
    else
      trigger(event.track); // Legacy host callback compatibility.
  }

  Engine() {
    for (int i = 0; i < 16; ++i) {
      tracks[i].pitch = 36 + i * 3;
      tracks[i].synth_pitch = tracks[i].pitch;
      tracks[i].wave = i;
    }
  }
  void apply(Command c) {
    if (c.track >= 16)
      return;
    auto &t = tracks[c.track];
    switch (c.kind) {
    case Kind::Play:
      playing = c.value;
      cancel_ratchets();
      rng = seed ? seed : default_seed;
      duration = straight;
      phase = 0;
      step = -1;
      first = playing;
      queued_pattern = -1;
      playing_pattern = selected_pattern;
      break;
    case Kind::Bpm:
      bpm = clamp(c.value, 30, 400);
      break;
    case Kind::Step:
      if (c.value >= 0 && c.value < 16)
        if (c.pattern < 16)
          patterns[c.pattern].track_steps[c.track] ^= uint16_t(1u << c.value);
      break;
    case Kind::Volume:
      t.volume = clamp(c.value, 0, 127);
      break;
    case Kind::Pan:
      t.pan = clamp(c.value, -127, 127);
      break;
    case Kind::Pitch:
      if (c.pitch_source == Command::PitchSource::Sample ||
          (c.pitch_source == Command::PitchSource::Current && t.sample))
        t.sample_pitch = clamp(c.value, 0, 127);
      else
        t.synth_pitch = clamp(c.value, 0, 127);
      t.pitch = t.sample ? t.sample_pitch : t.synth_pitch;
      break;
    case Kind::Length:
      t.length = clamp(c.value, 0, 127);
      break;
    case Kind::Wave:
      t.wave = clamp(c.value, 0, 15);
      break;
    case Kind::FilterCutoff:
      tracks[c.track].filter_cutoff = uint8_t(clamp(c.value, 0, 127));
      break;
    case Kind::FilterResonance:
      tracks[c.track].filter_resonance = uint8_t(clamp(c.value, 0, 127));
      break;
    case Kind::DelaySend:
      tracks[c.track].delay_send = uint8_t(clamp(c.value, 0, 127));
      break;
    case Kind::Delay:
      delay = c.value;
      break;
    case Kind::Mute:
      t.muted = c.value;
      break;
    case Kind::Source:
      t.source(c.value);
      break;
    case Kind::Solo:
      if (c.value)
        solos |= uint16_t(1u << c.track);
      else
        solos &= uint16_t(~(1u << c.track));
      break;
    case Kind::SelectPattern:
    case Kind::InspectPattern:
      if (c.value >= 0 && c.value < 16) {
        selected_pattern = c.value;
        if (c.kind == Kind::SelectPattern) {
          if (playing) {
            if (queued_pattern >= 0 && queued_pattern != c.value)
              ++queue_replacements;
            queued_pattern = c.value == playing_pattern ? -1 : c.value;
          } else {
            playing_pattern = c.value;
            queued_pattern = -1;
          }
        }
      }
      break;
    case Kind::PatternLength:
      if (c.pattern < 16)
        patterns[c.pattern].length = clamp(c.value, 1, 16);
      break;
    case Kind::CopyPattern:
      if (c.pattern < 16 && c.value >= 0 && c.value < 16)
        patterns[c.value] = patterns[c.pattern];
      break;
    case Kind::ClearPattern:
      if (c.pattern < 16)
        for (auto &mask : patterns[c.pattern].track_steps)
          mask = 0;
      break;
    case Kind::Velocity:
    case Kind::Probability:
    case Kind::Ratchet:
      if (c.pattern < 16 && c.step < 16) {
        auto &m = patterns[c.pattern].meta[c.track][c.step];
        if (c.kind == Kind::Velocity)
          m.velocity = clamp(c.value, 1, 127);
        if (c.kind == Kind::Probability)
          m.probability = clamp(c.value, 0, 100);
        if (c.kind == Kind::Ratchet)
          m.ratchets = clamp(c.value, 1, 4);
      }
      break;
    case Kind::LockPitch:
    case Kind::LockVolume:
    case Kind::LockPan:
    case Kind::LockWave:
    case Kind::LockFilterCutoff:
    case Kind::LockFilterResonance:
    case Kind::LockDelaySend:
    case Kind::UnlockParam:
    case Kind::ClearStepLocks:
      if (c.pattern < 16 && c.step < 16) {
        auto &l = patterns[c.pattern].locks[c.track][c.step];
        if (c.kind == Kind::ClearStepLocks)
          l = {};
        else if (c.kind == Kind::UnlockParam)
          l.mask &= uint8_t(~(c.value & 127));
        else {
          const int param = int(c.kind) - int(Kind::LockPitch);
          l.mask |= uint8_t(1u << param);
          if (param == 0)
            l.pitch = clamp(c.value, 0, 127);
          if (param == 1)
            l.volume = clamp(c.value, 0, 127);
          if (param == 2)
            l.pan = clamp(c.value, -127, 127);
          if (param == 3)
            l.wave = clamp(c.value, 0, 15);
          if (param == 4)
            l.filter_cutoff = clamp(c.value, 0, 127);
          if (param == 5)
            l.filter_resonance = clamp(c.value, 0, 127);
          if (param == 6)
            l.delay_send = clamp(c.value, 0, 127);
        }
      }
      break;
    case Kind::Swing:
      if (swing != clamp(c.value, 50, 75))
        ++swing_changes;
      swing = clamp(c.value, 50, 75);
      break;
    case Kind::Reroll: {
      EditRandom edit(edit_seed);
      edit_seed = edit.next();
      break;
    }
    case Kind::Rotate:
    case Kind::Reverse:
    case Kind::CopyTrack:
    case Kind::ClearTrack:
    case Kind::Duplicate:
    case Kind::Euclidean:
    case Kind::Randomize:
    case Kind::Mutate: {
      if (c.pattern >= 16)
        break;
      auto &p = patterns[c.pattern];
      if (c.kind == Kind::Rotate || c.kind == Kind::Reverse) {
        // step=1 explicitly requests all tracks; value is direction.
        for (int lane = c.step ? 0 : c.track;
             lane < (c.step ? 16 : c.track + 1); ++lane)
          if (c.kind == Kind::Rotate)
            rotate_lane(p, lane, c.value < 0 ? -1 : 1);
          else
            reverse_lane(p, lane);
      } else if (c.kind == Kind::CopyTrack) {
        if (c.value >= 0 && c.value < 16) {
          p.track_steps[c.value] = p.track_steps[c.track];
          for (int i = 0; i < 16; ++i) {
            p.meta[c.value][i] = p.meta[c.track][i];
            p.locks[c.value][i] = p.locks[c.track][i];
          }
        }
      } else if (c.kind == Kind::ClearTrack)
        p.track_steps[c.track] = 0; // Retain all metadata, like pattern CLEAR.
      else if (c.kind == Kind::Duplicate) {
        if (c.value >= 0 && c.value < 16 && c.value != c.pattern) {
          patterns[c.value] = p;
          selected_pattern = c.value; // Edit destination; no playback request.
        }
      } else if (c.kind == Kind::Euclidean) {
        const int pulses = clamp(c.value, 0, p.length);
        const int rotation = c.step % p.length;
        // Mechanical Euclidean word, anchored at step zero, rotated right.
        for (int i = 0; i < p.length; ++i) {
          uint16_t bit = uint16_t(1u << ((i + rotation) % p.length));
          bool on = (i * pulses) % p.length < pulses;
          p.track_steps[c.track] =
              uint16_t((p.track_steps[c.track] & ~bit) | (on ? bit : 0));
        }
      } else {
        EditRandom edit(edit_seed);
        const bool mutate = c.kind == Kind::Mutate;
        const int amount = clamp(c.value, 0, 100);
        const int density = clamp(c.value & 127, 0, 100);
        const int velocity = clamp((c.value >> 7) & 127, 0, 100);
        const int probability = clamp((c.value >> 14) & 127, 0, 100);
        const int ratchet = clamp((c.value >> 21) & 127, 0, 100);
        for (int i = 0; !mutate && i < p.length; ++i) {
          auto &m = p.meta[c.track][i];
          uint16_t bit = uint16_t(1u << i);
          {
            bool on = edit.range(100) < unsigned(density);
            p.track_steps[c.track] =
                uint16_t((p.track_steps[c.track] & ~bit) | (on ? bit : 0));
            if (!on)
              continue; // Inactive metadata remains intact.
            // Explicit accents survive; otherwise use a center of 100.
            if (m.velocity != 127)
              m.velocity = clamp(
                  100 + int(edit.range(2 * velocity + 1)) - velocity, 1, 126);
            m.probability = 100 - edit.range(probability + 1);
            m.ratchets =
                edit.range(100) < unsigned(ratchet) ? 2 + edit.range(3) : 1;
          }
        }
        if (mutate && amount) {
          // Partial Fisher-Yates: bounded exact number of edited positions.
          uint8_t positions[16];
          for (int i = 0; i < p.length; ++i)
            positions[i] = uint8_t(i);
          const int count = (p.length * amount + 99) / 100;
          for (int i = 0; i < count; ++i) {
            int j = i + edit.range(p.length - i);
            std::swap(positions[i], positions[j]);
            int pos = positions[i];
            p.track_steps[c.track] ^= uint16_t(1u << pos);
            auto &m = p.meta[c.track][pos];
            if (m.velocity != 127)
              m.velocity = clamp(m.velocity + int(edit.range(11)) - 5, 1, 126);
          }
        }
      }
      break;
    }
    case Kind::Count:
    case Kind::Trigger:
      break;
    }
  }
  bool sequence_enabled(int t) const {
    return !tracks[t].muted && (!solos || (solos & (1u << t)));
  }
  template <class Trigger> void audition(int t, Trigger trigger) const {
    if (t >= 0 && t < 16) {
      if constexpr (std::is_invocable<Trigger, TriggerEvent>::value)
        trigger(resolve_event(t, tracks[t], 127));
      else
        trigger(t); // Performance gates affect sequence only.
    }
  }
  // Exact rational sixteenth clock: accumulated BPM*4 per sample, no rounding
  // drift.
  template <class Trigger> void sample(Trigger trigger) {
    if (!playing)
      return;
    if (first || phase >= duration) {
      first = false;
      if (phase >= duration)
        phase -= duration;
      cancel_ratchets();
      // Length edits never move a live head between musical boundaries.
      // Wrap once on the next onset, then switch before triggering step zero.
      if (step + 1 >= patterns[playing_pattern].length) {
        ++loops;
        if (queued_pattern >= 0) {
          long_to_short += patterns[playing_pattern].length == 16 &&
                           patterns[queued_pattern].length == 7;
          short_to_long += patterns[playing_pattern].length == 7 &&
                           patterns[queued_pattern].length == 16;
          playing_pattern = queued_pattern;
          queued_pattern = -1;
          ++switches;
        }
        step = 0;
      } else
        ++step;
      // Capture swing per pair, so edits cannot change the pair's total.
      if (!(step & 1))
        pair_swing = swing;
      duration = straight * 2 *
                 unsigned((step & 1) ? 100 - pair_swing : pair_swing) / 100;
      unsigned scheduled = 0;
      for (int t = 0; t < 16; ++t) {
        if (!sequence_enabled(t) ||
            !(patterns[playing_pattern].track_steps[t] & (1u << step)))
          continue;
        const auto m = patterns[playing_pattern].meta[t][step];
        ++step_events;
        // Consume one draw for every eligible parent, including 0/100%.
        const auto draw = random();
        if (uint64_t(draw) * 100 >= uint64_t(m.probability) * (1ull << 32)) {
          ++probability_skipped;
          continue;
        }
        ++probability_passed;
        const auto event = resolve_event(
            t, tracks[t], m.velocity, patterns[playing_pattern].locks[t][step]);
        locked_parents += event.locked_mask != 0;
        unlocked_parents += event.locked_mask == 0;
        for (int i = 0; i < 7; ++i)
          lock_events[i] += (event.locked_mask >> i) & 1;
        emit(trigger, event);
        pending[t] = {event, m.ratchets, 1};
        scheduled += m.ratchets - 1;
        if (m.ratchets > 1)
          next_ratchet =
              std::min(next_ratchet, (duration + m.ratchets - 1) / m.ratchets);
      }
      pending_max = std::max(pending_max, uint32_t(scheduled));
    }
    if (phase >= next_ratchet) {
      next_ratchet = UINT64_MAX;
      for (int t = 0; t < 16; ++t) {
        auto &p = pending[t];
        if (p.next >= p.count)
          continue;
        auto offset = (duration * p.next + p.count - 1) / p.count;
        if (phase >= offset) {
          emit(trigger, p.event);
          ++ratchet_events;
          ++p.next;
        }
        if (p.next < p.count)
          next_ratchet = std::min(next_ratchet,
                                  (duration * p.next + p.count - 1) / p.count);
      }
    }
    phase += unsigned(bpm) * 4;
  }
};
struct Rect {
  int x, y, w, h;
  bool contains(int px, int py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
};
enum class Page {
  Sequence,
  Track,
  Fx,
  Sample,
  Pattern,
  Step,
  Tools,
  Locks,
  Tone,
  ToneLocks
};
struct Ui {
  Page page = Page::Sequence;
  int selected = 0, bank = 0, selected_step = -1, capture = -1;
  bool down = false, inspect = false;
  int copy_source = -1, clear_pattern = -1;
  int tool_section = 0, tool_destination = 0;
  int pulses = 5, rotation = 0, density = 45, vel_var = 20, prob_var = 15,
      ratchet_chance = 5, mutate_amount = 20;
  bool tool_pending = false;
  Command pending_tool{};
  bool lock_action(int id, int x, bool initial, const Engine &e,
                   Command &c) const;
  void cancel_tool() { tool_pending = false; }
  bool tool_action(int id, const Engine &e, Command &c) {
    if (id >= 80 && id <= 82) {
      tool_section = id - 80;
      cancel_tool();
      return false;
    }
    if (id == 91 && tool_section != 1) {
      cancel_tool();
      return false;
    }
    if (tool_pending) {
      if (pending_tool.kind == Kind::CopyTrack && (id == 88 || id == 89)) {
        pending_tool.value =
            clamp(pending_tool.value + (id == 88 ? -1 : 1), 0, 15);
      } else if (id == 90) {
        c = pending_tool;
        cancel_tool();
        return true;
      }
      return false;
    }
    if (id == 93 || id == 94) {
      selected = clamp(selected + (id == 93 ? -1 : 1), 0, 15);
      return false;
    }
    c = {Kind::Rotate, uint8_t(selected), 0};
    c.pattern = uint8_t(e.selected_pattern);
    if (tool_section == 0) {
      if (id == 83 || id == 84)
        c.value = id == 83 ? -1 : 1;
      else if (id == 85)
        c.kind = Kind::Reverse;
      else if (id == 86 || id == 87) {
        c.kind = id == 86 ? Kind::CopyTrack : Kind::ClearTrack;
        c.value = selected;
        pending_tool = c;
        tool_pending = true;
        return false;
      } else
        return false;
    } else if (tool_section == 1) {
      if (id == 89) {
        c.kind = Kind::Euclidean;
        c.value = clamp(pulses, 0, e.patterns[c.pattern].length);
        c.step = uint8_t(rotation % e.patterns[c.pattern].length);
      } else if (id == 90) {
        c.kind = Kind::Randomize;
        c.value =
            generation_parameters(density, vel_var, prob_var, ratchet_chance);
      } else if (id == 91) {
        c.kind = Kind::Mutate;
        c.value = mutate_amount;
      } else if (id == 92)
        c.kind = Kind::Reroll;
      else
        return false;
    } else {
      c.step = 1;
      if (id == 83) {
        c.kind = Kind::Duplicate;
        c.value = (c.pattern + 1) % 16;
        if (pattern_occupied(e.patterns[c.value])) {
          pending_tool = c;
          tool_pending = true;
          return false;
        }
      } else if (id == 84 || id == 85)
        c.value = id == 84 ? -1 : 1;
      else if (id == 86)
        c.kind = Kind::Reverse;
      else
        return false;
    }
    return true;
  }
  void cancel_pattern_action() { copy_source = clear_pattern = -1; }
  // Return a concrete command only after the deliberate workflow completes.
  bool pattern_action(int id, const Engine &e, Command &c) {
    if (id >= 46 && id <= 61) {
      int destination = id - 46;
      clear_pattern = -1;
      if (copy_source >= 0) {
        c = {Kind::CopyPattern, 0, destination};
        c.pattern = uint8_t(copy_source);
        copy_source = -1;
      } else
        c = {inspect ? Kind::InspectPattern : Kind::SelectPattern, 0,
             destination};
      return true;
    }
    if (id == 65) {
      clear_pattern = -1;
      copy_source = copy_source < 0 ? e.selected_pattern : -1;
    } else if (id == 66) {
      copy_source = -1;
      if (clear_pattern == e.selected_pattern) {
        c = {Kind::ClearPattern, 0, 0};
        c.pattern = uint8_t(clear_pattern);
        clear_pattern = -1;
        return true;
      }
      clear_pattern = e.selected_pattern;
    } else {
      cancel_pattern_action();
      if (id == 67)
        inspect = !inspect;
      if (id == 62 || id == 64) {
        c = {Kind::PatternLength, 0,
             e.patterns[e.selected_pattern].length + (id == 62 ? -1 : 1)};
        c.pattern = uint8_t(e.selected_pattern);
        return true;
      }
    }
    return false;
  }
};
inline Rect widget(int id) {
  if (id >= 114 && id <= 119) {
    int row = (id - 114) / 2;
    return {id % 2 == 0 ? 24 : 560, 100 + row * 68, id % 2 == 0 ? 520 : 216,
            56};
  }
  if (id == 120)
    return {24, 362, 240, 52};
  if (id == 121)
    return {280, 362, 240, 52};
  if (id == 122)
    return {536, 362, 240, 52};
  if (id == 37)
    return {24, 368, 200, 52};
  if (id == 106)
    return {24, 100, 752, 52}; // source / track
  if (id >= 107 && id <= 109)
    return {24, 164 + (id - 107) * 68, 752, 56};
  if (id == 110)
    return {24, 368, 240, 52};
  if (id == 111)
    return {280, 368, 240, 52};
  if (id == 112)
    return {536, 368, 240, 52};
  if (id == 113)
    return {24, 310, 200, 52}; // TRACK -> TONE

  if (id == 95)
    return {560, 390, 216, 52};
  if (id >= 96 && id <= 103) {
    int row = (id - 96) / 2;
    return {id % 2 == 0 ? 24 : 560, 100 + row * 68, id % 2 == 0 ? 520 : 216,
            56};
  }
  if (id == 104)
    return {24, 362, 240, 52};
  if (id == 105)
    return {536, 362, 240, 52};
  if (id == 78)
    return {488, 224, 288, 52};
  if (id == 79)
    return {16, 90, 768, 56};
  if (id >= 80 && id <= 82)
    return {16 + (id - 80) * 256, 150, 248, 52};
  if (id >= 83 && id <= 94)
    return {16 + (id - 83) % 4 * 192, 210 + (id - 83) / 4 * 68, 184, 60};
  if (id == 68)
    return {16, 224, 160, 52}; // STEP, between grid and pads
  if (id == 69)
    return {192, 224, 280, 52}; // persistent sequence swing
  if (id == 70)
    return {24, 100, 752, 56}; // step identity / active
  if (id == 71)
    return {24, 170, 520, 56}; // velocity
  if (id == 72)
    return {560, 170, 216, 56}; // accent
  if (id == 73)
    return {24, 240, 752, 56}; // probability
  if (id >= 74 && id <= 77)
    return {24 + (id - 74) * 192, 320, 176, 64};

  if (id >= 46 && id <= 61)
    return {16 + (id - 46) % 4 * 132, 100 + (id - 46) / 4 * 76, 124, 68};
  if (id == 62)
    return {560, 100, 64, 64};
  if (id == 63)
    return {628, 100, 80, 64};
  if (id == 64)
    return {712, 100, 64, 64};
  if (id >= 65 && id <= 67)
    return {560, 190 + (id - 65) * 76, 216, 64};
  if (id == 44)
    return {184, 16, 320, 56};
  if (id == 43)
    return {24, 170, 200, 56};
  if (id == 42)
    return {24, 250, 200, 56};
  if (id >= 38 && id <= 41)
    return {24 + ((id - 38) % 2) * 380, 100 + ((id - 38) / 2) * 90, 360, 72};
  if (id < 16)
    return {16 + (id % 8) * 96, 100 + (id / 8) * 66, 88, 58};
  if (id < 24)
    return {16 + (id - 16) % 4 * 192, 280 + (id - 16) / 4 * 70, 184, 62};
  if (id < 29)
    return {260, 90 + (id - 24) * 62, 520, 52};
  if (id == 29)
    return {16, 420, 144, 52};
  if (id < 33)
    return {176 + (id - 30) * 152, 420, 144, 52};
  if (id == 33)
    return {640, 420, 144, 52};
  if (id == 34)
    return {520, 16, 56, 56};
  if (id == 35)
    return {720, 16, 64, 56};
  if (id == 36)
    return {24, 110, 240, 64};
  return {24, 340, 200, 56};
}
inline int hit(Page p, int x, int y) {
  for (int i = 29; i <= 35; ++i)
    if (widget(i).contains(x, y))
      return i;
  if (widget(44).contains(x, y))
    return 44;
  if (p == Page::Tone) {
    for (int i = 107; i <= 112; ++i)
      if (widget(i).contains(x, y))
        return i;
  } else if (p == Page::ToneLocks) {
    for (int i = 114; i <= 122; ++i)
      if (widget(i).contains(x, y))
        return i;
  } else if (p == Page::Locks) {
    if (widget(121).contains(x, y))
      return 121;
    for (int i = 96; i <= 105; ++i)
      if (widget(i).contains(x, y))
        return i;
  } else if (p == Page::Step) {
    if (widget(95).contains(x, y))
      return 95;
    for (int i = 70; i <= 77; ++i)
      if (widget(i).contains(x, y))
        return i;
  } else if (p == Page::Tools) {
    for (int i = 80; i <= 94; ++i)
      if (widget(i).contains(x, y))
        return i;
  } else if (p == Page::Pattern) {
    for (int i = 46; i <= 67; ++i)
      if (i != 63 && widget(i).contains(x, y))
        return i;
  } else if (p == Page::Sequence) {
    for (int i : {68, 69, 78})
      if (widget(i).contains(x, y))
        return i;
    for (int i = 0; i < 24; ++i)
      if (widget(i).contains(x, y))
        return i;
  } else if (p == Page::Track) {
    if (widget(113).contains(x, y))
      return 113;
    for (int i = 24; i < 29; ++i)
      if (widget(i).contains(x, y))
        return i;
    if (widget(43).contains(x, y))
      return 43;
    if (widget(42).contains(x, y))
      return 42;
    if (widget(37).contains(x, y))
      return 37;
  } else if (p == Page::Sample) {
    for (int i = 38; i <= 41; ++i)
      if (widget(i).contains(x, y))
        return i;
  } else if (widget(36).contains(x, y))
    return 36;
  return -1;
}
inline int drag(int id, int x) {
  auto r = widget(id);
  int v = clamp(x - r.x, 0, r.w - 1) * 127 / (r.w - 1);
  return id == 25 ? v * 2 - 127 : v;
}
inline bool Ui::lock_action(int id, int x, bool initial, const Engine &e,
                            Command &c) const {
  if (!((id >= 96 && id <= 104) || (id >= 114 && id <= 120)) ||
      selected_step < 0 || selected_step >= 16)
    return false;
  c = {Kind::ClearStepLocks, uint8_t(selected), 0};
  c.pattern = uint8_t(e.selected_pattern);
  c.step = uint8_t(selected_step);
  if (id == 104 || id == 120)
    return initial;
  const int param = id >= 114 ? 4 + (id - 114) / 2 : (id - 96) / 2;
  const auto &base = e.tracks[c.track];
  const auto &locks = e.patterns[c.pattern].locks[c.track][c.step];
  if (param == 3 && base.sample)
    return false;
  c.kind = Kind(int(Kind::LockPitch) + param);
  if (id % 2) {
    if (!initial)
      return false;
    if (locks.mask & (1 << param)) {
      c.kind = Kind::UnlockParam;
      c.value = 1 << param;
    } else {
      const int values[] = {
          base.pitch,         base.volume,           base.pan,       base.wave,
          base.filter_cutoff, base.filter_resonance, base.delay_send};
      c.value = values[param];
    }
  } else {
    if (!(locks.mask & (1 << param)))
      return false;
    c.value = drag(id, x);
    if (param == 2)
      c.value = c.value * 2 - 127;
    if (param == 3)
      c.value = c.value * 15 / 127;
    if (param == 4)
      c.value = 127 - c.value;
  }
  return true;
}
} // namespace app
