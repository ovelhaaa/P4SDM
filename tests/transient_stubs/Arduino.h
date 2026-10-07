#pragma once
#include <mutex>
using portMUX_TYPE = std::recursive_mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(m) (m)->lock()
#define portEXIT_CRITICAL(m) (m)->unlock()
void vTaskDelay(unsigned);
inline unsigned uxTaskGetStackHighWaterMark(void *) { return 4096; }
