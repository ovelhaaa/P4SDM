#include "../src/app/project.h"
#include "m16_project.h"
#include <cassert>
#include <iostream>
#include <vector>
int main() {
  static legacy::State old;
  legacy::defaults(old);
  std::vector<uint8_t> golden(legacy::file_bytes);
  assert(legacy::encode(old, 1, golden.data(), golden.size()));
  assert(legacy::u32(golden.data() + 16) == 170069039u);
  old.bpm = 397;
  old.swing = 74;
  old.delay = 0;
  old.selected = 15;
  strcpy(old.name, "MIGRATION_17");
  // Reuse the accepted M16 fixture's full field variation below.
  for (int t = 0; t < 16; ++t) {
    auto &v = old.tracks[t];
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
    v.playback = {uint16_t(t * 100), uint16_t(65535 - t * 100), bool(t % 2),
                  sampler::Mode(t % 2), uint8_t(t % 9)};
    v.slices.divide(v.playback, 16);
    v.slices.selected = t;
    v.slice_enabled = t % 2;
    snprintf(old.references[t], 96, "SAMPLE_%02d.WAV", t);
  }
  for (int p = 0; p < 16; ++p) {
    auto &pat = old.patterns[p];
    pat.length = p + 1;
    for (int t = 0; t < 16; ++t)
      for (int j = 0; j < 16; ++j) {
        pat.track_steps[t] = uint16_t(0x9365 + p * 711 + t * 251);
        pat.meta[t][j] = {uint8_t(1 + (p + t + j) % 127),
                          uint8_t((p + t * 3 + j) % 101), uint8_t(1 + j % 4)};
        pat.locks[t][j] = {
            uint8_t(t * 16 + j),  uint8_t(j * 7), uint8_t(t * 7),
            int8_t(j * 13 - 100), uint8_t(p),     uint8_t(j * 8),
            uint8_t(t * 8),       uint8_t(p * 8), uint8_t(15 - j)};
      }
  }
  std::vector<uint8_t> v1(legacy::file_bytes), v2(project::file_bytes);
  assert(legacy::encode(old, 10, v1.data(), v1.size()));
  auto preserved = v1;
  static project::State state;
  assert(project::decode(v1.data(), v1.size(), state) == project::Error::Ok);
  for (unsigned n = 0; n < v1.size(); ++n)
    assert(project::decode(v1.data(), n, state) != project::Error::Ok);
  auto trailing = v1;
  trailing.push_back(0);
  assert(project::inspect(trailing.data(), trailing.size(), state) ==
         project::Error::Size);
  assert(!state.chain.length && !state.chain.loop &&
         state.mode == app::TransportMode::Pattern);
  assert(project::encode(state, 11, v2.data(), v2.size()));
  // Every encoded M16 field survives migration bit-for-bit, including inactive
  // data.
  assert(!memcmp(v1.data() + 24, v2.data() + 24, legacy::payload_bytes));
  assert(v1 == preserved && project::u16(v2.data() + 4) == 2);
  auto inspect = [&](const std::vector<uint8_t> &bytes) {
    auto error = project::inspect(bytes.data(), bytes.size(), state);
    return project::Slot{error == project::Error::Ok,
                         bytes.size() >= 24 ? project::u32(bytes.data() + 12)
                                            : 0};
  };
  assert(project::newest(inspect(v1), inspect(v2)) == 1);
  assert(project::newest(inspect(v2), inspect(v1)) == 0);
  auto second_v1 = v1;
  project::put32(second_v1.data() + 12, 11);
  assert(project::newest(inspect(v1), inspect(second_v1)) == 1);
  auto plan = project::plan(inspect(v1), {});
  assert(plan.slot == 1 && plan.generation == 11);
  auto bad = v2;
  bad.back() ^= 1;
  assert(project::newest(inspect(v1), inspect(bad)) == 0);
  bad = v1;
  bad.back() ^= 1;
  assert(project::newest(inspect(v2), inspect(bad)) == 0);
  for (unsigned cut :
       {0u, 12u, 24u, 25u, unsigned(v2.size() / 2), unsigned(v2.size() - 1)}) {
    auto truncated = v2;
    truncated.resize(cut);
    assert(project::newest(inspect(v1), inspect(truncated)) == 0);
  }
  auto future = v2;
  project::put16(future.data() + 4, 3);
  future.resize(24);
  assert(project::inspect(future.data(), future.size(), state) ==
         project::Error::Future);
  auto wrong = v1;
  project::put32(wrong.data() + 8, project::payload_bytes);
  assert(project::inspect(wrong.data(), wrong.size(), state) ==
         project::Error::Size);
  assert(project::decode(v2.data(), v2.size(), state) == project::Error::Ok);
  assert(project::encode(state, 11, bad.data(), bad.size()) ==
         false); // old-size destination
  std::cout
      << "Actual M16 V1 bytes -> V2 migration, exact old-field payload, mixed "
         "slots, corrupt fallback, future prefix and safe save plan: PASS\n";
}
