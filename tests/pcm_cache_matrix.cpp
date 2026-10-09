#include "../src/app/wav.h"
#include <cassert>
#include <cstdio>
#include <vector>
int main() {
  sampler::PcmReadCache c;
  for (unsigned size:{1u,2u,15u,16u,17u,31u,32u,33u,63u,64u,65u,127u,128u,129u,257u}) {
    std::vector<int16_t> pcm(size);
    for(unsigned n=0;n<size;++n) pcm[n]=int16_t(n*7919);
    for(unsigned start=0;start<size;++start) {
      for(unsigned n=start;n<size;++n) assert(c.read(pcm.data(),{start,size},n)==pcm[n]);
      for(unsigned n=size;n-->start;) assert(c.read(pcm.data(),{start,size},n)==pcm[n]);
      for(unsigned n=start;n<size;n+=7) assert(c.read(pcm.data(),{start,size},n)==pcm[n]);
      for (bool reverse:{false,true}) for(unsigned mode=0;mode<3;++mode)
        for(unsigned frame=0;frame<size-start;++frame)
          for(unsigned phase:{0u,1u,32767u,32768u,65535u}) {
            const uint64_t position=(uint64_t(frame)<<16)+phase;
            assert(sampler::lookup_valid(pcm.data(),{start,size},reverse,position,sampler::Interpolation(mode),&c)==
                   sampler::lookup(pcm.data(),{start,size},reverse,position,sampler::Interpolation(mode)));
          }
    }
    c.invalidate();
    assert(c.read(pcm.data(),{0,size},0)==pcm[0]);
    pcm[0]=1234; c.invalidate(); // acknowledged replacement at reused address
    assert(c.read(pcm.data(),{0,size},0)==1234);
    assert(c.read(nullptr,{0,size},0)==0);
    assert(c.read(pcm.data(),{0,0},0)==0);
    assert(c.read(pcm.data(),{0,size},size)==0);
  }
  printf("M212 cache frames=%u fill=%u bytes=%zu hits=%u misses=%u psram_reads=%u PASS\n",
    c.capacity,unsigned(P4SDM_PCM_CACHE_FILL),sizeof(c),c.hits,c.misses,c.reads);
}
