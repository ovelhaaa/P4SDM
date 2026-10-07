# M11 — Track Tone + Delay Send Foundation

Baseline: merged `main` 26ba45e, accepted M10 head
`232c35c36428ef19de6910318ea98cddf54ad052`.
Implementation commit: `461570c`.
Branch: `codex/m11-track-tone`. Qualification: 2026-10-06, COM13,
ESP32-P4 revision 1.3, 44,100 Hz / 256-frame blocks / NativeQueued.

Changed files: src/app/model.h, src/app_main.cpp, src/engine/synth_api.h,
src/engine/track_tone.h, synthESP32.ino, synthESP32LowPassFilter_E.h,
tests/track_tone_test.cpp, tests/tone_equivalence.cpp, tests/tone_equivalence.py,
tests/no_tone_equivalence.cpp, tests/no_tone_equivalence.py, tests/analyze_m11.py,
.github/workflows/guition.yml, README.md, docs/VALIDATION.md, this report,
GUITION_M11_STRESS_SERIAL.log, GUITION_M11_NORMAL_SERIAL.log and the four
retained failed-candidate/coverage captures named below.

## Filter audit and mappings

The existing filter is Paul Kellett's two-buffer fixed-point topology. Its actual
feedback expression is `fb = q + ((q * (255-f)) >> 8)`, not the floating-point
`q + q/(1-f)` quoted in its introductory comment. M11 preserves the actual
algorithm and its arithmetic rounding. Both setters now call one `refresh()`;
resonance alone previously changed q but left fb stale. The constructor now
initializes coefficients and history, making local instances deterministic.

M10 initialization sets all eighteen filters to q=255 then f=255. Global static
history starts at zero. App initialization/update_track then sets voice cutoff
0; master cutoff is also 0. Neither ordinary triggers nor track edits reset
history. Sample replacement and explicit source changes reset history. M11
retains these lifecycle points. `reset()` clears only buf0/buf1, never f/q/fb.

Canonical cutoff remains 0..127, with `f = 255 - floor(value*255/127)`.
This exactly matches Arduino's signed `map(value,0,127,255,0)` rounding.
0 is effectively OPEN (f=255), not a mathematical bypass. 127 gives f=0:
the legacy recurrence freezes its history at that endpoint. It is not an
explicit mute and can retain a DC value until a subsequent coefficient edit.
No automatic history clear was added to disguise this legacy behavior.

The old documentation's 0..8192 Hz range assumes another audio rate and is not
an accurate calibration at 44.1 kHz. The recurrence's approximate -3 dB points
at edited resonance zero are: cutoff 32 ~6972 Hz, 64 ~3185 Hz, 96 ~1283 Hz,
120 ~273 Hz, 126 ~54 Hz. At 0/default and 1 no -3 dB crossing occurs below
Nyquist (22050 Hz). These are transfer-function estimates, not physical
frequency-response measurements; the UI deliberately shows no Hz/Q units.

Contrary to the requested assumption, M10 q=255 is maximum raw resonance,
not low resonance. To reconcile exact default compatibility with a conservative
musical edited range, the exact pair cutoff=0/resonance=0 retains q=255;
all other pairs use `q = floor(resonance*160/127)` (0..160).
Thus the default corner has an explicit compatibility exception. Leaving it
refreshes feedback without discarding history. The UI's resonance 0 means low
resonance for edited tone; the accepted open/default pair has its legacy q.
This exception must remain explicit in any future M12 event resolution.

A q=192 candidate exceeded the selected history bound with full-scale signals;
q=160 passes every cutoff at resonance 0/32/64/96/127 with impulse, alternating
full-scale, DC step and periodic sine. A 64-bit oracle verifies intermediate
32-bit products and exact recurrence output. Peak is 43027, above int16 but
safe before the existing gain/clip stages. An app-only buf0 guard bounds edited
history to +/-65535, retaining buf1's convex update. This also bounds the
per-voice volume/pan product (`65535*127*255 < INT32_MAX`). All f=255 open configurations are exempt; in particular the accepted
open/default pair is exempt so the master filter can accept the sum of sixteen
voices with bit-exact M10 output. The guard is inactive in the qualified static
signal matrix. Rapid coefficient edits have a separate bounded-output test.
Non-app diagnostic/history behavior retains its original sample recurrence.

## Canonical state and realtime ownership

Track adds three serialization-friendly bytes: filter_cutoff=0,
filter_resonance=0, delay_send=127. Track alignment increases Engine by 64 bytes
(34184 -> 34248); audio + UI mirrors add 128 bytes. Pattern remains 2082 bytes,
StepLocks five bytes and TriggerEvent seven bytes. New commands FilterCutoff,
FilterResonance and DelaySend specify track, clamp 0..127, retain the 12-byte
Command, use the existing bounded queues and sixteen-command block limit.

Audio owns Track and DSP. UI mirrors only successfully queued commands.
`synthESP32_setTrackTone()` is the explicit combined coefficient boundary;
`update_track()` applies actual Track tone while retaining active M10 lock
volume/pan/pitch/wave overrides. Dedicated tone commands update only coefficients
and/or routing, never voice_event, oscillator phase, envelope or ratchet state.
No retrigger, restore job or realtime allocation is introduced.

Source switch and sample assignment preserve Track settings and clear only
history at the existing lifecycle points. Pattern copy/switch/transforms and
COPY TRACK retain lane-only semantics and never copy instrument/tone/routing.
Coefficients are derivable; history and Delay tails are not future project data.
No persistence or SD requirement was added.

## Delay audit, tap and semantics

Audio initialization and the global Delay command own `delays`: 0xffff when
Delay is available/enabled, zero when disabled. M10 sends all sixteen tracks
at full level. Delay is initialized with 88200 frames, time 12000, feedback 120,
input level 160, global wet `level_delay=100` (100/256 after processing).
These settings remain unchanged.

Actual app routing is source (synth or resident PCM) -> Q15 event velocity ->
FILTROS[track] -> effective M10 event volume/pan (`sss*channel_gain*255 >> 16`)
-> unchanged dry accumulation plus stereo Delay tap -> bus soft_clip ->
existing stereo Delay/input gain/feedback -> wet 100/256 -> mix with dry after
master L/R filters -> existing master volume -> final soft_clip.

The app-only tap scales each already-filtered, already-panned channel by send/127.
127 is an explicit identity; 0 contributes zero. Intermediate multiplication
uses bounded int32 and signed division toward zero. It does not apply volume,
velocity or pan twice and does not collapse stereo. The post-pan input bound
makes the product <=8322945. Global OFF still disables bus/processor; send
never enables it. Send zero does not reset Delay, so existing tails continue.
Historical non-app bitmask sends remain unchanged.

The initial hardware candidate used int64 multiplication/division and starved
IDLE0 during mixed sends, triggering the watchdog. Genuine failed evidence is
retained in GUITION_M11_INITIAL_FAILED_SERIAL.log. Bounded int32 scaling removes
that avoidable hot-path cost while preserving identical signed rounding. The
workload, audio settings, task priorities and display quality were not reduced.
The next genuine run (HEADROOM_FAILED) had max=4847 us; inlining the send hook
improved this to 4659 us (INLINE_SEND_FAILED), still just outside acceptance.
App-only filter inlining and its simpler open-state guard brought the full
workload below the unchanged 20% threshold. An additional passing-audio run
(COVERAGE_FAILED) exposed a missed Pattern Clear from competing scripted pages.
Stress switches and clears now carry explicit commands, alongside UI workflows,
so independent page navigation cannot silently remove intended coverage.

## TONE UI

TRACK has a separate TONE button. The subpage identifies current Track/source,
with three 752x56 touch rows, cutoff, resonance and Delay send, and 52-pixel
TRACK </TRACK >/BACK targets. Cutoff's explicit slider helper inverts only UI
position: right is OPEN; closed-to-open display is 0..127, with OPEN at the
endpoint. Resonance/send display canonical 0..127, and send shows global status.
Both sources have the same enabled controls. Track changes dirty the identity
and three values; dragging dirties one row. Navigation uses existing full-page
rendering. No filter-specific locks or generic automation framework was added.

## Host validation

All eleven C++ host tests pass, including the ten retained M3-M10 tests.
track_tone_test covers mapping/clamps, setter order/resonance refresh, reset
coefficient retention, history retention, numeric oracle, rapid edits, all-track
isolation, send monotonicity/zero/full endpoints, locked event volume/pan before
routing, source and pattern operation retention, hit geometry and 100000
allocation-free command iterations.

no_tone_equivalence compares against the actual M10 header, with probability,
ratchets, swing, locks and switching over 3.6 million sample positions; every
event field, RNG and transport state matches. The retained actual-M9 no-lock
regression also passes. tone_equivalence uses the actual M10 filter and existing
stereo Delay on deterministic oscillator-like/resident-PCM fixtures. All 132352
frames match filter, dry/master, stereo bus and wet output exactly at defaults /
full send; zero-send dry remains identical and existing tails continue. This
is DSP routing equivalence, not a physical DAC capture or a host build of the
complete ESP32 oscillator implementation. All eight historical FX digests match.

CI retains all prior builds/tests/analyzers and adds tone, actual-M10 default
scheduler/DSP equivalence and the genuine M11 capture analyzer. All ten firmware
environments compile locally with pinned pioarduino 55.03.36-1. All historical
M4/M4.1/M5/M6/M6.1/M7/M8/M9/M10 analyzers pass their retained captures; historical
M4's recorded failed configurations remain reported, not reclassified as passes.
No hosted GitHub CI run is claimed. Local Windows qualification uses the existing
Python 3.11.7 PlatformIO environment at C:/\.platformio/penv/Scripts/python.exe;
PYTHONIOENCODING=utf-8 avoids console-encoding failures during upload. New
comparison scripts place their binaries in ignored .pio rather than deleting an
executable immediately after exit, avoiding Windows file-handle cleanup races.

## Hardware qualification

Final genuine stress: GUITION_M11_STRESS_SERIAL.log. analyze_m11.py passes,
including nested retained M10/M9/M8/M5 assertions. The first twelve seconds
prove all sixteen filters away from open, significant resonance, all sixteen
voices, all M10 locks, four ratchets, 240 BPM and all sends=127. Subsequent
commands exercise mixed sends and every tone endpoint at realistic UI rates.
The complete render/control timing includes tone commands and transforms.

| Measurement | Result |
| --- | --- |
| Audio blocks / nominal duration | 10337 / 60.006 seconds |
| Render/control p50 / p95 / p99 / max | 3647 / 4291 / 4359 / 4505 us |
| Block budget / worst headroom | 5804.989 us / 22.39% |
| Dense all-filter/all-lock/full-send blocks / max | 2068 / 4369 us |
| Dense hit rate | 1024 hits/second |
| Tone-edit blocks / worst tone-edit block | 594 / 4505 us |
| Cutoff / resonance / send edits | 601 / 601 / 601 |
| Applied min / max for each tone control | 0 / 127 |
| Transform blocks / dense transform blocks / worst | 150 / 121 / 4424 us |
| Deadline misses / write failures / timeouts / PCM rails | 0 / 0 / 0 / 0 |
| Active voices min / max | 16 / 16 |
| PCM peak / nonzero values | 6495 / 5290731 |
| Parents / passed / skipped / ratchet hits | 15084 / 13790 / 1294 / 38820 |
| Locked / unlocked accepted parents | 13653 / 137 |
| Pattern switches / long-short / short-long / replacements | 9 / 2 / 2 / 10 |
| UI seconds / submitted / completed | 63.019 / 1315 / 1315 |
| TONE entries / edits / max widgets / edit full redraws | 8 / 36 / 1 / 0 |
| LOCKS entries / edits / max widgets / edit full redraws | 12 / 80 / 8 / 0 |
| Command rejects / touch errors | 0 / 0 |
| Internal heap before / after | 340920 / 340920 bytes |
| PSRAM free before / after | 30885508 / 30885508 bytes |
| Largest PSRAM block before / after | 30408692 / 30408692 bytes |

Stress firmware SHA256:
`1d141814ac45c6583377288b244b408471b3626a35f6c37c44a053cc22b3cafd`.

The page-heavy run does not establish a strict 30 FPS guarantee. Normal dirty
frames average 78876 bytes, with maximum 479232 bytes for the mixed workload.
TONE-specific edits retain the one-widget bound; unrelated navigation uses the
existing full-page path. No per-edit Serial output occurs during capture.

Normal `guition_app` was restored to COM13 with flash hash verification.
GUITION_M11_NORMAL_SERIAL.log passes analyze_m11.py --normal: a separate
10337-block / 60.006-second idle run has zero active voices, nonzero PCM,
parents, ratchets, tone edits, rails, deadline misses, write failures, timeouts
and rejects. Render p50/p95/p99/max is 1875/1887/1891/2009 us.
Internal heap remains 340952 bytes; PSRAM free/largest remain
30885508/30408692 bytes. Display, touch and audio initialize successfully with
the expected no-card status. Physical operation of the controls is not claimed.
Normal firmware SHA256:
`89f52a1da3c05f8a5132c99ee0d1e1ce1eba4ba43b81455c7611c55e50d653e1`.

Reproduce with the pinned environment (Windows upload additionally sets
PYTHONIOENCODING=utf-8):

```text
pio run -e guition_app_stress -t upload --upload-port COM13
python tests/capture_guition.py --port COM13 --output docs/GUITION_M11_STRESS_SERIAL.log --seconds 95 --until "[M5 memory]"
python tests/analyze_m11.py docs/GUITION_M11_STRESS_SERIAL.log
pio run -e guition_app -t upload --upload-port COM13
python tests/capture_guition.py --port COM13 --output docs/GUITION_M11_NORMAL_SERIAL.log --seconds 95 --until "[M5 memory]"
python tests/analyze_m11.py docs/GUITION_M11_NORMAL_SERIAL.log --normal
```

## Audible and physical limits; M12 answer

No audible listening, human touch/visual QA or physical resident sample validation
is claimed. The connected device has no SD card. Resident PCM uses the shared
routing by construction and host fixtures, with physical qualification pending.
No remote CI result, push or merge is asserted.

**Yes:** Track tone/routing is clean enough to add cutoff, resonance and Delay
send to M12's accepted-parent event snapshots without changing DSP ownership.
M12 must explicitly define live-base-versus-active-lock overrides, preserve the
compatibility mapping and test accepted-ratchet lifetime. M11 itself does not
extend StepLocks, TriggerEvent, Pattern or implement any of those locks.
Work stops after M11.
