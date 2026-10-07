#include "../src/app/model.h"
#include "../src/app/wav.h"
#include "../src/app/qualification.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>
using Bytes = std::vector<uint8_t>;
void le(Bytes &b, uint32_t n, unsigned count = 4) {
  for (unsigned i = 0; i < count; ++i)
    b.push_back(uint8_t(n >> (8 * i)));
}
void tag(Bytes &b, const char *s) { b.insert(b.end(), s, s + 4); }
void chunk(Bytes &b, const char *id, Bytes data) {
  tag(b, id);
  le(b, data.size());
  b.insert(b.end(), data.begin(), data.end());
  if (data.size() & 1)
    b.push_back(0);
}
Bytes fixture(unsigned channels = 1, unsigned bits = 16, unsigned encoding = 1,
              bool reverse = false) {
  Bytes b;
  tag(b, "RIFF");
  le(b, 0);
  tag(b, "WAVE");
  Bytes f;
  le(f, encoding, 2);
  le(f, channels, 2);
  le(f, 44100);
  le(f, 44100 * channels * 2);
  le(f, channels * 2, 2);
  le(f, bits, 2);
  chunk(b, "JUNK", {1, 2, 3});
  chunk(b, "LIST", {4, 5});
  chunk(b, "PAD ", {7});
  if (reverse)
    chunk(b, "data", Bytes(channels * 4, 1));
  chunk(b, "fmt ", f);
  if (!reverse)
    chunk(b, "data", Bytes(channels * 4, 1));
  uint32_t n = b.size() - 8;
  for (int i = 0; i < 4; ++i)
    b[4 + i] = uint8_t(n >> (i * 8));
  return b;
}
const char *parse(Bytes b, sampler::Wav &w) {
  return sampler::parse(
      b.size(),
      [&](uint64_t p, uint8_t *d, unsigned n) {
        if (p > b.size() || n > b.size() - p)
          return false;
        memcpy(d, b.data() + p, n);
        return true;
      },
      w);
}
int main(int argc, char **argv) {
  assert(!strcmp(sampler::qualification_fixture(0), sampler::qualification_fixture(3)));
  assert(!strcmp(sampler::qualification_fixture(4), "unsupported_depth.wav"));
  assert(!strcmp(sampler::qualification_fixture(5), "truncated.wav"));
  assert(!sampler::qualification_fixture(6));
  assert(sampler::qualification_pitch(1) == 60);
  assert(sampler::qualification_pitch(2) == 48);
  assert(sampler::qualification_pitch(3) == 72);
  assert(sampler::qualification_pitch(5) == sampler::qualification_pitch(6));
  assert(!sampler::memory_loss_suspect(10000, 5904, 20000, 15904));
  assert(sampler::memory_loss_suspect(10000, 5903, 20000, 20000));
  assert(sampler::memory_loss_suspect(10000, 10000, 20000, 15903));
  app::Engine model;
  auto &track = model.tracks[0];
  assert(track.pitch == 36 && !track.sample_configured);
  track.assigned();
  assert(track.pitch == 60 && sampler::pitch_increment(track.pitch) == 65536);
  model.apply({app::Kind::Pitch, 0, 48});
  assert(track.sample_pitch == 48 && sampler::pitch_increment(track.pitch) == 32768);
  track.assigned(); // replacement preserves intentional tune
  assert(track.pitch == 48);
  model.apply({app::Kind::Source, 0, 0});
  assert(track.pitch == 36);
  model.apply({app::Kind::Source, 0, 1});
  assert(track.pitch == 48);
  for (int id = 24; id < 29; ++id)
    assert(app::track_control_enabled(track, id) == (id < 27));
  model.apply({app::Kind::Source, 0, 0});
  for (int id = 24; id < 29; ++id)
    assert(app::track_control_enabled(track, id));
  track.assigned();
  assert(track.pitch == 48);
  assert(sampler::pitch_increment(60) == 65536);
  assert(sampler::pitch_increment(48) == 32768);
  assert(sampler::pitch_increment(72) == 131072);
  assert(sampler::pitch_increment(0) == 2048);
  assert(sampler::pitch_increment(127) == 3142176);
  assert(sampler::pitch_increment(-1) == sampler::pitch_increment(0));
  assert(sampler::pitch_increment(128) == sampler::pitch_increment(127));
  for (int pitch = 1; pitch < 128; ++pitch)
    assert(sampler::pitch_increment(pitch) > sampler::pitch_increment(pitch - 1));
  app::Track first;
  first.source(true);
  first.pitch = first.sample_pitch = 72;
  first.assigned();
  assert(first.pitch == 60); // no successful assignment before this
  app::Engine publication;
  publication.tracks[0].assigned();
  // Synth edit queued before publication must not detune newly resident PCM.
  publication.apply({app::Kind::Pitch, 0, 40, app::Command::PitchSource::Synth});
  assert(publication.tracks[0].pitch == 60 && publication.tracks[0].synth_pitch == 40);
  publication.apply({app::Kind::Source, 0, 0});
  publication.apply({app::Kind::Pitch, 0, 48, app::Command::PitchSource::Sample});
  assert(publication.tracks[0].pitch == 40 && publication.tracks[0].sample_pitch == 48);
  sampler::Wav w;
  for (unsigned c : {1u, 2u})
    for (bool order : {false, true}) {
      auto b = fixture(c, 16, 1, order);
      assert(!parse(b, w));
      assert(w.frames == 2 && w.channels == c);
      for (size_t n = 0; n < b.size(); ++n)
        assert(parse(Bytes(b.begin(), b.begin() + n), w));
    }
  assert(parse(fixture(1, 24), w));
  assert(parse(fixture(1, 16, 3), w));
  assert(parse(fixture(3), w));
  auto b = fixture();
  b[16] = 255;
  b[17] = 255;
  b[18] = 255;
  b[19] = 255;
  assert(parse(b, w));
  b = fixture();
  b.resize(b.size() - 4);
  uint32_t n = b.size() - 8;
  for (int i = 0; i < 4; ++i)
    b[4 + i] = uint8_t(n >> (8 * i));
  b[b.size() - 4] = 0;
  b[b.size() - 3] = 0;
  b[b.size() - 2] = 0;
  b[b.size() - 1] = 0;
  assert(parse(b, w));
  int16_t pcm[] = {10, 20, 30};
  sampler::Sample a;
  a.data = pcm;
  a.frames = 3;
  sampler::Voice v;
  v.assign(&a);
  v.trigger(sampler::pitch_increment(60), {}, false, 0);
  assert(v.next() == 10 && v.next() == 20 && v.next() == 30);
  assert(!v.active && v.next() == 0);
  for (uint64_t step : {32768ull, 65536ull, 131072ull, 999999999ull}) {
    v.trigger(step, {}, false, 0);
    assert(v.next() == 10);
    for (int i = 0; i < 10; ++i)
      v.next();
    assert(!v.active);
    assert(v.next() == 0);
  }
  v.trigger(65536, {}, false, 0);
  v.next();
  v.trigger(65536, {}, false, 0);
  assert(v.next() == 10);
  v.stop(); // Source switching uses this same voice operation.
  assert(v.next() == 0 && !v.active && v.position == 0);
  sampler::Sample short_sample;
  short_sample.data = pcm;
  short_sample.frames = 1;
  v.assign(&short_sample);
  v.trigger(65536, {}, false, 0);
  assert(v.next() == 10 && !v.active);
  assert(v.next() == 0);
  sampler::Voice voices[16];
  sampler::Transfer transfer;
  transfer.publish(&a, 5);
  assert(transfer.consume(voices) == 5);
  assert(transfer.acknowledged.load());
  assert(voices[5].sample == &a);
  voices[5].trigger(65536);
  transfer.publish(&short_sample, 5);
  assert(transfer.consume(voices) == 5);
  assert(transfer.retired.load() == &a);
  assert(!voices[5].active);
  assert(voices[5].next() == 0);
  // Explicit malformed metadata and reader failures.
  b = fixture();
  b[60] = 0;
  b[61] = 0;
  assert(parse(b, w)); // byte rate in fmt after padded JUNK/LIST/PAD
  b = fixture();
  b[56] = 0x80;
  b[57] = 0xbb;
  b[58] = 0;
  b[59] = 0;
  assert(parse(b, w)); // 48000 Hz
  b = fixture();
  b[72] = 3;
  assert(parse(b, w)); // partial PCM frame
  b = fixture();
  chunk(b, "data", {1, 2});
  uint32_t duplicate_size = b.size() - 8;
  for (int i = 0; i < 4; ++i)
    b[4 + i] = uint8_t(duplicate_size >> (8 * i));
  assert(parse(b, w));
  b = fixture();
  b[44] = 'J';
  b[45] = 'U';
  b[46] = 'N';
  b[47] = 'K';
  assert(parse(b, w)); // missing fmt
  Bytes empty;
  tag(empty, "RIFF");
  le(empty, 28);
  tag(empty, "WAVE");
  Bytes fmt;
  le(fmt, 1, 2);
  le(fmt, 1, 2);
  le(fmt, 44100);
  le(fmt, 88200);
  le(fmt, 2, 2);
  le(fmt, 16, 2);
  chunk(empty, "fmt ", fmt);
  chunk(empty, "data", {});
  uint32_t size = empty.size() - 8;
  for (int i = 0; i < 4; ++i)
    empty[4 + i] = uint8_t(size >> (8 * i));
  assert(parse(empty, w));
  auto valid = fixture();
  assert(sampler::parse(
      valid.size(), [](uint64_t, uint8_t *, unsigned) { return false; }, w));
  uint32_t rng = 12345;
  for (unsigned iteration = 0; iteration < 10000; ++iteration) {
    Bytes fuzz = valid;
    rng = rng * 1664525u + 1013904223u;
    fuzz[rng % fuzz.size()] ^= uint8_t(rng >> 24);
    sampler::parse(
        fuzz.size(),
        [&](uint64_t pos, uint8_t *d, unsigned n) {
          assert(pos <= fuzz.size() && n <= fuzz.size() - pos);
          memcpy(d, fuzz.data() + pos, n);
          return true;
        },
        w);
  }
  // Real producer/consumer replacement: retired memory is poisoned and freed
  // only after the audio consumer has stopped referencing it.
  sampler::Transfer concurrent;
  sampler::Voice realtime[16];
  std::atomic<bool> done{false};
  std::thread audio([&] {
    while (!done.load()) {
      int t = concurrent.consume(realtime);
      if (t >= 0)
        realtime[t].trigger(65536);
      for (auto &voice : realtime)
        voice.next();
    }
  });
  for (unsigned iteration = 0; iteration < 10000; ++iteration) {
    auto *next = new sampler::Sample;
    next->data = new int16_t[3]{10, 20, 30};
    next->frames = 3;
    concurrent.publish(next, iteration % 16);
    while (!concurrent.acknowledged.load(std::memory_order_acquire))
      std::this_thread::yield();
    auto *old = concurrent.retired.exchange(nullptr);
    if (old) {
      old->data[0] = -123;
      delete[] old->data;
      delete old;
    }
  }
  done.store(true);
  audio.join();
  for (auto *active : concurrent.active) {
    delete[] active->data;
    delete active;
  }
  assert(app::hit(app::Page::Track, 30, 270) == 42);
  for (int i = 38; i < 42; ++i) {
    auto r = app::widget(i);
    assert(app::hit(app::Page::Sample, r.x + 1, r.y + 1) == i);
  }
  assert(sampler::fits_budget(1024, 4 * 1024 * 1024 + 1024, 1024));
  assert(!sampler::fits_budget(1024, 4 * 1024 * 1024 + 1023, 1024));
  assert(!sampler::fits_budget(1024, 8 * 1024 * 1024, 1023));
  assert(!sampler::fits_budget(4 * 1024 * 1024 + 1, 32 * 1024 * 1024,
                               32 * 1024 * 1024));
  uint8_t stereo_pcm[] = {0xff, 0x7f, 0, 0x80};
  assert(sampler::decode_mono(stereo_pcm, 2) == 0);
  if (argc == 2) {
    unsigned accepted = 0, rejected = 0;
    for (auto &item : std::filesystem::directory_iterator(argv[1])) {
      std::ifstream stream(item.path(), std::ios::binary);
      Bytes bytes((std::istreambuf_iterator<char>(stream)), {});
      const char *error = parse(bytes, w);
      bool invalid = item.path().filename().string().find("unsupported") == 0 ||
                     item.path().stem() == "truncated";
      assert(bool(error) == invalid);
      if (error)
        ++rejected;
      else
        ++accepted;
    }
    assert(accepted == 5 && rejected == 3);
  }
  std::cout << "WAV bounds, padding/order, format, playback and transfer tests "
               "passed\n";
}
