# M6: native SDMMC and resident sample playback

M6 is implemented on branch `codex/m6-sampler`, from accepted main baseline `6a7131d6d86adb7a35f4d79c4a7e0ec46137b521`. Implementation commit: `251b33b`; a follow-up commit records qualification artifacts.

**PHYSICAL SD VALIDATION PENDING.** The user confirmed the board is connected, but no microSD card is available. Real card mounting, WAV loading, resident sample playback, replacement/fragmentation, SD-under-audio timing and physical sample listening remain pending. No such results are fabricated.

## Native SDMMC architecture and power ownership

Framework stays pinned: Arduino3.3.6, bundled IDF5.5 `f56bea3d1f`, pioarduino55.03.36-1. Native bus is **4-bit, 20MHz**: CLK GPIO43, CMD44, D0=39, D1=40, D2=41, D3=42. No SPI fallback.

Audited [Guition BSP board_p4.c](https://github.com/ultramcu/guition-jc4880p4-bsp/blob/324970bade0d1f4e52880fe8016580368bc1e06e/src/board_p4.c) around lines170-184 and `board_p4_pins.h` at the same commit. TF_VCC is VO4/3300mV, through an always-on P-FET; GPIO45 enable's resistor is NC. GPIO45 is never configured.

`storage_hal` alone acquires LDO4/3300mV, retaining the handle for app lifetime. Display independently owns LDO3/2500mV. No changes to audio, I2C or touch pins. The pinned Arduino `libraries/SD_MMC/src/SD_MMC.cpp` only creates its LDO power controller when `_power_channel != -1`. We explicitly set `setPowerChannel(-1)`, so SD_MMC treats power as externally owned and never recreates LDO4. `begin("/sdcard", false, false, 20000, 4)` selects 4-bit mode and **disables formatting**. Unmount ends SDMMC; the rail remains owned for app lifetime. No silent format or destructive recovery exists.

The HAL exposes unavailable, mounting, ready, missing/removed, filesystem/mount error and I/O error. Arduino's boolean mount result loses the detailed failure category; a mount failure reads `NO CARD / MOUNT ERROR` rather than claiming to distinguish absence from corrupt FAT. One boot mount attempt, no automatic retries. Failure does not halt audio/display/touch/sequencer. The UI memory baseline is taken after storage bootstrap.

There is no dedicated card-detect pin. The lower-priority worker probes sector0 before loading and every3 seconds when mounted. Failure disables new loads; short PCM reads report I/O error. This is best-effort I/O detection, not reliable electrical hotplug. Existing resident PCM is retained. After removal or failed mount, reboot with a card to mount/reindex; live remount UI is absent. Filesystem type is the Arduino FatFS VFS path; FAT subtype is not exposed by this wrapper.

## Original Tab5 sampler audit

Original loader is `cargarWavsEnPSRAM()` and `read16/read32`, around lines675-780 in `DRUM_2026_VSAMPLER_TAB5_2002.ino`. It starts SPI SD at40MHz, scans `/`, and preloads accepted WAVs up to128. `files_tools.ino` handles preset file operations; `LCD_tools.ino` and `keys.ino` implement old sample/track editing. Their filesystem/UI concurrency model is not ported.

Intended format: mono, PCM16, 44100Hz. The loader reads channels/rate/depth at fixed offsets22/24/34, without checking RIFF/WAVE signatures or encoding. Its data chunk walk ignores odd-byte padding and unchecked lengths/seeks/reads. It publishes even after a short read.

`SAMPLES[128]` holds independent PSRAM allocations, with `SAMPLES_SIZES`, `SAMPLE_NAMES` and numeric indices. It is not one large pool. Track sample selection uses `ROTvalue[track][0]`; source is `ROTvalue[track][16]`. `ENDS[index]=(dataSize/2)-1` mixes last-index and exclusive-end meanings. There is no active `INIS` array in this baseline. `NEWINIS[16]` and `NEWENDS[16]` are per-voice trim bounds, scaled from0-2048 controls by setters in `synthESP32.ino`. Commented older code used sample indices instead.

Original `render_buffer()` reads signed mono PCM into the stereo filter/volume/pan/FX mixer. `latch` marks activity. `samplePos`/`stepSize` use16 fractional bits. Trigger resets to trim start, or end for reverse; increment uses MIDI frequency/261.63Hz (C4/MIDI60 unity), without source-rate correction. There is **no interpolation**: nearest-neighbor memory reads. Trim-relative ADSR is applied. Forward end comparisons skip the final PCM frame; reverse/trim and unchecked index assumptions are unsafe. UI directly mutates realtime globals, with no safe buffer retirement.

M6 preserves C4 unity, fixed-point pitch and nearest-neighbor behavior, sharing the existing filter/pan/master/Delay mixer. Full-file forward one-shots are the M6 baseline; old trim ADSR and reverse are not added. Synth wave/length controls remain synth controls. Last-frame and one-frame playback now work. The adjacent synth pitch lookup at MIDI127 is clamped to127 instead of reading entry128.

## WAV support and parser safety

Support: little-endian **RIFF/WAVE integer PCM tag1, 16-bit, mono or stereo, exactly44100Hz**. Rate strategy is optionA; other rates fail explicitly. Float/compressed/extensible, other depths and more than2 channels are rejected with useful reasons. No arbitrary realtime resampler is added.

Stereo is downmixed once on load: `(int32(left)+right)/2`. Resident format is mono PCM16; pan produces stereo output through the existing mixer. Stereo spatial separation is not retained. Metadata records pointer, frames, original rate/channels, normalized channels1, filename and allocation size. Ready means fully loaded and published; loading/error state belongs to the storage service.

The allocation-free host parser verifies signatures, RIFF envelope against file size, then walks every chunk with64-bit arithmetic. It validates chunk headers/payload/padding against the RIFF boundary, handles data-before-fmt, skips LIST/JUNK/PAD/unknown chunks, checks fmt size/encoding/rate/channels/block alignment/byte rate, and rejects duplicate fmt/data, missing/empty data and partial frames. Odd chunks require their pad byte. At most4096 chunks bound malicious metadata work; oversized data offsets are rejected. Loader checks seek and every read, and never publishes truncated/half-loaded PCM.

## Library, UI and ownership

Library is nonrecursive `/P4SDM/SAMPLES/` (VFS mount `/sdcard`). Index limit128 WAV filenames, 95 name bytes, maximum512 directory entries. Extension matching is case-insensitive. Limits/overlong names are exposed. Indexing does not load the whole card.

At most16 resident allocations (one per track), plus one staging allocation; no extra cache. PCM uses `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`; small metadata objects are explicitly deleted. Worker priority1/core0 is below UI2, touch3 and highest-priority audio/core0. It reads at most4096 input bytes/1024 frames, downmixes and yields one RTOS tick per chunk. M4's cache/core contention lesson is retained; SD behavior still needs measurements.

Lifecycle: **load -> ready -> publish -> active -> replace -> retire -> free**. UI submits one job. Worker completely prepares an immutable object and release-publishes a single slot. Audio acquire-consumes it at a block boundary, copies track metadata before acknowledgement, stops/replaces the old voice and resets filter history. Audio then release-acknowledges the retired object; only afterward can storage free old PCM/metadata and accept another job. Independent per-track allocations prevent cross-track retirement hazards. No mutex is held in audio rendering. No file access, parsing, allocation/free, Serial or display operation occurs in the audio path.

Both pads and sequence steps call the same existing `trigger()` and transport. Voices have independent position/activity. Every read checks frame index before access, completion deactivates cleanly, and idle/retrigger start at frame0. Source changes stop previous playback. Synth/sample sources coexist in the same16-track mixer. Assignment resets track filter history; Delay retains its intended FX tail, without reading retired PCM.

`TRACK -> SAMPLE PAGE`: current track in header, indexed filename, assigned name, source and load/error state; PREVIOUS/NEXT, LOAD/ASSIGN, USE SYNTH/USE SAMPLE. Large targets. Source switching retains the loaded buffer for immediate reuse and is deferred while loading. Existing audio continues until a fully ready replacement is published. Unsupported files, I/O errors and allocation failure preserve the prior sample. Load status changes only dirty the small name/status region and relevant source button, not the full screen. No-card boots with synth tracks and reports the mount state. Reboot is required to mount/reindex later.

## PSRAM budget and telemetry

PSRAM total33554432 bytes. Display payload2304000 (three800x480 RGB565 buffers), Delay352800 (two88200-frame PCM16 buffers), plus allocator/system overhead. Per-sample cap is **4MiB normalized mono PCM**; stereo input may be twice that PCM size. Allocation requires enough largest contiguous free block and **4MiB remaining reserve**, while the old resident and new staging object coexist. Metadata/payload failure retains the old assignment and leaves synth available.

`[M6 budget]`, `[M6 read]`, `[M6 load]` report total/free/largest PSRAM, internal RAM, display/Delay payloads, resident payload, transient new allocation, peak simultaneous payload, actual file size, source PCM throughput, load/publish time and post-retirement memory. Actual small/medium loading, sequential assignments, A/B/C replacement and fragmentation results are pending a card. No host timing substitutes for SD hardware performance.

## Host tests and hardware results

Host tests pass: PCM; FX safety and original-output comparison (eight FX x132352 stereo frames); display geometry/dirty history; M4/M4.1 analyzers; M5 model/log analyzer; new WAV/sampler tests. New coverage includes mono/stereo, alternate chunk order, unknown/odd chunks, every truncation point, unsupported rates/depth/encoding/channels, alignment, empty/partial PCM, duplicates/missing chunks, malicious sizes, exact-reader failure,10000 parser mutations, unity/up/down/high-rate bounds, one-frame playback, retrigger,10000 concurrent producer/consumer replacements, memory budget boundaries, downmix extrema and sample-page hit targets.

`python tests/generate_m6_fixtures.py <output-directory>` generates8 public-domain synthetic fixtures: short/medium/long mono, stereo, extra/odd padded chunks, unsupported depth, unsupported encoding and truncation. The host parser verifies the generated files (5 accepted/3 rejected). Copy to `/P4SDM/SAMPLES/` for later qualification. No commercial samples are included.

All **nine** existing firmware environments build locally: guition_boot, guition_audio, guition_synth, guition_fx, guition_display, guition_ui_audio, guition_ui_audio_m41, guition_app and guition_app_stress. Local CI-equivalent checks are green. **Remote GitHub CI has not been run** in this session. Workflow adds generated-fixture/parser/lifecycle/bounds tests and the recorded no-card log analyzer without requiring attached hardware.

Final genuine no-card capture: `GUITION_M6_NO_CARD_STRESS_SERIAL.log`, ESP32-P4 rev1.3 on COM13. It contains one failed mount (255234us), ES8311/I2S initialization and10336 audio blocks. Inherited `[M5]` metric prefixes remain for the common analyzer; this is M6 firmware using synth sources without a card.

| Metric | M6 no-card stress | Accepted M5 stress |
|---|---:|---:|
| p50 us |3714|3530|
| p95 us |3843|3700|
| p99 us |3872|3728|
| max us |3928|3780|
| deadline misses/write errors/timeouts |0/0/0|0/0/0|
| peak/rails |6940/0|6531/0|
| active maximum |16|16|
| worst render headroom |32.33%|34.88%|

UI862 submitted/completed frames; zero touch API errors/rejected commands;54 pads,54 step edits,54 drags,217 page actions. PSRAM free stays30885508, largest block30408692, internal free409492 before/after. Bytes above reserve before samples:26691204, subject to contiguous-block limits and staging overlap. `analyze_m6.py` passes and delegates complete M5 measured checks. This establishes no-card runtime health, not SD/sample performance or new human touch/listening confirmation. Prior user-confirmed M5 physical touch remains accepted.

`GUITION_M6_NO_CARD_INITIAL_SERIAL.log` preserves the first genuine capture: max3921us, zero deadline misses, but incomplete audio serial row and a heap baseline before one-time storage allocation. It is not an acceptance capture. USB TX buffer was enlarged and the memory baseline moved after storage bootstrap. Final telemetry is complete and heap stable. Normal `guition_app` firmware is restored after scripted qualification.

## Pending physical card qualification and limitations

Acceptance17-22 are pending: native card mount/capacity/FAT details; real WAV loading; repeated replacement/leak/fragmentation;16 resident sample tracks with Delay/display/touch; file loading while audio runs; physical sample audio confirmation.

With a card, assign `long_mono.wav` to16 tracks. Test simultaneous pads and sequencer, Delay, normal UI, C4 unity/down/up pitch and retrigger-heavy patterns. Capture p50/p95/p99/max/misses/errors/PCM/activity. Load short/medium/stereo staging objects while running; cycle A/B/C/A and compare free/largest blocks at equal resident payload. Preserve `[M6 read/load]` rows, reject unexplained block collapse, allocation failures or audio misses. Stereo qualifies offline downmix, not stereo resident interpolation. Test card removal during load/playback, unsupported/truncated files and budget failure. Scheduling/throttle choice is provisional until SD-under-audio measurements are available.

**Is P4SDM now a real sample-based drum machine on the Guition?** Implementation readiness: yes, firmware contains the complete SD loader/resident sample path, shared pad/sequencer triggers, safe replacement and assignment UI. Physical qualification: it is not yet a physically verified SD/sample drum machine because no card is available. **PHYSICAL SD VALIDATION PENDING.** Stop at M6; no presets/projects/song/recording/USB MIDI/networking work was added.

Changed implementation files: `src/hal/storage_hal.{h,cpp}`, `src/app/{samples.h,samples.cpp,wav.h,model.h}`, `src/app_main.cpp`, `synthESP32.ino`, `platformio.ini`, `.github/workflows/guition.yml`, `tests/{wav_sampler_test.cpp,generate_m6_fixtures.py,analyze_m6.py}`. Qualification artifacts: this report, two genuine serial logs and README.
