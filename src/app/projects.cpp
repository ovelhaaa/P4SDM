#include "projects.h"
#include "../hal/storage_hal.h"
#include "esp_heap_caps.h"
#include "samples.h"
#include <new>
namespace projects {
std::atomic<Handoff> handoff{Handoff::Idle};
std::atomic<bool> restoring{false};
std::atomic<bool> view_ready{false};
std::atomic<bool> keep_resident{false};
std::atomic<unsigned> changes{0}, saved_revision{0}, revision{0},
    applied_revision{0};
std::atomic<unsigned> snapshot_blocks{0}, snapshot_max{0}, apply_blocks{0},
    apply_max{0}, fixtures{0}, fixture_errors{0};
project::State *staging = nullptr;
char references[16][96]{};
namespace {
uint8_t *bytes = nullptr;
std::atomic<Operation> operation{Operation::None};
std::atomic<bool> working{false};
char target[32]{}, current_name[32] = "UNTITLED", info[128] = "PROJECT READY";
char names[32][32]{};
std::atomic<unsigned> indexed{0};
portMUX_TYPE mutex = portMUX_INITIALIZER_UNLOCKED;
void report(const char *s) {
  portENTER_CRITICAL(&mutex);
  strlcpy(info, s, sizeof(info));
  portEXIT_CRITICAL(&mutex);
  revision.fetch_add(1);
}
void path(char *out, const char *name, unsigned slot) {
  snprintf(out, 128, "/P4SDM/PROJECTS/%s/%c.P4P", name, slot ? 'B' : 'A');
}
project::Error read_slot(const char *name, unsigned slot, project::Slot &out) {
  out = {};
  char filename[128];
  path(filename, name, slot);
  File f = storage::open(filename);
  if (!f)
    return SD_MMC.exists(filename) ? project::Error::Io
                                   : project::Error::Header;
  if (f.size() < project::header_bytes)
    return project::Error::Header;
  if (f.read(bytes, project::header_bytes) != project::header_bytes)
    return project::Error::Io;
  const auto prefix = project::inspect(bytes, project::header_bytes, *staging);
  if (prefix != project::Error::Size)
    return prefix;
  if (f.size() != project::file_bytes)
    return project::Error::Size;
  if (f.read(bytes + project::header_bytes, project::payload_bytes) !=
      project::payload_bytes)
    return project::Error::Io;
  auto e = project::inspect(bytes, project::file_bytes, *staging);
  if (e == project::Error::Ok)
    out = {true, project::u32(bytes + 12)};
  return e;
}
void scan() {
  indexed.store(0);
  File dir = storage::open("/P4SDM/PROJECTS");
  if (!dir) {
    report("NO PROJECTS");
    return;
  }
  for (unsigned entries = 0; entries < 128; ++entries) {
    File f = dir.openNextFile();
    if (!f)
      break;
    const char *n = f.name();
    if (f.isDirectory() && strlen(n) < 32 && project::name_valid(n)) {
      unsigned i = indexed.load();
      if (i == 32)
        break;
      portENTER_CRITICAL(&mutex);
      strlcpy(names[i], n, 32);
      portEXIT_CRITICAL(&mutex);
      indexed.store(i + 1);
    }
    vTaskDelay(1);
  }
  report(indexed ? "SELECT PROJECT" : "NO PROJECTS");
}
void wait(Handoff phase) {
  while (handoff.load(std::memory_order_acquire) != phase)
    vTaskDelay(1);
}
void capture() {
  strlcpy(staging->name, target, 32);
  handoff.store(Handoff::Snapshot, std::memory_order_release);
  wait(Handoff::SnapshotReady);
  handoff.store(Handoff::Idle, std::memory_order_release);
}
bool save() {
  capture();
  project::Slot slots[2];
  auto a = read_slot(target, 0, slots[0]);
  auto b = read_slot(target, 1, slots[1]);
  if (a == project::Error::Io || b == project::Error::Io || !storage::probe()) {
    report("PROJECT READ FAILED - PREVIOUS COPY KEPT");
    return false;
  }
  if (a == project::Error::Future || b == project::Error::Future) {
    report("PROJECT VERSION TOO NEW - PREVIOUS COPY KEPT");
    return false;
  }
  const auto next = project::plan(slots[0], slots[1]);
  const unsigned slot = next.slot;
  const uint32_t generation = next.generation;
  if (!project::encode(*staging, generation, bytes, project::file_bytes)) {
    report("INVALID SNAPSHOT");
    return false;
  }
  const uint32_t expected_crc = project::u32(bytes + 16);
  const uint64_t total = SD_MMC.totalBytes(), used = SD_MMC.usedBytes();
  if (!total || used > total || total - used < project::file_bytes + 65536) {
    report("INSUFFICIENT SPACE");
    return false;
  }
  SD_MMC.mkdir("/P4SDM");
  SD_MMC.mkdir("/P4SDM/PROJECTS");
  char dirname[128];
  snprintf(dirname, sizeof(dirname), "/P4SDM/PROJECTS/%s", target);
  if (!SD_MMC.exists(dirname) && !SD_MMC.mkdir(dirname)) {
    report("PROJECT DIRECTORY WRITE FAILED");
    return false;
  }
  char filename[128];
  path(filename, target, slot);
  // Only the non-newest slot may be removed/truncated. The good slot survives.
  if (SD_MMC.exists(filename) && !SD_MMC.remove(filename)) {
    report("PROJECT WRITE FAILED");
    return false;
  }
  File f = SD_MMC.open(filename, FILE_WRITE);
  if (!f) {
    report("PROJECT WRITE FAILED");
    return false;
  }
  bool ok = f.write(bytes, project::file_bytes) == project::file_bytes;
  f.flush();
  f.close();
  if (!ok) {
    report("PROJECT WRITE FAILED - PREVIOUS COPY KEPT");
    return false;
  }
  project::Slot verify;
  if (read_slot(target, slot, verify) != project::Error::Ok ||
      verify.generation != generation ||
      project::u32(bytes + 16) != expected_crc) {
    report("PROJECT VERIFY FAILED - PREVIOUS COPY KEPT");
    return false;
  }
  remember_name(target);
  saved_revision.store(applied_revision.load());
  report("PROJECT SAVED");
  return true;
}
bool load() {
  project::Slot slots[2];
  auto a = read_slot(target, 0, slots[0]);
  auto b = read_slot(target, 1, slots[1]);
  if (a == project::Error::Future || b == project::Error::Future) {
    report("PROJECT VERSION TOO NEW - CURRENT PROJECT KEPT");
    return false;
  }
  int chosen = project::newest(slots[0], slots[1]);
  if (chosen < 0) {
    char text[128];
    auto e = a == project::Error::Future || b == project::Error::Future
                 ? project::Error::Future
                 : a;
    snprintf(text, sizeof(text), "%s - CURRENT PROJECT KEPT",
             project::message(e));
    report(text);
    return false;
  }
  project::Slot check;
  if (read_slot(target, chosen, check) != project::Error::Ok ||
      project::decode(bytes, project::file_bytes, *staging) !=
          project::Error::Ok ||
      strcmp(staging->name, target)) {
    report("LOAD FAILED - CURRENT PROJECT KEPT");
    return false;
  }
  return true;
}
void activate() {
  keep_resident.store(false);
  view_ready.store(false);
  restoring.store(true, std::memory_order_release);
  handoff.store(Handoff::Apply, std::memory_order_release);
  wait(Handoff::ApplyReady);
  while (!view_ready.load(std::memory_order_acquire))
    vTaskDelay(1);
  // Audio detached active PCM and UI previews before the storage task frees it.
  samples::retire_project_samples();
  unsigned missing = 0, total = 0, done = 0;
  for (auto &ref : staging->references)
    total += ref[0] != 0;
  for (unsigned t = 0; t < 16; ++t)
    if (staging->references[t][0]) {
      char text[128];
      snprintf(text, sizeof(text), "LOADING SAMPLES %u/%u", ++done, total);
      report(text);
      if (!samples::restore(t, staging->references[t]))
        ++missing;
    }
  remember_name(staging->name);
  saved_revision.store(changes.load());
  handoff.store(Handoff::Idle, std::memory_order_release);
  restoring.store(false, std::memory_order_release);
  char text[128];
  snprintf(text, sizeof(text), "PROJECT READY / %u MISSING", missing);
  report(text);
}
} // namespace
void initialize() {
  staging = static_cast<project::State *>(heap_caps_malloc(
      sizeof(project::State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  bytes = static_cast<uint8_t *>(heap_caps_malloc(
      project::file_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!staging || !bytes) {
    report("PROJECT STAGING ALLOCATION FAILED");
    return;
  }
  new (staging) project::State;
  project::defaults(*staging);
}
bool request(Operation op, const char *name) {
  if (!staging || !bytes || samples::busy() || working.exchange(true))
    return false;
  if (name && !project::name_valid(name)) {
    working.store(false);
    report("INVALID PROJECT NAME");
    return false;
  }
  if (name)
    strlcpy(target, name, sizeof(target));
  else
    current(target, sizeof(target));
  operation.store(op, std::memory_order_release);
  revision.fetch_add(1);
  return true;
}
void poll() {
  auto op = operation.exchange(Operation::None, std::memory_order_acquire);
  if (op == Operation::None)
    return;
  if (op == Operation::New) {
    project::defaults(*staging);
    activate();
  } else if (op == Operation::Fixture) {
    capture();
    bool ok = project::encode(*staging, 1, bytes, project::file_bytes) &&
              project::decode(bytes, project::file_bytes, *staging) ==
                  project::Error::Ok;
    fixtures.fetch_add(1);
    fixture_errors.fetch_add(!ok);
    if (ok) {
      keep_resident.store(true);
      view_ready.store(false);
      restoring.store(true);
      handoff.store(Handoff::Apply);
      wait(Handoff::ApplyReady);
      while (!view_ready.load())
        vTaskDelay(1);
      handoff.store(Handoff::Idle);
      restoring.store(false);
    }
    report(ok ? "RAM PROJECT ROUNDTRIP READY" : "RAM PROJECT ROUNDTRIP FAILED");
  } else {
    bool ready = storage::state() == storage::State::Ready && storage::probe();
    if (!ready) {
      storage::unmount();
      ready = storage::mount();
    }
    if (!ready)
      report("NO SD - CURRENT PROJECT KEPT");
    else if (op == Operation::Scan)
      scan();
    else if (op == Operation::Save || op == Operation::SaveAs)
      save();
    else if (op == Operation::Load && load())
      activate();
  }
  working.store(false, std::memory_order_release);
  revision.fetch_add(1);
}
bool busy() { return working.load(); }
bool locked() {
  auto h = handoff.load(std::memory_order_acquire);
  return restoring.load() || h == Handoff::Snapshot ||
         h == Handoff::SnapshotReady;
}
bool dirty() { return changes.load() != saved_revision.load(); }
void edited(app::Kind k) {
  if (project::musical(k))
    changes.fetch_add(1);
}
void assigned(unsigned t, const char *name) {
  if (t < 16) {
    strlcpy(references[t], name, 96);
    if (!restoring.load())
      changes.fetch_add(1);
  }
}
void remember_name(const char *name) {
  portENTER_CRITICAL(&mutex);
  strlcpy(current_name, name, 32);
  portEXIT_CRITICAL(&mutex);
  revision.fetch_add(1);
}
void current(char *out, unsigned n) {
  portENTER_CRITICAL(&mutex);
  strlcpy(out, current_name, n);
  portEXIT_CRITICAL(&mutex);
}
void status(char *out, unsigned n) {
  portENTER_CRITICAL(&mutex);
  strlcpy(out, info, n);
  portEXIT_CRITICAL(&mutex);
}
unsigned count() { return indexed.load(); }
void describe(unsigned i, char *out, unsigned n) {
  portENTER_CRITICAL(&mutex);
  strlcpy(out, i < indexed ? names[i] : "NO PROJECTS", n);
  portEXIT_CRITICAL(&mutex);
}
void measured(bool snapshot, unsigned us) {
  auto &blocks = snapshot ? snapshot_blocks : apply_blocks;
  auto &max = snapshot ? snapshot_max : apply_max;
  blocks.fetch_add(1);
  max.store(std::max(max.load(), us));
}
} // namespace projects
