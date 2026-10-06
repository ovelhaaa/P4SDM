#pragma once
#include <cstddef>
#include "esp_err.h"
#include "display_geometry.h"
namespace display {
esp_err_t begin();
esp_err_t end();
bool ready();
esp_err_t present(); // UI context only; blocks for PPA and safe framebuffer reuse.
void backlight(bool on);
void fill(uint16_t color);
void rect(int x,int y,int w,int h,uint16_t color);
void line(int x0,int y0,int x1,int y1,uint16_t color);
void circle(int x,int y,int radius,uint16_t color);
void text(int x,int y,const char *value,uint16_t color,int scale=3);
uint32_t refresh_count();
}
