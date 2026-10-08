#pragma once
#include "sample_playback.h"
#include "sample_interpolation.h"
#include "waveform.h"
#include <atomic>
#include <cstdint>
#include <cstring>
namespace sampler {
// Rounded Q16 semitone ratios. Octaves are exact; clamp before lookup.
inline uint64_t pitch_increment(int pitch) {
  static constexpr uint32_t ratios[12] = {65536,  69433,  73562,  77936,
                                          82570,  87480,  92682,  98193,
                                          104032, 110218, 116772, 123715};
  if (pitch < 0)
    pitch = 0;
  if (pitch > 127)
    pitch = 127;
  int delta = pitch - 60;
  int octave = delta / 12, note = delta % 12;
  if (note < 0) {
    note += 12;
    --octave;
  }
  return octave >= 0 ? uint64_t(ratios[note]) << octave
                     : uint64_t(ratios[note]) >> -octave;
}
struct Wav {
  uint32_t offset = 0, bytes = 0, frames = 0, rate = 0;
  uint16_t channels = 0;
};
inline uint16_t u16(const uint8_t *p) { return p[0] | uint16_t(p[1]) << 8; }
inline uint32_t u32(const uint8_t *p) {
  return u16(p) | uint32_t(u16(p + 2)) << 16;
}
// Reader must perform an exact, bounded positional read. No allocation here.
template <class Read> const char *parse(uint64_t size, Read read, Wav &out) {
  out = {};
  uint8_t h[16];
  if (size < 12 || !read(0, h, 12))
    return "truncated RIFF";
  if (memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))
    return "not RIFF WAVE";
  uint64_t end = uint64_t(u32(h + 4)) + 8;
  if (end < 12 || end > size)
    return "invalid RIFF length";
  bool fmt = false, data = false;
  unsigned chunks = 0;
  for (uint64_t p = 12; p < end;) {
    if (++chunks > 4096)
      return "too many RIFF chunks";
    if (end - p < 8 || !read(p, h, 8))
      return "truncated chunk";
    uint32_t n = u32(h + 4);
    uint64_t start = p + 8, next = start + uint64_t(n) + (n & 1);
    if (next > end)
      return "invalid chunk length/padding";
    if (!memcmp(h, "fmt ", 4)) {
      if (fmt || n < 16 || !read(start, h, 16))
        return "invalid fmt";
      fmt = true;
      if (u16(h) != 1)
        return "unsupported encoding (PCM only)";
      out.channels = u16(h + 2);
      out.rate = u32(h + 4);
      if (out.channels != 1 && out.channels != 2)
        return "unsupported channels";
      if (u16(h + 14) != 16)
        return "unsupported depth (16-bit only)";
      if (out.rate != 44100)
        return "unsupported rate (44100 only)";
      if (u16(h + 12) != out.channels * 2 ||
          u32(h + 8) != out.rate * out.channels * 2)
        return "invalid PCM alignment";
    } else if (!memcmp(h, "data", 4)) {
      if (data)
        return "duplicate data";
      if (start > UINT32_MAX)
        return "data offset too large";
      data = true;
      out.offset = uint32_t(start);
      out.bytes = n;
    }
    p = next;
  }
  if (!fmt || !data)
    return "missing fmt/data";
  if (!out.bytes || out.bytes % (out.channels * 2))
    return "empty/partial PCM frame";
  out.frames = out.bytes / (out.channels * 2);
  return nullptr;
}
inline bool fits_budget(uint64_t bytes, uint64_t free, uint64_t largest) {
  constexpr uint64_t reserve = 4 * 1024 * 1024;
  return bytes > 0 && bytes <= 4 * 1024 * 1024 && free >= reserve &&
         bytes <= free - reserve && bytes <= largest;
}
inline int16_t decode_mono(const uint8_t *pcm, unsigned channels) {
  int32_t a = int16_t(u16(pcm));
  return channels == 2 ? int16_t((a + int16_t(u16(pcm + 2))) / 2) : int16_t(a);
}
struct Sample {
  int16_t *data = nullptr;
  uint32_t frames = 0, rate = 44100, allocation = 0;
  uint16_t original_channels = 1, channels = 1;
  char name[96]{};
  Waveform waveform{};
};
struct Voice {
  const Sample *sample = nullptr;
  uint64_t position = 0, increment = 65536;
  bool active = false;
  Playback playback{};
  Region region{};
  uint64_t remaining = 0;
  uint8_t attack = 0, release_left = 0, fade = boundary_fade;
  bool releasing = false, sequenced = false;
  void stop() {
    active = false;
    position = 0;
    releasing = false;
    release_left = 0;
  }
  void assign(const Sample *s) {
    sample = s;
    stop();
  }
  void set_increment(uint64_t step) {
    increment = step;
    const uint64_t limit = uint64_t(region.end - region.start) << 16;
    remaining =
        step && position < limit ? (limit - position - 1) / step + 1 : 0;
    if (!remaining)
      active = false;
  }
  void trigger(uint64_t step, Playback settings = {}, bool sequence = false,
               unsigned fade_samples = boundary_fade) {
    position = 0;
    playback = settings;
    sequenced = sequence;
    region = resolve_region(settings, sample ? sample->frames : 0);
    attack = 0;
    fade = fade_samples > boundary_fade ? boundary_fade : fade_samples;
    releasing = false;
    release_left = 0;
    active = sample && sample->data && region.end > region.start && step;
    set_increment(step);
  }
  bool release(bool choke = false) {
    if (!active || releasing || (!choke && playback.mode != Mode::Gate))
      return false;
    releasing = true;
    release_left = fade;
    if (!fade)
      active = false;
    return true;
  }
  uint32_t frame_index() const {
    uint32_t offset = uint32_t(position >> 16);
    return playback.reverse ? region.end - 1 - offset : region.start + offset;
  }
  inline __attribute__((always_inline)) int16_t next(Interpolation interpolation = default_interpolation) {
    if (!active || !sample)
      return 0;
    if ((position >> 16) >= region.end - region.start || !remaining) {
      active = false;
      return 0;
    }
    int16_t result = lookup_valid(sample->data, region, playback.reverse, position,
                            interpolation);
    if (fade) {
      unsigned gain = attack;
      if (remaining <= fade)
        gain = unsigned(remaining - 1) < gain ? unsigned(remaining - 1) : gain;
      if (releasing && release_left - 1u < gain)
        gain = release_left - 1u;
      result = int16_t(int32_t(result) * int(gain) / int(fade));
      if (attack < fade)
        ++attack;
    }
    --remaining;
    if (!remaining || (releasing && !--release_left))
      active = false;
    else
      position +=
          increment; // bounded by remaining; cannot wrap at valid PCM sizes
    return result;
  }
};
struct PlaybackMetrics {
  uint32_t triggers = 0, reverse = 0, gate = 0, releases = 0, ends = 0;
  uint32_t choke_ops = 0, choked = 0, retriggers = 0;
  uint32_t region_min = UINT32_MAX, region_max = 0;
};
// Called only for accepted triggers, on the audio owner. Ratchets skip the
// scan.
inline bool trigger_voice(Voice *voices, unsigned track, uint64_t increment,
                          Playback playback, bool sequenced, bool ratchet,
                          PlaybackMetrics &m) {
  if (track >= 16)
    return false;
  Voice &v = voices[track];
  if (!v.sample || !v.sample->data || !v.sample->frames || !increment)
    return false;
  if (!ratchet && playback.choke) {
    ++m.choke_ops;
    for (unsigned t = 0; t < 16; ++t)
      if (t != track && voices[t].playback.choke == playback.choke &&
          voices[t].release(true)) {
        ++m.choked;
        ++m.releases;
      }
  }
  m.retriggers += v.active;
  v.trigger(increment, playback, sequenced);
  ++m.triggers;
  m.reverse += playback.reverse;
  m.gate += playback.mode == Mode::Gate;
  const uint32_t n = v.region.end - v.region.start;
  if (n < m.region_min)
    m.region_min = n;
  if (n > m.region_max)
    m.region_max = n;
  return true;
}
// One staging slot. Storage owns staging/retired buffers; audio owns active.
// Storage cannot submit another object until audio acknowledges the first.
struct Transfer {
  std::atomic<Sample *> pending{nullptr}, retired{nullptr};
  std::atomic<bool> acknowledged{false};
  unsigned track = 0;
  Sample *active[16]{};
  void publish(Sample *s, unsigned t) {
    track = t;
    acknowledged.store(false);
    pending.store(s, std::memory_order_release);
  }
  int consume(Voice *voices) {
    Sample *s = pending.exchange(nullptr, std::memory_order_acquire);
    if (!s)
      return -1;
    unsigned t = track;
    Sample *old = active[t];
    active[t] = s;
    voices[t].assign(s);
    retired.store(old, std::memory_order_release);
    acknowledged.store(true, std::memory_order_release);
    return int(t);
  }
};
} // namespace sampler
