#pragma once
#include <Arduino.h>
#include <SD_MMC.h>
#include <atomic>
namespace storage {
enum class State {
  Unavailable,
  Mounting,
  Ready,
  CardMissing,
  FilesystemError,
  IoError
};
State state();
const char *status();
bool mount();
void unmount();
bool probe();
File open(const char *path);
void io_error();
} // namespace storage
