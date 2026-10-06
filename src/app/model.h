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
  Swing
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
struct TriggerEvent {
  uint8_t track, velocity;
};
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
};
static_assert(std::is_trivially_copyable<Pattern>::value);
static_assert(sizeof(Pattern) <= 802);
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
  int swing = 50, pair_swing = 50;
  uint64_t duration = straight, next_ratchet = UINT64_MAX;
  struct Pending {
    uint8_t velocity = 100, count = 1, next = 1;
  };
  Pending pending[16]{};
  uint32_t step_events = 0, probability_passed = 0, probability_skipped = 0,
           ratchet_events = 0, pending_max = 0, swing_changes = 0;
  uint8_t velocity_min = 127, velocity_max = 0;
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
  template <class Trigger> void emit(Trigger &trigger, int t, int velocity) {
    velocity_min = std::min(velocity_min, uint8_t(velocity));
    velocity_max = std::max(velocity_max, uint8_t(velocity));
    if constexpr (std::is_invocable<Trigger, TriggerEvent>::value)
      trigger(TriggerEvent{uint8_t(t), uint8_t(velocity)});
    else
      trigger(t); // Legacy host callback compatibility.
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
    case Kind::Swing:
      if (swing != clamp(c.value, 50, 75))
        ++swing_changes;
      swing = clamp(c.value, 50, 75);
      break;
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
        trigger(TriggerEvent{uint8_t(t), 127});
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
        emit(trigger, t, m.velocity);
        pending[t] = {m.velocity, m.ratchets, 1};
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
          emit(trigger, t, p.velocity);
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
enum class Page { Sequence, Track, Fx, Sample, Pattern, Step };
struct Ui {
  Page page = Page::Sequence;
  int selected = 0, bank = 0, selected_step = -1, capture = -1;
  bool down = false, inspect = false;
  int copy_source = -1, clear_pattern = -1;
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
  if (p == Page::Step) {
    for (int i = 70; i <= 77; ++i)
      if (widget(i).contains(x, y))
        return i;
  } else if (p == Page::Pattern) {
    for (int i = 46; i <= 67; ++i)
      if (i != 63 && widget(i).contains(x, y))
        return i;
  } else if (p == Page::Sequence) {
    for (int i : {68, 69})
      if (widget(i).contains(x, y))
        return i;
    for (int i = 0; i < 24; ++i)
      if (widget(i).contains(x, y))
        return i;
  } else if (p == Page::Track) {
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
} // namespace app
