#include "../src/app/ui_touch.h"
#include <cassert>
#include <iostream>
using namespace app;
int main() {
  ui22::TouchQueue<4> queue;
  ui22::TouchEvent event{Kind::Trigger,0,100,0,true},out;
  assert(queue.push(event)); event.initial=false;
  assert(queue.push(event)); assert(queue.push(event));
  event.kind=Kind::Play; event.release_generation=1;
  assert(!queue.push(event)); // actual dropped release
  Ui u; ui22::Gesture gesture; RepeatGate repeat;
  unsigned starts=0,stops=0; bool full=false;
  auto send=[&](Command c) { if(full) return false; if(c.kind==Kind::PerfRepeatStart) ++starts; else ++stops; return true; };
  assert(queue.pop(out)); assert(gesture.begin(out,u));
  u.down=true; u.capture=207; assert(repeat.press(8,send));
  while(queue.pop(out)) { assert(!gesture.begin(out,u)); }
  full=true;
  assert(gesture.reconcile(1,u)); assert(!u.down && u.capture==-1);
  repeat.release(send); assert(repeat.held && repeat.release_pending);
  full=false; repeat.flush(send); assert(!repeat.held && starts==1 && stops==1);
  assert(!gesture.reconcile(1,u));
  event={Kind::Trigger,0,200,1,false};
  assert(!gesture.begin(event,u)); // dropped press followed by a move
  event.initial=true; assert(gesture.begin(event,u));
  // Rapid alternating taps cannot duplicate a start on move or release.
  for(unsigned n=0;n<1000;++n) {
    event.release_generation=n+1; event.initial=true;
    assert(gesture.begin(event,u)); u.down=true;
    event.initial=false; assert(!gesture.begin(event,u));
    assert(gesture.reconcile(n+2,u)); assert(!u.down);
  }
  // Navigation cancels the capture; release owns the original musical gate.
  u.page=Page::PerformanceRepeat; u.down=true; u.capture=209;
  assert(repeat.press(8,send)); assert(ui20::navigate(u,300));
  assert(u.capture==-1 && !gesture.begin(event,u));
  assert(gesture.reconcile(2000,u)); repeat.release(send);
  assert(!repeat.held && stops==2);
  ui22::PollGuard guard;
  assert(!guard.fault(true) && !guard.fault(true));
  assert(guard.accept(true)); // a single/two transient I2C errors recover
  assert(!guard.fault(true) && !guard.fault(true) && guard.fault(true));
  assert(!guard.fault(true)); // exactly one cancellation
  assert(!guard.accept(true) && !guard.accept(true)); // no stale retrigger
  assert(!guard.accept(false) && guard.accept(true)); // actual lift/repress
  ui22::Histogram h;
  assert(h.percentile(95)==0);
  for(unsigned n=0;n<100;++n) h.add(n*1000);
  assert(h.percentile(50)==50000 && h.percentile(95)==95000 && h.percentile(99)==99000);
  h.add(500000); assert(h.maximum==500000 && h.percentile(100)==500000);
  ui22::Histogram wrapping; wrapping.add(uint32_t(20)-uint32_t(0xfffffff0));
  assert(wrapping.maximum==36);
  std::cout<<"M22 dropped release/press, alternating taps, navigation ownership, Stop retry, bounded histogram PASS\n";
}
