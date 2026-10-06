#pragma once
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "esp_heap_caps.h"

// Startup/control context only. Guition requires deterministic PSRAM placement;
// retain the original ordinary-heap fallback on Tab5.
inline void *fx_calloc(size_t count, size_t size) {
    void *p = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#if !P4SDM_HEADLESS
    if (!p) p = calloc(count, size);
#endif
    return p;
}

class FxFloatStorage {
    float *data_ = nullptr;
public:
    FxFloatStorage() = default;
    FxFloatStorage(const FxFloatStorage &) = delete;
    FxFloatStorage &operator=(const FxFloatStorage &) = delete;
    ~FxFloatStorage() { release(); }
    void release() { free(data_); data_ = nullptr; }
    bool allocate(size_t count) {
        release();
        data_ = static_cast<float *>(fx_calloc(count, sizeof(float)));
        return data_ != nullptr;
    }
    void clear(size_t count) { if (data_) memset(data_, 0, count * sizeof(float)); }
    float &operator[](size_t index) { return data_[index]; }
};
