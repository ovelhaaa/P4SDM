#pragma once
#include "slices.h"
#include <array>
namespace transient {
enum class Sensitivity : uint8_t { Low, Medium, High };
enum class Target : uint8_t { Natural = 0, Four = 4, Eight = 8, Sixteen = 16 };
struct Parameters {
  static constexpr unsigned block = 64, capacity = 64, chunk_blocks = 64;
  static constexpr unsigned fast_shift = 1, slow_shift = 4, baseline_shift = 4;
  static constexpr unsigned energy_weight = 2, attack_weight = 1;
  static constexpr unsigned minimum_ms = 30, refinement = 256;
  // Q8 feature units. Sensitivity changes only threshold multipliers.
  static constexpr unsigned threshold_multipliers[3] = {6, 4, 2};
  static constexpr unsigned floor = 2, relative_floor_shift = 2;
};
struct Candidate { uint32_t frame = 0, strength = 0; };
struct Features { int32_t energy = 0, attack = 0; }; // Q8 mean magnitude/difference
inline Features extract(const int16_t *pcm, uint32_t begin, uint32_t end,
                        unsigned channels, int32_t previous[2]) {
  if (!pcm || begin >= end || end - begin > Parameters::block ||
      (channels != 1 && channels != 2)) return {};
  uint32_t energy = 0, attack = 0;
  for (uint32_t n = begin; n < end; ++n)
    for (unsigned c = 0; c < channels; ++c) {
      const int32_t x = pcm[size_t(n) * channels + c];
      const int32_t delta = x - previous[c];
      energy += x < 0 ? -x : x;
      attack += delta < 0 ? -delta : delta;
      previous[c] = x;
    }
  const uint32_t divisor = (end - begin) * channels;
  return {int32_t(uint64_t(energy) * 256 / divisor), int32_t(uint64_t(attack) * 256 / divisor)};
}
inline bool stronger(Candidate a, Candidate b) {
  return a.strength > b.strength || (a.strength == b.strength && a.frame < b.frame);
}
struct Owner {
  uint32_t generation = 0, revision = 0;
  uint16_t start = 0, end = 65535;
  uint8_t track = 0;
  bool operator==(const Owner &o) const {
    return generation == o.generation && revision == o.revision &&
           start == o.start && end == o.end && track == o.track;
  }
};
struct Proposal {
  sampler::SliceBank bank{};
  Owner owner{};
  bool matches(Owner current) const { return owner == current; }
};
// Small streaming history; scratch size is independent of source duration.
class Detector {
  const int16_t *pcm_;
  uint32_t frames_, cursor_, spacing_;
  unsigned channels_;
  sampler::Playback playback_;
  sampler::Region domain_;
  Sensitivity sensitivity_;
  int32_t fast_ = 0, slow_ = 0, slow_attack_ = 0, baseline_ = 0;
  int32_t previous_[2]{};
  struct Peak { uint32_t frame = 0, novelty = 0, threshold = 0; } left_{}, middle_{};
  bool finished_ = false;
  static int32_t absolute(int32_t v) { return v < 0 ? -v : v; }
  static void smooth(int32_t &state, int32_t value, unsigned shift) {
    state += (value - state) / int32_t(1u << shift);
  }
  uint32_t magnitude(uint32_t frame) const {
    uint32_t sum = 0;
    for (unsigned c = 0; c < channels_; ++c)
      sum += absolute(pcm_[size_t(frame) * channels_ + c]);
    return sum / channels_;
  }
  uint32_t difference(uint32_t frame) const {
    if (frame <= domain_.start) return 0;
    uint32_t sum = 0;
    for (unsigned c = 0; c < channels_; ++c)
      sum += absolute(int32_t(pcm_[size_t(frame) * channels_ + c]) -
                      int32_t(pcm_[size_t(frame - 1) * channels_ + c]));
    return sum / channels_;
  }
  uint32_t refine(uint32_t frame) const {
    const uint32_t begin = frame > domain_.start + Parameters::refinement
                               ? frame - Parameters::refinement : domain_.start;
    const uint32_t end = std::min(domain_.end, frame + Parameters::block);
    uint32_t peak = 0;
    for (uint32_t n = begin; n < end; ++n) peak = std::max(peak, magnitude(n));
    uint32_t quiet = 0;
    const uint32_t quiet_end = std::min(end, begin + Parameters::block);
    for (uint32_t n = begin; n < quiet_end; ++n) quiet += magnitude(n);
    quiet /= std::max<uint32_t>(1u, quiet_end - begin);
    if (peak > quiet * 4) {
      // Locate the earliest rise, avoiding later zero crossings in a kick.
      const uint32_t level = std::max<uint32_t>(1u, peak / 4);
      for (uint32_t n = begin; n < end; ++n)
        if (magnitude(n) >= level) {
          while (n > begin && magnitude(n - 1) > quiet * 2) --n;
          return n;
        }
    }
    uint32_t difference_peak = 0, difference_quiet = 0;
    for (uint32_t i = begin; i < end; ++i) {
      const auto delta = difference(i);
      difference_peak = std::max(difference_peak, delta);
      if (i < quiet_end) difference_quiet += delta;
    }
    difference_quiet /= std::max<uint32_t>(1u, quiet_end - begin);
    if (difference_peak > difference_quiet * 4)
      for (uint32_t i = begin; i < end; ++i)
        if (difference(i) >= std::max<uint32_t>(1u, difference_peak / 4)) return i;
    // For ambiguous already loud material use the low point before the peak.
    uint32_t n = std::min(frame + Parameters::block - 1, domain_.end - 1);
    while (n > begin && magnitude(n) < peak / 4) --n;
    while (n > begin && magnitude(n - 1) >= std::max<uint32_t>(1u, peak / 8)) --n;
    return n;
  }
  void pick(Peak right) {
    if (middle_.novelty > middle_.threshold && middle_.novelty >= left_.novelty &&
        middle_.novelty > right.novelty)
      retain({refine(middle_.frame), middle_.novelty});
    left_ = middle_; middle_ = right;
  }
public:
  std::array<Candidate, Parameters::capacity> candidates{};
  unsigned count = 0, credible = 0;
  uint32_t analyzed = 0;
  Detector(const int16_t *pcm, uint32_t frames, unsigned channels,
           uint32_t rate, sampler::Playback playback, Sensitivity sensitivity)
      : pcm_(pcm), frames_(frames), cursor_(0),
        spacing_(std::max<uint32_t>(1u, uint32_t(uint64_t(rate) * Parameters::minimum_ms / 1000))),
        channels_(channels), playback_(playback),
        domain_(sampler::resolve_region(playback, frames)), sensitivity_(sensitivity) {
    cursor_ = domain_.start;
    if (!pcm || !frames || (channels != 1 && channels != 2)) finished_ = true;
    if (unsigned(sensitivity_) > 2) sensitivity_ = Sensitivity::Medium;
    if (!finished_)
      for (unsigned c = 0; c < channels_; ++c)
        previous_[c] = pcm_[size_t(cursor_) * channels_ + c];
  }
  uint32_t spacing() const { return spacing_; }
  // Shared deterministic strongest/earliest rule for refractory suppression,
  // overflow and final selection. Candidates are kept in chronological order.
  void retain(Candidate c) {
    if (c.frame < domain_.start || c.frame >= domain_.end ||
        c.frame - domain_.start < spacing_ || domain_.end - c.frame < spacing_) return;
    ++credible;
    for (unsigned i = 0; i < count; ++i) {
      const uint32_t distance = c.frame > candidates[i].frame
                                    ? c.frame - candidates[i].frame : candidates[i].frame - c.frame;
      if (distance < spacing_ && !stronger(c, candidates[i])) return;
    }
    for (unsigned i = 0; i < count;) {
      const uint32_t distance = c.frame > candidates[i].frame
                                    ? c.frame - candidates[i].frame : candidates[i].frame - c.frame;
      if (distance < spacing_) candidates[i] = candidates[--count];
      else ++i;
    }
    if (count == Parameters::capacity) {
      unsigned weakest = 0;
      for (unsigned i = 1; i < count; ++i)
        if (stronger(candidates[weakest], candidates[i])) weakest = i;
      if (!stronger(c, candidates[weakest])) return;
      candidates[weakest] = c;
    } else candidates[count++] = c;
    std::sort(candidates.begin(), candidates.begin() + count,
              [](Candidate a, Candidate b) { return a.frame < b.frame; });
  }
  bool process(unsigned blocks = Parameters::chunk_blocks) {
    if (finished_) return true;
    while (blocks-- && cursor_ < domain_.end) {
      const uint32_t begin = cursor_, end = std::min(domain_.end, cursor_ + Parameters::block);
      const auto features = extract(pcm_, begin, end, channels_, previous_);
      cursor_ = end;
      const int32_t e = features.energy, a = features.attack;
      smooth(fast_, e, Parameters::fast_shift);
      const uint32_t novelty = Parameters::energy_weight * uint32_t(std::max<int32_t>(0, fast_ - slow_)) +
                               Parameters::attack_weight * uint32_t(std::max<int32_t>(0, a - slow_attack_));
      const uint32_t threshold = Parameters::floor * 256 +
          (uint32_t(slow_) >> Parameters::relative_floor_shift) +
          uint32_t(baseline_) * Parameters::threshold_multipliers[unsigned(sensitivity_)];
      pick({begin, novelty, threshold});
      smooth(slow_, e, Parameters::slow_shift);
      smooth(slow_attack_, a, Parameters::slow_shift);
      smooth(baseline_, int32_t(novelty), Parameters::baseline_shift);
      analyzed += end - begin;
    }
    if (cursor_ == domain_.end) { pick({domain_.end, 0, 0}); finished_ = true; }
    return finished_;
  }
  sampler::SliceBank select(Target target) const {
    sampler::SliceBank bank; bank.reset(playback_);
    if (!frames_) return bank;
    auto selected = candidates;
    unsigned n = std::min(count, (target == Target::Natural ? 16u : unsigned(target)) - 1);
    std::sort(selected.begin(), selected.begin() + count, stronger);
    std::sort(selected.begin(), selected.begin() + n,
              [](Candidate a, Candidate b) { return a.frame < b.frame; });
    uint16_t boundary = bank.slices[0].start;
    const uint16_t final = bank.slices[0].end;
    unsigned slices = 0;
    for (unsigned i = 0; i < n; ++i) {
      // Ceiling in normalized space maps back to within one quantization unit
      // of the onset. Exact original normalized endpoints are preserved.
      const auto position = uint16_t(std::min<uint64_t>(65534,
          (uint64_t(selected[i].frame) * 65535 + frames_ - 1) / frames_));
      if (position <= boundary || position >= final) continue;
      bank.slices[slices++] = {boundary, position}; boundary = position;
    }
    bank.slices[slices++] = {boundary, final};
    bank.count = uint8_t(slices); bank.selected = 0;
    return bank;
  }
};
} // namespace transient
