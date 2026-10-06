#include <Arduino.h>
#include <algorithm>
#include <cstdarg>
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

// Fixed storage: no allocation, sorting or logging during measurement.
constexpr unsigned STAGE_BLOCKS = (SAMPLE_RATE * 3 + DMA_BUF_LEN - 1) / DMA_BUF_LEN;
constexpr double BUDGET_US = double(DMA_BUF_LEN) * 1000000 / SAMPLE_RATE;
struct Timing { uint32_t values[STAGE_BLOCKS]; };
struct Stage {
    unsigned voices, blocks = 0, active_min = 16, active_max = 0;
    unsigned render_misses = 0, cycle_misses = 0, failures = 0, timeouts = 0;
    unsigned silent_blocks = 0, nonzero = 0, rails = 0;
    int32_t peak_l = 0, peak_r = 0, peak_mono = 0;
    Timing render, write, cycle;
};
static Stage stages[4];
static unsigned active_voices() {
    unsigned n = 0;
    for (unsigned v = 0; v < 16; ++v) if (PITCH[v] != 255 && AMP[v] != 0) ++n;
    return n;
}
static void observe_active(Stage &s) {
    const unsigned n = active_voices();
    s.active_min = std::min(s.active_min, n);
    s.active_max = std::max(s.active_max, n);
}
// Low-priority reporting only. Pace sub-packet writes on native USB/JTAG CDC.
static void reportf(const char *format, ...) {
    char line[256];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    const size_t count = length > 0 ? std::min(size_t(length), sizeof(line)-1) : 0;
    for (size_t offset = 0; offset < count; offset += 32) {
        Serial.write(reinterpret_cast<const uint8_t *>(line + offset),
            std::min(size_t(32), count - offset));
        delay(10);
    }
}
static void print_timing(const char *name, Timing &t, unsigned n) {
    if (!n) return;
    uint64_t sum = 0;
    for (unsigned i = 0; i < n; ++i) sum += t.values[i];
    std::sort(t.values, t.values + n);
    auto percentile = [&](unsigned p) { return t.values[(n * p + 99) / 100 - 1]; };
    reportf("[M3.1] %s us min=%u avg=%.2f p50=%u p95=%u p99=%u max=%u\n",
        name, t.values[0], double(sum) / n, percentile(50), percentile(95),
        percentile(99), t.values[n-1]);
    delay(100); // allow deferred USB serial delivery without HWCDC flush/discard
    delay(10); // reporting only; measurement and transport already stopped
}
static void diagnostic_task(void *) {
    constexpr unsigned levels[] = {1, 4, 8, 16};
    esp_err_t last_error = ESP_OK;
    bool pass = true;
    vTaskPrioritySet(nullptr, 1);
    guition::memory_report("before stress (task allocated)");
    delay(100); // allow deferred USB serial delivery without HWCDC flush/discard
    vTaskPrioritySet(nullptr, configMAX_PRIORITIES - 1);
    for (unsigned stage = 0; stage < 4; ++stage) {
        Stage &s = stages[stage];
        s.voices = levels[stage];
        for (unsigned v = 0; v < 16; ++v) { PITCH[v] = 255; AMP[v] = 0; }
        // All triggers occur before the first sample: exact simultaneous onset
        // in engine sample time, distinct MIDI notes, wavetables and pans.
        for (unsigned v = 0; v < s.voices; ++v) synthESP32_TRIGGER_P(v, 48 + v);
        for (unsigned block = 0; block < STAGE_BLOCKS; ++block) {
            const int64_t start = esp_timer_get_time();
            observe_active(s);
            const int64_t render_start = esp_timer_get_time();
            render_buffer();
            const int64_t rendered = esp_timer_get_time();
            observe_active(s);
            unsigned nonzero = 0;
            for (unsigned i = 0; i < DMA_BUF_LEN; ++i) {
                const int32_t l = out_buf[2*i], r = out_buf[2*i+1];
                const int32_t mono = (l + r) / 2; // same arithmetic as HAL downmix
                s.peak_l = std::max(s.peak_l, std::abs(l));
                s.peak_r = std::max(s.peak_r, std::abs(r));
                s.peak_mono = std::max(s.peak_mono, std::abs(mono));
                if (mono) ++nonzero;
                if (l == -32768 || l == 32767 || r == -32768 || r == 32767 ||
                    mono == -32768 || mono == 32767) ++s.rails;
            }
            s.nonzero += nonzero;
            if (!nonzero) ++s.silent_blocks;
            const int64_t write_start = esp_timer_get_time();
            last_error = audio::write(out_buf, DMA_BUF_LEN);
            const int64_t end = esp_timer_get_time();
            s.render.values[block] = rendered - render_start;
            s.write.values[block] = end - write_start;
            s.cycle.values[block] = end - start; // includes PCM/active probes
            if (s.render.values[block] >= BUDGET_US) ++s.render_misses;
            if (s.cycle.values[block] >= BUDGET_US) ++s.cycle_misses;
            ++s.blocks;
            if (last_error != ESP_OK) {
                ++s.failures;
                if (last_error == ESP_ERR_TIMEOUT) ++s.timeouts;
                break;
            }
        }
        pass &= s.blocks == STAGE_BLOCKS && s.active_min == s.voices &&
            s.active_max == s.voices && s.nonzero && !s.silent_blocks &&
            !s.rails && !s.failures && !s.render_misses;
        if (last_error != ESP_OK) break;
    }
    memset(out_buf, 0, sizeof(out_buf));
    unsigned drain_errors = 0;
    if (last_error == ESP_OK) {
        for (unsigned i = 0; i < audio::DMA_DESCRIPTORS + 2; ++i)
            if (audio::write(out_buf, DMA_BUF_LEN) != ESP_OK) { ++drain_errors; break; }
    }
    vTaskPrioritySet(nullptr, 1);
    guition::memory_report("after stress (before transport shutdown)");
    const esp_err_t shutdown = audio::end();
    for (Stage &s : stages) {
        reportf("[M3.1] voices=%u blocks=%u active_min=%u active_max=%u budget_us=%.3f render_misses=%u cycle_misses=%u write_failures=%u timeouts=%u\n",
            s.voices, s.blocks, s.active_min, s.active_max, BUDGET_US,
            s.render_misses, s.cycle_misses, s.failures, s.timeouts);
        delay(100); // allow deferred USB serial delivery without HWCDC flush/discard
        delay(10);
        print_timing("render", s.render, s.blocks);
        print_timing("write", s.write, s.blocks);
        print_timing("cycle", s.cycle, s.blocks);
        reportf("[M3.1] PCM peak_L=%ld peak_R=%ld peak_mono=%ld nonzero_frames=%u silent_blocks=%u rail_frames=%u\n",
            long(s.peak_l), long(s.peak_r), long(s.peak_mono), s.nonzero,
            s.silent_blocks, s.rails);
        delay(100); // allow deferred USB serial delivery without HWCDC flush/discard
        delay(10);
    }
    reportf("[M3.1] DMA underrun/starvation metric unavailable: I2S TX sent/queue-overflow events do not reliably identify playback starvation.\n");
    delay(100); // allow deferred USB serial delivery without HWCDC flush/discard
    delay(10);
    reportf("[M3.1] write=%s drain_errors=%u shutdown=%s result=%s (PCM/render timing + transport API only; audible unverified)\n",
        esp_err_to_name(last_error), drain_errors, esp_err_to_name(shutdown),
        pass && !drain_errors && shutdown == ESP_OK ? "PASS" : "FAIL");
    guition::memory_report("after shutdown");
    delay(100); // allow deferred USB serial delivery without HWCDC flush/discard
    vTaskDelete(nullptr);
}

void setup() {
    gpio_set_level(guition::PA_ENABLE, 0);
    gpio_set_direction(guition::PA_ENABLE, GPIO_MODE_OUTPUT);
    Serial.setTxBufferSize(4096);
    Serial.begin(115200);
    Serial.setTxTimeoutMs(1000); // deferred report must survive USB host scheduling
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
        ROTvalue[voice][9] = 3; // original sustain/decay table stays nonzero for window
        ROTvalue[voice][13] = -120 + voice * 16;
        ROTvalue[voice][10] = 127; // 5 s envelope; 3 s measurement
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
    guition::memory_report("before stress (transport ready)");
    Serial.println("[M3.1] Dry original mixer: 1/4/8/16 voices, 3 s each, length=127 (5 s), MIDI 48..63, wave 0..15, pan -120..120.");
    delay(100); // allow deferred USB serial delivery without HWCDC flush/discard
    if (xTaskCreatePinnedToCore(diagnostic_task, "synth", 8000, nullptr,
            configMAX_PRIORITIES - 1, nullptr, 0) != pdPASS) {
        audio::end();
        Serial.println("[FAIL] Could not create synth task.");
    }
}
void loop() { delay(1000); }
