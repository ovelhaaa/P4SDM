#include <Arduino.h>
#include <math.h>
#include <algorithm>
#include <atomic>
#include "esp_chip_info.h"
#include "esp_psram.h"
#include "driver/gpio.h"
#include "hal/audio_hal.h"
#include "hal/guition_board.h"

static int16_t pcm[audio::BLOCK_FRAMES * 2];
static int16_t sine_table[1024];
static std::atomic<bool> finished{false};

static void fail(const char *stage, esp_err_t err) {
    Serial.printf("[FAIL] %s: %s (0x%x)\n", stage, esp_err_to_name(err), unsigned(err));
    const esp_err_t cleanup = audio::end();
    if (cleanup != ESP_OK) Serial.printf("[FAIL] cleanup: %s\n", esp_err_to_name(cleanup));
    finished = true;
}
static void tone_task(void *) {
    constexpr uint32_t duration = 44100 * 5;
    constexpr uint32_t ramp = 44100 / 50; // 20 ms fade at each end
    constexpr uint32_t phase_step = uint32_t(440.0 * 4294967296.0 / 44100.0 + 0.5);
    uint32_t phase = 0;
    esp_err_t err = ESP_OK;
    for (uint32_t frame = 0; frame < duration;) {
        const size_t count = std::min(size_t(audio::BLOCK_FRAMES), size_t(duration - frame));
        for (size_t i = 0; i < count; ++i) {
            const uint32_t pos = frame + i;
            const int32_t gain = std::min(ramp, std::min(pos, duration - 1 - pos));
            const int16_t value = int32_t(sine_table[phase >> 22]) * gain / int32_t(ramp);
            pcm[2*i] = pcm[2*i+1] = value;
            phase += phase_step;
        }
        err = audio::write(pcm, count);
        if (err != ESP_OK) break;
        frame += count;
    }
    // Drain tone with > one full DMA ring of silence before disabling PA.
    memset(pcm, 0, sizeof(pcm));
    if (err == ESP_OK) {
        for (unsigned i = 0; i < audio::DMA_DESCRIPTORS + 2; ++i) {
            err = audio::write(pcm, audio::BLOCK_FRAMES);
            if (err != ESP_OK) break;
        }
    }
    // No serial/I2C/allocation during generation; lower priority for shutdown.
    vTaskPrioritySet(nullptr, 1);
    if (err != ESP_OK) fail("PCM write", err);
    else {
        err = audio::end();
        if (err != ESP_OK) fail("audio shutdown", err);
        else Serial.println("[AUDIO] Tone finished; PA disabled. Reset to replay.");
    }
    guition::memory_report("after audio test");
    vTaskDelete(nullptr);
}
void setup() {
    // Hold the amplifier off even in boot-only mode or on a PSRAM failure.
    const esp_err_t pa_level = gpio_set_level(guition::PA_ENABLE, 0);
    const esp_err_t pa_direction = gpio_set_direction(guition::PA_ENABLE, GPIO_MODE_OUTPUT);
    Serial.begin(115200);
    const uint32_t start = millis();
    while (!Serial && millis() - start < 3000) delay(10);
    if (pa_level != ESP_OK || pa_direction != ESP_OK) {
        fail("PA boot disable", pa_level != ESP_OK ? pa_level : pa_direction);
        return;
    }
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    Serial.printf("\n[BOOT] P4SDM Guition diagnostic; cores=%u revision=%u\n", chip.cores, chip.revision);
    guition::memory_report("boot");
    const size_t physical_psram = psramFound() ? esp_psram_get_size() : 0;
    if (physical_psram != 32u * 1024u * 1024u) {
        Serial.printf("[FAIL] Expected 32 MiB physical PSRAM, detected %u bytes\n", unsigned(physical_psram));
        finished = true;
        return;
    }
    Serial.println("[BOOT] 32 MiB physical PSRAM detected OK");
#if P4SDM_TEST_AUDIO
    for (size_t i = 0; i < 1024; ++i) sine_table[i] = lroundf(8192.0f * sinf(2.0f * PI * i / 1024));
    const esp_err_t err = audio::begin(44100);
    if (err != ESP_OK) { fail("audio begin", err); return; }
    guition::memory_report("audio init");
    Serial.println("[AUDIO] Playing 440 Hz test tone for 5 seconds");
    if (xTaskCreatePinnedToCore(tone_task, "tone", 4096, nullptr,
            configMAX_PRIORITIES - 1, nullptr, 0) != pdPASS) fail("tone task", ESP_ERR_NO_MEM);
#else
    Serial.println("[BOOT] Boot-only test complete; audio not initialized.");
#endif
}
void loop() {
    delay(1000);
    if (finished) Serial.println("[FAIL] Diagnostic stopped. Fix reported error and reset.");
}
