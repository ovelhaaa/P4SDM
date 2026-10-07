#pragma once
#include "model.h"
#include <cstring>
namespace project {
constexpr uint16_t version = 2, header_bytes = 24;
constexpr unsigned name_bytes = 32, reference_bytes = 96;
// Memory model only. Disk fields are encoded individually below.
struct State {
  char name[name_bytes] = "UNTITLED";
  uint16_t bpm = 120;
  uint8_t swing = 50, delay = 1, selected = 0;
  app::PatternChain chain{};
  app::TransportMode mode = app::TransportMode::Pattern;
  app::Track tracks[16];
  char references[16][reference_bytes]{};
  app::Pattern patterns[16];
};
constexpr unsigned track_bytes = 21 + 64 + reference_bytes;
constexpr unsigned pattern_bytes = 1 + 32 + 256 * 12;
constexpr unsigned v1_payload_bytes =
    name_bytes + 5 + 16 * track_bytes + 16 * pattern_bytes;
constexpr unsigned payload_bytes = v1_payload_bytes + 67;
constexpr unsigned file_bytes = header_bytes + payload_bytes;
inline unsigned payload_size(unsigned v) {
  return v == 1 ? v1_payload_bytes : payload_bytes;
}
enum class Error { Ok, Header, Version, Future, Size, Crc, Semantic, Io };
inline const char *message(Error e) {
  switch (e) {
  case Error::Ok:
    return "READY";
  case Error::Header:
    return "BAD HEADER";
  case Error::Version:
    return "UNSUPPORTED PROJECT VERSION";
  case Error::Future:
    return "PROJECT VERSION TOO NEW";
  case Error::Size:
    return "BAD PROJECT SIZE";
  case Error::Crc:
    return "CRC ERROR";
  case Error::Semantic:
    return "INVALID PROJECT STATE";
  case Error::Io:
    return "PROJECT READ FAILED";
  }
  return "LOAD FAILED";
}
inline bool name_valid(const char *s) {
  unsigned i = 0;
  for (; i < name_bytes && s[i]; ++i)
    if (!((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') ||
          (s[i] >= '0' && s[i] <= '9') || s[i] == ' ' || s[i] == '_' ||
          s[i] == '-'))
      return false;
  return i && i < name_bytes && s[0] != ' ' && s[i - 1] != ' ';
}
inline bool reference_valid(const char *s) {
  unsigned i = 0;
  for (; i < reference_bytes && s[i]; ++i)
    if (uint8_t(s[i]) < 32 || s[i] == '/' || s[i] == '\\' || s[i] == ':' ||
        (s[i] == '.' && i + 1 < reference_bytes && s[i + 1] == '.'))
      return false;
  return i < reference_bytes;
}
inline uint32_t crc32(const uint8_t *p, unsigned n) {
  uint32_t crc = ~0u;
  while (n--) {
    crc ^= *p++;
    for (int i = 0; i < 8; ++i)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  return ~crc;
}
inline uint16_t u16(const uint8_t *p) {
  return uint16_t(p[0] | (uint16_t(p[1]) << 8));
}
inline uint32_t u32(const uint8_t *p) {
  return uint32_t(u16(p)) | (uint32_t(u16(p + 2)) << 16);
}
inline void put16(uint8_t *p, uint16_t v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
}
inline void put32(uint8_t *p, uint32_t v) {
  put16(p, uint16_t(v));
  put16(p + 2, uint16_t(v >> 16));
}
// One codec used for writing, validation and committing a validated decode.
struct Codec {
  uint8_t *write = nullptr;
  const uint8_t *read = nullptr;
  unsigned pos = 0;
  bool valid = true;
  uint8_t byte(unsigned v, unsigned lo = 0, unsigned hi = 255) {
    unsigned x = read ? read[pos] : v;
    if (x < lo || x > hi)
      valid = false;
    if (write)
      write[pos] = uint8_t(x);
    ++pos;
    return uint8_t(x);
  }
  uint16_t word(unsigned v, unsigned lo = 0, unsigned hi = 65535) {
    unsigned x = read ? u16(read + pos) : v;
    if (x < lo || x > hi)
      valid = false;
    if (write)
      put16(write + pos, uint16_t(x));
    pos += 2;
    return uint16_t(x);
  }
  void string(char *destination, const char *source, unsigned n, bool name) {
    const char *s = read ? reinterpret_cast<const char *>(read + pos) : source;
    valid &= name ? name_valid(s) : reference_valid(s);
    // All bytes after the terminator must be zero, keeping V1 deterministic.
    bool ended = false;
    for (unsigned i = 0; i < n; ++i) {
      if (!s[i])
        ended = true;
      else if (ended && read)
        valid = false;
      if (write)
        write[pos + i] = ended ? 0 : uint8_t(s[i]);
      if (destination)
        destination[i] = ended ? 0 : s[i];
    }
    pos += n;
  }
};
inline bool fields(Codec &c, const State &s, State *out, unsigned v = version) {
  c.string(out ? out->name : nullptr, s.name, name_bytes, true);
  auto bpm = c.word(s.bpm, 30, 400);
  auto swing = c.byte(s.swing, 50, 75), delay = c.byte(s.delay, 0, 1),
       selected = c.byte(s.selected, 0, 15);
  if (out) {
    out->bpm = bpm;
    out->swing = swing;
    out->delay = delay;
    out->selected = selected;
  }
  for (unsigned i = 0; i < 16; ++i) {
    const auto &t = s.tracks[i];
    app::Track v;
    v.sample = c.byte(t.sample, 0, 1);
    v.muted = c.byte(t.muted, 0, 1);
    v.volume = c.byte(t.volume, 0, 127);
    v.pan = int(c.byte(t.pan + 127, 0, 254)) - 127;
    v.synth_pitch = c.byte(t.sample ? t.synth_pitch : t.pitch, 0, 127);
    v.sample_pitch = c.byte(t.sample ? t.pitch : t.sample_pitch, 0, 127);
    v.length = c.byte(t.length, 0, 127);
    v.wave = c.byte(t.wave, 0, 15);
    v.filter_cutoff = c.byte(t.filter_cutoff, 0, 127);
    v.filter_resonance = c.byte(t.filter_resonance, 0, 127);
    v.delay_send = c.byte(t.delay_send, 0, 127);
    v.playback.start = c.word(t.playback.start);
    v.playback.end = c.word(t.playback.end);
    // START/END may cross during M13 editing; both normalized values are exact.
    v.playback.reverse = c.byte(t.playback.reverse, 0, 1);
    v.playback.mode = sampler::Mode(c.byte(unsigned(t.playback.mode), 0, 1));
    v.playback.choke = c.byte(t.playback.choke, 0, 8);
    v.slices.count = c.byte(t.slices.count, 1, 16);
    v.slices.selected = c.byte(t.slices.selected, 0, 15);
    v.slice_enabled = c.byte(t.slice_enabled, 0, 1);
    c.valid &= v.slices.selected < v.slices.count;
    for (unsigned j = 0; j < 16; ++j) {
      v.slices.slices[j].start = c.word(t.slices.slices[j].start);
      v.slices.slices[j].end = c.word(t.slices.slices[j].end);
      c.valid &= v.slices.slices[j].start < v.slices.slices[j].end;
    }
    v.pitch = v.sample ? v.sample_pitch : v.synth_pitch;
    v.sample_configured = true;
    if (out)
      out->tracks[i] = v;
    c.string(out ? out->references[i] : nullptr, s.references[i],
             reference_bytes, false);
  }
  for (unsigned p = 0; p < 16; ++p) {
    auto length = c.byte(s.patterns[p].length, 1, 16);
    if (out)
      out->patterns[p].length = length;
    for (unsigned t = 0; t < 16; ++t) {
      auto bits = c.word(s.patterns[p].track_steps[t]);
      if (out)
        out->patterns[p].track_steps[t] = bits;
    }
    for (unsigned t = 0; t < 16; ++t)
      for (unsigned j = 0; j < 16; ++j) {
        const auto &m = s.patterns[p].meta[t][j];
        app::StepMeta meta{c.byte(m.velocity, 1, 127),
                           c.byte(m.probability, 0, 100),
                           c.byte(m.ratchets, 1, 4)};
        const auto &l = s.patterns[p].locks[t][j];
        app::StepLocks lock;
        lock.mask = c.byte(l.mask);
        lock.pitch = c.byte(l.pitch, 0, 127);
        lock.volume = c.byte(l.volume, 0, 127);
        lock.pan = int8_t(int(c.byte(int(l.pan) + 127, 0, 254)) - 127);
        lock.wave = c.byte(l.wave, 0, 15);
        lock.filter_cutoff = c.byte(l.filter_cutoff, 0, 127);
        lock.filter_resonance = c.byte(l.filter_resonance, 0, 127);
        lock.delay_send = c.byte(l.delay_send, 0, 127);
        lock.slice = c.byte(l.slice, 0, 15);
        if (out) {
          out->patterns[p].meta[t][j] = meta;
          out->patterns[p].locks[t][j] = lock;
        }
      }
  }
  if (v >= 2) {
    app::PatternChain chain;
    chain.length = c.byte(s.chain.length, 0, 32);
    chain.loop = c.byte(s.chain.loop, 0, 1);
    auto mode = app::TransportMode(c.byte(unsigned(s.mode), 0, 1));
    c.valid &= chain.length || mode == app::TransportMode::Pattern;
    for (unsigned i = 0; i < 32; ++i) {
      chain.entries[i].pattern = c.byte(s.chain.entries[i].pattern, 0, 15);
      chain.entries[i].repeats = c.byte(s.chain.entries[i].repeats, 1, 16);
    }
    if (out) {
      out->chain = chain;
      out->mode = mode;
    }
  } else if (out) {
    out->chain = {};
    out->mode = app::TransportMode::Pattern;
  }
  return c.valid && c.pos == payload_size(v);
}
inline bool encode(const State &s, uint32_t generation, uint8_t *bytes,
                   unsigned n) {
  if (n != file_bytes)
    return false;
  Codec c;
  c.write = bytes + header_bytes;
  if (!fields(c, s, nullptr))
    return false;
  memcpy(bytes, "P4PR", 4);
  put16(bytes + 4, version);
  put16(bytes + 6, header_bytes);
  put32(bytes + 8, payload_bytes);
  put32(bytes + 12, generation);
  put32(bytes + 16, crc32(bytes + header_bytes, payload_bytes));
  put32(bytes + 20, 0);
  return true;
}
inline Error inspect(const uint8_t *bytes, unsigned n, const State &scratch) {
  if (n < header_bytes || memcmp(bytes, "P4PR", 4))
    return Error::Header;
  if (u16(bytes + 4) > version)
    return Error::Future;
  if (u16(bytes + 4) < 1)
    return Error::Version;
  if (u16(bytes + 6) != header_bytes || u32(bytes + 20))
    return Error::Header;
  const unsigned payload = payload_size(u16(bytes + 4));
  if (n != header_bytes + payload || u32(bytes + 8) != payload)
    return Error::Size;
  if (crc32(bytes + header_bytes, payload) != u32(bytes + 16))
    return Error::Crc;
  Codec c;
  c.read = bytes + header_bytes;
  return fields(c, scratch, nullptr, u16(bytes + 4)) ? Error::Ok
                                                     : Error::Semantic;
}
inline Error decode(const uint8_t *bytes, unsigned n, State &out) {
  auto error = inspect(bytes, n, out);
  if (error != Error::Ok)
    return error;
  Codec c;
  c.read = bytes + header_bytes;
  fields(c, out, &out, u16(bytes + 4));
  return Error::Ok;
}
inline bool newer(uint32_t a, uint32_t b) {
  return a != b && uint32_t(a - b) < 0x80000000u;
}
struct Slot {
  bool valid = false;
  uint32_t generation = 0;
};
inline int newest(Slot a, Slot b) {
  return !a.valid                            ? (b.valid ? 1 : -1)
         : !b.valid                          ? 0
         : newer(b.generation, a.generation) ? 1
                                             : 0;
}
struct SavePlan {
  unsigned slot;
  uint32_t generation;
};
inline SavePlan plan(Slot a, Slot b) {
  int good = newest(a, b);
  return {good < 0 ? 0u : unsigned(1 - good),
          good < 0 ? 1u : (good ? b.generation : a.generation) + 1};
}
inline void defaults(State &s) {
  memset(s.name, 0, sizeof(s.name));
  memcpy(s.name, "UNTITLED", 8);
  s.bpm = 120;
  s.swing = 50;
  s.delay = 1;
  s.selected = 0;
  s.chain = {};
  s.mode = app::TransportMode::Pattern;
  for (auto &t : s.tracks)
    t = app::Track{};
  memset(s.references, 0, sizeof(s.references));
  for (auto &p : s.patterns)
    p = app::Pattern{};
  for (unsigned t = 0; t < 16; ++t) {
    s.tracks[t].pitch = s.tracks[t].synth_pitch = 36 + t * 3;
    s.tracks[t].wave = t;
  }
}
inline void reset_runtime(app::Engine &e, const State &s) {
  e.performance = {};
  e.chain = s.chain;
  e.mode = s.mode;
  e.chain_entry = e.chain_repeat = 0;
  e.playing = false;
  e.cancel_ratchets();
  e.solos = 0;
  e.bpm = s.bpm;
  e.swing = e.pair_swing = s.swing;
  e.delay = s.delay;
  e.selected_pattern = e.playing_pattern = s.selected;
  e.queued_pattern = -1;
  e.step = -1;
  e.phase = 0;
  e.first = false;
  e.duration = app::Engine::straight;
  e.seed = e.rng = app::Engine::default_seed;
  e.edit_seed = 0x4d395031;
}
// Caller freezes musical edits for the 17-block transaction. Runtime clocks
// may continue during snapshot; no runtime fields are copied.
inline void snapshot_part(const app::Engine &e, State &s, unsigned part) {
  if (!part) {
    s.chain = e.chain;
    s.mode = e.mode;
    s.bpm = e.bpm;
    s.swing = e.swing;
    s.delay = e.delay;
    s.selected = e.selected_pattern;
    for (unsigned t = 0; t < 16; ++t) {
      s.tracks[t] = e.tracks[t];
      if (s.tracks[t].sample)
        s.tracks[t].sample_pitch = s.tracks[t].pitch;
      else
        s.tracks[t].synth_pitch = s.tracks[t].pitch;
    }
  } else if (part <= 16)
    s.patterns[part - 1] = e.patterns[part - 1];
}
inline void apply_part(app::Engine &e, const State &s, unsigned part) {
  if (!part) {
    reset_runtime(e, s);
    for (unsigned t = 0; t < 16; ++t)
      e.tracks[t] = s.tracks[t];
  } else if (part <= 16)
    e.patterns[part - 1] = s.patterns[part - 1];
}
inline bool musical(app::Kind k) {
  return !(k >= app::Kind::PerfOverride && k <= app::Kind::PerfRepeatStop) &&
         k != app::Kind::Play && k != app::Kind::Trigger &&
         k != app::Kind::Solo && k != app::Kind::GateRelease &&
         k != app::Kind::SliceAudition && k != app::Kind::Reroll &&
         k != app::Kind::Count;
}
} // namespace project
