#include "../src/app/model.h"
#include "../src/app/wav.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <vector>
using namespace app;
static unsigned allocations = 0;
void *operator new(std::size_t n) {
  ++allocations;
  if (auto p = std::malloc(n))
    return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
static Command cmd(Kind k, int v, int p = 0, int t = 0, int s = 0) {
  Command c{k, uint8_t(t), v};
  c.pattern = uint8_t(p);
  c.step = uint8_t(s);
  return c;
}
static bool same(StepLocks a, StepLocks b) {
  return std::memcmp(&a, &b, sizeof(a)) == 0;
}
static bool same(TriggerEvent a, TriggerEvent b) {
  return a.track == b.track && a.velocity == b.velocity && a.pitch == b.pitch &&
         a.volume == b.volume && a.pan == b.pan && a.wave == b.wave &&
         a.locked_mask == b.locked_mask;
}
int main() {
  Engine e;
  for (auto &p : e.patterns)
    for (auto &lane : p.locks)
      for (auto l : lane)
        assert(l.mask == 0);
  e.tracks[0].pitch = 48;
  e.tracks[0].volume = 80;
  e.tracks[0].pan = -20;
  e.tracks[0].wave = 3;
  auto base = e.tracks[0];
  for (int pan : {-127, 0, 127})
    for (int volume : {0, 20, 127}) {
      e.apply(cmd(Kind::LockPitch, 72));
      e.apply(cmd(Kind::LockVolume, volume));
      e.apply(cmd(Kind::LockPan, pan));
      e.apply(cmd(Kind::LockWave, 12));
      auto a = resolve_event(0, base, 64, e.patterns[0].locks[0][0]);
      auto b = resolve_event(0, base, 100, e.patterns[0].locks[0][1]);
      assert(a.pitch == 72 && a.volume == volume && a.pan == pan &&
             a.wave == 12 && a.locked_mask == 15);
      assert(b.pitch == 48 && b.volume == 80 && b.pan == -20 && b.wave == 3 &&
             !b.locked_mask);
      assert(e.tracks[0].pitch == 48 && e.tracks[0].volume == 80 &&
             e.tracks[0].pan == -20 && e.tracks[0].wave == 3);
      TriggerEvent pad{};
      e.audition(0, [&](TriggerEvent ev) { pad = ev; });
      assert(same(pad, resolve_event(0, base, 127)));
    }
  // Bounds, explicit equality presence, independently addressed intent.
  e.apply(cmd(Kind::LockPitch, 999, 3, 7, 9));
  e.apply(cmd(Kind::LockPan, -999, 3, 7, 9));
  e.apply(cmd(Kind::LockWave, 999, 3, 7, 9));
  e.apply(cmd(Kind::LockVolume, 80, 3, 7, 9));
  e.apply({Kind::InspectPattern, 0, 4});
  const auto intent = e.patterns[3].locks[7][9];
  assert(intent.mask == 15 && intent.pitch == 127 && intent.pan == -127 &&
         intent.wave == 15 && intent.volume == 80);
  e.apply(cmd(Kind::UnlockParam, VOLUME_LOCK, 3, 7, 9));
  assert(e.patterns[3].locks[7][9].mask == 13 &&
         !e.patterns[4].locks[7][9].mask);
  // Source switching preserves stored wave; PCM pitch follows existing C4 math.
  e.tracks[0].source(true);
  e.apply(cmd(Kind::LockPitch, 48));
  auto sample = resolve_event(0, e.tracks[0], 127, e.patterns[0].locks[0][0]);
  assert(sample.pitch == 48 && sampler::pitch_increment(sample.pitch) == 32768);
  assert(sample.wave == 3 && !(sample.locked_mask & WAVE_LOCK));
  assert(sampler::pitch_increment(resolve_event(0, e.tracks[0], 127).pitch) ==
         65536);
  assert(e.patterns[0].locks[0][0].mask == 15);
  e.tracks[0].source(false);
  assert(resolve_event(0, e.tracks[0], 127, e.patterns[0].locks[0][0]).wave ==
         12);
  // A 4x parent snapshots all params across edits, clears AND base changes.
  e.selected_pattern = 0;
  e.patterns[0].length = 1;
  e.patterns[0].track_steps[0] = 1;
  e.patterns[0].meta[0][0] = {64, 100, 4};
  e.bpm = 240;
  e.apply(cmd(Kind::Play, 1));
  std::vector<TriggerEvent> hits;
  e.sample([&](TriggerEvent ev) { hits.push_back(ev); });
  auto captured = hits[0];
  e.apply(cmd(Kind::ClearStepLocks, 0));
  e.apply(cmd(Kind::Volume, 99));
  for (int i = 1; i < 2757; ++i)
    e.sample([&](TriggerEvent ev) { hits.push_back(ev); });
  assert(hits.size() == 4);
  for (auto h : hits)
    assert(same(h, captured));
  e.sample([&](TriggerEvent ev) { hits.push_back(ev); });
  assert(hits.size() == 5 && hits.back().volume == 99 &&
         !hits.back().locked_mask);
  e.apply(cmd(Kind::Play, 0));
  for (int i = 0; i < 3000; ++i)
    e.sample([&](TriggerEvent ev) { hits.push_back(ev); });
  assert(hits.size() == 5 && e.tracks[0].volume == 99);
  {
    Engine edited;
    edited.patterns[0].length = 1;
    edited.patterns[0].track_steps[0] = 1;
    edited.patterns[0].meta[0][0] = {100, 100, 4};
    edited.patterns[0].locks[0][0] = {15, 72, 20, 80, 12};
    edited.bpm = 240;
    edited.apply(cmd(Kind::Play, 1));
    hits.clear();
    edited.sample([&](TriggerEvent ev) { hits.push_back(ev); });
    edited.apply(cmd(Kind::LockPitch, 48));
    edited.apply(cmd(Kind::LockVolume, 127));
    edited.apply(cmd(Kind::LockPan, -127));
    edited.apply(cmd(Kind::LockWave, 3));
    for (int i = 1; i <= 2757; ++i)
      edited.sample([&](TriggerEvent ev) { hits.push_back(ev); });
    assert(hits.size() == 5);
    for (int i = 0; i < 4; ++i)
      assert(same(hits[i], hits[0]));
    assert(hits[4].pitch == 48 && hits[4].volume == 127 &&
           hits[4].pan == -127 && hits[4].wave == 3 &&
           hits[4].locked_mask == 15);
  }
  // Failed probability and performance gates never resolve/apply a lock.
  e.patterns[0].locks[0][0] = {15, 72, 20, -127, 12};
  e.patterns[0].meta[0][0].probability = 0;
  auto accepted_before = e.locked_parents;
  e.apply(cmd(Kind::Play, 1));
  unsigned calls = 0;
  for (int i = 0; i < 6000; ++i)
    e.sample([&](TriggerEvent) { ++calls; });
  assert(!calls && e.locked_parents == accepted_before);
  e.patterns[0].meta[0][0].probability = 100;
  e.tracks[0].muted = true;
  for (int i = 0; i < 6000; ++i)
    e.sample([&](TriggerEvent) { ++calls; });
  assert(!calls);
  e.tracks[0].muted = false;
  e.solos = 2;
  for (int i = 0; i < 6000; ++i)
    e.sample([&](TriggerEvent) { ++calls; });
  assert(!calls);
  e.audition(
      0, [&](TriggerEvent ev) { assert(ev.volume == 99 && !ev.locked_mask); });
  // A final locked 4x step completes before B's unlocked step zero.
  Engine sw;
  sw.patterns[0].length = 1;
  sw.patterns[0].track_steps[0] = 1;
  sw.patterns[0].meta[0][0] = {100, 100, 4};
  sw.patterns[0].locks[0][0] = {PAN_LOCK, 0, 0, -127, 0};
  sw.patterns[1].track_steps[0] = 1;
  sw.bpm = 240;
  sw.apply(cmd(Kind::Play, 1));
  hits.clear();
  sw.sample([&](TriggerEvent ev) { hits.push_back(ev); });
  sw.apply({Kind::SelectPattern, 0, 1});
  for (int i = 1; i <= 2757; ++i)
    sw.sample([&](TriggerEvent ev) { hits.push_back(ev); });
  assert(hits.size() == 5);
  for (int i = 0; i < 4; ++i)
    assert(hits[i].pan == -127);
  assert(hits[4].pan == 0 && !hits[4].locked_mask);
  // Every bundle transform, hidden steps, positional generators and clears.
  for (int n : {1, 3, 7, 12, 16}) {
    Engine tools;
    auto &p = tools.patterns[2];
    p.length = uint8_t(n);
    for (int t = 0; t < 16; ++t) {
      p.track_steps[t] = 0x9563;
      for (int i = 0; i < 16; ++i) {
        p.meta[t][i] = {uint8_t(10 + i), uint8_t(i * 6), uint8_t(1 + i % 4)};
        p.locks[t][i] = {uint8_t(i % 16), uint8_t(30 + i), uint8_t(70 + i),
                         int8_t(i - 8), uint8_t(i)};
      }
    }
    auto original = p;
    tools.apply(cmd(Kind::Rotate, 1, 2, 0, 1));
    for (int t = 0; t < 16; ++t)
      for (int i = 0; i < 16; ++i) {
        int j = i < n ? (i + 1) % n : i;
        assert(same(p.locks[t][j], original.locks[t][i]));
        assert(std::memcmp(&p.meta[t][j], &original.meta[t][i],
                           sizeof(StepMeta)) == 0);
        assert(((p.track_steps[t] >> j) & 1) ==
               ((original.track_steps[t] >> i) & 1));
      }
    tools.apply(cmd(Kind::Rotate, -1, 2, 0, 1));
    tools.apply(cmd(Kind::Reverse, 0, 2, 0, 1));
    for (int t = 0; t < 16; ++t)
      for (int i = 0; i < 16; ++i) {
        int j = i < n ? n - 1 - i : i;
        assert(same(p.locks[t][j], original.locks[t][i]));
        assert(std::memcmp(&p.meta[t][j], &original.meta[t][i],
                           sizeof(StepMeta)) == 0);
        assert(((p.track_steps[t] >> j) & 1) ==
               ((original.track_steps[t] >> i) & 1));
      }
    tools.apply(cmd(Kind::Reverse, 0, 2, 0, 1));
    for (Kind k : {Kind::Euclidean, Kind::Randomize, Kind::Mutate,
                   Kind::ClearTrack, Kind::ClearPattern}) {
      tools.apply(cmd(
          k, k == Kind::Randomize ? generation_parameters(80, 30, 40, 60) : 7,
          2));
      assert(std::memcmp(p.locks, original.locks, sizeof(p.locks)) == 0);
    }
    tools.apply(cmd(Kind::CopyTrack, 8, 2, 3));
    assert(std::memcmp(p.locks[8], p.locks[3], sizeof(p.locks[3])) == 0);
    for (Kind k : {Kind::CopyPattern, Kind::Duplicate}) {
      tools.apply(cmd(k, 6, 2));
      assert(std::memcmp(tools.patterns[6].locks, p.locks, sizeof(p.locks)) ==
             0);
      tools.apply(cmd(Kind::LockPitch, 127, 6, 3, 5));
      assert(p.locks[3][5].pitch == 35);
    }
    auto meta = p.meta[3][5];
    auto mask = p.track_steps[3];
    tools.apply(cmd(Kind::ClearStepLocks, 0, 2, 3, 5));
    assert(!p.locks[3][5].mask && p.track_steps[3] == mask);
    assert(std::memcmp(&meta, &p.meta[3][5], sizeof(meta)) == 0);
  }
  // Resident PCM gain math uses M9 pan law and Q15 velocity attenuation.
  for (int pcm : {-32768, -17000, 0, 17000, 32767})
    for (int v : {1, 64, 127})
      for (int vol : {0, 20, 80, 127})
        for (int pan : {-127, 0, 127}) {
          int scaled = scale_velocity(int16_t(pcm), velocity_gain(v));
          for (bool right : {false, true}) {
            int gain = event_channel_gain(vol, pan, right);
            assert(gain >= 0 && gain <= 127);
            int output = int(int64_t(scaled) * gain * 255 / 65536);
            assert(output >= -16384 && output <= 16384);
            if (!vol)
              assert(output == 0);
          }
        }
  for (Page page : {Page::Locks, Page::Step}) {
    std::vector<int> ids;
    if (page == Page::Locks)
      for (int i = 96; i <= 105; ++i)
        ids.push_back(i);
    else {
      for (int i = 70; i <= 77; ++i)
        ids.push_back(i);
      ids.push_back(95);
    }
    for (auto id : ids) {
      auto r = widget(id);
      assert(r.x >= 0 && r.y >= 0 && r.x + r.w <= 800 && r.y + r.h <= 480 &&
             r.h >= 52);
      assert(hit(page, r.x + r.w / 2, r.y + r.h / 2) == id);
      for (auto other : ids)
        if (id != other) {
          auto b = widget(other);
          assert(r.x + r.w <= b.x || b.x + b.w <= r.x || r.y + r.h <= b.y ||
                 b.y + b.h <= r.y);
        }
    }
  }
  // UI enabling starts at the current source-specific base; commands freeze
  // intent.
  Ui ui;
  Engine editor;
  Command c{};
  ui.selected = 7;
  ui.selected_step = 9;
  editor.selected_pattern = 3;
  editor.tracks[7].pan = -20;
  editor.tracks[7].wave = 12;
  const int values[] = {editor.tracks[7].pitch, 80, -20, 12};
  for (int param = 0; param < 4; ++param) {
    assert(ui.lock_action(97 + param * 2, 0, true, editor, c));
    assert(c.pattern == 3 && c.track == 7 && c.step == 9 &&
           c.value == values[param]);
    editor.apply(c);
    assert(!ui.lock_action(97 + param * 2, 0, false, editor, c));
    assert(ui.lock_action(97 + param * 2, 0, true, editor, c) &&
           c.kind == Kind::UnlockParam);
    assert(c.value == (1 << param));
    editor.apply(c);
  }
  editor.tracks[7].source(true);
  assert(ui.lock_action(97, 0, true, editor, c) && c.value == 60);
  assert(!ui.lock_action(103, 0, true, editor, c) &&
         !ui.lock_action(102, 400, true, editor, c));
  assert(ui.lock_action(104, 0, true, editor, c));
  editor.selected_pattern = 4;
  ui.selected = 2;
  ui.selected_step = 1;
  assert(c.pattern == 3 && c.track == 7 && c.step == 9);
  // Allocation audit over lock edits, event resolution and ratchet scheduling.
  Engine realtime;
  realtime.patterns[0].track_steps[0] = 0xffff;
  realtime.patterns[0].meta[0][0].ratchets = 4;
  realtime.apply(cmd(Kind::Play, 1));
  const auto before = allocations;
  for (int i = 0; i < 100000; ++i) {
    if (i % 256 == 0)
      realtime.apply(cmd(Kind(int(Kind::LockPitch) + (i / 256) % 4), i % 128));
    if (i % 1024 == 0)
      realtime.apply(cmd(Kind::ClearStepLocks, 0));
    realtime.sample([](TriggerEvent) {});
  }
  assert(allocations == before);
  std::cout << "M10 locks: PASS; Pattern=" << sizeof(Pattern)
            << " bank=" << sizeof(e.patterns) << " Engine=" << sizeof(Engine)
            << "\n";
}
