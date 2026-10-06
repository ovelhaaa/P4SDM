#include <Arduino.h>
#include <algorithm>
#include "driver/gpio.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "hal/audio_hal.h"
#include "hal/guition_board.h"
#include "engine/synth_api.h"

// One translation unit preserves the original Arduino global state and mixer.
// P4SDM_HEADLESS excludes all Tab5 startup/peripherals; no generated/copied DSP.
#include "../DRUM_2026_VSAMPLER_TAB5_2002.ino"
#include "../synthESP32.ino"

static void diagnostic_task(void *) {
    constexpr uint32_t duration = SAMPLE_RATE * 10;
    constexpr uint32_t note_interval = SAMPLE_RATE / 2;
    constexpr uint32_t budget_us = uint64_t(DMA_BUF_LEN) * 1000000 / SAMPLE_RATE;
    constexpr uint8_t notes[] = {60, 64, 67, 72, 69, 67, 64, 60};
    uint32_t frame = 0, next_note = 0, event = 0, blocks = 0;
    uint32_t max_render_us = 0, overruns = 0;
    int32_t peak_l = 0, peak_r = 0, peak_mono = 0;
    uint32_t nonzero_frames = 0;
    esp_err_t err = ESP_OK;
    while (frame < duration) {
        // Changes belong to the audio task: no concurrent writes to engine state.
        if (frame >= next_note) {
            const byte voice = event % 16;
            ROTvalue[voice][13] = (event & 1) ? 127 : -127;
            synthESP32_updateVolPan(voice);
            synthESP32_TRIGGER_P(voice, notes[event % 8]);
            ++event;
            next_note += note_interval;
        }
        const int64_t start = esp_timer_get_time();
        render_buffer();
        const uint32_t elapsed = esp_timer_get_time() - start;
        max_render_us = std::max(max_render_us, elapsed);
        if (elapsed >= budget_us) ++overruns;
        for (size_t i = 0; i < DMA_BUF_LEN; ++i) {
            const int32_t l = out_buf[2*i], r = out_buf[2*i+1];
            const int32_t mono = (l + r) / 2;
            peak_l = std::max(peak_l, std::abs(l));
            peak_r = std::max(peak_r, std::abs(r));
            peak_mono = std::max(peak_mono, std::abs(mono));
            if (mono) ++nonzero_frames;
        }
        err = audio::write(out_buf, DMA_BUF_LEN);
        if (err != ESP_OK) break;
        frame += DMA_BUF_LEN;
        ++blocks;
    }
    memset(out_buf, 0, sizeof(out_buf));
    if (err == ESP_OK) {
        for (unsigned i = 0; i < audio::DMA_DESCRIPTORS + 2; ++i) {
            err = audio::write(out_buf, DMA_BUF_LEN);
            if (err != ESP_OK) break;
        }
    }
    vTaskPrioritySet(nullptr, 1);
    const esp_err_t shutdown = audio::end();
    Serial.printf("[SYNTH] blocks=%u notes=%u nonzero_mono=%u peaks L=%ld R=%ld mono=%ld\n",
        blocks, event, nonzero_frames, long(peak_l), long(peak_r), long(peak_mono));
    Serial.printf("[SYNTH] max_render_us=%u block_budget_us=%u render_overruns=%u\n",
        max_render_us, budget_us, overruns);
    Serial.printf("[SYNTH] write=%s shutdown=%s result=%s\n", esp_err_to_name(err),
        esp_err_to_name(shutdown), err == ESP_OK && shutdown == ESP_OK && nonzero_frames &&
        !overruns ? "PASS (PCM/timing only)" : "FAIL");
    Serial.println("[SYNTH] Finished; PA disabled. Reset to replay.");
    Serial.flush();
    guition::memory_report("after synth");
    vTaskDelete(nullptr);
}

void setup() {
    gpio_set_level(guition::PA_ENABLE, 0);
    gpio_set_direction(guition::PA_ENABLE, GPIO_MODE_OUTPUT);
    Serial.begin(115200);
    const uint32_t start = millis();
    while (!Serial && millis() - start < 3000) delay(10);
    Serial.printf("\n[BOOT] Original P4SDM synth diagnostic, CPU=%u MHz\n", ESP.getCpuFreqMHz());
    guition::memory_report("boot");
    if (!psramFound() || esp_psram_get_size() != 32u * 1024u * 1024u) {
        Serial.println("[FAIL] Expected 32 MiB PSRAM; stopped.");
        return;
    }
    // Incremental validation: dry wavetable first, no FX allocation or SD.
    is_reverb = is_delay = is_chorus = is_flanger = false;
    is_tremolo = is_ringmod = is_distortion = is_bitcrusher = false;
    synthESP32_begin(); // no task in headless mode
    initADSR();
    for (byte voice = 0; voice < 16; ++voice) {
        ROTvalue[voice][16] = 1;
        ROTvalue[voice][1] = voice % WT_COUNT;
        ROTvalue[voice][9] = voice % 4;
        ROTvalue[voice][10] = 10;
        ROTvalue[voice][11] = 64;
        ROTvalue[voice][14] = 80;
        ROTvalue[voice][15] = 0;
        addnextsnd[voice] = 1;
        detune[voice] = 0;
    }
    setSoundALL();
    for (byte voice = 0; voice < 16; ++voice) { PITCH[voice] = 255; AMP[voice] = 0; }
    synthESP32_setMVol(60);
    synthESP32_setMFilter(0);
    guition::memory_report("engine init (FX disabled)");
    const esp_err_t err = audio::begin(SAMPLE_RATE);
    if (err != ESP_OK) {
        Serial.printf("[FAIL] audio begin: %s\n", esp_err_to_name(err));
        return;
    }
    Serial.println("[SYNTH] Original 16-voice mixer; automatic notes for 10 s, alternating PAN.");
    Serial.flush();
    if (xTaskCreatePinnedToCore(diagnostic_task, "synth", 8000, nullptr,
            configMAX_PRIORITIES - 1, nullptr, 0) != pdPASS) {
        audio::end();
        Serial.println("[FAIL] Could not create synth task.");
    }
}
void loop() { delay(1000); }
