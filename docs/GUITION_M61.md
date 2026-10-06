# M6.1 — sample source semantics and card qualification preparation

Continues `codex/m6-sampler` from `753bf3a4f79101fd7a3ec18ac984dbff312766df`. M6 architecture, native 4-bit SDMMC, LDO4 storage/LDO3 display, unused GPIO45, original 44.1 kHz audio, nearest-neighbor playback and Transfer ownership are retained. No presets/projects/song/recording/MIDI/networking were added.

## Pitch and source rule

Each track stores independent synth and sample pitches, plus an explicit `sample_configured` flag. `pitch` is the currently selected source's value. Source switching saves the outgoing pitch and restores the incoming pitch. Sample pitch starts at 60; the first successful publication marks the sample configured and selects MIDI60. Failed loads do not change the source or pitch. UI pitch commands carry their intended source, so a synth edit queued before asynchronous publication cannot detune the sample. Every later successful replacement selects SAMPLE while retaining its saved pitch, including replacement while SYNTH is selected. Switching back to synth restores its original pitch (track1 initially36); returning to sample restores its intentional tuning.

Both pad and sequencer triggers use the same clamped, allocation-free Q16 increment helper. MIDI60 is exactly65536 (1x), MIDI48 exactly32768 (0.5x), MIDI72 exactly131072 (2x); MIDI0 is2048 and MIDI127 is3142176. A 12-entry rounded semitone ratio table and octave shifts avoid the old rounded frequency/261.63 ratio. Playback remains nearest-neighbor, full-file forward one-shot, without interpolation.

TRACK keeps VOLUME/PAN/PITCH/LENGTH/WAVE for synth. SAMPLE shows VOLUME/PAN and `TUNE +0 st UNITY`, `TUNE -12 st`, etc. LENGTH/WAVE have explicit `SYNTH ONLY` placeholders and reject touches/drags. Source assignment dirties these controls so an open TRACK page updates. Source commands stop/reset the old sample voice and clear synth/filter activity; publication stops/replaces the voice before retirement acknowledgement.

## Optional device runner

Build/upload `guition_sampler_stress` explicitly; its sole activation flag is `P4SDM_SAMPLER_QUALIFICATION=1`. `guition_app` remains normal firmware. Generate fixtures with `python tests/generate_m6_fixtures.py <directory>` and copy all eight WAV files to `/P4SDM/SAMPLES/`. Reboot to mount/index; there is no card-detect pin or reliable insertion hotplug. Missing card/required fixtures cause one clear SKIP/PENDING message, with synth fallback still available.

The priority1/core0 storage worker automatically runs short_mono -> stereo -> long_mono -> short_mono on track1, waiting for audio acknowledgement and freeing retirement after every publication. It rejects unsupported_depth and truncated while checking the previous pointer remains assigned. Each load logs physical file/PCM bytes, measured read and publish duration, source PCM throughput, channels/downmix, payload/free/largest/internal RAM, PSRAM delta and retirement clearance. Equal-payload A/A compares free/largest with a4096-byte metadata/alignment tolerance and flags suspicious losses; this flag requires investigation, never an automatic allocator excuse. Mount and index boot rows log bus width, requested clock, capacity, durations, WAV count/truncation and memory once.

It next loads long_mono independently into all16 tracks. With Delay/display/touch active and all16 sequencer steps enabled, it runs12-second UNITY/DOWN/UP/RETRIGGER modes, followed by a unity BASELINE and a unity SD-load window replacing track1 with stereo then long_mono. RETRIGGER also triggers all16 voices every8 blocks. Per-mode captured render timings report p50/p95/p99/max, deadline misses, write failures/timeouts, nonzero PCM/rails and maximum active sample voices. Timings are collected in fixed arrays; sorting/reporting is outside audio. SD_LOAD_INTERVAL samples blocks whose start falls inside the load function (probe/open/parse/chunked reads/downmix/publication/retirement), so it measures loading under audio rather than host or standalone SD speed. LOAD_WINDOW_IDLE is the remainder of that phase. Arrays cap at4096 blocks per mode and are sufficient for these12-second phases; compare counts and do not accept incomplete windows. A write failure emits FAILED and suspends audio as normal.

`AUTOMATION COMPLETE` means the scripted actions finished, **not qualification PASS**. Inspect every expected row, acceptance/rejection/retirement result, fragmentation warning, active_max16, nonzero PCM and audio deadline/error counters. Store the genuine serial capture. If misses occur, investigate core placement/chunk size/yield/pacing without reducing voice count or sample rate. No scheduling experiments were made without card measurements.

## Physical manual checks

Numerical metrics cannot establish audible quality. Listen to unity/original speed (no inherited octave down), down/up octaves, retrigger, replacement clicks, loading corruption, Delay and overlapping samples.

For removal, first confirm the board/slot and card permit electrical hot removal; this session has not established that. If uncertain, do not force removal. When safe: load resident PCM, play, remove card, verify resident playback continues and later loads reject without a crash/reboot/use-after-free. Sector probes report I/O availability; they are not card-detect/hotplug support. No remount/reindex retry occurs automatically. Power-cycle and restore normal `guition_app` after diagnostic use.

## Verification and merge decision

Host tests include first successful unity assignment from synth36, intentional sample48 edits, replacement and source round trips (including replacement from synth), exact octaves, safe MIDI bounds/all128 monotonic increments, source-aware TRACK control model, fixture order/mode pitch/memory warning thresholds, frame0 idle trigger/retrigger, final frame, one-frame and high-increment completion, explicit source stop, and10000 concurrent publish/retire cycles. All earlier host tests and recorded-log analyzers are retained in CI. The new diagnostic environment is included in the workflow; no physical card is required for CI.

Local checks on 2026-10-06:

- All ten firmware environments PASS: guition_boot, guition_audio, guition_synth, guition_fx, guition_display, guition_ui_audio, guition_ui_audio_m41, guition_app, guition_app_stress and guition_sampler_stress.
- Six host binaries PASS with C++17 / Wall / Wextra / Werror: PCM mono, FX safety, display geometry, display dirty history, app model and WAV/sampler. Generated fixtures: five accepted / three rejected. FX output matches the original across eight effects x 132352 stereo frames each. Existing M4/M4.1/M5/M6 recorded-log analyzers PASS.
- Device COM13: new sampler diagnostic boot capture confirms exactly one failed mount (255232us), then explicit PHYSICAL SD VALIDATION PENDING. No usable card mounted. This establishes the diagnostic's no-card branch, not card behavior.
- Build tooling uses `C:/.platformio/penv/Scripts/python.exe` (Python3.11.7). System Python3.14 is unsupported by the pinned platform, and the project Python3.13 environment cannot load its shared cp311 LittleFS extension. No platform/package upgrades were made. Set PYTHONUTF8=1 and PYTHONIOENCODING=utf-8 for Windows upload output.
- Remote GitHub CI was not triggered in this session. Local CI-equivalent builds and host checks passed; the workflow compiles the optional runner and keeps all previous checks.

New genuine stress capture `GUITION_M61_NO_CARD_STRESS_SERIAL.log` passes `analyze_m6.py` and is also checked in CI. Exactly one failed mount (255241us); synth fallback, sequencer, UI and audio remain active. 10336 blocks: p50=3721us, p95=3834us, p99=3862us, max=3923us, deadline misses/write failures/timeouts=0/0/0, peak=6715, nonzero=5267442, rails=0, active_max=16 (synth voices; not resident sample qualification). UI852 submitted/completed, zero touch errors/rejected commands. PSRAM30885508 -> 30885508, largest30408692 -> 30408692, internal408212 -> 408212. No heap growth or reboot occurred during the captured63-second stress interval.

Normal `guition_app` is restored after qualification; its boot capture is `GUITION_M61_NORMAL_BOOT_SERIAL.log`.

Implementation commit: `cca1f92` (source state, async pitch intent, exact Q16 playback, source-aware UI, runner/telemetry, pure tests and diagnostic CI build). The following qualification/report commit adds the genuine logs, this report, README link and new recorded-log CI check. Changed implementation files: `src/app/{model.h,qualification.h,samples.h,samples.cpp,wav.h}`, `src/app_main.cpp`, `tests/wav_sampler_test.cpp`, `platformio.ini`, `.github/workflows/guition.yml`; documentation/evidence: README, this report and three M6.1 serial captures.

**Is M6 now ready to merge into main?** Implementation readiness: **yes**, M6.1 code, host checks, all ten builds and fresh physical no-card regression pass. Full physical M6 qualification: **pending**. I do not recommend treating M6 as fully hardware-qualified for merge until native card loads, real16-voice sample stress, SD loading under audio, replacement/fragmentation and audible checks pass. There is no known remaining implementation blocker from these checks, but there are no physical SD/sample performance, memory-fragmentation or audible results to claim. M6.1 stops here.

**PHYSICAL SD VALIDATION PENDING**: card mount/index/load/playback, real A/B/C/A fragmentation, resident16-voice and loading-under-audio timing, hot removal and audible sample validation require an actual usable card. Implementation readiness and physical qualification are separate; do not declare full M6 hardware qualification based on host fixtures or no-card results.
