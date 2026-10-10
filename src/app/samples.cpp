#include "samples.h"
#include "../hal/storage_hal.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "projects.h"
#include "qualification.h"
#include "transient_service.h"
#include "pcm_placement.h"
#if P4SDM_PCM_PLACEMENT
#include "pcm_allocation.h"
#endif
#if P4SDM_M213_DIAGNOSTICS
#include "sample_layout_diagnostics.h"
#endif
namespace samples {
std::atomic<unsigned> waveform_builds{0}, waveform_build_max{0},
    waveform_large_us{0};
sampler::Voice voices[16];
sampler::Transfer transfer;
std::atomic<unsigned> qualification_phase{0};
std::atomic<bool> load_active{false};
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
const sampler::Sample *previews[16]{};
sampler::Sample *owned[16]{}, *project_retired[16]{};
#if P4SDM_PCM_PLACEMENT
struct LoadedSample : sampler::Sample { sampler::PcmAllocation pcm; };
#endif
void report(const char *s) {
  portENTER_CRITICAL(&lock);
  strlcpy(info, s, sizeof(info));
  portEXIT_CRITICAL(&lock);
  changed.fetch_add(1);
}
void destroy(sampler::Sample *s) {
  if (s) {
    payload -= s->allocation;
#if P4SDM_PCM_PLACEMENT
    auto *loaded = static_cast<LoadedSample *>(s);
    loaded->pcm.release([](void *base) { heap_caps_free(base); });
    delete loaded;
#else
    heap_caps_free(s->data);
    delete s;
#endif
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
  Serial.printf("[M6 index] files=%u entries=%u limited=%d scan_us=%lld "
                "ps_free=%u largest=%u internal=%u\n",
                indexed.load(), entries, limited, esp_timer_get_time() - start,
                unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),
                unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
}
bool load_name(const char *name) {
  struct Interval {
    Interval() { load_active.store(true); }
    ~Interval() { load_active.store(false); }
  } interval;
  if (!storage::probe()) {
    report(storage::status());
    return false;
  }
  char path[128];
  if (!project::reference_valid(name) || !name[0])
    return false;
  snprintf(path, sizeof(path), "/P4SDM/SAMPLES/%s", name);
  File file = storage::open(path);
  if (!file) {
    report("Cannot open WAV");
    return false;
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
    return false;
  }
  size_t bytes = size_t(wav.frames) * 2;
  size_t allocation_bytes = bytes;
#if P4SDM_PCM_PLACEMENT
  if (!sampler::PcmAllocation::request(bytes, P4SDM_PCM_PLACEMENT, target, allocation_bytes))
    return false;
#endif
  size_t free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
         largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  // PCM limit stays 4 MiB; padding must also fit the same reserve/block check.
  if (!sampler::fits_budget(bytes, free, largest) || allocation_bytes > largest ||
      allocation_bytes > free - reserve) {
    report("Sample exceeds PSRAM budget");
    return false;
  }
#if P4SDM_PCM_PLACEMENT
  auto *s = new (std::nothrow) LoadedSample;
#else
  auto *s = new (std::nothrow) sampler::Sample;
#endif
  if (!s) {
    report("Metadata allocation failed");
    return false;
  }
#if P4SDM_PCM_PLACEMENT
  if (s->pcm.acquire(bytes, P4SDM_PCM_PLACEMENT, target, [](size_t n) {
        return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      })) s->data = s->pcm.data;
#else
  s->data =
      (int16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
  if (!s->data) {
    delete s;
    report("PSRAM allocation failed");
    return false;
  }
#if P4SDM_PCM_PLACEMENT
  // Include allocator rounding and possible metadata fallback in the reserve,
  // rather than assuming requested padding equals the actual heap decrease.
  if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < reserve) {
    s->pcm.release([](void *base) { heap_caps_free(base); });
    delete s;
    report("Sample exceeds PSRAM reserve after allocation");
    return false;
  }
#endif
  s->original_channels = wav.channels;
  s->rate = wav.rate;
  s->frames = wav.frames;
  s->allocation = allocation_bytes;
  payload += allocation_bytes;
  size_t peak_payload = payload;
  strlcpy(s->name, name, sizeof(s->name));
  // Browser and project restoration loads share the single storage owner.
  // Keep its fixed read buffer off the 7000-byte task stack: restoring a WAV
  // adds the project activation frames above this loader and the SD driver.
  alignas(4) static uint8_t chunk[4096];
  unsigned frame = 0;
  if (!file.seek(wav.offset)) {
    destroy(s);
    storage::io_error();
    report("WAV seek failed");
    return false;
  }
  while (frame < wav.frames) {
    unsigned frames = std::min<uint32_t>(1024, wav.frames - frame),
             n = frames * wav.channels * 2;
    if (file.read(chunk, n) != n) {
      destroy(s);
      storage::io_error();
      report("Short WAV read / card removed");
      return false;
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
  analyze(*s);
  transfer.publish(s, target);
  while (!transfer.acknowledged.load(std::memory_order_acquire))
    vTaskDelay(1);
  publish_preview(target, s);
  destroy(transfer.retired.exchange(nullptr));
  owned[target] = s;
  portENTER_CRITICAL(&lock);
  strlcpy(assigned_names[target], s->name, 96);
  portEXIT_CRITICAL(&lock);
  if (!projects::restoring.load())
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
  Serial.printf(
      "[M6.1 replacement] retired_clear=%d ps_delta=%lld status=READY\n",
      transfer.retired.load() == nullptr,
      (long long)free - (long long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#if P4SDM_M213_DIAGNOSTICS
  sample_layout::address(target, *s,
#if P4SDM_PCM_PLACEMENT
    s->pcm.base,
#else
    s->data,
#endif
    [](const char *format, auto... args) { Serial.printf(format,args...); });
#endif
  return true;
}
void load(int index) { load_name(names[index]); }
#if P4SDM_SAMPLER_QUALIFICATION
int fixture(const char *name) {
  for (unsigned i = 0; i < indexed; ++i)
    if (!strcmp(names[i], name))
      return int(i);
  return -1;
}
void qualify() {
  const char *required[] = {"short_mono.wav", "stereo.wav", "long_mono.wav",
                            "unsupported_depth.wav", "truncated.wav"};
  for (auto name : required)
    if (fixture(name) < 0) {
      Serial.printf("[M6.1 qualification] SKIP missing=%s PHYSICAL SD "
                    "VALIDATION PENDING\n",
                    name);
      return;
    }
  size_t baseline_free = 0, baseline_largest = 0;
  for (unsigned i = 0; i < 6; ++i) {
    target = 0;
    auto *before = transfer.active[0];
    const char *name = sampler::qualification_fixture(i);
    load(fixture(name));
    bool accepted =
        transfer.active[0] && !strcmp(transfer.active[0]->name, name);
    bool preserved = transfer.active[0] == before;
    size_t free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    Serial.printf("[M6.1 fixture] name=%s accepted=%d preserved=%d "
                  "retired_clear=%d payload=%u free=%u largest=%u\n",
                  name, accepted, preserved, transfer.retired.load() == nullptr,
                  unsigned(payload), unsigned(free), unsigned(largest));
    if (i == 0) {
      baseline_free = free;
      baseline_largest = largest;
    }
    if (i == 3)
      Serial.printf("[M6.1 equal_payload] free_loss=%lld largest_loss=%lld "
                    "suspect=%d tolerance=4096\n",
                    (long long)baseline_free - (long long)free,
                    (long long)baseline_largest - (long long)largest,
                    sampler::memory_loss_suspect(baseline_free, free,
                                                 baseline_largest, largest));
    if ((i < 4 && !accepted) || (i >= 4 && (accepted || !preserved))) {
      Serial.println("[M6.1 qualification] FAIL fixture / assignment");
      return;
    }
  }
  for (target = 0; target < 16; ++target) {
    load(fixture("long_mono.wav"));
    if (!transfer.active[target] ||
        strcmp(transfer.active[target]->name, "long_mono.wav")) {
      Serial.println("[M6.1 qualification] FAIL resident setup");
      return;
    }
  }
  for (unsigned phase = 1; phase <= 6; ++phase) {
    qualification_phase.store(phase, std::memory_order_release);
    // Baseline and SD-load windows use the same unity pattern.
    if (phase == 6) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      target = 0;
      load(fixture("stereo.wav"));
      load(fixture("long_mono.wav"));
    }
    vTaskDelay(pdMS_TO_TICKS(12000));
  }
  qualification_phase.store(0);
  Serial.println("[M6.1 qualification] AUTOMATION COMPLETE; inspect metrics; "
                 "audible/removal checks MANUAL");
}
#endif
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
#if P4SDM_SAMPLER_QUALIFICATION
  if (storage::state() == storage::State::Ready) {
    working.store(true);
    qualify();
    working.store(false);
  } else
    Serial.println("[M6.1 qualification] PHYSICAL SD VALIDATION PENDING (no "
                   "mounted card)");
#endif
  for (;;) {
#if P4SDM_M213_MONITOR
    // Storage task only; no producer can retire/replace PCM during this sweep.
    // This intentionally adds competing PSRAM reads during sustained playback.
    static uint32_t monitor_due=0, hashes[16]{};
    static const sampler::Sample *sources[16]{};
    if(millis()>=monitor_due) {
      monitor_due=millis()+30000;
      unsigned resident=0, corrupt=0;
      uint32_t digest=2166136261u;
      for(unsigned t=0;t<16;++t) {
        const auto *s=previews[t]; if(!s || !s->data) continue;
        ++resident; uint32_t hash=2166136261u;
        const volatile int16_t *pcm=s->data;
        for(unsigned n=0;n<s->frames;++n) {
          hash=(hash^uint16_t(pcm[n]))*16777619u;
          if((n&4095)==4095) vTaskDelay(1);
        }
        if(sources[t]==s && hashes[t]!=hash) ++corrupt;
        sources[t]=s;hashes[t]=hash;digest=(digest^hash)*16777619u;
      }
      Serial.printf("[M213 monitor] ms=%u resident=%u corrupt=%u digest=%u free=%u largest=%u internal=%u\n",
        millis(),resident,corrupt,digest,unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
        unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    }
#endif
    transients::poll();
#if P4SDM_TRANSIENT_STRESS
    transients::qualification_poll();
#endif
    projects::poll();
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
void analyze(sampler::Sample &s, bool responsive) {
  const auto start = esp_timer_get_time();
  sampler::build_waveform(s.waveform, s.data, s.frames, [responsive] {
    if (responsive)
      vTaskDelay(1);
  });
  const unsigned us = unsigned(esp_timer_get_time() - start);
  waveform_builds.fetch_add(1);
  waveform_build_max.store(std::max(waveform_build_max.load(), us));
  if (s.frames == 2097152)
    waveform_large_us.store(us);
}
void publish_preview(unsigned track, const sampler::Sample *s) {
  if (track >= 16)
    return;
  portENTER_CRITICAL(&lock);
  previews[track] = s;
  portEXIT_CRITICAL(&lock);
  changed.fetch_add(1);
}
void preview(unsigned track, Preview &out) {
  portENTER_CRITICAL(&lock);
  const auto *s = track < 16 ? previews[track] : nullptr;
  if (s) {
    out.waveform = s->waveform;
    out.frames = s->frames;
    strlcpy(out.name, s->name, sizeof(out.name));
  } else
    out = {};
  portEXIT_CRITICAL(&lock);
}
bool resident(unsigned track, const sampler::Sample *sample) {
  portENTER_CRITICAL(&lock);
  const bool valid = track < 16 && sample && previews[track] == sample;
  portEXIT_CRITICAL(&lock);
  return valid;
}
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
      projects::busy() || storage::state() != storage::State::Ready ||
      working.exchange(true))
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
  if (assigned_names[t][0] && !previews[t])
    snprintf(d, n, "MISSING SAMPLE T%02u", t + 1);
  else
    strlcpy(d, assigned_names[t][0] ? assigned_names[t] : "SYNTH", n);
  portEXIT_CRITICAL(&lock);
}
void message(char *d, unsigned n) {
  portENTER_CRITICAL(&lock);
  strlcpy(d, info, n);
  portEXIT_CRITICAL(&lock);
}
void detach_project_samples() {
  // No producer can publish concurrently: project operation owns storage task.
  portENTER_CRITICAL(&lock);
  for (unsigned t = 0; t < 16; ++t) {
    project_retired[t] = owned[t];
    previews[t] = nullptr;
    transfer.active[t] = nullptr;
    voices[t].assign(nullptr);
  }
  portEXIT_CRITICAL(&lock);
  assignments.store(0);
}
void retire_project_samples() {
  for (unsigned t = 0; t < 16; ++t) {
    destroy(project_retired[t]);
    project_retired[t] = owned[t] = nullptr;
    portENTER_CRITICAL(&lock);
    assigned_names[t][0] = 0;
    portEXIT_CRITICAL(&lock);
  }
  changed.fetch_add(1);
}
bool restore(unsigned t, const char *ref) {
  target = t;
  // Remember unavailable references too; UI can identify the missing Track.
  portENTER_CRITICAL(&lock);
  strlcpy(assigned_names[t], ref, 96);
  portEXIT_CRITICAL(&lock);
  bool ok = load_name(ref);
  changed.fetch_add(1);
  return ok;
}
} // namespace samples
