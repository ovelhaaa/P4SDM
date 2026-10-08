#include "../src/app/wav.h"
#include "accepted/wav.h"
#include <cassert>
#include <cmath>
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
using namespace sampler;
static int16_t round_pcm(double value) {
  return int16_t(value >= 32767 ? 32767 : value <= -32768 ? -32768 : std::round(value));
}
static double reference(const int16_t *pcm, Region r, bool reverse,
                        uint64_t position, Interpolation mode) {
  const int64_t k = int64_t(position >> 16), length = r.end-r.start;
  auto tap = [&](int64_t n) -> double {
    n = n < 0 ? 0 : n >= length ? length-1 : n;
    return pcm[reverse ? r.end-1-n : r.start+n];
  };
  const double t = double(position & 65535)/65536;
  const double x0 = tap(k), x1 = tap(k+1);
  if (mode == Interpolation::Nearest) return x0;
  if (mode == Interpolation::Linear) return (1-t)*x0+t*x1;
  // Independent Hermite basis, rather than the implementation's Horner form.
  const double m0 = (x1-tap(k-1))/2, m1 = (tap(k+2)-x0)/2;
  return (2*t*t*t-3*t*t+1)*x0 + (t*t*t-2*t*t+t)*m0 +
      (-2*t*t*t+3*t*t)*x1 + (t*t*t-t*t)*m1;
}
int main() {
  int16_t edges[] = {-12345, 32767, -32768, 12345, -23456, 30000};
  for (unsigned length = 1; length <= 4; ++length) {
    // Exact-size allocation enables ASan to reject even a zero-weight tap
    // outside the region (canary output checks alone cannot detect that).
    std::vector<int16_t> guarded(edges+1,edges+1+length);
    for (bool reverse : {false,true})
      for (unsigned mode = 0; mode < 3; ++mode)
        for (unsigned k = 0; k < length; ++k)
          for (unsigned phase = 0; phase < 65536; ++phase) {
            const Region r{1,1+length};
            const uint64_t position = uint64_t(k)*65536+phase;
            const auto m = Interpolation(mode);
            const auto y = lookup(edges,r,reverse,position,m);
            assert(y == lookup(guarded.data(),{0,length},reverse,position,m));
            const auto expected = round_pcm(reference(edges,r,reverse,position,m));
            assert(std::abs(int(y)-expected) <= (mode == 2 ? 1 : 0));
            if (!phase) assert(y == edges[reverse ? r.end-1-k : r.start+k]);
            // Mirror the entire PCM sequence: reverse must preserve phase.
            int16_t mirror[6];
            for (unsigned n=0;n<length;++n) mirror[1+n]=edges[r.end-1-n];
            if (reverse) assert(y == lookup(mirror,r,false,position,m));
          }
  }
  assert(lookup(nullptr,{0,1},false,0)==0);
  assert(lookup(edges,{3,3},false,0)==0);
  assert(lookup(edges,{4,3},false,0)==0);
  assert(lookup(edges,{0,1},true,65536)==0);
  assert(lookup(edges,{0,1},true,UINT64_MAX)==0);
  // Catmull-Rom overshoot must saturate, never wrap. Fixed/float precision.
  for (unsigned phase=0;phase<65536;++phase) {
    assert(quantize(hermite_float(-32768,32767,32767,-32768,uint16_t(phase)))==32767);
    assert(quantize(hermite_float(32767,-32768,-32768,32767,uint16_t(phase)))==-32768);
    assert(std::abs(int(linear_fixed(-32768,32767,uint16_t(phase))) -
           quantize(linear_float(-32768,32767,uint16_t(phase)))) <= 1);
    assert(std::abs(int(hermite_fixed(-30000,12345,-23456,30000,uint16_t(phase)))-
           quantize(hermite_float(-30000,12345,-23456,30000,uint16_t(phase))))<=1);
  }
  std::vector<int16_t> pcm(257);
  for (unsigned n=0;n<pcm.size();++n) pcm[n]=int16_t((n*7919)%65536-32768);
  Sample sample; sample.data=pcm.data(); sample.frames=unsigned(pcm.size());
  accepted::Sample old_sample; old_sample.data=pcm.data(); old_sample.frames=sample.frames;
  const unsigned before=allocations;
  for (unsigned length : {1u,2u,3u,4u,33u,257u})
    for (int pitch=0;pitch<128;++pitch)
      for (bool reverse : {false,true})
        for (unsigned action=0;action<4;++action) {
          Playback p{65535,0}; // corrupt/inverted stored region is repaired
          if (length>1) p={0,uint16_t(uint64_t(length)*65535/sample.frames)};
          p.reverse=reverse; p.mode=action==3?Mode::OneShot:Mode::Gate;
          accepted::Playback op{p.start,p.end,p.reverse,
              action==3?accepted::Mode::OneShot:accepted::Mode::Gate};
          Voice v; accepted::Voice old;
          v.assign(&sample); old.assign(&old_sample);
          const uint64_t step=pitch_increment(pitch);
          assert(step==accepted::pitch_increment(pitch));
          v.trigger(step,p,true,action==0?0:32); old.trigger(step,op,true,action==0?0:32);
          unsigned n=0;
          while(v.active || old.active) {
            if(n==5 && action>=2) { assert(v.release(action==3)==old.release(action==3)); }
            if(n==3 && action==1) {
              v.trigger(step,p,true); old.trigger(step,op,true);
            }
            assert(v.next(Interpolation::Nearest)==old.next());
            assert(v.position==old.position && v.remaining==old.remaining && v.active==old.active);
            assert(v.attack==old.attack && v.release_left==old.release_left && v.releasing==old.releasing);
            assert(++n<100000);
          }
          // All modes must retain transport/end/release duration.
          Voice modes[3];
          for(auto &voice:modes) {voice.assign(&sample);voice.trigger(step,p);}
          n=0;
          while(modes[0].active) {
            if(n==5) for(auto &voice:modes) voice.release();
            int16_t outputs[3];
            for(unsigned m=0;m<3;++m) outputs[m]=modes[m].next(Interpolation(m));
            for(unsigned m=1;m<3;++m) {
              assert(modes[m].position==modes[0].position && modes[m].remaining==modes[0].remaining && modes[m].active==modes[0].active);
              if(pitch==60) assert(outputs[m]==outputs[0]);
            }
            ++n;
          }
        }
  // Replacement clears transport before old PCM is retired.
  Voice voices[16]; Transfer transfer;
  for(unsigned mode=0;mode<3;++mode) {
    voices[0].assign(&sample); voices[0].trigger(69433);
    transfer.active[0]=&sample;
    Sample replacement; int16_t one=2345; replacement.data=&one; replacement.frames=1;
    transfer.publish(&replacement,0); assert(transfer.consume(voices)==0);
    assert(!voices[0].active && voices[0].next(Interpolation(mode))==0);
    assert(transfer.retired.exchange(nullptr)==&sample);
    voices[0].trigger(65536,{},false,0);
    assert(voices[0].next(Interpolation(mode))==one);
    voices[0].assign(nullptr);
  }
  assert(allocations==before);
  std::printf("M21 all phases, edges, reverse, all MIDI, old Nearest, unity, fades/release/retrigger, duration, retirement, zero allocations PASS; Voice=%zu accepted=%zu\n",sizeof(Voice),sizeof(accepted::Voice));
}
