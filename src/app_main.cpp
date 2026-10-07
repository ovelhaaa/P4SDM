#include "app/model.h"
#include "engine/track_tone.h"
namespace {
app::Engine engine, view;
}
#include "app/qualification.h"
#include "app/samples.h"
#include <cstdarg>
static bool app_pcm(int track, int16_t &value);
static void app_sample();
static inline __attribute__((always_inline)) int32_t
app_delay_send(int track, int32_t sample) {
  return p4tone::delay_send(sample, engine.tracks[track].delay_send);
}
static int16_t app_velocity(int track, int16_t value);
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
// Native USB/JTAG can lose 64-byte fragments during burst summaries on this
// device. Pace only the final report, never command handling or audio
// rendering.
void summary_printf(const char *format, ...) {
  char line[512];
  va_list args;
  va_start(args, format);
  const int count = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  const int length = std::min(count, int(sizeof(line) - 1));
  for (int offset = 0; offset < length; offset += 32) {
    Serial.write(reinterpret_cast<const uint8_t *>(line + offset),
                 std::min(32, length - offset));
    delay(4);
  }
}
app::Queue<64> commands;
app::Queue<128> input_events;
app::Queue<32> pad_triggers;
std::atomic<int> visible_page{0}, visible_bank{0};
std::atomic<uint32_t> input_errors{0}, input_overflow{0};
app::Ui ui;
std::atomic<int> playhead{-1};
// Acknowledged command count and pattern transport published together. UI only
// accepts snapshots after all its optimistic edits have reached audio.
std::atomic<uint32_t> pattern_status{0}, pattern_switches{0}, pattern_loops{0};
uint16_t sent_commands = 0;
std::atomic<uint32_t> applied_actions[unsigned(app::Kind::Count)]{},
    long_to_short{0}, short_to_long{0}, queue_replacements{0};
std::atomic<uint32_t> diagnostic_p99{0}, diagnostic_max{0},
    diagnostic_misses{0};
std::atomic<uint32_t> flashes[16];
bool delay_ready = false;
int sample_index = 0;
bool dirty[114]{};
bool full = true;
uint32_t rejected = 0, touch_errors = 0, frames = 0, full_frames = 0;
uint32_t actions[114]{}, drags = 0;
uint64_t dirty_bytes = 0, normal_dirty_bytes = 0;
unsigned normal_frames = 0;
uint32_t normal_dirty_max = 0;
uint32_t dirty_max = 0, prep_max = 0, skipped_frames = 0;
uint32_t tool_cell_max = 0, tool_edit_full = 0;
uint32_t tone_ui_edits = 0, tone_ui_full = 0, tone_ui_widgets_max = 0;
uint32_t lock_ui_edits = 0, lock_ui_full = 0, lock_ui_widgets_max = 0;
constexpr unsigned capture_blocks = 10337;
#if P4SDM_SAMPLER_QUALIFICATION
struct QualificationMetrics {
  uint32_t times[4096]{}, count = 0, misses = 0, failures = 0, timeouts = 0;
  uint32_t nonzero = 0, rails = 0, active_max = 0;
};
QualificationMetrics qualification[8];
std::atomic<unsigned> qualification_finished{0};
#endif
uint32_t render_times[capture_blocks]{};
struct GrooveMetrics {
  uint32_t parents, passed, skipped, ratchets, pending_max, swing_changes;
  unsigned velocity_min, velocity_max;
};
GrooveMetrics groove_result{};
uint32_t lock_result[6]{};
struct ToneMetrics {
  uint32_t edits[3]{}, blocks = 0, worst = 0, dense = 0, dense_worst = 0;
  unsigned minimum[3]{127, 127, 127}, maximum[3]{};
};
ToneMetrics tone_metrics{}, tone_result{};
constexpr unsigned worst_blocks = 2068;
uint32_t worst_max = 0, worst_triggers = 0;
uint32_t locked_dense_max = 0, locked_dense_blocks = 0;
std::atomic<uint32_t> transform_max{0}, transform_blocks{0},
    dense_transform_blocks{0};

std::atomic<unsigned> captured{0};
// Serialize end-of-run reports from loop/UI without blocking the audio task.
std::atomic<bool> audio_summary_ready{false};
unsigned active_min = 16, active_max = 0;
uint32_t misses = 0, write_errors = 0, timeouts = 0, rails = 0, nonzero = 0,
         peak = 0;
uint32_t flash_until[16]{};
int old_head = -1;
[[maybe_unused]] uint32_t diagnostic_due = 0;
constexpr uint16_t bg = 0x1082, panel = 0x2104, accent = 0x07d3, white = 0xffff;
uint16_t event_gain[16]{};
app::TriggerEvent voice_event[16]{};
void trigger(app::TriggerEvent event) {
  int t = event.track;
  voice_event[t] = event;
  event_gain[t] = app::velocity_gain(event.velocity);
  VOL_L[t] = app::event_channel_gain(event.volume, event.pan, false);
  VOL_R[t] = app::event_channel_gain(event.volume, event.pan, true);
  synthESP32_setWave(t, event.wave);
  if (engine.tracks[t].sample && samples::voices[t].sample) {
    PITCH[t] = 255;
    AMP[t] = 0;
    samples::voices[t].trigger(sampler::pitch_increment(event.pitch));
  } else
    synthESP32_TRIGGER_P(t, event.pitch);
  flashes[t].fetch_add(1, std::memory_order_relaxed);
}
void trigger(int t) { trigger(app::resolve_event(t, engine.tracks[t], 127)); }
void update_track(int t) {
  auto &v = engine.tracks[t];
  ROTvalue[t][14] = v.volume;
  ROTvalue[t][13] = v.pan;
  ROTvalue[t][12] = v.pitch;
  ROTvalue[t][10] = v.length;
  ROTvalue[t][1] = v.wave;
  // Base edits retain any active voice overrides; unlocked controls remain
  // live.
  const auto &event = voice_event[t];
  const app::StepLocks locks{event.locked_mask, event.pitch, event.volume,
                             event.pan, event.wave};
  const auto effective = app::resolve_event(t, v, event.velocity, locks);
  synthESP32_setWave(t, effective.wave);
  synthESP32_setEnvelope(t, 3);
  synthESP32_setLength(t, v.length);
  synthESP32_setMod(t, 64);
  VOL_L[t] = app::event_channel_gain(effective.volume, effective.pan, false);
  VOL_R[t] = app::event_channel_gain(effective.volume, effective.pan, true);
  synthESP32_setTrackTone(t, v.filter_cutoff, v.filter_resonance);
  if (PITCH[t] != 255)
    synthESP32_setPitch(t, effective.pitch);
  if (v.sample)
    samples::voices[t].increment = sampler::pitch_increment(effective.pitch);
}
bool send(app::Command c) {
  if (c.kind == app::Kind::Pitch)
    c.pitch_source = view.tracks[c.track].sample
                         ? app::Command::PitchSource::Sample
                         : app::Command::PitchSource::Synth;
  if (!commands.push(c)) {
    ++rejected;
    return false;
  }
  ++sent_commands;
  view.apply(c);
  return true;
}
void draw(int id) {
  auto r = app::widget(id);
  char s[48]{};
  uint16_t color = panel;
  if (id >= 106) {
    const auto &t = view.tracks[ui.selected];
    if (id == 106)
      snprintf(s, sizeof(s), "TONE / TRACK %02d / %s", ui.selected + 1,
               t.sample ? "SAMPLE" : "SYNTH");
    else if (id == 107) {
      if (t.filter_cutoff == 0)
        snprintf(s, sizeof(s), "CUTOFF OPEN");
      else
        snprintf(s, sizeof(s), "CUTOFF %d / OPEN ->", 127 - t.filter_cutoff);
    } else if (id == 108)
      snprintf(s, sizeof(s), "RESONANCE %d", t.filter_resonance);
    else if (id == 109)
      snprintf(s, sizeof(s), "DELAY SEND %d / GLOBAL %s", t.delay_send,
               view.delay ? "ON" : "OFF");
    else
      snprintf(s, sizeof(s), "%s",
               id == 110   ? "TRACK <"
               : id == 111 ? "TRACK >"
               : id == 112 ? "BACK TO TRACK"
                           : "TONE");
  } else if (id >= 95) {
    if (id == 95)
      snprintf(s, sizeof(s), "LOCKS");
    else if (id == 104)
      snprintf(s, sizeof(s), "CLEAR LOCKS");
    else if (id == 105)
      snprintf(s, sizeof(s), "BACK TO STEP");
    else {
      int param = (id - 96) / 2;
      const char *names[] = {"PITCH", "VOLUME", "PAN", "WAVE"};
      const auto &l = view.patterns[view.selected_pattern]
                          .locks[ui.selected][std::max(0, ui.selected_step)];
      const int values[] = {l.pitch, l.volume, l.pan, l.wave};
      if (param == 3 && view.tracks[ui.selected].sample)
        snprintf(s, sizeof(s), "%s",
                 id % 2 ? "UNAVAILABLE" : "WAVE - SYNTH ONLY");
      else if (id % 2)
        snprintf(s, sizeof(s), "%s",
                 l.mask & (1 << param) ? "UNLOCK" : "ENABLE LOCK");
      else if (l.mask & (1 << param))
        snprintf(s, sizeof(s), "%s %d LOCKED", names[param], values[param]);
      else
        snprintf(s, sizeof(s), "%s -- UNLOCKED", names[param]);
    }
  } else if (id >= 78) {
    if (id == 78)
      snprintf(s, sizeof(s), "TOOLS");
    else if (id == 79) {
      if (ui.tool_pending) {
        const auto &c = ui.pending_tool;
        const char *operation = c.kind == app::Kind::ClearTrack  ? "CLEAR"
                                : c.kind == app::Kind::Duplicate ? "DUP"
                                                                 : "COPY";
        snprintf(s, sizeof(s), "P%02d T%02d LEN %d %s -> %s%02d?",
                 c.pattern + 1, c.track + 1, view.patterns[c.pattern].length,
                 operation, c.kind == app::Kind::Duplicate ? "P" : "T",
                 c.value + 1);
      } else
        snprintf(s, sizeof(s), "P%02d / T%02d / LEN %d / SEED %08X",
                 view.selected_pattern + 1, ui.selected + 1,
                 view.patterns[view.selected_pattern].length,
                 unsigned(view.edit_seed));
    } else if (id <= 82) {
      const char *sections[] = {"TRACK", "GENERATE", "PATTERN"};
      snprintf(s, sizeof(s), "%s", sections[id - 80]);
      color = ui.tool_section == id - 80 ? accent : panel;
    } else {
      const char *track[] = {"ROTATE <",    "ROTATE >", "REVERSE", "COPY TRACK",
                             "CLEAR TRACK", "DEST <",   "DEST >",  "CONFIRM",
                             "CANCEL",      "",         "TRACK <", "TRACK >"};
      const char *pattern[] = {"DUPLICATE", "ALL <", "ALL >",   "REVERSE ALL",
                               "",          "",      "",        "CONFIRM",
                               "CANCEL",    "",      "TRACK <", "TRACK >"};
      if (ui.tool_section != 1)
        snprintf(s, sizeof(s), "%s",
                 (ui.tool_section == 0 ? track : pattern)[id - 83]);
      else {
        const char *names[] = {"PULSES",       "ROTATION",     "DENSITY",
                               "VEL VAR",      "PROB VAR",     "RATCHET",
                               "EUCLID APPLY", "RANDOM APPLY", "MUTATE APPLY",
                               "REROLL",       "AMOUNT",       ""};
        int values[] = {ui.pulses,  ui.rotation, ui.density,
                        ui.vel_var, ui.prob_var, ui.ratchet_chance};
        if (id <= 88)
          snprintf(s, sizeof(s), "%s %d", names[id - 83], values[id - 83]);
        else if (id == 93)
          snprintf(s, sizeof(s), "AMOUNT %d%%", ui.mutate_amount);
        else
          snprintf(s, sizeof(s), "%s", names[id - 83]);
      }
    }
  } else if (id >= 68) {
    int step = std::max(0, ui.selected_step);
    auto &m = view.patterns[view.selected_pattern].meta[ui.selected][step];
    if (id == 68)
      snprintf(s, sizeof(s), "STEP %02d", step + 1);
    if (id == 69)
      snprintf(s, sizeof(s), "SWING %d%%", view.swing);
    if (id == 70)
      snprintf(s, sizeof(s), "P%02d T%02d STEP %02d %s",
               view.selected_pattern + 1, ui.selected + 1, step + 1,
               (view.patterns[view.selected_pattern].track_steps[ui.selected] &
                (1u << step))
                   ? "ON"
                   : "OFF");
    if (id == 71)
      snprintf(s, sizeof(s), "VEL %d%s", m.velocity,
               m.velocity == 127 ? " ACCENT" : "");
    if (id == 72) {
      snprintf(s, sizeof(s), "ACCENT");
      color = m.velocity == 127 ? accent : panel;
    }
    if (id == 73)
      snprintf(s, sizeof(s), "PROB %d%%", m.probability);
    if (id >= 74 && id <= 77) {
      snprintf(s, sizeof(s), "RATCHET %dx", id - 73);
      color = m.ratchets == id - 73 ? accent : panel;
    }
  } else if (id == 44) {
    color = bg;
    if (view.queued_pattern >= 0)
      snprintf(s, sizeof(s), "P%02d > P%02d / EDIT %02d",
               view.playing_pattern + 1, view.queued_pattern + 1,
               view.selected_pattern + 1);
    else
      snprintf(s, sizeof(s), "P%02d / EDIT %02d / PATTERN",
               view.playing_pattern + 1, view.selected_pattern + 1);
  } else if (id == 43) {
    char title[40];
    display::rect(24, 90, 216, 60, bg);
    snprintf(title, sizeof(title), "TRACK %02d / %s", ui.selected + 1,
             view.tracks[ui.selected].sample ? "SAMPLE" : "SYNTH");
    display::text(24, 100, title, white, 1);
    snprintf(s, sizeof(s), "%s",
             view.solos & (1u << ui.selected) ? "UNSOLO" : "SOLO");
  } else if (id >= 46 && id <= 61) {
    int p = id - 46;
    color = p == view.playing_pattern ? 0x03c0 : panel;
    if (p == view.queued_pattern)
      color = 0x820f;
    snprintf(s, sizeof(s), "P%02d %s%s", p + 1,
             p == view.playing_pattern ? "PLAY" : "",
             p == view.queued_pattern ? "NEXT" : "");
  } else if (id >= 62) {
    const char *labels[] = {"-",
                            "",
                            "+",
                            ui.copy_source >= 0 ? "CANCEL COPY" : "COPY",
                            ui.clear_pattern >= 0 ? "CONFIRM CLEAR" : "CLEAR",
                            ui.inspect ? "INSPECT" : "QUEUE / PLAY"};
    snprintf(s, sizeof(s), "%s", labels[id - 62]);
    if (id == 63)
      snprintf(s, sizeof(s), "%02d",
               view.patterns[view.selected_pattern].length);
  } else if (id >= 38 && id <= 42) {
    const char *labels[] = {"PREVIOUS", "NEXT", "LOAD / ASSIGN",
                            view.tracks[ui.selected].sample ? "USE SYNTH"
                                                            : "USE SAMPLE",
                            "SAMPLE PAGE"};
    snprintf(s, sizeof(s), "%s", labels[id - 38]);
  } else if (id < 16) {
    bool on = view.patterns[view.selected_pattern].track_steps[ui.selected] &
              (1u << id);
    color = on ? accent : panel;
    if (id >= view.patterns[view.selected_pattern].length)
      color = 0x1082;
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
    snprintf(s, sizeof(s), "%02d %s%s", t + 1,
             view.tracks[t].muted    ? "MUTE"
             : view.tracks[t].sample ? "SAMPLE"
                                     : "SYNTH",
             view.solos & (1u << t) ? " SOLO" : "");
  } else if (id < 29) {
    auto &t = view.tracks[ui.selected];
    const char *names[] = {"VOLUME", "PAN", "PITCH", "LENGTH", "WAVE"};
    int vals[] = {t.volume, t.pan, t.pitch, t.length, t.wave};
    if (!app::track_control_enabled(t, id))
      snprintf(s, sizeof(s), "%s  SYNTH ONLY", names[id - 24]);
    else if (t.sample && id == 26)
      snprintf(s, sizeof(s), "TUNE %+d st%s", t.pitch - 60,
               t.pitch == 60 ? " UNITY" : "");
    else
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
  if (id < 16 && id == ui.selected_step) {
    display::rect(r.x, r.y, r.w, 3, white);
    display::rect(r.x, r.y + r.h - 3, r.w, 3, white);
  }
  if (id == 65 && ui.copy_source >= 0) {
    snprintf(s, sizeof(s), "FROM P%02d: TAP DEST", ui.copy_source + 1);
    display::text(r.x + 8, r.y + 44, s, accent, 1);
  }
  if (id >= 46 && id <= 61 && id - 46 == view.selected_pattern) {
    display::rect(r.x, r.y, r.w, 3, accent);
    display::rect(r.x, r.y + r.h - 3, r.w, 3, accent);
    display::text(r.x + 8, r.y + 44, "EDIT", accent, 1);
  }
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
void tempo_header() {
  char s[8];
  display::rect(584, 16, 120, 56, bg);
  snprintf(s, sizeof(s), "%03d", view.bpm);
  display::text(600, 24, s, accent, 3);
  draw(34);
  draw(35);
}
void header() {
  char s[32];
  display::rect(0, 0, 800, 80, bg);
  snprintf(s, sizeof(s), "P4SDM T%02d", ui.selected + 1);
  display::text(16, 24, s, white, 2);
  draw(44);
  tempo_header();
}
void interact(int id, int x, bool initial) {
  if (id < 0)
    return;
  if (initial)
    ++actions[id];
  else
    ++drags;
  if (id >= 106) {
    if (id == 113 || id == 112) {
      if (initial) {
        ui.page = id == 113 ? app::Page::Tone : app::Page::Track;
        full = true;
      }
    } else if (id == 110 || id == 111) {
      if (initial) {
        ui.selected = app::clamp(ui.selected + (id == 110 ? -1 : 1), 0, 15);
        for (int i = 106; i <= 109; ++i)
          dirty[i] = true;
        dirty[45] = true;
      }
    } else if (id >= 107 && id <= 109) {
      int value = app::drag(id, x);
      if (id == 107)
        value = p4tone::cutoff_from_slider(value);
      const auto &t = view.tracks[ui.selected];
      const int current[] = {t.filter_cutoff, t.filter_resonance, t.delay_send};
      const bool was_full = full;
      if (current[id - 107] != value &&
          send({app::Kind(int(app::Kind::FilterCutoff) + id - 107),
                uint8_t(ui.selected), value})) {
        dirty[id] = true;
        ++tone_ui_edits;
        tone_ui_full += full && !was_full;
        tone_ui_widgets_max = std::max(tone_ui_widgets_max, uint32_t(1));
      }
    }
    return;
  } else if (id >= 95) {
    if (id == 95 || id == 105) {
      if (!initial)
        return;
      ui.page = id == 95 ? app::Page::Locks : app::Page::Step;
      full = true;
      return;
    }
    app::Command c{};
    if (!ui.lock_action(id, x, initial, view, c))
      return;
    const bool was_full = full;
    if (send(c)) {
      ++lock_ui_edits;
      lock_ui_full += full && !was_full;
      lock_ui_widgets_max =
          std::max(lock_ui_widgets_max, uint32_t(id == 104 ? 8 : 2));
      if (id == 104)
        for (int i = 96; i <= 103; ++i)
          dirty[i] = true;
      else {
        int row = 96 + ((id - 96) / 2) * 2;
        dirty[row] = dirty[row + 1] = true;
      }
    }
    return;
  } else if (id == 78 && initial) {
    ui.cancel_pattern_action();
    ui.cancel_tool();
    ui.page = app::Page::Tools;
    ui.pulses =
        app::clamp(ui.pulses, 0, view.patterns[view.selected_pattern].length);
    ui.rotation %= view.patterns[view.selected_pattern].length;
    full = true;
    return;
  } else if (id >= 80 && ui.page == app::Page::Tools) {
    app::Ui previous = ui;
    const bool was_full = full;
    if (ui.tool_section == 1 && id >= 83 && id <= 88) {
      int value = app::drag(id, x) * 100 / 127;
      int *values[] = {&ui.pulses,  &ui.rotation, &ui.density,
                       &ui.vel_var, &ui.prob_var, &ui.ratchet_chance};
      if (id <= 84)
        value = app::drag(id, x) *
                (view.patterns[view.selected_pattern].length - (id == 84)) /
                127;
      *values[id - 83] = value;
      dirty[id] = true;
      return;
    }
    if (ui.tool_section == 1 && id == 93) {
      ui.mutate_amount = app::drag(id, x) * 100 / 127;
      dirty[id] = true;
      return;
    }
    if (ui.tool_section == 1 && id == 94)
      return;
    if (!initial)
      return;
    app::Command c{};
    bool execute = ui.tool_action(id, view, c);
    if (execute && !send(c)) {
      ui = previous;
      return;
    }
    dirty[79] = true;
    if (previous.selected != ui.selected)
      dirty[45] = true;
    if (previous.tool_section != ui.tool_section)
      for (int i = 80; i <= 94; ++i)
        dirty[i] = true;
    if (execute)
      for (int i = 0; i < 16; ++i)
        dirty[i] = true;
    if (execute) {
      unsigned cells = 0;
      for (int i = 0; i < 16; ++i)
        cells += dirty[i];
      tool_cell_max = std::max(tool_cell_max, uint32_t(cells));
      tool_edit_full += full && !was_full;
    }
    if (execute && c.kind == app::Kind::Duplicate)
      dirty[44] = true;
    return;
  } else if (id >= 68) {
    if (id == 68 && initial) {
      if (ui.selected_step < 0)
        ui.selected_step = 0;
      ui.page = app::Page::Step;
      full = true;
      return;
    }
    app::Command c{};
    c.pattern = uint8_t(view.selected_pattern);
    c.track = uint8_t(ui.selected);
    c.step = uint8_t(std::max(0, ui.selected_step));
    auto &m = view.patterns[c.pattern].meta[c.track][c.step];
    if (id == 69) {
      c.kind = app::Kind::Swing;
      c.value = 50 + app::drag(id, x) * 25 / 127;
    } else if (id == 70 && initial) {
      c.kind = app::Kind::Step;
      c.value = c.step;
    } else if (id == 71) {
      c.kind = app::Kind::Velocity;
      c.value = std::max(1, app::drag(id, x));
    } else if (id == 72 && initial) {
      c.kind = app::Kind::Velocity;
      c.value = m.velocity == 127 ? 100 : 127;
    } else if (id == 73) {
      c.kind = app::Kind::Probability;
      c.value = app::drag(id, x) * 100 / 127;
    } else if (id >= 74 && initial) {
      c.kind = app::Kind::Ratchet;
      c.value = id - 73;
    } else
      return;
    if (send(c)) {
      dirty[id] = true;
      if (id == 71 || id == 72)
        dirty[71] = dirty[72] = true;
      if (id >= 74)
        for (int i = 74; i <= 77; ++i)
          dirty[i] = true;
    }
  } else if (initial && ui.page == app::Page::Pattern && id >= 46) {
    int selected = view.selected_pattern, queued = view.queued_pattern,
        playing = view.playing_pattern;
    int copy = ui.copy_source, clear = ui.clear_pattern;
    app::Ui previous = ui;
    app::Command c{};
    bool has_command = ui.pattern_action(id, view, c);
    if (has_command && !send(c)) {
      ui = previous;
      return;
    }
    if (has_command) {
      if (c.kind == app::Kind::SelectPattern ||
          c.kind == app::Kind::InspectPattern) {
        dirty[46 + selected] = dirty[46 + view.selected_pattern] = true;
        if (queued >= 0)
          dirty[46 + queued] = true;
        if (view.queued_pattern >= 0)
          dirty[46 + view.queued_pattern] = true;
        dirty[46 + playing] = dirty[46 + view.playing_pattern] = dirty[44] =
            true;
        dirty[63] = true;
      } else if (c.kind == app::Kind::CopyPattern) {
        dirty[46 + c.value] = true;
        if (c.value == view.selected_pattern)
          dirty[63] = true;
      } else if (c.kind == app::Kind::ClearPattern)
        dirty[46 + c.pattern] = true;
      else if (c.kind == app::Kind::PatternLength)
        dirty[63] = true;
    }
    if (copy != ui.copy_source)
      dirty[65] = true;
    if (clear != ui.clear_pattern)
      dirty[66] = true;
    if (id == 67)
      dirty[67] = true;
  } else if (id == 44 && initial) {
    if (ui.page != app::Page::Pattern) {
      ui.cancel_pattern_action();
      ui.cancel_tool();
      ui.page = app::Page::Pattern;
      full = true;
    }
  } else if (id == 43 && initial) {
    send({app::Kind::Solo, uint8_t(ui.selected),
          !(view.solos & (1u << ui.selected))});
    dirty[43] = true;
    dirty[16 + ui.selected % 8] = true;
  } else if (id >= 38 && id <= 42 && initial) {
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
    app::Command c{app::Kind::Step, uint8_t(ui.selected), id};
    c.pattern = uint8_t(view.selected_pattern);
    if (send(c)) {
      int old = ui.selected_step;
      ui.selected_step = id;
      dirty[id] = dirty[68] = true;
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
    dirty[45] = true;
  } else if (id < 29) {
    app::Kind kinds[] = {app::Kind::Volume, app::Kind::Pan, app::Kind::Pitch,
                         app::Kind::Length, app::Kind::Wave};
    int val = app::drag(id, x);
    if (id == 28)
      val = val * 15 / 127;
    auto &t = view.tracks[ui.selected];
    if (!app::track_control_enabled(t, id))
      return;
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
        ui.cancel_pattern_action();
        ui.cancel_tool();
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
      dirty[16 + ui.selected % 8] = true;
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
          view.tracks[t].assigned();
          if (int(t) == ui.selected)
            dirty[43] = dirty[106] = true;
          if (int(t) == ui.selected)
            for (int id = 24; id < 29; ++id)
              dirty[id] = true;
          if (int(t / 8) == ui.bank)
            dirty[16 + t % 8] = true;
        }
    }
#if P4SDM_APP_STRESS
    static uint32_t tone_due = 0;
    static unsigned tone_edit = 0;
    if (millis() >= tone_due) {
      tone_due = millis() + 80; // realistic control rate, bounded commands
      unsigned n = tone_edit++;
      uint8_t t = uint8_t(n % 16);
      bool dense = millis() - started < 12000;
      send({app::Kind::FilterCutoff, t,
            dense ? int(20 + n % 91) : int(n % 128)});
      send({app::Kind::FilterResonance, t,
            dense ? int(64 + n % 64) : int((n * 7) % 128)});
      send({app::Kind::DelaySend, t, dense ? 127 : int((n * 13) % 128)});
      if (!dense && n % 64 == 0)
        interact(113, 0, true);
      else if (!dense && ui.page == app::Page::Tone && !full) {
        interact(107 + int(n % 3), 24 + int(n % 128) * 751 / 127, false);
        if (n % 8 == 0)
          interact(111, 0, true);
      }
    }
    static uint32_t lock_due = 0;
    static unsigned lock_edit = 0;
    if (millis() - started >= 12000 && millis() >= lock_due) {
      lock_due = millis() + 120;
      unsigned n = lock_edit++;
      app::Command c{app::Kind(int(app::Kind::LockPitch) + n % 4),
                     uint8_t(n % 16), int(n % 128)};
      c.pattern = uint8_t(n % 2);
      c.step = uint8_t((n / 2) % 16);
      if (n % 5 == 0)
        c.kind = app::Kind::ClearStepLocks;
      send(c);
      if (n % 32 == 0) {
        ui.page = app::Page::Step;
        ui.selected_step = int((n / 2) % 16);
        interact(95, 0, true);
      } else if (ui.page == app::Page::Locks && n % 32 < 10 && !full) {
        int id = 97 + int(n % 4) * 2;
        interact(id, 0, true);
        interact(id - 1, app::widget(id - 1).x + 270, true);
        if (n % 32 == 9)
          interact(104, 0, true);
      }
    }
    static uint32_t stress_due = 0, retrigger_due = 0;
    static unsigned action = 0;
    static uint32_t tools_due = 0;
    static unsigned tool_action = 0;
    if (millis() >= tools_due) {
      tools_due = millis() + 350;
      // During the initial dense 4x phase, operate on all playing lanes.
      app::Command edit{app::Kind::Rotate, 0, 1};
      edit.pattern = 0;
      if (millis() - started < 12000) {
        edit.step = 1;
        unsigned op = tool_action++ % 5;
        edit.kind = op == 0   ? app::Kind::Reverse
                    : op == 1 ? app::Kind::Duplicate
                    : op == 2 ? app::Kind::CopyTrack
                              : app::Kind::Rotate;
        edit.value = tool_action & 1 ? -1 : 1;
        if (edit.kind == app::Kind::Duplicate)
          edit.value = 4;
        if (edit.kind == app::Kind::CopyTrack)
          edit.value = 1;
      } else {
        const app::Kind kinds[] = {
            app::Kind::Rotate,     app::Kind::Reverse,   app::Kind::CopyTrack,
            app::Kind::ClearTrack, app::Kind::Duplicate, app::Kind::Euclidean,
            app::Kind::Randomize,  app::Kind::Mutate,    app::Kind::Reroll};
        edit.kind = kinds[tool_action++ % 9];
        edit.pattern = 4; // Preserve M8's dense and mixed reference patterns.
        edit.track = 2;
        if (edit.kind == app::Kind::Duplicate)
          edit.value = 5;
        else if (edit.kind == app::Kind::Euclidean) {
          edit.value = 7;
          edit.step = 2;
        } else if (edit.kind == app::Kind::Randomize)
          edit.value = app::generation_parameters(45, 20, 15, 5);
        else if (edit.kind == app::Kind::Mutate)
          edit.value = 20;
      }
      send(edit);
      for (int i = 0; i < 16; ++i)
        dirty[i] = true;
      dirty[44] = dirty[79] = true;
    }
    if (millis() - started >= 12000 && millis() >= retrigger_due) {
      retrigger_due = millis() + 3000;
      for (int t = 0; t < 16; ++t)
        send({app::Kind::Trigger, uint8_t(t), 0});
    }
    if (millis() - started >= 12000 && millis() >= stress_due) {
      stress_due = millis() + 250;
      const unsigned slot = action++ % 48;
      // Continuous transport. Both real loop transitions and queue replacement
      // occur, with a long and a short pattern, under normal UI/command load.
      if (slot == 0) {
        send({app::Kind::SelectPattern, 0, 0});
        send({app::Kind::Swing, 0, 60});
        interact(44, 0, true);
        interact(46, 0, true);
      } else if (slot == 8) {
        send({app::Kind::SelectPattern, 0, 2});
        send({app::Kind::SelectPattern, 0, 1});
        interact(44, 0, true);
        interact(48, 0, true);
        interact(47, 0, true);
      } else if (slot == 20) {
        send({app::Kind::SelectPattern, 0, 0});
        send({app::Kind::Swing, 0, 75});
        interact(44, 0, true);
        interact(46, 0, true);
      } else if (slot == 24 || slot == 25)
        interact(62, 0, true);
      else if (slot == 26 || slot == 27)
        interact(64, 0, true);
      else if (slot == 28) {
        interact(44, 0, true);
        interact(65, 0, true);
        interact(49, 0, true);
      } else if (slot == 29) {
        interact(67, 0, true);
        interact(49, 0, true);
      } else if (slot == 30) {
        app::Command clear{app::Kind::ClearPattern, 0, 0};
        clear.pattern = 4; // Guarantee coverage despite independent page scripts.
        send(clear);
        interact(66, 0, true);
        interact(66, 0, true);
      } else if (slot == 31) {
        interact(67, 0, true);
        interact(46, 0, true);
      } else if (slot == 32)
        interact(31, 0, true);
      else if (slot == 33 || slot == 34)
        interact(37, 0, true);
      else if (slot == 35 || slot == 36)
        interact(43, 0, true);
      else if (slot == 37)
        interact(32, 0, true);
      else if (slot == 38)
        interact(30, 0, true);
      else if (slot == 39 || slot == 40)
        interact(5, 0, true);
      else if (slot == 41)
        interact(33, 0, true);
      else if (slot == 42) {
        interact(16, 0, true);
        send({app::Kind::Trigger, uint8_t(ui.selected), 0});
      } else if (slot == 43)
        interact(31, 0, true);
      else if (slot == 44)
        interact(24, 700, false);
      else if (slot == 45) {
        interact(30, 0, true);
        interact(68, 0, true);
      } else if (slot == 46) {
        interact(71, 350, false);
        interact(73, 600, false);
      } else if (slot == 47) {
        interact(72, 0, true);
        interact(77, 0, true);
        interact(30, 0, true);
        interact(78, 0, true);
        interact(81, 0, true);
        interact(85, 100, false);
        interact(90, 0, true);
        interact(80, 0, true);
        interact(86, 0, true);
        interact(89, 0, true);
        interact(90, 0, true);
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
        if (ui.page == app::Page::Track && ui.capture >= 24 &&
            ui.capture < 29 &&
            !app::track_control_enabled(view.tracks[ui.selected], ui.capture))
          ui.capture = -1;
        interact(ui.capture, touch_state.x, true);
      } else if (touch_state.pressed &&
                 ((ui.capture >= 107 && ui.capture <= 109) ||
                  (ui.capture >= 24 && ui.capture < 29) || ui.capture == 69 ||
                  ui.capture == 71 || ui.capture == 73 ||
                  (ui.page == app::Page::Tools && ui.tool_section == 1 &&
                   ((ui.capture >= 83 && ui.capture <= 88) ||
                    ui.capture == 93))))
        interact(ui.capture, touch_state.x, false);
      ui.down = touch_state.pressed;
      if (!ui.down)
        ui.capture = -1;
    }
    touch_errors = input_errors.load(std::memory_order_relaxed);
    uint32_t status = pattern_status.load(std::memory_order_acquire);
    if (uint16_t(status >> 16) == sent_commands) {
      int playing = status & 15, queued = int((status >> 4) & 31) - 1;
      if (playing != view.playing_pattern || queued != view.queued_pattern) {
        dirty[46 + view.playing_pattern] = dirty[46 + playing] = true;
        if (view.queued_pattern >= 0)
          dirty[46 + view.queued_pattern] = true;
        if (queued >= 0)
          dirty[46 + queued] = true;
        dirty[44] = true;
        view.playing_pattern = playing;
        view.queued_pattern = queued;
      }
    }
    int head = view.selected_pattern == view.playing_pattern
                   ? playhead.load(std::memory_order_relaxed)
                   : -1;
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
          draw(68);
          draw(69);
          draw(78);
        } else if (ui.page == app::Page::Tools) {
          for (int i = 79; i <= 94; ++i)
            draw(i);
        } else if (ui.page == app::Page::Locks) {
          for (int i = 96; i <= 105; ++i)
            draw(i);
        } else if (ui.page == app::Page::Step) {
          draw(95);
          for (int i = 70; i <= 77; ++i)
            draw(i);
        } else if (ui.page == app::Page::Tone) {
          for (int i = 106; i <= 112; ++i)
            draw(i);
        } else if (ui.page == app::Page::Track) {
          draw(113);
          for (int i = 24; i < 29; ++i)
            draw(i);
          char title[40];
          snprintf(title, sizeof(title), "TRACK %02d / %s", ui.selected + 1,
                   view.tracks[ui.selected].sample ? "SAMPLE" : "SYNTH");
          display::text(24, 100, title, white, 1);
          draw(37);
          draw(43);
          draw(42);
        } else if (ui.page == app::Page::Pattern) {
          for (int i = 46; i <= 67; ++i)
            draw(i);
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
          tempo_header();
        if (dirty[44])
          draw(44);
        if (dirty[45]) {
          char title[24];
          display::rect(0, 16, 176, 56, bg);
          snprintf(title, sizeof(title), "P4SDM T%02d", ui.selected + 1);
          display::text(16, 24, title, white, 2);
        }
        for (int i = 0; i < 114; ++i)
          if (dirty[i] &&
              (((i >= 106 && i <= 112 && ui.page == app::Page::Tone) ||
                (i == 113 && ui.page == app::Page::Track)) ||
               (i < 24 && ui.page == app::Page::Sequence) ||
               (i >= 24 && i < 29 && ui.page == app::Page::Track) ||
               (i >= 29 && i < 34) || (i == 36 && ui.page == app::Page::Fx) ||
               ((i == 37 || i == 42 || i == 43) &&
                ui.page == app::Page::Track) ||
               (i >= 46 && i <= 67 && ui.page == app::Page::Pattern) ||
               ((i == 68 || i == 69 || i == 78) &&
                ui.page == app::Page::Sequence) ||
               ((i == 95 || (i >= 70 && i <= 77)) &&
                ui.page == app::Page::Step) ||
               (i >= 96 && i <= 105 && ui.page == app::Page::Locks) ||
               (i >= 79 && i <= 94 && ui.page == app::Page::Tools)))
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
    if (!reported && millis() - started >= 63000 &&
        audio_summary_ready.load(std::memory_order_acquire)) {
      reported = true;
      if (display::wait_idle() != ESP_OK)
        vTaskSuspend(nullptr);
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
      summary_printf(
          "[M5 UI] seconds=%.3f submitted=%u completed=%u "
          "dirty_avg=%u dirty_max=%u full=%u preparation_max_us=%u "
          "touch_errors=%u rejected=%u pads=%u steps=%u drags=%u "
          "pages=%u transport=%u bpm=%u skipped=%u\n",
          (millis() - started) / 1000., frames,
          display::completed_presentations() - completed_start,
          unsigned(frames ? dirty_bytes / frames : 0), dirty_max, full_frames,
          prep_max, touch_errors, rejected + input_overflow.load(), pads, steps,
          drags, pages, actions[29], actions[34] + actions[35], skipped_frames);

      summary_printf(
          "[M7 patterns] switches=%u loops=%u selections=%u copies=%u "
          "clears=%u lengths=%u solos=%u mutes=%u normal_frames=%u "
          "dirty_avg=%u dirty_max=%u pattern_bytes=%u command_bytes=%u\n",
          pattern_switches.load(), pattern_loops.load(),
          actions[46] + actions[47] + actions[48] + actions[49], actions[65],
          actions[66] / 2, actions[62] + actions[64], actions[43], actions[37],
          normal_frames,
          unsigned(normal_frames ? normal_dirty_bytes / normal_frames : 0),
          normal_dirty_max, unsigned(sizeof(app::Pattern)),
          unsigned(sizeof(app::Command)));

      summary_printf("[M7 applied] long_to_short=%u short_to_long=%u "
                     "queue_replacements=%u copies=%u clears=%u lengths=%u "
                     "solos=%u mutes=%u steps=%u\n",
                     long_to_short.load(), short_to_long.load(),
                     queue_replacements.load(),
                     applied_actions[unsigned(app::Kind::CopyPattern)].load(),
                     applied_actions[unsigned(app::Kind::ClearPattern)].load(),
                     applied_actions[unsigned(app::Kind::PatternLength)].load(),
                     applied_actions[unsigned(app::Kind::Solo)].load(),
                     applied_actions[unsigned(app::Kind::Mute)].load(),
                     applied_actions[unsigned(app::Kind::Step)].load());

      summary_printf("[M9 UI] entries=%u copies=%u randomize=%u cell_max=%u "
                     "edit_full=%u\n",
                     actions[78], actions[86], actions[90], tool_cell_max,
                     tool_edit_full);
      summary_printf(
          "[M11 UI] entries=%u edits=%u widgets_max=%u edit_full=%u\n",
          actions[113], tone_ui_edits, tone_ui_widgets_max, tone_ui_full);
      summary_printf(
          "[M10 UI] entries=%u edits=%u widgets_max=%u edit_full=%u\n",
          actions[95], lock_ui_edits, lock_ui_widgets_max, lock_ui_full);

      summary_printf("[M5 memory] ps_before=%u ps_after=%u internal_before=%u "
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
#if P4SDM_SAMPLER_QUALIFICATION
    static unsigned previous_phase = 0, blocks = 0;
    unsigned phase =
        samples::qualification_phase.load(std::memory_order_acquire);
    if (phase != previous_phase) {
      qualification_finished.store(previous_phase, std::memory_order_release);
      previous_phase = phase;
      blocks = 0;
      if (phase) {
        engine.apply({app::Kind::Play, 0, 1});
        for (unsigned t = 0; t < 16; ++t) {
          engine.patterns[0].track_steps[t] = 0xffff;
          engine.apply({app::Kind::Pitch, uint8_t(t),
                        sampler::qualification_pitch(phase)});
          trigger(t);
        }
      }
    }
    if (phase == 4 && ++blocks % 8 == 0)
      for (int t = 0; t < 16; ++t)
        trigger(t);
    const unsigned metric =
        phase == 6 && samples::load_active.load() ? 7 : phase;
#endif
    int sample_track = samples::transfer.consume(samples::voices);
    if (sample_track >= 0) {
      unsigned t = unsigned(sample_track);
      engine.tracks[t].assigned();
      PITCH[t] = 255;
      AMP[t] = 0;
      FILTROS[t].reset();
    }
    app::Command c;
    static uint16_t applied_commands = 0;
    bool transformed = false, tone_edited = false;
    for (unsigned n = 0; n < 16 && commands.pop(c); ++n) {
      engine.apply(c);
      transformed |= c.kind >= app::Kind::Rotate && c.kind <= app::Kind::Mutate;
      ++applied_commands;
      applied_actions[unsigned(c.kind)].fetch_add(1, std::memory_order_relaxed);
      if (c.kind == app::Kind::Source) {
        voice_event[c.track] = {};
        samples::voices[c.track].stop();
        FILTROS[c.track].reset();
        PITCH[c.track] = 255;
        AMP[c.track] = 0;
      }
      if (c.kind == app::Kind::Trigger)
        engine.audition(c.track, [](app::TriggerEvent e) { trigger(e); });
      else if (c.kind == app::Kind::Delay) {
        is_delay = delay_ready && engine.delay;
        delays = is_delay ? 0xffff : 0;
      } else if (c.kind >= app::Kind::FilterCutoff &&
                 c.kind <= app::Kind::DelaySend) {
        tone_edited = true;
        unsigned i = unsigned(c.kind) - unsigned(app::Kind::FilterCutoff);
        const auto &track = engine.tracks[c.track];
        const unsigned values[] = {track.filter_cutoff, track.filter_resonance,
                                   track.delay_send};
        ++tone_metrics.edits[i];
        tone_metrics.minimum[i] = std::min(tone_metrics.minimum[i], values[i]);
        tone_metrics.maximum[i] = std::max(tone_metrics.maximum[i], values[i]);
        synthESP32_setTrackTone(c.track, engine.tracks[c.track].filter_cutoff,
                                engine.tracks[c.track].filter_resonance);
      } else if (c.kind >= app::Kind::Volume && c.kind <= app::Kind::Wave)
        update_track(c.track);
    }
    for (unsigned n = 0; n < 16 && pad_triggers.pop(c); ++n)
      engine.audition(c.track, [](app::TriggerEvent e) { trigger(e); });
    render_buffer();
    auto us = uint32_t(esp_timer_get_time() - start);
    unsigned b = captured.load(std::memory_order_relaxed);
    if (b < capture_blocks) {
      if (tone_edited) {
        ++tone_metrics.blocks;
        tone_metrics.worst = std::max(tone_metrics.worst, us);
      }
      bool tone_dense = b < worst_blocks && engine.bpm == 240 && engine.delay;
      for (int t = 0; t < 16 && tone_dense; ++t) {
        const auto &v = engine.tracks[t];
        tone_dense &= v.filter_cutoff >= 20 && v.filter_cutoff <= 110 &&
                      v.filter_resonance >= 64 && v.delay_send == 127;
      }
      if (tone_dense) {
        ++tone_metrics.dense;
        tone_metrics.dense_worst = std::max(tone_metrics.dense_worst, us);
      }
      if (b + 1 == capture_blocks)
        tone_result = tone_metrics;
      if (transformed) {
        transform_max.store(std::max(transform_max.load(), us));
        transform_blocks.fetch_add(1);
        bool dense = engine.bpm == 240 && engine.delay;
        for (int t = 0; t < 16 && dense; ++t) {
          const auto &p = engine.patterns[engine.playing_pattern];
          dense = p.track_steps[t] == 0xffff;
          for (const auto &m : p.meta[t])
            dense &= m.ratchets == 4;
        }
        if (dense)
          dense_transform_blocks.fetch_add(1);
      }
      unsigned active = 0;
      for (unsigned t = 0; t < 16; ++t)
        active += (PITCH[t] != 255 && AMP[t] != 0) || samples::voices[t].active;
      active_min = std::min(active_min, active);
      active_max = std::max(active_max, active);
      render_times[b] = us;
      if (b < worst_blocks) {
        worst_max = std::max(worst_max, us);
        worst_triggers = engine.probability_passed + engine.ratchet_events;
        bool locked =
            engine.playing_pattern == 0 && engine.bpm == 240 && engine.delay;
        for (int t = 0; t < 16 && locked; ++t) {
          const auto &p = engine.patterns[0];
          locked &= p.track_steps[t] == 0xffff;
          for (int i = 0; i < 16; ++i)
            locked &= p.meta[t][i].ratchets == 4 && p.locks[t][i].mask == 15;
        }
        if (locked) {
          ++locked_dense_blocks;
          locked_dense_max = std::max(locked_dense_max, us);
        }
      }
      if (b + 1 == capture_blocks) {
        lock_result[0] = engine.locked_parents;
        lock_result[1] = engine.unlocked_parents;
        for (int i = 0; i < 4; ++i)
          lock_result[i + 2] = engine.lock_events[i];
      }
      if (b + 1 == capture_blocks)
        groove_result = {engine.step_events,         engine.probability_passed,
                         engine.probability_skipped, engine.ratchet_events,
                         engine.pending_max,         engine.swing_changes,
                         engine.velocity_min,        engine.velocity_max};
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
    pattern_status.store((uint32_t(applied_commands) << 16) |
                             unsigned(engine.playing_pattern) |
                             (unsigned(engine.queued_pattern + 1) << 4),
                         std::memory_order_release);
    pattern_switches.store(engine.switches);
    pattern_loops.store(engine.loops);
    long_to_short.store(engine.long_to_short);
    short_to_long.store(engine.short_to_long);
    queue_replacements.store(engine.queue_replacements);
    auto err = audio::write(out_buf, DMA_BUF_LEN);
#if P4SDM_SAMPLER_QUALIFICATION
    if (metric && qualification[metric].count < 4096) {
      auto &m = qualification[metric];
      m.times[m.count++] = us;
      m.misses += us >= double(DMA_BUF_LEN) * 1e6 / SAMPLE_RATE;
      m.failures += err != ESP_OK;
      m.timeouts += err == ESP_ERR_TIMEOUT;
      unsigned active = 0;
      for (auto &v : samples::voices)
        active += v.active;
      m.active_max = std::max(m.active_max, uint32_t(active));
      for (int i = 0; i < DMA_BUF_LEN * 2; ++i) {
        m.nonzero += out_buf[i] != 0;
        m.rails += out_buf[i] == -32768 || out_buf[i] == 32767;
      }
    }
#endif
    if (b < capture_blocks) {
      write_errors += err != ESP_OK;
      timeouts += err == ESP_ERR_TIMEOUT;
      captured.store(b + 1, std::memory_order_release);
    }
    if (err != ESP_OK) {
#if P4SDM_SAMPLER_QUALIFICATION
      qualification_finished.store(phase, std::memory_order_release);
      Serial.printf("[M6.1 audio FAILED] phase=%u error=%d\n", phase, int(err));
#endif
      vTaskSuspend(nullptr);
    }
  }
}
} // namespace
static bool app_pcm(int t, int16_t &v) {
  if (!engine.tracks[t].sample)
    return false;
  v = samples::voices[t].next();
  return true;
}
static int16_t app_velocity(int t, int16_t v) {
  return app::scale_velocity(v, event_gain[t]);
}
static void app_sample() {
  engine.sample([](app::TriggerEvent event) { trigger(event); });
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
#if P4SDM_APP_STRESS
    engine.tracks[t].filter_cutoff = view.tracks[t].filter_cutoff =
        uint8_t(30 + t * 4);
    engine.tracks[t].filter_resonance = view.tracks[t].filter_resonance = 96;
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
#if P4SDM_APP_STRESS
  for (int t = 0; t < 16; ++t) {
    engine.patterns[0].track_steps[t] = view.patterns[0].track_steps[t] =
        0xffff;
    engine.patterns[1].track_steps[t] = view.patterns[1].track_steps[t] =
        0xffff;
    for (int step = 0; step < 16; ++step) {
      engine.patterns[0].meta[t][step] =
          view.patterns[0].meta[t][step] = {127, 100, 4};
      engine.patterns[0].locks[t][step] = view.patterns[0].locks[t][step] = {
          15, uint8_t(36 + (t + step) % 48), uint8_t(40 + (t * 7 + step) % 88),
          int8_t((t * 17 + step * 13) % 255 - 127), uint8_t((t + step) % 16)};
      engine.patterns[1].locks[t][step] = view.patterns[1].locks[t][step] =
          engine.patterns[0].locks[t][step];
      app::StepMeta mixed{uint8_t(1 + (t * 17 + step * 23) % 127),
                          uint8_t((t + step) % 3 == 0   ? 0
                                  : (t + step) % 3 == 1 ? 65
                                                        : 100),
                          uint8_t(1 + (t + step) % 4)};
      engine.patterns[1].meta[t][step] = view.patterns[1].meta[t][step] = mixed;
    }
  }
  engine.patterns[1].meta[0][0] = view.patterns[1].meta[0][0] = {1, 100, 4};
  engine.patterns[1].length = view.patterns[1].length = 7;
  engine.apply({app::Kind::Bpm, 0, 240});
  view.apply({app::Kind::Bpm, 0, 240});
  engine.apply({app::Kind::Play, 0, 1});
  view.apply({app::Kind::Play, 0, 1});
#endif
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
#if P4SDM_SAMPLER_QUALIFICATION
  static unsigned reported_modes = 0;
  unsigned finished = qualification_finished.load(std::memory_order_acquire);
  const char *modes[] = {"",
                         "UNITY",
                         "DOWN",
                         "UP",
                         "RETRIGGER",
                         "BASELINE",
                         "LOAD_WINDOW_IDLE",
                         "SD_LOAD_INTERVAL"};
  for (unsigned mode = 1; mode <= 7; ++mode) {
    if (!(reported_modes & (1u << mode)) &&
        (mode <= finished || (mode == 7 && finished == 6))) {
      auto &m = qualification[mode];
      if (!m.count)
        continue;
      std::sort(m.times, m.times + m.count);
      Serial.printf("[M6.1 audio] mode=%s blocks=%u p50=%u p95=%u p99=%u "
                    "max=%u misses=%u failures=%u timeouts=%u nonzero=%u "
                    "rails=%u active_max=%u\n",
                    modes[mode], m.count, m.times[m.count / 2],
                    m.times[m.count * 95 / 100], m.times[m.count * 99 / 100],
                    m.times[m.count - 1], m.misses, m.failures, m.timeouts,
                    m.nonzero, m.rails, m.active_max);
      reported_modes |= 1u << mode;
    }
  }
#endif
  static bool reported = false;
  if (!reported && captured.load(std::memory_order_acquire) == capture_blocks) {
    reported = true;
    static uint32_t sorted[capture_blocks];
    std::copy(render_times, render_times + capture_blocks, sorted);
    std::sort(sorted, sorted + capture_blocks);
    diagnostic_p99.store(sorted[capture_blocks * 99 / 100]);
    diagnostic_max.store(sorted[capture_blocks - 1]);
    diagnostic_misses.store(misses);
    summary_printf("[M5] blocks=%u p50=%u p95=%u p99=%u max=%u misses=%u "
                   "failures=%u timeouts=%u peak=%u nonzero=%u rails=%u "
                   "active_min=%u active_max=%u\n",
                   capture_blocks, sorted[capture_blocks / 2],
                   sorted[capture_blocks * 95 / 100],
                   sorted[capture_blocks * 99 / 100],
                   sorted[capture_blocks - 1], misses, write_errors, timeouts,
                   peak, nonzero, rails, active_min, active_max);

    summary_printf(
        "[M8 groove] parents=%u passed=%u skipped=%u ratchets=%u "
        "pending_max=%u velocity_min=%u velocity_max=%u swing_changes=%u\n",
        groove_result.parents, groove_result.passed, groove_result.skipped,
        groove_result.ratchets, groove_result.pending_max,
        groove_result.velocity_min, groove_result.velocity_max,
        groove_result.swing_changes);

#if P4SDM_APP_STRESS
    summary_printf(
        "[M8 worst] blocks=%u bpm=240 voices=16 ratchet=4 swing=50 delay=1 "
        "max=%u triggers=%u triggers_per_second=%u\n",
        worst_blocks, worst_max, worst_triggers,
        unsigned(uint64_t(worst_triggers) * 44100 / (worst_blocks * 256)));

#endif
    guition::memory_report("M8 after 60s");

    summary_printf(
        "[M10 locks] locked=%u unlocked=%u pitch=%u volume=%u pan=%u wave=%u "
        "pattern_bytes=%u bank_bytes=%u engine_bytes=%u\n",
        lock_result[0], lock_result[1], lock_result[2], lock_result[3],
        lock_result[4], lock_result[5], unsigned(sizeof(app::Pattern)),
        unsigned(sizeof(engine.patterns)), unsigned(sizeof(engine)));
    summary_printf(
        "[M11 tone] cutoff=%u resonance=%u send=%u cutoff_min=%u cutoff_max=%u "
        "resonance_min=%u resonance_max=%u send_min=%u send_max=%u blocks=%u "
        "max=%u dense=%u dense_max=%u\n",
        tone_result.edits[0], tone_result.edits[1], tone_result.edits[2],
        tone_result.minimum[0], tone_result.maximum[0], tone_result.minimum[1],
        tone_result.maximum[1], tone_result.minimum[2], tone_result.maximum[2],
        tone_result.blocks, tone_result.worst, tone_result.dense,
        tone_result.dense_worst);
    summary_printf("[M10 worst] blocks=%u max=%u\n", locked_dense_blocks,
                   locked_dense_max);
    summary_printf(
        "[M9 tools] rotate=%u reverse=%u copy_track=%u clear_track=%u "
        "duplicate=%u euclidean=%u randomize=%u mutate=%u reroll=%u "
        "blocks=%u dense_blocks=%u max=%u\n",
        applied_actions[unsigned(app::Kind::Rotate)].load(),
        applied_actions[unsigned(app::Kind::Reverse)].load(),
        applied_actions[unsigned(app::Kind::CopyTrack)].load(),
        applied_actions[unsigned(app::Kind::ClearTrack)].load(),
        applied_actions[unsigned(app::Kind::Duplicate)].load(),
        applied_actions[unsigned(app::Kind::Euclidean)].load(),
        applied_actions[unsigned(app::Kind::Randomize)].load(),
        applied_actions[unsigned(app::Kind::Mutate)].load(),
        applied_actions[unsigned(app::Kind::Reroll)].load(),
        transform_blocks.load(), dense_transform_blocks.load(),
        transform_max.load());

    audio_summary_ready.store(true, std::memory_order_release);
  }
  delay(1000);
}
