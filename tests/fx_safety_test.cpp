#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "fx_stubs/esp_heap_caps.h"
struct SerialStub { void printf(const char *, ...) {} void println(const char *) {} } Serial;
struct EspStub { int getFreePsram() { return 0; } } ESP;
#define P4SDM_HEADLESS 1
#define free fx_test_free
#ifdef FX_BASELINE
#include "../.pio/fx_original.h"
#else
#include "../fx.h"
#endif
#undef free

template<class T> void digest(const char *name,T &fx) {
    const int allocations=fx_alloc_calls;
    uint64_t hash=1469598103934665603ULL;
    for(int i=0;i<132352;++i) {
        int16_t l=int16_t((i*97)%16001-8000),r=int16_t((i*131)%14001-7000);
        int32_t a,b; fx.process(l,r,a,b);
        hash=(hash^uint32_t(a))*1099511628211ULL;
        hash=(hash^uint32_t(b))*1099511628211ULL;
    }
    std::printf("%s %llu\n",name,static_cast<unsigned long long>(hash));
    assert(fx_alloc_calls==allocations);
}
#ifndef FX_BASELINE
template<class T,class Init> void failures(T &fx,Init init,int allocations,bool delay=false) {
    for(int failure=0;failure<allocations;++failure) {
        fx.release(); fx_alloc_calls=0; fx_fail_at=failure;
        assert(!init()); assert(!fx.isReady()); assert(fx_live.empty());
        int32_t l=999,r=999; fx.process(123,-321,l,r);
        assert(l==(delay?0:123) && r==(delay?0:-321));
    }
    fx_fail_at=-1; assert(init());
    const auto count=fx_live.size();
    assert(init()); assert(fx_live.size()==count);
    fx.release(); assert(fx_live.empty());
}
template<class T> void invalid_rate(T &fx) {
    assert(!fx.init(0)); assert(!fx.isReady());
    int32_t l,r; fx.process(123,-321,l,r); assert(l==123 && r==-321);
    assert(!fx.init(NAN)); assert(fx.init());
}
#endif
int main() {
#ifndef FX_BASELINE
    failures(myReverb,[]{return myReverb.init();},24);
    failures(myDelay,[]{return myDelay.init(88200);},2,true);
    failures(myChorus,[]{return myChorus.init();},2);
    failures(myFlanger,[]{return myFlanger.init();},2);
    invalid_rate(myTremolo); invalid_rate(myRingMod); invalid_rate(myDistortion); invalid_rate(myBitcrusher);
#endif
    myReverb.init(); myDelay.init(88200); myChorus.init(); myFlanger.init();
    myTremolo.init(); myRingMod.init(); myDistortion.init(); myBitcrusher.init();
    myReverb.setPreset(8); myDelay.setTime(12000);
    myChorus.setPreset(1); myFlanger.setPreset(1); myTremolo.setPreset(5);
    myRingMod.setPreset(2); myDistortion.setPreset(2); myBitcrusher.setPreset(2);
    digest("Reverb",myReverb); digest("Delay",myDelay); digest("Chorus",myChorus); digest("Flanger",myFlanger);
    digest("Tremolo",myTremolo); digest("RingMod",myRingMod); digest("Distortion",myDistortion); digest("Bitcrusher",myBitcrusher);
#ifndef FX_BASELINE
    myReverb.release(); myDelay.release(); myChorus.release(); myFlanger.release();
    assert(fx_live.empty());
#endif
}
