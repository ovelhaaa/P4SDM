#include "storage_hal.h"
#include "esp_ldo_regulator.h"
#include "esp_timer.h"
namespace storage {
static std::atomic<State> current{State::Unavailable};
static esp_ldo_channel_handle_t rail = nullptr;
State state() { return current.load(); }
const char *status() {
  switch (state()) {
  case State::Mounting:
    return "MOUNTING";
  case State::Ready:
    return "SD READY";
  case State::CardMissing:
    return "CARD REMOVED / NO CARD";
  case State::FilesystemError:
    return "NO CARD / MOUNT ERROR";
  case State::IoError:
    return "SD I/O ERROR";
  default:
    return "SD UNAVAILABLE";
  }
}
bool mount() {
  if (state() == State::Ready)
    return true;
  current.store(State::Mounting);
  if (!rail) {
    esp_ldo_channel_config_t cfg = {};
    cfg.chan_id = 4;
    cfg.voltage_mv = 3300;
    if (esp_ldo_acquire_channel(&cfg, &rail) != ESP_OK) {
      current.store(State::Unavailable);
      return false;
    }
  }
  SD_MMC.setPowerChannel(-1); // HAL alone owns VO4; never touch display VO3.
  if (!SD_MMC.setPins(43, 44, 39, 40, 41, 42)) {
    current.store(State::Unavailable);
    return false;
  }
  int64_t start = esp_timer_get_time();
  bool ok = SD_MMC.begin("/sdcard", false, false, 20000, 4);
  current.store(ok ? State::Ready : State::FilesystemError);
  Serial.printf("[M6 SD] mounted=%d width=4 clock_khz=20000 mount_us=%lld "
                "capacity=%llu status=%s\n",
                ok, esp_timer_get_time() - start, ok ? SD_MMC.cardSize() : 0,
                status());
  return ok;
}
void unmount() {
  SD_MMC.end();
  current.store(State::Unavailable);
} // Rail retained for app lifetime.
bool probe() {
  if (state() != State::Ready)
    return false;
  uint8_t sector[512];
  if (!SD_MMC.readRAW(sector, 0)) {
    current.store(State::CardMissing);
    return false;
  }
  return true;
}
File open(const char *path) {
  return state() == State::Ready ? SD_MMC.open(path, FILE_READ) : File();
}
void io_error() { current.store(State::IoError); }
} // namespace storage
