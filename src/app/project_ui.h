#pragma once
#include "projects.h"
#include <cstdio>
namespace project {
// Captured intent survives changes to selection/dirty state while confirming.
struct Workflow {
  enum class Mode { Home, Browser, Naming, Confirm } mode = Mode::Home;
  projects::Operation pending = projects::Operation::None;
  char target[32]{};
  unsigned selection = 0, generated = 1;
  void generated_name() {
    snprintf(target, sizeof(target), "PROJECT_%04u", generated);
  }
  void cancel() {
    mode = Mode::Home;
    pending = projects::Operation::None;
  }
  bool confirm(projects::Operation op, const char *name, bool needed) {
    pending = op;
    if (name) {
      memset(target, 0, sizeof(target));
      strncpy(target, name, sizeof(target) - 1);
    }
    if (needed) {
      mode = Mode::Confirm;
      return false;
    }
    return true;
  }
};
} // namespace project
