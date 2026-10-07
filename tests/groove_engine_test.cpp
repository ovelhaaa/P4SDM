#include "../src/app/model.h"
#include "../src/app/wav.h"
#include "../src/hal/display_dirty.h"
#include <cassert>
#include <vector>
using namespace app;
static uint64_t ceildiv(uint64_t a, uint64_t b) { return (a + b - 1) / b; }
static void edit(Engine &e, Kind kind, int p, int t, int step, int value) {
  Command c{kind, uint8_t(t), value};
  c.pattern = uint8_t(p);
  c.step = uint8_t(step);
  e.apply(c);
}
static std::vector<uint64_t> decisions(uint32_t seed, bool restart = false) {
  Engine e;
  e.seed = seed;
  e.patterns[0].length = 1;
  e.patterns[0].track_steps[0] = 1;
  e.patterns[0].meta[0][0] = {64, 47, 4};
  e.bpm = 400;
  e.apply({Kind::Play, 0, 1});
  auto run = [&]() {
    std::vector<uint64_t> result;
    for (uint64_t i = 0; i < 200000; ++i)
      e.sample([&](TriggerEvent ev) {
        assert(ev.track == 0 && ev.velocity == 64);
        result.push_back(i);
      });
    return result;
  };
  auto first = run();
  if (restart) {
    e.apply({Kind::Play, 0, 0});
    e.apply({Kind::Play, 0, 1});
    assert(first == run());
  }
  return first;
}
int main() {
  Engine metadata;
  for (auto &p : metadata.patterns)
    for (auto &t : p.meta)
      for (auto &m : t)
        assert(m.velocity == 100 && m.probability == 100 && m.ratchets == 1);
  edit(metadata, Kind::Velocity, 3, 7, 9, 42);
  edit(metadata, Kind::Probability, 3, 7, 9, 23);
  edit(metadata, Kind::Ratchet, 3, 7, 9, 4);
  metadata.apply({Kind::InspectPattern, 0, 5});
  Command copy{Kind::CopyPattern, 0, 8};
  copy.pattern = 3;
  metadata.apply(copy);
  edit(metadata, Kind::Velocity, 8, 7, 9, 127);
  assert(metadata.patterns[3].meta[7][9].velocity == 42);
  assert(metadata.patterns[8].meta[7][9].probability == 23 &&
         metadata.patterns[8].meta[7][9].ratchets == 4);
  Command clear{Kind::ClearPattern, 0, 0};
  clear.pattern = 8;
  metadata.apply(clear);
  assert(metadata.patterns[8].meta[7][9].velocity == 127);
  Command toggle{Kind::Step, 7, 9};
  toggle.pattern = 8;
  metadata.apply(toggle);
  metadata.apply(toggle);
  assert(metadata.patterns[8].meta[7][9].ratchets == 4);
  edit(metadata, Kind::Velocity, 8, 7, 9, 0);
  edit(metadata, Kind::Probability, 8, 7, 9, 200);
  edit(metadata, Kind::Ratchet, 8, 7, 9, 9);
  assert(metadata.patterns[8].meta[7][9].velocity == 1 &&
         metadata.patterns[8].meta[7][9].probability == 100 &&
         metadata.patterns[8].meta[7][9].ratchets == 4);
  // Exact rational parent/subdivision positions, thousands of pairs, odd loops.
  for (int bpm : {97, 123, 240, 400})
    for (int swing : {50, 55, 60, 66, 75})
      for (int length : {1, 3, 5, 7, 16})
        for (int ratchet : {1, 2, 3, 4}) {
          Engine e;
          e.bpm = bpm;
          e.swing = swing;
          e.patterns[0].length = length;
          e.patterns[0].track_steps[0] = 0xffff;
          for (auto &m : e.patterns[0].meta[0])
            m = {100, 100, uint8_t(ratchet)};
          e.apply({Kind::Play, 0, 1});
          uint64_t ideal = 0, frame = 0;
          unsigned parent = 0, sub = 0;
          const unsigned parents = length == 16 && ratchet == 1 ? 2048 : 32;
          while (parent < parents) {
            e.sample([&](TriggerEvent event) {
              assert(event.track == 0 && event.velocity == 100);
              int position = parent % length;
              uint64_t duration = Engine::straight * 2 *
                                  unsigned(position % 2 ? 100 - swing : swing) /
                                  100;
              assert(frame == ceildiv(ideal + ceildiv(duration * sub, ratchet),
                                      bpm * 4));
              assert(frame < ceildiv(ideal + duration, bpm * 4));
              if (++sub == unsigned(ratchet)) {
                sub = 0;
                ++parent;
                ideal += duration;
              }
            });
            ++frame;
          }
          assert(e.ratchet_events == parents * unsigned(ratchet - 1));
        }
  // Known xorshift stream and exact probability decisions, not aggregate rate.
  Engine rng;
  // Reference recurrence independent of Engine implementation.
  uint32_t reference = Engine::default_seed;
  for (int i = 0; i < 100; ++i) {
    reference = reference ^ (reference << 13);
    reference = reference ^ (reference >> 17);
    reference = reference ^ (reference << 5);
    assert(rng.random() == reference);
  }
  // Independent exact reference for intermediate probability and all subhits.
  std::vector<uint64_t> expected_decisions;
  uint32_t state = 123;
  for (uint64_t occurrence = 0;; ++occurrence) {
    auto start = ceildiv(occurrence * Engine::straight, 1600);
    if (start >= 200000)
      break;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    if (uint64_t(state) * 100 < 47ull * (1ull << 32))
      for (unsigned sub = 0; sub < 4; ++sub) {
        auto frame = ceildiv(
            occurrence * Engine::straight + Engine::straight * sub / 4, 1600);
        if (frame < 200000)
          expected_decisions.push_back(frame);
      }
  }
  assert(decisions(123) == expected_decisions);
  Queue<8> intent;
  Command velocity{Kind::Velocity, 7, 88};
  velocity.pattern = 3;
  velocity.step = 9;
  assert(intent.push({Kind::InspectPattern, 0, 11}));
  assert(intent.push(velocity));
  Command received{};
  while (intent.pop(received))
    metadata.apply(received);
  assert(metadata.patterns[3].meta[7][9].velocity == 88);
  assert(metadata.patterns[11].meta[7][9].velocity == 100);
  auto same = decisions(123, true);
  assert(same == decisions(123));
  assert(same != decisions(456));
  for (int probability : {0, 100}) {
    Engine e;
    e.patterns[0].length = 1;
    e.patterns[0].track_steps[0] = 1;
    e.patterns[0].meta[0][0] = {1, uint8_t(probability), 4};
    e.apply({Kind::Play, 0, 1});
    unsigned hits = 0;
    for (int i = 0; i < 10000; ++i)
      e.sample([&](TriggerEvent v) {
        assert(v.velocity == 1);
        ++hits;
      });
    assert(probability ? hits > 0 && e.probability_skipped == 0
                       : hits == 0 && e.probability_passed == 0);
    unsigned before = hits;
    e.audition(0, [&](TriggerEvent v) {
      assert(v.velocity == 127);
      ++hits;
    });
    assert(hits == before + 1);
    e.apply({Kind::Play, 0, 0});
    for (int i = 0; i < 10000; ++i)
      e.sample([&](TriggerEvent) { assert(false); });
  }
  // Pattern switch keeps RNG, resets pairing, cancels stale schedules.
  Engine e;
  e.swing = 75;
  e.patterns[0].length = 1;
  e.patterns[1].length = 3;
  e.patterns[0].track_steps[0] = 1;
  e.patterns[0].meta[0][0] = {127, 100, 4};
  e.patterns[1].track_steps[1] = 7;
  e.patterns[1].meta[1][0] = {1, 100, 1};
  e.apply({Kind::Play, 0, 1});
  unsigned old_hits = 0, new_hits = 0;
  e.sample([&](TriggerEvent) { ++old_hits; });
  uint32_t after_first = e.rng;
  e.apply({Kind::SelectPattern, 0, 1});
  e.apply({Kind::Mute, 0, 1});
  auto boundary = ceildiv(Engine::straight * 150 / 100, 480);
  for (uint64_t frame = 1; frame <= boundary; ++frame)
    e.sample([&](TriggerEvent v) {
      if (frame < boundary) {
        assert(v.track == 0 && v.velocity == 127);
        ++old_hits;
      } else {
        assert(v.track == 1 && v.velocity == 1);
        ++new_hits;
      }
    });
  assert(old_hits == 4 && new_hits == 1 && e.step == 0 && e.pair_swing == 75 &&
         e.rng != after_first);
  Engine stream;
  stream.rng = after_first;
  assert(e.rng == stream.random());
  // Swing updates are latched for an entire pair.
  e.apply({Kind::Swing, 0, 55});
  assert(e.pair_swing == 75);
  // Event gain has unity full-scale, signed safety, retrigger replacement.
  int16_t pcm[] = {-32768, -32767, -1, 0, 1, 32767};
  sampler::Sample sample;
  sample.data = pcm;
  sample.frames = 6;
  sampler::Voice voice;
  voice.assign(&sample);
  for (int velocity : {1, 64, 100, 127}) {
    voice.trigger(65536, {}, false, 0);
    for (auto value : pcm) {
      auto out = scale_velocity(voice.next(), velocity_gain(velocity));
      assert(out == int32_t(value) * velocity_gain(velocity) / 32768);
      assert(out <= 32767 && out >= -32768);
      if (velocity == 127)
        assert(out == value);
    }
  }
  // STEP geometry is bounded, large and disjoint from global navigation.
  for (Page page : {Page::Step, Page::Sequence}) {
    std::vector<int> ids = {29, 30, 31, 32, 33, 34, 35, 44};
    if (page == Page::Step)
      for (int i = 70; i <= 77; ++i)
        ids.push_back(i);
    else {
      for (int i = 0; i < 24; ++i)
        ids.push_back(i);
      ids.push_back(68);
      ids.push_back(69);
    }
    for (int id : ids) {
      auto r = widget(id);
      assert(r.w >= 56 && r.h >= 52 && r.x + r.w <= 800 && r.y + r.h <= 480);
      assert(hit(page, r.x + r.w / 2, r.y + r.h / 2) == id);
      for (int other : ids)
        if (other != id) {
          auto b = widget(other);
          assert(r.x + r.w <= b.x || b.x + b.w <= r.x || r.y + r.h <= b.y ||
                 b.y + b.h <= r.y);
        }
    }
  }
  display::DirtyMask dirty;
  auto r = widget(71);
  dirty.logical_rect(r.x, r.y, r.w, r.h);
  assert(dirty.bytes() < 800 * 480 * 2 / 3);
}
