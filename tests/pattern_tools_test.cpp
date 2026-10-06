#include "../src/app/model.h"
#include "../src/hal/display_dirty.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>

static unsigned allocations = 0;
void *operator new(std::size_t n) {
  ++allocations;
  if (void *p = std::malloc(n))
    return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
static bool same(const app::StepMeta &a, const app::StepMeta &b) {
  return a.velocity == b.velocity && a.probability == b.probability &&
         a.ratchets == b.ratchets;
}
static bool same(const app::Pattern &a, const app::Pattern &b) {
  if (a.length != b.length)
    return false;
  for (int t = 0; t < 16; ++t) {
    if (a.track_steps[t] != b.track_steps[t])
      return false;
    for (int i = 0; i < 16; ++i)
      if (!same(a.meta[t][i], b.meta[t][i]))
        return false;
  }
  return true;
}
static app::Command command(app::Kind kind, int value = 0, int track = 3,
                            int pattern = 2, int step = 0) {
  app::Command c{kind, uint8_t(track), value};
  c.pattern = uint8_t(pattern);
  c.step = uint8_t(step);
  return c;
}
static app::Pattern fixture(int n) {
  app::Pattern p;
  p.length = uint8_t(n);
  for (int t = 0; t < 16; ++t) {
    p.track_steps[t] = uint16_t(0x9a65 ^ (t * 257));
    for (int i = 0; i < 16; ++i)
      p.meta[t][i] = {uint8_t(10 + i * 7), uint8_t((i * 6 + t) % 101),
                      uint8_t(1 + i % 4)};
  }
  return p;
}
static void hidden(const app::Pattern &a, const app::Pattern &b, int t) {
  for (int i = a.length; i < 16; ++i) {
    assert(same(a.meta[t][i], b.meta[t][i]));
    assert(((a.track_steps[t] ^ b.track_steps[t]) & (1u << i)) == 0);
  }
}
int main() {
  using app::Kind;
  // Golden local stream and generated musical data: catches algorithm drift.
  app::EditRandom golden(0x4d395031);
  assert(golden.next() == 0x80d7366eu);
  app::Engine gold;
  gold.patterns[2].length = 7;
  gold.apply(
      command(Kind::Randomize, app::generation_parameters(45, 20, 15, 5)));
  assert(gold.patterns[2].track_steps[3] == 0x10);
  assert(gold.patterns[2].meta[3][4].velocity == 83);
  assert(gold.patterns[2].meta[3][4].probability == 97);
  assert(gold.patterns[2].meta[3][4].ratchets == 1);
  for (int n : {1, 3, 7, 12, 16}) {
    app::Engine e;
    e.patterns[2] = fixture(n);
    auto original = e.patterns[2];
    e.apply(command(Kind::Rotate, 1));
    for (int i = 0; i < n; ++i) {
      int j = (i + 1) % n;
      assert(same(original.meta[3][i], e.patterns[2].meta[3][j]));
      assert(((original.track_steps[3] >> i) & 1) ==
             ((e.patterns[2].track_steps[3] >> j) & 1));
    }
    hidden(original, e.patterns[2], 3);
    e.apply(command(Kind::Rotate, -1));
    assert(same(original, e.patterns[2]));
    for (int i = 0; i < n; ++i)
      e.apply(command(Kind::Rotate, 1));
    assert(same(original, e.patterns[2]));
    e.apply(command(Kind::Reverse));
    for (int i = 0; i < n; ++i) {
      assert(same(original.meta[3][i], e.patterns[2].meta[3][n - 1 - i]));
      assert(((original.track_steps[3] >> i) & 1) ==
             ((e.patterns[2].track_steps[3] >> (n - 1 - i)) & 1));
    }
    hidden(original, e.patterns[2], 3);
    e.apply(command(Kind::Reverse));
    assert(same(original, e.patterns[2]));
    e.apply(command(Kind::Rotate, 1, 3, 2, 1));
    for (int t = 0; t < 16; ++t) {
      hidden(original, e.patterns[2], t);
      for (int i = 0; i < n; ++i)
        assert(same(original.meta[t][i], e.patterns[2].meta[t][(i + 1) % n]));
    }
    e.apply(command(Kind::Rotate, -1, 3, 2, 1));
    assert(same(original, e.patterns[2]));
    e.apply(command(Kind::Reverse, 0, 3, 2, 1));
    e.apply(command(Kind::Reverse, 0, 3, 2, 1));
    assert(same(original, e.patterns[2]));
  }
  app::Engine e;
  e.patterns[2] = fixture(7);
  auto original = e.patterns[2];
  e.tracks[5].volume = 17;
  e.tracks[5].pan = -42;
  e.tracks[5].length = 71;
  e.tracks[5].muted = true;
  e.tracks[5].assigned();
  auto instrument = e.tracks[5];
  e.solos = 32;
  e.apply(command(Kind::CopyTrack, 5));
  assert(e.patterns[2].track_steps[5] == original.track_steps[3]);
  for (int i = 0; i < 16; ++i)
    assert(same(e.patterns[2].meta[5][i], original.meta[3][i]));
  assert(e.tracks[5].volume == instrument.volume &&
         e.tracks[5].pan == instrument.pan);
  assert(e.tracks[5].pitch == instrument.pitch &&
         e.tracks[5].sample == instrument.sample);
  assert(e.tracks[5].muted == instrument.muted &&
         e.tracks[5].wave == instrument.wave && e.solos == 32);
  assert(e.tracks[5].length == instrument.length &&
         e.tracks[5].sample_configured == instrument.sample_configured &&
         e.tracks[5].synth_pitch == instrument.synth_pitch &&
         e.tracks[5].sample_pitch == instrument.sample_pitch);
  e.apply(command(Kind::Velocity, 1));
  assert(e.patterns[2].meta[5][0].velocity == original.meta[3][0].velocity);
  e.patterns[2] = original;
  e.apply(command(Kind::ClearTrack));
  assert(e.patterns[2].track_steps[3] == 0 && e.patterns[2].length == 7);
  for (int t = 0; t < 16; ++t) {
    if (t != 3)
      assert(e.patterns[2].track_steps[t] == original.track_steps[t]);
    for (int i = 0; i < 16; ++i)
      assert(same(e.patterns[2].meta[t][i], original.meta[t][i]));
  }
  // Exact mechanical Euclidean masks: least significant bit is step zero.
  struct Euclid {
    int n, k;
    uint16_t mask;
  };
  for (auto v : {Euclid{8, 3, 0x49},
                 {8, 5, 0xb5},
                 {12, 5, 0x529},
                 {16, 7, 0x54a9},
                 {7, 0, 0},
                 {7, 7, 0x7f},
                 {1, 0, 0},
                 {1, 1, 1}}) {
    e.patterns[2] = fixture(v.n);
    auto before = e.patterns[2];
    e.apply(command(Kind::Euclidean, v.k));
    unsigned active = (1u << v.n) - 1;
    assert((e.patterns[2].track_steps[3] & active) == v.mask);
    hidden(before, e.patterns[2], 3);
    for (int i = 0; i < 16; ++i)
      assert(same(before.meta[3][i], e.patterns[2].meta[3][i]));
    for (int r = 0; r < v.n; ++r) {
      e.apply(command(Kind::Euclidean, v.k, 3, 2, r));
      unsigned expected =
          ((unsigned(v.mask) << r) | (unsigned(v.mask) >> (v.n - r))) & active;
      assert((e.patterns[2].track_steps[3] & active) == expected);
    }
  }
  for (int n : {1, 3, 7, 12, 16}) {
    e.patterns[2] = fixture(n);
    auto source = e.patterns[2];
    auto rng = e.rng;
    auto seed = e.edit_seed;
    auto randomize =
        command(Kind::Randomize, app::generation_parameters(45, 20, 15, 5));
    e.apply(randomize);
    auto result = e.patterns[2];
    e.patterns[2] = source;
    e.apply(randomize);
    assert(same(result, e.patterns[2]));
    e.apply(randomize);
    assert(same(result, e.patterns[2]));
    assert(e.rng == rng && e.edit_seed == seed);
    hidden(source, result, 3);
    for (int t = 0; t < 16; ++t) {
      if (t == 3)
        continue;
      assert(result.track_steps[t] == source.track_steps[t]);
      for (int i = 0; i < 16; ++i)
        assert(same(result.meta[t][i], source.meta[t][i]));
    }
    for (const auto &m : result.meta[3])
      assert(m.velocity >= 1 && m.velocity <= 127 && m.probability <= 100 &&
             m.ratchets >= 1 && m.ratchets <= 4);
    for (int density : {0, 100}) {
      e.patterns[2] = source;
      e.apply(command(Kind::Randomize,
                      app::generation_parameters(density, 100, 100, 100)));
      unsigned active = (1u << n) - 1;
      assert((e.patterns[2].track_steps[3] & active) == (density ? active : 0));
      hidden(source, e.patterns[2], 3);
    }
    for (int amount : {0, 10, 20, 100}) {
      e.patterns[2] = source;
      e.apply(command(Kind::Mutate, amount));
      auto mutated = e.patterns[2];
      hidden(source, mutated, 3);
      unsigned changes = 0;
      for (int i = 0; i < n; ++i)
        changes += ((source.track_steps[3] ^ mutated.track_steps[3]) >> i) & 1;
      assert(changes == unsigned((n * amount + 99) / 100));
      if (!amount)
        assert(same(source, mutated));
      e.patterns[2] = source;
      e.apply(command(Kind::Mutate, amount));
      assert(same(mutated, e.patterns[2]));
      assert(e.rng == rng);
    }
  }
  e.patterns[2] = fixture(16);
  e.patterns[2].meta[3][0].velocity = 127;
  e.apply(command(Kind::Randomize, app::generation_parameters(100, 100, 0, 0)));
  assert(e.patterns[2].meta[3][0].velocity == 127);
  for (const auto &m : e.patterns[2].meta[3])
    assert(m.probability == 100 && m.ratchets == 1);
  auto accent_source = e.patterns[2];
  e.apply(command(Kind::Mutate, 100));
  assert(e.patterns[2].meta[3][0].velocity == 127);
  for (int t = 0; t < 16; ++t)
    for (int i = 0; i < 16; ++i) {
      assert(e.patterns[2].meta[t][i].probability ==
             accent_source.meta[t][i].probability);
      assert(e.patterns[2].meta[t][i].ratchets ==
             accent_source.meta[t][i].ratchets);
      if (t != 3) {
        assert(same(e.patterns[2].meta[t][i], accent_source.meta[t][i]));
        assert(e.patterns[2].track_steps[t] == accent_source.track_steps[t]);
      }
    }
  e.patterns[2] = fixture(16);
  auto randomize =
      command(Kind::Randomize, app::generation_parameters(45, 20, 15, 5));
  e.apply(randomize);
  auto generated = e.patterns[2];
  auto playback_rng = e.rng;
  e.apply(command(Kind::Reroll));
  e.apply(randomize);
  assert(!same(generated, e.patterns[2]) && e.rng == playback_rng);
  // Pending accepted parent survives every edit, even a live clear/duplicate.
  e = app::Engine{};
  e.patterns[0].track_steps[3] = 1;
  e.patterns[0].meta[3][0] = {127, 100, 4};
  e.apply({Kind::Play, 0, 1});
  int events = 0;
  e.sample([&](app::TriggerEvent) { ++events; });
  auto phase = e.phase;
  auto next = e.next_ratchet;
  auto rng = e.rng;
  e.queued_pattern = 7;
  for (Kind kind : {Kind::Rotate, Kind::Reverse, Kind::CopyTrack,
                    Kind::ClearTrack, Kind::Duplicate, Kind::Euclidean,
                    Kind::Randomize, Kind::Mutate, Kind::Reroll}) {
    auto p = e.patterns[0];
    e.apply(command(
        kind,
        kind == Kind::Randomize ? app::generation_parameters(45, 20, 15, 5) : 1,
        3, 0));
    assert(e.phase == phase && e.rng == rng && e.next_ratchet == next &&
           e.step == 0 && e.playing_pattern == 0 && e.queued_pattern == 7 &&
           e.playing);
    assert(e.pending[3].velocity == 127 && e.pending[3].count == 4 &&
           e.pending[3].next == 1);
    if (kind == Kind::Duplicate)
      assert(same(p, e.patterns[1]) && e.selected_pattern == 1);
  }
  while (e.phase < e.duration)
    e.sample([&](app::TriggerEvent ev) {
      assert(ev.velocity == 127);
      ++events;
    });
  assert(events == 4);
  // UI captures target and parameters before navigation and confirmations.
  app::Ui ui;
  app::Command c{};
  e = app::Engine{};
  ui.selected = 3;
  e.selected_pattern = 2;
  assert(!ui.tool_action(86, e, c));
  assert(!ui.tool_action(89, e, c));
  e.selected_pattern = 7;
  ui.selected = 10;
  assert(ui.tool_action(90, e, c));
  assert(c.pattern == 2 && c.track == 3 && c.value == 4);
  assert(!ui.tool_action(87, e, c));
  assert(!ui.tool_action(91, e, c));
  assert(!ui.tool_action(90, e, c));
  ui.tool_action(82, e, c);
  e.selected_pattern = 15;
  assert(ui.tool_action(83, e, c));
  assert(c.pattern == 15 && c.value == 0);
  e.patterns[0].meta[4][15].ratchets = 4;
  assert(!ui.tool_action(83, e, c));
  e.selected_pattern = 9;
  assert(ui.tool_action(90, e, c));
  assert(c.pattern == 15 && c.value == 0);
  // No pads/trigger hit targets on TOOLS; controls remain inside the screen.
  for (int id = 80; id <= 94; ++id) {
    auto r = app::widget(id);
    assert(r.w >= 52 && r.h >= 52);
    assert(r.x >= 0 && r.y >= 0 && r.x + r.w <= 800 && r.y + r.h <= 420);
    assert(app::hit(app::Page::Tools, r.x + r.w / 2, r.y + r.h / 2) == id);
  }
  for (int y = 90; y < 420; ++y)
    for (int x = 0; x < 800; ++x)
      assert(app::hit(app::Page::Tools, x, y) < 0 ||
             app::hit(app::Page::Tools, x, y) >= 80);
  auto count = allocations;
  for (int round = 0; round < 1000; ++round)
    for (Kind kind : {Kind::Rotate, Kind::Reverse, Kind::CopyTrack,
                      Kind::ClearTrack, Kind::Duplicate, Kind::Euclidean,
                      Kind::Randomize, Kind::Mutate, Kind::Reroll})
      e.apply(command(kind, 1));
  assert(count == allocations);
  std::cout << "M9 pattern tools: PASS\n";
}
