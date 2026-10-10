#include "../src/midi/midi_parser.h"
#include "../src/midi/midi_mapping.h"
#include "../src/midi/usb_descriptors.h"
#include "../src/app/model.h"
#include <cassert>
#include <cmath>
#include <thread>
#include <vector>
#include <cstdio>
int main() {
  midi::PendingDevices pending;assert(!pending.push(0));
  for(uint8_t address=1;address<=8;++address)assert(pending.push(address));
  assert(pending.push(1) && !pending.push(9));
  uint8_t address=0;for(uint8_t i=1;i<=8;++i){assert(pending.pop(address));assert(address==i);}assert(!pending.pop(address));
  uint8_t descriptor[]={9,2,32,0,1,1,0,0x80,50,9,4,7,0,1,1,3,0,0,7,0x24,1,0,1,7,0,7,5,0x83,2,64,0,0};
  midi::Endpoint ep;assert(midi::legacy_endpoint(descriptor,sizeof(descriptor),ep));
  assert(ep.interface_number==7 && ep.address==0x83 && ep.packet==64);
  descriptor[22]=2;assert(!midi::legacy_endpoint(descriptor,sizeof(descriptor),ep)); // UMP
  descriptor[22]=1;descriptor[25]=0;assert(!midi::legacy_endpoint(descriptor,sizeof(descriptor),ep));
  descriptor[25]=7;assert(!midi::legacy_endpoint(descriptor,20,ep));
  midi::Parser p; std::vector<midi::Event> events;
  auto emit=[&](midi::Event e){events.push_back(e);};
  uint8_t bytes[]={0x29,0x99,36,80,0x08,0x89,36,1,0x09,0x99,37,0,0x0b,0xb9,123,0,0x0c,0xc0,12,0,0x0f,0xfa,0,0,0x0f,0xfc,0,0,0x0f,0xfb,0,0,0x0f,0xf8,0,0};
  p.transfer(bytes,sizeof(bytes),3,100,emit);
  assert(events.size()==9 && events[0].cable==2 && events[0].channel==9 && events[0].velocity==80);
  assert(events[2].type==midi::Type::NoteOff && events[4].type==midi::Type::Program);
  for(unsigned cin=0;cin<16;++cin) for(unsigned status=0;status<256;++status) {
    uint8_t packet[]={uint8_t(cin),uint8_t(status),0xff,0xff}; p.transfer(packet,4,3,100,emit);
  }
  assert(p.malformed && p.unsupported && p.sysex);
  auto n=events.size(); uint8_t tail[]={9,0x99,36}; p.transfer(tail,3,3,100,emit);
  assert(events.size()==n && p.partial==1);
  midi::Event e; e.type=midi::Type::NoteOn; e.channel=9;e.note=36;e.velocity=1;e.generation=4;
  assert(midi::drum_track(e)==0); e.note=51;assert(midi::drum_track(e)==15);
  e.note=52;assert(midi::drum_track(e)==-1);e.note=36;e.channel=0;assert(midi::drum_track(e)==-1);e.channel=9;
  midi::Ownership o; o.invalidate(0);o.claim(0,e);o.invalidate(0);o.claim(0,e);
  assert(!o.release(0,e));assert(o.release(0,e));assert(!o.release(0,e));
  o.claim(0,e);o.invalidate(0);assert(!o.release(0,e)); // sequencer/source replacement
  o.claim(0,e);auto wrong=e;wrong.note=37;assert(!o.release(0,wrong));
  wrong=e;wrong.generation=5;assert(!o.release(0,wrong));
  unsigned released=0; o.clear([&](unsigned){++released;},0);assert(!released);
  o.clear([&](unsigned){++released;},9);assert(released==1);
  midi::Queue<4> q;assert(q.push(e)&&q.push(e)&&q.push(e)&&!q.push(e));assert(q.overflows==1);
  midi::Event out;assert(q.pop(out)&&out.generation==4);assert(out.generation!=5);
  midi::Queue<128> concurrent; constexpr unsigned count=100000;
  std::thread producer([&]{for(unsigned i=0;i<count;++i){midi::Event v;v.generation=i;while(!concurrent.push(v)) std::this_thread::yield();}});
  for(unsigned i=0;i<count;++i){while(!concurrent.pop(out))std::this_thread::yield();assert(out.generation==i);}producer.join();
  midi::ClockEstimator clock;e.type=midi::Type::Start;clock.event(e);assert(clock.running);
  e.type=midi::Type::Clock;for(unsigned i=0;i<49;++i){e.received_us=1000+uint64_t(i)*20833;clock.event(e);}
  assert(std::abs(clock.bpm()-120)<.01 && clock.minimum==clock.maximum);
  midi::ClockEstimator batched;
  for(unsigned i=0;i<49;++i){e.received_us=1000+uint64_t(i/2)*41666;batched.event(e);}
  assert(std::abs(batched.bpm()-120)<.01 && batched.minimum==0); // callback batching is visible jitter
  assert(clock.timed_out(e.received_us+250001));e.received_us+=300000;clock.event(e);assert(clock.missing==1 && clock.bpm()==0);
  e.type=midi::Type::Stop;clock.event(e);assert(!clock.running);e.type=midi::Type::Continue;clock.event(e);assert(clock.running);
  app::Engine engine;engine.apply({app::Kind::Play,0,1});assert(engine.playing);engine.apply({app::Kind::Play,0,0});assert(!engine.playing);
  auto quiet=app::resolve_event(0,engine.tracks[0],1),loud=app::resolve_event(0,engine.tracks[0],127);
  assert(quiet.velocity==1 && loud.velocity==127 && quiet.volume==loud.volume);
  std::puts("M23 parser/CIN/cable/channel/mapping/velocity/ownership/overflow/SPSC/transport/clock PASS");
}
