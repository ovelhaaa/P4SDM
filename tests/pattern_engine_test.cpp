#include "../src/app/model.h"
#include "../src/hal/display_dirty.h"
#include <cassert>
#include <cstring>
#include <vector>
using namespace app;
static void edit(Engine &e, Kind k, int p, int v, int t = 0) {
  Command c{k, uint8_t(t), v};
  c.pattern = uint8_t(p);
  e.apply(c);
}
static std::vector<int> onset(Engine &e) {
  e.phase = 44100ull * 60;
  std::vector<int> notes;
  e.sample([&](int t) { notes.push_back(t); });
  return notes;
}
int main() {
  Engine e;
  for (const auto &p : e.patterns) {
    assert(p.length == 16);
    for (auto m : p.track_steps)
      assert(m == 0);
  }
  // Exact rational clock and loop order, independent of audio block grouping.
  for (int length : {1, 3, 7, 12, 16})
    for (int bpm : {97, 123, 400}) {
      Engine clock;
      clock.patterns[0].track_steps[0] = 0xffff;
      edit(clock, Kind::PatternLength, 0, length);
      clock.apply({Kind::Bpm, 0, bpm});
      clock.apply({Kind::Play, 0, 1});
      unsigned n = 0;
      uint64_t frame = 0;
      while (n < unsigned(length * 3)) {
        clock.sample([&](int t) {
          assert(t == 0 && clock.step == int(n % length));
          assert(frame == (uint64_t(n) * 44100 * 60 + bpm * 4 - 1) / (bpm * 4));
          ++n;
        });
        ++frame;
      }
    }
  // P1 completes all steps; P2 starts at step zero on the next exact onset.
  e.patterns[0].track_steps[0] = e.patterns[1].track_steps[1] = 0xffff;
  e.patterns[1].length = 7;
  e.apply({Kind::Play, 0, 1});
  for (int i = 0; i < 16; ++i) {
    assert(onset(e) == std::vector<int>{0});
    assert(e.step == i);
    if (i == 4)
      e.apply({Kind::SelectPattern, 0, 1});
    assert(e.playing_pattern == 0);
  }
  assert(onset(e) == std::vector<int>{1});
  assert(e.step == 0 && e.playing_pattern == 1 && e.queued_pattern == -1);
  e.apply({Kind::SelectPattern, 0, 0});
  for (int i = 1; i < 7; ++i) {
    assert(onset(e) == std::vector<int>{1});
    assert(e.step == i);
  }
  assert(onset(e) == std::vector<int>{0});
  assert(e.step == 0);
  // Queue P3 alone while P1 plays: finish P1, begin P3 at zero.
  Engine single;
  single.patterns[0].track_steps[0] = 0xffff;
  single.patterns[2].track_steps[2] = 0xffff;
  single.apply({Kind::Play, 0, 1});
  for (int step = 0; step < 16; ++step) {
    if (step == 8)
      single.apply({Kind::SelectPattern, 0, 2});
    assert(onset(single) == std::vector<int>{0});
    assert(single.step == step && single.playing_pattern == 0);
  }
  assert(onset(single) == std::vector<int>{2});
  assert(single.step == 0 && single.playing_pattern == 2);
  // A command carries edit intent through changing selection in a fixed queue.
  Queue<8> intent;
  Command intended{Kind::Step, 7, 12};
  intended.pattern = 9;
  assert(intent.push({Kind::SelectPattern, 0, 3}));
  assert(intent.push(intended));
  assert(intent.push({Kind::InspectPattern, 0, 10}));
  Command received{};
  while (intent.pop(received))
    single.apply(received);
  assert(single.patterns[9].track_steps[7] == 4096 &&
         single.patterns[10].track_steps[7] == 0);
  // Clear/copy into the playing slot is visible at the next onset only;
  // it never resets phase or retriggers the current step.
  Engine live;
  live.patterns[0].track_steps[0] = 0xffff;
  live.apply({Kind::Play, 0, 1});
  assert(onset(live) == std::vector<int>{0});
  uint64_t phase_before = live.phase;
  edit(live, Kind::ClearPattern, 0, 0);
  assert(live.step == 0 && live.phase == phase_before && onset(live).empty());
  live.patterns[1].track_steps[2] = 0xffff;
  live.patterns[1].length = 1;
  phase_before = live.phase;
  edit(live, Kind::CopyPattern, 1, 0);
  assert(live.step == 1 && live.phase == phase_before);
  assert(onset(live) == std::vector<int>{2});
  assert(live.step == 0);
  // Latest queue wins. Inspect/edit P5 leaves queued P4 intact.
  e.apply({Kind::SelectPattern, 0, 2});
  e.apply({Kind::SelectPattern, 0, 3});
  e.apply({Kind::InspectPattern, 0, 4});
  edit(e, Kind::Step, 4, 9, 15);
  assert(e.selected_pattern == 4 && e.queued_pattern == 3 &&
         e.playing_pattern == 0);
  assert(e.patterns[4].track_steps[15] == 512 &&
         e.patterns[0].track_steps[15] == 0);
  for (int i = 1; i < 16; ++i)
    onset(e);
  onset(e);
  assert(e.playing_pattern == 3 && e.step == 0);
  // Selecting playing slot cancels queue. STOP cancels queue and adopts edit
  // slot; PLAY always starts selected slot at zero, including inspect-only
  // selection.
  e.apply({Kind::SelectPattern, 0, 2});
  e.apply({Kind::SelectPattern, 0, 3});
  assert(e.queued_pattern == -1);
  e.apply({Kind::SelectPattern, 0, 2});
  e.apply({Kind::InspectPattern, 0, 5});
  e.apply({Kind::Play, 0, 0});
  assert(e.queued_pattern == -1 && e.playing_pattern == 5);
  e.apply({Kind::SelectPattern, 0, 6});
  assert(e.playing_pattern == 6);
  e.apply({Kind::InspectPattern, 0, 5});
  assert(e.playing_pattern == 6);
  e.apply({Kind::Play, 0, 1});
  onset(e);
  assert(e.step == 0 && e.playing_pattern == 5);
  // Shorten beyond live head: no immediate trigger/move; one wrap next onset.
  Engine shortened;
  shortened.patterns[0].track_steps[0] = 0xffff;
  shortened.apply({Kind::Play, 0, 1});
  for (int i = 0; i <= 10; ++i)
    onset(shortened);
  edit(shortened, Kind::PatternLength, 0, 7);
  assert(shortened.step == 10);
  assert(onset(shortened) == std::vector<int>{0});
  assert(shortened.step == 0);
  for (int i = 1; i < 7; ++i) {
    onset(shortened);
    assert(shortened.step == i);
  }
  edit(shortened, Kind::PatternLength, 0, 16);
  onset(shortened);
  assert(shortened.step == 7);
  edit(shortened, Kind::PatternLength, 0, 0);
  assert(shortened.patterns[0].length == 1);
  edit(shortened, Kind::PatternLength, 0, 50);
  assert(shortened.patterns[0].length == 16);
  // Complete copy, independence, self-copy, playing destination and clear.
  for (int t = 0; t < 16; ++t)
    e.patterns[2].track_steps[t] = uint16_t(0xa501 + t);
  e.patterns[2].length = 12;
  edit(e, Kind::CopyPattern, 2, 5);
  assert(std::memcmp(&e.patterns[2], &e.patterns[5], sizeof(Pattern)) == 0);
  edit(e, Kind::Step, 5, 2, 7);
  assert(e.patterns[5].track_steps[7] == (e.patterns[2].track_steps[7] ^ 4));
  edit(e, Kind::CopyPattern, 2, 2);
  assert(e.patterns[2].length == 12);
  e.tracks[3].muted = true;
  e.solos = 12;
  edit(e, Kind::ClearPattern, 5, 0);
  assert(e.patterns[5].length == 12 && e.tracks[3].muted && e.solos == 12);
  for (auto m : e.patterns[5].track_steps)
    assert(m == 0);
  assert(e.patterns[2].track_steps[7] == 0xa508);
  // Invalid identifiers and values are rejected without touching musical data.
  auto before = e.patterns[2];
  edit(e, Kind::CopyPattern, 16, 2);
  edit(e, Kind::CopyPattern, 2, 16);
  edit(e, Kind::ClearPattern, 16, 0);
  edit(e, Kind::Step, 2, -1);
  edit(e, Kind::Step, 2, 16);
  edit(e, Kind::Step, 2, 0, 16);
  assert(std::memcmp(&before, &e.patterns[2], sizeof(Pattern)) == 0);
  // Sequencer gates, multiple solos, muted+soloed and unconditional audition.
  Engine mix;
  for (auto &m : mix.patterns[0].track_steps)
    m = 0xffff;
  mix.apply({Kind::Play, 0, 1});
  assert(onset(mix).size() == 16);
  mix.apply({Kind::Mute, 3, 1});
  assert(onset(mix).size() == 15);
  mix.apply({Kind::Solo, 2, 1});
  assert(onset(mix) == std::vector<int>{2});
  mix.apply({Kind::Solo, 4, 1});
  assert((onset(mix) == std::vector<int>{2, 4}));
  mix.apply({Kind::Solo, 3, 1});
  assert((onset(mix) == std::vector<int>{2, 4}));
  for (int t = 0; t < 16; ++t)
    mix.apply({Kind::Mute, uint8_t(t), 1});
  assert(onset(mix).empty());
  int auditions = 0;
  for (int t = 0; t < 16; ++t)
    mix.audition(t, [&](int n) {
      assert(n == t);
      ++auditions;
    });
  assert(auditions == 16);
  mix.audition(16, [&](int) { assert(false); });
  // UI target bounds, hit tests, no overlap (including header/footer).
  std::vector<int> targets;
  for (int id = 46; id <= 67; ++id)
    if (id != 63)
      targets.push_back(id);
  for (int id = 29; id <= 35; ++id)
    targets.push_back(id);
  targets.push_back(44);
  for (int id : targets) {
    auto r = widget(id);
    assert(r.w >= 56 && r.h >= 52 && r.x >= 0 && r.y >= 0 && r.x + r.w <= 800 &&
           r.y + r.h <= 480);
    assert(hit(Page::Pattern, r.x + r.w / 2, r.y + r.h / 2) == id);
    for (int other : targets)
      if (other != id) {
        auto b = widget(other);
        assert(r.x + r.w <= b.x || b.x + b.w <= r.x || r.y + r.h <= b.y ||
               b.y + b.h <= r.y);
      }
  }
  Ui ui;
  Engine editor;
  Command c{};
  assert(!ui.pattern_action(66, editor, c));
  assert(ui.clear_pattern == 0);
  assert(ui.pattern_action(66, editor, c) && c.kind == Kind::ClearPattern &&
         c.pattern == 0);
  assert(!ui.pattern_action(65, editor, c));
  assert(ui.copy_source == 0);
  assert(ui.pattern_action(50, editor, c) && c.kind == Kind::CopyPattern &&
         c.value == 4);
  assert(editor.selected_pattern == 0 && ui.copy_source == -1);
  ui.pattern_action(65, editor, c);
  ui.pattern_action(65, editor, c);
  assert(ui.copy_source == -1);
  ui.pattern_action(66, editor, c);
  ui.pattern_action(67, editor, c);
  assert(ui.clear_pattern == -1);
  assert(ui.pattern_action(48, editor, c) && c.kind == Kind::InspectPattern);
  ui.pattern_action(66, editor, c);
  ui.cancel_pattern_action();
  assert(ui.clear_pattern == -1);
  assert(ui.pattern_action(62, editor, c) && c.kind == Kind::PatternLength &&
         c.value == 15);
  assert(ui.pattern_action(64, editor, c) && c.value == 17);
  editor.apply(c);
  assert(editor.patterns[0].length == 16);
  assert(hit(Page::Track, 50, 190) == 43);
  // A pattern cell/header update cannot dirty the entire framebuffer.
  display::DirtyMask dirty;
  for (int id : {46, 47, 48, 44}) {
    auto r = widget(id);
    dirty.logical_rect(r.x, r.y, r.w, r.h);
  }
  assert(dirty.bytes() < 800 * 480 * 2 / 3);
}
