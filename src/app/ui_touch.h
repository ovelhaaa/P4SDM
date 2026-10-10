#pragma once
#include "ui_layout.h"
#include <atomic>

namespace app::ui22 {
// UI-only packets: never serialized or forwarded to the musical command queue.
struct TouchEvent {
  Kind kind=Kind::Play;
  int value=0;
  uint32_t observed_us=0, release_generation=0;
  bool initial=false;
};
template<unsigned N> struct TouchQueue {
  TouchEvent data[N]{};
  std::atomic<unsigned> write{0},read{0};
  bool push(TouchEvent event) {
    const unsigned w=write.load(std::memory_order_relaxed),next=(w+1)%N;
    if(next==read.load(std::memory_order_acquire)) return false;
    data[w]=event; write.store(next,std::memory_order_release); return true;
  }
  bool pop(TouchEvent &event) {
    const unsigned r=read.load(std::memory_order_relaxed);
    if(r==write.load(std::memory_order_acquire)) return false;
    event=data[r]; read.store((r+1)%N,std::memory_order_release); return true;
  }
};
struct Gesture {
  uint32_t release_generation=0;
  bool reconcile(uint32_t generation,Ui &u) {
    if(generation==release_generation) return false;
    release_generation=generation; u.down=false; u.capture=-1; return true;
  }
  // A move whose press was dropped must never become a new tap.
  bool begin(const TouchEvent &event,const Ui &u) const {
    return event.kind!=Kind::Play && event.initial && !u.down;
  }
};
// Fixed 1 ms histogram bins. Quantiles are upper bounds, overflow uses max.
// Atomics permit touch-task polling metrics to be read safely by the UI task.
struct Histogram {
  std::atomic<uint32_t> bins[256]{},count{0},maximum{0};
  void add(uint32_t us) {
    bins[std::min<uint32_t>(us/1000,255)].fetch_add(1,std::memory_order_relaxed);
    count.fetch_add(1,std::memory_order_relaxed);
    auto old=maximum.load(std::memory_order_relaxed);
    while(us>old && !maximum.compare_exchange_weak(old,us,std::memory_order_relaxed)) {}
  }
  uint32_t percentile(unsigned percent) const {
    const uint32_t total=count.load(std::memory_order_relaxed);
    if(!total) return 0;
    const uint32_t target=(uint64_t(total)*percent+99)/100;
    uint32_t sum=0;
    for(unsigned n=0;n<256;++n) {
      sum+=bins[n].load(std::memory_order_relaxed);
      if(sum>=target) return n==255 ? maximum.load() : (n+1)*1000;
    }
    return maximum.load();
  }
};
} // namespace app::ui22
