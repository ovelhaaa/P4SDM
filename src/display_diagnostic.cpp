#include <Arduino.h>
#include "esp_psram.h"
#include "hal/guition_board.h"
#include "hal/display_hal.h"
#include "hal/touch_hal.h"
#include "hal/audio_hal.h"
#include "diagnostics/ui_pattern.h"
static bool ok;
static unsigned frames,polls,errors,presses,releases;
static touch::State previous;
void setup() {
    Serial.setTxBufferSize(4096); Serial.begin(115200); Serial.setTxTimeoutMs(1000);
    uint32_t start=millis(); while(!Serial && millis()-start<3000) delay(10);
    Serial.printf("[M4 DISPLAY] CPU=%u MHz native=480x800 logical=800x480 PPA_CCW=270 RGB565 native_fbs=2 logical_fbs=1 bytes_each=768000\n",ESP.getCpuFreqMHz());
    guition::memory_report("before display"); delay(100);
    esp_err_t error=display::begin();
    Serial.printf("[M4 DISPLAY] begin=%s\n",esp_err_to_name(error)); if(error!=ESP_OK) return;
    guition::memory_report("after display"); delay(100);
    ui_pattern::base(); error=display::present();
    Serial.printf("[M4 DISPLAY] pattern=%s\n",esp_err_to_name(error)); if(error!=ESP_OK) return;
    error=touch::begin();
    Serial.printf("[M4 TOUCH] begin=%s address=0x%02x id=%s native=%ux%u\n",esp_err_to_name(error),touch::address(),touch::product_id(),touch::native_width(),touch::native_height());
    if(error!=ESP_OK) return;
    guition::memory_report("after touch"); delay(100);
    const auto original_bus=guition::i2c_bus();
    error=audio::begin(44100);
    Serial.printf("[M4 SHARED] audio_begin=%s codec_probe=%s touch_probe=%s same_bus=%u\n",esp_err_to_name(error),
        esp_err_to_name(i2c_master_probe(guition::i2c_bus(),0x18,100)),
        esp_err_to_name(i2c_master_probe(guition::i2c_bus(),touch::address(),100)),original_bus==guition::i2c_bus());
    if(error!=ESP_OK) return;
    Serial.printf("[M4 SHARED] audio_end=%s\n",esp_err_to_name(audio::end()));
    ok=true;
    Serial.println("[M4 READY] Verify TL1 TR2 BR3 BL4, colors, border and counter. Touch corners/center and drag. No audible test.");
}
void loop() {
    if(!ok) {delay(1000);return;}
    static uint32_t next_frame=0,next_report=0;
    touch::State point;
    const esp_err_t error=touch::poll(point); ++polls;
    if(error!=ESP_OK) ++errors;
    else if(point.updated) {
        if(point.pressed!=previous.pressed) {
            point.pressed?++presses:++releases;
            Serial.printf("[M4 TOUCH] %s raw=%u,%u logical=%u,%u count=%u\n",point.pressed?"PRESS":"RELEASE",point.raw_x,point.raw_y,point.x,point.y,point.count);
        }
        previous=point;
    }
    if(millis()>=next_frame) {
        ui_pattern::update(++frames,previous);
        const esp_err_t frame_error=display::present();
        if(frame_error!=ESP_OK) {Serial.printf("[M4 DISPLAY] update=%s\n",esp_err_to_name(frame_error)); ok=false;}
        next_frame=millis()+1; // actual cadence measured; present fences refreshes.
    }
    if(millis()>=next_report) {
        Serial.printf("[M4 LIVE] frames=%u refreshes=%u polls=%u errors=%u presses=%u releases=%u x=%u y=%u down=%u\n",frames,display::refresh_count(),polls,errors,presses,releases,previous.x,previous.y,previous.pressed);
        next_report=millis()+1000;
    }
    delay(2);
}
