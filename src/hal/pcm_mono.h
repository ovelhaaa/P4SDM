#pragma once
#include <stddef.h>
#include <stdint.h>

namespace audio {
// Frames, not scalar samples. Input may equal output; stereo engine untouched.
inline void downmix(const int16_t *input, int16_t *output, size_t frames) {
    for (size_t i = 0; i < frames; ++i) {
        const int32_t sum = int32_t(input[2*i]) + int32_t(input[2*i+1]);
        const int16_t mono = static_cast<int16_t>(sum / 2);
        output[2*i] = mono;
        output[2*i+1] = mono;
    }
}
}
