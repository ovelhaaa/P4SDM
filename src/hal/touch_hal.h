#pragma once
#include <cstdint>
#include "esp_err.h"
namespace touch {
struct State {
    uint16_t raw_x=0,raw_y=0,x=0,y=0;
    uint8_t count=0;
    bool pressed=false,updated=false;
};
esp_err_t begin(); // borrows guition::i2c_bus(); never creates/deletes I2C0.
esp_err_t end();   // removes this device only.
esp_err_t poll(State &state);
uint8_t address();
uint16_t native_width();
uint16_t native_height();
const char *product_id();
}
