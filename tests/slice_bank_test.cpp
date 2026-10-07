#include "../src/app/model.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace sampler;
int main() {
  SliceBank b;
  assert(b.count == 1 && b.selected == 0 && b.slices[0].end == 65535);
  for (Playback domain : {Playback{}, Playback{6553, 58981}})
    for (unsigned n : {2u, 4u, 8u, 16u}) {
      assert(b.divide(domain, n) && b.count == n);
      for (unsigned i = 0; i < n; ++i) {
        assert(b.slices[i].start ==
               domain.start + uint32_t(domain.end - domain.start) * i / n);
        assert(b.slices[i].end ==
               domain.start +
                   uint32_t(domain.end - domain.start) * (i + 1) / n);
        assert(b.slices[i].end > b.slices[i].start);
        if (i)
          assert(b.slices[i - 1].end == b.slices[i].start);
      }
      assert(b.slices[0].start == domain.start &&
             b.slices[n - 1].end == domain.end);
    }
  auto before = b;
  assert(!b.divide({100, 101}, 2) && !b.divide({300, 100}, 4) &&
         !b.divide({}, 3));
  assert(!std::memcmp(&before, &b, sizeof(b)));
  b.reset({100, 900});
  assert(b.count == 1 && b.selected == 0 && b.slices[0].start == 100);
  assert(!b.erase(0));
  assert(b.add(0));
  assert(b.slices[0].end == 500 && b.slices[1].start == 500 &&
         b.slices[1].end == 900);
  b.selected = 1;
  assert(b.add(0) && b.selected == 2);
  assert(b.erase(0) && b.selected == 1 && b.slices[1].start == 500);
  b.selected = 255;
  assert(b.erase(0) && b.selected == 0 && b.count == 1);
  b.reset({});
  while (b.count < 16)
    assert(b.add(b.count - 1));
  assert(!b.add(0));
  b.slices[0] = {50000, 60000};
  b.slices[1] = {100, 200};
  b.slices[2] = {150, 10000}; // unordered, overlap, gaps intentionally retained
  b.edit(1, true, 110);
  assert(b.slices[0].end == 60000 && b.slices[2].start == 150);
  b.edit(1, false, 0);
  assert(b.slices[1].end == 111);
  b.edit(1, true, 65535);
  assert(b.slices[1].start == 110);
  assert(b.index(99) == 15);
  b.count = 255;
  assert(b.index(99) == 15);
  b.count = 0;
  assert(b.index(99) == 0);
  b.reset({65535, 0});
  assert(b.slices[0].start == 65534 && b.slices[0].end == 65535);

  app::Engine e;
  auto &t = e.tracks[0];
  t.assigned();
  assert(t.slices.count == 1 && t.slices.slices[0].start == 0);
  t.playback = {100, 60000};
  t.slices.reset(t.playback);
  e.apply({app::Kind::SliceDivide, 0, 8});
  e.apply({app::Kind::SliceSelect, 0, 7});
  e.apply({app::Kind::SliceEnable, 0, 1});
  auto bank = t.slices;
  t.assigned();
  assert(!std::memcmp(&bank, &t.slices, sizeof(bank)) && t.slice_enabled);
  for (auto kind :
       {app::Kind::CopyPattern, app::Kind::Rotate, app::Kind::Reverse,
        app::Kind::CopyTrack, app::Kind::ClearTrack, app::Kind::Randomize,
        app::Kind::SelectPattern, app::Kind::Duplicate})
    e.apply({kind, 0, 1});
  assert(!std::memcmp(&bank, &t.slices, sizeof(bank)));
  t.playback.start = 30000;
  t.playback.end = 31000;
  assert(!std::memcmp(&bank, &t.slices, sizeof(bank)));
  t.playback.reverse = true;
  t.playback.choke = 4;
  t.playback.mode = Mode::Gate;
  e.apply({app::Kind::SliceReset, 0, 0});
  assert(t.slice_enabled && t.slices.count == 1 &&
         t.slices.slices[0].start == 30000);
  assert(t.playback.reverse && t.playback.choke == 4 &&
         t.playback.mode == Mode::Gate);
  for (int id = 133; id <= 146; ++id) {
    auto r = app::widget(id);
    assert(r.w >= 48 && r.h >= 48 && r.x >= 0 && r.y + r.h <= 480);
    assert(app::hit(app::Page::SampleSlice, r.x, r.y) == id);
    assert(app::hit(app::Page::SampleSlice, r.x + r.w - 1, r.y + r.h - 1) ==
           id);
    for (int j = id + 1; j <= 146; ++j) {
      auto q = app::widget(j);
      assert(r.x + r.w <= q.x || q.x + q.w <= r.x || r.y + r.h <= q.y ||
             q.y + q.h <= r.y);
    }
  }
  auto r = app::widget(131);
  assert(app::hit(app::Page::SamplePlayback, r.x, r.y) == 131);
  std::printf("Slice=%zu SliceBank=%zu Track=%zu Engine=%zu Sample-independent "
              "StepLocks=%zu\n",
              sizeof(Slice), sizeof(SliceBank), sizeof(app::Track), sizeof(e),
              sizeof(app::StepLocks));
}
