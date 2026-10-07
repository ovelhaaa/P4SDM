# M19 — offline transient detection and smart auto-slicing

Implementation commit: `752cb94`, based on accepted M18.1 `bb552f8` on `main`.
The qualification commit adds this report, genuine captures and host output.
Scope stops at M19.

## Detector and musical semantics

`src/app/transients.h` contains the entire deterministic detector and its
centralized parameters. PCM is analyzed directly, never through the 360-bin
waveform display cache. No FFT, DSP dependency, random decision, whole-file
novelty buffer, renderer change or analysis allocation is introduced.

Features use 64 source frames (1.45 ms at 44.1 kHz). Energy is mean absolute
amplitude, represented in Q8. Attack is mean absolute **signed first difference**
per channel, also Q8; intermediates safely handle -32768 to +32767 transitions.
Stereo energy is `(abs(L)+abs(R))/2`; attack averages the two absolute channel
differences, avoiding cancellation. Both channels share one frame boundary.

Fast energy EMA is 1/2; slow energy and slow attack EMAs are 1/16. Novelty is
`2*max(0,fast_energy-slow_energy) + max(0,attack-slow_attack)`. Threshold is
`2 PCM magnitude units + slow_energy/4 + multiplier*slow_novelty` in Q8.
Slow novelty EMA is 1/16. Sensitivity changes **only** multiplier: LOW=6,
MED=4 (default), HIGH=2. Integer divisions use deterministic truncation.
The level-relative floor rejects stationary low-level noise; HF rise detects
attacks with limited amplitude contrast. This is a starting musical tuning,
not a claim that MED is ideal on every recording.

One-block delayed peak picking requires novelty above its own adaptive threshold,
at least the previous block, and strictly above the next block. Refinement examines
at most 256 preceding frames plus the 64-frame feature block. It searches for
the earliest amplitude rise above a local quiet baseline, then a first-difference
rise for compressed material; ambiguous material falls back to a preceding low
point. It never scans the whole file per candidate or leaves the captured domain.

Spacing is `rate*30/1000` source frames, computed once (1323 at 44.1 kHz).
Conflicts retain the stronger candidate; equal-strength ties retain the earlier
frame. Spacing also protects the forced start and final slice. Tiny playable
domains and silence produce one safe full-domain slice.

The fixed array contains 64 `{uint32_t frame, uint32_t strength}` candidates.
Overflow replaces the weakest candidate deterministically. Retained candidates
are ranked by strength, the required number retained, then sorted chronologically.
NATURAL returns up to 16 slices without filling gaps; 4/8/16 return up to N slices,
exactly N when N-1 retained credible onsets exist. No hybrid/equal fill is used.
The UI reports FOUND M/N when the target is not met. Strongest selection avoids
taking merely the earliest 15 events from long samples.

The current normalized Track START is always the first boundary and END the
last. Intermediate source frames are converted with ceiling to normalized
coordinates; normalized endpoints are preserved exactly. Existing M13 frame
resolution remains unchanged. At 2,097,152 frames, normalized quantization adds
at most 32 source frames to boundary placement error.

## Ownership, proposal and UI

`transient_service.{h,cpp}` is a bounded mailbox serviced by the **existing**
priority-1 `sample_storage` task on core 0. The analyzer processes 64 analysis
blocks (4096 frames) per chunk, then yields one RTOS tick. The high-priority
audio task continues its existing render loop and never calls Detector.
No new task or task-stack allocation is added; the storage stack remains 7000 bytes.

Each request captures explicit Track, resident pointer, sample generation,
region revision and normalized START/END. Audio-owned source/region edits,
sample transfer and project replacement invalidate the captured revision.
Reverted trims and reused addresses cannot resurrect a result. PCM retirement,
load and project filesystem work are serialized by the storage task; pointer
validation occurs before dereferencing a queued request. Active cancellation or
invalidation is checked after every chunk. APPLY additionally requires the exact
proposal request ID, current pointer, generation, source and region.

ANALYZE never edits canonical Track state. A Ready proposal has its own 66-byte
SliceBank. APPLY enters the existing UI-to-audio command queue and copies that
bank at audio command processing, enables slicing, selects slice 0, and marks
the project dirty through the existing SliceDivide musical-edit classification.
It does not change pitch, direction, mode, choke, assignment, tone or Delay send.
ANALYZE/CANCEL/rejected APPLY are non-musical commands. CANCEL keeps the current
bank byte-identical and does not increment project changes.

SAMPLE SLICE retains equal AUTO DIVIDE and every manual control; TRANSIENTS opens
the AUTO SLICE subpage. NATURAL/4/8/16, LOW/MED/HIGH, ANALYZE, APPLY, CANCEL and
BACK are explicit controls. Progress is published chunkwise without blocking
touch/audio. Current boundaries remain solid yellow/cyan; proposals use thick,
dashed magenta markers with caps on the existing waveform. BACK cancels a proposal.
APPLY waits for the audio acknowledgement before permitting navigation/manual
edits, preventing an older bank publication from overwriting an optimistic edit.

After APPLY, slices are ordinary M14 regions. Voices and accepted Repeat events
already contain resolved Playback values and are untouched. There is no detector
participation in playback and no automatic re-analysis after later manual edits.

## Host qualification

`tests/transient_test.cpp` generates all PCM, with no copyrighted samples. It
checks bounded feature arithmetic, deterministic peak picking, exact 30-ms
candidate suppression (5/10/20/40/100-ms cases and exact 1323-frame boundary),
64-candidate overflow, strongest 4/8/16 selection, NATURAL 1/3/7/15 post-start
onsets, quiet/strong dynamics, sensitivity ordering on controlled fixtures,
soft attacks scoring below sharp attacks, trimming, silence, tiny regions,
stationary sine, fade-in, and direct interleaved stereo including anti-phase.

The raw host output is `GUITION_M19_HOST.log`. Strong-transient quality fixtures
include impulses, kicks, snares, hats, noise floor, compressed/HF attacks,
four-on-the-floor, syncopated break, ghost hats, percussion and consonant-like
bursts. Position tolerance is 64 source frames. These controlled fixtures have
52 expected onsets across 13 quality cases, all matched, zero misses and zero
false positives. Observed errors in these fixtures are 0 or 1 frame. Sustained
sine and slow fade-in have zero later markers. All five generated musical
fixtures differ from equal division. Ambiguous material is tested for sensible
and safe behavior rather than perceptual quality.

`tests/transient_service_test.cpp` executes the actual service implementation
with host RTOS/lifetime shims and forbids allocations during command, analysis,
apply and engine operations. It exercises cancel before apply, cancel during
analysis, generation replacement during analysis, trim changes during analysis,
wrong Track, wrong request ID, reverted trim, all 16 Slice Lock indexes,
unchanged active voice regions, reverse/Gate playback and immutable x8 Repeat
after replacing the generated SliceBank. Existing choke/renderer regressions
remain in the retained suite.

`tests/no_transient_equivalence.py` extracts actual accepted M18.1 model/codec
sources from Git, uses transient-generated 16-slice banks in both versions,
and compares 7.2 million simulated samples with all eight locks, PATTERN,
CHAIN, Override, Fill, mute/solo, x8/x2/x4 capture/rate/release and RNG/clock
state. Every encoded V2 byte is equal for identical canonical state. Project
version 2, 52,704 file bytes, 53,192 State bytes, Engine 52,616, Ui 116 and
TriggerEvent 20 are unchanged. `wav.h`, Voice, sample addressing, slices,
sample playback and voice routing are compared byte-for-byte to accepted Git.

Host wall-clock timings (Windows, optimized g++) are recorded in the host log;
they exclude task yields and are not substituted for device timing. The local
compiler lacks the UBSan runtime (`-lubsan`), so sanitizer execution is not claimed.

## Device qualification and limits

Final firmware is frozen at `752cb94`. Hardware captures use COM13, the ESP32-P4,
44.1 kHz and 256-frame audio blocks. SAMPLE and SYNTH keep the full inherited
80-second M18.1 scripts, 16 voices, 240 BPM, 4x ratchets, all applicable locks,
Delay, Chain, Performance and x8 Repeat. A separate immutable 4 MiB PSRAM
fixture is allocated before heap baseline measurement and analyzed concurrently;
it never replaces any of the 16 inherited voice fixtures. Timed domains cover
44,100, 176,400 and 2,097,152 frames. Scratch stays constant with duration.

The SAMPLE run also analyzes its actual playing resident Track three times,
applies one proposal, cancels one and invalidates a completed proposal with a
real queued region edit. The stationary inherited PCM truthfully produces a
one-slice proposal; it is not relabeled as 16 detected transients. Strong 16-slice
playback/Repeat integration is proven separately by the generated host fixtures.
The SYNTH run analyzes the separate fixture without assigning SAMPLE or editing
canonical SliceBanks. Normal firmware contains no qualification PCM allocation.

On the final SAMPLE capture, the largest concurrent UI loop gap is 102.803 ms;
waveform/full-page preparation is about 128 ms, already present in the inherited
renderer. The new analyzer bounds the UI loop to 250 ms (rather than claiming a
new 100-ms display deadline); every inherited UI error and dirty-region check is
unchanged. Touch polling remains on core 1 at priority 3. Physical touch latency
and appearance are not inferred from these scripted loop measurements.

Device scratch sizes are Detector 616 bytes including the 512-byte candidate
array, plus at most one 512-byte selection copy. Proposal is 84 bytes (66-byte
bank plus owner/alignment); published Status is 100 bytes. Metrics is 68 bytes;
per-Track generation/revision arrays total 128 bytes. There are no per-duration
allocations or stack-size increases. Storage stack high-water free minimum is
4412 bytes in the stress captures. The separate 4 MiB qualification PCM exists
only in the two M19 stress targets, allocated before measurement.

| Frozen firmware capture | SAMPLE | SYNTH |
|---|---:|---:|
| Audio blocks (80.004 s) | 13782 | 13782 |
| Overall worst render, us | 4617 | 4447 |
| Overall audio headroom | 20.46% | 23.39% |
| Deadline misses / I2S failures / timeouts | 0 / 0 / 0 | 0 / 0 / 0 |
| x8 blocks / full workload blocks | 1067 / 1066 | 1067 / 1066 |
| x8 active voice range | 16..16 | 16..16 |
| x8 worst, us | 4617 | 4201 |
| Analysis-overlapping blocks, all x8 | 316 | 204 |
| Concurrent worst render, us | 4617 | 3205 |
| Concurrent audio headroom | 20.46% | 44.79% |
| Concurrent deadline misses | 0 | 0 |
| One-second PCM analysis, us | 22989 | 19688 |
| Four-second loop analysis, us | 92917 | 92923 |
| 4 MiB PCM analysis, us | 1090095 | 1074985 |
| Fixture maximum chunk wall time, us | 4933 | 4066 |
| All analysis maximum chunk wall time, us | 68556 | 4066 |
| Worst concurrent UI loop interval, us | 102803 | 27286 |
| Proposal requests/completions | 3 / 3 | 0 / 0 |
| Proposals / APPLY / CANCEL / stale discard | 3 / 1 / 1 / 1 | 0 / 0 / 0 / 0 |
| Background timed fixture analyses | 3 | 3 |

Chunk wall times include higher-priority audio/UI preemption. In SAMPLE, the
68.556-ms service chunk overlaps expensive waveform/page preparation; it is not
68 ms of uninterrupted detector CPU or an audio block. The separate large-PCM
fixture chunks remain below 5 ms. Overall worst-case SAMPLE margin is only
0.46 percentage points above the requested 20% floor; these measurements qualify
the tested workload and do not guarantee arbitrary future firmware/content.

| Stable heaps, before = after (bytes) | SAMPLE | SYNTH |
|---|---:|---:|
| Internal free | 243988 | 268916 |
| PSRAM free | 24290932 | 26453624 |
| Largest PSRAM block | 24117236 | 26214388 |

NORMAL runs 10,337 blocks (60.006 s), worst render 1940 us, zero deadline misses,
failures/timeouts or PCM output. All M19 request/candidate/proposal/apply/cancel/
stale/analysis-frame counters and overlap counters are zero. It allocates no
qualification sample. Internal free remains 296596 bytes, PSRAM free 30779004,
largest PSRAM block 30408692, all before = after. Normal `guition_app` firmware is
restored on COM13.

Host/analyzer CI retains all 69 prior commands and adds six M19 commands. Firmware
CI retains all 21 prior environments and adds two M19 targets. The first local
build sweep filled the host disk near the final three environments; completed
ignored build caches were removed with PlatformIO's own clean target, preserving
logs and qualification binaries, then the affected builds were rerun. No source
or inherited check was removed to recover disk space. The disk-full failure also
truncated the installed SDK builder script; its intact packaged backup restored
the build environment. Local verification passes all 75 host/analyzer commands
and all 23 firmware environments; the compact record is
[`GUITION_M19_LOCAL_CHECKS.log`](GUITION_M19_LOCAL_CHECKS.log). Generated `.pio`
builds, legacy reference/test outputs, temporary scripts/logs and Python test
caches were removed after verification; regression sources and qualification
evidence remain.
Remote verification is available on the
[M19 branch Actions page](https://github.com/ovelhaaa/P4SDM/actions?query=branch%3Acodex%2Fm19-transient-slicing).

**PHYSICAL TRANSIENT AUTO-SLICING VALIDATION PENDING.** No SD card is mounted.
Drum-break/percussion/vocal loading, musical listening, physical touch usability
and subjective MED tuning are unperformed. Synthetic tests and scripted device
captures do not establish those results.

The detector accepts mono/interleaved stereo buffers. The accepted M18.1 WAV
loader still downmixes stereo to resident mono; M19 preserves that PCM/renderer
behavior. Anti-phase information already lost during loading cannot be recovered
by M19; the anti-phase test exercises raw stereo analysis input.

No BPM/beat-grid detection, FFT/spectral flux, time stretch, warp, per-slice pitch,
normalization or automatic Slice Locks was added.

## Changed files and acceptance answer

Firmware changes are in `src/app/transients.h`, `transient_service.{h,cpp}`,
`samples.{h,cpp}`, `model.h`, `project.h`, and `src/app_main.cpp`. Build/CI changes
are in `platformio.ini` and `.github/workflows/guition.yml`. Host coverage is in
`tests/transient_test.cpp`, `transient_service_test.cpp`, the two transient stub
headers, `no_transient_equivalence.py`, and `analyze_m19.py`. README, this report,
the host result and genuine M19 device logs document qualification.

**Yes, within the measured qualification scope:** P4SDM now analyzes resident PCM
outside realtime, proposes deterministic transient slices, safely applies them
to the existing SliceBank, and continues the tested dense Performance x8 workload
with zero deadline misses, over 20% worst-case audio headroom, and identical V2
project bytes. Generated musical fixtures support controlled onset usefulness;
musical listening and physical SD/touch validation remain pending as stated above.
