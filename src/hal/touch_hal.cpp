#if P4SDM_DISPLAY
#include "touch_hal.h"
#include "display_geometry.h"
#include "guition_board.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <initializer_list>
namespace touch {
static i2c_master_dev_handle_t device;
static uint8_t detected_address;
static uint16_t raw_width,raw_height;
static char identity[5];
static State last;
static esp_err_t read(uint16_t reg,uint8_t *data,size_t size) {
    const uint8_t command[]={uint8_t(reg>>8),uint8_t(reg)};
    return i2c_master_transmit_receive(device,command,2,data,size,10);
}
static esp_err_t acknowledge() {
    const uint8_t command[]={0x81,0x4e,0};
    return i2c_master_transmit(device,command,3,10);
}
uint8_t address() {return detected_address;}
uint16_t native_width() {return raw_width;}
uint16_t native_height() {return raw_height;}
const char *product_id() {return identity;}
esp_err_t end() {
    esp_err_t e=ESP_OK;
    if(device) {e=i2c_master_bus_rm_device(device); device=nullptr;}
    detected_address=0; last={}; return e;
}
esp_err_t begin() {
    if(device) return ESP_OK;
    esp_err_t error=guition::i2c_begin(); if(error!=ESP_OK) return error;
    gpio_set_direction(guition::TOUCH_RESET,GPIO_MODE_OUTPUT);
    gpio_set_level(guition::TOUCH_RESET,0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(guition::TOUCH_RESET,1); vTaskDelay(pdMS_TO_TICKS(50));
    for(uint8_t candidate : {uint8_t(0x5d),uint8_t(0x14)})
        if(i2c_master_probe(guition::i2c_bus(),candidate,100)==ESP_OK) {detected_address=candidate; break;}
    if(!detected_address) return ESP_ERR_NOT_FOUND;
    i2c_device_config_t config={}; config.dev_addr_length=I2C_ADDR_BIT_LEN_7;
    config.device_address=detected_address; config.scl_speed_hz=400000;
    error=i2c_master_bus_add_device(guition::i2c_bus(),&config,&device);
    if(error!=ESP_OK) {detected_address=0; return error;}
    error=read(0x8140,reinterpret_cast<uint8_t *>(identity),4);
    if(error!=ESP_OK) {end(); return error;}
    uint8_t dimensions[4]; error=read(0x8048,dimensions,4);
    if(error!=ESP_OK) {end(); return error;}
    raw_width=dimensions[0]|(uint16_t(dimensions[1])<<8);
    raw_height=dimensions[2]|(uint16_t(dimensions[3])<<8);
    // Do not silently scale or assume a controller orientation.
    if(raw_width!=display::NATIVE_WIDTH || raw_height!=display::NATIVE_HEIGHT) {
        end(); return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}
esp_err_t poll(State &state) {
    if(!device) return ESP_ERR_INVALID_STATE;
    state=last; state.updated=false;
    uint8_t status; esp_err_t error=read(0x814e,&status,1);
    if(error!=ESP_OK) return error;
    if(!(status&0x80)) return ESP_OK; // retain press until a fresh release packet.
    const uint8_t count=status&15;
    if(count>5) { acknowledge(); return ESP_ERR_INVALID_RESPONSE; }
    State next=last; next.count=count; next.pressed=count>0; next.updated=true;
    if(count) {
        uint8_t points[40]; error=read(0x814f,points,count*8);
        if(error!=ESP_OK) return error;
        next.raw_x=points[1]|(uint16_t(points[2])<<8);
        next.raw_y=points[3]|(uint16_t(points[4])<<8);
        if(next.raw_x>=raw_width || next.raw_y>=raw_height) {
            acknowledge(); return ESP_ERR_INVALID_RESPONSE;
        }
        const auto point=display::to_logical(next.raw_x,next.raw_y);
        next.x=point.x; next.y=point.y;
    }
    error=acknowledge(); if(error!=ESP_OK) return error;
    state=last=next; return ESP_OK;
}
}
#endif
