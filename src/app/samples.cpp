#include "samples.h"
#include "../hal/storage_hal.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
namespace samples {
sampler::Voice voices[16];
sampler::Transfer transfer;
namespace {
constexpr unsigned max_files = 128, max_entries = 512,
                   reserve = 4 * 1024 * 1024;
char names[max_files][96]{}, assigned_names[16][96]{}, info[128] = "STARTING";
std::atomic<unsigned> indexed{0};
std::atomic<bool> booted{false};
std::atomic<uint16_t> assignments{0};
std::atomic<unsigned> changed{0};
std::atomic<int> job{-1};
std::atomic<bool> working{false};
unsigned target = 0;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
size_t payload = 0;
void report(const char *s) {
  portENTER_CRITICAL(&lock);
  strlcpy(info, s, sizeof(info));
  portEXIT_CRITICAL(&lock);
  changed.fetch_add(1);
}
void destroy(sampler::Sample *s) {
  if (s) {
    payload -= s->allocation;
    heap_caps_free(s->data);
    delete s;
  }
}
void scan() {
  int64_t start = esp_timer_get_time();
  File dir = storage::open("/P4SDM/SAMPLES");
  if (!dir || !dir.isDirectory()) {
    report("Missing /P4SDM/SAMPLES");
    return;
  }
  unsigned entries = 0;
  bool limited = false;
  while (entries < max_entries) {
    File f = dir.openNextFile();
    if (!f)
      break;
    ++entries;
    String n = f.name();
    if (n.length() >= 96)
      limited = true;
    if (!f.isDirectory() && n.length() < 96 && n.length() > 4 &&
        n.substring(n.length() - 4).equalsIgnoreCase(".wav")) {
      if (indexed == max_files) {
        limited = true;
        break;
      }
      unsigned i = indexed.load();
      strlcpy(names[i], n.c_str(), 96);
      indexed.store(i + 1, std::memory_order_release);
    }
  }
  limited |= entries == max_entries;
  char s[128];
  snprintf(s, sizeof(s), "%u WAV files%s", indexed.load(),
           limited ? " (INDEX LIMIT)" : "");
  report(s);
  Serial.printf("[M6 index] files=%u entries=%u limited=%d scan_us=%lld\n",
                indexed.load(), entries, limited, esp_timer_get_time() - start);
}
void load(int index) {
  if (!storage::probe()) {
    report(storage::status());
    return;
  }
  char path[128];
  snprintf(path, sizeof(path), "/P4SDM/SAMPLES/%s", names[index]);
  File file = storage::open(path);
  if (!file) {
    report("Cannot open WAV");
    return;
  }
  unsigned file_bytes = file.size();
  sampler::Wav wav;
  int64_t start = esp_timer_get_time();
  auto read = [&](uint64_t p, uint8_t *b, unsigned n) {
    return p <= file.size() && n <= file.size() - p && file.seek(uint32_t(p)) &&
           file.read(b, n) == n;
  };
  const char *error = sampler::parse(file.size(), read, wav);
  if (error) {
    report(error);
    return;
  }
  size_t bytes = size_t(wav.frames) * 2;
  size_t free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
         largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  if (!sampler::fits_budget(bytes, free, largest)) {
    report("Sample exceeds PSRAM budget");
    return;
  }
  auto *s = new (std::nothrow) sampler::Sample;
  if (!s) {
    report("Metadata allocation failed");
    return;
  }
  s->data =
      (int16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s->data) {
    delete s;
    report("PSRAM allocation failed");
    return;
  }
  s->original_channels = wav.channels;
  s->rate = wav.rate;
  s->frames = wav.frames;
  s->allocation = bytes;
  payload += bytes;
  size_t peak_payload = payload;
  strlcpy(s->name, names[index], sizeof(s->name));
  uint8_t chunk[4096];
  unsigned frame = 0;
  if (!file.seek(wav.offset)) {
    destroy(s);
    storage::io_error();
    report("WAV seek failed");
    return;
  }
  while (frame < wav.frames) {
    unsigned frames = std::min<uint32_t>(1024, wav.frames - frame),
             n = frames * wav.channels * 2;
    if (file.read(chunk, n) != n) {
      destroy(s);
      storage::io_error();
      report("Short WAV read / card removed");
      return;
    }
    for (unsigned j = 0; j < frames; ++j) {
      s->data[frame + j] =
          sampler::decode_mono(chunk + j * wav.channels * 2, wav.channels);
    }
    frame += frames;
    vTaskDelay(1);
  }
  uint64_t read_us = esp_timer_get_time() - start;
  file.close();
  transfer.publish(s, target);
  while (!transfer.acknowledged.load(std::memory_order_acquire))
    vTaskDelay(1);
  destroy(transfer.retired.exchange(nullptr));
  portENTER_CRITICAL(&lock);
  strlcpy(assigned_names[target], s->name, 96);
  portEXIT_CRITICAL(&lock);
  assignments.fetch_or(uint16_t(1u << target));
  report("SAMPLE READY");
  Serial.printf("[M6 read] bytes=%u read_us=%llu bytes_per_second=%llu "
                "original_channels=%u rate=%u total_psram=%u\n",
                unsigned(wav.bytes), read_us,
                read_us ? uint64_t(wav.bytes) * 1000000 / read_us : 0,
                unsigned(wav.channels), unsigned(wav.rate),
                unsigned(heap_caps_get_total_size(MALLOC_CAP_SPIRAM)));
  Serial.printf(
      "[M6 load] track=%u file_bytes=%u pcm_bytes=%u load_publish_us=%lld "
      "payload=%u transient=%u peak_payload=%u ps_free=%u largest=%u "
      "internal=%u "
      "display=2304000 fx_delay=352800 reserve=%u\n",
      target, file_bytes, unsigned(bytes), esp_timer_get_time() - start,
      unsigned(payload), unsigned(bytes), unsigned(peak_payload),
      unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
      unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),
      unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)), reserve);
}
void worker(void *) {
  Serial.printf("[M6 budget] total=%u display=2304000 fx_delay=352800 "
                "samples=0 reserve=%u free=%u largest=%u internal=%u\n",
                unsigned(heap_caps_get_total_size(MALLOC_CAP_SPIRAM)), reserve,
                unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),
                unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
  if (storage::mount())
    scan();
  else
    report(storage::status());
  booted.store(true, std::memory_order_release);
  for (;;) {
    int index = job.exchange(-1, std::memory_order_acquire);
    if (index >= 0) {
      report("LOADING");
      load(index);
      working.store(false, std::memory_order_release);
    }
    static uint32_t due = 0;
    if (millis() > due && !working.load()) {
      due = millis() + 3000;
      if (storage::state() == storage::State::Ready && !storage::probe())
        report(storage::status());
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
} // namespace
bool initialized() { return booted.load(std::memory_order_acquire); }
void start() {
  if (xTaskCreatePinnedToCore(worker, "sample_storage", 7000, nullptr, 1,
                              nullptr, 0) != pdPASS) {
    report("STORAGE TASK ALLOCATION FAILED");
    booted.store(true);
  }
}
bool request(unsigned track, int index) {
  if (track >= 16 || index < 0 || unsigned(index) >= indexed ||
      storage::state() != storage::State::Ready || working.exchange(true))
    return false;
  target = track;
  job.store(index, std::memory_order_release);
  return true;
}
bool has_sample(unsigned t) {
  portENTER_CRITICAL(&lock);
  bool loaded = assigned_names[t][0];
  portEXIT_CRITICAL(&lock);
  return loaded;
}
bool assigned(unsigned t) {
  return assignments.fetch_and(uint16_t(~(1u << t))) & (1u << t);
}
unsigned count() { return indexed.load(); }
bool busy() { return working.load(); }
unsigned revision() { return changed.load(); }
void describe(int i, char *d, unsigned n) {
  portENTER_CRITICAL(&lock);
  strlcpy(d, i >= 0 && unsigned(i) < indexed ? names[i] : "NO WAV FILES", n);
  portEXIT_CRITICAL(&lock);
}
void track_name(unsigned t, char *d, unsigned n) {
  portENTER_CRITICAL(&lock);
  strlcpy(d, assigned_names[t][0] ? assigned_names[t] : "SYNTH", n);
  portEXIT_CRITICAL(&lock);
}
void message(char *d, unsigned n) {
  portENTER_CRITICAL(&lock);
  strlcpy(d, info, n);
  portEXIT_CRITICAL(&lock);
}
} // namespace samples
