#pragma once
#include "midi_events.h"
#include <cstddef>
namespace midi {
struct Parser {
  uint32_t malformed=0, unsupported=0, sysex=0, partial=0;
  // USB transfer boundaries must end on complete event packets. Discard an
  // incomplete tail, never combine it with another device/transfer's packet.
  template<class Emit> void transfer(const uint8_t *p,size_t n,uint32_t gen,uint64_t us,Emit emit) {
    if(n%4) { ++partial; ++malformed; }
    for(size_t i=0;i+4<=n;i+=4) packet(p+i,gen,us,emit);
  }
  template<class Emit> void packet(const uint8_t *p,uint32_t gen,uint64_t us,Emit emit) {
    unsigned cin=p[0]&15, status=p[1];
    static constexpr uint8_t lengths[16]={0,0,2,3,3,1,2,3,3,3,3,3,2,2,3,1};
    if(!lengths[cin]) { ++malformed; return; }
    if(cin>=4 && cin<=7) {
      // CIN5 also carries one-byte system common; none is routed here.
      ++sysex; ++unsupported; return;
    }
    if(cin==15) {
      Event e; e.received_us=us; e.generation=gen; e.cable=p[0]>>4;
      switch(status) {
        case 0xf8:e.type=Type::Clock;break;
        case 0xfa:e.type=Type::Start;break;
        case 0xfb:e.type=Type::Continue;break;
        case 0xfc:e.type=Type::Stop;break;
        default: if(status<0xf8) ++malformed; else ++unsupported; return;
      }
      emit(e); return;
    }
    if(cin==2 || cin==3) {
      if((cin==2 && status!=0xf1 && status!=0xf3) || (cin==3 && status!=0xf2) || p[2]>127 || (cin==3 && p[3]>127)) ++malformed;
      else ++unsupported;
      return;
    }
    if((status>>4)!=cin || p[2]>127 || (lengths[cin]==3 && p[3]>127)) { ++malformed; return; }
    Event e; e.received_us=us; e.generation=gen; e.cable=p[0]>>4;
    e.channel=status&15; e.note=p[2]; e.velocity=lengths[cin]==3?p[3]:0;
    switch(cin) {
      case 8:e.type=Type::NoteOff;break;
      case 9:e.type=e.velocity?Type::NoteOn:Type::NoteOff;break;
      case 11:e.type=Type::CC;break;
      case 12:e.type=Type::Program;break;
      default:++unsupported;return;
    }
    emit(e);
  }
};
} // namespace midi
