#pragma once
#include <cstddef>
#include <cstdint>
namespace midi {
struct Endpoint { uint8_t interface_number=0,alt=0,address=0; uint16_t packet=0; };
inline bool legacy_endpoint(const uint8_t *p,size_t size,Endpoint &out) {
  out={};
  if(size<9 || p[0]<9 || p[1]!=2) return false;
  const size_t total=p[2]|(size_t(p[3])<<8);
  if(total>size || total<9) return false;
  bool streaming=false,legacy=false;uint8_t number=0,alt=0;
  for(size_t i=0;i+2<=total;) {
    const size_t length=p[i];
    if(length<2 || i+length>total)return false;
    if(p[i+1]==4) {
      if(length<9)return false;
      streaming=p[i+5]==1 && p[i+6]==3 && p[i+7]==0;
      legacy=false;number=p[i+2];alt=p[i+3];
    } else if(streaming && p[i+1]==0x24 && length>=3 && p[i+2]==1) {
      if(length<7)return false;
      legacy=p[i+3]==0 && p[i+4]==1; // MIDIStreaming bcdMSC 1.00 only
    } else if(streaming && legacy && p[i+1]==5) {
      if(length<7)return false;
      const unsigned packet=p[i+4]|(unsigned(p[i+5])<<8);
      if((p[i+2]&0x80) && (p[i+2]&15) && (p[i+3]&3)==2 && (packet==8 || packet==16 || packet==32 || packet==64 || packet==512)) {
        out={number,alt,p[i+2],uint16_t(packet)};return true;
      }
    }
    i+=length;
  }
  return false;
}
}
