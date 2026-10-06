#include "../src/app/model.h"
#include "../src/hal/display_dirty.h"
#include <cassert>
int main() {
  app::Engine e;
  e.apply({app::Kind::Bpm, 0, 900});
  assert(e.bpm == 400);
  e.apply({app::Kind::Bpm, 0, 0});
  assert(e.bpm == 30);
  e.apply({app::Kind::Step, 3, 15});
  assert(e.tracks[3].steps == 0x8000);
  e.apply({app::Kind::Step, 3, 15});
  assert(e.tracks[3].steps == 0);
  app::Queue<4> q;
  assert(q.push({app::Kind::Trigger, 1, 0}));
  assert(q.push({app::Kind::Trigger, 2, 0}));
  assert(q.push({app::Kind::Trigger, 3, 0}));
  assert(!q.push({app::Kind::Trigger, 4, 0}));
  app::Command c;
  assert(q.pop(c) && c.track == 1);
  assert(q.push({app::Kind::Trigger, 4, 0}));
  for (int i = 2; i <= 4; ++i)
    assert(q.pop(c) && c.track == i);
  assert(!q.pop(c));
  assert(app::hit(app::Page::Sequence, 16, 100) == 0);
  assert(app::hit(app::Page::Sequence, 104, 100) == -1);
  assert(app::hit(app::Page::Track, 300, 90) == 24);
  assert(app::hit(app::Page::Fx, 30, 120) == 36);
  assert(app::drag(24, -100) == 0 && app::drag(24, 900) == 127);
  assert(app::drag(25, 260) == -127 && app::drag(25, 779) == 127);
  e.apply({app::Kind::Bpm, 0, 120});
  e.apply({app::Kind::Step, 0, 0});
  e.apply({app::Kind::Play, 0, 1});
  int triggers = 0;
  for (int i = 0; i < 88200; ++i)
    e.sample([&](int t) {
      assert(t == 0);
      ++triggers;
    });
  assert(triggers == 1);
  e.sample([&](int) { ++triggers; });
  assert(triggers == 2 && e.step == 0);
  e.apply({app::Kind::Play, 0, 0});
  for (int i = 0; i < 10000; ++i)
    e.sample([&](int) { ++triggers; });
  assert(triggers == 2);
  display::DirtyMask d;
  auto r = app::widget(0);
  d.logical_rect(r.x, r.y, r.w, r.h);
  assert(d.bytes() > 0 && d.bytes() < 800 * 480 * 2 / 10);

  // Verify exact rational step onsets at non-divisor tempos over 32 steps.
  for (int bpm : {30, 97, 123, 400}) {
    app::Engine clock;
    clock.tracks[0].steps = 0xffff;
    clock.apply({app::Kind::Bpm, 0, bpm});
    clock.apply({app::Kind::Play, 0, 1});
    uint64_t sample = 0;
    unsigned onset = 0;
    while (onset < 32) {
      clock.sample([&](int track) {
        assert(track == 0);
        const uint64_t expected =
            (uint64_t(onset) * 44100 * 60 + unsigned(bpm) * 4 - 1) /
            (unsigned(bpm) * 4);
        assert(sample == expected);
        ++onset;
      });
      ++sample;
    }
  }
}
