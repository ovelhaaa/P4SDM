#include "../src/app/project.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
static bool realtime = false;
void *operator new(std::size_t n) {
  assert(!realtime);
  if (auto p = std::malloc(n))
    return p;
  throw std::bad_alloc();
}
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
using namespace app;
void edit(Engine &e, Kind k, int row, int value) {
  Command c{k, 0, value};
  c.step = uint8_t(row);
  e.apply(c);
}
void tick(Engine &e) {
  e.sample([](TriggerEvent) {});
}
void onset(Engine &e) {
  do {
    tick(e);
  } while (e.playing && e.phase > unsigned(e.bpm) * 4);
}
void wrap(Engine &e) {
  auto loops = e.loops;
  do {
    tick(e);
  } while (e.playing && e.loops == loops);
}
void start(Engine &e) {
  e.apply({Kind::ChainMode, 0, 1});
  e.apply({Kind::Play, 0, 1});
  tick(e);
}
int main() {
  static Engine e;
  realtime = true;
  e.apply({Kind::ChainMode, 0, 1});
  assert(e.mode == TransportMode::Pattern);
  for (int i = 0; i < 32; ++i)
    edit(e, Kind::ChainAdd, i, i % 16);
  edit(e, Kind::ChainAdd, 32, 3);
  assert(e.chain.length == 32);
  edit(e, Kind::ChainInsert, 0, 3);
  assert(e.chain.entries[0].pattern == 0);
  edit(e, Kind::ChainPattern, 31, 16);
  edit(e, Kind::ChainRepeats, 31, 0);
  assert(e.chain.entries[31].pattern == 15 && e.chain.entries[31].repeats == 1);
  edit(e, Kind::ChainDelete, 32, 0);
  assert(e.chain.length == 32);
  edit(e, Kind::ChainDelete, 0, 0);
  assert(e.chain.length == 31 && e.chain.entries[0].pattern == 1);
  e.apply({Kind::ChainClear, 0, 0});
  assert(!e.chain.length && e.mode == TransportMode::Pattern);
  // Golden complete onset sequence: P1 x2 (4), P3 x3 (7), P8 x1 (16).
  for (int swing : {50, 60, 75}) {
    e = Engine{};
    e.swing = swing;
    e.bpm = 240;
    e.selected_pattern = 15;
    const int ps[] = {0, 2, 7}, lengths[] = {4, 7, 16}, reps[] = {2, 3, 1};
    for (int i = 0; i < 3; ++i) {
      edit(e, Kind::ChainAdd, i, ps[i]);
      edit(e, Kind::ChainRepeats, i, reps[i]);
      e.patterns[ps[i]].length = lengths[i];
      e.patterns[ps[i]].track_steps[0] = 0xffff;
    }
    start(e);
    assert(e.selected_pattern == 15 && e.playing_pattern == 0);
    e.apply({Kind::SelectPattern, 0, 4});
    assert(e.selected_pattern == 4 && e.queued_pattern == -1);
    e.apply({Kind::ChainMode, 0, 0});
    assert(e.mode == TransportMode::Chain);
    for (int row = 0; row < 3; ++row)
      for (int rep = 0; rep < reps[row]; ++rep)
        for (int step = 0; step < lengths[row]; ++step) {
          assert(e.playing && e.playing_pattern == ps[row] &&
                 e.chain_entry == row && e.chain_repeat == rep &&
                 e.step == step);
          // No early switch, including the final sample of the current loop.
          while (e.phase < e.duration) {
            tick(e);
            assert(e.playing_pattern == ps[row] && e.step == step);
          }
          tick(e);
        }
    assert(!e.playing && e.queued_pattern == -1 && e.step == -1 &&
           e.chain_stops == 1);
    start(e);
    assert(e.chain_entry == 0 && e.chain_repeat == 0 && e.step == 0);
  }
  // Repeats follow real lengths and swing, with no clock reset/drift on switch.
  for (int length : {1, 3, 7, 12, 16})
    for (int swing : {50, 75}) {
      e = Engine{};
      e.swing = swing;
      e.bpm = 240;
      edit(e, Kind::ChainAdd, 0, 0);
      edit(e, Kind::ChainAdd, 1, 1);
      e.patterns[0].length = e.patterns[1].length = length;
      e.chain.loop = true;
      start(e);
      uint64_t samples = 1;
      for (int n = 0; n < 100; ++n) {
        auto loops = e.loops;
        do {
          tick(e);
          ++samples;
        } while (e.loops == loops);
        assert(e.playing_pattern == (n + 1) % 2 && e.step == 0);
        uint64_t loop_duration =
            Engine::straight * (length / 2) * 2 +
            (length % 2 ? Engine::straight * 2 * swing / 100 : 0);
        // Each odd-length Pattern restarts the existing swing pair at step
        // zero.
        assert(samples == 1 + (loop_duration * (n + 1) + 959) / 960);
      }
      assert(e.chain_loops == 50);
    }
  e = Engine{};
  edit(e, Kind::ChainAdd, 0, 0);
  edit(e, Kind::ChainAdd, 1, 1);
  edit(e, Kind::ChainRepeats, 0, 2);
  e.patterns[0].length = 3;
  start(e);
  auto rng = e.rng, edit_rng = e.edit_seed;
  edit(e, Kind::ChainRepeats, 0, 4);
  edit(e, Kind::ChainPattern, 0, 2);
  assert(e.playing_pattern == 0);
  wrap(e);
  assert(e.playing_pattern == 2 && e.chain_repeat == 1);
  edit(e, Kind::ChainRepeats, 0, 1);
  edit(e, Kind::ChainPattern, 1,
       1); // Future entry remains explicit until accepted.
  wrap(e);
  assert(e.playing_pattern == 1 && e.chain_repeat == 0);
  assert(e.rng == rng && e.edit_seed == edit_rng);
  edit(e, Kind::ChainPattern, 1, 3);
  e.chain.loop = true;
  wrap(e);
  assert(e.playing_pattern == 2);
  // Insertion/deletion consult current numerical row; old audible loop
  // survives.
  edit(e, Kind::ChainRepeats, 0, 4);
  edit(e, Kind::ChainInsert, 0, 7);
  assert(e.playing_pattern == 2);
  wrap(e);
  assert(e.playing_pattern == 2 && e.chain_entry == 1);
  edit(e, Kind::ChainDelete, 1, 0);
  assert(e.playing_pattern == 2);
  wrap(e);
  assert(e.chain_entry == 0 && e.playing_pattern == 7);
  edit(e, Kind::ChainDelete, 1, 0);
  edit(e, Kind::ChainDelete, 0, 0);
  assert(e.mode == TransportMode::Pattern && e.queued_pattern == -1 &&
         e.playing_pattern == 7);
  // Accepted ratchet parents survive edits until wrap, then cancel before new
  // step zero.
  e = Engine{};
  edit(e, Kind::ChainAdd, 0, 0);
  edit(e, Kind::ChainAdd, 1, 1);
  e.patterns[0].length = e.patterns[1].length = 1;
  e.patterns[0].track_steps[0] = e.patterns[1].track_steps[0] = 1;
  e.patterns[0].meta[0][0].ratchets = 4;
  e.patterns[0].locks[0][0] = {PITCH_LOCK, 17};
  e.patterns[1].locks[0][0] = {PITCH_LOCK, 99};
  start(e);
  int old_children = 0, new_parents = 0, releases = 0;
  while (e.chain_entry == 0)
    e.sample(
        [&](TriggerEvent ev, bool child) {
          if (child) {
            assert(ev.pitch == 17);
            ++old_children;
          } else {
            assert(ev.pitch == 99);
            ++new_parents;
          }
        },
        [&](int) { ++releases; });
  assert(old_children == 3 && new_parents == 1 && releases == 16);
  assert(e.next_ratchet == UINT64_MAX);
  for (int id = 163; id <= 181; ++id) {
    auto r = widget(id);
    assert(r.h >= 48);
    assert(hit(Page::Chain, r.x + r.w / 2, r.y + r.h / 2) == id);
  }
  realtime = false;
  std::cout
      << "Chain bounds, golden playback, quantization, swing/odd lengths, "
         "edits, queue isolation, RNG, ratchets and allocation guard: PASS\n";
}
