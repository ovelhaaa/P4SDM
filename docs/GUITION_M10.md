# M10 — Parameter Locks v1

Baseline: M9 `250c5cd26d5d99388bbdd1c6a41371caad4a1ef9` on `main`.
Implementation: `44e3753` on local branch `codex/m10-parameter-locks`.
Qualification: 2026-10-06, ESP32-P4 revision 1.3, COM13.
Evidence/report are committed separately. No remote CI run, push or merge is asserted.

Changed files: `src/app/model.h`, `src/app_main.cpp`, `tests/parameter_locks_test.cpp`,
`tests/no_lock_equivalence.cpp`, `tests/no_lock_equivalence.py`,
`tests/pattern_tools_test.cpp`, `tests/analyze_m8.py`, `tests/analyze_m9.py`,
`tests/analyze_m10.py`, `.github/workflows/guition.yml`, `README.md`,
`docs/VALIDATION.md`, this report and the genuine stress/normal serial captures.

## State, ownership and lifetime

`StepLocks` is exactly five bytes: `uint8_t mask, pitch, volume`, `int8_t pan`,
`uint8_t wave`. Bits 1/2/4/8 explicitly enable pitch/volume/pan/wave. All masks
initialize to zero. Legal zero values are represented directly; comparing a lock
with a base value never determines presence. The structure is trivially copyable,
has no padding, pointers or heap ownership, and is suitable for future serialization.

`Pattern` contains `locks[16][16]` alongside existing masks and StepMeta. Six new
command kinds address a specific pattern, track and step: LockPitch, LockVolume,
LockPan, LockWave, UnlockParam and ClearStepLocks. UnlockParam carries the explicit
parameter bit. Values clamp to legal ranges. Command remains twelve bytes.
Audio applies commands at the start of a block, using the unchanged queue and
sixteen-command limit. UI mirrors only successfully enqueued edits.

An eligible parent still checks mute/multi-solo and draws probability before any
lock resolution. One common `resolve_event` function combines Track base settings,
velocity and StepLocks into a seven-byte TriggerEvent: track, velocity, pitch,
volume, signed pan, wave, effective lock mask. It is emitted and copied into the
fixed per-track Pending state. Ratchets use that complete snapshot without reading
Pattern or Track again. Clearing/changing locks or changing base controls after
acceptance affects subsequent parents, while accepted ratchets retain their values.
STOP cancels ratchets and retains musical locks and Track settings.

Every new parent resolves every parameter. Unlocked steps and manual audition
therefore explicitly reapply base pitch, volume, pan and wave; manual velocity is
127. No delayed restoration job exists. Quantized switching resolves the new
pattern's step zero after the boundary, with no inherited lock. Failed probability
and suppressed events never apply parameters. Playback RNG and scheduling math
are unchanged; no lock edit runs per sample.

## Voice paths and gain order

Audio owns a fixed `voice_event[16]` array. Trigger applies Q15 velocity gain,
effective VOL_L/VOL_R and runtime synth wave, then dispatches to the existing
source path. Synth uses `synthESP32_setWave` and `synthESP32_TRIGGER_P` with the
effective absolute MIDI pitch 0–127. These change runtime voice arrays, never
Track or canonical pattern state. ROTvalue remains a base-control mirror;
locks are not stored there. The next unlocked trigger reapplies all base values.

Resident PCM uses `sampler::pitch_increment(event.pitch)` and the existing Voice
trigger, ownership, interpolation and Transfer paths. Pitch 60 is unity, 48 is
32768 (0.5x Q16), and 72 is 131072 (2x). Effective volume/pan use the same voice
channel gains as synth. WAVE has no effect on PCM, is removed from the effective
sample event mask, and remains stored in Pattern. Switching back to synth makes
it relevant again. Sample selection and PCM contents are untouched.

The exact existing signal order is retained: source PCM/synth sample → Q15 velocity
attenuation → existing per-voice filter → effective volume/pan channel gains →
dry/FX sends → existing master gain/soft clip. Effective volume replaces base
volume once; it does not multiply it again. The retained M9 linear pan law is
`gainL = volume * clamp(128-pan,0,128) / 128`, and
`gainR = volume * clamp(128+pan,0,128) / 128`, with the original integer rounding.
The endpoint's quiet side can round to zero, as in M9. No new gain amplification
or equal-power pan algorithm was introduced.

Live base edits retain active voice overrides for locked parameters and update
unlocked parameters. Base ROTvalue/Track settings remain editable. Source changes
stop the old voice and clear its runtime override; stored locks remain. A new
parent or manual pad always applies a freshly resolved voice event.

## Memory

| State | M9 bytes | M10 bytes | Increase |
| --- | ---: | ---: | ---: |
| StepLocks per position | 0 | 5 | 5 |
| Pattern | 802 | 2,082 | 1,280 |
| Sixteen-pattern bank per Engine | 12,832 | 33,312 | 20,480 |
| Engine | 13,584 | 34,184 | 20,600 |
| Audio Engine + UI Engine mirror | 27,168 | 68,368 | 41,200 |

Runtime voice snapshots add 112 bytes. Rotation adds a fixed eighty-byte locks
temporary to its existing forty-eight-byte metadata temporary. There is no extra
bank or realtime heap allocation. Stress internal heap is 341,176 bytes before
and after the measurement; initialization reports 342,196 before the other task
allocations settle. Free PSRAM is 30,885,508 bytes and its largest block remains
30,408,692 bytes. Normal firmware's PlatformIO static RAM metric is 72,584 bytes;
the measured runtime internal heap includes the full engine/mirror cost.

## Editing and transform semantics

STEP gains a LOCKS button. The dedicated page has four 56-pixel rows, each with
a 520-pixel value target and a 216-pixel ENABLE LOCK / UNLOCK target. Values show
LOCKED or `-- UNLOCKED` explicitly. Enabling captures the current source-specific
Track base value. Only enabled value rows can be dragged. Pitch/volume span
0–127, pan -127…127, and synth wave 0–15. SAMPLE shows WAVE - SYNTH ONLY and
UNAVAILABLE. CLEAR LOCKS and BACK TO STEP are separate 52-pixel buttons.

Copy pattern, duplicate and copy track copy all sixteen lock positions, including
hidden steps, by value. Rotate/reverse move locks with the trigger and StepMeta
within active length; hidden bundles remain unchanged. Euclidean preserves locks
at their positions. Randomize and Mutate preserve every lock byte. Track/Pattern
CLEAR clear triggers and preserve locks. CLEAR LOCKS resets only the selected
step's lock structure, retaining its trigger and StepMeta. A lock-only pattern
counts as occupied for duplicate overwrite confirmation.

Lock edits dirty the changed value/toggle pair; clear dirties the eight row widgets.
Page navigation uses the existing full-page rendering path. The final capture
records thirteen LOCKS entries, 106 successful UI lock edits, maximum eight dirty
widgets and zero full redraws caused by edits. There is no animated lock marker
or sequence-grid presence marker. Normal dirty frames average 75,094 bytes
(9.78% of a framebuffer); their maximum is 460,800 bytes across the mixed workload.
Scripted geometry/hitbox and display retirement checks do not establish human
touch feel or visual appearance.

## Host coverage and CI

`parameter_locks_test.cpp` covers default absence; pitch, volume and pan endpoints;
wave fallback; explicit equal-to-base lock presence; command clamp/intent;
sample unity/octave increments; stored wave across source changes; base/manual
audition isolation; live edit and clear during 4x ratchets; next-parent restoration;
probability zero; mute/solo; STOP; quantized boundary with four locked hits before
an unlocked destination; full bundle transforms at lengths 1/3/7/12/16; hidden
steps; copy independence; generator/clear byte retention; clear-only-lock semantics;
resident PCM attenuation/pan bounds; touch geometry; enable-from-base UI commands;
sample WAVE disablement; and 100,000 realtime iterations without allocation.
Existing M9 transform fixtures also carry unique locks and compare them through
round trips and copies; the accepted-parent test uses Pending.event.velocity.

`no_lock_equivalence.py` retrieves the actual M9 header from the accepted commit,
compiles it in a separate namespace, and compares M10 to it sample by sample.
Nine combinations of playback seed and swing cover 3.6 million samples, probability,
ratchets, short/long switching, mute and solo. Every trigger track, velocity and
sample position matches; RNG, phase, step, playing pattern and event counters
also match exactly. This is scheduler/event equivalence, not a new bit-exact
synth PCM baseline capture.

All ten C++ host tests pass, including existing PCM, FX safety, display geometry,
dirty history, app model, pattern, groove, transform and WAV/Transfer tests.
Eight FX streams remain identical to their original baseline. All ten firmware
environments compile with the unchanged pinned platform. Historical M4/M4.1/M5/
M6/M6.1/M7/M8/M9 analyzer checks are retained. M8/M9 accept an explicit expected
Pattern size, still defaulting to 802 for their original captures; M10 passes
2082 and retains all their other assertions. Historical M4 intentionally reports
its recorded failed configurations; its unchanged analysis exits successfully.

CI adds lock tests, actual-M9 no-lock equivalence and the M10 capture analyzer.
Local checks pass; no hosted GitHub CI result is claimed.

## Genuine device stress

Final evidence: `GUITION_M10_STRESS_SERIAL.log`, analyzed successfully by
`tests/analyze_m10.py` and its nested M9/M8/M5 checks. The initial twelve seconds
run sixteen simultaneous synth tracks, all four locks at every position,
four ratchets, 240 BPM, straight swing and Delay. During that envelope rotate all,
reverse all, duplicate and copy track execute at audio command boundaries.
Later playback exercises swing, probability, pattern switching, all M9 tools,
mute/solo, manual audition and rapid lock edits/clears. Lock resolution telemetry
counts accepted parents, not individual ratchet hits. No per-hit Serial logging.

| Measurement | Result |
| --- | --- |
| Audio blocks / duration | 10,337 / 60.006 seconds |
| Full render + command p50 / p95 / p99 / max | 3,300 / 4,013 / 4,075 / 4,230 µs |
| Block budget / worst headroom | 5,804.989 µs / 27.13% |
| Proven all-lock dense blocks / worst block | 2,068 / 4,230 µs |
| Dense hit rate | 1,024 hits/second |
| Transform blocks / dense transform blocks / worst transform block | 151 / 125 / 4,230 µs |
| Deadline misses / write failures / timeouts / PCM rails | 0 / 0 / 0 / 0 |
| Active voices min / max | 16 / 16 |
| PCM peak / nonzero values | 6,702 / 5,290,836 |
| Parents / accepted / skipped / ratchet hits | 14,990 / 13,782 / 1,208 / 39,070 |
| Locked / unlocked accepted parents | 13,635 / 147 |
| Pitch / volume / pan / wave locked parents | 13,401 / 13,272 / 13,408 / 13,264 |
| Pending ratchet maximum / velocity range | 48 / 1–127 |
| Pattern switches / long→short / short→long / queue replacements | 8 / 2 / 2 / 4 |
| UI seconds / submitted / completed | 63.059 / 1,449 / 1,449 |
| Command rejects / touch errors / heap growth | 0 / 0 / 0 |
| Internal heap before / after | 341,176 / 341,176 bytes |
| PSRAM free before / after | 30,885,508 / 30,885,508 bytes |
| Largest PSRAM block before / after | 30,408,692 / 30,408,692 bytes |

Stress firmware SHA256:
`9c2c1b52abfb00eb76e52223413bc788da9fb0c254ebd4de98c26dd3712ea889`.
Normal firmware SHA256:
`725a9df74af2a13e16e5b8a27750e963bc4d3e88aa2ef9a2feb57faa99a35bdc`.

Normal `guition_app` was restored to COM13 with flash hash verification. Genuine
`GUITION_M10_NORMAL_SERIAL.log` captures a separate 10,337-block / 60.006-second
idle run. It reports zero parents, ratchets, active voices, nonzero PCM, rails,
deadline misses, write failures, timeouts and rejects. Render p50/p95/p99/max is
1,879/1,890/1,894/2,006 µs. Internal heap remains 341,208 bytes; PSRAM free/largest
remain 30,885,508/30,408,692 bytes. Display/touch/audio start successfully with the
expected no-card status. Normal base-control commands and source-specific values
remain covered by host tests; physical operation of those controls is not claimed.

Reproduce:

```text
pio run -e guition_app_stress -t upload --upload-port COM13
python tests/capture_guition.py --port COM13 --output docs/GUITION_M10_STRESS_SERIAL.log --seconds 95 --until "[M5 memory]"
python tests/analyze_m10.py docs/GUITION_M10_STRESS_SERIAL.log
pio run -e guition_app -t upload --upload-port COM13
python tests/capture_guition.py --port COM13 --output docs/GUITION_M10_NORMAL_SERIAL.log --seconds 95 --until "[M5 memory]"
python tests/analyze_m10.py docs/GUITION_M10_NORMAL_SERIAL.log --normal
```

## Limits and extension answer

Physical SD/sample playback qualification remains independent and pending.
Resident sample pitch/gain math is host-qualified; this no-card capture does not
prove physical sample-lock behavior. Human touch, picture and sound assessment
remain separate. The page-heavy run does not establish a strict 30 FPS guarantee.

**Yes:** M10 provides a safe first-generation parameter-lock architecture that
can later add bounded filter/FX/sample parameters without redesigning the
sequencer. The stable extension point is accepted-parent resolution and the
captured event; each future parameter still needs explicit storage, per-source
application, snapshot lifetime tests and realtime qualification. No generic
automation framework was added. Filter/FX/Delay/sample-selection/start/end locks,
microtiming, interpolation, ramps, persistence, undo, MIDI and song mode remain
out of scope. Work stops at M10.
