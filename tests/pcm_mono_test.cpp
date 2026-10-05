#include "../src/hal/pcm_mono.h"
#include <cassert>
#include <climits>

int main() {
    const int16_t stereo[] = {
        INT16_MAX, INT16_MAX, INT16_MIN, INT16_MIN,
        INT16_MIN, INT16_MAX, 20000, 0, 0, -20000,
        20000, -20000, -3, 0, 3, 0
    };
    const int16_t expected[] = {32767, -32768, 0, 10000, -10000, 0, -1, 1};
    int16_t output[18] = {};
    output[16] = 1234;
    output[17] = -1234;
    audio::downmix(stereo, output, 8);
    for (size_t i = 0; i < 8; ++i) {
        assert(output[2*i] == expected[i]);
        assert(output[2*i+1] == expected[i]);
    }
    assert(output[16] == 1234 && output[17] == -1234);
    audio::downmix(output, output, 8);
    for (size_t i = 0; i < 8; ++i) assert(output[2*i] == expected[i]);
    audio::downmix(nullptr, nullptr, 0);
}
