# M5 — Guition internal-synth application shell

Status: implemented; scripted integrated hardware checks pass; user confirmed functioning image and touch on 2026-10-06. M4.1 remains qualified separately.

## Structure and ownership

`src/app/model.h` contains portable track/transport state, rational sample clock, bounded SPSC command ring and touch geometry. `src/app_main.cpp` adapts those to the original synth and the existing display/touch/audio HAL. `guition_app` is separate from every preserved diagnostic target. The only modification to the original mixer is a compile-time M5 sample-clock hook; other targets compile the original path.

Audio on core0 at maximum priority is the sole writer of engine state, ROTvalue and DSP setters after startup. UI on core0 at priority 2 owns selection, page, bank, capture, predicted control values and dirty widget flags. Its 64-slot queue holds at most 63 commands and never overwrites; rejection is counted and the predicted value is not changed on rejection. Audio applies at most sixteen UI commands and sixteen touch pad triggers at each block boundary. Transport/BPM/step/volume/pan/pitch/length/wave/mute/Delay all cross that queue. Pads cross as trigger commands. Audio publishes the playhead and per-track trigger epochs through atomics. No I2C/display/Serial/allocation occurs in render. Diagnostic arrays are fixed storage and become readable after release/acquire publication of capture completion.

UI mirrors only successfully enqueued commands; it never reads mutable engine globals. Delay sends can only activate after successful Delay initialization. STOP stops new sequencer notes and leaves existing envelopes and FX tails.

## Native 800×480 layout

Dark neutral background, bright text, teal active/selected widgets and yellow playhead/trigger flash. Top bar: fixed pattern placeholder, selected track, BPM and 56/64-pixel tempo buttons. Steps form two rows of eight 88×58 cells (rather than sixteen narrow cells). Eight pads form two rows of four 184×62 cells; BANK A/B selects tracks 1–8 or 9–16. Bottom: PLAY/STOP, SEQ, TRACK, FX and bank. TRACK has five 520×52 sliders plus mute. FX exposes ready-gated Delay enable with qualified fixed DSP settings. Minimum actual target is 52 pixels high; steps use 58 pixels. Slider capture persists outside its rectangle and clamps, preventing gestures from moving into other widgets. Pads/steps act once per press; continuous controls act only when mapped values change.

A source-derived layout reference is provided in `GUITION_M5_LAYOUT.svg`; it is not a hardware screenshot. The user confirmed that the displayed image and touch work on 2026-10-06. No physical screenshot/photo is attached. This layout description reflects source geometry, not visual qualification. Sounds are temporary distinct MIDI pitches and wavetables, not a finished drum synthesizer.

## Sequencing and rendering

At 44.1 kHz each sample adds BPM×4 to an integer phase with threshold 44,100×60. A step starts on sample zero, then every sixteenth; subtracting the threshold preserves fractional timing without drift. BPM changes retain phase. PLAY restarts at step 1. Loop is fixed sixteen steps. Timing quantization is less than one sample, independent of UI rate; actual hardware timing/jitter has not yet been qualified.

NativeQueued remains unchanged: three native framebuffers, 16×16 dirty tiles and two-refresh retirement. Widgets redraw only their own rectangles; playhead changes redraw old/new steps. Selection redraws the visible step set and selected pads, flash expiration redraws its pad, page changes redraw the full page. A dedicated core1 priority-3 touch task polls every 8 ms and sleeps. It sends pad triggers through a separate 32-slot SPSC ring directly to audio, so display retirement cannot delay pad audio. UI input transitions/coordinates cross a fixed 128-slot SPSC ring; overflow is counted. UI sleeps, and presents only changes with at least 33 ms between frame submissions. Normal step animation therefore presents fewer than thirty frames per second. Capture diagnostics distinguish submissions from acknowledged presentations and record dirty bytes average/max, full-page count, frame preparation/presentation maximum, rejected commands, touch errors and interaction counts.

`P4SDM_APP_DIAGNOSTICS=1` enables a once-per-second small overlay (audio p99/max/misses become available after the initial capture). The default disables it. Capture reporting runs in low-priority contexts; no realtime Serial output.

## Qualification procedure and limitations

Build `guition_app`, upload to the connected board, capture genuine serial output using `tests/capture_guition.py`, and perform at least sixty seconds of PLAY/STOP, real step edits, both pad banks, all track sliders, BPM changes and page changes with Delay enabled. Exercise overlapping sounds on all sixteen tracks. Check audio output, GT911 errors, visual corruption/tearing and memory stability. The firmware captures 10,336 audio blocks (about sixty seconds), PCM peak/nonzero/rails, write errors/timeouts, render percentiles and deadline misses; UI reports after sixty-three seconds, with heap probes taken before reporting and after audio report completion. A silent/static run cannot qualify musical interaction or worst-case voice load. Restart before the interaction qualification so the full capture covers it.

User confirmation (2026-10-06): image and touch are functioning on the normal application firmware. This confirms functional display/touch operation, without claiming a recorded sixty-second physical interaction workload. Pending: recorded physical interaction coverage, sustained-animation FPS, audible confirmation and remote CI result. Host tests and local build results are recorded in VALIDATION.md. No SD, persistence, MIDI, networking or sampler integration was started.

**Is it usable and ready for SD/sample integration?** The interactive internal-synth shell is implemented, its integrated scripted workload passes, and the user confirms working image and touch. It is a functional baseline for the next milestone; full acceptance remains limited by the unrecorded physical interaction workload, audible confirmation and sustained-animation FPS. Stop at M5.

## Measured device result (2026-10-06)

`GUITION_M5_STRESS_SERIAL.log` is genuine COM13 capture of `guition_app_stress`, a compile-time scripted workload on the real board. The script invokes application actions, retriggers sixteen five-second voices every three seconds, and exercises transport, BPM, step/pad selection, sliders and frequent full page changes. GT911 polling runs, but there were no human touch events in this run. Scripted action counts must not be described as physical touch validation.

Audio: 10,336 blocks, active maximum sixteen (initial startup minimum zero), p50 3530 us, p95 3700 us, p99 3728 us, maximum 3780 us. Against the 5804.989 us block budget, worst reserve is 2024.989 us / 34.88%. Zero engine/render deadline misses, write failures, timeouts and PCM rails. Peak 6531, nonzero sample count 5,289,925. Timing includes bounded command application and rendering, not I2S blocking write.

UI: 895 submitted / 894 retired frames over 63.062 seconds (~14.18 acknowledged FPS). This workload changes pages about 2.7 times/sec; it does not establish sustained thirty-FPS animation qualification. 169 full-page draws, 199 missed cadence intervals, maximum preparation 114629 us. Average dirty bytes 213957 (27.86% of a full 768000-byte frame), maximum 768000 during page changes. Removing known full-page bytes from the aggregate gives approximately 84986 bytes / ordinary frame (11.1%); rounding of the published average introduces less than two bytes uncertainty. No display redesign was needed. Page drops did not cause audio misses.

Scripted interaction: 56 pad selections/triggers, 56 step toggles, 56 parameter updates, 224 navigation actions, 55 transport changes and 56 BPM changes; actual full-page transitions are fewer because tapping the current page does not invalidate it. Zero GT911 polling errors and zero queue rejections. No inference about touch ergonomics, audible quality, tearing or physical gesture latency is made from these counts.

Memory: PSRAM 30,885,508 bytes before/after, largest PSRAM block 30,408,692 before/after; internal free 435,676 before/after. No measured heap growth. `GUITION_M5_IDLE_SERIAL.log` is an earlier default-target static/silent capture: 10,336 blocks, p99 1695 us, maximum 1772 us, no errors, one submitted/retired frame. Its internal after-probe overlapped reporting and showed a transient 276-byte allocation; the final workload separates reporting from probes. `GUITION_M5_INITIAL_SERIAL.log` preserves the superseded initial capture that exposed startup pitch activation and static retirement accounting bugs; it is not acceptance evidence.

Local builds pass for all seven prior diagnostics, `guition_app` and `guition_app_stress`. All prior host regressions and new state/geometry/queue/sample-clock tests pass; the M5 captured-data analyzer passes. CI configuration compiles all nine environments and validates the genuine scripted log. Remote GitHub CI has not been dispatched from this local working tree.

`guition_app_stress` is solely a regression workload; the board is restored to normal `guition_app` after capture. The normal environment never performs scripted interaction. No storage milestone is started.
