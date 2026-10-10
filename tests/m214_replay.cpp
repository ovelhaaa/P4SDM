#include "../src/app/wav.h"
#include "../src/app/project.h"
#include "../src/app/m212_fixture.h"
#include <array>
#include <cassert>
#include <cstdio>

// Supplemental deterministic replay of A's musical action families. The
// original wall-clock UI/project/transient workload and analyzers are untouched.
// This is NOT a complete capture/replay of every original UI-generated command.
int main() {
  app::Engine e;
  m212::configure(e,3);
  static std::array<std::array<int16_t,65536>,16> pcm;
  sampler::Sample sources[16]; sampler::Voice voices[16];
  for(unsigned t=0;t<16;++t) {
    for(unsigned n=0;n<65536;++n) pcm[t][n]=int16_t((n%97)*160-7680);
    sources[t].data=pcm[t].data(); sources[t].frames=65536;
    voices[t].assign(&sources[t]);
  }
  project::State staged;
  std::array<uint8_t,project::file_bytes> encoded;
  static_assert(project::file_bytes==52704);
  m212::Trace trace;
  unsigned commands=0,projects=0,releases=0;
  auto command=[&](app::Command c) { e.apply(c); ++commands; };
  for(unsigned block=0;block<13782;++block) {
    trace.add(block);
    // Use integer audio block positions exclusively. Freeze edits for the same
    // 17-part snapshot and 17-part apply boundary protocol used by Project V2.
    const unsigned phase=block%4000;
    const bool transaction=block>=4000 && phase<34;
    if(transaction) {
      trace.add(0x50524f4a); trace.add(phase);
      if(phase<17) project::snapshot_part(e,staged,phase);
      if(phase==16) {
        assert(project::encode(staged,projects+1,encoded.data(),encoded.size()));
        assert(project::decode(encoded.data(),encoded.size(),staged)==project::Error::Ok);
        for(auto byte:encoded) trace.add(byte);
      }
      if(phase>=17) {
        project::apply_part(e,staged,phase-17);
        if(phase==17) for(auto &v:voices) v.release(true);
        if(phase==33) { ++projects; command({app::Kind::Play,0,1}); }
      }
    } else {
      if(block%11==0) {
        const unsigned n=block/11; const uint8_t t=uint8_t(n%16);
        command({app::Kind::FilterCutoff,t,int(n%128)});
        command({app::Kind::FilterResonance,t,int(n*7%128)});
        command({app::Kind::DelaySend,t,int(n*13%128)});
      }
      if(block%21==0) {
        const unsigned n=block/21;
        app::Command c{app::Kind(int(app::Kind::LockPitch)+n%7),uint8_t(n%16),int(n%128)};
        c.pattern=uint8_t(n%2); c.step=uint8_t(n/2%16);
        if(n%5==0) c.kind=app::Kind::ClearStepLocks;
        command(c);
      }
      if(block%43==0) {
        const unsigned n=block/43;
        app::Command c{n%5==0 ? app::Kind::UnlockParam : app::Kind::LockSlice,
                       uint8_t(n%16),n%5==0 ? int(app::SLICE_LOCK) : int(n*7&15)};
        c.pattern=uint8_t(e.playing_pattern); c.step=uint8_t(n/16%16); command(c);
      }
      if(block==1500) command({app::Kind::PerfOverride,0,1});
      if(block==1800) command({app::Kind::PerfOverrideCancel,0,0});
      if(block==2000) command({app::Kind::PerfFill,0,1});
      if(block==2500) command({app::Kind::PerfFillCancel,0,0});
      if(block==3000 || block==7000 || block==11000) command({app::Kind::PerfRepeatStart,0,8});
      if(block==3600 || block==7600 || block==11600) command({app::Kind::PerfRepeatStop,0,0});
      if(block%517==0) for(unsigned t=0;t<16;++t) {
        const auto event=app::resolve_event(t,e.tracks[t],100);
        trace.event(event,false,e.tracks[t].sample,e.rng);
        voices[t].trigger(sampler::pitch_increment(event.pitch),event.playback,false);
      }
    }
    for(unsigned n=0;n<256;++n) {
      e.sample([&](app::TriggerEvent event,bool ratchet,bool sample) {
        trace.add(block*256+n); trace.event(event,ratchet,sample,e.rng);
        voices[event.track].trigger(sampler::pitch_increment(event.pitch),event.playback,true);
      },[&](int t) { if(voices[t].sequenced) { voices[t].release(); ++releases; } });
      for(unsigned t=0;t<16;++t) {
        auto &v=voices[t]; v.next();
        // Canonical per-sample transport, fade and route state, exclude PCM.
        trace.add(v.active); trace.add(v.releasing); trace.add(v.remaining);
        trace.add(v.attack); trace.add(v.release_left); trace.add(v.sequenced);
        trace.add(uint32_t(v.position)); trace.add(uint32_t(v.position>>32));
        trace.add(e.tracks[t].sample); trace.add(v.region.start); trace.add(v.region.end);
        trace.add(uint32_t(v.increment));
      }
    }
    trace.block(e,voices);
  }
  assert(projects==3 && commands>3000 && trace.events>10000 && trace.final_repeat>0);
  printf("A-family replay hash=%u events=%u commands=%u projects=%u rng=%u chain=%u x8=%u releases=%u Voice=%zu V2=%u\n",
         trace.hash,trace.events,commands,projects,trace.final_rng,trace.final_chain,
         trace.final_repeat,releases,sizeof(sampler::Voice),project::file_bytes);
}
