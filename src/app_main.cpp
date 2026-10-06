#include "app/model.h"
#include "app/samples.h"
static bool app_pcm(int track, int16_t &value);
static void app_sample();
// clang-format off
#include <Arduino.h>
#include "engine/synth_api.h"
#include "esp_timer.h"
#include "hal/audio_hal.h"
#include "hal/display_hal.h"
#include "hal/guition_board.h"
#include "hal/touch_hal.h"
#include "../DRUM_2026_VSAMPLER_TAB5_2002.ino"
#include "../synthESP32.ino"
// clang-format on
namespace {
app::Queue<64> commands;
app::Queue<128> input_events;
app::Queue<32> pad_triggers;
std::atomic<int> visible_page{0}, visible_bank{0};
std::atomic<uint32_t> input_errors{0}, input_overflow{0};
app::Engine engine, view;
app::Ui ui;
std::atomic<int> playhead{-1};
std::atomic<uint32_t> diagnostic_p99{0}, diagnostic_max{0},
    diagnostic_misses{0};
std::atomic<uint32_t> flashes[16];
bool delay_ready = false;
int sample_index = 0;
bool dirty[43]{};
bool full = true;
uint32_t rejected = 0, touch_errors = 0, frames = 0, full_frames = 0;
uint32_t actions[43]{}, drags = 0;
uint64_t dirty_bytes = 0, normal_dirty_bytes = 0;
unsigned normal_frames = 0;
uint32_t normal_dirty_max = 0;
uint32_t dirty_max = 0, prep_max = 0, skipped_frames = 0;
constexpr unsigned capture_blocks = 10336;
uint32_t render_times[capture_blocks]{};
std::atomic<unsigned> captured{0};
unsigned active_min = 16, active_max = 0;
uint32_t misses = 0, write_errors = 0, timeouts = 0, rails = 0, nonzero = 0,
         peak = 0;
uint32_t flash_until[16]{};
int old_head = -1;
[[maybe_unused]] uint32_t diagnostic_due = 0;
constexpr uint16_t bg = 0x1082, panel = 0x2104, accent = 0x07d3, white = 0xffff;
void trigger(int t) {
  if (engine.tracks[t].sample && samples::voices[t].sample) {
    PITCH[t] = 255;
    AMP[t] = 0;
    samples::voices[t].trigger(uint64_t(
        midiFrequencies[engine.tracks[t].pitch] * BASE_FREQ_INV * 65536.f));
  } else
    synthESP32_TRIGGER_P(t, engine.tracks[t].pitch);
  flashes[t].fetch_add(1, std::memory_order_relaxed);
}
void update_track(int t) {
  auto &v = engine.tracks[t];
  ROTvalue[t][14] = v.volume;
  ROTvalue[t][13] = v.pan;
  ROTvalue[t][12] = v.pitch;
  ROTvalue[t][10] = v.length;
  ROTvalue[t][1] = v.wave;
  synthESP32_setWave(t, v.wave);
  synthESP32_setEnvelope(t, 3);
  synthESP32_setLength(t, v.length);
  synthESP32_setMod(t, 64);
  synthESP32_updateVolPan(t);
  synthESP32_setFilter(t, 0);
  if (PITCH[t] != 255)
    synthESP32_setPitch(t, v.pitch);
}
bool send(app::Command c) {
  if (!commands.push(c)) {
    ++rejected;
    return false;
  }
  view.apply(c);
  return true;
}
void draw(int id) {
  auto r = id == 42 ? app::Rect{24, 250, 200, 56} : app::widget(id);
  char s[48]{};
  uint16_t color = panel;
  if (id >= 38) {
    const char *labels[] = {"PREVIOUS", "NEXT", "LOAD / ASSIGN",
                            view.tracks[ui.selected].sample ? "USE SYNTH"
                                                            : "USE SAMPLE",
                            "SAMPLE PAGE"};
    snprintf(s, sizeof(s), "%s", labels[id - 38]);
  } else if (id < 16) {
    bool on = view.tracks[ui.selected].steps & (1u << id);
    color = on ? accent : panel;
    if (id == old_head)
      color = 0xffe0;
    if (id == ui.selected_step && !on)
      color = 0x528a;
    snprintf(s, sizeof(s), "%02d", id + 1);
  } else if (id < 24) {
    int t = ui.bank * 8 + id - 16;
    color = t == ui.selected ? accent : panel;
    if (millis() < flash_until[t])
      color = 0xffe0;
    snprintf(s, sizeof(s), "%02d %s", t + 1,
             view.tracks[t].muted    ? "MUTE"
             : view.tracks[t].sample ? "SAMPLE"
                                     : "SYNTH");
  } else if (id < 29) {
    auto &t = view.tracks[ui.selected];
    const char *names[] = {"VOLUME", "PAN", "PITCH", "LENGTH", "WAVE"};
    int vals[] = {t.volume, t.pan, t.pitch, t.length, t.wave};
    snprintf(s, sizeof(s), "%s  %d", names[id - 24], vals[id - 24]);
  } else {
    const char *names[] = {view.playing ? "STOP" : "PLAY",
                           "SEQ",
                           "TRACK",
                           "FX",
                           ui.bank ? "BANK B" : "BANK A",
                           "- BPM",
                           "+",
                           view.delay ? "DELAY ON" : "DELAY OFF",
                           view.tracks[ui.selected].muted ? "UNMUTE" : "MUTE"};
    snprintf(s, sizeof(s), "%s", names[id - 29]);
  }
  display::rect(r.x, r.y, r.w, r.h, color);
  display::text(r.x + 8, r.y + 16, s, white, 2);
}
void sample_status() {
  char line[128];
  display::rect(24, 290, 752, 120, bg);
  samples::describe(sample_index, line, sizeof(line));
  display::text(24, 296, line, white, 1);
  samples::track_name(ui.selected, line, sizeof(line));
  display::text(24, 325, line, accent, 1);
  samples::message(line, sizeof(line));
  display::text(24, 356, line, white, 1);
  display::text(24, 380,
                view.tracks[ui.selected].sample ? "SOURCE SAMPLE / C4=UNITY"
                                                : "SOURCE SYNTH",
                white, 1);
}
void overlay() {
#if P4SDM_APP_DIAGNOSTICS
  char s[110];
  display::rect(0, 80, 800, 18, bg);
  snprintf(s, sizeof(s), "p99 %lu max %lu miss %lu frames %lu dirty %lu PS %u",
           (unsigned long)diagnostic_p99.load(),
           (unsigned long)diagnostic_max.load(),
           (unsigned long)diagnostic_misses.load(), (unsigned long)frames,
           (unsigned long)(frames ? dirty_bytes / frames : 0),
           unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  display::text(8, 82, s, white, 1);
#endif
}
void header() {
  char s[64];
  display::rect(0, 0, 800, 80, bg);
  snprintf(s, sizeof(s), "P4SDM / PATTERN 01   T%02d", ui.selected + 1);
  display::text(16, 24, s, white, 2);
  snprintf(s, sizeof(s), "%03d", view.bpm);
  display::text(600, 24, s, accent, 3);
  draw(34);
  draw(35);
}
void interact(int id, int x, bool initial) {
  if (id < 0)
    return;
  if (initial)
    ++actions[id];
  else
    ++drags;
  if (id >= 38 && initial) {
    if (id == 42) {
      ui.page = app::Page::Sample;
      full = true;
    } else if (id == 38 || id == 39) {
      sample_index = app::clamp(sample_index + (id == 38 ? -1 : 1), 0,
                                int(samples::count()) - 1);
      dirty[38] = true;
    } else if (id == 40)
      samples::request(ui.selected, sample_index);
    else if (id == 41) {
      if (samples::has_sample(ui.selected) && !samples::busy())
        send({app::Kind::Source, uint8_t(ui.selected),
              !view.tracks[ui.selected].sample});
      dirty[38] = true;
    }
  } else if (id < 16 && initial) {
    if (send({app::Kind::Step, uint8_t(ui.selected), id})) {
      int old = ui.selected_step;
      ui.selected_step = id;
      dirty[id] = true;
      if (old >= 0)
        dirty[old] = true;
    }
  } else if (id < 24 && initial) {
    int old = ui.selected;
    ui.selected = ui.bank * 8 + id - 16;
    // Pad audio was queued by the touch worker before display retirement.
    dirty[id] = true;
    if (old / 8 == ui.bank)
      dirty[16 + old % 8] = true;
    for (int i = 0; i < 16; ++i)
      dirty[i] = true;
    dirty[34] = true;
  } else if (id < 29) {
    app::Kind kinds[] = {app::Kind::Volume, app::Kind::Pan, app::Kind::Pitch,
                         app::Kind::Length, app::Kind::Wave};
    int val = app::drag(id, x);
    if (id == 28)
      val = val * 15 / 127;
    auto &t = view.tracks[ui.selected];
    int current[] = {t.volume, t.pan, t.pitch, t.length, t.wave};
    if (val != current[id - 24] &&
        send({kinds[id - 24], uint8_t(ui.selected), val}))
      dirty[id] = true;
  } else if (initial) {
    if (id == 29) {
      send({app::Kind::Play, 0, !view.playing});
      dirty[29] = true;
    } else if (id >= 30 && id <= 32) {
      auto page = static_cast<app::Page>(id - 30);
      if (page != ui.page) {
        ui.page = page;
        full = true;
      }
    } else if (id == 33) {
      ui.bank ^= 1;
      for (int i = 16; i < 24; ++i)
        dirty[i] = true;
      dirty[33] = true;
    } else if (id == 34 || id == 35) {
      send({app::Kind::Bpm, 0, view.bpm + (id == 34 ? -1 : 1)});
      dirty[34] = true;
    } else if (id == 36) {
      if (delay_ready)
        send({app::Kind::Delay, 0, !view.delay});
      dirty[36] = true;
    } else if (id == 37) {
      send({app::Kind::Mute, uint8_t(ui.selected),
            !view.tracks[ui.selected].muted});
      dirty[37] = true;
    }
  }
}
void ui_task(void *) {
  while (!samples::initialized())
    vTaskDelay(1);
  uint32_t next = 0, seen[16]{};
  touch::State touch_state;
  const uint32_t started = millis(),
                 completed_start = display::completed_presentations();
  bool reported = false;
  const size_t ps_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
               in_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               largest_before =
                   heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  for (;;) {
    static unsigned sample_revision = 0;
    unsigned revision = samples::revision();
    if (revision != sample_revision) {
      sample_revision = revision;
      dirty[38] = true;
      for (unsigned t = 0; t < 16; ++t)
        if (samples::assigned(t)) {
          view.tracks[t].sample = true;
          if (int(t / 8) == ui.bank)
            dirty[16 + t % 8] = true;
        }
    }
#if P4SDM_APP_STRESS
    static uint32_t stress_due = 0, retrigger_due = 0;
    static unsigned action = 0;
    if (millis() >= retrigger_due) {
      retrigger_due = millis() + 3000;
      for (int t = 0; t < 16; ++t)
        send({app::Kind::Trigger, uint8_t(t), 0});
    }
    if (millis() >= stress_due) {
      stress_due = millis() + 100;
      ++action;
      switch (action % 10) {
      case 0:
        interact(29, 0, true);
        break;
      case 1:
        interact(30, 0, true);
        break;
      case 2:
        interact(33, 0, true);
        break;
      case 3:
        interact(16 + action % 8, 0, true);
        send({app::Kind::Trigger, uint8_t(ui.selected), 0});
        break;
      case 4:
        interact(action % 16, 0, true);
        break;
      case 5:
        interact(35, 0, true);
        break;
      case 6:
        interact(31, 0, true);
        break;
      case 7:
        interact(24 + action % 3, 260 + int(action % 128) * 4, false);
        break;
      case 8:
        interact(32, 0, true);
        break;
      case 9:
        interact(30, 0, true);
        break;
      }
    }
#endif
    app::Command event;
    while (input_events.pop(event)) {
      touch_state.pressed = event.kind != app::Kind::Play;
      touch_state.x = unsigned(event.value) & 0xffff;
      touch_state.y = unsigned(event.value) >> 16;
      if (touch_state.pressed && !ui.down) {
        ui.capture = app::hit(ui.page, touch_state.x, touch_state.y);
        interact(ui.capture, touch_state.x, true);
      } else if (touch_state.pressed && ui.capture >= 24 && ui.capture < 29)
        interact(ui.capture, touch_state.x, false);
      ui.down = touch_state.pressed;
      if (!ui.down)
        ui.capture = -1;
    }
    touch_errors = input_errors.load(std::memory_order_relaxed);
    int head = playhead.load(std::memory_order_relaxed);
    if (head != old_head) {
      if (old_head >= 0)
        dirty[old_head] = true;
      if (head >= 0)
        dirty[head] = true;
      old_head = head;
    }
    for (int t = 0; t < 16; ++t) {
      auto n = flashes[t].load(std::memory_order_relaxed);
      if (n != seen[t]) {
        seen[t] = n;
        flash_until[t] = millis() + 100;
        if (t / 8 == ui.bank)
          dirty[16 + t % 8] = true;
      }
      if (flash_until[t] && millis() >= flash_until[t]) {
        flash_until[t] = 0;
        if (t / 8 == ui.bank)
          dirty[16 + t % 8] = true;
      }
    }
#if P4SDM_APP_DIAGNOSTICS
    bool diag = millis() >= diagnostic_due;
    if (diag)
      diagnostic_due = millis() + 1000;
#else
    bool diag = false;
#endif
    bool changed = full || diag;
    for (bool d : dirty)
      changed |= d;
    if (changed && int32_t(millis() - next) >= 0) {
      const auto start = esp_timer_get_time();
      const uint32_t frame_ms = millis();
      const bool page_change = full;
      if (display::begin_frame() != ESP_OK) {
        vTaskSuspend(nullptr);
      }
      if (full) {
        display::fill(bg);
        header();
        if (ui.page == app::Page::Sequence) {
          for (int i = 0; i < 24; ++i)
            draw(i);
        } else if (ui.page == app::Page::Track) {
          for (int i = 24; i < 29; ++i)
            draw(i);
          draw(37);
          draw(42);
        } else if (ui.page == app::Page::Sample) {
          for (int i = 38; i <= 41; ++i)
            draw(i);
          sample_status();
        } else {
          draw(36);
          display::text(24, 210, "INTERNAL DELAY / NO SD REQUIRED", white, 2);
        }
        for (int i = 29; i < 34; ++i)
          draw(i);
        ++full_frames;
        full = false;
      } else {
        if (dirty[34])
          header();
        for (int i = 0; i < 43; ++i)
          if (dirty[i] &&
              ((i < 24 && ui.page == app::Page::Sequence) ||
               (i >= 24 && i < 29 && ui.page == app::Page::Track) ||
               (i >= 29 && i < 34) || (i == 36 && ui.page == app::Page::Fx) ||
               ((i == 37 || i == 42) && ui.page == app::Page::Track)))
            draw(i);
      }
      if (ui.page == app::Page::Sample && dirty[38]) {
        draw(41);
        sample_status();
      }
      if (diag)
        overlay();
      std::fill(std::begin(dirty), std::end(dirty), false);
      prep_max = std::max(prep_max, uint32_t(esp_timer_get_time() - start));
      if (display::present() != ESP_OK)
        vTaskSuspend(nullptr);
      auto tel = display::telemetry();
      dirty_bytes += tel.dirty_bytes;
      if (!page_change) {
        ++normal_frames;
        normal_dirty_bytes += tel.dirty_bytes;
        normal_dirty_max = std::max(normal_dirty_max, tel.dirty_bytes);
      }
      dirty_max = std::max(dirty_max, tel.dirty_bytes);

      ++frames;
      visible_page.store(int(ui.page));
      visible_bank.store(ui.bank);
      next = frame_ms + 33;
      if (int32_t(millis() - next) > 33)
        skipped_frames += (millis() - next) / 33;
    }
    if (!changed && !display::idle() && display::wait_idle() != ESP_OK)
      vTaskSuspend(nullptr);
    if (!reported && millis() - started >= 63000) {
      reported = true;
      const size_t ps_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                   in_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                   largest_after =
                       heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
      unsigned pads = 0, steps = 0, pages = 0;
      for (int i = 0; i < 16; ++i)
        steps += actions[i];
      for (int i = 16; i < 24; ++i)
        pads += actions[i];
      for (int i = 30; i <= 32; ++i)
        pages += actions[i];
      Serial.printf("[M5 UI] seconds=%.3f submitted=%u completed=%u "
                    "dirty_avg=%u dirty_max=%u full=%u preparation_max_us=%u "
                    "touch_errors=%u rejected=%u pads=%u steps=%u drags=%u "
                    "pages=%u transport=%u bpm=%u skipped=%u\n",
                    (millis() - started) / 1000., frames,
                    display::completed_presentations() - completed_start,
                    unsigned(frames ? dirty_bytes / frames : 0), dirty_max,
                    full_frames, prep_max, touch_errors,
                    rejected + input_overflow.load(), pads, steps, drags, pages,
                    actions[29], actions[34] + actions[35], skipped_frames);
      Serial.printf("[M5 memory] ps_before=%u ps_after=%u internal_before=%u "
                    "internal_after=%u largest_before=%u largest_after=%u\n",
                    unsigned(ps_before), unsigned(ps_after),
                    unsigned(in_before), unsigned(in_after),
                    unsigned(largest_before), unsigned(largest_after));
    }
    delay(8);
  }
}
void touch_worker(void *) {
  touch::State state;
  bool down = false;
  uint16_t x = 0, y = 0;
  for (;;) {
    if (touch::poll(state) != ESP_OK)
      input_errors.fetch_add(1);
    else {
      if (state.pressed != down ||
          (state.pressed && (state.x != x || state.y != y))) {
        if (state.pressed && !down && visible_page.load() == 0) {
          int id = app::hit(app::Page::Sequence, state.x, state.y);
          if (id >= 16 && id < 24 &&
              !pad_triggers.push({app::Kind::Trigger,
                                  uint8_t(visible_bank.load() * 8 + id - 16),
                                  0}))
            input_overflow.fetch_add(1);
        }
        if (!input_events.push(
                {state.pressed ? app::Kind::Trigger : app::Kind::Play, 0,
                 int(unsigned(state.x) | (unsigned(state.y) << 16))}))
          input_overflow.fetch_add(1);
      }
      down = state.pressed;
      x = state.x;
      y = state.y;
    }
    delay(8);
  }
}
void audio_worker(void *) {
  for (;;) {
    const auto start = esp_timer_get_time();
    int sample_track = samples::transfer.consume(samples::voices);
    if (sample_track >= 0) {
      unsigned t = unsigned(sample_track);
      engine.tracks[t].sample = true;
      PITCH[t] = 255;
      AMP[t] = 0;
      FILTROS[t].reset();
    }
    app::Command c;
    for (unsigned n = 0; n < 16 && commands.pop(c); ++n) {
      engine.apply(c);
      if (c.kind == app::Kind::Source) {
        samples::voices[c.track].active = false;
        FILTROS[c.track].reset();
        PITCH[c.track] = 255;
        AMP[c.track] = 0;
      }
      if (c.kind == app::Kind::Trigger)
        trigger(c.track);
      else if (c.kind == app::Kind::Delay) {
        is_delay = delay_ready && engine.delay;
        delays = is_delay ? 0xffff : 0;
      } else if (c.kind >= app::Kind::Volume && c.kind <= app::Kind::Wave)
        update_track(c.track);
      if (c.kind == app::Kind::Pitch && engine.tracks[c.track].sample)
        samples::voices[c.track].increment =
            uint64_t(midiFrequencies[engine.tracks[c.track].pitch] *
                     BASE_FREQ_INV * 65536.f);
    }
    for (unsigned n = 0; n < 16 && pad_triggers.pop(c); ++n)
      trigger(c.track);
    render_buffer();
    auto us = uint32_t(esp_timer_get_time() - start);
    unsigned b = captured.load(std::memory_order_relaxed);
    if (b < capture_blocks) {
      unsigned active = 0;
      for (unsigned t = 0; t < 16; ++t)
        active += (PITCH[t] != 255 && AMP[t] != 0) || samples::voices[t].active;
      active_min = std::min(active_min, active);
      active_max = std::max(active_max, active);
      render_times[b] = us;
      if (us >= double(DMA_BUF_LEN) * 1e6 / SAMPLE_RATE)
        ++misses;
      for (int i = 0; i < DMA_BUF_LEN * 2; ++i) {
        auto v = int(out_buf[i]);
        peak = std::max(peak, uint32_t(std::abs(v)));
        nonzero += v != 0;
        rails += v == -32768 || v == 32767;
      }
    }
    playhead.store(engine.playing ? engine.step : -1,
                   std::memory_order_relaxed);
    auto err = audio::write(out_buf, DMA_BUF_LEN);
    if (b < capture_blocks) {
      write_errors += err != ESP_OK;
      timeouts += err == ESP_ERR_TIMEOUT;
      captured.store(b + 1, std::memory_order_release);
    }
    if (err != ESP_OK)
      vTaskSuspend(nullptr);
  }
}
} // namespace
static bool app_pcm(int t, int16_t &v) {
  if (!engine.tracks[t].sample)
    return false;
  v = samples::voices[t].next();
  return true;
}
static void app_sample() {
  engine.sample([](int t) { trigger(t); });
}
static void initialize(void *) {
  Serial.setTxBufferSize(2048);
  Serial.begin(115200);
  is_reverb = is_delay = is_chorus = is_flanger = is_tremolo = is_ringmod =
      is_distortion = is_bitcrusher = false;
  if (display::begin(display::Pipeline::NativeQueued) != ESP_OK ||
      touch::begin() != ESP_OK) {
    Serial.println("[M5 INIT FAIL]");
    vTaskDelete(nullptr);
    return;
  }
  synthESP32_begin();
  initADSR();
  for (int t = 0; t < 16; ++t) {
    ROTvalue[t][16] = 1;
    ROTvalue[t][9] = 3;
    ROTvalue[t][11] = 64;
    ROTvalue[t][15] = 0;
    addnextsnd[t] = 1;
    detune[t] = 0;
#if P4SDM_APP_STRESS
    engine.tracks[t].length = view.tracks[t].length = 127;
#endif
    update_track(t);
  }
  synthESP32_setMVol(60);
  synthESP32_setMFilter(0);
  delay_ready = myDelay.init(88200);
  if (delay_ready) {
    myDelay.setTime(12000);
    myDelay.setFeedback(120);
    myDelay.setInputLevel(160);
  }
  view.delay = engine.delay = delay_ready;
  is_delay = delay_ready;
  delays = delay_ready ? 0xffff : 0;
  level_delay = 100;
  if (audio::begin(SAMPLE_RATE) != ESP_OK) {
    Serial.println("[M5 INIT FAIL]");
    vTaskDelete(nullptr);
    return;
  }
  xTaskCreatePinnedToCore(audio_worker, "app_audio", 8000, nullptr,
                          configMAX_PRIORITIES - 1, nullptr, 0);
  samples::start();
  xTaskCreatePinnedToCore(touch_worker, "app_touch", 5000, nullptr, 3, nullptr,
                          1);
  xTaskCreatePinnedToCore(ui_task, "app_ui", 8000, nullptr, 2, nullptr, 0);
  vTaskSuspend(nullptr);
}
void setup() {
  xTaskCreatePinnedToCore(initialize, "app_init", 8000, nullptr, 1, nullptr, 0);
}
void loop() {
  static bool reported = false;
  if (!reported && captured.load(std::memory_order_acquire) == capture_blocks) {
    reported = true;
    static uint32_t sorted[capture_blocks];
    std::copy(render_times, render_times + capture_blocks, sorted);
    std::sort(sorted, sorted + capture_blocks);
    diagnostic_p99.store(sorted[capture_blocks * 99 / 100]);
    diagnostic_max.store(sorted[capture_blocks - 1]);
    diagnostic_misses.store(misses);
    Serial.printf("[M5] blocks=%u p50=%u p95=%u p99=%u max=%u misses=%u "
                  "failures=%u timeouts=%u peak=%u nonzero=%u rails=%u "
                  "active_min=%u active_max=%u\n",
                  capture_blocks, sorted[capture_blocks / 2],
                  sorted[capture_blocks * 95 / 100],
                  sorted[capture_blocks * 99 / 100], sorted[capture_blocks - 1],
                  misses, write_errors, timeouts, peak, nonzero, rails,
                  active_min, active_max);
    guition::memory_report("M5 after 60s");
  }
  delay(1000);
}
