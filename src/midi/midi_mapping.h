#pragma once
#include "midi_events.h"
#include <algorithm>
namespace midi {
#ifndef P4SDM_MIDI_BASE_NOTE
#define P4SDM_MIDI_BASE_NOTE 36
#endif
inline int drum_track(const Event &e) {
  return e.cable==0 && e.channel==9 && e.note>=P4SDM_MIDI_BASE_NOTE && e.note<P4SDM_MIDI_BASE_NOTE+16 ? e.note-P4SDM_MIDI_BASE_NOTE : -1;
}
struct Owner { uint32_t device=0, trigger=0; uint8_t channel=0,note=0,cable=0; bool active=false; };
struct Ownership {
  uint32_t serial[16]{}, outstanding[16]{}; Owner owners[16]{};
  void invalidate(unsigned t) { ++serial[t]; owners[t].active=false; }
  void claim(unsigned t,const Event &e) {
    if(outstanding[t]!=UINT32_MAX) ++outstanding[t];
    owners[t]={e.generation,serial[t],e.channel,e.note,e.cable,true};
  }
  bool release(unsigned t,const Event &e) {
    auto &o=owners[t];
    if(o.device!=e.generation || o.channel!=e.channel || o.note!=e.note || o.cable!=e.cable) return false;
    if(outstanding[t]) --outstanding[t];
    if(!o.active || o.trigger!=serial[t] || outstanding[t]) return false;
    o.active=false; return true;
  }
  template<class Release> void clear(Release release,int channel=-1,int cable=-1) {
    for(unsigned t=0;t<16;++t) if((channel<0 || owners[t].channel==channel) && (cable<0 || owners[t].cable==cable)) { if(owners[t].active) release(t); invalidate(t); outstanding[t]=0; }
  }
};
struct ClockEstimator {
  uint64_t last=0, intervals=0, total=0, minimum=UINT64_MAX, maximum=0;
  uint32_t pulses=0, missing=0; bool running=false;
  void event(const Event &e) {
    if(e.type==Type::Start) { running=true; last=0; intervals=total=0; minimum=UINT64_MAX; maximum=0; }
    if(e.type==Type::Continue) running=true;
    if(e.type==Type::Stop) { running=false; last=0; }
    if(e.type!=Type::Clock) return;
    ++pulses;
    if(last && e.received_us>=last) {
      auto d=e.received_us-last;
      if(d>250000) { ++missing; intervals=total=0; minimum=UINT64_MAX; maximum=0; }
      else { ++intervals; total+=d; minimum=std::min(minimum,d); maximum=std::max(maximum,d); }
    }
    last=e.received_us;
  }
  bool timed_out(uint64_t now) const { return last && now>last && now-last>250000; }
  double bpm() const { return total && intervals>=24 ? 60000000.0*intervals/(24.0*total) : 0; }
};
} // namespace midi
