#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
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
  Source
};
struct Command {
  Kind kind;
  uint8_t track;
  int value;
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
  uint16_t steps = 0;
  int volume = 80, pan = 0, pitch = 48, length = 32, wave = 0;
  bool muted = false, sample = false;
};
struct Engine {
  Track tracks[16];
  int bpm = 120, step = 15;
  bool playing = false, delay = true;
  uint64_t phase = 0;
  bool first = false;
  Engine() {
    for (int i = 0; i < 16; ++i) {
      tracks[i].pitch = 36 + i * 3;
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
      phase = 0;
      step = 15;
      first = playing;
      break;
    case Kind::Bpm:
      bpm = clamp(c.value, 30, 400);
      break;
    case Kind::Step:
      if (c.value >= 0 && c.value < 16)
        t.steps ^= uint16_t(1u << c.value);
      break;
    case Kind::Volume:
      t.volume = clamp(c.value, 0, 127);
      break;
    case Kind::Pan:
      t.pan = clamp(c.value, -127, 127);
      break;
    case Kind::Pitch:
      t.pitch = clamp(c.value, 0, 127);
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
      t.sample = c.value;
      break;
    case Kind::Trigger:
      break;
    }
  }
  // Exact rational sixteenth clock: accumulated BPM*4 per sample, no rounding
  // drift.
  template <class Trigger> void sample(Trigger trigger) {
    if (!playing)
      return;
    if (first || phase >= 44100ull * 60) {
      first = false;
      if (phase >= 44100ull * 60)
        phase -= 44100ull * 60;
      step = (step + 1) % 16;
      for (int t = 0; t < 16; ++t)
        if (!tracks[t].muted && (tracks[t].steps & (1u << step)))
          trigger(t);
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
enum class Page { Sequence, Track, Fx, Sample };
struct Ui {
  Page page = Page::Sequence;
  int selected = 0, bank = 0, selected_step = -1, capture = -1;
  bool down = false;
};
inline Rect widget(int id) {
  if (id >= 38)
    return {24 + ((id - 38) % 2) * 380, 100 + ((id - 38) / 2) * 90, 360, 72};
  if (id < 16)
    return {16 + (id % 8) * 96, 100 + (id / 8) * 66, 88, 58};
  if (id < 24)
    return {16 + (id - 16) % 4 * 192, 250 + (id - 16) / 4 * 70, 184, 62};
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
  if (p == Page::Sequence) {
    for (int i = 0; i < 24; ++i)
      if (widget(i).contains(x, y))
        return i;
  } else if (p == Page::Track) {
    for (int i = 24; i < 29; ++i)
      if (widget(i).contains(x, y))
        return i;
    if (Rect{24, 250, 200, 56}.contains(x, y))
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
