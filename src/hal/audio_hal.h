#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

namespace audio {
constexpr size_t BLOCK_FRAMES = 256;
constexpr unsigned DMA_DESCRIPTORS = 4;
// Lifecycle/volume: control task only. No concurrent begin/end/volume calls.
esp_err_t begin(uint32_t sample_rate = 44100);
esp_err_t set_volume(unsigned percent);
esp_err_t end();
// One writer only; bounded DMA wait, no allocation/I2C/Serial in this path.
// On error caller must stop, not retry the full buffer (partial data may play).
esp_err_t write(const int16_t *interleaved_stereo, size_t frame_count);
}
