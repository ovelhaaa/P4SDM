#pragma once
#include "esp_heap_caps.h"

enum FxMask { RV=1, DL=2, CH=4, FL=8, TR=16, RM=32, DT=64, BC=128 };
struct FxStageSpec { const char *name; unsigned mask; };
static constexpr FxStageSpec fx_specs[] = {
    {"Dry",0}, {"Reverb",RV}, {"Delay",DL}, {"Chorus",CH}, {"Flanger",FL},
    {"Tremolo",TR}, {"RingMod",RM}, {"Distortion",DT}, {"Bitcrusher",BC},
    {"A",CH|DL|RV}, {"B",CH|FL|TR}, {"C",DT|BC|DL|RV}, {"D",255}
};
static unsigned fx_ready_mask;
static size_t fx_psram_before, fx_internal_before;
static bool fx_heap_ok = true;
static size_t fx_psram() { return heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }
static size_t fx_internal() { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }
static void fx_release_buffers() {
    myReverb.release(); myDelay.release(); myChorus.release(); myFlanger.release();
}
static bool fx_init(unsigned mask) {
    bool ok=true;
    if(mask&RV) ok &= myReverb.init();
    if(mask&DL) ok &= myDelay.init(88200);
    if(mask&CH) ok &= myChorus.init();
    if(mask&FL) ok &= myFlanger.init();
    if(mask&TR) ok &= myTremolo.init();
    if(mask&RM) ok &= myRingMod.init();
    if(mask&DT) ok &= myDistortion.init();
    if(mask&BC) ok &= myBitcrusher.init();
    return ok;
}
static void fx_allocate_audit() {
    guition::memory_report("before FX allocation"); delay(100);
    for(unsigned i=1;i<=8;++i) {
        const size_t ps=fx_psram(), in=fx_internal();
        const bool ok=fx_init(fx_specs[i].mask);
        guition::memory_report(fx_specs[i].name);
        reportf("[M3.2] alloc stage=%s ready=%u psram_bytes=%ld internal_bytes=%ld\n",
            fx_specs[i].name,ok,long(ps)-long(fx_psram()),long(in)-long(fx_internal()));
        fx_release_buffers();
        const bool restored=ps==fx_psram() && in==fx_internal();
        fx_heap_ok &= restored;
        reportf("[M3.2] release stage=%s heap_restored=%u\n",fx_specs[i].name,restored);
        delay(100);
    }
    for(unsigned i=1;i<=8;++i) if(fx_init(fx_specs[i].mask)) fx_ready_mask |= fx_specs[i].mask;
    guition::memory_report("all FX initialized"); delay(100);
}
static void fx_prepare(unsigned mask) {
    // All reset/memset work is outside the realtime measurement interval.
    myReverb.reset(); myDelay.reset(); myChorus.reset(); myFlanger.reset();
    myTremolo.reset(); myRingMod.reset(); myDistortion.reset(); myBitcrusher.reset();
    myReverb.setPreset(8); myDelay.setTime(12000); myDelay.setFeedback(120); myDelay.setInputLevel(160);
    myChorus.setPreset(1); myFlanger.setPreset(1); myTremolo.setPreset(5);
    myRingMod.setPreset(2); myDistortion.setPreset(2); myBitcrusher.setPreset(2);
    is_reverb=mask&RV; is_delay=mask&DL; is_chorus=mask&CH; is_flanger=mask&FL;
    is_tremolo=mask&TR; is_ringmod=mask&RM; is_distortion=mask&DT; is_bitcrusher=mask&BC;
    reverbs=mask&RV ? 0xffff:0; delays=mask&DL ? 0xffff:0;
    choruss=mask&CH ? 0xffff:0; flangers=mask&FL ? 0xffff:0;
    tremolos=mask&TR ? 0xffff:0; ringmods=mask&RM ? 0xffff:0;
    distortions=mask&DT ? 0xffff:0; bitcrushers=mask&BC ? 0xffff:0;
    level_reverb=level_delay=level_chorus=level_flanger=level_tremolo=level_ringmod=level_distortion=level_bitcrusher=100;
    for(auto &filter : FILTROS) filter.reset();
    setSoundALL(); synthESP32_setMFilter(0);
}
