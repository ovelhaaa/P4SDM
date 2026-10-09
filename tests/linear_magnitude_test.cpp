#include "../src/app/sample_interpolation.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main() {
  for (int a:{-32768,-32767,-1,0,1,32767})
    for(int b:{-32768,-32767,-1,0,1,32767})
      for(unsigned phase=0;phase<65536;++phase) {
        assert(sampler::linear_fixed32_magnitude(int16_t(a),int16_t(b),uint16_t(phase))==
               sampler::linear_fixed(int16_t(a),int16_t(b),uint16_t(phase)));
      }
  uint32_t rng=0x12345678;
  for(unsigned n=0;n<1000000;++n) {
    rng=rng*1664525u+1013904223u; const int16_t a=int16_t(rng>>16);
    rng=rng*1664525u+1013904223u; const int16_t b=int16_t(rng>>16);
    rng=rng*1664525u+1013904223u; const auto phase=uint16_t(rng>>16);
    assert(sampler::linear_fixed32_magnitude(a,b,phase)==sampler::linear_fixed(a,b,phase));
  }
  puts("M212 magnitude rounding vs Linear64: all phases/rails + 1000000 tuples PASS");
}
