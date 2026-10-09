#pragma once
#include "model.h"
#include "pcm_read_cache.h"
namespace m212 {
// B/C diagnostic fixtures configured once, before any parent is accepted.
// Private pattern construction is startup-only and excluded from audio timing.
inline void configure(app::Engine &e, unsigned scenario) {
  e.apply({app::Kind::Play,0,0});
  e.apply({app::Kind::Bpm,0,240});
  e.apply({app::Kind::Delay,0,1});
  for (unsigned t=0;t<16;++t) {
    auto &track=e.tracks[t];
    track.assigned();
    e.apply({app::Kind::Pitch,uint8_t(t),scenario==2 ? 59 : int(47+t%24)});
    e.apply({app::Kind::SampleStart,uint8_t(t),scenario==2 ? 0 : int(t*256)});
    e.apply({app::Kind::SampleEnd,uint8_t(t),scenario==2 ? 65535 : int(65535-t*128)});
    e.apply({app::Kind::SampleReverse,uint8_t(t),scenario==3 && (t&1)});
    track.playback.mode=sampler::Mode::OneShot;
    track.slices.divide(track.playback,16);
    for (unsigned s=0;s<16;++s) track.slices.slices[s]=
      {uint16_t(scenario==2 ? 0 : t*256+s*32), uint16_t(scenario==2 ? 65535 : 65535-t*128-s*16)};
    track.slice_enabled=true;
    for(unsigned p=0;p<16;++p) {
      auto &pattern=e.patterns[p];
      pattern.length=4;
      pattern.track_steps[t]=15;
      for(unsigned s=0;s<16;++s) {
        pattern.meta[t][s]={100,100,4};
        pattern.locks[t][s]={255,uint8_t(scenario==2 ? 59 : 47+(t+s)%24),
          60,int8_t(int(t)*8-60),0,uint8_t(30+t*4),96,127,uint8_t((t+s)%16)};
      }
    }
  }
  e.apply({app::Kind::ChainClear,0,0});
  e.apply({app::Kind::ChainAdd,0,0});
  e.apply({app::Kind::ChainAdd,0,1});
  e.apply({app::Kind::ChainLoop,0,1});
  e.apply({app::Kind::ChainMode,0,1});
  e.apply({app::Kind::Play,0,1});
}
// Deterministic audio-owned commands on block boundaries. No active transport
// overrides. Every interpolation build gets the same accepted command stream.
inline void commands(app::Engine &e,unsigned block) {
  if(block==1000) e.apply({app::Kind::PerfOverride,0,2});
  if(block==1200) e.apply({app::Kind::PerfOverrideCancel,0,0});
  if(block==1500) e.apply({app::Kind::PerfFill,0,3});
  if(block==2000) e.apply({app::Kind::PerfRepeatStart,0,8});
  if(block==2600) e.apply({app::Kind::PerfRepeatStop,0,0});
}
struct Trace {
  uint32_t hash=2166136261u, events=0;
  uint32_t final_rng=0, final_chain=0, final_repeat=0;
  uint32_t active_frames[16]{};
  void add(uint32_t x) { hash=(hash^x)*16777619u; }
  void event(const app::TriggerEvent &e,bool ratchet,bool sample,uint32_t rng) {
    ++events;
    add(e.track);add(e.pitch);add(e.velocity);add(e.volume);add(uint8_t(e.pan));
    add(e.wave);add(e.locked_mask);add(e.filter_cutoff);add(e.filter_resonance);add(e.delay_send);
    add(e.slice);add(e.playback.start);add(e.playback.end);add(e.playback.reverse);
    add(unsigned(e.playback.mode));add(e.sequenced);add(ratchet);add(sample);add(rng);
  }
  template<class Voice> void block(const app::Engine &e, const Voice *voices) {
    add(e.playing_pattern);add(e.chain_entry);add(e.chain_repeat);add(e.chain_loops);
    add(e.rng);add(e.performance.repeat.active);add(e.performance.repeat.capture.count);
    add(e.step_events);add(e.ratchet_events);
    for(unsigned t=0;t<16;++t) {
      const auto &v=voices[t];add(v.active);add(uint32_t(v.increment));
      add(v.region.start);add(v.region.end);add(uint32_t(v.position));add(uint32_t(v.position>>32));
    }
    final_rng=e.rng;final_chain=e.chain_loops;final_repeat=e.repeat_metrics.x8_hits;
  }
};
}
