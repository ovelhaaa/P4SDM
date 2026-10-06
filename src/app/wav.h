#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
namespace sampler {
// Rounded Q16 semitone ratios. Octaves are exact; clamp before lookup.
inline uint64_t pitch_increment(int pitch) {
  static constexpr uint32_t ratios[12] = {
      65536, 69433, 73562, 77936, 82570, 87480,
      92682, 98193, 104032, 110218, 116772, 123715};
  if (pitch < 0) pitch = 0;
  if (pitch > 127) pitch = 127;
  int delta = pitch - 60;
  int octave = delta / 12, note = delta % 12;
  if (note < 0) { note += 12; --octave; }
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
};
struct Voice {
  const Sample *sample = nullptr;
  uint64_t position = 0, increment = 65536;
  bool active = false;
  void stop() {
    active = false;
    position = 0;
  }
  void assign(const Sample *s) {
    sample = s;
    stop();
  }
  void trigger(uint64_t step) {
    position = 0;
    increment = step;
    active = sample && sample->frames && step;
  }
  int16_t next() {
    if (!active || !sample)
      return 0;
    uint64_t index = position >> 16;
    if (index >= sample->frames) {
      active = false;
      return 0;
    }
    int16_t result = sample->data[index];
    position += increment;
    if ((position >> 16) >= sample->frames)
      active = false;
    return result;
  }
};
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
