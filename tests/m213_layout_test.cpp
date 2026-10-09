#include "../src/app/pcm_allocation.h"
#include "../src/app/wav.h"
#include <cassert>
#include <cstdlib>
#include <set>
#include <vector>
#include <iostream>
struct Loaded : sampler::Sample { sampler::PcmAllocation pcm; };
int main() {
  std::set<void *> owners;
  unsigned frees=0;
  auto allocate=[&](size_t n) { void *p=std::malloc(n); assert(p); owners.insert(p); return p; };
  auto release=[&](void *p) { assert(owners.erase(p)==1); ++frees; std::free(p); };
  size_t total=123;
  assert(!sampler::PcmAllocation::request(0,0,0,total) && !total);
  assert(!sampler::PcmAllocation::request(3,1,0,total));
  assert(!sampler::PcmAllocation::request(2,3,0,total));
  assert(!sampler::PcmAllocation::request(2,1,16,total));
  assert(!sampler::PcmAllocation::request(SIZE_MAX-1,1,15,total));
  sampler::PcmAllocation failed;
  assert(!failed.acquire(512,1,0,[](size_t)->void * { return nullptr; }));
  assert(!failed.base && !failed.data && !failed.bytes);
  // Exercise every possible heap-base residue, including the maximum 63-byte
  // alignment correction, with canaries around the requested allocation.
  for(unsigned residue=0;residue<64;++residue) for(unsigned policy : {1u,2u}) {
    sampler::PcmAllocation a;
    std::vector<uint8_t> backing(2048,0xa5);
    uintptr_t aligned=(uintptr_t(backing.data())+63)&~uintptr_t(63);
    auto *base=reinterpret_cast<uint8_t *>(aligned+residue);
    size_t requested=0;
    assert(a.acquire(512,policy,15,[&](size_t n)->void * { requested=n; return base; }));
    for(unsigned n=0;n<256;++n) a.data[n]=int16_t(n);
    assert(base[requested]==0xa5);
    const size_t offset=uintptr_t(a.data)-uintptr_t(base);
    for(size_t n=0;n<offset;++n) assert(base[n]==0xa5);
    a.release([&](void *p) { assert(p==base); });
  }
  for(unsigned policy=0;policy<3;++policy) for(unsigned t=0;t<16;++t)
    for(unsigned frames : {1u,2u,3u,31u,32u,33u,65536u,2097152u}) {
      sampler::PcmAllocation a;
      assert(a.acquire(frames*2,policy,t,allocate));
      const size_t offset=uintptr_t(a.data)-uintptr_t(a.base);
      assert(offset+frames*2<=a.bytes);
      assert(a.bytes-frames*2<=1023);
      assert(uintptr_t(a.data)%2==0);
      if(policy) assert(uintptr_t(a.data)%64==0);
      for(unsigned n=0;n<frames;++n) a.data[n]=int16_t(n);
      assert(!a.acquire(frames*2,policy,t,allocate));
      a.release(release); const unsigned before=frees;
      a.release(release); assert(frees==before);
    }
  // Real Transfer publication/ack/retirement, mixed sizes, reordered loads,
  // retained fragmentation holes. A live Voice must keep its former owner
  // until consume() stops it and publishes the acknowledgement.
  sampler::Voice voices[16]; sampler::Transfer transfer;
  std::vector<void *> holes;
  auto destroy=[&](sampler::Sample *s) { auto *l=static_cast<Loaded *>(s); l->pcm.release(release); delete l; };
  for(unsigned cycle=0;cycle<64;++cycle) {
    unsigned t=(cycle*7)%16;
    if(cycle%4==0) holes.push_back(allocate(128+cycle*32));
    auto *s=new Loaded;
    const unsigned frames=1+(cycle*47)%2048;
    assert(s->pcm.acquire(frames*2,1,t,allocate));
    s->data=s->pcm.data; s->frames=frames; s->allocation=uint32_t(s->pcm.bytes);
    for(unsigned n=0;n<frames;++n) s->data[n]=int16_t(n*17);
    const auto *old=transfer.active[t];
    transfer.publish(s,t);
    assert(!transfer.acknowledged.load());
    if(old) { assert(voices[t].sample==old); (void)voices[t].next(); }
    assert(transfer.consume(voices)==int(t));
    assert(transfer.acknowledged.load() && voices[t].sample==s && !voices[t].active);
    auto *retired=transfer.retired.exchange(nullptr); if(retired) destroy(retired);
    sampler::Playback p; p.reverse=cycle&1;
    voices[t].trigger(sampler::pitch_increment(59),p);
    sampler::PcmReadCache cache;
    for(unsigned n=0;n<32;++n) (void)voices[t].next(sampler::Interpolation::Linear,&cache);
    cache.invalidate(); s->data[0]=123; assert(cache.read(s->data,{0,frames},0)==123);
  }
  // Project detachment ordering: stop/clear every audio view, then release.
  for(unsigned t=0;t<16;++t) { auto *s=transfer.active[t]; voices[t].assign(nullptr); transfer.active[t]=nullptr; if(s) destroy(s); }
  for(void *p:holes) release(p);
  assert(owners.empty());
  std::cout << "M213 placement bounds, owner-only free, failure, retirement, fragmentation and cache reuse PASS\n";
}
