#include "guition_board.h"
#include <Arduino.h>
#include "esp_heap_caps.h"

namespace guition {
static i2c_master_bus_handle_t bus = nullptr;
esp_err_t i2c_begin() {
    if (bus) return ESP_OK;
    i2c_master_bus_config_t config = {};
    config.i2c_port = I2C_NUM_0;
    config.sda_io_num = SDA;
    config.scl_io_num = SCL;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&config, &bus);
}
i2c_master_bus_handle_t i2c_bus() { return bus; }
void memory_report(const char *stage) {
    Serial.printf("[MEM] %s PSRAM total=%u free=%u largest=%u internal_free=%u\n",
        stage, (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}
}
