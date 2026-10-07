#include "../src/app/transients.h"
#include "../src/app/project.h"
#include "../src/app/wav.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
using namespace transient;
static uint32_t rng = 17;
static int noise() { rng = rng * 1664525 + 1013904223; return int(rng >> 16) - 32768; }
static void burst(std::vector<int16_t> &pcm, unsigned frame, int amplitude, unsigned type = 0, unsigned channels = 1, unsigned channel = 0) {
  const unsigned length = type == 0 ? 1 : 500;
  for (unsigned i = 0; i < length && frame + i < pcm.size() / channels; ++i) {
    const double decay = double(length - i) / length;
    int value = amplitude;
    if (type == 1) value = int(amplitude * std::sin(i * 0.08) * decay);
    if (type == 2 || type == 4) value = int(amplitude * double(noise()) / 32768 * decay);
    if (type == 3) value = int(amplitude * (i & 1 ? -1 : 1) * decay);
    if (type == 4) value = int(value * double(i) / length);
    pcm[size_t(frame + i) * channels + channel] = int16_t(value);
  }
}
static Detector analyze(const std::vector<int16_t> &pcm, unsigned channels = 1,
                        sampler::Playback region = {}, Sensitivity sensitivity = Sensitivity::Medium) {
  Detector d(pcm.data(), unsigned(pcm.size() / channels), channels, 44100, region, sensitivity);
  while (!d.process()) {}
  return d;
}
static void quality(const char *name, const std::vector<int16_t> &pcm, const std::vector<unsigned> &expected,
                    unsigned channels = 1, sampler::Playback region = {}) {
  const auto d = analyze(pcm, channels, region);
  unsigned matched = 0;
  for (auto frame : expected) {
    bool found = false;
    for (unsigned i = 0; i < d.count; ++i)
      if (std::abs(int(d.candidates[i].frame) - int(frame)) <= 64) found = true;
    matched += found;
  }
  std::cout << name << " expected=" << expected.size() << " matched=" << matched
            << " missed=" << expected.size() - matched << " false_positive=" << d.count - matched << " tolerance=64\n";
  for (unsigned i = 0; i < d.count; ++i) std::cout << " " << d.candidates[i].frame;
  std::cout << "\n";
  assert(matched == expected.size() && d.count == matched);
  const auto a = d.select(Target::Natural), b = analyze(pcm, channels, region).select(Target::Natural);
  assert(!std::memcmp(&a, &b, sizeof(a)));
  assert(a.count == std::min(16u, matched + 1));
  assert(a.slices[0].start == region.start && a.slices[a.count - 1].end == region.end);
}
int main() {
  std::cout << std::unitbuf;
  int32_t previous[2] = {-32768, 32767};
  const int16_t stereo[] = {32767, -32768, -32768, 32767};
  const auto features = extract(stereo, 0, 2, 2, previous);
  assert(features.energy == 32767 * 256 + 128 && features.attack == 65535 * 256);
  assert(previous[0] == -32768 && previous[1] == 32767);
  assert(extract(nullptr, 0, 0, 0, previous).energy == 0);
  std::vector<int16_t> pcm(44100);
  for (unsigned type = 0; type < 4; ++type) {
    pcm.assign(44100, 0); rng = 17;
    for (auto n : {4000u, 11000u, 21000u, 36000u}) burst(pcm, n, 24000, type);
    const char *names[] = {"impulses", "kicks", "snares", "hats"};
    quality(names[type], pcm, {4000, 11000, 21000, 36000});
  }
  for (auto count : {1u, 3u, 7u, 15u}) {
    pcm.assign(90000, 0);
    for (unsigned i = 0; i < count; ++i) burst(pcm, 3000 + i * 5000, 12000);
    assert(analyze(pcm).select(Target::Natural).count == count + 1);
  }
  pcm.assign(180000, 0);
  for (unsigned i = 0; i < 30; ++i) burst(pcm, 3000 + i * 5000, 4000 + int(i) * 800);
  auto d = analyze(pcm);
  for (auto target : {Target::Four, Target::Eight, Target::Sixteen}) {
    auto bank = d.select(target); assert(bank.count == unsigned(target));
    assert(bank.slices[1].start > 20000); // strongest late events, never first N
    for (unsigned i = 0; i < bank.count; ++i) assert(bank.slices[i].end > bank.slices[i].start);
  }
  // Constant-capacity overflow retains late strong events and earliest ties.
  pcm.assign(600000, 0); d = analyze(pcm);
  for (unsigned i = 0; i < 100; ++i) d.retain({3000 + i * 5000, i + 1});
  assert(d.count == Parameters::capacity && d.candidates[0].frame == 3000 + 36 * 5000);
  assert(d.select(Target::Four).count == 4);
  // Exact 30-ms source-frame refractory rule, including end spacing.
  for (auto ms : {5u, 10u, 20u, 40u, 100u}) {
    pcm.assign(44100, 0); d = analyze(pcm);
    d.retain({4000, 100}); d.retain({4000 + 44100 * ms / 1000, 200});
    assert(d.count == (ms < 30 ? 1u : 2u));
  }
  pcm.assign(44100, 0); d = analyze(pcm);
  d.retain({4000, 100}); d.retain({4000 + d.spacing(), 100}); assert(d.count == 2);
  // Stereo analysis never cancels opposite polarity; differences are reduced
  // per channel before summation, keeping signed HF attack activity.
  pcm.assign(44100 * 2, 0);
  burst(pcm, 4000, 20000, 0, 2, 0); burst(pcm, 11000, 20000, 0, 2, 1);
  burst(pcm, 21000, 20000, 0, 2, 0); burst(pcm, 21000, -20000, 0, 2, 1);
  burst(pcm, 36000, 20000, 0, 2, 0); burst(pcm, 36000, 20000, 0, 2, 1);
  quality("stereo", pcm, {4000, 11000, 21000, 36000}, 2);
  pcm.assign(44100, 0); rng = 17;
  for (auto &x : pcm) x = int16_t(noise() / 128);
  for (auto n : {4000u, 11000u, 21000u, 36000u}) burst(pcm, n, 20000, 2);
  quality("background_noise", pcm, {4000, 11000, 21000, 36000});
  // Limited amplitude contrast, sharp HF change: tests the complementary cue.
  pcm.assign(44100, 0);
  for (unsigned n = 0; n < pcm.size(); ++n) pcm[n] = int16_t(8000 * std::sin(n * 0.02));
  for (auto n : {4000u, 11000u, 21000u, 36000u}) burst(pcm, n, 10000, 3);
  quality("compressed_attack", pcm, {4000, 11000, 21000, 36000});
  const char *musical[] = {"four_on_floor", "syncopated_break", "ghost_hats", "percussion", "vocal_consonants"};
  const std::vector<std::vector<unsigned>> positions = {
      {4000, 14000, 24000, 34000}, {4000, 15000, 21000, 32500},
      {4000, 8500, 15000, 22500, 29000, 36000}, {4000, 11000, 17500, 26000, 36000},
      {4000, 12500, 23500, 36000}};
  for (unsigned fixture = 0; fixture < positions.size(); ++fixture) {
    pcm.assign(44100, 0); rng = 17;
    for (unsigned i = 0; i < positions[fixture].size(); ++i)
      burst(pcm, positions[fixture][i], fixture == 2 && i % 2 ? 1500 : 22000,
            fixture == 0 ? 1 : fixture == 2 ? 3 : 2);
    quality(musical[fixture], pcm, positions[fixture]);
    const auto bank = analyze(pcm).select(Target::Natural);
    sampler::SliceBank equal; equal.divide({}, 4);
    std::cout << musical[fixture] << " differs_from_equal=" << (std::memcmp(&bank, &equal, sizeof(bank)) != 0) << "\n";
  }
  pcm.assign(44100, 0);
  for (unsigned n = 0; n < pcm.size(); ++n) pcm[n] = int16_t(12000 * std::sin(n * 0.07));
  assert(analyze(pcm).count == 0);
  for (unsigned n = 0; n < pcm.size(); ++n) pcm[n] = int16_t(12000 * double(n) / pcm.size() * std::sin(n * 0.07));
  std::cout << "fade_in candidates=" << analyze(pcm).count << "\n";
  assert(analyze(pcm).count <= 1);
  for (unsigned n : {1u, 2u, 31u, 63u, 64u, 100u, 1322u}) {
    pcm.assign(n, 0); const auto bank = analyze(pcm).select(Target::Sixteen);
    assert(bank.count == 1 && bank.slices[0].start == 0 && bank.slices[0].end == 65535);
  }
  pcm.assign(44100, 0); burst(pcm, 2000, 20000); burst(pcm, 11000, 20000); burst(pcm, 40000, 20000);
  sampler::Playback region; region.start = 10000; region.end = 50000;
  quality("trim", pcm, {11000}, 1, region);
  for (unsigned amplitude : {200, 1000, 8000, 30000}) {
    pcm.assign(44100, 0);
    for (auto n : {4000u, 11000u, 21000u, 36000u}) burst(pcm, n, amplitude, 2);
    const auto low = analyze(pcm, 1, {}, Sensitivity::Low).count;
    const auto med = analyze(pcm).count;
    const auto high = analyze(pcm, 1, {}, Sensitivity::High).count;
    assert(low <= med && med <= high);
  }
  pcm.assign(44100, 0); rng = 17;
  burst(pcm, 4000, 24000, 2); burst(pcm, 21000, 24000, 4);
  d = analyze(pcm);
  uint32_t sharp = 0, soft = 0;
  for (unsigned i = 0; i < d.count; ++i) {
    if (d.candidates[i].frame < 10000) sharp = d.candidates[i].strength;
    else soft = d.candidates[i].strength;
  }
  assert(sharp > soft); // ambiguous slow attacks are allowed to be absent
  // Bounded scratch and reproducible timing: allocation belongs to fixtures.
  for (unsigned frames : {44100u, 176400u, 2097152u}) {
    pcm.assign(frames, 0);
    for (unsigned n = 3000; n < frames; n += 5000) burst(pcm, n, 20000, 2);
    const auto start = std::chrono::steady_clock::now(); d = analyze(pcm);
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    assert(d.count <= Parameters::capacity && d.analyzed == frames);
    std::cout << "timing frames=" << frames << " us=" << us << " detector_bytes=" << sizeof(Detector)
              << " candidates_bytes=" << sizeof(d.candidates) << " proposal_bytes=" << sizeof(Proposal) << "\n";
  }
  std::cout << "M19 detector PASS\n";
}
