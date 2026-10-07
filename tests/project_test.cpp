#include "../src/app/project_ui.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>
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
using namespace project;
void equal(const State &a, const State &b) {
  assert(!strcmp(a.name, b.name) && a.bpm == b.bpm && a.swing == b.swing &&
         a.delay == b.delay && a.selected == b.selected);
  for (int t = 0; t < 16; ++t) {
    const auto &x = a.tracks[t], &y = b.tracks[t];
    assert(x.sample == y.sample && x.muted == y.muted && x.volume == y.volume &&
           x.pan == y.pan && x.pitch == y.pitch &&
           x.synth_pitch == y.synth_pitch && x.sample_pitch == y.sample_pitch &&
           x.wave == y.wave && x.length == y.length);
    assert(x.filter_cutoff == y.filter_cutoff &&
           x.filter_resonance == y.filter_resonance &&
           x.delay_send == y.delay_send);
    assert(x.playback.start == y.playback.start &&
           x.playback.end == y.playback.end &&
           x.playback.reverse == y.playback.reverse &&
           x.playback.mode == y.playback.mode &&
           x.playback.choke == y.playback.choke);
    assert(x.slices.count == y.slices.count &&
           x.slices.selected == y.slices.selected &&
           x.slice_enabled == y.slice_enabled);
    for (int i = 0; i < 16; ++i)
      assert(x.slices.slices[i].start == y.slices.slices[i].start &&
             x.slices.slices[i].end == y.slices.slices[i].end);
    assert(!strcmp(a.references[t], b.references[t]));
  }
  for (int p = 0; p < 16; ++p) {
    assert(a.patterns[p].length == b.patterns[p].length);
    for (int t = 0; t < 16; ++t) {
      assert(a.patterns[p].track_steps[t] == b.patterns[p].track_steps[t]);
      for (int i = 0; i < 16; ++i) {
        const auto &m = a.patterns[p].meta[t][i], &n = b.patterns[p].meta[t][i];
        assert(m.velocity == n.velocity && m.probability == n.probability &&
               m.ratchets == n.ratchets);
        const auto &x = a.patterns[p].locks[t][i],
                   &y = b.patterns[p].locks[t][i];
        assert(x.mask == y.mask && x.pitch == y.pitch && x.volume == y.volume &&
               x.pan == y.pan && x.wave == y.wave &&
               x.filter_cutoff == y.filter_cutoff &&
               x.filter_resonance == y.filter_resonance &&
               x.delay_send == y.delay_send && x.slice == y.slice);
      }
    }
  }
}
int main() {
  static State original, decoded, unchanged, snapshot;
  defaults(original);
  strcpy(original.name, "TEST_16");
  original.bpm = 400;
  original.swing = 75;
  original.delay = 0;
  original.selected = 15;
  for (int t = 0; t < 16; ++t) {
    auto &v = original.tracks[t];
    v.sample = t % 2;
    v.muted = t % 3 == 0;
    v.volume = t * 7;
    v.pan = t * 16 - 120;
    v.synth_pitch = t * 7;
    v.sample_pitch = 127 - t * 5;
    v.pitch = v.sample ? v.sample_pitch : v.synth_pitch;
    v.wave = 15 - t;
    v.length = t * 8;
    v.filter_cutoff = t * 8;
    v.filter_resonance = 127 - t * 7;
    v.delay_send = t * 7;
    v.playback = {uint16_t(t * 100), uint16_t(65535 - t * 100),
                  bool(t % 2),       sampler::Mode(t % 2),
                  uint8_t(t % 9),    0};
    v.slices.divide(v.playback, 16);
    v.slices.selected = t;
    v.slice_enabled = t % 2;
    snprintf(original.references[t], 96, "DRUM_%02d.WAV", t);
  }
  for (int p = 0; p < 16; ++p)
    for (int t = 0; t < 16; ++t) {
      original.patterns[p].length = p + 1;
      original.patterns[p].track_steps[t] = uint16_t((p + 1) * 377 + t * 127);
      for (int i = 0; i < 16; ++i) {
        unsigned n = p * 256 + t * 16 + i;
        original.patterns[p].meta[t][i] = {
            uint8_t(1 + n % 127), uint8_t(n % 101), uint8_t(1 + n % 4)};
        original.patterns[p].locks[t][i] = {uint8_t(n),
                                            uint8_t(n % 128),
                                            uint8_t((n * 3) % 128),
                                            int8_t(int(n % 255) - 127),
                                            uint8_t(n % 16),
                                            uint8_t(n % 128),
                                            uint8_t((n * 7) % 128),
                                            uint8_t((n * 11) % 128),
                                            uint8_t((n * 13) % 16)};
      }
    }
  std::vector<uint8_t> bytes(file_bytes), other(file_bytes);
  assert(encode(original, 0xffffffff, bytes.data(), bytes.size()));
  assert(decode(bytes.data(), bytes.size(), decoded) == Error::Ok);
  equal(original, decoded);
  assert(encode(decoded, 0xffffffff, other.data(), other.size()) &&
         bytes == other);
  assert(crc32(reinterpret_cast<const uint8_t *>("123456789"), 9) ==
         0xcbf43926);
  assert(header_bytes == 24 && payload_bytes == 52613 && file_bytes == 52637);
  defaults(snapshot);
  assert(encode(snapshot, 1, other.data(), other.size()));
  // Golden defaults: fixed byte positions and stable complete payload CRC.
  assert(other[4] == 1 && other[6] == 24 &&
         u32(other.data() + 8) == payload_bytes &&
         u16(other.data() + 24 + 32) == 120);
  assert(u32(other.data() + 16) == 170069039u);
  std::cout << "default_crc=" << u32(other.data() + 16)
            << " state=" << sizeof(State) << " file=" << file_bytes << '\n';
  unchanged = decoded;
  for (unsigned n = 0; n < file_bytes; ++n) {
    assert(decode(bytes.data(), n, decoded) != Error::Ok);
  }
  equal(decoded, unchanged);
  auto corrupt = [&](unsigned pos, uint8_t value, Error expected,
                     bool repair = true) {
    other = bytes;
    other[pos] = value;
    if (repair)
      put32(other.data() + 16,
            crc32(other.data() + header_bytes, payload_bytes));
    auto actual = decode(other.data(), other.size(), decoded);
    if (actual != expected)
      std::cerr << "corrupt pos=" << pos << " got=" << int(actual)
                << " expected=" << int(expected) << std::endl;
    assert(actual == expected);
    equal(decoded, unchanged);
  };
  corrupt(0, 'X', Error::Header, false);
  corrupt(4, 0, Error::Version, false);
  corrupt(4, 2, Error::Future, false);
  corrupt(8, 0, Error::Size, false);
  corrupt(16, 0, Error::Crc, false);
  corrupt(20, 1, Error::Header, false);
  corrupt(24 + 33, 2, Error::Semantic);
  corrupt(24 + 34, 76, Error::Semantic);
  corrupt(24 + 35, 2, Error::Semantic);
  corrupt(24 + 36, 16, Error::Semantic);
  constexpr unsigned track = 24 + 37;
  for (auto field : {0u, 1u, 15u})
    corrupt(track + field, 2, Error::Semantic);
  for (auto field : {2u, 4u, 5u, 6u, 8u, 9u, 10u})
    corrupt(track + field, 128, Error::Semantic);
  corrupt(track + 3, 255, Error::Semantic);
  corrupt(track + 7, 16, Error::Semantic);
  corrupt(track + 16, 2, Error::Semantic);
  corrupt(track + 17, 9, Error::Semantic);
  corrupt(track + 18, 0, Error::Semantic);
  corrupt(track + 18, 17, Error::Semantic);
  corrupt(track + 19, 16, Error::Semantic);
  corrupt(track + 20, 2, Error::Semantic);
  corrupt(track + 21 + 64, '/', Error::Semantic);
  other = bytes;
  memset(other.data() + track + 21 + 64, 'X', 96);
  put32(other.data() + 16, crc32(other.data() + 24, payload_bytes));
  assert(decode(other.data(), other.size(), decoded) == Error::Semantic);
  constexpr unsigned pattern = 24 + 37 + 16 * track_bytes;
  corrupt(pattern, 0, Error::Semantic);
  corrupt(pattern, 17, Error::Semantic);
  corrupt(pattern + 33, 0, Error::Semantic);
  corrupt(pattern + 34, 101, Error::Semantic);
  corrupt(pattern + 35, 5, Error::Semantic);
  for (auto offset : {1u, 2u, 5u, 6u, 7u})
    corrupt(pattern + 36 + offset, 128, Error::Semantic);
  corrupt(pattern + 36 + 3, 255, Error::Semantic);
  corrupt(pattern + 36 + 4, 16, Error::Semantic);
  corrupt(pattern + 36 + 8, 16, Error::Semantic);
  other = bytes;
  other.push_back(0);
  assert(decode(other.data(), other.size(), decoded) == Error::Size);
  assert(!name_valid("../BAD") && !name_valid("BAD/NAME") &&
         !reference_valid("../kick.wav") && !reference_valid("C:kick.wav"));
  // Real byte-slot recovery: every interrupted write length preserves old slot.
  std::vector<uint8_t> good(file_bytes), fresh(file_bytes);
  assert(encode(original, 0xffffffff, good.data(), good.size()));
  assert(encode(original, 0, fresh.data(), fresh.size()));
  Slot a{inspect(good.data(), good.size(), decoded) == Error::Ok, 0xffffffff};
  for (unsigned n = 0; n <= file_bytes; ++n) {
    Slot b{inspect(fresh.data(), n, decoded) == Error::Ok, 0};
    assert(newest(a, b) == (n == file_bytes ? 1 : 0));
  }
  for (unsigned offset : {0u, 24u, file_bytes / 2, file_bytes - 1}) {
    other = fresh;
    other[offset] ^= 0x80;
    assert(newest({true, 0xffffffff},
                  {inspect(other.data(), other.size(), decoded) == Error::Ok,
                   0}) == 0);
  }
  other = fresh;
  memset(other.data() + file_bytes / 2, 0, file_bytes - file_bytes / 2);
  assert(inspect(other.data(), other.size(), decoded) != Error::Ok);
  assert(newest({}, {}) == -1 && newest({}, {true, 7}) == 1 &&
         newest({true, 7}, {true, 7}) == 0);
  assert(newer(0, 0xffffffff) && !newer(0xffffffff, 0) && !newer(7, 7));
  assert(plan({}, {}).slot == 0 && plan({}, {}).generation == 1);
  assert(plan(a, {}).slot == 1 && plan(a, {}).generation == 0);
  assert(plan({}, {true, 7}).slot == 0 && plan({}, {true, 7}).generation == 8);
  // The production save plan is used for partial-write/verification failures.
  for (unsigned cut :
       {0u, 12u, 24u, 25u, file_bytes / 2, file_bytes - 1, file_bytes}) {
    std::vector<uint8_t> disk[2] = {good, {}};
    auto next = plan(a, {});
    disk[next.slot].assign(fresh.begin(), fresh.begin() + cut);
    Slot inspected[2];
    for (unsigned slot = 0; slot < 2; ++slot)
      inspected[slot] = {
          inspect(disk[slot].data(), disk[slot].size(), decoded) == Error::Ok,
          slot ? 0u : 0xffffffffu};
    assert(disk[0] == good &&
           newest(inspected[0], inspected[1]) == (cut == file_bytes ? 1 : 0));
  }
  // Normalized crossed playback coordinates are legal M13 intent; malformed
  // active and inactive Slice regions are structural errors.
  other = bytes;
  put16(other.data() + track + 21, 60000);
  put16(other.data() + track + 23, 100);
  put32(other.data() + 16, crc32(other.data() + header_bytes, payload_bytes));
  assert(decode(other.data(), other.size(), decoded) == Error::Semantic);
  decoded = unchanged;
  static app::Engine engine;
  engine.playing = true;
  engine.solos = 0xffff;
  engine.queued_pattern = 5;
  engine.rng = 7;
  engine.edit_seed = 1;
  for (unsigned i = 0; i <= 16; ++i)
    apply_part(engine, original, i);
  assert(!engine.playing && engine.solos == 0 && engine.queued_pattern == -1 &&
         engine.selected_pattern == 15 && engine.playing_pattern == 15 &&
         engine.rng == app::Engine::default_seed &&
         engine.edit_seed == 0x4d395031);
  memcpy(snapshot.name, original.name, 32);
  memcpy(snapshot.references, original.references, sizeof(original.references));
  realtime = true;
  for (int run = 0; run < 1000; ++run) {
    for (unsigned part = 0; part <= 16; ++part)
      snapshot_part(engine, snapshot, part);
    for (unsigned part = 0; part <= 16; ++part)
      apply_part(engine, snapshot, part);
  }
  realtime = false;
  equal(snapshot, original);
  Workflow ui;
  assert(!ui.confirm(projects::Operation::Load, "TEST_16", true));
  assert(ui.mode == Workflow::Mode::Confirm && !strcmp(ui.target, "TEST_16"));
  ui.cancel();
  assert(ui.pending == projects::Operation::None);
  ui.generated_name();
  assert(!strcmp(ui.target, "PROJECT_0001"));
  for (int id = 154; id <= 160; ++id) {
    auto r = app::widget(id);
    assert(r.h >= 48 &&
           app::hit(app::Page::Project, r.x + r.w / 2, r.y + r.h / 2) == id);
  }
  std::cout << "Project V1 roundtrip, CRC-correct semantic rejection, all "
               "truncations, dual-slot recovery, runtime reset, UI and "
               "no-allocation boundaries PASS\n";
}
