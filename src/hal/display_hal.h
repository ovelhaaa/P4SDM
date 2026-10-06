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
enum class Pipeline {FullPpa,NativeFull,NativeDirty};
esp_err_t begin(Pipeline pipeline=Pipeline::NativeDirty);
esp_err_t set_pipeline(Pipeline pipeline); // idle UI context; FullPpa requires its startup buffer.
esp_err_t begin_frame(); // repair stale backbuffer tiles before any drawing
bool idle();
esp_err_t end();
bool ready();
esp_err_t present(); // single UI owner; returns after two refreshes, or faults until end().
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
