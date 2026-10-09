#include "../src/app/wav.h"
#include "../src/app/m212_fixture.h"
#include <array>
#include <cassert>
#include <cstdio>
int main() {
  for(unsigned scenario:{2u,3u}) {
    app::Engine e;
    m212::configure(e,scenario);
    static std::array<std::array<int16_t,65536>,16> pcm{};
    sampler::Sample samples[16]{};
    sampler::Voice voices[16]{};
    sampler::PcmReadCache caches[16]{};
    for(unsigned t=0;t<16;++t) {
      for(unsigned n=0;n<65536;++n) pcm[t][n]=int16_t((n%97)*160-7680);
      samples[t].data=pcm[t].data(); samples[t].frames=65536-(scenario==3 ? t*1024 : 0); voices[t].assign(&samples[t]);
    }
    m212::Trace trace;
    for(unsigned block=0;block<20672;++block) {
      m212::commands(e,block);
      for(auto &c:caches) c.invalidate();
      for(unsigned n=0;n<256;++n) {
        e.sample([&](app::TriggerEvent event,bool ratchet,bool sample) {
          trace.event(event,ratchet,sample,e.rng);
          voices[event.track].trigger(sampler::pitch_increment(event.pitch),event.playback,true);
        },[&](int t) { if(voices[t].sequenced) voices[t].release(); });
        for(unsigned t=0;t<16;++t) {
          trace.active_frames[t]+=voices[t].active && !voices[t].releasing;
          // PCM may differ. Only control/transport are compared.
#if P4SDM_PCM_READ_CACHE
          voices[t].next(sampler::default_interpolation,&caches[t]);
#else
          voices[t].next();
#endif
        }
      }
      trace.block(e,voices);
    }
    assert(trace.final_chain>0 && trace.final_repeat>0);
    printf("scenario=%u hash=%u events=%u rng=%u chain=%u x8=%u",scenario,trace.hash,trace.events,trace.final_rng,trace.final_chain,trace.final_repeat);
    for(auto n:trace.active_frames) { assert(n>20672*256-256); printf(" active=%u",n); }
    puts("");
  }
}
