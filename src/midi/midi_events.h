#pragma once
#include <atomic>
#include <cstdint>
namespace midi {
enum class Type : uint8_t { NoteOn, NoteOff, CC, Program, Start, Stop, Continue, Clock };
struct Event {
  uint64_t received_us=0, parsed_us=0, queued_us=0;
  uint32_t generation=0;
  Type type=Type::Clock;
  uint8_t cable=0, channel=0, note=0, velocity=0;
};
// Exactly one USB client task produces; exactly one audio task consumes.
template<unsigned N> struct Queue {
  static_assert(N>1);
  Event data[N]{};
  std::atomic<unsigned> write{0}, read{0}, overflows{0};
  bool push(const Event &e) {
    auto w=write.load(std::memory_order_relaxed), next=(w+1)%N;
    if(next==read.load(std::memory_order_acquire)) { ++overflows; return false; }
    data[w]=e; write.store(next,std::memory_order_release); return true;
  }
  bool pop(Event &e) {
    auto r=read.load(std::memory_order_relaxed);
    if(r==write.load(std::memory_order_acquire)) return false;
    e=data[r]; read.store((r+1)%N,std::memory_order_release); return true;
  }
};
struct Latency {
  uint64_t count=0, receive_parse_max=0, parse_queue_max=0, queue_block_max=0;
  void accept(const Event &e,uint64_t now) {
    ++count;
    if(e.parsed_us>=e.received_us && e.parsed_us-e.received_us>receive_parse_max) receive_parse_max=e.parsed_us-e.received_us;
    if(e.queued_us>=e.parsed_us && e.queued_us-e.parsed_us>parse_queue_max) parse_queue_max=e.queued_us-e.parsed_us;
    if(now>=e.queued_us && now-e.queued_us>queue_block_max) queue_block_max=now-e.queued_us;
  }
};
} // namespace midi
