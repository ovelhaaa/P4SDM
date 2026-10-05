#include "audio_hal.h"
#include "guition_board.h"
#include "pcm_mono.h"
#include <Arduino.h>
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "es8311.h"

namespace audio {
static i2s_chan_handle_t tx = nullptr;
static es8311_handle_t codec = nullptr;
static bool enabled = false;
static bool ready = false;
alignas(4) static int16_t mono_buffer[BLOCK_FRAMES * 2];

static esp_err_t report(const char *stage, esp_err_t err) {
    Serial.printf("[AUDIO] %s: %s (0x%x)\n", stage, esp_err_to_name(err), unsigned(err));
    return err;
}
static esp_err_t send(const int16_t *data, size_t frames) {
    const auto *bytes = reinterpret_cast<const uint8_t *>(data);
    const size_t length = frames * 2 * sizeof(int16_t);
    size_t offset = 0;
    while (offset < length) {
        size_t written = 0;
        const esp_err_t err = i2s_channel_write(tx, bytes + offset, length - offset, &written, 100);
        offset += written;
        if (err != ESP_OK) return err;
        if (!written) return ESP_FAIL;
    }
    return ESP_OK;
}
esp_err_t end() {
    ready = false;
    esp_err_t first = gpio_set_level(guition::PA_ENABLE, 0);
    auto collect = [&first](esp_err_t err) { if (first == ESP_OK) first = err; };
    if (codec) {
        collect(es8311_voice_mute(codec, true));
        es8311_delete(codec);
        codec = nullptr;
    }
    if (tx) {
        if (enabled) collect(i2s_channel_disable(tx));
        collect(i2s_del_channel(tx));
        tx = nullptr;
    }
    enabled = false;
    // Shared I2C bus remains alive for other peripherals.
    return first;
}
esp_err_t set_volume(unsigned percent) {
    if (!codec || percent > 100) return ESP_ERR_INVALID_ARG;
    return es8311_voice_volume_set(codec, int(percent), nullptr);
}
esp_err_t begin(uint32_t sample_rate) {
    if (tx || codec) return ESP_ERR_INVALID_STATE;
    if (sample_rate != 44100) return ESP_ERR_INVALID_ARG;
    auto check = [](const char *stage, esp_err_t err) {
        if (err != ESP_OK) {
            report(stage, err);
            const esp_err_t cleanup = end();
            if (cleanup != ESP_OK) report("cleanup", cleanup);
        }
        return err;
    };
#define AUDIO_CHECK(stage, call) do { esp_err_t e = check(stage, (call)); if (e != ESP_OK) return e; } while (0)
    AUDIO_CHECK("PA direction", gpio_set_direction(guition::PA_ENABLE, GPIO_MODE_OUTPUT));
    AUDIO_CHECK("PA off", gpio_set_level(guition::PA_ENABLE, 0));
    AUDIO_CHECK("I2C init", guition::i2c_begin());
    Serial.println("[AUDIO] I2C init OK");
    AUDIO_CHECK("ES8311 probe 0x18", i2c_master_probe(guition::i2c_bus(), guition::CODEC_ADDRESS, 100));
    Serial.println("[AUDIO] ES8311 found at 0x18");

    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = DMA_DESCRIPTORS;
    channel.dma_frame_num = BLOCK_FRAMES;
    channel.auto_clear = true;
    AUDIO_CHECK("I2S channel", i2s_new_channel(&channel, &tx, nullptr));
    i2s_std_config_t config = {};
    config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    config.gpio_cfg.mclk = guition::MCLK;
    config.gpio_cfg.bclk = guition::BCLK;
    config.gpio_cfg.ws = guition::WS;
    config.gpio_cfg.dout = guition::DOUT;
    config.gpio_cfg.din = I2S_GPIO_UNUSED;
    AUDIO_CHECK("I2S std init", i2s_channel_init_std_mode(tx, &config));
    // Preload all descriptors with zero before clocks start.
    memset(mono_buffer, 0, sizeof(mono_buffer));
    for (unsigned i = 0; i < DMA_DESCRIPTORS; ++i) {
        size_t loaded = 0;
        AUDIO_CHECK("I2S preload", i2s_channel_preload_data(tx, mono_buffer, sizeof(mono_buffer), &loaded));
        AUDIO_CHECK("I2S preload length", loaded == sizeof(mono_buffer) ? ESP_OK : ESP_FAIL);
    }
    AUDIO_CHECK("I2S enable", i2s_channel_enable(tx));
    enabled = true;
    Serial.println("[AUDIO] I2S init 44100/16 OK, MCLK=11289600 Hz");

    codec = es8311_create(guition::i2c_bus(), guition::CODEC_ADDRESS);
    AUDIO_CHECK("ES8311 create", codec ? ESP_OK : ESP_ERR_NO_MEM);
    es8311_clock_config_t clock = {};
    clock.mclk_from_mclk_pin = true;
    clock.mclk_frequency = sample_rate * 256;
    clock.sample_frequency = sample_rate;
    AUDIO_CHECK("ES8311 playback", es8311_init(codec, &clock, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16));
    AUDIO_CHECK("ES8311 mute", es8311_voice_mute(codec, true));
    AUDIO_CHECK("ES8311 volume", set_volume(80)); // Register 203: attenuated, not 80% speaker power.
    AUDIO_CHECK("ES8311 fade", es8311_voice_fade(codec, ES8311_FADE_32LRCK));
    Serial.println("[AUDIO] ES8311 playback configured (capture serial port disabled)");
    for (unsigned i = 0; i < 20; ++i) AUDIO_CHECK("I2S settling silence", send(mono_buffer, BLOCK_FRAMES));
    AUDIO_CHECK("PA enable", gpio_set_level(guition::PA_ENABLE, 1));
    Serial.println("[AUDIO] PA GPIO11 enabled");
    // Feed silence during amplifier wake-up; do not let DMA replay tone data.
    for (unsigned i = 0; i < 8; ++i) AUDIO_CHECK("PA settling silence", send(mono_buffer, BLOCK_FRAMES));
    AUDIO_CHECK("ES8311 unmute", es8311_voice_mute(codec, false));
    ready = true;
#undef AUDIO_CHECK
    return ESP_OK;
}
esp_err_t write(const int16_t *stereo, size_t frame_count) {
    if (!ready) return ESP_ERR_INVALID_STATE;
    if (!stereo || !frame_count) return ESP_ERR_INVALID_ARG;
    while (frame_count) {
        const size_t count = frame_count < BLOCK_FRAMES ? frame_count : BLOCK_FRAMES;
        downmix(stereo, mono_buffer, count);
        const esp_err_t err = send(mono_buffer, count);
        if (err != ESP_OK) {
            ready = false;
            gpio_set_level(guition::PA_ENABLE, 0); // immediate failure mute, no I2C
            return err;
        }
        stereo += count * 2;
        frame_count -= count;
    }
    return ESP_OK;
}
}
