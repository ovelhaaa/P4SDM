#pragma once
#include "driver/i2c_master.h"

namespace guition {
constexpr gpio_num_t SDA = GPIO_NUM_7;
constexpr gpio_num_t SCL = GPIO_NUM_8;
constexpr gpio_num_t MCLK = GPIO_NUM_13;
constexpr gpio_num_t BCLK = GPIO_NUM_12;
constexpr gpio_num_t WS = GPIO_NUM_10;
constexpr gpio_num_t DOUT = GPIO_NUM_9;
constexpr gpio_num_t PA_ENABLE = GPIO_NUM_11;
constexpr uint8_t CODEC_ADDRESS = 0x18;
constexpr gpio_num_t LCD_RESET = GPIO_NUM_5;
constexpr gpio_num_t LCD_BACKLIGHT = GPIO_NUM_23;
constexpr gpio_num_t TOUCH_RESET = GPIO_NUM_3;
// Single owner. Future GT911 initialization must use this handle, not create
// another I2C_NUM_0 bus or call Wire.begin() on these pins.
esp_err_t i2c_begin();
i2c_master_bus_handle_t i2c_bus();
void memory_report(const char *stage);
}
