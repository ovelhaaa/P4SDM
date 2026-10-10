#include "../src/app/pcm_placement.h"
#include "../src/app/wav.h"
#include "../src/app/project.h"
#include <cassert>
#include <cstdio>
#ifndef EXPECTED_PLACEMENT
#define EXPECTED_PLACEMENT 0
#endif
#ifndef EXPECTED_INTERPOLATION
#define EXPECTED_INTERPOLATION 0
#endif
static_assert(P4SDM_PCM_PLACEMENT==EXPECTED_PLACEMENT);
static_assert(unsigned(sampler::default_interpolation)==EXPECTED_INTERPOLATION);
static_assert(project::file_bytes==52704);
// Host pointer width differs from the 32-bit target; freeze both accepted sizes.
static_assert(sizeof(sampler::Voice)==(sizeof(void*)==8 ? 64 : 56));
int main() {
  assert(sampler::lookup(nullptr,{0,1},false,0)==0);
  const int16_t pcm[]{-32768,32767};
  for(unsigned fraction=0;fraction<65536;++fraction) {
    const auto y=sampler::lookup(pcm,{0,2},false,fraction);
    assert(y==(EXPECTED_INTERPOLATION==0 ? pcm[0] :
               sampler::linear_fixed(pcm[0],pcm[1],uint16_t(fraction))));
  }
  printf("placement=%u interpolation=%u Voice=%zu V2=%u PASS\n",
         unsigned(P4SDM_PCM_PLACEMENT),EXPECTED_INTERPOLATION,sizeof(sampler::Voice),project::file_bytes);
}
