#include "../src/app/wav.h"
#include "accepted/wav.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>
static unsigned allocations = 0;
void *operator new(std::size_t n) {
  ++allocations;
  if (void *p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
int main() {
  using namespace sampler;
  static_assert(sizeof(Voice) == sizeof(accepted::Voice));
  for (int16_t a : {int16_t(-32768), int16_t(-32767), int16_t(-1), int16_t(0), int16_t(1), int16_t(32767)})
    for (int16_t b : {int16_t(-32768), int16_t(-32767), int16_t(-1), int16_t(0), int16_t(1), int16_t(32767)})
      for (unsigned phase=0; phase<65536; ++phase)
        assert(linear_fixed32(a,b,uint16_t(phase)) == accepted::linear_fixed(a,b,uint16_t(phase)));
  for (unsigned length : {1u, 2u, 3u, 4u, 31u, 32u, 33u, 65u}) {
    std::vector<int16_t> pcm(length);
    for (unsigned i=0; i<length; ++i) pcm[i]=int16_t((i*7919)%65536-32768);
    for (bool reverse : {false, true})
      for (unsigned mode=0; mode<3; ++mode) {
        PcmReadCache cache;
        // Sweep every phase at both slice ends and cache-line boundaries.
        for (unsigned k=0; k<length; ++k)
          for (unsigned phase=0; phase<65536; ++phase) {
            const uint64_t position=(uint64_t(k)<<16)+phase;
            assert(lookup_valid(pcm.data(), {0,length}, reverse, position,
                                Interpolation(mode), &cache) ==
                   accepted::lookup(pcm.data(), {0,length}, reverse, position,
                                    accepted::Interpolation(mode)));
          }
      }
  }
  std::vector<int16_t> pcm(257), replacement(257);
  for (unsigned i=0; i<257; ++i) {
    pcm[i]=int16_t((i*7919)%65536-32768);
    replacement[i]=int16_t(-int(pcm[i])/2);
  }
  Sample sample; sample.data=pcm.data(); sample.frames=257;
  accepted::Sample reference; reference.data=pcm.data(); reference.frames=257;
  const unsigned before=allocations;
  for (int pitch=0; pitch<128; ++pitch)
    for (bool reverse : {false,true})
      for (unsigned mode=0; mode<3; ++mode) {
        Voice v; accepted::Voice old; PcmReadCache cache;
        Playback p{1000,62000,reverse,Mode::Gate};
        accepted::Playback op{p.start,p.end,reverse,accepted::Mode::Gate};
        v.assign(&sample); old.assign(&reference);
        v.trigger(pitch_increment(pitch),p,true);
        old.trigger(accepted::pitch_increment(pitch),op,true);
        for (unsigned n=0; v.active || old.active; ++n) {
          if (!(n%256)) cache.invalidate();
          if (n==17) { v.release(); old.release(); }
          assert(v.next(Interpolation(mode), &cache)==old.next(accepted::Interpolation(mode)));
          assert(v.position==old.position && v.remaining==old.remaining && v.active==old.active);
          assert(v.attack==old.attack && v.release_left==old.release_left);
        }
      }
  // Replacement at the audio boundary, including reused allocation addresses.
  PcmReadCache cache;
  assert(cache.read(pcm.data(), {0,257}, 3)==pcm[3]);
  assert(cache.read(replacement.data(), {0,257}, 3)==replacement[3]);
  assert(cache.read(pcm.data(), {3,4}, 3)==pcm[3]);
  pcm[3]=1234; cache.invalidate();
  assert(cache.read(pcm.data(), {3,4}, 3)==1234);
  assert(pitch_increment(0)==2048 && pitch_increment(127)==uint64_t(98193)*32);
  assert(allocations==before);
  std::printf("M211 cached PCM all phases/modes, edges, reverse, MIDI, fades, release, replacement, zero allocations PASS; Voice=%zu cache=%zu mode=%u\n",
              sizeof(Voice),sizeof(PcmReadCache),unsigned(default_interpolation));
}
