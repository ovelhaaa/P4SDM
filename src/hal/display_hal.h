#pragma once
#include <cstddef>
#include "esp_err.h"
#include "display_geometry.h"
namespace display {
struct Telemetry {
    uint32_t repair_us=0,ppa_us=0,cache_us=0,submit_us=0,wait_us=0,present_us=0;
    uint32_t dirty_bytes=0,repair_bytes=0,cache_span_bytes=0;
};
Telemetry telemetry();
enum class Pipeline {FullPpa,NativeFull,NativeDirty,NativeQueued};
esp_err_t begin(Pipeline pipeline=Pipeline::NativeQueued,bool reserve_third=false);
esp_err_t set_pipeline(Pipeline pipeline); // idle UI context; FullPpa requires its startup buffer.
esp_err_t begin_frame(); // repair stale backbuffer tiles before any drawing
bool idle();
esp_err_t wait_idle(); // drain a queued presentation before static/page/teardown
uint32_t completed_presentations(); // acknowledged two-refresh retirements, not requests
esp_err_t end();
bool ready();
// Single UI owner. NativeQueued fences the previous submission before selecting
// this one; wait_idle() drains the final pending image. Other paths fence here.
esp_err_t present(); // faults prevent reuse until end()/begin()
void backlight(bool on);
void fill(uint16_t color);
void clip(int x,int y,int w,int h); // single UI owner's logical drawing clip
void reset_clip();
void rect(int x,int y,int w,int h,uint16_t color);
void line(int x0,int y0,int x1,int y1,uint16_t color);
void circle(int x,int y,int radius,uint16_t color);
void text(int x,int y,const char *value,uint16_t color,int scale=3);
uint32_t refresh_count();
}
